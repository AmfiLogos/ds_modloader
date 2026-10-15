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
static inline Obj u_go(Obj comp) { return F(0x50ED970, Obj (*)(Obj, void *))(comp, NULL); }
static inline Obj u_tr(Obj comp) { return F(0x50ED864, Obj (*)(Obj, void *))(comp, NULL); }
static inline Obj u_go_tr(Obj go) { return F(0x50F300C, Obj (*)(Obj, void *))(go, NULL); }
static inline void u_go_active(Obj go, bool v) { if (go) F(0x50F3520, void (*)(Obj, bool, void *))(go, v, NULL); }
static inline bool u_go_active_self(Obj go) { return go && F(0x50F3638, bool (*)(Obj, void *))(go, NULL); }
static inline void u_active(Obj comp, bool v) { if (u_alive(comp)) u_go_active(u_go(comp), v); }
static inline Obj u_instantiate(Obj orig, Obj parent_tr) { return F(0x50FC30C, Obj (*)(Obj, Obj, void *))(orig, parent_tr, NULL); }
static inline void u_destroy(Obj o) { if (u_alive(o)) F(0x50FC8F8, void (*)(Obj, void *))(o, NULL); }
static inline Obj u_tr_parent(Obj t) { return F(0x510992C, Obj (*)(Obj, void *))(t, NULL); }
static inline void u_last_sibling(Obj t) { F(0x510ED6C, void (*)(Obj, void *))(t, NULL); }
static inline void u_enabled(Obj beh, bool v) { F(0x50ECEA0, void (*)(Obj, bool, void *))(beh, v, NULL); }
static inline void rt_anchor(Obj rt, float x0, float y0, float x1, float y1) {
    F(0x5106B64, void (*)(Obj, V2, void *))(rt, (V2){x0, y0}, NULL);
    F(0x5106D48, void (*)(Obj, V2, void *))(rt, (V2){x1, y1}, NULL);
}
static inline void rt_pivot(Obj rt, float x, float y) { F(0x51072F4, void (*)(Obj, V2, void *))(rt, (V2){x, y}, NULL); }
static inline void rt_pos(Obj rt, float x, float y) { F(0x5106F2C, void (*)(Obj, V2, void *))(rt, (V2){x, y}, NULL); }
static inline void rt_size(Obj rt, float w, float h) { F(0x5107110, void (*)(Obj, V2, void *))(rt, (V2){w, h}, NULL); }
static inline R4 rt_rect(Obj rt) { return F(0x5106978, R4 (*)(Obj, void *))(rt, NULL); }
static inline V2 rt_get_pos(Obj rt) { return F(0x5106E38, V2 (*)(Obj, void *))(rt, NULL); }
static inline void tr_scale(Obj t, float s) { F(0x510AFF8, void (*)(Obj, V3, void *))(t, (V3){s, s, 1.0f}, NULL); }
static inline void g_color(Obj g, float r, float gg, float b, float a) { F(0x52602F8, void (*)(Obj, C4, void *))(g, (C4){r, gg, b, a}, NULL); }
static inline void g_raycast(Obj g, bool v) { F(0x5260348, void (*)(Obj, bool, void *))(g, v, NULL); }
static inline void img_sprite(Obj img, Obj spr) { F(0x5257600, void (*)(Obj, Obj, void *))(img, spr, NULL); }
static inline Obj go_component(Obj go, Obj type) { return F(0x50EDAA8, Obj (*)(Obj, Obj, void *))(go, type, NULL); }
static Obj tmp_get(Obj t) {                                  /* TMP_Text.text: vtable slots 65 / 66 */
    if (!u_alive(t)) return NULL;
    void *k = *(void **)t;
    return (*(Obj (**)(Obj, void *))((char *)k + 0x548))(t, *(void **)((char *)k + 0x550));
}
static void tmp_set(Obj t, const char *s) {
    if (!u_alive(t)) return;
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
static int local_marks_n; static uint8_t local_marks[TM_MAX_MARK][16];   /* fallback while dsnet has no marks */
static int have_ready_api(void) { return dsnet_ready_mask && dsnet_marked_of && dsnet_expedition_ready; }

static int admin_mode(void);
static void model_refresh(void) {
    dsnet_client *c = cli();
    memset(&st, 0, sizeof st);
    st.me = me();
    st.in_dungeon = cur_map() == 1;
    st.admin = admin_mode();
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
            st_unit[st.nunits++] = u;
        }
    }
    if (c && have_ready_api()) {
        st.ready = dsnet_ready_mask(c);
        for (int p = 0; p < TM_MAX_PLAYERS; p++) {
            int n = dsnet_marked_of(c, p, st.marked[p]);
            st.nmarked[p] = n > 0 ? (n > TM_MAX_MARK ? TM_MAX_MARK : n) : 0;
        }
    } else if (st.me >= 0) {
        st.nmarked[st.me] = local_marks_n;
        memcpy(st.marked[st.me], local_marks, sizeof local_marks);
    }
}

/* ---------------------------------------------------------------- region vote (owner 2026-10-13)
   Each player's pick travels as mp order TM_ORDER_REGION (zero guid: open to everyone) and is kept here on
   every peer in command order. Only the host uses the result (it builds the map change), so a late joiner who
   missed earlier picks only shows them late; the expedition region is always the host's tally. */
static int region_pick[TM_MAX_PLAYERS];
static mp_session *region_sess;
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
static int region_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)guid;
    if (n < 4 || player < 0 || player >= TM_MAX_PLAYERS) return -1;
    int32_t r; memcpy(&r, a, 4);
    if (r < -1 || r >= 7) return -1;
    region_pick[player] = r;
    rig_log("teams: player %d picks region %d (%s)", player, r, region_name(r));
    return 0;
}
static void region_session(void) {
    mp_session *s = sess();
    if (s == region_sess) return;
    region_sess = s;
    for (int i = 0; i < TM_MAX_PLAYERS; i++) region_pick[i] = -1;
    if (s) mp_set_order_handler(s, TM_ORDER_REGION, region_order);
}
static void send_region(int r) {
    mp_session *s = sess(); static const uint8_t zero[16]; int32_t v = r;
    if (!s || mp_order(s, TM_ORDER_REGION, zero, &v, 4)) { rig_log("teams: region pick not sent"); }
}
/* the clan's last chosen region if it is still open, else the first open region (fresh world) */
static int region_default(void) {
    int av[8], n = avail_regions(av, 8); Obj cl = clan();
    int r = cl ? AT(cl, 0x50, int32_t) : -1;
    for (int i = 0; i < n; i++) if (av[i] == r) return r;
    return n ? av[0] : (r >= 0 && r < 7 ? r : 0);
}
static int region_result(void) {
    uint32_t present = st.present;
    int r = tm_region_vote(region_pick, present);
    if (r < 0) r = region_default();                       /* nobody picked */
    return r;
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
static uintptr_t h_shortcut, h_root, h_win, h_hud, h_title, h_ready, h_close, h_hint, h_region, h_rules;
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
static Obj tr_find(Obj t, const char *path) { return t ? F(0x510F3C0, Obj (*)(Obj, Obj, void *))(t, u_str(path), NULL) : NULL; }
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
    Obj tt = tmp_of(tr_find(panel, "Txt_Title")), ft = u_alive(tt) ? F(0x4F0A59C, Obj (*)(Obj, void *))(tt, NULL) : NULL;
    u_gc_free(h_font_title); h_font_title = u_gc_new(ft);
    Obj fb = F(0x4F0A59C, Obj (*)(Obj, void *))(tmp_of(name), NULL);
    int has_check = u_alive(fb) && F(0x4EEE580, bool (*)(Obj, uint16_t, bool, bool, void *))(fb, 0x2713, true, false, NULL);   /* HasCharacter */
    mark_glyph = has_check ? "\u2713" : "RUN";
    /* HUD entry: the Recruit popup's (PopUpType 8) framed top-line button + its population icon (recon 2026-10-13:
       Img_BackGroundBox/Img_ExitButton [32x32] topLineButton_Frame1 Button, .../Img_PopulationFrame/Img_Population) */
    Obj rp = so ? F(0xD3236C, Obj (*)(Obj, int, void *))(so, 8, NULL) : NULL;
    Obj btn = u_alive(rp) ? tr_find(u_tr(rp), "Img_BackGroundBox/Img_ExitButton") : NULL;
    Obj pim = u_alive(rp) ? img_of(tr_find(u_tr(rp), "Img_BackGroundBox/Img_PopulationFrame/Img_Population")) : NULL;
    Obj pop = u_alive(pim) ? F(0x5266F04, Obj (*)(Obj, void *))(pim, NULL) : NULL;              /* Image.sprite */
    u_gc_free(h_tpl_btn); h_tpl_btn = u_gc_new(u_alive(btn) && u_alive(pop) ? btn : NULL);
    u_gc_free(h_spr_pop); h_spr_pop = u_gc_new(u_alive(pop) ? pop : NULL);
    rig_log("teams: title font %p, check mark glyph %s, top-line button %p, population icon %p", ft, has_check ? "yes" : "no (RUN tag)", btn, pop);
    return 0;
}
static Obj tmp_of(Obj tr) { static uintptr_t h; return comp_of(tr, &h, "TMPro", "TMP_Text"); }
static Obj img_of(Obj tr) { static uintptr_t h; return comp_of(tr, &h, "UnityEngine.UI", "Image"); }
static void button_off(Obj tr) { static uintptr_t h; Obj b = comp_of(tr, &h, "UnityEngine.UI", "Button"); if (u_alive(b)) u_enabled(b, false); }
static void centre_anchor(Obj rt) { rt_anchor(rt, 0.5f, 0.5f, 0.5f, 0.5f); rt_pivot(rt, 0.5f, 0.5f); }
/* a body text (Lora) under parent: size w x h, font size, alignment */
static Obj new_text(Obj parent_tr, float w, float h, float size, int align) {
    Obj tpl = u_gc_get(h_tpl_text);
    if (!u_alive(tpl) && load_templates()) return NULL;
    tpl = u_gc_get(h_tpl_text);
    Obj c = u_instantiate(tpl, parent_tr);
    Obj t = tmp_of(c);
    if (!u_alive(t)) return NULL;
    Obj rt = u_tr(t);
    centre_anchor(rt); rt_size(rt, w, h); tr_scale(rt, 1.0f);
    F(0x4F0B340, void (*)(Obj, float, void *))(t, size, NULL);          /* fontSize */
    F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, false, NULL);          /* no auto sizing */
    F(0x4F0B7BC, void (*)(Obj, int, void *))(t, align, NULL);           /* alignment */
    F(0x4F0BAB4, void (*)(Obj, bool, void *))(t, true, NULL);           /* word wrap */
    g_raycast(t, false);
    tmp_set(t, "");
    return t;
}
static void tmp_font(Obj t, Obj f) { if (u_alive(t) && u_alive(f)) F(0x4F0A5A4, void (*)(Obj, Obj, void *))(t, f, NULL); }   /* TMP_Text.font */
/* a plain rectangle (the level box clone: sprite and children removed) under parent, stretched between anchors */
static Obj new_rect(Obj parent_tr, float x0, float y0, float x1, float y1, float w, float h, C4 c) {
    Obj r = u_instantiate(u_gc_get(h_tpl_bg), parent_tr);
    if (!u_alive(r)) return NULL;
    int n = F(0x510EAE4, int (*)(Obj, void *))(r, NULL);
    for (int i = 0; i < n; i++) u_go_active(u_go(F(0x510FC60, Obj (*)(Obj, int, void *))(r, i, NULL)), false);
    button_off(r);
    rt_anchor(r, x0, y0, x1, y1); rt_pivot(r, x0 == x1 ? x0 : 0.5f, y0 == y1 ? y0 : 0.5f); rt_size(r, w, h); rt_pos(r, 0, 0); tr_scale(r, 1.0f);
    Obj im = img_of(r);
    if (u_alive(im)) { img_sprite(im, NULL); g_color(im, c.r, c.g, c.b, c.a); g_raycast(im, false); }
    return im;
}
static Obj root(void) { Obj r = u_gc_get(h_root); return u_alive(r) ? r : NULL; }
static Obj win(void) { Obj w = u_gc_get(h_win); return u_alive(w) ? w : NULL; }
static void free_all(void) {
    Obj r = u_gc_get(h_root);
    if (u_alive(r)) u_destroy(u_go(r));                      /* children go with it */
    uintptr_t *hs[] = { &h_root, &h_win, &h_title, &h_ready, &h_close, &h_hint, &h_region, &h_rules };
    for (size_t i = 0; i < sizeof hs / sizeof hs[0]; i++) { u_gc_free(*hs[i]); *hs[i] = 0; }
    for (int i = 0; i < nhdr; i++) { u_gc_free(h_hdr[i]); h_hdr[i] = 0; }
    nhdr = 0;
    for (int i = 0; i < TM_MAX_UNITS; i++) { u_gc_free(por[i].card); u_gc_free(por[i].mark); por[i].card = por[i].mark = 0; por[i].used = 0; }
}
static Obj top_canvas_tr(void) {
    Obj v = shortcut_view(), cty = type_object("UnityEngine", "Canvas");
    if (!v || !cty) return NULL;
    for (Obj t = u_tr(v); t; t = u_tr_parent(t)) {
        Obj cv = go_component(u_go(t), cty);
        if (u_alive(cv)) { Obj top = F(0x5476FAC, Obj (*)(Obj, void *))(cv, NULL); return u_alive(top) ? u_tr(top) : NULL; }
    }
    return NULL;
}
static int build_root(void) {
    if (load_templates()) return -1;
    free_all();                                              /* the HUD can be rebuilt (map change): drop dead handles */
    Obj canvas_tr = top_canvas_tr();
    if (!canvas_tr) { rig_log("teams: top canvas not found"); return -1; }
    Obj rt = u_instantiate(u_gc_get(h_tpl_bg), canvas_tr);     /* dim full-screen layer, eats every click */
    if (!u_alive(rt)) { rig_log("teams: backdrop clone failed"); return -1; }
    button_off(rt);
    rt_anchor(rt, 0, 0, 1, 1); rt_pivot(rt, 0.5f, 0.5f); rt_size(rt, 0, 0); rt_pos(rt, 0, 0); tr_scale(rt, 1.0f);
    Obj bgi = img_of(rt);
    if (u_alive(bgi)) { img_sprite(bgi, NULL); g_color(bgi, 0, 0, 0, 0.55f); g_raycast(bgi, true); }
    u_last_sibling(rt);
    Obj cty = type_object("UnityEngine", "Canvas"), gty = type_object("UnityEngine.UI", "GraphicRaycaster");
    Obj cv = cty ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(rt), cty, NULL) : NULL;   /* GameObject.AddComponent */
    if (u_alive(cv)) { F(0x5476208, void (*)(Obj, bool, void *))(cv, true, NULL); F(0x547640C, void (*)(Obj, int, void *))(cv, 32000, NULL); }
    Obj gr = gty ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(rt), gty, NULL) : NULL;
    h_root = u_gc_new(bgi ? bgi : rt);
    /* the window: the panel with its frame; keep only the title and the close button */
    Obj w = u_instantiate(u_gc_get(h_tpl_panel), rt);
    if (!u_alive(w)) { rig_log("teams: window clone failed"); return -1; }
    centre_anchor(w); rt_pos(w, 0, 0); tr_scale(w, 1.0f);
    int n = F(0x510EAE4, int (*)(Obj, void *))(w, NULL);
    for (int i = 0; i < n; i++) {
        Obj c = F(0x510FC60, Obj (*)(Obj, int, void *))(w, i, NULL);
        char nm[64]; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(u_go(c), NULL), nm, sizeof nm);
        if (strcmp(nm, "Txt_Title") && strcmp(nm, "CloseButton")) u_go_active(u_go(c), false);   /* stock content off */
    }
    Obj wi = img_of(w); if (u_alive(wi)) g_raycast(wi, true);
    h_win = u_gc_new(w);
    Obj title = tmp_of(tr_find(w, "Txt_Title")), close = tr_find(w, "CloseButton");
    if (close) button_off(close);
    h_title = u_gc_new(title); h_close = u_gc_new(close);
    h_ready = u_gc_new(new_text(w, 300, 44, 22, AL_CENTER));
    tmp_font(u_gc_get(h_ready), u_gc_get(h_font_title));
    h_region = u_gc_new(new_text(w, 560, 56, 17, AL_LEFT));
    h_rules = u_gc_new(new_text(w, 1240, 48, 16, AL_CENTER));
    h_hint = u_gc_new(new_text(w, 1240, 30, 16, AL_CENTER));
    rig_log("teams: window built from the Task Planner prefab (card %.0fx%.0f); modal canvas %s sorting %d, raycaster %s",
            card_w, card_h, u_alive(cv) ? "added" : "MISSING", u_alive(cv) ? F(0x5476320, int (*)(Obj, void *))(cv, NULL) : -1, u_alive(gr) ? "added" : "MISSING");
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
    F(0xE95EE8, void (*)(Obj, Obj, void *))(pp, st_unit[unit], NULL);   /* stock Refresh: portrait, name, level, _unitGuid */
    Obj cg = F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(pp), u_gc_get(h_cg_type) ? u_gc_get(h_cg_type)
               : (h_cg_type = u_gc_new(type_object("UnityEngine", "CanvasGroup")), u_gc_get(h_cg_type)), NULL);
    (void)cg;
    /* "marked for the run": a gold frame + a check mark (or RUN) tag over the card, shown by layout */
    static const C4 gold = {1.0f, 0.80f, 0.25f, 1};
    Obj mk = new_rect(u_tr(pp), 0, 0, 1, 1, 0, 0, (C4){0, 0, 0, 0});
    if (mk) {
        Obj mt = u_tr(mk);
        new_rect(mt, 0, 1, 1, 1, 0, 4, gold); new_rect(mt, 0, 0, 1, 0, 0, 4, gold);
        new_rect(mt, 0, 0, 0, 1, 4, 0, gold); new_rect(mt, 1, 0, 1, 1, 4, 0, gold);
        int chk = mark_glyph[0] != 'R';
        Obj tag = new_rect(mt, 1, 1, 1, 1, chk ? 24 : 44, 22, gold);
        Obj tx = tag ? new_text(u_tr(tag), chk ? 24 : 44, 22, chk ? 18 : 16, AL_CENTER) : NULL;
        if (tx) { rt_anchor(u_tr(tx), 0, 0, 1, 1); rt_size(u_tr(tx), 0, 0); rt_pos(u_tr(tx), 0, 0);
                  if (!chk) tmp_font(tx, u_gc_get(h_font_title));    /* the check mark: the body font that has it */
                  char t[64]; snprintf(t, sizeof t, "<color=#2A1A08>%s</color>", mark_glyph); tmp_set(tx, t); }
        u_go_active(u_go(mk), false);
    }
    por[free_i].card = u_gc_new(pp); por[free_i].mark = u_gc_new(mk); memcpy(por[free_i].guid, st.guid[unit], 16); por[free_i].used = 1;
    return pp;
}
static void card_style(Obj pp, int owner, int clickable, int marked, int picked_) {
    Obj cg = comp_of(pp, &h_cg_type, "UnityEngine", "CanvasGroup");
    if (u_alive(cg)) F(0x546FA94, void (*)(Obj, float, void *))(cg, clickable ? 1.0f : 0.38f, NULL);   /* others: greyed */
    Obj im = img_of(u_tr(pp));
    if (u_alive(im)) {
        if (picked_) g_color(im, 0.45f, 0.85f, 1.0f, 1);
        else if (marked) g_color(im, 1.0f, 0.82f, 0.30f, 1);            /* marked for the run: gold card */
        else g_color(im, 1, 1, 1, 1);
    }
    (void)owner;
}

/* ---------------------------------------------------------------- layout */
static tm_column cols[TM_MAX_PLAYERS + 2];
static int ncols;
static uint64_t shown_sig;
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
    h = fnv(h, region_pick, sizeof region_pick);
    return fnv(h, &opened, sizeof opened);
}
static void section_header(int k, int player, float y, const char *txt) {
    Obj w = win();
    if (k >= nhdr) { h_hdr[k] = u_gc_new(new_text(w, 1240, 30, 19, AL_LEFT)); nhdr = k + 1; }
    Obj hd = u_gc_get(h_hdr[k]);
    hdr_player[k] = player;
    if (!u_alive(hd)) return;
    rt_pos(u_tr(hd), 0, y); tmp_set(hd, txt); u_active(hd, true);
}
static void layout(void) {
    Obj w = win();
    if (!w) return;
    ncols = tm_layout(&st, cols, TM_MAX_PLAYERS + 2);
    /* sections: you first, then the free units, then every other player (greyed, not clickable) */
    int order[TM_MAX_PLAYERS + 2], no = 0;
    for (int k = 0; k < ncols; k++) if (cols[k].player == st.me) order[no++] = k;
    for (int k = 0; k < ncols; k++) if (cols[k].player == TM_FREE) order[no++] = k;
    for (int k = 0; k < ncols; k++) if (cols[k].player >= 0 && cols[k].player != st.me) order[no++] = k;
    for (int k = 0; k < ncols; k++) if (cols[k].player == TM_RESERVED) order[no++] = k;
    int host_admin = st.me == 0 && st.admin;
    /* fit: content from y = 318 down to y = -268 (region line, status and rules below) */
    float avail = 586, gap = 8, s = 1.0f;
    for (int it = 0; it < 10; it++) {
        int per = (int)(1240 / (card_w * s + gap)); if (per < 1) per = 1;
        float need = 0;
        for (int i = 0; i < no; i++) { int n = cols[order[i]].n; need += 34 + (n ? ((n + per - 1) / per) * (card_h * s + gap) : 26); }
        if (need <= avail || s < 0.45f) break;
        s *= 0.88f;
    }
    int per = (int)(1240 / (card_w * s + gap)); if (per < 1) per = 1;
    float y = 318;                                         /* below the title band of the frame */
    for (int i = 0; i < TM_MAX_UNITS; i++) por[i].used = 0;
    for (int i = 0; i < no; i++) {
        tm_column *c = &cols[order[i]];
        int pl = c->player;
        char txt[400], nm[40]; size_t l;
        if (pl == TM_FREE) l = (size_t)snprintf(txt, sizeof txt, "<size=115%%>Free units</size>  <size=80%%>(nobody owns them: left-click to take one)</size>");
        else if (pl == TM_RESERVED) { char rn[200]; reserved_names(c, rn, sizeof rn);
            l = (size_t)snprintf(txt, sizeof txt, "<size=115%%><color=#999999>Reserved for %s</color></size>  <size=80%%>(away: the units come back when they rejoin)</size>", rn); }
        else {
            player_name(pl, nm);
            char lb[40]; ui_players pls; memset(&pls, 0, sizeof pls); snprintf(pls.name[pl], 32, "%s", nm);
            int rdy = st.ready >> pl & 1u, rp = region_pick[pl];
            l = (size_t)snprintf(txt, sizeof txt, "<size=115%%><color=%s>%s</color>%s</size>  <size=80%%>%s - %d/%d marked for the run - region vote: %s</size>",
                                 ui_player_color(pl), pl == st.me ? "Your units" : ui_player_label(&pls, pl, lb), pl == st.me ? "" : "'s units",
                                 !(st.present >> pl & 1u) ? "left the game" : rdy ? "<color=#7CFC00>READY</color>" : "not ready",
                                 st.nmarked[pl], TM_MAX_MARK, rp >= 0 ? region_name(rp) : "not voted");
        }
        if (host_admin && hsel.picked >= 0 && hsel.picked < st.nunits && pl != st.owner[hsel.picked] && pl != TM_RESERVED)
            l += (size_t)snprintf(txt + l, sizeof txt - l, "  <color=#FFD700>(click here to move the picked unit)</color>");
        else if (st.me == 0 && hsel.target == pl && pl != TM_NO_TARGET)   /* TM_NO_TARGET == TM_RESERVED */
            l += (size_t)snprintf(txt + l, sizeof txt - l, "  <color=#FFD700>(units you click go here)</color>");
        section_header(i, pl, y - 14, txt);
        y -= 34;
        if (!c->n) {
            Obj hd = u_gc_get(h_hdr[i]);
            if (hd) { char t2[460]; snprintf(t2, sizeof t2, "%s\n<size=80%%><color=#AAAAAA>no units</color></size>", txt); tmp_set(hd, t2); }
            y -= 26; continue;
        }
        for (int j = 0; j < c->n; j++) {
            int u = c->unit[j];
            Obj pp = card_for(u);
            if (!pp) continue;
            float px = -620 + (j % per) * (card_w * s + gap) + card_w * s / 2, py = y - (j / per) * (card_h * s + gap) - card_h * s / 2;
            Obj prt = u_tr(pp);
            rt_pos(prt, px, py); tr_scale(prt, s);
            int clickable = !st.in_dungeon && (pl == st.me || pl == TM_FREE || host_admin);
            int marked = pl >= 0 && tm_is_marked(&st, pl, st.guid[u]);
            card_style(pp, pl, clickable, marked, hsel.picked == u);
            for (int q = 0; q < TM_MAX_UNITS; q++) if (por[q].card && !memcmp(por[q].guid, st.guid[u], 16)) { por[q].owner = pl; por[q].clickable = clickable; por[q].marked = marked;
                                                                                              Obj mk = u_gc_get(por[q].mark); if (u_alive(mk)) u_go_active(u_go(mk), marked); }
            u_go_active(u_go(pp), true);
        }
        y -= ((c->n + per - 1) / per) * (card_h * s + gap) + 6;
    }
    for (int k = no; k < nhdr; k++) u_active(u_gc_get(h_hdr[k]), false);
    for (int i = 0; i < TM_MAX_UNITS; i++)                   /* units gone (dead, retired): drop their card */
        if (por[i].card && !por[i].used) { u_destroy(u_go(u_gc_get(por[i].card))); u_gc_free(por[i].card); u_gc_free(por[i].mark); por[i].card = por[i].mark = 0; }
    /* fixed texts */
    char t[600];
    Obj o;
    snprintf(t, sizeof t, "Teams%s", !mp_running() ? "  <size=60%><color=#FF6060>(multiplayer not started)</color></size>"
             : st.in_dungeon ? "  <size=60%>(locked in a dungeon)</size>" : "");
    if ((o = u_gc_get(h_title))) tmp_set(o, t);
    if ((o = u_gc_get(h_ready))) {
        int rdy = st.me >= 0 && (st.ready >> st.me & 1u);
        snprintf(t, sizeof t, !have_ready_api() ? "<color=#888888>[ Ready ]</color>" : rdy ? "<color=#7CFC00>[ READY ]</color>\n<size=60%%>click to cancel</size>" : "[ Ready ]");
        rt_size(u_tr(o), 300, 52); rt_pos(u_tr(o), 470, -300); tmp_set(o, t);
    }
    if ((o = u_gc_get(h_region))) {
        int mine = st.me >= 0 ? region_pick[st.me] : -1, res = region_result();
        snprintf(t, sizeof t, "<color=#F2C14E>Region vote:</color> < %s >  (leading: %s)\n<size=80%%>Click: next region, right-click: back. Most votes wins, the host breaks a tie.</size>",
                 mine >= 0 ? region_name(mine) : "not voted", region_name(res));
        rt_pos(u_tr(o), -340, -300); tmp_set(o, t);
    }
    if ((o = u_gc_get(h_rules))) {
        snprintf(t, sizeof t, "<color=#F2C14E>Left-click</color> a unit: take it / free it.   <color=#F2C14E>Right-click</color> your unit: mark it for the run (max %d).\n"
                 "The expedition starts when every player is Ready.%s", TM_MAX_MARK,
                 host_admin ? "   <color=#F2C14E>Host (admin)</color>: click a section name, then units to move them there."
                 : st.me == 0 ? "   <color=#F2C14E>Host</color>: click a player's name, then free units to give them." : "");
        rt_pos(u_tr(o), 0, -386); tmp_set(o, t);
    }
    if ((o = u_gc_get(h_hint))) {
        uint32_t wm = tm_waiting_mask(&st);
        char stl[300] = "";
        if (!mp_running()) snprintf(stl, sizeof stl, "<color=#FF6060>Multiplayer is not running yet: changes here are not applied.</color>");
        else if (st.in_dungeon) snprintf(stl, sizeof stl, "Teams cannot change in a dungeon.");
        else if (tm_all_ready(&st)) snprintf(stl, sizeof stl, "Everyone is ready: the expedition starts.");
        else if (st.me >= 0 && !(st.ready >> st.me & 1u)) {
            int mine = 0; for (int i = 0; i < st.nunits; i++) mine += st.owner[i] == st.me;
            snprintf(stl, sizeof stl, !mine ? "Next: take a free unit (left click)." : !st.nmarked[st.me]
                     ? "Next: right-click 1 to 3 of your units to mark them for the run." : "Next: press Ready.");
        } else if (wm) {
            size_t l = (size_t)snprintf(stl, sizeof stl, "Waiting for:");
            for (int p = 0; p < TM_MAX_PLAYERS && l < sizeof stl - 40; p++)
                if (wm >> p & 1u) { char nm[40], lb[40]; ui_players pl; memset(&pl, 0, sizeof pl); player_name(p, nm);
                    snprintf(pl.name[p], sizeof pl.name[p], "%s", nm);
                    l += (size_t)snprintf(stl + l, sizeof stl - l, " %s", ui_player_label(&pl, p, lb)); }
        }
        rt_pos(u_tr(o), 0, -348); tmp_set(o, stl);          /* inside the panel, above the rules */
    }
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
    case TM_READY: send_marks(1, (const uint8_t (*)[16])a.marks, a.nmarks); break;
    case TM_UNREADY: send_marks(0, (const uint8_t (*)[16])a.marks, a.nmarks); break;
    case TM_DENY_LOCKED: notice("Teams cannot change in a dungeon"); break;
    case TM_DENY_OWNED:
        if (a.target == TM_RESERVED) notice("%s is kept for a player who is away", un);
        else notice("%s belongs to %s", un, ui_player_label(&pl, a.target, lb));
        break;
    case TM_DENY_FULL: notice("You already marked %d units for the run", TM_MAX_MARK); break;
    case TM_DENY_NO_MARKS: notice("Right-click 1 to %d of your units to mark them for the run first", TM_MAX_MARK); break;
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
    Obj cv = u_alive(graphic) ? F(0x5260544, Obj (*)(Obj, void *))(graphic, NULL) : NULL;
    if (!u_alive(cv) || F(0x5475018, int (*)(Obj, void *))(cv, NULL) == 0) return NULL;
    return F(0x5477610, Obj (*)(Obj, void *))(cv, NULL);
}
static int hit(Obj comp, V2 m, Obj cam) {
    if (!u_alive(comp) || !u_go_active_self(u_go(comp))) return 0;
    return F(0x5473D40, bool (*)(Obj, V2, Obj, void *))(u_tr(comp), m, cam, NULL);
}
static void pick_region(int dir) {
    if (!mp_running()) { notice("Multiplayer is not running yet: this change cannot be applied"); return; }
    if (st.in_dungeon) { notice("Teams cannot change in a dungeon"); return; }
    int av[8], n = avail_regions(av, 8);
    if (!n) { notice("No region is open yet"); return; }
    int r = tm_region_step(av, n, st.me >= 0 ? region_pick[st.me] : -1, dir);
    send_region(r);
    shown_sig = 0;
}
/* button 0/1 at screen point m: 1 = handled */
static int click_at(V2 m, int button) {
    Obj hud = u_gc_get(h_hud);
    if (!opened) {
        if (button == 0 && hit(hud, m, cam_for(hud))) { if (open_screen()) notice("The Teams screen is not available here"); return 1; }
        return 0;
    }
    Obj cam = cam_for(root());
    if (hit(u_gc_get(h_close), m, cam) || hit(hud, m, cam_for(hud))) { close_screen(); return 1; }
    model_refresh();
    if (button == 0 && hit(u_gc_get(h_ready), m, cam)) { run(tm_click_ready(&st)); return 1; }
    if (hit(u_gc_get(h_region), m, cam)) { pick_region(button ? -1 : 1); return 1; }
    for (int i = 0; i < TM_MAX_UNITS; i++) {
        Obj p = u_gc_get(por[i].card);
        if (!por[i].card || !hit(p, m, cam)) continue;
        if (!por[i].clickable) return 1;                     /* other players' units: not clickable at all */
        int u = unit_by_guid(por[i].guid);
        if (u >= 0) run(tm_click_unit(&st, u, button, &hsel));
        return 1;
    }
    for (int k = 0; k < nhdr; k++)
        if (button == 0 && hit(u_gc_get(h_hdr[k]), m, cam)) { run(tm_click_header(&st, hdr_player[k], &hsel)); return 1; }
    return 1;                                                /* a click on the window or the dim layer */
}

/* HUD entry (owner 2026-10-13): the stock framed top-line button (Recruit popup clone) with the population icon and
   a "Teams" label in the title font, on the top canvas at a fixed place right of the clock / speed buttons, so it
   does not move when the portrait bar grows. Its Button is off: the click is hit-tested in click_at on the whole
   box (frame + label). Shown while the portrait bar is. */
#define HUD_X 400.0f
#define HUD_Y (-46.0f)
#define HUD_W 150.0f
#define HUD_H 40.0f
static uintptr_t h_hud_root;
static void hud_frame(void) {
    Obj v = shortcut_view();
    Obj hr = u_gc_get(h_hud_root);
    int show = v && F(0x50F3724, bool (*)(Obj, void *))(u_go(v), NULL);   /* activeInHierarchy */
    if (u_alive(hr) && u_alive(u_gc_get(h_hud))) { if (u_go_active_self(u_go(hr)) != show) u_go_active(u_go(hr), show); return; }
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
    h_hud = u_gc_new(box); h_hud_root = u_gc_new(rt);
    rig_log("teams: HUD button added on the top canvas (%s, %.0fx%.0f at %.0f,%.0f)", lx > 0 ? "top-line frame + icon" : "label only", HUD_W, HUD_H, HUD_X, HUD_Y);
}

/* ---------------------------------------------------------------- stock "Go to dungeon" (owner 2026-10-13)
   ExpeditionPlanView.InputOnAccept(DungeonRegion, List<Guid> units) 0xEE3A50 only sends UIInput 102 (the map
   change). In multiplayer it instead: picks that region, marks you Ready (your marked units; none marked = your
   own units among the panel's party, max 3), closes the panel (Close 0xEE3384) and opens the Teams screen. */
static void (*o_accept)(Obj, int, Obj, void *);
static void accept_core(Obj view, int region, const uint8_t (*pn)[16], int np);
static void hk_accept(Obj view, int region, Obj units, void *mi) {
    if (!sess()) { o_accept(view, region, units, mi); return; }   /* single player: stock */
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
    tm_panel_res r; tm_panel_party(&st, pn, np, &r);
    rig_log("teams: Go to dungeon pressed (region %d, panel %d units): %d go (%d to take), %d over the max, %d of other players%s",
            region, np, r.nmarks, r.nclaim, r.over, r.foreign, r.kept_old ? ", kept the old marks" : "");
    if (region >= 0 && region < 7) send_region(region);
    mp_session *s = sess();
    for (int k = 0; k < r.nclaim; k++) mp_claim(s, r.claim[k]);   /* runs before the Ready below (same sender, in order) */
    char msg[300]; size_t l = 0;
    if (r.nmarks) {
        send_marks(1, (const uint8_t (*)[16])r.marks, r.nmarks);
        l = (size_t)snprintf(msg, sizeof msg, "Ready with %d unit%s.", r.nmarks, r.nmarks > 1 ? "s" : "");
    } else l = (size_t)snprintf(msg, sizeof msg, "Choose 1 to %d of your units first.", TM_MAX_MARK);
    if (r.over) l += (size_t)snprintf(msg + l, sizeof msg - l, " Max %d units per player: %d not taken.", TM_MAX_MARK, r.over);
    if (r.foreign) l += (size_t)snprintf(msg + l, sizeof msg - l, " %d unit%s of other players: they choose their own.", r.foreign, r.foreign > 1 ? "s" : "");
    if (r.nmarks) snprintf(msg + l, sizeof msg - l, " The expedition starts when every player is ready.");
    notice("%s", msg);
    if (u_alive(view)) F(0xEE3384, void (*)(Obj, void *))(view, NULL);   /* ExpeditionPlanView.Close */
    if (!opened) open_screen();
}
/* World clicks under the open screen (owner 2026-10-13). Every mouse state of the game (left / right click,
   drag, hover, scroll, cursor) asks Util.UnityUtil.IsPointerOverUI 0xE54BA4 before it turns a click into a world
   command (SelectGrid / InteractGrid ...). The pair test showed the EventSystem raycast misses our backdrop, so
   while the screen is open this answers "over UI" for the whole screen: no click reaches the world. */
static bool (*o_over_ui)(void *);
static bool hk_over_ui(void *mi) {
    if (opened && root()) return true;
    return o_over_ui(mi);
}
static int hooks_done;
static void install_hooks(void) {
    if (hooks_done) return;
    hooks_done = 1;
    rig_hook(0xEE3A50, hk_accept, (void **)&o_accept, "teams ExpeditionPlanView.InputOnAccept");
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
static int fk_phase(void) { return fk_on ? (int)(rig_frames - fk_start) : -1; }   /* 0 = down frame, 1 = up frame */
static bool hk_mb(int b, void *mi) { int ph = fk_phase(); if (ph >= 0 && b == fk_btn) return ph == 0; if (ph >= 0) return false; return o_mb(b, mi); }
static bool hk_mbd(int b, void *mi) { int ph = fk_phase(); if (ph >= 0) return b == fk_btn && ph == 0; return o_mbd(b, mi); }
static bool hk_mbu(int b, void *mi) { int ph = fk_phase(); if (ph >= 0) return b == fk_btn && ph == 1; return o_mbu(b, mi); }
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

static void start_frame(void);
void ui_teams_frame(void) {
    if (!hooks_done) install_hooks();
#ifdef TEAMS_MOUSE_TEST
    fake_frame();
#endif
    static unsigned n; n++;
    if (scene() != built_scene) {                            /* scene change: the clones died with the old scene */
        built_scene = scene();
        opened = 0; hsel = (tm_sel)TM_SEL_INIT; free_all();
        u_gc_free(h_hud); h_hud = 0; u_gc_free(h_hud_root); h_hud_root = 0; u_gc_free(h_shortcut); h_shortcut = 0;
    }
    region_session();
    if (me() < 0 || scene() != 2) { if (opened) close_screen(); return; }
    start_frame();
    if (n % 30 == 1) hud_frame();
    for (int b = 0; b < 2; b++)
        if (F(0x51ACDD0, bool (*)(int, void *))(b, NULL)) {   /* Input.GetMouseButtonDown */
            V3 mp = F(0x51AD4DC, V3 (*)(void *))(NULL);
            click_at((V2){mp.x, mp.y}, b);
        }
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
    dsnet_client *c = cli();
    rig_log("teams: expedition start: %d players, %d units in the party%s", p[0], n, c && dsnet_is_host(c) ? " (host sends the map change)" : "");
    if (c && dsnet_is_host(c) && n > 0) start_pending = 1;   /* send from the frame, not inside command apply */
    if (opened) close_screen();
    notice(n ? "Everyone is ready: the expedition starts" : "Everyone is ready, but nobody marked a unit");
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
    int32_t region = region_result(); int skip = AT(cl, 0x54, uint8_t);
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
    if (u_alive(im)) { Obj sp = F(0x5266F04, Obj (*)(Obj, void *))(im, NULL); char sn[96] = "-"; if (u_alive(sp)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(sp, NULL), sn, sizeof sn); bput(d, " Image(%s)", sn); }
    if (u_alive(tx)) { char tv[64]; str_utf8(tmp_get(tx), tv, sizeof tv); Obj fa = F(0x4F0A59C, Obj (*)(Obj, void *))(tx, NULL); char fn[64] = "-";
                       if (u_alive(fa)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(fa, NULL), fn, sizeof fn);
                       bput(d, " TMP(%s, %.0f, \"%s\")", fn, F(0x4F0B338, float (*)(Obj, void *))(tx, NULL), tv); }
    if (u_alive(bt)) bput(d, " Button");
    if (depth >= maxd) return;
    int n = F(0x510EAE4, int (*)(Obj, void *))(t, NULL);
    for (int i = 0; i < n && i < 40; i++) dump_tree(d, F(0x510FC60, Obj (*)(Obj, int, void *))(t, i, NULL), depth + 1, maxd);
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
    int layer[128], mode[128], root_[128], slayer[128];
    Obj sl = F(0x5079198, Obj (*)(void *))(NULL);             /* SortingLayer.layers: the UI must beat world sprites */
    int nsl = sl ? AT(sl, 0x18, int32_t) : 0, top_id = nsl ? AT(sl, 0x20 + 4 * (nsl - 1), int32_t) : 0;
    float pd = F(0x507FC2C, float (*)(Obj, void *))(cam, NULL) + 0.5f;
    /* the world's sprites sort above a camera-space canvas: render the UI layer only (Camera.cullingMask has no
       managed setter in this build; the engine icall is still registered) */
    static void (*set_mask)(void *, int);
    if (!set_mask) set_mask = F(0x6F8B90, void *(*)(const char *))("UnityEngine.Camera::set_cullingMask_Injected");
    int old_mask = F(0x5080BC8, int (*)(Obj, void *))(cam, NULL);
    if (set_mask) set_mask(AT(cam, 0x10, void *), 1 << 5);
    for (int i = 0; i < n; i++) {
        Obj c = AT(arr, 0x20 + 8 * i, Obj);
        layer[i] = F(0x50F3118, int (*)(Obj, void *))(u_go(c), NULL);
        F(0x50F3204, void (*)(Obj, int, void *))(u_go(c), 5, NULL);        /* UI layer */
        root_[i] = F(0x547521C, bool (*)(Obj, void *))(c, NULL);
        slayer[i] = F(0x5476728, int (*)(Obj, void *))(c, NULL);
        F(0x5476814, void (*)(Obj, int, void *))(c, top_id, NULL);
        mode[i] = F(0x5475018, int (*)(Obj, void *))(c, NULL);
        if (root_[i] && mode[i] == 0) {
            F(0x5475104, void (*)(Obj, int, void *))(c, 1, NULL);
            F(0x547771C, void (*)(Obj, Obj, void *))(c, cam, NULL);
            F(0x5475F18, void (*)(Obj, float, void *))(c, pd, NULL);
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
        if (root_[i] && mode[i] == 0) { F(0x547771C, void (*)(Obj, Obj, void *))(c, NULL, NULL); F(0x5475104, void (*)(Obj, int, void *))(c, 0, NULL); }
        F(0x50F3204, void (*)(Obj, int, void *))(u_go(c), layer[i], NULL);
        F(0x5476814, void (*)(Obj, int, void *))(c, slayer[i], NULL);
    }
    if (set_mask) set_mask(AT(cam, 0x10, void *), old_mask);
    F(0x5477FEC, void (*)(void *))(NULL);
    rig_log("teams: capture mask %s", set_mask ? "UI only" : "unchanged (icall missing)");
    return ok ? 0 : -3;
}

/* ---------------------------------------------------------------- socket: ui_teams {action, guid, button, player}
   state = what is on screen, read back from the widgets themselves (portrait unit = parts._owner +0x228,
   header text = TMP text); open / close / click / header / ready run the same code as a mouse click. */
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
        float alpha = u_alive(cg) ? F(0x546F9A8, float (*)(Obj, void *))(cg, NULL) : -1;
        bput(&b, "%s{\"guid\":", w++ ? "," : ""); put_guid(&b, por[i].guid);
        bput(&b, ",\"unit_guid\":"); put_guid(&b, (const uint8_t *)p + 0x54);
        bput(&b, ",\"name\":"); bstr(&b, nm);
        Obj mk = u_gc_get(por[i].mark);
        bput(&b, ",\"x\":%.0f,\"y\":%.0f,\"active\":%d,\"selected\":%d,\"section\":%d,\"clickable\":%d,\"alpha\":%.2f,\"mark_shown\":%d}", pos.x, pos.y,
             u_go_active_self(u_go(p)), por[i].marked, por[i].owner, por[i].clickable, alpha, u_alive(mk) && u_go_active_self(u_go(mk)));
    }
    /* world clicks: what the game's mouse states ask (UnityUtil.IsPointerOverUI, hooked above) and the raw raycast */
    V3 mp = F(0x51AD4DC, V3 (*)(void *))(NULL);
    bput(&b, "],\"mouse\":[%.0f,%.0f],\"over_ui\":%d,\"over_ui_raycast\":%d", mp.x, mp.y,
         F(0xE54BA4, bool (*)(void *))(NULL), F(0xE548C8, bool (*)(void *))(NULL));
    bput(&b, ",\"region_picks\":[");
    for (int p = 0; p < TM_MAX_PLAYERS; p++) if (st.present >> p & 1u) bput(&b, "%s[%d,%d]", (st.present & ((1u << p) - 1)) ? "," : "", p, region_pick[p]);
    int av[8], na = avail_regions(av, 8);
    bput(&b, "],\"region_result\":%d,\"regions_open\":[", region_result());
    for (int i = 0; i < na; i++) bput(&b, "%s%d", i ? "," : "", av[i]);
    bput(&b, "]");
    { Obj hr = u_gc_get(h_hud_root), tty = type_object("TMPro", "TMP_Text"); V2 hp = u_alive(hr) ? rt_get_pos(hr) : (V2){0, 0}; char ht[64] = "";
      Obj ht_ = u_alive(hr) && tty ? F(0x50EDDF4, Obj (*)(Obj, Obj, bool, void *))(hr, tty, false, NULL) : NULL;
      if (u_alive(ht_)) str_utf8(tmp_get(ht_), ht, sizeof ht);
      bput(&b, ",\"hud_pos\":[%.0f,%.0f],\"hud_text\":", hp.x, hp.y); bstr(&b, ht); }
    char t1[200] = "", t2[700] = "", t3[400] = "", t4[700] = "", t5[200] = "";
    str_utf8(tmp_get(u_gc_get(h_rules)), t4, sizeof t4); str_utf8(tmp_get(u_gc_get(h_title)), t5, sizeof t5);
    bput(&b, ",\"rules\":"); bstr(&b, t4); bput(&b, ",\"title\":"); bstr(&b, t5);
    { Obj tt = u_gc_get(h_title), fa = u_alive(tt) ? F(0x4F0A59C, Obj (*)(Obj, void *))(tt, NULL) : NULL; char fn[64] = "";
      Obj bt = u_gc_get(h_rules), fb = u_alive(bt) ? F(0x4F0A59C, Obj (*)(Obj, void *))(bt, NULL) : NULL; char fn2[64] = "";
      if (u_alive(fa)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(fa, NULL), fn, sizeof fn);
      if (u_alive(fb)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(fb, NULL), fn2, sizeof fn2);
      bput(&b, ",\"font_title\":"); bstr(&b, fn); bput(&b, ",\"font_body\":"); bstr(&b, fn2);
      Obj ww = win(); R4 wr = ww ? rt_rect(ww) : (R4){0}; Obj rr = root(); R4 rr4 = rr ? rt_rect(u_tr(rr)) : (R4){0};
      bput(&b, ",\"window\":[%.0f,%.0f],\"root\":[%.0f,%.0f]", wr.w, wr.h, rr4.w, rr4.h); }
    str_utf8(tmp_get(u_gc_get(h_ready)), t1, sizeof t1); str_utf8(tmp_get(u_gc_get(h_hint)), t2, sizeof t2);
    str_utf8(tmp_get(u_gc_get(h_region)), t3, sizeof t3);
    bput(&b, ",\"region_text\":"); bstr(&b, t3);
    { Obj r = root(), cty = type_object("UnityEngine", "Canvas"), gty = type_object("UnityEngine.UI", "GraphicRaycaster");
      Obj cv = r && cty ? go_component(u_go(r), cty) : NULL, gr = r && gty ? go_component(u_go(r), gty) : NULL;
      bput(&b, ",\"modal_sorting\":%d,\"modal_raycaster\":%d", u_alive(cv) ? F(0x5476320, int (*)(Obj, void *))(cv, NULL) : -1, u_alive(gr)); }
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
        fk_x = (float)x; fk_y = (float)y; fk_btn = (int)bt; fk_start = rig_frames + 1; fk_on = 1;
        rig_log("teams: fake mouse %d at %.0f,%.0f from frame %lld", fk_btn, x, y, (long long)fk_start);
#endif
    } else if (!strcmp(act, "debug")) {                     /* raycast facts for the open screen and its parents */
        Buf d = {0}; Obj r = root();
        Obj cty = type_object("UnityEngine", "Canvas"), gty = type_object("UnityEngine.UI", "GraphicRaycaster"), cgt = type_object("UnityEngine", "CanvasGroup");
        if (r) { R4 rr = rt_rect(u_tr(r)); V2 pt = {F(0x5092C94, int (*)(void *))(NULL) / 2.0f, F(0x5092CD4, int (*)(void *))(NULL) / 2.0f};
                 bput(&d, "root rect %.0fx%.0f screen %.0fx%.0f centre-in-root %d; ", rr.w, rr.h, pt.x * 2, pt.y * 2,
                      F(0x5473D40, bool (*)(Obj, V2, Obj, void *))(u_tr(r), pt, NULL, NULL)); }
        if (r) bput(&d, "root depth %d raycastTarget %d activeInHierarchy %d; ", F(0x5261B60, int (*)(Obj, void *))(r, NULL),
                    F(0x5260340, bool (*)(Obj, void *))(r, NULL), F(0x50F3724, bool (*)(Obj, void *))(u_go(r), NULL));
        for (Obj t = r ? u_tr(r) : NULL; t; t = u_tr_parent(t)) {
            Obj go = u_go(t); char nm[80]; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(go, NULL), nm, sizeof nm);
            Obj cv = cty ? go_component(go, cty) : NULL, gr = gty ? go_component(go, gty) : NULL, cg = cgt ? go_component(go, cgt) : NULL;
            bput(&d, "[%s", nm);
            if (u_alive(cv)) bput(&d, " canvas(mode %d order %d override %d)", F(0x5475018, int (*)(Obj, void *))(cv, NULL),
                                  F(0x5476320, int (*)(Obj, void *))(cv, NULL), F(0x547611C, bool (*)(Obj, void *))(cv, NULL));
            if (u_alive(gr)) bput(&d, " raycaster(on %d)", F(0x50ECFB8, bool (*)(Obj, void *))(gr, NULL));
            if (u_alive(cg)) bput(&d, " group(blocks %d ignoreParent %d)", F(0x546FDB0, bool (*)(Obj, void *))(cg, NULL), F(0x546FFB4, bool (*)(Obj, void *))(cg, NULL));
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
    } else if (!strcmp(act, "region")) {
        if (!opened) rc = -2; else { model_refresh(); pick_region(bt ? -1 : 1); }
    } else if (!strcmp(act, "admin")) {                     /* host: the same DSNET_ADMIN_MODE command the lobby toggle sends */
        double on_ = 1; jnum(line, "on", &on_); uint8_t v = on_ != 0; dsnet_client *c = cli();
        rc = c && dsnet_is_host(c) ? dsnet_submit(c, 14, 0, &v, 1) : -1; shown_sig = 0;
    } else if (!strcmp(act, "relayout")) { shown_sig = 0; if (opened) { model_refresh(); layout(); } }
    else if (strcmp(act, "state")) rc = -1;
    return state_json(rc);
}
