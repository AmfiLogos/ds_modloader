// ui_game: in-game multiplayer interface for Dungeon Settlers build 25846389 (arm64). Dylib only.
// Built from the game's own widgets (Object.Instantiate of real buttons / texts) and hooks; the rules
// live in ui_model.c. Hook list, RVAs and what to check in the game: UI_NOTES.md.
#include "ui_tip.h"
#include "ui_mp.h"
#include "ui_model.h"
#include "rig.h"
#include "ord_api.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <math.h>

/* ---------------------------------------------------------------- il2cpp / Unity (same RVAs as ~/ds_multibar game.h) */
static inline Obj u_str(const char *s) { return F(0x6F9738, Obj (*)(const char *))(s); }
static inline uintptr_t u_gc_new(Obj o) { return o ? F(0x6F9540, uintptr_t (*)(Obj, bool))(o, false) : 0; }   /* pointer-sized handle */
static inline Obj u_gc_get(uintptr_t h) { return h ? F(0x6F9598, Obj (*)(uintptr_t))(h) : NULL; }
static inline void u_gc_free(uintptr_t h) { if (h) F(0x6F95E4, void (*)(uintptr_t))(h); }
static inline bool u_alive(Obj o) { return o && F(0x50FAF88, bool (*)(Obj, void *))(o, NULL); }
/* Checked calls (lead 2026-10-13, after the 15:45 crash: Graphic.set_color was called on a Transform): every typed
   Unity call on a cloned or looked-up object goes through ui_is(), which checks the object's class with
   il2cpp_object_get_class (0x6F96C0) + il2cpp_class_is_assignable_from (0x6F8BE0); a mismatch is logged once per
   (kind, class) and the call is skipped. */
enum { K_RT, K_TR, K_TMP, K_GR, K_IMG, K_SEL, K_BEH, K_N };
static int ui_is(Obj o, int k);
static inline Obj u_go(Obj comp) { return F(0x50ED970, Obj (*)(Obj, void *))(comp, NULL); }
static inline Obj u_tr(Obj comp) { return F(0x50ED864, Obj (*)(Obj, void *))(comp, NULL); }
static inline void u_go_active(Obj go, bool v) { if (go) F(0x50F3520, void (*)(Obj, bool, void *))(go, v, NULL); }
static inline bool u_go_active_self(Obj go) { return go && F(0x50F3638, bool (*)(Obj, void *))(go, NULL); }
static inline void u_active(Obj comp, bool v) { if (u_alive(comp)) u_go_active(u_go(comp), v); }
static inline Obj u_instantiate(Obj orig, Obj parent_tr) { return F(0x50FC30C, Obj (*)(Obj, Obj, void *))(orig, parent_tr, NULL); }
static inline Obj u_tr_parent(Obj t) { return F(0x510992C, Obj (*)(Obj, void *))(t, NULL); }
static inline int u_sibling(Obj t) { return F(0x510F070, int (*)(Obj, void *))(t, NULL); }
static inline void u_set_sibling(Obj t, int i) { F(0x510EE44, void (*)(Obj, int, void *))(t, i, NULL); }
typedef struct { float x, y; } V2;
typedef struct { float x, y, z; } V3;
static inline V2 u_rt_pos(Obj rt) { return (ui_is(rt, K_RT) ? F(0x5106E38, V2 (*)(Obj, void *))(rt, NULL) : (V2){0,0}); }
static inline void u_rt_set_pos(Obj rt, float x, float y) { (ui_is(rt, K_RT) ? F(0x5106F2C, void (*)(float, float, Obj, void *))(x, y, rt, NULL) : (void)0); }
static inline V2 u_rt_size(Obj rt) { return (ui_is(rt, K_RT) ? F(0x510701C, V2 (*)(Obj, void *))(rt, NULL) : (V2){0,0}); }
static inline void u_rt_set_size(Obj rt, float x, float y) { (ui_is(rt, K_RT) ? F(0x5107110, void (*)(float, float, Obj, void *))(x, y, rt, NULL) : (void)0); }
static inline Obj u_clipboard(void) { return F(0x518AEA0, Obj (*)(void *))(NULL); }   /* GUIUtility.systemCopyBuffer */
/* TMP_Text.text: virtual slots 65 (get, vtable +0x548) / 66 (set, +0x558), as the game calls them */
static Obj tmp_get(Obj t) {
    if (!ui_is(t, K_TMP)) return NULL;
    void *k = *(void **)t;
    return (*(Obj (**)(Obj, void *))((char *)k + 0x548))(t, *(void **)((char *)k + 0x550));
}
static void tmp_set(Obj t, const char *s) {
    if (!ui_is(t, K_TMP)) return;
    void *k = *(void **)t;
    (*(void (**)(Obj, Obj, void *))((char *)k + 0x558))(t, u_str(s), *(void **)((char *)k + 0x560));
}
/* class name of an object (Il2CppClass.name at +0x10) */
static const char *cls_name(Obj o) { return o ? *(const char **)((char *)*(void **)o + 0x10) : ""; }
#define TI_UnitEntity 0x6656458
static bool is_unit(Obj o) {
    if (!o) return false;
    void *k = *(void **)o, *t = md(TI_UnitEntity);
    uint8_t d = AT(t, 0x130, uint8_t), kd = AT(k, 0x130, uint8_t);
    return kd >= d && AT(k, 0xC8, void **)[d - 1] == t;
}
#define UNIT_GUID 0x24
/* il2cpp String (len +0x10, UTF-16 +0x14) -> UTF-8 */
static void str_utf8(Obj s, char *out, size_t cap) {
    size_t n = 0;
    if (cap) out[0] = 0;
    if (!s || !cap) return;
    int32_t len = AT(s, 0x10, int32_t);
    const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    for (int32_t i = 0; i < len; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < len) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
        char b[4]; int m;
        if (u < 0x80) { b[0] = (char)u; m = 1; }
        else if (u < 0x800) { b[0] = (char)(0xC0 | u >> 6); b[1] = (char)(0x80 | (u & 63)); m = 2; }
        else if (u < 0x10000) { b[0] = (char)(0xE0 | u >> 12); b[1] = (char)(0x80 | (u >> 6 & 63)); b[2] = (char)(0x80 | (u & 63)); m = 3; }
        else { b[0] = (char)(0xF0 | u >> 18); b[1] = (char)(0x80 | (u >> 12 & 63)); b[2] = (char)(0x80 | (u >> 6 & 63)); b[3] = (char)(0x80 | (u & 63)); m = 4; }
        if (n + (size_t)m >= cap) break;
        memcpy(out + n, b, (size_t)m); n += (size_t)m;
    }
    out[n] = 0;
}
static void unit_name(Obj unit, char *out, size_t cap) {   /* EntityDisplayNameExtensions.GetDisplayName */
    if (!unit) { snprintf(out, cap, "this unit"); return; }
    str_utf8(F(0xFA467C, Obj (*)(Obj, void *))(unit, NULL), out, cap);
    if (!out[0]) snprintf(out, cap, "this unit");
}

/* ---------------------------------------------------------------- session view */
static int story_held;                                       /* this peer sent DSNET_STORY_PAUSE 1 */
/* live-test settings (owner 2026-10-13), saved in <persistentDataPath>/mp_ui.json */
static ui_settings uiset; static int uiset_loaded;
static void settings_path(char *out, size_t cap) {
    char d[900]; str_utf8(F(0x507B74C, Obj (*)(void *))(NULL), d, sizeof d);   /* Application.persistentDataPath */
    snprintf(out, cap, "%s/mp_ui.json", d);
}
static void settings_load(void) {
    if (uiset_loaded) return;
    uiset_loaded = 1; ui_settings_default(&uiset);
    char pth[1024]; settings_path(pth, sizeof pth);
    FILE *f = fopen(pth, "r"); if (!f) return;
    char b[2048]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;   /* lan_v10: colour choices make it longer */
    ui_settings_parse(b, &uiset);
}
static void settings_save(void) {
    char pth[1024], b[2048]; settings_path(pth, sizeof pth);
    ui_settings_format(&uiset, b, sizeof b);
    FILE *f = fopen(pth, "w"); if (f) { fputs(b, f); fclose(f); }
}
static int result_open, result_chooser, result_forced, result_close;   /* return-to-base screen (popup 3) */
static double map_changing;                                  /* lan_v9 (lead): an agreed map change is running (now_s) */
static int holds_cleared;                                    /* test probe: map changes that released a hold (8e) */
static uintptr_t h_sysmenu;                                  /* SystemMenuView (8f) */
#define MAX_DIS 128
typedef struct { float r, g, b, a; } GCol;
typedef struct { uintptr_t h[MAX_DIS]; int n; uintptr_t gh[MAX_DIS]; GCol gc[MAX_DIS]; int ng; int dim; uintptr_t ih[MAX_DIS]; int ni; } dis_list;
static dis_list esc_dis = { .dim = 1 };                      /* guest's Save button (8f) */
static uint8_t result_guid[16];
#define UI_ORDER_RESULT_GO 7                                 /* return-to-base pick (guid = portal unit) */
#define UI_ORDER_RESULT_CANCEL 37                            /* lan_v9: the picker closed the Portal screen with its X */
static int result_picked;                                    /* this peer (the picker) sent its pick */
static mp_session *sess(void) { return mp_game_session ? mp_game_session() : NULL; }
static dsnet_client *cli(void) { mp_session *s = sess(); return s ? mp_client(s) : NULL; }
static int me(void) { dsnet_client *c = cli(); return c ? dsnet_player_id(c) : -1; }
static int on(void) { return me() >= 0; }                    /* session up and welcomed */
/* Spectator (owner 2026-10-14): this copy watches. It sees everything, chats and pings, owns no units; every order, vote,
   claim and popup choice is refused here with "You are watching" (dsnet refuses its commands anyway). */
static int spect(void) { mp_session *s = sess(); return s && mp_is_spectator(s); }
static int owner_of(Obj unit) {
    dsnet_client *c = cli();
    if (!c || !unit) return UI_UNCLAIMED;
    return dsnet_owner_of(c, (const uint8_t *)unit + UNIT_GUID);
}
/* a player's name in his own colour (uxreview, one colour per player everywhere): "<color=#..>Ben</color>" */
static const char *pname(const ui_players *p, int player, char *out, size_t cap) {
    char nm[40]; snprintf(out, cap, "<color=%s>%s</color>", ui_player_color(player), ui_player_label(p, player, nm)); return out;
}
static int owner_of_guid(const uint8_t g[16]) { dsnet_client *c = cli(); return c ? dsnet_owner_of(c, g) : UI_UNCLAIMED; }
/* lend control (owner 2026-10-13): who orders the unit now = the borrower while lent (dsnet_controller_of), else the owner */
static int eff_of_guid(const uint8_t g[16]) { dsnet_client *c = cli(); int k = c ? dsnet_lent_to(c, g) : -1; return k >= 0 ? k : owner_of_guid(g); }
static int eff_of(Obj unit) { return unit ? eff_of_guid((const uint8_t *)unit + UNIT_GUID) : UI_UNCLAIMED; }
/* host admin mode (owner 2026-10-13): one flag in dsnet's session state (DSNET_ADMIN_MODE, host-only) */
static int admin_on(void) { dsnet_client *c = cli(); return c ? dsnet_admin_mode(c) : 0; }
static int may_order(int owner) {                          /* reserved (owner away): host with admin only */
    if (owner == UI_RESERVED) return me() == 0 && admin_on();
    return ui_may_order_admin(me(), owner, admin_on());
}
static void players(ui_players *p);
int ui_admin_mode(void) { return on() ? admin_on() : 0; }   /* for other lanes (Teams): dlsym "ui_admin_mode" */
static void reserved_name(const uint8_t g[16], char *out, size_t cap) {   /* "Reserved for <name>" */
    uint8_t uid[16]; char nm[32] = "";
    dsnet_client *c = cli();
    if (c) dsnet_owner_info(c, g, uid, nm);
    snprintf(out, cap, "%s", nm[0] ? nm : "an absent player");
}
static void owner_tag_for(const uint8_t g[16], int o, char *tag, size_t cap) {
    ui_players p; players(&p);
    if (o == UI_RESERVED) { char nm[32]; reserved_name(g, nm, sizeof nm); snprintf(tag, cap, " <color=#8a8088>[%s, away]</color>", nm); }
    else ui_owner_tag(&p, o, tag, cap);
}
/* lan_v10 (owner 01:40): each viewer may choose other players' colours for their own screen (cog rows, mp_ui.json
   "colors", by stable uid). ui_player_color asks this first (ui_model.c ui_color_override). */
static int uid_hex_of(int player, char out[33]) {
    dsnet_client *c = cli(); uint8_t u[16];
    if (!c || player < 0 || dsnet_player_uid(c, player, u) != 0) return 0;
    for (int i = 0; i < 16; i++) snprintf(out + 2 * i, 3, "%02x", u[i]);
    return 1;
}
static int color_override_of(int player) {
    if (player == me()) return -1;                           /* only other players */
    char h[33]; settings_load();
    return uiset.ncol && uid_hex_of(player, h) ? ui_settings_color_get(&uiset, h) : -1;
}
static void color_step(int player, int dir) {                /* Auto -> 0 .. 7 -> Auto */
    char h[33]; if (!uid_hex_of(player, h)) return;
    settings_load();
    int cur = ui_settings_color_get(&uiset, h), nx = cur + dir;
    if (cur < 0) nx = dir > 0 ? 0 : UI_PALETTE_N - 1;
    if (nx >= UI_PALETTE_N || nx < 0) nx = -1;
    ui_settings_color_set(&uiset, h, nx);
    rig_log("ui: colour of player %d on this screen: %s", player, nx < 0 ? "auto" : ui_palette_name(nx));
}
static void players(ui_players *p) {
    memset(p, 0, sizeof *p);
    dsnet_client *c = cli();
    if (!c) { ui_spectators = 0; return; }
    p->present = dsnet_present_mask ? dsnet_present_mask(c) : (1u | 1u << (unsigned)dsnet_player_id(c));
    if (dsnet_player_name)
        for (int i = 0; i < UI_MAX_PLAYERS; i++) { const char *n = dsnet_player_name(c, i); if (n) snprintf(p->name[i], sizeof p->name[i], "%s", n); }
    /* spectators: never present (outside the sim), but named for chat, pings and the "Watching" rows; grey (ui_model) */
    ui_spectators = dsnet_spectator_mask(c);
    for (int i = 0; i < UI_MAX_PLAYERS; i++) if ((ui_spectators >> i & 1u) && !p->name[i][0]) dsnet_spectator_info(c, i, p->name[i], NULL, NULL);
}

/* ---------------------------------------------------------------- notices (HelpNoticeView, the game's toast) */
/* A view found by class (Object.FindObjectOfType(Type, includeInactive) 0x50FD97C); handle cached. Saves the
   Init hooks: the rig cave holds 80 stubs and the lanes use them all. */
void *ord_debug_class(const char *ns, const char *name) __attribute__((weak_import));
__attribute__((noinline)) static int ui_is(Obj o, int k) {
    static const char *const ns[K_N] = { "UnityEngine", "UnityEngine", "TMPro", "UnityEngine.UI", "UnityEngine.UI", "UnityEngine.UI", "UnityEngine" };
    static const char *const nm[K_N] = { "RectTransform", "Transform", "TMP_Text", "Graphic", "Image", "Selectable", "Behaviour" };
    static void *cls[K_N]; static int missing[K_N];
    if (!u_alive(o) || k < 0 || k >= K_N) return 0;
    if (!cls[k]) {
        cls[k] = ord_debug_class ? ord_debug_class(ns[k], nm[k]) : NULL;
        if (!cls[k]) { if (!missing[k]++) rig_log("ui: class %s.%s not found: its calls are not type-checked", ns[k], nm[k]); return 1; }
    }
    void *oc = F(0x6F96C0, void *(*)(Obj))(o);                                  /* il2cpp_object_get_class */
    if (oc && F(0x6F8BE0, bool (*)(void *, void *))(cls[k], oc)) return 1;      /* il2cpp_class_is_assignable_from */
    static struct { int k; void *c; } seen[32]; static int nseen;
    int known = 0;
    for (int i = 0; i < nseen && !known; i++) known = seen[i].k == k && seen[i].c == oc;
    if (!known) {
        if (nseen < 32) { seen[nseen].k = k; seen[nseen].c = oc; nseen++; }
        Dl_info di = {0}; void *ra = __builtin_return_address(0); dladdr(ra, &di);   /* caller: atos -o dsrig.dylib -l 0 <off> */
        rig_log("ui: TYPE CHECK: a %s.%s call was skipped on a %s (caller dylib +%#lx)", ns[k], nm[k], oc ? *(const char **)((char *)oc + 0x10) : "?",
                (unsigned long)((uintptr_t)ra - (uintptr_t)di.dli_fbase));
    }
    return 0;
}
static Obj find_view(uintptr_t *h, const char *ns, const char *name) {
    Obj v = u_gc_get(*h);
    if (u_alive(v)) return v;
    /* GameAssembly exports by address (dlsym does not see them: the image is not loaded RTLD_GLOBAL) */
    void *k = ord_debug_class ? ord_debug_class(ns, name) : NULL;
    if (!k) { rig_log("ui: class %s.%s not found", ns, name); return NULL; }
    void *type = F(0x6F8C9C, void *(*)(void *))(k);                   /* il2cpp_class_get_type */
    Obj tobj = F(0x6F97AC, Obj (*)(void *))(type);                     /* il2cpp_type_get_object */
    v = F(0x50FD97C, Obj (*)(Obj, bool, void *))(tobj, false, NULL);    /* an active one first (scene copies) */
    if (!u_alive(v)) v = F(0x50FD97C, Obj (*)(Obj, bool, void *))(tobj, true, NULL);
    u_gc_free(*h); *h = u_gc_new(v);
    return u_alive(v) ? v : NULL;
}
static uintptr_t h_help;
static void notice(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void notice(const char *fmt, ...) {
    static char last[256]; static double last_t;
    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    double t = ts.tv_sec + ts.tv_nsec / 1e9;
    if (!strcmp(b, last) && t - last_t < 1.5) return;        /* no spam on repeated clicks */
    snprintf(last, sizeof last, "%s", b); last_t = t;
    rig_log("ui: %s", b);
    Obj v = find_view(&h_help, "Refactor", "HelpNoticeView");
    if (v) F(0xFDC9FC, void (*)(Obj, Obj, void *))(v, u_str(b), NULL);   /* HelpNoticeView.Show(text) */
}

/* Read-only views: every Selectable under a view made non-interactable (greyed by the stock tint), and exactly
   those restored later. Selectable.m_Interactable +0xD8, set_interactable 0x549C6B4. */
static Obj type_of(const char *ns, const char *name);
/* Graphic.color through the vtable (slots 22 / 23): TMP_Text overrides it (its font colour) */
static GCol g_color(Obj g) { if (!ui_is(g, K_GR)) return (GCol){ 1, 1, 1, 1 }; void *k = *(void **)g; return (*(GCol (**)(Obj, void *))((char *)k + 0x298))(g, *(void **)((char *)k + 0x2A0)); }
static void g_set_color(Obj g, GCol c) { if (!ui_is(g, K_GR)) return; void *k = *(void **)g; (*(void (**)(Obj, GCol, void *))((char *)k + 0x2A8))(g, c, *(void **)((char *)k + 0x2B0)); }
/* the stock disabled look is a faint tint; d->dim adds the design report's stock disabled state (12b): labels in the
   muted #a69085 at alpha 0.5, images at alpha 0.5, and no hover (the button's InteractableUI feedback switched off).
   Everything is restored after. */
static void dis_dim(dis_list *d, Obj sel) {
    static uintptr_t h_gtype; Obj gt = u_gc_get(h_gtype);
    if (!gt) { gt = type_of("UnityEngine.UI", "Graphic"); if (!gt) return; h_gtype = u_gc_new(gt); }
    Obj arr = F(0x50EDFD0, Obj (*)(Obj, Obj, void *))(sel, gt, NULL);
    int n = arr ? AT(arr, 0x18, int32_t) : 0;
    for (int i = 0; i < n && d->ng < MAX_DIS; i++) {
        Obj g = AT(arr, 0x20 + 8 * i, Obj);
        if (!u_alive(g)) continue;
        int known = 0;
        for (int k = 0; k < d->ng && !known; k++) known = u_gc_get(d->gh[k]) == g;
        if (known) continue;
        GCol c = g_color(g); d->gc[d->ng] = c; d->gh[d->ng++] = u_gc_new(g);
        if (strstr(cls_name(g), "Text")) { GCol m = { 0xa6 / 255.0f, 0x90 / 255.0f, 0x85 / 255.0f, 0.5f }; g_set_color(g, m); }
        else { c.a *= 0.5f; g_set_color(g, c); }
    }
    static uintptr_t h_itype; Obj it = u_gc_get(h_itype);   /* hover feedback (Refactor.UI.Util.InteractableUI) off */
    if (!it) { it = type_of("Refactor.UI.Util", "InteractableUI"); if (it) h_itype = u_gc_new(it); }
    Obj ia = it ? F(0x50EDFD0, Obj (*)(Obj, Obj, void *))(sel, it, NULL) : NULL;
    int ni = ia ? AT(ia, 0x18, int32_t) : 0;
    for (int i = 0; i < ni && d->ni < MAX_DIS; i++) {
        Obj b = AT(ia, 0x20 + 8 * i, Obj);
        if (!u_alive(b) || !(ui_is(b, K_BEH) ? F(0x50ECDB4, bool (*)(Obj, void *))(b, NULL) : false)) continue;          /* Behaviour.enabled */
        (ui_is(b, K_BEH) ? F(0x50ECEA0, void (*)(Obj, bool, void *))(b, false, NULL) : (void)0);
        d->ih[d->ni++] = u_gc_new(b);
    }
}
static void dis_apply(dis_list *d, Obj view) {              /* call again to catch buttons the stock re-enabled */
    static uintptr_t h_seltype; Obj tt = u_gc_get(h_seltype);
    if (!tt) { tt = type_of("UnityEngine.UI", "Selectable"); if (!tt) return; h_seltype = u_gc_new(tt); }
    if (!u_alive(view)) return;
    Obj arr = F(0x50EDFD0, Obj (*)(Obj, Obj, void *))(view, tt, NULL);   /* GetComponentsInChildren(Type) */
    int n = arr ? AT(arr, 0x18, int32_t) : 0;
    for (int i = 0; i < n; i++) {
        Obj sel = AT(arr, 0x20 + 8 * i, Obj);
        if (!u_alive(sel)) continue;
        if (!AT(sel, 0xD8, uint8_t)) { if (d->dim) dis_dim(d, sel); continue; }   /* already off (stock or ours): just the look */
        (ui_is(sel, K_SEL) ? F(0x549C6B4, void (*)(Obj, bool, void *))(sel, false, NULL) : (void)0);
        int known = 0;
        for (int k = 0; k < d->n && !known; k++) known = u_gc_get(d->h[k]) == sel;
        if (!known && d->n < MAX_DIS) d->h[d->n++] = u_gc_new(sel);
        if (d->dim) dis_dim(d, sel);
    }
}
static void dis_restore(dis_list *d) {
    for (int i = 0; i < d->n; i++) { Obj sel = u_gc_get(d->h[i]); if (u_alive(sel)) (ui_is(sel, K_SEL) ? F(0x549C6B4, void (*)(Obj, bool, void *))(sel, true, NULL) : (void)0); u_gc_free(d->h[i]); }
    for (int i = 0; i < d->ng; i++) { Obj g = u_gc_get(d->gh[i]); if (u_alive(g)) g_set_color(g, d->gc[i]); u_gc_free(d->gh[i]); }
    for (int i = 0; i < d->ni; i++) { Obj b = u_gc_get(d->ih[i]); if (u_alive(b)) (ui_is(b, K_BEH) ? F(0x50ECEA0, void (*)(Obj, bool, void *))(b, true, NULL) : (void)0); u_gc_free(d->ih[i]); }
    d->n = 0; d->ng = 0; d->ni = 0;
}
/* A text line cloned from our status template into `parent`: anchored top centre, pivot (0.5, pivot_y), centred. */
static uintptr_t h_status, h_lora_font_ref;
static void tmp_plain(Obj t);
static Obj line_clone(Obj parent_tr, float pivot_y, float w, float h, float y) {
    Obj st = u_gc_get(h_status);
    if (!u_alive(st) || !parent_tr) return NULL;
    Obj b = u_instantiate(st, parent_tr);
    if (!u_alive(b)) return NULL;
    Obj rt = u_tr(b);
    (ui_is(rt, K_RT) ? F(0x5106B64, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL) : (void)0);                   /* anchorMin */
    (ui_is(rt, K_RT) ? F(0x5106D48, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL) : (void)0);                   /* anchorMax */
    (ui_is(rt, K_RT) ? F(0x51072F4, void (*)(float, float, Obj, void *))(0.5f, pivot_y, rt, NULL) : (void)0);             /* pivot */
    u_rt_set_size(rt, w, h); u_rt_set_pos(rt, 0, y);
    (ui_is(b, K_TMP) ? F(0x4F0B7BC, void (*)(Obj, int, void *))(b, 514, NULL) : (void)0);                                  /* TMP alignment Center */
    tmp_plain(b);
    u_go_active(u_go(b), true);
    return b;
}
/* owner 2026-10-13 (final): no black outline on our texts. Our lines are clones of the clock's time text, whose
   material has an outline; give every clone the plain material of the game's window body text: the stock Lora
   text material (quest title, kept by the panel) for Lora, else the font asset's own default material
   (TMP_Asset.m_Material +0x88). TMP_Text.fontSharedMaterial set = vtable slot 68 (+0x578). */
static uintptr_t h_plain_mat;                                /* stock Lora body text material (plist_style) */
static void tmp_plain(Obj t) {
    if (!ui_is(t, K_TMP)) return;
    Obj f = F(0x4F0A59C, Obj (*)(Obj, void *))(t, NULL);                                   /* TMP_Text.font */
    if (!u_alive(f)) return;
    /* the font asset's own default material (TMP_Asset.m_Material +0x88): plain, no outline / underlay. (The notice box
       body uses "Lora-Regular SDF OutLine16", an outline preset: not that one.) */
    Obj m = AT(f, 0x88, Obj);
    if (!u_alive(m)) return;
    static Obj logged;
    if (m != logged) { logged = m; char mn[96] = ""; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(m, NULL), mn, sizeof mn); rig_log("ui: plain text material in use: %s", mn); }
    void *k = *(void **)t;
    (*(void (**)(Obj, Obj, void *))((char *)k + 0x578))(t, m, *(void **)((char *)k + 0x580));
}
static void line_text(Obj t, const char *txt) {             /* set only when it changed */
    char cur[600]; if (!u_alive(t)) return;
    str_utf8(tmp_get(t), cur, sizeof cur);
    if (strcmp(cur, txt)) tmp_set(t, txt);
}

/* lend control / viewer (8g, 8h): used before their sections */
static int lmenu_open(Obj parts, Obj unit);
#define UI_ORDER_FOLLOW 30
#define UI_ORDER_CAMERA 31
#define UI_ORDER_ASK    32
static int follow_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n);
/* lan_v10 co-op markers (8j): used before their section */
static char *ui_coop_cmd(const char *line);
static char *ui_spect_cmd(const char *line);
static char *ui_mini_cmd(const char *line);
static int pmenu_open(int pl, float x, float y);
static int look_at(int pl);
static void scr_to_layer(Obj layer, float sx, float sy, float *ux, float *uy);
static int coop_follow;
static int camera_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n);
static int ask_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n);

/* For other lanes (ord_mp.c: "This item belongs to <name>"): the same toast. */
void ui_notice(const char *text) { notice("%s", text); }

/* 1. Title screen: owned by the transitions lane (src/mpmenu.c) since 2026-10-12. */

/* ================================================================ 2. unit pick + owner tags */
/* SubUI_UnitShortCutParts: _unitName DASText +0x138, _owner IEntity +0x228; PointerEventData.button +0x148 */
#define MAX_SC 64
static struct { uintptr_t parts, unit; } sc[MAX_SC];
static void (*o_sc_init)(Obj, Obj, Obj, Obj, void *);
static void (*o_sc_name)(Obj, Obj, void *);
static void (*o_sc_click)(Obj, Obj, void *);

static void sc_track(Obj parts, Obj unit) {
    int free_i = -1;
    for (int i = 0; i < MAX_SC; i++) {
        Obj p = u_gc_get(sc[i].parts);
        if (p == parts) { u_gc_free(sc[i].unit); sc[i].unit = u_gc_new(unit); return; }
        if (free_i < 0 && (!sc[i].parts || !u_alive(p))) free_i = i;
    }
    if (free_i < 0) return;
    u_gc_free(sc[free_i].parts); u_gc_free(sc[free_i].unit);
    sc[free_i].parts = u_gc_new(parts); sc[free_i].unit = u_gc_new(unit);
}
typedef struct { float r, g, b, a; } Col;
static inline void graphic_color(Obj g, Col c) { if (u_alive(g)) (ui_is(g, K_GR) ? F(0x52602F8, void (*)(Obj, Col, void *))(g, c, NULL) : (void)0); }   /* Graphic.set_color */
static Col hex_col(const char *h, float a) {               /* "#RRGGBB" */
    unsigned v = (unsigned)strtoul(h + 1, NULL, 16);
    Col c = { (v >> 16 & 255) / 255.0f, (v >> 8 & 255) / 255.0f, (v & 255) / 255.0f, a };
    return c;
}
/* demo-v2 item 4: units you do not control are dimmed; the card background takes the owner's colour */
/* The portrait is drawn with a shared HSV material (SetGrey / Image.color are unreliable: greying one card greyed
   all of them on the guest). Dim = our own overlay: a clone of the card's cooldown Image (_imgCooldown +0x208),
   full fill, black at 55 % alpha, kept by us (the stock never touches the clone). Owner colour: _topLineBar (+0x130). */
#define MAX_OV MAX_SC
static uintptr_t ov_parts[MAX_OV], ov_img[MAX_OV];
static Obj overlay_of(Obj parts, int create) {
    int free_i = -1;
    for (int i = 0; i < MAX_OV; i++) {
        Obj x = u_gc_get(ov_parts[i]);
        if (x == parts) { Obj o = u_gc_get(ov_img[i]); if (u_alive(o) || !create) return u_alive(o) ? o : NULL; free_i = i; break; }
        if (free_i < 0 && (!ov_parts[i] || !u_alive(x))) free_i = i;
    }
    if (!create || free_i < 0) return NULL;
    Obj cd = AT(parts, 0x208, Obj);
    if (!u_alive(cd)) return NULL;
    /* lan_v8: over the portrait picture only (_portraitImage +0x140), never over the stock bars and status frame */
    Obj pic = AT(parts, 0x140, Obj);
    Obj o = u_instantiate(cd, u_alive(pic) ? u_tr(pic) : u_tr_parent(u_tr(cd)));
    if (!u_alive(o)) return NULL;
    if (u_alive(pic) && ui_is(u_tr(o), K_RT)) {             /* stretch over the picture */
        Obj rt = u_tr(o);
        F(0x5106B64, void (*)(float, float, Obj, void *))(0, 0, rt, NULL);
        F(0x5106D48, void (*)(float, float, Obj, void *))(1, 1, rt, NULL);
        F(0x51072F4, void (*)(float, float, Obj, void *))(0.5f, 0.5f, rt, NULL);
        u_rt_set_size(rt, 0, 0); u_rt_set_pos(rt, 0, 0);
    } else u_set_sibling(u_tr(o), u_sibling(u_tr(cd)) + 1);
    (ui_is(o, K_IMG) ? F(0x5267504, void (*)(float, Obj, void *))(1.0f, o, NULL) : (void)0);                               /* Image.fillAmount = 1 */
    Col dark = { 0, 0, 0, 0.55f };
    graphic_color(o, dark);
    u_gc_free(ov_parts[free_i]); u_gc_free(ov_img[free_i]);
    ov_parts[free_i] = u_gc_new(parts); ov_img[free_i] = u_gc_new(o);
    return o;
}
static void portrait_look(Obj parts, int o) {
    Col white = { 1, 1, 1, 1 };
    int foreign = !may_order(o);
    Obj ov = overlay_of(parts, foreign);
    if (ov && u_go_active_self(u_go(ov)) != foreign) u_go_active(u_go(ov), foreign);
    /* lan_v8 (owner): the stock status line (_topLineBar: alive / battle / dead sprites) keeps its stock colour;
       the owner shows as a small chip on the card instead (chips_frame). Not touched at all. */
    (void)white;
}
static int grey_mark(Obj parts, int set) { (void)set; Obj ov = overlay_of(parts, 0); return ov && u_go_active_self(u_go(ov)); }
static void sc_look(Obj parts, Obj unit) { portrait_look(parts, eff_of(unit)); }   /* the colour bar shows the controller */
/* design 2026-10-13 #13: no "[Name]" text on the portrait (it shrank long names); the owner-coloured top bar is the cue */
static void sc_tag(Obj parts, Obj unit) {
    if (!on() || !u_alive(parts) || !unit) return;
    sc_look(parts, unit);
}
/* Teams lane (ui_teams.c): its screen clones the portrait prefab; those clones are not tracked here. */
/* looked up at run time (dlsym in our own image), so the dylib links with or without ui_teams.c */
static void (*ui_teams_frame)(void);
static char *(*ui_teams_exec)(const char *cmd, const char *line);
static int (*ui_teams_owns)(void *parts);
static void teams_lookup(void) {
    static int done;
    if (done) return;
    done = 1;
    ui_teams_frame = (void (*)(void))dlsym(RTLD_DEFAULT, "ui_teams_frame");
    ui_teams_exec = (char *(*)(const char *, const char *))dlsym(RTLD_DEFAULT, "ui_teams_exec");
    ui_teams_owns = (int (*)(void *))dlsym(RTLD_DEFAULT, "ui_teams_owns");
}
/* For the Teams screen: the same look as the unit bar (dimmed if not this player's, card in the owner's colour). */
void ui_portrait_style(void *parts, int owner) {
    if (!on() || !u_alive(parts)) return;
    portrait_look(parts, owner);
}
static void hk_sc_init(Obj parts, Obj unit, Obj on_select, Obj stream, void *mi) {
    o_sc_init(parts, unit, on_select, stream, mi);
    teams_lookup();
    if (ui_teams_owns && ui_teams_owns(parts)) return;
    sc_track(parts, unit);
    sc_tag(parts, unit);
}
static void hk_sc_name(Obj parts, Obj unit, void *mi) {      /* stock sets the plain name; add the tag after */
    o_sc_name(parts, unit, mi);
    sc_tag(parts, unit);
}
static void owned_counts(int cnt[UI_MAX_PLAYERS]) {          /* over the units on the shortcut bar */
    memset(cnt, 0, sizeof(int) * UI_MAX_PLAYERS);
    for (int i = 0; i < MAX_SC; i++) {
        Obj u = u_gc_get(sc[i].unit);
        if (!u || !u_alive(u_gc_get(sc[i].parts))) continue;
        int o = owner_of(u);
        if (o >= 0 && o < UI_MAX_PLAYERS) cnt[o]++;
    }
}
/* Right-click on a portrait = claim / release (host: give the unit to the next player). */
static void hk_sc_click(Obj parts, Obj ev, void *mi) {
    if (!on() || !ev || AT(ev, 0x148, int) != 1) { o_sc_click(parts, ev, mi); return; }
    o_sc_click(parts, ev, mi);                               /* stock: right-click only plays the button feedback */
    if (spect()) { notice("You are watching: spectators cannot take units"); return; }
    mp_session *s = sess();
    Obj unit = AT(parts, 0x228, Obj);
    if (!s || !is_unit(unit)) return;
    const uint8_t *g = (const uint8_t *)unit + UNIT_GUID;
    /* lend control: your own unit (or one lent to you) opens the lend menu instead (owner-approved board) */
    { dsnet_client *c = cli(); int ow = owner_of(unit), ct = c ? dsnet_lent_to(c, g) : -1;
      if ((ow == me() || ct == me()) && lmenu_open(parts, unit)) return; }
    int cnt[UI_MAX_PLAYERS]; owned_counts(cnt);
    ui_players p; players(&p);
    ui_act a = ui_portrait_action_admin(me(), owner_of(unit), cnt, p.present, admin_on());
    char un[64], pn[40]; unit_name(unit, un, sizeof un);
    switch (a.kind) {
    case UI_ACT_CLAIM: mp_claim(s, g); notice("You take %s", un); break;
    case UI_ACT_RELEASE: mp_release(s, g); notice("You release %s", un); break;
    case UI_ACT_ASSIGN:
        mp_assign(s, g, a.target < 0 ? DSNET_NO_PLAYER : a.target);
        if (a.target < 0) notice("%s is free", un); else notice("%s goes to %s", un, ui_player_label(&p, a.target, pn));
        break;
    case UI_ACT_DENY_OWNED:
        if (a.target == UI_RESERVED) { char rn[32]; reserved_name(g, rn, sizeof rn); notice("%s is reserved for %s", un, rn); }
        else notice("%s belongs to %s%s", un, ui_player_label(&p, a.target, pn), me() == 0 ? " (Admin mode lets the host take it)" : "");
        break;
    case UI_ACT_DENY_FULL: notice("You can control at most %d units (host rule)", ui_max_owned); break;
    default: break;
    }
}

/* ---- layout v2 helpers: stock sprites by name, plain Images, screen-point hit tests */
/* Sprites the mod reuses (layout v2 §0), found by name among all loaded Sprites (ResourcesAPIInternal.
   FindObjectsOfTypeAll 0x50E8010, Object.name 0x50FB198). One scan finds every name; repeated at most every 3 s
   while one is missing. */
enum { SP_FRAME1, SP_COG, SP_LIST_ON, SP_LIST_OFF, SP_ARROW_PREV, SP_ARROW_NEXT, SP_TOGGLE_OFF, SP_TOGGLE_ON,
       SP_TEXTBOX, SP_BOTTOMBUTTON, SP_CHECK, SP_N };
static const char *const sp_names[SP_N] = { "topLineButton_Frame1", "ITEM_CogWheel", "ListToggle_On", "ListToggle_Off",
    "ArrrowIcon_Previous", "ArrrowIcon_Next", "ToggleButton_Normal", "ToggleButton_Selected", "TextBox", "BottomButton", "CheckBox_Selected" };
static uintptr_t h_sprite[SP_N];
static Obj type_of(const char *ns, const char *name);
static double now_s(void);
static Obj sprite_get(int id) {
    Obj sp = u_gc_get(h_sprite[id]);
    if (u_alive(sp)) return sp;
    static double next;
    if (now_s() < next) return NULL;
    next = now_s() + 3;
    Obj st = type_of("UnityEngine", "Sprite");
    Obj arr = st ? F(0x50E8010, Obj (*)(Obj, void *))(st, NULL) : NULL;
    int n = arr ? AT(arr, 0x18, int32_t) : 0, found = 0;
    for (int i = 0; i < n; i++) {
        Obj o = AT(arr, 0x20 + 8 * i, Obj);
        if (!o) continue;
        char nm[64]; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(o, NULL), nm, sizeof nm);
        for (int k = 0; k < SP_N; k++)
            if (!u_alive(u_gc_get(h_sprite[k])) && !strcmp(nm, sp_names[k])) { u_gc_free(h_sprite[k]); h_sprite[k] = u_gc_new(o); found++; }
    }
    /* run 53: after a join-point reload in a dungeon the item icon ITEM_CogWheel is no loaded Sprite (the panel never
       built until the next map). Ask every loaded SpriteAtlas for the missing names (SpriteAtlas.GetSprite 0x5157130) */
    if (!u_alive(u_gc_get(h_sprite[id]))) {
        Obj at = type_of("UnityEngine.U2D", "SpriteAtlas");
        Obj aa = at ? F(0x50E8010, Obj (*)(Obj, void *))(at, NULL) : NULL;
        int na = aa ? AT(aa, 0x18, int32_t) : 0;
        for (int i = 0; i < na; i++) {
            Obj a = AT(aa, 0x20 + 8 * i, Obj);
            if (!u_alive(a)) continue;
            for (int k = 0; k < SP_N; k++) if (!u_alive(u_gc_get(h_sprite[k]))) {
                Obj o = F(0x5157130, Obj (*)(Obj, Obj, void *))(a, u_str(sp_names[k]), NULL);
                if (u_alive(o)) { u_gc_free(h_sprite[k]); h_sprite[k] = u_gc_new(o); found++; rig_log("ui: sprite %s from a sprite atlas", sp_names[k]); }
            }
        }
    }
    /* lead lan_v9: after a rejoin in a dungeon the game never loads the cog icon again on its own (run 57: the panel
       stayed without it until the next map). Load it like the game loads item icons: ResourceLoader.LoadItemIcon(key)
       0x829DF4 (= LoadIcon(prefix + key), the default icon when missing: checked by name) */
    if (!u_alive(u_gc_get(h_sprite[SP_COG]))) {
        static uintptr_t h_rl; Obj rl = find_view(&h_rl, "Refactor", "ResourceLoader");
        Obj o = rl ? F(0x829DF4, Obj (*)(Obj, Obj, void *))(rl, u_str(sp_names[SP_COG]), NULL) : NULL;   /* item key ITEM_CogWheel (ItemTable) */
        char nm[64] = ""; if (u_alive(o)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(o, NULL), nm, sizeof nm);
        if (u_alive(o) && !strcmp(nm, sp_names[SP_COG])) { u_gc_free(h_sprite[SP_COG]); h_sprite[SP_COG] = u_gc_new(o); found++; rig_log("ui: cog icon loaded through ResourceLoader.LoadItemIcon"); }
        else { static int said; if (!said++) rig_log("ui: ResourceLoader.LoadItemIcon(ITEM_CogWheel) gave %s (loader %p)", nm[0] ? nm : "nothing", rl); }
    }
    rig_log("ui: sprite scan: %d sprites, %d new names found (%s %s)", n, found, sp_names[id], u_alive(u_gc_get(h_sprite[id])) ? "found" : "MISSING");
    sp = u_gc_get(h_sprite[id]);
    return u_alive(sp) ? sp : NULL;
}
/* A plain Image under `parent`: a clone of a portrait card's cooldown Image (as the portrait dim overlay), made
   Simple, full, white, with `sprite` (NULL = a flat rectangle). Anchored at the canvas top centre, pivot top-left. */
static Obj img_make(Obj parent, Obj sprite, float w, float h) {
    Obj cd = NULL;
    for (int i = 0; i < MAX_SC && !u_alive(cd); i++) { Obj pa = u_gc_get(sc[i].parts); if (u_alive(pa)) cd = AT(pa, 0x208, Obj); }
    if (!u_alive(cd) || !parent) return NULL;
    Obj im = u_instantiate(cd, parent);
    if (!ui_is(im, K_IMG)) return NULL;
    F(0x5257600, void (*)(Obj, Obj, void *))(im, sprite, NULL);                             /* Image.sprite */
    F(0x52579CC, void (*)(Obj, int, void *))(im, 0, NULL);                                  /* Image.type = Simple */
    F(0x5267348, void (*)(Obj, bool, void *))(im, sprite != NULL, NULL);                    /* preserveAspect */
    F(0x5267504, void (*)(float, Obj, void *))(1.0f, im, NULL);                             /* fillAmount */
    Col white = { 1, 1, 1, 1 }; graphic_color(im, white);
    Obj rt = u_tr(im);
    F(0x5106B64, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL);
    F(0x5106D48, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL);
    F(0x51072F4, void (*)(float, float, Obj, void *))(0, 1, rt, NULL);
    u_rt_set_size(rt, w, h);
    u_go_active(u_go(im), true);
    return im;
}
static void img_sprite(Obj im, Obj sprite) { if (ui_is(im, K_IMG) && sprite) F(0x5257600, void (*)(Obj, Obj, void *))(im, sprite, NULL); }
/* the mouse is over this UI object (RectTransformUtility.RectangleContainsScreenPoint with its canvas camera) */
static int ui_hit(Obj g, float mx, float my) {
    if (!ui_is(g, K_GR) || !u_go_active_self(u_go(g)) || !F(0x50F3724, bool (*)(Obj, void *))(u_go(g), NULL)) return 0;
    Obj cv = F(0x5260544, Obj (*)(Obj, void *))(g, NULL);                                   /* Graphic.canvas */
    Obj cam = cv && F(0x5475018, int (*)(Obj, void *))(cv, NULL) != 0 ? F(0x5477610, Obj (*)(Obj, void *))(cv, NULL) : NULL;
    return F(0x5473D40, bool (*)(Obj, V2, Obj, void *))(u_tr(g), (V2){ mx, my }, cam, NULL);
}

/* World name above units: UnitEntityHUD._nameText +0x68 (DASText_GameObject), _entity +0x118 */
#define MAX_HUD 256
static struct { uintptr_t hud; uint8_t guid[16]; char base[64]; } hud[MAX_HUD];
static void (*o_hud_init)(Obj, Obj, void *);
static void hud_apply(int i) {
    Obj h = u_gc_get(hud[i].hud);
    if (!u_alive(h)) return;
    char tag[96], full[200]; ui_players p; players(&p);
    int o = owner_of_guid(hud[i].guid), e = eff_of_guid(hud[i].guid);
    if (o != UI_RESERVED && e >= 0) o = e;                   /* lan_v9 colour rule: a lent unit shows its CONTROLLER */
    settings_load();
    tag[0] = 0;                                              /* review #26: owner colour dot + name, no brackets */
    static const char *dot; static Obj dot_font;             /* a dot glyph the world font really has (TMP_FontAsset.HasCharacter) */
    Obj ft = (ui_is(AT(h, 0x68, Obj), K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(AT(h, 0x68, Obj), NULL) : NULL);                    /* TMP_Text.font */
    if (u_alive(ft) && ft != dot_font) {
        static const struct { uint16_t c; const char *s; } cand[] = { { 0x25CF, "\u25CF" }, { 0x2022, "\u2022" }, { 0x25A0, "\u25A0" }, { 0xB7, "\u00B7" } };
        dot = "";
        for (size_t k = 0; k < sizeof cand / sizeof cand[0] && !dot[0]; k++)
            if (F(0x4EEE580, bool (*)(Obj, uint16_t, bool, bool, void *))(ft, cand[k].c, true, false, NULL)) dot = cand[k].s;
        dot_font = ft;
        rig_log("ui: world name dot glyph: %s", dot[0] ? dot : "(none in the font)");
    }
    if (ui_name_tag_visible(uiset.names, o == UI_RESERVED ? 99 : o, me())) {
        if (o == UI_RESERVED) { char nm[32]; reserved_name(hud[i].guid, nm, sizeof nm); snprintf(tag, sizeof tag, " <color=#8a8088>%s%s%s, away</color>", dot ? dot : "", dot && dot[0] ? " " : "", nm); }
        else ui_owner_tag_world(&p, o, dot, tag, sizeof tag);
    }
    /* uxreview (one colour per player): the label's own colour (DASText_GameObject, about #CCBFB8) multiplies every
       <color> tag, so player colours rendered darker in the world. The label is set to white and the stock name keeps
       its colour through a tag. */
    static GCol base_col = { -1, 0, 0, 0 };
    Obj nt = AT(h, 0x68, Obj);
    if (ui_is(nt, K_GR)) {
        GCol cur = g_color(nt);
        if (cur.r < 0.999f || cur.g < 0.999f || cur.b < 0.999f) { if (base_col.r < 0) base_col = cur; g_set_color(nt, (GCol){ 1, 1, 1, cur.a }); }
    }
    if (base_col.r >= 0 && tag[0])
        snprintf(full, sizeof full, "<color=#%02X%02X%02X>%s</color>%s", (int)(base_col.r * 255 + 0.5f), (int)(base_col.g * 255 + 0.5f), (int)(base_col.b * 255 + 0.5f), hud[i].base, tag);
    else if (base_col.r >= 0)
        snprintf(full, sizeof full, "<color=#%02X%02X%02X>%s</color>", (int)(base_col.r * 255 + 0.5f), (int)(base_col.g * 255 + 0.5f), (int)(base_col.b * 255 + 0.5f), hud[i].base);
    else snprintf(full, sizeof full, "%s%s", hud[i].base, tag);
    tmp_set(AT(h, 0x68, Obj), full);
    static int probed;                                       /* uxreview: why world names render darker (probe once) */
    Obj t = AT(h, 0x68, Obj);
    if (!probed && ui_is(t, K_TMP)) {
        probed = 1;
        GCol gc = g_color(t); Obj m = F(0x4F0A6B4, Obj (*)(Obj, void *))(t, NULL);
        Obj sh = m ? F(0x50A8F80, Obj (*)(Obj, void *))(m, NULL) : NULL;
        GCol fc = m ? F(0x50B2B8C, GCol (*)(Obj, Obj, void *))(m, u_str("_FaceColor"), NULL) : (GCol){ -1, -1, -1, -1 };
        char mn[96] = "", sn[96] = "";
        if (m) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(m, NULL), mn, sizeof mn);
        if (sh) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(sh, NULL), sn, sizeof sn);
        rig_log("ui: world name probe: %s color %.2f %.2f %.2f %.2f, material %s, shader %s, _FaceColor %.2f %.2f %.2f %.2f",
                cls_name(t), gc.r, gc.g, gc.b, gc.a, mn, sn, fc.r, fc.g, fc.b, fc.a);
    }
}
static void hud_track(Obj h) {
    Obj e = AT(h, 0x118, Obj);
    if (!is_unit(e)) return;
    int slot = -1;
    for (int i = 0; i < MAX_HUD; i++) {
        Obj x = u_gc_get(hud[i].hud);
        if (x == h) { slot = i; break; }
        if (slot < 0 && (!hud[i].hud || !u_alive(x))) slot = i;
    }
    if (slot < 0) return;
    if (u_gc_get(hud[slot].hud) != h) { u_gc_free(hud[slot].hud); hud[slot].hud = u_gc_new(h); }
    memcpy(hud[slot].guid, (const uint8_t *)e + UNIT_GUID, 16);
    str_utf8(tmp_get(AT(h, 0x68, Obj)), hud[slot].base, sizeof hud[slot].base);   /* stock just set the plain name */
    if (on()) hud_apply(slot);
}
static void hk_hud_init(Obj h, Obj entity, void *mi) { o_hud_init(h, entity, mi); hud_track(h); }

/* re-tag everything when the ownership table changes */
static uint64_t own_sig;
unsigned mp_game_session_gen(void);
static void tags_frame(void) {
    if (!on()) return;
    { dsnet_client *c = cli(); static uint32_t refused = 0, seen_me = 99;   /* host rule "Units per player" */
      int u = c ? dsnet_units_per_player(c) : 0; if (u > 0) ui_max_owned = u;
      uint32_t r = c ? dsnet_claim_refusals(c, me()) : 0;
      if ((uint32_t)me() != seen_me) { seen_me = (uint32_t)me(); refused = r; }
      if (r > refused) notice("You can control at most %d units (host rule)", ui_max_owned);
      refused = r;
      /* ux_flow F12 (rule / Admin changes said on every peer): mpmenu.c does it (menu lane), not here */ }
    uint64_t sig = 1469598103934665603ull;
    ui_players p; players(&p);
    sig = fnv(sig, &p.present, sizeof p.present);
    for (int i = 0; i < MAX_SC; i++) {
        Obj u = u_gc_get(sc[i].unit);
        if (!u || !u_alive(u_gc_get(sc[i].parts))) continue;
        int o = owner_of(u);
        int k2 = eff_of(u);                                  /* a lend / take back changes the look too */
        sig = fnv(sig, (const uint8_t *)u + UNIT_GUID, 16); sig = fnv(sig, &o, sizeof o); sig = fnv(sig, &k2, sizeof k2);
    }
    static unsigned looks;
    if (++looks % 5 == 0)                                    /* stock refreshes (selection, status) reset the colours */
        for (int i = 0; i < MAX_SC; i++) {
            Obj pa = u_gc_get(sc[i].parts), u = u_gc_get(sc[i].unit);
            if (u_alive(pa) && u) sc_look(pa, u);
        }
    if (sig == own_sig) return;
    own_sig = sig;
    for (int i = 0; i < MAX_SC; i++) {
        Obj pa = u_gc_get(sc[i].parts), u = u_gc_get(sc[i].unit);
        if (u_alive(pa) && u) hk_sc_name(pa, u, NULL);       /* stock RefreshUnitName + tag */
    }
    for (int i = 0; i < MAX_HUD; i++) if (hud[i].hud) hud_apply(i);
}

/* why this player may not order the unit (lan_v9 wording): "Not your unit (owned by X)", "Lent to X: take it back to
   control it", reserved, free */
static void not_mine_text(const uint8_t g[16], char *out, size_t cap) {
    dsnet_client *c = cli(); int o = owner_of_guid(g), lent = c ? dsnet_lent_to(c, g) : -1;
    ui_players p; players(&p); char pn[40];
    if (o == me() && lent >= 0) snprintf(out, cap, "Lent to %s: take it back to control it", ui_player_label(&p, lent, pn));
    else if (o == UI_RESERVED) { char rn[32]; reserved_name(g, rn, sizeof rn); snprintf(out, cap, "Not your unit (%s)", rn); }
    else if (o >= 0) snprintf(out, cap, "Not your unit (owned by %s)", ui_player_label(&p, o, pn));
    else snprintf(out, cap, "Not your unit (free: only the host orders it)");
}
int trn_flow_busy(void) __attribute__((weak_import));
/* lan_v9 item 4: during an agreed map change (return to base, next floor), the return screen or the transition flow,
   orders wait for the new map: the player is told why. ord_game.c also asks it for a right-click on the map
   (InteractGridRequested), which the game itself ignores in that state without any order (run 47). 1 = refused. */
int ui_map_change_blocks(void) {
    if (!on()) { map_changing = 0; return 0; }               /* no session / title: nothing is changing */
    if (map_changing > 0 && now_s() - map_changing > 90) map_changing = 0;
    if (!(map_changing > 0 || result_open || (trn_flow_busy && trn_flow_busy()))) return 0;
    notice("Changing map: orders wait for the new map");
    rig_log("ui: order refused locally: map change running (agreed %d, return screen %d, flow %d)", map_changing > 0, result_open, trn_flow_busy ? trn_flow_busy() : -1);
    return 1;
}
int ui_check_order(const uint8_t g[16]) {
    static const uint8_t zero[16];
    if (on() && spect()) { notice("You are watching: spectators cannot give orders"); return 0; }   /* base work too */
    if (!on() || !memcmp(g, zero, 16)) return 1;
    /* runs 26 / 35: during an agreed map change (return to base, next floor) no unit moves, the host's own included;
       the order used to go out and do nothing. Now the player is told why. */
    if (ui_map_change_blocks()) return 0;
    int o = owner_of_guid(g);
    dsnet_client *c = cli();
    if (c ? dsnet_may_order(c, me(), g) : may_order(o)) return 1;   /* own, free->host, reserved->host admin/temp, admin */
    Guid gg; memcpy(&gg, g, 16);
    Obj u = find_unit(cur_map(), gg);
    char un[64], why[96];
    unit_name(u, un, sizeof un); not_mine_text(g, why, sizeof why);
    if (o < 0 && o != UI_RESERVED) notice("%s: %s. Right-click its portrait to take it.", un, why);
    else notice("%s: %s", un, why);
    return 0;
}

/* ================================================================ 3. speed / pause votes + 5. status line */
/* SubUI_CampaignClockUI: _timeText TMP +0x40, _pauseButton +0x48, _normal +0x50, _fast +0x58, _veryFast +0x60 */
static uintptr_t h_clock;
static void (*o_clock_init)(Obj, Obj, void *);
static void clock_lobby(Obj clock) {
    if (!on() || !u_alive(clock)) return;
    dsnet_client *c = cli();
    int sp = dsnet_allow_speedup(c), pa = dsnet_allow_pause(c);
    if (spect()) { sp = pa = 0; u_active(AT(clock, 0x50, Obj), false); }   /* spectators: no speed or pause buttons at all */
    u_active(AT(clock, 0x58, Obj), sp);
    u_active(AT(clock, 0x60, Obj), sp);
    u_active(AT(clock, 0x48, Obj), pa);
}
static void hk_clock_init(Obj clock, Obj view, void *mi) {
    o_clock_init(clock, view, mi);
    u_gc_free(h_clock); h_clock = u_gc_new(clock);
    Obj t = AT(clock, 0x40, Obj);                            /* clone the clock's time text for our line */
    if (u_alive(t)) {
        Obj s = u_instantiate(t, u_tr_parent(u_tr(t)));
        if (u_alive(s)) {
            V2 pos = u_rt_pos(u_tr(t)), sz = u_rt_size(u_tr(t));
            u_rt_set_pos(u_tr(s), pos.x, pos.y - sz.y - 4.0f);
            tmp_set(s, "");
            u_gc_free(h_status); h_status = u_gc_new(s);
        }
    }
    clock_lobby(clock);
}

static ui_net_state net_state(void) {
    mp_session *s = sess();
    if (!s) return UI_NET_OFF;
    if (me() < 0) return UI_NET_CONNECTING;
    mp_stats st; mp_get_stats(s, &st);
    if (st.desynced) return UI_NET_DESYNC;
    if (st.loading) return UI_NET_LOADING;
    if (st.needs_state) return st.loads ? UI_NET_DESYNC : UI_NET_WAITING;
    return UI_NET_PLAYING;
}
/* review #10: a resync reloads every peer from the host's save; say why on every peer, before the reload */
static void resync_frame(const ui_players *p) {
    static uint32_t was;                                     /* players seen out of sync (bit per player) */
    dsnet_client *c = cli();
    if (!c) { was = 0; return; }
    mp_stats ms; mp_get_stats(sess(), &ms);
    uint32_t now = 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) {
        if (!(p->present >> i & 1u)) continue;
        int ping = -1, behind = 0, ns = 0;
        if (dsnet_player_net(c, i, &ping, &behind, &ns) == 0 && ns == DSNET_NET_RESYNCING) now |= 1u << i;
    }
    if (ms.desynced && me() >= 0) now |= 1u << me();
    uint32_t fresh = now & ~was;
    was = now;
    for (int i = 0; fresh && i < UI_MAX_PLAYERS; i++)
        if (fresh >> i & 1u) { char tx[256]; ui_resync_text(p, i, me(), tx, sizeof tx); notice("%s", tx); break; }
}
static char status_text[256], pause_text[256];             /* pause_text: the one pause line (panel + banner, #20) */
static ui_mapvote mapv;
static void resync_frame(const ui_players *p);
static void status_frame(void) {
    clock_lobby(u_gc_get(h_clock));                          /* stock SetSpeedChangeOnUI may re-enable them */
    Obj t = u_gc_get(h_status);
    if (!u_alive(t)) return;
    char line[256] = "";
    pause_text[0] = 0;
    ui_net_state ns = net_state();
    if (ns != UI_NET_OFF) {
        dsnet_client *c = cli();
        ui_players p; players(&p);
        ui_votes v; memset(&v, 0, sizeof v);
        mp_stats st; mp_get_stats(sess(), &st);
        v.present = p.present;
        v.allow_speedup = c ? dsnet_allow_speedup(c) : 1; v.allow_pause = c ? dsnet_allow_pause(c) : 1;
        v.busy = c && dsnet_busy_mask ? dsnet_busy_mask(c) : 0;
        v.story_pause = c && dsnet_story_pause_mask ? dsnet_story_pause_mask(c) : (uint32_t)story_held << (me() & 31);
        v.pause_by1 = c ? dsnet_pause_by(c) + 1 : 0;
        v.paused_ext = st.paused;
        if (c && dsnet_vote_of)
            for (int i = 0; i < UI_MAX_PLAYERS; i++) {
                int sp = 1, pa = 0;
                if (v.present >> i & 1u && dsnet_vote_of(c, i, &sp, &pa) == 0) { v.speed[i] = (uint8_t)sp; v.pause[i] = (uint8_t)pa; }
            }
        ui_status_line(ns, &v, &p, me(), line, sizeof line);
        if (st.paused || ui_is_paused(&v)) ui_pause_text(&v, &p, pause_text, sizeof pause_text);
        resync_frame(&p);
    }
    /* holds (a player left, the host chose "Wait"): "Waiting for <name>" */
    dsnet_client *hc = cli();
    /* holds say more than "Paused" or "Waiting for <host> (loading)": a player left ("Waiting for <name> (disconnected)"),
       the host is in its lobby (DSNET_HOLD_LOBBY: "Waiting for <host> to start") */
    if (hc && net_state() == UI_NET_PLAYING) {
        int nh = dsnet_hold_count(hc);
        for (int i = 0; i < nh; i++) {
            int reason = 0; uint8_t uid[16]; char nm[32] = "";
            if (dsnet_hold_get(hc, i, &reason, uid, nm) != 0) continue;
            if (reason == DSNET_HOLD_PLAYER_LEFT) {
                /* ux_flow F10: how long, and who decides (the host's Play = Keep playing, mp_core) */
                static uint8_t seen_uid[8][16]; static double seen_t[8]; static int nseen;
                int k = -1; for (int j = 0; j < nseen && k < 0; j++) if (!memcmp(seen_uid[j], uid, 16)) k = j;
                if (k < 0) { k = nseen < 8 ? nseen++ : 7; memcpy(seen_uid[k], uid, 16); seen_t[k] = now_s(); }
                int w = (int)(now_s() - seen_t[k]); ui_players p; players(&p); char hn[40];
                if (me() == 0) snprintf(line, sizeof line, "Waiting for %s \u00B7 %d:%02d \u00B7 press Play to keep playing with their units", nm[0] ? nm : "a player", w / 60, w % 60);
                else snprintf(line, sizeof line, "Waiting for %s (disconnected) \u00B7 %d:%02d \u00B7 %s decides", nm[0] ? nm : "a player", w / 60, w % 60, ui_player_label(&p, 0, hn));
                for (int j = 0; j < nseen; j++) if (j != k && now_s() - seen_t[j] > 3600) { seen_t[j] = seen_t[nseen - 1]; memcpy(seen_uid[j], seen_uid[nseen - 1], 16); nseen--; }
            }
            else if (reason == DSNET_HOLD_LOBBY) {
                ui_players p; players(&p); char hn[40];
                snprintf(line, sizeof line, "Waiting for %s to start", ui_player_label(&p, 0, hn));
            } else continue;
            snprintf(pause_text, sizeof pause_text, "%s", line);   /* the banner says the same */
            break;
        }
    }
    snprintf(status_text, sizeof status_text, "%s", line);
    if (u_go_active_self(u_go(t))) u_go_active(u_go(t), false);   /* the template stays hidden: text shows in the panel */
}

/* ================================================================ 6. map-change vote */
/* CommonNoticeView (StaticUI 31): _noticeText +0x30, _onComplete +0x78 (closes it), _onConfirm +0x80,
   _onCancle +0x88. Show_Select(text, onConfirm, onCancel, yesNo) uses the text as is unless it starts
   with "TEXTKEY". Our prompt passes null actions; the confirm/cancel hooks turn the click into a vote. */
static uintptr_t h_notice;
static uint32_t prompt_id, map_counter;
static int map_submit, prompt_close, map_report = -1;
static double map_open_t; static uint32_t map_cancel_sent;  /* proposer: when its vote opened (60 s timeout, review #5) */
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
/* floor index (DungeonData +0x14 of EntityDataContainer +0x40; 0 = 1F, seen in a run), -1 outside a dungeon */
static int cur_floor(void) {
    Obj c = cdc(), dd = c && cur_map() > 0 ? AT(c, 0x40, Obj) : NULL;
    return dd ? AT(dd, 0x14, int32_t) : -1;
}
static const char *map_kind_now(int kind, char *out, size_t cap) {   /* "go down to floor 3" */
    int f = cur_floor();                                     /* 0-based: the next floor shows as f + 2 ("floor 2" from 1F) */
    return ui_map_kind_text_floor(kind, f >= 0 ? f + 2 : 0, out, cap);
}
static uint8_t map_order[UI_MAP_MAX_ORDER]; static size_t map_order_len;
static void (*o_cn_confirm)(Obj, void *);
static void (*o_cn_cancel)(Obj, void *);
static void map_vote_send(int accept) {
    mp_session *s = sess(); uint8_t a[5]; static const uint8_t zero[16];
    if (!s || !prompt_id) return;
    ui_map_vote_write(a, prompt_id, accept);
    prompt_id = 0;
    if (mp_order(s, UI_ORDER_MAP_VOTE, zero, a, sizeof a)) notice("Your answer could not be sent");
}
/* rig test command ui_map_vote: answer the open map-change prompt (batchmode has nobody to click it). 1 = sent */
int ui_debug_map_vote(int accept) { if (!prompt_id) return 0; map_vote_send(accept); return 1; }
/* "<name> disconnected" (host only, mpglue: mp_game_left_players / mp_game_left_choice): the same Yes/No box,
   buttons relabelled "Keep playing" (confirm) / "Wait for <name>" (cancel). */
static int left_prompt, left_shown_id = -1, prompt_pending;   /* prompt_pending: a map vote waits for the disconnect box (F20) */
static void box_restore(Obj v);
static void left_choice(int keep) {
    box_restore(find_view(&h_notice, "Refactor.UI", "CommonNoticeView"));
    static void (*fn)(int);
    if (!fn) fn = (void (*)(int))dlsym(RTLD_DEFAULT, "mp_game_left_choice");
    left_prompt = 0;
    if (fn) fn(keep);
    rig_log("ui: host chose %s", keep ? "keep playing" : "wait");
}
/* For mpmenu (transitions lane, dlsym): 1 while one of our Yes/No boxes (map vote, disconnect) is up. */
int ui_modal_open(void) { return left_prompt || prompt_id != 0; }
/* layout v2 §5a: the disconnect box's primary (right, confirm) button is "Wait for <name>", the secondary (left,
   cancel) "Keep playing" */
static int ext_notice(int confirm) {                         /* mpmenu's own Yes/No box (host Leave confirm, ux_flow F7) */
    static int (*fn)(int); static int looked;
    if (!looked) { looked = 1; fn = (int (*)(int))dlsym(RTLD_DEFAULT, "mpmenu_notice_click"); }
    return fn ? fn(confirm) : 0;
}
static void hk_cn_confirm(Obj v, void *mi) { if (prompt_id) map_vote_send(1); else if (left_prompt) left_choice(0); else ext_notice(1); o_cn_confirm(v, mi); }
static void hk_cn_cancel(Obj v, void *mi) { if (prompt_id) map_vote_send(0); else if (left_prompt) left_choice(1); else ext_notice(0); o_cn_cancel(v, mi); }
/* owner 2026-10-13: both buttons in the same font size (auto-size off, buttons widened), message larger + centred */
/* the stock box is shared with the game's own confirmations: remember its look and restore it after ours */
static struct { int saved; float fs[2]; bool autos[2]; V2 bsz[2]; int align; } box_orig;
static void box_restore(Obj v) {
    if (!box_orig.saved || !u_alive(v)) return;
    Obj ts[2] = { AT(v, 0x40, Obj), AT(v, 0x50, Obj) }, bs[2] = { AT(v, 0x38, Obj), AT(v, 0x48, Obj) };
    for (int i = 0; i < 2; i++) {
        if (u_alive(ts[i])) { (ui_is(ts[i], K_TMP) ? F(0x4F0B340, void (*)(Obj, float, void *))(ts[i], box_orig.fs[i], NULL) : (void)0); (ui_is(ts[i], K_TMP) ? F(0x4F0B5DC, void (*)(Obj, bool, void *))(ts[i], box_orig.autos[i], NULL) : (void)0); }
        if (u_alive(bs[i])) u_rt_set_size(u_tr(bs[i]), box_orig.bsz[i].x, box_orig.bsz[i].y);
    }
    if (u_alive(AT(v, 0x30, Obj))) (ui_is(AT(v, 0x30, Obj), K_TMP) ? F(0x4F0B7BC, void (*)(Obj, int, void *))(AT(v, 0x30, Obj), box_orig.align, NULL) : (void)0);
    box_orig.saved = 0;
}
static void box_style(Obj v, const char *msg) {
    Obj tc = AT(v, 0x40, Obj), tx = AT(v, 0x50, Obj), bc = AT(v, 0x38, Obj), bx = AT(v, 0x48, Obj), nt = AT(v, 0x30, Obj);
    (void)bc; (void)bx;
    if (!box_orig.saved && u_alive(tc) && u_alive(tx) && u_alive(bc) && u_alive(bx) && u_alive(nt)) {
        Obj ts[2] = { tc, tx }, bs[2] = { bc, bx };
        for (int i = 0; i < 2; i++) {
            box_orig.fs[i] = (ui_is(ts[i], K_TMP) ? F(0x4F0B338, float (*)(Obj, void *))(ts[i], NULL) : 0.0f);
            box_orig.autos[i] = (ui_is(ts[i], K_TMP) ? F(0x4F0B5D4, bool (*)(Obj, void *))(ts[i], NULL) : false);           /* enableAutoSizing */
            box_orig.bsz[i] = u_rt_size(u_tr(bs[i]));
        }
        box_orig.align = (ui_is(nt, K_TMP) ? F(0x4F0B7AC, int (*)(Obj, void *))(nt, NULL) : 0);                       /* alignment */
        box_orig.saved = 1;
    }
    if (u_alive(tc) && u_alive(tx)) {
        float fs = (ui_is(tc, K_TMP) ? F(0x4F0B338, float (*)(Obj, void *))(tc, NULL) : 0.0f);                         /* TMP fontSize of "Keep playing" */
        Obj ts[2] = { tc, tx };
        for (int i = 0; i < 2; i++) {
            (ui_is(ts[i], K_TMP) ? F(0x4F0B5DC, void (*)(Obj, bool, void *))(ts[i], false, NULL) : (void)0);                   /* enableAutoSizing = false */
            if (fs > 1) (ui_is(ts[i], K_TMP) ? F(0x4F0B340, void (*)(Obj, float, void *))(ts[i], fs, NULL) : (void)0);        /* fontSize */
        }
    }
    if (u_alive(nt)) {
        (ui_is(nt, K_TMP) ? F(0x4F0B7BC, void (*)(Obj, int, void *))(nt, 514, NULL) : (void)0);                            /* Center */
        tmp_set(nt, msg);
    }
}
/* layout v2 §5a body: title + red X, what happens, a waiting clock, what Keep playing means */
static char left_name[32]; static double left_t0;
static void left_body(char *m, size_t cap) {
    int s = (int)(now_s() - left_t0); if (s < 0) s = 0;
    snprintf(m, cap, "<size=150%%>%s disconnected</size>  <color=#f16071>X</color>\nThe game is paused for everyone.\n"
             "<size=88%%><color=#a69085>Waiting %d:%02d\nKeep playing: you control %s's units until %s comes back.</color></size>",
             left_name, s / 60, s % 60, left_name, left_name);
}
static void left_frame(void) {
    static int (*fn)(int *, char (*)[32], int);
    if (!fn) fn = (int (*)(int *, char (*)[32], int))dlsym(RTLD_DEFAULT, "mp_game_left_players");
    if (!fn || me() != 0) return;
    int ids[8]; char names[8][32];
    int n = fn(ids, names, 8);
    if (n <= 0) {
        left_shown_id = -1;
        if (left_prompt) {                                   /* the box closes by itself; "Ben is back" if he is */
            left_prompt = 0;
            Obj v = find_view(&h_notice, "Refactor.UI", "CommonNoticeView"); if (u_alive(v)) { box_restore(v); o_cn_cancel(v, NULL); }
            ui_players p; players(&p);
            for (int i = 1; i < UI_MAX_PLAYERS; i++) if ((p.present >> i & 1u) && !strcmp(p.name[i], left_name)) { notice("%s is back", left_name); break; }
        }
        return;
    }
    if (left_prompt) {                                       /* the waiting clock, once a second */
        static int last_s = -1; int s = (int)(now_s() - left_t0);
        Obj v = u_gc_get(h_notice);
        if (s != last_s && u_alive(v)) { last_s = s; char m[400]; left_body(m, sizeof m); tmp_set(AT(v, 0x30, Obj), m); }
        return;
    }
    if (prompt_id || ids[0] == left_shown_id) return;
    u_gc_free(h_notice); h_notice = 0;
    Obj v = find_view(&h_notice, "Refactor.UI", "CommonNoticeView");
    if (!v) { notice("%s disconnected", names[0]); left_shown_id = ids[0]; return; }
    char txt[128], wait[64], body[400];
    snprintf(txt, sizeof txt, "%s disconnected", names[0]);
    snprintf(left_name, sizeof left_name, "%s", names[0]); left_t0 = now_s();
    snprintf(wait, sizeof wait, "Wait for %s", names[0]);
    F(0xF652A8, void (*)(Obj, Obj, Obj, Obj, bool, void *))(v, u_str(txt), NULL, NULL, false, NULL);   /* Show_Select */
    tmp_set(AT(v, 0x40, Obj), wait);                          /* _txtConfirm: primary, right */
    tmp_set(AT(v, 0x50, Obj), "Keep playing");                /* _txtCancle: secondary, left */
    left_body(body, sizeof body);
    box_style(v, body);
    u_go_active(u_go(v), true);
    left_prompt = 1; left_shown_id = ids[0];
    rig_log("ui: host prompt: %s", txt);
}
/* layout v2 §5b, voter: the stock Yes / No box with the question as title, who proposed, who voted and the time left
   (updated once a second by map_frame) */
static double prompt_t0;
static const char *ok_glyph(Obj tmp) {                       /* a check mark the text's font has, else "yes" */
    Obj f = ui_is(tmp, K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(tmp, NULL) : NULL;
    return u_alive(f) && F(0x4EEE580, bool (*)(Obj, uint16_t, bool, bool, void *))(f, 0x2713, true, false, NULL) ? "\u2713" : "yes";
}
static int next_floor_no(void) { int f = cur_floor(); return f >= 0 ? f + 2 : 0; }   /* 0-based index: 1F = 0 */
static void prompt_text(Obj v, char *txt, size_t cap) {
    char q[64], nm[96], marks[256]; ui_players p; players(&p);
    int left = UI_MAP_TIMEOUT_S - (int)(now_s() - prompt_t0); if (left < 0) left = 0;
    ui_mapvote_marks(&mapv, &p, p.present, ok_glyph(AT(v, 0x30, Obj)), marks, sizeof marks);
    snprintf(txt, cap, "<size=150%%>%s</size>\n%s wants to %s.\n<size=88%%><color=#a69085>%s      %d s left</color></size>",
             ui_map_question(mapv.kind, next_floor_no(), q, sizeof q), pname(&p, mapv.proposer, nm, sizeof nm), ui_map_short(mapv.kind), marks, left);
}
static void prompt_show(void) {
    Obj v = find_view(&h_notice, "Refactor.UI", "CommonNoticeView");
    char txt[512];
    prompt_t0 = now_s();
    if (!u_alive(v)) { notice("A map change vote (no prompt window: counted as yes)"); prompt_id = mapv.id; map_vote_send(1); return; }
    prompt_text(v, txt, sizeof txt);
    prompt_id = mapv.id;
    rig_log("ui: map vote prompt: %s", txt);
    F(0xF652A8, void (*)(Obj, Obj, Obj, Obj, bool, void *))(v, u_str(txt), NULL, NULL, true, NULL);   /* Yes / No */
    Obj nt = AT(v, 0x30, Obj);
    (ui_is(nt, K_TMP) ? F(0x4F0B7BC, void (*)(Obj, int, void *))(nt, 514, NULL) : (void)0);          /* Center */
    u_go_active(u_go(v), true);
}
/* layout v2 §5b, proposer: a strip at the top centre under the pause banner (dark plate, Lora 16 cream), "Go down to
   floor 3? Waiting for Ben (1 of 2 agree) · 52 s" and a stock BottomButton "Cancel" (120x30) */
static uintptr_t h_strip_bg, h_strip_txt, h_strip_btn, h_strip_btxt;
static char strip_shown[300];
static void strip_frame(void) {
    ui_players p; players(&p);
    char wl[200], line[300], q[64];
    int open_ = on() && ui_mapvote_wait_line(&mapv, &p, me(), p.present, wl, sizeof wl);
    Obj bg = u_gc_get(h_strip_bg), tx = u_gc_get(h_strip_txt), bt = u_gc_get(h_strip_btn), btx = u_gc_get(h_strip_btxt);
    if (open_ && !u_alive(tx)) {
        Obj st = u_gc_get(h_status);
        Obj cv = ui_is(st, K_GR) ? F(0x5260544, Obj (*)(Obj, void *))(st, NULL) : NULL;
        if (cv) cv = F(0x5476FAC, Obj (*)(Obj, void *))(cv, NULL);                           /* Canvas.rootCanvas */
        Obj root = cv ? u_tr(cv) : NULL;
        if (!root) return;
        bg = img_make(root, NULL, 760, 40);
        if (bg) { Col d = { 0.13f, 0.125f, 0.145f, 0.88f }; graphic_color(bg, d); }        /* #222025-ish, as the windows */
        tx = u_instantiate(st, root);
        bt = img_make(root, sprite_get(SP_BOTTOMBUTTON), 120, 30);
        btx = u_instantiate(st, root);
        Obj all[2] = { tx, btx };
        for (int i = 0; i < 2; i++) {
            Obj t = all[i]; if (!ui_is(t, K_TMP)) continue;
            Obj rt = u_tr(t);
            F(0x5106B64, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL);
            F(0x5106D48, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL);
            F(0x51072F4, void (*)(float, float, Obj, void *))(0, 1, rt, NULL);
            F(0x4F0B7BC, void (*)(Obj, int, void *))(t, i ? 514 : 513, NULL);                 /* Center / Left */
            F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, false, NULL);
            F(0x4F0B340, void (*)(Obj, float, void *))(t, 16, NULL);
            tmp_plain(t);
            u_go_active(u_go(t), true);
        }
        if (ui_is(btx, K_TMP)) tmp_set(btx, "<color=#ccbeb8>Cancel</color>");
        u_gc_free(h_strip_bg); h_strip_bg = u_gc_new(bg); u_gc_free(h_strip_txt); h_strip_txt = u_gc_new(tx);
        u_gc_free(h_strip_btn); h_strip_btn = u_gc_new(bt); u_gc_free(h_strip_btxt); h_strip_btxt = u_gc_new(btx);
    }
    Obj parts[4] = { bg, tx, bt, btx };
    for (int i = 0; i < 4; i++) if (u_alive(parts[i]) && u_go_active_self(u_go(parts[i])) != open_) u_go_active(u_go(parts[i]), open_);
    if (!open_) { strip_shown[0] = 0; return; }
    int left = UI_MAP_TIMEOUT_S - (int)(now_s() - map_open_t); if (left < 0) left = 0;
    snprintf(line, sizeof line, "<color=#ccbeb8>%s %s \u00B7 %d s</color>", ui_map_question(mapv.kind, next_floor_no(), q, sizeof q), wl, left);
    if (strcmp(line, strip_shown)) { snprintf(strip_shown, sizeof strip_shown, "%s", line); tmp_set(tx, line); }
    float y = -150, x0 = -380;                               /* under the pause banner, top centre */
    if (u_alive(bg)) u_rt_set_pos(u_tr(bg), x0, y);
    if (u_alive(tx)) { u_rt_set_pos(u_tr(tx), x0 + 16, y); u_rt_set_size(u_tr(tx), 600, 40); }
    if (u_alive(bt)) u_rt_set_pos(u_tr(bt), x0 + 760 - 130, y - 5);
    if (u_alive(btx)) { u_rt_set_pos(u_tr(btx), x0 + 760 - 130, y - 5); u_rt_set_size(u_tr(btx), 120, 30); }
}
static void map_cancel_send(int why) {                     /* proposer: UI_MAP_CANCEL (timeout) or UI_MAP_CANCEL_USER */
    uint8_t a[5]; static const uint8_t zero[16];
    if (!ui_mapvote_open(&mapv) || mapv.proposer != me() || map_cancel_sent == mapv.id) return;
    map_cancel_sent = mapv.id;
    ui_map_vote_write(a, mapv.id, why);
    int rc = mp_order(sess(), UI_ORDER_MAP_VOTE, zero, a, sizeof a);
    rig_log("ui: map vote %08x: cancel sent (%s, rc %d)", mapv.id, why == UI_MAP_CANCEL ? "no decision in 60 s" : "Cancel pressed", rc);
}
static void strip_mouse(void) {
    Obj bt = u_gc_get(h_strip_btn);
    if (!u_alive(bt) || !u_go_active_self(u_go(bt)) || !F(0x51ACDD0, bool (*)(int, void *))(0, NULL)) return;
    struct { float x, y, z; } m3 = F(0x51AD4DC, __typeof__(m3) (*)(void *))(NULL);
    if (ui_hit(bt, m3.x, m3.y)) map_cancel_send(UI_MAP_CANCEL_USER);
}
/* test: press the strip's Cancel like the proposer */
int ui_debug_map_cancel(void) { if (!ui_mapvote_open(&mapv) || mapv.proposer != me()) return 0; map_cancel_send(UI_MAP_CANCEL_USER); return 1; }
static void map_decided(int d) {                             /* command apply: same step on every peer */
    if (d < 0) return;
    if (prompt_id == mapv.id) { prompt_id = 0; prompt_close = 1; }
    map_report = d;
    if (d == 1) map_changing = now_s();                      /* every peer: orders wait for the new map */
    dsnet_client *c = cli();
    if (d == 1 && c && dsnet_is_host(c)) { memcpy(map_order, mapv.order, mapv.order_len); map_order_len = mapv.order_len; map_submit = 1; }
}
static int map_propose_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *args, uint16_t len) {
    (void)s; (void)guid;
    uint32_t id; int kind; const uint8_t *ord; size_t olen;
    if (ui_map_propose_read(args, len, &id, &kind, &ord, &olen)) return -1;
    ui_players p; players(&p);
    if (!ui_mapvote_propose(&mapv, id, player, kind, ord, olen, p.present)) {
        if (player == me()) notice("Another map change is being voted on");
        return 0;
    }
    if (mapv.decided >= 0) map_decided(mapv.decided);
    else if (spect()) { char nm[40]; notice("%s wants to %s", ui_player_label(&p, player, nm), ui_map_short(kind)); }   /* no vote */
    else if (player != me() && left_prompt) { prompt_pending = 1; rig_log("ui: map vote waits for the disconnect box to close (ux_flow F20)"); }
    else if (player != me()) prompt_show();
    else { map_open_t = now_s(); map_cancel_sent = 0; }      /* the panel line "Waiting for <names> to agree" says the rest */
    return 0;
}
static int map_vote_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *args, uint16_t len) {
    (void)s; (void)guid;
    uint32_t id; int acc;
    if (ui_map_vote_read(args, len, &id, &acc)) return -1;
    ui_players p; players(&p);
    map_decided(ui_mapvote_vote(&mapv, player, id, acc, p.present));
    return 0;
}
static int result_send(int kind, const uint8_t *order, size_t len);
int ui_map_propose(int kind, const uint8_t *order, size_t len) {
    mp_session *s = sess();
    if (!on() || !s || len > UI_MAP_MAX_ORDER) return 0;
    if (spect()) { notice("You are watching: only players change the map"); return 1; }
    if (result_open && result_chooser == me()) return result_send(kind, order, len);
    if (ui_mapvote_open(&mapv)) { notice("Another map change is being voted on"); return 1; }
    uint8_t buf[UI_MAP_MAX_ORDER + 8]; static const uint8_t zero[16];
    uint32_t id = (uint32_t)(me() + 1) << 24 ^ ++map_counter ^ (uint32_t)rig_frames << 8;
    if (!id) id = 1;
    size_t n = ui_map_propose_write(buf, sizeof buf, id, kind, order, len);
    if (!n || mp_order(s, UI_ORDER_MAP_PROPOSE, zero, buf, (uint16_t)n)) { notice("The map change could not be sent"); return 1; }
    return 1;
}
static int result_send(int kind, const uint8_t *order, size_t len) {
    uint8_t buf[UI_MAP_MAX_ORDER + 1];
    buf[0] = (uint8_t)kind; memcpy(buf + 1, order, len);
    int rc = mp_order(sess(), UI_ORDER_RESULT_GO, result_guid, buf, (uint16_t)(len + 1));
    if (!rc) result_picked = 1;
    rig_log("ui: return screen pick sent as one order (kind %d, %zu bytes, rc %d)", kind, len, rc);
    if (rc) notice("The return could not be sent");
    return 1;
}
static void map_frame(void) {
    ui_players p; players(&p);
    if (ui_mapvote_open(&mapv)) map_decided(ui_mapvote_recheck(&mapv, p.present));   /* a voter left */
    /* ux_flow F20: the shared CommonNoticeView carried the disconnect box: the vote's prompt opens once it closed */
    if (prompt_pending && !left_prompt) { prompt_pending = 0; if (ui_mapvote_open(&mapv) && mapv.proposer != me() && !prompt_id) prompt_show(); }
    if (prompt_pending && !ui_mapvote_open(&mapv)) prompt_pending = 0;
    if (!on() || scene() != 2) map_changing = 0;             /* scene reset: never stuck */
    if (prompt_close) {
        prompt_close = 0;
        Obj v = find_view(&h_notice, "Refactor.UI", "CommonNoticeView");
        if (u_alive(v)) o_cn_cancel(v, NULL);                /* stock close (null actions), no vote: prompt_id is 0 */
    }
    /* review #5: the proposer withdraws its vote after 60 s with no decision (a command: same on every peer) */
    if (ui_mapvote_open(&mapv) && mapv.proposer == me() && map_open_t > 0 && now_s() - map_open_t > UI_MAP_TIMEOUT_S) map_cancel_send(UI_MAP_CANCEL);
    if (prompt_id) {                                         /* voter box: votes and time left, once a second */
        static int last = -1; int nv = 0;
        for (int i = 0; i < UI_MAX_PLAYERS; i++) nv += mapv.vote[i] >= 0;
        int s2 = (int)(now_s() - prompt_t0) * 64 + nv;
        Obj v = u_gc_get(h_notice);
        if (s2 != last && u_alive(v)) { last = s2; char txt[512]; prompt_text(v, txt, sizeof txt); tmp_set(AT(v, 0x30, Obj), txt); }
    }
    strip_frame();
    if (map_report >= 0) {                                   /* layout v2 §5b: one result toast on every peer */
        char r[200]; ui_mapvote_result(&mapv, &p, p.present, cur_floor() >= 0 ? cur_floor() + 1 : 0, r, sizeof r);
        notice("%s", r);
        map_report = -1;
    }
    if (map_submit) {
        map_submit = 0;
        if (!trn_submit_map_change) rig_log("ui: map change accepted but trn_submit_map_change is missing");
        else if (trn_submit_map_change(map_order, map_order_len)) { notice("The map change could not be started"); map_changing = 0; }
    }
}

/* ================================================================ 4. popups (owner rules 2026-10-12) */
/* Prefab truth (uiprefab bundle, read with UnityPy 2026-10-12): LevelUp _timeLock 1, TradeUI _timeLock 1
   (+ _keyLock 1), EventNodeUI is an EssentialPopUpUI_Base (always pauses: AddPauseReason EssentialPopup). */
static uintptr_t h_lu_unit, h_trade_unit;
static void (*o_lu_target)(Obj, Obj, void *);
static void (*o_trade_req)(Obj, Obj, void *);
static void (*o_activate_view)(Obj, int, void *);
static uintptr_t h_popups;                                   /* PopUpViewContainer (from ActivateView) */
static int popup_active(int type) {                          /* PopUpViewContainer.IsActivated 0xF62D64 */
    Obj c = u_gc_get(h_popups);
    return c && F(0xF62D64, bool (*)(Obj, int, void *))(c, type, NULL);
}
static void story_pause(int on_) {
    dsnet_client *c = cli();
    if (!c || dsnet_allow_pause(c) || on_ == story_held) return;   /* pause on: the EssentialPopup vote does it */
    uint8_t b = (uint8_t)on_;
    if (dsnet_submit(c, DSNET_STORY_PAUSE, 0, &b, 1) == 0) story_held = on_;
}
static void (*o_set_timelock)(Obj, void *);
static void (*o_unset_timelock)(Obj, void *);
static void hk_lu_target(Obj helper, Obj unit, void *mi) {    /* sim: LevelUpHandler.Process */
    u_gc_free(h_lu_unit); h_lu_unit = u_gc_new(unit);
    o_lu_target(helper, unit, mi);
}
static void hk_trade_req(Obj handler, Obj msg, void *mi) {     /* sim: TradeRequested{Map, Target, Performer +0x20} */
    u_gc_free(h_trade_unit); h_trade_unit = msg ? u_gc_new(AT(msg, 0x20, Obj)) : 0;
    o_trade_req(handler, msg, mi);
}
static int unit_owner_or_none(uintptr_t h) {
    Obj u = u_gc_get(h);
    if (!is_unit(u)) return UI_NO_UNIT;
    int o = owner_of(u);
    return o == UI_RESERVED ? UI_UNCLAIMED : o;            /* owner away: the host decides (same as unclaimed) */
}
/* PopUpViewContainer.ActivateView(PopUpType): every popup opens here. */
static void result_opened(void);
static void hk_activate_view(Obj cont, int type, void *mi) {
    if (u_gc_get(h_popups) != cont) { u_gc_free(h_popups); h_popups = u_gc_new(cont); }
    if (on()) {
        ui_pop_kind k = ui_pop_kind_of(type);
        rig_log("ui: popup %d requested (me %d, caller +%#lx)", type, me(), (unsigned long)((uintptr_t)__builtin_return_address(0) - ds_base));
        if (k == UI_POP_LEVELUP || k == UI_POP_TRADE) {
            Obj u = u_gc_get(k == UI_POP_LEVELUP ? h_lu_unit : h_trade_unit);
            int owner = unit_owner_or_none(k == UI_POP_LEVELUP ? h_lu_unit : h_trade_unit);
            if (ui_popup_route(k, me(), owner) == UI_ROUTE_SKIP) {
                char un[64], pn[40]; ui_players p; players(&p);
                unit_name(is_unit(u) ? u : NULL, un, sizeof un);
                rig_log("ui: popup %d not opened here (owner %d)", type, owner);
                if (k == UI_POP_TRADE) notice("%s is trading with %s", ui_player_label(&p, owner >= 0 ? owner : 0, pn), un);
                return;
            }
        }
        if (spect() && type == 2) {                            /* the stock Expedition window: players plan, spectators watch */
            notice("You are watching: only players plan expeditions");
            return;
        }
        if (k == UI_POP_STORY) {
            story_pause(1);
            /* lan_v8: a busy screen (recruitment, task planner...) must not hide the event: close this peer's other
               popups first (PopUpViewContainer.IsActivated / DeactivateView), except the return screen */
            for (int t = 1; t < 48; t++) {
                if (t == 22 || t == 3 || !F(0xF62D64, bool (*)(Obj, int, void *))(cont, t, NULL)) continue;
                rig_log("ui: story event: popup %d closed so the event shows", t);
                F(0xF62A88, void (*)(Obj, int, void *))(cont, t, NULL);
            }
        }
        if (k == UI_POP_RESULT) result_opened();
    }
    o_activate_view(cont, type, mi);
}

/* ---- return-to-base screen (DungeonResultView, popup 3; owner 2026-10-12): pauses everyone (stock time
   lock, plus DSNET_STORY_PAUSE when pause is off); the owner of the unit that used the portal picks
   (unclaimed / forced button: host); the others see it read-only. The picker's Return / Go down becomes
   one UI_ORDER_RESULT_GO order (guid = the portal unit, so mp_core's owner check admits only the picker)
   carrying the captured map-change order; every peer replays it at the same step (no vote). */
static uintptr_t h_portal_unit;                              /* sim: last InteractBuildingRequested.Performer */
static void (*o_interact_bld)(Obj, Obj, void *);
static void (*o_forced_return)(Obj, void *);
static void (*o_result_return)(Obj, Obj, void *);
static void (*o_result_godown)(Obj, Obj, void *);
static void hk_interact_bld(Obj h, Obj msg, void *mi) {    /* InteractionEventHandler.OnEvent(InteractBuildingRequested) */
    u_gc_free(h_portal_unit); h_portal_unit = msg ? u_gc_new(AT(msg, 0x18, Obj)) : 0;
    if (on() && msg) rig_log("ui: building interaction by %s on %s", cls_name(AT(msg, 0x18, Obj)), cls_name(AT(msg, 0x20, Obj)));
    o_interact_bld(h, msg, mi);
}
/* item 5 (lan_v9): who opens the return screen without a portal use? Every popup-3 request is logged with its callers
   (frame pointers, GameAssembly RVAs); the stock game over is named. */
static void (*o_pop_enqueue)(Obj, int, void *);
static void hk_pop_enqueue(Obj loader, int t, void *mi) {   /* ViewObjectLoader<PopUpType>.EnqueueActivatePool: who asks for popup 3 */
    if (t == 3 && on()) {                                    /* the caller chain (frame pointers), GameAssembly RVAs */
        char bt[200]; size_t k = 0; uintptr_t *fp = (uintptr_t *)__builtin_frame_address(0);
        for (int d = 0; d < 8 && fp && k < sizeof bt - 16; d++) {
            uintptr_t lr = fp[1]; fp = (uintptr_t *)fp[0];
            k += (size_t)snprintf(bt + k, sizeof bt - k, " +%#lx", (unsigned long)(lr - ds_base));
            if (!fp || (uintptr_t)fp & 7) break;
        }
        rig_log("ui: popup 3 queued, callers%s", bt);
        /* item 5 root cause (run 48): EntityEventHandler.CheckGameOver 0xD854EC (+0xD85838 here) opens it when no player
           unit on the dungeon map passes <CheckGameOver>b__69_0 0xD8DA20 (no Unconscious affecter, category 900): the
           stock party wipe. (Calling that test from here threw inside the sim cycle, run 51: never call it here.) */
        uintptr_t lr0 = (uintptr_t)__builtin_return_address(0) - ds_base;
        if (lr0 >= 0xD854EC && lr0 < 0xD858F0) rig_log("ui: return screen from the stock game over (CheckGameOver: every unit on the map unconscious)");
    }
    o_pop_enqueue(loader, t, mi);
}
static void hk_forced_return(Obj v, void *mi) {             /* ForcedBaseReturnView.OnClick_ReturnBase: host picks */
    if (on() && me() != 0) { notice("The host chooses who returns to the settlement"); return; }
    result_forced = 1;
    o_forced_return(v, mi);
}
static void result_opened(void) {
    Obj u = u_gc_get(h_portal_unit);
    int owner = !result_forced && is_unit(u) ? owner_of(u) : UI_NO_UNIT;
    result_chooser = ui_result_chooser(owner, result_forced);
    memset(result_guid, 0, 16);
    if (owner >= 0) memcpy(result_guid, (const uint8_t *)u + UNIT_GUID, 16);
    result_open = 1; result_close = 0; result_picked = 0;
    story_pause(1);
    char nm[96], un[64]; ui_players p; players(&p);
    unit_name(owner != UI_NO_UNIT ? u : NULL, un, sizeof un);
    rig_log("ui: return screen opened (portal unit %s, owner %d, chooser %d, forced %d)", un, owner, result_chooser, result_forced);
    if (result_chooser != me()) notice("%s is choosing who returns", pname(&p, result_chooser, nm, sizeof nm));
    result_forced = 0;
}
static int result_blocked(void) {                           /* read-only copy of the screen */
    if (!on() || !result_open || result_chooser == me()) return 0;
    char nm[40]; ui_players p; players(&p);
    notice("%s is choosing", ui_player_label(&p, result_chooser, nm));
    rig_log("ui: return screen click blocked (chooser %d)", result_chooser);
    return 1;
}
/* DungeonResultView.OnClick_Return 0xEA2460 / OnClick_GoDown 0xEA2C1C (Presenter.OnSelect starts with cbz: not hookable) */
static void hk_result_return(Obj v, Obj list, void *mi) { if (!result_blocked()) o_result_return(v, list, mi); }
static void hk_result_godown(Obj v, Obj list, void *mi) { if (!result_blocked()) o_result_godown(v, list, mi); }
/* lan_v9 (ux_flow_v10 #1): the picker closed the screen with its X (no pick): every peer closes its read-only copy and
   releases its hold; the game goes on where it is (nobody returns), as the X does in single player */
static int result_cancel_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *args, uint16_t len) {
    (void)s; (void)guid; (void)args; (void)len;
    rig_log("ui: return screen closed by the picker (player %d, X): read-only copies close here", player);
    if (result_open && player != me()) { ui_players p; players(&p); char nm[96]; notice("%s closed the Portal screen: nobody returns", pname(&p, player, nm, sizeof nm)); }
    result_close = 1;
    return 0;
}
static int result_go_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *args, uint16_t len) {
    (void)s; (void)guid;
    if (len < 2) return -1;
    rig_log("ui: return order from player %d (%u bytes), replayed here", player, (unsigned)len - 1);
    int rc = ord_apply(player, args + 1, (size_t)len - 1);
    result_close = 1;                                        /* close the read-only copies (frame) */
    return rc;
}
/* review #4: the read-only copy says who picks (a fixed line above the window) and its buttons and X are greyed */
static uintptr_t h_result_view, h_result_line, h_result_pill;
static dis_list result_dis = { .dim = 1 };
static void result_readonly_frame(void) {
    Obj v = result_open ? find_view(&h_result_view, "Refactor.UI", "DungeonResultView") : u_gc_get(h_result_view);
    int ro = on() && result_open && result_chooser != me() && v && u_go_active_self(u_go(v));
    Obj l = u_gc_get(h_result_line), pill = u_gc_get(h_result_pill);
    if (ro && !u_alive(l)) {
        /* layout v2 §5e: a stock TextBox pill (420x30, Lora 16) inside the window, on the rule under the title. The
           view's own rect is the whole screen: anchor to the "Member(n/m)" text (+0x50), centred 19 units above
           its top (1512x982: title 255-305, rule 325, Member pill 340-362) */
        Obj mt = AT(v, 0x50, Obj), par = u_alive(mt) ? u_tr(mt) : u_tr(v);
        pill = img_make(par, sprite_get(SP_TEXTBOX), 420, 30);
        if (pill) {
            Obj rt = u_tr(pill);
            F(0x51072F4, void (*)(float, float, Obj, void *))(0.5f, 0.5f, rt, NULL);
            u_rt_set_pos(rt, 0, u_alive(mt) ? 19 : -120);
            l = line_clone(rt, 0.5f, 420, 30, -15);
            if (ui_is(l, K_TMP)) { F(0x4F0B5DC, void (*)(Obj, bool, void *))(l, false, NULL); F(0x4F0B340, void (*)(Obj, float, void *))(l, 16, NULL); }
        }
        u_gc_free(h_result_line); h_result_line = u_gc_new(l); u_gc_free(h_result_pill); h_result_pill = u_gc_new(pill);
    }
    if (u_alive(pill) && u_go_active_self(u_go(pill)) != ro) u_go_active(u_go(pill), ro);
    if (u_alive(l) && u_go_active_self(u_go(l)) != ro) u_go_active(u_go(l), ro);
    if (ro) {
        char nm[96], t[200]; ui_players p; players(&p);
        snprintf(t, sizeof t, "<color=#ccbeb8>%s is choosing who returns</color>", pname(&p, result_chooser, nm, sizeof nm));
        line_text(l, t);
        dis_apply(&result_dis, v);
    } else if (result_dis.n) dis_restore(&result_dis);
}
/* frame: forget a closed screen, close the read-only copies after the picker's order, pause hold */
static void result_frame(void) {
    if (result_open && result_close) {
        result_close = 0;
        Obj c = u_gc_get(h_popups);
        if (c && popup_active(3)) F(0xF62A88, void (*)(Obj, int, void *))(c, 3, NULL);   /* PopUpViewContainer.DeactivateView */
    }
    if (result_open && !popup_active(3)) {
        result_open = 0; rig_log("ui: return screen closed");
        if (on() && result_chooser == me() && !result_picked && sess()) {   /* the picker's X: tell everyone */
            uint8_t b = 0; int rc = mp_order(sess(), UI_ORDER_RESULT_CANCEL, result_guid, &b, 1);
            rig_log("ui: return screen closed by its X without a pick: cancel sent (rc %d)", rc);
        }
        /* the screen's own RemovePauseReason(PopupTimeLock) can run inside the map-change flow, where mp_game
           lets it through as a local change instead of a run vote: release this peer's popup hold here
           (mp_popup_pause clamps at 0, so a second release is harmless for this reason) */
        mp_session *s = sess();
        if (s && mp_popup_pause(s, MP_PAUSE_POPUP_TIMELOCK, 0)) rig_log("ui: return screen pause hold released");
    }
    if (story_held && !popup_active(22) && !result_open) story_pause(0);
    result_readonly_frame();
}
/* PopUpUI_Base.Set/UnsetTimeLock: level-up and trade never pause anyone in multiplayer; the Esc menu
   (SystemMenuView) neither (owner 2026-10-13: the Esc menu in multiplayer pauses nobody). Single player: stock. */
static int no_timelock(Obj popup) {
    if (!on()) return 0;
    const char *n = cls_name(popup);
    /* lan_v8: the recruitment window neither (its time lock paused everyone, which froze its refresh timer) */
    return !strcmp(n, "LevelUpView") || !strcmp(n, "TradeUIView") || !strcmp(n, "SystemMenuView") || !strcmp(n, "RecruitView");
}
static void hk_set_timelock(Obj p, void *mi) {
    int skip = no_timelock(p);
    if (on()) rig_log("ui: %s time lock %s", cls_name(p), skip ? "skipped (multiplayer: never pauses)" : "kept");
    if (!skip) o_set_timelock(p, mi);
}
static void hk_unset_timelock(Obj p, void *mi) { if (!no_timelock(p)) o_unset_timelock(p, mi); }

/* ---- test hook: a level-up started by the sim on every peer at the same step (no statue walk needed).
   ui_levelup {unit, building, map}: mp order UI_ORDER_DEBUG_LEVELUP (guid = the unit, so the normal owner
   check applies); the handler queues InteractBuildingRequested{MapType +0x10, Performer +0x18,
   TargetBuilding +0x20} on EventQueue (mf+0x48, AddEvent 0xCCFAD4), which runs in the next sim tick:
   InteractionEventHandler 0xD9C6B4 -> LevelUpHandler.Process 0xF9E860 -> popup queue -> ActivateView(21). */
void *ord_debug_find_entity(int map, const uint8_t g[16]) __attribute__((weak_import));
void *ord_debug_class(const char *ns, const char *name) __attribute__((weak_import));
static int levelup_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)player;
    if (n < 20 || !ord_debug_find_entity || !ord_debug_class || !mf) return -1;
    int32_t map; memcpy(&map, a + 16, 4);
    Obj unit = ord_debug_find_entity(map, guid), bld = ord_debug_find_entity(map, a);
    void *k = ord_debug_class("Refactor.Main.Event", "InteractBuildingRequested");
    Obj eq = AT(mf, 0x48, Obj);
    rig_log("ui: debug level-up: unit %p building %p class %p queue %p", unit, bld, k, eq);
    if (!unit || !bld || !k || !eq) return -1;
    Obj ev = F(0x6F96CC, Obj (*)(void *))(k);
    AT(ev, 0x10, int32_t) = map;
    F(0x6F95C8, void (*)(Obj, void **, Obj))(ev, (void **)((char *)ev + 0x18), unit);
    F(0x6F95C8, void (*)(Obj, void **, Obj))(ev, (void **)((char *)ev + 0x20), bld);
    F(0xCCFAD4, void (*)(Obj, Obj, void *))(eq, ev, NULL);
    return 0;
}
#define UI_ORDER_DEBUG_LEVELUP 6
/* test only: a stock story event started on every peer at the same step: EventFlowProcessor.StartEvent(key) 0xCEA620
   (MainFlow._eventProcessor +0x58 .EventFlowProcessor +0x30), from the command (sim scope). Zero guid, args u8 len + key. */
#define UI_ORDER_DEBUG_EVENT 34
#define UI_ORDER_DEBUG_GOLD 35                               /* test only: clan gold (ClanDataContainer +0x2C) on every peer */
static int debug_gold_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)player; (void)guid;
    Obj c0 = cdc(), clan = c0 ? AT(c0, 0x50, Obj) : NULL;
    if (n < 4 || !clan) return -1;
    int32_t g; memcpy(&g, a, 4); AT(clan, 0x2C, int32_t) = g;
    rig_log("ui: debug clan gold set to %d", g);
    return 0;
}
/* test only (lan_v9 gear rule): the unit (guid; normal owner / controller check) picks up an item: PickUpItemRequested
   {Map +0x10, From +0x14, To +0x20, Item +0x28, Amount +0x30 null} queued on EventQueue (sim, every peer), handled by
   ItemEventHandler.OnEvent(PickUpItemRequested), where ord_game.c tags a free item to the unit's OWNER. args: map, item. */
#define UI_ORDER_DEBUG_PICKUP 36
static int debug_pickup_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)player;
    if (n < 20 || !ord_debug_find_entity || !ord_debug_class || !mf) return -1;
    int32_t map; memcpy(&map, a, 4);
    Obj unit = ord_debug_find_entity(map, guid), item = ord_debug_find_entity(map, a + 4);
    void *k = ord_debug_class("Refactor.Main.Event", "PickUpItemRequested");
    Obj eq = AT(mf, 0x48, Obj);
    rig_log("ui: debug pick-up: unit %p item %p (%s) by player %d", unit, item, item ? cls_name(item) : "-", player);
    if (!unit || !item || !k || !eq) return -1;
    Obj ev = F(0x6F96CC, Obj (*)(void *))(k);
    AT(ev, 0x10, int32_t) = map;
    { Obj pd = pos_dict(map); Obj pe = pd ? AT(pd, 0x18, Obj) : NULL; int pn = pd ? AT(pd, 0x20, int32_t) : 0;   /* From = the item's cell */
      for (int q = 0; pe && q < pn; q++) { char *r = (char *)pe + 0x20 + q * 0x20;
          if (!memcmp(r + 8, a + 4, 16)) { AT(ev, 0x14, int32_t) = (int32_t)(*(float *)(r + 0x18)); AT(ev, 0x18, int32_t) = (int32_t)(*(float *)(r + 0x1C)); break; } } }
    F(0x6F95C8, void (*)(Obj, void **, Obj))(ev, (void **)((char *)ev + 0x20), unit);
    F(0x6F95C8, void (*)(Obj, void **, Obj))(ev, (void **)((char *)ev + 0x28), item);
    F(0xCCFAD4, void (*)(Obj, Obj, void *))(eq, ev, NULL);
    return 0;
}
/* test: ui_items {map}: items lying on the map: EntityDataContainer (cdc +0x20) +0x28 Dictionary<MapType,
   ItemDataContainer>; container +0x20 _onMapItems Dictionary<Guid, ItemHolder> (stride 0x20: guid +8); position from
   pos_dict; dsnet item owner */
static char *ui_items_cmd(const char *line) {
    double dm = cur_map(); jnum(line, "map", &dm);
    Obj c0 = cdc(), edc = c0 ? AT(c0, 0x20, Obj) : NULL;
    Obj ic = edc ? dict_int_get(AT(edc, 0x28, Obj), (int)dm) : NULL, om = ic ? AT(ic, 0x20, Obj) : NULL;
    Obj pd = pos_dict((int)dm); dsnet_client *c = cli();
    Buf b = {0}; bput(&b, "{\"ok\":true,\"items\":[");
    Obj ent = om ? AT(om, 0x18, Obj) : NULL; int n = om ? AT(om, 0x20, int32_t) : 0, k = 0;
    for (int i = 0; ent && i < n && k < 60; i++) {
        char *e = (char *)ent + 0x20 + i * 0x38;              /* Dictionary<Guid, ItemHolder>: the value is a 0x20 struct */
        if (*(int32_t *)e < 0) continue;
        Guid g = *(Guid *)(e + 8); char gs[40]; guid_str(g, gs);
        float x = -1, y = -1;
        Obj pe = pd ? AT(pd, 0x18, Obj) : NULL; int pn = pd ? AT(pd, 0x20, int32_t) : 0;
        for (int q = 0; pe && q < pn; q++) { char *r = (char *)pe + 0x20 + q * 0x20; if (!memcmp(r + 8, &g, 16)) { x = *(float *)(r + 0x18); y = *(float *)(r + 0x1C); break; } }
        bput(&b, "%s{\"guid\":\"%s\",\"cell\":[%d,%d],\"owner\":%d}", k++ ? "," : "", gs, (int)x, (int)y,
             c ? dsnet_item_owner(c, (const uint8_t *)&g) : -9);
    }
    bput(&b, "],\"on_map\":%d,\"raw\":[", n);
    for (int i = 0; ent && i < n && i < 4; i++) {           /* debug: the entries as stored (hash, next, key, holder) */
        const uint8_t *e = (const uint8_t *)ent + 0x20 + i * 0x38; char hx[2 * 0x38 + 1];
        for (int q = 0; q < 0x38; q++) sprintf(hx + 2 * q, "%02x", e[q]);
        bput(&b, "%s\"%s\"", i ? "," : "", hx);
    }
    bput(&b, "],\"arr_len\":%d", ent ? (int)AT(ent, 0x18, int64_t) : -1);
    {   /* debug: is each on-map item also an ItemEntity in _items (+0x18, stride 0x20)? */
        Obj d1 = ic ? AT(ic, 0x18, Obj) : NULL, e1 = d1 ? AT(d1, 0x18, Obj) : NULL; int n1 = d1 ? AT(d1, 0x20, int32_t) : 0, hit = 0;
        for (int i = 0; ent && i < n; i++) {
            const uint8_t *e = (const uint8_t *)ent + 0x20 + i * 0x38;
            for (int j = 0; e1 && j < n1; j++) if (!memcmp((const uint8_t *)e1 + 0x20 + j * 0x20 + 8, e + 8, 16)) { hit++; break; }
        }
        bput(&b, ",\"items_n\":%d,\"in_items\":%d}", n1, hit);
    }
    return b.p;
}
static int debug_event_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)player; (void)guid;
    if (n < 1 || a[0] + 1 > n || !mf) return -1;
    char key[96]; int l = a[0] < 95 ? a[0] : 95; memcpy(key, a + 1, (size_t)l); key[l] = 0;
    Obj ep = AT(mf, 0x58, Obj), flow = ep ? AT(ep, 0x30, Obj) : NULL;
    if (!flow) return -1;
    bool ok = F(0xCEA620, bool (*)(Obj, Obj, void *))(flow, u_str(key), NULL);
    rig_log("ui: debug story event %s started: %d", key, ok);
    return 0;
}
static char *ui_result_cmd(const char *line);
/* ui_buildings {map, subtype}: buildings of a BuildingSubType (2 = DungeonPortal, 3 = LevelUp) with their position.
   EntityDataContainer (cdc) +0x20 Dictionary<MapType, BuildingDataContainer>; container +0x18 Dictionary<Guid,
   BuildingEntity> (entries stride 0x20: guid +8, value +0x18); building +0x48 table data, +0x24 SubType;
   positions: pos_dict(map) Dictionary<Guid, Vector2> (stride 0x20: guid +8, value +0x18). */
static char *ui_buildings_cmd(const char *line) {
    double dm = 1, ds = 2; jnum(line, "map", &dm); jnum(line, "subtype", &ds);
    Buf b = {0}; bput(&b, "{\"ok\":true,\"buildings\":[");
    Obj c = cdc(), edc = c ? AT(c, 0x20, Obj) : NULL;
    Obj bc = edc ? dict_int_get(AT(edc, 0x20, Obj), (int)dm) : NULL, bd = bc ? AT(bc, 0x18, Obj) : NULL;   /* BuildingDataContainer._buildings */
    Obj pd = pos_dict((int)dm);
    int k = 0;
    Obj ent = bd ? AT(bd, 0x18, Obj) : NULL; int n = bd ? AT(bd, 0x20, int32_t) : 0;
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x20;
        Obj bld = *(Obj *)(e + 0x18);
        if (!bld || *(int32_t *)e < 0) continue;
        Obj td = AT(bld, 0x48, Obj);
        if (!td || AT(td, 0x24, int32_t) != (int32_t)ds) continue;
        Guid g = *(Guid *)(e + 8); char gs[40]; guid_str(g, gs);
        float x = -1, y = -1;
        Obj pe = pd ? AT(pd, 0x18, Obj) : NULL; int pn = pd ? AT(pd, 0x20, int32_t) : 0;
        for (int j = 0; pe && j < pn; j++) {
            char *q = (char *)pe + 0x20 + j * 0x20;
            if (!memcmp(q + 8, &g, 16)) { x = *(float *)(q + 0x18); y = *(float *)(q + 0x1C); break; }
        }
        bput(&b, "%s{\"guid\":\"%s\",\"class\":\"%s\",\"x\":%.1f,\"y\":%.1f}", k++ ? "," : "", gs, cls_name(bld), x, y);
    }
    bput(&b, "]}");
    return b.p;
}
static char *ui_view_cmd(void);
static uintptr_t h_banner[2]; static int n_dis;
static void hk_cn_confirm(Obj v, void *mi); static void hk_cn_cancel(Obj v, void *mi);
static int dbg_foreign = -9, dbg_view_active[2] = {-9, -9};
static void plist_click(int i);
static void hk_activate_view(Obj cont, int type, void *mi);
static int pl_pop; static void plist_frame(void); int ui_debug_units(int dir);
static char *ui_panel_move_cmd(const char *line);
static uintptr_t h_ev_view; static uint32_t ev_node; static ui_story story; static int ev_chooser(void);
static int pop_state(int *have, int *cap, int *pool, int *mode); static uintptr_t h_campinfo;
static void hk_recruit_click(Obj v, int idx, void *mi);
static Obj tr_find(Obj t, const char *path);
static void hk_ev_click(Obj v, int idx, void *mi); static uintptr_t h_chip_tx[], h_recruit_line, h_recruit_view; static int owners_ro, owners_toggles;
static double tip_until; static char tip_shown[160]; int ui_conn_text(int player, char *out, int cap); static int ev_rule(void);
static char *ui_lend_cmd(const char *line); static char *ui_escape_cmd(const char *line); static char *ui_tip_hover_cmd(const char *line); static char *ui_tip_where_cmd(void); static char *ui_players_lists_cmd(const char *line); static void viewer_geom(float *g); static void viewer_move_test(const char *line, int reset); static void viewer_follow(int on_); static void viewer_ask(void); static Obj cam_setting(void);
static int viewer_on, viewer_leader, follow_target; static uintptr_t h_vtitle; static uint32_t followers[UI_MAX_PLAYERS];
char *ui_exec(const char *cmd, const char *line) {
    teams_lookup();
    if (ui_teams_exec) { char *r = ui_teams_exec(cmd, line); if (r) return r; }
    if (!strcmp(cmd, "ui_view")) return ui_view_cmd();
    if (!strcmp(cmd, "ui_capture")) {                        /* ui_capture {path}: the screen with the UI (needs a window) */
        char path[1024] = ""; jstr(line, "path", path, sizeof path);
        Obj tex = F(0x51FA520, Obj (*)(void *))(NULL);                                     /* ScreenCapture.CaptureScreenshotAsTexture */
        Obj png = tex ? F(0x51657D8, Obj (*)(Obj, void *))(tex, NULL) : NULL;               /* EncodeToPNG */
        size_t n = png ? (size_t)AT(png, 0x18, int64_t) : 0;
        FILE *f = n && path[0] ? fopen(path, "wb") : NULL;
        if (f) { fwrite((char *)png + 0x20, 1, n, f); fclose(f); }
        if (tex) F(0x50FC8F8, void (*)(Obj, void *))(tex, NULL);                           /* Object.Destroy */
        Buf b = {0}; bput(&b, "{\"ok\":%s,\"bytes\":%zu}", f ? "true" : "false", n); return b.p;
    }
    if (!strcmp(cmd, "ui_notice")) {                         /* ui_notice {button: "confirm"|"cancel"}: click the Yes/No box like a player */
        char bt[16] = "confirm"; jstr(line, "button", bt, sizeof bt);
        Obj v = find_view(&h_notice, "Refactor.UI", "CommonNoticeView");
        if (v) { if (!strcmp(bt, "cancel")) hk_cn_cancel(v, NULL); else hk_cn_confirm(v, NULL); }
        Buf b = {0}; bput(&b, "{\"ok\":%s,\"left_prompt\":%d}", v ? "true" : "false", left_prompt); return b.p;
    }
    if (!strcmp(cmd, "ui_owner")) {                          /* ui_owner {guid}: dsnet owner (-1 unclaimed) */
        char gs[64] = ""; Guid g; jstr(line, "guid", gs, sizeof gs);
        Buf b = {0}; bput(&b, "{\"ok\":true,\"owner\":%d}", guid_parse(gs, &g) ? owner_of_guid((const uint8_t *)&g) : -1); return b.p;
    }
    if (!strcmp(cmd, "ui_popup")) {                          /* ui_popup {type}: open a popup like the game (test) */
        double t = 0; jnum(line, "type", &t); Obj c = u_gc_get(h_popups);
        if (c) hk_activate_view(c, (int)t, NULL);
        Buf b = {0}; bput(&b, "{\"ok\":%s,\"active\":%d}", c ? "true" : "false", c ? popup_active((int)t) : -1); return b.p;
    }
    if (!strcmp(cmd, "ui_click")) {                          /* ui_click {row: 0 header, 2 see-through, 3 names} */
        double r = 0; jnum(line, "row", &r); plist_click((int)r);
        Buf b = {0}; bput(&b, "{\"ok\":true,\"see_through\":%d,\"names\":%d,\"list_open\":%d}", uiset.see_through, uiset.names, uiset.list_open);
        return b.p;
    }
    if (!strcmp(cmd, "ui_result")) return ui_result_cmd(line);
    if (!strcmp(cmd, "ui_pop")) {                            /* ui_pop {open}: the panel's settings box (cog) */
        double o = 1; jnum(line, "open", &o); pl_pop = o != 0; plist_frame();
        Buf b = {0}; bput(&b, "{\"ok\":true,\"pop\":%d}", pl_pop); return b.p;
    }
    if (!strcmp(cmd, "ui_units")) { double d = 1; jnum(line, "dir", &d); ui_debug_units(d < 0 ? -1 : 1); Buf b = {0}; bput(&b, "{\"ok\":true}"); return b.p; }
    if (!strcmp(cmd, "ui_texts")) {                          /* ui_texts {min}: active TMP texts at fontSize >= min (finding stock views) */
        double mn = 24; jnum(line, "min", &mn);
        Obj tt = type_of("TMPro", "TMP_Text");
        Obj arr = tt ? F(0x50FCD74, Obj (*)(Obj, bool, void *))(tt, false, NULL) : NULL;   /* Object.FindObjectsOfType(Type, includeInactive) */
        int n = arr ? AT(arr, 0x18, int32_t) : 0, k = 0;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"n\":%d,\"texts\":[", n);
        for (int i = 0; i < n && k < 60; i++) {
            Obj t = AT(arr, 0x20 + 8 * i, Obj);
            if (!ui_is(t, K_TMP) || !F(0x50F3724, bool (*)(Obj, void *))(u_go(t), NULL)) continue;
            float fs = F(0x4F0B338, float (*)(Obj, void *))(t, NULL);
            if (fs < mn) continue;
            char tx[160], path[300] = ""; str_utf8(tmp_get(t), tx, sizeof tx);
            Obj tr = u_tr(t); size_t pl = 0;
            for (int d = 0; tr && d < 8; d++) { char nm[64]; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(tr, NULL), nm, sizeof nm);
                pl += (size_t)snprintf(path + pl, sizeof path - pl, "%s%s", d ? "<" : "", nm); if (pl >= sizeof path) break; tr = u_tr_parent(tr); }
            bput(&b, "%s{\"size\":%.0f,\"text\":", k++ ? "," : "", fs); bstr(&b, tx); bput(&b, ",\"path\":"); bstr(&b, path); bput(&b, "}");
        }
        bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "ui_lend")) return ui_lend_cmd(line);
    if (!strcmp(cmd, "ui_story")) {                          /* ui_story {action: state | event | click | rule, type, idx, rule} */
        char a[16] = "state"; double v = 0; jstr(line, "action", a, sizeof a);
        int rc = 0;
        if (!strcmp(a, "event")) { char key[96] = "Event_Random4"; jstr(line, "key", key, sizeof key); uint8_t b[100]; size_t l = strlen(key);
                                    b[0] = (uint8_t)l; memcpy(b + 1, key, l); static const uint8_t zero[16];
                                    rc = sess() ? mp_order(sess(), UI_ORDER_DEBUG_EVENT, zero, b, (uint16_t)(l + 1)) : -1; }
        else if (!strcmp(a, "click")) { jnum(line, "idx", &v); Obj ev = u_gc_get(h_ev_view); if (u_alive(ev)) hk_ev_click(ev, (int)v, NULL); else rc = -1; }
        else if (!strcmp(a, "pop")) { jnum(line, "value", &v); dsnet_client *c = cli();
            if (c && me() == 0) { dsnet_rules r; dsnet_rules_get(c, &r); if (r.units_per_player <= 0) r.units_per_player = ui_max_owned; r.population = (int)v; rc = dsnet_set_rules(c, &r); } else rc = -1; }
        else if (!strcmp(a, "gear")) { jnum(line, "value", &v); dsnet_client *c = cli();
            if (c && me() == 0) { dsnet_rules r; dsnet_rules_get(c, &r); if (r.units_per_player <= 0) r.units_per_player = ui_max_owned; r.gear_mode = (int)v; rc = dsnet_set_rules(c, &r); } else rc = -1; }
        else if (!strcmp(a, "pickup")) { char us[64] = "", is[64] = ""; jstr(line, "unit", us, sizeof us); jstr(line, "item", is, sizeof is);
            Guid gu, gi; uint8_t b2[20]; int32_t m = cur_map();
            if (sess() && guid_parse(us, &gu) && guid_parse(is, &gi)) { memcpy(b2, &m, 4); memcpy(b2 + 4, &gi, 16); rc = mp_order(sess(), UI_ORDER_DEBUG_PICKUP, (const uint8_t *)&gu, b2, 20); } else rc = -1; }
        else if (!strcmp(a, "gold")) { jnum(line, "value", &v); int32_t g = (int32_t)v; static const uint8_t zero[16]; rc = sess() ? mp_order(sess(), UI_ORDER_DEBUG_GOLD, zero, &g, 4) : -1; }
        else if (!strcmp(a, "recruit")) { jnum(line, "value", &v); static uintptr_t h_rv3; Obj rv = find_view(&h_rv3, "Refactor.UI", "RecruitView");
            if (rv && u_go_active_self(u_go(rv))) hk_recruit_click(rv, (int)v, NULL); else rc = -1;   /* RecruitView.OnRecruitCandidate (our hook) */
            rig_log("ui: test recruit %d: view %p active %d rc %d", (int)v, rv, rv ? u_go_active_self(u_go(rv)) : -1, rc); }
        else if (!strcmp(a, "rule")) { jnum(line, "value", &v); dsnet_client *c = cli(); if (c && me() == 0) { dsnet_rules r; dsnet_rules_get(c, &r); if (r.units_per_player <= 0) r.units_per_player = ui_max_owned;   /* get gives 0 for the default, set refuses 0 */
              r.event_choice = (int)v; rc = dsnet_set_rules(c, &r);
              rig_log("ui: test rule: event choice %d -> set rc %d (pause %d speed %d gear %d units %d pop %d)", (int)v, rc, r.pause_mode, r.allow_speedup, r.gear_mode, r.units_per_player, r.population); } else rc = -1; }
        Obj ev = u_gc_get(h_ev_view); Obj cur = u_alive(ev) ? AT(ev, 0x68, Obj) : NULL;
        int ph = -1, pc_ = -1, pp = -1, pm = -1; pop_state(&ph, &pc_, &pp, &pm);
        Obj c1 = cdc(), cl = c1 ? AT(c1, 0x50, Obj) : NULL;
        char hud[32] = ""; { Obj ov = u_gc_get(h_campinfo), t = u_alive(ov) ? AT(ov, 0x38, Obj) : NULL; str_utf8(ui_is(t, K_TMP) ? tmp_get(t) : NULL, hud, sizeof hud); }
        Buf b = {0}; bput(&b, "{\"ok\":true,\"pop\":{\"have\":%d,\"cap\":%d,\"pool\":%d,\"mode\":%d,\"clan\":%d,\"base\":%d,\"gold\":%d,\"own\":%d,\"hud\":\"%s\"},",
                          ph, pc_, pp, pm, cl ? AT(cl, 0x38, int32_t) : -1, cl ? AT(cl, 0x3C, int32_t) : -1, cl ? AT(cl, 0x2C, int32_t) : -1, cli() ? dsnet_owned_count(cli(), me()) : -1, hud);
        bput(&b, "\"rc\":%d,\"active\":%d,\"recruit\":%d,\"node\":%u,\"story_node\":%u,\"decided\":%d,\"chooser\":%d,\"rule\":%d,\"choices\":%d,\"votes\":%d}",
                          rc, popup_active(22), popup_active(8), ev_node, story.node, story.decided, ev_chooser(), ev_rule(), cur ? AT(cur, 0x18, int32_t) : -1,
                          ui_story_votes_in(&story, 0xFFFFFFFFu));
        return b.p;
    }
    if (!strcmp(cmd, "ui_panel_move")) return ui_panel_move_cmd(line);
    if (!strcmp(cmd, "ui_v8")) {                             /* lan_v8 probe: chips, recruit line + timer, read-only columns, tooltip */
        Buf b = {0}; char t[200]; int k = 0;
        bput(&b, "{\"ok\":true,\"chips\":[");
        for (int i = 0; i < MAX_SC; i++) { Obj tx = u_gc_get(h_chip_tx[i]); if (!u_alive(tx) || !u_go_active_self(u_go(tx))) continue; str_utf8(tmp_get(tx), t, sizeof t); if (k++) bput(&b, ","); bstr(&b, t); }
        Obj rl = u_gc_get(h_recruit_line), rv = u_gc_get(h_recruit_view), rt = u_alive(rv) ? AT(rv, 0x58, Obj) : NULL;
        str_utf8(u_alive(rl) && u_go_active_self(u_go(rl)) ? tmp_get(rl) : NULL, t, sizeof t); bput(&b, "],\"recruit_line\":"); bstr(&b, t);
        str_utf8(ui_is(rt, K_TMP) ? tmp_get(rt) : NULL, t, sizeof t); bput(&b, ",\"recruit_timer\":"); bstr(&b, t);
        bput(&b, ",\"readonly_columns\":%d,\"readonly_toggles\":%d,\"tooltip\":", owners_ro, owners_toggles); bstr(&b, tip_until > 0 ? tip_shown : "");
        char ct[120] = ""; if (cli()) ui_conn_text(me() == 0 ? 1 : 0, ct, sizeof ct); bput(&b, ",\"conn_text_other\":"); bstr(&b, ct);
        bput(&b, ",\"event_rule\":%d}", ev_rule());
        return b.p;
    }
    if (!strcmp(cmd, "ui_viewer")) {                         /* ui_viewer {action: state | follow | free | ask} */
        char a[16] = "state"; jstr(line, "action", a, sizeof a);
        if (!strcmp(a, "follow")) viewer_follow(1); else if (!strcmp(a, "free")) viewer_follow(0); else if (!strcmp(a, "ask")) viewer_ask();
        else if (!strcmp(a, "move") || !strcmp(a, "reset")) viewer_move_test(line, a[0] == 'r');   /* the right-drag's result */
        Obj cs = cam_setting(); V3 cp = cs ? F(0xD23584, V3 (*)(Obj, void *))(cs, NULL) : (V3){ 0, 0, 0 };
        char t[200] = ""; Obj vt = u_gc_get(h_vtitle); if (u_alive(vt) && u_go_active_self(u_go(vt))) str_utf8(tmp_get(vt), t, sizeof t);
        Buf b = {0}; bput(&b, "{\"ok\":true,\"viewer\":%d,\"leader\":%d,\"follow\":%d,\"followers_of_me\":%u,\"cam\":[%.2f,%.2f],\"title\":", viewer_on, viewer_leader, follow_target, me() >= 0 ? followers[me()] : 0, cp.x, cp.y);
        bstr(&b, t);
        { float g[9]; viewer_geom(g);                        /* layer units: box x, y(top), h; panel L, Y(top), w, h; bar bottom; drag set */
          bput(&b, ",\"box\":[%.0f,%.0f,%.0f],\"panel\":[%.0f,%.0f,%.0f,%.0f],\"bar_bottom\":%.0f,\"moved\":%.0f", g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8]); }
        bput(&b, "}"); return b.p;
    }
    if (!strcmp(cmd, "ui_sprites")) {                        /* ui_sprites {match}: loaded Sprite names containing match (lan_v10 art search) */
        char mt[64] = ""; jstr(line, "match", mt, sizeof mt);
        Obj st = type_of("UnityEngine", "Sprite");
        Obj arr = st ? F(0x50E8010, Obj (*)(Obj, void *))(st, NULL) : NULL;
        int n = arr ? AT(arr, 0x18, int32_t) : 0, k = 0;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"n\":%d,\"names\":[", n);
        for (int i = 0; i < n && k < 400; i++) {
            Obj o = AT(arr, 0x20 + 8 * i, Obj); if (!o) continue;
            char nm[96]; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(o, NULL), nm, sizeof nm);
            if (mt[0] && !strcasestr(nm, mt)) continue;
            if (k++) bput(&b, ","); bstr(&b, nm);
        }
        bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "ui_escape")) return ui_escape_cmd(line);
    if (!strcmp(cmd, "ui_coop")) return ui_coop_cmd(line);
    if (!strcmp(cmd, "ui_spect")) return ui_spect_cmd(line);
    if (!strcmp(cmd, "ui_mini")) return ui_mini_cmd(line);
    if (!strcmp(cmd, "ui_tip_hover")) return ui_tip_hover_cmd(line);
    if (!strcmp(cmd, "ui_tip_where")) return ui_tip_where_cmd();
    if (!strcmp(cmd, "ui_players_lists")) return ui_players_lists_cmd(line);
    { char *r = ui_tip_exec(cmd, line); if (r) return r; }
    if (!strcmp(cmd, "ui_items")) return ui_items_cmd(line);
    if (!strcmp(cmd, "ui_map_cancel")) { Buf b = {0}; bput(&b, "{\"ok\":true,\"rc\":%d}", ui_debug_map_cancel()); return b.p; }
    if (!strcmp(cmd, "ui_buildings")) return ui_buildings_cmd(line);
    if (strcmp(cmd, "ui_levelup") && strcmp(cmd, "ui_interact")) return NULL;
    char us[64] = "", bs[64] = ""; double map = 0;
    jstr(line, "unit", us, sizeof us); jstr(line, "building", bs, sizeof bs); jnum(line, "map", &map);
    Guid gu, gb; mp_session *s = sess();
    Buf b = {0};
    if (!s || !guid_parse(us, &gu) || !guid_parse(bs, &gb)) { bput(&b, "{\"ok\":false,\"error\":\"need a session, unit and building guids\"}"); return b.p; }
    uint8_t a[20]; int32_t m = (int32_t)map;
    memcpy(a, &gb, 16); memcpy(a + 16, &m, 4);
    int rc = mp_order(s, UI_ORDER_DEBUG_LEVELUP, (const uint8_t *)&gu, a, sizeof a);
    bput(&b, "{\"ok\":%s,\"rc\":%d}", rc ? "false" : "true", rc);
    return b.p;
}

/* ---- story popups: EventNodeUIView _titleText +0x40, _currentChoices List<string> +0x68, _isChoiceSubmitting +0x70.
   EventInstance._satisfiedUnits IEntity[] +0x30 (the units the event is about). */
static uintptr_t h_ev_view, h_ev_unit;
static uint32_t ev_node;                                     /* node on screen here */
static char ev_title[256];
static ui_story story;
static int story_click = -1;                                 /* host: decided choice to press in ui_frame */
static uint32_t story_click_node;
static mp_session *story_sess;
static void (*o_ev_set_event)(Obj, Obj, void *);
static void (*o_ev_set_choices)(Obj, Obj, void *);
static void (*o_ev_click)(Obj, int, void *);

/* lan_v8 host rule "Event choices" (dsnet_rules.event_choice): owner / host / first answer / majority */
/* lan_v8 proof (dead vote): a decided choice is applied by the event flow inside the sim's tick, and the story popup's
   pause holds the sim at 0 ticks, so the choice never ran. Once a choice is decided, every peer lifts its popup pause
   (a vote: the same outcome everywhere); the sim runs, the flow applies the choice; when the next node shows, the
   pause is held again; when the event ends, the popup closes as usual. */
static uint32_t story_release; static int story_next_pause;
static int ev_rule(void) { dsnet_client *c = cli(); if (!c) return UI_EVC_OWNER; dsnet_rules r; dsnet_rules_get(c, &r); return r.event_choice; }
static int ev_chooser(void) {
    if (story.node && story.node == ev_node && story.handover) return 0;   /* lan_v10: 60 s with no answer: the host */
    return ui_story_chooser_rule(ev_rule(), unit_owner_or_none(h_ev_unit));
}
static double ev_open_t;                                     /* when the node on screen opened here (the 60 s countdown) */
/* review #28: non-choosers see the choices greyed; in a vote, your own choice is marked "(your vote)".
   _choiceButtons +0x60 List<SubUI_EventChoice> (_choiceButton +0x20 DASButton, _choiceText +0x28). */
static dis_list story_dis;
static char ev_choice_text[UI_MAX_CHOICES][256];
static uintptr_t ev_check[UI_MAX_CHOICES];                  /* your-vote check icons, one per choice button */
#define YOUR_VOTE "  <color=#58D68D>(your vote)</color>"
static void ev_choices_look(void) {
    Obj v = u_gc_get(h_ev_view);
    if (!on() || !u_alive(v) || !ev_node) return;
    Obj list = AT(v, 0x60, Obj), items = list ? AT(list, 0x10, Obj) : NULL;
    int n = list ? AT(list, 0x18, int32_t) : 0, ch = ev_chooser();
    int readonly = (spect() || (ch >= 0 && ch != me())) && !(story.node == ev_node && story.decided >= 0);
    int mine = story.node == ev_node && ch < 0 ? story.vote[me() & 31] : -1;
    if (n > UI_MAX_CHOICES) n = UI_MAX_CHOICES;
    for (int i = 0; i < n; i++) {
        Obj c = items ? AT(items, 0x20 + 8 * i, Obj) : NULL;
        if (!c) continue;
        Obj btn = AT(c, 0x20, Obj), tx = AT(c, 0x28, Obj);
        if (readonly && u_alive(btn) && AT(btn, 0xD8, uint8_t)) dis_apply(&story_dis, btn);
        if (!u_alive(tx)) continue;
        char cur[300]; str_utf8(tmp_get(tx), cur, sizeof cur);  /* (an older build's text mark: removed) */
        char *mark = strstr(cur, YOUR_VOTE);
        if (mark) { *mark = 0; line_text(tx, cur); }
        /* layout v2 §5d: your vote = the stock CheckBox_Selected icon at the choice's left edge */
        Obj ck = u_gc_get(ev_check[i]);
        if (i == mine && !u_alive(ck) && u_alive(btn)) {
            ck = img_make(u_tr(btn), sprite_get(SP_CHECK), 18, 18);
            if (ck) {
                Obj rt = u_tr(ck);
                F(0x5106B64, void (*)(float, float, Obj, void *))(0, 0.5f, rt, NULL);
                F(0x5106D48, void (*)(float, float, Obj, void *))(0, 0.5f, rt, NULL);
                F(0x51072F4, void (*)(float, float, Obj, void *))(1, 0.5f, rt, NULL);
                u_rt_set_pos(rt, -6, 0);
            }
            u_gc_free(ev_check[i]); ev_check[i] = u_gc_new(ck);
        }
        if (u_alive(ck) && u_go_active_self(u_go(ck)) != (i == mine)) u_go_active(u_go(ck), i == mine);
    }
    if (!readonly && story_dis.n) dis_restore(&story_dis);
}
static void ev_title_refresh(void) {
    Obj v = u_gc_get(h_ev_view);
    if (!on() || !u_alive(v) || !ev_node) return;
    ui_players p; players(&p);
    char line[200] = "", full[512];
    int ch = ev_chooser();
    /* lan_v10 (owner 04:10, ux_flow F3 / F11): every rule shows the 60 s countdown, then the host decides */
    int n = 0, all = 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) if (p.present >> i & 1u) all++;
    if (story.node == ev_node) n = ui_story_votes_in(&story, p.present);
    ui_story cur; if (story.node == ev_node) cur = story; else ui_story_reset(&cur, ev_node, ch);
    int left = UI_STORY_TIMEOUT_S - (int)(now_s() - ev_open_t); if (left < 0) left = 0;
    if (ch == 0 || cur.handover) left = -1;                  /* the host chooses anyway: no countdown */
    ui_story_line(&cur, &p, me(), ch, n, all, left, line, sizeof line);
    snprintf(full, sizeof full, "%s\n<size=70%%><color=#a69085>%s</color></size>", ev_title, line);
    tmp_set(AT(v, 0x40, Obj), full);
    ev_choices_look();
}
static void hk_ev_set_event(Obj v, Obj inst, void *mi) {
    o_ev_set_event(v, inst, mi);
    u_gc_free(h_ev_view); h_ev_view = u_gc_new(v);
    u_gc_free(h_ev_unit); h_ev_unit = 0;
    Obj arr = inst ? AT(inst, 0x30, Obj) : NULL;
    if (arr) {
        int n = AT(arr, 0x18, int32_t);
        for (int i = 0; i < n; i++) { Obj e = AT(arr, 0x20 + 8 * i, Obj); if (is_unit(e)) { h_ev_unit = u_gc_new(e); break; } }
    }
}
static void hk_ev_set_choices(Obj v, Obj list, void *mi) {
    o_ev_set_choices(v, list, mi);
    if (story_next_pause && on()) {                          /* the next node shows: pause everyone again (owner rule) */
        story_next_pause = 0;
        mp_session *s = sess();
        if (s) mp_popup_pause(s, MP_PAUSE_ESSENTIAL_POPUP, 1);
        story_pause(1);
        rig_log("ui: story: next node shown, pause held again");
    }
    u_gc_free(h_ev_view); h_ev_view = u_gc_new(v);
    Obj cur = AT(v, 0x68, Obj), items = cur ? AT(cur, 0x10, Obj) : NULL;
    int n = cur ? AT(cur, 0x18, int32_t) : 0;
    if (n > UI_MAX_CHOICES) n = UI_MAX_CHOICES;
    char keys[UI_MAX_CHOICES][96]; const char *kp[UI_MAX_CHOICES];
    for (int i = 0; i < n; i++) { str_utf8(items ? AT(items, 0x20 + 8 * i, Obj) : NULL, keys[i], sizeof keys[i]); kp[i] = keys[i]; }
    ev_node = ui_story_node_hash(kp, n);
    memset(ev_choice_text, 0, sizeof ev_choice_text);
    dis_restore(&story_dis);                                 /* a new node: the stock just built fresh buttons */
    str_utf8(tmp_get(AT(v, 0x40, Obj)), ev_title, sizeof ev_title);
    ev_title_refresh();
}
static void hk_ev_click(Obj v, int idx, void *mi) {
    mp_session *s = sess();
    if (!on() || !s) { o_ev_click(v, idx, mi); return; }
    if (story.node == ev_node && story.decided >= 0) return; /* already chosen; the host's click is on its way */
    int ch = ev_chooser();
    if (spect()) {                                           /* spectators never vote */
        ui_players p; players(&p); char nm[40];
        if (ch >= 0) notice("You are watching: %s is choosing", ui_player_label(&p, ch, nm)); else notice("You are watching: the players are voting");
        return;
    }
    ui_route r = ch >= 0 ? (ch == me() ? UI_ROUTE_STORY_CHOOSER : UI_ROUTE_STORY_READONLY) : UI_ROUTE_STORY_VOTE;
    if (r == UI_ROUTE_STORY_READONLY) {
        ui_players p; players(&p); char nm[40];
        notice("%s is choosing", ui_player_label(&p, ev_chooser(), nm));
        return;
    }
    uint8_t a[UI_STORY_ARGS]; static const uint8_t zero[16];
    ui_story_args_write(a, ev_node, idx);
    int rc = mp_order(s, UI_ORDER_STORY_VOTE, zero, a, sizeof a);
    rig_log("ui: story click %d on node %08x (chooser %d, rule %d): vote sent rc %d", idx, ev_node, ch, ev_rule(), rc);
    if (rc) notice("Your choice could not be sent");
    else if (r == UI_ROUTE_STORY_VOTE && ch == UI_STORY_VOTE) notice("Vote sent");
}
/* STORY_VOTE order handler: every peer, same step. Only the host presses the decided choice (its
   SelectChoiceRequested then travels as a normal host order, so every peer runs the same choice). */
/* majority timeout (host sends it 60 s after the node opened): the votes in decide (UI_ORDER_STORY_CLOSE {u32 node}) */
#define UI_ORDER_STORY_CLOSE 33
static uint32_t ev_open_node, ev_close_sent;
/* lan_v10: 60 s with no decision under any rule (host sends it): majority with votes in -> those decide; otherwise the
   host chooses (ui_story_timeout, the same on every peer: a command) */
static int story_close_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)guid;
    if (n < 4 || player != 0) return -1;
    uint32_t node = (uint32_t)a[0] | (uint32_t)a[1] << 8 | (uint32_t)a[2] << 16 | (uint32_t)a[3] << 24;
    ui_players p; players(&p);
    int d = ui_story_timeout(&story, node, ev_chooser(), p.present);
    rig_log("ui: story %08x: %d s with no decision: %s (choice %d)", node, UI_STORY_TIMEOUT_S, story.handover ? "the host chooses now" : "the votes in decide", d);
    if (d < 0 && story.handover && node == ev_node) { char hn[40]; ui_players q; players(&q); if (me() == 0) notice("No answer in 60 s: you choose for this event"); else notice("No answer in 60 s: %s chooses", ui_player_label(&q, 0, hn)); }
    ev_title_refresh();
    if (d >= 0) story_release = node;                        /* every peer: let the sim run so the choice applies */
    if (d >= 0 && dsnet_is_host(mp_client(s))) { story_click = d; story_click_node = node; }
    return 0;
}
static int story_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *args, uint16_t len) {
    (void)guid;
    uint32_t node; int choice;
    if (ui_story_args_read(args, len, &node, &choice)) return -1;
    ui_players p; players(&p);
    int d = ui_story_vote(&story, player, node, choice, p.present, ev_chooser());
    rig_log("ui: story vote from %d: node %08x choice %d -> decided %d", player, node, choice, d);
    if (d >= 0) story_release = node;                        /* every peer: let the sim run so the choice applies */
    if (d >= 0 && dsnet_is_host(mp_client(s))) { story_click = d; story_click_node = node; }
    return 0;
}
unsigned mp_game_session_gen(void);
static void story_frame(void) {
    mp_session *s = sess();
    static unsigned gen_seen;                                /* a new session can reuse the old one's address */
    if (s && (s != story_sess || mp_game_session_gen() != gen_seen)) {   /* new session: register the handlers once */
        gen_seen = mp_game_session_gen();
        rig_log("ui: order handlers registered (session %u)", gen_seen);
        mp_set_order_handler(s, UI_ORDER_STORY_VOTE, story_order);
        mp_set_order_handler(s, UI_ORDER_MAP_PROPOSE, map_propose_order);
        mp_set_order_handler(s, UI_ORDER_MAP_VOTE, map_vote_order);
        mp_set_order_handler(s, UI_ORDER_DEBUG_LEVELUP, levelup_order);
        mp_set_order_handler(s, UI_ORDER_DEBUG_EVENT, debug_event_order);
        mp_set_order_handler(s, UI_ORDER_DEBUG_GOLD, debug_gold_order);
        mp_set_order_handler(s, UI_ORDER_DEBUG_PICKUP, debug_pickup_order);
        mp_set_order_handler(s, UI_ORDER_RESULT_GO, result_go_order);
        mp_set_order_handler(s, UI_ORDER_RESULT_CANCEL, result_cancel_order);
        mp_set_order_handler(s, UI_ORDER_FOLLOW, follow_order);
        mp_set_order_handler(s, UI_ORDER_STORY_CLOSE, story_close_order);
        mp_set_order_handler(s, UI_ORDER_CAMERA, camera_order);
        mp_set_order_handler(s, UI_ORDER_ASK, ask_order);
        memset(followers, 0, sizeof followers); follow_target = -1;
        map_changing = 0;                                    /* never carried into a new session (lead 2026-10-14) */
        story_held = 0; ui_mapvote_clear(&mapv); prompt_id = 0;
        story_sess = s; ui_story_reset(&story, 0, -1);
    }
    if (!s) return;
    ui_players p; players(&p);
    if (story.node && story.decided < 0 && ui_story_recheck(&story, p.present) >= 0 && dsnet_is_host(mp_client(s))) {
        story_click = story.decided; story_click_node = story.node;   /* a voter left */
    }
    if (story_next_pause && !popup_active(22)) story_next_pause = 0;   /* the event ended: no re-pause for a later one */
    if (story_release && story_click < 0) {                  /* (the host first sends the choice, then lifts its pause) */
        story_release = 0;
        int n = 0; for (int k = 0; k < 8; k++) n += mp_popup_pause(s, MP_PAUSE_ESSENTIAL_POPUP, 0);
        if (story_held) story_pause(0);
        story_next_pause = 1;
        rig_log("ui: story choice decided: popup pause lifted so it applies (%d)", n);
    }
    /* lan_v10: every rule: after the timeout the host sends the close (never waiting on someone who cannot answer) */
    if (ev_node != ev_open_node) { ev_open_node = ev_node; ev_open_t = now_s(); }
    if (dsnet_is_host(mp_client(s)) && ev_node && popup_active(22) && !(story.node == ev_node && (story.decided >= 0 || story.handover)) && ev_close_sent != ev_node
        && ev_chooser() != 0 && now_s() - ev_open_t > UI_STORY_TIMEOUT_S) {
        uint8_t a[4] = { (uint8_t)ev_node, (uint8_t)(ev_node >> 8), (uint8_t)(ev_node >> 16), (uint8_t)(ev_node >> 24) }; static const uint8_t zero[16];
        ev_close_sent = ev_node;
        mp_order(s, UI_ORDER_STORY_CLOSE, zero, a, 4);
    }
    if (story_click >= 0) {
        Obj v = u_gc_get(h_ev_view);
        if (u_alive(v) && ev_node == story_click_node) {
            /* lan_v8 proof: the view's own click (OnClickChoice: UIInput 229 on its stream) never reached the input
               dispatcher while the story pause holds the sim, so nothing happened ("dead vote"). The host now sends the
               choice's own event, SelectChoiceRequested{string key +0x10}, through InputDispatcher.<Update>b__8_0
               (mf+0x68, 0xD3BAA4): ord captures it as a host order and every peer applies it at the same step. */
            Obj cur = AT(v, 0x68, Obj), items = cur ? AT(cur, 0x10, Obj) : NULL;
            Obj key = items && story_click < AT(cur, 0x18, int32_t) ? AT(items, 0x20 + 8 * story_click, Obj) : NULL;
            void *k = ord_debug_class ? ord_debug_class("Refactor.Main.Event", "SelectChoiceRequested") : NULL;
            Obj disp = mf ? AT(mf, 0x68, Obj) : NULL;
            char ks[96] = ""; str_utf8(key, ks, sizeof ks);
            if (k && key && disp) {
                Obj ev = F(0x6F96CC, Obj (*)(void *))(k);
                F(0x6F95C8, void (*)(Obj, void **, Obj))(ev, (void **)((char *)ev + 0x10), key);   /* write barrier */
                F(0xD3BAA4, void (*)(Obj, Obj, void *))(disp, ev, NULL);
                rig_log("ui: host sends story choice %d (%s)", story_click, ks);
            } else { rig_log("ui: host presses story choice %d (view click)", story_click); o_ev_click(v, story_click, NULL); }
        }
        else rig_log("ui: story choice %d for node %08x not pressed (node on screen %08x)", story_click, story_click_node, ev_node);
        story_click = -1;
    }
    static uint32_t shown_sig;
    uint32_t sig = ev_node ^ (uint32_t)ui_story_votes_in(&story, p.present) * 2654435761u ^ (uint32_t)(story.decided + 2) << 24 ^ p.present
                 ^ (uint32_t)(now_s() - ev_open_t) * 40503u ^ (uint32_t)story.handover << 30;   /* the countdown, once a second */
    if (sig != shown_sig) { shown_sig = sig; ev_title_refresh(); }
}

/* ui_result {action}: "state"; "return" = press the screen's Return button (Button.m_OnClick +0x100
   .Invoke 0x51171F4, the real click path); "confirm" = Yes on the confirmation box (CommonNoticeView.OnClick_Confirm). */
static char *ui_result_cmd(const char *line) {
    char act[32] = "state"; jstr(line, "action", act, sizeof act);
    Buf b = {0}; int rc = 0;
    if (!strcmp(act, "return") || !strcmp(act, "godown")) {
        static uintptr_t h_rv;
        Obj v = find_view(&h_rv, "Refactor.UI", "DungeonResultView");
        Obj btn = v ? AT(v, !strcmp(act, "return") ? 0x58 : 0x68, Obj) : NULL;
        Obj ev = btn ? AT(btn, 0x100, Obj) : NULL;
        if (ev) F(0x51171F4, void (*)(Obj, void *))(ev, NULL); else rc = -1;
    } else if (!strcmp(act, "confirm")) {
        u_gc_free(h_notice); h_notice = 0;                   /* look again: the box that is up now */
        Obj v = find_view(&h_notice, "Refactor.UI", "CommonNoticeView");
        rig_log("ui: confirm box %p active %d onConfirm %p", v, v ? u_go_active_self(u_go(v)) : -1, v ? AT(v, 0x80, Obj) : NULL);
        if (v) o_cn_confirm(v, NULL); else rc = -1;
    }
    if (strcmp(act, "state")) {
        static uintptr_t h_rv2;
        Obj rv = find_view(&h_rv2, "Refactor.UI", "DungeonResultView");
        rig_log("ui: ui_result %s: result view %p stage %d", act, rv, rv ? AT(rv, 0x98, int32_t) : -1);
    }
    bput(&b, "{\"ok\":%s,\"rc\":%d,\"open\":%d,\"active\":%d,\"chooser\":%d,\"me\":%d,\"story_held\":%d}", rc ? "false" : "true", rc,
         result_open, popup_active(3), result_chooser, me(), story_held);
    return b.p;
}

/* ================================================================ 7. demo-v2: pause panel, player list, world dim, local order */
/* 7a. The stock big Pause panel (PauseUIView: PauseUI GameObject +0x30, BackgroundImage +0x38; presenter +0x10 = view)
   follows the lockstep pause on every peer, with a line "Paused by <name>" (dsnet_pause_by). */
static uintptr_t h_pause_presenter, h_pause_line;
static void (*o_pause_speed)(Obj, Obj, void *);
static void hk_pause_speed(Obj presenter, Obj data, void *mi) {   /* PauseUIPresenter.OnSpeedChanged 0xF13234 */
    o_pause_speed(presenter, data, mi);
    if (u_gc_get(h_pause_presenter) != presenter) { u_gc_free(h_pause_presenter); h_pause_presenter = u_gc_new(presenter); }
}
static Obj type_of(const char *ns, const char *name) {
    void *k = ord_debug_class ? ord_debug_class(ns, name) : NULL;
    return k ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k)) : NULL;
}
static uintptr_t h_pause_view;
static int lobby_open(void);
static Obj pause_view(void) {                               /* presenter's view, else found by class */
    Obj pr = u_gc_get(h_pause_presenter), v = pr ? AT(pr, 0x10, Obj) : NULL;
    return u_alive(v) ? v : find_view(&h_pause_view, "Refactor.UI", "PauseUIView");
}
static void pause_frame(void) {
    if (!on()) return;
    Obj v = pause_view();
    if (!v) return;
    Obj go = AT(v, 0x30, Obj), bg = AT(v, 0x38, Obj);
    mp_stats st; mp_get_stats(sess(), &st);
    ui_players p; players(&p);
    dsnet_client *c = cli();
    int story = c && dsnet_story_pause_mask ? (dsnet_story_pause_mask(c) & p.present) != 0 : 0;
    int paused = (st.paused || story) && !lobby_open();     /* lead 2026-10-13: the lobby panel says it; no banner behind it */
    Obj pl0 = u_gc_get(h_pause_line);
    if (!paused && u_alive(pl0) && u_go_active_self(u_go(pl0))) u_go_active(u_go(pl0), false);
    if (u_go_active_self(go) != paused) {
        u_go_active(go, paused);
        if (u_alive(bg)) (ui_is(bg, K_BEH) ? F(0x50ECEA0, void (*)(Obj, bool, void *))(bg, paused, NULL) : (void)0);   /* Behaviour.set_enabled */
    }
    Obj line = u_gc_get(h_pause_line);
    if (!u_alive(line) && paused && u_alive(go)) {             /* clone a text of the panel, or our status line */
        Obj tr = F(0x50F300C, Obj (*)(Obj, void *))(go, NULL), tt = type_of("TMPro", "TMP_Text");
        Obj src = tt ? F(0x50EDDF4, Obj (*)(Obj, Obj, bool, void *))(tr, tt, true, NULL) : NULL;   /* GetComponentInChildren */
        if (!u_alive(src)) src = u_gc_get(h_status);
        if (u_alive(src)) {
            line = u_instantiate(src, u_alive(src) && src != u_gc_get(h_status) ? u_tr_parent(u_tr(src)) : tr);
            if (u_alive(line)) {
                V2 pos = u_rt_pos(u_tr(src)), sz = u_rt_size(u_tr(src));
                u_rt_set_pos(u_tr(line), pos.x, pos.y - 2.0f * sz.y - 22.0f);   /* owner 2026-10-13: right under the banner strip */
                u_gc_free(h_pause_line); h_pause_line = u_gc_new(line);
                rig_log("ui: pause line created from %s", cls_name(src));
            }
        }
    }
    if (u_alive(line) && paused) {
        if (!u_go_active_self(u_go(line))) u_go_active(u_go(line), true);   /* cloned from a hidden template */
        char l[300], cur[300], l0[300];
        /* review #20: the same text as the panel's status row (ui_pause_text); a hold ("Waiting for <name>
           (disconnected)") has no pause vote: then the status line */
        if (pause_text[0]) snprintf(l0, sizeof l0, "%s", pause_text);
        else if (status_text[0]) snprintf(l0, sizeof l0, "%s", status_text);
        else ui_pause_line(&p, c ? dsnet_pause_by(c) : -1, story, l0, sizeof l0);
        snprintf(l, sizeof l, "<size=70%%>%s</size>", l0);    /* smaller than the banner title */
        str_utf8(tmp_get(line), cur, sizeof cur);             /* compare with the text itself (a new clone starts with the panel's) */
        if (strcmp(l, cur)) tmp_set(line, l);
    }
}

/* 7b. Player panel, the owner-approved board (2026-10-13, artifact "In-game player panel"): 300 wide, top of the screen
   under the portrait bar. Frame = the notice box's Img_BackGroundBox (alpha 0.92). Header 34: flat #2d2a31 with a
   #633d33 rule, "Players (n)" in Cinzel 17 small caps, a 26x26 settings button (the stock top-line frame with the
   game's Cog Wheel item icon) and a 26x26 collapse button (frame + the stock ListToggle chevron). Under it an optional
   status row (Lora 16 #d9be8f), then one 32-high row per player on alternating #36323a / #2b282f: owner-colour dot,
   name (Lora 16), on the right "You" (Lora 12 muted) or the 3-bar connection indicator, or a red X (disconnected).
   Collapsed: the header only, with the worst connection in it. The cog swaps the rows for the settings rows
   (Lora 14 muted label, < value > on the right): Other units see-through, Names above units; host: Units per player,
   Admin mode. Hidden while the lobby, the character window, a popup or the disconnect box is up. */
enum { PL_HDR, PL_BODY, PL_SEE, PL_NAMES, PL_ADMIN, PL_STATUS, PL_NOTE, PL_BODYR, PL_SEER, PL_NAMESR, PL_ADMINR, PL_UNITS, PL_UNITSR,
       PL_TIPS, PL_TIPSR, PL_HINT, PL_CFADE, PL_CFADER, PL_CJOIN, PL_CJOINR, PL_CHAT, PL_CHATR, PL_SIZE, PL_SIZER,
       /* lan_v10: co-op markers, outline, other players' colours (7 rows: players 2-8 of a full game) */
       PL_PINGS, PL_PINGSR, PL_PSOUND, PL_PSOUNDR, PL_PCHAT, PL_PCHATR, PL_VIEWS, PL_VIEWSR, PL_OFFS, PL_OFFSR, PL_OUTL, PL_OUTLR,
       PL_SPECT, PL_SPECTR,                                  /* host: spectators allowed (owner 2026-10-14) */
       PL_MINI, PL_MINIR, PL_MSIZE, PL_MSIZER,               /* spectator: mini overview shown / its size (8k) */
       PL_COL0, PL_COLR0 = PL_COL0 + 7, PL_N = PL_COLR0 + 7 };
#define PL_SETMAX 24                                         /* settings rows at most (each: label, value, < >) */
static uintptr_t h_pl[PL_N], h_plate;                      /* h_plate: the panel frame */
static uintptr_t h_skill_ui, h_status_ui;                    /* character window views (8d) */
static int pl_written[PL_N];                                 /* row text set by us at least once since it was cloned */
static void chat_get(int *fade, int *join, int *en); static void chat_step(int row, int dir);
static float pl_y0;
static int pl_anchor_bar, pl_pop;                            /* pl_pop: the cog is pressed (settings rows shown) */
static float pl_cur_L, pl_cur_Y; static int pl_shown, pl_hide_why, cog_has_icon;       /* where the panel is now (lan_v8 drag) */
static int row_player[19];                                   /* player of each row (connection tooltip; PL_MAXROWS + 1) */
static int row_lvl[19];                                      /* its connection level (ui_conn_level; -2 = left, -3 = "Watching" divider) */
static int row_watch[19];                                    /* the row is a spectator (no Follow / look menu: they send no view) */
typedef struct { float x, y, w, h; } RectF4;
static uintptr_t h_bar_view;
static float bar_bottom_y(Obj root) {                       /* y just under the unit portrait bar (canvas root space) */
    Obj v = find_view(&h_bar_view, "Refactor.UI", "PlayerUnitShortCutView");
    Obj bar = v ? AT(v, 0x30, Obj) : NULL;                                                  /* _unitShortCutParent */
    if (!u_alive(bar) || !u_alive(root)) return 0;
    V3 bp = F(0x5109A08, V3 (*)(Obj, void *))(bar, NULL), bs = F(0x510F4F8, V3 (*)(Obj, void *))(bar, NULL);
    RectF4 br = (ui_is(bar, K_RT) ? F(0x5106978, RectF4 (*)(Obj, void *))(bar, NULL) : (RectF4){0,0,0,0});
    V3 rp = F(0x5109A08, V3 (*)(Obj, void *))(root, NULL), rs = F(0x510F4F8, V3 (*)(Obj, void *))(root, NULL);
    RectF4 rr = (ui_is(root, K_RT) ? F(0x5106978, RectF4 (*)(Obj, void *))(root, NULL) : (RectF4){0,0,0,0});
    if (rs.y == 0) return 0;
    return (bp.y + br.y * bs.y - (rp.y + (rr.y + rr.h) * rs.y)) / rs.y;                    /* <= 0: below the top edge */
}
#define PL_TOP   196.0f
#define PL_X     (-300.0f)
#define PL_WIDTH 420.0f
#define PL_LEFT  (PL_X - PL_WIDTH / 2 - 12)                  /* frame's left edge (canvas units from the top centre) */
#define PL_W     300.0f
#define PL_PAD   10.0f
#define PL_HDR_H 34.0f
#define PL_ROW_H 32.0f
#define PL_SET_H 28.0f
#define PL_MAXROWS 18                                        /* players (8), left players, "Watching (n)" + spectators (8) */
static int lobby_open(void) {                                /* review #30: hidden behind mpmenu's lobby panel */
    static int (*fn)(void); static unsigned tries;
    if (!fn && tries++ % 50 == 0) fn = (int (*)(void))dlsym(RTLD_DEFAULT, "mpmenu_lobby_open");
    return fn ? fn() : 0;
}
static uintptr_t h_hdrbg, h_hdrline, h_gear, h_cog, h_tog, h_chev, h_lora, h_cinzel, h_rowbg[PL_MAXROWS], h_bars[PL_MAXROWS + 1][3], h_set_arrow[2 * PL_SETMAX];
static uintptr_t h_band, h_arrow;                            /* (probes: header background / collapse button) */
static const char *pl_dot = "•";
static Obj tr_find(Obj t, const char *path) { return t ? F(0x510F3C0, Obj (*)(Obj, Obj, void *))(t, u_str(path), NULL) : NULL; }
static void tmp_style(Obj t, Obj font, float size, int align) {
    if (!ui_is(t, K_TMP)) return;
    if (u_alive(font)) F(0x4F0A5A4, void (*)(Obj, Obj, void *))(t, font, NULL);        /* TMP_Text.font */
    F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, false, NULL);                         /* enableAutoSizing off */
    F(0x4F0B340, void (*)(Obj, float, void *))(t, size, NULL);                         /* fontSize */
    F(0x4F0BAB4, void (*)(Obj, bool, void *))(t, false, NULL);                         /* no wrapping */
    F(0x4F0B7BC, void (*)(Obj, int, void *))(t, align, NULL);                          /* alignment */
    tmp_plain(t);
}
static void rt_top_left(Obj rt) {                            /* anchor top centre of the canvas, pivot top-left */
    if (!ui_is(rt, K_RT)) return;
    F(0x5106B64, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL);
    F(0x5106D48, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL);
    F(0x51072F4, void (*)(float, float, Obj, void *))(0, 1, rt, NULL);
}
static void rt_centre_in_parent(Obj rt) {
    if (!ui_is(rt, K_RT)) return;
    F(0x5106B64, void (*)(float, float, Obj, void *))(0.5f, 0.5f, rt, NULL);
    F(0x5106D48, void (*)(float, float, Obj, void *))(0.5f, 0.5f, rt, NULL);
    F(0x51072F4, void (*)(float, float, Obj, void *))(0.5f, 0.5f, rt, NULL);
    u_rt_set_pos(rt, 0, 0);
}
static void child_off(Obj tr, const char *name) { Obj c = tr_find(tr, name); if (c) u_go_active(u_go(c), false); }
static float tmp_pref_w(Obj t) {                             /* TMP_Text.preferredWidth (virtual slot 84) */
    if (!ui_is(t, K_TMP)) return 0;
    void *k = *(void **)t;
    return (*(float (**)(Obj, void *))((char *)k + 0x678))(t, *(void **)((char *)k + 0x680));
}
static void plist_set(int i, const char *txt) {
    Obj t = u_gc_get(h_pl[i]); char cur[3000];
    if (!u_alive(t)) return;
    str_utf8(tmp_get(t), cur, sizeof cur);
    if (strcmp(cur, txt) || !pl_written[i]) {
        tmp_set(t, txt); pl_written[i] = 1;
        /* the rows are clones of the clock's DASText: its Awake (first activation) re-applies _text (+0x7D0, "12:34")
           over ours (runs 54-58: settings rows read "12:34"); keep _text = ours */
        if (!strcmp(cls_name(t), "DASText")) F(0x6F95C8, void (*)(Obj, void **, Obj))(t, (void **)((char *)t + 0x7D0), u_str(txt));
    }
}
static void show(Obj o, int v) { if (u_alive(o) && u_go_active_self(u_go(o)) != v) u_go_active(u_go(o), v); }
static void place(Obj o, float x, float y, float w, float h) { if (u_alive(o)) { Obj rt = u_tr(o); u_rt_set_pos(rt, x, y); u_rt_set_size(rt, w, h); } }
static Obj frame_clone(Obj fsrc, Obj root) {                 /* notice box Img_BackGroundBox: buttons off, alpha 0.92 */
    Obj fr = u_instantiate(fsrc, root);                      /* a Transform: tint its Image, never the Transform */
    if (!u_alive(fr)) return NULL;
    child_off(fr, "CancelButton"); child_off(fr, "ConfirmButton");
    rt_top_left(fr);
    Obj it = type_of("UnityEngine.UI", "Image"), fimg = it ? F(0x50EDDF4, Obj (*)(Obj, Obj, bool, void *))(fr, it, true, NULL) : NULL;
    if (u_alive(fimg) && u_tr(fimg) == fr) { Col fc = { 1, 1, 1, 0.92f }; graphic_color(fimg, fc); }
    return fr;
}
static Obj flat(Obj root, const char *hex) { Obj im = img_make(root, NULL, 10, 10); if (im) graphic_color(im, hex_col(hex, 1)); return im; }
/* lan_v9: our windows live in a layer above the game's windows: a zero-size object at the HUD canvas's top centre with
   its own Canvas (overrideSorting, order PL_SORT) and GraphicRaycaster (clicks on our boxes do not reach the world).
   Inside it, the panel's own group: its scale is the "Panel size" (Compact 0.75). Tooltips, the lend menu and the viewer
   box use the layer (never compact). */
#define PL_SORT 5000
static Obj layer_make(Obj tmpl, Obj parent, int canvas) {
    Obj l = u_instantiate(tmpl, parent);
    if (!ui_is(l, K_TMP)) return NULL;
    tmp_set(l, ""); rt_top_left(u_tr(l)); u_rt_set_size(u_tr(l), 0, 0); u_rt_set_pos(u_tr(l), 0, 0); u_go_active(u_go(l), true);
    (ui_is(l, K_BEH) ? F(0x50ECEA0, void (*)(Obj, bool, void *))(l, false, NULL) : (void)0);   /* its own text off (no "12:34") */
    if (canvas) {
        Obj tc = type_of("UnityEngine", "Canvas"), tg = type_of("UnityEngine.UI", "GraphicRaycaster");
        Obj cv = tc ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(l), tc, NULL) : NULL;   /* GameObject.AddComponent */
        int order = canvas == 1 ? PL_SORT : canvas;          /* lan_v10: the markers layer passes its own order (no raycaster) */
        if (cv) { F(0x5476208, void (*)(Obj, bool, void *))(cv, true, NULL); F(0x547640C, void (*)(Obj, int, void *))(cv, order, NULL); }
        if (cv && tg && canvas == 1) F(0x50F2B90, Obj (*)(Obj, Obj, void *))(u_go(l), tg, NULL);
        rig_log("ui: %s layer %p canvas %p (sorting order %d)", canvas == 1 ? "top" : "markers", l, cv, order);   /* "top layer": uxgame_pair's check */
    }
    return u_tr(l);
}
static Obj layer_of_panel(void) { Obj hdr = u_gc_get(h_pl[PL_HDR]); return u_alive(hdr) ? u_tr_parent(u_tr(hdr)) : NULL; }
static Obj layer_top(void) { Obj g = layer_of_panel(); return g ? u_tr_parent(g) : NULL; }
int ui_panel_compact(void) { settings_load(); return uiset.compact; }   /* teamsfinish (dlsym): the Teams button follows it */
/* the cog's rows, in order (lan_v10: + co-op markers, outline, other players' colours; host rows last). arg = the
   player of a colour row, else -1. Returns the count. */
static int pl_is_label(int i) { return i == PL_SEE || i == PL_NAMES || i == PL_ADMIN || i == PL_UNITS || i == PL_TIPS || i == PL_CFADE || i == PL_CJOIN || i == PL_CHAT || i == PL_SIZE
                                     || i == PL_PINGS || i == PL_PSOUND || i == PL_PCHAT || i == PL_VIEWS || i == PL_OFFS || i == PL_OUTL || i == PL_SPECT || i == PL_MINI || i == PL_MSIZE || (i >= PL_COL0 && i < PL_COL0 + 7); }
static int pl_is_value(int i) { return i == PL_SEER || i == PL_NAMESR || i == PL_UNITSR || i == PL_ADMINR || i == PL_TIPSR || i == PL_CFADER || i == PL_CJOINR || i == PL_CHATR || i == PL_SIZER
                                     || i == PL_PINGSR || i == PL_PSOUNDR || i == PL_PCHATR || i == PL_VIEWSR || i == PL_OFFSR || i == PL_OUTLR || i == PL_SPECTR || i == PL_MINIR || i == PL_MSIZER || (i >= PL_COLR0 && i < PL_COLR0 + 7); }
static int set_rows(int *lab, int *val, int *arg) {
    static const int fixed[][2] = { { PL_SEE, PL_SEER }, { PL_NAMES, PL_NAMESR }, { PL_OUTL, PL_OUTLR }, { PL_TIPS, PL_TIPSR }, { PL_SIZE, PL_SIZER },
        { PL_CFADE, PL_CFADER }, { PL_CJOIN, PL_CJOINR }, { PL_CHAT, PL_CHATR },
        { PL_PINGS, PL_PINGSR }, { PL_PSOUND, PL_PSOUNDR }, { PL_PCHAT, PL_PCHATR }, { PL_VIEWS, PL_VIEWSR }, { PL_OFFS, PL_OFFSR } };
    int n = 0;
    for (size_t k = 0; k < sizeof fixed / sizeof fixed[0]; k++) { lab[n] = fixed[k][0]; val[n] = fixed[k][1]; arg[n++] = -1; }
    if (spect()) { lab[n] = PL_MINI; val[n] = PL_MINIR; arg[n++] = -1; lab[n] = PL_MSIZE; val[n] = PL_MSIZER; arg[n++] = -1; }
    ui_players p; players(&p); int c = 0;
    for (int i = 0; i < UI_MAX_PLAYERS && c < 7; i++) if (i != me() && (p.present >> i & 1u)) { lab[n] = PL_COL0 + c; val[n] = PL_COLR0 + c; arg[n++] = i; c++; }
    if (me() == 0) { lab[n] = PL_UNITS; val[n] = PL_UNITSR; arg[n++] = -1; lab[n] = PL_ADMIN; val[n] = PL_ADMINR; arg[n++] = -1;
                     lab[n] = PL_SPECT; val[n] = PL_SPECTR; arg[n++] = -1; }
    return n;
}
static int plist_style(void) {
    static double next; static uintptr_t h_quest, h_noticev;
    Obj hdr = u_gc_get(h_pl[PL_HDR]);
    if (u_alive(u_gc_get(h_plate)) && u_alive(u_gc_get(h_gear))) return 1;
    if (!u_alive(hdr) || now_s() < next) return 0;
    next = now_s() + 2;
    Obj qv = find_view(&h_quest, "Refactor.UI", "QuestUIView"), nv = find_view(&h_noticev, "Refactor.UI", "CommonNoticeView");
    Obj qsrc = qv ? tr_find(u_tr(qv), "SubUI_QuestSlot(Clone)/Panel_Title") : NULL;
    Obj fsrc = nv ? tr_find(u_tr(nv), "Panel_Notice/Img_BackGroundBox") : NULL;
    Obj spf = sprite_get(SP_FRAME1), spc = sprite_get(SP_COG), spl = sprite_get(SP_LIST_ON);
    /* the cog icon may be missing for a whole floor (run 53, after a rejoin): after 3 tries the panel is built without
       it (plain frame button; plist frame puts the icon in as soon as the sprite is loaded again) */
    static int cog_tries;
    if (!spc && qsrc && fsrc && spf && spl && ++cog_tries >= 1) rig_log("ui: panel built without the cog icon (ITEM_CogWheel not loaded)");
    else if (!qsrc || !fsrc || !spf || !spc || !spl) { rig_log("ui: panel parts: quest %p frame %p sprites frame1 %p cog %p list %p (retry)", qsrc, fsrc, spf, spc, spl); return 0; }
    cog_tries = 0;
    Obj root = u_tr_parent(u_tr(hdr));
    Obj tt = type_of("TMPro", "TMP_Text"), ttl = tt ? F(0x50EDDF4, Obj (*)(Obj, Obj, bool, void *))(qsrc, tt, true, NULL) : NULL;
    Obj lora = ui_is(ttl, K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(ttl, NULL) : NULL;   /* Lora-Regular SDF */
    Obj pv = pause_view(), pgo = pv ? AT(pv, 0x30, Obj) : NULL;                              /* "Game Paused": Cinzel-Bold */
    Obj ptx = pgo && tt ? F(0x50EDDF4, Obj (*)(Obj, Obj, bool, void *))(F(0x50F300C, Obj (*)(Obj, void *))(pgo, NULL), tt, true, NULL) : NULL;
    Obj cinzel = ui_is(ptx, K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(ptx, NULL) : NULL;
    u_gc_free(h_lora); h_lora = u_gc_new(lora); u_gc_free(h_cinzel); h_cinzel = u_gc_new(cinzel);
    {   /* the stock window body text material: the notice box's Txt_Notice (Lora 16), read with fontSharedMaterial (slot 67) */
        Obj nt = tt ? tr_find(u_tr(nv), "Panel_Notice/Txt_Notice") : NULL, ntt = nt ? F(0x50EDDF4, Obj (*)(Obj, Obj, bool, void *))(nt, tt, true, NULL) : NULL;
        Obj src = ui_is(ntt, K_TMP) ? ntt : ttl;
        if (ui_is(src, K_TMP)) {
            void *k = *(void **)src;
            Obj m = (*(Obj (**)(Obj, void *))((char *)k + 0x568))(src, *(void **)((char *)k + 0x570));
            Obj sf = F(0x4F0A59C, Obj (*)(Obj, void *))(src, NULL);
            char mn[96] = ""; if (u_alive(m)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(m, NULL), mn, sizeof mn);
            u_gc_free(h_plain_mat); h_plain_mat = u_gc_new(m); u_gc_free(h_lora_font_ref); h_lora_font_ref = u_gc_new(sf);
            rig_log("ui: plain text material: %s (from %s)", mn, src == ntt ? "the notice box body text" : "the quest title");
        }
    }
    if (u_alive(lora) && F(0x4EEE580, bool (*)(Obj, uint16_t, bool, bool, void *))(lora, 0x25CF, true, false, NULL)) pl_dot = "●";
    int first = 1 << 30;                                     /* the frame and backgrounds under the texts */
    for (int i = 0; i < PL_N; i++) { Obj t = u_gc_get(h_pl[i]); if (u_alive(t) && u_sibling(u_tr(t)) < first) first = u_sibling(u_tr(t)); }
    Obj fr = frame_clone(fsrc, root);
    Obj hbg = flat(root, "#2d2a31"), hln = flat(root, "#633d33");
    Obj rbg[PL_MAXROWS];
    for (int r = 0; r < PL_MAXROWS; r++) rbg[r] = flat(root, r % 2 ? "#2b282f" : "#36323a");
    int sib = first;
    if (u_alive(fr)) u_set_sibling(u_tr(fr), sib++);
    if (u_alive(hbg)) u_set_sibling(u_tr(hbg), sib++);
    if (u_alive(hln)) u_set_sibling(u_tr(hln), sib++);
    for (int r = 0; r < PL_MAXROWS; r++) if (u_alive(rbg[r])) u_set_sibling(u_tr(rbg[r]), sib++);
    /* buttons, bars and arrows after the texts (drawn above them) */
    Obj gear = img_make(root, spf, 26, 26), cog = gear ? img_make(u_tr(gear), spc, 18, 18) : NULL;
    if (cog) rt_centre_in_parent(u_tr(cog));
    Obj tog = img_make(root, spf, 26, 26), chev = tog ? img_make(u_tr(tog), spl, 18, 18) : NULL;
    if (chev) rt_centre_in_parent(u_tr(chev));
    for (int r = 0; r <= PL_MAXROWS; r++) for (int b = 0; b < 3; b++) { Obj im = img_make(root, NULL, 4, 5); u_gc_free(h_bars[r][b]); h_bars[r][b] = u_gc_new(im); show(im, 0); }
    for (int k = 0; k < 2 * PL_SETMAX; k++) { Obj a = img_make(root, sprite_get(k % 2 ? SP_ARROW_NEXT : SP_ARROW_PREV), 20, 20); u_gc_free(h_set_arrow[k]); h_set_arrow[k] = u_gc_new(a); show(a, 0); }
    u_gc_free(h_plate); h_plate = u_gc_new(fr); u_gc_free(h_hdrbg); h_hdrbg = u_gc_new(hbg); u_gc_free(h_hdrline); h_hdrline = u_gc_new(hln);
    for (int r = 0; r < PL_MAXROWS; r++) { u_gc_free(h_rowbg[r]); h_rowbg[r] = u_gc_new(rbg[r]); }
    u_gc_free(h_gear); h_gear = u_gc_new(gear); u_gc_free(h_cog); h_cog = u_gc_new(cog);
    u_gc_free(h_tog); h_tog = u_gc_new(tog); u_gc_free(h_chev); h_chev = u_gc_new(chev);
    u_gc_free(h_band); h_band = u_gc_new(hbg); u_gc_free(h_arrow); h_arrow = u_gc_new(tog);
    for (int i = 0; i < PL_N; i++) {
        Obj t = u_gc_get(h_pl[i]);
        int value = pl_is_value(i), set = pl_is_label(i) || i == PL_HINT;
        if (i == PL_HDR) tmp_style(t, u_alive(cinzel) ? cinzel : lora, 17, 513);
        else tmp_style(t, lora, i == PL_BODYR ? 12 : set || value || i == PL_NOTE ? 14 : 16, value ? 514 : i == PL_BODYR ? 516 : 513);   /* Left / Center / Right */
        if (u_alive(t)) u_set_sibling(u_tr(t), 1 << 20);
        pl_written[i] = 0;
    }
    for (int r = 0; r <= PL_MAXROWS; r++) for (int b = 0; b < 3; b++) { Obj im = u_gc_get(h_bars[r][b]); if (u_alive(im)) u_set_sibling(u_tr(im), 1 << 20); }
    rig_log("ui: player panel (board) built: frame %p gear %p toggle %p lora %p cinzel %p dot %s", fr, gear, tog, lora, cinzel, pl_dot);
    return 1;
}
/* 3 bars (4 wide; 5 / 9 / 14 tall; 3 apart; bottom-aligned), lit in the level's colour, unlit grey; level 0 = all grey */
static void bars_put(int slot, int level, float x, float ytop) {
    static const float bh_[3] = { 5, 9, 14 };
    for (int b = 0; b < 3; b++) {
        Obj im = u_gc_get(h_bars[slot][b]);
        if (!u_alive(im)) continue;
        show(im, 1); place(im, x + 7.0f * (float)b, ytop - (14.0f - bh_[b]), 4, bh_[b]);
        graphic_color(im, hex_col(level >= b + 1 ? ui_conn_color(level) : "#5d5860", 1));
    }
}
static void bars_hide(int slot) { for (int b = 0; b < 3; b++) show(u_gc_get(h_bars[slot][b]), 0); }
static void plist_frame(void) {
    Obj st = u_gc_get(h_status);
    if (!u_alive(st)) return;
    settings_load();
    if (!u_alive(u_gc_get(h_pl[PL_HDR]))) {
        Obj cv = ui_is(st, K_GR) ? F(0x5260544, Obj (*)(Obj, void *))(st, NULL) : NULL;      /* Graphic.canvas */
        if (cv) cv = F(0x5476FAC, Obj (*)(Obj, void *))(cv, NULL);                           /* Canvas.rootCanvas */
        Obj root = cv ? u_tr(cv) : u_tr_parent(u_tr(st));
        Obj top = layer_make(st, root, 1), grp = top ? layer_make(st, top, 0) : NULL;   /* lan_v9: above the game's windows */
        if (grp) root = grp;
        pl_anchor_bar = 1; pl_y0 = -PL_TOP;
        for (int i = 0; i < PL_N; i++) {
            Obj l = u_instantiate(st, root);
            if (!ui_is(l, K_TMP)) return;
            rt_top_left(u_tr(l));
            u_rt_set_size(u_tr(l), PL_W, 22);
            u_rt_set_pos(u_tr(l), PL_LEFT + PL_PAD, pl_y0);
            u_go_active(u_go(l), false); tmp_set(l, ""); pl_written[i] = 0;   /* never the template's "12:34" */
            u_gc_free(h_pl[i]); h_pl[i] = u_gc_new(l);
        }
    }
    Obj pc = u_gc_get(h_popups), csv = u_gc_get(h_status_ui), ckv = u_gc_get(h_skill_ui);
    int stock_window = (pc && F(0xF62338, bool (*)(Obj, void *))(pc, NULL))
        || (u_alive(csv) && F(0x50F3724, bool (*)(Obj, void *))(u_go(csv), NULL)) || (u_alive(ckv) && F(0x50F3724, bool (*)(Obj, void *))(u_go(ckv), NULL));
    static int (*teams_modal)(void); static unsigned tm_tries;   /* teamsfinish: Teams window / proposal summary up */
    if (!teams_modal && tm_tries++ % 50 == 0) teams_modal = (int (*)(void))dlsym(RTLD_DEFAULT, "ui_teams_modal_open");
    (void)stock_window;                                      /* lan_v9 owner: the panel stays above the game's windows */
    int hide = !on() || lobby_open() || popup_active(1) || left_prompt || (teams_modal && teams_modal())   /* Esc menu: design #3 */
               || busy();
    pl_hide_why = !on() | lobby_open() << 1 | popup_active(1) << 2 | (left_prompt != 0) << 3 | (teams_modal && teams_modal()) << 4 | busy() << 5;                                    /* lan_v9: not on the loading screen (v2_v9_changing_map_guest.png) */
    { Obj g = layer_of_panel(); if (g) { float k = uiset.compact ? 0.75f : 1.0f; V3 cur = F(0x510AEFC, V3 (*)(Obj, void *))(g, NULL);
        if (cur.x != k) F(0x510AFF8, void (*)(Obj, V3, void *))(g, (V3){ k, k, 1 }, NULL); } }
    /* the group scales around the layer's origin: shift it so the panel's top-left corner stays where it is (pos_x / pos_y
       stay in layer units, so drag, saved place and ui_panel_anchor mean the same in Normal and Compact) */
    int styled = hide ? 0 : plist_style();
    if (hide || !styled) {
        pl_shown = 0;
        for (int i = 0; i < PL_N; i++) show(u_gc_get(h_pl[i]), 0);
        Obj every[] = { u_gc_get(h_plate), u_gc_get(h_hdrbg), u_gc_get(h_hdrline), u_gc_get(h_gear), u_gc_get(h_tog) };
        for (size_t k = 0; k < sizeof every / sizeof every[0]; k++) show(every[k], 0);
        for (int r = 0; r < PL_MAXROWS; r++) show(u_gc_get(h_rowbg[r]), 0);
        for (int r = 0; r <= PL_MAXROWS; r++) bars_hide(r);
        for (int k = 0; k < 2 * PL_SETMAX; k++) show(u_gc_get(h_set_arrow[k]), 0);
        if (!on()) for (int i = 0; i < PL_N; i++) plist_set(i, "");
        if (hide) pl_pop = 0;
        return;
    }
    dsnet_client *c = cli(); ui_players p; players(&p);
    mp_stats ms; mp_get_stats(sess(), &ms);
    /* rows: present players, then players that left and are waited for (red X) */
    char bl[2600] = "<line-height=32>", br[1200] = "<line-height=32>", row[256], rr[128];
    size_t nl = strlen(bl), nr = strlen(br); int np = 0, rows = 0, lvl[PL_MAXROWS], worst = 3;
    memset(row_watch, 0, sizeof row_watch);
    for (int i = 0; i < UI_MAX_PLAYERS && rows < PL_MAXROWS; i++) {
        if (!(p.present >> i & 1u)) continue;
        np++;
        int ping = -1, behind = 0, state = -1, ns = 0;
        if (dsnet_player_net(c, i, &ping, &behind, &ns) == 0)
            state = ns == DSNET_NET_IN_SYNC ? 0 : ns == DSNET_NET_LOADING ? 1 : ns == DSNET_NET_RESYNCING ? 2 : -1;
        if (i == me()) state = ms.desynced ? 2 : (ms.loading || ms.needs_state) ? 1 : 0;
        char nm[40];
        snprintf(row, sizeof row, "<color=%s>%s</color> <color=#ccbeb8>%s</color>", ui_player_color(i), pl_dot, ui_player_label(&p, i, nm));
        lvl[rows] = i == me() ? -1 : ui_conn_level(ping, behind, state); row_lvl[rows] = lvl[rows];
        row_player[rows] = i;
        if (lvl[rows] >= 0 && lvl[rows] < worst) worst = lvl[rows];
        snprintf(rr, sizeof rr, "%s", i == me() ? "<color=#a69085>You</color>" : " ");
        nl += (size_t)snprintf(bl + nl, sizeof bl - nl, "%s%s", rows ? "\n" : "", row);
        nr += (size_t)snprintf(br + nr, sizeof br - nr, "%s%s", rows ? "\n" : "", rr);
        rows++;
    }
    { int nh = dsnet_hold_count(c);
      for (int k = 0; k < nh && rows < PL_MAXROWS; k++) {
          int reason = 0; uint8_t uid[16]; char nm[32] = "";
          if (dsnet_hold_get(c, k, &reason, uid, nm) != 0 || reason != DSNET_HOLD_PLAYER_LEFT) continue;
          snprintf(row, sizeof row, "<color=#5d5860>%s</color> <color=#a69085>%s</color>", pl_dot, nm[0] ? nm : "a player");
          lvl[rows] = -2; worst = 0; row_player[rows] = -1; row_lvl[rows] = -2;
          nl += (size_t)snprintf(bl + nl, sizeof bl - nl, "%s%s", rows ? "\n" : "", row);
          nr += (size_t)snprintf(br + nr, sizeof br - nr, "%s<color=#f16071>X</color>", rows ? "\n" : "");
          rows++;
      } }
    {   /* spectators (owner 2026-10-14): a "Watching (n)" divider, then one grey row each with its connection bars */
        int ns = 0; for (int i = 0; i < UI_MAX_PLAYERS; i++) ns += ui_spectators >> i & 1u;
        if (ns && rows < PL_MAXROWS) {
            snprintf(row, sizeof row, "<size=86%%><smallcaps><color=#a69085>Watching (%d)</color></smallcaps></size>", ns);
            lvl[rows] = -3; row_player[rows] = -1; row_lvl[rows] = -3;
            nl += (size_t)snprintf(bl + nl, sizeof bl - nl, "%s%s", rows ? "\n" : "", row);
            nr += (size_t)snprintf(br + nr, sizeof br - nr, "%s ", rows ? "\n" : "");
            rows++;
            for (int i = 0; i < UI_MAX_PLAYERS && rows < PL_MAXROWS; i++) {
                if (!(ui_spectators >> i & 1u)) continue;
                char snm[32] = ""; int ping = -1, sst = 0;
                dsnet_spectator_info(c, i, snm, &ping, &sst);
                int cs = sst == DSNET_NET_IN_SYNC ? 0 : sst == DSNET_NET_LOADING ? 1 : sst == DSNET_NET_RESYNCING ? 2 : -1;
                if (i == me()) cs = ms.desynced ? 2 : (ms.loading || ms.needs_state) ? 1 : 0;
                snprintf(row, sizeof row, "<color=%s>%s</color> <color=%s>%s</color>", UI_SPECTATOR_COLOR, pl_dot, UI_SPECTATOR_COLOR, snm[0] ? snm : "A spectator");
                lvl[rows] = i == me() ? -1 : ui_conn_level(ping, 0, cs); row_lvl[rows] = lvl[rows];   /* never the header's worst */
                row_player[rows] = i; row_watch[rows] = 1;
                nl += (size_t)snprintf(bl + nl, sizeof bl - nl, "\n%s", row);
                nr += (size_t)snprintf(br + nr, sizeof br - nr, "\n%s", i == me() ? "<color=#a69085>You</color>" : " ");
                rows++;
            }
        }
        for (int r = rows; r < PL_MAXROWS; r++) { row_player[r] = -1; row_lvl[r] = -3; }
    }
    char hdr[64], h2[128];
    ui_list_header(np, uiset.list_open, hdr, sizeof hdr);
    snprintf(h2, sizeof h2, "<smallcaps><color=#ccbeb8>%s</color></smallcaps>", hdr);
    plist_set(PL_HDR, h2);
    plist_set(PL_BODY, bl); plist_set(PL_BODYR, br);
    int host = me() == 0;
    plist_set(PL_SEE, "<color=#a69085>Other units see-through</color>");
    plist_set(PL_NAMES, "<color=#a69085>Names above units</color>");
    plist_set(PL_UNITS, "<color=#a69085>Units per player</color>");
    plist_set(PL_ADMIN, "<color=#a69085>Admin mode (host)</color>");
    snprintf(row, sizeof row, "<color=#ccbeb8>%d%%</color>", uiset.see_through); plist_set(PL_SEER, row);
    snprintf(row, sizeof row, "<color=#ccbeb8>%s</color>", ui_names_text(uiset.names)); plist_set(PL_NAMESR, row);
    snprintf(row, sizeof row, "<color=#ccbeb8>%d</color>", ui_max_owned); plist_set(PL_UNITSR, row);
    {   int nw = 0; for (int i = 0; i < UI_MAX_PLAYERS; i++) nw += ui_spectators >> i & 1u;
        plist_set(PL_SPECT, "<color=#a69085>Spectators (host)</color>");
        if (!dsnet_spectators_allowed(c)) snprintf(row, sizeof row, "<color=#ccbeb8>Off</color>");
        else if (nw) snprintf(row, sizeof row, "<color=#ccbeb8>On (%d)</color>", nw);
        else snprintf(row, sizeof row, "<color=#ccbeb8>On</color>");
        plist_set(PL_SPECTR, row); }
    plist_set(PL_TIPS, "<color=#a69085>Show tooltips on hover</color>");
    plist_set(PL_SIZE, "<color=#a69085>Panel size</color>");
    plist_set(PL_SIZER, uiset.compact ? "<color=#ccbeb8>Compact</color>" : "<color=#ccbeb8>Normal</color>");
    plist_set(PL_TIPSR, uiset.tooltips ? "<color=#ccbeb8>On</color>" : "<color=#ccbeb8>Off</color>");
    plist_set(PL_HINT, "<size=86%><color=#a69085>Right-click and drag to move the multiplayer windows</color></size>");
    { int fade = 12, join = 1, en = 1; chat_get(&fade, &join, &en);
      plist_set(PL_CFADE, "<color=#a69085>Chat messages fade after</color>");
      snprintf(row, sizeof row, "<color=#ccbeb8>%d s</color>", fade); plist_set(PL_CFADER, row);
      plist_set(PL_CJOIN, "<color=#a69085>Show join / leave lines</color>");
      plist_set(PL_CJOINR, join ? "<color=#ccbeb8>On</color>" : "<color=#ccbeb8>Off</color>");
      plist_set(PL_CHAT, "<color=#a69085>Chat</color>");
      plist_set(PL_CHATR, en ? "<color=#ccbeb8>On</color>" : "<color=#ccbeb8>Off</color>"); }
    plist_set(PL_ADMINR, admin_on() ? "<color=#f16071>On</color>" : "<color=#ccbeb8>Off</color>");
    /* lan_v10 rows */
    { static const struct { int lab, val; const char *txt; } onoff[] = { { PL_PINGS, PL_PINGSR, "Show pings" }, { PL_PSOUND, PL_PSOUNDR, "Ping sound" },
          { PL_PCHAT, PL_PCHATR, "Ping lines in chat" }, { PL_VIEWS, PL_VIEWSR, "Show friends' views" }, { PL_OFFS, PL_OFFSR, "Show off-screen friends" } };
      int vals[5] = { uiset.pings, uiset.ping_sound, uiset.ping_chat, uiset.views, uiset.offscreen };
      for (int k = 0; k < 5; k++) { snprintf(row, sizeof row, "<color=#a69085>%s</color>", onoff[k].txt); plist_set(onoff[k].lab, row);
                                    plist_set(onoff[k].val, vals[k] ? "<color=#ccbeb8>On</color>" : "<color=#ccbeb8>Off</color>"); }
      plist_set(PL_OUTL, "<color=#a69085>Unit outline</color>");
      plist_set(PL_MINI, "<color=#a69085>Mini overview (O)</color>");
      plist_set(PL_MINIR, uiset.mini ? "<color=#ccbeb8>Show</color>" : "<color=#ccbeb8>Hide</color>");
      plist_set(PL_MSIZE, "<color=#a69085>Overview size</color>");
      snprintf(row, sizeof row, "<color=#ccbeb8>%s</color>", ui_mini_size_text(uiset.mini_size)); plist_set(PL_MSIZER, row);
      snprintf(row, sizeof row, "<color=#ccbeb8>%s</color>", ui_outline_text(uiset.outline)); plist_set(PL_OUTLR, row);
      int lab2[PL_SETMAX], val2[PL_SETMAX], arg2[PL_SETMAX], n2 = set_rows(lab2, val2, arg2);
      for (int k = 0; k < n2; k++) if (arg2[k] >= 0) {
          char nm[40]; ui_player_label(&p, arg2[k], nm);
          snprintf(row, sizeof row, "<color=#a69085>%s's colour</color>", nm); plist_set(lab2[k], row);
          int ov = color_override_of(arg2[k]);
          snprintf(row, sizeof row, "<color=%s>%s</color>", ui_player_color(arg2[k]), ov >= 0 ? ui_palette_name(ov) : "Auto"); plist_set(val2[k], row);
      } }
    /* review #25: after "Keep playing" the host controls an absent player's units */
    char note[256] = ""; size_t nn = 0;
    if (host) {
        uint8_t seen[8][16]; int ns = 0;
        for (int i = 0; i < MAX_SC && ns < 8; i++) {
            Obj u = u_gc_get(sc[i].unit);
            if (!u || !u_alive(u_gc_get(sc[i].parts)) || owner_of(u) != UI_RESERVED) continue;
            uint8_t uid[16]; char nm[32] = "";
            dsnet_owner_info(c, (const uint8_t *)u + UNIT_GUID, uid, nm);
            int dup = 0;
            for (int k = 0; k < ns && !dup; k++) dup = !memcmp(seen[k], uid, 16);
            if (dup || !dsnet_temp_control(c, uid)) continue;
            memcpy(seen[ns++], uid, 16);
            nn += (size_t)snprintf(note + nn, sizeof note - nn, "%sYou control %s's units until they return", nn ? "\n" : "", nm[0] ? nm : "an absent player");
            if (nn >= sizeof note) break;
        }
    }
    { char nt[300]; snprintf(nt, sizeof nt, note[0] ? "<color=#d9be8f>%s</color>" : "%s", note); plist_set(PL_NOTE, nt); }
    { char st2[300]; snprintf(st2, sizeof st2, status_text[0] ? "<color=#d9be8f>%s</color>" : "%s", status_text); plist_set(PL_STATUS, st2); }
    if (pl_anchor_bar) {
        Obj cv = F(0x5260544, Obj (*)(Obj, void *))(u_gc_get(h_pl[PL_HDR]), NULL);
        if (cv) cv = F(0x5476FAC, Obj (*)(Obj, void *))(cv, NULL);
        float yb = cv ? bar_bottom_y(u_tr(cv)) : 0;
        pl_y0 = yb < -1 ? yb - 6.0f : -PL_TOP;
    }
    /* lan_v8: the player may drag the panel; its place is saved in mp_ui.json (pos_*), else the default place */
    float L = uiset.pos_set ? (float)uiset.pos_x : PL_LEFT, Y0 = uiset.pos_set ? (float)uiset.pos_y : pl_y0;
    pl_cur_L = L; pl_cur_Y = Y0; pl_shown = 1;
    { Obj g = layer_of_panel(); float k = uiset.compact ? 0.75f : 1.0f; if (g) u_rt_set_pos(g, (1 - k) * L, (1 - k) * Y0); }
    float x = L + PL_PAD, inner = PL_W - 2 * PL_PAD, y = Y0 - 2;
    int open_ = uiset.list_open;
    /* header */
    Obj hbg = u_gc_get(h_hdrbg), hln = u_gc_get(h_hdrline), gear = u_gc_get(h_gear), tog = u_gc_get(h_tog);
    show(hbg, 1); place(hbg, L + 2, y, PL_W - 4, PL_HDR_H);
    Obj th = u_gc_get(h_pl[PL_HDR]); show(th, 1); place(th, x, y, inner - 90, PL_HDR_H);
    show(gear, 1); place(gear, L + PL_W - PL_PAD - 26 - 6 - 26, y - 4, 26, 26);
    show(tog, 1); place(tog, L + PL_W - PL_PAD - 26, y - 4, 26, 26);
    img_sprite(u_gc_get(h_chev), sprite_get(open_ ? SP_LIST_ON : SP_LIST_OFF));
    { Col gc = pl_pop ? hex_col("#e2b48a", 1) : (Col){ 1, 1, 1, 1 }; graphic_color(gear, gc); }        /* pressed: lighter edge */
    {   /* a panel built without the cog icon gets it once the sprite is loaded; until then the icon is hidden */
        Obj cg = u_gc_get(h_cog); static uintptr_t h_cog_set;
        if (u_alive(cg) && u_gc_get(h_cog_set) != cg) { Obj sp = sprite_get(SP_COG); if (sp) { img_sprite(cg, sp); u_gc_free(h_cog_set); h_cog_set = u_gc_new(cg); rig_log("ui: cog icon set on the panel"); } }
        cog_has_icon = u_gc_get(h_cog_set) == cg;
        Col cc = admin_on() ? hex_col("#f16071", 1) : (Col){ 1, 1, 1, 1 };
        if (u_gc_get(h_cog_set) != cg) cc.a = 0;
        graphic_color(cg, cc);
    }
    if (!open_ && rows > 1) bars_put(PL_MAXROWS, worst, L + PL_W - PL_PAD - 26 - 6 - 26 - 8 - 18, y - 10);   /* worst, in the header */
    else bars_hide(PL_MAXROWS);
    y -= PL_HDR_H;
    show(hln, open_ || pl_pop); place(hln, L + 2, y, PL_W - 4, 1);
    /* status row (lan_v9 owner: collapsed = the header line only) */
    Obj ts = u_gc_get(h_pl[PL_STATUS]);
    int vs = open_ && status_text[0] != 0;
    show(ts, vs); if (vs) { place(ts, x, y - 2, inner, 26); y -= 28; }
    /* player rows, or the settings rows (cog) */
    Obj tb = u_gc_get(h_pl[PL_BODY]), tbr = u_gc_get(h_pl[PL_BODYR]);
    int show_rows = open_ && !pl_pop;
    show(tb, show_rows); show(tbr, show_rows);
    for (int r = 0; r < PL_MAXROWS; r++) { show(u_gc_get(h_rowbg[r]), show_rows && r < rows); bars_hide(r); }
    if (show_rows) {
        for (int r = 0; r < rows; r++) {
            place(u_gc_get(h_rowbg[r]), L + 2, y - PL_ROW_H * (float)r, PL_W - 4, PL_ROW_H);
            if (lvl[r] >= 0) bars_put(r, lvl[r], L + PL_W - PL_PAD - 18, y - PL_ROW_H * (float)r - 9);
        }
        place(tb, x, y + 1, inner - 60, PL_ROW_H * (float)rows);
        place(tbr, x, y + 1, inner, PL_ROW_H * (float)rows);
        y -= PL_ROW_H * (float)rows;
    }
    /* settings: everyone: see-through, names, tooltips; host: units per player, admin; then the move hint (lan_v8) */
    /* uxmenu (chat): fade time, join / leave lines, chat on / off, after tooltips; host rows last */
    int lab[PL_SETMAX], val[PL_SETMAX], sarg[PL_SETMAX], nall = set_rows(lab, val, sarg);
    int nset = pl_pop && open_ ? nall : 0;
    (void)host;
    for (int i = 0; i < PL_N; i++) if (pl_is_label(i) || pl_is_value(i)) {   /* rows not in the list now (an absent player's colour) */
        int in = 0; for (int k = 0; k < nset && !in; k++) in = lab[k] == i || val[k] == i;
        if (!in) show(u_gc_get(h_pl[i]), 0);
    }
    for (int k = nset; k < PL_SETMAX; k++) { show(u_gc_get(h_set_arrow[2 * k]), 0); show(u_gc_get(h_set_arrow[2 * k + 1]), 0); }
    for (int k = 0; k < nset; k++) {
        int vis = 1;
        show(u_gc_get(h_pl[lab[k]]), vis); show(u_gc_get(h_pl[val[k]]), vis);
        show(u_gc_get(h_set_arrow[2 * k]), vis); show(u_gc_get(h_set_arrow[2 * k + 1]), vis);
        float ry = y - 6 - PL_SET_H * (float)k;
        place(u_gc_get(h_pl[lab[k]]), x, ry, 160, PL_SET_H);
        place(u_gc_get(h_set_arrow[2 * k]), L + PL_W - PL_PAD - 22 - 72 - 22, ry - 4, 20, 20);
        place(u_gc_get(h_pl[val[k]]), L + PL_W - PL_PAD - 22 - 72, ry, 72, PL_SET_H);
        place(u_gc_get(h_set_arrow[2 * k + 1]), L + PL_W - PL_PAD - 22, ry - 4, 20, 20);
    }
    Obj th2 = u_gc_get(h_pl[PL_HINT]);
    show(th2, nset > 0);
    if (nset) { y -= 6 + PL_SET_H * (float)nset; place(th2, x, y - 2, inner, 20); y -= 26; }
    Obj tn = u_gc_get(h_pl[PL_NOTE]);
    show(tn, open_ && note[0] != 0);
    if (open_ && note[0]) { int nlines = 1 + (strchr(note, '\n') != NULL); place(tn, x, y - 4, inner, 20.0f * (float)nlines); y -= 20.0f * (float)nlines + 6; }
    Obj fr = u_gc_get(h_plate);
    show(fr, 1); place(fr, L, Y0, PL_W, Y0 - y + 4);
}
/* clicks: header / collapse button (open / shut), cog (settings rows), the settings arrows */
static int force_world;
static void world_frame(int force);
static void plist_click(int i) {                             /* also the test command ui_click {row} */
    if (i == PL_HDR) uiset.list_open = !uiset.list_open;
    else if (i == PL_SEE) { uiset.see_through = ui_see_through_next(uiset.see_through); force_world = 1; }
    else if (i == PL_NAMES) { uiset.names = (uiset.names + 2) % 3; own_sig ^= 1; }   /* re-tag on the next frame */
    else if (i == PL_TIPS) uiset.tooltips = !uiset.tooltips;
    else if (i == PL_SIZE) uiset.compact = !uiset.compact;
    else if (i == PL_ADMIN) {                                /* host only; a session command, same on every peer */
        dsnet_client *c = cli();
        if (!c || me() != 0) return;
        uint8_t b = (uint8_t)!admin_on();
        dsnet_submit(c, DSNET_ADMIN_MODE, 0, &b, 1);
        rig_log("ui: admin mode %s requested", b ? "on" : "off");
        own_sig ^= 1; force_world = 1;
        return;
    }
    else return;
    settings_save();
    plist_frame();
}
/* chat settings (uxmenu's chat, mpmenu.c; dlsym mpmenu_chat_config / mpmenu_chat_config_get, saved in mp_menu.json) */
static void chat_get(int *fade, int *join, int *en) {
    static void (*g)(int *, int *, int *); static unsigned tries;
    if (!g && tries++ % 50 == 0) g = (void (*)(int *, int *, int *))dlsym(RTLD_DEFAULT, "mpmenu_chat_config_get");
    if (g) g(fade, join, en);
}
static void chat_step(int row, int dir) {
    static void (*set)(int, int, int);
    if (!set) set = (void (*)(int, int, int))dlsym(RTLD_DEFAULT, "mpmenu_chat_config");
    int fade = 12, join = 1, en = 1; chat_get(&fade, &join, &en);
    static const int steps[7] = { 3, 6, 12, 20, 30, 60, 120 };
    if (row == PL_CFADE) { int k = 0; while (k < 6 && steps[k] < fade) k++; k = (k + dir + 7) % 7; fade = steps[k]; }
    else if (row == PL_CJOIN) join = !join;
    else en = !en;
    if (set) set(fade, join, en);
    rig_log("ui: chat settings: fade %d s, join lines %d, chat %d%s", fade, join, en, set ? "" : " (no chat in this build)");
    plist_frame();
}
/* host rule "Units per player": 1, 2, 3, 4, 6 (dsnet_set_rules: every peer applies it at the same step; mpmenu saves it) */
static void units_step(int dir) {
    static const int vals[5] = { 1, 2, 3, 4, 6 };
    dsnet_client *c = cli();
    if (!c || me() != 0) return;
    dsnet_rules r; dsnet_rules_get(c, &r);
    int cur = r.units_per_player > 0 ? r.units_per_player : ui_max_owned, k = 0;
    while (k < 4 && vals[k] < cur) k++;
    k = (k + dir + 5) % 5;
    r.units_per_player = vals[k];
    int rc = dsnet_set_rules(c, &r);
    rig_log("ui: units per player -> %d requested (rc %d)", vals[k], rc);
}
int ui_debug_units(int dir) { units_step(dir); return 0; }
static void plist_prev(int i) {                              /* the "<" arrows */
    if (i == PL_SEE) { uiset.see_through = ui_see_through_prev(uiset.see_through); force_world = 1; }
    else if (i == PL_NAMES) { uiset.names = (uiset.names + 1) % 3; own_sig ^= 1; }
    settings_save();
    plist_frame();
}
/* screen pixels per canvas unit of the HUD root canvas (its lossy scale) */
static Obj layer_top(void);
static float pl_scale(void) {                                 /* the top layer (pos_x / pos_y units), not the compact group */
    Obj root = layer_top();
    if (!root) return 1;
    V3 rs = F(0x510F4F8, V3 (*)(Obj, void *))(root, NULL);
    return rs.x > 0.01f ? rs.x : 1;
}
/* lan_v8 connection tooltip: "Ben: 42 ms, in sync" etc. (also for the Teams lane, dlsym) */
int ui_conn_text(int player, char *out, int cap) {
    dsnet_client *c = cli();
    if (!c || !out || cap < 8) return -1;
    ui_players p; players(&p); char nm[40];
    ui_player_label(&p, player, nm);
    int ping = -1, behind = 0, ns = 0;
    if (!(p.present >> player & 1u) || dsnet_player_net(c, player, &ping, &behind, &ns) != 0) { snprintf(out, (size_t)cap, "%s: disconnected", nm); return 0; }
    const char *st = ns == DSNET_NET_RESYNCING ? "resyncing" : ns == DSNET_NET_LOADING ? "loading" : behind >= UI_BEHIND_CATCHUP ? "catching up" : "in sync";
    if (ping >= 0 && behind >= UI_BEHIND_CATCHUP && ns == DSNET_NET_IN_SYNC) snprintf(out, (size_t)cap, "%s: %d ms, %s (%d steps behind)", nm, ping, st, behind);
    else if (ping >= 0) snprintf(out, (size_t)cap, "%s: %d ms, %s", nm, ping, st);
    else snprintf(out, (size_t)cap, "%s: %s", nm, st);
    return 0;
}
/* lan_v8 tooltip: a small notice-box frame + Lora 14 near the mouse; shown while called, hidden 0.2 s after the last call */
static uintptr_t h_tip_box, h_tip_txt; static double tip_until; static char tip_shown[160];
void ui_tooltip(const char *text) {
    Obj root = layer_top();
    settings_load();
    if (!root || !text || !uiset.tooltips) return;          /* "Show tooltips on hover" off: none of ours */
    Obj box = u_gc_get(h_tip_box), tx = u_gc_get(h_tip_txt);
    if (!u_alive(box)) {
        static uintptr_t h_noticev;
        Obj nv = find_view(&h_noticev, "Refactor.UI", "CommonNoticeView"), fsrc = nv ? tr_find(u_tr(nv), "Panel_Notice/Img_BackGroundBox") : NULL;
        if (!fsrc) return;
        box = frame_clone(fsrc, root);
        tx = line_clone(root, 1, 300, 24, 0);
        if (ui_is(tx, K_TMP)) { rt_top_left(u_tr(tx)); tmp_style(tx, u_gc_get(h_lora), 14, 513); }
        u_gc_free(h_tip_box); h_tip_box = u_gc_new(box); u_gc_free(h_tip_txt); h_tip_txt = u_gc_new(tx);
    }
    if (!u_alive(box) || !u_alive(tx)) return;
    char t[200]; snprintf(t, sizeof t, "<color=#ccbeb8>%s</color>", text);
    if (strcmp(t, tip_shown)) { snprintf(tip_shown, sizeof tip_shown, "%s", t); tmp_set(tx, t); }
    float w = tmp_pref_w(tx) + 20; if (w < 60) w = 60;
    struct { float x, y, z; } m3 = F(0x51AD4DC, __typeof__(m3) (*)(void *))(NULL);
    V3 rp = F(0x5109A08, V3 (*)(Obj, void *))(root, NULL);
    RectF4 rr = ui_is(root, K_RT) ? F(0x5106978, RectF4 (*)(Obj, void *))(root, NULL) : (RectF4){ 0, 0, 0, 0 };
    V3 rls = F(0x510F4F8, V3 (*)(Obj, void *))(root, NULL); float sc = rls.x > 0.01f ? rls.x : 1;   /* the top layer (not compact) */
    float ux = (m3.x - (rp.x + (rr.x + rr.w / 2) * sc)) / sc, uy = (m3.y - (rp.y + (rr.y + rr.h) * sc)) / sc;
    show(box, 1); show(tx, 1);
    place(box, ux + 14, uy - 14, w, 30); place(tx, ux + 24, uy - 17, w - 10, 24);
    u_set_sibling(u_tr(box), 1 << 20); u_set_sibling(u_tr(tx), 1 << 20);
    tip_until = now_s() + 0.2;
}
static void tooltip_frame(void) {
    if (tip_until > 0 && now_s() > tip_until) { show(u_gc_get(h_tip_box), 0); show(u_gc_get(h_tip_txt), 0); tip_until = 0; }
}
/* lan_v8: the panel's header rect for the Teams HUD button (it sits right of it). 1 = the panel shows */
int ui_panel_anchor(float *x, float *y, float *w, float *h) {   /* the header AS DRAWN (Compact: the corner stays, size x 0.75) */
    float k = uiset.compact ? 0.75f : 1.0f;
    if (x) *x = pl_cur_L; if (y) *y = pl_cur_Y; if (w) *w = PL_W * k; if (h) *h = PL_HDR_H * k;
    return pl_shown;
}
int ui_tooltips_enabled(void) { settings_load(); return uiset.tooltips; }   /* ui_tip.c (dlsym): "Show tooltips on hover" */
static int pl_drag_arm, pl_dragging; static float pl_drag_mx, pl_drag_my, pl_drag_L, pl_drag_Y;
static void plist_mouse(void) {
    Obj hdr = u_gc_get(h_pl[PL_HDR]);
    if (!u_alive(hdr) || !u_go_active_self(u_go(hdr))) { pl_drag_arm = pl_dragging = 0; return; }
    struct { float x, y, z; } m3 = F(0x51AD4DC, __typeof__(m3) (*)(void *))(NULL);       /* Input.mousePosition */
    float mx = m3.x, my = m3.y;
    /* tooltips (connection bars, header): tips_frame, the game's tooltip window (lan_v9) */
    /* move the panel: RIGHT-click + drag on its header (owner, lan_v8); left click keeps its normal action */
    if (!pl_drag_arm && F(0x51ACDD0, bool (*)(int, void *))(1, NULL) && (ui_hit(hdr, mx, my) || ui_hit(u_gc_get(h_hdrbg), mx, my))) {
        pl_drag_arm = 1; pl_dragging = 0; pl_drag_mx = mx; pl_drag_my = my; pl_drag_L = pl_cur_L; pl_drag_Y = pl_cur_Y;
        return;
    }
    if (pl_drag_arm) {
        if (F(0x51ACD80, bool (*)(int, void *))(1, NULL)) {                                 /* GetMouseButton(1): held */
            float dx = mx - pl_drag_mx, dy = my - pl_drag_my;
            if (!pl_dragging && dx * dx + dy * dy > 25) pl_dragging = 1;
            if (pl_dragging) {
                float sc = pl_scale(), nx = pl_drag_L + dx / sc, ny = pl_drag_Y + dy / sc;
                if (ny > -2) ny = -2;
                uiset.pos_set = 1; uiset.pos_x = (int)nx; uiset.pos_y = (int)ny;
                plist_frame();
            }
            return;
        }
        int was_drag = pl_dragging; pl_drag_arm = pl_dragging = 0;                          /* released */
        if (was_drag) { settings_save(); rig_log("ui: player panel moved to %d,%d", uiset.pos_x, uiset.pos_y); }
        return;
    }
    /* lan_v10: a friend's row: left click = look where they look, right click = Follow / Mute menu */
    { int rc = F(0x51ACDD0, bool (*)(int, void *))(1, NULL), lc = F(0x51ACDD0, bool (*)(int, void *))(0, NULL);
      if ((rc || lc) && uiset.list_open && !pl_pop)
          for (int r = 0; r < PL_MAXROWS; r++) {
              int pl = row_player[r];
              if (pl < 0 || pl == me() || row_watch[r] || !ui_hit(u_gc_get(h_rowbg[r]), mx, my)) continue;
              if (rc) { Obj top = layer_top(); float ux = 0, uy = 0; if (top) scr_to_layer(top, mx, my, &ux, &uy); pmenu_open(pl, ux + 4, uy - 4); }
              else if (!look_at(pl)) { ui_players p; players(&p); char nm[40]; notice("%s's view is not known yet", ui_player_label(&p, pl, nm)); }
              return;
          } }
    if (!F(0x51ACDD0, bool (*)(int, void *))(0, NULL)) return;                              /* GetMouseButtonDown(0) */
    if (ui_hit(u_gc_get(h_gear), mx, my)) { pl_pop = !pl_pop; if (pl_pop) uiset.list_open = 1; plist_frame(); return; }
    if (pl_pop) {
        int ids[PL_SETMAX], vv[PL_SETMAX], ag[PL_SETMAX], ns = set_rows(ids, vv, ag);
        for (int k = 0; k < 2 * ns; k++) if (ui_hit(u_gc_get(h_set_arrow[k]), mx, my)) {
            int i = ids[k / 2];
            if (ag[k / 2] >= 0) { color_step(ag[k / 2], k % 2 ? 1 : -1); settings_save(); own_sig ^= 1; force_world = 1; }   /* re-tag + outline in the new colour */
            else if (i == PL_OUTL) { uiset.outline = (uiset.outline + (k % 2 ? 1 : 3)) % 4; settings_save(); world_frame(1); }   /* at once, not at the next 0.5 s world pass */
            else if (i == PL_PINGS || i == PL_PSOUND || i == PL_PCHAT || i == PL_VIEWS || i == PL_OFFS) {
                int *f = i == PL_PINGS ? &uiset.pings : i == PL_PSOUND ? &uiset.ping_sound : i == PL_PCHAT ? &uiset.ping_chat : i == PL_VIEWS ? &uiset.views : &uiset.offscreen;
                *f = !*f; settings_save();
            }
            else if (i == PL_MINI) { uiset.mini = !uiset.mini; settings_save(); }
            else if (i == PL_MSIZE) { uiset.mini_size = (uiset.mini_size + (k % 2 ? 1 : 2)) % 3; settings_save(); }
            else if (i == PL_UNITS) units_step(k % 2 ? 1 : -1);
            else if (i == PL_CFADE || i == PL_CJOIN || i == PL_CHAT) chat_step(i, k % 2 ? 1 : -1);
            else if (i == PL_TIPS || i == PL_SIZE) plist_click(i);
            else if (i == PL_ADMIN) plist_click(PL_ADMIN);
            else if (i == PL_SPECT) {                        /* host: spectators on / off; off with watchers asks first (mpmenu box) */
                dsnet_client *c = cli(); int allow = c && !dsnet_spectators_allowed(c);
                static int (*set_fn)(int); if (!set_fn) set_fn = (int (*)(int))dlsym(RTLD_DEFAULT, "mpmenu_spectators_set");
                int rc = set_fn ? set_fn(allow) : mp_set_spectators(sess(), allow);
                rig_log("ui: spectators %s requested from the panel (rc %d)", allow ? "allowed" : "off", rc);
            }
            else if (k % 2) plist_click(i); else plist_prev(i);
            plist_frame();
            return;
        }
    }
    if (ui_hit(u_gc_get(h_tog), mx, my)) { pl_pop = 0; plist_click(PL_HDR); return; }
    if (ui_hit(hdr, mx, my) || ui_hit(u_gc_get(h_hdrbg), mx, my)) { pl_pop = 0; plist_click(PL_HDR); }   /* left click: open / shut */
}
/* for the Teams lane (dlsym): move the panel (and so the Teams button next to it) by screen pixels, saved */
int ui_panel_move_by(float dx, float dy) {
    float sc = pl_scale(), nx = pl_cur_L + dx / sc, ny = pl_cur_Y + dy / sc;
    if (ny > -2) ny = -2;
    uiset.pos_set = 1; uiset.pos_x = (int)nx; uiset.pos_y = (int)ny;
    settings_save(); plist_frame();
    return 0;
}
/* test: ui_panel_move {x, y} = a drag that ends there; {reset: 1} = back to the default place */
static char *ui_panel_move_cmd(const char *line) {
    double x = 0, y = 0, rs = 0; jnum(line, "x", &x); jnum(line, "y", &y); jnum(line, "reset", &rs);
    if (rs) uiset.pos_set = 0; else { uiset.pos_set = 1; uiset.pos_x = (int)x; uiset.pos_y = (int)(y > -2 ? -2 : y); }
    settings_save(); plist_frame();
    float ax, ay, aw, ah; int vis = ui_panel_anchor(&ax, &ay, &aw, &ah);
    Buf b = {0}; bput(&b, "{\"ok\":true,\"shown\":%d,\"x\":%.0f,\"y\":%.0f,\"pos_set\":%d}", vis, ax, ay, uiset.pos_set); return b.p;
}

#define MAX_LOCAL 32
static Guid local_order[MAX_LOCAL]; static int local_n;
/* 7c. World: units you do not control are see-through (SpriteRenderer alpha 0.55; own units 1.0).
   UnitObject.SetHUD 0xE26CFC pairs the unit's object with its HUD (UnitEntityHUD._entity +0x118). */
#define MAX_UO 256
static struct { uintptr_t uo, hud, eg; int shown, ol_sig; } uo[MAX_UO];
static void (*o_uo_sethud)(Obj, Obj, void *);
static void hk_uo_sethud(Obj o, Obj h, void *mi) {
    o_uo_sethud(o, h, mi);
    int slot = -1;
    for (int i = 0; i < MAX_UO; i++) {
        Obj x = u_gc_get(uo[i].uo);
        if (x == o) { slot = i; break; }
        if (slot < 0 && (!uo[i].uo || !u_alive(x))) slot = i;
    }
    if (slot < 0) return;
    if (u_gc_get(uo[slot].uo) != o) { u_gc_free(uo[slot].uo); uo[slot].uo = u_gc_new(o); }
    u_gc_free(uo[slot].hud); uo[slot].hud = u_gc_new(h); uo[slot].shown = -1;
    u_gc_free(uo[slot].eg); uo[slot].eg = 0; uo[slot].ol_sig = 0;
}
/* lan_v10 unit outline (owner 01:40: Off / Your units / Friends' units / Both): the game's own outline, the one its hover
   uses (EntityGraphic _outlineAlpha +0x34, _outlineColor +0x38, written to the renderer by ApplyMaterialProperties
   0xE139C8). Your units: the stock accent; friends' units: their colour. The hover's SetOutline(false) writes alpha 0 and
   applies: the hook puts ours back in that call (no flicker). Local look only (a view object, never the sim). */
#define MAX_OL MAX_UO
static struct { Obj eg; uintptr_t h; GCol c; } ol[MAX_OL]; static int ol_n;
static void (*o_eg_apply)(Obj, void *);
static int ol_find(Obj eg) { for (int i = 0; i < ol_n; i++) if (ol[i].eg == eg) return i; return -1; }
static void hk_eg_apply(Obj eg, void *mi) {
    int k = ol_n && eg ? ol_find(eg) : -1;
    if (k >= 0 && AT(eg, 0x34, float) < 0.5f) { AT(eg, 0x34, float) = 1; AT(eg, 0x38, GCol) = ol[k].c; }
    o_eg_apply(eg, mi);
}
static void ol_set(Obj eg, int on_, GCol c) {
    for (int i = 0; i < ol_n; i++) if (!u_alive(u_gc_get(ol[i].h))) { u_gc_free(ol[i].h); ol[i] = ol[--ol_n]; i--; }   /* gone units */
    int k = ol_find(eg);
    if (!on_) {
        if (k < 0) return;
        u_gc_free(ol[k].h); ol[k] = ol[--ol_n];
        AT(eg, 0x34, float) = 0; if (o_eg_apply) o_eg_apply(eg, NULL);
        return;
    }
    if (k < 0) { if (ol_n == MAX_OL) return; k = ol_n++; ol[k].eg = eg; ol[k].h = u_gc_new(eg); }
    ol[k].c = c; AT(eg, 0x34, float) = 1; AT(eg, 0x38, GCol) = c;
    if (o_eg_apply) o_eg_apply(eg, NULL);
}
static void world_frame(int force) {
    static uintptr_t h_srtype;
    Obj srt = u_gc_get(h_srtype);
    if (!srt) { srt = type_of("UnityEngine", "SpriteRenderer"); if (!srt) return; h_srtype = u_gc_new(srt); }
    for (int i = 0; i < MAX_UO; i++) {
        Obj o = u_gc_get(uo[i].uo), h = u_gc_get(uo[i].hud);
        if (!u_alive(o) || !h) continue;
        Obj e = AT(h, 0x118, Obj);
        if (!is_unit(e)) continue;
        int own = owner_of(e), mine = 1;
        if (on()) {                                          /* player units only (enemies stay as they are) */
            int pu = 0;
            for (int k = 0; k < local_n && !pu; k++) pu = !memcmp((char *)e + UNIT_GUID, &local_order[k], 16);
            mine = !pu || may_order(eff_of(e)); (void)own;
        }
        settings_load();
        /* outline: who controls it now (lent units: the borrower) */
        { int e2 = on() ? eff_of(e) : UI_UNCLAIMED, pu2 = 0;
          for (int k = 0; on() && k < local_n && !pu2; k++) pu2 = !memcmp((char *)e + UNIT_GUID, &local_order[k], 16);
          int own_ = pu2 && e2 == me(), fr_ = pu2 && e2 >= 0 && e2 != me();
          int want_ol = (own_ && (uiset.outline & UI_OUTLINE_OWN)) || (fr_ && (uiset.outline & UI_OUTLINE_FRIENDS));
          const char *hx = own_ ? UI_OWN_ACCENT : fr_ ? ui_player_color(e2) : "#ffffff";
          int sig = want_ol ? 1 + (int)(strtoul(hx + 1, NULL, 16) & 0xFFFFFF) : 0;
          if (sig != uo[i].ol_sig || (force && sig)) {
              Obj eg = u_gc_get(uo[i].eg);
              if (!u_alive(eg)) { static uintptr_t h_egt; Obj et = u_gc_get(h_egt); if (!et) { et = type_of("Refactor.View", "EntityGraphic"); h_egt = u_gc_new(et); }
                                  eg = et ? F(0x50EDDF4, Obj (*)(Obj, Obj, bool, void *))(o, et, true, NULL) : NULL; u_gc_free(uo[i].eg); uo[i].eg = u_gc_new(eg); }
              if (u_alive(eg)) { Col cc = hex_col(hx, 1); ol_set(eg, want_ol, (GCol){ cc.r, cc.g, cc.b, 1 }); uo[i].ol_sig = sig; }
          } }
        int want = mine || !uiset.see_through;
        if (!on() && uo[i].shown < 0) continue;
        if (want == uo[i].shown && !force) continue;
        Obj arr = F(0x50EDFD0, Obj (*)(Obj, Obj, void *))(o, srt, NULL);              /* GetComponentsInChildren(Type) */
        int n = arr ? AT(arr, 0x18, int32_t) : 0;
        for (int k = 0; k < n; k++) {
            Obj sr = AT(arr, 0x20 + 8 * k, Obj);
            if (!u_alive(sr)) continue;
            Col col = F(0x507413C, Col (*)(Obj, void *))(sr, NULL);
            col.a = want ? 1.0f : 1.0f - (float)uiset.see_through / 100.0f;
            F(0x507424C, void (*)(Obj, Col, void *))(sr, col, NULL);
        }
        uo[i].shown = want;
    }
}

/* 7d. Local numbering: own units first on the unit bar (SetSiblingIndex, local view only) and on the number
   hotkeys (SelectPlayerUnitRequested.Index -> my k-th unit's Guid; the event stays local, ord does not send it).
   Game order = UnitDataContainer._playerUnits (+0x18 List<Guid>) of the current map. */
static void (*o_select_unit)(Obj, Obj, void *);
static void hk_select_unit(Obj h, Obj msg, void *mi) {     /* PlayerControlEventHandler.OnEvent(SelectPlayerUnitRequested) */
    if (on() && msg) {
        Guid g = AT(msg, 0x14, Guid);
        int idx = AT(msg, 0x10, int32_t);
        if (!g.lo && !g.hi && idx >= 0 && idx < local_n) { AT(msg, 0x14, Guid) = local_order[idx]; }
    }
    o_select_unit(h, msg, mi);
}
static void order_frame(void) {
    if (!on()) { local_n = 0; return; }
    Obj c = cdc(), edc = c ? AT(c, 0x20, Obj) : NULL, ucd = edc ? dict_int_get(AT(edc, 0x18, Obj), cur_map()) : NULL;
    Obj list = ucd ? AT(ucd, 0x18, Obj) : NULL, items = list ? AT(list, 0x10, Obj) : NULL;
    int n = list ? AT(list, 0x18, int32_t) : 0;
    if (n > MAX_LOCAL) n = MAX_LOCAL;
    Guid g[MAX_LOCAL]; int own[MAX_LOCAL], ord[MAX_LOCAL];
    for (int i = 0; i < n; i++) { g[i] = *(Guid *)((char *)items + 0x20 + 16 * i); own[i] = owner_of_guid((const uint8_t *)&g[i]); }
    local_n = ui_local_order(own, n, me(), ord);
    for (int k = 0; k < local_n; k++) local_order[k] = g[ord[k]];
    for (int k = 0; k < local_n; k++)                        /* bar: parts of local_order[k] at sibling k */
        for (int i = 0; i < MAX_SC; i++) {
            Obj pa = u_gc_get(sc[i].parts), u = u_gc_get(sc[i].unit);
            if (!u_alive(pa) || !u || memcmp((char *)u + UNIT_GUID, &local_order[k], 16)) continue;
            if (u_sibling(u_tr(pa)) != k) u_set_sibling(u_tr(pa), k);
            break;
        }
}

/* ui_view: what this peer shows (test probe): pause panel + line, player list, local order, dimmed units, portraits */
static char *ui_view_cmd(void) {
    Buf b = {0}; char t[1024];
    Obj v = pause_view();
    bput(&b, "{\"ok\":true,\"pause_panel\":%d,\"pause_line\":", u_alive(v) ? u_go_active_self(AT(v, 0x30, Obj)) : -1);
    Obj pl = u_gc_get(h_pause_line); str_utf8(u_alive(pl) ? tmp_get(pl) : NULL, t, sizeof t); bstr(&b, t);
    Obj li = u_gc_get(h_pl[PL_BODY]); str_utf8(u_alive(li) ? tmp_get(li) : NULL, t, sizeof t);
    bput(&b, ",\"player_list\":"); bstr(&b, t);
    bput(&b, ",\"local_order\":[");
    for (int k = 0; k < local_n; k++) { char gs[40]; guid_str(local_order[k], gs); bput(&b, "%s\"%s\"", k ? "," : "", gs); }
    int dim = 0, full = 0;
    for (int i = 0; i < MAX_UO; i++) { if (!u_alive(u_gc_get(uo[i].uo))) continue; if (uo[i].shown == 0) dim++; else if (uo[i].shown == 1) full++; }
    bput(&b, "],\"world_dimmed\":%d,\"world_full\":%d,\"bar\":[", dim, full);
    int k = 0;
    for (int i = 0; i < MAX_SC; i++) {
        Obj pa = u_gc_get(sc[i].parts), u = u_gc_get(sc[i].unit);
        if (!u_alive(pa) || !u) continue;
        char gs[40]; guid_str(*(Guid *)((char *)u + UNIT_GUID), gs);
        bput(&b, "%s{\"guid\":\"%s\",\"sibling\":%d,\"owner\":%d,\"greyed\":%d,\"mat\":%d,\"img_mat_is_ours\":%d}", k++ ? "," : "", gs, u_sibling(u_tr(pa)), owner_of(u),
             grey_mark(pa, -1), AT(pa, 0x170, Obj) != NULL,
             u_alive(AT(pa, 0x140, Obj)) && AT(pa, 0x170, Obj) && F(0x5261CAC, Obj (*)(Obj, void *))(AT(pa, 0x140, Obj), NULL) == AT(pa, 0x170, Obj));
    }
    bput(&b, "],\"panel\":[");
    for (int i = 0; i < PL_N; i++) {
        Obj tt = u_gc_get(h_pl[i]); str_utf8(u_alive(tt) ? tmp_get(tt) : NULL, t, sizeof t);
        bput(&b, "%s{\"active\":%d,\"text\":", i ? "," : "", u_alive(tt) ? u_go_active_self(u_go(tt)) : -1); bstr(&b, t); bput(&b, "}");
    }
    bput(&b, "],\"banner\":[");
    for (int i = 0; i < 2; i++) {
        Obj bb = u_gc_get(h_banner[i]); str_utf8(u_alive(bb) && u_go_active_self(u_go(bb)) ? tmp_get(bb) : NULL, t, sizeof t);
        if (i) bput(&b, ","); bstr(&b, t);
    }
    bput(&b, "],\"disabled_buttons\":%d,\"dbg_foreign\":%d,\"dbg_views\":[%d,%d]", n_dis, dbg_foreign, dbg_view_active[0], dbg_view_active[1]);
    { Obj h0 = u_gc_get(h_pl[PL_HDR]); V2 hp = u_alive(h0) ? u_rt_pos(u_tr(h0)) : (V2){ 0, 0 };
      bput(&b, ",\"panel_pos\":[%.1f,%.1f],\"admin\":%d", hp.x, hp.y, on() ? admin_on() : -1); }
    bput(&b, ",\"status\":"); bstr(&b, status_text);
    bput(&b, ",\"left_prompt\":%d", left_prompt);
    { Obj pl = u_gc_get(h_plate); V2 ps = u_alive(pl) ? u_rt_size(u_tr(pl)) : (V2){ 0, 0 };
      bput(&b, ",\"plate\":{\"active\":%d,\"w\":%.0f,\"h\":%.0f},\"hide_why\":%d,\"cog_icon\":%d", u_alive(pl) ? u_go_active_self(u_go(pl)) : -1, ps.x, ps.y, pl_hide_why, cog_has_icon); }
    { Obj rl = u_gc_get(h_result_line); str_utf8(u_alive(rl) && u_go_active_self(u_go(rl)) ? tmp_get(rl) : NULL, t, sizeof t);
      bput(&b, ",\"result_line\":"); bstr(&b, t); bput(&b, ",\"result_disabled\":%d", result_dis.n); }
    { Obj v = u_gc_get(h_sysmenu), sb = u_alive(v) ? AT(v, 0x50, Obj) : NULL, tt = u_alive(sb) ? AT(sb, 0x28, Obj) : NULL;
      str_utf8(u_alive(tt) ? tmp_get(tt) : NULL, t, sizeof t); bput(&b, ",\"esc_save\":"); bstr(&b, t);
      Obj tf = u_alive(v) ? AT(v, 0x58, Obj) : NULL;                  /* TooltipFeedback._simpleText +0x38 */
      str_utf8(u_alive(tf) ? AT(tf, 0x38, Obj) : NULL, t, sizeof t); bput(&b, ",\"esc_save_tip\":"); bstr(&b, t);
      bput(&b, ",\"esc_save_off\":%d,\"esc_save_faded\":%d,\"esc_open\":%d", esc_dis.n, esc_dis.ng, u_alive(v) ? u_go_active_self(u_go(v)) : 0); }
    bput(&b, ",\"holds_cleared\":%d,\"pause_text\":", holds_cleared); bstr(&b, pause_text);
    bput(&b, ",\"floor\":%d", cur_floor());
    { Obj bd = u_gc_get(h_band), ar = u_gc_get(h_arrow);
      bput(&b, ",\"band\":%d,\"arrow\":%d,\"result_faded\":%d", u_alive(bd) ? u_go_active_self(u_go(bd)) : -1, u_alive(ar) ? 1 : 0, result_dis.ng); }
    { int on_ = 0; for (int r = 0; r < PL_MAXROWS; r++) for (int k = 0; k < 3; k++) { Obj im = u_gc_get(h_bars[r][k]); on_ += u_alive(im) && u_go_active_self(u_go(im)); }
      Obj pp = u_gc_get(h_pl[PL_SEE]), gr = u_gc_get(h_gear);   /* pop: the settings rows are shown */
      bput(&b, ",\"units_rule\":%d,", ui_max_owned);
      bput(&b, "\"bars_on\":%d,\"pop\":%d,\"gear\":%d,\"vote_strip\":", on_, u_alive(pp) ? u_go_active_self(u_go(pp)) : -1, u_alive(gr) ? u_go_active_self(u_go(gr)) : -1);
      bstr(&b, strip_shown); }
    bput(&b, ",\"bar_names\":[");
    { int k2 = 0; for (int i = 0; i < MAX_SC; i++) { Obj pa = u_gc_get(sc[i].parts); if (!u_alive(pa) || !u_gc_get(sc[i].unit)) continue;
        str_utf8(tmp_get(AT(pa, 0x138, Obj)), t, sizeof t); if (k2++) bput(&b, ","); bstr(&b, t); } }
    bput(&b, "]");
    bput(&b, ",\"settings\":{\"see_through\":%d,\"names\":%d,\"list_open\":%d}}", uiset.see_through, uiset.names, uiset.list_open);
    return b.p;
}

/* ================================================================ 8. live test (owner 2026-10-13) */
/* 8a. Select all / box select: only my units (SelectProcessor.Select 0xD54B7C / Toggle 0xD54574 take a
   List<IEntity>: items +0x10, size +0x18, version +0x1C). A player unit counts by the current map's
   UnitDataContainer._playerUnits (local_order holds the same set). */
static int is_player_unit(Obj e) {
    if (!is_unit(e)) return 0;
    for (int k = 0; k < local_n; k++) if (!memcmp((char *)e + UNIT_GUID, &local_order[k], 16)) return 1;
    return 0;
}
static void filter_list(Obj list) {
    if (!on() || !list) return;
    int n = AT(list, 0x18, int32_t);
    Obj items = AT(list, 0x10, Obj);
    if (n < 2 || n > 256 || !items) return;
    int isp[256], own[256], keep[256];
    for (int i = 0; i < n; i++) {
        Obj e = AT(items, 0x20 + 8 * i, Obj);
        isp[i] = is_player_unit(e); own[i] = isp[i] ? eff_of(e) : UI_UNCLAIMED;
    }
    int k = ui_filter_selection(isp, own, n, me(), admin_on(), keep);
    if (k == n) return;
    int j = 0;
    for (int i = 0; i < n; i++) if (keep[i]) AT(items, 0x20 + 8 * j++, Obj) = AT(items, 0x20 + 8 * i, Obj);
    for (int i = j; i < n; i++) AT(items, 0x20 + 8 * i, Obj) = NULL;
    AT(list, 0x18, int32_t) = j; AT(list, 0x1C, int32_t)++;
    rig_log("ui: selection of %d kept %d (only my units)", n, j);
}
static void (*o_sel_select)(Obj, Obj, void *);
static void (*o_sel_toggle)(Obj, Obj, void *);
static void hk_sel_select(Obj sp, Obj list, void *mi) { filter_list(list); o_sel_select(sp, list, mi); }
static void hk_sel_toggle(Obj sp, Obj list, void *mi) { filter_list(list); o_sel_toggle(sp, list, mi); }

/* 8b. Expedition window: only my units. SubUI_SelectRegionUI.InitOnActivate 0xEF3A80 gets the playerUnits
   List<UnitEntity>; SubUI_SelectRegionUI.SetUnit 0xEF42E0 gets the presenter's selected party List<Guid>
   (ExpeditionPlanPresenter._selectedUnits, used by StartExpedition), filtered in place. */
static void (*o_exp_setunit)(Obj, Obj, void *);
static void hk_exp_setunit(Obj v, Obj guids, void *mi) {
    if (on() && guids) {
        int n = AT(guids, 0x18, int32_t), j = 0;
        Obj items = AT(guids, 0x10, Obj);
        for (int i = 0; items && i < n; i++) {
            Guid g = AT(items, 0x20 + 16 * i, Guid);
            if (owner_of_guid((const uint8_t *)&g) != UI_RESERVED) AT(items, 0x20 + 16 * j++, Guid) = g;   /* lan_v8: allies too, not the away */
        }
        if (j != n) { AT(guids, 0x18, int32_t) = j; AT(guids, 0x1C, int32_t)++; rig_log("ui: expedition party keeps %d of %d units (mine)", j, n); }
    }
    o_exp_setunit(v, guids, mi);
}
static void (*o_exp_activate)(Obj, Obj, Obj, int, Obj, Obj, void *);
static void hk_exp_activate(Obj v, Obj regions, Obj units, int max_abyss, Obj on_region, Obj on_abyss, void *mi) {
    if (on() && units) {
        int n = AT(units, 0x18, int32_t), j = 0;
        Obj items = AT(units, 0x10, Obj);
        for (int i = 0; items && i < n; i++) {
            Obj u = AT(items, 0x20 + 8 * i, Obj);
            if (owner_of(u) != UI_RESERVED) AT(items, 0x20 + 8 * j++, Obj) = u;   /* lan_v8: allies' units join the proposal */
        }
        for (int i = j; items && i < n; i++) AT(items, 0x20 + 8 * i, Obj) = NULL;
        if (j != n) { AT(units, 0x18, int32_t) = j; AT(units, 0x1C, int32_t)++; rig_log("ui: expedition window shows %d of %d units (mine)", j, n); }
    }
    o_exp_activate(v, regions, units, max_abyss, on_region, on_abyss, mi);
}

/* 8d. Character screens of another player's unit (EntityStatusUI / EntitySkillUI, by class): a banner
   "Owned by <name>: you cannot modify this unit" and their Selectables made non-interactable (restored after). */
int ord_debug_selection(uint8_t (*out)[16], int max, int *group) __attribute__((weak_import));
static uintptr_t h_dis[MAX_DIS];
static void banner_frame(void) {
    int foreign = -2;                                        /* owner of a selected unit I may not order */
    if (on() && ord_debug_selection) {
        uint8_t g[32][16]; int grp = 0, n = ord_debug_selection(g, 32, &grp);
        for (int i = 0; i < n && foreign == -2; i++) {
            int pu = 0;
            for (int k = 0; k < local_n && !pu; k++) pu = !memcmp(g[i], &local_order[k], 16);
            int o = eff_of_guid(g[i]);
            if (pu && !may_order(o)) foreign = o;
        }
    }
    Obj views[2] = { find_view(&h_status_ui, "Refactor.UI", "EntityStatusUI"), find_view(&h_skill_ui, "Refactor.UI", "EntitySkillUI") };
    /* the cached view can be an inactive copy (other screens clone UI prefabs): look for an active one, at most every 1 s */
    static double relook;
    if (now_s() > relook) {
        relook = now_s() + 1;
        uintptr_t *hs[2] = { &h_status_ui, &h_skill_ui }; const char *nm[2] = { "EntityStatusUI", "EntitySkillUI" };
        for (int k = 0; k < 2; k++)
            if (views[k] && !F(0x50F3724, bool (*)(Obj, void *))(u_go(views[k]), NULL)) { u_gc_free(*hs[k]); *hs[k] = 0; views[k] = find_view(hs[k], "Refactor.UI", nm[k]); }
    }
    int any = 0;
    dbg_foreign = foreign;
    for (int k = 0; k < 2; k++) {
        Obj v = views[k];
        dbg_view_active[k] = v ? u_go_active_self(u_go(v)) : -1;
        int show = v && foreign != -2 && F(0x50F3724, bool (*)(Obj, void *))(u_go(v), NULL);   /* activeInHierarchy */
        Obj b = u_gc_get(h_banner[k]);
        if (show && !u_alive(b)) {
            Obj st = u_gc_get(h_status);
            if (u_alive(st)) {
                b = u_instantiate(st, u_tr(v));
                Obj rt = u_tr(b);
                (ui_is(rt, K_RT) ? F(0x5106B64, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL) : (void)0);
                (ui_is(rt, K_RT) ? F(0x5106D48, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL) : (void)0);
                (ui_is(rt, K_RT) ? F(0x51072F4, void (*)(float, float, Obj, void *))(0.5f, 0, rt, NULL) : (void)0);
                /* review #15: inside the window's width (parent - 16), 2 lines at 100 % */
                RectF4 pr = (ui_is(u_tr(v), K_RT) ? F(0x5106978, RectF4 (*)(Obj, void *))(u_tr(v), NULL) : (RectF4){0,0,0,0});             /* RectTransform.rect */
                float w = pr.w > 120 ? pr.w - 16 : 320;
                u_rt_set_size(rt, w, 2 * u_rt_size(u_tr(st)).y + 8); u_rt_set_pos(rt, 0, 6);
                (ui_is(b, K_TMP) ? F(0x4F0B7BC, void (*)(Obj, int, void *))(b, 514, NULL) : (void)0);                      /* Center */
                (ui_is(b, K_TMP) ? F(0x4F0BAB4, void (*)(Obj, bool, void *))(b, true, NULL) : (void)0);                    /* enableWordWrapping */
                tmp_plain(b);
                rig_log("ui: owner banner in %s: parent %.0fx%.0f, banner width %.0f", cls_name(v), pr.w, pr.h, w);
                u_gc_free(h_banner[k]); h_banner[k] = u_gc_new(b);
            }
        }
        if (u_alive(b)) {
            if (u_go_active_self(u_go(b)) != show) u_go_active(u_go(b), show);
            if (show) {
                char nm[40], t[160]; ui_players p; players(&p);
                if (spect()) snprintf(t, sizeof t, "<color=%s>Spectator: view only</color>\nYou are watching", UI_SPECTATOR_COLOR);
                else snprintf(t, sizeof t, "<color=%s>Owned by %s</color>\nYou cannot change this unit",
                         ui_player_color(foreign >= 0 ? foreign : 0), ui_player_label(&p, foreign >= 0 ? foreign : 0, nm));
                char cur[200]; str_utf8(tmp_get(b), cur, sizeof cur);
                if (strcmp(cur, t)) tmp_set(b, t);
            }
        }
        any |= show;
    }
    /* buttons: disable while a foreign unit is shown, restore exactly those we disabled */
    static int disabled;
    if (any && !disabled) {
        static uintptr_t h_seltype; Obj tt = u_gc_get(h_seltype);
        if (!tt) { tt = type_of("UnityEngine.UI", "Selectable"); h_seltype = u_gc_new(tt); }
        for (int k = 0; k < 2 && tt; k++) {
            if (!views[k] || !u_go_active_self(u_go(views[k]))) continue;
            Obj arr = F(0x50EDFD0, Obj (*)(Obj, Obj, void *))(views[k], tt, NULL);
            int n = arr ? AT(arr, 0x18, int32_t) : 0;
            for (int i = 0; i < n && n_dis < MAX_DIS; i++) {
                Obj sel = AT(arr, 0x20 + 8 * i, Obj);
                if (!u_alive(sel) || !AT(sel, 0xD8, uint8_t)) continue;                       /* Selectable.m_Interactable */
                (ui_is(sel, K_SEL) ? F(0x549C6B4, void (*)(Obj, bool, void *))(sel, false, NULL) : (void)0);
                h_dis[n_dis++] = u_gc_new(sel);
            }
        }
        disabled = 1;
    } else if (!any && disabled) {
        for (int i = 0; i < n_dis; i++) { Obj sel = u_gc_get(h_dis[i]); if (u_alive(sel)) (ui_is(sel, K_SEL) ? F(0x549C6B4, void (*)(Obj, bool, void *))(sel, true, NULL) : (void)0); u_gc_free(h_dis[i]); }
        n_dis = 0; disabled = 0;
    }
}

/* 8e. Review #6: a local pause hold (popup time lock) must not survive a map change. Before the 2026-10-13 fix, Esc
   menus open at the expedition start voted pause, the scene change closed them without the Unset, and
   "Paused by the owner, Ben" stayed on 1F, 2F and the Portal screen. The Esc menu no longer pauses (#7); this is the
   net under it: once a map / floor change is done (flows idle), with no story popup or return screen open,
   every local popup hold is released (a run vote if it held one) and the local reasons removed. */
int trn_flow_busy(void) __attribute__((weak_import));
static void mapchange_frame(void) {
    static int last_key = -1000000000, pending;
    int m = cur_map();
    if (!on() || m < 0) return;
    int key = m * 1000 + cur_floor();
    if (key != last_key) { if (last_key != -1000000000) pending = 1; last_key = key; map_changing = 0; }
    /* an agreed change that never changed the map (core triage 2026-10-14: a floor vote decided on the base) left
       map_changing set, so every order was refused and the holds stayed: 8 s after the decision with the transition
       flow idle, it is over; holds are released as after a real change */
    { static double idle_since; int busy_ = trn_flow_busy && trn_flow_busy();
      if (map_changing <= 0 || busy_) idle_since = 0; else if (!idle_since) idle_since = now_s();
      if (map_changing > 0 && idle_since && now_s() - map_changing > 8 && now_s() - idle_since > 3) {
          rig_log("ui: agreed map change ended with no map change: orders and holds released"); map_changing = 0; pending = 1; } }
    if (!pending || (trn_flow_busy && trn_flow_busy())) return;
    pending = 0;
    if (popup_active(22) || result_open) { rig_log("ui: map change done, a story popup / return screen is open: holds kept"); return; }
    mp_session *s = sess();
    static const int reasons[] = { MP_PAUSE_ESSENTIAL_POPUP, MP_PAUSE_POPUP_TIMELOCK, MP_PAUSE_CONSECRATION };
    int votes = 0;
    for (int r = 0; r < 3; r++) for (int k = 0; k < 16; k++) votes += mp_popup_pause(s, reasons[r], 0);
    Obj sp = mf ? AT(mf, 0xD8, Obj) : NULL, gs = sp ? AT(sp, 0x10, Obj) : NULL;      /* GameSpeedSetting */
    for (int r = 0; gs && r < 3; r++) F(0xD249BC, bool (*)(Obj, int, void *))(gs, reasons[r], NULL);   /* RemovePauseReason */
    if (votes) holds_cleared++;
    rig_log("ui: map change done (map %d floor %d): local popup holds released%s", m, cur_floor(), votes ? " (a pause vote was lifted)" : "");
}

/* 8f. Review #8: the guest's Esc menu "Save Campaign" is greyed with "Only the host saves this world" (the save
   lives on the host; mp_game already refuses a guest save). SystemMenuView._saveCampaignButton +0x50,
   SubUI_SystemMenuButton._buttonName +0x28, SetInteractable 0xF2DC14 (as mpmenu does for Load). */
static char save_label[128];
static void esc_save_frame(void) {
    static double next_look;
    if (!on() && !save_label[0]) return;                     /* single player: stock, nothing to restore */
    if (!u_alive(u_gc_get(h_sysmenu))) {                     /* not built yet: look for it at most every 2 s */
        if (now_s() < next_look) return;
        next_look = now_s() + 2;
    }
    Obj v = find_view(&h_sysmenu, "Refactor.UI", "SystemMenuView");
    if (!v || !u_go_active_self(u_go(v))) return;
    Obj sb = AT(v, 0x50, Obj), t = u_alive(sb) ? AT(sb, 0x28, Obj) : NULL;
    if (!u_alive(t)) return;
    /* design 2026-10-13 #7: the stock label stays; the button is greyed (and faded) and the reason goes in the stock
       tooltip (SystemMenuView._saveCampaignTooltipFeedBack +0x58: TooltipFeedback.SetSimpleText 0xF7B154) */
    char cur[160] = ""; str_utf8(tmp_get(t), cur, sizeof cur);
    if (strstr(cur, "Only the host") && save_label[0]) tmp_set(t, save_label);   /* a label from an older build */
    Obj tf = AT(v, 0x58, Obj);
    int guest = on() && me() != 0;
    if (guest) {
        if (!save_label[0]) snprintf(save_label, sizeof save_label, "%s", cur);
        dis_apply(&esc_dis, sb);                                 /* first: it skips buttons that are already off */
        F(0xF2DC14, void (*)(Obj, bool, void *))(sb, false, NULL);              /* SubUI_SystemMenuButton.SetInteractable */
        if (u_alive(tf)) F(0xF7B154, void (*)(Obj, Obj, bool, void *))(tf, u_str(spect() ? "You are watching: only the host saves this world" : "Only the host saves this world"), false, NULL);
    } else if (save_label[0]) {
        F(0xF2DC14, void (*)(Obj, bool, void *))(sb, true, NULL);
        dis_restore(&esc_dis);
        if (u_alive(tf)) F(0xF7B3D4, void (*)(Obj, void *))(tf, NULL);           /* TooltipFeedback.ClearTooltip */
        save_label[0] = 0;
    }
}

/* 8g. Lend control (owner-approved board "Lend control", 2026-10-13). Right-click on your own portrait opens a small
   menu: "Lend control to <name>" per other player, "Release (free unit)"; on a lent unit "Take back control"; on a unit
   lent to you "Give back". The unit stays its owner's (dsnet DSNET_LEND, uxmenu lane): only the borrower orders it, the
   colour bar shows the controller, the owner sees "Lent to Ben" + [Take back] on the portrait, the borrower "From the owner"
   + [Give back]. A borrower who leaves gives it back (dsnet). Messages: the game's centred notice (HelpNoticeView, the
   text the goddess statue uses: HelpNoticePresenter.ShowExecutionResultNotice). */
enum { LA_NONE, LA_LEND, LA_TAKE, LA_GIVE, LA_RELEASE, LA_FOLLOW, LA_UNFOLLOW, LA_MUTE, LA_UNMUTE };   /* lan_v10: + the player menu */
static void coop_act(int act, int pl);
static int lmenu_player = -1;                                /* lan_v10: the menu is a player's (Players panel row), not a unit's */
#define LM_ROWS 8
static uintptr_t h_lmenu, h_lrow[LM_ROWS];
static int lrow_act[LM_ROWS], lrow_arg[LM_ROWS], lmenu_n, lmenu_hover = -1;
static uint8_t lmenu_unit[16];
static uintptr_t h_lbl[MAX_SC], h_lblbg[MAX_SC], h_lbtn[MAX_SC], h_lbtxt[MAX_SC];
static int lbtn_act[MAX_SC];
void ui_center_message(const char *text) { notice("%s", text); }   /* for the Teams lane (dlsym): the same centred notice */
static void card_anchor(Obj parts, float *cx, float *bottom) {   /* card centre x / bottom y in the panel's canvas space */
    Obj root = layer_top(), rt = u_tr(parts);
    *cx = 0; *bottom = -150;
    if (!root || !ui_is(rt, K_RT) || !ui_is(root, K_RT)) return;
    V3 cp = F(0x5109A08, V3 (*)(Obj, void *))(rt, NULL), cs = F(0x510F4F8, V3 (*)(Obj, void *))(rt, NULL);
    RectF4 cr = F(0x5106978, RectF4 (*)(Obj, void *))(rt, NULL);
    V3 rp = F(0x5109A08, V3 (*)(Obj, void *))(root, NULL), rs = F(0x510F4F8, V3 (*)(Obj, void *))(root, NULL);
    RectF4 rr = F(0x5106978, RectF4 (*)(Obj, void *))(root, NULL);
    if (rs.x == 0 || rs.y == 0) return;
    *cx = (cp.x + (cr.x + cr.w / 2) * cs.x - (rp.x + (rr.x + rr.w / 2) * rs.x)) / rs.x;
    *bottom = (cp.y + cr.y * cs.y - (rp.y + (rr.y + rr.h) * rs.y)) / rs.y;
}
static void lend_do(const uint8_t g[16], int act, int arg) {
    dsnet_client *c = cli(); mp_session *s = sess();
    if (!c || !s) return;
    int rc = 0;
    if (act == LA_LEND) rc = dsnet_lend(c, g, arg);
    else if (act == LA_TAKE) rc = dsnet_lend(c, g, DSNET_NO_PLAYER);
    else if (act == LA_GIVE) rc = dsnet_give_back(c, g);
    else if (act == LA_RELEASE) {                            /* ux_flow F18: say it, as the portrait path does */
        rc = mp_release(s, g);
        Guid gg; memcpy(&gg, g, 16); char un[64]; unit_name(find_unit(cur_map(), gg), un, sizeof un);
        if (!rc) notice("You release %s", un);
    }
    else if (act >= LA_FOLLOW) { coop_act(act, arg); return; }
    rig_log("ui: lend action %d (arg %d) rc %d", act, arg, rc);
}
static void lmenu_close(void) { Obj m = u_gc_get(h_lmenu); show(m, 0); for (int i = 0; i < LM_ROWS; i++) show(u_gc_get(h_lrow[i]), 0); lmenu_n = 0; lmenu_player = -1; }
static void lmenu_draw(void) {
    Obj m = u_gc_get(h_lmenu);
    for (int i = 0; i < LM_ROWS; i++) {
        Obj t = u_gc_get(h_lrow[i]);
        show(t, i < lmenu_n);
        if (i >= lmenu_n) continue;
        Guid gg; memcpy(&gg, lmenu_unit, 16);
        char un[64], nm[40], txt[160]; ui_players p; players(&p);
        unit_name(find_unit(cur_map(), gg), un, sizeof un);
        const char *col = i == lmenu_hover ? "#d9be8f" : "#ccbeb8";
        if (lrow_act[i] == LA_FOLLOW) snprintf(txt, sizeof txt, "<color=%s>Follow %s's view</color>", col, ui_player_label(&p, lrow_arg[i], nm));
        else if (lrow_act[i] == LA_UNFOLLOW) snprintf(txt, sizeof txt, "<color=%s>Stop following</color>", col);
        else if (lrow_act[i] == LA_MUTE) snprintf(txt, sizeof txt, "<color=%s>Mute %s's pings</color>", col, ui_player_label(&p, lrow_arg[i], nm));
        else if (lrow_act[i] == LA_UNMUTE) snprintf(txt, sizeof txt, "<color=%s>Show %s's pings</color>", col, ui_player_label(&p, lrow_arg[i], nm));
        else if (lrow_act[i] == LA_LEND) snprintf(txt, sizeof txt, "<color=%s>Lend control to %s</color>", col, ui_player_label(&p, lrow_arg[i], nm));
        else if (lrow_act[i] == LA_TAKE) snprintf(txt, sizeof txt, "<color=%s>Take back control</color>", col);
        else if (lrow_act[i] == LA_GIVE) snprintf(txt, sizeof txt, "<color=%s>Give back %s</color>", col, un);
        else snprintf(txt, sizeof txt, "<color=%s>Release (free unit)</color>", col);
        line_text(t, txt);
    }
    (void)m;
}
static int lmenu_build(void) {                               /* the menu frame + rows, once (lend menu and player menu) */
    Obj root = layer_top();
    static uintptr_t h_noticev;
    Obj nv = find_view(&h_noticev, "Refactor.UI", "CommonNoticeView"), fsrc = nv ? tr_find(u_tr(nv), "Panel_Notice/Img_BackGroundBox") : NULL;
    if (!root || !fsrc) return 0;
    if (!u_alive(u_gc_get(h_lmenu))) {
        Obj fr = frame_clone(fsrc, root);
        u_gc_free(h_lmenu); h_lmenu = u_gc_new(fr);
        Obj st = u_gc_get(h_status);
        for (int i = 0; i < LM_ROWS; i++) {
            Obj t = u_instantiate(st, root);
            if (!ui_is(t, K_TMP)) continue;
            rt_top_left(u_tr(t)); tmp_style(t, u_gc_get(h_lora), 14, 513);
            u_gc_free(h_lrow[i]); h_lrow[i] = u_gc_new(t);
        }
    }
    return u_alive(u_gc_get(h_lmenu));
}
/* open the menu under a portrait; 1 = opened (the right-click is ours) */
static int lmenu_open(Obj parts, Obj unit) {
    dsnet_client *c = cli();
    if (!c || !is_unit(unit)) return 0;
    const uint8_t *g = (const uint8_t *)unit + UNIT_GUID;
    int owner = owner_of(unit), ctl = dsnet_lent_to(c, g), n = 0;
    if (owner == me()) {
        if (ctl >= 0) { lrow_act[n] = LA_TAKE; lrow_arg[n++] = -1; }
        else {
            ui_players p; players(&p);
            for (int i = 0; i < UI_MAX_PLAYERS && n < LM_ROWS - 1; i++) if (i != me() && (p.present >> i & 1u)) { lrow_act[n] = LA_LEND; lrow_arg[n++] = i; }
        }
        lrow_act[n] = LA_RELEASE; lrow_arg[n++] = -1;
    } else if (ctl == me()) { lrow_act[n] = LA_GIVE; lrow_arg[n++] = -1; }
    else return 0;
    if (!lmenu_build()) return 0;
    memcpy(lmenu_unit, g, 16); lmenu_n = n; lmenu_hover = -1; lmenu_player = -1;
    float cx, bot; card_anchor(parts, &cx, &bot);
    float w = 240, x = cx - w / 2, y = bot - 6;
    Obj fr = u_gc_get(h_lmenu);
    show(fr, 1); place(fr, x, y, w, 26.0f * (float)n + 12);
    u_set_sibling(u_tr(fr), 1 << 20);
    for (int i = 0; i < n; i++) { Obj t = u_gc_get(h_lrow[i]); show(t, 1); place(t, x + 12, y - 6 - 26.0f * (float)i, w - 24, 26); u_set_sibling(u_tr(t), 1 << 20); }
    lmenu_draw();
    rig_log("ui: lend menu opened (%d rows)", n);
    return 1;
}
/* the lend events (dsnet_lend_seq / dsnet_lend_last, same order on every peer): each player reads its own side */
static void lend_messages(dsnet_client *c, const ui_players *p) {
    static uint32_t seen; static dsnet_client *seen_c;
    uint32_t seq = dsnet_lend_seq(c);
    if (c != seen_c) { seen_c = c; seen = seq; return; }
    if (seq == seen) return;
    seen = seq;
    uint8_t g[16]; int owner = -1, from = -1, to = -1, why = -1;
    if (dsnet_lend_last(c, g, &owner, &from, &to, &why)) return;
    Guid gg; memcpy(&gg, g, 16);
    char un[64], on_[40], bn[40]; unit_name(find_unit(cur_map(), gg), un, sizeof un);
    int lender = owner >= 0 ? owner : 0, borrower = why == DSNET_LEND_LENT ? to : from;
    ui_player_label(p, lender, on_);
    ui_player_label(p, borrower >= 0 ? borrower : 0, bn);
    rig_log("ui: lend event %d (unit %s, owner %d, from %d, to %d)", why, un, owner, from, to);
    if (why == DSNET_LEND_LENT) {
        if (me() == borrower) notice("%s lent you %s", on_, un);
        else if (me() == lender) notice("You lent %s to %s", un, bn);
    } else if (why == DSNET_LEND_TAKEN_BACK) {
        if (me() == borrower) notice("%s took back %s", on_, un);
        else if (me() == lender) notice("You took back %s", un);
    } else if (why == DSNET_LEND_GIVEN_BACK) {
        if (me() == lender) notice("%s gave back %s", bn, un);
        else if (me() == borrower) notice("You gave back %s", un);
    } else if (why == DSNET_LEND_RETURNED && me() == lender) notice("%s is back with you (%s left)", un, bn);
}
static void lend_frame(void) {
    dsnet_client *c = cli();
    if (on() && c) { ui_players pp; players(&pp); lend_messages(c, &pp); }
    if (!on() || !c) { lmenu_close(); for (int i = 0; i < MAX_SC; i++) { show(u_gc_get(h_lbl[i]), 0); show(u_gc_get(h_lblbg[i]), 0); show(u_gc_get(h_lbtn[i]), 0); } return; }
    ui_players p; players(&p);
    for (int i = 0; i < MAX_SC; i++) {
        Obj pa = u_gc_get(sc[i].parts), u = u_gc_get(sc[i].unit);
        if (!u_alive(pa) || !is_unit(u)) { show(u_gc_get(h_lbl[i]), 0); show(u_gc_get(h_lblbg[i]), 0); show(u_gc_get(h_lbtn[i]), 0); continue; }
        const uint8_t *g = (const uint8_t *)u + UNIT_GUID;
        int owner = owner_of(u), ctl = dsnet_lent_to(c, g);
        /* portrait label + button */
        int act = ctl >= 0 && owner == me() ? LA_TAKE : ctl == me() && owner != me() ? LA_GIVE : LA_NONE;
        Obj lb = u_gc_get(h_lbl[i]), bg = u_gc_get(h_lblbg[i]), bt = u_gc_get(h_lbtn[i]), btx = u_gc_get(h_lbtxt[i]);
        if (act != LA_NONE && !u_alive(lb)) {
            Obj st = u_gc_get(h_status), ptr = u_tr(pa);
            bg = img_make(ptr, NULL, 76, 18);
            if (bg) { graphic_color(bg, hex_col("#222025", 0.85f)); Obj rt = u_tr(bg);
                      F(0x5106B64, void (*)(float, float, Obj, void *))(0.5f, 0, rt, NULL); F(0x5106D48, void (*)(float, float, Obj, void *))(0.5f, 0, rt, NULL);
                      F(0x51072F4, void (*)(float, float, Obj, void *))(0.5f, 0, rt, NULL); u_rt_set_pos(rt, 0, 22); }
            lb = line_clone(bg ? u_tr(bg) : ptr, 0.5f, 76, 18, -9);
            if (ui_is(lb, K_TMP)) tmp_style(lb, u_gc_get(h_lora), 12, 514);
            bt = img_make(ptr, sprite_get(SP_BOTTOMBUTTON), 76, 20);
            if (bt) { Obj rt = u_tr(bt);
                      F(0x5106B64, void (*)(float, float, Obj, void *))(0.5f, 0, rt, NULL); F(0x5106D48, void (*)(float, float, Obj, void *))(0.5f, 0, rt, NULL);
                      F(0x51072F4, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL); u_rt_set_pos(rt, 0, -4); }
            btx = bt ? line_clone(u_tr(bt), 0.5f, 76, 20, -10) : NULL;
            if (ui_is(btx, K_TMP)) tmp_style(btx, u_gc_get(h_lora), 12, 514);
            (void)st;
            u_gc_free(h_lbl[i]); h_lbl[i] = u_gc_new(lb); u_gc_free(h_lblbg[i]); h_lblbg[i] = u_gc_new(bg);
            u_gc_free(h_lbtn[i]); h_lbtn[i] = u_gc_new(bt); u_gc_free(h_lbtxt[i]); h_lbtxt[i] = u_gc_new(btx);
        }
        int vis = act != LA_NONE;
        show(lb, vis); show(bg, vis); show(bt, vis); show(btx, vis);
        lbtn_act[i] = act;
        if (vis) {
            char t1[160], nm[96];
            if (act == LA_TAKE) snprintf(t1, sizeof t1, "<color=#d9be8f>Lent to %s</color>", pname(&p, ctl, nm, sizeof nm));
            else snprintf(t1, sizeof t1, "<color=#d9be8f>From %s</color>", pname(&p, owner >= 0 ? owner : 0, nm, sizeof nm));
            line_text(lb, t1);
            line_text(btx, act == LA_TAKE ? "<color=#ccbeb8>Take back</color>" : "<color=#ccbeb8>Give back</color>");
        }
    }
    if (lmenu_n && lmenu_player < 0) {                       /* the menu's unit still lendable by me? */
        int o = owner_of_guid(lmenu_unit), k = dsnet_lent_to(c, lmenu_unit);
        if (o != me() && k != me()) lmenu_close();
    }
}
static void lend_mouse(void) {
    if (!on()) return;
    struct { float x, y, z; } m3 = F(0x51AD4DC, __typeof__(m3) (*)(void *))(NULL);
    if (lmenu_n) {                                           /* hover look */
        int h = -1;
        for (int i = 0; i < lmenu_n && h < 0; i++) if (ui_hit(u_gc_get(h_lrow[i]), m3.x, m3.y)) h = i;
        if (h != lmenu_hover) { lmenu_hover = h; lmenu_draw(); }
    }
    if (!F(0x51ACDD0, bool (*)(int, void *))(0, NULL)) return;
    if (lmenu_n) {
        for (int i = 0; i < lmenu_n; i++) if (ui_hit(u_gc_get(h_lrow[i]), m3.x, m3.y)) { lend_do(lmenu_unit, lrow_act[i], lrow_arg[i]); lmenu_close(); return; }
        lmenu_close();                                       /* a click elsewhere closes it */
    }
    for (int i = 0; i < MAX_SC; i++) {
        if (lbtn_act[i] == LA_NONE || !ui_hit(u_gc_get(h_lbtn[i]), m3.x, m3.y)) continue;
        Obj u = u_gc_get(sc[i].unit);
        if (is_unit(u)) lend_do((const uint8_t *)u + UNIT_GUID, lbtn_act[i], -1);
        return;
    }
}
/* test: ui_lend {guid, action: "menu" | "lend" | "take" | "give" | "row", to | row} */
static char *ui_lend_cmd(const char *line) {
    char gs[64] = "", act[16] = ""; double to = -1, row = 0; Guid g;
    jstr(line, "guid", gs, sizeof gs); jstr(line, "action", act, sizeof act); jnum(line, "to", &to); jnum(line, "row", &row);
    Buf b = {0}; int rc = -1;
    if (!strcmp(act, "row")) { int r = (int)row; if (r >= 0 && r < lmenu_n) { lend_do(lmenu_unit, lrow_act[r], lrow_arg[r]); lmenu_close(); rc = 0; } }
    else if (guid_parse(gs, &g)) {
        if (!strcmp(act, "menu")) {
            for (int i = 0; i < MAX_SC; i++) { Obj u = u_gc_get(sc[i].unit), pa = u_gc_get(sc[i].parts);
                if (u_alive(pa) && u && !memcmp((char *)u + UNIT_GUID, &g, 16)) { rc = lmenu_open(pa, u) ? 0 : -2; break; } }
        } else if (!strcmp(act, "lend") || !strcmp(act, "take") || !strcmp(act, "give")) {
            lend_do((const uint8_t *)&g, !strcmp(act, "lend") ? LA_LEND : !strcmp(act, "take") ? LA_TAKE : LA_GIVE, (int)to); rc = 0;
        } else rc = 0;                                       /* "state" */
    }
    dsnet_client *c = cli();
    bput(&b, "{\"ok\":%s,\"rc\":%d,\"menu_rows\":%d,\"controller\":%d,\"labels\":[", rc ? "false" : "true", rc, lmenu_n,
         c && guid_parse(gs, &g) ? dsnet_lent_to(c, (const uint8_t *)&g) : -9);
    int k = 0; char t[160];
    for (int i = 0; i < MAX_SC; i++) { Obj lb = u_gc_get(h_lbl[i]); if (!u_alive(lb) || !u_go_active_self(u_go(lb))) continue;
        str_utf8(tmp_get(lb), t, sizeof t); if (k++) bput(&b, ","); bstr(&b, t); }
    bput(&b, "],\"menu\":[");
    for (int i = 0; i < lmenu_n; i++) { str_utf8(tmp_get(u_gc_get(h_lrow[i])), t, sizeof t); if (i) bput(&b, ","); bstr(&b, t); }
    bput(&b, "]}");
    return b.p;
}

/* 8h. Viewer (owner-approved board "Viewer", 2026-10-13): on a dungeon map, a player with no unit he can order there
   watches. Banner (notice-box frame, 660 wide, top centre): "Watching the owner's expedition" (Cinzel 20 small caps) and
   "You have no units on this run. You can watch and chat; the owner gives the orders." (Lora 14 muted); buttons "Follow
   The owner's camera" / "Free camera" / "Ask the owner to lend a unit". Clicks on the world give no orders (ui_check_order); the
   pause and speed votes still work. Camera follow and the ask travel as zero-guid ui orders (no unit, no world change):
   UI_ORDER_FOLLOW {u8 target, u8 on}, UI_ORDER_CAMERA {f32 x, f32 y, f32 ortho} (sent by a followed player every
   0.5 s while it moves), UI_ORDER_ASK {u8 target}. */
static uint32_t followers[UI_MAX_PLAYERS];                   /* bit per follower, per followed player (command state) */
static int follow_target = -1, cam_new; static float cam_x, cam_y, cam_o;
static uintptr_t h_vbox, h_vtitle, h_vtext, h_vbtn_b[3], h_vbtn_bg[3], h_vbtn_t[3];
static int viewer_on, viewer_leader = -1;
static Obj cam_setting(void) { Obj cs = mf ? AT(mf, 0xD8, Obj) : NULL; return cs ? AT(cs, 0x18, Obj) : NULL; }   /* CampaignSettingContainer.CameraSetting */
static int follow_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)guid;
    if (n < 2 || a[0] >= UI_MAX_PLAYERS || player < 0 || player >= UI_MAX_PLAYERS) return -1;
    for (int t = 0; t < UI_MAX_PLAYERS; t++) followers[t] &= ~(1u << player);
    if (a[1]) followers[a[0]] |= 1u << player;
    return 0;
}
static int camera_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)guid;
    if (n < 12) return -1;
    if (player == follow_target && follow_target != me()) { memcpy(&cam_x, a, 4); memcpy(&cam_y, a + 4, 4); memcpy(&cam_o, a + 8, 4); cam_new = 1; }
    return 0;
}
static int ask_order(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)s; (void)guid;
    if (n < 1) return -1;
    if (a[0] == me()) { ui_players p; players(&p); char nm[40]; notice("%s asks you to lend a unit (right-click your portrait)", ui_player_label(&p, player, nm)); }
    return 0;
}
static void viewer_send(int type, const uint8_t *a, uint16_t n) { static const uint8_t zero[16]; mp_session *s = sess(); if (s) mp_order(s, (uint16_t)type, zero, a, n); }
/* lan_v10: the follow is local (the leader's P_VIEW moves this camera, 8j); no sim order any more (UI_ORDER_FOLLOW /
   UI_ORDER_CAMERA went through the command stream; their handlers stay registered for older peers' orders) */
static void follow_start(int pl); static void follow_stop(int quiet); static int viewer_follows;
static int mini_kept;                                        /* 8k: the mini overview stays after a click on it, until Free camera */
static void viewer_follow(int on_) {
    if (viewer_leader < 0) return;
    if (on_) { follow_start(viewer_leader); viewer_follows = 1; } else { follow_stop(1); viewer_follows = 0; mini_kept = 0; }
    rig_log("ui: viewer %s %d's camera", on_ ? "follows" : "stops following", viewer_leader);
}
static void viewer_ask(void) {
    if (viewer_leader < 0) return;
    static double last; char nm0[40]; ui_players p0; players(&p0);   /* ux_flow F13: not every click again */
    if (now_s() - last < 10) { notice("You asked %s a moment ago", ui_player_label(&p0, viewer_leader, nm0)); return; }
    last = now_s();
    uint8_t a[1] = { (uint8_t)viewer_leader };
    viewer_send(UI_ORDER_ASK, a, 1);
    ui_players p; players(&p); char nm[40];
    notice("Asked %s to lend you a unit", ui_player_label(&p, viewer_leader, nm));
}
static Obj vbutton(Obj root, int k) {                        /* framed text button: border + inner fill + Lora 14 */
    Obj b = img_make(root, NULL, 10, 32), bg = img_make(root, NULL, 10, 28), t = line_clone(root, 1, 10, 28, 0);
    if (ui_is(t, K_TMP)) { rt_top_left(u_tr(t)); tmp_style(t, u_gc_get(h_lora), 14, 514); }
    u_gc_free(h_vbtn_b[k]); h_vbtn_b[k] = u_gc_new(b); u_gc_free(h_vbtn_bg[k]); h_vbtn_bg[k] = u_gc_new(bg); u_gc_free(h_vbtn_t[k]); h_vbtn_t[k] = u_gc_new(t);
    return b;
}
static int viewer_min;                                       /* Escape folded the viewer box to its title line */
static float pl_scale(void);
/* the viewer box's own place (right-click drag), <persistentDataPath>/mp_ui_viewer.json {"x":..,"y":..} */
static int vpos_loaded, vpos_set, vb_drag, vb_dragging; static float vpos_x, vpos_y, vb_cur_x, vb_cur_y, vb_mx, vb_my, vb_x0, vb_y0;
static void vpos_path(char *out, size_t cap) { char d[900]; str_utf8(F(0x507B74C, Obj (*)(void *))(NULL), d, sizeof d); snprintf(out, cap, "%s/mp_ui_viewer.json", d); }
static void vpos_load(void) {
    if (vpos_loaded) return;
    vpos_loaded = 1; char pth[1024]; vpos_path(pth, sizeof pth);
    FILE *f = fopen(pth, "r"); if (!f) return;
    if (fscanf(f, " {\"x\":%f,\"y\":%f}", &vpos_x, &vpos_y) == 2) vpos_set = 1;
    fclose(f);
}
static void viewer_frame(void); static void vpos_save(void);
static void viewer_move_test(const char *line, int reset) {
    double x = vb_cur_x, y = vb_cur_y; jnum(line, "x", &x); jnum(line, "y", &y);
    vpos_load(); vpos_set = !reset; vpos_x = (float)x; vpos_y = (float)(y > -2 ? -2 : y); vpos_save(); viewer_frame();
}
static void viewer_geom(float *g) {
    Obj pl = u_gc_get(h_plate), bx = u_gc_get(h_vbox); float k = uiset.compact ? 0.75f : 1.0f;
    g[0] = vb_cur_x; g[1] = vb_cur_y; g[2] = u_alive(bx) ? u_rt_size(u_tr(bx)).y : 0;
    g[3] = pl_cur_L; g[4] = pl_cur_Y; g[5] = u_alive(pl) ? u_rt_size(u_tr(pl)).x * k : 0; g[6] = u_alive(pl) && pl_shown ? u_rt_size(u_tr(pl)).y * k : 0;
    g[7] = pl_y0 + 6; g[8] = vpos_set;
}
static void vpos_save(void) {
    char pth[1024]; vpos_path(pth, sizeof pth);
    FILE *f = fopen(pth, "w"); if (!f) return;
    if (vpos_set) fprintf(f, "{\"x\":%.0f,\"y\":%.0f}\n", vpos_x, vpos_y); else fputs("{}\n", f);
    fclose(f);
}
/* spectator (owner 2026-10-14): the box shows on every map: "Watching the owner's world", Follow <player>'s camera (the
   leader's P_VIEW moves this camera, outside the sim) / Free camera / Next player (another present player to follow) */
static int spect_leader = -1;
static int spect_next_player(int from) {
    ui_players p; players(&p);
    for (int k = 1; k <= UI_MAX_PLAYERS; k++) { int i = ((from < 0 ? -1 : from) + k) % UI_MAX_PLAYERS; if (p.present >> i & 1u) return i; }
    return -1;
}
static void viewer_frame(void) {
    cam_new = 0;                                             /* (lan_v10: the follow is 8j's follow_frame) */
    /* who watches: on a dungeon map, none of the player units there may be ordered by me */
    int v = 0, leader = -1;
    if (on() && spect() && cur_map() >= 0 && !busy()) {
        ui_players sp; players(&sp);
        if (spect_leader < 0 || !(sp.present >> spect_leader & 1u)) spect_leader = spect_next_player(-1);
        v = spect_leader >= 0; leader = spect_leader;
    } else if (on() && cur_map() > 0 && local_n > 0) {
        v = 1; int cnt[UI_MAX_PLAYERS] = {0};
        for (int k = 0; k < local_n; k++) {
            int e = eff_of_guid((const uint8_t *)&local_order[k]);
            if (may_order(e)) { v = 0; break; }
            if (e >= 0 && e < UI_MAX_PLAYERS) cnt[e]++;
        }
        for (int i = 0; i < UI_MAX_PLAYERS; i++) if (cnt[i] && (leader < 0 || cnt[i] > cnt[leader])) leader = i;
        if (leader < 0) leader = 0;
    }
    if (!v && viewer_follows) { viewer_follows = 0; if (coop_follow == viewer_leader) follow_stop(1); }
    if (viewer_follows && coop_follow != leader) viewer_follows = 0;   /* the player moved the camera: the follow stopped */
    if (!v) viewer_min = 0;                                  /* the next watching starts unfolded */
    viewer_on = v; viewer_leader = leader;
    Obj box = u_gc_get(h_vbox);
    if (v && !u_alive(box)) {
        Obj root = layer_top();
        static uintptr_t h_noticev;
        Obj nv = find_view(&h_noticev, "Refactor.UI", "CommonNoticeView"), fsrc = nv ? tr_find(u_tr(nv), "Panel_Notice/Img_BackGroundBox") : NULL;
        if (!root || !fsrc) return;
        box = frame_clone(fsrc, root);
        Obj tt = line_clone(root, 1, 600, 28, 0), tx = line_clone(root, 1, 600, 22, 0);
        if (ui_is(tt, K_TMP)) { rt_top_left(u_tr(tt)); tmp_style(tt, u_gc_get(h_cinzel), 20, 513); }
        if (ui_is(tx, K_TMP)) { rt_top_left(u_tr(tx)); tmp_style(tx, u_gc_get(h_lora), 14, 513); }
        u_gc_free(h_vbox); h_vbox = u_gc_new(box); u_gc_free(h_vtitle); h_vtitle = u_gc_new(tt); u_gc_free(h_vtext); h_vtext = u_gc_new(tx);
        for (int k = 0; k < 3; k++) vbutton(root, k);
    }
    int full = v && !viewer_min;
    show(box, v); show(u_gc_get(h_vtitle), v); show(u_gc_get(h_vtext), full);
    for (int k = 0; k < 3; k++) { show(u_gc_get(h_vbtn_b[k]), full); show(u_gc_get(h_vbtn_bg[k]), full); show(u_gc_get(h_vbtn_t[k]), full); }
    if (!v) return;
    ui_players p; players(&p); char nm[40], t[200];
    ui_player_label(&p, leader, nm);
    /* lead lan_v9: never over the portrait bar or the Players panel: under the panel (its drawn bottom), left-aligned
       with it, unless the player dragged it (right-click drag, own place in mp_ui_viewer.json) */
    vpos_load();
    float X0, Y0;
    if (vpos_set) { X0 = vpos_x; Y0 = vpos_y; }
    else {
        Obj pl = u_gc_get(h_plate); float k = uiset.compact ? 0.75f : 1.0f;
        float ph = u_alive(pl) && pl_shown ? u_rt_size(u_tr(pl)).y * k : 0;
        X0 = pl_shown ? pl_cur_L : PL_LEFT; Y0 = (pl_shown ? pl_cur_Y : pl_y0) - ph - 10;
    }
    vb_cur_x = X0; vb_cur_y = Y0;
    place(box, X0, Y0, 660, full ? 70 : 48);
    int sv = spect(); char hn[40]; ui_player_label(&p, 0, hn);
    const char *what = sv ? "world" : "expedition", *whose = sv ? hn : nm;
    if (full) snprintf(t, sizeof t, "<smallcaps><color=#ccbeb8>Watching %s's %s</color></smallcaps>", whose, what);
    else snprintf(t, sizeof t, "<smallcaps><color=#ccbeb8>Watching %s's %s</color></smallcaps> <size=80%%><color=#a69085>(click for options)</color></size>", whose, what);
    place(u_gc_get(h_vtitle), X0 + 18, Y0 - 10, 620, 28); line_text(u_gc_get(h_vtitle), t);
    if (!full) return;
    if (sv) snprintf(t, sizeof t, "<color=#a69085>You are a spectator. You can chat and ping.</color>");
    else snprintf(t, sizeof t, "<color=#a69085>You have no units on this run. You can watch and chat; %s gives the orders.</color>", nm);
    place(u_gc_get(h_vtext), X0 + 18, Y0 - 38, 620, 22); line_text(u_gc_get(h_vtext), t);
    /* HUD A (owner 2026-10-14 ~19:55): "Follow the owner" / "Following the owner" */
    const char *lab[3] = { coop_follow == leader ? "Following %s" : "Follow %s", "Free camera", sv ? "Next player \u203A" : "Ask %s to lend a unit" };
    int nb = 3;
    if (sv) { int np = 0; for (int i = 0; i < UI_MAX_PLAYERS; i++) np += p.present >> i & 1u; if (np < 2) nb = 2; }   /* one player: nobody else to pick */
    for (int k = nb; k < 3; k++) { show(u_gc_get(h_vbtn_b[k]), 0); show(u_gc_get(h_vbtn_bg[k]), 0); show(u_gc_get(h_vbtn_t[k]), 0); }
    float bx = X0;
    for (int k = 0; k < nb; k++) {
        char bl[96]; snprintf(bl, sizeof bl, lab[k], nm);
        Obj bt = u_gc_get(h_vbtn_t[k]);
        char tx2[160]; snprintf(tx2, sizeof tx2, "<color=#ccbeb8>%s</color>", bl);
        line_text(bt, tx2);
        float w = tmp_pref_w(bt) + 30; if (w < 120) w = 120;
        int active = (k == 0 && coop_follow == leader) || (k == 1 && coop_follow != leader);
        Obj b = u_gc_get(h_vbtn_b[k]), bg = u_gc_get(h_vbtn_bg[k]);
        graphic_color(b, hex_col(active ? "#b6835a" : "#633d33", 1)); graphic_color(bg, hex_col(active ? "#36323a" : "#2a262c", 1));
        place(b, bx, Y0 - 78, w, 32); place(bg, bx + 2, Y0 - 80, w - 4, 28); place(bt, bx, Y0 - 80, w, 28);
        bx += w + 10;
    }
}
static void viewer_mouse(void) {
    if (!viewer_on) { vb_drag = 0; return; }
    struct { float x, y, z; } m3 = F(0x51AD4DC, __typeof__(m3) (*)(void *))(NULL);
    /* right-click + drag on the box moves it (like the panel and the Teams button); its place is saved */
    if (!vb_drag && F(0x51ACDD0, bool (*)(int, void *))(1, NULL) && ui_hit(u_gc_get(h_vbox), m3.x, m3.y)) {
        vb_drag = 1; vb_dragging = 0; vb_mx = m3.x; vb_my = m3.y; vb_x0 = vb_cur_x; vb_y0 = vb_cur_y; return;
    }
    if (vb_drag) {
        if (F(0x51ACD80, bool (*)(int, void *))(1, NULL)) {
            float dx = m3.x - vb_mx, dy = m3.y - vb_my;
            if (!vb_dragging && dx * dx + dy * dy > 25) vb_dragging = 1;
            if (vb_dragging) { float sc = pl_scale(); vpos_set = 1; vpos_x = vb_x0 + dx / sc; vpos_y = vb_y0 + dy / sc; if (vpos_y > -2) vpos_y = -2; viewer_frame(); }
            return;
        }
        if (vb_dragging) { vpos_save(); rig_log("ui: viewer box moved to %.0f,%.0f", vpos_x, vpos_y); }
        vb_drag = vb_dragging = 0; return;
    }
    if (!F(0x51ACDD0, bool (*)(int, void *))(0, NULL)) return;
    if (viewer_min) { if (ui_hit(u_gc_get(h_vbox), m3.x, m3.y)) { viewer_min = 0; viewer_frame(); } return; }   /* unfold */
    for (int k = 0; k < 3; k++) {
        if (!ui_hit(u_gc_get(h_vbtn_b[k]), m3.x, m3.y)) continue;
        if (k == 0) viewer_follow(1); else if (k == 1) viewer_follow(0);
        else if (spect()) {                                  /* next player: the box (and a running follow) move to him */
            int was = coop_follow == spect_leader, nx = spect_next_player(spect_leader);
            if (nx >= 0) spect_leader = nx;
            viewer_frame();
            if (was) viewer_follow(1);
        }
        else viewer_ask();
        return;
    }
}

/* test: ui_spect {press: 0 Follow, 1 Free camera, 2 Next player / Ask} -> the spectator state as the screen shows it */
static char *ui_spect_cmd(const char *line) {
    double k = -1; jnum(line, "press", &k);
    if (k >= 0 && k < 3 && viewer_on) {
        if (k == 0) viewer_follow(1); else if (k == 1) viewer_follow(0);
        else if (spect()) { int was = coop_follow == spect_leader, nx = spect_next_player(spect_leader); if (nx >= 0) spect_leader = nx; viewer_frame(); if (was) viewer_follow(1); }
    }
    viewer_frame();
    char tt[300] = "", tx[300] = "", b0[160] = "", b1[160] = "", b2[160] = "", body[2600] = "", spr[200] = "", sprv[200] = "";
    str_utf8(tmp_get(u_gc_get(h_vtitle)), tt, sizeof tt); str_utf8(tmp_get(u_gc_get(h_vtext)), tx, sizeof tx);
    str_utf8(tmp_get(u_gc_get(h_vbtn_t[0])), b0, sizeof b0); str_utf8(tmp_get(u_gc_get(h_vbtn_t[1])), b1, sizeof b1); str_utf8(tmp_get(u_gc_get(h_vbtn_t[2])), b2, sizeof b2);
    str_utf8(tmp_get(u_gc_get(h_pl[PL_BODY])), body, sizeof body);
    str_utf8(tmp_get(u_gc_get(h_pl[PL_SPECT])), spr, sizeof spr); str_utf8(tmp_get(u_gc_get(h_pl[PL_SPECTR])), sprv, sizeof sprv);
    Obj ck = u_gc_get(h_clock); int clk[4] = { -1, -1, -1, -1 };
    if (u_alive(ck)) for (int i = 0; i < 4; i++) { Obj bt = AT(ck, 0x48 + 8 * i, Obj); clk[i] = u_alive(bt) ? u_go_active_self(u_go(bt)) : -1; }
    Buf b = {0};
    bput(&b, "{\"ok\":true,\"spectator\":%d,\"me\":%d,\"viewer_on\":%d,\"leader\":%d,\"follow\":%d,\"spectators\":%u,\"clock_buttons\":[%d,%d,%d,%d],\"b2_shown\":%d,\"title\":",
         spect(), me(), viewer_on, viewer_leader, coop_follow, ui_spectators, clk[0], clk[1], clk[2], clk[3], u_alive(u_gc_get(h_vbtn_b[2])) && u_go_active_self(u_go(u_gc_get(h_vbtn_b[2]))));
    bstr(&b, tt); bput(&b, ",\"text\":"); bstr(&b, tx); bput(&b, ",\"buttons\":["); bstr(&b, b0); bput(&b, ","); bstr(&b, b1); bput(&b, ","); bstr(&b, b2);
    bput(&b, "],\"panel_body\":"); bstr(&b, body); bput(&b, ",\"panel_spect\":["); bstr(&b, spr); bput(&b, ","); bstr(&b, sprv); bput(&b, "]}");
    return b.p;
}

/* 8j. Co-op markers (lan_v10; design research/ping_camera_v10.md, owner override 2026-10-14 01:55). Everything here is
   OUTSIDE the simulation, like the chat: views and pings travel as dsnet P_VIEW / P_MARK packets (never stamped, never
   in saves, hashes or replays); a ping clears on each peer from that peer's own (lockstep-identical) unit positions,
   read only. Nothing here writes game state; the camera calls are the local CameraSetting, as the game's own keys.
   - Ping: B at the mouse (B over a ping deletes it, any player's), middle-click tap places. Shift + B moves the camera
     to the newest ping (again: the next one). No timeout: a player unit reaching the cell (or the 8 around) clears it.
   - Friends' views: corner brackets of each friend's camera rectangle, in their colour, with their name.
   - Players panel: left click on a friend's row = look where they look; right click = Follow / Mute pings menu.
   - Colours: other players get the palette colour, or this viewer's own choice (cog, local); own things keep the
     stock look (own ping = the stock accent). */
#ifndef DSNET_HAS_MARKS
enum { DSNET_MARK_ADD = 1, DSNET_MARK_DEL = 2 };
typedef struct dsnet_mark_ev { int from, op, owner, id; uint32_t map_key; int x, y; } dsnet_mark_ev;
#endif
/* dsnet's marker API (core lane), looked up at run time: a build without it keeps pings local, views off */
static int (*fn_view_send)(dsnet_client *, uint32_t, float, float, float, float);
static int (*fn_view_of)(dsnet_client *, int, uint32_t *, float *, double *);
static int (*fn_mark_send)(dsnet_client *, int, int, int, uint32_t, int, int);
static int (*fn_mark_poll)(dsnet_client *, dsnet_mark_ev *);
static void marks_api(void) {
    static unsigned tries; if (fn_mark_poll || tries++ % 100) return;
    fn_view_send = (int (*)(dsnet_client *, uint32_t, float, float, float, float))dlsym(RTLD_DEFAULT, "dsnet_view_send");
    fn_view_of = (int (*)(dsnet_client *, int, uint32_t *, float *, double *))dlsym(RTLD_DEFAULT, "dsnet_view_of");
    fn_mark_send = (int (*)(dsnet_client *, int, int, int, uint32_t, int, int))dlsym(RTLD_DEFAULT, "dsnet_mark_send");
    fn_mark_poll = (int (*)(dsnet_client *, dsnet_mark_ev *))dlsym(RTLD_DEFAULT, "dsnet_mark_poll");
}
#define SFX_PING 233                                         /* SFXEnum UI_Alert_Tutorial: a stock UI notice sound */
static ui_pings pings; static ui_spam ping_spam; static int ping_next_id; static uint32_t ping_mute;   /* mute: bit per slot */
static double ping_sound_t[UI_MAX_PLAYERS], ping_cycle_t; static int ping_cycle_k;
static uintptr_t h_mlayer, h_pring[UI_PING_MAX], h_prip[UI_PING_MAX], h_ppin[UI_PING_MAX], h_pname[UI_PING_MAX], h_parrow[UI_PING_MAX], h_ppinbg[UI_PING_MAX], h_parrowt[UI_PING_MAX];
static uintptr_t h_vbr[UI_MAX_PLAYERS][8], h_vname[UI_MAX_PLAYERS], h_vedge[UI_MAX_PLAYERS];
static float vr_cur[UI_MAX_PLAYERS][4]; static int vr_have[UI_MAX_PLAYERS], vr_shown[UI_MAX_PLAYERS], vedge_shown[UI_MAX_PLAYERS];
static int coop_follow = -1; static V3 follow_set_pos; static float follow_set_o; static int follow_set;   /* local follow (no packet) */
static int ping_on_screen[UI_PING_MAX], ping_arrow_on[UI_PING_MAX]; static float ping_arrow_xy[UI_PING_MAX][2];   /* arrow centre, screen px (test probe) */
static float ping_ring_px;                                    /* last ring size (layer units), for the hover test */
static uint32_t coop_key(void) { int m = cur_map(); return m < 0 ? 0 : ui_map_key(m, cur_floor()); }
static int coop_ok(void) { return on() && cur_map() >= 0 && !busy() && !(trn_flow_busy && trn_flow_busy()) && !(map_changing > 0); }
static Obj cam_main(void) { Obj c = F(0x5083208, Obj (*)(void *))(NULL); return u_alive(c) ? c : NULL; }   /* Camera.main */
static V3 w2s(float x, float y) { Obj c = cam_main(); return c ? F(0x50829A8, V3 (*)(Obj, V3, void *))(c, (V3){ x, y, 0 }, NULL) : (V3){ -1e6f, -1e6f, 0 }; }
static V3 s2w(float x, float y) { Obj c = cam_main(); return c ? F(0x5082BE8, V3 (*)(Obj, V3, void *))(c, (V3){ x, y, 10 }, NULL) : (V3){ 0, 0, 0 }; }
static int scr_w(void) { return F(0x5092C94, int (*)(void *))(NULL); }
static int scr_h(void) { return F(0x5092CD4, int (*)(void *))(NULL); }
static V3 mouse_px(void) { return F(0x51AD4DC, V3 (*)(void *))(NULL); }
static void mouse_cell(int *cx, int *cy) { V3 m = mouse_px(), w = s2w(m.x, m.y); *cx = (int)floorf(w.x); *cy = (int)floorf(w.y); }   /* VectorExtensions.ToLogicalPosition: cell = world - 0.5 rounded */
/* screen pixels -> units of a layer (zero-size object at the HUD canvas's top centre; children anchored there) */
static void scr_to_layer(Obj layer, float sx, float sy, float *ux, float *uy) {
    V3 lp = F(0x5109A08, V3 (*)(Obj, void *))(layer, NULL), ls = F(0x510F4F8, V3 (*)(Obj, void *))(layer, NULL);
    float k = ls.x > 0.01f ? ls.x : 1;
    *ux = (sx - lp.x) / k; *uy = (sy - lp.y) / k;
}
static float layer_k(Obj layer) { V3 ls = F(0x510F4F8, V3 (*)(Obj, void *))(layer, NULL); return ls.x > 0.01f ? ls.x : 1; }
static void pivot_centre(Obj o) { Obj rt = u_tr(o); if (ui_is(rt, K_RT)) F(0x51072F4, void (*)(float, float, Obj, void *))(0.5f, 0.5f, rt, NULL); }
static void rot_z(Obj o, float deg) { Obj t = u_tr(o); if (ui_is(t, K_TR)) F(0x510A3B4, void (*)(Obj, V3, void *))(t, (V3){ 0, 0, deg }, NULL); }   /* Transform.localEulerAngles */
static int text_focused(void) { return F(0xD42BBC, bool (*)(void *))(NULL); }   /* KeyInputDispatcher.IsTextInputFocused (mpmenu adds chat / our fields) */
static int over_ui(void) {                                   /* UnityUtil.IsPointerOverUI (the game's test before a world click), or a chat line */
    static int (*chat_over)(void); static unsigned tries;
    if (!chat_over && tries++ % 100 == 0) chat_over = (int (*)(void))dlsym(RTLD_DEFAULT, "mpmenu_chat_over");
    return F(0xE54BA4, bool (*)(void *))(NULL) || (chat_over && chat_over());
}
static void ping_sound(int owner) {
    settings_load();
    if (!uiset.ping_sound || owner < 0 || owner >= UI_MAX_PLAYERS || now_s() - ping_sound_t[owner] < 1.5) return;   /* 2.4: one per sender per 1.5 s */
    ping_sound_t[owner] = now_s();
    static uintptr_t h_sm; Obj sm = find_view(&h_sm, "Refactor.Sound", "SoundManager"), hd = sm ? AT(sm, 0x58, Obj) : NULL;   /* _uiOneTimeSFXSoundHandler */
    if (u_alive(hd)) F(0x8877B0, void (*)(Obj, int, void *))(hd, SFX_PING, NULL);    /* OneTimeSFXSoundHandler.PlayUISFX */
}
static void ping_chat_line(int owner, int slot) {
    settings_load();
    if (!uiset.ping_chat) return;
    static void (*fn)(const char *, int); static unsigned tries;
    if (!fn && tries++ % 50 == 0) fn = (void (*)(const char *, int))dlsym(RTLD_DEFAULT, "mpmenu_chat_system_line");
    if (!fn) return;
    char t[200], nm[96]; ui_players p; players(&p);
    if (owner == me()) snprintf(t, sizeof t, "<color=#a69085>You pinged a spot.</color>");
    else snprintf(t, sizeof t, "%s<color=#a69085> pinged a spot.</color>", pname(&p, owner, nm, sizeof nm));
    fn(t, 1 + (pings.p[slot].owner << 8 | pings.p[slot].id));   /* link = owner + id (still live when clicked?) */
}
/* player units of this map now in the area: they never clear the new ping until they left it once */
static int units_at(uint32_t *hs, int cap, int x, int y, int r) {
    int n = 0; Obj pd = pos_dict(cur_map()); Obj pe = pd ? AT(pd, 0x18, Obj) : NULL; int pn = pd ? AT(pd, 0x20, int32_t) : 0;
    for (int q = 0; pe && q < pn && n < cap; q++) {
        char *e = (char *)pe + 0x20 + q * 0x20;
        if (*(int32_t *)e < 0) continue;
        int pu = 0; for (int k = 0; k < local_n && !pu; k++) pu = !memcmp(e + 8, &local_order[k], 16);
        if (!pu) continue;
        int ux = (int)floorf(*(float *)(e + 0x18)), uy = (int)floorf(*(float *)(e + 0x1C));
        if (abs(ux - x) <= r && abs(uy - y) <= r) hs[n++] = (uint32_t)fnv(1469598103934665603ull, e + 8, 16);
    }
    return n;
}
static void ping_add_local(int owner, int id, uint32_t key, int x, int y) {
    uint32_t in[UI_PING_INSIDE]; int n = units_at(in, UI_PING_INSIDE, x, y, 1);
    int slot = ui_ping_add(&pings, owner, id, key, x, y, now_s(), in, n, 0);
    rig_log("ui: ping %s by %d (id %d) at %d,%d map %08x (%d units already there)", owner == me() ? "placed" : "received", owner, id, x, y, key, n);
    if (owner != me() && (ping_mute >> (owner & 31) & 1u)) return;
    ping_sound(owner); ping_chat_line(owner, slot);
}
static int ping_place(int x, int y) {
    dsnet_client *c = cli();
    if (!coop_ok() || !c) return -1;
    if (!ui_spam_try(&ping_spam, now_s())) { notice("Wait a moment before you ping again."); return -2; }
    int id = ping_next_id++ & 255; uint32_t key = coop_key();
    ping_add_local(me(), id, key, x, y);
    int rc = fn_mark_send ? fn_mark_send(c, DSNET_MARK_ADD, me(), id, key, x, y) : -9;
    if (rc) rig_log("ui: ping send rc %d%s", rc, fn_mark_send ? "" : " (no P_MARK in this build: local only)");
    return 0;
}
static void ping_remove(int slot) {
    dsnet_client *c = cli(); ui_ping *p = &pings.p[slot];
    if (!p->used) return;
    rig_log("ui: ping of %d (id %d) at %d,%d deleted by me", p->owner, p->id, p->x, p->y);
    if (c && fn_mark_send) fn_mark_send(c, DSNET_MARK_DEL, p->owner, p->id, p->map_key, p->x, p->y);
    p->used = 0;
}
static void ping_toggle(int x, int y) {                      /* B: over a ping (within 1 cell) = delete it, else place */
    int k = ui_ping_near(&pings, coop_key(), x, y, 1);
    if (k >= 0) ping_remove(k); else ping_place(x, y);
}
/* CameraSetting holds the camera's target; the camera object moves only when CameraHandlerPresenter.Refresh 0x85B50C
   runs (its update data is not read: it takes GetPosition / GetOrthoSize and calls the view). The game calls it after its
   own camera input; ours call it too (pair run 11: SetPosition alone left the screen where it was). The presenter is
   taken from the game's own calls (hook), forgotten on a map change (a new map makes a new one). */
static uintptr_t h_camp; static void (*o_cam_refresh)(Obj, Obj, void *);
static void hk_cam_refresh(Obj pr, Obj data, void *mi) {
    if (pr && u_gc_get(h_camp) != pr) { u_gc_free(h_camp); h_camp = u_gc_new(pr); }
    o_cam_refresh(pr, data, mi);
}
static void cam_apply(void) { Obj pr = u_gc_get(h_camp); if (pr && o_cam_refresh) o_cam_refresh(pr, NULL, NULL); }
static void cam_jump(float wx, float wy) {                   /* keep the zoom */
    Obj cs = cam_setting(); if (!cs) return;
    V3 cur = F(0xD23584, V3 (*)(Obj, void *))(cs, NULL);
    F(0xD22C54, void (*)(Obj, V3, void *))(cs, (V3){ wx, wy, cur.z }, NULL);
    cam_apply();
}
static void ping_jump(int slot) { ui_ping *p = &pings.p[slot]; if (p->used) cam_jump((float)p->x + 0.5f, (float)p->y + 0.5f); }
static void ping_cycle(void) {                               /* Shift + B: newest live ping, then the next older one */
    int ord[UI_PING_MAX], n = 0; uint32_t key = coop_key();
    for (int i = 0; i < UI_PING_MAX; i++) if (pings.p[i].used && pings.p[i].map_key == key && !(pings.p[i].owner != me() && (ping_mute >> (pings.p[i].owner & 31) & 1u))) ord[n++] = i;
    if (!n) { notice("No ping to look at"); return; }
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (pings.p[ord[b]].t0 > pings.p[ord[a]].t0) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
    if (now_s() - ping_cycle_t > 4) ping_cycle_k = 0;
    ping_cycle_t = now_s();
    ping_jump(ord[ping_cycle_k++ % n]);
}
/* chat line click (mpmenu, dlsym): 1 = the ping is still live and the camera moved there */
int ui_chat_link_click(int link) {
    int owner = (link - 1) >> 8, id = (link - 1) & 255;
    for (int i = 0; i < UI_PING_MAX; i++) if (pings.p[i].used && pings.p[i].owner == owner && pings.p[i].id == id && pings.p[i].map_key == coop_key()) { ping_jump(i); return 1; }
    return 0;
}
static void marks_poll(void) {
    dsnet_client *c = cli(); dsnet_mark_ev ev;
    if (!c || !fn_mark_poll) return;
    while (fn_mark_poll(c, &ev) == 1) {
        if (ev.from < 0 || ev.from >= UI_MAX_PLAYERS) continue;
        if (ev.map_key != coop_key()) { rig_log("ui: ping event from %d for another map (%08x): dropped", ev.from, ev.map_key); continue; }
        if (ev.op == DSNET_MARK_ADD) ping_add_local(ev.from, ev.id, ev.map_key, ev.x, ev.y);
        else if (ev.op == DSNET_MARK_DEL) { int d = ui_ping_del(&pings, ev.owner, ev.id); rig_log("ui: ping of %d (id %d) deleted by %d (%d)", ev.owner, ev.id, ev.from, d); }
    }
}
/* arrival (owner rule): every player unit on this map, by its sim cell (pos_dict), read only */
static void ping_arrival(void) {
    uint32_t key = coop_key();
    if (!ui_ping_count(&pings, -1)) return;
    Obj pd = pos_dict(cur_map()); Obj pe = pd ? AT(pd, 0x18, Obj) : NULL; int pn = pd ? AT(pd, 0x20, int32_t) : 0;
    for (int q = 0; pe && q < pn; q++) {
        char *e = (char *)pe + 0x20 + q * 0x20;
        if (*(int32_t *)e < 0) continue;
        int pu = 0; for (int k = 0; k < local_n && !pu; k++) pu = !memcmp(e + 8, &local_order[k], 16);
        if (!pu) continue;
        int ux = (int)floorf(*(float *)(e + 0x18)), uy = (int)floorf(*(float *)(e + 0x1C)), cl[8];
        int n = ui_ping_arrive(&pings, key, (uint32_t)fnv(1469598103934665603ull, e + 8, 16), ux, uy, cl, 8);
        for (int k = 0; k < n && k < 8; k++) rig_log("ui: ping of %d (id %d) at %d,%d cleared: a unit reached it (%d,%d)", pings.p[cl[k]].owner, pings.p[cl[k]].id, pings.p[cl[k]].x, pings.p[cl[k]].y, ux, uy);
    }
}
/* the ring: a stock marker sprite, else a flat square */
static const char *const ring_names[] = { "SelectionMarker_Neutral", "GridInteractionMarker_Normal" };   /* the game's own selection ring, else its click marker (ui_sprites probe, pair CD 13:46) */
static Obj ring_sprite(void) {
    static uintptr_t h; static double next; Obj sp = u_gc_get(h);
    if (u_alive(sp) || now_s() < next) return u_alive(sp) ? sp : NULL;
    next = now_s() + 5;
    Obj st = type_of("UnityEngine", "Sprite"), arr = st ? F(0x50E8010, Obj (*)(Obj, void *))(st, NULL) : NULL;
    int n = arr ? AT(arr, 0x18, int32_t) : 0;
    for (size_t k = 0; k < sizeof ring_names / sizeof ring_names[0] && !u_alive(sp); k++)
        for (int i = 0; i < n; i++) {
            Obj o = AT(arr, 0x20 + 8 * i, Obj); if (!o) continue;
            char nm[64]; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(o, NULL), nm, sizeof nm);
            if (!strcmp(nm, ring_names[k])) { sp = o; u_gc_free(h); h = u_gc_new(o); rig_log("ui: ping ring sprite %s", nm); break; }
        }
    return u_alive(sp) ? sp : NULL;
}
static Obj mlayer(void) {                                    /* markers: under the HUD (its own canvas, order -50, no raycaster) */
    Obj l = u_gc_get(h_mlayer);
    if (u_alive(l)) return l;
    Obj st = u_gc_get(h_status);
    Obj cv = ui_is(st, K_GR) ? F(0x5260544, Obj (*)(Obj, void *))(st, NULL) : NULL;
    if (cv) cv = F(0x5476FAC, Obj (*)(Obj, void *))(cv, NULL);
    if (!cv) return NULL;
    l = layer_make(st, u_tr(cv), -50);
    u_gc_free(h_mlayer); h_mlayer = u_gc_new(l);
    return l;
}
/* lead (lan_v10): edge marks never sit on a game window or on our boxes. Hidden while a stock window is open (a popup,
   the character window, the Esc menu, our Teams window); slid along the edge out of the portrait bar, the Players panel,
   the Teams button and the viewer box. Rects in screen pixels (overlay canvases: world = screen). */
typedef struct { float x0, y0, x1, y1; } SR;
static int scr_rect(Obj tr, SR *r) {
    if (!u_alive(tr) || !ui_is(tr, K_RT) || !F(0x50F3724, bool (*)(Obj, void *))(u_go(tr), NULL)) return 0;
    RectF4 q = F(0x5106978, RectF4 (*)(Obj, void *))(tr, NULL);
    V3 a = F(0x510E0AC, V3 (*)(Obj, V3, void *))(tr, (V3){ q.x, q.y, 0 }, NULL), b = F(0x510E0AC, V3 (*)(Obj, V3, void *))(tr, (V3){ q.x + q.w, q.y + q.h, 0 }, NULL);   /* Transform.TransformPoint */
    r->x0 = a.x < b.x ? a.x : b.x; r->x1 = a.x < b.x ? b.x : a.x; r->y0 = a.y < b.y ? a.y : b.y; r->y1 = a.y < b.y ? b.y : a.y;
    return r->x1 - r->x0 > 1 && r->y1 - r->y0 > 1;
}
static int edge_blocked(void) {                             /* a stock window (or ours) is open: no edge marks */
    Obj pc = u_gc_get(h_popups), csv = u_gc_get(h_status_ui), ckv = u_gc_get(h_skill_ui);
    static int (*teams_modal)(void); static unsigned tries;
    if (!teams_modal && tries++ % 50 == 0) teams_modal = (int (*)(void))dlsym(RTLD_DEFAULT, "ui_teams_modal_open");
    return (pc && F(0xF62338, bool (*)(Obj, void *))(pc, NULL))
        || (u_alive(csv) && F(0x50F3724, bool (*)(Obj, void *))(u_go(csv), NULL)) || (u_alive(ckv) && F(0x50F3724, bool (*)(Obj, void *))(u_go(ckv), NULL))
        || popup_active(1) || left_prompt || prompt_id || (teams_modal && teams_modal());
}
static int avoid_rects(SR *r, int cap) {
    int n = 0;
    /* the portrait bar = the union of its cards (its parent object has no size of its own: run 9) */
    { SR u = { 1e9f, 1e9f, -1e9f, -1e9f }, q; int any = 0;
      for (int i = 0; i < MAX_SC; i++) { Obj pa = u_gc_get(sc[i].parts); if (!u_alive(pa) || !scr_rect(u_tr(pa), &q)) continue;
          any = 1; if (q.x0 < u.x0) u.x0 = q.x0; if (q.y0 < u.y0) u.y0 = q.y0; if (q.x1 > u.x1) u.x1 = q.x1; if (q.y1 > u.y1) u.y1 = q.y1; }
      if (any && n < cap) r[n++] = u; }
    if (n < cap && pl_shown && scr_rect(u_tr(u_gc_get(h_plate)), &r[n])) n++;
    if (n < cap && viewer_on && scr_rect(u_tr(u_gc_get(h_vbox)), &r[n])) n++;
    static int (*tb)(float *, float *, float *, float *); static unsigned tries;
    if (!tb && tries++ % 50 == 0) tb = (int (*)(float *, float *, float *, float *))dlsym(RTLD_DEFAULT, "ui_teams_button_rect");
    float x, y, w, h; if (n < cap && tb && tb(&x, &y, &w, &h)) { r[n].x0 = x; r[n].y0 = y; r[n].x1 = x + w; r[n].y1 = y + h; n++; }
    return n;
}
/* slide (ax, ay) along its screen edge until a box of half size hs clears every rect; 0 = no free place (hide it) */
static int edge_avoid(float *ax, float *ay, float hs, float sw, float sh) {
    SR r[6]; int n = avoid_rects(r, 6);
    int vert = *ax <= hs * 2 + 30 || *ax >= sw - hs * 2 - 30;   /* on the left / right edge: slide up or down */
    for (int it = 0; it < 6; it++) {
        int hit = -1;
        for (int k = 0; k < n && hit < 0; k++) if (*ax + hs > r[k].x0 && *ax - hs < r[k].x1 && *ay + hs > r[k].y0 && *ay - hs < r[k].y1) hit = k;
        if (hit < 0) return 1;
        if (vert) { float up = r[hit].y1 + hs + 4, dn = r[hit].y0 - hs - 4; *ay = (fabsf(up - *ay) < fabsf(dn - *ay) && up < sh - hs) || dn < hs ? up : dn; }
        else { float lf = r[hit].x0 - hs - 4, rt = r[hit].x1 + hs + 4; *ax = (fabsf(lf - *ax) < fabsf(rt - *ax) && lf > hs) || rt > sw - hs ? lf : rt; }
        if (*ax < hs || *ax > sw - hs || *ay < hs || *ay > sh - hs) return 0;
    }
    return 0;
}
static const char *ping_col(int owner) { return owner == me() ? UI_OWN_ACCENT : ui_player_color(owner); }
static void ping_hide(int i) { show(u_gc_get(h_pring[i]), 0); show(u_gc_get(h_prip[i]), 0); show(u_gc_get(h_ppin[i]), 0); show(u_gc_get(h_pname[i]), 0); show(u_gc_get(h_parrow[i]), 0); show(u_gc_get(h_ppinbg[i]), 0); show(u_gc_get(h_parrowt[i]), 0); ping_on_screen[i] = ping_arrow_on[i] = 0; }
static void pings_draw(void) {
    static int drawn;                                        /* something of ours is up: hide it once there is nothing */
    if (!ui_ping_count(&pings, -1)) { if (drawn) { for (int i = 0; i < UI_PING_MAX; i++) ping_hide(i); drawn = 0; } return; }
    drawn = 1;
    settings_load();
    Obj ml = mlayer(), top = layer_top();
    int vis_all = coop_ok() && ml && top && cam_main();
    uint32_t key = coop_key();
    float sw = (float)scr_w(), sh = (float)scr_h();
    float km = ml ? layer_k(ml) : 1;
    V3 a0 = w2s(0, 0), a1 = w2s(1, 0); float cell = (a1.x - a0.x) / km; if (cell < 4) cell = 4;   /* one cell in layer units */
    ping_ring_px = cell * 1.5f;
    double t = now_s();
    for (int i = 0; i < UI_PING_MAX; i++) {
        ui_ping *p = &pings.p[i];
        int muted = p->owner != me() && (ping_mute >> (p->owner & 31) & 1u);
        if (!vis_all || !p->used || p->map_key != key || muted || (!uiset.pings && p->owner != me())) { ping_hide(i); continue; }
        Obj ring = u_gc_get(h_pring[i]), rip = u_gc_get(h_prip[i]), pin = u_gc_get(h_ppin[i]), nmt = u_gc_get(h_pname[i]), arw = u_gc_get(h_parrow[i]);
        Obj pinbg = u_gc_get(h_ppinbg[i]), arwt = u_gc_get(h_parrowt[i]);
        if (!u_alive(ring) || !u_alive(arw)) {
            /* pin = a diamond in the player's colour on a dark one (#222025: readable on any ground); off screen = a dark
               diamond plate with the game font's chevron in the player's colour (the stock arrow icons are dark art:
               a tint cannot make them a player colour, frame v10_ping_host run 3) */
            Obj rs = ring_sprite();
            ring = img_make(ml, rs, 40, 40); rip = img_make(ml, rs, 40, 40); pinbg = img_make(ml, NULL, 14, 14); pin = img_make(ml, NULL, 9, 9);
            nmt = line_clone(ml, 0.5f, 200, 22, 0); arw = img_make(top, NULL, 26, 26); arwt = line_clone(top, 0.5f, 30, 30, 0);
            Obj all[7] = { ring, rip, pin, nmt, arw, pinbg, arwt };
            for (int k = 0; k < 7; k++) if (all[k]) pivot_centre(all[k]);
            if (ui_is(nmt, K_TMP)) tmp_style(nmt, u_gc_get(h_lora), 14, 514);
            if (ui_is(arwt, K_TMP)) { tmp_style(arwt, u_gc_get(h_lora), 30, 514); tmp_set(arwt, "\u203A"); }
            if (pin) rot_z(pin, 45);
            if (pinbg) { rot_z(pinbg, 45); graphic_color(pinbg, hex_col("#222025", 1)); }
            if (arw) { rot_z(arw, 45); graphic_color(arw, hex_col("#222025", 0.85f)); }
            u_gc_free(h_pring[i]); h_pring[i] = u_gc_new(ring); u_gc_free(h_prip[i]); h_prip[i] = u_gc_new(rip);
            u_gc_free(h_ppin[i]); h_ppin[i] = u_gc_new(pin); u_gc_free(h_pname[i]); h_pname[i] = u_gc_new(nmt); u_gc_free(h_parrow[i]); h_parrow[i] = u_gc_new(arw);
            u_gc_free(h_ppinbg[i]); h_ppinbg[i] = u_gc_new(pinbg); u_gc_free(h_parrowt[i]); h_parrowt[i] = u_gc_new(arwt);
        }
        const char *hex = ping_col(p->owner);
        V3 sp = w2s((float)p->x + 0.5f, (float)p->y + 0.5f);
        float ax = 0, ay = 0, dg = 0;                        /* (written only when off screen: the probe printed garbage, release coop 17:46) */
        int off = ui_edge_arrow(sw, sh, 24 * km, sp.x, sp.y, &ax, &ay, &dg);
        double age = t - p->t0;
        int arrow_ok = off && !edge_blocked() && edge_avoid(&ax, &ay, 16 * km, sw, sh);
        ping_on_screen[i] = !off; ping_arrow_on[i] = arrow_ok; ping_arrow_xy[i][0] = ax; ping_arrow_xy[i][1] = ay;
        if (off && !arrow_ok) { show(ring, 0); show(rip, 0); show(pin, 0); show(pinbg, 0); show(nmt, 0); show(arw, 0); show(arwt, 0); continue; }
        if (!off) {
            float ux, uy; scr_to_layer(ml, sp.x, sp.y, &ux, &uy);
            float pulse = 0.9f - 0.5f * (float)(fmod(age, 2.0) / 2.0);              /* alpha 0.9 -> 0.4 every 2 s */
            int square = !ring_sprite();
            show(ring, 1); place(ring, ux, uy, ping_ring_px, ping_ring_px); graphic_color(ring, hex_col(hex, square ? pulse * 0.35f : pulse));
            if (age < 1.8) {                                  /* 3 ripples, 0.6 s each: 0.5 -> 2 cells, fading */
                float f = (float)fmod(age, 0.6) / 0.6f, d = cell * (0.5f + 1.5f * f);
                show(rip, 1); place(rip, ux, uy, d, d); graphic_color(rip, hex_col(hex, 0.8f * (1 - f) * (square ? 0.4f : 1)));
            } else show(rip, 0);
            float bob = (float)(fmod(age, 2.0) < 0.25 ? 2.0 * sin(fmod(age, 2.0) / 0.25 * 3.14159) : 0);
            show(pinbg, 1); place(pinbg, ux, uy + ping_ring_px / 4 + 14 + bob, 14, 14);
            show(pin, 1); place(pin, ux, uy + ping_ring_px / 4 + 14 + bob, 9, 9); graphic_color(pin, hex_col(hex, 1));
            V3 m = mouse_px(); float mx, my; scr_to_layer(ml, m.x, m.y, &mx, &my);
            int hover = fabsf(mx - ux) < ping_ring_px / 2 + 6 && fabsf(my - uy) < ping_ring_px / 2 + 22;
            int named = age < 4 || hover;
            show(nmt, named);
            if (named) {
                char nm[40], tx[120]; ui_players pl; players(&pl);
                if (p->owner == me()) snprintf(tx, sizeof tx, "<color=%s>You</color>", hex);
                else snprintf(tx, sizeof tx, "<color=%s>%s</color>", hex, ui_player_label(&pl, p->owner, nm));
                line_text(nmt, tx); place(nmt, ux, uy + ping_ring_px / 4 + 34 + bob, 200, 22);
            }
            show(arw, 0); show(arwt, 0);
        } else {
            show(ring, 0); show(rip, 0); show(pin, 0); show(pinbg, 0); show(nmt, 0);
            float ux, uy; scr_to_layer(top, ax, ay, &ux, &uy);
            float grow = age < 2 ? 1.0f + 0.3f * (float)fabs(sin(age * 3.14159)) : 1.0f;   /* first 2 s: 1.0 -> 1.3 twice */
            show(arw, 1); place(arw, ux, uy, 26 * grow, 26 * grow);
            show(arwt, 1); place(arwt, ux + 1, uy + 2, 30, 30); rot_z(arwt, dg);
            char at[64]; snprintf(at, sizeof at, "<color=%s>\u203A</color>", hex); line_text(arwt, at);
            if (ui_is(arwt, K_TMP)) F(0x4F0B340, void (*)(Obj, float, void *))(arwt, 30 * grow, NULL);
        }
    }
}
/* friends' views: P_VIEW out (at most 5 a second, only when it moved; 1 s after the last move; every 5 s), brackets in */
static void view_send(void) {
    dsnet_client *c = cli(); Obj cs = cam_setting();
    if (!c || !cs || !fn_view_send || !coop_ok() || spect()) return;   /* the server drops a spectator's view anyway */
    static double last_send, last_move; static float lx = 1e9f, ly, lo; static uint32_t lk; static int tail;
    V3 pos = F(0xD23584, V3 (*)(Obj, void *))(cs, NULL); float o = F(0xD23590, float (*)(Obj, void *))(cs, NULL);
    double t = now_s(); uint32_t key = coop_key();
    int moved = fabsf(pos.x - lx) > 0.25f || fabsf(pos.y - ly) > 0.25f || fabsf(o - lo) > 0.01f || key != lk;
    if (moved) { last_move = t; tail = 1; }
    int due = (moved && t - last_send >= 0.2) || (tail && !moved && t - last_move >= 1.0) || t - last_send >= 5.0;
    if (!due) return;
    int h = scr_h(); float asp = h > 0 ? (float)scr_w() / (float)h : 1.6f;
    if (fn_view_send(c, key, pos.x, pos.y, o * asp, o) == 0) { last_send = t; lx = pos.x; ly = pos.y; lo = o; lk = key; if (!moved) tail = 0; }
}
static int view_get(int pl, float v[4]) {                    /* a friend's view on this map, 1 = known */
    dsnet_client *c = cli(); uint32_t k = 0; double age = 0;
    if (!c || !fn_view_of || fn_view_of(c, pl, &k, v, &age) != 0) return 0;
    for (int i = 0; i < 4; i++) if (!isfinite(v[i]) || fabsf(v[i]) > 1e5f) return 0;   /* a bad packet never reaches the camera */
    return k == coop_key();
}
static void views_draw(void) {
    settings_load();
    Obj ml = mlayer(), top = layer_top(); Obj cs = cam_setting();
    ui_players p; players(&p);
    int ok = coop_ok() && ml && top && cs && cam_main();
    V3 mp = ok ? F(0xD23584, V3 (*)(Obj, void *))(cs, NULL) : (V3){ 0, 0, 0 }; float mo = ok ? F(0xD23590, float (*)(Obj, void *))(cs, NULL) : 0;
    float sw = (float)scr_w(), sh = (float)scr_h(), km = ml ? layer_k(ml) : 1;
    static double last_t; double t = now_s(); float a = last_t > 0 ? (float)((t - last_t) / 0.2) : 1; if (a > 1) a = 1; last_t = t;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) {
        float v[4];
        int vis = ok && i != me() && (p.present >> i & 1u) && view_get(i, v);
        int same = vis && fabsf(v[0] - mp.x) < 2 && fabsf(v[1] - mp.y) < 2 && fabsf(v[3] - mo) < 0.05f;   /* follow / same view: nothing */
        if (vis && !vr_have[i]) { memcpy(vr_cur[i], v, sizeof v); vr_have[i] = 1; }
        if (vis) for (int k = 0; k < 4; k++) vr_cur[i][k] += (v[k] - vr_cur[i][k]) * a;   /* smoothing (2.8) */
        if (!vis) vr_have[i] = 0;
        const char *hex = ui_player_color(i);
        /* the 4 corners on screen */
        V3 bl = vis ? w2s(vr_cur[i][0] - vr_cur[i][2], vr_cur[i][1] - vr_cur[i][3]) : (V3){ 0, 0, 0 }, tr = vis ? w2s(vr_cur[i][0] + vr_cur[i][2], vr_cur[i][1] + vr_cur[i][3]) : (V3){ 0, 0, 0 };
        int overlap = vis && tr.x > 0 && bl.x < sw && tr.y > 0 && bl.y < sh;
        int brackets = vis && uiset.views && overlap && !same;
        if (brackets && !u_alive(u_gc_get(h_vbr[i][0]))) {
            for (int k = 0; k < 8; k++) { Obj im = img_make(ml, NULL, 2, 16); u_gc_free(h_vbr[i][k]); h_vbr[i][k] = u_gc_new(im); }
            Obj nt = line_clone(ml, 1, 200, 20, 0); if (ui_is(nt, K_TMP)) { rt_top_left(u_tr(nt)); tmp_style(nt, u_gc_get(h_lora), 14, 513); }
            u_gc_free(h_vname[i]); h_vname[i] = u_gc_new(nt);
        }
        for (int k = 0; k < 8; k++) show(u_gc_get(h_vbr[i][k]), brackets);
        show(u_gc_get(h_vname[i]), brackets);
        vr_shown[i] = brackets;
        if (brackets) {
            float x0, y0, x1, y1; scr_to_layer(ml, bl.x, bl.y, &x0, &y0); scr_to_layer(ml, tr.x, tr.y, &x1, &y1);
            float L = 16, T = 2;                             /* each bracket 16 long, 2 thick (2.8) */
            float bx[8] = { x0, x0, x1 - L, x1 - T, x0, x0, x1 - L, x1 - T }, by[8] = { y1, y1, y1, y1, y0 + T, y0 + L, y0 + T, y0 + L };
            float bw[8] = { L, T, L, T, L, T, L, T }, bh[8] = { T, L, T, L, T, L, T, L };
            for (int k = 0; k < 8; k++) { Obj im = u_gc_get(h_vbr[i][k]); place(im, bx[k], by[k], bw[k], bh[k]); graphic_color(im, hex_col(hex, 0.7f)); }
            char nm[40], tx[120]; snprintf(tx, sizeof tx, "<color=%s>%s</color>", hex, ui_player_label(&p, i, nm));
            /* the name stays on screen when their corner is off it (views of the same size overlap a lot) */
            float sx0, sy0, sx1, sy1; scr_to_layer(ml, 8, 8, &sx0, &sy0); scr_to_layer(ml, sw - 8, sh - 8, &sx1, &sy1);
            float nx = x0 + 6 < sx0 ? sx0 : x0 + 6, ny = y1 - 4 > sy1 - 40 ? sy1 - 40 : y1 - 4;
            if (nx > sx1 - 200) nx = sx1 - 200; if (ny < sy0 + 20) ny = sy0 + 20;
            Obj nt = u_gc_get(h_vname[i]); line_text(nt, tx); place(nt, nx, ny, 200, 20);
        }
        /* off-screen friend (option, off by default): a small chevron at the edge toward their view's centre */
        int edge = vis && uiset.offscreen && !overlap;
        float ax = 0, ay = 0, dg = 0;
        if (edge) { V3 c0 = w2s(vr_cur[i][0], vr_cur[i][1]); edge = ui_edge_arrow(sw, sh, 20 * km, c0.x, c0.y, &ax, &ay, &dg) && !edge_blocked() && edge_avoid(&ax, &ay, 10 * km, sw, sh); }
        Obj eg = u_gc_get(h_vedge[i]);
        if (edge && !u_alive(eg)) { eg = img_make(top, NULL, 12, 12); if (eg) { pivot_centre(eg); rot_z(eg, 45); } u_gc_free(h_vedge[i]); h_vedge[i] = u_gc_new(eg); }
        show(eg, edge); vedge_shown[i] = edge; (void)dg;
        if (edge) { float ux, uy; scr_to_layer(top, ax, ay, &ux, &uy); place(eg, ux, uy, 12, 12); graphic_color(eg, hex_col(hex, 0.6f)); }   /* a small diamond in their colour */
    }
}
/* look where a friend looks (keep the zoom), follow (local: their P_VIEW moves this camera; any own camera move stops it) */
static int look_at(int pl) { float v[4]; if (!view_get(pl, v)) return 0; cam_jump(v[0], v[1]); return 1; }
static void follow_stop(int quiet) {
    if (coop_follow < 0) return;
    char nm[40]; ui_players p; players(&p);
    if (!quiet) notice("Stopped following %s.", ui_player_label(&p, coop_follow, nm));
    rig_log("ui: stopped following %d", coop_follow);
    coop_follow = -1; follow_set = 0; follow_target = -1;
}
static void follow_start(int pl) {
    char nm[40]; ui_players p; players(&p);
    coop_follow = pl; follow_set = 0; follow_target = pl;
    rig_log("ui: following %d's view", pl);
    if (!look_at(pl)) notice("%s's view is not known yet: it shows as soon as %s moves the camera", ui_player_label(&p, pl, nm), ui_player_label(&p, pl, nm));
}
static void follow_frame(void) {
    if (coop_follow < 0) return;
    ui_players p; players(&p);
    Obj cs = cam_setting();
    if (!on() || !(p.present >> coop_follow & 1u) || !cs) { follow_stop(1); return; }
    V3 cur = F(0xD23584, V3 (*)(Obj, void *))(cs, NULL); float co = F(0xD23590, float (*)(Obj, void *))(cs, NULL);
    /* the player moved the camera (keys, edge scroll, middle drag, zoom): CameraSetting's target is no longer ours */
    if (follow_set && (fabsf(cur.x - follow_set_pos.x) > 0.05f || fabsf(cur.y - follow_set_pos.y) > 0.05f || fabsf(co - follow_set_o) > 0.01f)) { follow_stop(0); return; }
    float v[4];
    if (!view_get(coop_follow, v)) return;
    if (fabsf(v[0] - cur.x) > 0.01f || fabsf(v[1] - cur.y) > 0.01f || fabsf(v[3] - co) > 0.01f) {
        F(0xD22C54, void (*)(Obj, V3, void *))(cs, (V3){ v[0], v[1], cur.z }, NULL);
        if (v[3] > 0.5f) F(0xD22DB0, void (*)(Obj, float, void *))(cs, v[3], NULL);
        cam_apply();
    }
    follow_set_pos = F(0xD23584, V3 (*)(Obj, void *))(cs, NULL); follow_set_o = F(0xD23590, float (*)(Obj, void *))(cs, NULL); follow_set = 1;   /* after the stock clamp */
}
/* 8k. Spectator mini overview (owner 2026-10-14 ~19:55, board design/project/SpectatorHUDAMini). While a spectator
   follows a player, a small window shows the whole map (a second camera renders it into a texture a few times a
   second), every player's view as a rectangle in their colour, this screen's own view (grey), player units as dots and
   pings as rings. Click on the map: look there (the follow stops; the window stays until Free camera). Title bar: drag
   it (left or right button) to move the window, saved; - / + size; x hides it. O shows / hides it; the cog has
   "Mini overview" and "Overview size". Everything is local to this screen, like the friends' views (8j): the second
   camera only draws, unit positions are read only. A "Following <name>'s view" chip and a thin frame round the screen
   say that the camera is not the spectator's own. */
#define IL_OBJECT_NEW 0x6F96CC                               /* il2cpp_object_new */
#define IL_RESOLVE_ICALL 0x6F8B90                            /* il2cpp_resolve_icall */
#define MINI_TITLE 30
#define MINI_LEGEND 24
#define MINI_DOTS 64
#define MINI_RECTS 8
#define MINI_EVERY 6                                         /* the map camera draws one frame in 6 */
static uintptr_t h_mcam, h_mrt;
static uintptr_t h_mfr, h_mbg, h_mbar, h_mtitle, h_mfloor, h_mimg, h_mlegend, h_mown[4], h_mdot[MINI_DOTS], h_mring[UI_PING_MAX];
static uintptr_t h_mbtn_b[3], h_mbtn_bg[3], h_mbtn_t[3], h_mrect[MINI_RECTS][4], h_mrname[MINI_RECTS];
static uintptr_t h_fchip_b, h_fchip_bg, h_fchip_t, h_fedge[4];
static int mini_on, mini_kept, mini_rt_w, mini_rt_h, mini_renders, mini_ndots, mini_nrings, mini_rect_pl[MINI_RECTS];
static unsigned mini_tick; static float mini_X, mini_Y, mini_W, mini_H; static ui_mini_view mini_v; static int mini_have_v;
static int mini_drag, mini_dragging; static float mini_mx0, mini_my0, mini_x0, mini_y0;
static float mini_rect_at[MINI_RECTS][4], mini_own_at[4]; static int mini_own_on;   /* test probe: map px */
static void *nat(Obj o) { return u_alive(o) ? AT(o, 0x10, void *) : NULL; }   /* Object.m_CachedPtr: the engine object */
static void *icall(const char *n) {
    void *f = F(IL_RESOLVE_ICALL, void *(*)(const char *))(n);
    if (!f) rig_log("ui: mini overview: engine call %s missing", n);
    return f;
}
static Obj new_go(const char *name) {                        /* new GameObject(name), in the active scene */
    void *k = ord_debug_class ? ord_debug_class("UnityEngine", "GameObject") : NULL;
    Obj go = k ? F(IL_OBJECT_NEW, Obj (*)(void *))(k) : NULL;
    if (go) F(0x50F4674, void (*)(Obj, Obj, void *))(go, u_str(name), NULL);   /* GameObject..ctor(string) */
    return u_alive(go) ? go : NULL;
}
/* the map camera: a copy of the main camera's settings (Camera.CopyFrom, engine call: the managed one is stripped),
   orthographic, no UI layer, drawing into its own RenderTexture; disabled except one frame in MINI_EVERY */
static Obj mini_cam(int w, int h) {
    Obj cam = u_gc_get(h_mcam), rt = u_gc_get(h_mrt), main = cam_main();
    if (u_alive(cam) && u_alive(rt) && w == mini_rt_w && h == mini_rt_h) return cam;
    if (!main) return NULL;
    if (!u_alive(cam)) {
        static double next; if (now_s() < next) return NULL;
        next = now_s() + 5;                                  /* a failure retries every 5 s, not every frame */
        Obj tc = type_of("UnityEngine", "Camera"), go = tc ? new_go("DSMP_MiniOverviewCamera") : NULL;
        cam = go ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(go, tc, NULL) : NULL;   /* GameObject.AddComponent */
        if (!u_alive(cam)) { rig_log("ui: mini overview: no camera (go %p)", go); return NULL; }
        static void (*copy)(void *, void *), (*ortho)(void *, bool), (*mask)(void *, int);
        if (!copy) copy = (void (*)(void *, void *))icall("UnityEngine.Camera::CopyFrom_Injected");
        if (!ortho) ortho = (void (*)(void *, bool))icall("UnityEngine.Camera::set_orthographic_Injected");
        if (!mask) mask = (void (*)(void *, int))icall("UnityEngine.Camera::set_cullingMask_Injected");
        if (copy) copy(nat(cam), nat(main));
        if (ortho) ortho(nat(cam), true);
        Obj top = layer_top(); int ul = top ? F(0x50F3118, int (*)(Obj, void *))(u_go(top), NULL) : 5;   /* GameObject.layer */
        int cm = F(0x5080BC8, int (*)(Obj, void *))(main, NULL) & ~(1 << 5) & ~(1 << (ul & 31));   /* Camera.cullingMask */
        if (mask) mask(nat(cam), cm);
        F(0x5081194, void (*)(Obj, int, void *))(cam, 2, NULL);                    /* clearFlags = SolidColor */
        F(0x5080F9C, void (*)(Obj, Col, void *))(cam, hex_col("#141218", 1), NULL); /* backgroundColor */
        F(0x50ECEA0, void (*)(Obj, bool, void *))(cam, false, NULL);               /* Behaviour.enabled */
        u_gc_free(h_mcam); h_mcam = u_gc_new(cam);
        rig_log("ui: mini overview camera %p (copy %d, ortho %d, mask %d, culling 0x%x)", cam, copy != NULL, ortho != NULL, mask != NULL, cm);
    }
    if (!u_alive(rt) || w != mini_rt_w || h != mini_rt_h) {
        void *k = ord_debug_class ? ord_debug_class("UnityEngine", "RenderTexture") : NULL;
        Obj nrt = k ? F(IL_OBJECT_NEW, Obj (*)(void *))(k) : NULL;
        if (nrt) F(0x50D1D9C, void (*)(Obj, int, int, int, void *))(nrt, w, h, 24, NULL);   /* RenderTexture(w, h, 24) */
        if (!u_alive(nrt)) { rig_log("ui: mini overview: no render texture"); return NULL; }
        F(0x50BE5C0, void (*)(Obj, int, void *))(nrt, 1, NULL);                    /* filterMode = Bilinear */
        F(0x5081CB8, void (*)(Obj, Obj, void *))(cam, nrt, NULL);                  /* Camera.targetTexture */
        Obj img = u_gc_get(h_mimg); if (u_alive(img)) F(0x54920B4, void (*)(Obj, Obj, void *))(img, nrt, NULL);   /* RawImage.texture */
        if (u_alive(rt)) { F(0x50CF5B8, void (*)(Obj, void *))(rt, NULL); F(0x50FC8F8, void (*)(Obj, void *))(rt, NULL); }   /* Release, Destroy */
        u_gc_free(h_mrt); h_mrt = u_gc_new(nrt); mini_rt_w = w; mini_rt_h = h;
        rig_log("ui: mini overview texture %dx%d", w, h);
    }
    return cam;
}
static void mini_cam_off(void) { Obj cam = u_gc_get(h_mcam); if (u_alive(cam)) F(0x50ECEA0, void (*)(Obj, bool, void *))(cam, false, NULL); }
static Obj mini_raw(Obj root) {                              /* a RawImage under root (a new GameObject: an Image clone cannot hold one) */
    Obj tr = type_of("UnityEngine.UI", "RawImage"), go = tr ? new_go("DSMP_MiniOverview") : NULL;
    Obj ri = go ? F(0x50F2B90, Obj (*)(Obj, Obj, void *))(go, tr, NULL) : NULL;
    if (!ui_is(ri, K_GR)) { rig_log("ui: mini overview: no RawImage (go %p)", go); return NULL; }
    F(0x50F3204, void (*)(Obj, int, void *))(go, F(0x50F3118, int (*)(Obj, void *))(u_go(root), NULL), NULL);   /* the layer's layer */
    F(0x510B2C4, void (*)(Obj, Obj, bool, void *))(u_tr(ri), root, false, NULL);  /* Transform.SetParent(root, false) */
    Obj rt = u_tr(ri);
    F(0x5106B64, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL);          /* as img_make: top centre, pivot top-left */
    F(0x5106D48, void (*)(float, float, Obj, void *))(0.5f, 1, rt, NULL);
    F(0x51072F4, void (*)(float, float, Obj, void *))(0, 1, rt, NULL);
    graphic_color(ri, (Col){ 1, 1, 1, 1 });
    return ri;
}
static Obj mini_text(Obj root, float size, int align) {
    Obj t = line_clone(root, 1, 100, 22, 0);
    if (ui_is(t, K_TMP)) { rt_top_left(u_tr(t)); tmp_style(t, u_gc_get(h_lora), size, align); }
    return t;
}
static int mini_build(void) {
    if (u_alive(u_gc_get(h_mimg)) && u_alive(u_gc_get(h_mfr))) return 1;
    Obj root = layer_top(); if (!root || !u_alive(u_gc_get(h_lora))) return 0;
    static double next; if (now_s() < next) return 0;
    next = now_s() + 5;
    Obj fr = flat(root, "#915847"), bg = flat(root, "#222025"), bar = flat(root, "#36323a");
    Obj img = mini_raw(root);
    if (!fr || !bg || !bar || !img) return 0;
    u_gc_free(h_mfr); h_mfr = u_gc_new(fr); u_gc_free(h_mbg); h_mbg = u_gc_new(bg); u_gc_free(h_mbar); h_mbar = u_gc_new(bar);
    u_gc_free(h_mimg); h_mimg = u_gc_new(img);
    u_gc_free(h_mtitle); h_mtitle = u_gc_new(mini_text(root, 15, 513));
    u_gc_free(h_mfloor); h_mfloor = u_gc_new(mini_text(root, 12, 516));
    u_gc_free(h_mlegend); h_mlegend = u_gc_new(mini_text(root, 11, 513));
    for (int k = 0; k < 3; k++) {
        Obj b = flat(root, "#633d33"), b2 = flat(root, "#2a262c"), t = mini_text(root, 14, 514);
        u_gc_free(h_mbtn_b[k]); h_mbtn_b[k] = u_gc_new(b); u_gc_free(h_mbtn_bg[k]); h_mbtn_bg[k] = u_gc_new(b2); u_gc_free(h_mbtn_t[k]); h_mbtn_t[k] = u_gc_new(t);
    }
    for (int k = 0; k < 4; k++) { u_gc_free(h_mown[k]); h_mown[k] = u_gc_new(flat(root, "#b5aeb0")); }
    Obj ri = u_gc_get(h_mrt); if (u_alive(ri)) F(0x54920B4, void (*)(Obj, Obj, void *))(img, ri, NULL);
    rig_log("ui: mini overview built");
    return 1;
}
static void mini_hide_all(void) {
    uintptr_t *one[] = { &h_mfr, &h_mbg, &h_mbar, &h_mtitle, &h_mfloor, &h_mimg, &h_mlegend };
    for (size_t k = 0; k < sizeof one / sizeof one[0]; k++) show(u_gc_get(*one[k]), 0);
    for (int k = 0; k < 3; k++) { show(u_gc_get(h_mbtn_b[k]), 0); show(u_gc_get(h_mbtn_bg[k]), 0); show(u_gc_get(h_mbtn_t[k]), 0); }
    for (int k = 0; k < 4; k++) show(u_gc_get(h_mown[k]), 0);
    for (int i = 0; i < MINI_RECTS; i++) { for (int k = 0; k < 4; k++) show(u_gc_get(h_mrect[i][k]), 0); show(u_gc_get(h_mrname[i]), 0); }
    for (int i = 0; i < MINI_DOTS; i++) show(u_gc_get(h_mdot[i]), 0);
    for (int i = 0; i < UI_PING_MAX; i++) show(u_gc_get(h_mring[i]), 0);
    mini_ndots = mini_nrings = 0; mini_own_on = 0;
    for (int i = 0; i < MINI_RECTS; i++) mini_rect_pl[i] = -1;
}
/* a rectangle outline (4 bars of 2) in map px, inside the map area */
static void mini_outline(uintptr_t *h4, const float r[4], const char *hex, float a) {
    float ix = mini_X + 2, iy = mini_Y - 2 - MINI_TITLE, T = 2;
    float x[4] = { r[0], r[0], r[0], r[0] + r[2] - T }, y[4] = { r[1], r[1] + r[3] - T, r[1], r[1] };
    float w[4] = { r[2], r[2], T, T }, h[4] = { T, T, r[3], r[3] };
    for (int k = 0; k < 4; k++) { Obj im = u_gc_get(h4[k]); show(im, 1); place(im, ix + x[k], iy - y[k], w[k], h[k]); graphic_color(im, hex_col(hex, a)); }
}
static int mini_visible(void) { return coop_ok() && spect() && uiset.mini && (coop_follow >= 0 || mini_kept); }
static void mini_place_default(Obj root, float wt, float ht, float *X, float *Y) {
    float rx, ry; scr_to_layer(root, (float)scr_w() - 16, (float)scr_h() * 0.14f, &rx, &ry);   /* bottom right, over the skill keys */
    *X = rx - wt; *Y = ry + ht;
}
static void mini_clamp(Obj root, float wt, float ht, float *X, float *Y) {
    float x0, y0, x1, y1; scr_to_layer(root, 0, (float)scr_h(), &x0, &y0); scr_to_layer(root, (float)scr_w(), 0, &x1, &y1);
    if (*X > x1 - wt) *X = x1 - wt; if (*X < x0) *X = x0;
    if (*Y < y1 + ht) *Y = y1 + ht; if (*Y > y0) *Y = y0;
}
static void follow_chip(int on_) {                           /* "Following the owner's view" at the top centre + the screen frame */
    Obj root = layer_top();
    if (on_ && root && !u_alive(u_gc_get(h_fchip_t)) && u_alive(u_gc_get(h_lora))) {
        u_gc_free(h_fchip_b); h_fchip_b = u_gc_new(flat(root, "#d9be8f")); u_gc_free(h_fchip_bg); h_fchip_bg = u_gc_new(flat(root, "#222025"));
        u_gc_free(h_fchip_t); h_fchip_t = u_gc_new(mini_text(root, 14, 514));
        for (int k = 0; k < 4; k++) { u_gc_free(h_fedge[k]); h_fedge[k] = u_gc_new(flat(root, "#d9be8f")); }
        uintptr_t hs[] = { h_fchip_b, h_fchip_bg, h_fchip_t, h_fedge[0], h_fedge[1], h_fedge[2], h_fedge[3] };   /* clicks go through */
        for (size_t k = 0; k < sizeof hs / sizeof hs[0]; k++) { Obj g = u_gc_get(hs[k]); if (ui_is(g, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(g, false, NULL); }
    }
    Obj b = u_gc_get(h_fchip_b), bg = u_gc_get(h_fchip_bg), t = u_gc_get(h_fchip_t);
    show(b, on_); show(bg, on_); show(t, on_);
    for (int k = 0; k < 4; k++) show(u_gc_get(h_fedge[k]), on_);
    if (!on_ || !root || !u_alive(t)) return;
    ui_players p; players(&p); char nm[40], tx[160];
    snprintf(tx, sizeof tx, "<color=#d9be8f>%s  Following %s's view</color>", pl_dot, ui_player_label(&p, coop_follow, nm));
    line_text(t, tx);
    float w = tmp_pref_w(t) + 28; if (w < 160) w = 160;
    float x0, y0, x1, y1; scr_to_layer(root, 0, (float)scr_h(), &x0, &y0); scr_to_layer(root, (float)scr_w(), 0, &x1, &y1);
    float cy = y0 - 64;                                      /* under the top HUD row */
    place(b, -w / 2, cy, w, 30); place(bg, -w / 2 + 1, cy - 1, w - 2, 28); place(t, -w / 2, cy - 1, w, 28);
    graphic_color(bg, hex_col("#222025", 0.92f));
    float T = 3, ex[4] = { x0, x0, x0, x1 - T }, ey[4] = { y0, y1 + T, y0, y0 }, ew[4] = { x1 - x0, x1 - x0, T, T }, eh[4] = { T, T, y0 - y1, y0 - y1 };
    for (int k = 0; k < 4; k++) { Obj e = u_gc_get(h_fedge[k]); place(e, ex[k], ey[k], ew[k], eh[k]); graphic_color(e, hex_col("#d9be8f", 0.55f)); }
}
/* every frame: the window, its marks, the camera's one-frame-in-6 */
static void mini_frame(void) {
    follow_chip(coop_ok() && spect() && coop_follow >= 0);
    int vis = mini_visible() && mini_build();
    Obj root = layer_top(), cs = cam_setting();
    if (!vis || !root || !cs) { if (mini_on) { mini_hide_all(); mini_cam_off(); mini_on = 0; } mini_drag = mini_dragging = 0; return; }
    settings_load();
    float W = ui_mini_width(uiset.mini_size), H = ui_mini_height(uiset.mini_size), wt = W + 4, ht = MINI_TITLE + H + MINI_LEGEND + 4;
    float X, Y;
    if (uiset.mini_pos_set) { X = (float)uiset.mini_x; Y = (float)uiset.mini_y; } else mini_place_default(root, wt, ht, &X, &Y);
    if (mini_dragging) { X = mini_X; Y = mini_Y; }
    mini_clamp(root, wt, ht, &X, &Y);
    mini_X = X; mini_Y = Y; mini_W = W; mini_H = H; mini_on = 1;
    /* the map: bounds from CameraSetting._mapBounds (+0x40: MinX, MaxX, MinY, MaxY) */
    ui_mini_frame(AT(cs, 0x40, int32_t), AT(cs, 0x44, int32_t), AT(cs, 0x48, int32_t), AT(cs, 0x4C, int32_t), W / H, &mini_v); mini_have_v = 1;
    float sk = layer_k(root) * 1.5f; if (sk < 1) sk = 1; if (sk > 3) sk = 3;   /* texture px per layer unit (sharp on big screens) */
    Obj cam = mini_cam((int)(W * sk), (int)(H * sk));
    if (cam) {
        int draw = mini_tick++ % MINI_EVERY == 0;
        if (draw) {
            Obj mc = cam_main(); float z = mc ? F(0x5109A08, V3 (*)(Obj, void *))(u_tr(mc), NULL).z : -10;
            F(0x5109B04, void (*)(Obj, V3, void *))(u_tr(cam), (V3){ mini_v.cx, mini_v.cy, z }, NULL);   /* Transform.position */
            F(0x50805E8, void (*)(Obj, float, void *))(cam, mini_v.hh, NULL);                          /* orthographicSize */
            mini_renders++;
        }
        F(0x50ECEA0, void (*)(Obj, bool, void *))(cam, draw, NULL);
    }
    /* the window */
    Obj fr = u_gc_get(h_mfr), bg = u_gc_get(h_mbg), bar = u_gc_get(h_mbar), img = u_gc_get(h_mimg);
    show(fr, 1); show(bg, 1); show(bar, 1); show(img, 1);
    place(fr, X, Y, wt, ht); place(bg, X + 2, Y - 2, W, ht - 4); place(bar, X + 2, Y - 2, W, MINI_TITLE);
    graphic_color(bg, hex_col("#222025", 0.95f));
    place(img, X + 2, Y - 2 - MINI_TITLE, W, H);
    Obj tt = u_gc_get(h_mtitle), fl = u_gc_get(h_mfloor), lg = u_gc_get(h_mlegend);
    show(tt, 1); show(fl, 1); show(lg, 1);
    place(tt, X + 12, Y - 6, 120, 22); line_text(tt, "<color=#ccbeb8>Overview</color>");
    char ft[64]; int f = cur_floor();
    if (cur_map() > 0 && f >= 0) snprintf(ft, sizeof ft, "<color=#a69085>Floor %d</color>", f + 1); else snprintf(ft, sizeof ft, "<color=#a69085>Settlement</color>");
    place(fl, X + 2 + W - 3 * 28 - 118, Y - 6, 110, 22); line_text(fl, ft);
    static const char *bl[3] = { "−", "+", "×" };
    for (int k = 0; k < 3; k++) {
        float bx = X + 2 + W - (float)(3 - k) * 28;
        Obj b = u_gc_get(h_mbtn_b[k]), b2 = u_gc_get(h_mbtn_bg[k]), t = u_gc_get(h_mbtn_t[k]);
        show(b, 1); show(b2, 1); show(t, 1);
        place(b, bx, Y - 6, 24, 22); place(b2, bx + 1, Y - 7, 22, 20); place(t, bx, Y - 6, 24, 22);
        char tx[48]; snprintf(tx, sizeof tx, "<color=#ccbeb8>%s</color>", bl[k]); line_text(t, tx);
    }
    /* players' views, in their colour, with the name */
    ui_players p; players(&p);
    char leg[600]; size_t ln = 0; leg[0] = 0;
    int r = 0;
    for (int i = 0; i < UI_MAX_PLAYERS && r < MINI_RECTS; i++) {
        if (!(p.present >> i & 1u)) continue;
        const char *hex = ui_player_color(i); char nm[40]; ui_player_label(&p, i, nm);
        if (ln < sizeof leg - 80) ln += (size_t)snprintf(leg + ln, sizeof leg - ln, "<color=%s>%s</color> %s   ", hex, pl_dot, nm);
        float v[4], q[4];
        int have = uiset.views && view_get(i, v) && ui_mini_rect(&mini_v, W, H, v[0], v[1], v[2], v[3], q);
        if (have && !u_alive(u_gc_get(h_mrect[r][0]))) {
            for (int k = 0; k < 4; k++) { u_gc_free(h_mrect[r][k]); h_mrect[r][k] = u_gc_new(flat(root, hex)); }
            u_gc_free(h_mrname[r]); h_mrname[r] = u_gc_new(mini_text(root, 11, 513));
        }
        if (have) {
            mini_outline(h_mrect[r], q, hex, 1);
            Obj nt = u_gc_get(h_mrname[r]); char tx[80]; snprintf(tx, sizeof tx, "<color=%s>%s</color>", hex, nm);
            show(nt, 1); line_text(nt, tx);
            float ny = q[1] > 14 ? q[1] - 15 : q[1] + 2;     /* above the rectangle, else just inside it */
            place(nt, X + 2 + q[0] + 3, Y - 2 - MINI_TITLE - ny, 120, 14);
            memcpy(mini_rect_at[r], q, sizeof q);
        } else { for (int k = 0; k < 4; k++) show(u_gc_get(h_mrect[r][k]), 0); show(u_gc_get(h_mrname[r]), 0); }
        mini_rect_pl[r] = have ? i : -1;
        r++;
    }
    for (; r < MINI_RECTS; r++) { for (int k = 0; k < 4; k++) show(u_gc_get(h_mrect[r][k]), 0); show(u_gc_get(h_mrname[r]), 0); mini_rect_pl[r] = -1; }
    /* own view (grey; it is the followed player's view while following, so only shown when free) */
    { V3 cp = F(0xD23584, V3 (*)(Obj, void *))(cs, NULL); float co = F(0xD23590, float (*)(Obj, void *))(cs, NULL);
      int h = scr_h(); float asp = h > 0 ? (float)scr_w() / (float)h : 1.6f, q[4];
      mini_own_on = coop_follow < 0 && ui_mini_rect(&mini_v, W, H, cp.x, cp.y, co * asp, co, q);
      if (mini_own_on) { mini_outline(h_mown, q, "#b5aeb0", 0.9f); memcpy(mini_own_at, q, sizeof q); }
      else for (int k = 0; k < 4; k++) show(u_gc_get(h_mown[k]), 0); }
    if (ln < sizeof leg - 120) ln += (size_t)snprintf(leg + ln, sizeof leg - ln, "<color=#b5aeb0>%s</color> You   <color=#a69085>Click: look there</color>", pl_dot);
    place(lg, X + 12, Y - 2 - MINI_TITLE - H - 1, W - 20, 22); line_text(lg, leg);
    /* player units (dots, 5 a second) and pings (rings) */
    if (mini_tick % MINI_EVERY == 1) {
        Obj pd = pos_dict(cur_map()); Obj pe = pd ? AT(pd, 0x18, Obj) : NULL; int pn = pd ? AT(pd, 0x20, int32_t) : 0, nd = 0;
        for (int q = 0; pe && q < pn && nd < MINI_DOTS; q++) {
            char *e = (char *)pe + 0x20 + q * 0x20;
            if (*(int32_t *)e < 0) continue;
            int o = owner_of_guid((const uint8_t *)(e + 8)), ef = eff_of_guid((const uint8_t *)(e + 8));
            if (o != UI_RESERVED && ef >= 0) o = ef;           /* a lent unit shows its controller (lan_v9 colour rule) */
            if (o < 0 || o >= UI_MAX_PLAYERS) continue;
            float mx, my; ui_mini_w2m(&mini_v, W, H, *(float *)(e + 0x18), *(float *)(e + 0x1C), &mx, &my);
            if (mx < 2 || my < 2 || mx > W - 2 || my > H - 2) continue;
            Obj d = u_gc_get(h_mdot[nd]);
            if (!u_alive(d)) { d = flat(root, "#ffffff"); u_gc_free(h_mdot[nd]); h_mdot[nd] = u_gc_new(d); }
            show(d, 1); place(d, X + 2 + mx - 2.5f, Y - 2 - MINI_TITLE - my + 2.5f, 5, 5); graphic_color(d, hex_col(ui_player_color(o), 1));
            nd++;
        }
        for (int k = nd; k < mini_ndots; k++) show(u_gc_get(h_mdot[k]), 0);
        mini_ndots = nd;
        uint32_t key = coop_key(); int nr = 0; Obj rs = ring_sprite();
        for (int i = 0; i < UI_PING_MAX; i++) {
            ui_ping *g = &pings.p[i];
            if (!g->used || g->map_key != key || !uiset.pings) continue;
            float mx, my; ui_mini_w2m(&mini_v, W, H, (float)g->x + 0.5f, (float)g->y + 0.5f, &mx, &my);
            if (mx < 0 || my < 0 || mx > W || my > H) continue;
            Obj rg = u_gc_get(h_mring[nr]);
            if (!u_alive(rg)) { rg = img_make(root, rs, 14, 14); u_gc_free(h_mring[nr]); h_mring[nr] = u_gc_new(rg); }
            show(rg, 1); place(rg, X + 2 + mx - 7, Y - 2 - MINI_TITLE - my + 7, 14, 14); graphic_color(rg, hex_col(ping_col(g->owner), 1));
            nr++;
        }
        for (int k = nr; k < mini_nrings; k++) show(u_gc_get(h_mring[k]), 0);
        mini_nrings = nr;
    }
}
static void mini_set_size(int s) { if (s < UI_MINI_SMALL) s = UI_MINI_SMALL; if (s > UI_MINI_LARGE) s = UI_MINI_LARGE; uiset.mini_size = s; settings_save(); }
static void mini_hide(void) { uiset.mini = 0; settings_save(); notice("Overview hidden. Press O to show it again."); rig_log("ui: mini overview hidden"); }
static void mini_toggle(void) {                              /* O */
    if (mini_visible()) { mini_hide(); return; }
    uiset.mini = 1; settings_save();
    if (coop_follow < 0) mini_kept = 1;                      /* free camera: show it anyway, until Free camera is pressed */
    rig_log("ui: mini overview shown");
}
static void mini_look(float mx, float my) {                  /* a click on the map: look there (the follow stops) */
    float wx, wy; ui_mini_m2w(&mini_v, mini_W, mini_H, mx, my, &wx, &wy);
    if (coop_follow >= 0) follow_stop(0);
    mini_kept = 1;
    cam_jump(wx, wy);
    rig_log("ui: mini overview: look at %.1f,%.1f", wx, wy);
}
/* every frame: O, the buttons, the map click, the title-bar drag (left or right button, as the other windows) */
static void mini_input(void) {
    if (spect() && coop_ok() && F(0x51AD280, bool (*)(int, void *))(111, NULL) && !text_focused()) { mini_toggle(); return; }   /* O */
    if (!mini_on) return;
    Obj root = layer_top(); if (!root) return;
    V3 m = mouse_px(); float ux, uy; scr_to_layer(root, m.x, m.y, &ux, &uy);
    int ld = F(0x51ACDD0, bool (*)(int, void *))(0, NULL), rd = F(0x51ACDD0, bool (*)(int, void *))(1, NULL);
    if (mini_drag) {
        int held = F(0x51ACD80, bool (*)(int, void *))(mini_drag - 1, NULL);
        if (held) {
            float sc = pl_scale(), dx = (m.x - mini_mx0) / sc, dy = (m.y - mini_my0) / sc;
            if (!mini_dragging && dx * dx + dy * dy > 25) mini_dragging = 1;
            if (mini_dragging) { mini_X = mini_x0 + dx; mini_Y = mini_y0 + dy; }
            return;
        }
        if (mini_dragging) {
            float wt = mini_W + 4, ht = MINI_TITLE + mini_H + MINI_LEGEND + 4, X = mini_X, Y = mini_Y; mini_clamp(root, wt, ht, &X, &Y);
            uiset.mini_pos_set = 1; uiset.mini_x = (int)X; uiset.mini_y = (int)Y; settings_save();
            rig_log("ui: mini overview moved to %d,%d", uiset.mini_x, uiset.mini_y);
        }
        mini_drag = mini_dragging = 0; return;
    }
    if (!ld && !rd) return;
    if (ld) for (int k = 0; k < 3; k++) if (ui_hit(u_gc_get(h_mbtn_b[k]), m.x, m.y)) {
        if (k == 0) mini_set_size(uiset.mini_size - 1); else if (k == 1) mini_set_size(uiset.mini_size + 1); else mini_hide();
        return;
    }
    if (ui_hit(u_gc_get(h_mbar), m.x, m.y)) { mini_drag = ld ? 1 : 2; mini_dragging = 0; mini_mx0 = m.x; mini_my0 = m.y; mini_x0 = mini_X; mini_y0 = mini_Y; return; }
    if (ld && ui_hit(u_gc_get(h_mimg), m.x, m.y)) mini_look(ux - (mini_X + 2), (mini_Y - 2 - MINI_TITLE) - uy);
}
static float jf(float v);
/* test: ui_mini {action: o | smaller | bigger | hide | click (fx, fy: 0..1 of the map) | move (x, y) | reset} */
static char *ui_mini_cmd(const char *line) {
    char a[16] = ""; jstr(line, "action", a, sizeof a);
    double fx = 0.5, fy = 0.5, x = 0, y = 0; jnum(line, "fx", &fx); jnum(line, "fy", &fy); jnum(line, "x", &x); jnum(line, "y", &y);
    int rc = 0;
    settings_load();
    if (!strcmp(a, "o")) mini_toggle();
    else if (!strcmp(a, "smaller")) mini_set_size(uiset.mini_size - 1);
    else if (!strcmp(a, "bigger")) mini_set_size(uiset.mini_size + 1);
    else if (!strcmp(a, "hide")) mini_hide();
    else if (!strcmp(a, "click")) { if (mini_on) mini_look((float)fx * mini_W, (float)fy * mini_H); else rc = -1; }
    else if (!strcmp(a, "move")) { uiset.mini_pos_set = 1; uiset.mini_x = (int)x; uiset.mini_y = (int)(y > 0 ? 0 : y); settings_save(); }
    else if (!strcmp(a, "reset")) { uiset.mini_pos_set = 0; settings_save(); }
    else if (a[0]) rc = -1;
    mini_frame();
    char leg[600] = "", chip[200] = "", fl[80] = "";
    str_utf8(tmp_get(u_gc_get(h_mlegend)), leg, sizeof leg); str_utf8(tmp_get(u_gc_get(h_fchip_t)), chip, sizeof chip); str_utf8(tmp_get(u_gc_get(h_mfloor)), fl, sizeof fl);
    Obj chipo = u_gc_get(h_fchip_t);
    Buf b = {0};
    bput(&b, "{\"ok\":true,\"rc\":%d,\"spectator\":%d,\"follow\":%d,\"setting\":%d,\"kept\":%d,\"shown\":%d,\"size\":%d,\"pos_set\":%d,\"x\":%.0f,\"y\":%.0f,\"w\":%.0f,\"h\":%.0f,"
         "\"camera\":%d,\"texture\":[%d,%d],\"renders\":%d,\"view\":[%.2f,%.2f,%.2f,%.2f],\"dots\":%d,\"rings\":%d,\"chip_on\":%d,\"own\":",
         rc, spect(), coop_follow, uiset.mini, mini_kept, mini_on, uiset.mini_size, uiset.mini_pos_set, mini_X, mini_Y, mini_W, mini_H,
         u_alive(u_gc_get(h_mcam)), mini_rt_w, mini_rt_h, mini_renders, jf(mini_v.cx), jf(mini_v.cy), jf(mini_v.hw), jf(mini_v.hh), mini_ndots, mini_nrings,
         u_alive(chipo) && u_go_active_self(u_go(chipo)));
    if (mini_own_on) bput(&b, "[%.1f,%.1f,%.1f,%.1f]", mini_own_at[0], mini_own_at[1], mini_own_at[2], mini_own_at[3]); else bput(&b, "null");
    bput(&b, ",\"rects\":[");
    int n = 0;
    for (int r = 0; r < MINI_RECTS; r++) if (mini_rect_pl[r] >= 0)
        bput(&b, "%s{\"player\":%d,\"r\":[%.1f,%.1f,%.1f,%.1f]}", n++ ? "," : "", mini_rect_pl[r], mini_rect_at[r][0], mini_rect_at[r][1], mini_rect_at[r][2], mini_rect_at[r][3]);
    bput(&b, "],\"legend\":"); bstr(&b, leg); bput(&b, ",\"chip\":"); bstr(&b, chip); bput(&b, ",\"floor\":"); bstr(&b, fl); bput(&b, "}");
    return b.p;
}
/* keys and the middle button, every frame */
static double mid_t; static float mid_x, mid_y; static int mid_ok;
static void coop_input(void) {
    if (!coop_ok()) { mid_ok = 0; return; }
    V3 m = mouse_px();
    if (F(0x51ACDD0, bool (*)(int, void *))(2, NULL)) { mid_t = now_s(); mid_x = m.x; mid_y = m.y; mid_ok = !over_ui(); }   /* middle down */
    if (mid_ok && F(0x51ACE20, bool (*)(int, void *))(2, NULL)) {                                                          /* middle up: a tap, not a pan */
        float dx = m.x - mid_x, dy = m.y - mid_y;
        if (dx * dx + dy * dy < 36 && now_s() - mid_t < 0.3) { V3 w = s2w(mid_x, mid_y); ping_place((int)floorf(w.x), (int)floorf(w.y)); }
        mid_ok = 0;
    }
    if (F(0x51AD280, bool (*)(int, void *))(98, NULL) && !text_focused()) {                                                /* B */
        int shift = F(0x51AD054, bool (*)(int, void *))(304, NULL) || F(0x51AD054, bool (*)(int, void *))(303, NULL);
        if (shift) ping_cycle();
        else if (!over_ui()) { int cx, cy; mouse_cell(&cx, &cy); ping_toggle(cx, cy); }
    }
    if (F(0x51ACDD0, bool (*)(int, void *))(0, NULL)) {                                                                   /* off-screen arrows */
        for (int i = 0; i < UI_PING_MAX; i++) if (ping_arrow_on[i] && ui_hit(u_gc_get(h_parrow[i]), m.x, m.y)) { ping_jump(i); return; }
        for (int i = 0; i < UI_MAX_PLAYERS; i++) if (vedge_shown[i] && ui_hit(u_gc_get(h_vedge[i]), m.x, m.y)) { look_at(i); return; }
    }
}
/* the frame part (10 a second): packets in, arrival, map change, leavers */
static void coop_frame(void) {
    static uint32_t key_seen, present_seen;
    marks_api();
    if (!on()) { memset(&pings, 0, sizeof pings); coop_follow = -1; return; }
    uint32_t key = coop_key();
    if (key != key_seen) { u_gc_free(h_camp); h_camp = 0;   /* the old map's camera presenter */
        int n = ui_ping_clear_other_maps(&pings, key); if (n) rig_log("ui: map changed: %d pings cleared", n); key_seen = key; memset(vr_have, 0, sizeof vr_have); }
    ui_players p; players(&p);
    uint32_t gone = present_seen & ~(p.present | ui_spectators); present_seen = p.present | ui_spectators;   /* spectators ping too */
    for (int i = 0; gone && i < UI_MAX_PLAYERS; i++) if (gone >> i & 1u) { ui_ping_clear_owner(&pings, i); ping_mute &= ~(1u << i); if (coop_follow == i) follow_stop(0); }
    marks_poll();
    if (coop_ok()) ping_arrival();
    view_send();
}
/* the player menu (right click on a friend's row): Follow / Stop following, Mute / Unmute pings */
static int pmenu_open(int pl, float x, float y) {
    if (pl < 0 || pl >= UI_MAX_PLAYERS || pl == me() || !lmenu_build()) return 0;
    int n = 0;
    lrow_act[n] = coop_follow == pl ? LA_UNFOLLOW : LA_FOLLOW; lrow_arg[n++] = pl;
    lrow_act[n] = (ping_mute >> (pl & 31) & 1u) ? LA_UNMUTE : LA_MUTE; lrow_arg[n++] = pl;
    memset(lmenu_unit, 0, 16); lmenu_n = n; lmenu_hover = -1; lmenu_player = pl;
    float w = 240; Obj fr = u_gc_get(h_lmenu);
    show(fr, 1); place(fr, x, y, w, 26.0f * (float)n + 12); u_set_sibling(u_tr(fr), 1 << 20);
    for (int i = 0; i < n; i++) { Obj t = u_gc_get(h_lrow[i]); show(t, 1); place(t, x + 12, y - 6 - 26.0f * (float)i, w - 24, 26); u_set_sibling(u_tr(t), 1 << 20); }
    lmenu_draw();
    rig_log("ui: player menu for %d", pl);
    return 1;
}
static void coop_act(int act, int pl) {
    char nm[40]; ui_players p; players(&p); ui_player_label(&p, pl, nm);
    if (act == LA_FOLLOW) follow_start(pl);
    else if (act == LA_UNFOLLOW) follow_stop(0);
    else if (act == LA_MUTE) { ping_mute |= 1u << (pl & 31); notice("%s's pings are hidden on your screen", nm); }
    else if (act == LA_UNMUTE) { ping_mute &= ~(1u << (pl & 31)); notice("%s's pings show again", nm); }
}
/* test: ui_coop {action: state | ping | toggle | del | cycle | look | follow | unfollow | mute | unmute | probe, x, y, player} */
static float jf(float v) { return isfinite(v) && fabsf(v) < 1e9f ? v : 0; }   /* JSON numbers only (release coop 17:46: a bad float broke the reply) */
static char *ui_coop_cmd(const char *line) {
    char a[16] = "state"; double x = 0, y = 0, pl = -1; jstr(line, "action", a, sizeof a); jnum(line, "x", &x); jnum(line, "y", &y); jnum(line, "player", &pl);
    int rc = 0;
    if (!strcmp(a, "ping")) rc = ping_place((int)x, (int)y);
    else if (!strcmp(a, "toggle")) ping_toggle((int)x, (int)y);
    else if (!strcmp(a, "cycle")) ping_cycle();
    else if (!strcmp(a, "look")) rc = look_at((int)pl) ? 0 : -1;
    else if (!strcmp(a, "follow")) follow_start((int)pl);
    else if (!strcmp(a, "unfollow")) follow_stop(0);
    else if (!strcmp(a, "mute")) coop_act(LA_MUTE, (int)pl);
    else if (!strcmp(a, "unmute")) coop_act(LA_UNMUTE, (int)pl);
    else if (!strcmp(a, "menu")) rc = pmenu_open((int)pl, -300, -300) ? 0 : -1;
    else if (!strcmp(a, "color")) { color_step((int)pl, x < 0 ? -1 : 1); settings_save(); own_sig ^= 1; force_world = 1; }
    else if (!strcmp(a, "set")) {                            /* set {key, value}: a cog setting, as its arrows do */
        char k2[24] = ""; double v = 0; jstr(line, "key", k2, sizeof k2); jnum(line, "value", &v);
        int *f = !strcmp(k2, "pings") ? &uiset.pings : !strcmp(k2, "ping_sound") ? &uiset.ping_sound : !strcmp(k2, "ping_chat") ? &uiset.ping_chat
               : !strcmp(k2, "views") ? &uiset.views : !strcmp(k2, "offscreen") ? &uiset.offscreen : !strcmp(k2, "outline") ? &uiset.outline
               : !strcmp(k2, "see_through") ? &uiset.see_through : NULL;
        if (f) { *f = (int)v; settings_save(); force_world = 1; if (f == &uiset.outline) world_frame(1); } else rc = -1;
    }
    else if (!strcmp(a, "chatlink")) rc = ui_chat_link_click(1 + ((int)pl << 8 | ((int)x & 255))) ? 0 : -1;   /* chatlink {player, x: id}: the chat line's click */
    else if (!strcmp(a, "mapchanging")) map_changing = now_s();   /* test: an agreed change that never changes the map */
    else if (!strcmp(a, "zoom")) { Obj cs = cam_setting(); if (cs && x > 0.5) F(0xD22DB0, void (*)(Obj, float, void *))(cs, (float)x, NULL); cam_apply(); }   /* zoom {x: ortho size} */
    else if (!strcmp(a, "camera")) { Obj cs = cam_setting(); if (cs) { V3 cur = F(0xD23584, V3 (*)(Obj, void *))(cs, NULL); F(0xD22C54, void (*)(Obj, V3, void *))(cs, (V3){ (float)x, (float)y, cur.z }, NULL); cam_apply(); } }
    pings_draw(); views_draw();
    Obj cs = cam_setting(); V3 cp = cs ? F(0xD23584, V3 (*)(Obj, void *))(cs, NULL) : (V3){ 0, 0, 0 }; float co = cs ? F(0xD23590, float (*)(Obj, void *))(cs, NULL) : 0;
    int mx = 0, my = 0; if (cam_main()) mouse_cell(&mx, &my);
    Buf b = {0}; bput(&b, "{\"ok\":true,\"rc\":%d,\"me\":%d,\"key\":%u,\"coop_ok\":%d,\"cam\":[%.2f,%.2f,%.2f],\"screen\":[%d,%d],\"mouse_cell\":[%d,%d],\"follow\":%d,\"mute\":%u,\"net\":%d,\"ring_sprite\":%d,\"outlined\":%d,\"see_through\":%d,\"outline\":%d,\"cam_real\":[%.2f,%.2f],\"map_changing\":%d,\"pings\":[",
                      rc, me(), coop_key(), coop_ok(), jf(cp.x), jf(cp.y), jf(co), scr_w(), scr_h(), mx, my, coop_follow, ping_mute, fn_mark_send != NULL && fn_view_send != NULL, ring_sprite() != NULL,
                      ol_n, uiset.see_through, uiset.outline, cam_main() ? jf(F(0x5109A08, V3 (*)(Obj, void *))(u_tr(cam_main()), NULL).x) : 0.0f,
                      cam_main() ? jf(F(0x5109A08, V3 (*)(Obj, void *))(u_tr(cam_main()), NULL).y) : 0.0f, map_changing > 0);
    int k = 0;
    for (int i = 0; i < UI_PING_MAX; i++) {
        ui_ping *q = &pings.p[i]; if (!q->used) continue;
        V3 sp = w2s((float)q->x + 0.5f, (float)q->y + 0.5f);
        bput(&b, "%s{\"owner\":%d,\"id\":%d,\"cell\":[%d,%d],\"key\":%u,\"inside\":%d,\"on_screen\":%d,\"arrow\":%d,\"arrow_at\":[%.0f,%.0f],\"screen\":[%.0f,%.0f]}", k++ ? "," : "", q->owner, q->id, q->x, q->y, q->map_key, q->ninside, ping_on_screen[i], ping_arrow_on[i], jf(ping_arrow_xy[i][0]), jf(ping_arrow_xy[i][1]), jf(sp.x), jf(sp.y));
    }
    bput(&b, "],\"views\":[");
    k = 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) { float v[4]; if (i == me() || !view_get(i, v)) continue;
        bput(&b, "%s{\"player\":%d,\"view\":[%.2f,%.2f,%.2f,%.2f],\"brackets\":%d,\"edge\":%d}", k++ ? "," : "", i, jf(v[0]), jf(v[1]), jf(v[2]), jf(v[3]), vr_shown[i], vedge_shown[i]); }
    { SR r[6]; int nr = avoid_rects(r, 6); bput(&b, "],\"avoid\":[");
      for (int k2 = 0; k2 < nr; k2++) bput(&b, "%s[%.0f,%.0f,%.0f,%.0f]", k2 ? "," : "", jf(r[k2].x0), jf(r[k2].y0), jf(r[k2].x1), jf(r[k2].y1)); }
    bput(&b, "],\"menu\":[");
    for (int i = 0; i < lmenu_n; i++) { char t[160]; str_utf8(tmp_get(u_gc_get(h_lrow[i])), t, sizeof t); if (i) bput(&b, ","); bstr(&b, t); }
    bput(&b, "]}");
    return b.p;
}

/* Escape (owner, after lan_v8): InputEventHandler.OnEvent(EscapeActionRequested) 0xD94930. The event is local only
   (ord never sends it), so skipping the game's handler changes nothing in the simulation. One Escape closes the top
   one of our boxes (lend menu, then the panel's settings box, then folds the viewer box) and stops there; with none of
   ours open, the game's Escape runs (its popups, then the Esc menu). A focused text field never gets here: the game
   makes no Escape event while a field has the focus (mpmenu.c h_textfocus also covers the chat). */
static void (*o_escape)(Obj, Obj, void *);
static int escape_ours(void) {
    static int (*mm_esc)(void); if (!mm_esc) mm_esc = (int (*)(void))dlsym(RTLD_DEFAULT, "mpmenu_escape_ours");
    if (mm_esc && mm_esc()) { rig_log("ui: Escape taken by mpmenu (chat / lobby rules)"); return 1; }   /* modal: first */
    if (tip_until > 0) { show(u_gc_get(h_tip_box), 0); show(u_gc_get(h_tip_txt), 0); tip_until = 0; }   /* not a box: no stop */
    if (!on()) return 0;
    if (lmenu_n) { lmenu_close(); rig_log("ui: Escape closed the lend menu"); return 1; }
    if (pl_pop) { pl_pop = 0; plist_frame(); rig_log("ui: Escape closed the panel settings"); return 1; }
    if (viewer_on && !viewer_min) { viewer_min = 1; viewer_frame(); rig_log("ui: Escape folded the viewer box"); return 1; }
    return 0;
}
static void hk_escape(Obj h, Obj msg, void *mi) {
    if (escape_ours()) return;
    o_escape(h, msg, mi);
}

/* test: ui_escape {action: state | unfold}: what Escape would close */
static char *ui_escape_cmd(const char *line) {
    char a[16] = "state"; jstr(line, "action", a, sizeof a);
    if (!strcmp(a, "unfold")) { viewer_min = 0; viewer_frame(); }
    Obj b0 = u_gc_get(h_vbtn_b[0]);
    Buf b = {0}; bput(&b, "{\"ok\":true,\"lend_menu\":%d,\"settings\":%d,\"viewer\":%d,\"viewer_folded\":%d,\"viewer_buttons\":%d,\"esc_menu\":%d}",
                      lmenu_n, pl_pop, viewer_on, viewer_min, u_alive(b0) && u_go_active_self(u_go(b0)), popup_active(1));
    return b.p;
}
/* lan_v9 owner: a unit that is not mine and not lent to me can be selected and looked at, but its skill bar
   (QuickSlotUI: skill / ammo / item slots, the get-down button) is greyed out with "Not your unit (owned by X)" above
   it, and the slot clicks do nothing. Its orders were already refused (ui_check_order, mp_core dsnet_may_order).
   QuickSlotUI._selected +0x50 (Guid of the selected unit), _skillSlots +0x30 (the bar's slots). */
static Obj list_at(Obj list, int i); static int list_n(Obj list);
static dis_list qs_dis = { .dim = 1 };
static uintptr_t h_quick, h_qs_line; static int qs_foreign;
static int qs_selected_foreign(Obj v, char *who, size_t cap) {
    static const uint8_t zero[16];
    dsnet_client *c = cli();
    if (!on() || !c || !u_alive(v)) return 0;
    const uint8_t *g = (const uint8_t *)v + 0x50;
    if (!memcmp(g, zero, 16) || dsnet_may_order(c, me(), g)) return 0;
    if (who && spect()) snprintf(who, cap, "You are watching");
    else if (who) not_mine_text(g, who, cap);
    return 1;
}
static void quick_frame(void) {
    Obj v = find_view(&h_quick, "Refactor.UI", "QuickSlotUI");
    char who[96] = "";
    int f = qs_selected_foreign(v, who, sizeof who);
    Obj l = u_gc_get(h_qs_line);
    if (f) {
        dis_apply(&qs_dis, v);
        Obj slots = AT(v, 0x30, Obj), s0 = slots && list_n(slots) > 0 ? list_at(slots, 0) : NULL;
        Obj bar = u_alive(s0) ? u_tr_parent(u_tr(s0)) : NULL;
        if (!u_alive(l) && bar) {
            l = line_clone(bar, 0, 600, 26, 8);
            if (ui_is(l, K_TMP)) tmp_style(l, u_gc_get(h_lora), 18, 514);
            u_gc_free(h_qs_line); h_qs_line = u_gc_new(l);
        }
        char t[140]; snprintf(t, sizeof t, "<color=#d9be8f>%s</color>", who);
        line_text(l, t); show(l, 1);
        static char said[96]; if (!qs_foreign || strcmp(said, who)) { rig_log("ui: skill bar greyed: %s", who); snprintf(said, sizeof said, "%s", who); }
    } else if (qs_foreign) { dis_restore(&qs_dis); show(l, 0); rig_log("ui: skill bar back to normal"); }
    qs_foreign = f;
}
/* the bar's clicks: QuickSlotUI.OnSelect_SkillSlot / OnInteract_SkillSlot / OnClick_AmmoSlot (int) and
   OnClick_ItemQuickSlot (): nothing happens on a foreign unit (a short notice says why) */
static void (*o_qs_sel)(Obj, int, void *), (*o_qs_int)(Obj, int, void *), (*o_qs_ammo)(Obj, int, void *), (*o_qs_item)(Obj, void *);
static int qs_refuse(Obj v) { char who[96]; if (!qs_selected_foreign(v, who, sizeof who)) return 0; notice("%s", who); return 1; }
static void hk_qs_sel(Obj v, int i, void *mi) { if (!qs_refuse(v)) o_qs_sel(v, i, mi); }
static void hk_qs_int(Obj v, int i, void *mi) { if (!qs_refuse(v)) o_qs_int(v, i, mi); }
static void hk_qs_ammo(Obj v, int i, void *mi) { if (!qs_refuse(v)) o_qs_ammo(v, i, mi); }
static void hk_qs_item(Obj v, void *mi) { if (!qs_refuse(v)) o_qs_item(v, mi); }

/* 8i. lan_v8 owner feedback (2026-10-13 evening). */
/* Owner chip on each portrait card (the stock status line stays untouched): a small dark chip at the card's top-left,
   "Free" for a free unit, else a dot in the controller's colour (grey when the owner is away). */
static uintptr_t h_chip_bg[MAX_SC], h_chip_tx[MAX_SC];
static void chips_frame(void) {
    for (int i = 0; i < MAX_SC; i++) {
        Obj pa = u_gc_get(sc[i].parts), u = u_gc_get(sc[i].unit), bg = u_gc_get(h_chip_bg[i]), tx = u_gc_get(h_chip_tx[i]);
        int vis = on() && u_alive(pa) && is_unit(u) && eff_of(u) != me();   /* lan_v10: your own units keep the stock card */
        if (vis && !u_alive(bg)) {
            /* at the portrait picture's bottom-left (the name is at the top, the level badge at the bottom-right) */
            Obj pic = AT(pa, 0x140, Obj);
            bg = img_make(u_alive(pic) ? u_tr(pic) : u_tr(pa), NULL, 20, 16);
            if (bg) {
                graphic_color(bg, hex_col("#222025", 0.85f));
                Obj rt = u_tr(bg);
                F(0x5106B64, void (*)(float, float, Obj, void *))(0, 0, rt, NULL); F(0x5106D48, void (*)(float, float, Obj, void *))(0, 0, rt, NULL);
                F(0x51072F4, void (*)(float, float, Obj, void *))(0, 0, rt, NULL); u_rt_set_pos(rt, 2, 2);
                tx = line_clone(rt, 0.5f, 20, 16, -8);
                if (ui_is(tx, K_TMP)) tmp_style(tx, u_gc_get(h_lora), 11, 514);
            }
            u_gc_free(h_chip_bg[i]); h_chip_bg[i] = u_gc_new(bg); u_gc_free(h_chip_tx[i]); h_chip_tx[i] = u_gc_new(tx);
        }
        show(bg, vis); show(tx, vis);
        if (!vis || !u_alive(tx)) continue;
        if (u_alive(bg)) u_set_sibling(u_tr(bg), 1 << 20);            /* above the dim overlay */
        int o = owner_of(u), e = eff_of(u);
        char t[64]; float w;
        if (o == UI_UNCLAIMED) { snprintf(t, sizeof t, "<color=#ccbeb8>Free</color>"); w = 30; }
        else if (o == UI_RESERVED) { snprintf(t, sizeof t, "<color=#8a8088>%s</color>", pl_dot); w = 16; }
        else { snprintf(t, sizeof t, "<color=%s>%s</color>", ui_player_color(e >= 0 ? e : o), pl_dot); w = 16; }
        line_text(tx, t);
        if (u_alive(bg)) u_rt_set_size(u_tr(bg), w, 16);
        if (ui_is(tx, K_TMP)) u_rt_set_size(u_tr(tx), w, 16);
    }
}

/* Recruitment window (popup 8): a line "Units you recruit are yours", and the refresh timer counted down live.
   The stock presenter sets the timer text only when the window opens (RecruitPresenter.OnActivateView): every 1 s we
   ask it again (GetRemainingRecruitTimeText 0xF26334) and set RecruitView._recruitRerollTimer (+0x58). The presenter
   is found in PopUpViewContainer._presenterContainer (+0x20) ._viewPresenters (+0x18, Dictionary<Type, presenter>). */
static uintptr_t h_recruit_view, h_recruit_line, h_recruit_presenter;
static int bar_hidden[MAX_SC];
static Obj presenter_by_class(const char *cls) {
    Obj pc = u_gc_get(h_popups), vpc = pc ? AT(pc, 0x20, Obj) : NULL, d = vpc ? AT(vpc, 0x18, Obj) : NULL;
    Obj ent = d ? AT(d, 0x18, Obj) : NULL; int n = d ? AT(d, 0x20, int32_t) : 0;
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x18;                   /* {int hash, int next, key, value} */
        Obj v = *(Obj *)(e + 0x10);
        if (*(int32_t *)e >= 0 && v && !strcmp(cls_name(v), cls)) return v;
    }
    return NULL;
}
/* POPULATION rule (lan_v8): the numbers this player sees. Per player / Split: own units / own cap; Shared / Multiplied:
   the clan's units / the pool. 0 = stock numbers (Shared: nothing to change). */
static int pop_state(int *have, int *cap, int *pool, int *mode) {
    dsnet_client *c = cli(); Obj c0 = cdc(), clan = c0 ? AT(c0, 0x50, Obj) : NULL;
    if (!c || !clan || me() < 0) return 0;
    dsnet_rules r; dsnet_rules_get(c, &r);
    ui_players p; players(&p); int n = 0, rank = 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) if (p.present >> i & 1u) { if (i < me()) rank++; n++; }
    int base = AT(clan, 0x3C, int32_t), count = AT(clan, 0x38, int32_t);
    *cap = dsnet_population_cap(r.population, base, n, rank, pool);
    *have = *pool ? count : dsnet_owned_count(c, me());
    *mode = r.population;
    return 1;
}
static const char *pop_mode_text(int m) {
    return m == DSNET_POP_PER_PLAYER ? "Per player" : m == DSNET_POP_SPLIT ? "Split equally" : m == DSNET_POP_MULTIPLIED ? "Multiplied" : "Shared";
}
static int pop_numbers(char *out, size_t cap_, const char *sep) {
    int have, cap, pool, mode;
    if (!pop_state(&have, &cap, &pool, &mode) || mode == DSNET_POP_SHARED) return 0;
    snprintf(out, cap_, "%d%s%d", have, sep, cap);
    return 1;
}
static int pop_hud(char *out, size_t cap_) {                   /* the HUD counter, every rule (Shared: the stock clan/cap) */
    int have, cap, pool, mode;
    if (!pop_state(&have, &cap, &pool, &mode)) return 0;
    snprintf(out, cap_, "%d/%d", have, cap);
    return 1;
}
static void pop_line(char *out, size_t cap_) {
    int have, cap, pool, mode;
    if (!pop_state(&have, &cap, &pool, &mode)) { snprintf(out, cap_, "<color=#d9be8f>Units you recruit are yours</color>"); return; }
    int left = cap - have; if (left < 0) left = 0;
    if (!left) { snprintf(out, cap_, "<color=#d9be8f>Units you recruit are yours. Population full%s (%d/%d): host rule %s</color>",
                          pool ? "" : " for you", have, cap, pop_mode_text(mode)); return; }
    snprintf(out, cap_, "<color=#d9be8f>Units you recruit are yours. %s: %d more (%s)</color>", pool ? "Clan limit" : "Your limit", left, pop_mode_text(mode));
}
/* the HUD's unit counter (SubUI_CampaignOverviewUI._playerCountText +0x38, "Area_Unit/Txt_UnitNum"; the overview is
   taken from the game's own SetPlayerCount call: no view search) follows the rule; and with more than 8 player
   units on the base, the unit bar shows only your own units (the others are in the Teams window) */
static uintptr_t h_campinfo;
static void pop_frame(void) {
    static double next;
    if (!on() || now_s() < next) return;
    next = now_s() + 0.5;
    Obj ov = u_gc_get(h_campinfo), t = u_alive(ov) ? AT(ov, 0x38, Obj) : NULL;   /* the overview the game writes to */
    char pv[32];
    if (ui_is(t, K_TMP) && pop_hud(pv, sizeof pv)) {
        static char said[32]; char cur[32]; str_utf8(tmp_get(t), cur, sizeof cur);
        if (strcmp(cur, pv) && strcmp(said, pv)) { rig_log("ui: HUD units %s -> %s", cur, pv); snprintf(said, sizeof said, "%s", pv); }
        line_text(t, pv);
    }
    int many = cur_map() == 0 && local_n > 8;
    for (int i = 0; i < MAX_SC; i++) {
        Obj pa = u_gc_get(sc[i].parts), u = u_gc_get(sc[i].unit);
        if (!u_alive(pa) || !is_unit(u)) continue;
        int hide_it = many && !may_order(eff_of(u));
        Obj go = u_go(pa);
        if (hide_it && u_go_active_self(go)) u_go_active(go, false);
        else if (!hide_it && !u_go_active_self(go) && bar_hidden[i]) u_go_active(go, true);
        bar_hidden[i] = hide_it;
    }
}
/* What the stock UI / HUD SEE (lead + owner, lan_v8): ClanDataContainer.get_CurrentPopulation 0xFAB3F4 and
   get_MaxPopulation 0xFAB404 return this player's numbers under the rule (Per player / Split: own units / own cap;
   Multiplied: clan / pool), so the recruit window allows a hire the rule allows and the HUD counter shows the rule.
   Inside the simulation (rig_det_in_sim) they return the stock values: the sim never sees ours. The real decision
   stays in the replayed recruit order (ord_mp.c, dsnet_recruit_check, same on every peer). */
int rig_det_in_sim(void) __attribute__((weak_import));
static int (*o_clan_cur)(Obj, void *), (*o_clan_max)(Obj, void *);
static int pop_override(int *have, int *cap) {
    if (!on() || (rig_det_in_sim && rig_det_in_sim())) return 0;
    int pool, mode;
    return pop_state(have, cap, &pool, &mode) && mode != DSNET_POP_SHARED;
}
static int hk_clan_cur(Obj clan, void *mi) { int h, c; return pop_override(&h, &c) ? h : o_clan_cur(clan, mi); }
/* CampaignInfoPresenter.SetPlayerCount(IViewUpdateData) 0xE852DC writes the HUD counter (view +0x10 -> overview +0x30
   -> _playerCountText +0x38; SubUI_CampaignOverviewUI.SetPlayerCount is inlined there). It runs with the stock
   numbers (simulation side), so its text is replaced right after by the rule's numbers; pop_frame keeps it (0.5 s) */
static void (*o_set_player_count)(Obj, Obj, void *);
static void hk_set_player_count(Obj pr, Obj data, void *mi) {
    o_set_player_count(pr, data, mi);
    Obj v = pr ? AT(pr, 0x10, Obj) : NULL, ov = v ? AT(v, 0x30, Obj) : NULL;
    if (!ov) return;
    if (u_gc_get(h_campinfo) != ov) { u_gc_free(h_campinfo); h_campinfo = u_gc_new(ov); rig_log("ui: HUD overview %p", ov); }
    char pv[32]; Obj t = AT(ov, 0x38, Obj);
    if (on() && ui_is(t, K_TMP) && pop_hud(pv, sizeof pv)) line_text(t, pv);
}
static int hk_clan_max(Obj clan, void *mi) { int h, c; return pop_override(&h, &c) ? c : o_clan_max(clan, mi); }
/* RecruitView.OnRecruitCandidate 0xF27E18 (the Recruit button): the same check as the replay, done first so a refused
   hire says why (the stock window would refuse silently at its own cap, e.g. Shared overflow) */
static void (*o_recruit_click)(Obj, int, void *);
static void hk_recruit_click(Obj v, int idx, void *mi) {
    dsnet_client *c = cli(); Obj c0 = cdc(), clan = c0 ? AT(c0, 0x50, Obj) : NULL;
    if (on() && spect()) { notice("You are watching: spectators cannot recruit"); return; }
    if (on() && c && clan) {
        int base = AT(clan, 0x3C, int32_t), count = AT(clan, 0x38, int32_t), own = dsnet_owned_count(c, me()), cap = 0, pool = 1;
        int why = dsnet_recruit_check(c, me(), base, count, own, &cap, &pool);
        rig_log("ui: recruit click: check %d (clan %d/%d, own %d, cap %d, pool %d)", why, count, base, own, cap, pool);
        if (why != DSNET_RECRUIT_OK) {
            dsnet_rules r; dsnet_rules_get(c, &r);
            if (why == DSNET_RECRUIT_UNITS_FULL) notice("You already have %d units: host rule Units per player", own);
            else if (pool) notice("Population full (%d/%d): host rule %s", count, cap, pop_mode_text(r.population));
            else notice("Population full for you (%d/%d): host rule %s", own, cap, pop_mode_text(r.population));
            return;
        }
    }
    o_recruit_click(v, idx, mi);
}
static void recruit_frame(void) {
    static double next;
    if (!on() || now_s() < next) return;
    next = now_s() + 1;
    if (!popup_active(8)) return;                            /* PopUpType Recruit = 8 */
    Obj v = find_view(&h_recruit_view, "Refactor.UI", "RecruitView");
    if (!v || !u_go_active_self(u_go(v))) return;
    Obj pr = u_gc_get(h_recruit_presenter);
    if (!pr) { pr = presenter_by_class("RecruitPresenter"); u_gc_free(h_recruit_presenter); h_recruit_presenter = u_gc_new(pr); }
    Obj tt = AT(v, 0x58, Obj);
    if (pr && ui_is(tt, K_TMP)) {
        Obj txt = F(0xF26334, Obj (*)(Obj, void *))(pr, NULL);
        char nw[64], cur[64]; str_utf8(txt, nw, sizeof nw); str_utf8(tmp_get(tt), cur, sizeof cur);
        if (nw[0] && strcmp(nw, cur)) tmp_set(tt, nw);
    }
    Obj l = u_gc_get(h_recruit_line);
    if (!u_alive(l)) {
        Obj box = tr_find(u_tr(v), "Img_BackGroundBox");
        l = line_clone(box ? box : u_tr(v), 1, 600, 24, -46);
        if (ui_is(l, K_TMP)) tmp_style(l, u_gc_get(h_lora), 16, 514);
        u_gc_free(h_recruit_line); h_recruit_line = u_gc_new(l);
    }
    char pl[200]; pop_line(pl, sizeof pl);
    line_text(l, pl);
    show(l, 1);
    /* the window's own population chip (RecruitView._clanPopulation +0x68): the rule's numbers */
    char pv[32]; if (pop_numbers(pv, sizeof pv, " / ")) { Obj pc2 = AT(v, 0x68, Obj); if (ui_is(pc2, K_TMP)) line_text(pc2, pv); }
}

/* Task Planner (TaskPriorityView) and Consume Policy (ConsumePolicyView): only your own units can be changed. The other
   players' columns are read-only (their toggles off and faded, their portrait faded). TaskPriorityView: _portraits +0x48,
   _rows +0x50 (SubUI_TaskRow._taskToggles +0x20, one per unit), _unitCache +0x60 (List<UnitEntity>, the column order).
   ConsumePolicyView: _policyPortraits +0x48 (SubUI_PolicyPortrait._unitGuid +0x54), _policyRows +0x50
   (SubUI_PolicyRow._checkToggles +0x20, one per unit column). */
static uintptr_t h_task_view, h_policy_view;
static int owners_ro, owners_toggles;                       /* test probe */
static dis_list owners_dis = { .dim = 1 };
static uint64_t owners_sig;
static Obj list_at(Obj list, int i) { Obj items = list ? AT(list, 0x10, Obj) : NULL; return items && i < AT(list, 0x18, int32_t) ? AT(items, 0x20 + 8 * i, Obj) : NULL; }
static int list_n(Obj list) { return list ? AT(list, 0x18, int32_t) : 0; }
static void owner_views_frame(void) {
    static double next;
    if (now_s() < next) return;
    next = now_s() + 0.4;
    Obj tv = on() ? find_view(&h_task_view, "Refactor.UI", "TaskPriorityView") : NULL, pv = on() ? find_view(&h_policy_view, "Refactor.UI", "ConsumePolicyView") : NULL;
    int ta = tv && F(0x50F3724, bool (*)(Obj, void *))(u_go(tv), NULL), pa = pv && F(0x50F3724, bool (*)(Obj, void *))(u_go(pv), NULL);
    if (!ta && !pa) { if (owners_dis.n || owners_dis.ng) dis_restore(&owners_dis); owners_sig = 0; return; }
    int mine[64]; int nu = 0; Obj ports = NULL, rows = NULL;
    if (ta) {
        Obj units = AT(tv, 0x60, Obj); nu = list_n(units); if (nu > 64) nu = 64;
        for (int i = 0; i < nu; i++) { Obj u = list_at(units, i); mine[i] = !is_unit(u) || may_order(eff_of(u)); }
        ports = AT(tv, 0x48, Obj); rows = AT(tv, 0x50, Obj);
    } else {
        ports = AT(pv, 0x48, Obj); rows = AT(pv, 0x50, Obj); nu = list_n(ports); if (nu > 64) nu = 64;
        for (int i = 0; i < nu; i++) { Obj pp = list_at(ports, i); mine[i] = !pp || may_order(eff_of_guid((const uint8_t *)pp + 0x54)); }
    }
    uint64_t sig = 1469598103934665603ull ^ (uint64_t)ta << 1 ^ (uint64_t)list_n(rows) << 8;
    for (int i = 0; i < nu; i++) sig = fnv(sig, &mine[i], sizeof mine[i]);
    for (int i = 0; i < nu; i++) { Obj pp = list_at(ports, i); sig = fnv(sig, &pp, sizeof pp); }
    for (int r = 0; r < list_n(rows); r++) { Obj row = list_at(rows, r); sig = fnv(sig, &row, sizeof row); }
    if (sig == owners_sig) return;
    owners_sig = sig;
    dis_restore(&owners_dis);
    int nd = 0, nro = 0;
    for (int i = 0; i < nu; i++) {
        if (mine[i]) continue;
        nro++;
        Obj pp = list_at(ports, i); if (u_alive(pp)) dis_dim(&owners_dis, pp);
        for (int r = 0; r < list_n(rows); r++) {
            Obj row = list_at(rows, r), tg = row ? list_at(AT(row, 0x20, Obj), i) : NULL;
            if (u_alive(tg)) { dis_apply(&owners_dis, tg); nd++; }
        }
    }
    rig_log("ui: %s: %d of %d unit columns read-only (%d toggles)", ta ? "task planner" : "consume policy", nro, nu, nd);
    owners_ro = nro; owners_toggles = nd;
}

/* 8c. Default player name for the lobby (mpmenu.c): the Mac account's full name, else the login name. */
#include <pwd.h>
#include <unistd.h>
const char *ui_default_player_name(void) {
    static char nm[64];
    struct passwd *pw = getpwuid(getuid());
    const char *g = pw && pw->pw_gecos && pw->pw_gecos[0] ? pw->pw_gecos : pw ? pw->pw_name : getenv("USER");
    snprintf(nm, sizeof nm, "%s", g ? g : "Player");
    char *comma = strchr(nm, ','); if (comma) *comma = 0;
    return nm;
}

/* lan_v10 (core lane): the host is told when a desync report was written (mp_game_desync_report: counter + newest path) */
static void report_frame(void) {
    static uint32_t (*fn)(char *, size_t); static unsigned tries; static uint32_t seen; static int init;
    if (!fn && tries++ % 50 == 0) fn = (uint32_t (*)(char *, size_t))dlsym(RTLD_DEFAULT, "mp_game_desync_report");
    if (!fn) return;
    char path[600] = ""; uint32_t n = fn(path, sizeof path);
    if (!init) { init = 1; seen = n; return; }
    if (n > seen && on() && me() == 0) notice("Desync report saved: %s", path);
    seen = n;
}

/* ================================================================ install + frame */
/* lan_v9 tooltips (research/tooltips_v9.md 3.6, 3.7, 3.9, 3.10, 3.12, 3.14 Save, 3.15): the game's own tooltip window
   (ui_tip.c, game_tip_show_rect). Each frame the first element of ours under the mouse shows its title + body; ui_tip
   hides it 0.2 s after the last call. Test: ui_tip_hover {title} hovers the element with that title (no mouse). */
static char tip_test[64], tip_shown_title[64];
static int tip_done;
static int tip_on(Obj o) { return u_alive(o) && u_go_active_self(u_go(o)); }
/* the Graphic to hit-test for an element: itself, or its first Graphic child (stock buttons such as the notice box's
   ClickMiddleSoundButton or the Esc menu's SubUI_SystemMenuButton are Selectables, not Graphics: runs 54-58 TYPE CHECK) */
static Obj tip_graphic(Obj o) {
    static uintptr_t ht; Obj t = u_gc_get(ht);
    if (!t) { t = type_of("UnityEngine.UI", "Graphic"); if (!t) return NULL; ht = u_gc_new(t); }
    return F(0x50EDDF4, Obj (*)(Obj, Obj, bool, void *))(o, t, true, NULL);           /* GetComponentInChildren(Type, true) */
}
static void tip_at(Obj o, const char *title, const char *body) {
    if (tip_done || !tip_on(o)) return;
    int hov;
    if (tip_test[0]) hov = !strcmp(tip_test, title);
    else { struct { float x, y, z; } m3 = F(0x51AD4DC, __typeof__(m3) (*)(void *))(NULL); Obj g = tip_graphic(o); hov = g && ui_hit(g, m3.x, m3.y); }
    if (!hov) return;
    tip_done = 1; snprintf(tip_shown_title, sizeof tip_shown_title, "%s", title);
    game_tip_show_rect(title, body, u_tr(o));
}
static const char *conn_words(int lvl) {                     /* 3.7: no milliseconds (owner rule) */
    return lvl >= 3 ? "Good connection. In sync." : lvl == 2 ? "Slow connection, or catching up with the game."
         : lvl == 1 ? "Poor connection, or loading, or resyncing." : "Disconnected. The game waits, or the host keeps playing their units.";
}
static void tips_frame(void) {
    game_tip_frame();
    tip_done = 0; tip_shown_title[0] = 0;
    if (!on() || !game_tip_ready()) return;
    ui_players p; players(&p); char nm[40], t[160], b[300];
    /* 3.9 lend menu (on top of the panel) */
    for (int i = 0; i < lmenu_n && i < LM_ROWS; i++) {
        Obj r = u_gc_get(h_lrow[i]);
        if (lrow_act[i] >= LA_FOLLOW) {                     /* lan_v10 player menu */
            ui_player_label(&p, lrow_arg[i], nm);
            if (lrow_act[i] == LA_FOLLOW) { snprintf(t, sizeof t, "Follow %s's View", nm); snprintf(b, sizeof b, "Your camera follows what %s looks at. Move your camera to stop.", nm); }
            else if (lrow_act[i] == LA_UNFOLLOW) { snprintf(t, sizeof t, "Stop Following"); snprintf(b, sizeof b, "Your camera stays where it is. Moving the camera also stops following."); }
            else if (lrow_act[i] == LA_MUTE) { snprintf(t, sizeof t, "Mute %s's Pings", nm); snprintf(b, sizeof b, "Hide %s's pings and their sound on your screen. %s is not told.", nm, nm); }
            else { snprintf(t, sizeof t, "Show %s's Pings", nm); snprintf(b, sizeof b, "Show %s's pings on your screen again.", nm); }
            tip_at(r, t, b);
        }
        else if (lrow_act[i] == LA_LEND) { ui_player_label(&p, lrow_arg[i], nm); snprintf(t, sizeof t, "Lend Control to %s", nm);
                                      snprintf(b, sizeof b, "%s orders this unit until you take it back. It stays your unit.", nm); tip_at(r, t, b); }
        else if (lrow_act[i] == LA_TAKE) tip_at(r, "Take Back Control", "You order this unit again.");
        else if (lrow_act[i] == LA_GIVE) tip_at(r, "Give Back", "Returns control of this unit to its owner.");
        else if (lrow_act[i] == LA_RELEASE) tip_at(r, "Release", "Frees this unit: nobody controls it, and any player can take it.");
    }
    /* 3.6 panel: cog rows first (they sit inside the panel frame) */
    if (pl_pop) {
        static const struct { int id; const char *t, *b; } rows[] = {
            { PL_SEE, "Other Units See-through", "Makes the units you do not control see-through (30 to 90%), so your own units stand out. They never become fully solid. Only on your screen." },
            { PL_OUTL, "Unit Outline", "Draws an outline around units. Your units: in the game's gold. Friends' units: in each friend's colour. Both, or Off. Only on your screen." },
            { PL_PINGS, "Show Pings", "Show the spots other players mark. Mark a spot yourself: click the middle mouse button, or press B. B over a ping removes it." },
            { PL_PSOUND, "Ping Sound", "Play a short sound when someone pings." },
            { PL_PCHAT, "Ping Lines in Chat", "Write \"Ben pinged a spot.\" in the chat. Click the line to look there." },
            { PL_VIEWS, "Show Friends' Views", "Show the corners of what each friend sees, in their colour." },
            { PL_OFFS, "Show Off-screen Friends", "Show a small mark at the edge of the screen toward friends who look somewhere else. Click it to look there." },
            { PL_NAMES, "Names Above Units", "Shows the owner's name above units. Others: only other players' units. All: every unit. Off: none." },
            { PL_TIPS, "Show Tooltips on Hover", "Shows these explanations when you point at a multiplayer button or setting." },
            { PL_SIZE, "Panel Size", "Normal or Compact. Compact makes the Players panel and the Teams button smaller." },
            { PL_CFADE, "Chat Fade Time", "How long a chat message stays on screen (3 to 120 seconds). Press Enter to see the recent messages again." },
            { PL_CJOIN, "Join and Leave Lines", "Shows a chat line when a player joins, leaves or reconnects." },
            { PL_CHAT, "Chat", "Turns the chat on or off on your screen." },
            { PL_UNITS, "Units per Player", "The most units each player can control (1, 2, 3, 4 or 6). Changes apply to everyone at once." },
            { PL_ADMIN, "Admin Mode", "On: you can order every unit and release the units of absent players." },
            { PL_SPECT, "Spectators", "On: anyone you invite can join to watch (at most 8). They see everything, can chat and ping, own no units, and cannot pause or vote. Off: nobody can watch; turning it off removes the spectators (you are asked first)." } };
        for (size_t k = 0; k < sizeof rows / sizeof rows[0]; k++) tip_at(u_gc_get(h_pl[rows[k].id]), rows[k].t, rows[k].b);
        int lb[PL_SETMAX], vl[PL_SETMAX], ag[PL_SETMAX], ns = set_rows(lb, vl, ag);
        for (int k = 0; k < ns; k++) if (ag[k] >= 0) {
            ui_player_label(&p, ag[k], nm); snprintf(t, sizeof t, "%s's Colour", nm);
            snprintf(b, sizeof b, "The colour %s has on your screen: name, pings, view corners, outline. Auto: the colour the game gave. Only you see your choice.", nm);
            tip_at(u_gc_get(h_pl[lb[k]]), t, b); tip_at(u_gc_get(h_pl[vl[k]]), t, b);
        }
    }
    /* lan_v10: a friend's row in the panel */
    if (uiset.list_open && !pl_pop)
        for (int r = 0; r < PL_MAXROWS; r++) if (row_player[r] >= 0 && row_player[r] != me()) {
            ui_player_label(&p, row_player[r], nm);
            if (row_watch[r]) snprintf(b, sizeof b, "%s is watching: a spectator sees everything and can chat and ping, but owns no units and cannot pause or vote.", nm);
            else snprintf(b, sizeof b, "Click to look where %s looks. Right-click for more: Follow %s's view, Mute %s's pings.", nm, nm, nm);
            tip_at(u_gc_get(h_rowbg[r]), nm, b);
        }
    /* lan_v10 markers: pings (on the map / at the edge), friends' views */
    for (int i = 0; i < UI_PING_MAX && !tip_done; i++) {
        ui_ping *q = &pings.p[i]; if (!q->used) continue;
        if (q->owner == me()) { snprintf(t, sizeof t, "Your Ping"); snprintf(b, sizeof b, "Press B over it to remove it. It goes away when a unit reaches it."); }
        else { ui_player_label(&p, q->owner, nm); snprintf(t, sizeof t, "%s's Ping", nm); snprintf(b, sizeof b, "%s marked this spot. It goes away when a unit reaches it, or when a player presses B over it.", nm); }
        if (ping_on_screen[i]) { tip_at(u_gc_get(h_pring[i]), t, b); tip_at(u_gc_get(h_ppin[i]), t, b); }
        if (ping_arrow_on[i]) tip_at(u_gc_get(h_parrow[i]), t, "Click to move your camera there.");
    }
    for (int i = 0; i < UI_MAX_PLAYERS && !tip_done; i++) {
        if (!vr_shown[i] && !vedge_shown[i]) continue;
        ui_player_label(&p, i, nm); snprintf(t, sizeof t, "%s's View", nm);
        snprintf(b, sizeof b, "This is what %s sees now.", nm); if (vr_shown[i]) tip_at(u_gc_get(h_vname[i]), t, b);
        snprintf(b, sizeof b, "%s looks at another part of the map. Click to look there.", nm); if (vedge_shown[i]) tip_at(u_gc_get(h_vedge[i]), t, b);
    }
    for (int r = 0; r < PL_MAXROWS; r++) if (row_player[r] >= 0 && tip_on(u_gc_get(h_bars[r][0])))
        for (int k = 0; k < 3; k++) tip_at(u_gc_get(h_bars[r][k]), ui_player_label(&p, row_player[r], nm), conn_words(row_lvl[r]));
    if (tip_on(u_gc_get(h_bars[PL_MAXROWS][0]))) {           /* header bars (collapsed): every other player, worst first */
        size_t n = 0; b[0] = 0;
        for (int lv = -2; lv <= 3; lv++) for (int r = 0; r < PL_MAXROWS; r++) if (row_lvl[r] == lv && (row_player[r] != me() || lv == -2) && (row_player[r] >= 0 || lv == -2)) {
            char who[40] = "a player"; if (row_player[r] >= 0) ui_player_label(&p, row_player[r], who);
            n += (size_t)snprintf(b + n, n < sizeof b ? sizeof b - n : 0, "%s%s: %s", n ? "\n" : "", who, conn_words(lv));
        }
        for (int k = 0; k < 3; k++) tip_at(u_gc_get(h_bars[PL_MAXROWS][k]), "Connections", b);
    }
    tip_at(u_gc_get(h_gear), "Settings", "Your own display settings for multiplayer. The host also sets Units per Player and Admin Mode here.");
    tip_at(u_gc_get(h_tog), "Show / Hide", "Shows or hides the player list. The header stays.");
    tip_at(u_gc_get(h_pl[PL_HDR]), "Players", "Everyone in this world and their connection. Right-click and drag to move this panel.");
    tip_at(u_gc_get(h_hdrbg), "Players", "Everyone in this world and their connection. Right-click and drag to move this panel.");
    tip_at(u_gc_get(h_pl[PL_NOTE]), "Temporary Control", "A player disconnected and you chose Keep playing. You order their units until they come back; they stay theirs.");
    /* 3.9 skill bar of a unit that is not mine */
    if (qs_foreign) { char who[96] = ""; Obj v = u_gc_get(h_quick); qs_selected_foreign(v, who, sizeof who);
        if (spect()) tip_at(u_gc_get(h_qs_line), "Spectator", "You are watching. You can look at every unit's details, but spectators give no orders.");
        else { snprintf(b, sizeof b, "%s. You can look at its details, but only its controller gives it orders. Ask them to lend it to you.", who);
               tip_at(u_gc_get(h_qs_line), "Not Your Unit", b); } }
    /* 3.10 viewer box */
    if (viewer_on) {
        ui_player_label(&p, viewer_leader, nm);
        if (spect()) tip_at(u_gc_get(h_vtitle), "Spectator", "You see everything and can chat (Enter) and ping (B or the middle mouse button). You own no units and cannot pause or vote.");
        else { snprintf(b, sizeof b, "You have no units on this run. You can watch and chat; %s gives the orders.", nm); tip_at(u_gc_get(h_vtitle), "Watching", b); }
        snprintf(b, sizeof b, "Your camera follows what %s looks at.", nm); tip_at(u_gc_get(h_vbtn_b[0]), "Follow Camera", b);
        tip_at(u_gc_get(h_vbtn_b[1]), "Free Camera", "Move your camera yourself.");
        if (spect()) tip_at(u_gc_get(h_vbtn_b[2]), "Next Player", "Pick the next player to follow.");
        else { snprintf(b, sizeof b, "Sends %s a request to lend you one of their units on this run.", nm); tip_at(u_gc_get(h_vbtn_b[2]), "Ask for a Unit", b); }
    }
    if (spect()) { Obj ck = u_gc_get(h_clock); if (u_alive(ck)) tip_at(AT(ck, 0x40, Obj), "Spectator", "Spectators cannot pause or change speed."); }
    /* 3.12 votes and boxes */
    tip_at(u_gc_get(h_strip_bg), "Map Vote", "Your proposal waits for the other players.");
    Obj nv = u_gc_get(h_notice);
    if (u_alive(nv) && prompt_id) {
        tip_at(AT(nv, 0x30, Obj), "Change Map", "A player wants to go down a floor (or return to the settlement). Everyone goes together, so every player votes; the host breaks a tie. With no answer in 60 seconds, the vote is cancelled.");
    } else if (u_alive(nv) && left_prompt) {
        snprintf(t, sizeof t, "Wait for %s", left_name);
        snprintf(b, sizeof b, "The game stays paused until %s comes back. %s's units stay %s's.", left_name, left_name, left_name);
        tip_at(AT(nv, 0x38, Obj), t, b);
        snprintf(b, sizeof b, "The game goes on. You order %s's units until %s comes back.", left_name, left_name);
        tip_at(AT(nv, 0x48, Obj), "Keep Playing", b);
    }
    if (strstr(status_text, "press >>"))
        tip_at(u_gc_get(h_pl[PL_STATUS]), "Speed Request", "Fast speed needs every player. Press >> to agree.");
    { Obj ev = u_gc_get(h_ev_view);
      if (ev_node && u_alive(ev) && popup_active(22))
          tip_at(AT(ev, 0x40, Obj), "Event Choice", "Who answers this event, from the host's Event Choices rule. In a vote, the host breaks a tie."); }
    if (result_open && result_chooser != me()) {
        ui_player_label(&p, result_chooser, nm);
        snprintf(b, sizeof b, "The player whose unit used the portal chooses who returns. Everyone else watches; when %s confirms, everyone returns together.", nm);
        tip_at(u_gc_get(h_result_pill), "Portal", b);
    }
    /* 3.14 Esc menu Save (guest, greyed) */
    if (me() != 0) { Obj v = u_gc_get(h_sysmenu); Obj sb = u_alive(v) ? AT(v, 0x50, Obj) : NULL;
        tip_at(sb, "Save Campaign", "Only the host saves this world. It is saved on the host's computer."); }
    /* 3.15 portrait chips, recruitment line */
    for (int i = 0; i < MAX_SC && !tip_done; i++) {
        Obj tx = u_gc_get(h_chip_tx[i]), u = u_gc_get(sc[i].unit);
        if (!tip_on(tx) || !is_unit(u)) continue;
        int o = owner_of(u), e = eff_of(u);
        if (o == UI_UNCLAIMED) tip_at(tx, "Free Unit", "Nobody controls this unit. Take it in the Teams window.");
        else { int who = e >= 0 ? e : o; char ttl[40] = "Away"; if (who >= 0) ui_player_label(&p, who, ttl);
               tip_at(tx, ttl, "This player controls the unit. Grey: the player is away."); }
    }
    tip_at(u_gc_get(h_recruit_line), "Recruiting", "Units you recruit are yours. The clan limit and Units per Player both apply (host's Population rule).");
}
/* debug (item 1): where ui_tip.c's rect_point puts the tooltip for our header: the same calls, and the canvas facts */
static char *ui_tip_where_cmd(void) {
    Obj o = u_gc_get(h_pl[PL_HDR]); Obj rect = u_alive(o) ? u_tr(o) : NULL;
    Buf b = {0}; bput(&b, "{\"ok\":true");
    if (rect) {
        RectF4 r = F(0x5106978, RectF4 (*)(Obj, void *))(rect, NULL);
        V3 w = F(0x510E0AC, V3 (*)(Obj, V3, void *))(rect, (V3){ r.x, r.y, 0 }, NULL);
        Obj cv = F(0x5260544, Obj (*)(Obj, void *))(o, NULL), root = cv ? F(0x5476FAC, Obj (*)(Obj, void *))(cv, NULL) : NULL;
        int mode = root ? F(0x5475018, int (*)(Obj, void *))(root, NULL) : -1, mode2 = cv ? F(0x5475018, int (*)(Obj, void *))(cv, NULL) : -1;
        Obj cam = root && mode != 0 ? F(0x5477610, Obj (*)(Obj, void *))(root, NULL) : NULL;
        V2 sp = F(0x5474610, V2 (*)(Obj, V3, void *))(u_alive(cam) ? cam : NULL, w, NULL);
        bput(&b, ",\"rect\":[%.1f,%.1f,%.1f,%.1f],\"world\":[%.2f,%.2f,%.2f],\"root_mode\":%d,\"own_mode\":%d,\"cam\":%d,\"screen\":[%.1f,%.1f]",
             r.x, r.y, r.w, r.h, w.x, w.y, w.z, mode, mode2, u_alive(cam), sp.x, sp.y);
    }
    bput(&b, "}"); return b.p;
}
/* debug (item 5): the dungeon's player lists that EntityEventHandler.CheckGameOver reads (RuntimeDataContainer mf +0xD0
   .GetDataReader(map) 0xD2BD44 -> RuntimeMapData._player +0x68 -> PlayerDataHandler _totalPlayerUnits +0x20,
   _alivePlayerUnits +0x28, _disabledUnits +0x30; List<T>: items +0x10, size +0x18) */
static char *ui_players_lists_cmd(const char *line) {
    double dm = 1; jnum(line, "map", &dm);
    Obj rc = mf ? AT(mf, 0xD0, Obj) : NULL;
    Obj rd = rc ? F(0xD2BD44, Obj (*)(Obj, int, void *))(rc, (int)dm, NULL) : NULL;
    Obj ph = rd ? AT(rd, 0x68, Obj) : NULL;
    Buf b = {0}; bput(&b, "{\"ok\":true,\"reader\":%d,\"handler\":%d", rd != NULL, ph != NULL);
    static const char *nm[3] = { "total", "alive", "disabled" };
    for (int k = 0; ph && k < 3; k++) {
        Obj l = AT(ph, 0x20 + 8 * k, Obj); Obj arr = l ? AT(l, 0x10, Obj) : NULL; int n = l ? AT(l, 0x18, int32_t) : -1;
        bput(&b, ",\"%s\":[", nm[k]);
        for (int i = 0; arr && i < n && i < 16; i++) {
            Obj u = *(Obj *)((char *)arr + 0x20 + 8 * i); char un[64]; unit_name(is_unit(u) ? u : NULL, un, sizeof un);
            bput(&b, "%s", i ? "," : ""); bstr(&b, un);
        }
        bput(&b, "]");
    }
    bput(&b, "}"); return b.p;
}
static char *ui_tip_hover_cmd(const char *line) {             /* test: ui_tip_hover {title}: "" = the real mouse */
    tip_test[0] = 0; jstr(line, "title", tip_test, sizeof tip_test);
    tips_frame();
    Buf b = {0}; bput(&b, "{\"ok\":true,\"ready\":%d,\"shown\":", game_tip_ready()); bstr(&b, tip_shown_title); bput(&b, "}"); return b.p;
}

void ui_frame(void) {
    static unsigned n;
    teams_lookup();
    if (ui_teams_frame) ui_teams_frame();                    /* every frame: it reads mouse-down itself */
    plist_mouse();
    tooltip_frame();
    strip_mouse();
    lend_mouse();
    viewer_mouse();
    coop_input(); follow_frame(); pings_draw(); views_draw();   /* lan_v10 co-op markers (8j), every frame: smooth */
    mini_input(); mini_frame();                              /* spectator mini overview (8k) */
    tips_frame();                                            /* lan_v9: the game-format tooltips (ui_tip.c) */
    if (n++ % 6) return;                                     /* 10 times a second is enough */
    static unsigned m; m++;
    tags_frame();
    status_frame();
    story_frame();
    if (sess()) { map_frame(); result_frame(); left_frame(); mapchange_frame(); }
    coop_frame();
    report_frame();
    pause_frame();
    if (m % 2 == 0) esc_save_frame();
    if (m % 2 == 1) { lend_frame(); viewer_frame(); chips_frame(); }
    recruit_frame(); owner_views_frame(); pop_frame(); quick_frame();
    if (m % 3 == 1) plist_frame();
    if (m % 3 == 2) banner_frame();
    if (m % 5 == 2) { static uint64_t sig_seen; int force = own_sig != sig_seen || force_world; sig_seen = own_sig; force_world = 0; world_frame(force); order_frame(); }
}

void ui_install(void) {
    struct { uintptr_t va; void *h; void **o; const char *who; } hk[] = {
        { 0xF1A67C, hk_sc_init, (void **)&o_sc_init, "ui SubUI_UnitShortCutParts.Initialize" },
        { 0xF1AEB0, hk_sc_name, (void **)&o_sc_name, "ui SubUI_UnitShortCutParts.RefreshUnitName" },
        { 0xF1B91C, hk_sc_click, (void **)&o_sc_click, "ui SubUI_UnitShortCutParts.OnPointerClick" },
        { 0xE16944, hk_hud_init, (void **)&o_hud_init, "ui UnitEntityHUD.Init" },
        { 0xE865E8, hk_clock_init, (void **)&o_clock_init, "ui SubUI_CampaignClockUI.Initialize" },
        { 0xF62554, hk_activate_view, (void **)&o_activate_view, "ui PopUpViewContainer.ActivateView" },
        { 0xF65A18, hk_cn_confirm, (void **)&o_cn_confirm, "ui CommonNoticeView.OnClick_Confirm" },
        { 0xF65638, hk_cn_cancel, (void **)&o_cn_cancel, "ui CommonNoticeView.OnClick_Cancle" },
        { 0xF5A538, hk_set_timelock, (void **)&o_set_timelock, "ui PopUpUI_Base.SetTimeLock" },
        { 0xF5A6F0, hk_unset_timelock, (void **)&o_unset_timelock, "ui PopUpUI_Base.UnsetTimeLock" },
        { 0xC9F8B8, hk_lu_target, (void **)&o_lu_target, "ui LevelUpHelper.SetLevelUpTargetUnit" },
        { 0xD895C4, hk_trade_req, (void **)&o_trade_req, "ui EntityEventHandler.OnEvent(TradeRequested)" },
        { 0xEDDBE0, hk_ev_set_event, (void **)&o_ev_set_event, "ui EventNodeUIView.SetEventUI" },
        { 0xEDD9D0, hk_ev_set_choices, (void **)&o_ev_set_choices, "ui EventNodeUIView.SetChoices" },
        { 0xD9C6B4, hk_interact_bld, (void **)&o_interact_bld, "ui InteractionEventHandler.OnEvent(InteractBuildingRequested)" },
        { 0xEF8694, hk_forced_return, (void **)&o_forced_return, "ui ForcedBaseReturnView.OnClick_ReturnBase" },
        { 0x355E84C, hk_pop_enqueue, (void **)&o_pop_enqueue, "ui ViewObjectLoader<Int32Enum>.EnqueueActivatePool" },
        { 0xEA2460, hk_result_return, (void **)&o_result_return, "ui DungeonResultView.OnClick_Return" },
        { 0xEA2C1C, hk_result_godown, (void **)&o_result_godown, "ui DungeonResultView.OnClick_GoDown" },
        { 0xF13234, hk_pause_speed, (void **)&o_pause_speed, "ui PauseUIPresenter.OnSpeedChanged" },
        { 0xE26CFC, hk_uo_sethud, (void **)&o_uo_sethud, "ui UnitObject.SetHUD" },
        { 0xDBD6C4, hk_select_unit, (void **)&o_select_unit, "ui PlayerControlEventHandler.OnEvent(SelectPlayerUnitRequested)" },
        { 0xD54B7C, hk_sel_select, (void **)&o_sel_select, "ui SelectProcessor.Select" },
        { 0xD54574, hk_sel_toggle, (void **)&o_sel_toggle, "ui SelectProcessor.Toggle" },
        { 0xEF3A80, hk_exp_activate, (void **)&o_exp_activate, "ui SubUI_SelectRegionUI.InitOnActivate" },
        { 0xEF42E0, hk_exp_setunit, (void **)&o_exp_setunit, "ui SubUI_SelectRegionUI.SetUnit" },
        { 0xEDF334, hk_ev_click, (void **)&o_ev_click, "ui EventNodeUIView.OnClickChoice" },
        { 0xFAB3F4, hk_clan_cur, (void **)&o_clan_cur, "ui ClanDataContainer.get_CurrentPopulation" },
        { 0xFAB404, hk_clan_max, (void **)&o_clan_max, "ui ClanDataContainer.get_MaxPopulation" },
        { 0xF27E18, hk_recruit_click, (void **)&o_recruit_click, "ui RecruitView.OnRecruitCandidate" },
        { 0xF2195C, hk_qs_sel, (void **)&o_qs_sel, "ui QuickSlotUI.OnSelect_SkillSlot" },
        { 0xF219F4, hk_qs_int, (void **)&o_qs_int, "ui QuickSlotUI.OnInteract_SkillSlot" },
        { 0xF21A88, hk_qs_ammo, (void **)&o_qs_ammo, "ui QuickSlotUI.OnClick_AmmoSlot" },
        { 0xF21B30, hk_qs_item, (void **)&o_qs_item, "ui QuickSlotUI.OnClick_ItemQuickSlot" },
        { 0xD94930, hk_escape, (void **)&o_escape, "ui InputEventHandler.OnEvent(EscapeActionRequested)" },
        { 0xE852DC, hk_set_player_count, (void **)&o_set_player_count, "ui CampaignInfoPresenter.SetPlayerCount" },
        { 0x85B50C, hk_cam_refresh, (void **)&o_cam_refresh, "ui CameraHandlerPresenter.Refresh" },   /* lan_v10 camera jump / follow */
        { 0xE139C8, hk_eg_apply, (void **)&o_eg_apply, "ui EntityGraphic.ApplyMaterialProperties" },   /* lan_v10 outline */
    };
    ui_color_override = color_override_of;                   /* lan_v10: this viewer's colours for other players */
    int bad = 0;
    for (size_t i = 0; i < sizeof hk / sizeof hk[0]; i++) bad |= rig_hook(hk[i].va, hk[i].h, hk[i].o, hk[i].who) != 0;
    rig_log("ui: %zu hooks%s; lobby api %s, dsnet views %s", sizeof hk / sizeof hk[0], bad ? " (SOME FAILED)" : "",
            mp_game_host && mp_game_join && mp_game_session ? "ok" : "missing",
            dsnet_present_mask && dsnet_vote_of && dsnet_player_name ? "ok" : "missing");
}
