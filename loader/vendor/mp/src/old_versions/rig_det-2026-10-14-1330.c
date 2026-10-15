// Lockstep determinism layer (build 25846389, arm64). Notes: ~/ds_multiplayer/src/DETERMINISM_NOTES.md
//
// Locked mode = the rig owns the simulation clock. Each "sim cycle" is exactly one world tick:
//   [inject scheduled orders] -> TickRequested -> EventQueue.Update -> EventProcessor.Update
//   -> TickFlow.Update(Base) -> TickFlow.Update(Dungeon) -> ObserverDriven.Update
// (the same order as MainFlow.Update, minus input and view). The number of cycles per frame is set
// by the rig (run/step), never by frame time or game speed. MainFlow.Update still runs once per
// frame for input and view; the hooks below make its sim parts no-ops while locked.
// Inside a cycle: UnityEngine.Random runs on its own sim stream (view callers keep the view stream),
// Guid.NewGuid and new System.Random() are counter-based, A* paths are computed synchronously and
// queued graph updates are flushed. Each cycle appends one fingerprint line, and every RNG draw /
// Guid / System.Random / path start is written to a binary trace with GameAssembly return addresses.
#include "rig.h"
#include "mp_game.h"
#include "ord_api.h"
#include "trn.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>

/* ---------------------------------------------------------------- game addresses */
#define VA_TIME_UPDATE   0xD1BA98   /* TimeHandler.Update */
#define VA_EQ_UPDATE     0xCCF5E4   /* EventQueue.Update */
#define VA_EP_UPDATE     0xCEC070   /* EventProcessor.Update */
#define VA_TF_UPDATE     0xAADCF4   /* TickFlow.Update */
#define VA_OBS_UPDATE    0xBC0F04   /* ObserverDriven.Update */
#define VA_EQ_ADD        0xCCFAD4   /* EventQueue.AddEvent */
#define VA_TICKREQ_CTOR  0xD61E0C   /* TickRequested..ctor(bool logicalRefresh) */
#define TI_TICKREQ       0x6655AA0
#define VA_R_RANGEF      0x50E78B4  /* Random.Range(float,float) */
#define VA_R_RANGEI      0x50E7910  /* Random.Range(int,int) */
#define VA_R_RANGEINT    0x50E7974  /* Random.RandomRangeInt */
#define VA_R_VALUE       0x50E79D8  /* Random.get_value */
#define VA_R_INIT        0x50E7710  /* Random.InitState */
#define VA_R_GETST       0x50E77B0  /* Random.get_state_Injected */
#define VA_R_SETST       0x50E7864  /* Random.set_state_Injected */
#define VA_NEWGUID       0x425803C  /* System.Guid.NewGuid */
#define VA_SRAND_CTOR    0x4276180  /* System.Random..ctor() */
#define VA_SRAND_CTOR_I  0x42763E4  /* System.Random..ctor(int) */
#define VA_AP_STARTPATH  0xFE19C0   /* AstarPath.StartPath(Path, bool) */
#define VA_AP_BLOCK      0xFE138C   /* AstarPath.BlockUntilCalculated(Path) */
#define VA_AP_FLUSHGU    0xFDF880   /* AstarPath.FlushGraphUpdates() */
#define TI_ASTARPATH     0x664A0A8  /* static AstarPath.active @ static+0x18 */
#define TI_BEHAVIORMGR   0x664A570  /* static BehaviorManager.instance @ static+0x0; updateInterval @+0x20 */
#define TI_MOVETASK      0x6651C78
#define VA_MOVETASK_CTOR 0xB1BF60
#define TI_DIRECTORDER   0x664C9A0
#define VA_DIRECTORDER_CTOR 0xD64F1C
#define VA_TFH_ENQUEUE   0xD28034

/* ---------------------------------------------------------------- state */
static int locked, in_sim, det_on, hold, load_left;   /* load_left: the old campaign was left (load really started) */   /* hold: no game ticks between load_save and lock */
static RState sim_state, view_saved;
static uint32_t seed = 12345;
static uint64_t guid_sim_n, guid_out_n, sr_sim_n, sr_out_n, draws_sim, draws_view_in_sim;
static int64_t target = -1;            /* run until tick() == target */
static int per_frame = 1, jitter;
static uint64_t jit_state = 0x9E3779B97F4A7C15ULL;
static uint32_t seq;                   /* trace record counter within a tick */
static int64_t cycles, last_cycle_tick = -1, tick_errors, exc_recovered;
static FILE *fpf, *trf, *dtf;
static int64_t detail_from = -1, detail_to = -1;
static uint64_t frame_ticks[10];           /* histogram: frames that ran 0..8, 9+ world ticks (locked) */
static char fp_path[1200], tr_path[1200];

/* ---------------------------------------------------------------- originals */
static void (*o_time)(Obj, void *);
static void (*o_eq)(Obj, void *);
static void (*o_ep)(Obj, void *);
static void (*o_tf)(Obj, void *);
static void (*o_obs)(Obj, void *);
static float (*o_rangef)(float, float, void *);
static int (*o_rangei)(int, int, void *);
static int (*o_rri)(int, int, void *);
static float (*o_value)(void *);
static Guid (*o_newguid)(void *);
static void (*o_srand)(Obj, void *);
static void (*o_startpath)(Obj, bool, void *);

static inline void rs_get(RState *s) { F(VA_R_GETST, void (*)(RState *, void *))(s, NULL); }
static inline void rs_set(RState *s) { F(VA_R_SETST, void (*)(RState *, void *))(s, NULL); }

/* ---------------------------------------------------------------- caller classification */
typedef struct { uint32_t lo, hi; uint8_t k; } Range;
static Range *ranges; static int nranges;
static void load_ranges(void) {
    if (ranges) return;
    char p[1200]; snprintf(p, sizeof p, "%s/../../build/det_ranges.tsv", rig_dir);
    FILE *f = fopen(p, "r");
    if (!f) {                                         /* MP app: the file ships next to dsrig.dylib (Frameworks) */
        Dl_info di;
        if (dladdr((void *)load_ranges, &di) && di.dli_fname) {
            snprintf(p, sizeof p, "%s", di.dli_fname);
            char *sl = strrchr(p, '/'); if (sl) snprintf(sl + 1, sizeof p - (size_t)(sl + 1 - p), "det_ranges.tsv");
            f = fopen(p, "r");
        }
    }
    if (!f) { rig_log("det: %s missing: no view routing (run rig/tools/gen_ranges.py)", p); return; }
    int cap = 32768; ranges = malloc(sizeof(Range) * cap);
    unsigned lo, hi, k; char name[600];
    while (fscanf(f, "%x %x %u %599s", &lo, &hi, &k, name) == 4) {
        if (nranges == cap) { cap *= 2; ranges = realloc(ranges, sizeof(Range) * cap); }
        ranges[nranges++] = (Range){ lo, hi, (uint8_t)k };
    }
    fclose(f);
    rig_log("det: %d caller ranges loaded", nranges);
}
static int cls(uint32_t rva) {
    int a = 0, b = nranges - 1;
    while (a <= b) {
        int m = (a + b) / 2;
        if (rva < ranges[m].lo) b = m - 1; else if (rva >= ranges[m].hi) a = m + 1; else return ranges[m].k;
    }
    return 0;
}
static int is_view(const uint32_t c[3]) {
    for (int i = 0; i < 3; i++) { int k = cls(c[i]); if (k != 2) return k == 1; }
    return 0;
}
static inline uint32_t rva_of(uintptr_t a) { return (a >= ds_base && a < ds_base + 0x7000000) ? (uint32_t)(a - ds_base) : 0xFFFFFFFFu; }
/* return addresses of the hook's caller and two frames above (frame-pointer walk) */
#define CALLERS(c) do { \
    void **fp_ = (void **)__builtin_frame_address(0); \
    (c)[0] = rva_of((uintptr_t)__builtin_return_address(0)); (c)[1] = (c)[2] = 0; \
    void **g_ = (void **)fp_[0]; \
    uintptr_t lo_ = (uintptr_t)fp_, hi_ = (uintptr_t)pthread_get_stackaddr_np(pthread_self()); \
    if ((uintptr_t)g_ > lo_ && (uintptr_t)g_ < hi_) { (c)[1] = rva_of((uintptr_t)g_[1]); \
        void **h_ = (void **)g_[0]; if ((uintptr_t)h_ > (uintptr_t)g_ && (uintptr_t)h_ < hi_) (c)[2] = rva_of((uintptr_t)h_[1]); } \
} while (0)

/* ---------------------------------------------------------------- trace */
enum { K_RANGEF = 1, K_RANGEI, K_RANGEINT, K_VALUE, K_GUID, K_SRAND, K_PATH, K_ORDER, K_MOVETHING, K_SPAWNTHING, K_WORLDPOS, K_CENTEROCC, K_SETTARGET, K_ADDHOSTILE, K_REMOVEHOSTILE, K_SRAND_DRAW, K_TARGETDATA, K_DECIDE_TARGET, K_FIND_NEW, K_SPECIFIC, K_CLOSEST_ENEMY, K_DECIDE_SKILL, K_CTX_CONVERT, K_CTX_SETTARGET };
enum { FL_VIEW = 1, FL_OUT = 2 };
typedef struct { int64_t tick; uint16_t kind, flags; uint32_t seq; uint32_t c[3]; uint32_t val; } TRec;   /* 32 B */
static void trace(int kind, int flags, const uint32_t c[3], uint32_t val) {
    if (!trf) return;
    TRec r = { in_sim ? tick() : -1, (uint16_t)kind, (uint16_t)flags, seq++, { c[0], c[1], c[2] }, val };
    fwrite(&r, sizeof r, 1, trf);
}
static inline uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* ---------------------------------------------------------------- RNG hooks */
/* Routing rule (main thread, det on): a call goes to the SIM source when its caller is not view code
   and either a sim cycle is running or the game is still loading (det on, not locked yet: load-time
   gameplay draws such as RandomEventContainer.InitializeStartTick). Everything else uses the
   ordinary Unity stream / the view counters. */
static inline int sim_route(const uint32_t c[3]) { return (in_sim || !locked) && !is_view(c); }
#define RNG_HOOK_BODY(KIND, CALL, VALBITS) \
    if (!det_on || !pthread_main_np()) return CALL; \
    uint32_t c[3]; CALLERS(c); \
    int simr = sim_route(c); __typeof__(CALL) r; \
    if (simr == in_sim) { r = CALL; } \
    else if (simr) { RState t; rs_get(&t); rs_set(&sim_state); r = CALL; rs_get(&sim_state); rs_set(&t); } \
    else { RState t; rs_get(&t); rs_set(&view_saved); r = CALL; rs_get(&view_saved); rs_set(&t); } \
    if (in_sim) { if (simr) draws_sim++; else draws_view_in_sim++; } \
    trace(KIND, (simr ? 0 : FL_VIEW) | (in_sim ? 0 : FL_OUT), c, VALBITS(r)); \
    return r;
#define IBITS(r) ((uint32_t)(r))
static float h_rangef(float a, float b, void *mi) { RNG_HOOK_BODY(K_RANGEF, o_rangef(a, b, mi), fbits) }
static int h_rangei(int a, int b, void *mi) { RNG_HOOK_BODY(K_RANGEI, o_rangei(a, b, mi), IBITS) }
static int h_rri(int a, int b, void *mi) { RNG_HOOK_BODY(K_RANGEINT, o_rri(a, b, mi), IBITS) }
static float h_value(void *mi) { RNG_HOOK_BODY(K_VALUE, o_value(mi), fbits) }

static uint32_t mix32(uint32_t a, uint32_t b) {
    uint64_t z = ((uint64_t)a << 32 | b) + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL; z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return (uint32_t)(z ^ (z >> 31));
}

/* Guid.NewGuid: counter-based on the main thread once det is on. Sim and out-of-sim use separate
   counters and tags, so out-of-sim (UI/frame-dependent) Guids never shift the sim sequence. */
static Guid h_newguid(void *mi) {
    if (!det_on || !pthread_main_np()) return o_newguid(mi);
    uint32_t c[3]; CALLERS(c);
    Guid g; int sim = sim_route(c);                                                       /* FX/UI Guids: view counter */
    if (sim) { g.lo = ++guid_sim_n; g.hi = 0x4D49530000000000ULL | seed; }               /* "SIM" */
    else { g.lo = ++guid_out_n; g.hi = 0x54554F0000000000ULL | seed; }                   /* "OUT" */
    trace(K_GUID, (sim ? 0 : FL_VIEW) | (in_sim ? 0 : FL_OUT), c, (uint32_t)g.lo);
    return g;
}

/* new System.Random(): seed from (session seed, creation counter) instead of the clock */
static void h_srand(Obj self, void *mi) {
    if (!det_on || !pthread_main_np()) { o_srand(self, mi); return; }
    uint32_t c[3]; CALLERS(c);
    int sim = sim_route(c);
    uint32_t s = sim ? mix32(seed, (uint32_t)++sr_sim_n) : mix32(seed ^ 0xA5A5A5A5u, (uint32_t)++sr_out_n);
    s &= 0x7FFFFFFF;
    F(VA_SRAND_CTOR_I, void (*)(Obj, int, void *))(self, (int)s, NULL);
    trace(K_SRAND, (sim ? 0 : FL_VIEW) | (in_sim ? 0 : FL_OUT), c, s);
}

/* A*: inside a sim cycle every path is finished before StartPath returns (callbacks run now) */
static void h_startpath(Obj path, bool front, void *mi) {
    o_startpath(path, front, mi);
    if (locked && in_sim && pthread_main_np()) {
        uint32_t c[3]; CALLERS(c);
        F(VA_AP_BLOCK, void (*)(Obj, void *))(path, NULL);
        trace(K_PATH, 0, c, 0);
    }
}
static void astar_flush(void) {
    Obj k = md(TI_ASTARPATH);
    Obj sf = k ? AT(k, 0xB8, Obj) : NULL;
    Obj active = sf ? AT(sf, 0x18, Obj) : NULL;
    if (active) F(VA_AP_FLUSHGU, void (*)(Obj, void *))(active, NULL);
}

/* thing positions (projectiles etc.): trace who writes them, in and out of the sim cycle */
static void (*o_movething)(Obj, int, Obj, float, float, float, float, void *);
static void (*o_spawnthing)(Obj, int, Obj, float, float, void *);
static void h_movething(Obj self, int map, Obj ent, float lx, float ly, float cx, float cy, void *mi) {
    if (det_on && trf && pthread_main_np()) {
        uint32_t c[3]; CALLERS(c); c[2] = fbits(cy);
        trace(K_MOVETHING, in_sim ? 0 : FL_OUT, c, fbits(cx));
    }
    o_movething(self, map, ent, lx, ly, cx, cy, mi);
}
static void h_spawnthing(Obj self, int map, Obj ent, float x, float y, void *mi) {
    if (det_on && trf && pthread_main_np()) {
        uint32_t c[3]; CALLERS(c); c[2] = fbits(y);
        trace(K_SPAWNTHING, in_sim ? 0 : FL_OUT, c, fbits(x));
    }
    o_spawnthing(self, map, ent, x, y, mi);
}

/* position reads (only traced inside the detail window: they are hot) */
typedef struct { float x, y; } V2;
static V2 (*o_worldpos)(Obj, uint64_t, uint64_t, void *);
static V2 (*o_centerocc)(Obj, uint64_t, uint64_t, void *);
static inline int detail_now(void) { return in_sim && trf && tick() + 1 >= detail_from && tick() + 1 <= detail_to; }
static V2 h_worldpos(Obj self, uint64_t g0, uint64_t g1, void *mi) {
    V2 r = o_worldpos(self, g0, g1, mi);
    if (detail_now() && pthread_main_np()) { uint32_t c[3]; CALLERS(c); int vw = is_view(c); c[2] = (uint32_t)g0; trace(K_WORLDPOS, vw ? FL_VIEW : 0, c, fbits(r.x) ^ (fbits(r.y) * 31)); }
    return r;
}
static V2 h_centerocc(Obj self, uint64_t g0, uint64_t g1, void *mi) {
    V2 r = o_centerocc(self, g0, g1, mi);
    if (detail_now() && pthread_main_np()) { uint32_t c[3]; CALLERS(c); int vw = is_view(c); c[2] = (uint32_t)g0; trace(K_CENTEROCC, vw ? FL_VIEW : 0, c, fbits(r.x) ^ (fbits(r.y) * 31)); }
    return r;
}

/* CombatInfoComponent.SetCombatTarget: who picks a unit's target (val = target guid low bits, c[2] = owner) */
static void (*o_settarget)(Obj, Obj, void *);
static void h_settarget(Obj self, Obj target, void *mi) {
    if (det_on && trf && pthread_main_np()) {
        uint32_t c[3]; CALLERS(c);
        Obj owner = AT(self, 0x10, Obj);
        c[2] = (uint32_t)ent_guid(owner).lo;
        trace(K_SETTARGET, in_sim ? 0 : FL_OUT, c, (uint32_t)ent_guid(target).lo);
    }
    o_settarget(self, target, mi);
}

/* HostileEntityData.AddHostile / RemoveHostile: insertion order decides distance ties (val = entity, c[2] = owner) */
static void (*o_addhostile)(Obj, uint64_t, uint64_t, uint64_t, void *);
static void (*o_remhostile)(Obj, uint64_t, uint64_t, void *);
static void h_addhostile(Obj self, uint64_t g0, uint64_t g1, uint64_t pos, void *mi) {
    if (det_on && trf && pthread_main_np()) {
        uint32_t c[3]; CALLERS(c); c[2] = (uint32_t)AT(self, 0x10, uint64_t);
        trace(K_ADDHOSTILE, in_sim ? 0 : FL_OUT, c, (uint32_t)g0);
    }
    o_addhostile(self, g0, g1, pos, mi);
}
static void h_remhostile(Obj self, uint64_t g0, uint64_t g1, void *mi) {
    if (det_on && trf && pthread_main_np()) {
        uint32_t c[3]; CALLERS(c); c[2] = (uint32_t)AT(self, 0x10, uint64_t);
        trace(K_REMOVEHOSTILE, in_sim ? 0 : FL_OUT, c, (uint32_t)g0);
    }
    o_remhostile(self, g0, g1, mi);
}

/* System.Random draws (Next/Next(max)/Next(min,max)/Sample): catches shared static instances */
static int (*o_next0)(Obj, void *);
static int (*o_next1)(Obj, int, void *);
static int (*o_next2)(Obj, int, int, void *);
static double (*o_sample)(Obj, void *);
#define SR_TRACE(r) do { if (det_on && trf && pthread_main_np()) { uint32_t c[3]; CALLERS(c); \
        trace(K_SRAND_DRAW, (in_sim ? 0 : FL_OUT) | (is_view(c) ? FL_VIEW : 0), c, (uint32_t)(r)); } } while (0)
static int h_next0(Obj self, void *mi) { int r = o_next0(self, mi); SR_TRACE(r); return r; }
static int h_next1(Obj self, int m, void *mi) { int r = o_next1(self, m, mi); SR_TRACE(r); return r; }
static int h_next2(Obj self, int a, int b, void *mi) { int r = o_next2(self, a, b, mi); SR_TRACE(r); return r; }
static double h_sample(Obj self, void *mi) { double r = o_sample(self, mi); SR_TRACE(r * 4294967296.0); return r; }

/* HostileEntityData.GetClosestCombatableHostiles: dump the owner's sorted hostile list (detail window only) */
static Obj (*o_closest)(Obj, int, void *);
static int dict_guid_find(Obj d, const void *g, int stride, void *val, int vsize) {
    if (!d) return 0;
    Obj ent = AT(d, 0x18, Obj); int n = AT(d, 0x20, int32_t);
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * stride;
        if (*(int32_t *)e >= 0 && !memcmp(e + 8, g, 16)) { memcpy(val, e + 0x18, vsize); return 1; }
    }
    return 0;
}
static Obj h_closest(Obj self, int count, void *mi) {
    Obj r = o_closest(self, count, mi);
    if (dtf && in_sim && detail_now() && pthread_main_np()) {
        Guid owner; memcpy(&owner, (char *)self + 0x10, 16);
        char gs[40]; guid_str(owner, gs);
        fprintf(dtf, "%lld C owner=%s pos=%d,%d dirty=%d count=%d ->", (long long)tick(), gs, AT(self, 0x20, int32_t), AT(self, 0x24, int32_t),
                (int)AT(self, 0x60, bool), count);
        Obj lst = AT(self, 0x48, Obj);
        int n = lst ? AT(lst, 0x18, int32_t) : 0;
        Obj items = lst ? AT(lst, 0x10, Obj) : NULL;
        for (int i = 0; items && i < n && i < 12; i++) {
            const char *g = (char *)items + 0x20 + i * 16;
            int pos[2] = { 0, 0 }, ord = -1; float dist = -1;
            dict_guid_find(AT(self, 0x28, Obj), g, 0x20, pos, 8);
            dict_guid_find(AT(self, 0x30, Obj), g, 0x1C, &dist, 4);
            dict_guid_find(AT(self, 0x38, Obj), g, 0x1C, &ord, 4);
            fprintf(dtf, " %08x@%d,%d d=%.6g o=%d", *(uint32_t *)g, pos[0], pos[1], dist, ord);
        }
        Obj res = r; int rn = res ? AT(res, 0x18, int32_t) : 0;
        fprintf(dtf, " | result n=%d first=%08x\n", rn, rn ? *(uint32_t *)((char *)AT(res, 0x10, Obj) + 0x20) : 0);
    }
    return r;
}

/* CombatTargetApplyData..ctor(owner, target): where combat targets are decided (val = target, c[2] = owner) */
static void (*o_tgtdata)(Obj, Obj, Obj, void *);
static void h_tgtdata(Obj self, Obj owner, Obj target, void *mi) {
    if (det_on && trf && pthread_main_np()) {
        uint32_t c[3]; CALLERS(c); c[2] = (uint32_t)ent_guid(owner).lo;
        trace(K_TARGETDATA, in_sim ? 0 : FL_OUT, c, (uint32_t)ent_guid(target).lo);
    }
    o_tgtdata(self, owner, target, mi);
}

/* player combat decision (DecidePlayerCombatBehaviour): returned target / skill, to find where peers split */
static uint32_t str_hash(Obj s) { uint32_t h = 2166136261u; if (!s) return 0; int n = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14); for (int i = 0; i < n; i++) { h ^= c[i]; h *= 16777619u; } return h; }
#define DECIDE_HOOK(NAME, KIND) \
    static Obj (*o_##NAME)(Obj, Obj, void *); \
    static Obj h_##NAME(Obj a, Obj b, void *mi) { \
        Obj r = o_##NAME(a, b, mi); \
        if (det_on && trf && in_sim && pthread_main_np()) { uint32_t c[3]; CALLERS(c); c[2] = b ? str_hash(AT(b, 0x10, Obj)) : 0; trace(KIND, 0, c, (uint32_t)ent_guid(r).lo); } \
        return r; }
DECIDE_HOOK(decide_target, K_DECIDE_TARGET)
DECIDE_HOOK(find_new, K_FIND_NEW)
DECIDE_HOOK(specific, K_SPECIFIC)
static Obj (*o_closest_enemy)(Obj, Obj, void *);
static Obj h_closest_enemy(Obj user, Obj map, void *mi) {
    Obj r = o_closest_enemy(user, map, mi);
    if (det_on && trf && in_sim && pthread_main_np()) { uint32_t c[3]; CALLERS(c); c[2] = (uint32_t)ent_guid(user).lo; trace(K_CLOSEST_ENEMY, 0, c, (uint32_t)ent_guid(r).lo); }
    return r;
}
static Obj (*o_decide_skill)(Obj, void *);
static Obj h_decide_skill(Obj self, void *mi) {
    Obj r = o_decide_skill(self, mi);
    if (det_on && trf && in_sim && pthread_main_np()) { uint32_t c[3]; CALLERS(c); c[2] = (uint32_t)ent_guid(AT(self, 0x68, Obj)).lo; trace(K_DECIDE_SKILL, 0, c, r ? str_hash(AT(r, 0x10, Obj)) : 0); }
    return r;
}

/* ExecutionContext.Convert(publisher, user, target, ...) / SetTarget: every combat-behaviour target */
static Obj (*o_ctx_convert)(Obj, Obj, Obj, Obj, Obj, int, void *);
static Obj h_ctx_convert(Obj pub, Obj user, Obj target, Obj item, Obj deleg, int idx, void *mi) {
    if (det_on && trf && in_sim && pthread_main_np()) { uint32_t c[3]; CALLERS(c); c[2] = (uint32_t)ent_guid(user).lo; trace(K_CTX_CONVERT, 0, c, (uint32_t)ent_guid(target).lo); }
    return o_ctx_convert(pub, user, target, item, deleg, idx, mi);
}
static void (*o_ctx_settarget)(Obj, Obj, void *);
static void h_ctx_settarget(Obj self, Obj target, void *mi) {
    if (det_on && trf && in_sim && pthread_main_np()) { uint32_t c[3]; CALLERS(c); c[2] = (uint32_t)ent_guid(AT(self, 0x18, Obj)).lo; trace(K_CTX_SETTARGET, 0, c, (uint32_t)ent_guid(target).lo); }
    o_ctx_settarget(self, target, mi);
}

/* System.HashCode seeds itself with random bytes per process. EntityTargetSelector.InCloseOrder breaks
   distance ties with HashCode.Combine(Guid, Guid), so equidistant targets were picked differently on
   each peer. A fixed seed makes HashCode identical everywhere (only hash-table layout depends on it). */
static uint32_t (*o_hc_seed)(void *);
static uint32_t h_hc_seed(void *mi) { (void)mi; (void)o_hc_seed; return 0x9E3779B9u; }
static uint32_t hashcode_seed_fix(void) {        /* also force the static itself (the cctor may have run already) */
    Obj k = md(0x664E378);                       /* System.HashCode TypeInfo; static s_seed @ static+0x0 */
    Obj sf = k ? AT(k, 0xB8, Obj) : NULL;
    if (!sf) return 0;
    uint32_t old = AT(sf, 0x0, uint32_t);
    AT(sf, 0x0, uint32_t) = 0x9E3779B9u;
    return old;
}

/* ---------------------------------------------------------------- frame-loop hooks */
static void h_time(Obj th, void *mi) { if (locked || hold) return; o_time(th, mi); }
/* While locked, EventQueue.Update still runs once per frame OUTSIDE the sim: by then the queue holds only
   local input events (selection, camera, right-click...), because every sim cycle and every order scope ends
   with eq_drain(). Their state changes are captured by ord and come back as orders; an exception in a local
   handler no longer cuts a sim cycle short on one peer. */
static void h_eq(Obj o, void *mi) { o_eq(o, mi); }
static void eq_drain(void) { Obj q = mf ? AT(mf, 0x48, Obj) : NULL; if (q) F(0xCCF7C4, void (*)(Obj, void *))(q, NULL); }   /* ProcessSyncEvent */
static void h_ep(Obj o, void *mi) { if (locked && !in_sim) return; o_ep(o, mi); }
static void h_tf(Obj o, void *mi) { if (locked && !in_sim) return; o_tf(o, mi); }
static void h_obs(Obj o, void *mi) { if (locked && !in_sim) return; o_obs(o, mi); }

void rig_det_install(void) {
    int bad = 0;
    bad |= rig_hook(0x425B62C, (void *)h_hc_seed, (void **)&o_hc_seed, "System.HashCode.GenerateGlobalSeed");
    bad |= rig_hook(VA_TIME_UPDATE, (void *)h_time, (void **)&o_time, "TimeHandler.Update");
    bad |= rig_hook(VA_EQ_UPDATE, (void *)h_eq, (void **)&o_eq, "EventQueue.Update");
    bad |= rig_hook(VA_EP_UPDATE, (void *)h_ep, (void **)&o_ep, "EventProcessor.Update");
    bad |= rig_hook(VA_TF_UPDATE, (void *)h_tf, (void **)&o_tf, "TickFlow.Update");
    bad |= rig_hook(VA_OBS_UPDATE, (void *)h_obs, (void **)&o_obs, "ObserverDriven.Update");
    bad |= rig_hook(VA_R_RANGEF, (void *)h_rangef, (void **)&o_rangef, "Random.Range(float)");
    bad |= rig_hook(VA_R_RANGEI, (void *)h_rangei, (void **)&o_rangei, "Random.Range(int)");
    bad |= rig_hook(VA_R_RANGEINT, (void *)h_rri, (void **)&o_rri, "Random.RandomRangeInt");
    bad |= rig_hook(VA_R_VALUE, (void *)h_value, (void **)&o_value, "Random.value");
    bad |= rig_hook(VA_NEWGUID, (void *)h_newguid, (void **)&o_newguid, "Guid.NewGuid");
    bad |= rig_hook(VA_SRAND_CTOR, (void *)h_srand, (void **)&o_srand, "System.Random..ctor()");
    bad |= rig_hook(VA_AP_STARTPATH, (void *)h_startpath, (void **)&o_startpath, "AstarPath.StartPath");
    if (!getenv("DSRIG_DIAG")) { if (bad) rig_log("det: some hooks failed; locked mode is NOT safe"); return; }
    /* desync-hunting hooks (DSRIG_DIAG=1 only: they cost 21 of the 80 hook stubs) */
    bad |= rig_hook(0xC83BC4, (void *)h_movething, (void **)&o_movething, "EntityDataContainer.MoveThing");
    bad |= rig_hook(0xC81E10, (void *)h_spawnthing, (void **)&o_spawnthing, "EntityDataContainer.OnSpawnThing");
    bad |= rig_hook(0xFC4DA0, (void *)h_worldpos, (void **)&o_worldpos, "InMapDataInterface.GetWorldPosition");
    bad |= rig_hook(0xFC9A18, (void *)h_centerocc, (void **)&o_centerocc, "InMapDataInterface.GetCenterOfWorldOccupies");
    bad |= rig_hook(0xDD7990, (void *)h_settarget, (void **)&o_settarget, "CombatInfoComponent.SetCombatTarget");
    bad |= rig_hook(0xBF3B88, (void *)h_addhostile, (void **)&o_addhostile, "HostileEntityData.AddHostile");
    bad |= rig_hook(0xBF3C10, (void *)h_remhostile, (void **)&o_remhostile, "HostileEntityData.RemoveHostile");
    bad |= rig_hook(0x42767D0, (void *)h_next0, (void **)&o_next0, "System.Random.Next()");
    bad |= rig_hook(0x4276AA0, (void *)h_next1, (void **)&o_next1, "System.Random.Next(max)");
    bad |= rig_hook(0x427696C, (void *)h_next2, (void **)&o_next2, "System.Random.Next(min,max)");
    bad |= rig_hook(0x4276678, (void *)h_sample, (void **)&o_sample, "System.Random.Sample");
    bad |= rig_hook(0xBF4A90, (void *)h_closest, (void **)&o_closest, "HostileEntityData.GetClosestCombatableHostiles");
    bad |= rig_hook(0x847EF0, (void *)h_tgtdata, (void **)&o_tgtdata, "CombatTargetApplyData..ctor");
    bad |= rig_hook(0xB251E4, (void *)h_decide_target, (void **)&o_decide_target, "DecidePlayerCombatBehaviour.DecideTarget");
    bad |= rig_hook(0xB2616C, (void *)h_find_new, (void **)&o_find_new, "DecidePlayerCombatBehaviour.FindNewTarget");
    bad |= rig_hook(0xB260A8, (void *)h_specific, (void **)&o_specific, "DecidePlayerCombatBehaviour.GetSpecificTarget");
    bad |= rig_hook(0x8AF164, (void *)h_closest_enemy, (void **)&o_closest_enemy, "CombatUtility.GetClosestSingleEnemy");
    bad |= rig_hook(0xB24DE8, (void *)h_decide_skill, (void **)&o_decide_skill, "DecidePlayerCombatBehaviour.DecideSkill");
    bad |= rig_hook(0x8A7B9C, (void *)h_ctx_convert, (void **)&o_ctx_convert, "ExecutionContext.Convert(entity)");
    bad |= rig_hook(0x8A7D6C, (void *)h_ctx_settarget, (void **)&o_ctx_settarget, "ExecutionContext.SetTarget");
    if (bad) rig_log("det: some hooks failed; locked mode is NOT safe");
}

/* ---------------------------------------------------------------- scheduled orders */
typedef struct { int64_t tick; int kind; Guid g; int x, y; int done; } Order;
static Order orders[4096]; static int norders;

/* MoveTask + DirectOrderRequested into the current map's TickFlowHandler (drained by the next TickFlow.Tick) */
int rig_order_move(Guid g, int x, int y) {
    int map = cur_map();
    Obj u = find_unit(map, g);
    Obj tfh = (u && mf) ? dict_int_get(AT(mf, 0x140, Obj), map) : NULL;
    if (!tfh) return -1;
    uint64_t cell = (uint32_t)x | ((uint64_t)(uint32_t)y << 32);
    Obj task = obj_new(TI_MOVETASK);
    F(VA_MOVETASK_CTOR, void (*)(Obj, uint64_t, int, int, void *))(task, cell, 0, 0, NULL);
    Obj ord = obj_new(TI_DIRECTORDER);
    F(VA_DIRECTORDER_CTOR, void (*)(Obj, Obj, Obj, void *))(ord, u, task, NULL);
    F(VA_TFH_ENQUEUE, void (*)(Obj, Obj, bool, void *))(tfh, ord, false, NULL);
    return 0;
}

static void inject_orders(int64_t t) {
    for (int i = 0; i < norders; i++) {
        Order *o = &orders[i];
        if (o->done || o->tick != t) continue;
        o->done = 1;
        uint32_t c[3] = { 0, 0, 0 };
        if (rig_order_move(o->g, o->x, o->y)) { rig_log("det: order %d at tick %lld: unit not found", i, (long long)t); trace(K_ORDER, 0, c, 0xDEAD); continue; }
        trace(K_ORDER, 0, c, (uint32_t)i);
    }
}

/* ---------------------------------------------------------------- one sim cycle = one world tick */
static int runnable(void) {
    Obj c = cdc();
    return mf && AT(mf, 0x188, bool) && scene() == 2 && c && AT(c, 0x10, int32_t) != -1 && !busy();
}

/* every entity position and every unit's hp/energy/task, for one tick (desync hunting) */
static void write_detail(int64_t t) {
    if (!dtf) { char p[1200]; snprintf(p, sizeof p, "%s/detail.log", rig_dir); dtf = fopen(p, "w"); if (!dtf) return; }
    for (int map = 0; map < 2; map++) {
        Obj pd = pos_dict(map), ud = units_dict(map);
        if (pd) {
            Obj ent = AT(pd, 0x18, Obj); int n = AT(pd, 0x20, int32_t);
            for (int i = 0; ent && i < n; i++) {
                char *e = (char *)ent + 0x20 + i * 0x20;
                if (*(int32_t *)e < 0) continue;
                char gs[40]; guid_str(*(Guid *)(e + 8), gs);
                fprintf(dtf, "%lld P %d %d %s %.9g %.9g\n", (long long)t, map, i, gs, *(float *)(e + 0x18), *(float *)(e + 0x1C));
            }
        }
        if (ud) {
            Obj ent = AT(ud, 0x18, Obj); int n = AT(ud, 0x20, int32_t);
            for (int i = 0; ent && i < n; i++) {
                char *e = (char *)ent + 0x20 + i * 0x20;
                if (*(int32_t *)e < 0) continue;
                Obj u = *(Obj *)(e + 0x18); if (!u) continue;
                char gs[40], g1[40], g2[40], g3[40]; guid_str(*(Guid *)(e + 8), gs);
                int cst; Guid ct, cf, tt; unit_combat(u, &cst, &ct, &cf, &tt);
                guid_str(ct, g1); guid_str(cf, g2); guid_str(tt, g3);
                fprintf(dtf, "%lld U %d %d %s hp=%.9g en=%.9g task=%d dead=%d combat=%d target=%s force=%s task_target=%s\n", (long long)t, map, i, gs,
                        unit_stat(u, 9), unit_stat(u, 15), unit_task(u), (int)unit_dead(u), cst, g1, g2, g3);
            }
        }
    }
    fflush(dtf);
}

static void sim_cycle(void) {
    int64_t t0 = tick();
    seq = 0;
    rs_get(&view_saved); rs_set(&sim_state);
    in_sim = 1;
    astar_flush();
    inject_orders(t0 + 1);
    Obj th = AT(mf, 0x40, Obj);
    if (th) AT(th, 0x38, int64_t) += 1;                               /* TimeHandler._currentTick */
    Obj ev = obj_new(TI_TICKREQ);
    F(VA_TICKREQ_CTOR, void (*)(Obj, bool, void *))(ev, false, NULL);
    F(VA_EQ_ADD, void (*)(Obj, Obj, void *))(AT(mf, 0x48, Obj), ev, NULL);
    o_eq(AT(mf, 0x48, Obj), NULL);
    o_ep(AT(mf, 0x58, Obj), NULL);
    Obj flows = AT(mf, 0x138, Obj);
    Obj f0 = dict_int_get(flows, 0), f1 = dict_int_get(flows, 1);
    if (f0) o_tf(f0, NULL);
    if (f1) o_tf(f1, NULL);
    Obj c = cdc();
    if (scene() == 2 && c && AT(c, 0x10, int32_t) != -1 && AT(mf, 0x60, Obj)) o_obs(AT(mf, 0x60, Obj), NULL);
    eq_drain();                         /* events this tick produced: handled now, inside the sim, not at frame time */
    astar_flush();
    in_sim = 0;
    rs_get(&sim_state); rs_set(&view_saved);
    cycles++;
    int64_t t1 = tick();
    if (t1 != t0 + 1) { tick_errors++; rig_log("det: cycle moved the tick %lld -> %lld", (long long)t0, (long long)t1); }
    last_cycle_tick = t1;
    if (t1 >= detail_from && t1 <= detail_to) write_detail(t1);
    if (fpf) {
        uint64_t h[4]; int nu, np;
        rig_world_hash(sim_state, h, &nu, &np);
        fprintf(fpf, "%lld %016llx %016llx %016llx %016llx %d %d %llu %llu\n", (long long)t1,
                (unsigned long long)h[0], (unsigned long long)h[1], (unsigned long long)h[2], (unsigned long long)h[3],
                nu, np, (unsigned long long)guid_sim_n, (unsigned long long)sr_sim_n);
    }
}

int rig_det_frame(void *mainflow) {
    (void)mainflow;
    if (in_sim) {                       /* an exception left a cycle half done: restore the view stream */
        in_sim = 0; exc_recovered++;
        rs_get(&sim_state); rs_set(&view_saved);
        rig_log("det: sim cycle did not return (exception?) before tick %lld", (long long)tick());
    }
    if (det_on) hashcode_seed_fix();      /* the HashCode cctor may run at any frame and re-randomise it */
    if (hold && !runnable()) load_left = 1;
    if (hold && !locked && load_left && runnable()) {   /* load finished: lock before the first tick */
        char *r = rig_det_exec("lock", "{}"); rig_log("det: auto-lock after load: %s", r); free(r);
        if (getenv("DSRIG_DETAIL_SYNC")) rig_det_write_detail("lock");
    }
    if (locked && scene() != 2) { locked = 0; rig_log("det: left the campaign: unlocked"); }
    /* What TimeHandler.Update would do each frame (it is skipped while locked): _lastTick = _currentTick, so
       GetTickAmount() = ticks of THIS frame; _absoluteTime += deltaTime while time runs. Without this the
       view saw GetTickAmount grow without end (all ticks since the lock) and the copy slowed to ~2-3 fps. */
    Obj th = (locked && mf) ? AT(mf, 0x40, Obj) : NULL;
    int64_t tick_before = th ? AT(th, 0x38, int64_t) : 0;
    if (th) AT(th, 0x30, int64_t) = tick_before;
    if (mp_game_active() && mf && AT(mf, 0x188, bool) && (scene() == 1 || scene() == 2)) {
        /* multiplayer: dsnet paces the steps. Title screen too (join, save transfer); not during boot,
           when a host save could not be loaded yet. */
        mp_game_frame();
        if (th && AT(th, 0x38, int64_t) != tick_before) AT(th, 0x40, float) += F(0x5104258, float (*)(void *))(NULL);   /* Time.deltaTime */
        if (th && locked) {                                   /* world ticks per frame (smoothness of the view) */
            int64_t d = AT(th, 0x38, int64_t) - tick_before; frame_ticks[d < 0 ? 0 : d > 9 ? 9 : d]++;
        }
        if (fpf) fflush(fpf);
        if (trf) fflush(trf);
        return locked;
    }
    if (!locked) return 0;
    if (!runnable()) return 1;
    int n = per_frame;
    if (jitter) { jit_state ^= jit_state << 13; jit_state ^= jit_state >> 7; jit_state ^= jit_state << 17; n = (int)(jit_state % (uint64_t)(per_frame + 1)); }
    for (int i = 0; i < n && target >= 0 && tick() < target; i++) sim_cycle();
    if (th && AT(th, 0x38, int64_t) != tick_before) AT(th, 0x40, float) += F(0x5104258, float (*)(void *))(NULL);
    if (fpf) fflush(fpf);
    if (trf) fflush(trf);
    return 1;
}

int rig_det_locked(void) { return locked; }

/* A multiplayer session starts (host or join, any menu / Steam path). In live play the campaign is loaded by the
   stock flows (new campaign, load list) or is already running, never by the rig's load command, so det must be
   switched on here: hold the game clock, seed the sim streams, and auto-lock as soon as a campaign is runnable
   (at once if one already runs; the host's join-point save + reload then gives every peer the same start). */
void rig_det_arm(void) {
    if (locked || hold) return;                       /* already in det (rig tests: load_save det:1) */
    rig_det_on_load();
    if (runnable()) load_left = 1;                    /* campaign already running: lock on the next frame */
    rig_log("det: armed by the multiplayer session (%s)", load_left ? "campaign running: locking" : "waiting for a campaign");
}

/* Long-lived System.Random objects keep a state that is NOT in the save (WeatherSystem._random decides the next
   base/dungeon weather change minute; EnvironmentAffectHelper._rnd; ItemDropSelector's static Random). A peer
   that loaded later, or a host whose objects outlived a reload, would draw a different sequence: the orders lane
   saw EnvironmentSaveData.BaseWeatherChangeMin differ (49119 vs 49259) and a desync after ~9 min. At every lock
   they are re-seeded from (session seed, campaign tick), the same on every peer. */
static Obj persistent_random(int i) {
    if (!mf) return NULL;
    if (i < 2) { Obj tf = dict_int_get(AT(mf, 0x138, Obj), i); Obj ws = tf ? AT(tf, 0x30, Obj) : NULL; return ws ? AT(ws, 0x68, Obj) : NULL; }
    if (i == 2) { Obj hs = AT(mf, 0xE8, Obj); Obj eh = hs ? AT(hs, 0x48, Obj) : NULL; return eh ? AT(eh, 0x60, Obj) : NULL; }
    Obj k = md(0x66506C8); Obj sf = k ? AT(k, 0xB8, Obj) : NULL; return sf ? AT(sf, 0x0, Obj) : NULL;   /* ItemDropSelector.Random */
}
static void reseed_persistent_randoms(void) {
    int64_t t = tick();
    for (int i = 0; i < 4; i++) {
        Obj r = persistent_random(i);
        if (!r) continue;
        uint32_t sd = mix32(seed ^ (0x5EED0000u + (uint32_t)i), (uint32_t)t) & 0x7FFFFFFF;
        F(VA_SRAND_CTOR_I, void (*)(Obj, int, void *))(r, (int)sd, NULL);
    }
}
/* weather timers + the state of those Random objects, for the fingerprint */
uint64_t rig_det_extra_hash(uint64_t h) {
    for (int i = 0; i < 2; i++) {
        Obj tf = mf ? dict_int_get(AT(mf, 0x138, Obj), i) : NULL; Obj ws = tf ? AT(tf, 0x30, Obj) : NULL;
        if (ws) { h = fnv(h, (char *)ws + 0x40, 16); }        /* _dungeonWeatherChangeMin, _baseWeatherChangeMin */
    }
    for (int i = 0; i < 4; i++) {
        Obj r = persistent_random(i);
        if (!r) continue;
        h = fnv(h, (char *)r + 0x10, 8);                         /* _inext, _inextp */
        Obj a = AT(r, 0x18, Obj);
        if (a) h = fnv(h, (char *)a + 0x20, 56 * 4);            /* _seedArray[56] */
    }
    return h;
}

/* ---------------------------------------------------------------- mp_game.h API */
int rig_det_mp_ready(void) { return locked && !hold && runnable() && !trn_flow_busy(); }   /* trn: hold mid-step during a flow */
void rig_det_sim_tick(void) { sim_cycle(); }
int rig_det_in_sim(void) { return in_sim; }
static int scope_depth;
void rig_det_sim_scope(int enter) {
    if (enter) {
        if (scope_depth++ || in_sim) return;           /* already in a cycle: nothing to switch */
        rs_get(&view_saved); rs_set(&sim_state); in_sim = 1; seq = 0;
    } else {
        if (--scope_depth || !in_sim) { if (scope_depth < 0) scope_depth = 0; return; }
        eq_drain();                                    /* sub-events of a replayed order: inside the scope */
        in_sim = 0; rs_get(&sim_state); rs_set(&view_saved);
    }
}
typedef struct { RState rs; uint32_t seed; uint64_t guid_sim_n, sr_sim_n; } DetState;
static DetState pending; static int pending_set;
size_t rig_det_state_size(void) { return sizeof(DetState); }
void rig_det_state_get(void *out) { DetState d = { sim_state, seed, guid_sim_n, sr_sim_n }; memcpy(out, &d, sizeof d); }
static void state_apply(const DetState *d) { sim_state = d->rs; seed = d->seed; guid_sim_n = d->guid_sim_n; sr_sim_n = d->sr_sim_n; }
void rig_det_state_set(const void *in) {             /* always followed by a load: applied at its auto-lock */
    memcpy(&pending, in, sizeof pending); pending_set = 1;
}
void rig_det_write_detail(const char *why) {         /* per-entity dump of the current state (save / lock) */
    if (!dtf) { char p[1200]; snprintf(p, sizeof p, "%s/detail.log", rig_dir); dtf = fopen(p, "w"); if (!dtf) return; }
    fprintf(dtf, "# %s at tick %lld\n", why, (long long)tick());
    write_detail(tick());
}
RState rig_det_sim_state(void) { return sim_state; }

static void open_logs(void) {
    if (!fpf) { snprintf(fp_path, sizeof fp_path, "%s/fp.log", rig_dir); fpf = fopen(fp_path, "w"); }
    if (!trf && getenv("DSRIG_LIVE") && !getenv("DSRIG_TRACE")) return;   /* live play: no trace file (it grows ~MBs/min) */
    if (!trf) { snprintf(tr_path, sizeof tr_path, "%s/trace.bin", rig_dir); trf = fopen(tr_path, "wb"); if (trf) setvbuf(trf, NULL, _IOFBF, 1 << 20); }
}

void rig_det_on_load(void) {
    det_on = 1; hold = 1; locked = 0; load_left = 0;
    guid_out_n = sr_out_n = 0;
    load_ranges();
    if (trf) { fclose(trf); trf = NULL; }
    if (fpf) { fclose(fpf); fpf = NULL; }
    open_logs();
    RState cur; rs_get(&cur);                                       /* sim stream from the session seed; */
    F(VA_R_INIT, void (*)(int, void *))((int)seed, NULL);           /* load-time gameplay draws use it too */
    rs_get(&sim_state); rs_set(&cur);
    guid_sim_n = sr_sim_n = 0;
    uint32_t hs = hashcode_seed_fix();
    rig_log("det: on (load), seed %u, HashCode.s_seed was %08x", seed, hs);
}

/* ---------------------------------------------------------------- commands */
static char *ok_json(Buf *b) { bput(b, "}"); return b->p; }
static char *derr(const char *m) { Buf b = {0}; bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, m); bput(&b, "}"); return b.p; }

void rig_det_status(Buf *b) {
    bput(b, ",\"frame_ticks\":[");
    for (int i = 0; i < 10; i++) bput(b, "%s%llu", i ? "," : "", (unsigned long long)frame_ticks[i]);
    bput(b, "]");
    bput(b, ",\"det\":{\"on\":%s,\"hold\":%s,\"locked\":%s,\"target\":%lld,\"per_frame\":%d,\"jitter\":%d,\"cycles\":%lld,\"tick_errors\":%lld,"
            "\"exc_recovered\":%lld,\"guid_sim\":%llu,\"guid_out\":%llu,\"srand_sim\":%llu,\"srand_out\":%llu,\"draws_sim\":%llu,\"draws_view_in_sim\":%llu,"
            "\"orders\":%d,\"seed\":%u}",
         det_on ? "true" : "false", hold ? "true" : "false", locked ? "true" : "false", (long long)target, per_frame, jitter, (long long)cycles, (long long)tick_errors,
         (long long)exc_recovered, (unsigned long long)guid_sim_n, (unsigned long long)guid_out_n, (unsigned long long)sr_sim_n,
         (unsigned long long)sr_out_n, (unsigned long long)draws_sim, (unsigned long long)draws_view_in_sim, norders, seed);
}

char *rig_det_exec(const char *cmd, const char *line) {
    double v;
    if (!strcmp(cmd, "det_seed")) {                       /* before load_save: session seed */
        if (!jnum(line, "seed", &v)) return derr("need seed");
        seed = (uint32_t)v; Buf b = {0}; bput(&b, "{\"ok\":true,\"seed\":%u", seed); return ok_json(&b);
    }
    if (!strcmp(cmd, "lock")) {
        if (!runnable()) return derr("not in a loaded campaign (or busy)");
        if (jnum(line, "seed", &v)) seed = (uint32_t)v;
        if (!det_on || jnum(line, "seed", &v)) {                    /* manual lock without a det load: seed now */
            det_on = 1; load_ranges(); open_logs();
            RState cur; rs_get(&cur);
            F(VA_R_INIT, void (*)(int, void *))((int)seed, NULL);
            rs_get(&sim_state); rs_set(&cur);
            guid_sim_n = sr_sim_n = 0;
        }
        if (pending_set) { state_apply(&pending); pending_set = 0; rig_log("det: lock uses the host's det state (seed %u, guids %llu)", seed, (unsigned long long)guid_sim_n); }
        reseed_persistent_randoms();
        draws_sim = draws_view_in_sim = 0; cycles = tick_errors = 0;
        norders = 0; target = tick(); per_frame = 1; jitter = 0;
        locked = 1; hold = 0;
        uint32_t hs = hashcode_seed_fix();
        Obj ak = md(TI_ASTARPATH); Obj asf = ak ? AT(ak, 0xB8, Obj) : NULL; Obj act = asf ? AT(asf, 0x18, Obj) : NULL;
        Obj bk = md(TI_BEHAVIORMGR); Obj bsf = bk ? AT(bk, 0xB8, Obj) : NULL; Obj bm = bsf ? AT(bsf, 0x0, Obj) : NULL;
        Obj th = AT(mf, 0x40, Obj);
        Buf b = {0};
        bput(&b, "{\"ok\":true,\"locked\":true,\"hashcode_seed_was\":\"%08x\",\"tick\":%lld,\"seed\":%u,\"astar_threads\":%d,\"bt_update_interval\":%d,\"time_remain\":%.4f",
             hs, (long long)tick(), seed, act ? AT(act, 0x70, int32_t) : -99, bm ? AT(bm, 0x20, int32_t) : -99, th ? AT(th, 0x44, float) : -1.0f);
        rig_log("det: locked at tick %lld seed %u", (long long)tick(), seed);
        return ok_json(&b);
    }
    if (!strcmp(cmd, "unlock")) {
        locked = 0; hold = 0; target = -1;
        Obj th = AT(mf, 0x40, Obj); if (th) AT(th, 0x44, float) = 0.0f;   /* no burst of saved-up time */
        return strdup("{\"ok\":true,\"locked\":false}");
    }
    if (!strcmp(cmd, "run") || !strcmp(cmd, "step") || !strcmp(cmd, "run_to")) {
        if (!locked) return derr("not locked");
        if (!strcmp(cmd, "step")) { double n = 1; jnum(line, "n", &n); target = tick() + (int64_t)n; }
        else if (jnum(line, "to", &v) || jnum(line, "tick", &v)) target = (int64_t)v;
        if (jnum(line, "per_frame", &v)) per_frame = v < 0 ? 0 : (int)v;
        if (jnum(line, "jitter", &v)) jitter = (int)v;
        if (jnum(line, "jitter_seed", &v)) jit_state = 0x9E3779B97F4A7C15ULL ^ (uint64_t)v;
        if (!strcmp(cmd, "step") && !jnum(line, "per_frame", &v)) per_frame = 1 << 30;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"tick\":%lld,\"target\":%lld,\"per_frame\":%d,\"jitter\":%d", (long long)tick(), (long long)target, per_frame, jitter);
        return ok_json(&b);
    }
    if (!strcmp(cmd, "det_weather")) {                    /* weather timers (WeatherSystem per map) */
        Buf b = {0}; bput(&b, "{\"ok\":true,\"tick\":%lld", (long long)tick());
        for (int i = 0; i < 2; i++) {
            Obj tf = mf ? dict_int_get(AT(mf, 0x138, Obj), i) : NULL; Obj ws = tf ? AT(tf, 0x30, Obj) : NULL;
            if (ws) bput(&b, ",\"map%d\":{\"dungeon_change_min\":%lld,\"base_change_min\":%lld}", i, (long long)AT(ws, 0x40, int64_t), (long long)AT(ws, 0x48, int64_t));
        }
        return ok_json(&b);
    }
    if (!strcmp(cmd, "det_frame_reset")) { memset(frame_ticks, 0, sizeof frame_ticks); return strdup("{\"ok\":true}"); }
    if (!strcmp(cmd, "det_corrupt")) {                    /* test: break this peer's sim RNG state (forced desync) */
        sim_state.s[0] ^= 0x5A5A5A5Au;
        return strdup("{\"ok\":true,\"corrupted\":\"sim rng\"}");
    }
    if (!strcmp(cmd, "detail")) {                         /* detail {from, to}: per-entity dump for these ticks */
        double a, b2; if (!jnum(line, "from", &a) || !jnum(line, "to", &b2)) return derr("need from, to");
        detail_from = (int64_t)a; detail_to = (int64_t)b2;
        if (!dtf) { char p[1200]; snprintf(p, sizeof p, "%s/detail.log", rig_dir); dtf = fopen(p, "w"); }
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "schedule")) {
        char gs[64], kind[32] = "move"; Guid g; double t, x, y;
        jstr(line, "kind", kind, sizeof kind);
        if (strcmp(kind, "move")) return derr("only kind move");
        if (!jnum(line, "tick", &t) || !jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g) || !jnum(line, "x", &x) || !jnum(line, "y", &y))
            return derr("need tick, guid, x, y");
        if (locked && (int64_t)t <= tick()) return derr("tick already passed");
        if (norders >= (int)(sizeof orders / sizeof *orders)) return derr("order table full");
        orders[norders] = (Order){ (int64_t)t, 0, g, (int)x, (int)y, 0 };
        Buf b = {0}; bput(&b, "{\"ok\":true,\"index\":%d,\"tick\":%lld", norders++, (long long)t);
        return ok_json(&b);
    }
    return NULL;
}
