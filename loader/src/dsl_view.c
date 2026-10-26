// dsl_view.c - game-view lookups: THE one path for the loader and its modules (owner rule 2026-10-25 "TEMPLATES +
// GUARDS"; lane fpsscan 2026-10-26), and the SCAN-RULE guard of test copies.
//
// WHY. Object.FindObjectOfType / FindObjectsOfType / Resources.FindObjectsOfTypeAll walk every object of the scene:
// 2-10 ms each on a Mac (a whole frame at 120 Hz), more on Windows and in a big world. Polls of them were the loader's
// FPS cost in the 2026-10-26 audit (ds_multiplayer ext/fpsaudit core.md, mac_measure.md): the Load list's SaveLoadView
// poll (every 30 frames in every world until Esc > Load was opened once: 238 slow frames a minute), the title corner's
// window check (FindObjectsOfType every 5 frames: a > 10 ms frame every 5th frame at 120 fps), the notice box,
// language picker and title menu polls (also while loading).
//
// dsl_view_get(klass, flags) (modules: DSLUi.view_find, wrapper dsl_view_find): the live object of that class (a view,
// a MonoBehaviour), else NULL. In this order:
//   1. the one found before, while it lives: a weak GC handle + Unity's own null test (op_Implicit = m_CachedPtr,
//      traps T1). Nothing is searched while it lives.
//   2. the game's own records, no scene walk: the View of every view presenter (MainFlow._viewPresenters: the title
//      menu, the notice box, the tooltip, the Load window, the language picker ...), the popup container
//      (MainFlow._viewContainer: PopUpViewContainer Active / InactiveUIs, its UIFactory) and the static view container.
//      The game makes and keeps its windows (PopUpUI_Base) there: such a class is only searched in the scene for a
//      DSL_VIEW_NOW call while these records can be read.
//   3. a real search (FindObjectOfType), only when allowed:
//      - at most one a frame for all callers; DSL_VIEW_NOW (an event or a player action says it exists now) always
//        gets its search;
//      - never while loading (MainFlow scene not Title or Campaign) unless DSL_VIEW_NOW; DSL_VIEW_TITLE only at the
//        title, DSL_VIEW_WORLD only in a world (also with DSL_VIEW_NOW);
//      - a miss is kept for the scene: the next search of that class waits 2 s, then 4, 8 ... 60 s (DSL_VIEW_NOW does
//        not wait); a new scene (a new EventSystem, another MainFlow scene) clears it.
// The object may be hidden (DSL_VIEW_INACTIVE only changes step 3): check its state yourself. Never keep the pointer
// past the frame: ask again (step 1 is a handle read).
// dsl_view_all / dsl_view_assets (loader only): one FindObjectsOfType / Resources.FindObjectsOfTypeAll array, with the
// same frame and loading rules (assets: at most one a second per class). dsl_view_popup_shown: a window of the game
// shows (the popup container, no search).
#include "dsl_internal.h"
#include "../sdk/dsl_sdk.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define CALL(mi, R, ...) ((R (*)(__VA_ARGS__, const void *))(*(void **)(mi)))
int dsl_reg_scene(void);                                                    /* dsl_reg.c: 1 title, 2 world */

static struct {
    uintptr_t (*gc_new)(Obj, bool); Obj (*gc_get)(uintptr_t); void (*gc_free)(uintptr_t);
    void *(*obj_class)(Obj); bool (*assignable)(void *, void *); void *(*class_type)(void *); Obj (*type_obj)(void *);
    void *(*methods)(void *, void **); const char *(*mname)(void *); uint32_t (*pcount)(void *); void *(*param)(void *, uint32_t);
    char *(*tname)(void *); bool (*is_generic)(void *); void (*il_free)(void *); const char *(*cname)(void *); int (*elem_size)(void *);
    void *(*elem_class)(void *);
} I;
static void *m_alive, *m_find, *m_finds, *m_findall, *m_comp_go, *m_go_tree;   /* MethodInfo */
static void *k_popup, *k_factory, *k_comp;
static int32_t o_mf_vc = -1, o_mf_vp = -1, o_vc_pop = -1, o_vc_sta = -1, o_pop_act = -1, o_pop_inact = -1, o_pop_fac = -1,
               o_sta_act = -1, o_vp_dict = -1;
static int ok, recs_ok, test_copy;

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
static int live(Obj o) { return o && CALL(m_alive, bool, Obj)(o, m_alive); }
static int is_a(Obj o, void *k) { return o && k && I.assignable(k, I.obj_class(o)); }   /* class test only, no managed call */
static Obj type_of(void *k) { return k ? I.type_obj(I.class_type(k)) : NULL; }
static const char *kname(void *k) { return k && I.cname ? I.cname(k) : "?"; }
static int shown(Obj c) {
    if (!is_a(c, k_comp) || !live(c)) return 0;
    Obj g = CALL(m_comp_go, Obj, Obj)(c, m_comp_go);
    return g && CALL(m_go_tree, bool, Obj)(g, m_go_tree);
}

/* ------------------------------------------------------------------ the game's records (step 2) */
/* Dictionary<K, V> with a 4- or 8-byte key and a reference value: entries {hashCode, next, key, value} of 24 bytes,
   value at +16, a free entry has next < -1 (the runtime's own layout; the entry size is checked once) */
static int32_t o_d_entries = -1, o_d_count = -1;
static int dict_n(Obj d, Obj *ents) {
    *ents = NULL;
    if (!d || !o_d_entries) return 0;
    if (o_d_entries < 0) {
        void *k = I.obj_class(d);
        o_d_entries = dsl_field_offset(k, "_entries"); o_d_count = dsl_field_offset(k, "_count");
        if (o_d_entries <= 0 || o_d_count <= 0) { o_d_entries = 0; dsl_log("view: Dictionary._entries / _count not found: the game's records are off"); return 0; }
    }
    Obj e = AT(d, o_d_entries, Obj); int n = AT(d, o_d_count, int32_t);
    if (!e || n <= 0 || n > (int)AT(e, 0x18, uintptr_t)) return 0;
    static struct { void *k; int ok; } sz[16]; static int nsz;            /* the entry size per Dictionary type, checked once */
    void *ak = I.obj_class(e); int i = 0; while (i < nsz && sz[i].k != ak) i++;
    if (i == nsz) {
        void *ek = I.elem_class ? I.elem_class(ak) : NULL;               /* the Entry struct (the array class itself gives 8) */
        int s = ek && I.elem_size ? I.elem_size(ek) : -1;
        if (s != 24) dsl_log("view: Dictionary entry size %d (not 24) of %s: that record is not read", s, ek ? kname(ek) : "?");
        if (nsz == 16) return 0;
        sz[nsz].k = ak; sz[nsz].ok = s == 24; nsz++;
    }
    if (!sz[i].ok) return 0;
    *ents = e; return n;
}
/* for each value v of the dictionary d */
#define DICT_FOR(d, v) for (Obj e_ = NULL, v = NULL, once_ = (Obj)1; once_; once_ = NULL) \
    for (int i_ = 0, n_ = dict_n((d), &e_); i_ < n_; i_++) \
        if (AT((char *)e_ + 0x20 + 24 * i_, 4, int32_t) >= -1 && (v = AT((char *)e_ + 0x20 + 24 * i_, 16, Obj)) != NULL)
static Obj vcont(int32_t o) {                                               /* a container of MainFlow._viewContainer */
    Obj mf = dsl_soft_mainflow(), c = mf && o_mf_vc >= 0 ? AT(mf, o_mf_vc, Obj) : NULL;
    return c && o >= 0 ? AT(c, o, Obj) : NULL;
}
static Obj fld(Obj o, int32_t off) { return o && off >= 0 ? AT(o, off, Obj) : NULL; }
/* ViewPresenter<T>.<View>k__BackingField: its offset per presenter class, kept */
static struct { void *k; int32_t off; } pv[256]; static int npv;
static int32_t view_off(void *pk) {
    for (int i = 0; i < npv; i++) if (pv[i].k == pk) return pv[i].off;
    int32_t o = dsl_field_offset(pk, "<View>k__BackingField");
    if (npv < 256) { pv[npv].k = pk; pv[npv].off = o; npv++; }
    return o;
}
static Obj from_records(void *k, int *route) {
    if (!recs_ok) return NULL;
    Obj pop = vcont(o_vc_pop);
    if (k == k_factory) { Obj f = fld(pop, o_pop_fac); if (live(f)) { *route = 3; return f; } }
    Obj mf = dsl_soft_mainflow();
    DICT_FOR(fld(fld(mf, o_mf_vp), o_vp_dict), p) {
        int32_t o = view_off(I.obj_class(p)); Obj v = o > 0 ? AT(p, o, Obj) : NULL;
        if (is_a(v, k) && live(v)) { *route = 1; return v; }
    }
    DICT_FOR(fld(pop, o_pop_act), v) if (is_a(v, k) && live(v)) { *route = 2; return v; }
    DICT_FOR(fld(pop, o_pop_inact), v) if (is_a(v, k) && live(v)) { *route = 2; return v; }
    DICT_FOR(fld(vcont(o_vc_sta), o_sta_act), v) if (is_a(v, k) && live(v)) { *route = 2; return v; }
    return NULL;
}
/* the records are the truth for the game's windows while they can be read (MainFlow and its popup container exist) */
static int records_cover(void *k) { return recs_ok && k_popup && I.assignable(k_popup, k) && vcont(o_vc_pop); }

int dsl_view_popup_shown(void) {
    if (!ok) return 0;
    Obj pop = recs_ok ? vcont(o_vc_pop) : NULL;
    if (pop) {
        DICT_FOR(fld(pop, o_pop_act), v) if (shown(v)) return 1;
        DICT_FOR(fld(pop, o_pop_inact), v) if (shown(v)) return 1;
        return 0;
    }
    /* no records (a game update renamed them): the search, at most once a second */
    static double t; static int last;
    if (now_s() - t < 1.0) return last;
    t = now_s(); last = 0;
    Obj a = dsl_view_all(k_popup, 0); int n = a ? (int)AT(a, 0x18, uintptr_t) : 0;
    for (int i = 0; i < n && !last; i++) last = shown(AT((char *)a + 0x20, (size_t)i * 8, Obj));
    return last;
}

/* ------------------------------------------------------------------ frames, scenes, the search budget (step 3) */
static uint32_t gen = 1; static int64_t vframe, searched_at = -1; static int scene_now = -1;
void dsl_view_new_scene(void) { gen++; }
unsigned dsl_view_scene(void) { return gen; }
typedef struct { void *k; int inactive; uintptr_t h; uint32_t miss_gen, said_gen; int misses; double miss_t, wait; } Slot;
static Slot slots[96]; static int nslots;
static Slot *slot_of(void *k, int inactive) {
    for (int i = 0; i < nslots; i++) if (slots[i].k == k && slots[i].inactive == inactive) return &slots[i];
    if (nslots >= 96) return NULL;
    Slot *s = &slots[nslots++]; memset(s, 0, sizeof *s); s->k = k; s->inactive = inactive; return s;
}
static int may_search(int flags) {
    if (flags & DSL_VIEW_TITLE) { if (scene_now != 1) return 0; }
    else if (flags & DSL_VIEW_WORLD) { if (scene_now != 2) return 0; }
    else if (scene_now != 1 && scene_now != 2 && !(flags & DSL_VIEW_NOW)) return 0;
    return (flags & DSL_VIEW_NOW) || searched_at != vframe;
}
void *dsl_view_get(void *k, int flags) {
    if (!ok || !k) return NULL;
    Slot *s = slot_of(k, (flags & DSL_VIEW_INACTIVE) != 0); if (!s) return NULL;
    Obj o = s->h ? I.gc_get(s->h) : NULL;
    if (live(o)) return o;
    if (s->h) { I.gc_free(s->h); s->h = 0; }
    int route = 0; o = from_records(k, &route);
    if (!o) {
        if (records_cover(k) && !(flags & DSL_VIEW_NOW)) return NULL;   /* a window of the game: not made (or gone) */
        if (s->misses && s->miss_gen != gen) s->misses = 0;             /* a new scene: search again at once */
        if (s->misses && !(flags & DSL_VIEW_NOW) && now_s() - s->miss_t < s->wait) return NULL;
        if (!may_search(flags)) return NULL;
        searched_at = vframe; route = 4;
        o = CALL(m_find, Obj, Obj, bool)(type_of(k), (flags & DSL_VIEW_INACTIVE) != 0, m_find);
        if (!live(o)) {
            s->miss_gen = gen; s->miss_t = now_s(); s->wait = !s->misses ? 2 : s->wait * 2 > 60 ? 60 : s->wait * 2; s->misses++;
            if (test_copy && s->misses <= 3) dsl_log("view: %s: searched, not found (scene %d; next search in %.0f s)", kname(k), scene_now, s->wait);
            return NULL;
        }
    }
    s->misses = 0; s->h = I.gc_new(o, false);
    if (test_copy && s->said_gen != gen) {
        s->said_gen = gen;
        dsl_log("view: %s: %s (scene %d)", kname(k), route == 1 ? "a presenter's view" : route == 2 ? "the game's view container"
                : route == 3 ? "the popup container's factory" : "searched, found", scene_now);
    }
    return o;
}
void *dsl_view_all(void *k, int flags) {
    if (!ok || !k || !m_finds || !may_search(flags)) return NULL;
    searched_at = vframe;
    return CALL(m_finds, Obj, Obj, bool)(type_of(k), (flags & DSL_VIEW_INACTIVE) != 0, m_finds);
}
void *dsl_view_assets(void *k, int flags) {
    if (!ok || !k || !m_findall) return NULL;
    static struct { void *k; double t; } last[16]; static int nl;
    int i = 0; while (i < nl && last[i].k != k) i++;
    if (!(flags & DSL_VIEW_NOW) && ((i < nl && now_s() - last[i].t < 1.0) || !may_search(flags))) return NULL;
    if (i == nl && nl < 16) last[nl++].k = k;
    if (i < nl) last[i].t = now_s();
    searched_at = vframe;
    return CALL(m_findall, Obj, Obj)(type_of(k), m_findall);
}

/* ------------------------------------------------------------------ SCAN-RULE guard (test copies only)
   A test copy (background-only app or DSRIG_CTL=1: dsl_soft_test_copy; never a player's start, traps T7) counts every
   UnityEngine.Object.Find*Object* / Resources.FindObjectsOfTypeAll call by its caller's image (the return address:
   "loader" = this binary, "game" = GameAssembly or the engine, else the module's file name). In the steady states
   (title = the idle title, list = the Load list open, world), after the state's first 5 s: more than SG_LIMIT calls
   of one module within 60 s logs "SCAN-RULE <module> <n>/min <state> (...)" once per state; each state's end logs
   "scan: <state> <secs> s: <module> <n>, ...". loader/test/scan_rate_test.py fails on any SCAN-RULE line. The fix is
   dsl_view_find (cached) or an event: a lookup that repeats while nothing happens is a poll.
   Known limits: a search tail-called straight from a hook body counts as the game's; a search the game makes inside
   one of these (FindObjectOfType calls FindObjectsOfType) counts as the game's. */
#define SG_LIMIT 6
#define SG_WARM 5.0
static const struct { const char *k, *m; int argc; const char *sig; } SG[] = {
    { "Object", "FindObjectOfType", 1, "System.Type" },
    { "Object", "FindObjectOfType", 2, "System.Type,System.Boolean" },
    { "Object", "FindFirstObjectByType", 1, "System.Type" },
    { "Object", "FindFirstObjectByType", 2, "System.Type,UnityEngine.FindObjectsInactive" },
    { "Object", "FindAnyObjectByType", 1, "System.Type" },
    { "Object", "FindAnyObjectByType", 2, "System.Type,UnityEngine.FindObjectsInactive" },
    { "Object", "FindObjectsOfType", 1, "System.Type" },
    { "Object", "FindObjectsOfType", 2, "System.Type,System.Boolean" },
    { "Object", "FindObjectsByType", 2, "System.Type,UnityEngine.FindObjectsSortMode" },
    { "Object", "FindObjectsByType", 3, "System.Type,UnityEngine.FindObjectsInactive,UnityEngine.FindObjectsSortMode" },
    { "Object", "FindSceneObjectsOfType", 1, "System.Type" },
    { "Object", "FindObjectsOfTypeIncludingAssets", 1, "System.Type" },
    { "Object", "FindObjectsOfTypeAll", 1, "System.Type" },
    { "Resources", "FindObjectsOfTypeAll", 1, "System.Type" },
};
enum { NSG = sizeof SG / sizeof SG[0], NMOD = 32 };
enum { ST_LOADING, ST_TITLE, ST_LIST, ST_WORLD };
static const char *const st_name[] = { "loading", "title", "list", "world" };
typedef Obj (*SgFn)(Obj, void *, void *, void *);
static SgFn sg_orig[NSG];
typedef struct { char name[48]; int rule, n, nw, flagged, ri; double ring[SG_LIMIT + 1]; } Mod;
static Mod mods[NMOD]; static int nmods, sg_on, sg_state = -1;
static double sg_t0, sg_tw;                                                 /* the state's start, this count window's start */
static struct { uintptr_t ra; int m; } racache[1024];
static uintptr_t self_base;
static int mod_index(const char *nm, int rule) {
    for (int i = 0; i < nmods; i++) if (!strcmp(mods[i].name, nm)) return i;
    if (nmods >= NMOD) return 0;
    Mod *m = &mods[nmods]; memset(m, 0, sizeof *m); snprintf(m->name, sizeof m->name, "%s", nm); m->rule = rule;
    return nmods++;
}
static int mod_of(uintptr_t ra) {
    unsigned h = (unsigned)((ra >> 2) * 2654435761u) & 1023;
    for (int p = 0; p < 1024 && racache[h].ra; p++, h = (h + 1) & 1023) if (racache[h].ra == ra) return racache[h].m;
    char nm[260] = ""; uintptr_t base = 0;
#ifdef _WIN32
    HMODULE hm = NULL;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ra, &hm) && hm) {
        wchar_t w[520]; DWORD n = GetModuleFileNameW(hm, w, 520); base = (uintptr_t)hm;
        if (n && n < 520) {
            char u[1040]; w[n] = 0; WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL);
            const char *b = strrchr(u, '\\'); snprintf(nm, sizeof nm, "%s", b ? b + 1 : u);
        }
    }
#else
    Dl_info di;
    if (dladdr((void *)ra, &di) && di.dli_fname) { const char *b = strrchr(di.dli_fname, '/'); snprintf(nm, sizeof nm, "%s", b ? b + 1 : di.dli_fname); base = (uintptr_t)di.dli_fbase; }
#endif
    char *dot = strrchr(nm, '.'); if (dot && dot != nm) *dot = 0;
    int game = !strncmp(nm, "GameAssembly", 12) || !strncmp(nm, "UnityPlayer", 11);
    int m = base && base == self_base ? mod_index("loader", 1) : game ? mod_index("game", 0) : !nm[0] ? mod_index("unknown", 0) : mod_index(nm, 1);
    if (!racache[h].ra) { racache[h].ra = ra; racache[h].m = m; }
    return m;
}
static void sg_count(int site, uintptr_t ra, Obj type) {
    Mod *m = &mods[mod_of(ra)];
    m->n++;
    double t = now_s(), w = t - sg_t0 - SG_WARM;
    if (!m->rule || m->flagged || sg_state == ST_LOADING || w < 0) return;
    m->nw++;
    m->ring[m->ri] = t; m->ri = (m->ri + 1) % (SG_LIMIT + 1);
    double oldest = m->ring[m->ri];                                         /* the call SG_LIMIT calls before this one */
    if (oldest <= 0 || t - oldest > 60.0) return;
    m->flagged = 1;
    void *tt = type ? AT(type, 0x10, void *) : NULL;                        /* Il2CppReflectionType.type */
    char *tn = tt ? I.tname(tt) : NULL;
    dsl_log("SCAN-RULE %s %d/min %s (%d scene searches in %.0f s after the state's first %.0f s; last %s.%s/%d %s): a poll; ask dsl_view_find (cached) or use an event",
            m->name, (int)(m->nw * 60.0 / (w > 1 ? w : 1) + 0.5), st_name[sg_state], m->nw, w, SG_WARM, SG[site].k, SG[site].m, SG[site].argc, tn ? tn : "?");
    if (tn && I.il_free) I.il_free(tn);
}
#define SG_FN(i) static Obj sg_h##i(Obj a, void *b, void *c, void *d) { sg_count(i, (uintptr_t)__builtin_return_address(0), a); return sg_orig[i](a, b, c, d); }
SG_FN(0) SG_FN(1) SG_FN(2) SG_FN(3) SG_FN(4) SG_FN(5) SG_FN(6) SG_FN(7) SG_FN(8) SG_FN(9) SG_FN(10) SG_FN(11) SG_FN(12) SG_FN(13)
static const SgFn sg_fn[NSG] = { sg_h0, sg_h1, sg_h2, sg_h3, sg_h4, sg_h5, sg_h6, sg_h7, sg_h8, sg_h9, sg_h10, sg_h11, sg_h12, sg_h13 };
int dsl_rows_list_shown(void);                                              /* dsl_rows.c */
/* "scan: <state> <secs> s: <module> <n>, ...": the searches of each module since the last such line (at the state's end,
   and every 30 s in a long state: a test stops the game in its last state) */
static void sg_counts(double t) {
    char b[700] = ""; size_t o = 0;
    for (int i = 0; i < nmods; i++) if (mods[i].n) o += (size_t)snprintf(b + o, o < sizeof b ? sizeof b - o : 0, "%s%s %d", o ? ", " : "", mods[i].name, mods[i].n);
    dsl_log("scan: %s %.1f s: %s", st_name[sg_state], t - sg_tw, o ? b : "none");
    for (int i = 0; i < nmods; i++) mods[i].n = 0;
    sg_tw = t;
}
static void sg_frame(void) {
    int st = scene_now == 2 ? ST_WORLD : scene_now == 1 && dsl_soft_title_idle() ? ST_TITLE : ST_LOADING;
    if (st != ST_LOADING && dsl_rows_list_shown()) st = ST_LIST;
    double t = now_s();
    if (st == sg_state) { if (t - sg_tw >= 30) sg_counts(t); return; }
    if (sg_state >= 0) sg_counts(t);
    for (int i = 0; i < nmods; i++) { mods[i].n = mods[i].nw = mods[i].flagged = mods[i].ri = 0; memset(mods[i].ring, 0, sizeof mods[i].ring); }
    sg_state = st; sg_t0 = sg_tw = t;
}
void dsl_view_flush(void) { if (sg_on && sg_state >= 0) sg_counts(now_s()); }
static void *msig(void *k, const char *name, int argc, const char *sig) {   /* overloads: parameter type names; no generics */
    void *it = NULL, *m;
    while (k && (m = I.methods(k, &it))) {
        if (strcmp(I.mname(m), name) || (int)I.pcount(m) != argc || (I.is_generic && I.is_generic(m))) continue;
        char b[400] = ""; size_t o = 0;
        for (int i = 0; i < argc; i++) {
            char *tn = I.tname(I.param(m, (uint32_t)i));
            o += (size_t)snprintf(b + o, o < sizeof b ? sizeof b - o : 0, "%s%s", i ? "," : "", tn ? tn : "?");
            if (tn && I.il_free) I.il_free(tn);
        }
        if (!strcmp(b, sig)) return *(void **)m ? m : NULL;
    }
    return NULL;
}
static void sg_install(void) {
#ifdef _WIN32
    HMODULE hm = NULL;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(void *)sg_install, &hm)) self_base = (uintptr_t)hm;
#else
    Dl_info di; if (dladdr((void *)sg_install, &di)) self_base = (uintptr_t)di.dli_fbase;
#endif
    int n = 0; char miss[600] = ""; size_t mo = 0;
    for (int i = 0; i < NSG; i++) {
        void *k = dsl_find_class("UnityEngine", SG[i].k), *m = k ? msig(k, SG[i].m, SG[i].argc, SG[i].sig) : NULL;
        char who[96]; snprintf(who, sizeof who, "loader test copy scan guard %s.%s/%d", SG[i].k, SG[i].m, SG[i].argc);
        if (m && !dsl_hook(DSL_OWNER_LOADER, *(void **)m, (void *)sg_fn[i], (void **)&sg_orig[i], who)) n++;
        else mo += (size_t)snprintf(miss + mo, mo < sizeof miss ? sizeof miss - mo : 0, " %s.%s/%d%s", SG[i].k, SG[i].m, SG[i].argc, m ? "(refused)" : "(missing)");
    }
    sg_on = n > 0;
    dsl_log("scan guard: %s (test copy, %s): %d of %d scene-search methods counted%s%s", sg_on ? "on" : "OFF", dsl_soft_test_copy(), n, NSG, mo ? "; not:" : "", miss);
}

/* ------------------------------------------------------------------ install + frame */
/* every frame: the kit tick (main thread), before the loader's and the modules' frame work */
void dsl_view_frame(void) {
    if (!ok) return;
    vframe++;
    int s = dsl_reg_scene(); if (s != scene_now) { scene_now = s; gen++; }
    if (!test_copy && dsl_soft_test_copy()) { test_copy = 1; sg_install(); }   /* the signal is set at the start (Mac) or soon after */
    if (sg_on) sg_frame();
}
/* after il2cpp_init, from dsl_ui_install (main thread, before the first frame) */
void dsl_view_install(void) {
    static int done; if (done) return; done = 1;
#define X(var, name) *(void **)&I.var = dsl_il2cpp_fn(name)
    X(gc_new, "il2cpp_gchandle_new"); X(gc_get, "il2cpp_gchandle_get_target"); X(gc_free, "il2cpp_gchandle_free");
    X(obj_class, "il2cpp_object_get_class"); X(assignable, "il2cpp_class_is_assignable_from"); X(class_type, "il2cpp_class_get_type");
    X(type_obj, "il2cpp_type_get_object"); X(methods, "il2cpp_class_get_methods"); X(mname, "il2cpp_method_get_name");
    X(pcount, "il2cpp_method_get_param_count"); X(param, "il2cpp_method_get_param"); X(tname, "il2cpp_type_get_name");
    X(is_generic, "il2cpp_method_is_generic"); X(il_free, "il2cpp_free"); X(cname, "il2cpp_class_get_name");
    X(elem_size, "il2cpp_class_array_element_size"); X(elem_class, "il2cpp_class_get_element_class");
#undef X
    void *kObj = dsl_find_class("UnityEngine", "Object"), *kRes = dsl_find_class("UnityEngine", "Resources");
    void *kGo = dsl_find_class("UnityEngine", "GameObject"); k_comp = dsl_find_class("UnityEngine", "Component");
    if (!I.gc_new || !I.gc_get || !I.gc_free || !I.obj_class || !I.assignable || !I.class_type || !I.type_obj || !I.methods
        || !I.mname || !I.pcount || !I.param || !I.tname || !kObj || !kGo || !k_comp) { dsl_log("view: il2cpp names missing: game-view lookups off"); return; }
    m_alive = msig(kObj, "op_Implicit", 1, "UnityEngine.Object");
    m_find = msig(kObj, "FindObjectOfType", 2, "System.Type,System.Boolean");
    m_finds = msig(kObj, "FindObjectsOfType", 2, "System.Type,System.Boolean");
    m_findall = kRes ? msig(kRes, "FindObjectsOfTypeAll", 1, "System.Type") : NULL;
    m_comp_go = msig(k_comp, "get_gameObject", 0, ""); m_go_tree = msig(kGo, "get_activeInHierarchy", 0, "");
    if (!m_alive || !m_find || !m_comp_go || !m_go_tree) { dsl_log("view: Object.op_Implicit / FindObjectOfType missing: game-view lookups off"); return; }
    /* the game's records (step 2): MainFlow._viewContainer (ViewObjectContainer) and _viewPresenters (ViewPresenterContainer) */
    void *kMF = dsl_find_class("Refactor.Main", "MainFlow"), *kVOC = dsl_find_class("Refactor.Main", "ViewObjectContainer");
    void *kPop = dsl_find_class("Refactor.UI", "PopUpViewContainer"), *kSta = dsl_find_class("Refactor.UI", "StaticViewContainer");
    void *kVPC = dsl_find_class("Refactor.View", "ViewPresenterContainer");
    k_popup = dsl_find_class("Refactor.UI", "PopUpUI_Base"); k_factory = dsl_find_class("Refactor.Main.UI", "UIFactory");
    if (kMF) { o_mf_vc = dsl_field_offset(kMF, "_viewContainer"); o_mf_vp = dsl_field_offset(kMF, "_viewPresenters"); }
    if (kVOC) { o_vc_pop = dsl_field_offset(kVOC, "PopUpViewContainer"); o_vc_sta = dsl_field_offset(kVOC, "StaticViewContainer"); }
    if (kPop) { o_pop_act = dsl_field_offset(kPop, "<ActiveUIs>k__BackingField"); o_pop_inact = dsl_field_offset(kPop, "<InactiveUIs>k__BackingField"); o_pop_fac = dsl_field_offset(kPop, "_factory"); }
    if (kSta) o_sta_act = dsl_field_offset(kSta, "<ActiveUIs>k__BackingField");
    if (kVPC) o_vp_dict = dsl_field_offset(kVPC, "_viewPresenters");
    recs_ok = o_mf_vc >= 0 && o_vc_pop >= 0 && o_pop_act >= 0 && o_pop_inact >= 0 && k_popup;
    ok = 1;
    dsl_log("view: game-view lookups on (the game's records: popups %s, presenters %s, static views %s)", recs_ok ? "on" : "OFF",
            o_mf_vp >= 0 && o_vp_dict >= 0 ? "on" : "OFF", o_vc_sta >= 0 && o_sta_act >= 0 ? "on" : "OFF");
}
