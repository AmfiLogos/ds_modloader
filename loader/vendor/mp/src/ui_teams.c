// ui_teams: the Teams screen (owner 2026-10-12, "like Super Smash Bros"). Dylib only, build 25846389 arm64.
// One column per player (colour, name, ready state) with all his units (bench, any number), a "Free units"
// column, a Ready button and a Close button, over a dark backdrop. Built from clones of stock widgets:
//   - portraits: Instantiate(PlayerUnitShortCutView._playerUnitShortCutPrefab +0x38) + SubUI_UnitShortCutParts
//     .Initialize(unit, null, null) 0xF1A67C; the component is then disabled (no stock click / drag / drop:
//     clicks are hit-tested here with RectTransformUtility); "marked for the run" = stock SetSelected 0xF1AF50.
//   - texts: clones of the portrait's _unitName (DASText +0x138); backdrop: clone of its _backgroundImage +0x1D8.
// No hooks. Rules: ui_teams_model.c (tested in ui_test/teams_test.c). Notes: TEAMS_NOTES.md.
#include "ui_teams_model.h"
#include "ui_model.h"
#include "ui_mp.h"
#include "rig.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>

/* ---------------------------------------------------------------- what we need from other lanes (weak) */
#ifndef DSNET_EXPEDITION_READY
#define DSNET_EXPEDITION_READY TM_READY_TYPE
#endif
uint32_t dsnet_ready_mask(dsnet_client *c) __attribute__((weak_import));
int      dsnet_marked_of(dsnet_client *c, int player, uint8_t out[3][16]) __attribute__((weak_import));
int      dsnet_expedition_ready(dsnet_client *c, int ready, const uint8_t (*guids)[16], int n) __attribute__((weak_import));
void    *ord_debug_class(const char *ns, const char *name) __attribute__((weak_import));
void     ui_portrait_style(void *parts, int owner) __attribute__((weak_import));   /* mpui: colour frame / dim */

/* ---------------------------------------------------------------- il2cpp / Unity */
typedef struct { float x, y; } V2;
typedef struct { float x, y, z; } V3;
typedef struct { float x, y, w, h; } R4;
typedef struct { float r, g, b, a; } C4;
static inline Obj u_str(const char *s) { return F(0x6F9738, Obj (*)(const char *))(s); }
static inline uintptr_t u_gc_new(Obj o) { return o ? F(0x6F9540, uintptr_t (*)(Obj, bool))(o, false) : 0; }
static inline Obj u_gc_get(uintptr_t h) { return h ? F(0x6F9598, Obj (*)(uintptr_t))(h) : NULL; }
static inline void u_gc_free(uintptr_t h) { if (h) F(0x6F95E4, void (*)(uintptr_t))(h); }
static inline bool u_alive(Obj o) { return o && F(0x50FAF88, bool (*)(Obj, void *))(o, NULL); }
/* Checked calls (lead 2026-10-13, after a 15:45 crash in ui_game: Graphic.set_color on a Transform): every typed Unity
   call goes through tm_is(), which checks the object's class (il2cpp_object_get_class 0x6F96C0 +
   il2cpp_class_is_assignable_from 0x6F8BE0, as ui_is in ui_game.c); a mismatch is logged once per (kind, class)
   and the call is skipped (default result). */
enum { K_RT, K_TR, K_TMP, K_FA, K_BEH, K_COMP, K_GO, K_GR, K_IMG, K_CG, K_CV, K_CAM, K_PP, K_N };
static int tm_isq(Obj o, int k, int quiet);
static int tm_is(Obj o, int k) { return tm_isq(o, k, 0); }
static int tm_isq(Obj o, int k, int quiet) {
    static const char *const ns[K_N] = { "UnityEngine", "UnityEngine", "TMPro", "TMPro", "UnityEngine", "UnityEngine", "UnityEngine",
                                         "UnityEngine.UI", "UnityEngine.UI", "UnityEngine", "UnityEngine", "UnityEngine", "Refactor.UI" };
    static const char *const nm[K_N] = { "RectTransform", "Transform", "TMP_Text", "TMP_FontAsset", "Behaviour", "Component", "GameObject",
                                         "Graphic", "Image", "CanvasGroup", "Canvas", "Camera", "SubUI_PolicyPortrait" };
    static void *cls[K_N]; static int missing[K_N];
    if (!u_alive(o) || k < 0 || k >= K_N) return 0;
    if (!cls[k]) {
        cls[k] = ord_debug_class ? ord_debug_class(ns[k], nm[k]) : NULL;
        if (!cls[k]) { if (!missing[k]++) rig_log("teams: class %s.%s not found: its calls are not type-checked", ns[k], nm[k]); return 1; }
    }
    void *oc = F(0x6F96C0, void *(*)(Obj))(o);                                  /* il2cpp_object_get_class */
    if (oc && F(0x6F8BE0, bool (*)(void *, void *))(cls[k], oc)) return 1;      /* il2cpp_class_is_assignable_from */
    if (quiet) return 0;
    static struct { int k; void *c; } seen[32]; static int nseen;
    int known = 0;
    for (int i = 0; i < nseen && !known; i++) known = seen[i].k == k && seen[i].c == oc;
    if (!known) {
        if (nseen < 32) { seen[nseen].k = k; seen[nseen].c = oc; nseen++; }
        rig_log("teams: TYPE CHECK: a %s.%s call was skipped on a %s", ns[k], nm[k], oc ? *(const char **)((char *)oc + 0x10) : "?");
    }
    return 0;
}
static inline Obj u_go(Obj comp) { return (tm_is(comp, K_COMP) ? F(0x50ED970, Obj (*)(Obj, void *))(comp, NULL) : NULL); }
static inline Obj u_tr(Obj comp) {                          /* a component's transform; a GameObject (prefabs) works too */
    if (tm_isq(comp, K_GO, 1)) return F(0x50F300C, Obj (*)(Obj, void *))(comp, NULL);
    return (tm_is(comp, K_COMP) ? F(0x50ED864, Obj (*)(Obj, void *))(comp, NULL) : NULL);
}
static inline Obj u_go_tr(Obj go) { return (tm_is(go, K_GO) ? F(0x50F300C, Obj (*)(Obj, void *))(go, NULL) : NULL); }
static inline void u_go_active(Obj go, bool v) { if (go) (tm_is(go, K_GO) ? F(0x50F3520, void (*)(Obj, bool, void *))(go, v, NULL) : (void)0); }
static inline bool u_go_active_self(Obj go) { return go && (tm_is(go, K_GO) ? F(0x50F3638, bool (*)(Obj, void *))(go, NULL) : false); }
static inline void u_active(Obj comp, bool v) { if (u_alive(comp)) u_go_active(u_go(comp), v); }
static inline Obj u_instantiate(Obj orig, Obj parent_tr) { return F(0x50FC30C, Obj (*)(Obj, Obj, void *))(orig, parent_tr, NULL); }
static inline void u_destroy(Obj o) { if (u_alive(o)) F(0x50FC8F8, void (*)(Obj, void *))(o, NULL); }
static inline Obj u_tr_parent(Obj t) { return (tm_is(t, K_TR) ? F(0x510992C, Obj (*)(Obj, void *))(t, NULL) : NULL); }
static inline void u_last_sibling(Obj t) { (tm_is(t, K_TR) ? F(0x510ED6C, void (*)(Obj, void *))(t, NULL) : (void)0); }
static inline void u_enabled(Obj beh, bool v) { (tm_is(beh, K_BEH) ? F(0x50ECEA0, void (*)(Obj, bool, void *))(beh, v, NULL) : (void)0); }
static inline void rt_anchor(Obj rt, float x0, float y0, float x1, float y1) {
    (tm_is(rt, K_RT) ? F(0x5106B64, void (*)(Obj, V2, void *))(rt, (V2){x0, y0}, NULL) : (void)0);
    (tm_is(rt, K_RT) ? F(0x5106D48, void (*)(Obj, V2, void *))(rt, (V2){x1, y1}, NULL) : (void)0);
}
static inline void rt_pivot(Obj rt, float x, float y) { (tm_is(rt, K_RT) ? F(0x51072F4, void (*)(Obj, V2, void *))(rt, (V2){x, y}, NULL) : (void)0); }
static inline void rt_pos(Obj rt, float x, float y) { (tm_is(rt, K_RT) ? F(0x5106F2C, void (*)(Obj, V2, void *))(rt, (V2){x, y}, NULL) : (void)0); }
static inline void rt_size(Obj rt, float w, float h) { (tm_is(rt, K_RT) ? F(0x5107110, void (*)(Obj, V2, void *))(rt, (V2){w, h}, NULL) : (void)0); }
static inline R4 rt_rect(Obj rt) { return (tm_is(rt, K_RT) ? F(0x5106978, R4 (*)(Obj, void *))(rt, NULL) : (R4){0, 0, 0, 0}); }
static inline V2 rt_get_pos(Obj rt) { return (tm_is(rt, K_RT) ? F(0x5106E38, V2 (*)(Obj, void *))(rt, NULL) : (V2){0, 0}); }
static inline void tr_scale(Obj t, float s) { (tm_is(t, K_TR) ? F(0x510AFF8, void (*)(Obj, V3, void *))(t, (V3){s, s, 1.0f}, NULL) : (void)0); }
static inline void g_color(Obj g, float r, float gg, float b, float a) { (tm_is(g, K_GR) ? F(0x52602F8, void (*)(Obj, C4, void *))(g, (C4){r, gg, b, a}, NULL) : (void)0); }
static inline void g_raycast(Obj g, bool v) { (tm_is(g, K_GR) ? F(0x5260348, void (*)(Obj, bool, void *))(g, v, NULL) : (void)0); }
static inline void img_sprite(Obj img, Obj spr) { (tm_is(img, K_IMG) ? F(0x5257600, void (*)(Obj, Obj, void *))(img, spr, NULL) : (void)0); }
static inline Obj go_component(Obj go, Obj type) { return (tm_is(go, K_GO) ? F(0x50EDAA8, Obj (*)(Obj, Obj, void *))(go, type, NULL) : NULL); }
static Obj tmp_get(Obj t) {                                  /* TMP_Text.text: vtable slots 65 / 66 */
    if (!tm_is(t, K_TMP)) return NULL;
    void *k = *(void **)t;
    return (*(Obj (**)(Obj, void *))((char *)k + 0x548))(t, *(void **)((char *)k + 0x550));
}
static void tmp_set(Obj t, const char *s) {
    if (!tm_is(t, K_TMP)) return;
    void *k = *(void **)t;
    (*(void (**)(Obj, Obj, void *))((char *)k + 0x558))(t, u_str(s), *(void **)((char *)k + 0x560));
}
static void str_utf8(Obj s, char *out, size_t cap) {         /* il2cpp String -> UTF-8 (BMP is enough here) */
    size_t n = 0;
    if (cap) out[0] = 0;
    if (!s || !cap) return;
    int32_t len = AT(s, 0x10, int32_t);
    const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    for (int32_t i = 0; i < len && n + 4 < cap; i++) {
        uint32_t u = c[i];
        if (u < 0x80) out[n++] = (char)u;
        else if (u < 0x800) { out[n++] = (char)(0xC0 | u >> 6); out[n++] = (char)(0x80 | (u & 63)); }
        else { out[n++] = (char)(0xE0 | u >> 12); out[n++] = (char)(0x80 | (u >> 6 & 63)); out[n++] = (char)(0x80 | (u & 63)); }
    }
    out[n] = 0;
}
static Obj type_object(const char *ns, const char *name) {
    void *k = ord_debug_class ? ord_debug_class(ns, name) : NULL;
    if (!k) return NULL;
    return F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k));   /* il2cpp_type_get_object(class_get_type) */
}
static Obj find_view(uintptr_t *h, const char *ns, const char *name) {   /* Object.FindObjectOfType(Type, inactive) */
    Obj v = u_gc_get(*h);
    if (u_alive(v)) return v;
    Obj t = type_object(ns, name);
    if (!t) return NULL;
    v = F(0x50FD97C, Obj (*)(Obj, bool, void *))(t, false, NULL);
    if (!u_alive(v)) v = F(0x50FD97C, Obj (*)(Obj, bool, void *))(t, true, NULL);
    u_gc_free(*h); *h = u_gc_new(v);
    return u_alive(v) ? v : NULL;
}

/* ---------------------------------------------------------------- session */
static mp_session *sess(void) { return mp_game_session ? mp_game_session() : NULL; }
static dsnet_client *cli(void) { mp_session *s = sess(); return s ? mp_client(s) : NULL; }
static int me(void) { dsnet_client *c = cli(); return c ? dsnet_player_id(c) : -1; }
static int spect(void) { mp_session *s = sess(); return s && mp_is_spectator(s); }   /* watches: Teams read only, no proposal */
static uintptr_t h_help;
static void notice(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void notice(const char *fmt, ...) {
    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    rig_log("teams: %s", b);
    Obj v = find_view(&h_help, "Refactor", "HelpNoticeView");
    if (v) F(0xFDC9FC, void (*)(Obj, Obj, void *))(v, u_str(b), NULL);   /* HelpNoticeView.Show */
}
static void player_name(int p, char out[40]) {
    dsnet_client *c = cli();
    const char *n = c && dsnet_player_name ? dsnet_player_name(c, p) : NULL;
    snprintf(out, 40, "%s", n ? n : "");
}

/* Orders (claims, marks, picks) only take effect once the lockstep session runs (det lock on): until then the
   screen says so instead of failing silently (owner live test 2026-10-13). */
static int mp_running(void) { return me() >= 0 && rig_det_locked(); }

/* ---------------------------------------------------------------- model from the game + dsnet */
#define UNIT_GUID 0x24
static int unit_faction(Obj u) { Obj r = AT(u, 0x40, Obj); Obj c = r ? AT(r, 0x10, Obj) : NULL; return c ? AT(c, 0x18, int32_t) : -1; }
static tm_state st;                                          /* rebuilt by model_refresh */
static Obj st_unit[TM_MAX_UNITS];                            /* this frame only (no GC handle) */
static int st_map[TM_MAX_UNITS];                             /* 0 settlement, 1 dungeon (the current run) */
static int local_marks_n; static uint8_t local_marks[TM_MAX_MARK][16];   /* fallback while dsnet has no marks */
static int have_ready_api(void) { return dsnet_ready_mask && dsnet_marked_of && dsnet_expedition_ready; }
int dsnet_units_per_player(dsnet_client *c) __attribute__((weak_import));
int dsnet_player_net(dsnet_client *c, int player, int *ping_ms, int *ticks_behind, int *state) __attribute__((weak_import));
int dsnet_marked_list(dsnet_client *c, int player, uint8_t (*out)[16], int max) __attribute__((weak_import));
static int marks_of(dsnet_client *c, int p, uint8_t (*out)[16]) {   /* up to the host rule (dsnet_marked_list: up to 6) */
    int n = dsnet_marked_list ? dsnet_marked_list(c, p, out, TM_MAX_MARK) : dsnet_marked_of(c, p, (uint8_t (*)[16])out);
    return n > 0 ? (n > TM_MAX_MARK ? TM_MAX_MARK : n) : 0;
}

static int admin_mode(void);
static void model_refresh(void) {
    dsnet_client *c = cli();
    memset(&st, 0, sizeof st);
    st.me = me();
    st.in_dungeon = cur_map() == 1;
    st.admin = admin_mode();
    st.max_mark = c && dsnet_units_per_player ? dsnet_units_per_player(c) : 0;
    st.present = c && dsnet_present_mask ? dsnet_present_mask(c) : (st.me >= 0 ? 1u << st.me : 0);
    for (int map = 0; map <= 1; map++) {                     /* base first (stock order), then the dungeon party */
        Obj ud = units_dict(map);
        if (!ud) continue;
        Obj ent = AT(ud, 0x18, Obj); int n = AT(ud, 0x20, int32_t);
        for (int i = 0; ent && i < n && st.nunits < TM_MAX_UNITS; i++) {
            char *e = (char *)ent + 0x20 + i * 0x20;
            if (*(int32_t *)e < 0) continue;
            Obj u = *(Obj *)(e + 0x18);
            if (!u || unit_faction(u) != 1 || unit_dead(u)) continue;
            int dup = 0;
            for (int k = 0; k < st.nunits; k++) if (!memcmp(st.guid[k], e + 8, 16)) dup = 1;
            if (dup) continue;
            memcpy(st.guid[st.nunits], e + 8, 16);
            st.owner[st.nunits] = c ? dsnet_owner_of(c, (const uint8_t *)e + 8) : TM_FREE;   /* slot, -1 free, -2 reserved */
            if (st.owner[st.nunits] < TM_RESERVED) st.owner[st.nunits] = TM_FREE;
            st_map[st.nunits] = map;
            st_unit[st.nunits++] = u;
        }
    }
    if (c && have_ready_api()) {
        st.ready = dsnet_ready_mask(c);
        for (int p = 0; p < TM_MAX_PLAYERS; p++) {
            st.nmarked[p] = marks_of(c, p, st.marked[p]);
        }
    } else if (st.me >= 0) {
        st.nmarked[st.me] = local_marks_n;
        memcpy(st.marked[st.me], local_marks, sizeof local_marks);
    }
}

/* ---------------------------------------------------------------- regions (the proposer picks: stock panel, or the clan's last) */
static const char *region_name(int r) {
    static const char *n[] = { "Veil of Silence", "Festering Burrow", "Abandoned Mine", "Forgotten Abyss", "Dead Swamp",
                               "Tower of Apocalypse", "Abyss" };
    return r >= 0 && r < (int)(sizeof n / sizeof n[0]) ? n[r] : "(none)";
}
static Obj clan(void) { Obj c = cdc(); return c ? AT(c, 0x50, Obj) : NULL; }
static int avail_regions(int *out, int max) {               /* ClanDataContainer.GetAvailableExpeditionRegions */
    Obj cl = clan();
    Obj l = cl ? F(0xFABC40, Obj (*)(Obj, void *))(cl, NULL) : NULL;
    if (!l) return 0;
    Obj arr = AT(l, 0x10, Obj); int n = AT(l, 0x18, int32_t), k = 0;
    for (int i = 0; arr && i < n && k < max; i++) { int r = AT(arr, 0x20 + 4 * i, int32_t); if (r >= 0 && r < 7) out[k++] = r; }
    return k;
}
/* the clan's last chosen region if it is still open, else the first open region (fresh world) */
static int region_default(void) {
    int av[8], n = avail_regions(av, 8); Obj cl = clan();
    int r = cl ? AT(cl, 0x50, int32_t) : -1;
    for (int i = 0; i < n; i++) if (av[i] == r) return r;
    return n ? av[0] : (r >= 0 && r < 7 ? r : 0);
}
/* ---------------------------------------------------------------- expedition proposal (owner 2026-10-13, replaces the vote)
   Rules and orders: ui_teams_model.h (tm_prop_*). Approval = Ready (dsnet); dsnet starts the run when all are ready. */
static tm_prop prop;
static mp_session *prop_sess;
static double prop_t0;                                       /* local clock at the start (host: the 60 s timeout) */
static int prop_timeout_sent, start_region = -1;
static char prop_event[200];                                 /* the last "proposal ended" message (state / tests) */
static int prop_folded;                                      /* the side panel shows only its title and one line */
/* this player's step in the open proposal (owner 2026-10-13): the receivers first get the stock supplies step
   (titled "Expedition Proposal", their own going units), Next -> the summary; the proposer goes to the summary */
enum { ST_NONE, ST_SUPPLIES_PENDING, ST_SUPPLIES, ST_SUMMARY };
static int pstage, close_stock, close_teams;
/* results and proposal messages: the game's big centred message (ui_game.c ui_center_message, HelpNoticeView) */
static void center_msg(const char *t) {
    static void (*fn)(const char *); static unsigned tries;
    if (!fn && tries++ % 50 == 0) fn = (void (*)(const char *))dlsym(RTLD_DEFAULT, "ui_center_message");
    rig_log("teams: message: %s", t);
    if (fn) fn(t); else notice("%s", t);
}
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec / 1e9; }
static uint32_t present_now(void) { dsnet_client *c = cli(); return c && dsnet_present_mask ? dsnet_present_mask(c) : 0; }
static void send_marks(int ready, const uint8_t (*marks)[16], int n);
static void pname(int p, char out[40]) { player_name(p, out); if (!out[0]) snprintf(out, 40, p == 0 ? "the host" : "Player %d", p + 1); }
static void prop_end(tm_pevent e, int player, uint32_t mask) {
    char nm[40]; pname(player, nm);
    if (e == TM_PE_DECLINED) snprintf(prop_event, sizeof prop_event, "%s declined the expedition", nm);
    else if (e == TM_PE_CANCELLED) snprintf(prop_event, sizeof prop_event, "%s cancelled the expedition", nm);
    else if (e == TM_PE_PROPOSER_LEFT) snprintf(prop_event, sizeof prop_event, "%s left: the expedition proposal is cancelled", nm);
    else if (e == TM_PE_TIMEOUT) {
        size_t l = (size_t)snprintf(prop_event, sizeof prop_event, "No answer from");
        for (int p = 0; p < TM_MAX_PLAYERS && l < sizeof prop_event - 44; p++)
            if (mask >> p & 1u) { char n2[40]; pname(p, n2); l += (size_t)snprintf(prop_event + l, sizeof prop_event - l, "%s %s", l > 14 ? "," : "", n2); }
        snprintf(prop_event + l, sizeof prop_event - l, ": expedition cancelled");
    }
    center_msg(prop_event);
    if (pstage == ST_SUPPLIES) close_stock = 1;
    pstage = ST_NONE;
    /* whoever approved takes it back (ready 0, marks kept) */
    dsnet_client *c = cli(); int m = me();
    if (c && m >= 0 && have_ready_api() && (dsnet_ready_mask(c) >> m & 1u)) {
        uint8_t mk[TM_MAX_MARK][16]; int n = marks_of(c, m, mk);
        send_marks(0, (const uint8_t (*)[16])mk, n);
    }
}
/* the open proposal's units, all players (PROPOSE args: i32 region, u8 n, n x guid) */
static uint8_t prop_party[TM_PARTY_MAXN][16];
static int prop_np;
static int propose_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)guid;
    if (n < 5) return -1;
    int32_t r; memcpy(&r, a, 4);
    int k = a[4];
    if (r < 0 || r >= 7 || k > TM_PARTY_MAXN || n < 5 + 16 * k) return -1;
    tm_pevent e = tm_prop_propose(&prop, player, r, present_now());
    if (e) { prop_np = k; memcpy(prop_party, a + 5, (size_t)k * 16); }
    if (e == TM_PE_STARTED) { prop_t0 = now_s(); prop_timeout_sent = 0; prop_event[0] = 0; prop_folded = 0; pstage = player == me() ? ST_SUMMARY : ST_SUPPLIES_PENDING; close_teams = 1; }
    if (e == TM_PE_STARTED && spect()) {                     /* a spectator approves nothing: no supplies, no summary panel */
        pstage = ST_NONE; close_teams = 0;
        char pn_[40]; pname(player, pn_); char m[160]; snprintf(m, sizeof m, "%s proposes an expedition to %s", pn_, region_name(r)); center_msg(m);
    }
    rig_log("teams: proposal from player %d, region %d (%s): %s", player, r, region_name(r),
            e == TM_PE_STARTED ? "started" : e == TM_PE_UPDATED ? "region updated" : "ignored (another proposal is open)");
    return 0;
}
static int answer_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)guid;
    if (n < 8) return -1;
    int32_t kind; uint32_t mask; memcpy(&kind, a, 4); memcpy(&mask, a + 4, 4);
    tm_pevent e = tm_prop_answer(&prop, player, kind, present_now());
    rig_log("teams: proposal answer %d from player %d (mask %x): %s", kind, player, mask, e ? "ends the proposal" : "ignored");
    if (e) prop_end(e, player, mask);
    return 0;
}
unsigned mp_game_session_gen(void);                          /* mp_game.c: +1 per started session */
static void prop_session(void) {                             /* a rejoin's session can reuse the freed address: compare the generation too */
    mp_session *s = sess();
    static unsigned gen_seen; unsigned g = mp_game_session_gen();
    if (s == prop_sess && g == gen_seen) return;
    prop_sess = s; gen_seen = g; memset(&prop, 0, sizeof prop);
    if (s) { mp_set_order_handler(s, TM_ORDER_PROPOSE, propose_order); mp_set_order_handler(s, TM_ORDER_ANSWER, answer_order); }
}
static int send_order(int type, const void *a, uint16_t n) {
    mp_session *s = sess(); static const uint8_t zero[16];
    return !s || mp_order(s, (uint16_t)type, zero, a, n) ? -1 : 0;
}
static void send_answer(int kind, uint32_t mask) {
    uint8_t b[8]; int32_t k = kind; memcpy(b, &k, 4); memcpy(b + 4, &mask, 4);
    if (send_order(TM_ORDER_ANSWER, b, 8)) rig_log("teams: proposal answer not sent");
}
/* unit u goes for player p in the open proposal (a free unit in it counts for the proposer) */
static int goes_for(int u, int p) {
    int in = 0; for (int i = 0; i < prop_np; i++) if (!memcmp(prop_party[i], st.guid[u], 16)) in = 1;
    return in && (st.owner[u] == p || (st.owner[u] == TM_FREE && p == prop.proposer));
}
/* my units in the open proposal (a free unit in it counts for the proposer) */
static int my_part(uint8_t (*out)[16]) { return st.me < 0 ? 0 : tm_party_of(&st, (const uint8_t (*)[16])prop_party, prop_np, st.me, prop.proposer, out); }
/* Propose (owner lan_v7: the party may hold allies' units; each owner approves his own): the stock panel's list ->
   claims for free units, the PROPOSE order, then Ready with my part (same sender, in order). 0 = sent. */
static int propose(int region, const uint8_t (*panel)[16], int np) {
    if (prop.active && prop.proposer != st.me) { char pn_[40]; pname(prop.proposer, pn_); notice("%s's proposal is open: approve or decline it first", pn_); return -1; }
    tm_prop_res r; tm_proposal_party(&st, panel, np, &r);
    if (r.err == 3) { notice("Choose at least one unit for the expedition"); return -1; }
    if (r.err == 1) { notice("At most %d units can go on an expedition (now %d)", TM_PARTY_CAP, r.n); return -1; }
    if (r.err == 2) { char nm[40]; pname(r.err_player, nm); notice("Max %d units per player: %s has more", tm_mark_limit(&st), nm); return -1; }
    mp_session *s = sess();
    for (int k = 0; s && k < r.nclaim; k++) mp_claim(s, r.claim[k]);
    uint8_t b[5 + 16 * TM_PARTY_MAXN]; int32_t rg = region >= 0 && region < 7 ? region : region_default();
    memcpy(b, &rg, 4); b[4] = (uint8_t)r.n; memcpy(b + 5, r.party, (size_t)r.n * 16);
    if (send_order(TM_ORDER_PROPOSE, b, (uint16_t)(5 + 16 * r.n))) { notice("The proposal could not be sent"); return -1; }
    uint8_t mine[TM_MAX_MARK][16]; int k = tm_party_of(&st, (const uint8_t (*)[16])r.party, r.n, st.me, st.me, mine);
    send_marks(1, (const uint8_t (*)[16])mine, k);
    rig_log("teams: proposal sent: region %d, %d units (%d mine, %d claimed, %d skipped)", rg, r.n, k, r.nclaim, r.skipped);
    return 0;
}
/* Approve: Ready with my units of the proposal (none = a viewer) */
static int approve(void) {
    if (!prop.active) return -1;
    uint8_t mine[TM_MAX_MARK][16]; int k = my_part(mine);
    send_marks(1, (const uint8_t (*)[16])mine, k);
    return 0;
}
static void prop_decline(void) {                             /* Decline (others) / Cancel (the proposer) */
    if (!prop.active) return;
    send_answer(prop.proposer == st.me ? TM_ANS_CANCEL : TM_ANS_DECLINE, 0);
}

/* ---------------------------------------------------------------- widgets (owner 2026-10-13: a real game window)
   Everything is cloned from the stock Task Planner popup PREFAB (PopUpType 13, TaskPriorityView), read from
   UIFactory._prefabs (+0x20) .GetUIPrefab 0xD3236C, so the game's own frame, title font (Cinzel-Bold SDF), body
   font (Lora-Regular SDF) and unit cards are used. Prefab hierarchy (read in the game, recon 2026-10-13):
     TaskPriorityUI [1920x1080]
       (BackgroundCloseButton [1920x1080]: added at run time, not in the prefab -> our dim root is a plain Image)
       Img_BackPanel [1320x888] Image(ContentsBoxNew)       -> our window (content children removed)
         CloseButton [32x32] Image Button                   -> our close (Button disabled, hit-tested)
         Txt_Title [960x54] TMP Cinzel-Bold 40              -> "Teams"
         PolicyPortraitHolder / SubUI_PolicyPortrait [92x108] -> unit cards (Refresh(UnitEntity) 0xE95EE8)
             Portrait / Txt_Name TMP Lora-Regular 14         -> body texts
   The root sits on the top canvas (rootCanvas of the HUD), with its own Canvas (sorting 32000) + raycaster. */
static uintptr_t h_shortcut, h_root, h_win, h_hud, h_title, h_ready, h_close, h_hint, h_region;
static uintptr_t h_ready_txt, h_region_txt, h_info, h_band, h_empty[3];
static uintptr_t h_tpl_box;
static uintptr_t h_tpl_red, h_tpl_bottom, h_tpl_band, h_spr_red, h_spr_bottom, h_spr_check;   /* Recruit / Expedition Plan parts */
static char region_line[300];                              /* the region vote as one line (state / tests) */
static int test_escape;
static uintptr_t h_tpl_card, h_tpl_text, h_tpl_close, h_tpl_bg, h_tpl_panel, h_card_type, h_cg_type;
static uintptr_t h_tpl_btn, h_spr_pop, h_font_title;        /* Recruit popup: framed top-line button + population icon; Cinzel-Bold SDF */
static const char *mark_glyph = "RUN";                      /* the check mark if the game's fonts have it (load_templates) */
static uintptr_t h_hdr[TM_MAX_PLAYERS + 2];
static int hdr_player[TM_MAX_PLAYERS + 2], nhdr;
static struct { uintptr_t card, mark; uint8_t guid[16]; int used, owner, clickable, marked; } por[TM_MAX_UNITS];
static int opened, built_scene = -1;
static tm_sel hsel = TM_SEL_INIT;                         /* host (admin mode): picked unit / target column */
static float card_w = 92, card_h = 108;
#define WIN_W 1320.0f
#define WIN_H 888.0f
enum { AL_LEFT = 513, AL_CENTER = 514, AL_RIGHT = 516 };   /* TMPro.TextAlignmentOptions */

int ui_teams_owns(void *parts) { (void)parts; return 0; }   /* cards are SubUI_PolicyPortrait now: never the bar's parts */
static Obj shortcut_view(void) { return find_view(&h_shortcut, "Refactor.UI", "PlayerUnitShortCutView"); }
static Obj tr_find(Obj t, const char *path) { return t ? (tm_is(t, K_TR) ? F(0x510F3C0, Obj (*)(Obj, Obj, void *))(t, u_str(path), NULL) : NULL) : NULL; }
static Obj comp_of(Obj any, uintptr_t *h, const char *ns, const char *name) {
    Obj ty = u_gc_get(*h);
    if (!ty) { ty = type_object(ns, name); *h = u_gc_new(ty); }
    return ty && any ? go_component(u_go(any), ty) : NULL;
}
static Obj tmp_of(Obj tr);
static Obj img_of(Obj tr);
static int load_templates(void) {
    if (u_alive(u_gc_get(h_tpl_card)) && u_alive(u_gc_get(h_tpl_panel))) return 0;
    uintptr_t hf = 0; Obj fac = find_view(&hf, "Refactor.Main.UI", "UIFactory");
    Obj so = fac ? AT(fac, 0x20, Obj) : NULL;
    Obj pf = so ? F(0xD3236C, Obj (*)(Obj, int, void *))(so, 13, NULL) : NULL;    /* PopUpType.TaskPriorityUI */
    u_gc_free(hf);
    if (!u_alive(pf)) { rig_log("teams: Task Planner prefab not found (UIFactory %p, prefabs %p)", fac, so); return -1; }
    Obj t = u_tr(pf);
    Obj panel = tr_find(t, "Img_BackPanel");
    Obj card = tr_find(panel, "PolicyPortraitHolder/SubUI_PolicyPortrait"), name = tr_find(card, "Portrait/Txt_Name");
    /* the instance's full-screen BackgroundCloseButton is added at run time (not in the prefab): the dim layer is
       a plain Image instead (the card's level box, sprite removed, stretched) */
    Obj bg = tr_find(card, "Portrait/Img_LevelCase");
    Obj close = tr_find(panel, "CloseButton");
    rig_log("teams: templates bg %p panel %p card %p name %p close %p", bg, panel, card, name, close);
    if (!bg || !panel || !card || !name || !close) return -1;
    u_gc_free(h_tpl_bg); h_tpl_bg = u_gc_new(bg); u_gc_free(h_tpl_panel); h_tpl_panel = u_gc_new(panel);
    u_gc_free(h_tpl_card); h_tpl_card = u_gc_new(card); u_gc_free(h_tpl_text); h_tpl_text = u_gc_new(name);
    u_gc_free(h_tpl_close); h_tpl_close = u_gc_new(close);
    R4 r = rt_rect(card); if (r.w > 20 && r.h > 20) { card_w = r.w; card_h = r.h; }
    /* no TMP <b> (owner 2026-10-13: it swaps in a font that is not the game's): labels use the title's Cinzel-Bold */
    Obj tt = tmp_of(tr_find(panel, "Txt_Title")), ft = u_alive(tt) ? (tm_is(tt, K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(tt, NULL) : NULL) : NULL;
    u_gc_free(h_font_title); h_font_title = u_gc_new(ft);
    Obj fb = (tm_is(tmp_of(name), K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(tmp_of(name), NULL) : NULL);
    int has_check = u_alive(fb) && (tm_is(fb, K_FA) ? F(0x4EEE580, bool (*)(Obj, uint16_t, bool, bool, void *))(fb, 0x2713, true, false, NULL) : false);   /* HasCharacter */
    mark_glyph = has_check ? "\u2713" : "RUN";
    /* HUD entry: the Recruit popup's (PopUpType 8) framed top-line button + its population icon (recon 2026-10-13:
       Img_BackGroundBox/Img_ExitButton [32x32] topLineButton_Frame1 Button, .../Img_PopulationFrame/Img_Population) */
    Obj rp = so ? F(0xD3236C, Obj (*)(Obj, int, void *))(so, 8, NULL) : NULL;
    Obj btn = u_alive(rp) ? tr_find(u_tr(rp), "Img_BackGroundBox/Img_ExitButton") : NULL;
    Obj pim = u_alive(rp) ? img_of(tr_find(u_tr(rp), "Img_BackGroundBox/Img_PopulationFrame/Img_Population")) : NULL;
    Obj pop = u_alive(pim) ? (tm_is(pim, K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(pim, NULL) : NULL) : NULL;              /* Image.sprite */
    u_gc_free(h_tpl_btn); h_tpl_btn = u_gc_new(u_alive(btn) && u_alive(pop) ? btn : NULL);
    u_gc_free(h_spr_pop); h_spr_pop = u_gc_new(u_alive(pop) ? pop : NULL);
    rig_log("teams: title font %p, check mark glyph %s, top-line button %p, population icon %p", ft, has_check ? "yes" : "no (RUN tag)", btn, pop);
    /* design pass (research/ux_design_2026-10-13.md): Recruit's red main button (Img_RecruitButton 232x40 + Txt_Recruit,
       Lora 18); Expedition Plan (PopUpType 2): the secondary BottomButton (BackButton 206x30 + BackButtonText, Lora 16),
       the region-title band (RegionTitleDecorate 556x44) and the CheckBox_Selected sign (10x10) */
    Obj red = u_alive(rp) ? tr_find(u_tr(rp), "Img_BackGroundBox/CandidateDetailPanel/Img_RecruitButton") : NULL;
    Obj box = u_alive(rp) ? tr_find(u_tr(rp), "Img_BackGroundBox") : NULL;           /* PopUpBox_Box: the proposal window */
    u_gc_free(h_tpl_box); h_tpl_box = u_gc_new(u_alive(box) ? box : NULL);
    Obj ep = so ? F(0xD3236C, Obj (*)(Obj, int, void *))(so, 2, NULL) : NULL;
    Obj et = u_alive(ep) ? u_tr(ep) : NULL;
    Obj bot = et ? tr_find(et, "Panel_BackGroundBox/LowerExpeditionControlPanel/BackButton") : NULL;
    Obj band = et ? tr_find(et, "Panel_BackGroundBox/Panel_MiddleExpedition/SubUI_SelectRegion/Panel_CurrentSelectedRegion/Img_CurrentRegionTitle") : NULL;
    Obj chk = et ? img_of(tr_find(et, "Panel_BackGroundBox/LowerExpeditionControlPanel/SelectRegionSign/Img_SelectRegionSign_Current")) : NULL;
    Obj sp_red = u_alive(red) ? (tm_is(img_of(red), K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(img_of(red), NULL) : NULL) : NULL;
    Obj sp_bot = u_alive(bot) ? (tm_is(img_of(bot), K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(img_of(bot), NULL) : NULL) : NULL;
    Obj sp_chk = u_alive(chk) ? (tm_is(chk, K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(chk, NULL) : NULL) : NULL;
    uintptr_t *hh[] = { &h_tpl_red, &h_tpl_bottom, &h_tpl_band, &h_spr_red, &h_spr_bottom, &h_spr_check };
    Obj vv[] = { red, bot, band, sp_red, sp_bot, sp_chk };
    for (int i = 0; i < 6; i++) { u_gc_free(*hh[i]); *hh[i] = u_gc_new(u_alive(vv[i]) ? vv[i] : NULL); }
    rig_log("teams: design parts: red button %p, bottom button %p, band %p, sprites %p %p %p", red, bot, band, sp_red, sp_bot, sp_chk);
    return 0;
}
static Obj tmp_of(Obj tr) { static uintptr_t h; return comp_of(tr, &h, "TMPro", "TMP_Text"); }
static Obj img_of(Obj tr) { static uintptr_t h; return comp_of(tr, &h, "UnityEngine.UI", "Image"); }
static void button_off(Obj tr) { static uintptr_t h; Obj b = comp_of(tr, &h, "UnityEngine.UI", "Button"); if (u_alive(b)) u_enabled(b, false); }
static void centre_anchor(Obj rt) { rt_anchor(rt, 0.5f, 0.5f, 0.5f, 0.5f); rt_pivot(rt, 0.5f, 0.5f); }
/* a body text (Lora) under parent: size w x h, font size, alignment */
static void tmp_plain(Obj t);
static Obj new_text(Obj parent_tr, float w, float h, float size, int align) {
    Obj tpl = u_gc_get(h_tpl_text);
    if (!u_alive(tpl) && load_templates()) return NULL;
    tpl = u_gc_get(h_tpl_text);
    Obj c = u_instantiate(tpl, parent_tr);
    Obj t = tmp_of(c);
    if (!u_alive(t)) return NULL;
    Obj rt = u_tr(t);
    centre_anchor(rt); rt_size(rt, w, h); tr_scale(rt, 1.0f);
    (tm_is(t, K_TMP) ? F(0x4F0B340, void (*)(Obj, float, void *))(t, size, NULL) : (void)0);          /* fontSize */
    (tm_is(t, K_TMP) ? F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, false, NULL) : (void)0);          /* no auto sizing */
    (tm_is(t, K_TMP) ? F(0x4F0B7BC, void (*)(Obj, int, void *))(t, align, NULL) : (void)0);           /* alignment */
    (tm_is(t, K_TMP) ? F(0x4F0BAB4, void (*)(Obj, bool, void *))(t, true, NULL) : (void)0);           /* word wrap */
    { static int logged; Obj m0 = (tm_is(t, K_TMP) ? F(0x4F0A6B4, Obj (*)(Obj, void *))(t, NULL) : NULL); char a_[80] = "", b_[80] = "";
      if (!logged && u_alive(m0)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(m0, NULL), a_, sizeof a_);
      tmp_plain(t);
      Obj m1 = (tm_is(t, K_TMP) ? F(0x4F0A6B4, Obj (*)(Obj, void *))(t, NULL) : NULL);
      if (!logged && u_alive(m1)) { str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(m1, NULL), b_, sizeof b_); rig_log("teams: text material %s -> %s", a_, b_); logged = 1; } }
    g_raycast(t, false);
    tmp_set(t, "");
    return t;
}
/* the font asset's own default material (TMP_Asset.m_Material +0x88): plain, no outline preset (owner 2026-10-13:
   the card-name material we cloned from has a black outline the game's windows do not) */
static void tmp_plain(Obj t) {
    if (!tm_is(t, K_TMP)) return;
    Obj f = F(0x4F0A59C, Obj (*)(Obj, void *))(t, NULL);
    Obj m = tm_is(f, K_FA) ? AT(f, 0x88, Obj) : NULL;
    if (!u_alive(m)) return;
    void *k = *(void **)t;                                    /* TMP_Text.fontSharedMaterial setter: vtable slot 68 */
    (*(void (**)(Obj, Obj, void *))((char *)k + 0x578))(t, m, *(void **)((char *)k + 0x580));
}
static void tmp_font(Obj t, Obj f) {                         /* TMP_Text.font, with that font's own material */
    if (u_alive(t) && u_alive(f)) (tm_is(t, K_TMP) ? F(0x4F0A5A4, void (*)(Obj, Obj, void *))(t, f, NULL) : (void)0);
    tmp_plain(t);
}
/* a plain rectangle (the level box clone: sprite and children removed) under parent, stretched between anchors */
static Obj new_rect(Obj parent_tr, float x0, float y0, float x1, float y1, float w, float h, C4 c) {
    Obj r = u_instantiate(u_gc_get(h_tpl_bg), parent_tr);
    if (!u_alive(r)) return NULL;
    int n = (tm_is(r, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(r, NULL) : 0);
    for (int i = 0; i < n; i++) u_go_active(u_go((tm_is(r, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(r, i, NULL) : NULL)), false);
    button_off(r);
    rt_anchor(r, x0, y0, x1, y1); rt_pivot(r, x0 == x1 ? x0 : 0.5f, y0 == y1 ? y0 : 0.5f); rt_size(r, w, h); rt_pos(r, 0, 0); tr_scale(r, 1.0f);
    Obj im = img_of(r);
    if (u_alive(im)) { img_sprite(im, NULL); g_color(im, c.r, c.g, c.b, c.a); g_raycast(im, false); }
    return im;
}
static Obj root(void) { Obj r = u_gc_get(h_root); return u_alive(r) ? r : NULL; }
static Obj win(void) { Obj w = u_gc_get(h_win); return u_alive(w) ? w : NULL; }
static void pool_free(void);
static void free_all(void) {
    Obj r = u_gc_get(h_root);
    if (u_alive(r)) u_destroy(u_go(r));                      /* children go with it */
    uintptr_t *hs[] = { &h_root, &h_win, &h_title, &h_ready, &h_close, &h_hint, &h_region, &h_ready_txt, &h_region_txt,
                        &h_info, &h_band, &h_empty[0], &h_empty[1], &h_empty[2] };
    for (size_t i = 0; i < sizeof hs / sizeof hs[0]; i++) { u_gc_free(*hs[i]); *hs[i] = 0; }
    for (int i = 0; i < nhdr; i++) { u_gc_free(h_hdr[i]); h_hdr[i] = 0; }
    nhdr = 0;
    for (int i = 0; i < TM_MAX_UNITS; i++) { u_gc_free(por[i].card); u_gc_free(por[i].mark); por[i].card = por[i].mark = 0; por[i].used = 0; }
    pool_free();
}
static Obj top_canvas_tr(void) {
    Obj v = shortcut_view(), cty = type_object("UnityEngine", "Canvas");
    if (!v || !cty) return NULL;
    for (Obj t = u_tr(v); t; t = u_tr_parent(t)) {
        Obj cv = go_component(u_go(t), cty);
        if (u_alive(cv)) { Obj top = (tm_is(cv, K_CV) ? F(0x5476FAC, Obj (*)(Obj, void *))(cv, NULL) : NULL); return u_alive(top) ? u_tr(top) : NULL; }
    }
    return NULL;
}
/* the popups' canvas (UIFactory._parent): the stock popup scale, not the HUD's (design #9) */
static Obj popup_parent_tr(void) {
    uintptr_t hf = 0; Obj fac = find_view(&hf, "Refactor.Main.UI", "UIFactory");
    Obj p = fac ? AT(fac, 0x28, Obj) : NULL;
    u_gc_free(hf);
    return u_alive(p) ? p : NULL;
}
static float canvas_scale(Obj tr) {
    Obj cty = type_object("UnityEngine", "Canvas");
    for (Obj t = tr; t && cty; t = u_tr_parent(t)) {
        Obj cv = go_component(u_go(t), cty);
        if (u_alive(cv)) { Obj top = (tm_is(cv, K_CV) ? F(0x5476FAC, Obj (*)(Obj, void *))(cv, NULL) : NULL); return u_alive(top) ? (tm_is(top, K_CV) ? F(0x5475418, float (*)(Obj, void *))(top, NULL) : 0.0f) : -1; }
    }
    return -1;
}
/* a clone of a stock button (Button off: hit-tested in click_at) under parent; *txt = its label */
static Obj new_button(uintptr_t tpl, Obj parent, const char *label_path, Obj *txt) {
    Obj t = u_gc_get(tpl);
    *txt = NULL;
    if (!u_alive(t)) return NULL;
    Obj b = u_instantiate(t, parent);
    if (!u_alive(b)) return NULL;
    button_off(b); centre_anchor(b); tr_scale(b, 1.0f);
    *txt = tmp_of(tr_find(b, label_path));
    if (u_alive(*txt)) { g_raycast(*txt, false); (tm_is(*txt, K_TMP) ? F(0x4F0B5DC, void (*)(Obj, bool, void *))(*txt, false, NULL) : (void)0); }
    Obj im = img_of(b);
    if (u_alive(im)) g_raycast(im, true);
    return im;
}
static int build_root(void) {
    if (load_templates()) return -1;
    free_all();                                              /* the HUD can be rebuilt (map change): drop dead handles */
    Obj hud_tr = top_canvas_tr(), pop_tr = popup_parent_tr();
    Obj canvas_tr = pop_tr ? pop_tr : hud_tr;
    if (!canvas_tr) { rig_log("teams: top canvas not found"); return -1; }
    rig_log("teams: canvas scale: HUD %.3f, popups %.3f (%s)", hud_tr ? canvas_scale(hud_tr) : -1, pop_tr ? canvas_scale(pop_tr) : -1, pop_tr ? "popup parent used" : "HUD canvas used");
    Obj rt = u_instantiate(u_gc_get(h_tpl_bg), canvas_tr);     /* full-screen click eater, no dimming (stock popups do not dim) */
    if (!u_alive(rt)) { rig_log("teams: backdrop clone failed"); return -1; }
    button_off(rt);
    rt_anchor(rt, 0, 0, 1, 1); rt_pivot(rt, 0.5f, 0.5f); rt_size(rt, 0, 0); rt_pos(rt, 0, 0); tr_scale(rt, 1.0f);
    int n0 = (tm_is(rt, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(rt, NULL) : 0);
    for (int i = 0; i < n0; i++) u_go_active(u_go((tm_is(rt, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(rt, i, NULL) : NULL)), false);
    Obj bgi = img_of(rt);
    if (u_alive(bgi)) { img_sprite(bgi, NULL); g_color(bgi, 0, 0, 0, 0); g_raycast(bgi, true); }
    u_last_sibling(rt);
    Obj cty = type_object("UnityEngine", "Canvas"), gty = type_object("UnityEngine.UI", "GraphicRaycaster");
    Obj cv = cty ? (tm_is(u_go(rt), K_GO) ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(rt), cty, NULL) : NULL) : NULL;   /* GameObject.AddComponent */
    if (u_alive(cv)) { (tm_is(cv, K_CV) ? F(0x5476208, void (*)(Obj, bool, void *))(cv, true, NULL) : (void)0); (tm_is(cv, K_CV) ? F(0x547640C, void (*)(Obj, int, void *))(cv, 32000, NULL) : (void)0); }
    Obj gr = gty ? (tm_is(u_go(rt), K_GO) ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(rt), gty, NULL) : NULL) : NULL;
    /* design #9: the stock popups carry a Refactor.UIScaleRoot (the user's UI scale, 1.1 in the rig: TaskPriorityUI local
       scale 1.100, probe 2026-10-13); the same component here gives the same size and follows the setting */
    Obj sty = type_object("Refactor", "UIScaleRoot");
    Obj sr = sty ? (tm_is(u_go(rt), K_GO) ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(rt), sty, NULL) : NULL) : NULL;
    rig_log("teams: UIScaleRoot %s, root scale %.3f", u_alive(sr) ? "added" : "MISSING", (tm_is(rt, K_TR) ? F(0x510AEFC, V3 (*)(Obj, void *))(rt, NULL) : (V3){0, 0, 0}).x);
    h_root = u_gc_new(bgi ? bgi : rt);
    /* the window: the panel with its frame; keep the title, the close button and the info icon */
    Obj w = u_instantiate(u_gc_get(h_tpl_panel), rt);
    if (!u_alive(w)) { rig_log("teams: window clone failed"); return -1; }
    centre_anchor(w); rt_pos(w, 0, 0); tr_scale(w, 1.0f);
    int n = (tm_is(w, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(w, NULL) : 0);
    for (int i = 0; i < n; i++) {
        Obj c = (tm_is(w, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(w, i, NULL) : NULL);
        char nm[64]; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(u_go(c), NULL), nm, sizeof nm);
        if (strcmp(nm, "Txt_Title") && strcmp(nm, "CloseButton") && strcmp(nm, "InfoIcon")) u_go_active(u_go(c), false);   /* stock content off */
    }
    Obj wi = img_of(w); if (u_alive(wi)) g_raycast(wi, true);
    h_win = u_gc_new(w);
    Obj title = tmp_of(tr_find(w, "Txt_Title")), close = tr_find(w, "CloseButton"), info = tr_find(w, "InfoIcon");
    if (close) button_off(close);
    if (info) button_off(info);
    h_title = u_gc_new(title); h_close = u_gc_new(close); h_info = u_gc_new(info ? img_of(info) : NULL);
    /* Ready: Recruit's red button (Lora 18); when ready it turns into the BottomButton "Cancel Ready" */
    Obj tx = NULL, b = new_button(h_tpl_red, w, "Txt_Recruit", &tx);
    if (!b) { b = new_text(w, 232, 40, 18, AL_CENTER); tx = b; }
    h_ready = u_gc_new(b); h_ready_txt = u_gc_new(tx);
    h_hint = u_gc_new(new_text(w, 360, 40, 14, AL_CENTER));
    /* the rules: the info icon's "How Teams Work" tooltip in the game's tooltip window (tips_frame; the old plain box is gone) */
    rig_log("teams: window built from the Task Planner prefab (card %.0fx%.0f); modal canvas %s sorting %d, raycaster %s",
            card_w, card_h, u_alive(cv) ? "added" : "MISSING", u_alive(cv) ? (tm_is(cv, K_CV) ? F(0x5476320, int (*)(Obj, void *))(cv, NULL) : 0) : -1, u_alive(gr) ? "added" : "MISSING");
    return 0;
}
static Obj card_for(int unit) {                              /* one card per unit (SubUI_PolicyPortrait), kept while it lives */
    int free_i = -1;
    for (int i = 0; i < TM_MAX_UNITS; i++) {
        if (por[i].card && !memcmp(por[i].guid, st.guid[unit], 16)) {
            Obj c = u_gc_get(por[i].card);
            if (u_alive(c)) { por[i].used = 1; return c; }
            u_gc_free(por[i].card); u_gc_free(por[i].mark); por[i].card = por[i].mark = 0;
        }
        if (free_i < 0 && !por[i].card) free_i = i;
    }
    Obj w = win();
    if (free_i < 0 || !w) return NULL;
    Obj ct = u_instantiate(u_gc_get(h_tpl_card), w);
    Obj pp = comp_of(ct, &h_card_type, "Refactor.UI", "SubUI_PolicyPortrait");
    if (!u_alive(pp)) { u_destroy(u_go(ct)); return NULL; }
    centre_anchor(u_tr(pp));
    (tm_is(pp, K_PP) ? F(0xE95EE8, void (*)(Obj, Obj, void *))(pp, st_unit[unit], NULL) : (void)0);   /* stock Refresh: portrait, name, level, _unitGuid */
    Obj cg = (tm_is(u_go(pp), K_GO) ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(pp), u_gc_get(h_cg_type) ? u_gc_get(h_cg_type)
               : (h_cg_type = u_gc_new(type_object("UnityEngine", "CanvasGroup")), u_gc_get(h_cg_type)), NULL) : NULL);
    (void)cg;
    /* "marked for the run" (design #4): a 2 px copper frame + a small tag with the stock CheckBox_Selected sign */
    static const C4 copper = {0.714f, 0.514f, 0.353f, 1}, tagc = {0.176f, 0.165f, 0.192f, 1};   /* #b6835a, #2d2a31 */
    Obj mk = new_rect(u_tr(pp), 0, 0, 1, 1, 0, 0, (C4){0, 0, 0, 0});
    if (mk) {
        Obj mt = u_tr(mk);
        new_rect(mt, 0, 1, 1, 1, 0, 2, copper); new_rect(mt, 0, 0, 1, 0, 0, 2, copper);
        new_rect(mt, 0, 0, 0, 1, 2, 0, copper); new_rect(mt, 1, 0, 1, 1, 2, 0, copper);
        Obj tag = new_rect(mt, 1, 1, 1, 1, 22, 22, tagc);
        Obj spr = u_gc_get(h_spr_check);
        if (tag && u_alive(spr)) { Obj ic = new_rect(u_tr(tag), 0.5f, 0.5f, 0.5f, 0.5f, 12, 12, (C4){1, 1, 1, 1}); if (ic) img_sprite(ic, spr); }
        else if (tag) { Obj t = new_text(u_tr(tag), 22, 22, 13, AL_CENTER); if (t) { rt_pos(u_tr(t), 0, 0); tmp_set(t, "<color=#d9be8f>RUN</color>"); } }
        u_go_active(u_go(mk), false);
    }
    por[free_i].card = u_gc_new(pp); por[free_i].mark = u_gc_new(mk); memcpy(por[free_i].guid, st.guid[unit], 16); por[free_i].used = 1;
    return pp;
}
static void card_style(Obj pp, float alpha, int picked_) {
    Obj cg = comp_of(pp, &h_cg_type, "UnityEngine", "CanvasGroup");
    if (u_alive(cg)) (tm_is(cg, K_CG) ? F(0x546FA94, void (*)(Obj, float, void *))(cg, alpha, NULL) : (void)0);
    Obj im = img_of(u_tr(pp));
    if (u_alive(im)) { if (picked_) g_color(im, 0.85f, 0.745f, 0.561f, 1); else g_color(im, 1, 1, 1, 1); }   /* host pick: #d9be8f */
}

/* ---------------------------------------------------------------- layout */
static tm_column cols[TM_MAX_PLAYERS + 2];
static int ncols;
static uint64_t shown_sig;
int dsnet_lent_to(dsnet_client *c, const uint8_t unit[16]) __attribute__((weak_import));
static int lpop_unit = -1;                                   /* the unit whose lending menu is open (-1 none) */
static uintptr_t bar_h[8]; static int bar_pl[8], bar_lv[8], nbar;   /* connection bars per other-player row (tooltip) */
int dsnet_admin_mode(dsnet_client *c) __attribute__((weak_import));
int dsnet_owner_info(dsnet_client *c, const uint8_t guid[16], uint8_t uid[16], char name[32]) __attribute__((weak_import));
/* "Reserved for A, B": names of the away owners of the reserved units in this column */
static void reserved_names(const tm_column *c, char *out, size_t cap) {
    dsnet_client *cl = cli(); size_t l = 0; out[0] = 0;
    char seen[8][32]; int ns = 0;
    for (int j = 0; j < c->n && cl && dsnet_owner_info; j++) {
        uint8_t uid[16]; char nm[32] = "";
        if (dsnet_owner_info(cl, st.guid[c->unit[j]], uid, nm) != TM_RESERVED || !nm[0]) continue;
        int dup = 0; for (int k = 0; k < ns; k++) if (!strcmp(seen[k], nm)) dup = 1;
        if (dup || ns >= 8) continue;
        snprintf(seen[ns++], 32, "%s", nm);
        l += (size_t)snprintf(out + l, cap > l ? cap - l : 0, "%s%s", l ? ", " : "", nm);
    }
    if (!out[0]) snprintf(out, cap, "a player who is away");
}
static int admin_mode(void) {                                /* host admin mode: dsnet session state (DSNET_ADMIN_MODE 14) */
    dsnet_client *c = cli();
    return c && dsnet_admin_mode ? dsnet_admin_mode(c) != 0 : 0;
}
static uint64_t state_sig(void) {
    uint64_t h = fnv(1469598103934665603ull, &st, sizeof st);
    h = fnv(h, &hsel, sizeof hsel);
    int run_ = mp_running(); h = fnv(h, &run_, sizeof run_);
    h = fnv(h, &prop, sizeof prop);
    { int lt[TM_MAX_UNITS]; dsnet_client *c = cli();
      for (int u = 0; u < st.nunits; u++) lt[u] = c && dsnet_lent_to ? dsnet_lent_to(c, st.guid[u]) : -1;
      h = fnv(h, lt, sizeof(int) * (size_t)st.nunits); h = fnv(h, &lpop_unit, sizeof lpop_unit); }
    return fnv(h, &opened, sizeof opened);
}
/* pools of plain rectangles and texts on the window, reused by every layout (unused ones are hidden) */
#define NPOOL 40
typedef struct upool { uintptr_t r[NPOOL], t[NPOOL]; int ur, ut; Obj (*parent)(void); } upool;
static Obj pwin(void);
static upool pool_teams = { .parent = win }, pool_prop = { .parent = pwin }, *pl = &pool_teams;   /* one per window */
static void pool_clear(upool *q) { for (int i = 0; i < NPOOL; i++) { u_gc_free(q->r[i]); u_gc_free(q->t[i]); q->r[i] = q->t[i] = 0; } }
static void pool_free(void) { pool_clear(&pool_teams); }
static void pool_begin(upool *q) { pl = q; q->ur = q->ut = 0; }
static void pool_end(void) {
    for (int i = pl->ur; i < NPOOL; i++) { Obj o = u_gc_get(pl->r[i]); if (u_alive(o)) u_active(o, false); }
    for (int i = pl->ut; i < NPOOL; i++) { Obj o = u_gc_get(pl->t[i]); if (u_alive(o)) u_active(o, false); }
}
static C4 hexc(const char *h, float a) {                     /* "#RRGGBB" */
    unsigned v = 0; if (h && h[0] == '#') v = (unsigned)strtoul(h + 1, NULL, 16);
    return (C4){(v >> 16 & 255) / 255.0f, (v >> 8 & 255) / 255.0f, (v & 255) / 255.0f, a};
}
/* centre (cx, cy), size w x h; back = behind every other child of the window (row backgrounds) */
static Obj p_rect(float cx, float cy, float w, float h, C4 c, Obj sprite, int back) {
    if (pl->ur >= NPOOL) return NULL;
    Obj o = u_gc_get(pl->r[pl->ur]);
    if (!u_alive(o)) { o = new_rect(pl->parent(), 0.5f, 0.5f, 0.5f, 0.5f, w, h, c); u_gc_free(pl->r[pl->ur]); pl->r[pl->ur] = u_gc_new(o); }
    pl->ur++;
    if (!o) return NULL;
    Obj t = u_tr(o);
    rt_size(t, w, h); rt_pos(t, cx, cy); img_sprite(o, sprite); g_color(o, c.r, c.g, c.b, c.a);
    if (back) (tm_is(t, K_TR) ? F(0x510EC94, void (*)(Obj, void *))(t, NULL) : (void)0); else u_last_sibling(t);    /* SetAsFirstSibling */
    u_active(o, true);
    return o;
}
/* text with its left edge at lx (AL_LEFT) or centred on lx + w / 2; cinzel = the title font */
static void set_text(Obj o, float lx, float cy, float w, float h, float size, int align, int cinzel, const char *txt) {
    if (!u_alive(o)) return;
    Obj t = u_tr(o);
    centre_anchor(t); rt_size(t, w, h); rt_pos(t, lx + w / 2, cy);
    tmp_font(o, cinzel ? u_gc_get(h_font_title) : (tm_is(tmp_of(u_gc_get(h_tpl_text)), K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(tmp_of(u_gc_get(h_tpl_text)), NULL) : NULL));
    (tm_is(o, K_TMP) ? F(0x4F0B340, void (*)(Obj, float, void *))(o, size, NULL) : (void)0);
    (tm_is(o, K_TMP) ? F(0x4F0B7BC, void (*)(Obj, int, void *))(o, align, NULL) : (void)0);
    tmp_set(o, txt); u_active(o, true); u_last_sibling(t);
}
static Obj p_text(float lx, float cy, float w, float h, float size, int align, int cinzel, const char *txt) {
    if (pl->ut >= NPOOL) return NULL;
    Obj o = u_gc_get(pl->t[pl->ut]);
    if (!u_alive(o)) { o = new_text(pl->parent(), w, h, size, align); u_gc_free(pl->t[pl->ut]); pl->t[pl->ut] = u_gc_new(o); }
    pl->ut++;
    set_text(o, lx, cy, w, h, size, align, cinzel, txt);
    return o;
}
/* section headers: clickable (host: target column), read back by the tests */
static void section_header(int k, int player, float lx, float cy, float w, float h, float size, int cinzel, const char *txt) {
    if (k >= nhdr) { h_hdr[k] = u_gc_new(new_text(win(), w, h, size, AL_LEFT)); nhdr = k + 1; }
    Obj hd = u_gc_get(h_hdr[k]);
    hdr_player[k] = player;
    set_text(hd, lx, cy, w, h, size, AL_LEFT, cinzel, txt);
}
/* ---- lending from the Teams window (owner lan_v7; dsnet DSNET_LEND, also in a dungeon). A chip on the card:
   your unit "Lend" / "Lent: Ben", a unit lent to you "From the owner"; the chip opens a small menu: "Lend control to
   <name>" per other player, "Take back control", or "Give back". uxgame's lend events show the messages. */
int dsnet_lend(dsnet_client *c, const uint8_t unit[16], int to_slot) __attribute__((weak_import));
int dsnet_give_back(dsnet_client *c, const uint8_t unit[16]) __attribute__((weak_import));
enum { LA_LEND = 1, LA_TAKE, LA_GIVE };
#define C_MAIN "#ccbeb8"
#define C_SEC "#a69085"
#define C_ACC "#d9be8f"
#define C_WARN "#f16071"
static Obj cam_for(Obj graphic);
static int hit(Obj comp, V2 m, Obj cam);
#define NCHIP 24
static int lent[TM_MAX_UNITS];                               /* borrower per unit (-1 none), refreshed by layout */
static uintptr_t chip_h[NCHIP]; static int chip_u[NCHIP], nchip;
static uintptr_t lrow_h[8]; static int lrow_act[8], lrow_arg[8], nlrow;
static char lpop_s[300];
static void lent_refresh(void) {
    dsnet_client *c = cli();
    for (int u = 0; u < st.nunits; u++) lent[u] = c && dsnet_lent_to ? dsnet_lent_to(c, st.guid[u]) : -1;
}
static void lend_do(int u, int act, int arg) {
    dsnet_client *c = cli();
    if (!c || u < 0 || u >= st.nunits || !dsnet_lend) return;
    int rc = act == LA_LEND ? dsnet_lend(c, st.guid[u], arg) : act == LA_TAKE ? dsnet_lend(c, st.guid[u], DSNET_NO_PLAYER)
           : act == LA_GIVE && dsnet_give_back ? dsnet_give_back(c, st.guid[u]) : -1;
    rig_log("teams: lend action %d unit %d to %d rc %d", act, u, arg, rc);
}
static void chip_draw(int u, float x, float top, float cw, int owner) {
    if (nchip >= NCHIP || !mp_running()) return;
    int lt = lent[u];
    char t[120], nm[40];
    if (owner == st.me && lt < 0) snprintf(t, sizeof t, "<color=" C_ACC ">Lend</color>");
    else if (owner == st.me) { pname(lt, nm); snprintf(t, sizeof t, "<color=" C_ACC ">Lent: </color><color=%s>%s</color>", ui_player_color(lt), nm); }
    else if (lt == st.me && owner >= 0) { pname(owner, nm); snprintf(t, sizeof t, "<color=" C_ACC ">From </color><color=%s>%s</color>", ui_player_color(owner), nm); }
    else return;
    if (lt >= 0) p_rect(x, top - 1.5f, cw - 4, 3, hexc(ui_player_color(lt), 1), NULL, 0);   /* lent: the controller's colour */
    float w = owner == st.me && lt < 0 ? 50 : 92;               /* inside the card's top edge, left */
    Obj r = p_rect(x - cw / 2 + w / 2 + 2, top - 12, w, 22, hexc("#2d2a31", 0.95f), NULL, 0);
    p_text(x - cw / 2 + 4, top - 12, w - 4, 22, 14, AL_CENTER, 0, t);
    if (r) { chip_h[nchip] = pl->r[pl->ur - 1]; chip_u[nchip++] = u; }
}
static void lpop_draw(void) {                                /* the menu of the open chip, under it */
    nlrow = 0; lpop_s[0] = 0;
    if (lpop_unit < 0 || lpop_unit >= st.nunits) { lpop_unit = -1; return; }
    int u = lpop_unit, owner = st.owner[u], lt = lent[u];
    Obj anchor = NULL; for (int i = 0; i < nchip; i++) if (chip_u[i] == u) anchor = u_gc_get(chip_h[i]);
    if (!u_alive(anchor)) { lpop_unit = -1; return; }
    V2 ap = rt_get_pos(u_tr(anchor));
    char lab[8][80];
    if (owner == st.me && lt < 0) {
        for (int p = 0; p < TM_MAX_PLAYERS && nlrow < 8; p++) if ((st.present >> p & 1u) && p != st.me) {
            char nm[40]; pname(p, nm); snprintf(lab[nlrow], 80, "Lend control to %s", nm); lrow_act[nlrow] = LA_LEND; lrow_arg[nlrow++] = p; }
        if (!nlrow) { snprintf(lab[0], 80, "No other player"); lrow_act[0] = 0; nlrow = 1; }
    } else if (owner == st.me) { snprintf(lab[0], 80, "Take back control"); lrow_act[0] = LA_TAKE; nlrow = 1; }
    else { snprintf(lab[0], 80, "Give back"); lrow_act[0] = LA_GIVE; nlrow = 1; }
    size_t l = 0;
    for (int i = 0; i < nlrow; i++) {
        float y = ap.y - 26 - i * 30;
        Obj r = p_rect(ap.x - 23 + 110, y, 220, 28, hexc("#222025", 0.98f), NULL, 0);
        p_rect(ap.x - 23, y, 2, 28, hexc("#915847", 1), NULL, 0);
        char t[120]; snprintf(t, sizeof t, "<color=" C_MAIN ">%s</color>", lab[i]);
        p_text(ap.x - 23 + 8, y, 210, 28, 15, AL_LEFT, 0, t);
        lrow_h[i] = r ? pl->r[pl->ur - 2] : 0;
        l += (size_t)snprintf(lpop_s + l, sizeof lpop_s - l, "%s%s", i ? "|" : "", lab[i]);
    }
}
static int lend_click(V2 m, Obj cam) {                       /* 1 = taken by a chip or the menu */
    if (lpop_unit >= 0) {
        int u = lpop_unit; lpop_unit = -1; shown_sig = 0;
        for (int i = 0; i < nlrow; i++) if (hit(u_gc_get(lrow_h[i]), m, cam)) { if (lrow_act[i]) lend_do(u, lrow_act[i], lrow_arg[i]); return 1; }
        return 1;                                            /* a click elsewhere closes the menu */
    }
    for (int i = 0; i < nchip; i++) if (hit(u_gc_get(chip_h[i]), m, cam)) { lpop_unit = chip_u[i]; shown_sig = 0; return 1; }
    return 0;
}
static int unit_card(int u, float x, float y, float s, float alpha, int clickable, int owner) {
    Obj pp = card_for(u);
    if (!pp) return 0;
    Obj prt = u_tr(pp);
    rt_pos(prt, x, y); tr_scale(prt, s); u_last_sibling(prt);
    int marked = 0;                                          /* marks are not shown in Teams (owner lan_v8) */
    card_style(pp, alpha, hsel.picked == u);
    for (int q = 0; q < TM_MAX_UNITS; q++) if (por[q].card && !memcmp(por[q].guid, st.guid[u], 16)) {
        por[q].owner = owner; por[q].clickable = clickable; por[q].marked = marked;
        Obj mk = u_gc_get(por[q].mark); if (u_alive(mk)) u_go_active(u_go(mk), marked);
    }
    u_go_active(u_go(pp), true);
    if (st.in_dungeon) {                                     /* owner lan_v8: on the run (gold frame + band) vs at the settlement (muted) */
        int run_ = st_map[u] == 1;
        for (int q = 0; q < TM_MAX_UNITS; q++) if (por[q].card && !memcmp(por[q].guid, st.guid[u], 16)) {
            Obj mk = u_gc_get(por[q].mark); if (u_alive(mk)) u_go_active(u_go(mk), run_); }
        if (!run_) card_style(pp, alpha * 0.45f, hsel.picked == u);
        float cw = card_w * s, by = y - card_h * s / 2 + 11;
        p_rect(x, by, cw - 6, 18, hexc("#2d2a31", 0.92f), NULL, 0);
        p_text(x - cw / 2 + 3, by, cw - 6, 18, 12, AL_CENTER, 0, run_ ? "<color=" C_ACC ">On the run</color>" : "<color=" C_SEC ">At the settlement</color>");
    }
    chip_draw(u, x, y + card_h * s / 2, card_w * s, owner);
    return 1;
}
/* height of the free row and the other players' rows at card scales sf (free) and so (others) */
#define LX (-630.0f)
#define ROW_W 1260.0f
static float rows_h(int n, int per, float s) { int r = n ? (n + per - 1) / per : 0; return r ? r * (card_h * s + 8) : 0; }
static float lower_h(const int *order, int no, float sf, float so) {
    float h = 0;
    for (int i = 0; i < no; i++) {
        tm_column *c = &cols[order[i]];
        if (c->player == TM_FREE) h += 24 + 46 + (c->n ? rows_h(c->n, (int)(ROW_W / (100 * sf)), sf) + 8 : 44) + 16;
        else if (c->player != st.me) h += 30 + (c->n ? rows_h(c->n, (int)(ROW_W / (76 * so / 0.75f)), so) : 0) + 8 + 8;
    }
    return h;
}
/* Layout (research/ux_design_2026-10-13.md section 3), window-local units, window 1320 x 888:
     zone 1, the light strip (y 336..206): your units, full size, label column on the left;
     zone 2, from y 196: "Free units" on the stock region-title band, cards on a Slot1 row;
     zone 3: one muted row per other player / reserved (cards 0.75, alpha 0.45 / 0.35, not clickable unless host admin);
     bottom bar (y -405): region button + its line, one hint, the Ready button. Rules: the info icon's tooltip. */
static void layout(void) {
    Obj w = win();
    if (!w) return;
    ncols = tm_layout(&st, cols, TM_MAX_PLAYERS + 2);
    int order[TM_MAX_PLAYERS + 2], no = 0, kme = -1;
    for (int k = 0; k < ncols; k++) if (cols[k].player == st.me) kme = k;
    for (int k = 0; k < ncols; k++) if (cols[k].player == TM_FREE) order[no++] = k;
    for (int k = 0; k < ncols; k++) if (cols[k].player >= 0 && cols[k].player != st.me) order[no++] = k;
    for (int k = 0; k < ncols; k++) if (cols[k].player == TM_RESERVED) order[no++] = k;
    int host_admin = st.me == 0 && st.admin;
    pool_begin(&pool_teams);
    lent_refresh(); nchip = 0; nbar = 0;
    for (int i = 0; i < TM_MAX_UNITS; i++) por[i].used = 0;
    char t[600];
    int nh = 0;
    /* zone 1: your units */
    int mine = kme >= 0 ? cols[kme].n : 0, rdy_me = st.me >= 0 && (st.ready >> st.me & 1u);
    section_header(nh++, st.me, LX + 10, 304, 220, 34, 24, 1, "<color=" C_MAIN ">Your units</color>");
    /* owner rule (lan_v8): Teams only allocates units (take / free / lend); expeditions go through the game's
       own expedition window. No marks, no Ready, no Go here. */
    (void)rdy_me;
    snprintf(t, sizeof t, "<color=" C_SEC ">%d unit%s \xC2\xB7 max %d</color>", mine, mine == 1 ? "" : "s", st.max_mark > 0 ? st.max_mark : 3);
    p_text(LX + 10, 272, 220, 24, 14, AL_LEFT, 0, t);
    float step = 100, s1 = 1.0f;
    if (mine > 10) { step = mine > 12 ? 1030.0f / mine : 86; s1 = step / 100 * (mine > 12 ? 0.95f : 1.0f); if (s1 > 0.85f) s1 = 0.85f; }
    for (int j = 0; j < mine; j++)
        unit_card(cols[kme].unit[j], -350 + j * step, 271, s1, 1.0f, !st.in_dungeon, st.me);
    for (int e = 0; e < 3; e++) {                              /* no units: three empty stock slots + how to get one */
        Obj es = u_gc_get(h_empty[e]);
        if (mine) { if (u_alive(es)) u_go_active(u_go(es), false); continue; }
        if (!u_alive(es)) {
            Obj c = u_instantiate(u_gc_get(h_tpl_card), w);
            Obj pp = comp_of(c, &h_card_type, "Refactor.UI", "SubUI_PolicyPortrait");
            if (u_alive(pp)) { u_enabled(pp, false); Obj por_ = tr_find(u_tr(pp), "Portrait"); if (por_) u_go_active(u_go(por_), false);
                               centre_anchor(u_tr(pp)); tr_scale(u_tr(pp), 1.0f); }
            u_gc_free(h_empty[e]); h_empty[e] = u_gc_new(pp); es = pp;
        }
        if (u_alive(es)) { rt_pos(u_tr(es), -350 + e * 100, 271); u_go_active(u_go(es), true); }
    }
    if (!mine) p_text(-190, 271, 420, 30, 14, AL_LEFT, 0, "<color=" C_SEC ">Take a free unit below (left-click).</color>");
    /* zones 2 and 3: fit above y -372, cards never below 0.75 (free) / 0.6 (others) */
    float sf = 1.0f, so = 0.75f;
    for (int it = 0; it < 8 && lower_h(order, no, sf, so) > 196 + 372; it++) { if (sf > 0.75f) sf -= 0.05f; else if (so > 0.6f) so -= 0.05f; }
    float y = 196;
    for (int i = 0; i < no; i++) {
        tm_column *c = &cols[order[i]];
        int pl = c->player;
        if (pl == TM_FREE) {
            Obj band = u_gc_get(h_band);
            if (!u_alive(band) && u_alive(u_gc_get(h_tpl_band))) {
                band = u_instantiate(u_gc_get(h_tpl_band), w); centre_anchor(band); tr_scale(band, 1.0f);
                Obj bi = img_of(band); if (u_alive(bi)) g_raycast(bi, false);
                u_gc_free(h_band); h_band = u_gc_new(band);
            }
            if (u_alive(band)) { rt_size(band, 556, 44); rt_pos(band, LX + 278, y - 22); u_go_active(u_go(band), true); }
            section_header(nh++, TM_FREE, LX + 24, y - 22, 400, 44, 24, 1, "<color=" C_MAIN ">Free units</color>");
            p_text(LX + 570, y - 22, 300, 30, 14, AL_LEFT, 0, "<color=" C_SEC ">Left-click to take</color>");
            y -= 46;
            int per = (int)(ROW_W / (100 * sf)); if (per < 1) per = 1;
            float rh = c->n ? rows_h(c->n, per, sf) + 8 : 44;
            p_rect(LX + ROW_W / 2, y - rh / 2, ROW_W, rh, hexc("#36323a", 1), NULL, 1);   /* Slot1 */
            if (!c->n) p_text(LX + 20, y - 22, 400, 30, 14, AL_LEFT, 0, "<color=" C_SEC ">No free units</color>");
            for (int j = 0; j < c->n; j++)
                unit_card(c->unit[j], LX + 60 * sf + (j % per) * 100 * sf, y - 8 - card_h * sf / 2 - (j / per) * (card_h * sf + 8), sf, 1.0f, !st.in_dungeon, TM_FREE);
            y -= rh + 16;
            continue;
        }
        if (pl == st.me) continue;
        char nm[40], lb[40], hd[300]; ui_players pls; memset(&pls, 0, sizeof pls);
        int per = (int)(ROW_W / (76 * so / 0.75f)); if (per < 1) per = 1;
        float rh = 30 + (c->n ? rows_h(c->n, per, so) : 0) + 8;
        p_rect(LX + ROW_W / 2, y - rh / 2, ROW_W, rh, hexc("#2b282f", 1), NULL, 1);       /* Slot2 */
        float alpha = host_admin ? 1.0f : pl == TM_RESERVED ? 0.35f : 0.45f;
        if (pl == TM_RESERVED) {
            char rn[200]; reserved_names(c, rn, sizeof rn);
            snprintf(hd, sizeof hd, "<color=" C_SEC ">Reserved for %s (away)</color>", rn);
            section_header(nh++, pl, LX + 16, y - 15, 900, 28, 16, 0, hd);
        } else {
            player_name(pl, nm); snprintf(pls.name[pl], 32, "%s", nm);
            int rdy = st.ready >> pl & 1u, here = st.present >> pl & 1u;
            p_rect(LX + 2, y - rh / 2, 4, rh, hexc(ui_player_color(pl), 1), NULL, 0);    /* owner colour strip */
            if (rdy && here) p_rect(LX + 20, y - 15, 10, 10, (C4){1, 1, 1, 1}, u_gc_get(h_spr_check), 0);
            if (here && nbar < 8) {                               /* hover area for the connection tooltip */
                Obj ha = p_rect(LX + ROW_W - 23, y - 15, 30, 22, (C4){0, 0, 0, 0}, NULL, 0);
                if (ha) { bar_h[nbar] = pool_teams.r[pool_teams.ur - 1]; bar_lv[nbar] = 0; bar_pl[nbar++] = pl; }
            }
            if (here && dsnet_player_net && cli()) {            /* connection: 3 bars (#71ab40 / #ea9f3d / #c82e48), no ms */
                int ping = -1, behind = 0, nst = 0; dsnet_player_net(cli(), pl, &ping, &behind, &nst);
                int cs = nst == DSNET_NET_IN_SYNC ? 0 : nst == DSNET_NET_LOADING ? 1 : nst == DSNET_NET_RESYNCING ? 2 : -1;
                int lv = ui_conn_level(ping, behind, cs);           /* dsnet state -> ui state (as ui_game.c's panel) */
                if (nbar > 0 && bar_pl[nbar - 1] == pl) bar_lv[nbar - 1] = lv;
                for (int b_ = 0; b_ < 3; b_++) {
                    float hb = b_ == 0 ? 5 : b_ == 1 ? 9 : 14;
                    p_rect(LX + ROW_W - 30 + b_ * 7, y - 22 + hb / 2, 4, hb, hexc(lv >= b_ + 1 ? ui_conn_color(lv) : "#5d5860", 1), NULL, 0);
                }
            }
            snprintf(hd, sizeof hd, "<color=" C_SEC ">%s's units%s</color>", ui_player_label(&pls, pl, lb), !here ? "  <size=88%>left the game</size>" : "");
            (void)rdy;
            section_header(nh++, pl, LX + 32, y - 15, 900, 28, 16, 0, hd);
        }
        for (int j = 0; j < c->n; j++)
            unit_card(c->unit[j], LX + 12 + card_w * so / 2 + (j % per) * 76 * so / 0.75f, y - 30 - card_h * so / 2 - (j / per) * (card_h * so + 8),
                      so, alpha, !st.in_dungeon && host_admin, pl);
        y -= rh + 8;
    }
    for (int k = nh; k < nhdr; k++) u_active(u_gc_get(h_hdr[k]), false);
    for (int i = 0; i < TM_MAX_UNITS; i++)                   /* units gone (dead, retired): drop their card */
        if (por[i].card && !por[i].used) { u_destroy(u_go(u_gc_get(por[i].card))); u_gc_free(por[i].card); u_gc_free(por[i].mark); por[i].card = por[i].mark = 0; }
    Obj o;
    if ((o = u_gc_get(h_title))) {
        tmp_set(o, spect() ? "Teams (watching)" : "Teams");
        int np_ = 0, nr_ = 0;                                 /* "Ready n / m" in the title band (approved mockup Teams A) */
        for (int p = 0; p < TM_MAX_PLAYERS; p++) if (st.present >> p & 1u) { np_++; nr_ += st.ready >> p & 1u; }
        (void)nr_; (void)np_;                                 /* no "Ready n / m": Teams has no Ready (owner lan_v8) */
    }
    /* bottom bar */
    int reg = prop.active ? prop.region : region_default();
    char pn_[40] = ""; if (prop.active) pname(prop.proposer, pn_);
    snprintf(region_line, sizeof region_line, "Region: %s%s%s%s", region_name(reg), prop.active ? " (proposal by " : "", pn_, prop.active ? ")" : "");
    snprintf(t, sizeof t, "<color=" C_SEC ">Region</color>  <color=" C_MAIN ">%s</color>  <color=" C_SEC ">%s</color>", region_name(reg),
             prop.active ? "(proposed)" : "(pick another one in Go to dungeon)");
    (void)t;                                                  /* owner lan_v7: no region line in Teams (the proposal shows it) */
    if ((o = u_gc_get(h_ready))) u_active(o, false);        /* no Go / Ready in Teams (owner lan_v8) */
    if ((o = u_gc_get(h_hint))) {
        char stl[300] = "";
        if (!mp_running()) snprintf(stl, sizeof stl, "<color=" C_WARN ">Multiplayer is not running yet: changes here are not applied.</color>");
        else if (st.in_dungeon) snprintf(stl, sizeof stl, "In a dungeon only lending works: use Lend on your units.");
        else if (host_admin && hsel.picked >= 0) snprintf(stl, sizeof stl, "Host: click a section name to move the picked unit there.");
        else if (st.me == 0 && hsel.target != TM_NO_TARGET) {
            char nm[40], lb[40]; ui_players pls; memset(&pls, 0, sizeof pls);
            if (hsel.target >= 0) { player_name(hsel.target, nm); snprintf(pls.name[hsel.target], 32, "%s", nm); }
            snprintf(stl, sizeof stl, "Host: units you click go to %s.", hsel.target == TM_FREE ? "Free units" : hsel.target >= 0 ? ui_player_label(&pls, hsel.target, lb) : "?");
        }
        else if (st.me >= 0 && !mine) snprintf(stl, sizeof stl, "Next: take a free unit (left-click).");
        else if (st.me >= 0) snprintf(stl, sizeof stl, "To start an expedition, use the game's Go to dungeon.");
        snprintf(t, sizeof t, "<color=" C_SEC ">%s</color>", stl);
        set_text(o, -10, -405, 390, 40, 14, AL_CENTER, 0, t);
    }
    lpop_draw();
    pool_end();
}

/* ---------------------------------------------------------------- the proposal side panel (every player while a proposal is open)
   Owner lan_v10: a small panel at the right edge of the screen, not modal: everyone keeps playing and the world keeps
   running (it replaced the centred window that blocked every world click for up to 60 s, ux_flow F4). The Recruit
   window's box (PopUpBox_Box), Cinzel title, Lora rows: per player the units going (small stock cards) and approved /
   waiting; "n / 4 members"; the time left (red in the last 10 s); Approve (red) + Decline, or (proposer) Cancel.
   "Hide" folds it to the title and one line (Escape and Approve fold it too, ux_flow F15); "Show" unfolds it.
   Own canvas (sorting 32010); outside the panel every click goes to the world (hk_over_ui, prop_click). */
#define PCARDS 48
#define PW 380.0f                                            /* panel width (popup canvas units) */
static uintptr_t h_ptip[3];                                  /* region line, waiting line, time line (tooltips) */
static uintptr_t h_proot, h_pwin, h_p_ok, h_p_ok_txt, h_p_no, h_p_no_txt, h_pfold, h_pcard[PCARDS];
static uintptr_t h_prow[TM_MAX_PLAYERS]; static int prow_pl[TM_MAX_PLAYERS], prow_going[TM_MAX_PLAYERS], nprow;   /* player rows (tooltips) */
static int prop_shown;
static uint64_t prop_sig;
static Obj proot(void) { Obj r = u_gc_get(h_proot); return u_alive(r) ? r : NULL; }
static Obj pwin(void) { Obj w = u_gc_get(h_pwin); return u_alive(w) ? w : NULL; }
static void free_prop(void) {
    Obj r = proot();
    if (r) u_destroy(u_go(r));
    uintptr_t *hs[] = { &h_proot, &h_pwin, &h_p_ok, &h_p_ok_txt, &h_p_no, &h_p_no_txt, &h_pfold };
    for (size_t i = 0; i < sizeof hs / sizeof hs[0]; i++) { u_gc_free(*hs[i]); *hs[i] = 0; }
    for (int i = 0; i < PCARDS; i++) { u_gc_free(h_pcard[i]); h_pcard[i] = 0; }
    pool_clear(&pool_prop); prop_shown = 0; prop_sig = 0; nprow = 0;
}
static int build_prop(void) {
    if (load_templates()) return -1;
    free_prop();
    Obj ct = popup_parent_tr(); if (!ct) ct = top_canvas_tr();
    if (!ct) return -1;
    Obj rt = u_instantiate(u_gc_get(h_tpl_bg), ct);            /* full-screen holder of the panel's own canvas: no dimming, no click target */
    if (!u_alive(rt)) return -1;
    button_off(rt);
    rt_anchor(rt, 0, 0, 1, 1); rt_pivot(rt, 0.5f, 0.5f); rt_size(rt, 0, 0); rt_pos(rt, 0, 0); tr_scale(rt, 1.0f);
    int n0 = (tm_is(rt, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(rt, NULL) : 0);
    for (int i = 0; i < n0; i++) u_go_active(u_go((tm_is(rt, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(rt, i, NULL) : NULL)), false);
    Obj bgi = img_of(rt);
    if (u_alive(bgi)) { img_sprite(bgi, NULL); g_color(bgi, 0, 0, 0, 0); g_raycast(bgi, false); }   /* the world stays visible and clickable */
    u_last_sibling(rt);
    Obj cty = type_object("UnityEngine", "Canvas"), gty = type_object("UnityEngine.UI", "GraphicRaycaster"), sty = type_object("Refactor", "UIScaleRoot");
    Obj cv = cty ? (tm_is(u_go(rt), K_GO) ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(rt), cty, NULL) : NULL) : NULL;
    if (u_alive(cv)) { (tm_is(cv, K_CV) ? F(0x5476208, void (*)(Obj, bool, void *))(cv, true, NULL) : (void)0);
                       (tm_is(cv, K_CV) ? F(0x547640C, void (*)(Obj, int, void *))(cv, 32010, NULL) : (void)0); }
    if (gty) (void)(tm_is(u_go(rt), K_GO) ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(rt), gty, NULL) : NULL);
    if (sty) (void)(tm_is(u_go(rt), K_GO) ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(rt), sty, NULL) : NULL);
    h_proot = u_gc_new(bgi ? bgi : rt);
    Obj tpl = u_gc_get(h_tpl_box); if (!u_alive(tpl)) tpl = u_gc_get(h_tpl_panel);
    Obj w = u_instantiate(tpl, rt);
    if (!u_alive(w)) return -1;
    rt_anchor(w, 1, 0.5f, 1, 0.5f); rt_pivot(w, 1, 1); rt_size(w, PW, 300); rt_pos(w, -16, 0); tr_scale(w, 1.0f);   /* right edge; top set by prop_layout */
    int n = (tm_is(w, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(w, NULL) : 0);
    for (int i = 0; i < n; i++) u_go_active(u_go((tm_is(w, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(w, i, NULL) : NULL)), false);
    Obj wi = img_of(w); if (u_alive(wi)) g_raycast(wi, true);
    /* the box sprite is partly see-through (title band, inner panels): an opaque #222025 fill inside its border */
    Obj fill = new_rect(w, 0, 0, 1, 1, -16, -16, (C4){0.133f, 0.125f, 0.145f, 1});
    if (fill) (tm_is(u_tr(fill), K_TR) ? F(0x510EC94, void (*)(Obj, void *))(u_tr(fill), NULL) : (void)0);   /* SetAsFirstSibling */
    h_pwin = u_gc_new(w);
    Obj tx = NULL, b = new_button(h_tpl_red, w, "Txt_Recruit", &tx);
    if (!b) { b = new_text(w, 170, 36, 18, AL_CENTER); tx = b; }
    h_p_ok = u_gc_new(b); h_p_ok_txt = u_gc_new(tx);
    b = new_button(h_tpl_bottom, w, "BackButtonText", &tx);
    if (!b) { b = new_text(w, 150, 30, 16, AL_CENTER); tx = b; }
    h_p_no = u_gc_new(b); h_p_no_txt = u_gc_new(tx);
    h_pfold = u_gc_new(new_text(w, 64, 24, 14, AL_RIGHT));
    rig_log("teams: proposal side panel built (%s)", u_alive(u_gc_get(h_tpl_box)) ? "Recruit box" : "Task Planner panel");
    return 0;
}
static Obj pcard(int k, int unit) {                          /* small stock card k showing unit */
    if (k >= PCARDS) return NULL;
    Obj pp = u_gc_get(h_pcard[k]);
    if (!u_alive(pp)) {
        Obj c = u_instantiate(u_gc_get(h_tpl_card), pwin());
        pp = comp_of(c, &h_card_type, "Refactor.UI", "SubUI_PolicyPortrait");
        if (!u_alive(pp)) return NULL;
        centre_anchor(u_tr(pp));
        u_gc_free(h_pcard[k]); h_pcard[k] = u_gc_new(pp);
    }
    (tm_is(pp, K_PP) ? F(0xE95EE8, void (*)(Obj, Obj, void *))(pp, st_unit[unit], NULL) : (void)0);   /* stock Refresh */
    u_go_active(u_go(pp), true);
    return pp;
}
static int unit_of_guid(const uint8_t g[16]) { for (int i = 0; i < st.nunits; i++) if (!memcmp(st.guid[i], g, 16)) return i; return -1; }
static int prop_secs_left(void) { int l = TM_PROPOSAL_SECONDS - (int)(now_s() - prop_t0); return l < 0 ? 0 : l; }
static char prop_ok_s[40], prop_no_s[40], prop_wait_s[200], prop_time_s[120];
static int scr_rect(Obj comp, float out[4]);
static float prop_top_frac = 0.05f;                          /* panel top: this fraction of the screen height above the middle (under the game's alerts) */
static void prop_layout(void) {
    Obj w = pwin(), r = proot();
    if (!w) return;
    pool_begin(&pool_prop);
    char t[600], pn_[40]; pname(prop.proposer, pn_);
    int rdy_me = st.me >= 0 && (st.ready >> st.me & 1u), mine_prop = prop.proposer == st.me, left = prop_secs_left();
    const char *tc = left <= 10 ? C_WARN : C_ACC;              /* the last 10 s in the warning red */
    int order[TM_MAX_PLAYERS], no = 0, k = 0;                 /* one row per present player: the proposer first */
    if (st.present >> prop.proposer & 1u) order[no++] = prop.proposer;
    for (int p = 0; p < TM_MAX_PLAYERS; p++) if ((st.present >> p & 1u) && p != prop.proposer) order[no++] = p;
    uint32_t wm = tm_prop_waiting(&prop, &st);
    size_t l = (size_t)snprintf(prop_wait_s, sizeof prop_wait_s, wm ? "Waiting for" : "Everyone approved");
    for (int p = 0; p < TM_MAX_PLAYERS && l < sizeof prop_wait_s - 44; p++)
        if (wm >> p & 1u) { char nm[40]; pname(p, nm); l += (size_t)snprintf(prop_wait_s + l, sizeof prop_wait_s - l, "%s %s", l > 11 ? "," : ":", p == st.me ? "you" : nm); }
    float rh = no > 2 ? 62.0f : 74.0f, cs = no > 2 ? 0.36f : 0.42f, lx = -PW / 2 + 22, tw = PW - 44;
    float H = prop_folded ? 96.0f : 108 + no * rh + 112, top = H / 2, yb = -H / 2;
    rt_size(w, PW, H);
    { R4 rr = r ? rt_rect(u_tr(r)) : (R4){0, 0, 0, 0};
      /* the holder can be larger than the screen (UIScaleRoot scales it about the centre with the game's UI size): keep
         the panel's right edge 16 units inside the screen, measured in pixels */
      float sr[4], dx = 0, sw = (float)F(0x5092C94, int (*)(void *))(NULL);   /* Screen.width */
      if (r && rr.w > 0 && scr_rect(r, sr) && sr[2] > 0 && sw > 0) dx = (sr[0] + sr[2] - sw) / (sr[2] / rr.w);
      rt_pos(w, -16 - (dx > 0 ? dx : 0), rr.h > 0 ? rr.h * prop_top_frac : 0); }
    p_text(lx, top - 30, tw - 64, 30, 20, AL_LEFT, 1, "<color=" C_MAIN ">Expedition proposal</color>");
    set_text(u_gc_get(h_pfold), PW / 2 - 22 - 64, top - 30, 64, 24, 14, AL_RIGHT, 0, prop_folded ? "<color=" C_ACC ">Show</color>" : "<color=" C_ACC ">Hide</color>");
    snprintf(prop_time_s, sizeof prop_time_s, "%d s left", left);
    if (prop_folded) {                                        /* one line: the time and who is still to answer */
        snprintf(t, sizeof t, "<color=%s>%s</color><color=" C_SEC "> \xC2\xB7 %s</color>", tc, prop_time_s, prop_wait_s);
        p_text(lx, top - 64, tw, 26, 15, AL_LEFT, 0, t); h_ptip[2] = pool_prop.t[pool_prop.ut - 1]; h_ptip[1] = h_ptip[2]; h_ptip[0] = 0;
        nprow = 0;
    } else {
        if (mine_prop) snprintf(t, sizeof t, "<color=" C_SEC ">Proposed by you \xC2\xB7 </color><color=%s>%s</color>", tc, prop_time_s);
        else snprintf(t, sizeof t, "<color=" C_SEC ">Proposed by </color><color=%s>%s</color><color=" C_SEC "> \xC2\xB7 </color><color=%s>%s</color>",
                      ui_player_color(prop.proposer), pn_, tc, prop_time_s);   /* one colour per player everywhere */
        p_text(lx, top - 58, tw, 24, 15, AL_LEFT, 0, t); h_ptip[2] = pool_prop.t[pool_prop.ut - 1];
        snprintf(t, sizeof t, "<color=" C_SEC ">Region</color>  <color=" C_MAIN ">%s</color>", region_name(prop.region));
        p_text(lx, top - 84, tw, 26, 17, AL_LEFT, 0, t); h_ptip[0] = pool_prop.t[pool_prop.ut - 1];
        float y = top - 104;
        nprow = 0;
        for (int i = 0; i < no; i++) {
            int p = order[i], rd = st.ready >> p & 1u, going = 0, stays = 0;
            for (int u = 0; u < st.nunits; u++) { if (goes_for(u, p)) going++; else if (st.owner[u] == p) stays++; }
            char nm[40], lb[40]; ui_players pls; memset(&pls, 0, sizeof pls); pname(p, nm); snprintf(pls.name[p], 32, "%s", nm);
            p_rect(0, y - rh / 2, PW - 28, rh - 6, hexc("#2b282f", 1), NULL, 1);
            p_rect(-(PW - 28) / 2 + 2, y - rh / 2, 4, rh - 6, hexc(ui_player_color(p), 1), NULL, 0);
            snprintf(t, sizeof t, "<color=" C_MAIN ">%s%s</color>  <color=%s>%s</color>", ui_player_label(&pls, p, lb), p == st.me ? " (you)" : "",
                     rd ? C_ACC : C_SEC, p == prop.proposer ? (rd ? "proposes" : "proposes (not ready)") : rd ? "approved" : "waiting");
            p_text(lx, y - 14, tw - 90, 24, 16, AL_LEFT, 0, t);
            if (nprow < TM_MAX_PLAYERS) { h_prow[nprow] = pool_prop.t[pool_prop.ut - 1]; prow_pl[nprow] = p; prow_going[nprow] = going; nprow++; }
            if (stays) { snprintf(t, sizeof t, "<color=" C_SEC ">%d at home</color>", stays); p_text(PW / 2 - 22 - 90, y - 14, 90, 24, 13, AL_RIGHT, 0, t); }
            int g = 0;
            for (int u = 0; u < st.nunits; u++) {
                if (!goes_for(u, p)) continue;
                Obj pc = pcard(k++, u);
                if (pc) { rt_pos(u_tr(pc), lx + card_w * cs / 2 + g * (card_w * cs + 5), y - 26 - card_h * cs / 2); tr_scale(u_tr(pc), cs); u_last_sibling(u_tr(pc)); }
                g++;
            }
            if (!going) p_text(lx, y - 26 - card_h * cs / 2, tw, 24, 14, AL_LEFT, 0, "<color=" C_SEC ">No units: watches the expedition</color>");
            y -= rh;
        }
        snprintf(t, sizeof t, "<color=%s>%d / %d members</color>", prop_np > TM_PARTY_CAP ? C_WARN : C_MAIN, prop_np, TM_PARTY_CAP);
        p_text(lx, yb + 90, tw, 22, 15, AL_CENTER, 0, t);
        snprintf(t, sizeof t, "<color=" C_SEC ">%s</color>", prop_wait_s);
        p_text(lx, yb + 68, tw, 22, 14, AL_CENTER, 0, t); h_ptip[1] = pool_prop.t[pool_prop.ut - 1];
    }
    for (int i = k; i < PCARDS; i++) { Obj pc = u_gc_get(h_pcard[i]); if (u_alive(pc)) u_go_active(u_go(pc), false); }
    /* buttons (not while folded) */
    int show_ok = !prop_folded && !mine_prop && !rdy_me, show_no = !prop_folded;
    snprintf(prop_ok_s, sizeof prop_ok_s, "%s", show_ok ? "Approve" : "");
    snprintf(prop_no_s, sizeof prop_no_s, "%s", show_no ? (mine_prop ? "Cancel" : "Decline") : "");
    Obj o;
    if ((o = u_gc_get(h_p_ok))) { u_active(o, show_ok); rt_size(u_tr(o), 168, 38); rt_pos(u_tr(o), 86, yb + 30); u_last_sibling(u_tr(o)); }
    if ((o = u_gc_get(h_p_ok_txt))) { snprintf(t, sizeof t, "<color=" C_MAIN ">%s</color>", prop_ok_s); tmp_set(o, t); }
    if ((o = u_gc_get(h_p_no))) { u_active(o, show_no); rt_size(u_tr(o), 150, 30); rt_pos(u_tr(o), show_ok ? -92 : 0, yb + 30); u_last_sibling(u_tr(o)); }
    if ((o = u_gc_get(h_p_no_txt))) { snprintf(t, sizeof t, "<color=" C_MAIN ">%s</color>", prop_no_s); tmp_set(o, t); }
    pool_end();
}
static int stock_open(int mode);
static void stock_close(void);
static void close_screen(void);
static void stock_countdown(void);
/* every frame: show / hide / refresh; host: the timeout; everyone: the proposer left; the receiver's steps */
static void prop_frame(unsigned n) {
    if (close_stock) { close_stock = 0; stock_close(); }
    if (pstage == ST_SUPPLIES_PENDING && me() >= 0 && scene() == 2) {
        model_refresh();
        uint8_t mine_[TM_MAX_MARK][16]; int going = my_part(mine_);
        if (going && !stock_open(2)) pstage = ST_SUPPLIES;
        else pstage = ST_SUMMARY;                            /* no units of his go: straight to the summary */
        rig_log("teams: proposal received: %s", pstage == ST_SUPPLIES ? "supplies step first" : "summary");
    }
    if (prop.active) {
        tm_pevent e = tm_prop_check(&prop, present_now());
        if (e) prop_end(e, prop.proposer, 0);
    }
    if (prop.active && me() == 0 && !prop_timeout_sent && now_s() - prop_t0 >= TM_PROPOSAL_SECONDS) {
        model_refresh(); send_answer(TM_ANS_TIMEOUT, tm_prop_waiting(&prop, &st)); prop_timeout_sent = 1;
        rig_log("teams: proposal timeout sent (waiting %x)", tm_prop_waiting(&prop, &st));
    }
    if (prop.active && pstage == ST_SUPPLIES && n % 6 == 0) stock_countdown();   /* ux_flow F5: the receiver packing sees the clock */
    int want = prop.active && pstage == ST_SUMMARY && me() >= 0 && scene() == 2;
    static int was_want;
    if (want && !was_want && (opened || close_teams)) close_screen();   /* the panel replaces the Teams window (once) */
    if (want) close_teams = 0;
    was_want = want;
    /* the panel stays while the game's Esc menu is open (it sits at the right edge, the menu in the centre): a receiver
       who opened the Esc menu before the proposal came never got its summary (uxgame run, 2026-10-14 17:15) */
    if (want && !proot() && build_prop()) want = 0;
    if (proot() && u_go_active_self(u_go(proot())) != want) { u_go_active(u_go(proot()), want); if (want) u_last_sibling(u_tr(proot())); }
    prop_shown = want && proot();
    if (!prop_shown || n % 6) return;
    model_refresh();
    uint64_t sig = fnv(fnv(fnv(fnv(1469598103934665603ull, &st, sizeof st), &prop, sizeof prop), &(int){prop_secs_left()}, sizeof(int)), &prop_folded, sizeof prop_folded);
    if (sig != prop_sig) { prop_layout(); prop_sig = sig; }
}
static Obj cam_for(Obj graphic);
static int hit(Obj comp, V2 m, Obj cam);
static int prop_over(V2 m) { return prop_shown && hit(pwin(), m, cam_for(proot())); }   /* the pointer is on the side panel */
static int prop_click(V2 m, int button) {                    /* 1 = the side panel took the click (outside it: the world's) */
    if (!prop_over(m)) return 0;
    Obj cam = cam_for(proot());
    model_refresh();
    if (button != 0) return 1;
    if (hit(u_gc_get(h_pfold), m, cam)) { prop_folded = !prop_folded; rig_log("teams: proposal panel %s", prop_folded ? "folded" : "unfolded"); }
    else if (!prop_folded && u_go_active_self(u_go(u_gc_get(h_p_ok))) && hit(u_gc_get(h_p_ok), m, cam)) { approve(); prop_folded = 1; }   /* approved: out of the way */
    else if (!prop_folded && u_go_active_self(u_go(u_gc_get(h_p_no))) && hit(u_gc_get(h_p_no), m, cam)) prop_decline();
    prop_sig = 0;
    return 1;
}
/* Escape (ux_flow F15, through mpmenu_escape_ours): folds the open panel; 1 = taken */
__attribute__((visibility("default"))) int ui_teams_escape_ours(void) {
    if (!prop_shown || prop_folded) return 0;
    prop_folded = 1; prop_sig = 0;
    rig_log("teams: Escape folded the proposal panel");
    return 1;
}

/* ---------------------------------------------------------------- actions */
static void send_marks(int ready, const uint8_t (*marks)[16], int n) {
    dsnet_client *c = cli();
    if (!have_ready_api() || !c) {                           /* local only until netcore has EXPEDITION_READY */
        local_marks_n = n; memcpy(local_marks, marks, (size_t)n * 16);
        if (ready) notice("Ready is not available in this build");
        return;
    }
    if (dsnet_expedition_ready(c, ready, marks, n)) notice("Your team could not be sent");
}
static void unit_label(int u, char out[64]) {
    out[0] = 0;
    if (u >= 0 && u < st.nunits && st_unit[u]) str_utf8(F(0xFA467C, Obj (*)(Obj, void *))(st_unit[u], NULL), out, 64);   /* GetDisplayName */
    if (!out[0]) snprintf(out, 64, "this unit");
}
static void run(tm_action a) {
    mp_session *s = sess();
    if (!mp_running() && a.kind != TM_NONE && a.kind != TM_PICK && a.kind != TM_UNPICK && a.kind != TM_TARGET && a.kind < TM_DENY_LOCKED) {
        notice("Multiplayer is not running yet: this change cannot be applied");
        rig_log("teams: action %d refused: session not running (det lock off)", (int)a.kind);
        return;
    }
    char un[64], pn[40], lb[40]; ui_players pl; memset(&pl, 0, sizeof pl);
    unit_label(a.unit, un);
    if (a.target >= 0 && a.target < TM_MAX_PLAYERS) { player_name(a.target, pn); snprintf(pl.name[a.target], 32, "%s", pn); }
    switch (a.kind) {
    case TM_CLAIM: if (s) mp_claim(s, st.guid[a.unit]); break;
    case TM_RELEASE: if (s) mp_release(s, st.guid[a.unit]); break;
    case TM_ASSIGN:
        if (s) mp_assign(s, st.guid[a.unit], a.target == TM_FREE ? DSNET_NO_PLAYER : a.target);
        if (a.target == TM_FREE) notice("%s is free", un); else notice("%s goes to %s", un, ui_player_label(&pl, a.target, lb));
        break;
    case TM_MARKS: send_marks(0, (const uint8_t (*)[16])a.marks, a.nmarks); break;
    case TM_READY:                                           /* Go: the stock window (step 2) to pack, then propose; or approve */
        if (!prop.active) { if (stock_open(1)) notice("The expedition window could not be opened"); }
        else approve();
        break;
    case TM_UNREADY: if (prop.active) prop_decline(); else send_marks(0, (const uint8_t (*)[16])a.marks, a.nmarks); break;
    case TM_DENY_LOCKED: notice("In a dungeon you can only lend units (Lend on a portrait)"); break;
    case TM_DENY_OWNED:
        if (a.target == TM_RESERVED) notice("%s is kept for a player who is away", un);
        else notice("%s belongs to %s", un, ui_player_label(&pl, a.target, lb));
        break;
    case TM_DENY_FULL: notice("You already marked %d units for the run", TM_MAX_MARK); break;
    case TM_DENY_NO_MARKS:
        if (prop.active && prop.proposer != st.me) { approve(); break; } notice("Right-click 1 to %d of your units to mark them for the run first", TM_MAX_MARK); break;
    case TM_DENY_NOT_OWN: notice("You can only mark your own units"); break;
    default: break;
    }
    rig_log("teams: action %d unit %d target %d marks %d", (int)a.kind, a.unit, a.target, a.nmarks);
    shown_sig = 0;                                           /* re-layout now (pick highlight) */
}
static int unit_by_guid(const uint8_t g[16]) { for (int i = 0; i < st.nunits; i++) if (!memcmp(st.guid[i], g, 16)) return i; return -1; }

/* ---------------------------------------------------------------- open / close + mouse */
static int screen_ok(void) { return me() >= 0 && scene() == 2 && cur_map() >= 0; }
static int open_screen(void) {
    if (!screen_ok()) return -1;
    if (!root() && build_root()) return -1;
    u_last_sibling(u_tr(root()));
    u_go_active(u_go(root()), true);
    opened = 1; hsel = (tm_sel)TM_SEL_INIT; shown_sig = 0;
    model_refresh(); layout(); shown_sig = state_sig();
    return 0;
}
static void close_screen(void) {
    opened = 0; hsel = (tm_sel)TM_SEL_INIT;
    if (root()) u_go_active(u_go(root()), false);
}
static Obj cam_for(Obj graphic) {                            /* null for an overlay canvas */
    Obj cv = u_alive(graphic) ? (tm_is(graphic, K_GR) ? F(0x5260544, Obj (*)(Obj, void *))(graphic, NULL) : NULL) : NULL;
    if (!u_alive(cv) || (tm_is(cv, K_CV) ? F(0x5475018, int (*)(Obj, void *))(cv, NULL) : 0) == 0) return NULL;
    return (tm_is(cv, K_CV) ? F(0x5477610, Obj (*)(Obj, void *))(cv, NULL) : NULL);
}
static int hit(Obj comp, V2 m, Obj cam) {
    if (!u_alive(comp) || !u_go_active_self(u_go(comp))) return 0;
    return (tm_is(u_tr(comp), K_RT) ? F(0x5473D40, bool (*)(Obj, V2, Obj, void *))(u_tr(comp), m, cam, NULL) : false);
}
/* button 0/1 at screen point m: 1 = handled */
static int click_at(V2 m, int button) {
    if (prop_click(m, button)) return 1;                     /* the proposal side panel (only clicks on it) */
    Obj hud = u_gc_get(h_hud);
    if (!opened) {
        if (hit(hud, m, cam_for(hud))) { if (button == 0 && open_screen()) notice("The Teams screen is not available here"); return 1; }
        return 0;
    }
    Obj cam = cam_for(root());
    if (hit(hud, m, cam_for(hud))) { if (button == 0) close_screen(); return 1; }   /* right = drag (hud_input) */
    if (hit(u_gc_get(h_close), m, cam)) { close_screen(); return 1; }
    if (button == 0 && hit(u_gc_get(h_info), m, cam)) return 1;   /* the info icon: hover shows "How Teams Work" */
    if (spect()) { if (button == 0) notice("You are watching: spectators cannot change teams"); return 1; }   /* read only */
    model_refresh();
    if (button == 0 && lend_click(m, cam)) return 1;          /* lending chips / menu (also in a dungeon) */
    model_refresh();
    /* no Ready / Go in Teams (owner lan_v8) */
    for (int i = 0; i < TM_MAX_UNITS; i++) {
        Obj p = u_gc_get(por[i].card);
        if (!por[i].card || !hit(p, m, cam)) continue;
        if (!por[i].clickable) return 1;                     /* other players' units: not clickable at all */
        int u = unit_by_guid(por[i].guid);
        if (u >= 0 && button == 0) run(tm_click_unit(&st, u, button, &hsel));   /* right-click: nothing (no marks here) */
        return 1;
    }
    for (int k = 0; k < nhdr; k++)
        if (button == 0 && hit(u_gc_get(h_hdr[k]), m, cam)) { run(tm_click_header(&st, hdr_player[k], &hsel)); return 1; }
    return 1;                                                /* a click on the window or the dim layer */
}

/* HUD entry (owner 2026-10-13): the stock framed top-line button (Recruit popup clone) with the population icon and
   a "Teams" label in the title font, on the top canvas at a fixed place right of the clock / speed buttons, so it
   does not move when the portrait bar grows. Its Button is off: the click is hit-tested in click_at on the whole
   box (frame + label). Shown while the portrait bar is, hidden while mpmenu's lobby panel is open. */
#define HUD_X 480.0f                                        /* clear of the clock / speed buttons (end ~380) */
#define HUD_Y (-46.0f)
#define HUD_W 150.0f
#define HUD_H 40.0f
static uintptr_t h_hud_root;
static int lobby_open(void) {                                /* dlsym: the dylib links with or without mpmenu */
    static int (*fn)(void); static unsigned tries;
    if (!fn && tries++ % 50 == 0) fn = (int (*)(void))dlsym(RTLD_DEFAULT, "mpmenu_lobby_open");
    return fn ? fn() : 0;
}
/* lan_v9 (owner): the Teams button is on its own. It stays above game windows (own Canvas, sorting 31000: above the
   stock popups, below our modal windows), moves by its own right-click drag, and keeps its own place in
   <persistentDataPath>/mp_teams_button.json. First place: right of the player panel (ui_panel_anchor, read once),
   else next to the clock. Compact when uxgame's panel "Compact" size is on (ui_panel_compact, dlsym). */
static void *dl(const char *name) { return dlsym(RTLD_DEFAULT, name); }
static int hud_drag, hud_pos_set, hud_compact = -1, hud_dirty = 1, hud_dragged;   /* dragged: the player chose a place */
static float hud_px, hud_py;                                 /* top-left of the button, canvas units from the top-left */
static V3 hud_last;
static void hud_path(char *out, size_t cap) {
    char d[900]; str_utf8(F(0x507B74C, Obj (*)(void *))(NULL), d, sizeof d);   /* Application.persistentDataPath */
    snprintf(out, cap, "%s/mp_teams_button.json", d);
}
static void hud_load(void) {
    static int loaded; if (loaded) return; loaded = 1;
    char p[1000]; hud_path(p, sizeof p); FILE *f = fopen(p, "r"); if (!f) return;
    char b[200] = ""; size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f);
    double x = 0, y = 0, dr = 0; jnum(b, "dragged", &dr);
    if (dr && jnum(b, "x", &x) && jnum(b, "y", &y)) { hud_px = (float)x; hud_py = (float)y; hud_pos_set = 1; hud_dragged = 1; }   /* else: snapped to the panel */
}
static void hud_save(void) {
    char p[1000]; hud_path(p, sizeof p); FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "{\"x\":%.0f,\"y\":%.0f,\"dragged\":%d}\n", hud_px, hud_py, hud_dragged); fclose(f);
}
static int hud_is_compact(void) {
    static int (*fn)(void); static unsigned tries;
    if (!fn && tries++ % 60 == 0) fn = (int (*)(void))dl("ui_panel_compact");
    return fn ? fn() != 0 : 0;
}
#define HUD_SIZE(c) ((c) ? 28.0f : 34.0f)                    /* frame height (compact / normal) */
#define HUD_WID(c) ((c) ? 88.0f : 112.0f)
static int hud_show = 1;                                     /* hud_frame's rule: shown while the portrait bar is, not in the lobby */
static uintptr_t h_sysmenu;
static int sysmenu_open(void) {                              /* the game's Esc menu (SystemMenuView) is up */
    static unsigned n; static int last;
    if (n++ % 5) return last;
    Obj v = find_view(&h_sysmenu, "Refactor.UI", "SystemMenuView");
    last = v && F(0x50F3724, bool (*)(Obj, void *))(u_go(v), NULL);
    return last;
}
/* the Teams button as drawn, in screen pixels (x, y = bottom-left, y up); 0 = not shown. For ui_game (dlsym): its centred
   boxes (viewer banner, toasts) keep clear of the button. */
__attribute__((visibility("default"))) int ui_teams_button_rect(float *x, float *y, float *w, float *h) {
    Obj hr = u_gc_get(h_hud_root);
    if (!u_alive(hr) || !u_go_active_self(u_go(hr)) || !tm_is(hr, K_RT)) return 0;
    R4 r = rt_rect(hr);
    V3 a = F(0x510E0AC, V3 (*)(Obj, V3, void *))(hr, (V3){ r.x, r.y, 0 }, NULL), b = F(0x510E0AC, V3 (*)(Obj, V3, void *))(hr, (V3){ r.x + r.w, r.y + r.h, 0 }, NULL);
    *x = a.x; *y = a.y; *w = b.x - a.x; *h = b.y - a.y;          /* overlay canvas: world = screen pixels */
    return 1;
}
static void hud_place(void) {
    Obj hr = u_gc_get(h_hud_root);
    if (!u_alive(hr)) return;
    int want = hud_show && !sysmenu_open()                   /* lan_v9: hidden while the Esc menu is open (it covered "Load Campaign") */
               && !opened && !prop_shown                     /* and while our own windows are (Teams + its lend menu, proposal): lead 2026-10-14 */
               && !busy();                                   /* and on the loading screen (map change, load): gamelane 2026-10-14 */
    if (u_go_active_self(u_go(hr)) != want) u_go_active(u_go(hr), want);
    if (!want) return;
    hud_load();
    Obj ct = u_tr_parent(hr); R4 cr = ct ? rt_rect(ct) : (R4){0, 0, 1920, 1080};
    if (!hud_dragged) {                                      /* never dragged: snapped right of the player panel, any panel size */
        static int (*anchor)(float *, float *, float *, float *);
        if (!anchor) anchor = (int (*)(float *, float *, float *, float *))dl("ui_panel_anchor");
        float x, y, w, h;
        if (anchor && anchor(&x, &y, &w, &h)) { hud_px = cr.w / 2 + x + w + 8; hud_py = y; hud_pos_set = 1; }
        else if (!hud_pos_set) { hud_px = HUD_X; hud_py = HUD_Y; }
    }
    int c = hud_is_compact();
    float W = HUD_WID(c), H = HUD_SIZE(c);
    if (hud_px < 0) hud_px = 0; if (hud_px > cr.w - W) hud_px = cr.w - W;          /* stay on screen */
    if (hud_py > 0) hud_py = 0; if (hud_py < -(cr.h - H)) hud_py = -(cr.h - H);
    static float last[4] = {-1}; float now[4] = { hud_px, hud_py, W, (float)c };
    if (!hud_dirty && !memcmp(now, last, sizeof now) && c == hud_compact) return;
    memcpy(last, now, sizeof now); hud_compact = c; hud_dirty = 0;
    rt_anchor(hr, 0, 1, 0, 1); rt_pivot(hr, 0, 1); rt_size(hr, W, H); rt_pos(hr, hud_px, hud_py);
    int n = (tm_is(hr, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(hr, NULL) : 0);
    for (int i = 0; i < n; i++) {
        Obj ch = (tm_is(hr, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(hr, i, NULL) : NULL);
        Obj tx = ch ? tmp_of(ch) : NULL;
        if (!ch) continue;
        if (!tx) { rt_size(ch, H, H); Obj ic = img_of(tr_find(ch, "Img_ExitIcon")); if (u_alive(ic)) rt_size(u_tr(ic), H * 0.7f, H * 0.6f); }
        else { rt_size(ch, W - H - 6, H); rt_pos(ch, H + 6, 0); (tm_is(tx, K_TMP) ? F(0x4F0B340, void (*)(Obj, float, void *))(tx, c ? 15.0f : 18.0f, NULL) : (void)0); }
    }
}
static void hud_move(float dx, float dy) {                   /* screen pixels -> canvas units */
    Obj hr = u_gc_get(h_hud_root);
    float sc = u_alive(hr) ? canvas_scale(hr) : 1; if (sc <= 0) sc = 1;
    float x0 = hud_px, y0 = hud_py;
    hud_px += dx / sc; hud_py += dy / sc; hud_pos_set = 1; hud_dragged = 1;
    hud_place();
    rig_log("teams: HUD move %.0f,%.0f px (scale %.3f): %.0f,%.0f -> %.0f,%.0f", dx, dy, sc, x0, y0, hud_px, hud_py);
}
static void hud_input(void) {                                   /* right-drag (the button only) + tooltip; every frame */
    Obj hud = u_gc_get(h_hud);
    if (!u_alive(hud) || !u_go_active_self(u_go(u_gc_get(h_hud_root)))) { hud_drag = 0; return; }
    V3 mp = F(0x51AD4DC, V3 (*)(void *))(NULL);
    int over = hit(hud, (V2){mp.x, mp.y}, cam_for(hud));
    static void (*tip)(const char *); static unsigned tries;
    if (!tip && tries++ % 60 == 0) tip = (void (*)(const char *))dl("ui_tooltip");
    if (!hud_drag && over && F(0x51ACDD0, bool (*)(int, void *))(1, NULL)) { hud_drag = 1; hud_last = mp; }   /* right button down */
    if (hud_drag) {
        if (!F(0x51ACD80, bool (*)(int, void *))(1, NULL)) { hud_drag = 0; hud_save(); return; }          /* released: saved */
        float dx = mp.x - hud_last.x, dy = mp.y - hud_last.y;
        if (dx || dy) hud_move(dx, dy);
        hud_last = mp;
        return;
    }
    (void)tip; (void)over;                                    /* the button's tooltip: tips_frame */
}
static void hud_frame(void) {
    Obj v = shortcut_view();
    Obj hr = u_gc_get(h_hud_root);
    int show = v && (tm_is(u_go(v), K_GO) ? F(0x50F3724, bool (*)(Obj, void *))(u_go(v), NULL) : false) && !lobby_open();   /* activeInHierarchy */
    hud_show = show;
    if (u_alive(hr) && u_alive(u_gc_get(h_hud))) { hud_place(); return; }
    u_gc_free(h_hud); h_hud = 0; u_gc_free(h_hud_root); h_hud_root = 0;
    if (!show || load_templates()) return;
    Obj ct = top_canvas_tr();
    Obj box = ct ? new_rect(ct, 0, 1, 0, 1, HUD_W, HUD_H, (C4){0, 0, 0, 0}) : NULL;   /* invisible click area */
    if (!box) return;
    Obj rt = u_tr(box);
    g_raycast(box, true);                                        /* so the click does not reach the world */
    rt_pos(rt, HUD_X, HUD_Y);
    float lx = 0;
    Obj tpl = u_gc_get(h_tpl_btn);
    if (u_alive(tpl)) {
        Obj fr = u_instantiate(tpl, rt);
        if (u_alive(fr)) {
            button_off(fr);
            rt_anchor(fr, 0, 0.5f, 0, 0.5f); rt_pivot(fr, 0, 0.5f); rt_size(fr, HUD_H, HUD_H); rt_pos(fr, 0, 0); tr_scale(fr, 1.0f);
            Obj ic = img_of(tr_find(fr, "Img_ExitIcon"));
            if (u_alive(ic)) { img_sprite(ic, u_gc_get(h_spr_pop)); rt_size(u_tr(ic), 28, 24); g_raycast(ic, false); }
            Obj fi = img_of(fr); if (u_alive(fi)) g_raycast(fi, false);
            lx = HUD_H + 8;
        }
    }
    Obj t = new_text(rt, HUD_W - lx, HUD_H, 22, AL_LEFT);
    if (t) { rt_anchor(u_tr(t), 0, 0.5f, 0, 0.5f); rt_pivot(u_tr(t), 0, 0.5f); rt_pos(u_tr(t), lx, 0);
             tmp_font(t, u_gc_get(h_font_title)); tmp_set(t, "Teams"); }
    u_last_sibling(rt);
    { Obj cty = type_object("UnityEngine", "Canvas"), gty = type_object("UnityEngine.UI", "GraphicRaycaster");
      Obj cv = cty ? (tm_is(u_go(rt), K_GO) ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(rt), cty, NULL) : NULL) : NULL;
      if (u_alive(cv)) { (tm_is(cv, K_CV) ? F(0x5476208, void (*)(Obj, bool, void *))(cv, true, NULL) : (void)0);        /* overrideSorting */
                         (tm_is(cv, K_CV) ? F(0x547640C, void (*)(Obj, int, void *))(cv, 31000, NULL) : (void)0); }      /* above the stock popups */
      if (gty) (void)(tm_is(u_go(rt), K_GO) ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(rt), gty, NULL) : NULL); }
    h_hud = u_gc_new(box); h_hud_root = u_gc_new(rt); hud_dirty = 1;   /* a new button: place it again */
    rig_log("teams: HUD button added on the top canvas (%s, %.0fx%.0f at %.0f,%.0f)", lx > 0 ? "top-line frame + icon" : "label only", HUD_W, HUD_H, HUD_X, HUD_Y);
    hud_place();
}

/* ---------------------------------------------------------------- stock "Go to dungeon" (owner 2026-10-13)
   ExpeditionPlanView.InputOnAccept(DungeonRegion, List<Guid> units) 0xEE3A50 only sends UIInput 102 (the map
   change). In multiplayer it instead sends the panel's region and party as an expedition proposal (accept_core:
   PROPOSE + Ready with your units) and closes the panel (Close 0xEE3384); every player then answers in the side panel. */
/* ---------------------------------------------------------------- the stock expedition window in the proposal flow
   ExpeditionPlanPresenter (PopUpType 2): +0x10 view, +0x38 PopUpState (0 region, 1 supplies), +0x3C region,
   +0x48 List<Guid> selected units (also written to ExpeditionPlanPartyContext, which the orders lane carries
   with every expedition item move). View: +0x68 lower panel (+0x20 Back, +0x28 Next button).
   mode 1 = proposer (Teams Go): region + his marked units, straight to the supplies step, normal buttons;
   mode 2 = receiver: the same with the proposal's region, title "Expedition Proposal", no Back, Next -> summary,
   and a side list of the other players' going units. */
static uintptr_t h_pres, h_title_orig, h_side, h_side_time;
static char side_time_s[160];                                /* the Going block's time line (state / tests) */
static int stock_mode, stock_req;
static void (*o_pact)(Obj, void *), (*o_pnext)(Obj, void *), (*o_pdeact)(Obj, void *);
static int show_popup(int type);
static Obj pres_view(void) { Obj p = u_gc_get(h_pres); return p ? AT(p, 0x10, Obj) : NULL; }
static Obj stock_title(Obj view) { return view ? tmp_of(tr_find(u_tr(view), "Panel_BackGroundBox/Txt_UpperExpeditionTitle")) : NULL; }
static Obj next_label(Obj view) {
    Obj lp = view ? AT(view, 0x68, Obj) : NULL, nb = lp ? AT(lp, 0x28, Obj) : NULL;
    Obj tty = type_object("TMPro", "TMP_Text");
    return nb && tty ? (tm_is(nb, K_COMP) ? F(0x50EDDF4, Obj (*)(Obj, Obj, bool, void *))(nb, tty, false, NULL) : NULL) : NULL;
}
/* receiver: "Going" block over the last unit panel (always empty for him: the proposer takes at least one of the
   4 places): Cinzel header, one row per other player (name in his colour + small stock cards of his going units) */
static char side_s[400];
static void stock_side_block(Obj view) {
    Obj cip = view ? AT(view, 0x58, Obj) : NULL, arr = cip ? AT(cip, 0x20, Obj) : NULL;
    int np = arr ? (int)AT(arr, 0x18, uint64_t) : 0;
    Obj panel = np > 0 ? AT(arr, 0x20 + 8 * (np - 1), Obj) : NULL;
    /* the visible slot box: ExpeditionInventoryPanel_<n-1> (the panel script may sit on a child that hides itself
       while "Not Selected" shows) */
    char path[160]; snprintf(path, sizeof path, "Panel_BackGroundBox/Panel_MiddleExpedition/SubUI_CheckInventoryPanel/Panel_UnitInventoriesPlace/ExpeditionInventoryPanel%s%d",
                             np > 1 ? "_" : "", np - 1);
    Obj ptr = tr_find(u_tr(view), np > 1 ? path : "Panel_BackGroundBox/Panel_MiddleExpedition/SubUI_CheckInventoryPanel/Panel_UnitInventoriesPlace/ExpeditionInventoryPanel");
    if (!ptr && panel) ptr = u_tr(panel);
    if (!ptr) return;
    R4 pr = rt_rect(ptr);
    /* not a child of the panel (the stock refresh toggles the panel's children): a child of the step's root,
       laid over the panel at its world position, last sibling */
    Obj host = tr_find(u_tr(view), "Panel_BackGroundBox/Panel_MiddleExpedition/SubUI_CheckInventoryPanel");
    if (!host) host = u_tr_parent(u_tr_parent(ptr));
    Obj blk = new_rect(host, 0.5f, 0.5f, 0.5f, 0.5f, pr.w, pr.h, hexc("#2d2a31", 1));
    if (!blk) return;
    g_raycast(blk, true);
    Obj bt = u_tr(blk);
    V3 wp = (tm_is(ptr, K_TR) ? F(0x5109A08, V3 (*)(Obj, void *))(ptr, NULL) : (V3){0, 0, 0});
    (tm_is(bt, K_TR) ? F(0x5109B04, void (*)(Obj, V3, void *))(bt, wp, NULL) : (void)0);   /* Transform.position */
    u_last_sibling(bt);
    u_gc_free(h_side); h_side = u_gc_new(blk);
    float top = pr.h / 2, w = pr.w;
    Obj t = new_text(bt, w - 20, 34, 22, AL_CENTER);
    if (t) { tmp_font(t, u_gc_get(h_font_title)); rt_pos(u_tr(t), 0, top - 26); tmp_set(t, "<color=" C_MAIN ">Going</color>"); }
    model_refresh();
    char pn_[40]; pname(prop.proposer, pn_);
    Obj t2 = new_text(bt, w - 20, 24, 14, AL_CENTER);
    char tx[300]; snprintf(tx, sizeof tx, "<color=" C_SEC ">%s \xC2\xB7 proposed by </color><color=%s>%s</color>", region_name(prop.region), ui_player_color(prop.proposer), pn_);
    if (t2) { rt_pos(u_tr(t2), 0, top - 52); tmp_set(t2, tx); }
    Obj t3 = new_text(bt, w - 20, 26, 17, AL_CENTER);          /* the time left (ux_flow F5), set once a second by stock_countdown */
    u_gc_free(h_side_time); h_side_time = t3 ? u_gc_new(t3) : 0;
    if (t3) { rt_pos(u_tr(t3), 0, top - 78); g_raycast(t3, true); }
    float y = top - 100, cs = 0.75f;
    size_t sl = 0; side_s[0] = 0;
    for (int p = 0; p < TM_MAX_PLAYERS; p++) {
        if (!(st.present >> p & 1u) || p == st.me) continue;
        char nm[40]; pname(p, nm);
        Obj tn = new_text(bt, w - 24, 26, 18, AL_LEFT);
        snprintf(tx, sizeof tx, "<color=%s>%s</color>", ui_player_color(p), nm);
        if (tn) { rt_pos(u_tr(tn), 0, y - 13); tmp_set(tn, tx); }
        sl += (size_t)snprintf(side_s + sl, sizeof side_s - sl, "%s%s:", sl ? "\n" : "", nm);
        int k = 0;
        for (int u = 0; u < st.nunits; u++) {
            if (!goes_for(u, p)) continue;
            Obj c = u_instantiate(u_gc_get(h_tpl_card), bt);
            Obj pp = comp_of(c, &h_card_type, "Refactor.UI", "SubUI_PolicyPortrait");
            if (u_alive(pp)) {
                centre_anchor(u_tr(pp));
                (tm_is(pp, K_PP) ? F(0xE95EE8, void (*)(Obj, Obj, void *))(pp, st_unit[u], NULL) : (void)0);
                tr_scale(u_tr(pp), cs);
                rt_pos(u_tr(pp), -w / 2 + 14 + card_w * cs / 2 + k * (card_w * cs + 6), y - 30 - card_h * cs / 2);
            }
            char un[64] = ""; Obj nmo = st_unit[u] ? F(0xFA467C, Obj (*)(Obj, void *))(st_unit[u], NULL) : NULL; str_utf8(nmo, un, sizeof un);
            sl += (size_t)snprintf(side_s + sl, sizeof side_s - sl, "%s %s", k ? "," : "", un);
            k++;
        }
        if (!k) { Obj tv = new_text(bt, w - 24, 24, 14, AL_LEFT); if (tv) { rt_pos(u_tr(tv), 0, y - 44); tmp_set(tv, "<color=" C_SEC ">No units: watches</color>"); }
                  sl += (size_t)snprintf(side_s + sl, sizeof side_s - sl, " no units"); }
        y -= 30 + card_h * cs + 14;
    }
    rig_log("teams: Going block on the last unit panel (%.0fx%.0f): %s", pr.w, pr.h, side_s);
}
static void stock_setup(Obj pres, int mode) {
    model_refresh();
    int region = mode == 2 && prop.active ? prop.region : region_default();
    F(0xEE2890, void (*)(Obj, int, void *))(pres, region, NULL);                       /* OnSelectRegion */
    if (mode == 3) { rig_log("teams: stock expedition window, plain (step 1)"); return; }   /* like the game's Go to dungeon */
    for (int guard = 0; guard < 16; guard++) {                                         /* clear the previous party */
        Obj list = AT(pres, 0x48, Obj);
        if (!list || AT(list, 0x18, int32_t) <= 0) break;
        uint64_t g[2]; memcpy(g, (char *)AT(list, 0x10, Obj) + 0x20, 16);
        F(0xEE2728, void (*)(Obj, uint64_t, uint64_t, void *))(pres, g[0], g[1], NULL);   /* OnUnitUnselected */
    }
    /* proposer: every present player's marked units (allies' too: their owners approve them); receiver: his part */
    uint8_t sel[TM_PARTY_MAXN][16]; int ns = 0;
    if (mode == 2) ns = my_part(sel);
    else for (int p = 0; p < TM_MAX_PLAYERS; p++) {
        if (!(st.present >> p & 1u)) continue;
        for (int k = 0; k < st.nmarked[p] && ns < TM_PARTY_MAXN; k++) {
            int u = -1; for (int i = 0; i < st.nunits; i++) if (!memcmp(st.guid[i], st.marked[p][k], 16)) u = i;
            if (u >= 0 && st.owner[u] == p) memcpy(sel[ns++], st.marked[p][k], 16);
        }
    }
    int n = 0;
    for (int k = 0; k < ns; k++) {
        uint64_t g[2]; memcpy(g, sel[k], 16);
        F(0xEE250C, void (*)(Obj, uint64_t, uint64_t, void *))(pres, g[0], g[1], NULL);   /* OnUnitSelected */
        n++;
    }
    F(0xEE3428, void (*)(Obj, void *))(pres, NULL);                                      /* OnClickNext: to the supplies step */
    Obj view = AT(pres, 0x10, Obj);
    if (mode == 2) {
        Obj tt = stock_title(view);
        if (tt) { u_gc_free(h_title_orig); h_title_orig = u_gc_new(tmp_get(tt)); tmp_set(tt, "Expedition Proposal"); }
        Obj lp = view ? AT(view, 0x68, Obj) : NULL, bb = lp ? AT(lp, 0x20, Obj) : NULL;
        if (u_alive(bb)) u_active(bb, false);
        Obj nl = next_label(view); if (nl) tmp_set(nl, "Next");
        stock_side_block(view);
        stock_countdown();
    }
    rig_log("teams: stock expedition window, %s: region %d, %d units, state %d", mode == 2 ? "receiver (Expedition Proposal)" : "proposer",
            region, n, AT(pres, 0x38, int32_t));
}
/* ux_flow F5: the receiver packing sees the time left, once a second, under the Going title (red in the last 10 s;
   the window title has no room: "Expedition Proposal · 55 s left" wrapped above the window at 960 x 540) */
static void stock_countdown(void) {
    Obj t = u_gc_get(h_side_time);
    if (stock_mode != 2 || !u_alive(t)) return;
    int l = prop_secs_left();
    snprintf(side_time_s, sizeof side_time_s, "<color=%s>%d s left</color><color=" C_SEC "> to pack and answer</color>", l <= 10 ? C_WARN : C_ACC, l);
    tmp_set(t, side_time_s);
}
static void stock_restore(void) {                            /* the view is pooled: put the stock look back */
    Obj view = pres_view();
    Obj tt = stock_title(view), orig = u_gc_get(h_title_orig);
    if (tt && orig && tm_is(tt, K_TMP)) { void *k = *(void **)tt; (*(void (**)(Obj, Obj, void *))((char *)k + 0x558))(tt, orig, *(void **)((char *)k + 0x560)); }
    Obj lp = view ? AT(view, 0x68, Obj) : NULL, bb = lp ? AT(lp, 0x20, Obj) : NULL;
    if (u_alive(bb)) u_active(bb, true);
    Obj sd = u_gc_get(h_side); if (u_alive(sd)) u_destroy(u_go(sd));
    u_gc_free(h_side); h_side = 0; side_s[0] = 0; u_gc_free(h_title_orig); h_title_orig = 0;
    u_gc_free(h_side_time); h_side_time = 0; side_time_s[0] = 0;
}
static void hk_pact(Obj pres, void *mi) {
    o_pact(pres, mi);
    u_gc_free(h_pres); h_pres = u_gc_new(pres);
    if (stock_req) { stock_mode = stock_req; stock_req = 0; stock_setup(pres, stock_mode); }
}
/* owner lan_v8: step 2 (supplies) shows and modifies ONLY your own units, host included. The step-1 party (allies'
   units too) is kept as a draft: allies' units are unselected before the step changes, Back puts them back,
   and the proposal sends the whole draft. */
static uint8_t draft[TM_PARTY_MAXN][16];
static int draft_n;
static void (*o_pback)(Obj, void *);
static void hk_pback(Obj pres, void *mi) {
    int st1 = AT(pres, 0x38, int32_t) == 1;
    o_pback(pres, mi);
    if (st1 && draft_n && stock_mode != 2) {                  /* back to step 1: the allies' picks again */
        Obj list = AT(pres, 0x48, Obj);
        for (int i = 0; i < draft_n; i++) {
            int in = 0; Obj arr = list ? AT(list, 0x10, Obj) : NULL; int n = list ? AT(list, 0x18, int32_t) : 0;
            for (int k = 0; arr && k < n; k++) if (!memcmp((char *)arr + 0x20 + 16 * k, draft[i], 16)) in = 1;
            if (in) continue;
            uint64_t q[2]; memcpy(q, draft[i], 16);
            F(0xEE250C, void (*)(Obj, uint64_t, uint64_t, void *))(pres, q[0], q[1], NULL);
        }
        draft_n = 0;
    }
}
static void hk_pnext(Obj pres, void *mi) {
    if (sess() && stock_mode != 2 && AT(pres, 0x38, int32_t) == 0) {   /* step 1 -> 2: keep the party, show only mine */
        model_refresh();
        Obj list = AT(pres, 0x48, Obj); Obj arr = list ? AT(list, 0x10, Obj) : NULL; int n = list ? AT(list, 0x18, int32_t) : 0;
        draft_n = 0;
        for (int k = 0; arr && k < n && draft_n < TM_PARTY_MAXN; k++) memcpy(draft[draft_n++], (char *)arr + 0x20 + 16 * k, 16);
        int off = 0;
        for (int i = 0; i < draft_n; i++) {
            int u = -1; for (int j = 0; j < st.nunits; j++) if (!memcmp(st.guid[j], draft[i], 16)) u = j;
            if (u < 0 || st.owner[u] == st.me || st.owner[u] == TM_FREE) continue;   /* mine, or free (taken for me) */
            uint64_t q[2]; memcpy(q, draft[i], 16);
            F(0xEE2728, void (*)(Obj, uint64_t, uint64_t, void *))(pres, q[0], q[1], NULL);   /* OnUnitUnselected */
            off++;
        }
        rig_log("teams: step 2 shows only my units (%d of %d; %d allies' units kept for the proposal)", draft_n - off, draft_n, off);
        if (!off) draft_n = 0;                                 /* nothing hidden: no draft needed */
    }
    if (stock_mode == 2 && AT(pres, 0x38, int32_t) == 1) {   /* receiver: Next -> the summary (no map change) */
        Obj v = AT(pres, 0x10, Obj);
        if (u_alive(v)) F(0xEE3384, void (*)(Obj, void *))(v, NULL);                      /* ExpeditionPlanView.Close */
        if (pstage == ST_SUPPLIES) pstage = ST_SUMMARY;
        rig_log("teams: receiver pressed Next: the proposal summary");
        return;
    }
    o_pnext(pres, mi);
}
static void hk_pdeact(Obj pres, void *mi) {
    o_pdeact(pres, mi);
    draft_n = 0;
    if (stock_mode == 2) { stock_restore(); if (pstage == ST_SUPPLIES) pstage = ST_SUMMARY; }
    stock_mode = 0;
}
static int stock_open(int mode) {                            /* open the stock window for the proposal flow */
    if (close_teams || opened) { close_screen(); close_teams = 0; }
    stock_req = mode;
    if (show_popup(2)) { stock_req = 0; return -1; }
    return 0;
}
/* gear rule "own storage" (owner rule): in the stock window's storage list, items owned by another player are
   greyed (CanvasGroup 0.35) and not usable (the slot script and its InteractableUI off: no click, no drag) */
int dsnet_may_use_item(dsnet_client *c, int player, const uint8_t item[16]) __attribute__((weak_import));
int dsnet_gear_mode(dsnet_client *c) __attribute__((weak_import));
static int gear_greyed;
static uintptr_t h_notice;
static void stock_gear_frame(void) {
    Obj v = pres_view(); dsnet_client *c = cli();
    /* owner lan_v7: the unresolved-warnings prompt of the stock window (CommonNoticeView, key
       TEXTKEY_ExpeditionPlan_CriticalConfirm, +0x90 _textKey, +0x30 _noticeText) -> "Send the proposal to your friends anyway?" */
    Obj nv = find_view(&h_notice, "Refactor.UI", "CommonNoticeView");
    if (c && nv && F(0x50F3724, bool (*)(Obj, void *))(u_go(nv), NULL)) {
        char key[80] = ""; str_utf8(AT(nv, 0x90, Obj), key, sizeof key);
        Obj tx = AT(nv, 0x30, Obj);
        if (!strcmp(key, "TEXTKEY_ExpeditionPlan_CriticalConfirm") && u_alive(tx)) {
            char cur[120] = ""; str_utf8(tmp_get(tx), cur, sizeof cur);
            if (strcmp(cur, "Send the proposal to your friends anyway?")) tmp_set(tx, "Send the proposal to your friends anyway?");
        }
    }
    if (!u_alive(v) || !c || !F(0x50F3724, bool (*)(Obj, void *))(u_go(v), NULL)) return;
    /* owner lan_v7: the last button says what it does in multiplayer */
    Obj pr = u_gc_get(h_pres);
    if (pr && AT(pr, 0x38, int32_t) == 1) {
        Obj nl = next_label(v); const char *want = stock_mode == 2 ? "Next" : "Send to friends";
        char cur[40] = ""; if (nl) str_utf8(tmp_get(nl), cur, sizeof cur);
        if (nl && strcmp(cur, want)) tmp_set(nl, want);
    }
    if (!dsnet_may_use_item) return;
    Obj cip = AT(v, 0x58, Obj), ep = cip ? AT(cip, 0x28, Obj) : NULL, list = ep ? AT(ep, 0x50, Obj) : NULL;
    if (!list) return;
    Obj arr = AT(list, 0x10, Obj); int n = AT(list, 0x18, int32_t), g = 0, m = me();
    Obj cgt = u_gc_get(h_cg_type) ? u_gc_get(h_cg_type) : (h_cg_type = u_gc_new(type_object("UnityEngine", "CanvasGroup")), u_gc_get(h_cg_type));
    for (int i = 0; arr && i < n && i < 512; i++) {
        Obj sl = AT(arr, 0x20 + 8 * i, Obj);
        if (!u_alive(sl)) continue;
        const uint8_t *item = (const uint8_t *)sl + 0x160;
        static const uint8_t z[16];
        int ok = !memcmp(item, z, 16) || dsnet_may_use_item(c, m, item);
        g += !ok;
        Obj cg = go_component(u_go(sl), cgt);
        if (!u_alive(cg) && !ok) cg = (tm_is(u_go(sl), K_GO) ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(sl), cgt, NULL) : NULL);
        if (u_alive(cg)) (tm_is(cg, K_CG) ? F(0x546FA94, void (*)(Obj, float, void *))(cg, ok ? 1.0f : 0.35f, NULL) : (void)0);
        if (tm_is(sl, K_BEH)) u_enabled(sl, ok);
        Obj ia = AT(sl, 0x158, Obj); if (u_alive(ia) && tm_is(ia, K_BEH)) u_enabled(ia, ok);
    }
    if (g != gear_greyed) rig_log("teams: storage list: %d item(s) of other players greyed (own storage)", g);
    gear_greyed = g;
}
/* for the player panel (ui_game, dlsym): 1 while a Teams window, the proposal or the stock window in the proposal flow is up */
/* the proposal side panel is not modal (lan_v10): the player panel, hotkeys and Escape stay as without it */
__attribute__((visibility("default"))) int ui_teams_modal_open(void) { return opened || stock_mode == 2; }
static void stock_close(void) { Obj v = pres_view(); if (stock_mode && u_alive(v)) F(0xEE3384, void (*)(Obj, void *))(v, NULL); }
/* owner lan_v7: opening the expedition plan is local. The portal interaction runs in the simulation on every peer
   (PortalOrder.Open 0xC050B0, Interaction by reference: +0x28 UserEntity); only the player who controls the unit
   opens the window. */
int dsnet_controller_of(dsnet_client *c, const uint8_t unit[16]) __attribute__((weak_import));
static void (*o_portal)(Obj, void *, void *);
static void hk_portal(Obj self, void *data, void *mi) {
    dsnet_client *c = cli();
    Obj user = data ? *(Obj *)((char *)data + 0x28) : NULL;
    if (c && sess() && user && dsnet_controller_of && unit_faction(user) == 1) {
        int ctl = dsnet_controller_of(c, (const uint8_t *)user + UNIT_GUID);
        if (ctl != me()) { rig_log("teams: portal used by player %d's unit: the plan opens on his screen only", ctl); return; }
    }
    o_portal(self, data, mi);
}
static void (*o_accept)(Obj, int, Obj, void *);
static void accept_core(Obj view, int region, const uint8_t (*pn)[16], int np);
static void hk_accept(Obj view, int region, Obj units, void *mi) {
    if (!sess()) { o_accept(view, region, units, mi); return; }   /* single player: stock */
    if (spect()) { notice("You are watching: only players plan expeditions"); if (u_alive(view)) F(0xEE3384, void (*)(Obj, void *))(view, NULL); return; }
    if (stock_mode == 2) { if (u_alive(view)) F(0xEE3384, void (*)(Obj, void *))(view, NULL); if (pstage == ST_SUPPLIES) pstage = ST_SUMMARY; return; }
    model_refresh();
    if (st.in_dungeon) { o_accept(view, region, units, mi); return; }
    uint8_t pn[64][16]; int np = 0;
    if (units) {
        Obj arr = AT(units, 0x10, Obj); int cnt = AT(units, 0x18, int32_t);
        for (int i = 0; arr && i < cnt && np < 64; i++) memcpy(pn[np++], (const uint8_t *)arr + 0x20 + 16 * i, 16);
    }
    accept_core(view, region, (const uint8_t (*)[16])pn, np);
}
static void accept_core(Obj view, int region, const uint8_t (*pn)[16], int np) {
    model_refresh();
    /* owner bug 2026-10-13: never start the map change from here in multiplayer, even before the session runs */
    if (!mp_running()) { notice("Multiplayer is not running yet: the expedition cannot start"); return; }
    rig_log("teams: Go to dungeon pressed (region %d, panel %d units)", region, np);
    if (draft_n) { pn = (const uint8_t (*)[16])draft; np = draft_n; }   /* the whole step-1 party (allies too) */
    if (!propose(region, pn, np)) notice("Expedition proposed: every player must approve.");
    draft_n = 0;
    if (u_alive(view)) F(0xEE3384, void (*)(Obj, void *))(view, NULL);   /* ExpeditionPlanView.Close */
}
/* World clicks under the open screen (owner 2026-10-13). Every mouse state of the game (left / right click,
   drag, hover, scroll, cursor) asks Util.UnityUtil.IsPointerOverUI 0xE54BA4 before it turns a click into a world
   command (SelectGrid / InteractGrid ...). The pair test showed the EventSystem raycast misses our backdrop, so
   while the screen is open this answers "over UI" for the whole screen: no click reaches the world. The proposal
   side panel (lan_v10) answers "over UI" only while the pointer is on it. */
static bool (*o_over_ui)(void *);
static bool hk_over_ui(void *mi) {
    if (opened && root()) return true;
    if (prop_shown) { V3 mp = F(0x51AD4DC, V3 (*)(void *))(NULL); if (prop_over((V2){ mp.x, mp.y })) return true; }   /* side panel: only over it */
    return o_over_ui(mi);
}
static int hooks_done;
static void install_hooks(void) {
    if (hooks_done) return;
    hooks_done = 1;
    rig_hook(0xEE3A50, hk_accept, (void **)&o_accept, "teams ExpeditionPlanView.InputOnAccept");
    rig_hook(0xEE09F4, hk_pact, (void **)&o_pact, "teams ExpeditionPlanPresenter.OnActivateView");
    rig_hook(0xC050B0, hk_portal, (void **)&o_portal, "teams PortalOrder.Open (plan opens locally)");
    rig_hook(0xEE3428, hk_pnext, (void **)&o_pnext, "teams ExpeditionPlanPresenter.OnClickNext");
    rig_hook(0xEE3008, hk_pback, (void **)&o_pback, "teams ExpeditionPlanPresenter.OnClickBack");
    rig_hook(0xEE1764, hk_pdeact, (void **)&o_pdeact, "teams ExpeditionPlanPresenter.OnDeactivateView");
    rig_hook(0xE54BA4, hk_over_ui, (void **)&o_over_ui, "teams UnityUtil.IsPointerOverUI");
}
/* GameAssembly add-image callback (rig_main.c, after ui_install): hooks at load, before game code runs.
   ui_teams_frame installs them late only if this was not called. */
#ifdef TEAMS_MOUSE_TEST
/* Test build only (-DTEAMS_MOUSE_TEST): a fake mouse fed into the game's real input path. UnityEngine.Input
   GetMouseButton / Down / Up and mousePosition return scripted values while a fake click runs, so the stock
   MouseInputDispatcher -> RightClickState -> UnityUtil.IsPointerOverUI -> InteractGrid / InteractUI chain is
   what decides. ui_teams {action:"mouse", x, y, button}. */
static bool (*o_mb)(int, void *), (*o_mbd)(int, void *), (*o_mbu)(int, void *);
static V3 (*o_mpos)(void *);
static int fk_on, fk_btn; static float fk_x, fk_y; static int64_t fk_start;
/* a real pointer rests on the spot before it presses: FK_HOVER frames of position only, then the press. A press in the
   first frame at a new spot made the stock right-click throw (NRE in InteractionOptionControl.GetQuickAction: the hover
   option is computed a frame later; gamelane 2026-10-14), which no real mouse can do. */
#define FK_HOVER 10
static int fk_phase(void) { return fk_on ? (int)(rig_frames - fk_start) : -1000; }   /* <0 hover, 0 = down frame, 1 = up frame */
static bool hk_mb(int b, void *mi) { int ph = fk_phase(); if (ph == -1000) return o_mb(b, mi); return ph == 0 && b == fk_btn; }
static bool hk_mbd(int b, void *mi) { int ph = fk_phase(); if (ph == -1000) return o_mbd(b, mi); return ph == 0 && b == fk_btn; }
static bool hk_mbu(int b, void *mi) { int ph = fk_phase(); if (ph == -1000) return o_mbu(b, mi); return ph == 1 && b == fk_btn; }
static V3 hk_mpos(void *mi) { if (fk_on) return (V3){fk_x, fk_y, 0}; return o_mpos(mi); }
static void fake_frame(void) { if (fk_on && fk_phase() > 100) fk_on = 0; }   /* hold the position ~100 frames, then real mouse */
#endif
void ui_teams_install(void) {
    install_hooks();
#ifdef TEAMS_MOUSE_TEST
    rig_hook(0x51ACD80, hk_mb, (void **)&o_mb, "teams test Input.GetMouseButton");
    rig_hook(0x51ACDD0, hk_mbd, (void **)&o_mbd, "teams test Input.GetMouseButtonDown");
    rig_hook(0x51ACE20, hk_mbu, (void **)&o_mbu, "teams test Input.GetMouseButtonUp");
    rig_hook(0x51AD4DC, hk_mpos, (void **)&o_mpos, "teams test Input.mousePosition");
#endif
}
/* rig socket test: the same call the panel's Accept makes (ui_teams {action:"accept", region}) */
static int debug_accept(int region) {
    uintptr_t h = 0; Obj v = find_view(&h, "Refactor.UI", "ExpeditionPlanView");
    rig_log("teams: debug accept: view %p", v);
    F(0xEE3A50, void (*)(Obj, int, Obj, void *))(v, region, NULL, NULL);   /* through the hook */
    u_gc_free(h);
    return 0;
}

/* ---------------------------------------------------------------- tooltips (owner lan_v9; research/tooltips_v9.md 3.7, 3.8,
   3.9 and 3.11): our explanations in the game's own tooltip window (ui_tip.c game_tip_show_rect, every frame while
   hovered; it honours "Show tooltips on hover"). */
void game_tip_show_rect(const char *title, const char *body, void *rect_transform) __attribute__((weak_import));
void game_tip_show(const char *title, const char *body) __attribute__((weak_import));
void game_tip_frame(void) __attribute__((weak_import));
static char tip_last[80], tip_body_last[400];
static uintptr_t h_tip_comp;                                  /* the element of the last tooltip (tests keep it shown) */
/* ui_tip.c says when its game addresses are right for this build (int game_tip_ready(void)); until then no calls:
   its first RVAs (0xC5xxxx) were not this build's TooltipUI (0xF4xxxx) and crashed two copies (2026-10-14 01:09-01:17) */
static int tips_ready(void) {
    static int (*fn)(void); static unsigned tries;
    if (!fn && tries++ % 120 == 0) fn = (int (*)(void))dlsym(RTLD_DEFAULT, "game_tip_ready");
    return fn && fn();
}
static int tip_on(Obj comp, const char *title, const char *body) {
    if (!u_alive(comp) || !game_tip_show_rect) return 0;
    snprintf(tip_last, sizeof tip_last, "%s", title);            /* the element and its text are chosen (state / tests) */
    snprintf(tip_body_last, sizeof tip_body_last, "%s", body); u_gc_free(h_tip_comp); h_tip_comp = u_gc_new(comp);
    if (!tips_ready()) return 1;
    game_tip_show_rect(title, body, u_tr(comp));              /* next to the element, as the game's own buttons */
    return 1;
}
static const char *conn_body(int lv) {
    return lv >= 3 ? "Good connection. In sync." : lv == 2 ? "Slow connection, or catching up with the game."
         : lv == 1 ? "Poor connection, or loading, or resyncing." : "Disconnected. The game waits, or the host keeps playing their units.";
}
static int tip_card(Obj card, int u) {
    char nm[64], t[64]; unit_label(u, nm);
    if (st.in_dungeon && st_map[u] == 1) return tip_on(card, "On the Run", "This unit is in the dungeon now. The others stay at the settlement.");
    if (st.owner[u] == st.me) { snprintf(t, sizeof t, "%s", nm); return tip_on(card, t, "Left-click: free this unit. Use Lend to give control to another player."); }
    if (st.owner[u] == TM_FREE) { snprintf(t, sizeof t, "%s", nm); return tip_on(card, t, "Left-click: take this unit."); }
    return 0;
}
static int tip_header(Obj h, int p) {
    char t[80], b[200], nm[40];
    if (st.me == 0 && st.admin) return tip_on(h, "Move Units", "Click a section name, then units: they go to that player, or back to Free units.");
    if (p == st.me) return tip_on(h, "Your Units", "The units you control. Only you can give them orders.");
    if (p == TM_FREE) return tip_on(h, "Free Units", "Nobody controls these units. Left-click one to take it. The host orders them until someone takes them.");
    if (p == TM_RESERVED) return tip_on(h, "Reserved", "This player is not here. These units wait and go back to them when they rejoin. With Admin Mode, the host can release them.");
    pname(p, nm); snprintf(t, sizeof t, "%s's Units", nm);
    snprintf(b, sizeof b, "Units %s controls. You cannot order them unless %s lends you one.", nm, nm);
    return tip_on(h, t, b);
}
static int tip_chip(Obj c, int u) {
    char t[80], b[200], nm[40]; int lt = lent[u], owner = st.owner[u];
    if (owner == st.me && lt < 0) return tip_on(c, "Lend Control", "Lets another player order this unit, in the dungeon too. It stays your unit; take it back any time.");
    if (owner == st.me) { pname(lt, nm); snprintf(t, sizeof t, "Lent to %s", nm); snprintf(b, sizeof b, "%s orders this unit now. Click to take back control.", nm); return tip_on(c, t, b); }
    pname(owner, nm); snprintf(t, sizeof t, "Lent by %s", nm);
    snprintf(b, sizeof b, "%s lent you this unit. You order it fully (move and skills). Click to give it back.", nm);
    return tip_on(c, t, b);
}
static int tip_lrow(Obj r, int i) {
    char t[80], b[200], nm[40];
    if (lrow_act[i] == LA_LEND) { pname(lrow_arg[i], nm); snprintf(t, sizeof t, "Lend Control to %s", nm);
        snprintf(b, sizeof b, "%s orders this unit until you take it back. It stays your unit.", nm); return tip_on(r, t, b); }
    if (lrow_act[i] == LA_TAKE) return tip_on(r, "Take Back Control", "You order this unit again.");
    if (lrow_act[i] == LA_GIVE) return tip_on(r, "Give Back", "Returns control of this unit to its owner.");
    return 0;
}
static int tip_bar(int i) { char nm[40]; pname(bar_pl[i], nm); return tip_on(u_gc_get(bar_h[i]), nm, conn_body(bar_lv[i])); }
static int tip_hud(void) { return tip_on(u_gc_get(h_hud), "Teams", "Who controls which units. Take free units, and lend yours to another player. Right-click and drag to move this button."); }
static int tip_info(void) {
    return tip_on(u_gc_get(h_info), "How Teams Work", "Left-click a free unit to take it, or your unit to free it. You can control up to the host's Units per Player. "
                                                    "To start an expedition, use the game's Go to dungeon; every player approves it.");
}
static int tip_prop(int what) {   /* the proposal panel: 0 region, 1 waiting, 2 approve, 3 decline / cancel, 4 Hide / Show, 5 time left, 6+i player row i */
    if (what == 0) return tip_on(u_gc_get(h_ptip[0]), "Region", "Where the expedition goes. Chosen by the player who proposed it.");
    if (what == 1) return tip_on(u_gc_get(h_ptip[1]), "Waiting", "Players who have not answered yet. With no answer in 60 seconds, the expedition is cancelled.");
    if (what == 2) return tip_on(u_gc_get(h_p_ok), "Approve", "You agree to go. When every player approves, everyone leaves together. The panel then folds to one line.");
    if (what == 3 && prop.proposer == st.me) return tip_on(u_gc_get(h_p_no), "Cancel Proposal", "Withdraws your proposal. Nobody leaves.");
    if (what == 3) return tip_on(u_gc_get(h_p_no), "Decline", "Cancels the expedition for everyone.");
    if (what == 4 && prop_folded) return tip_on(u_gc_get(h_pfold), "Show", "Shows the whole proposal again: who goes, who approved, and the buttons.");
    if (what == 4) return tip_on(u_gc_get(h_pfold), "Hide", "Folds the proposal to one line so it covers less of the map. Escape folds it too. The game keeps running either way.");
    if (what == 5) return tip_on(u_gc_get(h_ptip[2]), "Time Left", "The proposal closes when the time runs out. Every player who has not answered by then cancels the expedition.");
    int i = what - 6;
    if (i < 0 || i >= nprow) return 0;
    char t[80], b[300], nm[40]; int p = prow_pl[i], rd = st.ready >> p & 1u; pname(p, nm);
    snprintf(t, sizeof t, "%s%s", nm, p == st.me ? " (You)" : "");
    snprintf(b, sizeof b, "%s %s", p == prop.proposer ? "Proposed this expedition." : rd ? "Approved: ready to go." : "Has not answered yet.",
             prow_going[i] ? "The cards are the units that go. The other units stay at the settlement and keep working." : "No unit of theirs goes: they watch the expedition.");
    return tip_on(u_gc_get(h_prow[i]), t, b);
}
static int tip_stock(int what) {                             /* the stock window: 0 "Send to friends" button, 1 the receiver's title, 2 its time line */
    Obj v = pres_view(); if (!u_alive(v)) return 0;
    if (what == 2) return tip_on(u_gc_get(h_side_time), "Time Left", "Time to pack and answer the proposal. When it runs out, the expedition is cancelled for everyone. Next shows the proposal with Approve and Decline.");
    if (what == 1) return tip_on(stock_title(v), "Pack Supplies", "Pack supplies for your own units on this run, from the settlement storage. Other players pack for their own units. The time left is in the Going list: when it runs out, the expedition is cancelled.");
    Obj lp = AT(v, 0x68, Obj), nb = lp ? AT(lp, 0x28, Obj) : NULL;
    return tip_on(nb ? img_of(u_tr(nb)) : NULL, "Send to Friends", "Sends this region and these units to every player as a proposal. The expedition starts only when everyone approves.");
}
static double tip_test_until;                                /* a test asked for a tooltip: hover off for a moment */
static void tips_frame(void) {
    if (!game_tip_show_rect) return;
    if (now_s() < tip_test_until) {                           /* a test's tooltip stays up (no mouse in the rig) */
        Obj c = u_gc_get(h_tip_comp);
        if (u_alive(c) && tips_ready()) game_tip_show_rect(tip_last, tip_body_last, u_tr(c));
        return;
    }
    V3 mp3 = F(0x51AD4DC, V3 (*)(void *))(NULL); V2 m = { mp3.x, mp3.y };
    Obj hud = u_gc_get(h_hud);
    if (!hud_drag && u_alive(hud) && u_go_active_self(u_go(u_gc_get(h_hud_root))) && hit(hud, m, cam_for(hud))) { tip_hud(); return; }
    if (prop_shown && proot() && prop_over(m)) {            /* the side panel; elsewhere the screen's other tips */
        Obj cam = cam_for(proot());
        if (hit(u_gc_get(h_pfold), m, cam)) { tip_prop(4); return; }
        if (u_go_active_self(u_go(u_gc_get(h_p_ok))) && hit(u_gc_get(h_p_ok), m, cam)) { tip_prop(2); return; }
        if (hit(u_gc_get(h_p_no), m, cam)) { tip_prop(3); return; }
        if (hit(u_gc_get(h_ptip[0]), m, cam)) { tip_prop(0); return; }
        if (hit(u_gc_get(h_ptip[2]), m, cam)) { tip_prop(5); return; }
        if (hit(u_gc_get(h_ptip[1]), m, cam)) { tip_prop(1); return; }
        for (int i = 0; i < nprow; i++) if (hit(u_gc_get(h_prow[i]), m, cam)) { tip_prop(6 + i); return; }
        return;
    }
    Obj v = pres_view();
    if (stock_mode && u_alive(v) && F(0x50F3724, bool (*)(Obj, void *))(u_go(v), NULL)) {
        Obj cam = NULL; Obj tt = stock_title(v);
        Obj lp = AT(v, 0x68, Obj), nb = lp ? AT(lp, 0x28, Obj) : NULL, nbi = nb ? img_of(u_tr(nb)) : NULL;
        if (stock_mode != 2 && nbi && hit(nbi, m, cam_for(nbi))) { tip_stock(0); return; }
        if (stock_mode == 2 && tt && hit(tt, m, cam_for(tt))) { tip_stock(1); return; }
        Obj stt = u_gc_get(h_side_time);
        if (stock_mode == 2 && u_alive(stt) && hit(stt, m, cam_for(stt))) { tip_stock(2); return; }
        (void)cam;
    }
    if (!opened || !root()) return;
    Obj cam = cam_for(root());
    for (int i = 0; lpop_unit >= 0 && i < nlrow; i++) if (hit(u_gc_get(lrow_h[i]), m, cam)) { tip_lrow(u_gc_get(lrow_h[i]), i); return; }
    for (int i = 0; i < nchip; i++) if (hit(u_gc_get(chip_h[i]), m, cam)) { tip_chip(u_gc_get(chip_h[i]), chip_u[i]); return; }
    for (int i = 0; i < nbar; i++) if (hit(u_gc_get(bar_h[i]), m, cam)) { tip_bar(i); return; }
    if (hit(u_gc_get(h_info), m, cam)) { tip_info(); return; }
    for (int i = 0; i < TM_MAX_UNITS; i++) {
        Obj p = u_gc_get(por[i].card);
        if (!por[i].card || !hit(p, m, cam)) continue;
        int u = unit_by_guid(por[i].guid); if (u >= 0) tip_card(p, u); return;
    }
    for (int k = 0; k < nhdr; k++) if (hit(u_gc_get(h_hdr[k]), m, cam)) { tip_header(u_gc_get(h_hdr[k]), hdr_player[k]); return; }
}
static void start_frame(void);
void ui_teams_frame(void) {
    if (!hooks_done) install_hooks();
#ifdef TEAMS_MOUSE_TEST
    fake_frame();
#endif
    static unsigned n; n++;
    if (scene() != built_scene) {                            /* scene change: the clones died with the old scene */
        built_scene = scene();
        opened = 0; hsel = (tm_sel)TM_SEL_INIT; free_all(); free_prop();
        u_gc_free(h_hud); h_hud = 0; u_gc_free(h_hud_root); h_hud_root = 0; u_gc_free(h_shortcut); h_shortcut = 0;
    }
    prop_session();
    prop_frame(n);
    if (n % 3 == 0 && sess()) stock_gear_frame();        /* any stock expedition window in multiplayer */
    if (me() < 0 || scene() != 2) { if (opened) close_screen(); return; }
    start_frame();
    if (n % 30 == 1) hud_frame();
    hud_place();
    hud_input();
    for (int b = 0; b < 2; b++)
        if (F(0x51ACDD0, bool (*)(int, void *))(b, NULL)) {   /* Input.GetMouseButtonDown */
            V3 mp = F(0x51AD4DC, V3 (*)(void *))(NULL);
            click_at((V2){mp.x, mp.y}, b);
        }
    if (opened && root()) {                                  /* owner lan_v8: Escape closes Teams (the menu / tip first) */
        static bool (*kd)(int); static int looked;
        if (!looked) { looked = 1; kd = (bool (*)(int))F(0x6F8B90, void *(*)(const char *))("UnityEngine.Input::GetKeyDownInt(UnityEngine.KeyCode)"); }
        if ((kd && kd(27)) || test_escape) {
            if (lpop_unit >= 0) { lpop_unit = -1; shown_sig = 0; }
            else { close_screen(); rig_log("teams: Escape closed the Teams window"); }
        }
    }
    test_escape = 0;
    tips_frame();                                            /* our tooltips, in the game's tooltip window */
    if (game_tip_frame && tips_ready()) game_tip_frame();     /* its 0.2 s auto-hide (a second call per frame is harmless) */
    static int was_dungeon;
    int dun = cur_map() == 1;
    if (dun && !was_dungeon && n > 1) {                      /* the expedition started: clear my ready flag */
        model_refresh();
        if (st.me >= 0 && (st.ready >> st.me & 1u)) send_marks(0, (const uint8_t (*)[16])st.marked[st.me], st.nmarked[st.me]);
    }
    was_dungeon = dun;
    if (!opened || n % 6) return;
    if (!root()) { opened = 0; return; }
    model_refresh();
    uint64_t sig = state_sig();
    if (sig != shown_sig) { layout(); shown_sig = sig; }
}

/* ---------------------------------------------------------------- expedition start (dsnet EXPEDITION_START)
   mp_core calls ui_teams_expedition_start (dlsym) on every peer at the same step when everyone is ready.
   The host then sends ONE ChangeMapRequested (ord id 42, the same event the stock "Go to dungeon" button
   makes: UIInput 102 -> MapTransitionRequest(Base -> Dungeon, region, party, floor, skip)) through the trn
   map-change path (trn_submit_map_change: host order -> every peer replays it at the same step; the flow
   barrier waits for the slowest loader, speed back to 1x, host-only autosave). Party = the union of the
   marked units. Region / safe-floor skip: ClanDataContainer.RecentlySelectedRegion +0x50 / IsSkipSafeFloor
   +0x54 (cdc +0x50; sim state, same on every peer). */
static int start_pending, start_n;
static uint8_t start_party[TM_MAX_PLAYERS * TM_MAX_MARK][16];
static uint32_t start_count;
int ui_teams_expedition_start(const uint8_t *p, size_t len) {
    if (!p || len < 1) return -1;
    size_t i = 1; int n = 0;
    for (int k = 0; k < p[0] && i + 2 <= len; k++) {
        int m = p[i + 1]; i += 2;
        for (int j = 0; j < m && i + 16 <= len; j++, i += 16)
            if (n < TM_MAX_PLAYERS * TM_MAX_MARK) memcpy(start_party[n++], p + i, 16);
    }
    start_n = n; start_count++;
    start_region = prop.active ? prop.region : region_default();   /* the approved proposal's region */
    prop.active = 0;
    dsnet_client *c = cli();
    rig_log("teams: expedition start: %d players, %d units in the party%s", p[0], n, c && dsnet_is_host(c) ? " (host sends the map change)" : "");
    if (c && dsnet_is_host(c) && n > 0) start_pending = 1;   /* send from the frame, not inside command apply */
    if (opened) close_screen();
    { char m_[120]; snprintf(m_, sizeof m_, n ? "Everyone approved: leaving for %s" : "Everyone approved, but nobody marked a unit", region_name(start_region)); center_msg(m_); }
    if (pstage == ST_SUPPLIES) close_stock = 1;
    pstage = ST_NONE;
    return 0;
}
static void le32(uint8_t *b, size_t *n, int32_t v) { memcpy(b + *n, &v, 4); *n += 4; }
static size_t build_change_map(uint8_t *b, size_t cap, int32_t region, int skip, const uint8_t (*party)[16], int n) {
    if (cap < 64 + 16 * (size_t)n || n > 64) return 0;
    size_t k = 0;
    b[k++] = 'O'; b[k++] = 2; b[k++] = 1; b[k++] = 0;      /* magic, ORD_VERSION 2, kind ORD_K_EVENT */
    le32(b, &k, 0); le32(b, &k, 0); b[k++] = 0;              /* ctx: map Base, sel_group 0, no selection */
    b[k++] = 42; b[k++] = 0;                                 /* ord event id 42 = ChangeMapRequested */
    le32(b, &k, 0); le32(b, &k, 1); le32(b, &k, region);     /* FromMap Base, ToMap Dungeon, Region */
    b[k++] = (uint8_t)n; b[k++] = 0;                         /* IndexList: u16 count + guids */
    for (int i = 0; i < n; i++) { memcpy(b + k, party[i], 16); k += 16; }
    le32(b, &k, 0);                                          /* Floor (stock: ParamIndex2, 0 = first floor) */
    b[k++] = (uint8_t)(skip ? 1 : 0); b[k++] = 0;            /* IsSkipSafeFloor, HasForcedDungeonSeed */
    le32(b, &k, 0);                                          /* ForcedDungeonSeed */
    b[k++] = 0; b[k++] = 0; b[k++] = 0;                      /* the three commit flags */
    le32(b, &k, 0);                                          /* VisualGuardMode */
    return k;
}
static void start_frame(void) {
    if (!start_pending) return;
    start_pending = 0;
    Obj cl = clan();
    if (!cl) { rig_log("teams: expedition start: no clan data"); return; }
    model_refresh();
    int32_t region = start_region >= 0 ? start_region : region_default(); int skip = AT(cl, 0x54, uint8_t);
    uint8_t buf[64 + 16 * 64];
    size_t len = build_change_map(buf, sizeof buf, region, skip, (const uint8_t (*)[16])start_party, start_n);
    int rc = !len ? -2 : trn_submit_map_change ? trn_submit_map_change(buf, len) : -3;
    rig_log("teams: expedition map change sent (region %d, skip %d, %d units, %zu bytes, rc %d)", region, skip, start_n, len, rc);
    if (rc) notice("The expedition could not be started");
}

/* ---------------------------------------------------------------- expedition gate (transitions / mpui) */
int ui_teams_all_ready(void) { if (me() < 0) return 1; model_refresh(); return tm_all_ready(&st); }
int ui_teams_party(uint8_t (*out)[16], int max) { if (me() < 0) return 0; model_refresh(); return tm_party(&st, out, max); }

/* ---------------------------------------------------------------- dev tools: stock popup hierarchy + UI capture */
static void dump_tree(Buf *d, Obj t, int depth, int maxd) {
    Obj go = u_go(t); char nm[96]; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(go, NULL), nm, sizeof nm);
    static uintptr_t h_img, h_tmp, h_btn;
    Obj ti = u_gc_get(h_img), tt = u_gc_get(h_tmp), tb = u_gc_get(h_btn);
    if (!ti) { ti = type_object("UnityEngine.UI", "Image"); h_img = u_gc_new(ti); }
    if (!tt) { tt = type_object("TMPro", "TMP_Text"); h_tmp = u_gc_new(tt); }
    if (!tb) { tb = type_object("UnityEngine.UI", "Button"); h_btn = u_gc_new(tb); }
    R4 r = rt_rect(t);
    bput(d, "\n%*s%s [%.0fx%.0f]%s", depth * 2, "", nm, r.w, r.h, u_go_active_self(go) ? "" : " (off)");
    Obj im = ti ? go_component(go, ti) : NULL, tx = tt ? go_component(go, tt) : NULL, bt = tb ? go_component(go, tb) : NULL;
    if (u_alive(im)) { Obj sp = (tm_is(im, K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(im, NULL) : NULL); char sn[96] = "-"; if (u_alive(sp)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(sp, NULL), sn, sizeof sn); bput(d, " Image(%s)", sn); }
    if (u_alive(tx)) { char tv[64]; str_utf8(tmp_get(tx), tv, sizeof tv); Obj fa = (tm_is(tx, K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(tx, NULL) : NULL); char fn[64] = "-";
                       if (u_alive(fa)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(fa, NULL), fn, sizeof fn);
                       bput(d, " TMP(%s, %.0f, \"%s\")", fn, (tm_is(tx, K_TMP) ? F(0x4F0B338, float (*)(Obj, void *))(tx, NULL) : 0.0f), tv); }
    if (u_alive(bt)) bput(d, " Button");
    if (depth >= maxd) return;
    int n = (tm_is(t, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(t, NULL) : 0);
    for (int i = 0; i < n && i < 40; i++) dump_tree(d, (tm_is(t, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(t, i, NULL) : NULL), depth + 1, maxd);
}
static int show_popup(int type) {                            /* ShowPopUpRequested{PopUp} on the event queue (UI only) */
    void *k = ord_debug_class ? ord_debug_class("Refactor.Main.Event", "ShowPopUpRequested") : NULL;
    Obj eq = mf ? AT(mf, 0x48, Obj) : NULL;
    if (!k || !eq) return -1;
    Obj ev = F(0x6F96CC, Obj (*)(void *))(k);
    AT(ev, 0x10, int32_t) = type;
    F(0xCCFAD4, void (*)(Obj, Obj, void *))(eq, ev, NULL);
    return 0;
}
/* Camera.main renders no screen-space-overlay UI: for the capture, the top canvas goes to camera mode on a
   layer the camera draws (Default), then everything is put back. */
static int ui_capture(const char *path, int w, int h) {
    Obj cty = type_object("UnityEngine", "Canvas");
    Obj cam = F(0x5083208, Obj (*)(void *))(NULL);
    if (!cty || !cam) return -2;
    if (w <= 0) w = F(0x5092C94, int (*)(void *))(NULL);      /* the screen's size: the canvases lay out for it */
    if (h <= 0) h = F(0x5092CD4, int (*)(void *))(NULL);
    Obj arr = F(0x50FCCC0, Obj (*)(Obj, void *))(cty, NULL);  /* every active Canvas */
    int n = arr ? AT(arr, 0x18, int32_t) : 0;
    if (n > 128) n = 128;
    int layer[128], mode[128], root_[128], slayer[128], order_[128], over_[128]; Obj wcam[128];
    Obj sl = F(0x5079198, Obj (*)(void *))(NULL);             /* SortingLayer.layers: the UI must beat world sprites */
    int nsl = sl ? AT(sl, 0x18, int32_t) : 0, top_id = nsl ? AT(sl, 0x20 + 4 * (nsl - 1), int32_t) : 0;
    float pd = (tm_is(cam, K_CAM) ? F(0x507FC2C, float (*)(Obj, void *))(cam, NULL) : 0.0f) + 0.5f;
    /* the world's sprites sort above a camera-space canvas: render the UI layer only (Camera.cullingMask has no
       managed setter in this build; the engine icall is still registered) */
    static void (*set_mask)(void *, int);
    if (!set_mask) set_mask = F(0x6F8B90, void *(*)(const char *))("UnityEngine.Camera::set_cullingMask_Injected");
    int old_mask = (tm_is(cam, K_CAM) ? F(0x5080BC8, int (*)(Obj, void *))(cam, NULL) : 0);
    if (set_mask) set_mask(AT(cam, 0x10, void *), 1 << 5);
    for (int i = 0; i < n; i++) {
        Obj c = AT(arr, 0x20 + 8 * i, Obj);
        layer[i] = (tm_is(u_go(c), K_GO) ? F(0x50F3118, int (*)(Obj, void *))(u_go(c), NULL) : 0);
        (tm_is(u_go(c), K_GO) ? F(0x50F3204, void (*)(Obj, int, void *))(u_go(c), 5, NULL) : (void)0);        /* UI layer */
        root_[i] = (tm_is(c, K_CV) ? F(0x547521C, bool (*)(Obj, void *))(c, NULL) : false);
        slayer[i] = (tm_is(c, K_CV) ? F(0x5476728, int (*)(Obj, void *))(c, NULL) : 0);
        (tm_is(c, K_CV) ? F(0x5476814, void (*)(Obj, int, void *))(c, top_id, NULL) : (void)0);
        mode[i] = (tm_is(c, K_CV) ? F(0x5475018, int (*)(Obj, void *))(c, NULL) : 0);
        order_[i] = (tm_is(c, K_CV) ? F(0x5476320, int (*)(Obj, void *))(c, NULL) : 0);      /* put back exactly afterwards: */
        over_[i] = (tm_is(c, K_CV) ? F(0x547611C, bool (*)(Obj, void *))(c, NULL) : 0);       /* a partial restore left the HUD */
        wcam[i] = (tm_is(c, K_CV) ? F(0x5477610, Obj (*)(Obj, void *))(c, NULL) : NULL);      /* over the Teams window (2026-10-14) */
        if (root_[i] && mode[i] == 0) {
            (tm_is(c, K_CV) ? F(0x5475104, void (*)(Obj, int, void *))(c, 1, NULL) : (void)0);
            (tm_is(c, K_CV) ? F(0x547771C, void (*)(Obj, Obj, void *))(c, cam, NULL) : (void)0);
            (tm_is(c, K_CV) ? F(0x5475F18, void (*)(Obj, float, void *))(c, pd, NULL) : (void)0);
        }
    }
    F(0x5477FEC, void (*)(void *))(NULL);                                                   /* ForceUpdateCanvases */
    char cmd[1200]; snprintf(cmd, sizeof cmd, "{\"cmd\":\"screenshot\",\"path\":\"%s\",\"w\":%d,\"h\":%d}", path, w, h);
    char *res = rig_exec(cmd);
    rig_log("teams: ui capture %s (%d canvases, %dx%d) -> %s", path, n, w, h, res ? res : "(null)");
    int ok = res && strstr(res, "\"ok\":true");
    free(res);
    for (int i = 0; i < n; i++) {
        Obj c = AT(arr, 0x20 + 8 * i, Obj);
        if (root_[i] && mode[i] == 0) { (tm_is(c, K_CV) ? F(0x547771C, void (*)(Obj, Obj, void *))(c, NULL, NULL) : (void)0); (tm_is(c, K_CV) ? F(0x5475104, void (*)(Obj, int, void *))(c, 0, NULL) : (void)0); }
        (tm_is(u_go(c), K_GO) ? F(0x50F3204, void (*)(Obj, int, void *))(u_go(c), layer[i], NULL) : (void)0);
        (tm_is(c, K_CV) ? F(0x5476814, void (*)(Obj, int, void *))(c, slayer[i], NULL) : (void)0);
        if (tm_is(c, K_CV)) {
            if (!root_[i]) F(0x5476208, void (*)(Obj, bool, void *))(c, over_[i] != 0, NULL);
            F(0x547640C, void (*)(Obj, int, void *))(c, order_[i], NULL);
            if (mode[i] != 0) F(0x547771C, void (*)(Obj, Obj, void *))(c, wcam[i], NULL);
        }
    }
    if (set_mask) set_mask(AT(cam, 0x10, void *), old_mask);
    F(0x5477FEC, void (*)(void *))(NULL);
    rig_log("teams: capture mask %s", set_mask ? "UI only" : "unchanged (icall missing)");
    return ok ? 0 : -3;
}

/* ---------------------------------------------------------------- socket: ui_teams {action, guid, button, player}
   state = what is on screen, read back from the widgets themselves (portrait unit = parts._owner +0x228,
   header text = TMP text); open / close / click / header / ready run the same code as a mouse click. */
/* screen rect (x, y = bottom-left, pixels, y up) of a shown element on an overlay canvas; 0 = not shown */
static int scr_rect(Obj comp, float out[4]) {
    if (!u_alive(comp) || !u_go_active_self(u_go(comp)) || !tm_is(u_tr(comp), K_RT)) return 0;
    Obj t = u_tr(comp); R4 r = rt_rect(t);
    V3 a = F(0x510E0AC, V3 (*)(Obj, V3, void *))(t, (V3){ r.x, r.y, 0 }, NULL), c = F(0x510E0AC, V3 (*)(Obj, V3, void *))(t, (V3){ r.x + r.w, r.y + r.h, 0 }, NULL);
    out[0] = a.x; out[1] = a.y; out[2] = c.x - a.x; out[3] = c.y - a.y;
    return 1;
}
static void put_guid(Buf *b, const uint8_t g[16]) { Guid x; memcpy(&x, g, 16); char s[40]; guid_str(x, s); bput(b, "\"%s\"", s); }
static char *state_json(int rc) {
    Buf b = {0};
    model_refresh();
    bput(&b, "{\"ok\":%s,\"rc\":%d,\"me\":%d,\"open\":%d,\"built\":%s,\"in_dungeon\":%d,\"ready_api\":%d,\"ready\":%u,\"present\":%u,"
         "\"all_ready\":%d,\"starts\":%u,\"start_units\":%d,\"picked\":%d,\"target\":%d,\"running\":%d,\"hud\":%s,\"units\":%d,\"columns\":[",
         rc ? "false" : "true", rc, st.me, opened, root() ? "true" : "false", st.in_dungeon, have_ready_api(), st.ready, st.present,
         tm_all_ready(&st), start_count, start_n, hsel.picked, hsel.target, mp_running(), u_alive(u_gc_get(h_hud)) ? "true" : "false", st.nunits);
    int nc = tm_layout(&st, cols, TM_MAX_PLAYERS + 2);
    for (int k = 0; k < nc; k++) {
        bput(&b, "%s{\"player\":%d,\"model\":[", k ? "," : "", cols[k].player);
        for (int j = 0; j < cols[k].n; j++) { if (j) bput(&b, ","); put_guid(&b, st.guid[cols[k].unit[j]]); }
        bput(&b, "],\"marked\":[");
        for (int j = 0; j < (cols[k].player >= 0 ? st.nmarked[cols[k].player] : 0); j++) { if (j) bput(&b, ","); put_guid(&b, st.marked[cols[k].player][j]); }
        bput(&b, "]");
        for (int q = 0; opened && q < nhdr; q++) if (hdr_player[q] == cols[k].player && u_alive(u_gc_get(h_hdr[q])) && u_go_active_self(u_go(u_gc_get(h_hdr[q])))) {
            char txt[500]; str_utf8(tmp_get(u_gc_get(h_hdr[q])), txt, sizeof txt);
            bput(&b, ",\"header\":"); bstr(&b, txt);
            bput(&b, ",\"header_player\":%d,\"section_index\":%d", hdr_player[q], q);
            break;
        }
        bput(&b, "}");
    }
    bput(&b, "],\"admin\":%d,\"widgets\":[", st.admin);        /* every card: its unit (SubUI_PolicyPortrait._unitGuid +0x54) */
    int w = 0;
    for (int i = 0; i < TM_MAX_UNITS; i++) {
        Obj p = u_gc_get(por[i].card);
        if (!por[i].card || !u_alive(p)) continue;
        V2 pos = rt_get_pos(u_tr(p));
        char nm[80] = ""; str_utf8(tmp_get(tmp_of(tr_find(u_tr(p), "Portrait/Txt_Name"))), nm, sizeof nm);
        Obj cg = comp_of(p, &h_cg_type, "UnityEngine", "CanvasGroup");
        float alpha = u_alive(cg) ? (tm_is(cg, K_CG) ? F(0x546F9A8, float (*)(Obj, void *))(cg, NULL) : 0.0f) : -1;
        bput(&b, "%s{\"guid\":", w++ ? "," : ""); put_guid(&b, por[i].guid);
        bput(&b, ",\"unit_guid\":"); put_guid(&b, (const uint8_t *)p + 0x54);
        bput(&b, ",\"name\":"); bstr(&b, nm);
        Obj mk = u_gc_get(por[i].mark);
        int ui_ = unit_by_guid(por[i].guid); dsnet_client *c_ = cli();
        bput(&b, ",\"x\":%.0f,\"y\":%.0f,\"active\":%d,\"selected\":%d,\"section\":%d,\"clickable\":%d,\"alpha\":%.2f,\"mark_shown\":%d,\"lent_to\":%d,\"controller\":%d,\"chip\":%d}", pos.x, pos.y,
             u_go_active_self(u_go(p)), por[i].marked, por[i].owner, por[i].clickable, alpha, u_alive(mk) && u_go_active_self(u_go(mk)),
             c_ && dsnet_lent_to ? dsnet_lent_to(c_, por[i].guid) : -1, c_ && dsnet_controller_of ? dsnet_controller_of(c_, por[i].guid) : -9,
             ({ int k_ = 0; for (int q = 0; q < nchip; q++) if (chip_u[q] == ui_) k_ = 1; k_; }));
    }
    /* world clicks: what the game's mouse states ask (UnityUtil.IsPointerOverUI, hooked above) and the raw raycast */
    V3 mp = F(0x51AD4DC, V3 (*)(void *))(NULL);
    bput(&b, "],\"mouse\":[%.0f,%.0f],\"over_ui\":%d,\"over_ui_raycast\":%d", mp.x, mp.y,
         F(0xE54BA4, bool (*)(void *))(NULL), F(0xE548C8, bool (*)(void *))(NULL));
    { model_refresh(); char pn_[40] = ""; if (prop.active) pname(prop.proposer, pn_);
      bput(&b, ",\"prop\":{\"active\":%d,\"proposer\":%d,\"region\":%d,\"shown\":%d,\"waiting\":%u,\"members\":%d,\"cap\":%d,\"secs\":%d,\"start_region\":%d,\"ok\":",
           prop.active, prop.active ? prop.proposer : -1, prop.active ? prop.region : -1, prop_shown, tm_prop_waiting(&prop, &st),
           prop.active ? prop_np : tm_party_members(&st), TM_PARTY_CAP, prop.active ? prop_secs_left() : 0, start_region);
      bstr(&b, prop_shown ? prop_ok_s : ""); bput(&b, ",\"no\":"); bstr(&b, prop_shown ? prop_no_s : "");
      bput(&b, ",\"wait_text\":"); bstr(&b, prop_shown ? prop_wait_s : ""); bput(&b, ",\"event\":"); bstr(&b, prop_event);
      bput(&b, ",\"by\":"); bstr(&b, pn_); bput(&b, ",\"mark_limit\":%d", tm_mark_limit(&st));
      /* side panel (lan_v10): folded, screen rects (pixels, y up) of the panel and its buttons, the time line */
      float r4[4];
      bput(&b, ",\"folded\":%d,\"cam\":%d,\"rows\":%d,\"time\":", prop_folded, prop_shown && cam_for(proot()) != NULL, nprow); bstr(&b, prop_shown ? prop_time_s : "");
      Obj pr_[4] = { pwin(), u_gc_get(h_p_ok), u_gc_get(h_p_no), u_gc_get(h_pfold) }; const char *pk_[4] = { "rect", "ok_rect", "no_rect", "fold_rect" };
      for (int q = 0; q < 4; q++) { if (prop_shown && scr_rect(pr_[q], r4)) bput(&b, ",\"%s\":[%.0f,%.0f,%.0f,%.0f]", pk_[q], r4[0], r4[1], r4[2], r4[3]); else bput(&b, ",\"%s\":null", pk_[q]); }
      bput(&b, "}"); }
    { Obj pr = u_gc_get(h_pres), v = pres_view(); char tt[80] = "", sd[700] = "", nx[40] = "";
      int vis = u_alive(v) && F(0x50F3724, bool (*)(Obj, void *))(u_go(v), NULL);
      if (vis) { str_utf8(tmp_get(stock_title(v)), tt, sizeof tt); str_utf8(tmp_get(next_label(v)), nx, sizeof nx); }
      snprintf(sd, sizeof sd, "%s", side_s);
      Obj sb = u_gc_get(h_side);
      if (u_alive(sb) && !F(0x50F3724, bool (*)(Obj, void *))(u_go(sb), NULL)) snprintf(sd, sizeof sd, "(Going block hidden)");
      int listed = 0;                                           /* step 1: unit rows shown (SelectRegionUI +0x50, _transferUnitUIs +0xB0) */
      { Obj sr = vis ? AT(v, 0x50, Obj) : NULL, tl = sr ? AT(sr, 0xB0, Obj) : NULL, ta = tl ? AT(tl, 0x10, Obj) : NULL; int tn = tl ? AT(tl, 0x18, int32_t) : 0;
        for (int i = 0; ta && i < tn && i < 64; i++) { Obj e = AT(ta, 0x20 + 8 * i, Obj); if (u_alive(e) && tm_is(e, K_COMP) && F(0x50F3724, bool (*)(Obj, void *))(u_go(e), NULL)) listed++; } }
      bput(&b, ",\"stock\":{\"listed\":%d,\"open\":%d,\"mode\":%d,\"stage\":%d,\"state\":%d,\"units\":%d,\"title\":", listed, vis, stock_mode, pstage,
           pr ? AT(pr, 0x38, int32_t) : -1, pr && AT(pr, 0x48, Obj) ? AT(AT(pr, 0x48, Obj), 0x18, int32_t) : -1);
      bstr(&b, tt); bput(&b, ",\"time\":"); bstr(&b, vis ? side_time_s : ""); bput(&b, ",\"next\":"); bstr(&b, nx); bput(&b, ",\"side\":"); bstr(&b, sd);
      bput(&b, ",\"panels\":[");                              /* unit panel owners, in panel order (GetPanelOwner 0xEECF18) */
      { Obj cip = vis ? AT(v, 0x58, Obj) : NULL;
        for (int i = 0; cip && i < 4; i++) { Obj o = F(0xEECF18, Obj (*)(Obj, int, void *))(cip, i, NULL);
          if (i) bput(&b, ","); if (o && unit_faction(o) == 1) put_guid(&b, (const uint8_t *)o + UNIT_GUID); else bput(&b, "null"); } }
      bput(&b, "]");
      bput(&b, ",\"greyed\":%d,\"storage\":[", gear_greyed);
      Obj cip = vis ? AT(v, 0x58, Obj) : NULL, ep = cip ? AT(cip, 0x28, Obj) : NULL, list = ep ? AT(ep, 0x50, Obj) : NULL;
      Obj arr = list ? AT(list, 0x10, Obj) : NULL; int ns = list ? AT(list, 0x18, int32_t) : 0, w_ = 0;
      for (int i = 0; arr && i < ns && w_ < 60; i++) {
          Obj sl = AT(arr, 0x20 + 8 * i, Obj); static const uint8_t z[16];
          if (!u_alive(sl) || !memcmp((const uint8_t *)sl + 0x160, z, 16)) continue;
          bput(&b, "%s[", w_++ ? "," : ""); put_guid(&b, (const uint8_t *)sl + 0x160);
          bput(&b, ",%d]", tm_is(sl, K_BEH) ? !F(0x50ECFB8, bool (*)(Obj, void *))(sl, NULL) : -1);
      }
      bput(&b, "]}"); }
    { /* every player unit's inventory slots (item guids), to compare the peers */
      Obj e = mf ? AT(mf, 0x50, Obj) : NULL, pc = e ? AT(e, 0x38, Obj) : NULL, ec = pc ? AT(pc, 0x18, Obj) : NULL;
      bput(&b, ",\"inv\":{");
      for (int u = 0, k = 0; ec && u < st.nunits; u++) {
          Obj inv = st_unit[u] ? F(0x185F284, Obj (*)(Obj, Obj, void *))(ec, st_unit[u], md(0x66919E0)) : NULL;   /* GetComponentOf<InventoryComponent> */
          Obj arr = inv ? AT(inv, 0x18, Obj) : NULL;
          if (!arr) continue;
          bput(&b, "%s", k++ ? "," : ""); put_guid(&b, st.guid[u]); bput(&b, ":[");
          int n = (int)AT(arr, 0x18, uint64_t);
          for (int i = 0; i < n && i < 64; i++) { if (i) bput(&b, ","); put_guid(&b, (const uint8_t *)arr + 0x20 + i * 0x20); }
          bput(&b, "]");
      }
      bput(&b, "}"); }
    int av[8], na = avail_regions(av, 8);
    bput(&b, ",\"region_default\":%d,\"regions_open\":[", region_default());
    for (int i = 0; i < na; i++) bput(&b, "%s%d", i ? "," : "", av[i]);
    bput(&b, "]");
    { Obj hr = u_gc_get(h_hud_root), tty = type_object("TMPro", "TMP_Text"); V2 hp = u_alive(hr) ? rt_get_pos(hr) : (V2){0, 0}; char ht[64] = "";
      Obj ht_ = u_alive(hr) && tty ? (tm_is(hr, K_COMP) ? F(0x50EDDF4, Obj (*)(Obj, Obj, bool, void *))(hr, tty, false, NULL) : NULL) : NULL;
      if (u_alive(ht_)) str_utf8(tmp_get(ht_), ht, sizeof ht);
      { R4 hz = u_alive(hr) ? rt_rect(hr) : (R4){0}; bput(&b, ",\"hud_size\":[%.0f,%.0f],\"hud_compact\":%d,\"hud_dragged\":%d", hz.w, hz.h, hud_compact, hud_dragged);
        int (*an)(float *, float *, float *, float *) = (int (*)(float *, float *, float *, float *))dlsym(RTLD_DEFAULT, "ui_panel_anchor");
        float ax = 0, ay = 0, aw = 0, ah = 0; int av = an ? an(&ax, &ay, &aw, &ah) : 0;
        Obj hp_ = u_alive(hr) ? u_tr_parent(hr) : NULL; R4 cw = hp_ ? rt_rect(hp_) : (R4){0};
        bput(&b, ",\"panel\":[%d,%.0f,%.0f,%.0f,%.0f],\"hud_canvas_w\":%.0f", av, ax, ay, aw, ah, cw.w);
        int (*tr_)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "game_tip_ready"); bput(&b, ",\"tip_ready\":%d", tr_ ? tr_() : -1); 
        V3 hc = u_alive(hr) && tm_is(hr, K_TR) ? F(0x510E0AC, V3 (*)(Obj, V3, void *))(hr, (V3){ hz.x + hz.w / 2, hz.y + hz.h / 2, 0 }, NULL) : (V3){0, 0, 0};
        bput(&b, ",\"hud_screen\":[%.0f,%.0f]", hc.x, hc.y); }   /* the button's centre (overlay canvas: world = screen px) */
      bput(&b, ",\"hud_pos\":[%.0f,%.0f],\"hud_shown\":%d,\"lobby_open\":%d,\"hud_text\":", hp.x, hp.y,
           u_alive(hr) && (tm_is(u_go(hr), K_GO) ? F(0x50F3724, bool (*)(Obj, void *))(u_go(hr), NULL) : false), lobby_open()); bstr(&b, ht); }
    char t1[200] = "", t2[700] = "", t3[400] = "", t5[200] = "";
    str_utf8(tmp_get(u_gc_get(h_title)), t5, sizeof t5);
    bput(&b, ",\"title\":"); bstr(&b, t5);
    { Obj tt = u_gc_get(h_title), fa = u_alive(tt) ? (tm_is(tt, K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(tt, NULL) : NULL) : NULL; char fn[64] = "";
      Obj bt = u_gc_get(h_hint), fb = u_alive(bt) ? (tm_is(bt, K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(bt, NULL) : NULL) : NULL; char fn2[64] = "";
      if (u_alive(fa)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(fa, NULL), fn, sizeof fn);
      if (u_alive(fb)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(fb, NULL), fn2, sizeof fn2);
      bput(&b, ",\"font_title\":"); bstr(&b, fn); bput(&b, ",\"font_body\":"); bstr(&b, fn2);
      Obj ww = win(); R4 wr = ww ? rt_rect(ww) : (R4){0}; Obj rr = root(); R4 rr4 = rr ? rt_rect(u_tr(rr)) : (R4){0};
      bput(&b, ",\"window\":[%.0f,%.0f],\"root\":[%.0f,%.0f]", wr.w, wr.h, rr4.w, rr4.h); }
    str_utf8(tmp_get(u_gc_get(h_ready_txt)), t1, sizeof t1); str_utf8(tmp_get(u_gc_get(h_hint)), t2, sizeof t2);
    snprintf(t3, sizeof t3, "%s", region_line);
    bput(&b, ",\"region_text\":"); bstr(&b, t3);
    { Obj r = root(), cty = type_object("UnityEngine", "Canvas"), gty = type_object("UnityEngine.UI", "GraphicRaycaster");
      Obj cv = r && cty ? go_component(u_go(r), cty) : NULL, gr = r && gty ? go_component(u_go(r), gty) : NULL;
      bput(&b, ",\"modal_sorting\":%d,\"modal_raycaster\":%d", u_alive(cv) ? (tm_is(cv, K_CV) ? F(0x5476320, int (*)(Obj, void *))(cv, NULL) : 0) : -1, u_alive(gr)); }
    { Obj inf = u_gc_get(h_info); bput(&b, ",\"info_icon\":%d", u_alive(inf) && u_go_active_self(u_go(inf))); }
    bput(&b, ",\"lpop\":"); bstr(&b, lpop_unit >= 0 ? lpop_s : "");
    bput(&b, ",\"tip\":"); bstr(&b, tip_last); bput(&b, ",\"tip_body\":"); bstr(&b, tip_body_last);
    bput(&b, ",\"ready_text\":"); bstr(&b, t1); bput(&b, ",\"hint\":"); bstr(&b, t2); bput(&b, "}");
    return b.p;
}
char *ui_teams_exec(const char *cmd, const char *line) {
    if (strcmp(cmd, "ui_teams")) return NULL;
    char act[32] = "state", gs[64] = ""; double bt = 0, pl = -1;
    jstr(line, "action", act, sizeof act); jstr(line, "guid", gs, sizeof gs); jnum(line, "button", &bt); jnum(line, "player", &pl);
    int rc = 0;
    if (!strcmp(act, "open")) rc = open_screen();
    else if (!strcmp(act, "close")) close_screen();
    else if (!strcmp(act, "click") || !strcmp(act, "header") || !strcmp(act, "ready")) {
        if (!opened) rc = -2;
        else {
            model_refresh();
            if (!strcmp(act, "ready")) run(tm_click_ready(&st));
            else if (!strcmp(act, "header")) run(tm_click_header(&st, (int)pl, &hsel));
            else {
                Guid g; int u = guid_parse(gs, &g) ? unit_by_guid((const uint8_t *)&g) : -1;
                int ci = -1; for (int q = 0; q < TM_MAX_UNITS; q++) if (por[q].card && u >= 0 && !memcmp(por[q].guid, st.guid[u], 16)) ci = q;
                if (u < 0) rc = -3;
                else if (ci >= 0 && !por[ci].clickable) rc = -4;     /* same as the mouse: not clickable */
                else run(tm_click_unit(&st, u, (int)bt, &hsel));
            }
        }
#ifdef TEAMS_MOUSE_TEST
    } else if (!strcmp(act, "mouse")) {
        double x = -1, y = -1; jnum(line, "x", &x); jnum(line, "y", &y);
        if (x < 0) x = F(0x5092C94, int (*)(void *))(NULL) / 2.0;   /* Screen.width / height: centre by default */
        if (y < 0) y = F(0x5092CD4, int (*)(void *))(NULL) / 2.0;
        fk_x = (float)x; fk_y = (float)y; fk_btn = (int)bt; fk_start = rig_frames + 1 + FK_HOVER; fk_on = 1;
        rig_log("teams: fake mouse %d at %.0f,%.0f from frame %lld", fk_btn, x, y, (long long)fk_start);
#endif
    } else if (!strcmp(act, "debug")) {                     /* raycast facts for the open screen and its parents */
        Buf d = {0}; Obj r = root();
        Obj cty = type_object("UnityEngine", "Canvas"), gty = type_object("UnityEngine.UI", "GraphicRaycaster"), cgt = type_object("UnityEngine", "CanvasGroup");
        if (r) { R4 rr = rt_rect(u_tr(r)); V2 pt = {F(0x5092C94, int (*)(void *))(NULL) / 2.0f, F(0x5092CD4, int (*)(void *))(NULL) / 2.0f};
                 bput(&d, "root rect %.0fx%.0f screen %.0fx%.0f centre-in-root %d; ", rr.w, rr.h, pt.x * 2, pt.y * 2,
                      (tm_is(u_tr(r), K_RT) ? F(0x5473D40, bool (*)(Obj, V2, Obj, void *))(u_tr(r), pt, NULL, NULL) : false)); }
        if (r) bput(&d, "root depth %d raycastTarget %d activeInHierarchy %d; ", (tm_is(r, K_GR) ? F(0x5261B60, int (*)(Obj, void *))(r, NULL) : 0),
                    (tm_is(r, K_GR) ? F(0x5260340, bool (*)(Obj, void *))(r, NULL) : false), (tm_is(u_go(r), K_GO) ? F(0x50F3724, bool (*)(Obj, void *))(u_go(r), NULL) : false));
        for (Obj t = r ? u_tr(r) : NULL; t; t = u_tr_parent(t)) {
            Obj go = u_go(t); char nm[80]; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(go, NULL), nm, sizeof nm);
            Obj cv = cty ? go_component(go, cty) : NULL, gr = gty ? go_component(go, gty) : NULL, cg = cgt ? go_component(go, cgt) : NULL;
            bput(&d, "[%s", nm);
            if (u_alive(cv)) bput(&d, " canvas(mode %d order %d override %d)", (tm_is(cv, K_CV) ? F(0x5475018, int (*)(Obj, void *))(cv, NULL) : 0),
                                  (tm_is(cv, K_CV) ? F(0x5476320, int (*)(Obj, void *))(cv, NULL) : 0), (tm_is(cv, K_CV) ? F(0x547611C, bool (*)(Obj, void *))(cv, NULL) : false));
            if (u_alive(gr)) bput(&d, " raycaster(on %d)", (tm_is(gr, K_BEH) ? F(0x50ECFB8, bool (*)(Obj, void *))(gr, NULL) : false));
            if (u_alive(cg)) bput(&d, " group(blocks %d ignoreParent %d)", (tm_is(cg, K_CG) ? F(0x546FDB0, bool (*)(Obj, void *))(cg, NULL) : false), (tm_is(cg, K_CG) ? F(0x546FFB4, bool (*)(Obj, void *))(cg, NULL) : false));
            bput(&d, "] ");
        }
        rig_log("teams: debug: %s", d.p ? d.p : "(no root)");
        free(d.p);
    } else if (!strcmp(act, "dump")) {                      /* {ns, cls, depth}: hierarchy of a live stock view */
        char ns[64] = "Refactor.UI", cl[64] = ""; double dp = 4; jstr(line, "ns", ns, sizeof ns); jstr(line, "cls", cl, sizeof cl); jnum(line, "depth", &dp);
        uintptr_t h = 0; Obj v = cl[0] ? find_view(&h, ns, cl) : root();
        Buf d = {0};
        if (v) dump_tree(&d, u_tr(v), 0, (int)dp); else rc = -1;
        rig_log("teams: dump %s.%s:%s", ns, cl, d.p ? d.p : " (not found)");
        free(d.p); u_gc_free(h);
    } else if (!strcmp(act, "popup")) { double ty = 0; jnum(line, "type", &ty); rc = show_popup((int)ty);
    } else if (!strcmp(act, "shot")) {
        char path[1024] = ""; double w = 0, h = 0; jstr(line, "path", path, sizeof path); jnum(line, "w", &w); jnum(line, "h", &h);
        rc = path[0] ? ui_capture(path, (int)w, (int)h) : -1;
    } else if (!strcmp(act, "accept")) {                    /* stock panel accept: units = "guid,guid,..." (the panel party) */
        double rg = 0; jnum(line, "region", &rg);
        char us[2048] = ""; jstr(line, "units", us, sizeof us);
        if (!us[0]) rc = debug_accept((int)rg);
        else {
            uint8_t pn[64][16]; int np = 0; char *save = NULL;
            for (char *t = strtok_r(us, ",", &save); t && np < 64; t = strtok_r(NULL, ",", &save)) { Guid g; if (guid_parse(t, &g)) memcpy(pn[np++], &g, 16); }
            accept_core(NULL, (int)rg, (const uint8_t (*)[16])pn, np);
        }
    } else if (!strcmp(act, "scales")) {                     /* dev: scale chain of a live stock view vs our window ({cls}) */
        char cl[64] = "TaskPriorityView"; jstr(line, "cls", cl, sizeof cl);
        uintptr_t h = 0; Obj v = find_view(&h, "Refactor.UI", cl);
        Buf d = {0};
        Obj chains[2] = { v ? u_tr(v) : NULL, win() };
        for (int c = 0; c < 2; c++) {
            bput(&d, c ? " | ours:" : "stock:");
            for (Obj t = chains[c]; t; t = u_tr_parent(t)) {
                char nm[80]; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(u_go(t), NULL), nm, sizeof nm);
                V3 ls = (tm_is(t, K_TR) ? F(0x510AEFC, V3 (*)(Obj, void *))(t, NULL) : (V3){0, 0, 0}), gs = (tm_is(t, K_TR) ? F(0x510F4F8, V3 (*)(Obj, void *))(t, NULL) : (V3){0, 0, 0});
                bput(&d, " [%s local %.3f lossy %.4f]", nm, ls.x, gs.x);
            }
        }
        rig_log("teams: scales %s", d.p ? d.p : "-"); free(d.p); u_gc_free(h);
    } else if (!strcmp(act, "go")) {                         /* the game's Go to dungeon: the stock window, step 1 (local) */
        rc = prop.active ? -5 : stock_open(3);
    } else if (!strcmp(act, "stock_select")) {               /* step 1: click units in the list ("g1,g2") */
        char us[1024] = ""; jstr(line, "units", us, sizeof us); Obj pr = u_gc_get(h_pres); char *sv = NULL;
        if (!pr || !stock_mode) rc = -2;
        else for (int guard = 0; guard < 16; guard++) {               /* the click list replaces the remembered party */
            Obj list = AT(pr, 0x48, Obj);
            if (!list || AT(list, 0x18, int32_t) <= 0) break;
            uint64_t q[2]; memcpy(q, (char *)AT(list, 0x10, Obj) + 0x20, 16);
            F(0xEE2728, void (*)(Obj, uint64_t, uint64_t, void *))(pr, q[0], q[1], NULL);   /* OnUnitUnselected */
        }
        if (pr && stock_mode) for (char *tk = strtok_r(us, ",", &sv); tk; tk = strtok_r(NULL, ",", &sv)) {
            Guid g; if (!guid_parse(tk, &g)) continue; uint64_t q[2]; memcpy(q, &g, 16);
            F(0xEE250C, void (*)(Obj, uint64_t, uint64_t, void *))(pr, q[0], q[1], NULL);     /* OnUnitSelected */
        }
    } else if (!strcmp(act, "stock_accept")) {               /* the stock window's last button (Enter dungeon), confirm skipped */
        Obj pr = u_gc_get(h_pres), v = pres_view();
        if (!pr || !u_alive(v) || AT(pr, 0x38, int32_t) != 1) rc = -2;
        else F(0xEE3A50, void (*)(Obj, int, Obj, void *))(v, AT(pr, 0x3C, int32_t), AT(pr, 0x48, Obj), NULL);   /* InputOnAccept */
    } else if (!strcmp(act, "stock_close")) {                /* the stock window's close button */
        if (!stock_mode) rc = -2; else stock_close();
    } else if (!strcmp(act, "stock_next")) {                 /* the stock Next button (receiver: to the summary) */
        Obj pr = u_gc_get(h_pres);
        if (!pr || !stock_mode) rc = -2; else F(0xEE3428, void (*)(Obj, void *))(pr, NULL);
    } else if (!strcmp(act, "chip")) {                       /* the lending chip of a unit (same as a click on it) */
        Guid g; int u = guid_parse(gs, &g) ? unit_by_guid((const uint8_t *)&g) : -1, k = -1;
        for (int i = 0; i < nchip; i++) if (chip_u[i] == u) k = i;
        if (!opened || k < 0) rc = -2; else { lpop_unit = lpop_unit == u ? -1 : u; shown_sig = 0; }
    } else if (!strcmp(act, "lpop")) {                       /* a row of the lending menu */
        double row = 0; jnum(line, "row", &row); int u = lpop_unit;
        if (!opened || u < 0 || row < 0 || row >= nlrow) rc = -2;
        else { model_refresh(); lent_refresh(); if (lrow_act[(int)row]) lend_do(u, lrow_act[(int)row], lrow_arg[(int)row]); lpop_unit = -1; shown_sig = 0; }
    } else if (!strcmp(act, "canvases")) {                   /* test: every active Canvas (name, root, mode, order, override, layer) in the log */
        Obj cty = type_object("UnityEngine", "Canvas"), arr = cty ? F(0x50FCCC0, Obj (*)(Obj, void *))(cty, NULL) : NULL;
        int n = arr ? AT(arr, 0x18, int32_t) : 0;
        for (int i = 0; i < n && i < 128; i++) {
            Obj cv = AT(arr, 0x20 + 8 * i, Obj); if (!tm_is(cv, K_CV)) continue;
            char nm[80] = "", rn[80] = ""; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(u_go(cv), NULL), nm, sizeof nm);
            Obj rc_ = F(0x5476FAC, Obj (*)(Obj, void *))(cv, NULL); if (u_alive(rc_)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(u_go(rc_), NULL), rn, sizeof rn);
            rig_log("teams: canvas %s (under %s) active %d root %d mode %d order %d override %d layer %d", nm, rn, F(0x50F3724, bool (*)(Obj, void *))(u_go(cv), NULL), F(0x547521C, bool (*)(Obj, void *))(cv, NULL),
                    F(0x5475018, int (*)(Obj, void *))(cv, NULL), F(0x5476320, int (*)(Obj, void *))(cv, NULL),
                    F(0x547611C, bool (*)(Obj, void *))(cv, NULL), F(0x5476728, int (*)(Obj, void *))(cv, NULL));
        }
        rc = n;
    } else if (!strcmp(act, "hud_move")) {                   /* test: a right-drag of the button by {dx, dy} screen pixels */
        double dx = 0, dy = 0; jnum(line, "dx", &dx); jnum(line, "dy", &dy); hud_move((float)dx, (float)dy); hud_save();
    } else if (!strcmp(act, "tip")) {                        /* test: the tooltip of an element, as on hover: {what, guid, player} */
        char w[24] = ""; jstr(line, "what", w, sizeof w); model_refresh(); lent_refresh();
        Guid g; int u = guid_parse(gs, &g) ? unit_by_guid((const uint8_t *)&g) : -1;
        tip_last[0] = 0; tip_test_until = now_s() + 4;
        if (!strcmp(w, "hud")) rc = !tip_hud();
        else if (!strcmp(w, "info")) rc = !tip_info();
        else if (!strcmp(w, "card")) { rc = -2; for (int i = 0; i < TM_MAX_UNITS; i++) if (por[i].card && u >= 0 && !memcmp(por[i].guid, st.guid[u], 16)) rc = !tip_card(u_gc_get(por[i].card), u); }
        else if (!strcmp(w, "header")) { rc = -2; for (int k = 0; k < nhdr; k++) if (hdr_player[k] == (int)pl) rc = !tip_header(u_gc_get(h_hdr[k]), hdr_player[k]); }
        else if (!strcmp(w, "chip")) { rc = -2; for (int i = 0; i < nchip; i++) if (chip_u[i] == u) rc = !tip_chip(u_gc_get(chip_h[i]), u); }
        else if (!strcmp(w, "lrow")) rc = nlrow ? !tip_lrow(u_gc_get(lrow_h[0]), 0) : -2;
        else if (!strcmp(w, "bar")) rc = nbar ? !tip_bar(0) : -2;
        else if (!strcmp(w, "region")) rc = !tip_prop(0);
        else if (!strcmp(w, "waiting")) rc = !tip_prop(1);
        else if (!strcmp(w, "approve")) rc = !tip_prop(2);
        else if (!strcmp(w, "decline")) rc = !tip_prop(3);
        else if (!strcmp(w, "fold")) rc = !tip_prop(4);
        else if (!strcmp(w, "time")) rc = !tip_prop(5);
        else if (!strcmp(w, "row")) rc = !tip_prop(6 + (int)pl);
        else if (!strcmp(w, "send")) rc = !tip_stock(0);
        else if (!strcmp(w, "pack")) rc = !tip_stock(1);
        else if (!strcmp(w, "pack_time")) rc = !tip_stock(2);
        else rc = -1;
    } else if (!strcmp(act, "escape")) {                     /* test: as if Escape was pressed (no keyboard here) */
        test_escape = 1;
    } else if (!strcmp(act, "fold")) {                       /* test: the side panel's Hide / Show (the same as its click) */
        rc = prop_shown ? 0 : -2; if (prop_shown) { prop_folded = !prop_folded; prop_sig = 0; }
    } else if (!strcmp(act, "gear")) {                       /* host: the lobby rule "gear" (0 shared own, 1 any, 2 own storage) */
        double gm = 0; jnum(line, "mode", &gm); dsnet_client *c = cli(); dsnet_rules r;
        if (!c || !dsnet_is_host(c)) rc = -1; else { dsnet_rules_get(c, &r); r.gear_mode = (int)gm; rc = dsnet_set_rules(c, &r); }
    } else if (!strcmp(act, "item_owner")) {                 /* tag an item (DSNET_ITEM_OWNER) */
        Guid g; double ow = -1; jnum(line, "owner", &ow); dsnet_client *c = cli();
        rc = c && guid_parse(gs, &g) ? dsnet_item_set_owner(c, (const uint8_t *)&g, (int)ow) : -1;
    } else if (!strcmp(act, "stock_quick")) {                /* right-click an item in a unit panel: to the expedition inventory */
        double pi = 0, si = 0; jnum(line, "panel", &pi); jnum(line, "slot", &si);
        Obj v = pres_view();
        if (!u_alive(v) || !stock_mode) rc = -2; else F(0xEE4748, void (*)(Obj, int, int, void *))(v, (int)pi, (int)si, NULL);
    } else if (!strcmp(act, "approve_direct") || !strcmp(act, "approve")) {   /* the summary's Approve (or propose directly) */
        double rg_ = -1; jnum(line, "region", &rg_);
        model_refresh(); (void)rg_; rc = approve();
    } else if (!strcmp(act, "decline") || !strcmp(act, "cancel")) {
        model_refresh(); prop_decline();
    } else if (!strcmp(act, "admin")) {                     /* host: the same DSNET_ADMIN_MODE command the lobby toggle sends */
        double on_ = 1; jnum(line, "on", &on_); uint8_t v = on_ != 0; dsnet_client *c = cli();
        rc = c && dsnet_is_host(c) ? dsnet_submit(c, 14, 0, &v, 1) : -1; shown_sig = 0;
    } else if (!strcmp(act, "relayout")) { shown_sig = 0; if (opened) { model_refresh(); layout(); } }
    else if (strcmp(act, "state")) rc = -1;
    return state_json(rc);
}
