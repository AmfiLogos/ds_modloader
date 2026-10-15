// mpmenu.c - the in-game Multiplayer menu (build 25846389, arm64, dylib only). Rules: mpmenu_model.c.
//  Title screen: clones of the stock "Credits" button (Multiplayer, Host, host settings, Back, guest status + Cancel).
//  Host flow: settings on the title -> the stock load list or new-campaign screen -> once the campaign is in, the
//  session starts and the lobby panel opens: the stock Esc menu (SystemMenuView, popup 1) with its buttons hidden,
//  a text line (players + settings) and clones Invite friends / Start / Leave multiplayer.
//  Guest: after a Steam invite is accepted, the title shows "Joining the host's game..." + Cancel.
//  In game: the Esc menu gets "Invite friends" (host) and "Leave multiplayer".
// Clicks: two hooks, SubUI_SystemMenuButton.OnPressButton (Esc-menu clones) and SubUI_TitleMenuButton.OnPointerClick
// (title clones; the ui lane gave the title screen to this file). Views are found with FindObjectOfType (no Init hooks).
#include "mpmenu.h"
#include "mp_game.h"
#include "mp_steam.h"
#include "rig.h"
#include "ui_model.h"
#include "ui_tip.h"                                      /* game_tip_show_rect: our tooltips in the game's own window */                                    /* ui_player_color, ui_conn_level / ui_conn_color (one indicator) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreGraphics/CoreGraphics.h>

/* Typed calls are class-checked (lead 2026-10-13, after a Graphic.set_color on a Transform crashed a copy): mm_is(o, K_x)
 * = il2cpp_object_get_class 0x6F96C0 + il2cpp_class_is_assignable_from 0x6F8BE0; a mismatch is logged once and the call
 * is skipped (its default value is used). */
enum { K_RT, K_TR, K_COMP, K_GO, K_TMP, K_FIELD, K_GR, K_IMG, K_BEH, K_SEL, K_SMB, K_SMV, K_TMV, K_CNV, K_N };
static const struct { const char *ns, *name; } mm_kinds[K_N] = {
    { "UnityEngine", "RectTransform" },
    { "UnityEngine", "Transform" },
    { "UnityEngine", "Component" },
    { "UnityEngine", "GameObject" },
    { "TMPro", "TMP_Text" },
    { "TMPro", "TMP_InputField" },
    { "UnityEngine.UI", "Graphic" },
    { "UnityEngine.UI", "Image" },
    { "UnityEngine", "Behaviour" },
    { "UnityEngine.UI", "Selectable" },
    { "Refactor.UI", "SubUI_SystemMenuButton" },
    { "Refactor.UI", "SystemMenuView" },
    { "Refactor.UI", "TitleMenuView" },
    { "Refactor.UI", "CommonNoticeView" }
};
static void *class_by_name(const char *ns, const char *name);
static int mm_is(Obj o, int k) {
    static void *kc[K_N]; static int warned[K_N];
    if (!o || !F(0x50FAF88, bool (*)(Obj, void *))(o, NULL)) return 0;    /* destroyed (scene load): its calls throw NRE (u_alive) */
    if (!kc[k]) kc[k] = class_by_name(mm_kinds[k].ns, mm_kinds[k].name);
    void *oc = F(0x6F96C0, void *(*)(Obj))(o);
    if (kc[k] && oc && F(0x6F8BE0, bool (*)(void *, void *))(kc[k], oc)) return 1;
    if (!warned[k]) { warned[k] = 1; rig_log("mpmenu: typed call skipped: a %s where %s.%s was expected", oc ? *(const char **)((char *)oc + 0x10) : "?", mm_kinds[k].ns, mm_kinds[k].name); }
    return 0;
}
/* ---------------------------------------------------------------- Unity / il2cpp (RVAs as ui_game.c, ~/ds_multibar game.h) */
static inline Obj u_str(const char *s) { return F(0x6F9738, Obj (*)(const char *))(s); }
static inline uintptr_t u_gc_new(Obj o) { return o ? F(0x6F9540, uintptr_t (*)(Obj, bool))(o, false) : 0; }
static inline Obj u_gc_get(uintptr_t h) { return h ? F(0x6F9598, Obj (*)(uintptr_t))(h) : NULL; }
static inline void u_gc_free(uintptr_t h) { if (h) F(0x6F95E4, void (*)(uintptr_t))(h); }
static inline bool u_alive(Obj o) { return o && F(0x50FAF88, bool (*)(Obj, void *))(o, NULL); }
static inline Obj u_go(Obj comp) { return (mm_is(comp, K_COMP) ? F(0x50ED970, Obj (*)(Obj, void *))(comp, NULL) : NULL); }
static inline Obj u_tr(Obj comp) { return (mm_is(comp, K_COMP) ? F(0x50ED864, Obj (*)(Obj, void *))(comp, NULL) : NULL); }
static inline void u_go_active(Obj go, bool v) { if (go) (mm_is(go, K_GO) ? F(0x50F3520, void (*)(Obj, bool, void *))(go, v, NULL) : (void)0); }
static inline bool u_go_active_self(Obj go) { return go && (mm_is(go, K_GO) ? F(0x50F3638, bool (*)(Obj, void *))(go, NULL) : false); }
static inline void u_active(Obj comp, bool v) { if (u_alive(comp) && u_go_active_self(u_go(comp)) != v) u_go_active(u_go(comp), v); }
static inline bool u_on(Obj comp) { return u_alive(comp) && u_go_active_self(u_go(comp)); }
static inline Obj u_instantiate(Obj orig, Obj parent_tr) { return F(0x50FC30C, Obj (*)(Obj, Obj, void *))(orig, parent_tr, NULL); }
static inline Obj u_tr_parent(Obj t) { return (mm_is(t, K_TR) ? F(0x510992C, Obj (*)(Obj, void *))(t, NULL) : NULL); }
static inline int u_sibling(Obj t) { return (mm_is(t, K_TR) ? F(0x510F070, int (*)(Obj, void *))(t, NULL) : 0); }
static inline void u_set_sibling(Obj t, int i) { (mm_is(t, K_TR) ? F(0x510EE44, void (*)(Obj, int, void *))(t, i, NULL) : (void)0); }
typedef struct { float x, y; } V2;
typedef struct { float x, y, w, h; } RectF2;
typedef struct { float x, y, z; } V3;
static int rect_of(Obj comp, float r[4]);
static double mono_s(void);
static const char *palette_color(int idx);
static const char *cls_name(Obj o) { return o ? *(const char **)((char *)*(void **)o + 0x10) : ""; }
/* TMP_Text.text: virtual slots 65 (get, +0x548) / 66 (set, +0x558) */
static Obj tmp_get(Obj t) {
    if (!u_alive(t) || !mm_is(t, K_TMP)) return NULL;
    void *k = *(void **)t;
    return (*(Obj (**)(Obj, void *))((char *)k + 0x548))(t, *(void **)((char *)k + 0x550));
}
static void tmp_set(Obj t, const char *s) {
    if (!u_alive(t) || !mm_is(t, K_TMP)) return;
    if (!strcmp(cls_name(t), "DASText")) AT(t, 0x7C0, bool) = false;     /* DASText._useTextKey: keep our raw text */
    void *k = *(void **)t;
    (*(void (**)(Obj, Obj, void *))((char *)k + 0x558))(t, u_str(s), *(void **)((char *)k + 0x560));
}
static void str_utf8(Obj s, char *out, size_t cap) {          /* ASCII-safe copy of an il2cpp string (read-back) */
    size_t n = 0; if (cap) out[0] = 0;
    if (!s || !cap) return;
    int32_t len = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    for (int32_t i = 0; i < len && n + 1 < cap; i++) out[n++] = c[i] < 0x80 ? (char)c[i] : '?';
    out[n] = 0;
}
/* class lookup over the loaded assemblies (il2cpp API exports) + FindObjectOfType(Type, includeInactive) */
static void *class_by_name(const char *ns, const char *name);
typedef struct { float r, g, b, a; } Col;
static Obj image_of(Obj comp) {                                            /* GetComponent(typeof(UnityEngine.UI.Image)) */
    static void *k; if (!k) k = class_by_name("UnityEngine.UI", "Image");
    if (!k || !u_alive(comp)) return NULL;
    Obj ty = F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k));
    return (mm_is(comp, K_COMP) ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(comp, ty, NULL) : NULL);
}

static void *class_by_name(const char *ns, const char *name) {
    void *dom = F(0x6F91F4, void *(*)(void))();
    size_t n = 0;
    void **as = F(0x6F9200, void **(*)(void *, size_t *))(dom, &n);
    for (size_t i = 0; as && i < n; i++) {
        void *img = F(0x6F8BBC, void *(*)(void *))(as[i]);
        void *k = img ? F(0x6F8BF4, void *(*)(void *, const char *, const char *))(img, ns, name) : NULL;
        if (k) return k;
    }
    return NULL;
}
static Obj find_view_x(uintptr_t *h, void **kcache, const char *ns, const char *name, bool inactive) {
    Obj v = u_gc_get(*h);
    if (u_alive(v)) return v;
    if (!*kcache) *kcache = class_by_name(ns, name);
    if (!*kcache) return NULL;
    Obj tobj = F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(*kcache));   /* class -> Type object */
    v = F(0x50FD97C, Obj (*)(Obj, bool, void *))(tobj, inactive, NULL);
    u_gc_free(*h); *h = u_alive(v) ? u_gc_new(v) : 0;
    return u_alive(v) ? v : NULL;
}
static Obj find_view(uintptr_t *h, void **k, const char *ns, const char *name) { return find_view_x(h, k, ns, name, false); }

/* ---------------------------------------------------------------- other lanes */
/* steamnet's mp_steam.h (in mp_game.c); mp_game_host / mp_game_join for direct TCP (and the test rig, which has no Steam). */
int ui_parse_address(const char *in, char *host, size_t host_cap, uint16_t *port, uint16_t def_port);   /* ui_model.c */

/* ---------------------------------------------------------------- state */
static mpm_model M;
static int host_failed, pending_click;
/* title view: TitleMenuView _continueButton +0x30 .. _quitButton +0x60, _loadCampaignButton +0x40, _creditButton +0x50 */
static uintptr_t h_title; static void *k_title;
static uintptr_t h_tb[MPB_COUNT];                       /* title clones (SubUI_TitleMenuButton, _buttonName +0x130) */
static uintptr_t h_title_built;                         /* view the clones belong to */
static int stock_hidden;
static uintptr_t h_field, h_cs; static void *k_cs;           /* address field: clone of the new-campaign name field */
/* Esc menu: SystemMenuView _returnToGameButton +0x48 .. _quitToTitleSceneButton +0x78, _version DASText +0x80 */
static uintptr_t h_esc; static void *k_esc;
static uintptr_t h_eb[MPB_COUNT];                       /* Esc-menu clones (SubUI_SystemMenuButton, _buttonName +0x28) */
static uintptr_t h_etext, h_ebg, h_esc_built;
static uintptr_t h_etitle, h_ereason;                    /* lobby title (Cinzel), Esc "why Load is off" line (seed-line clone) */
static int ph_sibling = -1;                              /* the Esc divider (PlaceHolder) index before the lobby moved it */
static int layout_done, lobby_boxed;
static char layout_log[512];
static int esc_lobby_mode;
/* ---------------------------------------------------------------- layout v2 kit (research/ux_layout_v2.md) */
/* Methods by name (il2cpp_class_get_method_from_name 0x6F8C1C; call through MethodInfo->methodPointer), stock assets by
 * name over everything loaded (Resources.FindObjectsOfTypeAll), and small builders for images / texts / rows. Sizes are
 * the canvas's own units (1663x1080 reference). */
static void *mth(const char *ns, const char *cls, const char *name, int argc) {
    void *k = class_by_name(ns, cls);
    void *mi = k ? F(0x6F8C1C, void *(*)(void *, const char *, int))(k, name, argc) : NULL;
    if (!mi) rig_log("mpmenu: method %s.%s.%s/%d not found", ns, cls, name, argc);
    return mi;
}
#define MCALL(mi, R, ...) ((R (*)(__VA_ARGS__))(*(void **)(mi)))
static Obj type_obj(const char *ns, const char *cls) { void *k = class_by_name(ns, cls); return k ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k)) : NULL; }
static Obj comp_of(Obj any, const char *ns, const char *cls) {             /* GetComponent(typeof(ns.cls)) on any component */
    Obj ty = type_obj(ns, cls);
    return ty && u_alive(any) ? (mm_is(any, K_COMP) ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(any, ty, NULL) : NULL) : NULL;
}
/* stock assets by name: sprites of the v2 spec, the Cinzel font. Scanned once, again every 300 frames while one is missing */
enum { S_FRAME, S_COG, S_TOG_ON, S_TOG_OFF, S_PREV, S_NEXT, S_TGL_N, S_TGL_S, S_RECRUIT, S_BOTTOM, S_BOX, S_DIV2, S_DIV, S_SLOT1, S_SLOT2, S_NBTN, S_N };
static const char *spr_name[S_N] = { "topLineButton_Frame1", "ITEM_CogWheel", "ListToggle_On", "ListToggle_Off", "ArrrowIcon_Previous",
    "ArrrowIcon_Next", "ToggleButton_Normal", "ToggleButton_Selected", "RecruitButton", "BottomButton", "Box", "Devider_Detail2",
    "Icon_Devider", "Slot1", "Slot2", "Button_Normal" };
static uintptr_t h_spr[S_N], h_cinzel, h_cog_built;      /* h_cog_built: the runtime cog (cog_built), not the stock one */
static int64_t frames;
static Obj popup_prefab(int type);
static void cog_from_bundles(void);
static int cog_built(void);
static void assets_scan_now(void);
static int64_t scan_last = -100000;
static int scan_done, scans;
static void assets_scan(void) { if (!scan_done && frames - scan_last >= 300 && scans < 30) assets_scan_now(); }   /* at most 30 scans */
static void assets_scan_now(void) {
    scan_last = frames; scans++;
    popup_prefab(8); popup_prefab(2); popup_prefab(13);                     /* load the Recruit / Expedition / Task Planner prefabs: their sprites too */
    static void *mi; if (!mi) mi = mth("UnityEngine", "Resources", "FindObjectsOfTypeAll", 1);
    if (!mi) return;
    int miss = 0, found = 0;
    Obj arr = MCALL(mi, Obj, Obj, const void *)(type_obj("UnityEngine", "Sprite"), mi);
    int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0;
    for (int i = 0; i < n; i++) {
        Obj sp = AT((char *)arr + 0x20, i * 8, Obj); char nm[64] = "";
        str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(sp, NULL), nm, sizeof nm);
        for (int k = 0; k < S_N; k++) if (!h_spr[k] && !strcmp(nm, spr_name[k])) { h_spr[k] = u_gc_new(sp); found++; }
    }
    if (!h_cinzel) {
        Obj fa = MCALL(mi, Obj, Obj, const void *)(type_obj("TMPro", "TMP_FontAsset"), mi);
        int nf = fa ? (int)AT(fa, 0x18, uintptr_t) : 0;
        for (int i = 0; i < nf && !h_cinzel; i++) {
            Obj f = AT((char *)fa + 0x20, i * 8, Obj); char nm[64] = "";
            str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(f, NULL), nm, sizeof nm);
            if (!strcmp(nm, "Cinzel-Bold SDF")) h_cinzel = u_gc_new(f);
        }
    }
    if (!h_spr[S_COG]) cog_from_bundles();
    char missing[256] = ""; size_t o = 0;
    for (int k = 0; k < S_N; k++) if (!h_spr[k]) { miss++; o += (size_t)snprintf(missing + o, sizeof missing - (o < sizeof missing ? o : sizeof missing), " %s", spr_name[k]); }
    if (!h_cinzel) miss++;
    rig_log("mpmenu: assets scan (%d sprites loaded): %d new, missing%s%s", n, found, missing[0] ? missing : " none", h_cinzel ? "" : " Cinzel");
    if (!miss) scan_done = 1;
}
static Obj spr(int k) {
    Obj o = u_gc_get(h_spr[k]);
    if (o && !u_alive(o)) { u_gc_free(h_spr[k]); h_spr[k] = 0; scan_done = 0; o = NULL; }   /* unloaded with a scene: find it again */
    return o;
}
/* ITEM_CogWheel built at run time from its own 16x16 PNG (copied from the game's icon bundle, research/ux_shots/design/
 * sprites/ITEM_CogWheel.png), for screens where the stock sprite is not loaded yet (title screen). new Texture2D(16, 16,
 * RGBA32, no mips) + ImageConversion.LoadImage, filterMode Point, Sprite.Create (pivot centre, 100 px per unit). */
static const unsigned char cog_png[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0xf3, 0xff, 0x61, 0x00, 0x00, 0x00, 0xd1, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0xa5, 0x93, 0x31, 0x0e, 0x83, 0x30, 0x0c, 0x45, 0x5f, 0xaa, 0x1e, 0xa4, 0xaa, 0xd4, 0x85, 0x95, 0x81, 0x2b, 0x20, 0x6e, 0xc0, 0x3d, 0xb2, 0x23, 0xf6, 0xdc, 0x83, 0x1b, 0x20, 0xae, 0xd0, 0xa1, 0x6b, 0xaf, 0x93, 0x0e, 0xc5, 0xa9, 0x71, 0xd2, 0x52, 0x09, 0x4b, 0x11, 0x20, 0xfb, 0xdb, 0xff, 0x7f, 0x13, 0x77, 0xbd, 0xdc, 0x38, 0x12, 0xa7, 0x43, 0x68, 0xe0, 0xbc, 0x93, 0x8f, 0xea, 0xdd, 0xfd, 0xc3, 0x20, 0xaa, 0x03, 0x40, 0x18, 0xfc, 0xcf, 0x7c, 0x26, 0x21, 0x0c, 0x5e, 0x40, 0xb1, 0x6f, 0x3b, 0x00, 0xd6, 0x67, 0x54, 0xb9, 0x0f, 0x2d, 0x63, 0x62, 0xec, 0xdb, 0x8e, 0xba, 0xa9, 0xca, 0x82, 0x80, 0xc7, 0xfd, 0xc9, 0xb4, 0xcc, 0x49, 0x92, 0x65, 0xe0, 0xd6, 0x64, 0x2a, 0x96, 0x23, 0xa1, 0xc1, 0xb0, 0x35, 0x31, 0x2a, 0xba, 0x7a, 0x52, 0x0a, 0xc5, 0x4c, 0x3c, 0x70, 0x9b, 0x2d, 0x58, 0x7d, 0x52, 0x44, 0xc1, 0x54, 0x3f, 0x06, 0x32, 0x09, 0x7e, 0x0c, 0x1b, 0xba, 0x66, 0x5a, 0x62, 0x26, 0x60, 0x2b, 0xc1, 0x01, 0x4c, 0xcb, 0x1c, 0xeb, 0xa6, 0xca, 0x8c, 0x94, 0x6f, 0xeb, 0x41, 0x71, 0x0b, 0x1a, 0x60, 0xa7, 0xdb, 0x26, 0x5f, 0xff, 0x44, 0xa1, 0xd9, 0xb7, 0x5d, 0x32, 0x53, 0x9a, 0xeb, 0xc8, 0x1a, 0x28, 0xe7, 0x37, 0xe6, 0xf1, 0x5e, 0x71, 0xb4, 0xf5, 0x56, 0x82, 0x8d, 0xdd, 0xbb, 0xb0, 0x77, 0x99, 0x8a, 0x20, 0x1d, 0x2f, 0x27, 0x01, 0x57, 0x55, 0x1a, 0x6c, 0x3c, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
static int cog_built(void) {
    static void *mload, *mfilter, *mcreate; static int tried;
    if (!mload && !tried) {
        tried = 1;
        mload = mth("UnityEngine", "ImageConversion", "LoadImage", 2);
        mfilter = mth("UnityEngine", "Texture", "set_filterMode", 1);
        mcreate = mth("UnityEngine", "Sprite", "Create", 4);
    }
    if (!mload || !mcreate) return 0;
    void *kb = class_by_name("System", "Byte");
    Obj bytes = kb ? F(0x6F8BA8, Obj (*)(void *, size_t))(kb, sizeof cog_png) : NULL;   /* il2cpp_array_new */
    if (!bytes) return 0;
    memcpy((char *)bytes + 0x20, cog_png, sizeof cog_png);
    Obj tex = obj_new(0x6655970);                                           /* Texture2D */
    F(0x50C4E5C, void (*)(Obj, int, int, int, bool, void *))(tex, 16, 16, 4, false, NULL);   /* (w, h, RGBA32, mipChain) */
    bool ok = MCALL(mload, bool, Obj, Obj, const void *)(tex, bytes, mload);
    if (mfilter) MCALL(mfilter, void, Obj, int, const void *)(tex, 0, mfilter);         /* Point: crisp pixel art */
    { static void *mapply; if (!mapply) mapply = mth("UnityEngine", "Texture2D", "Apply", 0);   /* upload to the GPU (LoadImage alone left it blank on screen) */
      if (mapply) MCALL(mapply, void, Obj, const void *)(tex, mapply); }
    Obj sp = ok ? MCALL(mcreate, Obj, Obj, RectF2, V2, float, const void *)(tex, (RectF2){ 0, 0, 16, 16 }, (V2){ 0.5f, 0.5f }, 100.0f, mcreate) : NULL;
    if (u_alive(sp)) { h_spr[S_COG] = u_gc_new(sp); u_gc_free(h_cog_built); h_cog_built = u_gc_new(sp); }
    rig_log("mpmenu: ITEM_CogWheel built from its PNG (%zu bytes): load %d, sprite %s", sizeof cog_png, ok, u_alive(sp) ? "ok" : "FAILED");
    return u_alive(sp);
}
/* ITEM_CogWheel when no loaded object uses it yet (title screen): ask every loaded AssetBundle for it by name */
static void cog_from_bundles(void) {
    static void *mall, *mload;
    if (!mall) { mall = mth("UnityEngine", "AssetBundle", "GetAllLoadedAssetBundles_Native", 0); mload = mth("UnityEngine", "AssetBundle", "LoadAsset", 2);
                 if (!mload) mload = mth("UnityEngine", "AssetBundle", "LoadAsset_Internal", 2); }
    if (!mall || !mload) return;
    Obj arr = MCALL(mall, Obj, const void *)(mall);
    int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0;
    Obj ty = type_obj("UnityEngine", "Sprite");
    for (int i = 0; i < n && !h_spr[S_COG]; i++) {
        Obj bu = AT((char *)arr + 0x20, i * 8, Obj);
        Obj sp = u_alive(bu) ? MCALL(mload, Obj, Obj, Obj, Obj, const void *)(bu, u_str("ITEM_CogWheel"), ty, mload) : NULL;
        if (u_alive(sp)) h_spr[S_COG] = u_gc_new(sp);
    }
    rig_log("mpmenu: ITEM_CogWheel from %d loaded asset bundles: %s", n, h_spr[S_COG] ? "found" : "not found");
}
/* a sprite that is not loaded yet: a plain stock-colour rect instead of Unity's white (the cog: nothing) */
static void img_default_material(Obj im) {               /* a runtime texture: the default UI material, not the template's */
    static void *mm; if (!mm) mm = mth("UnityEngine.UI", "Graphic", "set_material", 1);
    if (mm && mm_is(im, K_GR)) MCALL(mm, void, Obj, Obj, const void *)(im, NULL, mm);
}
/* The cog drawn from plain rects, pixel by pixel (73 runs, 3 colours, from ITEM_CogWheel.png), for screens where the
 * stock sprite is not loaded (title). A runtime Texture2D + Sprite.Create loaded fine (pixels read back) but drew nothing
 * on this UI (tested 2026-10-13), so the art is rebuilt from the UI's own plain rects, which always draw. */
static const struct { uint8_t x, y, w, c; } cog_runs[] = { {7,2,3,0}, {3,3,3,0}, {7,3,1,0}, {8,3,1,2}, {9,3,1,0}, {11,3,3,0}, {3,4,1,0}, {4,4,2,2}, {6,4,1,0}, {7,4,1,1}, {8,4,1,2}, {9,4,1,1}, {10,4,1,0}, {11,4,2,2}, {13,4,1,0}, {3,5,1,0}, {4,5,1,1}, {5,5,7,2}, {12,5,1,1}, {13,5,1,0}, {4,6,1,0}, {5,6,2,2}, {7,6,3,1}, {10,6,2,2}, {12,6,1,0}, {2,7,2,0}, {4,7,1,1}, {5,7,1,2}, {6,7,1,1}, {7,7,3,0}, {10,7,1,1}, {11,7,1,2}, {12,7,3,0}, {2,8,1,0}, {3,8,3,2}, {6,8,1,1}, {7,8,1,0}, {9,8,1,0}, {10,8,1,1}, {11,8,3,2}, {14,8,1,0}, {2,9,2,0}, {4,9,1,1}, {5,9,1,2}, {6,9,1,1}, {7,9,3,0}, {10,9,1,1}, {11,9,1,2}, {12,9,1,1}, {13,9,2,0}, {4,10,1,0}, {5,10,2,2}, {7,10,3,1}, {10,10,2,2}, {12,10,1,0}, {3,11,1,0}, {4,11,2,1}, {6,11,5,2}, {11,11,2,1}, {13,11,1,0}, {3,12,1,0}, {4,12,2,1}, {6,12,2,0}, {8,12,1,1}, {9,12,2,0}, {11,12,2,1}, {13,12,1,0}, {3,13,3,0}, {7,13,1,0}, {8,13,1,1}, {9,13,1,0}, {11,13,3,0}, {7,14,3,0} };
static const Col cog_cols[] = { {0.133f,0.125f,0.145f,1.000f}, {0.482f,0.408f,0.435f,1.000f}, {0.682f,0.624f,0.600f,1.000f} };
static Obj mk_img(Obj tpl_img, Obj parent_tr, int sprite, Col c, float x, float y, float w, float h);
static Obj mk_pixel_cog(Obj tpl_img, Obj parent_tr, float x, float y, float size) {
    Obj box = mk_img(tpl_img, parent_tr, -1, (Col){ 0, 0, 0, 0 }, x, y, size, size);
    if (!box) return NULL;
    float px = size / 16;
    for (size_t i = 0; i < sizeof cog_runs / sizeof cog_runs[0]; i++)
        mk_img(tpl_img, u_tr(box), -1, cog_cols[cog_runs[i].c], (cog_runs[i].x + cog_runs[i].w / 2.0f - 8) * px, (8 - cog_runs[i].y - 0.5f) * px,
               cog_runs[i].w * px, px);
    return box;
}
static Col spr_fallback(int k) {
    switch (k) {
    case S_BOX: return (Col){ 0.133f, 0.125f, 0.145f, 1 };
    case S_RECRUIT: return (Col){ 0.353f, 0.063f, 0.051f, 1 };
    case S_COG: case S_PREV: case S_NEXT: case S_TOG_ON: case S_TOG_OFF: return (Col){ 0, 0, 0, 0 };
    default: return (Col){ 0.165f, 0.149f, 0.173f, 1 };                  /* #2a262c: buttons, frames */
    }
}
/* RectTransform: centre anchors + pivot, size, position from the parent's centre (y up) */
static void place(Obj comp, float x, float y, float w, float h) {
    Obj rt = u_tr(comp); if (!mm_is(rt, K_RT)) return;
    F(0x5106B64, void (*)(Obj, V2, void *))(rt, (V2){ 0.5f, 0.5f }, NULL);  /* anchorMin */
    F(0x5106D48, void (*)(Obj, V2, void *))(rt, (V2){ 0.5f, 0.5f }, NULL);  /* anchorMax */
    F(0x51072F4, void (*)(Obj, V2, void *))(rt, (V2){ 0.5f, 0.5f }, NULL);  /* pivot */
    F(0x5107110, void (*)(Obj, V2, void *))(rt, (V2){ w, h }, NULL);        /* sizeDelta */
    F(0x5106F2C, void (*)(Obj, V2, void *))(rt, (V2){ x, y }, NULL);        /* anchoredPosition */
}
static void stretch(Obj comp) {                                             /* fill the parent */
    Obj rt = u_tr(comp); if (!mm_is(rt, K_RT)) return;
    F(0x5106B64, void (*)(Obj, V2, void *))(rt, (V2){ 0, 0 }, NULL); F(0x5106D48, void (*)(Obj, V2, void *))(rt, (V2){ 1, 1 }, NULL);
    F(0x51072F4, void (*)(Obj, V2, void *))(rt, (V2){ 0.5f, 0.5f }, NULL);
    F(0x5107110, void (*)(Obj, V2, void *))(rt, (V2){ 0, 0 }, NULL); F(0x5106F2C, void (*)(Obj, V2, void *))(rt, (V2){ 0, 0 }, NULL);
}
static void row_height(Obj comp, float w, float h) {                       /* a title-column row: size + its LayoutElement */
    Obj rt = u_tr(comp);
    if (mm_is(rt, K_RT)) F(0x5107110, void (*)(Obj, V2, void *))(rt, (V2){ w, h }, NULL);
    Obj le = comp_of(comp, "UnityEngine.UI", "LayoutElement");
    static void *mp, *mm2;
    if (u_alive(le)) {
        if (!mp) { mp = mth("UnityEngine.UI", "LayoutElement", "set_preferredHeight", 1); mm2 = mth("UnityEngine.UI", "LayoutElement", "set_minHeight", 1); }
        if (mp) MCALL(mp, void, Obj, float, const void *)(le, h, mp);
        if (mm2) MCALL(mm2, void, Obj, float, const void *)(le, h, mm2);
    }
}
static void img_default_material(Obj im);
static void img_set(Obj im, Obj sprite, Col c) {                          /* sprite (sliced) + tint; never a click target */
    if (!mm_is(im, K_IMG)) return;
    if (sprite && sprite == u_gc_get(h_cog_built)) img_default_material(im);   /* runtime sprite: default UI material */
    static void *mt; if (!mt) mt = mth("UnityEngine.UI", "Image", "set_type", 1);
    F(0x5257600, void (*)(Obj, Obj, void *))(im, sprite, NULL);
    if (mt) MCALL(mt, void, Obj, int, const void *)(im, sprite ? 1 : 0, mt);   /* Sliced (plain rect: Simple) */
    F(0x52602F8, void (*)(Obj, Col, void *))(im, c, NULL);
    F(0x5260348, void (*)(Obj, bool, void *))(im, false, NULL);
}
/* Body text material (owner 2026-10-13: our texts had a black outline the game's windows do not have; the clones carried
 * the menu-button label's material). The stock window body text, CommonNoticeView Panel_Notice/Txt_Notice (Lora 16), gives
 * its fontSharedMaterial; every Lora text of our windows uses it. Cinzel titles use the Cinzel font's own material. */
static Obj tr_find(Obj t, const char *path);
static uintptr_t h_body_mat, h_body_font;
static int body_mat_logged;
static Obj body_material(void) {
    Obj m = u_gc_get(h_body_mat);
    if (u_alive(m)) return m;
    static uintptr_t hv; static void *kv;
    Obj cn = find_view_x(&hv, &kv, "Refactor.UI", "CommonNoticeView", true);
    Obj nt = cn ? tr_find(u_tr(cn), "Panel_Notice/Txt_Notice") : NULL;
    static void *k; if (!k) k = class_by_name("TMPro", "TMP_Text");
    Obj ty = k ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k)) : NULL;
    Obj t = nt && ty ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(nt, ty, NULL) : NULL;
    /* that text's material is "Lora-Regular SDF OutLine16" (an outline preset): use its font asset's own default material
     * (TMP_Asset.m_Material: plain, no outline / underlay), as the ui lane does (lead 2026-10-13) */
    Obj font = u_alive(t) ? F(0x4F0A59C, Obj (*)(Obj, void *))(t, NULL) : NULL;
    static void *mm; static int fo = -1;
    if (!mm) mm = mth("TMPro", "TMP_Asset", "get_material", 0);
    if (!mm && fo < 0) { void *ka = class_by_name("TMPro", "TMP_Asset"), *fld = ka ? F(0x6F8C14, void *(*)(void *, const char *))(ka, "m_Material") : NULL;
                          fo = fld ? (int)F(0x6F949C, size_t (*)(void *))(fld) : 0; }
    m = !u_alive(font) ? NULL : mm ? MCALL(mm, Obj, Obj, const void *)(font, mm) : fo > 0 ? AT(font, fo, Obj) : NULL;
    if (u_alive(m)) { h_body_mat = u_gc_new(m); h_body_font = u_gc_new(font); }
    if (!body_mat_logged) { body_mat_logged = 1; char nm[96] = ""; if (u_alive(m)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(m, NULL), nm, sizeof nm);
                            rig_log("mpmenu: body text: font %s, its default material \"%s\" (%s)", u_alive(font) ? "found" : "NOT found", nm, mm ? "get_material" : "m_Material field"); }
    return u_alive(m) ? m : NULL;
}
static void txt_body(Obj t) {                                             /* the stock body font + material, no outline */
    if (!mm_is(t, K_TMP)) return;
    Obj m = body_material(); if (!m) return;
    Obj f = u_gc_get(h_body_font); if (u_alive(f)) F(0x4F0A5A4, void (*)(Obj, Obj, void *))(t, f, NULL);
    static void *ms; if (!ms) ms = mth("TMPro", "TMP_Text", "set_fontSharedMaterial", 1);
    if (ms) MCALL(ms, void, Obj, Obj, const void *)(t, m, ms);
}
static void txt_style(Obj t, int cinzel, float size, int align, int wrap) {
    if (!mm_is(t, K_TMP)) return;
    Obj f = cinzel ? u_gc_get(h_cinzel) : NULL;
    if (u_alive(f)) F(0x4F0A5A4, void (*)(Obj, Obj, void *))(t, f, NULL);
    F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, false, NULL);
    F(0x4F0B340, void (*)(Obj, float, void *))(t, size, NULL);
    F(0x4F0B7BC, void (*)(Obj, int, void *))(t, align, NULL);              /* 513 middle-left, 514 centre, 516 middle-right */
    F(0x4F0BA48, void (*)(Obj, int, void *))(t, wrap, NULL);               /* 0 no wrap, 1 wrap */
    F(0x4F0BB74, void (*)(Obj, int, void *))(t, 0, NULL);
    F(0x4F0C254, void (*)(Obj, bool, void *))(t, true, NULL);
    F(0x5260348, void (*)(Obj, bool, void *))(t, false, NULL);             /* raycastTarget off */
}
static const Col C_MAIN = { 0.8f, 0.745f, 0.722f, 1 }, C_SUB = { 0.651f, 0.565f, 0.522f, 1 }, C_WHITE = { 1, 1, 1, 1 },
                 C_ORANGE = { 0.918f, 0.624f, 0.239f, 1 };                  /* #ccbeb8, #a69085, -, #ea9f3d */
static void txt_color(Obj t, Col c) { if (mm_is(t, K_GR)) F(0x52602F8, void (*)(Obj, Col, void *))(t, c, NULL); }
/* new image / text under parent_tr, from a template (an Image without children, a TMP label) */
static Obj mk_img(Obj tpl_img, Obj parent_tr, int sprite, Col c, float x, float y, float w, float h) {
    if (!u_alive(tpl_img) || !u_alive(parent_tr)) return NULL;
    Obj im = u_instantiate(tpl_img, parent_tr);
    if (!u_alive(im)) return NULL;
    u_active(im, true);
    img_set(im, sprite >= 0 ? spr(sprite) : NULL, sprite >= 0 && !spr(sprite) ? spr_fallback(sprite) : c);
    place(im, x, y, w, h);
    return im;
}
static Obj mk_txt(Obj tpl_txt, Obj parent_tr, int cinzel, float size, int align, int wrap, float x, float y, float w, float h, Col c) {
    if (!u_alive(tpl_txt) || !u_alive(parent_tr)) return NULL;
    Obj t = u_instantiate(tpl_txt, parent_tr);
    if (!u_alive(t)) return NULL;
    u_active(t, true);
    if (!cinzel) txt_body(t);
    txt_style(t, cinzel, size, align, wrap); txt_color(t, c);
    place(t, x, y, w, h);
    return t;
}
/* sibling order on the title column (layout v2 §2a); the address field goes right after LABEL_ADDR, the name field
 * after LABEL_NAME. The selector arrows are children of their rows (arrow_ids), not column entries. */
static const int title_ids[] = { MPB_MULTIPLAYER, MPB_HEADER, MPB_LABEL_ADDR, MPB_LABEL_NAME, MPB_INFO,
                                 MPB_FRIEND0, MPB_FRIEND1, MPB_FRIEND2, MPB_FRIEND3, MPB_FRIEND4, MPB_FRIEND5, MPB_SETTINGS,
                                 MPB_DIVIDER, MPB_HOST, MPB_JOIN_ADDR, MPB_JOIN_FRIEND, MPB_LOAD_SAVE, MPB_NEW_CAMPAIGN, MPB_CONNECT, MPB_CANCEL_JOIN, MPB_BACK };
/* the Rules panel's buttons (title: SubUI_TitleMenuButton clones registered in h_tb; lobby: in h_lwb) */
static const int arrow_ids[] = { MPB_PAUSE_PREV, MPB_PAUSE_NEXT, MPB_SPEED_PREV, MPB_SPEED_NEXT, MPB_GEAR_PREV, MPB_GEAR_NEXT,
                                 MPB_UNITS_PREV, MPB_UNITS_NEXT, MPB_POP_PREV, MPB_POP_NEXT, MPB_EVENTS_PREV, MPB_EVENTS_NEXT, MPB_ADMIN_PREV, MPB_ADMIN_NEXT,
                                 MPB_RULES_DONE };
#define NA (int)(sizeof arrow_ids / sizeof arrow_ids[0])
#define HIST_MAX 12
typedef struct { char key[128], world[64], how[8], addr[96], host[48], players[240], units[200], portraits[400]; long long day, when; uint64_t steamid; } hist_t;
static hist_t hist[HIST_MAX]; static int nhist, hist_loaded;
static uintptr_t h_title_cog, h_title_more, h_title_sum, h_title_box, h_title_pcog;
/* Recent worlds panel (mockup "Multiplayer page: recent worlds"): beside the column on the Multiplayer page */
static uintptr_t h_rw_root, h_rw_l1[6], h_rw_l2[6], h_rw_ul[6], h_rw_u[6], h_rw_note, h_rw_band, h_rw_line, h_rw_title;
static uintptr_t h_rw_pf[6][6], h_rw_pi[6][6], h_rw_pp[6][6], h_rw_pn[6][6];   /* + the unit's name beside its box */   /* "Your units": 30x30 frames (copper edge), initial, portrait image */
/* a portrait sprite by name, if loaded now (FindObjectsOfTypeAll once per title visit); NULL -> the initial */
static Obj portrait_sprite(const char *name) {
    static struct { char n[64]; uintptr_t h; } cache[48]; static int ncache; static int64_t scanned = -1;
    if (!name[0]) return NULL;
    for (int i = 0; i < ncache; i++) if (!strcmp(cache[i].n, name)) { Obj o = u_gc_get(cache[i].h); return u_alive(o) ? o : NULL; }
    if (scanned >= 0 && frames - scanned < 600) return NULL;               /* not loaded: look again in ~20 s */
    scanned = frames;
    static void *mi; if (!mi) mi = mth("UnityEngine", "Resources", "FindObjectsOfTypeAll", 1);
    Obj arr = mi ? MCALL(mi, Obj, Obj, const void *)(type_obj("UnityEngine", "Sprite"), mi) : NULL;
    int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0;
    for (int i = 0; i < n; i++) {
        Obj sp = AT((char *)arr + 0x20, i * 8, Obj); char nm[64] = "";
        str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(sp, NULL), nm, sizeof nm);
        if (!strcmp(nm, name) && ncache < 48) { snprintf(cache[ncache].n, 64, "%s", nm); cache[ncache++].h = u_gc_new(sp); return sp; }
    }
    return NULL;
}
static float rw_h;
static const int hist_ids[] = { MPB_HIST0, MPB_HIST1, MPB_HIST2, MPB_HIST3, MPB_HIST4, MPB_HIST5, MPB_HISTX0, MPB_HISTX1, MPB_HISTX2,
                                MPB_HISTX3, MPB_HISTX4, MPB_HISTX5, MPB_HIST_PREV, MPB_HIST_NEXT };   /* Rules box: cog, "›", summary, frame image */
typedef struct { uintptr_t root, val[MPM_RULE_ROWS], note, desc, row[MPM_RULE_ROWS]; float pw, ph; int tip_row; } rpanel;   /* the Rules panel (approved mockup "Host page B") */
static rpanel rp_title, rp_lobby;                  /* value texts of the 3 selector rows; the settings row's toggle image */
static const int esc_ids[] = { MPB_ESC_INVITE, MPB_LEAVE };   /* in game; the lobby has its own window (v2 §2b) */
#define NT (int)(sizeof title_ids / sizeof title_ids[0])
#define NE (int)(sizeof esc_ids / sizeof esc_ids[0])

static Obj title_view(void) { return find_view(&h_title, &k_title, "Refactor.UI", "TitleMenuView"); }
static Obj esc_view(void) { return find_view(&h_esc, &k_esc, "Refactor.UI", "SystemMenuView"); }
static Obj popups(void) {                               /* MainFlow._eventHandler -> CampaignLoadingEventHandler -> _popUpUIs */
    Obj eh = mf ? AT(mf, 0x50, Obj) : NULL, cl = eh ? AT(eh, 0x28, Obj) : NULL;
    return cl ? AT(cl, 0xF0, Obj) : NULL;
}
static bool popup_open(int type) { Obj p = popups(); return p && !strcmp(cls_name(p), "PopUpViewContainer") && F(0xF62D64, bool (*)(Obj, int, void *))(p, type, NULL); }
static void popup_activate(int type) { Obj p = popups(); if (p && !strcmp(cls_name(p), "PopUpViewContainer")) F(0xF62554, void (*)(Obj, int, void *))(p, type, NULL); }

/* ---------------------------------------------------------------- observations */
static int rules_close_frames;                          /* frames after the lobby Rules closed: the closing Escape stays ours */
static int teams_close_frames;                          /* same after the teams lane's window closed */
static int teams_modal(void);
static int chat_typing, chat_close_frames, chat_scroll, chat_fade_s = 12, chat_show_join = 1, chat_on = 1;
static float chat_x = 0, chat_y = 0;                     /* offset from the default place (canvas units), remembered */
static int fields_edited, test_escape;                   /* test_escape: mpm_escape (tests: no keyboard in a hidden copy) */                               /* title_sync: a field's text changed (read in the next observe) */
static void observe(mpm_obs *o) {
    memset(o, 0, sizeof *o);
    o->scene = scene();
    Obj c = cdc();
    o->campaign_ready = o->scene == 2 && c && AT(c, 0x10, int32_t) != -1 && !busy() && tick() > 0;
    Obj tv = o->scene == 1 ? title_view() : NULL;
    o->title_visible = tv && u_on(tv);
    o->picker_open = o->scene == 1 && popup_open(5);                         /* SaveLoadUI */
    o->loading = busy();
    static int (*load_pending)(void); static int lp_looked;                /* mpglue: an mp load / reload / retry runs */
    if (!lp_looked) { lp_looked = 1; load_pending = (int (*)(void))dlsym(RTLD_DEFAULT, "mp_game_load_pending"); }
    if (load_pending && load_pending()) o->loading = 1;                    /* never tear the session down during it */
    Obj ev = o->scene == 2 ? esc_view() : NULL;
    o->esc_open = ev && u_on(ev);
    mp_steam_info in; mp_steam_state(&in);
    if (in.state == MP_STEAM_ENDED) { o->failed = 1; snprintf(o->msg, sizeof o->msg, "%s", in.msg); }
    else if (in.is_host) o->role = 1;
    else if (in.state == MP_STEAM_JOINING || in.state == MP_STEAM_IN_GAME) {
        o->role = 2;
        o->guest_loading = in.state == MP_STEAM_JOINING && in.player >= 0;           /* welcomed, waiting for / loading the save */
    }
    if (o->role == 2) snprintf(o->host_name, sizeof o->host_name, "%s", in.host_name);
    o->steam = in.steam;
    if (o->scene == 1 || o->scene == 2) {                                   /* Escape = Back on the title pages; closes the lobby Rules */
        static bool (*keydown)(int); static int looked;
        if (!looked) { looked = 1; keydown = (bool (*)(int))F(0x6F8B90, void *(*)(const char *))("UnityEngine.Input::GetKeyDownInt(UnityEngine.KeyCode)");
                       rig_log("mpmenu: Escape key check %s", keydown ? "ready (Input.GetKeyDownInt)" : "NOT available"); }
        o->escape = (keydown && keydown(27)) || test_escape ? 1 : 0;        /* KeyCode.Escape */
        test_escape = 0;
    }
    if (host_failed) o->failed = 1;
    mp_session *s = o->role == 2 ? mp_game_session() : NULL;               /* guest: is the host still in the lobby? */
    for (int i = 0, n = s ? dsnet_hold_count(mp_client(s)) : 0, r; i < n; i++)
        if (dsnet_hold_get(mp_client(s), i, &r, NULL, NULL) == 0 && r == DSNET_HOLD_LOBBY) o->host_lobby = 1;
}

/* ---------------------------------------------------------------- title clones */
static Obj tb(int id) { Obj b = u_gc_get(h_tb[id]); return u_alive(b) ? b : NULL; }
/* text fields (address on the Join page, player name on the Host and Join pages): clones of a stock TMP_InputField.
 * Sources, best first: the load list's save-name field (SaveLoadView +0xB8), the new-campaign name field
 * (SubUI_CampaignSetting +0x30), any TMP_InputField. Popups are made on first use: if none exists yet, the load list
 * (popup 5) is opened for a moment, its field taken, and it is closed. Their listeners are runtime ones: not cloned. */
static uintptr_t h_anyf, h_slv, h_namefield; static void *k_anyf, *k_slv;
static int primed;
static char my_name[64];                                                   /* current name (file / Steam / Mac user) */
static Obj field_source(const char **from) {
    Obj slv = find_view_x(&h_slv, &k_slv, "Refactor", "SaveLoadView", true), fld = slv ? AT(slv, 0xB8, Obj) : NULL;
    *from = "the load list's name field";
    if (!u_alive(fld)) { Obj cs = find_view_x(&h_cs, &k_cs, "Refactor.UI", "SubUI_CampaignSetting", true); fld = cs ? AT(cs, 0x30, Obj) : NULL; *from = "the new-campaign name field"; }
    if (!u_alive(fld)) { fld = find_view_x(&h_anyf, &k_anyf, "TMPro", "TMP_InputField", true); *from = "a scene TMP_InputField"; }
    return u_alive(fld) ? fld : NULL;
}
static Obj first_image(Obj tr, int depth);
static Obj field_make(Obj src, Obj parent, const char *placeholder, const char *text) {
    Obj f = u_instantiate(src, parent);
    if (!u_alive(f)) return NULL;
    Obj ref = tb(MPB_CONNECT);                                               /* frame source: a menu button */
    /* the source field's background sprite "Search" draws a magnifier: use the menu button's frame (Button_Normal),
     * a little darker so it reads as a field (lead 2026-10-13) */
    { Obj fim = image_of(f), bim = ref ? first_image(u_tr(ref), 0) : NULL;
      Obj spr = u_alive(bim) ? (mm_is(bim, K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(bim, NULL) : NULL) : NULL;
      if (u_alive(fim) && u_alive(spr)) {
          (mm_is(fim, K_IMG) ? F(0x5257600, void (*)(Obj, Obj, void *))(fim, spr, NULL) : (void)0);
          (mm_is(fim, K_GR) ? F(0x52602F8, void (*)(Obj, Col, void *))(fim, (Col){ 0.6f, 0.58f, 0.6f, 1 }, NULL) : (void)0);   /* darker: an input */
      }
      rig_log("mpmenu: field frame %s", u_alive(fim) && u_alive(spr) ? "= menu button frame (no magnifier)" : "unchanged (no image / sprite)"); }
    row_height(f, 262, 44);                                                 /* v2 §2a: input 262x44, Lora 18, centred vertically */
    { static void *mtc; if (!mtc) mtc = mth("TMPro", "TMP_InputField", "get_textComponent", 0);
      Obj tc = mtc && mm_is(f, K_FIELD) ? MCALL(mtc, Obj, Obj, const void *)(f, mtc) : NULL;
      if (u_alive(tc)) { txt_body(tc); txt_style(tc, 0, 18, 513, 0); txt_color(tc, C_MAIN); } }
    (mm_is(f, K_FIELD) ? F(0x4EFC2D8, void (*)(Obj, int, void *))(f, 64, NULL) : (void)0);                   /* characterLimit */
    (mm_is(f, K_FIELD) ? F(0x4EFC75C, void (*)(Obj, int, void *))(f, 0, NULL) : (void)0);                    /* contentType Standard */
    Obj ph = (mm_is(f, K_FIELD) ? F(0x4EFB9CC, Obj (*)(Obj, void *))(f, NULL) : NULL);                    /* placeholder ("Save File Name" in the source) */
    if (u_alive(ph) && strstr(cls_name(ph), "Text")) { tmp_set(ph, placeholder); txt_body(ph); txt_style(ph, 0, 16, 513, 0); txt_color(ph, C_SUB); }
    (mm_is(f, K_FIELD) ? F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(text), NULL) : (void)0);          /* SetTextWithoutNotify */
    u_active(f, false);
    return f;
}
static void fields_find(Obj parent) {
    static int64_t last_try = -1000;
    if (u_alive(u_gc_get(h_field)) && u_alive(u_gc_get(h_namefield))) return;
    if (frames - last_try < 3) return;
    last_try = frames;
    const char *from; Obj src = field_source(&from);
    if (!src) {
        if (primed < 40) { if (!primed) { popup_activate(5); rig_log("mpmenu: opening the load list once to get a text field"); } primed++; return; }
        M.no_field = 1; return;                                            /* gave up: the address comes from the clipboard */
    }
    if (!u_alive(u_gc_get(h_field))) {
        char clip[128] = ""; str_utf8(F(0x518AEA0, Obj (*)(void *))(NULL), clip, sizeof clip);   /* prefill: clipboard, if it is an address */
        char hh[128]; uint16_t pp = 0;
        int is_addr = clip[0] && strchr(clip, '.') && !ui_parse_address(clip, hh, sizeof hh, &pp, MPM_DEFAULT_PORT);
        Obj f = field_make(src, parent, "ip:port, e.g. 192.168.1.20:47777", is_addr ? clip : "");
        u_gc_free(h_field); h_field = f ? u_gc_new(f) : 0;
        rig_log("mpmenu: address field cloned from %s%s", from, is_addr ? " (prefilled from the clipboard)" : "");
    }
    if (!u_alive(u_gc_get(h_namefield))) {
        Obj f = field_make(src, parent, "Your name", my_name);
        u_gc_free(h_namefield); h_namefield = f ? u_gc_new(f) : 0;
    }
    if (primed && primed < 40) { Obj p = popups(); if (p && popup_open(5)) F(0xF62A88, void (*)(Obj, int, void *))(p, 5, NULL); }   /* DeactivateView(5) */
    primed = 40; M.no_field = 0;
}
static void field_text(uintptr_t h, char *out, size_t cap) {
    out[0] = 0; Obj f = u_gc_get(h);
    if (u_alive(f)) str_utf8((mm_is(f, K_FIELD) ? F(0x4EFAE84, Obj (*)(Obj, void *))(f, NULL) : NULL), out, cap);   /* TMP_InputField.text */
}
/* text-only line (labels, status): the button clone with every Image under it switched off (Behaviour.enabled), so
 * no frame shows (ux #16). Clicks on it reach our hook and do nothing. */
static void images_off(Obj tr, int depth) {
    Obj im = image_of(tr);
    if (im) (mm_is(im, K_BEH) ? F(0x50ECEA0, void (*)(Obj, bool, void *))(im, false, NULL) : (void)0);
    if (depth >= 4) return;
    int k = (mm_is(tr, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(tr, NULL) : 0);
    for (int i = 0; i < k; i++) images_off((mm_is(tr, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(tr, i, NULL) : NULL), depth + 1);
}
static int fields_seen;                                 /* last texts below are valid (reset when the fields are rebuilt) */
static char seen_addr[256], seen_name[64];
/* approved mockups (dsdesign project: Main / LobbyGuest / HostPageA / HostPageB), owner 2026-10-13. Mockup px = canvas units. */
static const Col C_BAND = { 0.176f, 0.165f, 0.192f, 1 }, C_ROW1 = { 0.212f, 0.196f, 0.227f, 1 }, C_ROW2 = { 0.169f, 0.157f, 0.184f, 1 },
                 C_LINE = { 0.388f, 0.239f, 0.2f, 1 }, C_COPPER = { 0.714f, 0.514f, 0.353f, 1 }, C_GOLD = { 0.851f, 0.745f, 0.561f, 1 },
                 C_START = { 0.918f, 0.851f, 0.812f, 1 }, C_UNLIT = { 0.29f, 0.27f, 0.314f, 1 };
/* a clickable clone: esc = SubUI_SystemMenuButton (lobby), else SubUI_TitleMenuButton (title). Sprite on its background
 * (sliced, stretched), label centred. The caller registers it for clicks. */
static Obj mk_button(Obj src, Obj parent, int esc, int sprite, Col tint, float x, float y, float w, float h, float fs, const char *label, Col tc) {
    if (!u_alive(src) || !u_alive(parent)) return NULL;
    Obj b = u_instantiate(src, parent);
    if (!u_alive(b)) return NULL;
    if (esc) { if (mm_is(b, K_SMB)) F(0xF2DB60, void (*)(Obj, Obj, void *))(b, NULL, NULL); AT(b, 0x30, Obj) = NULL; }
    else AT(b, 0x140, Obj) = NULL;
    u_active(b, true);
    Obj bg = first_image(u_tr(b), 0);
    if (bg && sprite >= 0 && !spr(sprite) && sprite == S_NBTN) {}       /* keep the clone's own frame (it is the menu button) */
    else if (bg) img_set(bg, sprite >= 0 ? spr(sprite) : NULL, sprite >= 0 && !spr(sprite) ? spr_fallback(sprite) : tint);
    if (bg) { stretch(bg); if (mm_is(bg, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(bg, true, NULL); }
    Obj l = AT(b, esc ? 0x28 : 0x130, Obj);
    if (u_alive(l)) { txt_body(l); txt_style(l, 0, fs, 514, 0); txt_color(l, tc); stretch(l); tmp_set(l, label); }
    place(b, x, y, w, h);
    return b;
}
static Obj btn_label(Obj b, int esc) { return b ? AT(b, esc ? 0x28 : 0x130, Obj) : NULL; }
/* Rules panel: band "Rules" (Cinzel 24), rows [label  < value >], note, Done. Centre (cx, cy) in the parent.
 * Narrow (host page) 430 x 520: one column. Wide (lobby, RP_WIDE_W x RP_WIDE_H): Pause..Units left, Population..Admin right,
 * so it fits inside the lobby body under the title and above Start/Leave. */
#define RP_WIDE_W 830
#define RP_WIDE_H 330
static void rp_build(rpanel *p, Obj parent, Obj src, int esc, Obj tpl_img, Obj tpl_txt, float cx, float cy, const char *note, uintptr_t *reg, int wide) {
    float pw = wide ? RP_WIDE_W : 430, ph = wide ? RP_WIDE_H : 520;
    #define PX(x) ((x) - pw / 2)
    #define PY(y) (ph / 2 - (y))
    #define RCOL(i) (wide && (i) >= MPM_RULE_POP ? 1 : 0)
    #define RTOP(i) (wide ? 62 + 46 * ((i) - (RCOL(i) ? MPM_RULE_POP : 0)) + (RCOL(i) && (i) > MPM_RULE_POP ? 22 : 0) \
                          : 68 + 50 * (i) + ((i) > MPM_RULE_POP ? 22 : 0))     /* the Population row has a description line under it */
    #define RX(i) (wide ? (RCOL(i) ? 422 : 14) : 18)                            /* row left edge; rows are 394 wide */
    float rh = wide ? 44 : 48;
    Obj root = mk_img(tpl_img, parent, S_BOX, C_WHITE, cx, cy, pw, ph);
    if (!root) return;
    Obj R = u_tr(root);
    u_gc_free(p->root); p->root = u_gc_new(root);
    mk_img(tpl_img, R, -1, C_BAND, 0, PY(wide ? 24 : 27), pw - 4, wide ? 44 : 50);
    mk_img(tpl_img, R, -1, C_COPPER, 0, PY(wide ? 46 : 52), pw - 4, 1);
    Obj t = mk_txt(tpl_txt, R, 1, 24, 514, 0, 0, PY(wide ? 24 : 27), 400, 40, C_MAIN); tmp_set(t, "Rules");
    for (int i = 0; i < MPM_RULE_ROWS; i++) {
        float yc = PY(RTOP(i) + rh / 2), x0 = RX(i);
        Obj rb = mk_img(tpl_img, R, -1, i % 2 ? C_ROW2 : C_ROW1, PX(x0 + 197), yc, 394, rh);
        u_gc_free(p->row[i]); p->row[i] = rb ? u_gc_new(rb) : 0;
        Obj lb = mk_txt(tpl_txt, R, 0, 15, 513, 0, PX(x0 + 77), yc, 130, 40, C_SUB); tmp_set(lb, mpm_rule_label(i));   /* "Units per player" fits */
        Obj v = mk_txt(tpl_txt, R, 0, 16, 514, 0, PX(x0 + 261), yc, 186, 40, C_MAIN);
        u_gc_free(p->val[i]); p->val[i] = v ? u_gc_new(v) : 0;
        for (int a = 0; a < 2; a++) {
            Obj b = mk_button(src, R, esc, S_NBTN, C_WHITE, PX(x0 + (a ? 368 : 154)), yc, 28, 28, 16, a ? "\xE2\x80\xBA" : "\xE2\x80\xB9", C_MAIN);
            int id = arrow_ids[2 * i + a];
            if (reg) { u_gc_free(reg[id]); reg[id] = b ? u_gc_new(b) : 0; }
        }
    }
    { Obj ds = mk_txt(tpl_txt, R, 0, 12, 513, 0, PX(RX(MPM_RULE_POP) + 197), PY(RTOP(MPM_RULE_POP) + rh + 11), 380, 20, C_SUB);   /* under Population */
      u_gc_free(p->desc); p->desc = ds ? u_gc_new(ds) : 0; }
    float end = wide ? RTOP(MPM_RULE_POP - 1) + rh : RTOP(MPM_RULE_ROWS - 1) + rh;   /* the longer column */
    Obj nt = mk_txt(tpl_txt, R, 0, 12, 513, 1, wide ? PX(14 + 300) : 0, PY(end + (wide ? 40 : 18)), wide ? 600 : 394, 22, C_SUB); tmp_set(nt, note);
    u_gc_free(p->note); p->note = nt ? u_gc_new(nt) : 0;
    Obj d = mk_button(src, R, esc, S_NBTN, C_WHITE, PX(pw - 88), PY(end + (wide ? 40 : 59)), 140, 34, 16, "Done", C_MAIN);
    if (reg) { u_gc_free(reg[MPB_RULES_DONE]); reg[MPB_RULES_DONE] = d ? u_gc_new(d) : 0; }
    p->pw = pw; p->ph = ph; p->tip_row = -1;
    u_active(root, false);
    #undef PX
    #undef PY
    #undef RTOP
    #undef RCOL
    #undef RX
}
/* ---- our tooltips in the game's own tooltip window (ui_tip.c, owner lan_v9; texts research/tooltips_v9.md 3.1-3.5,
 * 3.7, 3.13, 3.14). Each frame the topmost element under the mouse that has a tooltip shows it (game_tip_show_rect);
 * ui_tip hides it 0.2 s after the last call and follows "Show tooltips on hover". Tests name the element by title. */
static int tip_ready(void) { return game_tip_ready(); }                      /* build matches + the scene has its TooltipUI */
static char test_hover[64];                                                  /* test: hover the element with this title */
static char tip_now[64];                                                     /* test status: the title shown this frame */
static int tip_hit;
static inline bool u_on_tree(Obj comp) { Obj g = u_alive(comp) ? u_go(comp) : NULL; return g && mm_is(g, K_GO) && F(0x50F3724, bool (*)(Obj, void *))(g, NULL); }   /* activeInHierarchy */
static int tip_try(Obj o, const char *title, const char *body) {
    if (tip_hit || !o || !u_on_tree(o)) return 0;
    int hov;
    if (test_hover[0]) hov = !strcmp(test_hover, title);
    else { float r[4]; V3 m = F(0x51AD4DC, V3 (*)(void *))(NULL); hov = rect_of(o, r) && m.x >= r[0] && m.x <= r[2] && m.y >= r[1] && m.y <= r[3]; }
    if (!hov) return 0;
    tip_hit = 1; snprintf(tip_now, sizeof tip_now, "%s", title);
    if (tip_ready()) game_tip_show_rect(title, body, u_tr(o));
    return 1;
}
static void rp_tooltip(rpanel *p) {                                          /* Rules rows (3.3) */
    p->tip_row = -1;
    for (int i = 0; i < MPM_RULE_ROWS; i++) if (tip_try(u_gc_get(p->row[i]), mpm_rule_tip_title(i), mpm_rule_tip(i))) { p->tip_row = i; return; }
}
static void rp_update(rpanel *p, const mpm_settings *st, int show) {
    Obj root = u_gc_get(p->root);
    if (!u_alive(root)) return;
    u_active(root, show);
    if (!show) return;
    for (int i = 0; i < MPM_RULE_ROWS; i++) { char v[64]; mpm_rule_value(st, i, v, sizeof v); Obj t = u_gc_get(p->val[i]); if (u_alive(t)) tmp_set(t, v); }
    Obj ds = u_gc_get(p->desc); if (u_alive(ds)) tmp_set(ds, mpm_population_desc(st->population));
}

static void title_build(Obj v) {
    Obj src = AT(v, 0x50, Obj), after = AT(v, 0x40, Obj);
    if (!u_alive(src) || !u_alive(after)) { rig_log("mpmenu: title buttons not found"); return; }
    Obj parent = u_tr_parent(u_tr(src));
    int at = u_sibling(u_tr(after)) + 1;
    if (!scan_done) assets_scan_now();
    Obj tpl_img = first_image(u_tr(src), 0), tpl_txt = AT(src, 0x130, Obj);
    for (int i = 0; i < NT; i++) {
        int id = title_ids[i];
        Obj b = u_instantiate(src, parent);
        if (!u_alive(b)) { rig_log("mpmenu: title clone failed"); return; }
        AT(b, 0x140, Obj) = NULL;                                           /* _onButtonClicked: no stock action */
        Obj l = AT(b, 0x130, Obj);
        txt_style(l, 0, 22, 514, 1);                                        /* design #8: the stock menu size 22 (Credits is 24) */
        int plain = id == MPB_LABEL_ADDR || id == MPB_LABEL_NAME || id == MPB_INFO || id == MPB_HEADER || id == MPB_DIVIDER;
        if (plain) images_off(u_tr(b), 0);
        if (plain || id == MPB_SETTINGS) txt_body(l);                      /* text rows: the stock body material, no outline */
        /* approved mockup "Host page A": subtitle, Your name, field, Rules box, line, Host from a save (primary), ... Back */
        if (id == MPB_HEADER) { row_height(b, 262, 20); txt_style(l, 0, 12, 514, 0); txt_color(l, C_SUB); }
        if (id == MPB_LABEL_ADDR || id == MPB_LABEL_NAME) { row_height(b, 262, 18); txt_style(l, 0, 14, 513, 0); txt_color(l, C_SUB); }
        if (id == MPB_INFO) { row_height(b, 262, 40); txt_style(l, 0, 14, 514, 1); }
        if (id == MPB_DIVIDER) { row_height(b, 262, 17); mk_img(tpl_img, u_tr(b), -1, C_LINE, 0, 0, 262, 1); }
        if (id == MPB_BACK) {                                               /* text-only look, but its frame stays the click target */
            row_height(b, 262, 44); txt_body(l); txt_style(l, 0, 18, 514, 0); txt_color(l, C_SUB);
            Obj bg = first_image(u_tr(b), 0);
            if (bg && mm_is(bg, K_GR)) { F(0x52602F8, void (*)(Obj, Col, void *))(bg, (Col){ 1, 1, 1, 0 }, NULL); F(0x5260348, void (*)(Obj, bool, void *))(bg, true, NULL); }
        }
        if (id == MPB_LOAD_SAVE) {                                          /* the main path: the red primary style */
            Obj bg = first_image(u_tr(b), 0); if (bg) img_set(bg, spr(S_RECRUIT), spr(S_RECRUIT) ? C_WHITE : spr_fallback(S_RECRUIT));
            if (bg && mm_is(bg, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(bg, true, NULL);
            txt_color(l, C_START);
        }
        if (id == MPB_SETTINGS) {                                           /* [cog] Rules  ›  + summary under it */
            row_height(b, 262, 104);
            Obj bg = first_image(u_tr(b), 0); if (bg) { stretch(bg); u_gc_free(h_title_box); h_title_box = u_gc_new(bg); }
            txt_style(l, 0, 18, 513, 0); txt_color(l, C_MAIN);
            Obj cg = mk_img(tpl_img, u_tr(b), S_COG, C_WHITE, -108, 18, 22, 22);
            u_gc_free(h_title_cog); h_title_cog = cg ? u_gc_new(cg) : 0;
            Obj pc = mk_pixel_cog(tpl_img, u_tr(b), -108, 18, 22);
            u_gc_free(h_title_pcog); h_title_pcog = pc ? u_gc_new(pc) : 0;
            Obj mo = mk_txt(tpl_txt, u_tr(b), 0, 18, 514, 0, 113, 18, 20, 30, C_SUB); tmp_set(mo, "\xE2\x80\xBA");
            u_gc_free(h_title_more); h_title_more = mo ? u_gc_new(mo) : 0;
            Obj sm = mk_txt(tpl_txt, u_tr(b), 0, 12, 513, 1, 0, -18, 238, 60, C_SUB);   /* up to 4 lines inside the box (owner lan_v8: it spilled) */
            u_gc_free(h_title_sum); h_title_sum = sm ? u_gc_new(sm) : 0;
        }
        u_set_sibling(u_tr(b), at + i);
        u_active(b, false);
        u_gc_free(h_tb[id]); h_tb[id] = u_gc_new(b);
    }
    /* Recent worlds: 760 wide at the mockup's left 470 / top 130 (1512 x 982); 6 rows of 66, built once, filled per page */
    {
        rw_h = 56 + 12 + 6 * 72 + 40;
        Obj root = mk_img(tpl_img, u_tr(v), S_BOX, C_WHITE, 103, 397 - rw_h / 2, 760, rw_h);
        if (root) {
            Obj R = u_tr(root);
            u_gc_free(h_rw_root); h_rw_root = u_gc_new(root);
            #define RY(y) (rw_h / 2 - (y))
            Obj bd = mk_img(tpl_img, R, -1, C_BAND, 0, RY(28), 756, 54); u_gc_free(h_rw_band); h_rw_band = bd ? u_gc_new(bd) : 0;
            Obj ln = mk_img(tpl_img, R, -1, C_COPPER, 0, RY(56), 756, 1); u_gc_free(h_rw_line); h_rw_line = ln ? u_gc_new(ln) : 0;
            Obj t = mk_txt(tpl_txt, R, 1, 26, 514, 0, 0, RY(28), 700, 44, C_MAIN); u_gc_free(h_rw_title); h_rw_title = t ? u_gc_new(t) : 0;
            for (int i = 0; i < 6; i++) {
                float y = RY(68 + 72 * i + 33);
                Obj b = mk_button(src, R, 0, S_NBTN, i % 2 ? (Col){ 0.85f, 0.85f, 0.85f, 1 } : C_WHITE, 0, y, 736, 66, 16, "", C_MAIN);
                if (!b) continue;
                u_gc_free(h_tb[MPB_HIST0 + i]); h_tb[MPB_HIST0 + i] = u_gc_new(b);
                Obj B = u_tr(b);
                Obj l1 = mk_txt(tpl_txt, B, 0, 18, 513, 0, -368 + 14 + 230, 12, 460, 28, C_MAIN);
                Obj l2 = mk_txt(tpl_txt, B, 0, 13, 513, 0, -368 + 14 + 190, -14, 380, 22, C_SUB);
                Obj ul = mk_txt(tpl_txt, B, 0, 12, 516, 0, 368 - 40 - 110, 12, 220, 20, C_SUB);
                Obj un = mk_txt(tpl_txt, B, 0, 13, 516, 0, 368 - 40 - 110, -12, 220, 22, C_MAIN);
                for (int k = 0; k < 6; k++) {                               /* each unit: [portrait or initial] Name (lead 2026-10-13) */
                    float px = 368 - 50 - 15 - 96 * (5 - k) - 66;
                    Obj fr = mk_img(tpl_img, B, -1, C_COPPER, px, -10, 30, 30);
                    if (fr) mk_img(tpl_img, u_tr(fr), -1, (Col){ 0.29f, 0.231f, 0.212f, 1 }, 0, 0, 28, 28);   /* #4a3b36 */
                    Obj it = fr ? mk_txt(tpl_txt, u_tr(fr), 0, 15, 514, 0, 0, 0, 28, 28, C_MAIN) : NULL;
                    Obj pi = fr ? mk_img(tpl_img, u_tr(fr), -1, C_WHITE, 0, 0, 28, 28) : NULL;
                    if (pi) u_active(pi, false);                                /* shown only with a loaded portrait */
                    u_gc_free(h_rw_pf[i][k]); h_rw_pf[i][k] = fr ? u_gc_new(fr) : 0; u_gc_free(h_rw_pi[i][k]); h_rw_pi[i][k] = it ? u_gc_new(it) : 0;
                    u_gc_free(h_rw_pp[i][k]); h_rw_pp[i][k] = pi ? u_gc_new(pi) : 0;
                    Obj nmt = mk_txt(tpl_txt, B, 0, 12, 513, 0, px + 15 + 4 + 31, -10, 62, 20, C_MAIN);
                    u_gc_free(h_rw_pn[i][k]); h_rw_pn[i][k] = nmt ? u_gc_new(nmt) : 0;
                }
                u_gc_free(h_rw_l1[i]); h_rw_l1[i] = l1 ? u_gc_new(l1) : 0; u_gc_free(h_rw_l2[i]); h_rw_l2[i] = l2 ? u_gc_new(l2) : 0;
                u_gc_free(h_rw_ul[i]); h_rw_ul[i] = ul ? u_gc_new(ul) : 0; u_gc_free(h_rw_u[i]); h_rw_u[i] = un ? u_gc_new(un) : 0;
                Obj x = mk_button(src, B, 0, S_NBTN, C_WHITE, 368 - 22, 0, 28, 28, 16, "x", C_SUB);   /* remove: a small framed "x", own click */
                if (x) { u_gc_free(h_tb[MPB_HISTX0 + i]); h_tb[MPB_HISTX0 + i] = u_gc_new(x); }
            }
            Obj nt = mk_txt(tpl_txt, R, 0, 12, 513, 0, -380 + 16 + 250, RY(rw_h - 20), 500, 20, C_SUB);
            if (nt) tmp_set(nt, "Click a world to reconnect the same way. x removes it from this list.");
            u_gc_free(h_rw_note); h_rw_note = nt ? u_gc_new(nt) : 0;
            Obj pv = mk_button(src, R, 0, S_NBTN, C_WHITE, 380 - 16 - 160, RY(rw_h - 20), 100, 26, 14, "\xE2\x80\xB9 Newer", C_MAIN);
            Obj nx = mk_button(src, R, 0, S_NBTN, C_WHITE, 380 - 16 - 52, RY(rw_h - 20), 100, 26, 14, "Older \xE2\x80\xBA", C_MAIN);
            if (pv) { u_gc_free(h_tb[MPB_HIST_PREV]); h_tb[MPB_HIST_PREV] = u_gc_new(pv); }
            if (nx) { u_gc_free(h_tb[MPB_HIST_NEXT]); h_tb[MPB_HIST_NEXT] = u_gc_new(nx); }
            #undef RY
            u_active(root, false);
        }
    }
    /* the Rules panel opens to the right of the column (mockup: left 470, top 312 at 1512 x 982 -> canvas centre offset) */
    rp_build(&rp_title, u_tr(v), src, 0, tpl_img, tpl_txt, -78, -89, "Saved with this world. New worlds start with your last rules.", h_tb, 0);
    u_gc_free(h_field); h_field = 0; u_gc_free(h_namefield); h_namefield = 0; primed = 0; fields_seen = 0;
    u_gc_free(h_title_built); h_title_built = u_gc_new(v);
    stock_hidden = 0;
    rig_log("mpmenu: title clones added at sibling %d", at);
}
static void title_sync(const mpm_obs *o) {
    Obj v = title_view();
    if (!v || o->scene != 1) return;
    if (u_gc_get(h_title_built) != v || !tb(MPB_MULTIPLAYER)) title_build(v);
    uint64_t vis = mpm_visible(&M, o);
    int sub = vis & ~(1ull << MPB_MULTIPLAYER) ? 1 : 0;                        /* our own menu page: hide the stock buttons */
    if (sub != stock_hidden) { for (int off = 0x30; off <= 0x60; off += 8) u_active(AT(v, off, Obj), !sub); stock_hidden = sub; }
    for (int i = 0; i < NT; i++) {
        Obj b = tb(title_ids[i]); if (!b) continue;
        int on = (vis >> title_ids[i]) & 1;
        u_active(b, on);
        if (on) { char l[160]; mpm_label(&M, title_ids[i], l, sizeof l); tmp_set(AT(b, 0x130, Obj), l); }
    }
    if (M.state == MPM_TITLE_HOST) {                                        /* Rules box: closed = button + summary (mockup A), open = button (B) */
        int open = M.settings_open; Obj b = tb(MPB_SETTINGS), l = b ? AT(b, 0x130, Obj) : NULL;
        if (b) row_height(b, 262, open ? 52 : 104);
        float yb = open ? 0 : 30;
        if (u_alive(l)) { place(l, 0, yb, 262, 44); txt_style(l, 0, open ? 20 : 18, 513, 0); txt_color(l, C_MAIN); tmp_set(l, "<margin-left=46>Rules</margin>"); }
        Obj cg = u_gc_get(h_title_cog); if (u_alive(cg)) { img_set(cg, spr(S_COG), C_WHITE); u_active(cg, spr(S_COG) != NULL); place(cg, -108, yb, 22, 22); }
        Obj pc = u_gc_get(h_title_pcog); if (u_alive(pc)) { u_active(pc, spr(S_COG) == NULL); place(pc, -108, yb, 22, 22); }   /* stock not loaded: pixel cog */
        Obj mo = u_gc_get(h_title_more); if (u_alive(mo)) { place(mo, 113, yb, 20, 30); txt_color(mo, open ? C_GOLD : C_SUB); }
        Obj sm = u_gc_get(h_title_sum); if (u_alive(sm)) { char t[200]; mpm_settings_summary(&M.set, t, sizeof t); tmp_set(sm, t); u_active(sm, !open); }
        Obj bx = u_gc_get(h_title_box); if (u_alive(bx) && mm_is(bx, K_GR)) F(0x52602F8, void (*)(Obj, Col, void *))(bx, open ? (Col){ 1.25f, 1.2f, 1.2f, 1 } : C_WHITE, NULL);   /* open: lighter */
        if (bx && mm_is(bx, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(bx, true, NULL);
    }
    rp_update(&rp_title, &M.set, M.state == MPM_TITLE_HOST && M.settings_open);
    /* Recent worlds (Multiplayer page) */
    Obj rw = u_gc_get(h_rw_root);
    int rwon = M.state == MPM_TITLE_MENU && M.nhist > 0;
    if (u_alive(rw)) u_active(rw, rwon);
    if (rwon && u_alive(rw) && frames % 10 == 0) {
        int shown = 0;
        for (int i = 0; i < 6; i++) {
            int k = M.hist_page * 6 + i; Obj b = tb(MPB_HIST0 + i);
            u_active(b, k < nhist);
            if (k >= nhist || !b) continue;
            shown++;
            hist_t *h = &hist[k]; char t[600], pl[400] = ""; size_t o = 0;
            snprintf(t, sizeof t, "%s  <size=72%%><color=#A69085>Day %lld</color></size>", h->world[0] ? h->world : "A world", h->day);
            tmp_set(u_gc_get(h_rw_l1[i]), t);
            char tmp[240]; snprintf(tmp, sizeof tmp, "%s", h->players); char *save = NULL;
            for (char *tok = strtok_r(tmp, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {   /* "Name|id" (older: "Name|#colour") */
                char *bar = strchr(tok, '|'); if (bar) *bar = 0;
                const char *col = bar && bar[1] >= '0' && bar[1] <= '9' ? palette_color(atoi(bar + 1)) : "#CCBEB8";   /* old hex / none: neutral */
                o += (size_t)snprintf(pl + o, sizeof pl - (o < sizeof pl ? o : sizeof pl), "%s<color=%s>%s</color>%s", o ? ", " : "", col, tok,
                                      !strcmp(h->how, "host") || strcmp(tok, h->host) ? "" : " (host)");
            }
            if (!strcmp(h->how, "host")) snprintf(t, sizeof t, "You hosted%s%s", pl[0] ? " \xC2\xB7 with " : "", pl);
            else if (!strcmp(h->how, "steam")) snprintf(t, sizeof t, "Steam%s%s", pl[0] ? " \xC2\xB7 with " : "", pl);
            else snprintf(t, sizeof t, "IP %s%s%s", h->addr, pl[0] ? " \xC2\xB7 with " : "", pl);
            tmp_set(u_gc_get(h_rw_l2[i]), t);
            { char lab[32] = ""; int cnt = 0; for (const char *q = h->units; *q; q++) if (*q == ',') cnt++; if (h->units[0]) cnt++;
              if (cnt > 3) snprintf(lab, sizeof lab, "Your units (+%d)", cnt - 3); else if (cnt) snprintf(lab, sizeof lab, "Your units");
              tmp_set(u_gc_get(h_rw_ul[i]), lab); tmp_set(u_gc_get(h_rw_u[i]), ""); }   /* "[box] Name" for 3, "+N" in the label */
            { char ub[200]; snprintf(ub, sizeof ub, "%s", h->units); char *sv = NULL, *nm[6]; int nn = 0;
              for (char *tk = strtok_r(ub, ",", &sv); tk && nn < 6; tk = strtok_r(NULL, ",", &sv)) { while (*tk == ' ') tk++; nm[nn++] = tk; }
              char pb[400]; snprintf(pb, sizeof pb, "%s", h->portraits); char *sv2 = NULL, *pn[6] = { "", "", "", "", "", "" }; int np = 0;
              for (char *p0 = pb, *cm; np < 6 && p0; p0 = cm ? cm + 1 : NULL) { cm = strchr(p0, ','); if (cm) *cm = 0; pn[np++] = p0; }
              (void)sv2;
              int shownu = nn > 3 ? 3 : nn;                                 /* room for 3 "[box] Name"; more: "+N" */
              for (int q = 0; q < 6; q++) {                                 /* right-aligned: the last box is the rightmost */
                  int idx = q - (6 - shownu); Obj fr = u_gc_get(h_rw_pf[i][q]), nt = u_gc_get(h_rw_pn[i][q]);
                  u_active(fr, idx >= 0); u_active(nt, idx >= 0);
                  if (idx >= 0) { char nb[64]; snprintf(nb, sizeof nb, "%s%s", nm[idx], idx == shownu - 1 && nn > 3 ? "" : ""); tmp_set(nt, nb); }
                  if (idx >= 0) { char ini[8] = ""; unsigned char c0 = (unsigned char)nm[idx][0]; int L = c0 < 0x80 ? 1 : c0 < 0xE0 ? 2 : c0 < 0xF0 ? 3 : 4;
                                  snprintf(ini, sizeof ini, "%.*s", L, nm[idx]); tmp_set(u_gc_get(h_rw_pi[i][q]), ini);
                                  Obj ps = idx < np ? portrait_sprite(pn[idx]) : NULL, pim = u_gc_get(h_rw_pp[i][q]);   /* the portrait when loaded */
                                  if (pim) { u_active(pim, ps != NULL); if (ps) img_set(pim, ps, C_WHITE); }
                                  u_active(u_gc_get(h_rw_pi[i][q]), ps == NULL); }
              } }
        }
        u_active(tb(MPB_HIST_PREV), M.hist_page > 0); u_active(tb(MPB_HIST_NEXT), (M.hist_page + 1) * 6 < nhist);
        /* the panel's height follows the rows shown */
        float H = 56 + 12 + (shown ? shown : 1) * 72 + 40;
        place(rw, 103, 397 - H / 2, 760, H);
        place(u_gc_get(h_rw_band), 0, H / 2 - 28, 756, 54); place(u_gc_get(h_rw_line), 0, H / 2 - 56, 756, 1);
        Obj tt = u_gc_get(h_rw_title); if (tt) { place(tt, 0, H / 2 - 28, 700, 44); tmp_set(tt, "Recent Worlds"); }
        for (int i = 0; i < 6; i++) { Obj b = tb(MPB_HIST0 + i); if (b) place(b, 0, H / 2 - (68 + 72 * i + 33), 736, 66); }
        place(u_gc_get(h_rw_note), -380 + 16 + 250, -H / 2 + 20, 500, 20);
        if (tb(MPB_HIST_PREV)) place(tb(MPB_HIST_PREV), 380 - 16 - 160, -H / 2 + 20, 100, 26);
        if (tb(MPB_HIST_NEXT)) place(tb(MPB_HIST_NEXT), 380 - 16 - 52, -H / 2 + 20, 100, 26);
    }
    int join = M.state == MPM_TITLE_JOIN, hostp = M.state == MPM_TITLE_HOST;
    if ((join || hostp) && tb(MPB_MULTIPLAYER)) fields_find(u_tr_parent(u_tr(tb(MPB_MULTIPLAYER))));
    Obj af = u_gc_get(h_field), nf = u_gc_get(h_namefield);
    if ((join || hostp) && frames % 10 == 0 && tb(MPB_LABEL_ADDR) && tb(MPB_LABEL_NAME)) {   /* order: label, field, label, field, buttons */
        Obj la = u_tr(tb(MPB_LABEL_ADDR)), ln = u_tr(tb(MPB_LABEL_NAME));
        if (u_alive(af) && u_sibling(u_tr(af)) != u_sibling(la) + 1) u_set_sibling(u_tr(af), u_sibling(la) + 1);
        if (u_alive(nf) && u_sibling(u_tr(nf)) != u_sibling(ln) + 1) u_set_sibling(u_tr(nf), u_sibling(ln) + 1);
    }
    u_active(af, join);
    u_active(nf, join || hostp);
    if ((join || hostp) && u_on(nf)) { char nm[64]; field_text(h_namefield, nm, sizeof nm); if (nm[0]) snprintf(my_name, sizeof my_name, "%s", nm); }   /* the title scene goes away on load: keep the typed name */
    if (join || hostp) {                                                    /* a changed text clears an old error (ux #16) */
        char ta[256], tn[64]; field_text(h_field, ta, sizeof ta); field_text(h_namefield, tn, sizeof tn);
        if (fields_seen && (strcmp(ta, seen_addr) || strcmp(tn, seen_name))) fields_edited = 1;
        snprintf(seen_addr, sizeof seen_addr, "%s", ta); snprintf(seen_name, sizeof seen_name, "%s", tn); fields_seen = 1;
    } else fields_seen = 0;
}
void mpmenu_title_init(void *view) { (void)view; h_title_built = 0; }      /* stock InitOnActivate ran: rebuild on next frame */
int mpmenu_title_click(void *btn) {
    for (int i = 0; i < NT; i++) if (btn && btn == tb(title_ids[i])) { pending_click = title_ids[i]; return 1; }
    for (int i = 0; i < NA; i++) if (btn && btn == tb(arrow_ids[i])) { pending_click = arrow_ids[i]; return 1; }
    for (int i = 0; i < (int)(sizeof hist_ids / sizeof hist_ids[0]); i++) if (btn && btn == tb(hist_ids[i])) { pending_click = hist_ids[i]; return 1; }
    return 0;
}
static void (*o_title_click)(Obj, Obj, void *);
static void h_title_click(Obj btn, Obj ev, void *mi) {                     /* SubUI_TitleMenuButton.OnPointerClick */
    if (mpmenu_title_click(btn)) return;                                    /* our clone: no stock action */
    o_title_click(btn, ev, mi);
}

/* ---------------------------------------------------------------- Esc-menu clones / lobby panel */
/* stock parts (design report 2026-10-13, section 2.3): prefabs from UIFactory (+0x20 prefab table, 0xD3236C by PopUpType),
 * Transform.Find 0x510F3C0, Image.sprite get 0x5266F04 / set 0x5257600, TMP_Text.font get 0x4F0A59C / set 0x4F0A5A4 */
static Obj tr_find(Obj t, const char *path) { return u_alive(t) ? (mm_is(t, K_TR) ? F(0x510F3C0, Obj (*)(Obj, Obj, void *))(t, u_str(path), NULL) : NULL) : NULL; }
static Obj popup_prefab(int type) {
    static uintptr_t h; static void *k;
    Obj fac = find_view_x(&h, &k, "Refactor.Main.UI", "UIFactory", true), so = fac ? AT(fac, 0x20, Obj) : NULL;
    return so ? F(0xD3236C, Obj (*)(Obj, int, void *))(so, type, NULL) : NULL;
}
static Obj first_image(Obj tr, int depth) {                              /* the button's background (root has none) */
    Obj im = image_of(tr); if (im) return im;
    if (depth >= 3) return NULL;
    int k = (mm_is(tr, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(tr, NULL) : 0);
    for (int i = 0; i < k; i++) { Obj c = first_image((mm_is(tr, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(tr, i, NULL) : NULL), depth + 1); if (c) return c; }
    return NULL;
}
static Obj eb(int id) { Obj b = u_gc_get(h_eb[id]); return u_alive(b) ? b : NULL; }
/* owner lan_v8: our Esc buttons (and the reason line) make the column taller downward, so the menu sat low (seed lines
 * near the screen bottom). The group (logo + version, buttons, seed lines) moves up by half of what we add: centred as
 * the stock menu. Moved: the lowest common parent of those parts, or, if that is the view itself (its dim layer), each of
 * its children holding a part. The applied shift is kept in local units (a resolution change keeps it right). */
#define ESC_MOVE_MAX 6
static uintptr_t h_emove[ESC_MOVE_MAX]; static int n_emove; static float emove_base[ESC_MOVE_MAX];   /* stock local y */
static void esc_shift_reset(void) { for (int i = 0; i < n_emove; i++) { u_gc_free(h_emove[i]); h_emove[i] = 0; } n_emove = 0; }
static V3 tr_lpos(Obj t) { return mm_is(t, K_TR) ? F(0x51074F4, V3 (*)(Obj, void *))(t, NULL) : (V3){ 0, 0, 0 }; }
static int is_under(Obj t, Obj anc) { for (int d = 0; t && d < 32; d++, t = u_tr_parent(t)) if (t == anc) return 1; return 0; }
static void esc_shift_find(Obj v, Obj holder) {
    Obj parts[4] = { u_tr(AT(v, 0x80, Obj)), holder, u_tr(AT(v, 0x88, Obj)), u_tr(AT(v, 0x90, Obj)) };
    Obj lca = parts[1];
    while (lca) { int all = 1; for (int i = 0; i < 4; i++) if (parts[i] && !is_under(parts[i], lca)) all = 0; if (all) break; lca = u_tr_parent(lca); }
    if (!lca) return;
    if (lca != u_tr(v)) { emove_base[n_emove] = tr_lpos(lca).y; h_emove[n_emove++] = u_gc_new(lca); return; }
    for (int i = 0; i < 4; i++) {                                           /* the view itself: its children that hold a part */
        Obj c = parts[i]; if (!c) continue;
        while (c && u_tr_parent(c) != lca) c = u_tr_parent(c);
        int dup = 0; for (int k = 0; k < n_emove; k++) if (u_gc_get(h_emove[k]) == c) dup = 1;
        if (c && !dup && n_emove < ESC_MOVE_MAX) { emove_base[n_emove] = tr_lpos(c).y; h_emove[n_emove++] = u_gc_new(c); }
    }
}
/* add_world: height our parts add (world units). Each mover's local y = its stock y + half of that, in its parent's units */
static void esc_shift_apply(float add_world) {
    for (int i = 0; i < n_emove; i++) {
        Obj t = u_gc_get(h_emove[i]), pa = t ? u_tr_parent(t) : NULL; if (!mm_is(t, K_TR) || !mm_is(pa, K_TR)) continue;
        V3 ls = F(0x510F4F8, V3 (*)(Obj, void *))(pa, NULL); if (ls.y <= 0) continue;
        V3 p = tr_lpos(t); float want = emove_base[i] + add_world / 2 / ls.y;
        if (p.y - want > 0.25f || want - p.y > 0.25f) { p.y = want; F(0x5107724, void (*)(Obj, V3, void *))(t, p, NULL); }
    }
}
static void esc_build(Obj v) {
    Obj src = AT(v, 0x70, Obj), before = AT(v, 0x78, Obj), ver = AT(v, 0x80, Obj);   /* clone "Report bug", place before "Quit" */
    if (!u_alive(src) || !u_alive(before)) { rig_log("mpmenu: Esc-menu buttons not found"); return; }
    Obj parent = u_tr_parent(u_tr(src));
    int at = u_sibling(u_tr(before));
    for (int i = 0; i < NE; i++) {
        Obj b = u_instantiate(src, parent);
        if (!u_alive(b)) { rig_log("mpmenu: Esc clone failed"); return; }
        (mm_is(b, K_SMB) ? F(0xF2DB60, void (*)(Obj, Obj, void *))(b, NULL, NULL) : (void)0);            /* Init(null): adds the clone's own onClick listener */
        AT(b, 0x30, Obj) = NULL;                                            /* (runtime listeners are not cloned) */
        u_set_sibling(u_tr(b), at + i);
        u_active(b, false);
        u_gc_free(h_eb[esc_ids[i]]); h_eb[esc_ids[i]] = u_gc_new(b);
    }
    esc_shift_reset(); esc_shift_find(v, parent);
    rig_log("mpmenu: Esc menu centring moves %d part(s)", n_emove);
    if (u_alive(ver)) {
        /* lobby info: a clone of the version line, OUTSIDE the button holder's layout group (parent = the holder's
         * parent, Transform_ScaleAdjust), behind it a dark panel cloned from the menu's letterbox image */
        Obj holder = u_tr_parent(u_tr(src)), area = u_tr_parent(holder);
        Obj lb = NULL;
        int k = (mm_is(u_tr(v), K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(u_tr(v), NULL) : 0);
        for (int i = 0; i < k && !lb; i++) {
            Obj c = (mm_is(u_tr(v), K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(u_tr(v), i, NULL) : NULL); char nm[64] = "";
            str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(c, NULL), nm, sizeof nm);
            if (strstr(nm, "LetterBox")) lb = c;
        }
        /* frame: the stock notice box (CommonNoticeView Panel_Notice/Img_BackGroundBox, sprite Box), its buttons off;
         * fallback the letterbox image, tinted */
        static uintptr_t h_cnv2; static void *k_cnv2;
        Obj cn = find_view_x(&h_cnv2, &k_cnv2, "Refactor.UI", "CommonNoticeView", true);
        Obj box = cn ? tr_find(u_tr(cn), "Panel_Notice/Img_BackGroundBox") : NULL;
        Obj bg = u_alive(box) ? u_instantiate(box, area) : u_alive(lb) ? u_instantiate(lb, area) : NULL;
        if (u_alive(box) && u_alive(bg)) {
            int nc = (mm_is(u_tr(bg), K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(u_tr(bg), NULL) : 0);
            for (int i = 0; i < nc; i++) u_go_active(u_go((mm_is(u_tr(bg), K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(u_tr(bg), i, NULL) : NULL)), false);
        }
        lobby_boxed = u_alive(box) && u_alive(bg);
        Obj t = u_instantiate(ver, area);
        Obj tt = u_instantiate(ver, area);                                  /* title: Cinzel-Bold from the Recruit popup's title */
        Obj rp = popup_prefab(8), rt = rp ? tr_find(u_tr(rp), "Txt_Notice") : NULL, rtt = NULL;
        if (u_alive(rt)) { static void *kt; if (!kt) kt = class_by_name("TMPro", "TMP_Text");
            Obj ty = kt ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(kt)) : NULL;
            rtt = ty ? (mm_is(rt, K_COMP) ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(rt, ty, NULL) : NULL) : NULL; }
        Obj cinzel = u_alive(rtt) ? (mm_is(rtt, K_TMP) ? F(0x4F0A59C, Obj (*)(Obj, void *))(rtt, NULL) : NULL) : NULL;
        if (u_alive(tt) && u_alive(cinzel)) (mm_is(tt, K_TMP) ? F(0x4F0A5A4, void (*)(Obj, Obj, void *))(tt, cinzel, NULL) : (void)0);
        if (u_alive(tt)) u_active(tt, false);
        u_gc_free(h_etitle); h_etitle = u_alive(tt) ? u_gc_new(tt) : 0;
        /* Start = primary action: the Recruit button's sprite (RecruitButton, red) on the clone's background; the label keeps the
         * menu size of the other buttons (lead 2026-10-13) */
        Obj rb = rp ? tr_find(u_tr(rp), "Img_BackGroundBox/CandidateDetailPanel/Img_RecruitButton") : NULL;
        Obj rspr = u_alive(rb) && image_of(rb) ? (mm_is(image_of(rb), K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(image_of(rb), NULL) : NULL) : NULL;
        Obj sb = eb(MPB_START), sim = sb ? first_image(u_tr(sb), 0) : NULL;
        if (u_alive(sim) && u_alive(rspr)) (mm_is(sim, K_IMG) ? F(0x5257600, void (*)(Obj, Obj, void *))(sim, rspr, NULL) : (void)0);
        rig_log("mpmenu: lobby parts: notice box %p, Cinzel %p, RecruitButton sprite %p on Start %p", box, cinzel, rspr, sim);
        if (u_alive(bg)) { u_active(bg, false); { Obj im = image_of(bg); if (im) (mm_is(im, K_GR) ? F(0x5260348, void (*)(Obj, bool, void *))(im, false, NULL) : (void)0); } }
        if (u_alive(t)) u_active(t, false);
        u_gc_free(h_etext); h_etext = u_alive(t) ? u_gc_new(t) : 0;
        u_gc_free(h_ebg); h_ebg = u_alive(bg) ? u_gc_new(bg) : 0;
        layout_done = 0;
    }
    Obj seed = AT(v, 0x88, Obj);                                            /* design #7: why Load is off, a seed-style line */
    if (u_alive(seed)) {
        Obj r = u_instantiate(seed, u_tr_parent(u_tr(seed)));
        if (u_alive(r)) { tmp_set(r, "<color=#A69085>Leave multiplayer to load another world</color>"); u_active(r, false); }
        u_gc_free(h_ereason); h_ereason = u_alive(r) ? u_gc_new(r) : 0;
    }
    ph_sibling = -1;
    u_gc_free(h_esc_built); h_esc_built = u_gc_new(v);
    esc_lobby_mode = 0;
    rig_log("mpmenu: Esc-menu clones added at sibling %d", at);
}
static void lobby_text(char *out, size_t cap) {
    mp_steam_info in; mp_steam_state(&in);
    const char *names[8]; int n = 0;
    static char fb[8][24];
    for (int i = 0; i < in.nplayers && n < 8; i++) {
        snprintf(fb[n], sizeof fb[n], in.players[i].id == in.player ? "You" : "Player %d", in.players[i].id + 1);
        names[n] = in.players[i].name[0] && strcmp(in.players[i].name, "player") ? in.players[i].name : fb[n]; n++;
    }
    mpm_lobby_text(&M, names, n, out, cap);
}
/* Lobby panel (design #6): the stock notice box frame (546 wide, or wider for a long line) above the buttons, a centred
 * Cinzel title (24) and a left-aligned Lora body (16), never over a button. World coordinates of an overlay canvas =
 * pixels; sizes below are the canvas's own units (sc = screen pixels per unit, from the first button). */
static void esc_load_rule(Obj v);
static int in_session(void);
static void tmp_setup(Obj t, float size, int align) {
    (mm_is(t, K_TMP) ? F(0x4F0B5DC, void (*)(Obj, bool, void *))(t, false, NULL) : (void)0);             /* enableAutoSizing off */
    (mm_is(t, K_TMP) ? F(0x4F0B340, void (*)(Obj, float, void *))(t, size, NULL) : (void)0);
    (mm_is(t, K_TMP) ? F(0x4F0B7BC, void (*)(Obj, int, void *))(t, align, NULL) : (void)0);
    (mm_is(t, K_TMP) ? F(0x4F0BA48, void (*)(Obj, int, void *))(t, 0, NULL) : (void)0);                  /* textWrappingMode NoWrap */
    (mm_is(t, K_TMP) ? F(0x4F0BB74, void (*)(Obj, int, void *))(t, 0, NULL) : (void)0);                  /* overflowMode Overflow */
    (mm_is(t, K_TMP) ? F(0x4F0C254, void (*)(Obj, bool, void *))(t, true, NULL) : (void)0);              /* rich text */
    (mm_is(u_tr(t), K_RT) ? F(0x51072F4, void (*)(Obj, V2, void *))(u_tr(t), (V2){ 0.5f, 0.0f }, NULL) : (void)0);   /* pivot bottom-centre */
}
static void tmp_measure(Obj t, float *w, float *h) {
    (mm_is(u_tr(t), K_RT) ? F(0x5107110, void (*)(Obj, V2, void *))(u_tr(t), (V2){ 4000.0f, 10.0f }, NULL) : (void)0);
    *w = (mm_is(t, K_TMP) ? F(0x4F0D81C, float (*)(Obj, void *))(t, NULL) : 0.0f);                     /* preferredWidth: widest line, unwrapped */
    *h = (mm_is(t, K_TMP) ? F(0x4F0DA20, float (*)(Obj, void *))(t, NULL) : 0.0f);
}
static void put_at(Obj t, float x, float y) {
    V3 cur = (mm_is(u_tr(t), K_TR) ? F(0x5109A08, V3 (*)(Obj, void *))(u_tr(t), NULL) : (V3){0, 0, 0});
    (mm_is(u_tr(t), K_TR) ? F(0x5109B04, void (*)(Obj, V3, void *))(u_tr(t), (V3){ x, y, cur.z }, NULL) : (void)0);
}
static void lobby_layout(Obj v, Obj t, Obj tt, Obj bg) {
    (void)v;
    Obj first = NULL; float fr[4], top = -1e9f;
    for (int i = 0; i < NE; i++) { Obj b = eb(esc_ids[i]); float r[4]; if (u_on(b) && rect_of(b, r) && r[3] > top) { top = r[3]; first = b; memcpy(fr, r, sizeof fr); } }
    if (!first) return;
    V2 bsz = (mm_is(u_tr(first), K_RT) ? F(0x510701C, V2 (*)(Obj, void *))(u_tr(first), NULL) : (V2){0, 0});
    float sc = (fr[2] - fr[0]) / (bsz.x > 1 ? bsz.x : 262), cx = (fr[0] + fr[2]) / 2;
    float fs = 16, ts = 24, pad = 22, gap = 8, minw = 546;
    /* the Box sprite's lower band (75 units, where the stock box puts its buttons) holds the first button (Start, or
     * the guest's Leave): no empty strip (lead 2026-10-13). Fallback frame: plain padding under the text. */
    const float band = 75;
    float padb = lobby_boxed ? band : pad;
    int sh = F(0x5092CD4, int (*)(void *))(NULL);
    float tr[4] = {0}, ttr[4] = {0}, wloc = 0, bw = 0, bh = 0, ph = 0, th = 0;
    tmp_setup(t, fs, 257);                                                 /* TopLeft */
    if (u_alive(tt)) tmp_setup(tt, ts, 514);                               /* Centre */
    for (int pass = 0; pass < 5; pass++) {
        (mm_is(t, K_TMP) ? F(0x4F0B340, void (*)(Obj, float, void *))(t, fs, NULL) : (void)0);
        float pw, tw = 0; tmp_measure(t, &pw, &ph); th = 0;
        if (u_alive(tt)) { (mm_is(tt, K_TMP) ? F(0x4F0B340, void (*)(Obj, float, void *))(tt, ts, NULL) : (void)0); tmp_measure(tt, &tw, &th); }
        wloc = pw > tw ? pw : tw; if (wloc < minw - 2 * pad) wloc = minw - 2 * pad;
        (mm_is(u_tr(t), K_RT) ? F(0x5107110, void (*)(Obj, V2, void *))(u_tr(t), (V2){ wloc, ph }, NULL) : (void)0);
        float tb0 = lobby_boxed ? fr[1] - (band - (fr[3] - fr[1]) / sc) / 2 * sc + (band + 8) * sc   /* box bottom + band + gap */
                                : fr[3] + (pad + 14) * sc;                  /* body bottom above the first button */
        put_at(t, cx, tb0);
        rect_of(t, tr);
        if (u_alive(tt)) { (mm_is(u_tr(tt), K_RT) ? F(0x5107110, void (*)(Obj, V2, void *))(u_tr(tt), (V2){ wloc, th }, NULL) : (void)0); put_at(tt, cx, tr[3] + gap * sc); rect_of(tt, ttr); }
        else memcpy(ttr, tr, sizeof ttr);
        if (ttr[3] + pad * sc <= sh - 4) break;                            /* too high for the screen: smaller type */
        fs *= 0.9f; ts *= 0.9f;
    }
    bw = wloc + 2 * pad; bh = (ttr[3] - tr[1]) / sc + pad + padb + (lobby_boxed ? 8 : 0);
    if (u_alive(bg)) {
        (mm_is(u_tr(bg), K_RT) ? F(0x51072F4, void (*)(Obj, V2, void *))(u_tr(bg), (V2){ 0.5f, 0.5f }, NULL) : (void)0);
        (mm_is(u_tr(bg), K_RT) ? F(0x5106B64, void (*)(Obj, V2, void *))(u_tr(bg), (V2){ 0.5f, 0.5f }, NULL) : (void)0);   /* anchors: centre (no stretch) */
        (mm_is(u_tr(bg), K_RT) ? F(0x5106D48, void (*)(Obj, V2, void *))(u_tr(bg), (V2){ 0.5f, 0.5f }, NULL) : (void)0);
        (mm_is(u_tr(bg), K_RT) ? F(0x5107110, void (*)(Obj, V2, void *))(u_tr(bg), (V2){ bw, bh }, NULL) : (void)0);
        float bot = lobby_boxed ? tr[1] - (band + 8) * sc : tr[1] - padb * sc;
        put_at(bg, cx, (bot + ttr[3] + pad * sc) / 2);
        { Obj im = image_of(bg); if (im) {
            if (lobby_boxed) (mm_is(im, K_GR) ? F(0x52602F8, void (*)(Obj, Col, void *))(im, (Col){ 1, 1, 1, 1 }, NULL) : (void)0);          /* the Box sprite as drawn */
            else (mm_is(im, K_GR) ? F(0x52602F8, void (*)(Obj, Col, void *))(im, (Col){ 0.133f, 0.125f, 0.145f, 0.96f }, NULL) : (void)0);   /* #222025 */
            (mm_is(im, K_GR) ? F(0x5260348, void (*)(Obj, bool, void *))(im, false, NULL) : (void)0); } }
        u_set_sibling(u_tr(bg), lobby_boxed ? 0 : 9997);                   /* boxed: behind the button holder */
    }
    if (u_alive(tt)) u_set_sibling(u_tr(tt), 9998);
    u_set_sibling(u_tr(t), 9999);                                          /* draw order: frame, title, body */
    float br[4] = {0}; if (u_alive(bg)) rect_of(bg, br);
    snprintf(layout_log, sizeof layout_log, "{\"text\":[%.0f,%.0f,%.0f,%.0f],\"title\":[%.0f,%.0f,%.0f,%.0f],\"panel\":[%.0f,%.0f,%.0f,%.0f],\"first_button\":[%.0f,%.0f,%.0f,%.0f],\"font\":%.1f,\"boxed\":%d}",
             tr[0], tr[1], tr[2], tr[3], ttr[0], ttr[1], ttr[2], ttr[3], br[0], br[1], br[2], br[3], fr[0], fr[1], fr[2], fr[3], fs, lobby_boxed);
    if (!layout_done) { rig_log("mpmenu: lobby layout %s", layout_log); layout_done = 1; }
}
static void lw_sync(Obj v, int lobby);
static void esc_sync(const mpm_obs *o) {
    Obj v = o->scene == 2 ? esc_view() : NULL;
    if (!v || !u_on(v)) return;
    if (u_gc_get(h_esc_built) != v || !eb(MPB_LEAVE)) esc_build(v);
    esc_load_rule(v);
    mpm_obs oo = *o; oo.esc_open = 1;
    uint64_t vis = mpm_visible(&M, &oo);
    int lobby = M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY;
    if (lobby != esc_lobby_mode) {                                          /* lobby: the stock buttons go away */
        for (int off = 0x48; off <= 0x78; off += 8) if (off != 0x58) u_active(AT(v, off, Obj), !lobby);
        esc_lobby_mode = lobby;
    }
    for (int i = 0; i < NE; i++) {
        Obj b = eb(esc_ids[i]); if (!b) continue;
        int on = (vis >> esc_ids[i]) & 1;
        u_active(b, on);
        if (on) { char l[160]; mpm_label(&M, esc_ids[i], l, sizeof l); tmp_set(AT(b, 0x28, Obj), l); }
    }
    /* in game: our buttons grow the menu downward; the stock seed lines (Txt_CampaignSeed +0x88, Txt_DungeonSeed +0x90)
     * move down by as many button steps as we added, so they never sit between buttons */
    {   /* centre: half of what we add (our visible buttons, one button step each, and the reason line) goes up */
        float add = 0, r[4]; Obj ref = AT(v, 0x48, Obj), rl = u_gc_get(h_ereason);
        if (!lobby && u_on(ref) && rect_of(ref, r)) {
            Obj nx = AT(v, 0x50, Obj); float r2[4];
            float step = u_on(nx) && rect_of(nx, r2) ? r[1] - r2[1] : (r[3] - r[1]) * 1.25f;   /* Return -> Save: one step */
            for (int i = 0; i < NE; i++) if (u_on(eb(esc_ids[i]))) add += step;
            float rr[4]; if (in_session() && u_alive(rl) && rect_of(rl, rr)) add += rr[3] - rr[1] + 2;
        }
        esc_shift_apply(lobby ? 0 : add);                                   /* the lobby hides the column: stock place */
    }
    if (!lobby) {
        /* the seed lines go right under the lowest visible button (stock ones and ours), keeping their own spacing */
        float low = 1e9f, bh = 0, r[4];
        for (int off = 0x48; off <= 0x78; off += 8) if (off != 0x58) { Obj b = AT(v, off, Obj); if (u_on(b) && rect_of(b, r) && r[1] < low) { low = r[1]; bh = r[3] - r[1]; } }
        for (int i = 0; i < NE; i++) { Obj b = eb(esc_ids[i]); if (u_on(b) && rect_of(b, r) && r[1] < low) { low = r[1]; bh = r[3] - r[1]; } }
        Obj l0 = AT(v, 0x88, Obj), l1 = AT(v, 0x90, Obj); float s0[4];
        Obj rl = u_gc_get(h_ereason); int show_r = in_session() && u_alive(rl);
        u_active(rl, show_r);
        float cursor = low - bh * 0.3f;
        if (show_r && low < 1e8f) {                                         /* design #7: the reason line first, then the seeds */
            float rr[4];
            if (rect_of(rl, rr)) {
                float d = cursor - rr[3];
                if (d < -0.5f || d > 0.5f) { V3 p = (mm_is(u_tr(rl), K_TR) ? F(0x5109A08, V3 (*)(Obj, void *))(u_tr(rl), NULL) : (V3){0, 0, 0}); p.y += d; (mm_is(u_tr(rl), K_TR) ? F(0x5109B04, void (*)(Obj, V3, void *))(u_tr(rl), p, NULL) : (void)0); }
                cursor = rr[1] + d - 2;
            }
        }
        if (low < 1e8f && u_alive(l0) && rect_of(l0, s0)) {
            float top = s0[3], r1[4];
            if (u_on(l1) && rect_of(l1, r1) && r1[3] > top) top = r1[3];
            float delta = cursor - top;                                    /* < 0: move down; > 0: move up to sit close */
            if (delta < -0.5f || delta > 0.5f)
                for (int k = 0; k < 2; k++) {
                    Obj l = AT(v, 0x88 + 8 * k, Obj); if (!u_alive(l)) continue;
                    V3 p = (mm_is(u_tr(l), K_TR) ? F(0x5109A08, V3 (*)(Obj, void *))(u_tr(l), NULL) : (V3){0, 0, 0}); p.y += delta;
                    (mm_is(u_tr(l), K_TR) ? F(0x5109B04, void (*)(Obj, V3, void *))(u_tr(l), p, NULL) : (void)0);
                }
        }
    }
    if (lobby) u_active(u_gc_get(h_ereason), false);
    lw_sync(v, lobby);
}
/* ---------------------------------------------------------------- lobby window (approved mockups "Lobby A" / "Lobby guest") */
/* A framed window on the Esc menu's root (its dim layer stays behind; its own column, Transform_ScaleAdjust, is hidden):
 * title band, hint strip, two columns (Players + Invite | Rules with the cog), bottom bar Leave left / Start right.
 * Host 880 x 740, guest 880 x 640. Buttons are SubUI_SystemMenuButton clones (the click hook knows them, h_lwb). The cog
 * opens the Rules panel (same as the host page) over the window. Mockup px from the window's top-left -> centre units. */
enum { LW_START, LW_INVITE, LW_COPY, LW_LEAVE, LW_GEAR, LW_NB };
static const int lw_ids[LW_NB] = { MPB_START, MPB_INVITE, MPB_COPY_ADDR, MPB_LEAVE, MPB_LOBBY_SETTINGS };
static uintptr_t h_lwr[MPB_COUNT];                       /* the lobby Rules panel's arrows + Done, by button id */
#define LW_ROWS 4
enum { T_TITLE, T_HINT, T_GUEST, T_PLAYERS, T_INV, T_RULES, T_INV_ADDR, T_INV_NOTE, T_RULES_NOTE, T_NOTE,
       T_R0L, T_R1L, T_R2L, T_R3L, T_R4L, T_R5L, T_R6L, T_R0V, T_R1V, T_R2V, T_R3V, T_R4V, T_R5V, T_R6V, T_NT };
_Static_assert(T_R0V - T_R0L == MPM_RULE_ROWS, "one label + one value text per rules row");
static uintptr_t h_lw_root, h_lw_view, h_lw_area, h_lwb[LW_NB], h_lwt[T_NT];
static uintptr_t h_lw_rowbg[LW_ROWS], h_lw_dot[LW_ROWS], h_lw_name[LW_ROWS], h_lw_stat[LW_ROWS], h_lw_tagbox[LW_ROWS][2], h_lw_bar[LW_ROWS][3], h_lw_x[LW_ROWS];
static uintptr_t h_lw_lines[6], h_lw_addrbox[2], h_lw_rrow[MPM_RULE_ROWS], h_lw_cog, h_lw_guestdot, h_lw_frame, h_lw_bbar, h_lw_bline, h_lw_body, h_lw_pcog, h_lw_rshade;
static char lw_rowname[LW_ROWS][32];                    /* row names (tooltip titles) */
static int lw_host_built = -1, lw_level[LW_ROWS];   /* connection level shown per row (-1 none), for the read-back */
static int64_t copied_until;
static float lw_h = 740;
static Obj lwt(int k) { Obj t = u_gc_get(h_lwt[k]); return u_alive(t) ? t : NULL; }
#define LX(x) ((x) - 440.0f)
#define LY(y) (lw_h / 2 - (y))
static void lw_build(Obj v, int host) {
    if (!scan_done) assets_scan_now();
    Obj src = AT(v, 0x70, Obj);                                             /* Report bug: button template */
    if (!u_alive(src)) return;
    Obj tpl_img = first_image(u_tr(src), 0), tpl_txt = AT(src, 0x28, Obj);
    lw_h = 740;                       /* layout space (top-based); the frame's real height follows the content (lw_texts) */
    Obj root = mk_img(tpl_img, u_tr(v), -1, (Col){ 0, 0, 0, 0 }, 0, 0, 880, lw_h);   /* invisible container */
    if (!root) { rig_log("mpmenu: lobby window: no frame"); return; }
    Obj R = u_tr(root);
    { Obj fr = mk_img(tpl_img, R, S_BOX, C_WHITE, 0, 0, 880, lw_h); u_gc_free(h_lw_frame); h_lw_frame = fr ? u_gc_new(fr) : 0; }
    { Obj bd = mk_img(tpl_img, R, -1, (Col){ 0.133f, 0.125f, 0.145f, 1 }, 0, 0, 876, 10); u_gc_free(h_lw_body); h_lw_body = bd ? u_gc_new(bd) : 0; }   /* #222025: one body colour (the Box sprite's inner panel has a fixed lower band) */
    u_gc_free(h_lw_root); h_lw_root = u_gc_new(root);
    u_gc_free(h_lw_view); h_lw_view = u_gc_new(v);
    u_gc_free(h_lw_area); h_lw_area = u_gc_new(u_tr_parent(u_tr_parent(u_tr(src))));   /* Transform_ScaleAdjust */
    lw_host_built = host;
    #define T(k, cz, sz, al, wr, x, y, w, h, c) { Obj t_ = mk_txt(tpl_txt, R, cz, sz, al, wr, x, y, w, h, c); u_gc_free(h_lwt[k]); h_lwt[k] = t_ ? u_gc_new(t_) : 0; }
    #define KEEP(hh, o) { Obj o_ = (o); u_gc_free(hh); hh = o_ ? u_gc_new(o_) : 0; }
    /* title band 0-72, hint strip 72-122 (guest: 72-126 with a dot) */
    mk_img(tpl_img, R, -1, C_BAND, 0, LY(36), 876, 70);
    mk_img(tpl_img, R, -1, C_COPPER, 0, LY(72), 876, 1);
    T(T_TITLE, 1, 34, 514, 0, 0, LY(36), 820, 60, C_MAIN);
    mk_img(tpl_img, R, -1, C_ROW1, 0, LY(host ? 97 : 99), 876, host ? 49 : 53);
    mk_img(tpl_img, R, -1, C_ROW2, 0, LY(host ? 122 : 126), 876, 1);
    if (host) T(T_HINT, 0, 16, 513, 0, 0, LY(97), 816, 30, C_SUB)
    else { KEEP(h_lw_guestdot, mk_txt(tpl_txt, R, 0, 26, 514, 0, LX(37), LY(99), 20, 30, C_GOLD)); T(T_GUEST, 0, 18, 513, 0, LX(54 + 300), LY(99), 600, 30, C_MAIN); }
    /* section headers: Cinzel 20 gold between two lines (lines placed at refresh, once the text width is known) */
    T(T_PLAYERS, 1, 20, 514, 0, LX(32 + 198), LY(159), 200, 28, C_GOLD);
    T(T_RULES, 1, 20, 514, 0, LX(452 + 198), LY(159), 200, 28, C_GOLD);
    if (host) T(T_INV, 1, 20, 514, 0, LX(32 + 198), LY(345), 200, 28, C_GOLD);
    for (int k = 0; k < 6; k++) KEEP(h_lw_lines[k], mk_img(tpl_img, R, -1, C_LINE, 0, 0, 10, 1));
    /* players: 4 row slots, 44 high from y 182 */
    for (int i = 0; i < LW_ROWS; i++) {
        float y = LY(182 + 44 * i + 22);
        KEEP(h_lw_rowbg[i], mk_img(tpl_img, R, -1, i % 2 ? C_ROW2 : C_ROW1, LX(32 + 198), y, 396, 44));
        KEEP(h_lw_dot[i], mk_txt(tpl_txt, R, 0, 26, 514, 0, LX(32 + 19), y, 20, 30, C_MAIN));
        KEEP(h_lw_name[i], mk_txt(tpl_txt, R, 0, 16, 513, 0, LX(32 + 36 + 120), y, 240, 30, C_MAIN));
        KEEP(h_lw_tagbox[i][0], mk_img(tpl_img, R, -1, C_LINE, 0, y, 40, 20));
        KEEP(h_lw_tagbox[i][1], mk_img(tpl_img, R, -1, i % 2 ? C_ROW2 : C_ROW1, 0, y, 38, 18));
        KEEP(h_lw_stat[i], mk_txt(tpl_txt, R, 0, 12, 514, 0, 0, y, 60, 20, C_SUB));
        for (int k = 0; k < 3; k++) {                                       /* bars 4 wide, 6 / 11 / 16 high, gap 3, bottom-aligned */
            float bh = k == 0 ? 6 : k == 1 ? 11 : 16;
            KEEP(h_lw_bar[i][k], mk_img(tpl_img, R, -1, C_UNLIT, LX(428 - 14 - 18 + 2 + 7 * k), y - 8 + bh / 2, 4, bh));
        }
        Obj x = mk_txt(tpl_txt, R, 0, 14, 514, 0, LX(428 - 14 - 9), y, 20, 20, (Col){ 0.945f, 0.376f, 0.443f, 1 });
        if (x) tmp_set(x, "x");
        KEEP(h_lw_x[i], x);
    }
    /* invite (host): Steam invite (full width), address box + Copy, note */
    Obj b;
    if (host) {
        b = mk_button(src, R, 1, S_NBTN, C_WHITE, LX(32 + 198), 0, 396, 40, 16, "Steam invite", C_MAIN); KEEP(h_lwb[LW_INVITE], b);
        KEEP(h_lw_addrbox[0], mk_img(tpl_img, R, -1, C_LINE, 0, 0, 292, 40));
        KEEP(h_lw_addrbox[1], mk_img(tpl_img, R, -1, (Col){ 0.106f, 0.098f, 0.122f, 1 }, 0, 0, 290, 38));
        T(T_INV_ADDR, 0, 16, 513, 0, 0, 0, 268, 30, C_MAIN);
        b = mk_button(src, R, 1, S_NBTN, C_WHITE, 0, 0, 96, 40, 16, "Copy", C_MAIN); KEEP(h_lwb[LW_COPY], b);
        T(T_INV_NOTE, 0, 12, 513, 1, 0, 0, 396, 34, C_SUB);
    }
    /* rules (read-only rows; the host's cog opens the Rules panel) */
    for (int i = 0; i < MPM_RULE_ROWS; i++) {
        float y = LY(188 + 40 * i + 20);
        KEEP(h_lw_rrow[i], mk_img(tpl_img, R, -1, i % 2 ? C_ROW2 : C_ROW1, LX(452 + 198), y, 396, 40));
        T(T_R0L + i, 0, 16, 513, 0, LX(452 + 14 + 100), y, 200, 30, C_SUB);
        T(T_R0V + i, 0, 16, 516, 0, LX(848 - 14 - 110), y, 220, 30, C_MAIN);
    }
    T(T_RULES_NOTE, 0, 12, 513, 0, LX(452 + 198), LY(188 + 40 * MPM_RULE_ROWS + 16), 396, 20, C_SUB);
    if (host) {
        b = mk_button(src, R, 1, S_NBTN, C_WHITE, LX(848 - 16), LY(159), 32, 32, 16, "", C_MAIN); KEEP(h_lwb[LW_GEAR], b);
        if (b) KEEP(h_lw_cog, mk_img(tpl_img, u_tr(b), S_COG, C_WHITE, 0, 0, 20, 20));
        if (b) KEEP(h_lw_pcog, mk_pixel_cog(tpl_img, u_tr(b), 0, 0, 20));
    }
    /* bottom bar (placed at the content's height in lw_texts) */
    KEEP(h_lw_bbar, mk_img(tpl_img, R, -1, C_BAND, 0, LY(lw_h - 38), 876, 74));
    KEEP(h_lw_bline, mk_img(tpl_img, R, -1, C_COPPER, 0, LY(lw_h - 76), 876, 1));
    b = mk_button(src, R, 1, S_NBTN, C_WHITE, LX(32 + 103), LY(lw_h - 38), 206, 34, 16, host ? "Leave multiplayer" : "Leave", C_MAIN); KEEP(h_lwb[LW_LEAVE], b);
    if (host) { b = mk_button(src, R, 1, S_RECRUIT, C_WHITE, LX(848 - 116), LY(lw_h - 38), 232, 44, 18, "Start", C_START); KEEP(h_lwb[LW_START], b); }
    T(T_NOTE, 0, 14, 514, 0, 0, LY(lw_h - 38), 300, 24, C_ORANGE);
    #undef T
    #undef KEEP
    if (host) { Obj sh = mk_img(tpl_img, R, -1, (Col){ 0.133f, 0.125f, 0.145f, 1 }, 0, 0, 876, 10);   /* hides the columns behind the open Rules */
                u_gc_free(h_lw_rshade); h_lw_rshade = sh ? u_gc_new(sh) : 0; u_active(sh, false); }
    if (host) rp_build(&rp_lobby, R, src, 1, tpl_img, tpl_txt, 0, 0, "Saved with this world. New worlds start with your last rules.", h_lwr, 1);
    u_active(root, false);
    rig_log("mpmenu: lobby window built (%s; frame %s, Cinzel %s, cog %s)", host ? "host" : "guest", spr(S_BOX) ? "Box" : "plain", h_cinzel ? "yes" : "no", spr(S_COG) ? "yes" : "no");
}
static float txt_w(Obj t) { if (!mm_is(t, K_TMP)) return 0; V2 sz = F(0x510701C, V2 (*)(Obj, void *))(u_tr(t), NULL); F(0x5107110, void (*)(Obj, V2, void *))(u_tr(t), (V2){ 4000, sz.y }, NULL);
    float w = F(0x4F0D81C, float (*)(Obj, void *))(t, NULL); F(0x5107110, void (*)(Obj, V2, void *))(u_tr(t), sz, NULL); return w; }
/* a section header line pair around a centred title (x0 = column left, w = column width, right = space kept for a cog) */
static void lw_header_lines(int k0, Obj t, float x0, float w, float y, float right) {
    float tw = txt_w(t), cx = x0 + (w - right) / 2, l1 = cx - tw / 2 - 10 - x0, r0 = cx + tw / 2 + 10, l2 = x0 + w - right - r0;
    Obj a = u_gc_get(h_lw_lines[k0]), b = u_gc_get(h_lw_lines[k0 + 1]);
    if (a) { place(a, LX(x0 + l1 / 2), y, l1 > 0 ? l1 : 0, 1); u_active(a, true); }
    if (b) { place(b, LX(r0 + l2 / 2), y, l2 > 0 ? l2 : 0, 1); u_active(b, true); }
    place(t, LX(cx), y, tw + 4, 28);
}
static Col hexcol(const char *h) { unsigned r = 0, g = 0, b = 0; sscanf(h[0] == '#' ? h + 1 : h, "%02x%02x%02x", &r, &g, &b); return (Col){ r / 255.f, g / 255.f, b / 255.f, 1 }; }
static void lw_texts(void) {
    int host = M.state == MPM_LOBBY;
    mp_steam_info in; mp_steam_state(&in);
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    char b[256]; const char *hn = M.host[0] ? M.host : "the host";
    if (host) tmp_set(lwt(T_TITLE), "Multiplayer Lobby"); else { snprintf(b, sizeof b, "%s's Lobby", hn); tmp_set(lwt(T_TITLE), b); }
    tmp_set(lwt(T_HINT), "Share your address, then press Start when everyone is in.");
    snprintf(b, sizeof b, "Waiting for %s to start", hn); tmp_set(lwt(T_GUEST), b);
    if (u_gc_get(h_lw_guestdot)) tmp_set(u_gc_get(h_lw_guestdot), "\xE2\x80\xA2");
    int n = in.nplayers > 0 ? in.nplayers : 1;
    snprintf(b, sizeof b, "Players (%d)", n); tmp_set(lwt(T_PLAYERS), b); tmp_set(lwt(T_RULES), "Rules"); tmp_set(lwt(T_INV), "Invite");
    /* players */
    char loading[64] = "";
    for (int i = 0; i < LW_ROWS; i++) {
        int shown = i < n && i < LW_ROWS;
        if (!shown) lw_rowname[i][0] = 0;
        Obj nm = u_gc_get(h_lw_name[i]), st = u_gc_get(h_lw_stat[i]), dot = u_gc_get(h_lw_dot[i]), x = u_gc_get(h_lw_x[i]);
        Obj tb0 = u_gc_get(h_lw_tagbox[i][0]), tb1 = u_gc_get(h_lw_tagbox[i][1]);
        u_active(u_gc_get(h_lw_rowbg[i]), shown); u_active(nm, shown); u_active(dot, shown); u_active(st, shown);
        int lvl = -1, tag = 0;                                              /* tag: 1 "Host" (boxed), 2 "You" */
        float y = LY(182 + 44 * i + 22);
        if (shown) {
            int id = in.nplayers > 0 ? in.players[i].id : 0, me = in.nplayers > 0 ? in.player : 0;
            const char *nmn = in.nplayers > 0 && in.players[i].name[0] && strcmp(in.players[i].name, "player") ? in.players[i].name : in.nplayers > 0 ? "Player" : my_name;
            tmp_set(nm, nmn); snprintf(lw_rowname[i], sizeof lw_rowname[i], "%s", nmn);
            { char d[48]; snprintf(d, sizeof d, "<color=%s>\xE2\x80\xA2</color>", ui_player_color(id)); tmp_set(dot, d); }   /* the player's colour */
            int ping = 0, behind = 0, state = DSNET_NET_IN_SYNC;
            if (c && in.nplayers > 0) dsnet_player_net(c, id, &ping, &behind, &state);
            int cs = state == DSNET_NET_IN_SYNC ? 0 : state == DSNET_NET_LOADING ? 1 : state == DSNET_NET_RESYNCING ? 2 : -1;   /* ui_conn_level's states */
            if (state == DSNET_NET_LOADING && !loading[0]) snprintf(loading, sizeof loading, "%s", nmn);
            tag = id == 0 ? 1 : id == me ? 2 : 0;
            if (id != me) lvl = state == DSNET_NET_ABSENT ? 0 : ui_conn_level(ping, behind, cs);
            tmp_set(st, tag == 1 ? "Host" : tag == 2 ? "You" : "");
            float right = 428 - 14 - (lvl >= 0 ? 18 + 12 : 0);              /* tag left of the bars */
            place(st, LX(right - 20), y, 40, 20);
            place(tb0, LX(right - 20), y, 42, 20); place(tb1, LX(right - 20), y, 40, 18);
        }
        u_active(tb0, shown && tag == 1); u_active(tb1, shown && tag == 1);
        for (int k = 0; k < 3; k++) {
            Obj bar = u_gc_get(h_lw_bar[i][k]);
            u_active(bar, shown && lvl >= 1);
            if (shown && lvl >= 1) img_set(bar, NULL, k < lvl ? hexcol(ui_conn_color(lvl)) : C_UNLIT);
        }
        u_active(x, shown && lvl == 0);
        lw_level[i] = shown ? lvl : -1;
    }
    /* header lines + invite block position (after the visible rows) */
    float rows_end = 182 + 44 * (n < LW_ROWS ? n : LW_ROWS);
    lw_header_lines(0, lwt(T_PLAYERS), 32, 396, LY(159), 0);
    lw_header_lines(2, lwt(T_RULES), 452, 396, LY(159), host ? 42 : 0);
    if (host) {
        float hy = rows_end + 18 + 13;
        lw_header_lines(4, lwt(T_INV), 32, 396, LY(hy), 0);
        float y1 = hy + 13 + 10 + 20;                                       /* Steam invite 40 high */
        Obj ib = u_gc_get(h_lwb[LW_INVITE]); if (ib) place(ib, LX(32 + 198), LY(y1), 396, 40);
        float y2 = y1 + 20 + 10 + 20;
        place(u_gc_get(h_lw_addrbox[0]), LX(32 + 146), LY(y2), 292, 40); place(u_gc_get(h_lw_addrbox[1]), LX(32 + 146), LY(y2), 290, 38);
        place(lwt(T_INV_ADDR), LX(32 + 12 + 134), LY(y2), 268, 30);
        Obj cb = u_gc_get(h_lwb[LW_COPY]); if (cb) place(cb, LX(428 - 48), LY(y2), 96, 40);
        place(lwt(T_INV_NOTE), LX(32 + 198), LY(y2 + 20 + 10 + 17), 396, 34);
        tmp_set(lwt(T_INV_ADDR), M.direct[0] ? M.direct : "...");
        { const char *port = strrchr(M.direct, ':');
          snprintf(b, sizeof b, "Friends outside your home: open port %s on your router%s.%s%s", port ? port + 1 : "47777",
                   in.steam ? ", or use Steam invite" : "", M.internet[0] ? " Internet: " : "", M.internet); tmp_set(lwt(T_INV_NOTE), b); }
        if (cb) tmp_set(AT(cb, 0x28, Obj), frames < copied_until ? "Copied" : "Copy");
        Obj ib2 = u_gc_get(h_lwb[LW_INVITE]);                               /* both ways in one app: Steam invite + the address */
        if (ib2) tmp_set(AT(ib2, 0x28, Obj), in.steam ? "Steam invite" : "<color=#A69085>Steam invite <size=80%>\xC2\xB7 Needs the Steam version</size></color>");
    }
    /* rules: host = what he set (sent as DSNET_RULES), guest = dsnet's */
    mpm_settings ss = M.set;
    if (c) { dsnet_rules r; dsnet_rules_get(c, &r); if (!host) { ss.pause_mode = r.pause_mode; ss.speedup = r.allow_speedup; ss.gear = r.gear_mode; ss.units = r.units_per_player; ss.events = r.event_choice; ss.population = r.population; }
             ss.admin = dsnet_admin_mode(c); }
    for (int i = 0; i < MPM_RULE_ROWS; i++) { char v[64]; mpm_rule_value(&ss, i, v, sizeof v); tmp_set(lwt(T_R0L + i), mpm_rule_label(i)); tmp_set(lwt(T_R0V + i), v); }
    tmp_set(lwt(T_RULES_NOTE), host ? "Only the host can change the rules." : "Set by the host.");
    if (loading[0]) snprintf(b, sizeof b, "%s is still loading", loading); else b[0] = 0;
    tmp_set(lwt(T_NOTE), b);
    Obj cg = u_gc_get(h_lw_cog); if (cg) { img_set(cg, spr(S_COG), C_WHITE); u_active(cg, spr(S_COG) != NULL); }
    u_active(u_gc_get(h_lw_pcog), spr(S_COG) == NULL);
    /* height from the content: no empty band under the columns; the window stays centred */
    float left = host ? rows_end + 18 + 26 + 10 + 40 + 10 + 40 + 10 + 34 : rows_end, right = 188 + 40 * MPM_RULE_ROWS + 8 + 20;
    float H = (left > right ? left : right) + 24 + 76;
    place(u_gc_get(h_lw_frame), 0, LY(H / 2), 880, H);
    { float top = host ? 123 : 127, bot = H - 77; place(u_gc_get(h_lw_body), 0, LY((top + bot) / 2), 876, bot - top); }
    if (!host) { u_active(u_gc_get(h_lw_lines[4]), false); u_active(u_gc_get(h_lw_lines[5]), false); }   /* no Invite header */
    place(u_gc_get(h_lw_bbar), 0, LY(H - 38), 876, 74); place(u_gc_get(h_lw_bline), 0, LY(H - 76), 876, 1);
    Obj lb = u_gc_get(h_lwb[LW_LEAVE]); if (lb) place(lb, LX(32 + 103), LY(H - 38), 206, 34);
    Obj sb = u_gc_get(h_lwb[LW_START]); if (sb) place(sb, LX(848 - 116), LY(H - 38), 232, 44);
    place(lwt(T_NOTE), 0, LY(H - 38), 300, 24);
    place(u_gc_get(h_lw_root), 0, H / 2 - lw_h / 2, 880, lw_h);
    { float top = host ? 123 : 127, bot = H - 77;                          /* inside the body: never over the title or Start */
      if (u_gc_get(rp_lobby.root)) place(u_gc_get(rp_lobby.root), 0, LY((top + bot) / 2), RP_WIDE_W, RP_WIDE_H);
      Obj sh = u_gc_get(h_lw_rshade); if (sh) { place(sh, 0, LY((top + bot) / 2), 876, bot - top); u_active(sh, host && M.lobby_settings_open); } }
    rp_update(&rp_lobby, &ss, host && M.lobby_settings_open);
}
#undef LX
#undef LY
static void lw_sync(Obj v, int lobby) {
    Obj root = u_gc_get(h_lw_root);
    int host = M.state == MPM_LOBBY;
    if (lobby && (!u_alive(root) || u_gc_get(h_lw_view) != v || lw_host_built != host)) {
        if (u_alive(root)) u_active(root, false);
        lw_build(v, host); root = u_gc_get(h_lw_root);
    }
    if (!u_alive(root)) return;
    Obj area = u_gc_get(h_lw_area);
    int was = u_on(root);
    u_active(root, lobby); u_active(area, !lobby);
    if (lobby && (!was || frames % 10 == 0)) lw_texts();
    { static int was_open; int op = M.state == MPM_LOBBY && M.lobby_settings_open;
      if (was_open && !op) rules_close_frames = 3; else if (rules_close_frames > 0) rules_close_frames--; was_open = op; }
}
static void (*o_esc_press)(Obj, void *);
static void h_esc_press(Obj btn, void *mi) {                                /* SubUI_SystemMenuButton.OnPressButton */
    for (int i = 0; i < NE; i++) if (btn && btn == eb(esc_ids[i])) { pending_click = esc_ids[i]; return; }
    for (int i = 0; i < LW_NB; i++) if (btn && btn == u_gc_get(h_lwb[i])) { pending_click = lw_ids[i]; return; }
    for (int i = 0; i < MPB_COUNT; i++) if (h_lwr[i] && btn == u_gc_get(h_lwr[i])) { pending_click = i; return; }
    o_esc_press(btn, mi);
}

/* ---------------------------------------------------------------- clipboard (test copies never touch the real one) */
/* Rig / test copies (DSRIG_BATCH, DSRIG_HIDEWIN, DSRIG_SPACE or DSMP_TEST set): GUIUtility.systemCopyBuffer get/set are
 * hooked to <copy folder>/clipboard.txt, for every caller in the game (owner rule 2026-10-12). Live app: real clipboard. */
static int test_mode(void) { return getenv("DSRIG_BATCH") || getenv("DSRIG_HIDEWIN") || getenv("DSRIG_SPACE") || getenv("DSMP_TEST"); }
static Obj (*o_clip_get)(void *);
static void (*o_clip_set)(Obj, void *);
static Obj h_clip_get(void *mi) {
    (void)mi; char p[1200], b[1024] = ""; snprintf(p, sizeof p, "%s/clipboard.txt", rig_dir);
    FILE *f = fopen(p, "r"); if (f) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); }
    return u_str(b);
}
static void h_clip_set(Obj v, void *mi) {
    (void)mi; char p[1200], b[1024]; snprintf(p, sizeof p, "%s/clipboard.txt", rig_dir);
    str_utf8(v, b, sizeof b);
    FILE *f = fopen(p, "w"); if (f) { fputs(b, f); fclose(f); }
}

/* player name when none is set (no Steam persona, no mp.json): the first word of the Mac user's full name */
#include <pwd.h>
#include <unistd.h>
static void default_name(char *name, size_t cap) {
    if (name[0] && strcmp(name, "player")) return;
    struct passwd *pw = getpwuid(getuid());
    const char *g = pw && pw->pw_gecos && pw->pw_gecos[0] ? pw->pw_gecos : pw ? pw->pw_name : "Player";
    size_t n = strcspn(g, " ,");
    snprintf(name, cap, "%.*s", (int)(n < cap - 1 ? n : cap - 1), g);
}

/* ---------------------------------------------------------------- remembered settings (owner 2026-10-13)
 * <app data folder>/mp_menu.json  {"pause":1,"speedup":1,"gear":0,"name":"The owner"}: the last used host settings + name.
 * <app data folder>/mp_worlds.txt one line per world played in multiplayer: "<campaign guid> <pause> <speedup> <gear>".
 * The app data folder (DSRIG_DIR) is shared by every lan_vN build, so both survive app updates. A world's own line
 * is its stored settings (loading it uses them); a new world gets the last used ones. The load list tags these worlds. */
const char *ui_default_player_name(void);                                   /* ui_game.c */
static void app_path(char *p, size_t cap, const char *f) { snprintf(p, cap, "%s/%s", rig_dir, f); }
static void prefs_load(void) {
    char p[1200], b[1024] = ""; app_path(p, sizeof p, "mp_menu.json");
    FILE *f = fopen(p, "r"); if (f) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); }
    double v;
    if (jnum(b, "pause", &v) && v >= 0 && v <= 2) M.set.pause_mode = (int)v;
    if (jnum(b, "speedup", &v)) M.set.speedup = v != 0;
    if (jnum(b, "gear", &v) && v >= 0 && v <= 2) M.set.gear = (int)v;
    if (jnum(b, "units", &v) && (v == 1 || v == 2 || v == 3 || v == 4 || v == 6)) M.set.units = (int)v;
    if (jnum(b, "admin", &v)) M.set.admin = v != 0;
    if (jnum(b, "events", &v) && v >= 0 && v <= 3) M.set.events = (int)v;
    if (jnum(b, "population", &v) && v >= 0 && v <= 3) M.set.population = (int)v;
    if (jnum(b, "chat_fade", &v) && v >= 3 && v <= 120) chat_fade_s = (int)v;
    if (jnum(b, "chat_join_lines", &v)) chat_show_join = v != 0;
    if (jnum(b, "chat_on", &v)) chat_on = v != 0;
    if (jnum(b, "chat_x", &v)) chat_x = (float)v;
    if (jnum(b, "chat_y", &v)) chat_y = (float)v;
    if (!jstr(b, "name", my_name, sizeof my_name) || !my_name[0]) {
        const char *st = dsnet_steam_load(NULL) == 0 ? dsnet_steam_my_name() : "";
        snprintf(my_name, sizeof my_name, "%s", st && st[0] ? st : ui_default_player_name());
    }
    rig_log("mpmenu: remembered settings pause %d speed-up %d gear %d, name \"%s\"%s", M.set.pause_mode, M.set.speedup, M.set.gear, my_name, b[0] ? "" : " (defaults)");
}
static void prefs_save(void) {
    char p[1200]; app_path(p, sizeof p, "mp_menu.json");
    FILE *f = fopen(p, "w"); if (!f) return;
    Buf b = {0}; bput(&b, "{\"pause\":%d,\"speedup\":%d,\"gear\":%d,\"units\":%d,\"admin\":%d,\"events\":%d,\"population\":%d,\"chat_fade\":%d,\"chat_join_lines\":%d,\"chat_on\":%d,\"chat_x\":%.0f,\"chat_y\":%.0f,\"name\":",
        M.set.pause_mode, M.set.speedup, M.set.gear, M.set.units, M.set.admin, M.set.events, M.set.population, chat_fade_s, chat_show_join, chat_on, chat_x, chat_y); bstr(&b, my_name); bput(&b, "}\n");
    fputs(b.p, f); fclose(f); free(b.p);
}
static int world_get(const char *guid, mpm_settings *out) {
    char p[1200], line[256]; app_path(p, sizeof p, "mp_worlds.txt");
    FILE *f = fopen(p, "r"); if (!f) return 0;
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        char g[64]; int a, b2, c, u = 3, ad = 0, ev = 0, po = 0;            /* units, admin, events, population: 5th-8th fields (older: 3, 0, 0, 0) */
        if (sscanf(line, "%63s %d %d %d %d %d %d %d", g, &a, &b2, &c, &u, &ad, &ev, &po) >= 4 && !strcmp(g, guid)) { out->pause_mode = a; out->speedup = b2; out->gear = c; out->units = u; out->admin = ad; out->events = ev; out->population = po; found = 1; }
    }
    fclose(f); return found;
}
static void world_put(const char *guid, const mpm_settings *st) {
    char p[1200], tmp[1300], line[256]; app_path(p, sizeof p, "mp_worlds.txt"); snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *in = fopen(p, "r"), *out = fopen(tmp, "w"); if (!out) { if (in) fclose(in); return; }
    while (in && fgets(line, sizeof line, in)) { char g[64]; if (sscanf(line, "%63s", g) == 1 && strcmp(g, guid)) fputs(line, out); }
    fprintf(out, "%s %d %d %d %d %d %d %d\n", guid, st->pause_mode, st->speedup, st->gear, st->units, st->admin, st->events, st->population);
    if (in) fclose(in);
    fclose(out); rename(tmp, p);
}
static int campaign_guid(char *out, size_t cap) {                          /* ClanDataContainer.CampaignGuid (campaign data +0x50, +0x10) = the save header's */
    Obj c = cdc(), cl = c ? AT(c, 0x50, Obj) : NULL; if (!cl) return 0;
    Guid g = AT(cl, 0x10, Guid); if (!g.lo && !g.hi) return 0;
    char gs[40]; guid_str(g, gs); snprintf(out, cap, "%s", gs); return 1;
}

/* load list: worlds played in multiplayer get a tag after their title (SubUI_SaveFileButton.SetData post-hook) */
static void (*o_slot_set)(Obj, const void *, bool, Obj, void *);
static void h_slot_set(Obj self, const void *hdr, bool inter, Obj act, void *mi) {
    o_slot_set(self, hdr, inter, act, mi);
    if (!hdr || !self) return;
    Guid g; memcpy(&g, hdr, 16);                                           /* CampaignSaveHeader.CampaignGuid @0x0 (header by reference) */
    char gs[40]; guid_str(g, gs); mpm_settings st;
    if (!world_get(gs, &st)) return;
    Obj t = AT(self, 0x38, Obj);                                            /* _titleText */
    char cur[256] = ""; str_utf8(tmp_get(t), cur, sizeof cur);
    if (strstr(cur, "[Multiplayer]")) return;
    char nt[320]; snprintf(nt, sizeof nt, "%s  <color=#8FC4FF>[Multiplayer]</color>", cur);
    tmp_set(t, nt);
}

/* owner rule: no loading another world during a multiplayer session (host or guest). The Esc menu's Load button is
 * disabled with a note; its click and the quick-load key do nothing. Leaving multiplayer restores them. */
static int in_session(void) { return mp_game_session() != NULL; }
static void (*o_esc_load)(Obj, void *);
static void h_esc_load(Obj v, void *mi) { if (in_session()) { rig_log("mpmenu: Load blocked during multiplayer"); return; } o_esc_load(v, mi); }
static Obj (*o_quickload)(Obj, Obj, void *);
static Obj h_quickload(Obj self, Obj msg, void *mi) { if (in_session()) { rig_log("mpmenu: quick load blocked during multiplayer"); return NULL; } return o_quickload(self, msg, mi); }
/* design #7: the stock label stays ("Load Campaign"), the button is greyed (SetInteractable, stock tint); the reason is a
 * seed-style line under the buttons (esc_sync, h_ereason). Older builds swapped the label: restored if seen. */
static char load_label[128];
static void esc_load_rule(Obj v) {
    Obj lb = AT(v, 0x60, Obj), t = u_alive(lb) ? AT(lb, 0x28, Obj) : NULL;  /* _loadCampaignButton, _buttonName */
    if (!u_alive(t)) return;
    char cur[160] = ""; str_utf8(tmp_get(t), cur, sizeof cur);
    if (strstr(cur, "Leave multiplayer") && load_label[0]) tmp_set(t, load_label);   /* label of an older build */
    /* the stock tint alone is hard to see (design run 8): the label also takes the secondary colour #a69085 */
    static int off;                                                         /* only undo our own greying (stock rules stay) */
    if (in_session()) {
        if (!strstr(cur, "#A69085")) { snprintf(load_label, sizeof load_label, "%s", cur); char g[200]; snprintf(g, sizeof g, "<color=#A69085>%s</color>", load_label); tmp_set(t, g); }
        (mm_is(lb, K_SMB) ? F(0xF2DC14, void (*)(Obj, bool, void *))(lb, false, NULL) : (void)0); off = 1;   /* SubUI_SystemMenuButton.SetInteractable */
    } else if (off) { tmp_set(t, load_label); (mm_is(lb, K_SMB) ? F(0xF2DC14, void (*)(Obj, bool, void *))(lb, true, NULL) : (void)0); off = 0; }
}

/* ---------------------------------------------------------------- direct address */
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
static void lan_ip(char *out, size_t cap) {                                 /* first up, non-loopback IPv4 (en0 first) */
    struct ifaddrs *ifs = NULL, *i; snprintf(out, cap, "127.0.0.1");
    if (getifaddrs(&ifs)) return;
    for (int pass = 0; pass < 2; pass++)
        for (i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
            if (pass == 0 && strcmp(i->ifa_name, "en0")) continue;
            inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, out, (socklen_t)cap);
            freeifaddrs(ifs); return;
        }
    freeifaddrs(ifs);
}
/* public IPv4 as Steam sees it: ISteamUser::GetPublicIP (flat API, SDK 1.62), from the game's own libsteam_api. No web lookup. */
typedef struct { union { uint32_t v4; uint8_t v6[16]; } a; int32_t type; } SteamIP;   /* SteamIPAddress_t; type 0 = IPv4, 1 = IPv6 */
static int steam_public_ip(char *out, size_t cap) {
    static void *(*user)(void); static SteamIP (*getip)(void *); static int tried;
    if (!tried) {
        tried = 1;
        for (uint32_t i = 0, n = _dyld_image_count(); i < n; i++) {
            const char *img = _dyld_get_image_name(i);
            if (!img || !strstr(img, "libsteam_api.dylib")) continue;
            void *lib = dlopen(img, RTLD_NOLOAD | RTLD_NOW);
            if (lib) { user = (void *(*)(void))dlsym(lib, "SteamAPI_SteamUser_v023"); getip = (SteamIP (*)(void *))dlsym(lib, "SteamAPI_ISteamUser_GetPublicIP"); }
            break;
        }
        rig_log("mpmenu: Steam public IP call %s", user && getip ? "found" : "not available");
    }
    void *u = user && getip ? user() : NULL;
    if (!u) return 0;
    SteamIP ip = getip(u);
    if (ip.type != 0 || !ip.a.v4) return 0;
    snprintf(out, cap, "%u.%u.%u.%u", ip.a.v4 >> 24, ip.a.v4 >> 16 & 255, ip.a.v4 >> 8 & 255, ip.a.v4 & 255);
    return 1;
}
static void direct_refresh(void) {
    mp_session *s = mp_game_session(); uint16_t p = s ? mp_server_port(s) : 0;
    if (!p) { M.direct[0] = M.internet[0] = 0; return; }
    char ip[64], pub[64]; lan_ip(ip, sizeof ip);
    snprintf(M.direct, sizeof M.direct, "%s:%u", ip, p);
    if (steam_public_ip(pub, sizeof pub)) snprintf(M.internet, sizeof M.internet, "%s:%u", pub, p);
    else M.internet[0] = 0;
}
static char hist_host[48];                               /* reconnecting from a Recent worlds row: that host's name */
static char joined_how[8], joined_addr[96];               /* how this guest joined: "ip" + address, or "steam" */
static uint64_t joined_steamid;
/* Join by address (ux #1): the TCP connect runs on dsnet's helper thread (dsnet_connect_*), limited to 8 s; the page
 * shows "Connecting to <address>..." with Cancel meanwhile. Once connected, the socket is handed to mp_game_join. */
static dsnet_connector *conn;
static mp_config conn_cfg;
static double conn_t0;
static double now_ms_(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
static void join_refused(int rc, const char *addr) {
    static int (*last_reject)(void); static int looked;                 /* mpglue: enum dsnet_reject of the last failed start */
    if (!looked) { looked = 1; last_reject = (int (*)(void))dlsym(RTLD_DEFAULT, "mp_game_last_reject"); }
    int r = rc && last_reject ? last_reject() : 0;
    const char *w = dsnet_reject_text(r);                               /* the same wording as mp_game's reject_text (ux #22) */
    if (*w) snprintf(M.msg, sizeof M.msg, "Could not join: %s", w);
    else if (rc) snprintf(M.msg, sizeof M.msg, "%s answered, but the game did not start. Try again.", addr);
}
static void join_by_address(void) {
    char txt[256] = "";
    Obj f = u_gc_get(h_field);
    if (u_alive(f)) str_utf8((mm_is(f, K_FIELD) ? F(0x4EFAE84, Obj (*)(Obj, void *))(f, NULL) : NULL), txt, sizeof txt);       /* TMP_InputField.text */
    else str_utf8(F(0x518AEA0, Obj (*)(void *))(NULL), txt, sizeof txt);                       /* clipboard */
    mp_config c; mp_config_defaults(&c); c.host = 0; c.need_save = 1;
    char p[1200]; snprintf(p, sizeof p, "%s/mp.json", rig_dir);
    mp_config fj; if (mp_config_load(p, &fj) == 0) snprintf(c.name, sizeof c.name, "%s", fj.name);
    uint16_t port = 0;
    if (ui_parse_address(txt, c.host_addr, sizeof c.host_addr, &port, MPM_DEFAULT_PORT)) {
        snprintf(M.msg, sizeof M.msg, "Not an address: \"%.40s\" (use ip:port)", txt); return;
    }
    c.port = port;
    char nm[64]; field_text(h_namefield, nm, sizeof nm); if (nm[0]) snprintf(my_name, sizeof my_name, "%s", nm);
    snprintf(c.name, sizeof c.name, "%s", my_name); default_name(c.name, sizeof c.name);
    if (conn) dsnet_connect_free(conn);
    conn_cfg = c; conn_t0 = now_ms_();
    conn = dsnet_connect_start(c.host_addr, port, DSNET_CONNECT_TIMEOUT_MS);
    snprintf(M.connecting, sizeof M.connecting, "%s:%u", c.host_addr, port);
    rig_log("mpmenu: join by address %s: connecting (limit %d ms)", M.connecting, DSNET_CONNECT_TIMEOUT_MS);
}
static void join_poll(void) {                                              /* each frame: never waits */
    if (!conn) return;
    if (!M.connecting[0]) { dsnet_connect_free(conn); conn = NULL; rig_log("mpmenu: connect cancelled"); return; }
    int r = dsnet_connect_poll(conn);
    if (!r) return;
    char addr[96]; snprintf(addr, sizeof addr, "%s", M.connecting);
    M.connecting[0] = 0;
    if (r == 1) dsnet_connect_adopt(conn);
    dsnet_connect_free(conn); conn = NULL;
    rig_log("mpmenu: connect to %s: %d after %.0f ms", addr, r, now_ms_() - conn_t0);
    if (r == DSNET_CONN_TIMEOUT) snprintf(M.msg, sizeof M.msg, "No answer from %s. Check the address, and ask the host to open port %u on the router.", addr, conn_cfg.port);
    else if (r == DSNET_CONN_REFUSED) snprintf(M.msg, sizeof M.msg, "Nobody is hosting at %s. Check the address and the port.", addr);
    else if (r == DSNET_CONN_BAD_ADDRESS) snprintf(M.msg, sizeof M.msg, "Unknown address: %s. Check the address.", addr);
    else if (r < 0) snprintf(M.msg, sizeof M.msg, "Cannot reach %s. Check the address and your network.", addr);
    if (r != 1 && hist_host[0]) { snprintf(M.msg, sizeof M.msg, "%s is not hosting this world right now.", hist_host); hist_host[0] = 0; }   /* a Recent worlds row */
    if (r != 1) return;
    int rc = mp_game_join(&conn_cfg);                                     /* uses the adopted socket: no connect wait */
    join_refused(rc, addr);
    if (rc && hist_host[0]) snprintf(M.msg, sizeof M.msg, "%s is not hosting this world right now.", hist_host);
    hist_host[0] = 0;
    if (!rc) { prefs_save(); snprintf(joined_how, sizeof joined_how, "ip"); snprintf(joined_addr, sizeof joined_addr, "%s", addr); }
    rig_log("mpmenu: join by address %s rc %d as \"%s\"", addr, rc, conn_cfg.name);
}

/* ---------------------------------------------------------------- host rules (DSNET_RULES) */
/* The host's settings are the session's rules: sent at host start (dsnet's WELCOME has no unit limit field in
 * mp_config) and on each change in the lobby. Changes made elsewhere (the in-game panel's cog, ui lane) come back
 * through rules_sync: M.set follows dsnet and the world's line in mp_worlds.txt is rewritten. */
static int64_t rules_sent_frame = -100000;
static void rules_send(const char *why) {
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    dsnet_rules r = { M.set.pause_mode, M.set.speedup, M.set.gear, M.set.units, M.set.events, M.set.population };
    int rc = c ? dsnet_set_rules(c, &r) : -1;
    rules_sent_frame = frames;
    if (c && !strcmp(why, "host start") && M.set.admin) { uint8_t one = 1; dsnet_submit(c, DSNET_ADMIN_MODE, 0, &one, 1); }   /* host page "Admin mode: on" */
    char gid[64] = ""; prefs_save(); if (campaign_guid(gid, sizeof gid)) world_put(gid, &M.set);
    rig_log("mpmenu: rules (%s): pause %d speed-up %d gear %d units %d (rc %d)", why, r.pause_mode, r.allow_speedup, r.gear_mode, r.units_per_player, rc);
}
static void rules_sync(const mpm_obs *o) {
    if (o->role != 1 || frames % 60 || frames - rules_sent_frame < 120) return;   /* host; our own change has landed */
    mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
    if (!c) return;
    dsnet_rules r; dsnet_rules_get(c, &r);
    int adm = dsnet_admin_mode(c);
    if (r.pause_mode == M.set.pause_mode && (r.allow_speedup != 0) == (M.set.speedup != 0) && r.gear_mode == M.set.gear && r.units_per_player == M.set.units && r.event_choice == M.set.events && r.population == M.set.population && adm == M.set.admin) return;
    M.set.pause_mode = r.pause_mode; M.set.speedup = r.allow_speedup; M.set.gear = r.gear_mode; M.set.units = r.units_per_player; M.set.events = r.event_choice; M.set.population = r.population; M.set.admin = adm;
    char gid[64] = ""; prefs_save(); if (campaign_guid(gid, sizeof gid)) world_put(gid, &M.set);
    rig_log("mpmenu: rules changed in play: pause %d speed-up %d gear %d units %d (world saved)", r.pause_mode, r.allow_speedup, r.gear_mode, r.units_per_player);
}

/* ---------------------------------------------------------------- host lobby hold (ux #14) */
/* The world waits while the host is in the lobby: a dsnet LOBBY hold (0 game ticks on every peer, every pause mode),
 * on at host start, off at Start. Guests see it (observe: host_lobby) and get the waiting panel. */
static void lobby_hold(int on) {
    mp_session *s = mp_game_session();
    int rc = s ? dsnet_hold(mp_client(s), on, DSNET_HOLD_LOBBY, NULL) : -1;
    rig_log("mpmenu: lobby hold %s (rc %d)", on ? "on" : "off", rc);
}

/* ---------------------------------------------------------------- session ended: stock notice box (ux #9) */
/* CommonNoticeView (StaticUI): Show_Confirm(text, onConfirm) 0xF65498, _noticeText +0x30, _txtConfirm +0x40. Shown once
 * the title is up; if the box cannot be found, the text goes to the Multiplayer button line as before. */
static uintptr_t h_cnv; static void *k_cnv;
static int notice_tries, title_calm;
static void notice_frame(const mpm_obs *o) {
    title_calm = o->scene == 1 && o->title_visible && !o->loading ? title_calm + 1 : 0;
    if (!M.notice[0] || title_calm < 90) return;                            /* the loading curtain is gone (~3 s of title) */
    Obj v = find_view_x(&h_cnv, &k_cnv, "Refactor.UI", "CommonNoticeView", true);
    if (!v) {
        if (++notice_tries < 120) return;
        snprintf(M.msg, sizeof M.msg, "%.*s", (int)strcspn(M.notice, ".\n"), M.notice);
        rig_log("mpmenu: no notice box: \"%s\" goes to the button line", M.msg);
        M.notice[0] = 0; notice_tries = 0; return;
    }
    (mm_is(v, K_CNV) ? F(0xF65498, void (*)(Obj, Obj, Obj, void *))(v, u_str(M.notice), NULL, NULL) : (void)0);   /* Show_Confirm */
    tmp_set(AT(v, 0x30, Obj), M.notice);                                   /* raw text (not a text key) */
    tmp_set(AT(v, 0x40, Obj), "OK");
    u_go_active(u_go(v), true);
    rig_log("mpmenu: notice box: %s", M.notice);
    M.notice[0] = 0; notice_tries = 0;
}

/* ---------------------------------------------------------------- "Host from a save": the stock load list, retitled (ux #18) */
/* SaveLoadView _saveLoadTitle +0x48 (DASText), _loadButton +0xA0 (DASButton, label = first TMP_Text below it).
 * Retitled while the menu picks a save to host; the stock texts come back when it closes. */
static uintptr_t h_pick_title, h_pick_btn; static char pick_title0[128], pick_btn0[64]; static bool pick_key0[2]; static int pick_on;
static Obj tmp_below(Obj tr, int depth) {
    static void *k; if (!k) k = class_by_name("TMPro", "TMP_Text");
    if (!k || !u_alive(tr)) return NULL;
    Obj ty = F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(k));
    Obj t = (mm_is(tr, K_COMP) ? F(0x50EDA7C, Obj (*)(Obj, Obj, void *))(tr, ty, NULL) : NULL);
    if (u_alive(t) || depth >= 3) return u_alive(t) ? t : NULL;
    int n = (mm_is(tr, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(tr, NULL) : 0);
    for (int i = 0; i < n; i++) { Obj c = tmp_below((mm_is(tr, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(tr, i, NULL) : NULL), depth + 1); if (c) return c; }
    return NULL;
}
static void pick_frame(const mpm_obs *o) {
    int want = M.state == MPM_PICK_SAVE && o->picker_open;
    if (want && frames % 5 == 0) {
        Obj slv = find_view_x(&h_slv, &k_slv, "Refactor", "SaveLoadView", true);
        Obj t = slv ? AT(slv, 0x48, Obj) : NULL, lb = slv ? AT(slv, 0xA0, Obj) : NULL, bt = u_alive(lb) ? tmp_below(u_tr(lb), 0) : NULL;
        if (!u_alive(t)) return;
        if (!pick_on) {
            str_utf8(tmp_get(t), pick_title0, sizeof pick_title0); pick_key0[0] = !strcmp(cls_name(t), "DASText") && AT(t, 0x7C0, bool);
            pick_btn0[0] = 0; if (bt) { str_utf8(tmp_get(bt), pick_btn0, sizeof pick_btn0); pick_key0[1] = !strcmp(cls_name(bt), "DASText") && AT(bt, 0x7C0, bool); }
            u_gc_free(h_pick_title); h_pick_title = u_gc_new(t); u_gc_free(h_pick_btn); h_pick_btn = bt ? u_gc_new(bt) : 0;
            pick_on = 1; rig_log("mpmenu: load list retitled (was \"%s\" / \"%s\")", pick_title0, pick_btn0);
        }
        char cur[128] = ""; str_utf8(tmp_get(t), cur, sizeof cur); if (strcmp(cur, "Host a world")) tmp_set(t, "Host a world");
        if (bt) { str_utf8(tmp_get(bt), cur, sizeof cur); if (strcmp(cur, "Host")) tmp_set(bt, "Host"); }
    } else if (!want && pick_on) {
        Obj t = u_gc_get(h_pick_title), bt = u_gc_get(h_pick_btn);
        if (u_alive(t)) { tmp_set(t, pick_title0); if (pick_key0[0]) AT(t, 0x7C0, bool) = true; }
        if (u_alive(bt)) { tmp_set(bt, pick_btn0); if (pick_key0[1]) AT(bt, 0x7C0, bool) = true; }
        u_gc_free(h_pick_title); u_gc_free(h_pick_btn); h_pick_title = h_pick_btn = 0; pick_on = 0;
        rig_log("mpmenu: load list titles restored");
    }
}

/* ---------------------------------------------------------------- Steam friends hosting (lan_v8) */
/* Steam friends playing this game who sit in a lobby (dsnet_steam_friends_in_game): one "Join <name>'s world" each. */
static uint64_t friend_lobby[6], friend_id[6];
static void friends_refresh(void) {
    dsnet_steam_friend f[32]; int n = M.steam ? dsnet_steam_friends_in_game(f, 32) : 0, k = 0;
    for (int i = 0; i < n && k < 6; i++) if (f[i].lobby) { friend_lobby[k] = f[i].lobby; friend_id[k] = f[i].steamid; snprintf(M.friends[k], sizeof M.friends[k], "%s", f[i].name); k++; }
    if (k != M.nfriends) rig_log("mpmenu: %d Steam friend(s) hosting", k);
    M.nfriends = k;
}

/* ---------------------------------------------------------------- Recent worlds (owner, lan_v8) */
/* <app data folder>/mp_history.json: a JSON array, one world per line, newest first, max 12. Written every ~10 s while
 * a session plays (so the last state survives a crash or a host leaving), for guests and hosts. Click = reconnect the
 * same way (TCP to the address, or Steam to that host's lobby); "x" removes a row. */
static void hist_save(void) {
    char p[1200], tmp[1300]; app_path(p, sizeof p, "mp_history.json"); snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "w"); if (!f) return;
    fputs("[\n", f);
    for (int i = 0; i < nhist; i++) {
        hist_t *h = &hist[i]; Buf b = {0};
        bput(&b, "{\"key\":"); bstr(&b, h->key); bput(&b, ",\"world\":"); bstr(&b, h->world); bput(&b, ",\"day\":%lld,\"how\":", h->day); bstr(&b, h->how);
        bput(&b, ",\"addr\":"); bstr(&b, h->addr); bput(&b, ",\"host\":"); bstr(&b, h->host); bput(&b, ",\"steamid\":\"%llu\",\"players\":", (unsigned long long)h->steamid);
        bstr(&b, h->players); bput(&b, ",\"units\":"); bstr(&b, h->units); bput(&b, ",\"portraits\":"); bstr(&b, h->portraits);
        bput(&b, ",\"when\":%lld}%s\n", h->when, i + 1 < nhist ? "," : "");
        fputs(b.p, f); free(b.p);
    }
    fputs("]\n", f); fclose(f); rename(tmp, p);
}
static void hist_load(void) {
    hist_loaded = 1; nhist = 0;
    char p[1200], line[2048]; app_path(p, sizeof p, "mp_history.json");
    FILE *f = fopen(p, "r"); if (!f) return;
    while (nhist < HIST_MAX && fgets(line, sizeof line, f)) {
        if (line[0] != '{') continue;
        hist_t *h = &hist[nhist]; memset(h, 0, sizeof *h); double v; char sid[32] = "";
        if (!jstr(line, "key", h->key, sizeof h->key)) continue;
        jstr(line, "world", h->world, sizeof h->world); jstr(line, "how", h->how, sizeof h->how); jstr(line, "addr", h->addr, sizeof h->addr);
        jstr(line, "host", h->host, sizeof h->host); jstr(line, "players", h->players, sizeof h->players); jstr(line, "units", h->units, sizeof h->units); jstr(line, "portraits", h->portraits, sizeof h->portraits);
        if (jnum(line, "day", &v)) h->day = (long long)v; if (jnum(line, "when", &v)) h->when = (long long)v;
        if (jstr(line, "steamid", sid, sizeof sid)) h->steamid = strtoull(sid, NULL, 10);
        nhist++;
    }
    fclose(f);
}
/* the current session as a Recent worlds row (newest first) */
/* palette index -> "#RRGGBB" (ui_player_color without the session's slot -> uid mapping: the index is stored as is) */
static const char *palette_color(int idx) {
    int (*res)(int) = ui_color_resolver; ui_color_resolver = NULL;
    const char *col = ui_player_color(idx < 0 ? 0 : idx);
    ui_color_resolver = res; return col;
}
static void hist_update(void) {
    mp_session *ss = mp_game_session(); dsnet_client *c = ss ? mp_client(ss) : NULL; Obj cd = cdc();
    if (!c || !cd) return;
    if (!hist_loaded) hist_load();
    mp_steam_info in; mp_steam_state(&in);
    hist_t e; memset(&e, 0, sizeof e);
    char gid[64] = ""; campaign_guid(gid, sizeof gid);
    Obj clan = AT(cd, 0x50, Obj); if (clan) str_utf8(AT(clan, 0x20, Obj), e.world, sizeof e.world);     /* ClanDataContainer.ClanName */
    Obj td = AT(cd, 0xA8, Obj); if (td) e.day = AT(td, 0x20, int64_t);                              /* CampaignDataContainer.TimeData (dump 25846389) .Day */
    if (in.is_host) { snprintf(e.how, sizeof e.how, "host"); snprintf(e.addr, sizeof e.addr, "%s", M.direct); snprintf(e.host, sizeof e.host, "%s", my_name); }
    else if (!strcmp(joined_how, "ip")) { snprintf(e.how, sizeof e.how, "ip"); snprintf(e.addr, sizeof e.addr, "%s", joined_addr); snprintf(e.host, sizeof e.host, "%s", in.host_name); }
    else { snprintf(e.how, sizeof e.how, "steam"); e.steamid = joined_steamid ? joined_steamid : dsnet_steam_lobby_owner(in.lobby); snprintf(e.host, sizeof e.host, "%s", in.host_name); }
    if (!e.host[0]) snprintf(e.host, sizeof e.host, "the host");
    size_t o = 0;
    for (int i = 0; i < in.nplayers; i++) if (in.players[i].id != in.player)
        o += (size_t)snprintf(e.players + o, sizeof e.players - (o < sizeof e.players ? o : sizeof e.players), "%s%s|%d", o ? ";" : "", in.players[i].name, dsnet_player_color(c, in.players[i].id) >= 0 ? dsnet_player_color(c, in.players[i].id) : in.players[i].id);   /* the world's palette index (stable uid); none: the slot */
    uint8_t g[8][16]; int nu = dsnet_owned_list(c, dsnet_player_id(c), g, 8); o = 0; size_t op = 0;
    /* portraits: the shortcut bar's parts (SubUI_UnitShortCutParts: _owner +0x228, _portraitImage +0x140 (dump 25846389)): the sprite name */
    static void *kparts; if (!kparts) kparts = class_by_name("Refactor.UI", "SubUI_UnitShortCutParts");
    Obj parr = kparts ? F(0x50FCCC0, Obj (*)(Obj, void *))(F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(kparts)), NULL) : NULL;
    int nparts = parr ? (int)AT(parr, 0x18, uintptr_t) : 0;
    for (int i = 0; i < nu && i < 8; i++) {
        Guid gg; memcpy(&gg, g[i], 16); Obj u = find_unit(cur_map(), gg); if (!u && cur_map() != 0) u = find_unit(0, gg);
        char un[48] = ""; if (u) str_utf8(F(0xFA467C, Obj (*)(Obj, void *))(u, NULL), un, sizeof un);   /* GetDisplayName */
        if (!un[0]) continue;
        o += (size_t)snprintf(e.units + o, sizeof e.units - (o < sizeof e.units ? o : sizeof e.units), "%s%s", o ? ", " : "", un);
        char sn[64] = "";
        for (int k = 0; k < nparts && u; k++) {
            Obj pt = AT((char *)parr + 0x20, k * 8, Obj);
            if (!u_alive(pt) || AT(pt, 0x228, Obj) != u) continue;
            Obj im = AT(pt, 0x140, Obj), sp = mm_is(im, K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(im, NULL) : NULL;
            if (u_alive(sp)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(sp, NULL), sn, sizeof sn);
            break;
        }
        op += (size_t)snprintf(e.portraits + op, sizeof e.portraits - (op < sizeof e.portraits ? op : sizeof e.portraits), "%s%s", op ? "," : "", sn);
    }
    snprintf(e.key, sizeof e.key, "%s|%s|%s", gid, e.how, !strcmp(e.how, "steam") ? "" : e.addr);
    if (!strcmp(e.how, "steam")) { size_t kl = strlen(e.key); snprintf(e.key + kl, sizeof e.key - kl, "%llu", (unsigned long long)e.steamid); }
    e.when = (long long)time(NULL);
    int at = -1; for (int i = 0; i < nhist; i++) if (!strcmp(hist[i].key, e.key)) at = i;
    if (at < 0) at = nhist < HIST_MAX ? nhist++ : HIST_MAX - 1;
    memmove(&hist[1], &hist[0], sizeof hist[0] * at);                   /* newest first */
    hist[0] = e;
    M.nhist = nhist;
    hist_save();
}
static void hist_join(int k) {
    if (k < 0 || k >= nhist) return;
    hist_t *h = &hist[k];
    if (!strcmp(h->how, "host")) { snprintf(M.msg, sizeof M.msg, "You hosted %s: open it with Host a game > Host from a save.", h->world); return; }
    if (!strcmp(h->how, "steam")) {
        if (!M.steam) { snprintf(M.msg, sizeof M.msg, "This world was joined through Steam: needs the Steam version."); return; }
        friends_refresh();
        for (int i = 0; i < M.nfriends; i++) if (friend_id[i] == h->steamid) {
            int rc = mp_steam_join(friend_lobby[i]);
            if (!rc) { snprintf(joined_how, sizeof joined_how, "steam"); joined_steamid = h->steamid; return; }
        }
        snprintf(M.msg, sizeof M.msg, "%s is not hosting this world right now.", h->host); return;
    }
    /* IP: the same non-blocking connect as Join by address; "not hosting" if nobody answers there */
    mp_config cc; mp_config_defaults(&cc); cc.host = 0; cc.need_save = 1; uint16_t port = 0;
    if (ui_parse_address(h->addr, cc.host_addr, sizeof cc.host_addr, &port, MPM_DEFAULT_PORT)) { snprintf(M.msg, sizeof M.msg, "Bad address in this row: %s", h->addr); return; }
    cc.port = port; snprintf(cc.name, sizeof cc.name, "%s", my_name); default_name(cc.name, sizeof cc.name);
    if (conn) dsnet_connect_free(conn);
    conn_cfg = cc; conn_t0 = now_ms_(); snprintf(hist_host, sizeof hist_host, "%s", h->host);
    conn = dsnet_connect_start(cc.host_addr, port, DSNET_CONNECT_TIMEOUT_MS);
    snprintf(M.connecting, sizeof M.connecting, "%s:%u", cc.host_addr, port);
    rig_log("mpmenu: recent world %s: reconnecting to %s", h->world, M.connecting);
}
static void hist_del(int k) {
    if (k < 0 || k >= nhist) return;
    rig_log("mpmenu: recent world %s removed", hist[k].world);
    memmove(&hist[k], &hist[k + 1], sizeof hist[0] * (size_t)(nhist - k - 1)); nhist--; M.nhist = nhist;
    if (M.hist_page * 6 >= nhist && M.hist_page > 0) M.hist_page--;
    hist_save();
}

/* ---------------------------------------------------------------- chat (owner, lan_v8; mockup "Chat: idle, typing, history") */
/* Lines come from dsnet (outside the sim). Idle: the last lines bottom-left above the build bar, no frame, fading after
 * the fade time. Enter: a framed box with the history (wheel / PageUp-PageDown scroll) and "Say:" + the typed text;
 * Enter sends, Escape closes. While typing, the game's own key handling sees a focused text field
 * (KeyInputDispatcher.IsTextInputFocused hooked to true), so no hotkey fires. Right-click + drag moves it. */
#define CH_HIST 100
#define CH_IDLE 5
#define CH_BOX_LINES 9
static struct { char text[300]; double t; } chist[CH_HIST]; static int nch;
static char chat_buf[DSNET_CHAT_MAX + 1];
static uintptr_t h_ch_view, h_ch_root, h_ch_box, h_ch_idle[CH_IDLE], h_ch_idlebg[CH_IDLE], h_ch_lines[CH_BOX_LINES], h_ch_input, h_ch_hint, h_ch_field, h_ch_typing, h_ch_typingbg;
static char typing_shown[200];                           /* test status: the "is typing" line now shown */
static void utf16_to_utf8(Obj s, char *out, size_t cap) {             /* typed text: keep accents etc. (str_utf8 is ASCII-only) */
    size_t n = 0; if (cap) out[0] = 0;
    if (!s || !cap) return;
    int32_t len = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    for (int32_t i = 0; i < len; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < len) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
        char b[4]; int k = u < 0x80 ? (b[0] = (char)u, 1) : u < 0x800 ? (b[0] = (char)(0xC0 | u >> 6), b[1] = (char)(0x80 | (u & 63)), 2)
              : u < 0x10000 ? (b[0] = (char)(0xE0 | u >> 12), b[1] = (char)(0x80 | (u >> 6 & 63)), b[2] = (char)(0x80 | (u & 63)), 3)
              : (b[0] = (char)(0xF0 | u >> 18), b[1] = (char)(0x80 | (u >> 12 & 63)), b[2] = (char)(0x80 | (u >> 6 & 63)), b[3] = (char)(0x80 | (u & 63)), 4);
        if (n + (size_t)k + 1 > cap) break;
        memcpy(out + n, b, (size_t)k); n += (size_t)k;
    }
    out[n] = 0;
}
static char test_chat_in[256]; static int test_chat_enter, test_chat_escape, test_chat_open;
static bool (*o_textfocus)(void *);
/* hotkeys off while typing, and while the lobby Rules panel is open (its Escape must not open the game's Esc menu) */
static int teams_modal(void) {                                             /* teams lane: Teams window / proposal summary / stock step open */
    static int (*f)(void); static int looked; if (!looked) { looked = 1; f = (int (*)(void))dlsym(RTLD_DEFAULT, "ui_teams_modal_open"); }
    return f && f();
}
static bool h_textfocus(void *mi) { return chat_typing || chat_close_frames > 0 || (M.state == MPM_LOBBY && M.lobby_settings_open) || rules_close_frames > 0 || teams_modal() || teams_close_frames > 0 || o_textfocus(mi); }
/* Escape for our in-game boxes (ui lane's InputEventHandler.OnEvent(EscapeActionRequested) hook asks first, dlsym):
 * 1 = one of ours took it (chat typing: closes without sending; lobby Rules: closed), so the game's Esc menu stays shut */
__attribute__((visibility("default"))) int mpmenu_escape_ours(void) {
    rig_log("mpmenu: Escape asked (chat %d, lobby rules %d, rules closing %d, teams %d, teams closing %d)", chat_typing,
            M.state == MPM_LOBBY && M.lobby_settings_open, rules_close_frames, teams_modal(), teams_close_frames);
    if (chat_typing) { test_chat_escape = 1; rig_log("mpmenu: Escape closed the chat"); return 1; }
    if (M.state == MPM_LOBBY && M.lobby_settings_open) { pending_click = MPB_RULES_DONE; rig_log("mpmenu: Escape closed the lobby Rules"); return 1; }
    if (rules_close_frames > 0) return 1;                                    /* the same Escape, seen by us first this frame */
    if (teams_modal() || teams_close_frames > 0) return 1;                  /* Teams closes itself on Escape (teams lane) */
    return 0;
}
/* panel cog (ui lane, dlsym): fade seconds, join / leave lines, chat on / off; saved in mp_menu.json */
__attribute__((visibility("default"))) void mpmenu_chat_config(int fade_s, int show_join, int enabled) {
    if (fade_s >= 3 && fade_s <= 120) chat_fade_s = fade_s; chat_show_join = show_join != 0; chat_on = enabled != 0;
    if (!chat_on) chat_typing = 0;
    prefs_save();
}
__attribute__((visibility("default"))) void mpmenu_chat_config_get(int *fade_s, int *show_join, int *enabled) {
    if (fade_s) *fade_s = chat_fade_s; if (show_join) *show_join = chat_show_join; if (enabled) *enabled = chat_on;
}
static double mono_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
static void chat_add(const char *rich) {
    if (nch == CH_HIST) { memmove(&chist[0], &chist[1], sizeof chist[0] * (CH_HIST - 1)); nch--; }
    snprintf(chist[nch].text, sizeof chist[nch].text, "%s", rich); chist[nch].t = mono_s(); nch++;
    chat_scroll = 0;
}
static void chat_esc(const char *in, char *out, size_t cap) {          /* no rich-text tags from players */
    size_t o = 0;
    for (; *in && o + 8 < cap; in++) { if (*in == '<') { memcpy(out + o, "<​", 4); o += 4; } else out[o++] = *in; }
    out[o] = 0;
}
static void chat_pull(dsnet_client *c) {
    dsnet_chat_msg m;
    while (dsnet_chat_poll(c, &m)) {
        char t[300], body[260]; chat_esc(m.text, body, sizeof body);
        const char *col = ui_player_color(m.player);
        if (m.kind == DSNET_CHAT_TEXT) snprintf(t, sizeof t, "<color=%s>%s:</color> %s", col, m.name[0] ? m.name : "Player", body);
        else {
            if (!chat_show_join || (m.kind == DSNET_CHAT_JOINED && m.player == dsnet_player_id(c))) continue;   /* not "you joined" */
            snprintf(t, sizeof t, "<i><color=#A69085>%s %s</color></i>", m.name[0] ? m.name : "A player",
                     m.kind == DSNET_CHAT_LEFT ? "left the game" : m.kind == DSNET_CHAT_RECONNECTED ? "reconnected" : "joined the game");
        }
        chat_add(t);
    }
}
static Obj chat_parent(void) {                            /* the HUD's root canvas (CampaignInfoView's root) */
    static uintptr_t hv; static void *kv;
    Obj v = find_view_x(&hv, &kv, "Refactor.UI", "CampaignInfoView", false);
    static void *mr; if (!mr) mr = mth("UnityEngine", "Transform", "get_root", 0);
    return v && mr ? MCALL(mr, Obj, Obj, const void *)(u_tr(v), mr) : NULL;
}
static void chat_build(Obj parent) {
    static uintptr_t hc; static void *kc;
    Obj cn = find_view_x(&hc, &kc, "Refactor.UI", "CommonNoticeView", true);
    Obj nt = cn ? tr_find(u_tr(cn), "Panel_Notice/Txt_Notice") : NULL, lb = cn ? tr_find(u_tr(cn), "Img_BlackLetterBox") : NULL;
    Obj tpl_txt = nt ? comp_of(nt, "TMPro", "TMP_Text") : NULL, tpl_img = lb ? image_of(lb) : NULL;
    if (!tpl_txt || !tpl_img) { rig_log("mpmenu: chat: templates not found"); return; }
    Obj root = mk_img(tpl_img, parent, -1, (Col){ 0, 0, 0, 0 }, 0, 0, 10, 10);
    if (!root) return;
    u_gc_free(h_ch_root); h_ch_root = u_gc_new(root); u_gc_free(h_ch_view); h_ch_view = u_gc_new(parent);
    Obj R = u_tr(root);
    for (int i = 0; i < CH_IDLE; i++) {                                    /* idle lines: a soft plate + the text */
        Obj bg = mk_img(tpl_img, R, -1, (Col){ 0.133f, 0.125f, 0.145f, 0.7f }, 0, 0, 430, 26);
        Obj t = mk_txt(tpl_txt, R, 0, 16, 513, 0, 0, 0, 410, 26, C_MAIN);
        u_gc_free(h_ch_idlebg[i]); h_ch_idlebg[i] = bg ? u_gc_new(bg) : 0; u_gc_free(h_ch_idle[i]); h_ch_idle[i] = t ? u_gc_new(t) : 0;
    }
    { Obj bg = mk_img(tpl_img, R, -1, (Col){ 0.133f, 0.125f, 0.145f, 0.7f }, 0, 0, 430, 22);      /* "<Name> is typing..." */
      Obj t = mk_txt(tpl_txt, R, 0, 14, 513, 0, 0, 0, 410, 22, C_SUB);
      u_gc_free(h_ch_typingbg); h_ch_typingbg = bg ? u_gc_new(bg) : 0; u_gc_free(h_ch_typing); h_ch_typing = t ? u_gc_new(t) : 0;
      u_active(bg, false); u_active(t, false); }
    Obj box = mk_img(tpl_img, R, S_BOX, C_WHITE, 0, 0, 470, 270);
    if (box) {
        Obj B = u_tr(box);
        mk_img(tpl_img, B, -1, C_BAND, 0, 135 - 16, 466, 30);
        mk_img(tpl_img, B, -1, C_LINE, 0, 135 - 31, 466, 1);
        Obj ti = mk_txt(tpl_txt, B, 1, 15, 513, 0, -235 + 12 + 60, 135 - 16, 120, 26, C_MAIN); if (ti) tmp_set(ti, "Chat");
        Obj hi = mk_txt(tpl_txt, B, 0, 11, 516, 0, 235 - 12 - 120, 135 - 16, 240, 22, C_SUB);
        if (hi) tmp_set(hi, "Right-click and drag to move");
        u_gc_free(h_ch_hint); h_ch_hint = hi ? u_gc_new(hi) : 0;
        for (int i = 0; i < CH_BOX_LINES; i++) {
            Obj l = mk_txt(tpl_txt, B, 0, 15, 513, 0, 0, 135 - 40 - 10 - 20 * i, 446, 20, C_MAIN);
            u_gc_free(h_ch_lines[i]); h_ch_lines[i] = l ? u_gc_new(l) : 0;
        }
        mk_img(tpl_img, B, -1, C_LINE, 0, -135 + 46, 466, 1);
        Obj say = mk_txt(tpl_txt, B, 0, 14, 513, 0, -235 + 12 + 20, -135 + 23, 40, 24, C_GOLD); if (say) tmp_set(say, "Say:");
        mk_img(tpl_img, B, -1, C_LINE, 22, -135 + 23, 396, 30);
        mk_img(tpl_img, B, -1, (Col){ 0.106f, 0.098f, 0.122f, 1 }, 22, -135 + 23, 394, 28);
        Obj in = mk_txt(tpl_txt, B, 0, 15, 513, 0, 22, -135 + 23, 380, 26, C_MAIN);
        u_gc_free(h_ch_input); h_ch_input = in ? u_gc_new(in) : 0;
        /* the real text field (the game's own input handling: any keyboard layout, accents): the Rename popup's field */
        Obj rp = popup_prefab(19), fs = rp ? tr_find(u_tr(rp), "Input_SaveFileName") : NULL, fsrc = fs ? comp_of(fs, "TMPro", "TMP_InputField") : NULL;
        Obj f = fsrc ? u_instantiate(fsrc, B) : NULL;
        if (u_alive(f)) {
            u_active(f, true); place(f, 22, -135 + 23, 394, 28);
            Obj fim = image_of(f); if (fim) img_set(fim, NULL, (Col){ 0, 0, 0, 0 });   /* our own field plate shows */
            if (mm_is(fim, K_GR)) F(0x5260348, void (*)(Obj, bool, void *))(fim, true, NULL);
            F(0x4EFC2D8, void (*)(Obj, int, void *))(f, DSNET_CHAT_MAX, NULL);                 /* characterLimit */
            static void *mtc; if (!mtc) mtc = mth("TMPro", "TMP_InputField", "get_textComponent", 0);
            Obj tc = mtc ? MCALL(mtc, Obj, Obj, const void *)(f, mtc) : NULL;
            if (u_alive(tc)) { txt_body(tc); txt_style(tc, 0, 15, 513, 0); txt_color(tc, C_MAIN); }
            Obj ph = F(0x4EFB9CC, Obj (*)(Obj, void *))(f, NULL); if (u_alive(ph) && strstr(cls_name(ph), "Text")) tmp_set(ph, "");
            F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(""), NULL);
            u_active(in, false);                                            /* the field draws the text and the caret */
        }
        u_gc_free(h_ch_field); h_ch_field = u_alive(f) ? u_gc_new(f) : 0;
        rig_log("mpmenu: chat text field %s", u_alive(f) ? "= the Rename popup's TMP_InputField" : "not found (typed keys only in tests)");
        u_active(box, false);
    }
    u_gc_free(h_ch_box); h_ch_box = box ? u_gc_new(box) : 0;
    rig_log("mpmenu: chat built");
}
static int dbg_istr_n, dbg_enter_n, dbg_a_n;
/* give the chat field the keyboard: the box must be active first (ActivateInputField ignores an inactive field: owner
 * lan_v8 "Enter opens the box but I can never type"), then select it in the EventSystem and activate it */
static void chat_focus(void) {
    Obj f = u_gc_get(h_ch_field), bx = u_gc_get(h_ch_box);
    if (!u_alive(f) || !bx) return;
    u_active(bx, true);
    static void *mfoc, *mcur, *mset, *ma;
    if (!mfoc) { mfoc = mth("TMPro", "TMP_InputField", "get_isFocused", 0); mcur = mth("UnityEngine.EventSystems", "EventSystem", "get_current", 0);
                 mset = mth("UnityEngine.EventSystems", "EventSystem", "SetSelectedGameObject", 1); ma = mth("TMPro", "TMP_InputField", "ActivateInputField", 0); }
    if (mfoc && MCALL(mfoc, bool, Obj, const void *)(f, mfoc)) return;     /* already has it */
    if (AT(f, 0x261, bool)) return;                                         /* m_ShouldActivateNextUpdate: activates in its LateUpdate */
    Obj es = mcur ? MCALL(mcur, Obj, const void *)(mcur) : NULL;
    if (u_alive(es) && mset) MCALL(mset, void, Obj, Obj, const void *)(es, u_go(f), mset);
    if (ma) MCALL(ma, void, Obj, const void *)(f, ma);
}
static void chat_frame(const mpm_obs *o) {
    mp_session *ss = mp_game_session(); dsnet_client *c = ss ? mp_client(ss) : NULL;
    if (chat_close_frames > 0) chat_close_frames--;
    if (!c) { chat_typing = 0; Obj r = u_gc_get(h_ch_root); if (u_alive(r)) u_active(r, false); return; }
    chat_pull(c);
    if (o->scene != 2 || !o->campaign_ready) return;
    Obj parent = chat_parent(); if (!parent) return;
    if (u_gc_get(h_ch_view) != parent || !u_alive(u_gc_get(h_ch_root))) {
        chat_build(parent);
        Obj f = u_gc_get(h_ch_field), bx = u_gc_get(h_ch_box);              /* rebuilt while typing (resync, reload): keep the draft open */
        if (chat_typing && u_alive(f) && bx) { u_active(bx, true); F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(chat_buf), NULL); chat_focus(); }
    }
    Obj root = u_gc_get(h_ch_root); if (!u_alive(root)) return;
    u_active(root, chat_on != 0);
    if (!chat_on) return;
    /* keys (icalls: Input.GetKeyDownInt / get_inputString; tests feed the same paths) */
    static bool (*kd)(int); static Obj (*istr)(void); static bool (*mb)(int); static void (*mpos)(V3 *); static void (*wheel)(V2 *); static int looked;
    if (!looked) { looked = 1; void *(*ri)(const char *) = F(0x6F8B90, void *(*)(const char *));
        kd = (bool (*)(int))ri("UnityEngine.Input::GetKeyDownInt(UnityEngine.KeyCode)"); istr = (Obj (*)(void))ri("UnityEngine.Input::get_inputString()");
        mb = (bool (*)(int))ri("UnityEngine.Input::GetMouseButton(System.Int32)"); mpos = (void (*)(V3 *))ri("UnityEngine.Input::get_mousePosition_Injected(UnityEngine.Vector3&)");
        wheel = (void (*)(V2 *))ri("UnityEngine.Input::get_mouseScrollDelta_Injected(UnityEngine.Vector2&)");
        rig_log("mpmenu: chat keys %s, text %s, mouse %s", kd ? "ok" : "MISSING", istr ? "ok" : "MISSING", mb && mpos ? "ok" : "MISSING"); }
    { char d[64] = ""; if (istr) str_utf8(istr(), d, sizeof d); dbg_istr_n += (int)strlen(d); if (kd && kd(13)) dbg_enter_n++; if (kd && kd(97)) dbg_a_n++; }   /* test status: what Unity's input saw */
    int enter = (kd && (kd(13) || kd(271))) || test_chat_enter, esc = (kd && kd(27)) || test_chat_escape;
    test_chat_enter = test_chat_escape = 0;
    if (!chat_typing) {
        int other_text = o_textfocus && o_textfocus(NULL);                /* the game's own text field has the keys */
        if ((enter || test_chat_open) && !o->esc_open && !other_text) {
            chat_typing = 1; chat_buf[0] = 0; chat_scroll = 0; enter = 0;
            Obj f = u_gc_get(h_ch_field);
            if (u_alive(f)) { F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(""), NULL); chat_focus(); }
        }
        test_chat_open = 0;
    } else {
        Obj f = u_gc_get(h_ch_field);
        if (!enter && !esc) chat_focus();                                   /* keeps the keyboard while typing (a click on the world took it) */
        if (u_alive(f)) {                                                  /* the field holds the text; tests write into it */
            if (test_chat_in[0]) { char cur[DSNET_CHAT_MAX + 1]; utf16_to_utf8(F(0x4EFAE84, Obj (*)(Obj, void *))(f, NULL), cur, sizeof cur);
                                   char nw[512]; snprintf(nw, sizeof nw, "%s%s", cur, test_chat_in); F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(nw), NULL); test_chat_in[0] = 0; }
            utf16_to_utf8(F(0x4EFAE84, Obj (*)(Obj, void *))(f, NULL), chat_buf, sizeof chat_buf);
        }
        { static char last[DSNET_CHAT_MAX + 1]; if (strcmp(last, chat_buf)) { snprintf(last, sizeof last, "%s", chat_buf); if (chat_buf[0]) dsnet_chat_typing(c, 1); } }   /* a key changed the line: "is typing" (dsnet sends at most 1/s) */
        char in[256] = ""; if (istr && !u_alive(f)) str_utf8(istr(), in, sizeof in);
        size_t L = strlen(in), T = strlen(test_chat_in); if (T && L + T < sizeof in) { memcpy(in + L, test_chat_in, T + 1); } test_chat_in[0] = 0;
        for (char *q = in; *q; q++) {
            size_t n = strlen(chat_buf);
            if (*q == '\b') { while (n && ((unsigned char)chat_buf[n - 1] & 0xC0) == 0x80) n--; if (n) n--; chat_buf[n] = 0; }
            else if (*q == '\n' || *q == '\r') enter = 1;
            else if ((unsigned char)*q >= 0x20 && n < DSNET_CHAT_MAX) { chat_buf[n] = *q; chat_buf[n + 1] = 0; }
        }
        if (enter || esc) {
            if (enter && chat_buf[0]) { int rc = dsnet_chat_send(c, chat_buf); rig_log("mpmenu: chat sent (%zu bytes, rc %d)", strlen(chat_buf), rc); }
            chat_typing = 0; chat_close_frames = 2;
            dsnet_chat_typing(c, 0);                                        /* sent or cancelled: the others drop the line */
            if (u_alive(f)) { F(0x4EFB000, void (*)(Obj, Obj, void *))(f, u_str(""), NULL);
                              static void *md; if (!md) { md = mth("TMPro", "TMP_InputField", "DeactivateInputField", 1); if (!md) md = mth("TMPro", "TMP_InputField", "DeactivateInputField", 0); }
                              if (md) MCALL(md, void, Obj, bool, const void *)(f, false, md); }
        }
        if (wheel) { V2 d = { 0, 0 }; wheel(&d); if (d.y > 0) chat_scroll++; else if (d.y < 0 && chat_scroll > 0) chat_scroll--; }
        if (kd && kd(280)) chat_scroll += 3; if (kd && kd(281)) chat_scroll = chat_scroll > 3 ? chat_scroll - 3 : 0;   /* PageUp / PageDown */
        int maxs = nch > CH_BOX_LINES ? nch - CH_BOX_LINES : 0; if (chat_scroll > maxs) chat_scroll = maxs;
    }
    /* place: bottom-left above the build bar, right of the resource list (canvas x 0..~105), plus the dragged offset.
     * The canvas is 1080 high; its width follows the screen (1663 at 1512x982, 1920 at 16:9). */
    static void *msw, *msh; if (!msw) { msw = mth("UnityEngine", "Screen", "get_width", 0); msh = mth("UnityEngine", "Screen", "get_height", 0); }
    int sw = msw ? MCALL(msw, int, const void *)(msw) : 0, shh = msh ? MCALL(msh, int, const void *)(msh) : 0;
    float hw = sw > 0 && shh > 0 ? 540.0f * sw / shh : 831;
    float bx = -hw + 130 + 235 + chat_x, by = -540 + 130 + 135 + chat_y;
    Obj box = u_gc_get(h_ch_box);
    if (box) {
        u_active(box, chat_typing);
        place(box, bx, by, 470, 270);
        if (chat_typing) {
            for (int i = 0; i < CH_BOX_LINES; i++) {                      /* newest at the bottom */
                int k = nch - CH_BOX_LINES - chat_scroll + i; Obj l = u_gc_get(h_ch_lines[i]);
                if (l) tmp_set(l, k >= 0 && k < nch ? chist[k].text : "");
            }
            if (!u_alive(u_gc_get(h_ch_field))) {                           /* no field: draw the text + a caret ourselves */
                char t[DSNET_CHAT_MAX + 64], e[DSNET_CHAT_MAX * 2 + 8]; chat_esc(chat_buf, e, sizeof e);
                snprintf(t, sizeof t, "%s%s", e, (frames / 15) % 2 ? "|" : " ");
                tmp_set(u_gc_get(h_ch_input), t);
            }
        }
    }
    /* "<Name> is typing..." for the others (owner 2026-10-14): one line in their colours; it takes the lowest idle slot
     * (the idle lines move up one) or sits above the open box */
    int me = dsnet_player_id(c), lift = 0;
    { char line[200] = ""; int k = 0, more = 0; size_t o = 0;
      for (int p = 0; p < DSNET_MAX_PLAYERS; p++) {
          if (p == me || !dsnet_chat_typing_of(c, p)) continue;
          const char *nm = dsnet_player_name(c, p); if (!nm) continue;
          if (k == 3) { more = 1; break; }
          o += (size_t)snprintf(line + o, o < sizeof line ? sizeof line - o : 0, "%s<color=%s>%s</color>", k ? ", " : "", ui_player_color(p), nm); k++;
      }
      if (k && o < sizeof line) snprintf(line + o, sizeof line - o, "%s", more ? " and others are typing..." : k > 1 ? " are typing..." : " is typing...");
      Obj t = u_gc_get(h_ch_typing), bg = u_gc_get(h_ch_typingbg);
      u_active(t, k > 0); u_active(bg, k > 0);
      snprintf(typing_shown, sizeof typing_shown, "%s", k ? line : "");
      if (k) {
          float y = chat_typing ? by + 135 + 14 : by - 135 + 11;
          place(t, bx - 235 + 10 + 205, y, 410, 22); place(bg, bx, y, 430, 22); tmp_set(t, line);
          if (!chat_typing) lift = 24;
      }
    }
    /* idle lines: the newest at the bottom, fading after chat_fade_s (1 s fade) */
    double now = mono_s();
    for (int i = 0; i < CH_IDLE; i++) {
        int k = nch - CH_IDLE + i; Obj t = u_gc_get(h_ch_idle[i]), bg = u_gc_get(h_ch_idlebg[i]);
        float a = 0;
        if (!chat_typing && k >= 0 && k < nch) { double age = now - chist[k].t; a = age < chat_fade_s ? 1 : age < chat_fade_s + 1 ? (float)(chat_fade_s + 1 - age) : 0; }
        u_active(t, a > 0); u_active(bg, a > 0);
        if (a > 0) {
            float y = by - 135 + 13 + lift + 30 * (CH_IDLE - 1 - i);
            place(t, bx - 235 + 10 + 205, y, 410, 26); place(bg, bx, y, 430, 26);
            tmp_set(t, chist[k].text);
            if (mm_is(t, K_GR)) F(0x52602F8, void (*)(Obj, Col, void *))(t, (Col){ C_MAIN.r, C_MAIN.g, C_MAIN.b, a }, NULL);
            if (mm_is(bg, K_GR)) F(0x52602F8, void (*)(Obj, Col, void *))(bg, (Col){ 0.133f, 0.125f, 0.145f, 0.7f * a }, NULL);
        }
    }
    /* right-click + drag on the box (or the idle lines): move; remembered */
    static int dragging; static V3 last;
    if (mb && mpos && box) {
        V3 mp = { 0, 0, 0 }; mpos(&mp);
        float r[4] = { 0 }; if (chat_typing) rect_of(box, r); else { Obj t0 = u_gc_get(h_ch_idlebg[CH_IDLE - 1]); if (u_on(t0)) rect_of(t0, r); }
        float sc = r[2] > r[0] ? (r[2] - r[0]) / (chat_typing ? 470 : 430) : 1;
        if (mb(1)) {
            if (!dragging && mp.x >= r[0] && mp.x <= r[2] && mp.y >= r[1] && mp.y <= r[3] && r[2] > r[0]) { dragging = 1; last = mp; }
            else if (dragging && sc > 0) { chat_x += (mp.x - last.x) / sc; chat_y += (mp.y - last.y) / sc; last = mp; }
        } else if (dragging) { dragging = 0; prefs_save(); rig_log("mpmenu: chat moved to %.0f,%.0f", chat_x, chat_y); }
    }
}

/* ---------------------------------------------------------------- actions */
static void run_actions(int a, const mpm_obs *o) {
    Obj tv = title_view();
    if (a & (MPA_OPEN_LOAD_LIST | MPA_OPEN_NEW)) host_failed = 0;
    if ((a & MPA_OPEN_LOAD_LIST) && tv) (mm_is(tv, K_TMV) ? F(0xE710B8, void (*)(Obj, void *))(tv, NULL) : (void)0);   /* TitleMenuView.OnLoadCampaignPressed */
    if ((a & MPA_OPEN_NEW) && tv) (mm_is(tv, K_TMV) ? F(0xE71020, void (*)(Obj, void *))(tv, NULL) : (void)0);        /* TitleMenuView.OnNewGamePressed */
    if ((a & MPA_HOST_START) && o->role == 1) rig_log("mpmenu: a host session already runs (mp.json?): using it");
    else if (a & MPA_HOST_START) {
        mp_steam_info in; mp_steam_state(&in);
        char gid[64] = ""; mpm_settings ws;
        if (campaign_guid(gid, sizeof gid) && world_get(gid, &ws)) { M.set = ws; rig_log("mpmenu: world %s: its stored settings", gid); }
        { char nm[64]; field_text(h_namefield, nm, sizeof nm); if (nm[0]) snprintf(my_name, sizeof my_name, "%s", nm); }   /* name typed on the Host page */
        mp_config c; mp_config_defaults(&c);
        char p[1200]; snprintf(p, sizeof p, "%s/mp.json", rig_dir);
        mp_config f; if (mp_config_load(p, &f) == 0) { snprintf(c.name, sizeof c.name, "%s", f.name); c.port = f.port; }
        c.host = 1; c.allow_speedup = M.set.speedup; c.allow_pause = M.set.pause_mode != MPM_PAUSE_NONE;
        c.pause_mode = M.set.pause_mode; c.gear_mode = M.set.gear; c.port = MPM_DEFAULT_PORT;
        if (my_name[0]) snprintf(c.name, sizeof c.name, "%s", my_name);
        else if (!in.steam) default_name(c.name, sizeof c.name);
        int rc = in.steam ? mp_steam_host("", &c)                           /* the campaign just loaded; TCP listens too */
                          : mp_game_host(&c);                               /* no Steam: direct TCP only */
        if (rc == -2 && !in.steam) { c.port = 0; rc = mp_game_host(&c); }   /* port taken: any free port */
        host_failed = rc != 0;
        if (!rc) { prefs_save(); if (gid[0]) world_put(gid, &M.set); lobby_hold(1); rules_send("host start"); }
        rig_log("mpmenu: host start (%s) rc %d (pause %d speed-up %d gear %d)", in.steam ? "Steam" : "TCP", rc, M.set.pause_mode, M.set.speedup, M.set.gear);
    }
    if (a & MPA_JOIN_ADDR) join_by_address();
    if ((a & MPA_JOIN_CANCEL) && conn) { dsnet_connect_free(conn); conn = NULL; rig_log("mpmenu: connect cancelled"); }
    if (a & MPA_START_GAME) lobby_hold(0);
    if (a & MPA_RULES) rules_send("lobby");
    if (a & MPA_ADMIN) {                                                    /* lobby window: host admin mode on / off */
        mp_session *s = mp_game_session(); dsnet_client *c = s ? mp_client(s) : NULL;
        uint8_t b = c && !dsnet_admin_mode(c);
        int rc = c ? dsnet_submit(c, DSNET_ADMIN_MODE, 0, &b, 1) : -1;
        M.set.admin = b; rules_sent_frame = frames;
        char gid[64] = ""; prefs_save(); if (campaign_guid(gid, sizeof gid)) world_put(gid, &M.set);
        rig_log("mpmenu: admin mode -> %d (rc %d)", b, rc);
    }
    if (a & MPA_COPY_ADDR) { copied_until = frames + 60; F(0x518AFE0, void (*)(Obj, void *))(u_str(M.direct), NULL); rig_log("mpmenu: copied %s", M.direct); }
    if (a & MPA_INVITE) { int rc = mp_steam_invite(); rig_log("mpmenu: invite rc %d", rc); }
    if (a & MPA_LEAVE) {                                                    /* guest: straight to the title (mp_game) */
        hist_update();                                                      /* Recent worlds: the state at leaving */
        static void (*leave_title)(void); static int looked;
        if (!looked) { looked = 1; leave_title = (void (*)(void))dlsym(RTLD_DEFAULT, "mp_game_leave_to_title"); }
        if (leave_title) leave_title(); else mp_steam_leave();
        host_failed = 0; rig_log("mpmenu: left the session");
    }
    if (a & MPA_FRIENDS) friends_refresh();
    if (a & MPA_HIST_JOIN) hist_join(M.hist_pick);
    if (a & MPA_HIST_DEL) hist_del(M.hist_pick);
    if (a & MPA_JOIN_FRIEND) {
        int k = M.friend_pick, rc = k >= 0 && k < M.nfriends ? mp_steam_join(friend_lobby[k]) : -1;
        if (!rc) { snprintf(joined_how, sizeof joined_how, "steam"); joined_steamid = friend_id[k]; }
        if (rc) snprintf(M.msg, sizeof M.msg, "Could not join %s's world (Steam). Try again, or use Join by address.", k >= 0 && k < 6 ? M.friends[k] : "a friend");
        rig_log("mpmenu: join friend %d (lobby %llu): rc %d", k, (unsigned long long)(k >= 0 && k < 6 ? friend_lobby[k] : 0), rc);
    }
    if (a & MPA_LOBBY_SHOW) { popup_activate(1); rig_log("mpmenu: lobby panel opened"); }
    if (a & MPA_LOBBY_HIDE) {
        Obj ev = esc_view(); Obj back = ev ? AT(ev, 0x48, Obj) : NULL;
        if (ev && u_on(ev)) (mm_is(ev, K_SMV) ? F(0xF2ED1C, void (*)(Obj, void *))(ev, NULL) : (void)0);  /* SystemMenuView.OnReturnToGamePressed */
        (void)back; esc_lobby_mode = -1;
    }
    (void)o;
}

/* every element of ours with a tooltip, topmost first (research/tooltips_v9.md) */
static void tips_frame(const mpm_obs *o) {
    game_tip_frame();                                                        /* auto-hide (harmless if ui_frame also calls it) */
    tip_hit = 0; tip_now[0] = 0;
    if (!test_hover[0] && !tip_ready()) return;
    #define TIP(obj, t, b) tip_try((obj), (t), (b))
    /* chat box (3.13): no tooltip. The box is open only while its field has the focus, and a tooltip there covered the
       text being typed (lead 2026-10-14); its header already says "Right-click and drag to move". */
    /* Rules panels (3.3) */
    rpanel *rps[2] = { &rp_lobby, &rp_title };
    for (int k = 0; k < 2; k++) if (u_on_tree(u_gc_get(rps[k]->root))) {
        rp_tooltip(rps[k]);
        TIP(k ? tb(MPB_RULES_DONE) : u_gc_get(h_lwr[MPB_RULES_DONE]), "Done", "Closes the Rules.");
    }
    int lobby_rules = M.state == MPM_LOBBY && M.lobby_settings_open, host = M.state == MPM_LOBBY;
    /* lobby window (3.5, 3.7) */
    if (M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY) {
        TIP(u_gc_get(h_lwb[LW_START]), "Start", "Starts the game for everyone in the lobby. Players who join later catch up with the running game.");
        TIP(u_gc_get(h_lwb[LW_LEAVE]), "Leave Multiplayer", "Host: closes the world for everyone, and the world stays saved on your computer. Guest: you leave and return to the title; your units stay reserved for you.");
        if (!lobby_rules) {
            TIP(u_gc_get(h_lwb[LW_GEAR]), "Rules", "Shows how this world plays. Only the host can change them.");
            TIP(lwt(T_RULES), "Rules", "Shows how this world plays. Only the host can change them.");
            TIP(u_gc_get(h_lwb[LW_INVITE]), "Steam Invite", "Opens Steam's invite window so you can invite friends without sharing your address. Needs the Steam version.");
            TIP(u_gc_get(h_lwb[LW_COPY]), "Copy Address", "Copies your address (ip:port) so you can send it to a friend. They use Join by Address.");
            TIP(u_gc_get(h_lw_addrbox[0]), "Copy Address", "Copies your address (ip:port) so you can send it to a friend. They use Join by Address.");
            TIP(lwt(T_PLAYERS), "Players", "Everyone in this world. The bars show each player's connection.");
            for (int i = 0; i < LW_ROWS; i++) {
                static const char *lv[4] = { "Disconnected. The game waits, or the host keeps playing their units.", "Poor connection, or loading, or resyncing.",
                                             "Slow connection, or catching up with the game.", "Good connection. In sync." };
                if (lw_level[i] >= 0 && lw_level[i] <= 3) for (int k = 0; k < 3; k++) TIP(u_gc_get(h_lw_bar[i][k]), lw_rowname[i], lv[lw_level[i]]);
                if (lw_rowname[i][0]) TIP(u_gc_get(h_lw_rowbg[i]), lw_rowname[i], "The colour is this player's colour everywhere: names above units, portraits, Teams and chat.");
            }
        }
    }
    (void)host;
    /* Esc menu (3.14) */
    Obj ev = o->scene == 2 ? esc_view() : NULL;
    if (ev && u_on(ev) && M.state != MPM_LOBBY && M.state != MPM_GUEST_LOBBY) {
        TIP(eb(MPB_ESC_INVITE), "Steam Invite", "Opens Steam's invite window so you can invite friends into this world.");
        TIP(eb(MPB_LEAVE), "Leave Multiplayer", "Host: closes the world for everyone; it stays saved on your computer. Guest: you return to the title; your units stay reserved for you.");
        if (in_session()) TIP(AT(ev, 0x60, Obj), "Load Campaign", "Leave multiplayer to load another world.");
    }
    if (o->scene != 1) return;
    /* Recent worlds (3.1) */
    for (int i = 0; i < 6; i++) {
        TIP(tb(MPB_HISTX0 + i), "Remove from List", "Removes this world from Recent Worlds. The world itself and its save are not deleted.");
        TIP(u_gc_get(h_rw_ul[i]), "Your Units", "The units you controlled in this world when you last left. They stay reserved for you until you come back.");
        TIP(tb(MPB_HIST0 + i), "Reconnect", "Joins this world again the same way as last time (you hosted it, Steam, or the IP address). Your units come back to you.");
    }
    TIP(tb(MPB_HIST_PREV), "More Worlds", "Shows the next page of Recent Worlds."); TIP(tb(MPB_HIST_NEXT), "More Worlds", "Shows the next page of Recent Worlds.");
    TIP(u_gc_get(h_rw_title), "Recent Worlds", "Worlds you played in multiplayer, newest first, with the day, the players and your units.");
    /* title column (3.1, 3.2, 3.4) */
    TIP(tb(MPB_MULTIPLAYER), "Multiplayer", "Play one world together with friends. Every player runs the game; only the orders travel, so all screens stay the same.");
    TIP(tb(MPB_HOST), "Host a Game", "Open one of your worlds, or a new campaign, to other players. The world is saved on your computer.");
    TIP(tb(MPB_JOIN_ADDR), "Join by Address", "Join a friend's world with their address (ip:port). Use it when Steam is not available, or on the same home network.");
    TIP(tb(MPB_JOIN_FRIEND), "Join a Friend on Steam", "Lists the Steam friends who are hosting a world right now. Click one to join. Needs the Steam version of the game.");
    for (int i = 0; i < 6; i++) { char l[160], t[64]; mpm_label(&M, MPB_FRIEND0 + i, l, sizeof l); const char *q = strstr(l, "Join ");
        snprintf(t, sizeof t, "%s", q && strstr(q, "'s") ? q : "Join a Friend's World"); { char *z = strchr(t, '<'); if (z) *z = 0; }
        if (t[0] >= 'a' && t[0] <= 'z') t[0] -= 32;
        TIP(tb(MPB_FRIEND0 + i), t, "Joins this friend's world through Steam. The world loads from the host; your own saves are not touched."); }
    TIP(tb(MPB_SETTINGS), "Rules", "How this multiplayer world plays: pause, speed, gear, units and events. Saved with the world; new worlds start with your last rules.");
    TIP(tb(MPB_LOAD_SAVE), "Host from a Save", "Pick one of your saves to open to other players. The world stays on your computer, and only you save it.");
    TIP(tb(MPB_NEW_CAMPAIGN), "Host a New Campaign", "Start a new campaign and open it to other players as soon as it is created.");
    TIP(u_gc_get(h_namefield), "Your Name", "The name other players see: in the player list, above your units and in chat.");
    TIP(u_gc_get(h_field), "Host Address", "The address the host shares from their lobby, for example 192.168.1.20:47777. Outside your home network, the host must open the port on their router.");
    TIP(tb(MPB_CONNECT), "Connect", "Connects to the host and loads their world. It stops trying after 8 seconds without an answer.");
    TIP(tb(MPB_CANCEL_JOIN), "Cancel", "Stops joining and returns to the title.");
    TIP(tb(MPB_BACK), "Back", "Returns to the previous menu.");
    #undef TIP
}
void mpmenu_frame(void) {
    frames++;
    { static int was; int t = teams_modal(); if (was && !t) teams_close_frames = 3; else if (teams_close_frames > 0) teams_close_frames--; was = t; }   /* the Escape that closed Teams stays ours */
    if (!mf) return;
    join_poll();
    assets_scan();                                                          /* every 300 frames until all are found */
    mpm_obs o; observe(&o);
    o.edited = fields_edited; fields_edited = 0;
    int c = pending_click; pending_click = MPB_NONE;
    int st0 = M.state;
    int a = mpm_step(&M, &o, c);
    if (M.state != st0) rig_log("mpmenu: state %d -> %d (click %d)", st0, M.state, c);
    if (o.failed && o.msg[0] && (M.state == MPM_FAILED || M.state == MPM_IDLE)) mp_steam_ack();   /* the message is in M.msg now */
    if (a & ~MPA_REFRESH) run_actions(a, &o);
    int lobby = M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY;
    if (lobby && o.campaign_ready && !o.esc_open && !(a & MPA_LOBBY_SHOW) && frames % 30 == 0) {
        /* the lobby stays until Start / Leave, but never over another window: a stock popup (story, level-up...) or the
         * ui lane's own boxes (player disconnected: Keep playing / Wait) */
        static int (*ui_modal)(void); static int looked;
        if (!looked) { looked = 1; ui_modal = (int (*)(void))dlsym(RTLD_DEFAULT, "ui_modal_open"); }
        Obj p = popups();
        int busy_ui = (p && !strcmp(cls_name(p), "PopUpViewContainer") && F(0xF62338, bool (*)(Obj, void *))(p, NULL)) || (ui_modal && ui_modal());
        if (!busy_ui) popup_activate(1);
    }
    if (lobby && o.esc_open && frames % 5 == 0) {                          /* a ui-lane box came up: the lobby steps aside */
        static int (*ui_modal2)(void); static int looked2;
        if (!looked2) { looked2 = 1; ui_modal2 = (int (*)(void))dlsym(RTLD_DEFAULT, "ui_modal_open"); }
        Obj ev = esc_view();
        if (ui_modal2 && ui_modal2() && ev) { (mm_is(ev, K_SMV) ? F(0xF2ED1C, void (*)(Obj, void *))(ev, NULL) : (void)0); rig_log("mpmenu: lobby closed for a ui box; it comes back after"); }
    }
    if (M.state == MPM_LOBBY && frames % 30 == 0) direct_refresh();
    rules_sync(&o);
    if (o.role && o.campaign_ready && !o.loading && frames % 300 == 150) hist_update();   /* Recent worlds: the session now */
    if (!hist_loaded && o.scene == 1) { hist_load(); M.nhist = nhist; }
    if (M.state == MPM_TITLE_FRIENDS && frames % 60 == 0) friends_refresh();
    title_sync(&o);
    esc_sync(&o);
    notice_frame(&o);
    pick_frame(&o);
    chat_frame(&o);
    tips_frame(&o);
    if (!o.role && !conn && !mp_game_session() && joined_how[0] && M.state != MPM_GUEST_JOINING && M.state != MPM_GUEST_LOADING) { joined_how[0] = 0; joined_steamid = 0; }   /* session over */
}

int mpmenu_gear_mode(void) { return mp_game_session() ? M.set.gear : -1; }
/* ui lane (ux #30): the player panel hides while the host's lobby or the guest's waiting panel is up */
__attribute__((visibility("default"))) int mpmenu_lobby_open(void) { return M.state == MPM_LOBBY || M.state == MPM_GUEST_LOBBY; }

void mpmenu_install(void) {
    mpm_init(&M);
    prefs_load();
    int bad = rig_hook(0xF2DC30, (void *)h_esc_press, (void **)&o_esc_press, "mpmenu SubUI_SystemMenuButton.OnPressButton");
    bad |= rig_hook(0xE6FF20, (void *)h_title_click, (void **)&o_title_click, "mpmenu SubUI_TitleMenuButton.OnPointerClick");
    bad |= rig_hook(0xF2EEA8, (void *)h_esc_load, (void **)&o_esc_load, "mpmenu SystemMenuView.OnLoadCampaignPressed (blocked in MP)");
    bad |= rig_hook(0xD6C800, (void *)h_quickload, (void **)&o_quickload, "mpmenu OnEvent(QuickLoadRequested) (blocked in MP)");
    bad |= rig_hook(0xF70150, (void *)h_slot_set, (void **)&o_slot_set, "mpmenu SubUI_SaveFileButton.SetData (MP tag)");
    bad |= rig_hook(0xD42BBC, (void *)h_textfocus, (void **)&o_textfocus, "mpmenu KeyInputDispatcher.IsTextInputFocused (chat typing: no hotkeys)");
    if (test_mode()) {
        bad |= rig_hook(0x518AEA0, (void *)h_clip_get, (void **)&o_clip_get, "mpmenu GUIUtility.get_systemCopyBuffer (test: file)");
        bad |= rig_hook(0x518AFE0, (void *)h_clip_set, (void **)&o_clip_set, "mpmenu GUIUtility.set_systemCopyBuffer (test: file)");
    }
    rig_log("mpmenu: installed (%s)%s", bad ? "HOOK FAILED" : "ok", test_mode() ? ", clipboard -> clipboard.txt" : "");
}

/* ---------------------------------------------------------------- socket commands (tests: read-back + simulated clicks) */
static const char *state_name(int s) {
    static const char *n[] = { "idle", "title_menu", "title_host", "pick_save", "pick_new", "host_loading", "lobby", "playing",
                               "guest_joining", "guest_loading", "failed", "title_join", "guest_lobby", "title_friends" };
    return s >= 0 && s <= MPM_TITLE_FRIENDS ? n[s] : "?";
}
static void put_btns(Buf *b, const char *key, const int *ids, int n, uintptr_t *hs, int txt_off) {
    bput(b, ",\"%s\":{", key);
    for (int i = 0; i < n; i++) {
        Obj x = u_gc_get(hs[ids[i]]); char t[200] = "";
        if (u_alive(x)) str_utf8(tmp_get(AT(x, txt_off, Obj)), t, sizeof t);
        bput(b, "%s\"%d\":{\"exists\":%s,\"active\":%s,\"text\":", i ? "," : "", ids[i], u_alive(x) ? "true" : "false", u_on(x) ? "true" : "false");
        bstr(b, t); bput(b, "}");
    }
    bput(b, "}");
}
/* ---------------------------------------------------------------- layout read-back (tests) */
static int rect_of(Obj comp, float r[4]) {                                 /* screen-space x0,y0,x1,y1 (overlay canvas: world = pixels) */
    if (!u_alive(comp)) return 0;
    Obj arr = F(0x6F8BA8, Obj (*)(void *, size_t))(md(0x6656B28), 4);       /* new Vector3[4] */
    (mm_is(u_tr(comp), K_RT) ? F(0x51089D0, void (*)(Obj, Obj, void *))(u_tr(comp), arr, NULL) : (void)0);      /* RectTransform.GetWorldCorners */
    V3 *c = (V3 *)((char *)arr + 0x20);
    r[0] = r[2] = c[0].x; r[1] = r[3] = c[0].y;
    for (int i = 1; i < 4; i++) { if (c[i].x < r[0]) r[0] = c[i].x; if (c[i].x > r[2]) r[2] = c[i].x; if (c[i].y < r[1]) r[1] = c[i].y; if (c[i].y > r[3]) r[3] = c[i].y; }
    return 1;
}
/* ---- test input through AppKit (the same NSEvent path a real keyboard / mouse takes into Unity) */
static id ns_game_window(void) {
    id app = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    id wins = ((id (*)(id, SEL))objc_msgSend)(app, sel_registerName("windows"));
    unsigned long n = ((unsigned long (*)(id, SEL))objc_msgSend)(wins, sel_registerName("count")); id best = nil; double ba = 0;
    for (unsigned long i = 0; i < n; i++) {
        id w = ((id (*)(id, SEL, unsigned long))objc_msgSend)(wins, sel_registerName("objectAtIndex:"), i);
        CGRect f = ((CGRect (*)(id, SEL))objc_msgSend)(w, sel_registerName("frame"));
        if (f.size.width * f.size.height > ba) { ba = f.size.width * f.size.height; best = w; }
    }
    return best;
}
static void ns_deliver(id ev, const char *via) {
    if (!ev) return;
    if (!strcmp(via, "app")) {
        id app = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
        ((void (*)(id, SEL, id, BOOL))objc_msgSend)(app, sel_registerName("postEvent:atStart:"), ev, NO);
    } else { id w = ns_game_window(); if (w) ((void (*)(id, SEL, id))objc_msgSend)(w, sel_registerName("sendEvent:"), ev); }
}
static id ns_str(const char *s) { return ((id (*)(Class, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), s); }
static int mac_keycode(char c) {                                             /* US layout virtual key codes */
    static const struct { char c; unsigned char k; } t[] = {
        {'a',0},{'s',1},{'d',2},{'f',3},{'h',4},{'g',5},{'z',6},{'x',7},{'c',8},{'v',9},{'b',11},{'q',12},{'w',13},{'e',14},{'r',15},
        {'y',16},{'t',17},{'1',18},{'2',19},{'3',20},{'4',21},{'6',22},{'5',23},{'=',24},{'9',25},{'7',26},{'-',27},{'8',28},{'0',29},
        {']',30},{'o',31},{'u',32},{'[',33},{'i',34},{'p',35},{'l',37},{'j',38},{'\'',39},{'k',40},{';',41},{'\\',42},{',',43},{'/',44},
        {'n',45},{'m',46},{'.',47},{' ',49} };
    if (c >= 'A' && c <= 'Z') c += 32;
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) if (t[i].c == c) return t[i].k;
    return 49;
}
/* Characters: macOS hands typed text (insertText) only to the active app, which a hidden test copy never is; key codes
 * do reach Unity (GetKeyDown). So the test builds Unity Events (Event.KeyboardEvent) and gives them to the field's own
 * TMP_InputField.ProcessEvent: the handler OnUpdateSelected runs for every key event of the selected field. */
static int tmp_keys(const char *chars, const char *special) {
    Obj f = u_gc_get(h_ch_field); if (!u_alive(f)) return 0;
    static void *mke, *mpe; if (!mke) { mke = mth("UnityEngine", "Event", "KeyboardEvent", 1); mpe = mth("TMPro", "TMP_InputField", "ProcessEvent", 1); }
    if (!mke || !mpe) { rig_log("mpmenu: test keys: Event.KeyboardEvent %p / ProcessEvent %p missing", mke, mpe); return 0; }
    int n = 0, len = chars ? (int)strlen(chars) : 1;
    for (int i = 0; i < len; i++) {
        char one[16]; if (chars) { one[0] = chars[i]; one[1] = 0; if (chars[i] == ' ') snprintf(one, sizeof one, "space"); } else snprintf(one, sizeof one, "%s", special);
        Obj ev = MCALL(mke, Obj, Obj, const void *)(u_str(one), mke);
        if (!ev) continue;
        MCALL(mpe, void, Obj, Obj, const void *)(f, ev, mpe); n++;
    }
    return n;
}
/* chars: type each character (key down + up); else one special key (code). Returns events sent. */
static int key_inject(const char *chars, int code, const char *via) {
    id w = ns_game_window(); if (!w) return 0;
    long wn = ((long (*)(id, SEL))objc_msgSend)(w, sel_registerName("windowNumber"));
    id pi = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSProcessInfo"), sel_registerName("processInfo"));
    double ts = ((double (*)(id, SEL))objc_msgSend)(pi, sel_registerName("systemUptime"));
    int n = 0, len = chars ? (int)strlen(chars) : 1;
    for (int i = 0; i < len; i++) {
        char one[8]; int kc;
        if (chars) { one[0] = chars[i]; one[1] = 0; kc = mac_keycode(chars[i]); }
        else { kc = code; if (kc < 0) return 0; one[0] = kc == 36 ? '\r' : kc == 53 ? 0x1b : kc == 51 ? 0x7f : 0; one[1] = 0; }
        for (int up = 0; up < 2; up++) {
            id ev = ((id (*)(Class, SEL, unsigned long, CGPoint, unsigned long, double, long, id, id, id, BOOL, unsigned short))objc_msgSend)(
                objc_getClass("NSEvent"), sel_registerName("keyEventWithType:location:modifierFlags:timestamp:windowNumber:context:characters:charactersIgnoringModifiers:isARepeat:keyCode:"),
                up ? 11 : 10, CGPointZero, 0, ts, wn, nil, ns_str(one), ns_str(one), NO, (unsigned short)kc);
            ns_deliver(ev, via); n++;
        }
    }
    return n;
}
static int click_field(const char *via) {                                    /* left click in the middle of the chat text field */
    Obj f = u_gc_get(h_ch_field); float r[4] = { 0 }; if (!u_on(f) || !rect_of(f, r)) return 0;
    id w = ns_game_window(); if (!w) return 0;
    long wn = ((long (*)(id, SEL))objc_msgSend)(w, sel_registerName("windowNumber"));
    double sc = ((double (*)(id, SEL))objc_msgSend)(w, sel_registerName("backingScaleFactor")); if (sc <= 0) sc = 1;
    CGPoint pt = { (r[0] + r[2]) / 2 / sc, (r[1] + r[3]) / 2 / sc };
    id pi = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSProcessInfo"), sel_registerName("processInfo"));
    double ts = ((double (*)(id, SEL))objc_msgSend)(pi, sel_registerName("systemUptime"));
    for (int up = 0; up < 2; up++) {
        id ev = ((id (*)(Class, SEL, unsigned long, CGPoint, unsigned long, double, long, id, long, long, float))objc_msgSend)(
            objc_getClass("NSEvent"), sel_registerName("mouseEventWithType:location:modifierFlags:timestamp:windowNumber:context:eventNumber:clickCount:pressure:"),
            up ? 2 : 1, pt, 0, ts, wn, nil, 0, 1, up ? 0.0f : 1.0f);
        ns_deliver(ev, via);
    }
    rig_log("mpmenu: test click at %.0f,%.0f (points, scale %.1f)", pt.x, pt.y, sc);
    return 2;
}
static void tree(Buf *b, Obj tr, int depth, int *n) {
    if (!u_alive(tr) || *n > 200) return;
    char nm[96] = ""; str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(tr, NULL), nm, sizeof nm);
    float r[4] = {0}; rect_of(tr, r);
    bput(b, "%s{\"d\":%d,\"name\":", (*n)++ ? "," : "", depth); bstr(b, nm);
    bput(b, ",\"on\":%d,\"rect\":[%.0f,%.0f,%.0f,%.0f]}", u_go_active_self(u_go(tr)), r[0], r[1], r[2], r[3]);
    if (depth >= 4) return;
    int k = (mm_is(tr, K_TR) ? F(0x510EAE4, int (*)(Obj, void *))(tr, NULL) : 0);
    for (int i = 0; i < k; i++) tree(b, (mm_is(tr, K_TR) ? F(0x510FC60, Obj (*)(Obj, int, void *))(tr, i, NULL) : NULL), depth + 1, n);
}

/* UI screenshot (tests): screen-space overlay canvases are switched to camera mode on Camera.main for one render into
 * a screen-sized RenderTexture, then switched back. Same pixels as the overlay layout (camera rect = screen size). */
static char *png_out(Obj tex, const char *path, int w, int h, const char *how) {
    Obj png = F(0x51657D8, Obj (*)(Obj, void *))(tex, NULL);
    F(0x50FC8F8, void (*)(Obj, void *))(tex, NULL);
    if (!png) return strdup("{\"ok\":false,\"error\":\"EncodeToPNG null\"}");
    size_t nb = (size_t)AT(png, 0x18, uintptr_t);
    FILE *f = fopen(path, "wb"); if (!f) return strdup("{\"ok\":false,\"error\":\"cannot write\"}");
    fwrite((char *)png + 0x20, 1, nb, f); fclose(f);
    Buf b = {0}; bput(&b, "{\"ok\":true,\"how\":\"%s\",\"w\":%d,\"h\":%d,\"bytes\":%zu}", how, w, h, nb); return b.p;
}
/* the window's back buffer (overlay UI included): RenderTexture.active = null, ReadPixels */
static char *backbuffer_shot(const char *path) {
    int w = F(0x5092C94, int (*)(void *))(NULL), h = F(0x5092CD4, int (*)(void *))(NULL);
    F(0x50CE6A4, void (*)(Obj, void *))(NULL, NULL);
    Obj tex = obj_new(0x6655970);
    F(0x50C4E5C, void (*)(Obj, int, int, int, bool, void *))(tex, w, h, 3, false, NULL);
    RectF2 r = { 0, 0, (float)w, (float)h };
    F(0x50C63B0, void (*)(Obj, RectF2, int, int, void *))(tex, r, 0, 0, NULL);
    F(0x50C5F34, void (*)(Obj, void *))(tex, NULL);
    return png_out(tex, path, w, h, "backbuffer");
}
static char *ui_shot(const char *path) {
    Obj cam = F(0x5083208, Obj (*)(void *))(NULL);                          /* Camera.main */
    if (!cam) return strdup("{\"ok\":false,\"error\":\"no Camera.main\"}");
    int w = F(0x5092C94, int (*)(void *))(NULL), h = F(0x5092CD4, int (*)(void *))(NULL);
    Obj tobj = F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(md(0x664B018)));   /* typeof(Canvas) */
    Obj arr = F(0x50FCCC0, Obj (*)(Obj, void *))(tobj, NULL);               /* Object.FindObjectsOfType(Type) */
    int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0, nsw = 0;
    Obj sw[64]; Obj oldcam[64];
    for (int i = 0; i < n && nsw < 64; i++) {
        Obj cv = AT((char *)arr + 0x20, i * 8, Obj);
        if (!u_alive(cv) || !F(0x547521C, bool (*)(Obj, void *))(cv, NULL)) continue;          /* root canvases */
        if (F(0x5475018, int (*)(Obj, void *))(cv, NULL) != 0) continue;                        /* overlay only */
        oldcam[nsw] = F(0x5477610, Obj (*)(Obj, void *))(cv, NULL);
        F(0x547771C, void (*)(Obj, Obj, void *))(cv, cam, NULL);
        F(0x5475104, void (*)(Obj, int, void *))(cv, 1, NULL);              /* ScreenSpaceCamera */
        F(0x5475F18, void (*)(Obj, float, void *))(cv, F(0x507FC2C, float (*)(Obj, void *))(cam, NULL) + 0.5f, NULL);   /* planeDistance: just past the near plane */
        sw[nsw++] = cv;
    }
    static void (*render)(void *);
    if (!render) render = F(0x6F8B90, void *(*)(const char *))("UnityEngine.Camera::Render_Injected");
    Obj rt = F(0x50D2E38, Obj (*)(int, int, int, void *))(w, h, 24, NULL);
    F(0x5081CB8, void (*)(Obj, Obj, void *))(cam, rt, NULL);
    F(0x5477FEC, void (*)(void *))(NULL);                                    /* Canvas.ForceUpdateCanvases */
    if (render) render(AT(cam, 0x10, void *));
    F(0x50CE6A4, void (*)(Obj, void *))(rt, NULL);
    Obj tex = obj_new(0x6655970);
    F(0x50C4E5C, void (*)(Obj, int, int, int, bool, void *))(tex, w, h, 3, false, NULL);
    RectF2 r = { 0, 0, (float)w, (float)h };
    F(0x50C63B0, void (*)(Obj, RectF2, int, int, void *))(tex, r, 0, 0, NULL);
    F(0x50C5F34, void (*)(Obj, void *))(tex, NULL);
    F(0x50CE6A4, void (*)(Obj, void *))(NULL, NULL);
    F(0x5081CB8, void (*)(Obj, Obj, void *))(cam, NULL, NULL);
    F(0x50CFE6C, void (*)(Obj, void *))(rt, NULL);
    for (int i = 0; i < nsw; i++) { F(0x5475104, void (*)(Obj, int, void *))(sw[i], 0, NULL); F(0x547771C, void (*)(Obj, Obj, void *))(sw[i], oldcam[i], NULL); }
    F(0x5477FEC, void (*)(void *))(NULL);
    Obj png = F(0x51657D8, Obj (*)(Obj, void *))(tex, NULL);
    F(0x50FC8F8, void (*)(Obj, void *))(tex, NULL);
    if (!png) return strdup("{\"ok\":false,\"error\":\"EncodeToPNG null\"}");
    size_t nb = (size_t)AT(png, 0x18, uintptr_t);
    FILE *f = fopen(path, "wb"); if (!f) return strdup("{\"ok\":false,\"error\":\"cannot write\"}");
    fwrite((char *)png + 0x20, 1, nb, f); fclose(f);
    Buf b = {0}; bput(&b, "{\"ok\":true,\"w\":%d,\"h\":%d,\"canvases\":%d,\"bytes\":%zu}", w, h, nsw, nb); return b.p;
}

char *mpmenu_exec(const char *cmd, const char *line) {
    if (!strcmp(cmd, "mpm_shot")) {
        char p[1024], how[32] = "camera"; if (!jstr(line, "path", p, sizeof p)) return strdup("{\"ok\":false}");
        jstr(line, "how", how, sizeof how);
        return !strcmp(how, "backbuffer") ? backbuffer_shot(p) : ui_shot(p);
    }
    if (!strcmp(cmd, "mpm_tree")) {                                         /* hierarchy of the Esc menu (layout debugging) */
        Obj v = esc_view(); if (!v) return strdup("{\"ok\":false,\"error\":\"no Esc menu\"}");
        Buf b = {0}; int n = 0; bput(&b, "{\"ok\":true,\"screen\":[%d,%d],\"tree\":[", F(0x5092C94, int (*)(void *))(NULL), F(0x5092CD4, int (*)(void *))(NULL));
        tree(&b, u_tr(v), 0, &n); bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "mpm_click")) {                                        /* mpm_click {button: enum mpm_button} */
        double v; if (!jnum(line, "button", &v)) return strdup("{\"ok\":false,\"error\":\"need button\"}");
        pending_click = (int)v;
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_press")) {                                        /* real path: OnPointerClick / OnPressButton of the clone */
        double v; if (!jnum(line, "button", &v)) return strdup("{\"ok\":false,\"error\":\"need button\"}");
        int id = (int)v; Obj e = id > 0 && id < MPB_COUNT ? eb(id) : NULL;
        if (e) { (mm_is(e, K_SMB) ? F(0xF2DC30, void (*)(Obj, void *))(e, NULL) : (void)0); return strdup("{\"ok\":true,\"via\":\"OnPressButton\"}"); }
        for (int i = 0; i < LW_NB; i++) if (lw_ids[i] == id && u_on(u_gc_get(h_lwb[i]))) {    /* lobby window button: the real path */
            Obj w = u_gc_get(h_lwb[i]); (mm_is(w, K_SMB) ? F(0xF2DC30, void (*)(Obj, void *))(w, NULL) : (void)0);
            return strdup("{\"ok\":true,\"via\":\"OnPressButton (lobby window)\"}"); }
        if (id > 0 && id < MPB_COUNT && u_on(u_gc_get(h_lwr[id]))) {        /* lobby Rules panel */
            Obj w = u_gc_get(h_lwr[id]); (mm_is(w, K_SMB) ? F(0xF2DC30, void (*)(Obj, void *))(w, NULL) : (void)0);
            return strdup("{\"ok\":true,\"via\":\"OnPressButton (lobby rules)\"}"); }
        Obj t = id > 0 && id < MPB_COUNT ? tb(id) : NULL;
        if (t && mpmenu_title_click(t)) return strdup("{\"ok\":true,\"via\":\"title_click\"}");
        if (id > 0 && id < MPB_COUNT) { pending_click = id; return strdup("{\"ok\":true,\"via\":\"queued (the clone is being rebuilt)\"}"); }
        return strdup("{\"ok\":false,\"error\":\"no such button\"}");
    }
    if (!strcmp(cmd, "mpm_type")) {                                         /* type into the address (or "field":"name") field (test) */
        char t[256] = "", which[16] = ""; jstr(line, "text", t, sizeof t); jstr(line, "field", which, sizeof which);
        Obj f = u_gc_get(strcmp(which, "name") ? h_field : h_namefield);
        if (!u_alive(f)) { F(0x518AFE0, void (*)(Obj, void *))(u_str(t), NULL); return strdup("{\"ok\":true,\"via\":\"clipboard\"}"); }
        (mm_is(f, K_FIELD) ? F(0x4EFAE8C, void (*)(Obj, Obj, void *))(f, u_str(t), NULL) : (void)0); return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_newcampaign")) {                                  /* test: what the new-campaign screen's Start sends */
        char nm[64] = "MP Test", sd[32] = "12345"; double sc = 1; jstr(line, "name", nm, sizeof nm); jstr(line, "seed", sd, sizeof sd); jnum(line, "scenario", &sc);
        Obj ev = obj_new(0x664BC18);                                        /* CreateCampaignRequested */
        F(0xD61EDC, void (*)(Obj, Obj, int, int, int, Obj, bool, bool, bool, Obj, bool, void *))(ev, u_str(nm), (int)sc, 1, 1, u_str(sd), false, false, false, u_str(""), false, NULL);
        F(0xCCFAD4, void (*)(Obj, Obj, void *))(AT(mf, 0x48, Obj), ev, NULL);
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_skipintro")) {                                    /* test: the player clicks through the new-campaign intro */
        if (!popup_open(25)) return strdup("{\"ok\":true,\"open\":false}");
        Obj ev = obj_new(0x664B6F8); F(0xDCF898, void (*)(Obj, void *))(ev, NULL);   /* CompleteNarrativeIntroRequested */
        F(0xCCFAD4, void (*)(Obj, Obj, void *))(AT(mf, 0x48, Obj), ev, NULL);
        return strdup("{\"ok\":true,\"open\":true}");
    }
    if (!strcmp(cmd, "mpm_slots")) {                                        /* load list slot titles (MP tag check) */
        static void *ks; if (!ks) ks = class_by_name("Refactor.UI", "SubUI_SaveFileButton");
        Obj ty = ks ? F(0x6F97AC, Obj (*)(void *))(F(0x6F8C9C, void *(*)(void *))(ks)) : NULL;
        Obj arr = ty ? F(0x50FCCC0, Obj (*)(Obj, void *))(ty, NULL) : NULL;   /* FindObjectsOfType: active slots */
        int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"open\":%s,\"slots\":[", popup_open(5) ? "true" : "false");
        for (int i = 0; i < n; i++) {
            Obj sb = AT((char *)arr + 0x20, i * 8, Obj); char t[256] = "";
            if (u_alive(sb)) str_utf8(tmp_get(AT(sb, 0x38, Obj)), t, sizeof t);
            bput(&b, "%s", i ? "," : ""); bstr(&b, t);
        }
        bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "mpm_open_load")) {                                    /* test: the stock Load Campaign button (fills the list) */
        Obj tv = title_view(); if (!tv) return strdup("{\"ok\":false,\"error\":\"not on the title\"}");
        (mm_is(tv, K_TMV) ? F(0xE710B8, void (*)(Obj, void *))(tv, NULL) : (void)0); return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_quit_title")) {                                   /* test: Esc > Quit to title, confirmed */
        Obj v = esc_view(); if (!v) { popup_activate(1); v = esc_view(); }
        if (!v) return strdup("{\"ok\":false,\"error\":\"no Esc menu\"}");
        (mm_is(v, K_SMV) ? F(0xF2F234, void (*)(Obj, void *))(v, NULL) : (void)0);                        /* SystemMenuView.QuitToTitleSceneCallback */
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_esc_load")) {                                     /* test: press the Esc menu's Load */
        Obj v = esc_view(); if (!v) return strdup("{\"ok\":false}");
        (mm_is(v, K_SMV) ? F(0xF2EEA8, void (*)(Obj, void *))(v, NULL) : (void)0); return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_chat")) {                                         /* test: {open:1} {text:"..."} {enter:1} {escape:1} (no keyboard here) */
        double v; char t[256];
        if (jnum(line, "open", &v)) test_chat_open = 1;
        if (jstr(line, "text", t, sizeof t)) snprintf(test_chat_in, sizeof test_chat_in, "%s", t);
        if (jnum(line, "enter", &v)) test_chat_enter = 1;
        if (jnum(line, "escape", &v)) test_chat_escape = 1;
        if (jnum(line, "defocus", &v)) {                                    /* as if the player clicked the world: the field loses the focus */
            static void *mcur, *mset; if (!mcur) { mcur = mth("UnityEngine.EventSystems", "EventSystem", "get_current", 0); mset = mth("UnityEngine.EventSystems", "EventSystem", "SetSelectedGameObject", 1); }
            Obj es = mcur ? MCALL(mcur, Obj, const void *)(mcur) : NULL; if (u_alive(es) && mset) MCALL(mset, void, Obj, Obj, const void *)(es, NULL, mset);
        }
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_key")) {                                         /* test: REAL key / mouse events through AppKit -> Unity's own input */
        char t[256] = "", k[32] = "", via[16] = "window"; double v;
        jstr(line, "via", via, sizeof via);
        int n = 0;
        if (jstr(line, "chars", t, sizeof t)) n += key_inject(t, 0, via) + tmp_keys(t, NULL);   /* key codes to Unity + the text to the field */
        if (jstr(line, "key", k, sizeof k)) n += key_inject(NULL, !strcmp(k, "return") ? 36 : !strcmp(k, "escape") ? 53 : !strcmp(k, "backspace") ? 51 : -1, via)
                                                  + (!strcmp(k, "backspace") ? tmp_keys(NULL, "backspace") : 0);
        if (jnum(line, "click_field", &v)) n += click_field(via);
        char o[96]; snprintf(o, sizeof o, "{\"ok\":%s,\"events\":%d}", n > 0 ? "true" : "false", n); return strdup(o);
    }
    if (!strcmp(cmd, "mpm_find_text")) {                                    /* test: active TMP texts whose rect holds the point (y up, px) */
        double x = 0, y = 0; jnum(line, "px", &x); jnum(line, "py", &y);
        static void *mi; if (!mi) mi = mth("UnityEngine", "Resources", "FindObjectsOfTypeAll", 1);
        Obj arr = mi ? MCALL(mi, Obj, Obj, const void *)(type_obj("TMPro", "TMP_Text"), mi) : NULL;
        int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0, k = 0;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"n\":%d,\"hits\":[", n);
        for (int i = 0; i < n && k < 20; i++) {
            Obj t = AT((char *)arr + 0x20, i * 8, Obj); float r[4];
            if (!u_alive(t) || !u_go_active_self(u_go(t)) || !rect_of(t, r) || x < r[0] || x > r[2] || y < r[1] || y > r[3]) continue;
            char path[400] = "", nm[64]; Obj tr = u_tr(t);
            for (int d = 0; tr && d < 10; d++, tr = u_tr_parent(tr)) { str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(tr, NULL), nm, sizeof nm); char tmp[400]; snprintf(tmp, sizeof tmp, "%s/%s", nm, path); snprintf(path, sizeof path, "%s", tmp); }
            char tx[120] = ""; Obj so = tmp_get(t); if (so) str_utf8(so, tx, sizeof tx);
            bput(&b, "%s{\"path\":", k++ ? "," : ""); bstr(&b, path); bput(&b, ",\"text\":"); bstr(&b, tx);
            bput(&b, ",\"rect\":[%.0f,%.0f,%.0f,%.0f]}", r[0], r[1], r[2], r[3]);
        }
        bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "mpm_tip")) { test_hover[0] = 0; jstr(line, "title", test_hover, sizeof test_hover); return strdup("{\"ok\":true}"); }   /* test: hover the element with this title; none = real mouse */
    if (!strcmp(cmd, "mpm_escape")) { test_escape = 1; return strdup("{\"ok\":true}"); }   /* test: as if Escape was pressed */
    if (!strcmp(cmd, "mpm_cogtest")) {                                      /* test: the built cog on the big Rules box image */
        Obj bx = u_gc_get(h_title_box); if (bx) img_set(bx, spr(S_COG), C_WHITE);
        Obj cg = u_gc_get(h_title_cog); if (cg) { static void *mt; if (!mt) mt = mth("UnityEngine.UI", "Image", "set_type", 1); if (mt) MCALL(mt, void, Obj, int, const void *)(cg, 0, mt); place(cg, 0, 0, 80, 80); }
        return strdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "mpm_cogdbg")) {                                       /* test: the title cog image and its sprite */
        Obj cg = u_gc_get(h_title_cog), sp = spr(S_COG); float r[4] = {0}; if (cg) rect_of(cg, r);
        Obj isp = cg && mm_is(cg, K_IMG) ? F(0x5266F04, Obj (*)(Obj, void *))(cg, NULL) : NULL;
        static void *mtex, *men; if (!mtex) { mtex = mth("UnityEngine", "Sprite", "get_texture", 0); men = mth("UnityEngine", "Behaviour", "get_enabled", 0); }
        Obj tx = sp && mtex ? MCALL(mtex, Obj, Obj, const void *)(sp, mtex) : NULL;
        int en = cg && men ? MCALL(men, bool, Obj, const void *)(cg, men) : -1;
        static void *mrect, *mw, *mpix; if (!mrect) { mrect = mth("UnityEngine", "Sprite", "get_rect", 0); mw = mth("UnityEngine", "Texture", "get_width", 0); mpix = mth("UnityEngine", "Texture2D", "GetPixel", 2); }
        RectF2 sr = { 0 }; if (sp && mrect) sr = MCALL(mrect, RectF2, Obj, const void *)(sp, mrect);
        int tw = tx && mw ? MCALL(mw, int, Obj, const void *)(tx, mw) : -1;
        Col px = { -1, -1, -1, -1 }; if (tx && mpix) px = MCALL(mpix, Col, Obj, int, int, const void *)(tx, 3, 8, mpix);
        Col ic = { -1, -1, -1, -1 }; static void *mcol; if (!mcol) mcol = mth("UnityEngine.UI", "Graphic", "get_color", 0); if (cg && mcol) ic = MCALL(mcol, Col, Obj, const void *)(cg, mcol);
        Buf b = {0}; bput(&b, "{\"ok\":true,\"img\":%d,\"active\":%d,\"enabled\":%d,\"sprite\":%d,\"img_sprite_same\":%d,\"texture\":%d,\"rect\":[%.0f,%.0f,%.0f,%.0f],"
             "\"sprite_rect\":[%.1f,%.1f,%.1f,%.1f],\"tex_w\":%d,\"px38\":[%.2f,%.2f,%.2f,%.2f],\"img_color\":[%.2f,%.2f,%.2f,%.2f]}",
             cg != NULL, u_on(cg), en, u_alive(sp), isp == sp, u_alive(tx), r[0], r[1], r[2], r[3], sr.x, sr.y, sr.w, sr.h, tw, px.r, px.g, px.b, px.a, ic.r, ic.g, ic.b, ic.a);
        return b.p;
    }
    if (!strcmp(cmd, "mpm_rules")) {                                        /* test: a rule change in play (what the panel cog sends) */
        mp_session *cs = mp_game_session(); double u = 0;
        if (!cs || !jnum(line, "units", &u)) return strdup("{\"ok\":false}");
        dsnet_rules r; dsnet_rules_get(mp_client(cs), &r); r.units_per_player = (int)u;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"rc\":%d}", dsnet_set_rules(mp_client(cs), &r)); return b.p;
    }
    if (!strcmp(cmd, "mpm_claim")) {                                        /* test: claim a unit for this player (mp_claim) */
        char gs[64]; Guid g; mp_session *cs = mp_game_session();
        if (!cs || !jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g)) return strdup("{\"ok\":false}");
        Buf b = {0}; bput(&b, "{\"ok\":true,\"rc\":%d}", mp_claim(cs, (const uint8_t *)&g)); return b.p;
    }
    if (!strcmp(cmd, "mpm_esc")) { popup_activate(1); return strdup("{\"ok\":true}"); }   /* open the Esc menu (test) */
    if (!strcmp(cmd, "mpm_join_tcp")) {                                     /* test stand-in for an accepted Steam invite */
        char h[256] = "127.0.0.1"; double port = 0, v; jstr(line, "host", h, sizeof h); jnum(line, "port", &port);
        mp_config c; mp_config_defaults(&c); c.host = 0; snprintf(c.host_addr, sizeof c.host_addr, "%s", h); c.port = (uint16_t)port;
        if (jstr(line, "name", c.name, sizeof c.name)) {} (void)v;
        Buf b = {0}; bput(&b, "{\"ok\":true,\"rc\":%d}", mp_game_join(&c)); return b.p;
    }
    if (!strcmp(cmd, "mpm_status")) {
        mpm_obs o; observe(&o);
        Buf b = {0};
        bput(&b, "{\"ok\":true,\"state\":\"%s\",\"settings\":{\"pause\":%d,\"speedup\":%d,\"gear\":%d,\"units\":%d,\"admin\":%d,\"events\":%d,\"population\":%d},\"msg\":", state_name(M.state),
             M.set.pause_mode, M.set.speedup, M.set.gear, M.set.units, M.set.admin, M.set.events, M.set.population);
        bstr(&b, M.msg);
        bput(&b, ",\"obs\":{\"scene\":%d,\"ready\":%d,\"title\":%d,\"picker\":%d,\"role\":%d,\"guest_loading\":%d,\"failed\":%d,\"esc\":%d},\"visible\":%llu",
             o.scene, o.campaign_ready, o.title_visible, o.picker_open, o.role, o.guest_loading, o.failed, o.esc_open, (unsigned long long)mpm_visible(&M, &o));
        mp_session *ss = mp_game_session();
        bput(&b, ",\"port\":%u,\"direct\":", ss ? mp_server_port(ss) : 0); bstr(&b, M.direct);
        Obj fl = u_gc_get(h_field); char ft[256] = "";
        if (u_alive(fl)) str_utf8((mm_is(fl, K_FIELD) ? F(0x4EFAE84, Obj (*)(Obj, void *))(fl, NULL) : NULL), ft, sizeof ft);
        bput(&b, ",\"field\":{\"exists\":%s,\"active\":%s,\"text\":", u_alive(fl) ? "true" : "false", u_on(fl) ? "true" : "false"); bstr(&b, ft); bput(&b, "}");
        char clip[128] = ""; str_utf8(F(0x518AEA0, Obj (*)(void *))(NULL), clip, sizeof clip);
        bput(&b, ",\"clipboard\":"); bstr(&b, clip);
        put_btns(&b, "title", title_ids, NT, h_tb, 0x130);
        put_btns(&b, "esc", esc_ids, NE, h_eb, 0x28);
        Obj t = u_gc_get(h_etext); char tt[1024] = "";
        if (u_alive(t)) str_utf8(tmp_get(t), tt, sizeof tt);
        bput(&b, ",\"lobby_text_active\":%s,\"lobby_text\":", u_on(t) ? "true" : "false"); bstr(&b, tt);
        { Obj ti = u_gc_get(h_etitle), rl = u_gc_get(h_ereason); char a1[160] = "", a2[160] = "";
          if (u_alive(ti)) str_utf8(tmp_get(ti), a1, sizeof a1); if (u_alive(rl)) str_utf8(tmp_get(rl), a2, sizeof a2);
          bput(&b, ",\"lobby_title\":"); bstr(&b, a1); bput(&b, ",\"lobby_title_active\":%d,\"esc_reason\":", u_on(ti)); bstr(&b, a2);
          bput(&b, ",\"esc_reason_active\":%d", u_on(rl)); }
        bput(&b, ",\"layout\":%s", layout_log[0] ? layout_log : "null");
        bput(&b, ",\"button_rects\":{");
        for (int i = 0, k = 0; i < NE; i++) { float r[4]; Obj x = eb(esc_ids[i]); if (u_on(x) && rect_of(x, r)) bput(&b, "%s\"%d\":[%.0f,%.0f,%.0f,%.0f]", k++ ? "," : "", esc_ids[i], r[0], r[1], r[2], r[3]); }
        bput(&b, "}");
        Obj ev2 = esc_view(); float sr[4];
        bput(&b, ",\"seed_rects\":[");
        for (int k = 0, kk = 0; ev2 && k < 2; k++) { Obj l = AT(ev2, 0x88 + 8 * k, Obj); if (u_on(l) && rect_of(l, sr)) bput(&b, "%s[%.0f,%.0f,%.0f,%.0f]", kk++ ? "," : "", sr[0], sr[1], sr[2], sr[3]); }
        bput(&b, "]");
        { Obj nf = u_gc_get(h_namefield); char nt[128] = ""; field_text(h_namefield, nt, sizeof nt);
          bput(&b, ",\"name_field\":{\"exists\":%s,\"active\":%s,\"text\":", u_alive(nf) ? "true" : "false", u_on(nf) ? "true" : "false"); bstr(&b, nt); bput(&b, "}"); }
        bput(&b, ",\"my_name\":"); bstr(&b, my_name);
        bput(&b, ",\"frames\":%lld,\"connecting\":", (long long)frames); bstr(&b, M.connecting);
        { Obj root = u_gc_get(h_lw_root); char t[256];                     /* lobby window (v2 §2b) read-back */
          bput(&b, ",\"lw\":{\"active\":%d,\"area_active\":%d,\"settings_open\":%d,\"title\":", u_on(root), u_on(u_gc_get(h_lw_area)), M.lobby_settings_open);
          t[0] = 0; if (lwt(T_TITLE)) str_utf8(tmp_get(lwt(T_TITLE)), t, sizeof t); bstr(&b, t);
          bput(&b, ",\"guest\":"); t[0] = 0; if (lwt(T_GUEST) && u_on(lwt(T_GUEST))) str_utf8(tmp_get(lwt(T_GUEST)), t, sizeof t); bstr(&b, t);
          bput(&b, ",\"note\":"); t[0] = 0; if (lwt(T_NOTE)) str_utf8(tmp_get(lwt(T_NOTE)), t, sizeof t); bstr(&b, t);
          { Obj ib3 = u_gc_get(h_lwb[LW_INVITE]); char it[160] = ""; if (u_on(ib3)) str_utf8(tmp_get(AT(ib3, 0x28, Obj)), it, sizeof it); bput(&b, ",\"invite\":"); bstr(&b, it); }
          bput(&b, ",\"addr\":"); t[0] = 0; if (lwt(T_INV_ADDR) && u_on(lwt(T_INV_ADDR))) str_utf8(tmp_get(lwt(T_INV_ADDR)), t, sizeof t); bstr(&b, t);
          bput(&b, ",\"rows\":[");
          for (int i = 0; i < LW_ROWS; i++) {
              char n[128] = "", st[64] = ""; int lit = 0;
              if (u_gc_get(h_lw_name[i])) str_utf8(tmp_get(u_gc_get(h_lw_name[i])), n, sizeof n);
              if (u_gc_get(h_lw_stat[i])) str_utf8(tmp_get(u_gc_get(h_lw_stat[i])), st, sizeof st);
              for (int k = 0; k < 3; k++) lit += u_on(u_gc_get(h_lw_bar[i][k]));
              bput(&b, "%s{\"name\":", i ? "," : ""); bstr(&b, n); bput(&b, ",\"status\":"); bstr(&b, st);
              bput(&b, ",\"bars_shown\":%d,\"level\":%d,\"x\":%d}", lit, lw_level[i], u_on(u_gc_get(h_lw_x[i])));
          }
          bput(&b, "],\"buttons\":{");
          for (int i = 0, k = 0; i < LW_NB; i++) { Obj x = u_gc_get(h_lwb[i]); float r[4]; if (u_on(x) && rect_of(x, r)) bput(&b, "%s\"%d\":[%.0f,%.0f,%.0f,%.0f]", k++ ? "," : "", lw_ids[i], r[0], r[1], r[2], r[3]); }
          float wr[4] = {0}; if (u_on(root)) rect_of(root, wr);
          bput(&b, "},\"rect\":[%.0f,%.0f,%.0f,%.0f]}", wr[0], wr[1], wr[2], wr[3]); }
        bput(&b, ",\"teams_modal\":%d", teams_modal());
        bput(&b, ",\"rules_panel\":{\"title\":%d,\"lobby\":%d}", u_on(u_gc_get(rp_title.root)), u_on(u_gc_get(rp_lobby.root)));
        { Obj bk = tb(MPB_BACK), bg = bk ? first_image(u_tr(bk), 0) : NULL;
          static void *mr; if (!mr) mr = mth("UnityEngine.UI", "Graphic", "get_raycastTarget", 0);
          bput(&b, ",\"back_click_target\":%d", bg && mr && mm_is(bg, K_GR) ? (int)MCALL(mr, bool, Obj, const void *)(bg, mr) : -1); }
        { bput(&b, ",\"hist\":{\"n\":%d,\"panel\":%d,\"rows\":[", nhist, u_on(u_gc_get(h_rw_root)));
          for (int i = 0; i < nhist && i < 3; i++) {
              hist_t *h = &hist[i]; bput(&b, "%s{\"world\":", i ? "," : ""); bstr(&b, h->world); bput(&b, ",\"day\":%lld,\"how\":", h->day); bstr(&b, h->how);
              bput(&b, ",\"addr\":"); bstr(&b, h->addr); bput(&b, ",\"host\":"); bstr(&b, h->host); bput(&b, ",\"players\":"); bstr(&b, h->players);
              bput(&b, ",\"units\":"); bstr(&b, h->units); bput(&b, ",\"portraits\":"); bstr(&b, h->portraits);
              int shown = 0; for (int q = 0; q < 6; q++) shown += u_on(u_gc_get(h_rw_pp[i < 6 ? i : 0][q])); bput(&b, ",\"portraits_shown\":%d}", shown); }
          bput(&b, "]}"); }
        { bput(&b, ",\"chat\":{\"typing\":%d,\"buf\":", chat_typing); bstr(&b, chat_buf);
          int idle = 0; float a0 = 0; for (int i = 0; i < CH_IDLE; i++) if (u_on(u_gc_get(h_ch_idle[i]))) idle++;
          if (nch) { double age = mono_s() - chist[nch - 1].t; a0 = age < chat_fade_s ? 1 : age < chat_fade_s + 1 ? (float)(chat_fade_s + 1 - age) : 0; }
          bput(&b, ",\"lines\":%d,\"idle_shown\":%d,\"newest_alpha\":%.2f,\"box\":%d,\"hotkeys_blocked\":%d,\"last\":[", nch, idle, a0, u_on(u_gc_get(h_ch_box)),
               (int)F(0xD42BBC, bool (*)(void *))(NULL));
          for (int i = nch > 4 ? nch - 4 : 0, k = 0; i < nch; i++, k++) { bput(&b, "%s", k ? "," : ""); bstr(&b, chist[i].text); }
          float cr[4] = { 0 }; Obj cb = u_gc_get(h_ch_box); if (u_on(cb)) rect_of(cb, cr); else { Obj t0 = u_gc_get(h_ch_idlebg[CH_IDLE - 1]); if (u_on(t0)) rect_of(t0, cr); }
          static void *msw, *msh; if (!msw) { msw = mth("UnityEngine", "Screen", "get_width", 0); msh = mth("UnityEngine", "Screen", "get_height", 0); }
          { Obj f = u_gc_get(h_ch_field), sel = NULL; char sn[64] = "";
            static void *mcur, *msel, *mfoc, *mint; if (!mcur) { mcur = mth("UnityEngine.EventSystems", "EventSystem", "get_current", 0); msel = mth("UnityEngine.EventSystems", "EventSystem", "get_currentSelectedGameObject", 0);
                mfoc = mth("TMPro", "TMP_InputField", "get_isFocused", 0); mint = mth("UnityEngine.UI", "Selectable", "IsInteractable", 0); }
            Obj es = mcur ? MCALL(mcur, Obj, const void *)(mcur) : NULL; if (u_alive(es) && msel) sel = MCALL(msel, Obj, Obj, const void *)(es, msel);
            if (u_alive(sel)) str_utf8(F(0x50FB198, Obj (*)(Obj, void *))(sel, NULL), sn, sizeof sn);
            char ft[DSNET_CHAT_MAX + 1] = ""; if (u_alive(f)) utf16_to_utf8(F(0x4EFAE84, Obj (*)(Obj, void *))(f, NULL), ft, sizeof ft);
            bput(&b, "],\"typing_line\":"); bstr(&b, typing_shown);
          bput(&b, ",\"field\":{\"alive\":%d,\"on\":%d,\"es\":%d,\"selected\":", u_alive(f), u_on(f), u_alive(es)); bstr(&b, sn);
            bput(&b, ",\"is_field\":%d,\"focused\":%d,\"allow_input\":%d,\"activate_next\":%d,\"interactable\":%d,\"readonly\":%d,\"text\":",
                 u_alive(f) && u_alive(sel) && sel == u_go(f), u_alive(f) && mfoc ? (int)MCALL(mfoc, bool, Obj, const void *)(f, mfoc) : -1,
                 u_alive(f) ? AT(f, 0x260, bool) : -1, u_alive(f) ? AT(f, 0x261, bool) : -1, u_alive(f) && mint ? (int)MCALL(mint, bool, Obj, const void *)(f, mint) : -1, u_alive(f) ? AT(f, 0x220, bool) : -1);
            bstr(&b, ft); bput(&b, ",\"input_chars\":%d,\"input_enters\":%d,\"input_a\":%d}", dbg_istr_n, dbg_enter_n, dbg_a_n); }
          bput(&b, ",\"fade\":%d,\"pos\":[%.0f,%.0f],\"rect\":[%.0f,%.0f,%.0f,%.0f],\"screen\":[%d,%d]}", chat_fade_s, chat_x, chat_y, cr[0], cr[1], cr[2], cr[3],
               msw ? MCALL(msw, int, const void *)(msw) : 0, msh ? MCALL(msh, int, const void *)(msh) : 0); }
        { rpanel *rp = u_on(u_gc_get(rp_lobby.root)) ? &rp_lobby : &rp_title;
          char *pr = ui_tip_exec("ui_tip", "{\"action\":\"probe\"}");          /* the game window's state (shown / tips_on) */
          bput(&b, ",\"tip\":{\"title\":"); bstr(&b, tip_now); bput(&b, ",\"rules_row\":%d,\"ready\":%d,\"game\":%s}", rp->tip_row, tip_ready(), pr ? pr : "null"); free(pr); }
        bput(&b, ",\"cog\":{\"title\":%d,\"title_pixel\":%d,\"lobby\":%d}", u_on(u_gc_get(h_title_cog)), u_on(u_gc_get(h_title_pcog)), u_on(u_gc_get(h_lw_cog)) || u_on(u_gc_get(h_lw_pcog)));
        { mp_session *as = mp_game_session(); bput(&b, ",\"admin\":%d", as ? dsnet_admin_mode(mp_client(as)) : -1);
          if (as) { dsnet_client *ac = mp_client(as); dsnet_rules r; dsnet_rules_get(ac, &r); int me = dsnet_player_id(ac);
                    bput(&b, ",\"rules\":{\"pause\":%d,\"speedup\":%d,\"gear\":%d,\"units\":%d,\"events\":%d,\"population\":%d},\"owned\":%d,\"refused\":%u",
                         r.pause_mode, r.allow_speedup, r.gear_mode, r.units_per_player, r.event_choice, r.population, dsnet_owned_count(ac, me), dsnet_claim_refusals(ac, me)); } }
        bput(&b, ",\"notice_pending\":"); bstr(&b, M.notice);
        bput(&b, ",\"host_lobby\":%d,\"lobby_open\":%d", o.host_lobby, mpmenu_lobby_open());
        { mp_session *hs = mp_game_session(); int nl = 0, r;
          for (int i = 0, n = hs ? dsnet_hold_count(mp_client(hs)) : 0; i < n; i++) if (dsnet_hold_get(mp_client(hs), i, &r, NULL, NULL) == 0 && r == DSNET_HOLD_LOBBY) nl++;
          bput(&b, ",\"lobby_holds\":%d", nl); }
        { Obj cn = find_view_x(&h_cnv, &k_cnv, "Refactor.UI", "CommonNoticeView", true); char nt[400] = "";
          if (cn) str_utf8(tmp_get(AT(cn, 0x30, Obj)), nt, sizeof nt);
          bput(&b, ",\"notice_box\":{\"exists\":%d,\"active\":%d,\"text\":", cn != NULL, u_on(cn)); bstr(&b, nt); bput(&b, "}"); }
        { Obj slv = find_view_x(&h_slv, &k_slv, "Refactor", "SaveLoadView", true); char t1[128] = "", t2[64] = "";
          if (slv && u_alive(AT(slv, 0x48, Obj))) str_utf8(tmp_get(AT(slv, 0x48, Obj)), t1, sizeof t1);
          Obj lb = slv ? AT(slv, 0xA0, Obj) : NULL, bt = u_alive(lb) ? tmp_below(u_tr(lb), 0) : NULL; if (bt) str_utf8(tmp_get(bt), t2, sizeof t2);
          bput(&b, ",\"load_list\":{\"title\":"); bstr(&b, t1); bput(&b, ",\"button\":"); bstr(&b, t2); bput(&b, "}"); }
        { Obj ev3 = esc_view(); Obj lb = ev3 ? AT(ev3, 0x60, Obj) : NULL; char lt[160] = "";
          if (u_alive(lb)) str_utf8(tmp_get(AT(lb, 0x28, Obj)), lt, sizeof lt);
          Obj btn = u_alive(lb) ? AT(lb, 0x20, Obj) : NULL;
          bput(&b, ",\"esc_load\":{\"text\":"); bstr(&b, lt);
          bput(&b, ",\"interactable\":%d}", btn ? (int)(mm_is(btn, K_SEL) ? F(0x549C6AC, bool (*)(Obj, void *))(btn, NULL) : false) : -1); }
        Obj tv = title_view();
        bput(&b, ",\"stock_title\":[");
        for (int off = 0x30; tv && off <= 0x60; off += 8) bput(&b, "%s%d", off > 0x30 ? "," : "", u_on(AT(tv, off, Obj)));
        bput(&b, "]}");
        return b.p;
    }
    return NULL;
}
