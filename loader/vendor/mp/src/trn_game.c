// trn_game.c - transitions lane, game side (build 25846389, arm64).
//  1. Floor generation made peer-identical: the dungeon seed comes from saved state instead of the clock-seeded
//     DSDungeonPreDataGenerator._runtimeSeedGenerator, and Edgar's wall-clock time limit becomes an iteration cap.
//  2. Flow barrier helpers: trn_flow_busy(), sim scope around the flow coroutines (rig_det_sim_scope if present).
//  3. Host-only autosave in multiplayer.
//  4. Map-change vote orders (trn_order_*), applied from the ordered command stream.
// Design and RVA sources: ~/ds_multiplayer/research/transitions_design.md
#include "rig.h"
#include "trn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <dlfcn.h>

/* ---------------------------------------------------------------- addresses */
#define VA_GEN_GETRNG      0xC5ABDC   /* DSDungeonPreDataGenerator.GetRandomNumbersGenerator(bool useRandomSeed, int seed) */
#define VA_GEN_SETRESULT   0xC5B1E4   /* DSDungeonPreDataGenerator.SetGenerationResult(...) */
#define VA_EDGAR_GEN_CTOR  0x1E71D84  /* GraphBasedGeneratorGrid2D<object>..ctor(levelDescription, configuration) */
#define VA_EDGAR_EARLYSTOP 0x1D0F1F0  /* <GetEarlyStoppingHandler>b__0 (per iteration, worker thread) */
#define VA_EQ_ADD          0xCCFAD4   /* EventQueue.AddEvent */
#define VA_GODOWN_CTOR     0xD622AC   /* GoDownDungeonRequested..ctor(List<Guid>) */
#define TI_GODOWN          0x664DF58  /* GoDownDungeonRequested_TypeInfo */
#define VA_CHANGEMAP_CTOR  0xD621E8   /* ChangeMapRequested..ctor(from,to,region,list,floor,skipSafe,hasSeed,seed,clrFloor,decDepl,clrRegion,vgm) */
#define TI_CHANGEMAP       0x664B1D0  /* ChangeMapRequested_TypeInfo */
#define TI_LIST_GUID       0x66457D8  /* List<Guid>_TypeInfo */
#define TI_GUID            0x664E200  /* System.Guid_TypeInfo */
#define VA_IL_ARRAY_NEW    0x6F8BA8
#define VA_IL_GCHANDLE_NEW 0x6F9540
#define VA_IL_GCHANDLE_FREE 0x6F95E4
#define VA_IL_WBARRIER     0x6F95C8
#define VA_IL_CLASS_NAME   0x6F8C20
/* autosave */
#define VA_SAVE_REQUEST    0xFCA3C0   /* CampaignSaveHelper.RequestAutoSave(bool force) */
#define VA_SAVE_START_MN   0xFCD068   /* CampaignSaveHelper.<StartAutoSave>d__53.MoveNext */
#define VA_SAVE_PENDING_MN 0xFCCE18   /* CampaignSaveHelper.<ProcessPendingAutoSave>d__50.MoveNext */
#define VA_AUTOSAVE_PRES   0xE84070   /* AutoSavePresenter.CallAutoSave(IViewUpdateData) -> UIInput */
#define VA_AUTOSAVE_VIEW   0xE83E98   /* AutoSaveHandlerView.CallAutoSave -> UIInput */
/* flow coroutines run under the sim scope */
static const struct { uintptr_t va; const char *name; } scope_sites[] = {
    { 0xCD00FC, "EventQueue.<ProcessAsyncEvent>d__14.MoveNext" },
    { 0xD74C08, "CampaignLoadingEventHandler.<OnEvent>d__100.MoveNext (ChangeMap)" },
    { 0xDB6370, "MapTransitionCoordinator.<Run>d__1.MoveNext" },
    { 0xDB6794, "MapTransitionCoordinator.<RunGuarded>d__3.MoveNext" },
    { 0xD69D78, "CampaignEventHandler.<OnEvent>d__30.MoveNext (BaseFieldDiscover)" },
    { 0xD6A2A4, "CampaignEventHandler.<OnEvent>d__31.MoveNext (CreepClearBatch)" },
    { 0xD6B5AC, "CampaignEventHandler.<OnEvent>d__51.MoveNext (ClearDungeonRegion)" },
    { 0xD6B6E4, "CampaignEventHandler.<OnEvent>d__52.MoveNext (SetRecentlySelectedRegion)" },
    { 0xD6B820, "CampaignEventHandler.<OnEvent>d__53.MoveNext (SetSafeFloorSkip)" },
};
#define NSCOPE (int)(sizeof scope_sites / sizeof scope_sites[0])

/* determinism lane API, looked up at install (the lane may not export it yet) */
static void (*rig_det_sim_scope)(int enter);
static int  (*rig_det_in_sim)(void);

/* ---------------------------------------------------------------- state */
static int cfg_seed = 1;              /* 1 = seed from saved state; 0 = stock clock seed */
static int cfg_cap = 20000;           /* Edgar iteration cap (0 = keep the stock time limit); seen 4-15 per floor */
static int cfg_use_tick = 1;
static int role = -1;                 /* -1 no multiplayer, 0 client, 1 host */
static int64_t key_tick = -1; static int32_t key_floor = -1, key_region = -99, attempt;
static _Atomic int iters_now, iters_max;
typedef struct { int32_t seed, iters; } Att;
static Att atts[32]; static int natt;
static struct { int ok, gens; int32_t req, used, attempts, fail, fallback; uint64_t hash; int rooms, gw, gh;
                int64_t tick; int32_t floor, region; } last;
static int hooks_bad;
static int scope_depth;
static uint64_t scope_calls, autosave_blocked, autosave_host;

static Obj dungeon_data(void) { Obj c = cdc(); return c ? AT(c, 0x40, Obj) : NULL; }

/* ---------------------------------------------------------------- 1. floor seed */
static Obj (*o_getrng)(Obj, int, int32_t, void *);
static Obj h_getrng(Obj self, int use_random, int32_t seed, void *mi) {
    if (natt > 0 && natt <= 32) atts[natt - 1].iters = atomic_load(&iters_max);   /* close the previous attempt */
    atomic_store(&iters_now, 0); atomic_store(&iters_max, 0);
    if (use_random && cfg_seed) {
        Obj dd = dungeon_data();
        int32_t region = dd ? AT(dd, 0x10, int32_t) : -1, floor = dd ? AT(dd, 0x14, int32_t) : -1, cur = dd ? AT(dd, 0x20, int32_t) : 0;
        int64_t t = cfg_use_tick ? tick() : 0;
        if (t != key_tick || floor != key_floor || region != key_region) { key_tick = t; key_floor = floor; key_region = region; attempt = 0; natt = 0; }
        seed = trn_floor_seed(t, region, floor, cur, attempt++);
        use_random = 0;
        rig_log("trn: floor seed %d (tick %lld region %d floor %d cur_seed %d attempt %d)", seed, (long long)t, region, floor, cur, attempt - 1);
    } else rig_log("trn: generator seed left to the game (use_random %d, seed %d)", use_random, seed);
    if (natt < 32) atts[natt++] = (Att){ seed, 0 };
    return o_getrng(self, use_random, seed, mi);
}

/* Edgar: no wall-clock limit, an iteration cap instead (same result on every Mac).
 * GraphBasedGeneratorConfiguration: EarlyStopIfIterationsExceeded Nullable<int> @0x14/0x18,
 * EarlyStopIfTimeExceeded Nullable<TimeSpan> @0x20/0x28 (read in <GetEarlyStoppingHandler>b__0). */
static void (*o_edgar_ctor)(Obj, Obj, Obj, void *);
static void h_edgar_ctor(Obj self, Obj desc, Obj conf, void *mi) {
    if (conf && cfg_cap > 0) {
        AT(conf, 0x20, uint8_t) = 0;
        AT(conf, 0x14, uint8_t) = 1; AT(conf, 0x18, int32_t) = cfg_cap;
    }
    o_edgar_ctor(self, desc, conf, mi);
}
static void (*o_earlystop)(Obj, void *);
static void h_earlystop(Obj closure, void *mi) {
    o_earlystop(closure, mi);
    int n = AT(closure, 0x10, int32_t);
    atomic_store(&iters_now, n);
    int m = atomic_load(&iters_max); if (n > m) atomic_store(&iters_max, n);
}

static uint64_t str_hash(uint64_t h, Obj s) {
    if (!s) return fnv(h, "-", 1);
    int32_t n = AT(s, 0x10, int32_t);
    return fnv(h, (char *)s + 0x14, (size_t)(n > 0 ? n : 0) * 2);
}
static uint64_t predata_hash(Obj pd, int *rooms, int *gw, int *gh) {
    uint64_t h = 1469598103934665603ULL; *rooms = 0; *gw = *gh = 0;
    if (!pd) return 0;
    h = fnv(h, (char *)pd + 0x10, 4);                                   /* Seed */
    Obj g = AT(pd, 0x18, Obj);                                          /* GridFlag[,] RawGrids */
    if (g) {
        uintptr_t n = AT(g, 0x18, uintptr_t); uintptr_t *b = AT(g, 0x10, uintptr_t *);
        if (b) { *gw = (int)b[0]; *gh = (int)b[2]; }                    /* Il2CppArrayBounds {uintptr len, int lower} x2 */
        h = fnv(h, (char *)g + 0x20, n * 4);
    }
    h = fnv(h, (char *)pd + 0x30, 8);                                   /* OffsetPos */
    for (int li = 0; li < 2; li++) {                                    /* RawCorridors, RawRooms */
        Obj l = AT(pd, li ? 0x40 : 0x38, Obj); if (!l) continue;
        Obj items = AT(l, 0x10, Obj); int n = AT(l, 0x18, int32_t);
        for (int i = 0; items && i < n; i++) {
            Obj r = AT((char *)items + 0x20, i * 8, Obj); if (!r) continue;
            h = str_hash(h, AT(r, 0x18, Obj));
            h = fnv(h, (char *)r + 0x20, 4); h = fnv(h, (char *)r + 0x28, 8);   /* RoomType, RoomPivot */
            if (li) (*rooms)++;
        }
    }
    return h;
}
static void (*o_setresult)(Obj, int, int32_t, int32_t, int, int32_t, int32_t, Obj, void *);
static void h_setresult(Obj self, int ok, int32_t req, int32_t used, int fallback, int32_t attempts, int32_t fail, Obj pd, void *mi) {
    o_setresult(self, ok, req, used, fallback, attempts, fail, pd, mi);
    if (natt > 0 && natt <= 32) atts[natt - 1].iters = atomic_load(&iters_max);
    last.ok = ok & 1; last.gens++; last.req = req; last.used = used; last.attempts = attempts; last.fail = fail;
    last.fallback = fallback & 1; last.hash = predata_hash(pd, &last.rooms, &last.gw, &last.gh);
    last.tick = key_tick; last.floor = key_floor; last.region = key_region;
    rig_log("trn: generation %s requested %d used %d attempts %d fail %d fallback %d rooms %d grid %dx%d hash %016llx",
            last.ok ? "ok" : "FAILED", req, used, attempts, fail, last.fallback, last.rooms, last.gw, last.gh, (unsigned long long)last.hash);
}

/* ---------------------------------------------------------------- 2. flow barrier helpers */
int trn_flow_busy(void) {
    if (busy()) return 1;
    Obj eq = mf ? AT(mf, 0x48, Obj) : NULL;
    return eq && AT(eq, 0x40, uint8_t);                                 /* EventQueue._isProcessiningAsync */
}

typedef int (*MoveNextFn)(Obj, void *);
static MoveNextFn o_scope[NSCOPE];
static inline int scoped(int i, Obj self, void *mi) {
    int enter = rig_det_sim_scope && rig_det_locked() && scope_depth == 0 && !(rig_det_in_sim && rig_det_in_sim());
    if (enter) rig_det_sim_scope(1);
    scope_depth++; scope_calls++;
    int r = o_scope[i](self, mi);
    scope_depth--;
    if (enter) rig_det_sim_scope(0);
    return r;
}
#define SCOPE_FN(i) static int h_scope##i(Obj s, void *mi) { return scoped(i, s, mi); }
SCOPE_FN(0) SCOPE_FN(1) SCOPE_FN(2) SCOPE_FN(3) SCOPE_FN(4) SCOPE_FN(5) SCOPE_FN(6) SCOPE_FN(7) SCOPE_FN(8)
static void *const h_scope[NSCOPE] = { h_scope0, h_scope1, h_scope2, h_scope3, h_scope4, h_scope5, h_scope6, h_scope7, h_scope8 };

/* ---------------------------------------------------------------- 3. host-only autosave */
void trn_set_role(int is_host) { role = is_host ? 1 : 0; }
static Obj save_helper(void) {                 /* MainFlow._eventHandler -> CampaignEventHandler -> _campaignSaveHelper */
    Obj eh = mf ? AT(mf, 0x50, Obj) : NULL; Obj ce = eh ? AT(eh, 0x20, Obj) : NULL;
    return ce ? AT(ce, 0xA8, Obj) : NULL;
}
static void (*o_save_req)(Obj, int, void *);
static void h_save_req(Obj self, int force, void *mi) {
    if (role == 0) { autosave_blocked++; return; }
    o_save_req(self, force, mi);
}
static int (*o_save_start)(Obj, void *), (*o_save_pend)(Obj, void *);
static int h_save_start(Obj self, void *mi) { if (role == 0) { autosave_blocked++; return 0; } return o_save_start(self, mi); }
static int h_save_pend(Obj self, void *mi) { if (role == 0) return 0; return o_save_pend(self, mi); }
/* the game-hour autosave goes UI input -> AutoSaveRequested -> async EventQueue. In multiplayer it must never enter
 * the queue (the async queue must be identical on every peer): the host asks the save helper directly instead. */
static void (*o_as_pres)(Obj, Obj, void *), (*o_as_view)(Obj, Obj, void *);
static int as_divert(void) {
    if (role < 0) return 0;
    if (role == 1) { Obj sh = save_helper(); if (sh) { autosave_host++; h_save_req(sh, 0, NULL); } }
    else autosave_blocked++;
    return 1;
}
static void h_as_pres(Obj s, Obj d, void *mi) { if (!as_divert()) o_as_pres(s, d, mi); }
static void h_as_view(Obj s, Obj d, void *mi) { if (!as_divert()) o_as_view(s, d, mi); }

/* ---------------------------------------------------------------- 4. map-change requests */
static void *held[16]; static int nheld;
static Obj hold(Obj o) { if (o && nheld < 16) held[nheld++] = F(VA_IL_GCHANDLE_NEW, void *(*)(Obj, bool))(o, false); return o; }
static void release_all(void) { for (int i = 0; i < nheld; i++) F(VA_IL_GCHANDLE_FREE, void (*)(void *))(held[i]); nheld = 0; }
static Obj guid_list(const Guid *g, int n) {
    Obj l = hold(obj_new(TI_LIST_GUID));
    Obj a = hold(F(VA_IL_ARRAY_NEW, Obj (*)(void *, size_t))(md(TI_GUID), (size_t)(n > 4 ? n : 4)));
    if (!l || !a) return NULL;
    for (int i = 0; i < n; i++) memcpy((char *)a + 0x20 + i * 16, &g[i], 16);
    F(VA_IL_WBARRIER, void (*)(Obj, void **, Obj))(l, (void **)((char *)l + 0x10), a);
    AT(l, 0x18, int32_t) = n; AT(l, 0x1C, int32_t) = 1;
    return l;
}
/* rig test helper (multiplayer map changes go through the ord payload + ui vote). payload: u8 kind (1 GoDown, 2 ChangeMap); ChangeMap adds u8 from, u8 to, i32 region, i32 floor, u8 flags, i32 seed,
 * i32 visual guard; then u8 n, n x guid[16]. Little-endian. Returns 0 ok. */
static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static int trn_apply_map_request(const uint8_t *p, uint16_t len) {
    if (!mf || len < 2) return -1;
    int kind = p[0], off = 1;
    int from = 0, to = 0, flags = 0; int32_t region = 0, floor = 0, seed = 0, vgm = 0;
    if (kind == 2) {
        if (len < 1 + 2 + 4 + 4 + 1 + 4 + 4 + 1) return -1;
        from = p[1]; to = p[2]; region = rd32(p + 3); floor = rd32(p + 7); flags = p[11]; seed = rd32(p + 12); vgm = rd32(p + 16);
        off = 20;
    } else if (kind != 1) return -1;
    int n = p[off++];
    if (n > 32 || off + n * 16 > len) return -1;
    Guid g[32]; memcpy(g, p + off, (size_t)n * 16);
    Obj l = guid_list(g, n), ev = NULL;
    if (l && kind == 1) {
        ev = hold(obj_new(TI_GODOWN));
        if (ev) F(VA_GODOWN_CTOR, void (*)(Obj, Obj, void *))(ev, l, NULL);
    } else if (l) {
        ev = hold(obj_new(TI_CHANGEMAP));
        if (ev) F(VA_CHANGEMAP_CTOR, void (*)(Obj, int, int, int32_t, Obj, int32_t, int, int, int32_t, int, int, int, int32_t, void *))
                   (ev, from, to, region, l, floor, flags & 1, (flags >> 1) & 1, seed, (flags >> 2) & 1, (flags >> 3) & 1, (flags >> 4) & 1, vgm, NULL);
    }
    int rc = -1;
    if (ev) { F(VA_EQ_ADD, void (*)(Obj, Obj, void *))(AT(mf, 0x48, Obj), ev, NULL); rc = 0; }
    release_all();
    return rc;
}

/* ---------------------------------------------------------------- install, commands */
void trn_install(void) {
    rig_det_sim_scope = (void (*)(int))dlsym(RTLD_DEFAULT, "rig_det_sim_scope");
    rig_det_in_sim = (int (*)(void))dlsym(RTLD_DEFAULT, "rig_det_in_sim");
    hooks_bad |= rig_hook(VA_GEN_GETRNG, (void *)h_getrng, (void **)&o_getrng, "DSDungeonPreDataGenerator.GetRandomNumbersGenerator");
    hooks_bad |= rig_hook(VA_GEN_SETRESULT, (void *)h_setresult, (void **)&o_setresult, "DSDungeonPreDataGenerator.SetGenerationResult");
    hooks_bad |= rig_hook(VA_EDGAR_GEN_CTOR, (void *)h_edgar_ctor, (void **)&o_edgar_ctor, "GraphBasedGeneratorGrid2D..ctor");
    hooks_bad |= rig_hook(VA_EDGAR_EARLYSTOP, (void *)h_earlystop, (void **)&o_earlystop, "GraphBasedGeneratorGrid2D early stop");
    for (int i = 0; i < NSCOPE; i++) hooks_bad |= rig_hook(scope_sites[i].va, h_scope[i], (void **)&o_scope[i], scope_sites[i].name);
    hooks_bad |= rig_hook(VA_SAVE_REQUEST, (void *)h_save_req, (void **)&o_save_req, "CampaignSaveHelper.RequestAutoSave");
    hooks_bad |= rig_hook(VA_SAVE_START_MN, (void *)h_save_start, (void **)&o_save_start, "CampaignSaveHelper.<StartAutoSave>.MoveNext");
    hooks_bad |= rig_hook(VA_SAVE_PENDING_MN, (void *)h_save_pend, (void **)&o_save_pend, "CampaignSaveHelper.<ProcessPendingAutoSave>.MoveNext");
    hooks_bad |= rig_hook(VA_AUTOSAVE_PRES, (void *)h_as_pres, (void **)&o_as_pres, "AutoSavePresenter.CallAutoSave");
    hooks_bad |= rig_hook(VA_AUTOSAVE_VIEW, (void *)h_as_view, (void **)&o_as_view, "AutoSaveHandlerView.CallAutoSave");
    rig_log("trn: installed (%s), det scope api %s", hooks_bad ? "SOME HOOKS FAILED" : "all hooks ok", rig_det_sim_scope ? "present" : "absent");
}

static char *jdup(Buf *b) { bput(b, "}"); return b->p; }
static void put_status(Buf *b) {
    Obj dd = dungeon_data();
    bput(b, "\"hooks_ok\":%s,\"cfg\":{\"seed\":%d,\"cap\":%d,\"use_tick\":%d,\"role\":%d},\"flow_busy\":%d,\"tick\":%lld,",
         hooks_bad ? "false" : "true", cfg_seed, cfg_cap, cfg_use_tick, role, trn_flow_busy(), (long long)tick());
    if (dd) bput(b, "\"dungeon\":{\"region\":%d,\"floor\":%d,\"seed\":%d,\"attempts\":%d},", AT(dd, 0x10, int32_t), AT(dd, 0x14, int32_t), AT(dd, 0x20, int32_t), AT(dd, 0x28, int32_t));
    bput(b, "\"gen\":{\"count\":%d,\"ok\":%d,\"requested\":%d,\"used\":%d,\"attempts\":%d,\"fail\":%d,\"fallback\":%d,\"rooms\":%d,\"grid\":[%d,%d],\"hash\":\"%016llx\",\"key\":[%lld,%d,%d],\"tries\":[",
         last.gens, last.ok, last.req, last.used, last.attempts, last.fail, last.fallback, last.rooms, last.gw, last.gh, (unsigned long long)last.hash, (long long)last.tick, last.region, last.floor);
    for (int i = 0; i < natt && i < 32; i++) bput(b, "%s[%d,%d]", i ? "," : "", atts[i].seed, atts[i].iters);
    bput(b, "]},\"scope_calls\":%llu,\"autosave_blocked\":%llu,\"autosave_host\":%llu",
         (unsigned long long)scope_calls, (unsigned long long)autosave_blocked, (unsigned long long)autosave_host);
}
char *trn_exec(const char *cmd, const char *line) {
    double v;
    if (!strcmp(cmd, "trn_status")) { Buf b = {0}; bput(&b, "{\"ok\":true,"); put_status(&b); return jdup(&b); }
    if (!strcmp(cmd, "trn_cfg")) {               /* trn_cfg {seed 0|1, cap N, use_tick 0|1, role -1|0|1} */
        if (jnum(line, "seed", &v)) cfg_seed = (int)v;
        if (jnum(line, "cap", &v)) cfg_cap = (int)v;
        if (jnum(line, "use_tick", &v)) cfg_use_tick = (int)v;
        if (jnum(line, "role", &v)) role = (int)v;
        Buf b = {0}; bput(&b, "{\"ok\":true,"); put_status(&b); return jdup(&b);
    }
    if (!strcmp(cmd, "trn_godown") || !strcmp(cmd, "trn_changemap")) {
        /* trn_godown {guids:"g1,g2"}; trn_changemap {from,to,region,floor,flags,seed,guids} */
        char gs[4096]; if (!jstr(line, "guids", gs, sizeof gs)) return strdup("{\"ok\":false,\"error\":\"need guids\"}");
        uint8_t p[600]; int o = 0;
        if (!strcmp(cmd, "trn_godown")) p[o++] = 1;
        else {
            double a = 1, bb = 1, r = 0, f = 0, fl = 0, sd = 0;
            jnum(line, "from", &a); jnum(line, "to", &bb); jnum(line, "region", &r); jnum(line, "floor", &f); jnum(line, "flags", &fl); jnum(line, "seed", &sd);
            int32_t vals[2] = { (int32_t)r, (int32_t)f };
            p[o++] = 2; p[o++] = (uint8_t)a; p[o++] = (uint8_t)bb; memcpy(p + o, vals, 8); o += 8; p[o++] = (uint8_t)fl;
            int32_t s = (int32_t)sd, z = 0; memcpy(p + o, &s, 4); o += 4; memcpy(p + o, &z, 4); o += 4;
        }
        int ni = o++, n = 0;
        for (char *t = strtok(gs, ","); t && n < 32; t = strtok(NULL, ",")) { Guid g; if (guid_parse(t, &g)) { memcpy(p + o, &g, 16); o += 16; n++; } }
        p[ni] = (uint8_t)n;
        int rc = trn_apply_map_request(p, (uint16_t)o);
        Buf b = {0}; bput(&b, "{\"ok\":%s,\"units\":%d,\"tick\":%lld}", rc ? "false" : "true", n, (long long)tick()); return b.p;
    }
    return NULL;
}
