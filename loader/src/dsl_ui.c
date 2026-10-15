// dsl_ui.c - the dsl_ui widget kit (struct DSLUi, dsl_sdk.h API 3), the title screen buttons (dsl_ui_title_add)
// and the loader's frame tick (EventSystem.Update hook, once per Time.frameCount, then dsl_frame_hook).
//
// Look: ported once from ~/ds_multiplayer/src/mpmenu.c (mk_img, mk_txt, mk_button, field_make, rp_build rows, txt_style,
// txt_body, img_set, tip_try, leave_box_show) and ui_tip.c, so the widgets look like the multiplayer pages.
// Every game function is found by its il2cpp NAME (dsl_find_class / dsl_find_method + parameter types where
// overloads share a name and argument count) and every field by name (dsl_field_offset), so the file ports to Windows.
// The only fixed offsets are the il2cpp runtime's own layouts (object header 0x10, array length 0x18 / data 0x20,
// string length 0x10 / chars 0x14, delegate fields), which are the same in every il2cpp build of this Unity version.
//
// Clicks without hooks (modules/modmenu/DESIGN.md): the multiplayer module already hooks ~112 game functions and the
// engine refuses a second hook on one function. So:
//   - buttons are clones of SubUI_TitleMenuButton (a DASButton) whose Init(Action) gets a crafted System.Action;
//     the stock OnPointerClick invokes it (MP's OnPointerClick hook passes clones that are not its own to the stock code);
//   - the notice box is the game's CommonNoticeView.Show_Select / Show_Confirm with crafted Actions (MP's OnClick_*
//     hooks call the stock code, which invokes them);
//   - text field submit, tooltip hover: polled every frame (TMP_InputField.isFocused + Input.GetKeyDown(Return),
//     Input.mousePosition against the widget's screen rect);
//   - the tooltip is the game's own TooltipUI / Tooltip_ETC: shown with a stock key, then its two texts are replaced
//     right after the stock Show (no hook on Tooltip_ETC.Show, which MP owns).
// A crafted Action only queues the click; the module's callback runs from the frame tick (main thread, after
// EventSystem.Update returned, never inside a game call chain).
#include "dsl_internal.h"
#include "../sdk/dsl_sdk.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void *Obj;
typedef struct { float x, y; } V2;
typedef struct { float x, y, z; } V3;
typedef struct { float r, g, b, a; } Col;
typedef struct { float x, y, w, h; } R4;
typedef struct { uint64_t lo, hi; } Guid;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
/* call a method through its MethodInfo (methodPointer = first field; the MethodInfo is passed last) */
#define CALL(mi, R, ...) ((R (*)(__VA_ARGS__, const void *))(*(void **)(mi)))
#define CALL0(mi, R) ((R (*)(const void *))(*(void **)(mi)))

/* the lead's per-frame work (dsl_reg.c); this weak empty default lets the loader link without it (a strong definition
   in dsl_reg.c replaces it at link time) */
__attribute__((weak)) void dsl_frame_hook(void) {}

/* ------------------------------------------------------------------ il2cpp exports */
static struct {
    Obj (*string_new)(const char *);
    uintptr_t (*gchandle_new)(Obj, bool);
    Obj (*gchandle_get_target)(uintptr_t);
    void (*gchandle_free)(uintptr_t);
    void *(*object_get_class)(Obj);
    bool (*class_is_assignable_from)(void *, void *);
    void *(*class_get_type)(void *);
    Obj (*type_get_object)(void *);
    Obj (*object_new)(void *);
    Obj (*array_new)(void *, size_t);
    void *(*class_get_methods)(void *, void **);
    const char *(*method_get_name)(void *);
    uint32_t (*method_get_param_count)(void *);
    void *(*method_get_param)(void *, uint32_t);
    char *(*type_get_name)(void *);
    void *(*class_get_parent)(void *);
    void *(*object_get_virtual_method)(Obj, void *);
    int32_t (*class_value_size)(void *, uint32_t *);
    bool (*method_is_generic)(void *);
    void (*il_free)(void *);
} I;

/* ------------------------------------------------------------------ classes, methods, fields (by name) */
static struct {
    void *Object, *Component, *GameObject, *Transform, *RectTransform, *Graphic, *Image, *TMP_Text, *TMP_Input, *Selectable,
         *Sprite, *FontAsset, *Action, *Byte, *Vector3, *Canvas, *CanvasScaler, *GraphicRaycaster, *RectMask2D, *ScrollRect,
         *Texture2D, *TitleMenuView, *TitleButton, *Notice, *DASText, *TooltipUI, *TooltipETC, *TipCtx, *UIFactory,
         *SaveLoadView, *CampaignSetting, *TMP_Asset, *CanvasGroup;
} K;

static void *m_obj_implicit, *m_obj_name, *m_obj_inst, *m_obj_destroy, *m_obj_dontdestroy, *m_obj_find, *m_obj_finds, *m_res_findall,
    *m_comp_go, *m_comp_tr, *m_comp_get, *m_comp_inparent,
    *m_go_ctor, *m_go_active, *m_go_self, *m_go_tree, *m_go_add, *m_go_tr, *m_go_layer,
    *m_tr_parent, *m_tr_setparent, *m_tr_sib, *m_tr_setsib, *m_tr_count, *m_tr_child,
    *m_rt_amin, *m_rt_amax, *m_rt_pivot, *m_rt_size, *m_rt_getsize, *m_rt_pos, *m_rt_corners,
    *m_gr_color, *m_gr_raycast, *m_gr_material, *m_img_sprite, *m_img_getsprite, *m_img_type, *m_beh_enabled,
    *m_tmp_get, *m_tmp_set, *m_tmp_font, *m_tmp_getfont, *m_tmp_size, *m_tmp_auto, *m_tmp_align, *m_tmp_wrap, *m_tmp_overflow,
    *m_tmp_rich, *m_tmp_shmat, *m_asset_mat,
    *m_in_text, *m_in_settext, *m_in_limit, *m_in_ctype, *m_in_ph, *m_in_tc, *m_in_focused, *m_sel_inter,
    *m_input_mouse, *m_input_keydown, *m_time_frame, *m_clip_get, *m_clip_set,
    *m_tex_ctor, *m_tex_load, *m_tex_filter, *m_tex_wrap, *m_tex_apply, *m_tex_w, *m_tex_h, *m_spr_create,
    *m_cv_mode, *m_cv_getmode, *m_cv_order, *m_cv_override, *m_cv_root, *m_cv_cam, *m_cs_mode, *m_cs_getmode, *m_cs_ref, *m_cs_getref,
    *m_cs_match, *m_cs_getmatch, *m_cs_smm, *m_cs_getsmm, *m_rtu_w2s,
    *m_sr_content, *m_sr_viewport, *m_sr_h, *m_sr_v, *m_sr_move, *m_sr_sens,
    *m_es_update, *m_tb_init, *m_tb_inter, *m_nv_select, *m_nv_confirm, *m_ui_prefab,
    *m_tt_show, *m_tt_hide, *m_tb_id, *m_tb_hidediv, *m_tb_disablehold, *m_cg_blocks;

static int32_t f_tb_label = -1, f_tv_btn[7], f_tv_credit = -1, f_nv_text = -1, f_nv_tok = -1, f_nv_tcancel = -1,
               f_das_usekey = -1, f_etc_title = -1, f_etc_sublayout = -1, f_etc_desc = -1, f_etc_flavor = -1, f_tb_canhold = -1,
               f_ctx_hidehold = -1, f_uif_prefabs = -1, f_slv_field = -1, f_cs_field = -1, f_asset_mat = -1;
static int32_t ctx_size;

/* a method by name, argument count and (optional) parameter type names "A,B" (overloads), on klass or its parents */
static void *mi_sig(void *k, const char *name, int argc, const char *sig) {
    for (void *c = k; c; c = I.class_get_parent(c)) {
        void *it = NULL, *m;
        while ((m = I.class_get_methods(c, &it))) {
            if (strcmp(I.method_get_name(m), name) || (int)I.method_get_param_count(m) != argc) continue;
            if (I.method_is_generic && I.method_is_generic(m)) continue;
            if (!sig) return m;
            char buf[512] = ""; size_t o = 0;
            for (int i = 0; i < argc; i++) {
                char *tn = I.type_get_name(I.method_get_param(m, (uint32_t)i));
                o += (size_t)snprintf(buf + o, o < sizeof buf ? sizeof buf - o : 0, "%s%s", i ? "," : "", tn ? tn : "?");
                if (tn && I.il_free) I.il_free(tn);
            }
            if (!strcmp(buf, sig)) return m;
        }
    }
    return NULL;
}
static int miss_core, miss_opt;
static char miss_list[1024];
static void *klass(const char *ns, const char *n) {
    void *k = dsl_find_class(ns, n);
    if (!k) { miss_core++; size_t o = strlen(miss_list); snprintf(miss_list + o, sizeof miss_list - o, " class %s.%s", ns, n); }
    return k;
}
static void *mth_(int core, void *k, const char *name, int argc, const char *sig) {
    void *m = k ? mi_sig(k, name, argc, sig) : NULL;
    if (!m || !*(void **)m) {
        if (core) miss_core++; else miss_opt++;
        size_t o = strlen(miss_list); snprintf(miss_list + o, sizeof miss_list - o, " %s%s/%d", core ? "" : "(opt)", name, argc);
        return NULL;
    }
    return m;
}
#define MTH(k, n, a) mth_(1, k, n, a, NULL)
#define MSIG(k, n, a, s) mth_(1, k, n, a, s)
#define OPT(k, n, a) mth_(0, k, n, a, NULL)
#define OPTSIG(k, n, a, s) mth_(0, k, n, a, s)
static int32_t fld(void *k, const char *n) {
    int32_t o = k ? dsl_field_offset(k, n) : -1;
    if (o < 0) { miss_opt++; size_t l = strlen(miss_list); snprintf(miss_list + l, sizeof miss_list - l, " (opt)field %s", n); }
    return o;
}

/* ------------------------------------------------------------------ small Unity helpers */
static Obj str(const char *s) { return I.string_new(s ? s : ""); }
static void utf8_of(Obj s, char *out, size_t cap) {                  /* il2cpp string (UTF-16) -> UTF-8 */
    size_t n = 0; if (!cap) return; out[0] = 0;
    if (!s) return;
    int32_t len = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    for (int32_t i = 0; i < len; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < len) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
        char b[4]; int k = 0;
        if (u < 0x80) b[k++] = (char)u;
        else if (u < 0x800) { b[k++] = (char)(0xC0 | u >> 6); b[k++] = (char)(0x80 | (u & 63)); }
        else if (u < 0x10000) { b[k++] = (char)(0xE0 | u >> 12); b[k++] = (char)(0x80 | (u >> 6 & 63)); b[k++] = (char)(0x80 | (u & 63)); }
        else { b[k++] = (char)(0xF0 | u >> 18); b[k++] = (char)(0x80 | (u >> 12 & 63)); b[k++] = (char)(0x80 | (u >> 6 & 63)); b[k++] = (char)(0x80 | (u & 63)); }
        if (n + (size_t)k + 1 > cap) break;
        memcpy(out + n, b, (size_t)k); n += (size_t)k;
    }
    out[n] = 0;
}
static size_t utf8_len_of(Obj s) { char b[8192]; utf8_of(s, b, sizeof b); return strlen(b); }
static int alive(Obj o) { return o && CALL(m_obj_implicit, bool, Obj)(o, m_obj_implicit); }
static int isa(Obj o, void *k) { return k && alive(o) && I.class_is_assignable_from(k, I.object_get_class(o)); }
static Obj type_of(void *k) { return k ? I.type_get_object(I.class_get_type(k)) : NULL; }
static uintptr_t hnew(Obj o) { return o ? I.gchandle_new(o, false) : 0; }
static Obj hget(uintptr_t h) { Obj o = h ? I.gchandle_get_target(h) : NULL; return alive(o) ? o : NULL; }
static void hfree(uintptr_t *h) { if (*h) I.gchandle_free(*h); *h = 0; }
static void hset(uintptr_t *h, Obj o) { hfree(h); *h = hnew(o); }
static Obj go_of(Obj o) {                                            /* GameObject of a component (or the GameObject itself) */
    if (isa(o, K.GameObject)) return o;
    return isa(o, K.Component) ? CALL(m_comp_go, Obj, Obj)(o, m_comp_go) : NULL;
}
static Obj tr_of(Obj o) {
    if (isa(o, K.Transform)) return o;
    if (isa(o, K.GameObject)) return CALL(m_go_tr, Obj, Obj)(o, m_go_tr);
    return isa(o, K.Component) ? CALL(m_comp_tr, Obj, Obj)(o, m_comp_tr) : NULL;
}
static void go_active(Obj o, int on) { Obj g = go_of(o); if (g && (CALL(m_go_self, bool, Obj)(g, m_go_self) != 0) != (on != 0)) CALL(m_go_active, void, Obj, bool)(g, on != 0, m_go_active); }
static int active_self(Obj o) { Obj g = go_of(o); return g && CALL(m_go_self, bool, Obj)(g, m_go_self); }
static int on_tree(Obj o) { Obj g = go_of(o); return g && CALL(m_go_tree, bool, Obj)(g, m_go_tree); }
static Obj inst(Obj orig, Obj parent) { Obj p = tr_of(parent); return alive(orig) && p ? CALL(m_obj_inst, Obj, Obj, Obj)(orig, p, m_obj_inst) : NULL; }
static void destroy_obj(Obj o) { if (alive(o)) CALL(m_obj_destroy, void, Obj)(o, m_obj_destroy); }
static Obj comp_get(Obj any, void *k) { Obj g = go_of(any); return g && k ? CALL(m_comp_get, Obj, Obj, Obj)(tr_of(g), type_of(k), m_comp_get) : NULL; }
static Obj comp_add(Obj any, void *k) { Obj g = go_of(any); return g && k ? CALL(m_go_add, Obj, Obj, Obj)(g, type_of(k), m_go_add) : NULL; }
static Obj tr_parent(Obj t) { t = tr_of(t); return t ? CALL(m_tr_parent, Obj, Obj)(t, m_tr_parent) : NULL; }
static int child_count(Obj t) { t = tr_of(t); return t ? CALL(m_tr_count, int, Obj)(t, m_tr_count) : 0; }
static Obj child_at(Obj t, int i) { t = tr_of(t); return t ? CALL(m_tr_child, Obj, Obj, int)(t, i, m_tr_child) : NULL; }
static int sibling(Obj t) { t = tr_of(t); return t ? CALL(m_tr_sib, int, Obj)(t, m_tr_sib) : -1; }
static void set_sibling(Obj t, int i) { t = tr_of(t); if (t) CALL(m_tr_setsib, void, Obj, int)(t, i, m_tr_setsib); }
static void obj_name(Obj o, char *out, size_t cap) { utf8_of(alive(o) ? CALL(m_obj_name, Obj, Obj)(o, m_obj_name) : NULL, out, cap); }
static Obj find_obj(void *k, int inactive) { return k ? CALL(m_obj_find, Obj, Obj, bool)(type_of(k), inactive != 0, m_obj_find) : NULL; }
static Obj find_all(void *k) { return k && m_res_findall ? CALL(m_res_findall, Obj, Obj)(type_of(k), m_res_findall) : NULL; }
static int arr_n(Obj a) { return a ? (int)AT(a, 0x18, uintptr_t) : 0; }
static Obj arr_at(Obj a, int i) { return AT((char *)a + 0x20, (size_t)i * 8, Obj); }
static Col rgba_col(uint32_t c, Col dflt) { return c ? (Col){ (c >> 24) / 255.f, (c >> 16 & 255) / 255.f, (c >> 8 & 255) / 255.f, (c & 255) / 255.f } : dflt; }

/* RectTransform: top-left origin inside the parent, y down (the kit's coordinates) */
static void rt_set(Obj o, V2 amin, V2 amax, V2 pivot, V2 size, V2 pos) {
    Obj rt = tr_of(o); if (!isa(rt, K.RectTransform)) return;
    CALL(m_rt_amin, void, Obj, V2)(rt, amin, m_rt_amin); CALL(m_rt_amax, void, Obj, V2)(rt, amax, m_rt_amax);
    CALL(m_rt_pivot, void, Obj, V2)(rt, pivot, m_rt_pivot); CALL(m_rt_size, void, Obj, V2)(rt, size, m_rt_size);
    CALL(m_rt_pos, void, Obj, V2)(rt, pos, m_rt_pos);
}
static void place_tl(Obj o, float x, float y, float w, float h) { rt_set(o, (V2){ 0, 1 }, (V2){ 0, 1 }, (V2){ 0, 1 }, (V2){ w, h }, (V2){ x, -y }); }
static void stretch(Obj o) { rt_set(o, (V2){ 0, 0 }, (V2){ 1, 1 }, (V2){ 0.5f, 0.5f }, (V2){ 0, 0 }, (V2){ 0, 0 }); }
static V2 rt_size(Obj o) { Obj rt = tr_of(o); return isa(rt, K.RectTransform) ? CALL(m_rt_getsize, V2, Obj)(rt, m_rt_getsize) : (V2){ 0, 0 }; }
/* screen rect x0,y0,x1,y1 in pixels (overlay canvas: world = pixels; camera canvas: through its camera) */
static int rect_screen(Obj o, float r[4]) {
    Obj rt = tr_of(o); if (!isa(rt, K.RectTransform) || !K.Vector3) return 0;
    static uintptr_t h_arr; Obj arr = h_arr ? I.gchandle_get_target(h_arr) : NULL;
    if (!arr) { arr = I.array_new(K.Vector3, 4); if (!arr) return 0; h_arr = I.gchandle_new(arr, false); }
    CALL(m_rt_corners, void, Obj, Obj)(rt, arr, m_rt_corners);
    V3 *c = (V3 *)((char *)arr + 0x20);
    Obj cv = K.Canvas ? CALL(m_comp_inparent, Obj, Obj, Obj)(rt, type_of(K.Canvas), m_comp_inparent) : NULL;
    Obj root = alive(cv) && m_cv_root ? CALL(m_cv_root, Obj, Obj)(cv, m_cv_root) : NULL;
    Obj cam = alive(root) && m_cv_getmode && CALL(m_cv_getmode, int, Obj)(root, m_cv_getmode) != 0 && m_cv_cam ? CALL(m_cv_cam, Obj, Obj)(root, m_cv_cam) : NULL;
    for (int i = 0; i < 4; i++) if (alive(cam) && m_rtu_w2s) { V2 s = CALL(m_rtu_w2s, V2, Obj, V3)(cam, c[i], m_rtu_w2s); c[i].x = s.x; c[i].y = s.y; }
    r[0] = r[2] = c[0].x; r[1] = r[3] = c[0].y;
    for (int i = 1; i < 4; i++) { if (c[i].x < r[0]) r[0] = c[i].x; if (c[i].x > r[2]) r[2] = c[i].x; if (c[i].y < r[1]) r[1] = c[i].y; if (c[i].y > r[3]) r[3] = c[i].y; }
    return 1;
}

/* graphics (virtual setters through the object's own override) */
static void *vmi(Obj o, void *mi) { void *v = I.object_get_virtual_method ? I.object_get_virtual_method(o, mi) : NULL; return v ? v : mi; }
static void gr_color(Obj g, Col c) { if (isa(g, K.Graphic)) { void *m = vmi(g, m_gr_color); CALL(m, void, Obj, Col)(g, c, m); } }
static void gr_raycast(Obj g, int on) { if (isa(g, K.Graphic)) { void *m = vmi(g, m_gr_raycast); CALL(m, void, Obj, bool)(g, on != 0, m); } }
static void gr_material(Obj g, Obj mat) { if (isa(g, K.Graphic) && m_gr_material) { void *m = vmi(g, m_gr_material); CALL(m, void, Obj, Obj)(g, mat, m); } }
static Obj img_sprite(Obj im) { return isa(im, K.Image) ? CALL(m_img_getsprite, Obj, Obj)(im, m_img_getsprite) : NULL; }
static void img_set(Obj im, Obj sprite, Col c, int sliced) {          /* sprite + tint; never a click target */
    if (!isa(im, K.Image)) return;
    CALL(m_img_sprite, void, Obj, Obj)(im, sprite, m_img_sprite);
    if (m_img_type) CALL(m_img_type, void, Obj, int)(im, sprite && sliced ? 1 : 0, m_img_type);
    gr_color(im, c); gr_raycast(im, 0);
}
static Obj tmp_get(Obj t) { if (!isa(t, K.TMP_Text)) return NULL; void *m = vmi(t, m_tmp_get); return CALL(m, Obj, Obj)(t, m); }
static void tmp_set(Obj t, const char *s) {
    if (!isa(t, K.TMP_Text)) return;
    if (f_das_usekey >= 0 && isa(t, K.DASText)) AT(t, f_das_usekey, bool) = false;   /* DASText: keep our raw text (no text key) */
    void *m = vmi(t, m_tmp_set); CALL(m, void, Obj, Obj)(t, str(s), m);
}

/* ------------------------------------------------------------------ stock look: sprites, fonts, templates */
static const Col C_MAIN = { 0.8f, 0.745f, 0.722f, 1 }, C_SUB = { 0.651f, 0.565f, 0.522f, 1 }, C_WHITE = { 1, 1, 1, 1 },
                 C_ORANGE = { 0.918f, 0.624f, 0.239f, 1 }, C_ROW1 = { 0.212f, 0.196f, 0.227f, 1 }, C_BOXF = { 0.133f, 0.125f, 0.145f, 1 },
                 C_GREY = { 0.45f, 0.42f, 0.45f, 1 }, C_FIELD = { 0.6f, 0.58f, 0.6f, 1 };
enum { S_BOX, S_NBTN, S_CHECK, S_TEXTBOX, S_N };
static const char *const spr_names[S_N] = { "Box", "Button_Normal", "CheckBox_Selected", "TextBox" };
static uintptr_t h_spr[S_N], h_cinzel, h_body_font, h_body_mat;
static int64_t frames, scan_last = -100000; static int scans, scan_done, kit_used;
static Obj ui_prefab(int type);
static void prefab_touch(int type) { (void)ui_prefab(type); }
static void assets_scan(int force) {
    if (scan_done || (!force && (!kit_used || frames - scan_last < 300 || scans >= 30))) return;
    scan_last = frames; scans++;
    prefab_touch(8); prefab_touch(2); prefab_touch(13); prefab_touch(5);   /* load popup prefabs: their sprites too (as mpmenu) */
    Obj arr = find_all(K.Sprite); int n = arr_n(arr), found = 0;
    for (int i = 0; i < n; i++) {
        Obj sp = arr_at(arr, i); char nm[64]; obj_name(sp, nm, sizeof nm);
        for (int k = 0; k < S_N; k++) if (!hget(h_spr[k]) && !strcmp(nm, spr_names[k])) { hset(&h_spr[k], sp); found++; }
    }
    if (!hget(h_cinzel)) {
        Obj fa = find_all(K.FontAsset); int nf = arr_n(fa);
        for (int i = 0; i < nf; i++) { Obj f = arr_at(fa, i); char nm[64]; obj_name(f, nm, sizeof nm); if (!strcmp(nm, "Cinzel-Bold SDF")) { hset(&h_cinzel, f); break; } }
    }
    int miss = 0; char ms[160] = ""; size_t o = 0;
    for (int k = 0; k < S_N; k++) if (!hget(h_spr[k])) { miss++; o += (size_t)snprintf(ms + o, o < sizeof ms ? sizeof ms - o : 0, " %s", spr_names[k]); }
    if (!hget(h_cinzel)) { miss++; snprintf(ms + o, o < sizeof ms ? sizeof ms - o : 0, " Cinzel"); }
    dsl_log("ui: assets scan %d (%d sprites loaded): %d found, missing%s", scans, n, found, miss ? ms : " none");
    if (!miss) scan_done = 1;
}
static Obj spr(int k) { Obj s = hget(h_spr[k]); if (!s && h_spr[k]) { hfree(&h_spr[k]); scan_done = 0; } return s; }

static Obj notice_view(void);
/* body text: the stock window body font (CommonNoticeView Txt_Notice) with its font asset's own plain material
   (no outline), as mpmenu's txt_body */
static void body_font(void) {
    if (hget(h_body_mat)) return;
    static int64_t last = -1000; if (frames - last < 120) return; last = frames;
    Obj v = notice_view(); Obj nt = v && f_nv_text >= 0 ? AT(v, f_nv_text, Obj) : NULL;
    Obj font = isa(nt, K.TMP_Text) ? CALL(m_tmp_getfont, Obj, Obj)(nt, m_tmp_getfont) : NULL;
    Obj m = !alive(font) ? NULL : m_asset_mat ? CALL(m_asset_mat, Obj, Obj)(font, m_asset_mat) : f_asset_mat > 0 ? AT(font, f_asset_mat, Obj) : NULL;
    if (alive(m)) { hset(&h_body_mat, m); hset(&h_body_font, font); char nm[96]; obj_name(m, nm, sizeof nm); dsl_log("ui: body text font found, material \"%s\"", nm); }
}
static void txt_body(Obj t) {
    if (!isa(t, K.TMP_Text)) return;
    body_font();
    Obj f = hget(h_body_font), m = hget(h_body_mat);
    if (f) CALL(m_tmp_font, void, Obj, Obj)(t, f, m_tmp_font);
    if (m && m_tmp_shmat) CALL(m_tmp_shmat, void, Obj, Obj)(t, m, m_tmp_shmat);
}
static void txt_style(Obj t, int cinzel, float size, int align, int wrap) {
    if (!isa(t, K.TMP_Text)) return;
    Obj f = cinzel ? hget(h_cinzel) : NULL;
    if (f) CALL(m_tmp_font, void, Obj, Obj)(t, f, m_tmp_font);
    CALL(m_tmp_auto, void, Obj, bool)(t, false, m_tmp_auto);
    CALL(m_tmp_size, void, Obj, float)(t, size, m_tmp_size);
    CALL(m_tmp_align, void, Obj, int)(t, align, m_tmp_align);         /* 513 middle-left, 514 centre, 516 middle-right, 257.. top */
    CALL(m_tmp_wrap, void, Obj, int)(t, wrap, m_tmp_wrap);             /* TextWrappingModes 0 none, 1 normal */
    CALL(m_tmp_overflow, void, Obj, int)(t, 0, m_tmp_overflow);
    CALL(m_tmp_rich, void, Obj, bool)(t, true, m_tmp_rich);
    gr_raycast(t, 0);
}

/* templates: kept under one inactive DontDestroyOnLoad holder, so the kit works in every scene once the title was seen.
   button = clone of TitleMenuView._creditButton (SubUI_TitleMenuButton), image = its background Image, text = its label,
   field = the load list's save-name TMP_InputField (UIFactory prefab of popup 5), else the new-campaign name field,
   else any loaded TMP_InputField. */
static uintptr_t h_holder, h_tpl_btn, h_tpl_img, h_tpl_txt, h_tpl_field, h_layer;
static Obj new_go(const char *name, Obj parent) {
    if (!K.GameObject || !m_go_ctor) return NULL;
    Obj g = I.object_new(K.GameObject); if (!g) return NULL;
    CALL(m_go_ctor, void, Obj, Obj)(g, str(name), m_go_ctor);
    if (parent && m_tr_setparent) CALL(m_tr_setparent, void, Obj, Obj, bool)(CALL(m_go_tr, Obj, Obj)(g, m_go_tr), tr_of(parent), false, m_tr_setparent);
    if (m_go_layer) CALL(m_go_layer, void, Obj, int)(g, 5, m_go_layer);   /* UI */
    return g;
}
static Obj new_rect(const char *name, Obj parent) {                  /* an empty RectTransform under parent */
    Obj g = new_go(name, NULL); if (!g) return NULL;
    Obj rt = comp_add(g, K.RectTransform); if (!rt) rt = tr_of(g);
    if (parent && m_tr_setparent) CALL(m_tr_setparent, void, Obj, Obj, bool)(rt, tr_of(parent), false, m_tr_setparent);
    return isa(rt, K.RectTransform) ? rt : NULL;
}
static Obj holder(void) {
    Obj h = hget(h_holder); if (h) return h;
    Obj g = new_go("dsl_ui_templates", NULL); if (!g) return NULL;
    CALL(m_go_active, void, Obj, bool)(g, false, m_go_active);
    if (m_obj_dontdestroy) CALL(m_obj_dontdestroy, void, Obj)(g, m_obj_dontdestroy);
    hset(&h_holder, g); return g;
}
static Obj first_image(Obj tr, int depth) {                          /* the button's background (its root has none) */
    Obj im = comp_get(tr, K.Image); if (im) return im;
    if (depth >= 3) return NULL;
    for (int i = 0, n = child_count(tr); i < n; i++) { Obj c = first_image(child_at(tr, i), depth + 1); if (c) return c; }
    return NULL;
}
static Obj btn_label(Obj b) { return f_tb_label >= 0 && isa(b, K.TitleButton) ? AT(b, f_tb_label, Obj) : NULL; }
static Obj ui_prefab(int type) {                                     /* UIFactory._prefabs.GetUIPrefab(PopUpType) */
    if (!m_ui_prefab || f_uif_prefabs < 0) return NULL;
    static uintptr_t h; Obj f = hget(h); if (!f) { f = find_obj(K.UIFactory, 1); hset(&h, f); }
    Obj so = f ? AT(f, f_uif_prefabs, Obj) : NULL;
    return so ? CALL(m_ui_prefab, Obj, Obj, int)(so, type, m_ui_prefab) : NULL;
}
static Obj field_source(const char **from) {
    Obj p = ui_prefab(5), f = p && f_slv_field >= 0 && isa(p, K.SaveLoadView) ? AT(p, f_slv_field, Obj) : NULL;
    *from = "the load list's save-name field (popup prefab 5)";
    if (!isa(f, K.TMP_Input)) {
        Obj a = find_all(K.SaveLoadView); for (int i = 0; i < arr_n(a) && !isa(f, K.TMP_Input); i++) f = f_slv_field >= 0 ? AT(arr_at(a, i), f_slv_field, Obj) : NULL;
        *from = "a SaveLoadView's save-name field";
    }
    if (!isa(f, K.TMP_Input)) {
        Obj a = find_all(K.CampaignSetting); for (int i = 0; i < arr_n(a) && !isa(f, K.TMP_Input); i++) f = f_cs_field >= 0 ? AT(arr_at(a, i), f_cs_field, Obj) : NULL;
        *from = "the new-campaign name field";
    }
    if (!isa(f, K.TMP_Input)) { Obj a = find_all(K.TMP_Input); f = arr_n(a) ? arr_at(a, 0) : NULL; *from = "a loaded TMP_InputField"; }
    return isa(f, K.TMP_Input) ? f : NULL;
}
static Obj title_view(void);
static int templates_ready(void) {
    if (hget(h_tpl_btn) && hget(h_tpl_img) && hget(h_tpl_txt)) return 1;
    Obj v = title_view(); if (!v || f_tv_credit < 0) return 0;
    Obj src = AT(v, f_tv_credit, Obj), hd = holder();
    if (!isa(src, K.TitleButton) || !hd) return 0;
    assets_scan(1);
    Obj b = inst(src, hd); if (!b) return 0;
    hset(&h_tpl_btn, b);
    Obj l = btn_label(b); if (isa(l, K.DASText) && f_das_usekey >= 0) AT(l, f_das_usekey, bool) = false;
    Obj bg = first_image(tr_of(src), 0), im = bg ? inst(go_of(bg), hd) : NULL;
    Obj imc = im ? comp_get(im, K.Image) : NULL;
    for (int i = child_count(imc) - 1; i >= 0; i--) destroy_obj(go_of(child_at(imc, i)));   /* a plain image: no children */
    hset(&h_tpl_img, imc);
    Obj lt = btn_label(src), t = lt ? inst(go_of(lt), hd) : NULL;
    hset(&h_tpl_txt, t ? comp_get(t, K.TMP_Text) : NULL);
    dsl_log("ui: templates from the title screen: button %s, image %s (%d children removed), text %s",
            hget(h_tpl_btn) ? "ok" : "MISSING", hget(h_tpl_img) ? "ok" : "MISSING", imc ? child_count(imc) : -1, hget(h_tpl_txt) ? "ok" : "MISSING");
    return hget(h_tpl_btn) && hget(h_tpl_img) && hget(h_tpl_txt);
}
static Obj field_tpl(void) {
    Obj f = hget(h_tpl_field); if (f) return f;
    static int64_t last = -1000; if (frames - last < 60) return NULL; last = frames;
    const char *from; Obj src = field_source(&from); Obj hd = holder();
    if (!src || !hd) { dsl_log("ui: no TMP_InputField loaded yet for text fields"); return NULL; }
    Obj c = inst(go_of(src), hd); Obj ci = c ? comp_get(c, K.TMP_Input) : NULL;
    hset(&h_tpl_field, ci);
    dsl_log("ui: text field template from %s: %s", from, ci ? "ok" : "FAILED");
    return ci;
}

/* the kit's own screen layer (parent NULL): a screen-space overlay canvas with the game UI's scaler settings,
   kept across scenes; sorting 30900: under the multiplayer panels (31000+) and the tooltip / notice (raised to 32040+) */
static Obj layer(void) {
    Obj l = hget(h_layer); if (l) return l;
    if (!K.Canvas || !m_cv_mode) return NULL;
    Obj g = new_go("dsl_ui_layer", NULL); if (!g) return NULL;
    if (m_obj_dontdestroy) CALL(m_obj_dontdestroy, void, Obj)(g, m_obj_dontdestroy);
    Obj cv = comp_add(g, K.Canvas);
    if (!cv) return NULL;
    CALL(m_cv_mode, void, Obj, int)(cv, 0, m_cv_mode);
    if (m_cv_order) CALL(m_cv_order, void, Obj, int)(cv, 30900, m_cv_order);
    Obj cs = comp_add(g, K.CanvasScaler);
    /* the game's scaler: the root canvas above the title view (or the notice view in a world) */
    Obj ref = title_view(); if (!ref) ref = notice_view();
    Obj gcv = ref ? CALL(m_comp_inparent, Obj, Obj, Obj)(tr_of(ref), type_of(K.Canvas), m_comp_inparent) : NULL;
    Obj groot = alive(gcv) && m_cv_root ? CALL(m_cv_root, Obj, Obj)(gcv, m_cv_root) : NULL;
    Obj gcs = groot ? comp_get(groot, K.CanvasScaler) : NULL;
    if (cs && m_cs_mode) {
        if (gcs && m_cs_getmode && m_cs_getref && m_cs_getmatch && m_cs_getsmm) {
            CALL(m_cs_mode, void, Obj, int)(cs, CALL(m_cs_getmode, int, Obj)(gcs, m_cs_getmode), m_cs_mode);
            CALL(m_cs_ref, void, Obj, V2)(cs, CALL(m_cs_getref, V2, Obj)(gcs, m_cs_getref), m_cs_ref);
            CALL(m_cs_smm, void, Obj, int)(cs, CALL(m_cs_getsmm, int, Obj)(gcs, m_cs_getsmm), m_cs_smm);
            CALL(m_cs_match, void, Obj, float)(cs, CALL(m_cs_getmatch, float, Obj)(gcs, m_cs_getmatch), m_cs_match);
            V2 r = CALL(m_cs_getref, V2, Obj)(gcs, m_cs_getref);
            dsl_log("ui: screen layer: scaler copied from the game UI (mode %d, reference %.0fx%.0f)", CALL(m_cs_getmode, int, Obj)(gcs, m_cs_getmode), r.x, r.y);
        } else {
            CALL(m_cs_mode, void, Obj, int)(cs, 1, m_cs_mode); CALL(m_cs_ref, void, Obj, V2)(cs, (V2){ 1920, 1080 }, m_cs_ref);
            CALL(m_cs_match, void, Obj, float)(cs, 0.5f, m_cs_match);
            dsl_log("ui: screen layer: no game scaler found, 1920x1080 match 0.5");
        }
    }
    comp_add(g, K.GraphicRaycaster);
    Obj rt = tr_of(g);
    hset(&h_layer, rt); return rt;
}
/* a nested override-sorting canvas order (stock TooltipUI 10020; our layer 30900): raise a stock view above it */
static void raise_view(Obj view, int order) {
    Obj cv = view && K.Canvas ? CALL(m_comp_inparent, Obj, Obj, Obj)(tr_of(view), type_of(K.Canvas), m_comp_inparent) : NULL;
    if (!alive(cv) || !m_cv_override || !m_cv_order) return;
    Obj root = m_cv_root ? CALL(m_cv_root, Obj, Obj)(cv, m_cv_root) : NULL;
    if (root == cv) {                                                /* the view sits in the root canvas: give it its own */
        Obj g = go_of(view); cv = comp_get(g, K.Canvas); if (!cv) cv = comp_add(g, K.Canvas);
        if (cv && !comp_get(g, K.GraphicRaycaster)) comp_add(g, K.GraphicRaycaster);
        if (!cv) return;
    }
    CALL(m_cv_override, void, Obj, bool)(cv, true, m_cv_override);
    CALL(m_cv_order, void, Obj, int)(cv, order, m_cv_order);
}

/* ------------------------------------------------------------------ crafted System.Action (no hook needed)
   il2cpp invokes an Action inline as invoke_impl(invoke_impl_this, method): x2=[d+0x18], x0=[d+0x40], x1=[d+0x28]
   (SubUI_TitleMenuButton.OnPointerClick, CommonNoticeView.OnClick_Confirm). Runtime delegate layout, same on every
   il2cpp build of this Unity version. The C function only queues; the work runs in the frame tick. */
static char fake_method[64];                                         /* "any non-null" MethodInfo slot */
enum { P_CLICK = 1, P_PREV, P_NEXT, P_OK, P_CANCEL, P_TITLE };
#define QMAX 64
static uint32_t queue[QMAX]; static int nq;
static void act_fn(void *ctx, const void *mi) { (void)mi; if (nq < QMAX) queue[nq++] = (uint32_t)(uintptr_t)ctx; }
static Obj mk_action(uint32_t token) {
    if (!K.Action) return NULL;
    Obj d = I.object_new(K.Action); if (!d) return NULL;
    AT(d, 0x10, void *) = (void *)act_fn;                            /* method_ptr */
    AT(d, 0x18, void *) = (void *)act_fn;                            /* invoke_impl */
    AT(d, 0x28, void *) = fake_method;                               /* method */
    AT(d, 0x40, void *) = (void *)(uintptr_t)token;                  /* invoke_impl_this = our token */
    return d;
}
#define TOKEN(id, part) ((uint32_t)(id) << 4 | (uint32_t)(part))

/* ------------------------------------------------------------------ widget registry */
enum { W_PANEL = 1, W_TEXT, W_BUTTON, W_TOGGLE, W_VALUE, W_FIELD, W_SCROLL, W_IMAGE };
typedef struct W {
    int id, kind, enabled, on, index, n, was_focused;
    Obj rt;                                                         /* the DSLObj handed out (held by h_rt) */
    uintptr_t h_rt, h_root, h_btn, h_label, h_a, h_b, h_c, h_act[3];
    char **values;
    dsl_ui_cb cb; void *ud;
    float w, h;
} W;
#define WMAX 1024
static W *ws[WMAX]; static int nws, next_id = 1;
static W *w_new(int kind) {
    if (nws >= WMAX) { dsl_log("ui: more than %d live widgets: refused", WMAX); return NULL; }
    W *w = calloc(1, sizeof *w); w->id = next_id++; w->kind = kind; w->enabled = 1; ws[nws++] = w; return w;
}
static void w_free(int i) {
    W *w = ws[i];
    uintptr_t *hs[] = { &w->h_rt, &w->h_root, &w->h_btn, &w->h_label, &w->h_a, &w->h_b, &w->h_c, &w->h_act[0], &w->h_act[1], &w->h_act[2] };
    for (size_t k = 0; k < sizeof hs / sizeof *hs; k++) hfree(hs[k]);
    if (w->values) { for (int k = 0; k < w->n; k++) free(w->values[k]); free(w->values); }
    free(w); ws[i] = ws[--nws];
}
static W *w_of(DSLObj o) { if (!o) return NULL; for (int i = 0; i < nws; i++) if (ws[i]->rt == o) return ws[i]; return NULL; }
static W *w_by_id(int id) { for (int i = 0; i < nws; i++) if (ws[i]->id == id) return ws[i]; return NULL; }
static Obj w_root(W *w) { Obj r = hget(w->h_root); return r ? r : hget(w->h_rt); }
static DSLObj w_done(W *w, Obj rt, Obj root) {
    w->rt = rt; w->h_rt = hnew(rt); if (root && root != rt) w->h_root = hnew(root);
    return rt;
}
static void w_sweep(void) {                                          /* destroyed with a parent or a scene: forget */
    for (int i = nws - 1; i >= 0; i--) if (!hget(ws[i]->h_rt)) w_free(i);
}

/* tooltips: target -> title / body (any RectTransform, kit widget or not) */
typedef struct { Obj target; uintptr_t h; char title[160], body[700]; } TipE;
#define TMAX 256
static TipE tips[TMAX]; static int ntips;

static Obj parent_or_layer(DSLObj p) { Obj t = p ? tr_of((Obj)p) : layer(); return t; }
static int ready_basic(void) { kit_used = 1; return templates_ready(); }

/* ------------------------------------------------------------------ widgets */
static DSLObj k_panel(DSLObj parent, float x, float y, float w, float h, uint32_t rgba) {
    if (!ready_basic()) return NULL;
    Obj p = parent_or_layer(parent); Obj im = p ? inst(hget(h_tpl_img), p) : NULL;
    if (!im) return NULL;
    go_active(im, 1);
    Obj b = spr(S_BOX);
    img_set(im, b, b ? rgba_col(rgba, C_WHITE) : rgba_col(rgba, C_BOXF), 1);
    gr_material(im, NULL);
    gr_raycast(im, 1);                                               /* a panel blocks clicks to what is under it */
    place_tl(im, x, y, w, h);
    W *wd = w_new(W_PANEL); if (!wd) { destroy_obj(go_of(im)); return NULL; }
    return w_done(wd, tr_of(im), NULL);
}
static int txt_align(int flags) { int a = flags & 3; int wrap = flags & DSL_TXT_WRAP; return (wrap ? 256 : 512) + (a == DSL_TXT_CENTER ? 2 : a == DSL_TXT_RIGHT ? 4 : 1); }
static Obj mk_text(Obj parent, float x, float y, float w, float h, const char *s, float size, int flags, Col c) {
    Obj t = inst(hget(h_tpl_txt), parent); if (!t) return NULL;
    go_active(t, 1);
    Obj tc = comp_get(t, K.TMP_Text);
    if (!(flags & DSL_TXT_TITLE)) txt_body(tc);
    txt_style(tc, flags & DSL_TXT_TITLE, size > 0 ? size : 16, txt_align(flags), flags & DSL_TXT_WRAP ? 1 : 0);
    gr_color(tc, c); tmp_set(tc, s);
    place_tl(tc, x, y, w, h);
    return tc;
}
static DSLObj k_text(DSLObj parent, float x, float y, float w, float h, const char *s, float size, int flags, uint32_t rgba) {
    if (!ready_basic()) return NULL;
    Obj p = parent_or_layer(parent); Obj t = p ? mk_text(p, x, y, w, h, s, size, flags, rgba_col(rgba, C_MAIN)) : NULL;
    if (!t) return NULL;
    W *wd = w_new(W_TEXT); if (!wd) { destroy_obj(go_of(t)); return NULL; }
    hset(&wd->h_label, t);
    return w_done(wd, tr_of(t), NULL);
}
/* a clickable clone of the title menu button: its own frame (Button_Normal), label centred, crafted Action */
static Obj mk_btn(Obj parent, float x, float y, float w, float h, const char *label, float fs, uint32_t token, uintptr_t *h_act) {
    Obj b = inst(hget(h_tpl_btn), parent); if (!b) return NULL;
    Obj bc = comp_get(b, K.TitleButton); if (!bc) { destroy_obj(go_of(b)); return NULL; }
    go_active(bc, 1);
    Obj a = mk_action(token);
    if (a && m_tb_init) CALL(m_tb_init, void, Obj, Obj)(bc, a, m_tb_init);
    if (h_act) hset(h_act, a);
    Obj bg = first_image(tr_of(bc), 0);
    if (bg) { stretch(bg); gr_raycast(bg, 1); }                      /* the frame is the click target */
    Obj l = btn_label(bc);
    if (isa(l, K.TMP_Text)) { txt_body(l); txt_style(l, 0, fs, 514, 0); gr_color(l, C_MAIN); stretch(l); tmp_set(l, label); }
    place_tl(bc, x, y, w, h);
    return bc;
}
static float btn_fs(float h) { return h >= 44 ? 18 : h >= 30 ? 16 : 14; }
static DSLObj k_button(DSLObj parent, float x, float y, float w, float h, const char *label, dsl_ui_cb click, void *ud) {
    if (!ready_basic()) return NULL;
    Obj p = parent_or_layer(parent); if (!p) return NULL;
    W *wd = w_new(W_BUTTON); if (!wd) return NULL;
    Obj b = mk_btn(p, x, y, w, h, label, btn_fs(h), TOKEN(wd->id, P_CLICK), &wd->h_act[0]);
    if (!b) { w_free(nws - 1); return NULL; }
    wd->cb = click; wd->ud = ud; wd->w = w; wd->h = h;
    hset(&wd->h_btn, b); hset(&wd->h_label, btn_label(b));
    return w_done(wd, tr_of(b), NULL);
}

/* toggle row: [label ............ [x]] (the button's frame is the row; check box at the right) */
#define TOGGLE_H 36
static void toggle_look(W *w) {
    Obj ck = hget(w->h_b), box = hget(w->h_a);
    if (ck) { Obj s = spr(S_CHECK); img_set(ck, s, s ? C_WHITE : C_ORANGE, 0); go_active(ck, w->on); }
    if (box) { Obj s = spr(S_TEXTBOX); img_set(box, s, s ? C_WHITE : C_BOXF, 1); }
}
static void toggle_layout(W *w) {
    Obj l = hget(w->h_label), box = hget(w->h_a), ck = hget(w->h_b);
    if (l) { place_tl(l, 12, 0, w->w - 60, TOGGLE_H); txt_style(l, 0, 16, 513, 0); }
    if (box) place_tl(box, w->w - 36, (TOGGLE_H - 24) / 2.f, 24, 24);
    if (ck) place_tl(ck, 3, 3, 18, 18);
}
static DSLObj k_toggle(DSLObj parent, float x, float y, float w, const char *label, int on, dsl_ui_cb change, void *ud) {
    if (!ready_basic()) return NULL;
    Obj p = parent_or_layer(parent); if (!p) return NULL;
    W *wd = w_new(W_TOGGLE); if (!wd) return NULL;
    Obj b = mk_btn(p, x, y, w, TOGGLE_H, label, 16, TOKEN(wd->id, P_CLICK), &wd->h_act[0]);
    if (!b) { w_free(nws - 1); return NULL; }
    Obj box = inst(hget(h_tpl_img), b); go_active(box, 1);
    Obj ck = box ? inst(hget(h_tpl_img), box) : NULL; go_active(ck, 1);
    hset(&wd->h_btn, b); hset(&wd->h_label, btn_label(b)); hset(&wd->h_a, box); hset(&wd->h_b, ck);
    wd->on = on != 0; wd->cb = change; wd->ud = ud; wd->w = w; wd->h = TOGGLE_H;
    toggle_layout(wd); toggle_look(wd);
    return w_done(wd, tr_of(b), NULL);
}
static int k_toggle_get(DSLObj t) { W *w = w_of(t); return w && w->kind == W_TOGGLE ? w->on : 0; }
static void k_toggle_set(DSLObj t, int on) { W *w = w_of(t); if (w && w->kind == W_TOGGLE) { w->on = on != 0; toggle_look(w); } }

/* value row (multiplayer Rules rows): [label   < value >] */
#define VALUE_H 44
static void value_text(W *w) { Obj v = hget(w->h_c); if (v && w->n > 0) tmp_set(v, w->values[w->index]); }
static void value_layout(W *w) {
    float lw = w->w * 0.36f, ax = lw, bx = w->w - 12 - 28;
    place_tl(hget(w->h_label), 12, 0, lw - 16, VALUE_H);
    place_tl(hget(w->h_a), ax, (VALUE_H - 28) / 2.f, 28, 28);
    place_tl(hget(w->h_b), bx, (VALUE_H - 28) / 2.f, 28, 28);
    place_tl(hget(w->h_c), ax + 28, 0, bx - ax - 28, VALUE_H);
}
static DSLObj k_value_row(DSLObj parent, float x, float y, float w, const char *label, const char *const *values, int n,
                          int index, dsl_ui_cb change, void *ud) {
    if (!ready_basic() || n <= 0 || !values) return NULL;
    Obj p = parent_or_layer(parent); if (!p) return NULL;
    Obj row = inst(hget(h_tpl_img), p); if (!row) return NULL;
    go_active(row, 1); img_set(row, NULL, C_ROW1, 0); gr_material(row, NULL);
    place_tl(row, x, y, w, VALUE_H);
    W *wd = w_new(W_VALUE); if (!wd) { destroy_obj(go_of(row)); return NULL; }
    Obj lb = mk_text(row, 0, 0, 10, 10, label, 15, DSL_TXT_LEFT, C_SUB);
    Obj vt = mk_text(row, 0, 0, 10, 10, "", 16, DSL_TXT_CENTER, C_MAIN);
    Obj pb = mk_btn(row, 0, 0, 28, 28, "\xE2\x80\xB9", 16, TOKEN(wd->id, P_PREV), &wd->h_act[0]);
    Obj nb = mk_btn(row, 0, 0, 28, 28, "\xE2\x80\xBA", 16, TOKEN(wd->id, P_NEXT), &wd->h_act[1]);
    hset(&wd->h_label, lb); hset(&wd->h_c, vt); hset(&wd->h_a, pb); hset(&wd->h_b, nb);
    wd->values = calloc((size_t)n, sizeof *wd->values);
    for (int i = 0; i < n; i++) wd->values[i] = strdup(values[i] ? values[i] : "");
    wd->n = n; wd->index = index < 0 ? 0 : index >= n ? n - 1 : index; wd->cb = change; wd->ud = ud; wd->w = w; wd->h = VALUE_H;
    value_layout(wd); value_text(wd);
    return w_done(wd, tr_of(row), NULL);
}
static int k_value_get(DSLObj v) { W *w = w_of(v); return w && w->kind == W_VALUE ? w->index : -1; }
static void k_value_set(DSLObj v, int index) { W *w = w_of(v); if (w && w->kind == W_VALUE && index >= 0 && index < w->n) { w->index = index; value_text(w); } }

/* text field: a clone of a stock TMP_InputField, framed like a menu button (no magnifier), as mpmenu field_make */
static void in_settext(Obj f, const char *s) { if (isa(f, K.TMP_Input)) CALL(m_in_settext, void, Obj, Obj)(f, str(s), m_in_settext); }
static DSLObj k_field(DSLObj parent, float x, float y, float w, float h, const char *placeholder, const char *text, dsl_ui_cb submit, void *ud) {
    if (!ready_basic()) return NULL;
    Obj src = field_tpl(); Obj p = parent_or_layer(parent);
    if (!src || !p) return NULL;
    Obj f = inst(src, p); Obj fc = f ? comp_get(f, K.TMP_Input) : NULL;
    if (!fc) { if (f) destroy_obj(go_of(f)); return NULL; }
    go_active(fc, 1);
    Obj fim = comp_get(fc, K.Image), bim = first_image(tr_of(hget(h_tpl_btn)), 0), bs = img_sprite(bim);
    if (fim && bs) { CALL(m_img_sprite, void, Obj, Obj)(fim, bs, m_img_sprite); gr_color(fim, C_FIELD); }
    float fs = h >= 40 ? 18 : 16;
    Obj tc = m_in_tc ? CALL(m_in_tc, Obj, Obj)(fc, m_in_tc) : NULL;
    if (isa(tc, K.TMP_Text)) { txt_body(tc); txt_style(tc, 0, fs, 513, 0); gr_color(tc, C_MAIN); }
    if (m_in_limit) CALL(m_in_limit, void, Obj, int)(fc, 256, m_in_limit);
    if (m_in_ctype) CALL(m_in_ctype, void, Obj, int)(fc, 0, m_in_ctype);   /* Standard */
    Obj ph = m_in_ph ? CALL(m_in_ph, Obj, Obj)(fc, m_in_ph) : NULL;
    if (isa(ph, K.TMP_Text)) { tmp_set(ph, placeholder ? placeholder : ""); txt_body(ph); txt_style(ph, 0, fs - 2, 513, 0); gr_color(ph, C_SUB); }
    in_settext(fc, text ? text : "");
    place_tl(fc, x, y, w, h);
    W *wd = w_new(W_FIELD); if (!wd) { destroy_obj(go_of(fc)); return NULL; }
    hset(&wd->h_a, fc); wd->cb = submit; wd->ud = ud; wd->w = w; wd->h = h;
    return w_done(wd, tr_of(fc), NULL);
}
static size_t k_field_get(DSLObj f, char *out, size_t cap) {
    W *w = w_of(f); Obj fc = w && w->kind == W_FIELD ? hget(w->h_a) : NULL;
    Obj s = fc ? CALL(m_in_text, Obj, Obj)(fc, m_in_text) : NULL;
    if (out && cap) utf8_of(s, out, cap);
    return s ? utf8_len_of(s) : 0;
}
static void k_field_set(DSLObj f, const char *s) { W *w = w_of(f); if (w && w->kind == W_FIELD) in_settext(hget(w->h_a), s); }

/* scroll list: viewport (RectMask2D + ScrollRect, mouse wheel) with a content RectTransform anchored top-left */
static DSLObj k_scroll(DSLObj parent, float x, float y, float w, float h) {
    if (!ready_basic() || !K.ScrollRect || !m_sr_content) return NULL;
    Obj p = parent_or_layer(parent); if (!p) return NULL;
    Obj vp = inst(hget(h_tpl_img), p); if (!vp) return NULL;
    go_active(vp, 1); img_set(vp, NULL, (Col){ 0, 0, 0, 0.004f }, 0); gr_material(vp, NULL);
    gr_raycast(vp, 1);                                               /* the wheel needs a raycast target */
    place_tl(vp, x, y, w, h);
    comp_add(vp, K.RectMask2D);
    Obj sr = comp_add(vp, K.ScrollRect);
    Obj ct = new_rect("dsl_scroll_content", vp);
    if (!sr || !ct) { destroy_obj(go_of(vp)); return NULL; }
    place_tl(ct, 0, 0, w, h);
    CALL(m_sr_content, void, Obj, Obj)(sr, ct, m_sr_content);
    if (m_sr_viewport) CALL(m_sr_viewport, void, Obj, Obj)(sr, tr_of(vp), m_sr_viewport);
    if (m_sr_h) CALL(m_sr_h, void, Obj, bool)(sr, false, m_sr_h);
    if (m_sr_v) CALL(m_sr_v, void, Obj, bool)(sr, true, m_sr_v);
    if (m_sr_move) CALL(m_sr_move, void, Obj, int)(sr, 2, m_sr_move);   /* Clamped */
    if (m_sr_sens) CALL(m_sr_sens, void, Obj, float)(sr, 30, m_sr_sens);
    W *wd = w_new(W_SCROLL); if (!wd) { destroy_obj(go_of(vp)); return NULL; }
    hset(&wd->h_a, sr); wd->w = w; wd->h = h;
    return w_done(wd, ct, tr_of(vp));
}
static void k_scroll_height(DSLObj content, float h) {
    W *w = w_of(content); Obj ct = w && w->kind == W_SCROLL ? hget(w->h_rt) : NULL;
    if (!ct) return;
    V2 s = rt_size(ct); CALL(m_rt_size, void, Obj, V2)(ct, (V2){ s.x, h < w->h ? w->h : h }, m_rt_size);
}

/* PNG image: Texture2D + ImageConversion.LoadImage + Apply, filter Point when nearest, Sprite.Create, the default UI
   material (a template's material drew nothing with a runtime texture: mpmenu cog_built notes) */
static DSLObj k_image_png(DSLObj parent, float x, float y, float w, float h, const char *png_path, int nearest) {
    if (!ready_basic() || !png_path || !m_tex_ctor || !m_tex_load || !m_spr_create || !K.Byte) return NULL;
    FILE *fp = fopen(png_path, "rb"); if (!fp) { dsl_log("ui: image_png: cannot read %s", png_path); return NULL; }
    fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (n <= 0 || n > (32 << 20)) { fclose(fp); return NULL; }
    Obj bytes = I.array_new(K.Byte, (size_t)n);
    size_t got = bytes ? fread((char *)bytes + 0x20, 1, (size_t)n, fp) : 0; fclose(fp);
    if (got != (size_t)n) return NULL;
    Obj tex = I.object_new(K.Texture2D); if (!tex) return NULL;
    CALL(m_tex_ctor, void, Obj, int, int, int, bool)(tex, 2, 2, 4, false, m_tex_ctor);     /* RGBA32, no mips */
    bool ok = CALL(m_tex_load, bool, Obj, Obj)(tex, bytes, m_tex_load);
    if (!ok) { dsl_log("ui: image_png: %s is not a PNG the game can load", png_path); return NULL; }
    if (m_tex_filter) CALL(m_tex_filter, void, Obj, int)(tex, nearest ? 0 : 1, m_tex_filter);   /* Point / Bilinear */
    if (m_tex_wrap) CALL(m_tex_wrap, void, Obj, int)(tex, 1, m_tex_wrap);                      /* Clamp */
    if (m_tex_apply) CALL(m_tex_apply, void, Obj)(tex, m_tex_apply);
    int tw = m_tex_w ? CALL(m_tex_w, int, Obj)(tex, m_tex_w) : 0, th = m_tex_h ? CALL(m_tex_h, int, Obj)(tex, m_tex_h) : 0;
    Obj sp = tw > 0 && th > 0 ? CALL(m_spr_create, Obj, Obj, R4, V2, float)(tex, (R4){ 0, 0, (float)tw, (float)th }, (V2){ 0.5f, 0.5f }, 100.0f, m_spr_create) : NULL;
    if (!alive(sp)) { dsl_log("ui: image_png: Sprite.Create failed for %s", png_path); return NULL; }
    Obj p = parent_or_layer(parent); Obj im = p ? inst(hget(h_tpl_img), p) : NULL;
    if (!im) return NULL;
    go_active(im, 1);
    gr_material(im, NULL);
    img_set(im, sp, C_WHITE, 0);
    place_tl(im, x, y, w, h);
    W *wd = w_new(W_IMAGE); if (!wd) { destroy_obj(go_of(im)); return NULL; }
    hset(&wd->h_a, sp); hset(&wd->h_b, tex);
    dsl_log("ui: image %s %dx%d (%s)", png_path, tw, th, nearest ? "point filter" : "bilinear");
    return w_done(wd, tr_of(im), NULL);
}

/* ------------------------------------------------------------------ game tooltip (TooltipUI / Tooltip_ETC, no hook) */
#define BASE_KEY "Shared_Gold"                                       /* any stock ETC key: every stock layout step runs */
static const Guid TIP_ID = { 0x64736c7569746970ull, 0x0000000000000003ull };   /* the kit's one tooltip session */
static uintptr_t h_tipview, h_tip_shown_target, h_tip_etc;
static int tip_shown; static Obj tip_test_target; static char tip_cur_title[160];
static Obj tooltip_view(void) { Obj v = hget(h_tipview); if (!v) { v = find_obj(K.TooltipUI, 1); hset(&h_tipview, v); } return v; }
static int tip_ok(void) { return m_tt_show && m_tt_hide && m_tb_id && f_etc_title >= 0 && f_etc_desc >= 0 && ctx_size > 0 && ctx_size <= 0xC0 && f_ctx_hidehold >= 0 && f_ctx_hidehold < ctx_size; }
/* the tooltip window never takes a click while it shows our text (the stock screen clamp can push it over the element,
   e.g. a title button low on the screen); the window is pooled, so stock tooltips get their raycasts back on hide */
static void tip_blocks(Obj etc, int on) {
    if (!etc || !K.CanvasGroup || !m_cg_blocks) return;
    Obj cg = comp_get(etc, K.CanvasGroup); if (!cg && !on) cg = comp_add(etc, K.CanvasGroup);
    if (cg) CALL(m_cg_blocks, void, Obj, bool)(cg, on != 0, m_cg_blocks);
}
static void tip_hide(void) {
    if (!tip_shown) return;
    tip_blocks(hget(h_tip_etc), 1);
    tip_shown = 0; hfree(&h_tip_shown_target); tip_cur_title[0] = 0;
    Obj v = tooltip_view(); if (v) CALL(m_tt_hide, void, Obj, Guid)(v, TIP_ID, m_tt_hide);
}
static void tip_show(TipE *e) {
    Obj v = tooltip_view(); if (!v || !tip_ok()) return;
    if (tip_shown) tip_hide();
    raise_view(v, 32050);
    float r[4]; if (!rect_screen(e->target, r)) return;
    V2 pos = { r[0] + 2, r[1] - 2 };                                  /* bottom-left corner, stock OFFSET_X / OFFSET_Y */
    struct { uint8_t b[0xC0]; } ctx; memset(&ctx, 0, sizeof ctx); ctx.b[f_ctx_hidehold] = 1;   /* HideHoldLayout */
    CALL(m_tt_show, void, Obj, Guid, int, Obj, V2, int, __typeof__(ctx))(v, TIP_ID, 0, str(BASE_KEY), pos, 0, ctx, m_tt_show);
    tip_shown = 1; hset(&h_tip_shown_target, e->target);
    snprintf(tip_cur_title, sizeof tip_cur_title, "%s", e->title);
    /* the shown Tooltip_ETC: the one with our id */
    Obj arr = m_obj_finds ? CALL(m_obj_finds, Obj, Obj)(type_of(K.TooltipETC), m_obj_finds) : NULL, etc = NULL;
    for (int i = 0; i < arr_n(arr) && !etc; i++) { Obj t = arr_at(arr, i); Guid g = CALL(m_tb_id, Guid, Obj)(t, m_tb_id); if (g.lo == TIP_ID.lo && g.hi == TIP_ID.hi) etc = t; }
    if (!etc) { dsl_log("ui: tooltip shown but its Tooltip_ETC was not found"); return; }
    hset(&h_tip_etc, etc);
    tmp_set(AT(etc, f_etc_title, Obj), e->title);
    tmp_set(AT(etc, f_etc_desc, Obj), e->body);
    if (f_etc_sublayout >= 0) go_active(AT(etc, f_etc_sublayout, Obj), 0);
    if (f_etc_flavor >= 0) { Obj fl = AT(etc, f_etc_flavor, Obj); if (alive(fl)) { tmp_set(fl, ""); go_active(fl, 0); } }
    if (m_tb_hidediv) CALL(m_tb_hidediv, void, Obj)(etc, m_tb_hidediv);
    if (f_tb_canhold >= 0) AT(etc, f_tb_canhold, bool) = false;
    if (m_tb_disablehold) CALL(m_tb_disablehold, void, Obj)(etc, m_tb_disablehold);
    tip_blocks(etc, 0);
}
static void tips_frame(void) {
    if (!ntips && !tip_shown) return;
    TipE *hit = NULL;
    V3 m = m_input_mouse ? CALL0(m_input_mouse, V3)(m_input_mouse) : (V3){ -1, -1, 0 };
    for (int i = ntips - 1; i >= 0; i--) {                            /* forget dead targets */
        if (!hget(tips[i].h)) { hfree(&tips[i].h); tips[i] = tips[--ntips]; }
    }
    for (int i = 0; i < ntips && !hit; i++) {
        Obj t = tips[i].target; if (!on_tree(t)) continue;
        if (tip_test_target) { if (t == tip_test_target) hit = &tips[i]; continue; }
        float r[4]; if (rect_screen(t, r) && m.x >= r[0] && m.x <= r[2] && m.y >= r[1] && m.y <= r[3]) hit = &tips[i];
    }
    Obj cur = hget(h_tip_shown_target);
    if (hit && (!tip_shown || cur != hit->target || strcmp(tip_cur_title, hit->title))) tip_show(hit);
    else if (!hit && tip_shown) tip_hide();
}
static void k_tooltip(DSLObj target, const char *title, const char *body) {
    if (!target) return;
    for (int i = 0; i < ntips; i++) if (tips[i].target == target) {
        if (!body) { if (hget(h_tip_shown_target) == target) tip_hide(); hfree(&tips[i].h); tips[i] = tips[--ntips]; return; }
        snprintf(tips[i].title, sizeof tips[i].title, "%s", title ? title : ""); snprintf(tips[i].body, sizeof tips[i].body, "%s", body);
        return;
    }
    if (!body || ntips >= TMAX) return;
    TipE *e = &tips[ntips++]; memset(e, 0, sizeof *e);
    e->target = target; e->h = hnew(target);
    snprintf(e->title, sizeof e->title, "%s", title ? title : ""); snprintf(e->body, sizeof e->body, "%s", body);
}
/* test only: hover this target instead of the mouse (NULL = the mouse) */
__attribute__((visibility("default"))) void dsl_ui_test_hover(DSLObj target) { tip_test_target = target; }
__attribute__((visibility("default"))) int dsl_ui_test_tip(char *title, size_t cap) {   /* 1 = our tooltip shows; its title as drawn */
    Obj etc = hget(h_tip_etc);
    if (title && cap) { title[0] = 0; if (tip_shown && etc) utf8_of(tmp_get(AT(etc, f_etc_title, Obj)), title, cap); }
    return tip_shown && etc && on_tree(etc);
}

/* ------------------------------------------------------------------ notice box (CommonNoticeView) */
static uintptr_t h_nview; static int64_t nview_last = -1000;
static Obj notice_view(void) {
    Obj v = hget(h_nview); if (v) return v;
    if (frames - nview_last < 30 && nview_last >= 0) return NULL;
    nview_last = frames; v = find_obj(K.Notice, 1); hset(&h_nview, v); return v;
}
static struct { int serial; dsl_ui_cb ok, cancel; void *ud; uintptr_t h_ok, h_cancel; } NB;
static void k_notice(const char *text, const char *ok_label, const char *cancel_label, dsl_ui_cb ok, dsl_ui_cb cancel, void *ud) {
    nview_last = -1000; Obj v = notice_view();
    if (!v || !m_nv_select || !m_nv_confirm || f_nv_text < 0) { dsl_log("ui: notice: no CommonNoticeView in this scene: \"%s\" not shown", text ? text : ""); return; }
    NB.serial = (NB.serial + 1) & 0x0FFFFFFF; NB.ok = ok; NB.cancel = cancel; NB.ud = ud;
    Obj a = mk_action(TOKEN(NB.serial, P_OK)), c = cancel_label ? mk_action(TOKEN(NB.serial, P_CANCEL)) : NULL;
    hset(&NB.h_ok, a); hset(&NB.h_cancel, c);
    if (cancel_label) CALL(m_nv_select, void, Obj, Obj, Obj, Obj, bool)(v, str(text), a, c, false, m_nv_select);
    else CALL(m_nv_confirm, void, Obj, Obj, Obj)(v, str(text), a, m_nv_confirm);
    Obj nt = AT(v, f_nv_text, Obj);
    tmp_set(nt, text);
    if (isa(nt, K.TMP_Text)) CALL(m_tmp_align, void, Obj, int)(nt, 514, m_tmp_align);
    if (f_nv_tok >= 0) tmp_set(AT(v, f_nv_tok, Obj), ok_label ? ok_label : "OK");
    if (cancel_label && f_nv_tcancel >= 0) tmp_set(AT(v, f_nv_tcancel, Obj), cancel_label);
    go_active(v, 1);
    raise_view(v, 32040);
    dsl_log("ui: notice %d shown (%s): %s", NB.serial, cancel_label ? "two buttons" : "one button", text ? text : "");
}

/* ------------------------------------------------------------------ place / active / enabled / destroy / clipboard */
static void k_set_text(DSLObj o, const char *s) {
    W *w = w_of(o);
    if (w && w->kind == W_FIELD) { in_settext(hget(w->h_a), s); return; }
    Obj t = w ? hget(w->h_label) : NULL;
    if (!t && o) t = isa((Obj)o, K.TMP_Text) ? (Obj)o : comp_get((Obj)o, K.TMP_Text);
    if (t) tmp_set(t, s);
}
static void k_place(DSLObj o, float x, float y, float w_, float h) {
    W *w = w_of(o); Obj r = w ? w_root(w) : (Obj)o;
    if (w && w->kind == W_TOGGLE) h = TOGGLE_H;
    if (w && w->kind == W_VALUE) h = VALUE_H;
    place_tl(r, x, y, w_, h);
    if (!w) return;
    w->w = w_; w->h = h;
    if (w->kind == W_TOGGLE) toggle_layout(w);
    if (w->kind == W_VALUE) value_layout(w);
    if (w->kind == W_SCROLL) { V2 s = rt_size(hget(w->h_rt)); CALL(m_rt_size, void, Obj, V2)(hget(w->h_rt), (V2){ w_, s.y < h ? h : s.y }, m_rt_size); }
}
static void k_set_active(DSLObj o, int on) { W *w = w_of(o); go_active(w ? w_root(w) : (Obj)o, on); }
static void btn_enable(Obj b, int on) {
    if (!isa(b, K.TitleButton)) return;
    if (m_tb_inter) CALL(m_tb_inter, void, Obj, bool)(b, on != 0, m_tb_inter);
    Obj l = btn_label(b); if (l) gr_color(l, on ? C_MAIN : C_GREY);
}
static void k_set_enabled(DSLObj o, int on) {
    W *w = w_of(o); if (!w) return;
    w->enabled = on != 0;
    switch (w->kind) {
    case W_BUTTON: case W_TOGGLE: btn_enable(hget(w->h_btn), on); if (w->kind == W_TOGGLE) { Obj ck = hget(w->h_b); if (ck) gr_color(ck, on ? (spr(S_CHECK) ? C_WHITE : C_ORANGE) : C_GREY); } break;
    case W_VALUE: btn_enable(hget(w->h_a), on); btn_enable(hget(w->h_b), on); gr_color(hget(w->h_c), on ? C_MAIN : C_GREY); break;
    case W_FIELD: { Obj f = hget(w->h_a); if (f && m_sel_inter) CALL(m_sel_inter, void, Obj, bool)(f, on != 0, m_sel_inter); } break;
    default: break;
    }
}
static void k_destroy(DSLObj o) {
    if (!o) return;
    W *w = w_of(o); Obj r = w ? w_root(w) : (Obj)o;
    if (hget(h_tip_shown_target) && (hget(h_tip_shown_target) == o || hget(h_tip_shown_target) == r)) tip_hide();
    destroy_obj(go_of(r));
    for (int i = nws - 1; i >= 0; i--) if (ws[i] == w) { w_free(i); break; }
}
static size_t k_clipboard_get(char *out, size_t cap) {
    if (out && cap) out[0] = 0;
    if (!m_clip_get) return 0;
    Obj s = CALL0(m_clip_get, Obj)(m_clip_get);
    if (out && cap) utf8_of(s, out, cap);
    return s ? utf8_len_of(s) : 0;
}
static void k_clipboard_set(const char *s) { if (m_clip_set) CALL(m_clip_set, void, Obj)(str(s), m_clip_set); }

static DSLUi KIT = {
    .size = sizeof(DSLUi), .panel = k_panel, .text = k_text, .set_text = k_set_text, .button = k_button, .toggle = k_toggle,
    .toggle_get = k_toggle_get, .toggle_set = k_toggle_set, .value_row = k_value_row, .value_get = k_value_get, .value_set = k_value_set,
    .field = k_field, .field_get = k_field_get, .field_set = k_field_set, .scroll = k_scroll, .scroll_height = k_scroll_height,
    .image_png = k_image_png, .tooltip = k_tooltip, .notice = k_notice, .place = k_place, .set_active = k_set_active,
    .set_enabled = k_set_enabled, .destroy = k_destroy, .clipboard_get = k_clipboard_get, .clipboard_set = k_clipboard_set
};

/* ------------------------------------------------------------------ title screen buttons */
#define TBMAX 16
typedef struct { char owner[64], label[96], tip[600]; int order, seq; void (*click)(void *); void *ud; uintptr_t h_btn, h_view, h_act; } TBE;
static TBE tbs[TBMAX]; static int ntbs, tb_seq;
static uintptr_t h_tview; static int64_t tview_last = -1000;
static Obj title_view(void) {
    Obj v = hget(h_tview); if (v) return v;
    if (frames - tview_last < 30 && tview_last >= 0) return NULL;
    tview_last = frames; v = find_obj(K.TitleMenuView, 0); hset(&h_tview, v); return v;
}
__attribute__((visibility("default")))
int dsl_ui_title_add(const char *owner_id, const char *label, int order, const char *tip, void (*click)(void *), void *ud) {
    if (!label || !click || ntbs >= TBMAX) return -1;
    kit_used = 1;
    TBE *e = &tbs[ntbs++]; memset(e, 0, sizeof *e);
    snprintf(e->owner, sizeof e->owner, "%s", owner_id ? owner_id : "?"); snprintf(e->label, sizeof e->label, "%s", label);
    snprintf(e->tip, sizeof e->tip, "%s", tip ? tip : "");
    e->order = order; e->seq = tb_seq++; e->click = click; e->ud = ud;
    dsl_log("ui: title button \"%s\" (%s, order %d) registered", label, e->owner, order);
    return 0;
}
static int tb_cmp(const void *a, const void *b) {
    const TBE *x = *(TBE *const *)a, *y = *(TBE *const *)b;
    return x->order != y->order ? (x->order < y->order ? -1 : 1) : x->seq - y->seq;
}
/* every frame on the title: our clones exist for this view, sit last in the column (below the stock buttons and the
   multiplayer clones, which are inserted after Load), in order, and show only while the stock buttons show (the
   multiplayer pages hide the stock column) */
static void title_frame(void) {
    if (!ntbs) return;
    Obj v = title_view(); if (!v || !on_tree(v) || f_tv_credit < 0) return;
    Obj credit = AT(v, f_tv_credit, Obj); if (!isa(credit, K.TitleButton)) return;
    Obj col = tr_parent(credit); if (!col) return;
    int stock_on = 0;
    for (int i = 0; i < 7; i++) if (f_tv_btn[i] >= 0) { Obj b = AT(v, f_tv_btn[i], Obj); if (b && active_self(b)) stock_on = 1; }
    TBE *ord[TBMAX]; for (int i = 0; i < ntbs; i++) ord[i] = &tbs[i];
    qsort(ord, (size_t)ntbs, sizeof *ord, tb_cmp);
    int n = child_count(col);
    for (int i = 0; i < ntbs; i++) {
        TBE *e = ord[i]; Obj b = hget(e->h_btn);
        if (!b || hget(e->h_view) != v) {
            if (b) destroy_obj(go_of(b));
            Obj c = inst(credit, col); b = c ? comp_get(c, K.TitleButton) : NULL;
            if (!b) { dsl_log("ui: title button \"%s\": clone failed", e->label); continue; }
            Obj a = mk_action(TOKEN((int)(e - tbs), P_TITLE));          /* the index in tbs[], not in ord[] */
            if (m_tb_init) CALL(m_tb_init, void, Obj, Obj)(b, a, m_tb_init);
            hset(&e->h_act, a);
            Obj l = btn_label(b);
            if (l) { txt_style(l, 0, 22, 514, 1); tmp_set(l, e->label); }   /* the stock menu size (mpmenu title clones) */
            hset(&e->h_btn, b); hset(&e->h_view, v); n = child_count(col);
            if (e->tip[0]) k_tooltip(tr_of(b), e->label, e->tip);
            dsl_log("ui: title button \"%s\" added to the title column (%d children)", e->label, n);
        }
        int want = n - ntbs + i;
        if (sibling(b) != want) set_sibling(b, want);
        go_active(b, stock_on);
    }
}

/* ------------------------------------------------------------------ frame tick */
static void run_queue(void) {
    uint32_t q[QMAX]; int n = nq; memcpy(q, queue, (size_t)n * sizeof *q); nq = 0;
    for (int i = 0; i < n; i++) {
        int id = (int)(q[i] >> 4), part = (int)(q[i] & 15);
        if (part == P_TITLE) { if (id >= 0 && id < ntbs) { dsl_log("ui: title button \"%s\" clicked", tbs[id].label); tbs[id].click(tbs[id].ud); } continue; }
        if (part == P_OK || part == P_CANCEL) {
            if (id != NB.serial) continue;
            dsl_ui_cb cb = part == P_OK ? NB.ok : NB.cancel; void *ud = NB.ud;
            NB.ok = NB.cancel = NULL; NB.serial = (NB.serial + 1) & 0x0FFFFFFF;
            if (cb) cb(ud, NULL);
            continue;
        }
        W *w = w_by_id(id); if (!w || !w->enabled || !hget(w->h_rt)) continue;
        if (w->kind == W_BUTTON && part == P_CLICK) { if (w->cb) w->cb(w->ud, w->rt); }
        else if (w->kind == W_TOGGLE && part == P_CLICK) { w->on = !w->on; toggle_look(w); if (w->cb) w->cb(w->ud, w->rt); }
        else if (w->kind == W_VALUE && (part == P_PREV || part == P_NEXT)) {
            w->index = (w->index + (part == P_NEXT ? 1 : w->n - 1)) % w->n; value_text(w); if (w->cb) w->cb(w->ud, w->rt);
        }
    }
}
static void fields_frame(void) {
    if (!m_in_focused) return;
    int enter = m_input_keydown && (CALL(m_input_keydown, bool, int)(13, m_input_keydown) || CALL(m_input_keydown, bool, int)(271, m_input_keydown));
    int sub[16], ns = 0;
    for (int i = 0; i < nws; i++) {
        W *w = ws[i]; if (w->kind != W_FIELD) continue;
        Obj f = hget(w->h_a); if (!f) continue;
        int foc = CALL(m_in_focused, bool, Obj)(f, m_in_focused);
        if (w->was_focused && enter && w->cb && w->enabled && ns < 16) sub[ns++] = w->id;   /* Enter in the field = submit */
        w->was_focused = foc;
    }
    for (int i = 0; i < ns; i++) { W *w = w_by_id(sub[i]); if (w && w->cb) w->cb(w->ud, w->rt); }
}
static int kit_ok, in_tick; static int32_t last_frame = -1;
static void (*o_es_update)(Obj, const void *);
static void tick(void) {
    frames++;
    if (frames % 60 == 0) w_sweep();
    assets_scan(0);
    if (ntbs) title_frame();
    run_queue();
    fields_frame();
    tips_frame();
}
static void h_es_update(Obj self, const void *mi) {
    o_es_update(self, mi);
    int32_t fc = m_time_frame ? CALL0(m_time_frame, int)(m_time_frame) : last_frame + 1;
    if (fc == last_frame || in_tick) return;                        /* several EventSystems: once per frame */
    last_frame = fc; in_tick = 1;
    if (kit_ok) tick();
    dsl_frame_hook();
    in_tick = 0;
}

/* ------------------------------------------------------------------ test only (loader/test/uikit_test.py through the
   uikit-demo module; never used by a player's modules) */
static void *m_ev_key, *m_in_process;
__attribute__((visibility("default"))) int dsl_ui_test_rect(DSLObj o, float r[4]) {      /* 1 = shown (active in hierarchy) */
    W *w = w_of(o); Obj t = w ? w_root(w) : (Obj)o;
    if (!t || !alive(t)) { r[0] = r[1] = r[2] = r[3] = 0; return -1; }
    return rect_screen(t, r) ? on_tree(t) : -1;
}
__attribute__((visibility("default"))) size_t dsl_ui_test_label(DSLObj o, char *out, size_t cap) {   /* drawn text of a widget */
    W *w = w_of(o); Obj t = w ? hget(w->kind == W_VALUE ? w->h_c : w->h_label) : NULL;
    if (!t && o) t = isa((Obj)o, K.TMP_Text) ? (Obj)o : comp_get((Obj)o, K.TMP_Text);
    if (w && w->kind == W_FIELD) { Obj f = hget(w->h_a); t = f && m_in_tc ? CALL(m_in_tc, Obj, Obj)(f, m_in_tc) : NULL; }
    out[0] = 0; if (t) utf8_of(tmp_get(t), out, cap);
    return strlen(out);
}
/* title column: "label|active|sibling;..." for every child with a title-button label; returns the RectTransform of the
   first child whose label is `find` (NULL = none) */
__attribute__((visibility("default"))) DSLObj dsl_ui_test_title(const char *find, char *out, size_t cap) {
    if (out && cap) out[0] = 0;
    tview_last = -1000; Obj v = title_view(); if (!v || f_tv_credit < 0) return NULL;
    Obj col = tr_parent(AT(v, f_tv_credit, Obj)); DSLObj hit = NULL; size_t o = 0;
    for (int i = 0, n = child_count(col); i < n; i++) {
        Obj c = child_at(col, i), b = comp_get(c, K.TitleButton); if (!b) continue;
        char l[96]; utf8_of(tmp_get(btn_label(b)), l, sizeof l);
        if (out && cap) o += (size_t)snprintf(out + o, o < cap ? cap - o : 0, "%s%s|%d|%d", o ? ";" : "", l, active_self(c), i);
        if (find && !hit && !strcmp(l, find)) hit = tr_of(c);
    }
    return hit;
}
__attribute__((visibility("default"))) DSLObj dsl_ui_test_notice_button(int cancel) {   /* the shown notice's button */
    Obj v = notice_view(); if (!v || !on_tree(v)) return NULL;
    int32_t f = fld(K.Notice, cancel ? "_cancelButton" : "_confirmButton");
    Obj b = f >= 0 ? AT(v, f, Obj) : NULL;
    return on_tree(b) ? tr_of(b) : NULL;
}
/* typed characters through the field's own TMP_InputField.ProcessEvent (Event.KeyboardEvent): macOS gives typed text
   only to the active app, which a hidden test copy never is (mpmenu tmp_keys) */
__attribute__((visibility("default"))) int dsl_ui_test_type(DSLObj field, const char *chars) {
    W *w = w_of(field); Obj f = w && w->kind == W_FIELD ? hget(w->h_a) : NULL;
    if (!m_ev_key) { m_ev_key = mi_sig(dsl_find_class("UnityEngine", "Event"), "KeyboardEvent", 1, NULL); m_in_process = mi_sig(K.TMP_Input, "ProcessEvent", 1, NULL); }
    if (!f || !m_ev_key || !m_in_process) return -1;
    int n = 0;
    for (const char *c = chars; *c; c++) {
        char one[16]; if (*c == ' ') snprintf(one, sizeof one, "space"); else { one[0] = *c; one[1] = 0; }
        Obj ev = CALL(m_ev_key, Obj, Obj)(str(one), m_ev_key);
        if (ev) { CALL(m_in_process, void, Obj, Obj)(f, ev, m_in_process); n++; }
    }
    return n;
}

/* ------------------------------------------------------------------ install */
const DSLUi *dsl_ui_kit(void) { return kit_ok ? &KIT : NULL; }
void dsl_ui_install(void) {
    static int done; if (done) return; done = 1;
    if (!dsl_ready) { dsl_log("ui: dsl_ui_install before il2cpp_init returned: kit off"); return; }
#define X(var, name) *(void **)&I.var = dsl_il2cpp_fn(name)
    X(string_new, "il2cpp_string_new"); X(gchandle_new, "il2cpp_gchandle_new"); X(gchandle_get_target, "il2cpp_gchandle_get_target");
    X(gchandle_free, "il2cpp_gchandle_free"); X(object_get_class, "il2cpp_object_get_class");
    X(class_is_assignable_from, "il2cpp_class_is_assignable_from"); X(class_get_type, "il2cpp_class_get_type");
    X(type_get_object, "il2cpp_type_get_object"); X(object_new, "il2cpp_object_new"); X(array_new, "il2cpp_array_new");
    X(class_get_methods, "il2cpp_class_get_methods"); X(method_get_name, "il2cpp_method_get_name");
    X(method_get_param_count, "il2cpp_method_get_param_count"); X(method_get_param, "il2cpp_method_get_param");
    X(type_get_name, "il2cpp_type_get_name"); X(class_get_parent, "il2cpp_class_get_parent");
    X(object_get_virtual_method, "il2cpp_object_get_virtual_method"); X(class_value_size, "il2cpp_class_value_size");
    X(method_is_generic, "il2cpp_method_is_generic"); X(il_free, "il2cpp_free");
#undef X
    if (!I.string_new || !I.gchandle_new || !I.gchandle_get_target || !I.gchandle_free || !I.object_get_class || !I.class_is_assignable_from
        || !I.class_get_type || !I.type_get_object || !I.object_new || !I.array_new || !I.class_get_methods || !I.method_get_name
        || !I.method_get_param_count || !I.method_get_param || !I.type_get_name || !I.class_get_parent) {
        dsl_log("ui: il2cpp exports missing: kit off"); return;
    }
    const char *UE = "UnityEngine", *UI = "UnityEngine.UI", *TM = "TMPro", *RU = "Refactor.UI";
    K.Object = klass(UE, "Object"); K.Component = klass(UE, "Component"); K.GameObject = klass(UE, "GameObject");
    K.Transform = klass(UE, "Transform"); K.RectTransform = klass(UE, "RectTransform"); K.Graphic = klass(UI, "Graphic");
    K.Image = klass(UI, "Image"); K.TMP_Text = klass(TM, "TMP_Text"); K.TMP_Input = klass(TM, "TMP_InputField");
    K.Selectable = klass(UI, "Selectable"); K.Sprite = klass(UE, "Sprite"); K.FontAsset = klass(TM, "TMP_FontAsset");
    K.TMP_Asset = klass(TM, "TMP_Asset"); K.Action = klass("System", "Action"); K.Byte = klass("System", "Byte");
    K.Vector3 = klass(UE, "Vector3"); K.Canvas = klass(UE, "Canvas"); K.CanvasScaler = klass(UI, "CanvasScaler");
    K.GraphicRaycaster = klass(UI, "GraphicRaycaster"); K.RectMask2D = klass(UI, "RectMask2D"); K.ScrollRect = klass(UI, "ScrollRect");
    K.Texture2D = klass(UE, "Texture2D"); K.TitleMenuView = klass(RU, "TitleMenuView"); K.TitleButton = klass(RU, "SubUI_TitleMenuButton");
    K.Notice = klass(RU, "CommonNoticeView"); K.DASText = klass(RU, "DASText"); K.TooltipUI = klass(RU, "TooltipUI");
    K.TooltipETC = klass(RU, "Tooltip_ETC"); K.TipCtx = klass(RU, "TooltipContext"); K.UIFactory = klass("Refactor.Main.UI", "UIFactory");
    K.SaveLoadView = klass("Refactor", "SaveLoadView"); K.CanvasGroup = klass(UE, "CanvasGroup"); K.CampaignSetting = klass(RU, "SubUI_CampaignSetting");
    void *kInput = klass(UE, "Input"), *kTime = klass(UE, "Time"), *kGUI = klass(UE, "GUIUtility"), *kRes = klass(UE, "Resources"),
         *kConv = klass(UE, "ImageConversion"), *kTex = klass(UE, "Texture"), *kRTU = klass(UE, "RectTransformUtility"),
         *kES = klass("UnityEngine.EventSystems", "EventSystem"), *kTB = klass(RU, "Tooltip_Base"), *kSO = klass("Refactor.Main.UI", "UIPrefabSO"),
         *kBeh = klass(UE, "Behaviour");
    m_obj_implicit = MTH(K.Object, "op_Implicit", 1); m_obj_name = MTH(K.Object, "get_name", 0);
    m_obj_inst = MSIG(K.Object, "Instantiate", 2, "UnityEngine.Object,UnityEngine.Transform");
    m_obj_destroy = MTH(K.Object, "Destroy", 1); m_obj_dontdestroy = MTH(K.Object, "DontDestroyOnLoad", 1);
    m_obj_find = MSIG(K.Object, "FindObjectOfType", 2, "System.Type,System.Boolean");
    m_obj_finds = OPTSIG(K.Object, "FindObjectsOfType", 1, "System.Type");
    m_res_findall = MSIG(kRes, "FindObjectsOfTypeAll", 1, "System.Type");
    m_comp_go = MTH(K.Component, "get_gameObject", 0); m_comp_tr = MTH(K.Component, "get_transform", 0);
    m_comp_get = MSIG(K.Component, "GetComponent", 1, "System.Type");
    m_comp_inparent = MSIG(K.Component, "GetComponentInParent", 1, "System.Type");
    m_go_ctor = MSIG(K.GameObject, ".ctor", 1, "System.String"); m_go_active = MTH(K.GameObject, "SetActive", 1);
    m_go_self = MTH(K.GameObject, "get_activeSelf", 0); m_go_tree = MTH(K.GameObject, "get_activeInHierarchy", 0);
    m_go_add = MSIG(K.GameObject, "AddComponent", 1, "System.Type"); m_go_tr = MTH(K.GameObject, "get_transform", 0);
    m_go_layer = OPT(K.GameObject, "set_layer", 1);
    m_tr_parent = MTH(K.Transform, "get_parent", 0); m_tr_setparent = MSIG(K.Transform, "SetParent", 2, "UnityEngine.Transform,System.Boolean");
    m_tr_sib = MTH(K.Transform, "GetSiblingIndex", 0); m_tr_setsib = MTH(K.Transform, "SetSiblingIndex", 1);
    m_tr_count = MTH(K.Transform, "get_childCount", 0); m_tr_child = MTH(K.Transform, "GetChild", 1);
    m_rt_amin = MTH(K.RectTransform, "set_anchorMin", 1); m_rt_amax = MTH(K.RectTransform, "set_anchorMax", 1);
    m_rt_pivot = MTH(K.RectTransform, "set_pivot", 1); m_rt_size = MTH(K.RectTransform, "set_sizeDelta", 1);
    m_rt_getsize = MTH(K.RectTransform, "get_sizeDelta", 0); m_rt_pos = MTH(K.RectTransform, "set_anchoredPosition", 1);
    m_rt_corners = MTH(K.RectTransform, "GetWorldCorners", 1);
    m_gr_color = MTH(K.Graphic, "set_color", 1); m_gr_raycast = MTH(K.Graphic, "set_raycastTarget", 1); m_gr_material = OPT(K.Graphic, "set_material", 1);
    m_img_sprite = MTH(K.Image, "set_sprite", 1); m_img_getsprite = MTH(K.Image, "get_sprite", 0); m_img_type = OPT(K.Image, "set_type", 1);
    m_beh_enabled = OPT(kBeh, "set_enabled", 1);
    m_tmp_get = MTH(K.TMP_Text, "get_text", 0); m_tmp_set = MTH(K.TMP_Text, "set_text", 1);
    m_tmp_font = MTH(K.TMP_Text, "set_font", 1); m_tmp_getfont = MTH(K.TMP_Text, "get_font", 0); m_tmp_size = MTH(K.TMP_Text, "set_fontSize", 1);
    m_tmp_auto = MTH(K.TMP_Text, "set_enableAutoSizing", 1); m_tmp_align = MTH(K.TMP_Text, "set_alignment", 1);
    m_tmp_wrap = MTH(K.TMP_Text, "set_textWrappingMode", 1); m_tmp_overflow = MTH(K.TMP_Text, "set_overflowMode", 1);
    m_tmp_rich = MTH(K.TMP_Text, "set_richText", 1); m_tmp_shmat = OPT(K.TMP_Text, "set_fontSharedMaterial", 1);
    m_asset_mat = OPT(K.TMP_Asset, "get_material", 0);
    if (!m_asset_mat) f_asset_mat = fld(K.TMP_Asset, "m_Material");
    m_in_text = MTH(K.TMP_Input, "get_text", 0); m_in_settext = MTH(K.TMP_Input, "SetTextWithoutNotify", 1);
    m_in_limit = OPT(K.TMP_Input, "set_characterLimit", 1); m_in_ctype = OPT(K.TMP_Input, "set_contentType", 1);
    m_in_ph = OPT(K.TMP_Input, "get_placeholder", 0); m_in_tc = OPT(K.TMP_Input, "get_textComponent", 0);
    m_in_focused = OPT(K.TMP_Input, "get_isFocused", 0); m_sel_inter = OPT(K.Selectable, "set_interactable", 1);
    m_input_mouse = OPT(kInput, "get_mousePosition", 0); m_input_keydown = OPTSIG(kInput, "GetKeyDown", 1, "UnityEngine.KeyCode");
    m_time_frame = MTH(kTime, "get_frameCount", 0);
    m_clip_get = OPT(kGUI, "get_systemCopyBuffer", 0); m_clip_set = OPT(kGUI, "set_systemCopyBuffer", 1);
    m_tex_ctor = OPTSIG(K.Texture2D, ".ctor", 4, "System.Int32,System.Int32,UnityEngine.TextureFormat,System.Boolean");
    m_tex_load = OPTSIG(kConv, "LoadImage", 2, "UnityEngine.Texture2D,System.Byte[]");
    m_tex_filter = OPT(kTex, "set_filterMode", 1); m_tex_wrap = OPT(kTex, "set_wrapMode", 1); m_tex_apply = OPT(K.Texture2D, "Apply", 0);
    m_tex_w = OPT(kTex, "get_width", 0); m_tex_h = OPT(kTex, "get_height", 0);
    m_spr_create = OPTSIG(K.Sprite, "Create", 4, "UnityEngine.Texture2D,UnityEngine.Rect,UnityEngine.Vector2,System.Single");
    m_cv_mode = MTH(K.Canvas, "set_renderMode", 1); m_cv_order = OPT(K.Canvas, "set_sortingOrder", 1);
    m_cv_override = OPT(K.Canvas, "set_overrideSorting", 1); m_cv_root = OPT(K.Canvas, "get_rootCanvas", 0);
    m_cv_cam = OPT(K.Canvas, "get_worldCamera", 0);
    m_cv_getmode = OPT(K.Canvas, "get_renderMode", 0);
    m_cs_mode = OPT(K.CanvasScaler, "set_uiScaleMode", 1); m_cs_getmode = OPT(K.CanvasScaler, "get_uiScaleMode", 0);
    m_cs_ref = OPT(K.CanvasScaler, "set_referenceResolution", 1); m_cs_getref = OPT(K.CanvasScaler, "get_referenceResolution", 0);
    m_cs_match = OPT(K.CanvasScaler, "set_matchWidthOrHeight", 1); m_cs_getmatch = OPT(K.CanvasScaler, "get_matchWidthOrHeight", 0);
    m_cs_smm = OPT(K.CanvasScaler, "set_screenMatchMode", 1); m_cs_getsmm = OPT(K.CanvasScaler, "get_screenMatchMode", 0);
    m_rtu_w2s = OPTSIG(kRTU, "WorldToScreenPoint", 2, "UnityEngine.Camera,UnityEngine.Vector3");
    m_sr_content = OPT(K.ScrollRect, "set_content", 1); m_sr_viewport = OPT(K.ScrollRect, "set_viewport", 1);
    m_sr_h = OPT(K.ScrollRect, "set_horizontal", 1); m_sr_v = OPT(K.ScrollRect, "set_vertical", 1);
    m_sr_move = OPT(K.ScrollRect, "set_movementType", 1); m_sr_sens = OPT(K.ScrollRect, "set_scrollSensitivity", 1);
    m_es_update = MTH(kES, "Update", 0);
    m_tb_init = MTH(K.TitleButton, "Init", 1); m_tb_inter = OPT(K.TitleButton, "SetInteractable", 1);
    m_nv_select = OPT(K.Notice, "Show_Select", 4); m_nv_confirm = OPT(K.Notice, "Show_Confirm", 2);
    m_ui_prefab = OPT(kSO, "GetUIPrefab", 1);
    m_tt_show = OPT(K.TooltipUI, "ShowTooltipAtScreenPosition", 6); m_tt_hide = OPT(K.TooltipUI, "HideToolTip", 1);
    m_tb_id = OPT(kTB, "get_TooltipID", 0); m_tb_hidediv = OPT(kTB, "HideLastDivider", 0); m_tb_disablehold = OPT(kTB, "DisableHoldLayout", 0);
    m_cg_blocks = OPT(K.CanvasGroup, "set_blocksRaycasts", 1);
    f_tb_label = fld(K.TitleButton, "_buttonName");
    { static const char *const nm[7] = { "_continueButton", "_newGameButton", "_loadCampaignButton", "_settingButton", "_creditButton", "_reportBugButton", "_quitButton" };
      for (int i = 0; i < 7; i++) f_tv_btn[i] = fld(K.TitleMenuView, nm[i]); f_tv_credit = f_tv_btn[4]; }
    f_nv_text = fld(K.Notice, "_noticeText"); f_nv_tok = fld(K.Notice, "_txtConfirm"); f_nv_tcancel = fld(K.Notice, "_txtCancle");
    f_das_usekey = fld(K.DASText, "_useTextKey");
    f_etc_title = fld(K.TooltipETC, "_titleText"); f_etc_sublayout = fld(K.TooltipETC, "_subtitleLayout");
    f_etc_desc = fld(K.TooltipETC, "_desText"); f_etc_flavor = fld(K.TooltipETC, "_flavorText"); f_tb_canhold = fld(kTB, "_canShowHoldLayout");
    { int32_t o = fld(K.TipCtx, "<HideHoldLayout>k__BackingField"); f_ctx_hidehold = o >= 0x10 ? o - 0x10 : -1;   /* value type: minus the object header */
      ctx_size = K.TipCtx && I.class_value_size ? I.class_value_size(K.TipCtx, NULL) : 0; }
    f_uif_prefabs = fld(K.UIFactory, "_prefabs"); f_slv_field = fld(K.SaveLoadView, "_saveNameInputField"); f_cs_field = fld(K.CampaignSetting, "_campaignNameInputField");
    if (miss_core) { dsl_log("ui: %d core game names missing (%s): kit off", miss_core, miss_list); return; }
    if (miss_opt) dsl_log("ui: %d optional names missing (%s): those parts are off", miss_opt, miss_list);
    if (!tip_ok()) dsl_log("ui: game tooltip off (TooltipContext size %#x, HideHoldLayout at %#x)", ctx_size, f_ctx_hidehold);
    void *upd = *(void **)m_es_update;
    if (dsl_hook(DSL_OWNER_LOADER, upd, (void *)h_es_update, (void **)&o_es_update, "loader ui EventSystem.Update")) {
        dsl_log("ui: EventSystem.Update hook refused: kit off (no frame tick)"); return;
    }
    kit_ok = 1;
    dsl_log("ui: kit ready (DSLUi size %zu, tooltip %s, notice %s, fields %s, images %s)", sizeof(DSLUi), tip_ok() ? "on" : "OFF",
            m_nv_select && m_nv_confirm ? "on" : "OFF", m_in_focused ? "on" : "OFF", m_tex_load && m_spr_create ? "on" : "OFF");
}
