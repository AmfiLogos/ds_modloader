// mp_fog.c - per-player fog of war (host rule "Shared fog of war: Off" = each player sees only what his own units see).
// DISPLAY ONLY. Research: ~/ds_multiplayer/ext/core_next/fog_research.md (build 25846389).
//
// Rules this file keeps:
//  - The sim's fog state (VisionArea, RawMapData._visitedPositions = save "VisitedPositions", VisionDataHandler stacks
//    and caches) is only READ here, never written. Every hooked presenter method still runs its original, so the shared
//    getters that drain state (GetPlayerVisionDeltas, ConsumeTemporaryVisionRestoreFog, GetHideVisionDeltas) run exactly
//    as in vanilla, on every peer.
//  - Only the LAST step changes: which FogOfWarView leaf (visible / explored / black) a tile ends in, and the bool given to
//    EntityObjectView.SetVisible for non-player units. Both are view (det_ranges class 1).
//  - No session, shared fog on, or a layout check failed: every hook calls its original at once (zero change).
//
// Masks (per map type, world fog coordinates = the Vector2Int the presenter gives the view leaves):
//   my_vis  : tiles seen now by vision sources that count for me (rebuilt from VisionDataHandler._visions each refresh)
//   my_seen : tiles I explored. Reseeded at each map load (SetDefaultVision) from the shared visited list (night decision
//             2026-10-15: "explored after a reload / map change: shared again"); between loads it grows from my_vis only.
// A source counts for me: its guid is in VisionDataHandler._playerVisions AND (dsnet owner == me, or lent to me, or no owner
// (free unit, building, anything not a unit)). Plus the _temporaryPlayerVisions tiles (shown, never added to my_seen,
// like vanilla where temporary vision never marks a tile visited).
#include "rig.h"
#include "mp.h"
#include "mp_game.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* hooks */
#define VA_FOGP_REFRESH_VISION   0xC2B9D4   /* FogOfWarPresenter.RefreshVision(IViewUpdateData)          UpdateType 25 */
#define VA_FOGP_SET_DEFAULT      0xC2B09C   /* FogOfWarPresenter.SetDefaultVision(IViewUpdateData)       UpdateType 24 */
#define VA_FOGP_REFRESH_IMPL     0xC2C3DC   /* FogOfWarPresenter.RefreshImplementedArea(IViewUpdateData) UpdateType 30 */
#define VA_FOGV_SET_VISIBLE      0xC2B838   /* FogOfWarView.SetVisible(Vector2Int) */
#define VA_FOGV_SET_HALF         0xC2C054   /* FogOfWarView.SetHalfVisible(Vector2Int) (Windows: one body with LoadVisible) */
#define VA_FOGV_LOAD_VISIBLE     0xC2B69C   /* FogOfWarView.LoadVisible(Vector2Int) (Mac only, see install) */
#define VA_EOV_SET_VISIBLE       0xE24640   /* EntityObjectView.SetVisible(Guid, bool) */
#define VA_EOP_REFRESH_HIDE      0xE1FD90   /* EntityObjectPresenter.OnRefreshHideVision(IViewUpdateData) */
/* called, not hooked */
#define VA_FOGV_SET_NOT_VISITED  0xC2BEB8   /* FogOfWarView.SetNotVisited(Vector2Int) */
#define VA_IL_CLASS_NAME         0x6F8C20   /* il2cpp_class_get_name */
#define VA_IL_GCH_WEAK           0x6F9544   /* il2cpp_gchandle_new_weakref(obj, track_resurrection) */
#define VA_IL_GCH_TARGET         0x6F9598   /* il2cpp_gchandle_get_target(handle) */
#define VA_IL_GCH_FREE           0x6F95E4   /* il2cpp_gchandle_free(handle) */

typedef struct { int32_t x, y; } V2I;
typedef void (*presenter_fn)(Obj self, Obj data, void *mi);
typedef void (*leaf_fn)(Obj view, V2I pos, void *mi);
typedef void (*eov_fn)(Obj view, Guid g, bool visible, void *mi);

static presenter_fn o_refresh_vision, o_set_default, o_refresh_impl, o_eop_refresh_hide;
static leaf_fn o_set_visible, o_set_half, o_load_visible;
static eov_fn o_eov_set_visible;

/* ------------------------------------------------------------------ state */
#define NMAP 8                       /* MapType values used as keys (0 base, 1 dungeon, ...) */
#define DIM 2048                     /* bitmap side; tiles outside [0, DIM) are left vanilla (logged once) */
#define WORDS (DIM * DIM / 64)
enum { CTX_NONE, CTX_LOAD, CTX_REDRAW, CTX_REFRESH, CTX_IMPL };

typedef struct {
    uint64_t *vis, *seen;            /* lazily allocated */
    int32_t *vis_list; int vis_n, vis_cap;   /* tiles set in vis (cleared one by one at the next rebuild) */
    uintptr_t presenter_h;           /* weak gchandle (pointer-sized in this il2cpp, like ui_game.c u_gc_new; uint32 crashed) to this map's FogOfWarPresenter */
    int presenter_scene;
    Obj vh;                          /* VisionDataHandler the masks were built from */
    int vh_scene, seeded;            /* scene of vh; my_seen was seeded by a SetDefaultVision */
    int64_t built_frame;
} MapFog;

static MapFog M[NMAP];
static int g_shared;                 /* 1 = vanilla (host rule On) */
static int g_bad;                    /* a layout check failed: vanilla for the rest of the run */
static int g_ctx = CTX_NONE, g_ctx_map = -1;
static int g_pending;                /* a full redraw is wanted (main thread, steady game) */
static pthread_t g_main; static int g_main_known;
static int g_logged_layout, g_logged_range;

static const char *cls_name(Obj o) { return o && *(void **)o ? F(VA_IL_CLASS_NAME, const char *(*)(void *))(*(void **)o) : ""; }
static int is_cls(Obj o, const char *n) { return o && !strcmp(cls_name(o), n); }
static void note_main(void) { if (!g_main_known) { g_main = pthread_self(); g_main_known = 1; } }
static int on_main(void) { return g_main_known && pthread_equal(g_main, pthread_self()); }

/* session + me; -1 = no session (vanilla) */
static dsnet_client *client(int *me) {
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    int id = c ? dsnet_player_id(c) : -1;
    if (me) *me = id;
    return id >= 0 ? c : NULL;
}
static int tracking(void) { return !g_bad && client(NULL) != NULL; }   /* keep masks up to date */
static int filtering(void) { return !g_shared && tracking(); }         /* change what the view shows */
/* A load or a map change frees the old RuntimeMapData: every cached VisionDataHandler is dropped and the view stays
   vanilla until a presenter call (SetDefaultVision / RefreshVision / OnRefreshHideVision) hands a live one again.
   (Loader lane 07:31: SIGBUS in rebuild <- decide <- EntityObjectView.SetVisible during the host's join-point reload.) */
static void drop_vh(void);
static int world_changing(void) {
    if (!busy() && !mp_game_load_pending()) return 0;
    drop_vh();
    return 1;
}

static int counts_for_me(dsnet_client *c, int me, const uint8_t g[16]) {
    int o = dsnet_owner_of(c, g);
    if (o == me || o == -1) return 1;          /* mine, or free / not a unit (buildings ...) */
    return dsnet_lent_to(c, g) == me;          /* lent to me; another player's or reserved: no */
}

/* ------------------------------------------------------------------ bitmaps */
static int idx(V2I p) { return (p.x >= 0 && p.y >= 0 && p.x < DIM && p.y < DIM) ? p.y * DIM + p.x : -1; }
static int bit(const uint64_t *b, int i) { return b && i >= 0 && (b[i >> 6] >> (i & 63) & 1); }
static void setb(uint64_t *b, int i) { b[i >> 6] |= 1ULL << (i & 63); }
static void clrb(uint64_t *b, int i) { b[i >> 6] &= ~(1ULL << (i & 63)); }
static MapFog *mapfog(int map) {
    if (map < 0 || map >= NMAP) return NULL;
    MapFog *m = &M[map];
    if (!m->vis) { m->vis = calloc(WORDS, 8); m->seen = calloc(WORDS, 8); }
    return (m->vis && m->seen) ? m : NULL;
}
static void range_note(V2I p) {
    if (!g_logged_range) { g_logged_range = 1; rig_log("fog: tile (%d,%d) outside the %d x %d mask: left vanilla", p.x, p.y, DIM, DIM); }
}

/* ------------------------------------------------------------------ IL2CPP collection readers (validated) */
/* Il2CppArray: length at 0x18, data at 0x20. Dictionary: _entries 0x18, _count 0x20, _freeList 0x24, _freeCount 0x28.
   HashSet: _slots 0x18, _count 0x20, _lastIndex 0x24. List: _items 0x10, _size 0x18. Entries: hashCode int @0 (<0 = free). */
static int64_t arr_len(Obj a) { return a ? (int64_t)AT(a, 0x18, uintptr_t) : 0; }

static int layout_fail(const char *what, long a, long b) {
    g_bad = 1;
    rig_log("fog: layout check failed (%s: %ld vs %ld): per-player fog OFF for this run, vanilla fog", what, a, b);
    g_pending = 1;                                  /* redraw vanilla if anything was filtered */
    return -1;
}
/* number of live entries, or -1 (check failed) */
static int dict_live(Obj d, int stride, const char *what) {
    if (!d) return 0;
    Obj e = AT(d, 0x18, Obj); int n = AT(d, 0x20, int32_t), fc = AT(d, 0x28, int32_t), live = 0;
    if (n < 0 || fc < 0 || (n && (!e || n > arr_len(e)))) return layout_fail(what, n, (long)arr_len(e));
    for (int i = 0; i < n; i++) if (*(int32_t *)((char *)e + 0x20 + (int64_t)i * stride) >= 0) live++;
    if (live != n - fc) return layout_fail(what, live, n - fc);
    return live;
}
static int set_live(Obj h, int stride, const char *what) {
    if (!h) return 0;
    Obj s = AT(h, 0x18, Obj); int cnt = AT(h, 0x20, int32_t), last = AT(h, 0x24, int32_t), live = 0;
    if (cnt < 0 || last < 0 || (last && (!s || last > arr_len(s)))) return layout_fail(what, last, (long)arr_len(s));
    for (int i = 0; i < last; i++) if (*(int32_t *)((char *)s + 0x20 + (int64_t)i * stride) >= 0) live++;
    if (live != cnt) return layout_fail(what, live, cnt);
    return live;
}

/* ------------------------------------------------------------------ mask rebuild */
static int vh_ok(Obj vh, int map) {                  /* class + VisionDataHandler._mapData.MapType, every time (cheap) */
    static int logged;
    Obj md = vh ? AT(vh, 0x10, Obj) : NULL;
    if (is_cls(vh, "VisionDataHandler") && md && AT(md, 0x10, int32_t) == map) return 1;
    if (vh && !logged) { logged = 1; rig_log("fog: vision handler %s / map %d does not match map %d: left vanilla", cls_name(vh), md ? AT(md, 0x10, int32_t) : -1, map); }
    return 0;
}
static Obj vh_of_runtime(Obj rt) { return rt ? AT(rt, 0x48, Obj) : NULL; }   /* RuntimeMapData._vision */

static void add_vis(MapFog *m, V2I p, int seen) {
    int i = idx(p);
    if (i < 0) { range_note(p); return; }
    if (!bit(m->vis, i)) {
        setb(m->vis, i);
        if (m->vis_n == m->vis_cap) {
            int nc = m->vis_cap ? m->vis_cap * 2 : 4096; int32_t *nl = realloc(m->vis_list, (size_t)nc * 4);
            if (!nl) return;
            m->vis_list = nl; m->vis_cap = nc;
        }
        m->vis_list[m->vis_n++] = i;
    }
    if (seen) setb(m->seen, i);
}

/* rebuild my_vis of `map` from vh (read only). 0 ok, -1 not possible (caller stays vanilla for this call). */
static void drop_vh(void) { for (int i = 0; i < NMAP; i++) M[i].vh = NULL; if (filtering()) g_pending = 1; }   /* redraw after it */
static int rebuild(int map, Obj vh) {
    MapFog *m = mapfog(map);
    int me; dsnet_client *c = client(&me);
    if (!m || !c || g_bad || world_changing() || !vh_ok(vh, map)) return -1;
    for (int k = 0; k < m->vis_n; k++) clrb(m->vis, m->vis_list[k]);
    m->vis_n = 0; m->vh = vh; m->vh_scene = scene(); m->built_frame = rig_frames;

    Obj visions = AT(vh, 0x20, Obj), pvs = AT(vh, 0x28, Obj), tmp = AT(vh, 0x50, Obj);
    int nv = dict_live(visions, 0x20, "_visions Dictionary<Guid,VisionArea> stride 0x20"); if (nv < 0) return -1;
    int np = set_live(pvs, 24, "_playerVisions HashSet<Guid> stride 24"); if (np < 0) return -1;
    int nt = dict_live(tmp, 20, "_temporaryPlayerVisions Dictionary<Vector2Int,int> stride 20"); if (nt < 0) return -1;

    /* player-side guids that count for me */
    Guid mine[1024]; int nm = 0;
    if (pvs) {
        Obj s = AT(pvs, 0x18, Obj); int last = AT(pvs, 0x24, int32_t);
        for (int i = 0; i < last && nm < 1024; i++) {
            char *e = (char *)s + 0x20 + (int64_t)i * 24;
            if (*(int32_t *)e < 0) continue;
            if (counts_for_me(c, me, (const uint8_t *)(e + 8))) memcpy(&mine[nm++], e + 8, 16);
        }
    }
    int nsrc = 0, ntiles = 0, first_area_checked = 0;
    if (visions && nm) {
        Obj ent = AT(visions, 0x18, Obj); int n = AT(visions, 0x20, int32_t);
        for (int i = 0; i < n; i++) {
            char *e = (char *)ent + 0x20 + (int64_t)i * 0x20;
            if (*(int32_t *)e < 0) continue;
            int hit = 0;
            for (int k = 0; k < nm && !hit; k++) hit = !memcmp(&mine[k], e + 8, 16);
            if (!hit) continue;
            Obj area = *(Obj *)(e + 0x18);
            if (!area) continue;
            if (!first_area_checked) {
                first_area_checked = 1;
                if (!is_cls(area, "VisionArea")) return layout_fail("_visions value class is not VisionArea", 0, 0);
            }
            Obj list = AT(area, 0x20, Obj); if (!list) continue;
            Obj items = AT(list, 0x10, Obj); int sz = AT(list, 0x18, int32_t);
            if (sz < 0 || (sz && (!items || sz > arr_len(items)))) return layout_fail("VisionArea.Positions List size", sz, (long)arr_len(items));
            for (int j = 0; j < sz; j++) add_vis(m, *(V2I *)((char *)items + 0x20 + (int64_t)j * 8), 1);
            nsrc++; ntiles += sz;
        }
    }
    if (tmp && nt) {
        Obj ent = AT(tmp, 0x18, Obj); int n = AT(tmp, 0x20, int32_t);
        for (int i = 0; i < n; i++) {
            char *e = (char *)ent + 0x20 + (int64_t)i * 20;
            if (*(int32_t *)e >= 0) add_vis(m, *(V2I *)(e + 8), 0);
        }
    }
    if (!g_logged_layout) {
        g_logged_layout = 1;
        rig_log("fog: layout ok (map %d): _visions %d live, _playerVisions %d live (%d count for player %d), temporary tiles %d; "
                "%d of my sources, %d tiles", map, nv, np, nm, me, nt, nsrc, ntiles);
    }
    return 0;
}

/* ------------------------------------------------------------------ presenter cache (weak handles) */
static void cache_presenter(int map, Obj P) {
    MapFog *m = mapfog(map); if (!m) return;
    Obj cur = m->presenter_h ? F(VA_IL_GCH_TARGET, Obj (*)(uintptr_t))(m->presenter_h) : NULL;
    if (cur == P && m->presenter_scene == scene()) return;
    if (m->presenter_h) F(VA_IL_GCH_FREE, void (*)(uintptr_t))(m->presenter_h);
    m->presenter_h = F(VA_IL_GCH_WEAK, uintptr_t (*)(Obj, bool))(P, false);
    m->presenter_scene = scene();
}
static Obj presenter_of(int map) {
    if (map < 0 || map >= NMAP || !M[map].presenter_h || M[map].presenter_scene != scene()) return NULL;
    Obj P = F(VA_IL_GCH_TARGET, Obj (*)(uintptr_t))(M[map].presenter_h);
    return (P && is_cls(P, "FogOfWarPresenter") && AT(P, 0x10, Obj)) ? P : NULL;
}

/* ------------------------------------------------------------------ presenter hooks */
static void run_ctx(int ctx, int map, presenter_fn orig, Obj P, Obj d, void *mi) {
    int oc = g_ctx, om = g_ctx_map;
    g_ctx = ctx; g_ctx_map = map;
    orig(P, d, mi);
    g_ctx = oc; g_ctx_map = om;
}
static int presenter_map(Obj P) { return AT(P, 0x20, int32_t); }   /* FogOfWarPresenter._targetMap */
static Obj presenter_vh(Obj P) { return vh_of_runtime(AT(P, 0x30, Obj)); }   /* _runtimeData (IRuntimeDataReader) */

static void h_set_default(Obj P, Obj d, void *mi) {
    note_main();
    if (!P || !tracking()) { o_set_default(P, d, mi); return; }
    int map = presenter_map(P); MapFog *m = mapfog(map);
    cache_presenter(map, P);
    if (!m) { o_set_default(P, d, mi); return; }
    memset(m->seen, 0, WORDS * 8);                  /* reseed: LoadVisible tiles (shared visited) become my explored */
    if (rebuild(map, presenter_vh(P)) < 0) { o_set_default(P, d, mi); return; }
    m->seeded = 1;
    run_ctx(CTX_LOAD, map, o_set_default, P, d, mi);
}
static void h_refresh_vision(Obj P, Obj d, void *mi) {
    note_main();
    if (!P || !tracking()) { o_refresh_vision(P, d, mi); return; }
    int map = presenter_map(P);
    cache_presenter(map, P);
    if (rebuild(map, presenter_vh(P)) < 0) { o_refresh_vision(P, d, mi); return; }
    run_ctx(CTX_REFRESH, map, o_refresh_vision, P, d, mi);
}
static void h_refresh_impl(Obj P, Obj d, void *mi) {
    note_main();
    if (!P || !tracking()) { o_refresh_impl(P, d, mi); return; }
    int map = presenter_map(P);
    cache_presenter(map, P);
    if (rebuild(map, presenter_vh(P)) < 0) { o_refresh_impl(P, d, mi); return; }
    run_ctx(CTX_IMPL, map, o_refresh_impl, P, d, mi);
}

/* ------------------------------------------------------------------ view leaf hooks */
static void not_visited(Obj V, V2I p) { F(VA_FOGV_SET_NOT_VISITED, leaf_fn)(V, p, NULL); }

static void h_set_visible(Obj V, V2I p, void *mi) {
    MapFog *m = (g_ctx != CTX_NONE) ? mapfog(g_ctx_map) : NULL;
    int i = idx(p);
    if (!m || !filtering() || i < 0) { if (m && i < 0) range_note(p); o_set_visible(V, p, mi); return; }
    if (bit(m->vis, i)) { o_set_visible(V, p, mi); return; }
    if (g_ctx == CTX_IMPL) {                        /* vanilla: visited tile in the new area -> SetVisible */
        if (bit(m->seen, i)) o_set_visible(V, p, mi); else not_visited(V, p);
        return;
    }
    if (bit(m->seen, i)) o_set_half(V, p, NULL); else not_visited(V, p);
}
/* SetHalfVisible and (Mac) LoadVisible: one rule, because on Windows both are one function body.
   CTX_LOAD: only LoadVisible runs (SetDefaultVision never calls SetHalfVisible) -> seed my explored, show as vanilla. */
static void half_like(leaf_fn orig, Obj V, V2I p, void *mi) {
    MapFog *m = (g_ctx != CTX_NONE) ? mapfog(g_ctx_map) : NULL;
    int i = idx(p);
    if (!m || i < 0) { if (m) range_note(p); orig(V, p, mi); return; }
    if (g_ctx == CTX_LOAD) { setb(m->seen, i); orig(V, p, mi); return; }
    if (!filtering()) { orig(V, p, mi); return; }
    if (bit(m->vis, i)) o_set_visible(V, p, NULL);  /* cannot happen in vanilla flows (half = shared stack 0); safe */
    else if (bit(m->seen, i)) orig(V, p, mi);
    else not_visited(V, p);
}
static void h_set_half(Obj V, V2I p, void *mi) { half_like(o_set_half, V, p, mi); }
static void h_load_visible(Obj V, V2I p, void *mi) { half_like(o_load_visible, V, p, mi); }

/* ------------------------------------------------------------------ enemies (units hidden in my fog) */
/* EntityObjectPresenter shows/hides units through EntityObjectView.SetVisible(guid, bool). Player units (faction 1) and
   subfaction 2 are always shown in vanilla: passed through. Other units: visible only if vanilla says so AND my_vis
   has their tile. Each vanilla wish is remembered so a redraw (owner change, rule change) can re-decide it. */
typedef struct { Guid g; Obj view; int map, scene, want, shown, used; } EntRec;
#define NENT 2048
static EntRec E[NENT];

static Obj dict_guid_get(Obj d, Guid g, int voff) {    /* Dictionary<Guid, X>, stride 0x20, value at voff */
    if (!d) return NULL;
    Obj ent = AT(d, 0x18, Obj); int n = AT(d, 0x20, int32_t);
    if (n < 0 || (n && (!ent || n > arr_len(ent)))) return NULL;
    for (int i = 0; i < n; i++) {
        char *e = (char *)ent + 0x20 + (int64_t)i * 0x20;
        if (*(int32_t *)e >= 0 && !memcmp(e + 8, &g, 16)) return (Obj)(e + voff);
    }
    return NULL;
}
static Obj unit_of(int map, Guid g) { Obj *p = (Obj *)dict_guid_get(units_dict(map), g, 0x18); return p ? *p : NULL; }
static int unit_tile(int map, Guid g, V2I *t) {        /* VectorExtensions.ToLogicalPosition: Math.Round(x - 0.5), half to even */
    float *v = (float *)dict_guid_get(pos_dict(map), g, 0x18);
    if (!v) return 0;
    t->x = (int)rint((double)(v[0] - 0.5f)); t->y = (int)rint((double)(v[1] - 0.5f));
    return 1;
}
static int always_shown(Obj u) {                        /* UnitEntity.get_Faction == 1 || get_SubFaction == 2 (fields read) */
    Obj a = AT(u, 0x40, Obj), b = a ? AT(a, 0x10, Obj) : NULL, s = AT(u, 0x50, Obj);
    int fac = b ? AT(b, 0x18, int32_t) : -1, sub = s ? AT(s, 0x34, int32_t) : -1;
    return fac == 1 || sub == 2 || fac < 0;            /* unreadable: leave vanilla */
}
static EntRec *ent_rec(Guid g, int make) {
    int h = (int)((g.lo ^ g.hi ^ (g.hi >> 29)) % NENT), freei = -1;
    for (int k = 0; k < NENT; k++) {
        EntRec *r = &E[(h + k) % NENT];
        if (r->used && !memcmp(&r->g, &g, 16)) return r;
        if (!r->used) { if (freei < 0) freei = (h + k) % NENT; break; }
    }
    if (!make) return NULL;
    if (freei < 0) { memset(E, 0, sizeof E); freei = h; }   /* full: forget all (only redraw precision is lost) */
    EntRec *r = &E[freei]; memset(r, 0, sizeof *r); r->g = g; r->used = 1;
    return r;
}
static int decide(int map, Guid g, int want) {         /* 1 = show */
    if (!want || !filtering()) return want;
    MapFog *m = mapfog(map); V2I t;
    if (world_changing() || !m || !m->vh || m->vh_scene != scene()) return want;
    if (m->built_frame != rig_frames && rebuild(map, m->vh) < 0) return want;
    if (!unit_tile(map, g, &t)) return want;
    int i = idx(t);
    if (i < 0) { range_note(t); return want; }
    return bit(m->vis, i);
}
static void h_eov_set_visible(Obj V, Guid g, bool vis, void *mi) {
    int map;
    if (!tracking() || (map = cur_map()) < 0) { o_eov_set_visible(V, g, vis, mi); return; }
    Obj u = unit_of(map, g);
    if (!u || always_shown(u)) { o_eov_set_visible(V, g, vis, mi); return; }
    EntRec *r = ent_rec(g, 1);
    int show = decide(map, g, vis ? 1 : 0);
    r->view = V; r->map = map; r->scene = scene(); r->want = vis ? 1 : 0; r->shown = show;
    o_eov_set_visible(V, g, show ? true : false, mi);
}
static void h_eop_refresh_hide(Obj P, Obj d, void *mi) {   /* fresh mask before the presenter re-decides units */
    note_main();
    if (P && tracking()) {
        Obj rt = AT(P, 0x40, Obj);                      /* EntityObjectPresenter._runtimeData (RuntimeMapData) */
        int map = AT(P, 0x20, int32_t);                 /* EntityObjectPresenter._targetMap */
        if (rt && AT(rt, 0x10, int32_t) == map) rebuild(map, vh_of_runtime(rt));   /* RuntimeMapData.MapType must match */
    }
    o_eop_refresh_hide(P, d, mi);
}

/* ------------------------------------------------------------------ redraw */
static void redraw_entities(int map) {
    int sc = scene();
    for (int k = 0; k < NENT; k++) {
        EntRec *r = &E[k];
        if (!r->used || r->map != map || r->scene != sc || !r->want) continue;
        int show = decide(map, r->g, 1);
        if (show != r->shown) { r->shown = show; o_eov_set_visible(r->view, r->g, show ? true : false, NULL); }
    }
}
static void redraw_now(void) {
    int map = cur_map();
    if (map < 0 || busy() || mp_game_load_pending() || scene() != 2) return;   /* keep pending until the campaign runs */
    g_pending = 0;
    Obj P = presenter_of(map);
    if (P) {
        MapFog *m = mapfog(map);
        if (m && !g_bad && tracking() && rebuild(map, presenter_vh(P)) == 0) {
            int ctx = m->seeded ? CTX_REDRAW : CTX_LOAD;  /* never loaded under a session: seed my_seen now */
            if (!m->seeded) { memset(m->seen, 0, WORDS * 8); m->seeded = 1; }
            run_ctx(ctx, map, o_set_default, P, NULL, NULL);
        } else o_set_default(P, NULL, NULL);              /* vanilla redraw */
    }
    if (g_main_known) redraw_entities(map);
}
static void want_redraw(void) {
    g_pending = 1;
    if (on_main()) redraw_now();
}
/* CTX_REDRAW: same as a refresh, but LoadVisible must not reseed: half_like treats it as a normal explored check.
   (half_like seeds only in CTX_LOAD; in CTX_REDRAW it filters like CTX_REFRESH.) */

/* ------------------------------------------------------------------ API */
void mp_fog_set_shared(int on) {
    on = on ? 1 : 0;
    if (on == g_shared) return;
    g_shared = on;
    rig_log("fog: shared fog of war %s", on ? "on (vanilla)" : "off (per player)");
    want_redraw();
}
void mp_fog_owner_changed(void) { if (!g_shared && tracking()) want_redraw(); }
void mp_fog_frame(void) { note_main(); if (g_pending) redraw_now(); }   /* main thread, once per frame (rig_frame) */

void mp_fog_install(void) {
    int bad = 0;
    bad |= rig_hook(VA_FOGP_SET_DEFAULT, (void *)h_set_default, (void **)&o_set_default, "FogOfWarPresenter.SetDefaultVision");
    bad |= rig_hook(VA_FOGP_REFRESH_VISION, (void *)h_refresh_vision, (void **)&o_refresh_vision, "FogOfWarPresenter.RefreshVision");
    bad |= rig_hook(VA_FOGP_REFRESH_IMPL, (void *)h_refresh_impl, (void **)&o_refresh_impl, "FogOfWarPresenter.RefreshImplementedArea");
    bad |= rig_hook(VA_FOGV_SET_VISIBLE, (void *)h_set_visible, (void **)&o_set_visible, "FogOfWarView.SetVisible");
    bad |= rig_hook(VA_FOGV_SET_HALF, (void *)h_set_half, (void **)&o_set_half, "FogOfWarView.SetHalfVisible");
#ifndef _WIN32   /* Windows: LoadVisible and SetHalfVisible are one body; the SetHalfVisible hook covers both */
    bad |= rig_hook(VA_FOGV_LOAD_VISIBLE, (void *)h_load_visible, (void **)&o_load_visible, "FogOfWarView.LoadVisible");
#else
    o_load_visible = o_set_half;
#endif
    if (bad) {   /* a leaf without its presenter (or the reverse) would show wrong tiles: run vanilla */
        g_bad = 1; rig_log("fog: a fog hook failed: per-player fog OFF (vanilla)");
    }
    int ebad = 0;
    ebad |= rig_hook(VA_EOV_SET_VISIBLE, (void *)h_eov_set_visible, (void **)&o_eov_set_visible, "EntityObjectView.SetVisible");
    ebad |= rig_hook(VA_EOP_REFRESH_HIDE, (void *)h_eop_refresh_hide, (void **)&o_eop_refresh_hide, "EntityObjectPresenter.OnRefreshHideVision");
    if (ebad) rig_log("fog: an entity hook failed: enemies use vanilla visibility");
    rig_log("fog: installed (%s)", bad ? "inactive" : "per-player fog available");
}
