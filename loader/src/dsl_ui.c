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
#include "dsl_os.h"
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
typedef struct { float x, y, z, w; } V4;
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
         *Texture2D, *TitleMenuView, *LangView, *GridLayout, *TitleButton, *Notice, *DASText, *TooltipUI, *TooltipETC, *TipCtx, *UIFactory,
         *SaveLoadView, *CampaignSetting, *TMP_Asset, *CanvasGroup, *HVLayout, *OptionView, *GeneralSetting, *SubSlider, *Slider;
} K;

static void *m_obj_implicit, *m_obj_name, *m_obj_inst, *m_obj_destroy, *m_obj_dontdestroy, *m_obj_find, *m_obj_finds, *m_res_findall,
    *m_comp_go, *m_comp_tr, *m_comp_get, *m_comp_inparent,
    *m_go_ctor, *m_go_active, *m_go_self, *m_go_tree, *m_go_add, *m_go_tr, *m_go_layer,
    *m_tr_parent, *m_tr_setparent, *m_tr_sib, *m_tr_setsib, *m_tr_count, *m_tr_child, *m_tr_euler, *m_tr_wpos, *m_tr_setwpos,
    *m_rt_amin, *m_rt_amax, *m_rt_pivot, *m_rt_size, *m_rt_getsize, *m_rt_pos, *m_rt_getpos, *m_rt_corners, *m_rt_omin, *m_rt_setomin, *m_rt_getamin, *m_rt_omax, *m_rt_setomax, *m_rt_getamax, *m_tr_find, *m_spr_rect, *m_scr_h, *m_tr_setscale, *m_lg_setsp, *m_lg_getsp,
    *m_gr_color, *m_gr_raycast, *m_gr_material, *m_img_sprite, *m_img_getsprite, *m_img_type, *m_beh_enabled,
    *m_tmp_get, *m_tmp_set, *m_tmp_font, *m_tmp_getfont, *m_tmp_size, *m_tmp_auto, *m_tmp_getsize, *m_tmp_getauto, *m_grid_cell, *m_grid_getcell, *m_rt_rect, *m_tmp_prefw, *m_tmp_align, *m_tmp_wrap, *m_tmp_overflow,
    *m_tmp_rich, *m_tmp_shmat, *m_asset_mat,
    *m_tmp_fsmin, *m_tmp_fsmax, *m_tmp_pw, *m_tmp_ph, *m_tmp_prefv,   /* text fit (+ m_tmp_getsize, m_tmp_getauto, m_rt_rect) */
    *m_in_text, *m_in_settext, *m_in_limit, *m_in_ctype, *m_in_ph, *m_in_tc, *m_in_focused, *m_sel_inter,
    *m_input_mouse, *m_input_keydown, *m_time_frame, *m_clip_get, *m_clip_set,
    *m_tex_ctor, *m_tex_load, *m_tex_filter, *m_tex_wrap, *m_tex_apply, *m_tex_w, *m_tex_h, *m_spr_create, *m_spr_create7, *m_spr_tex, *m_spr_texrect, *m_img_ppm,
    *m_cv_mode, *m_cv_getmode, *m_cv_order, *m_cv_override, *m_cv_root, *m_cv_cam, *m_cs_mode, *m_cs_getmode, *m_cs_ref, *m_cs_getref,
    *m_cs_match, *m_cs_getmatch, *m_cs_smm, *m_cs_getsmm, *m_rtu_w2s,
    *m_sr_content, *m_sr_viewport, *m_sr_h, *m_sr_v, *m_sr_move, *m_sr_sens,
    *m_es_update, *m_tb_init, *m_tb_inter, *m_nv_select, *m_nv_confirm, *m_ui_prefab,
    *m_tt_show, *m_tt_hide, *m_tb_id, *m_tb_hidediv, *m_tb_disablehold, *m_cg_blocks, *m_scr_w,
    *m_ls_font, *m_ls_mat;                                           /* LanguageSetting.GetFont / GetMaterial (the language's fonts) */
static void *m_lr_force, *m_cur_perform;                             /* LayoutRebuilder.ForceRebuildLayoutImmediate, CanvasUpdateRegistry.PerformUpdate */
static void *m_lg_getpad, *m_ro_gettop, *m_ro_settop;                /* LayoutGroup.padding, RectOffset.top (the notice box grows) */
static void *m_sl_setmin, *m_sl_setmax, *m_sl_whole, *m_sl_get, *m_sl_set;           /* UnityEngine.UI.Slider (kit slider) */
static int32_t f_gov_general = -1, f_gs_cam = -1, f_ss_slider = -1, f_ss_value = -1, f_ss_toggle = -1, f_ss_toggletext = -1;                             /* LayoutRebuilder.ForceRebuildLayoutImmediate, CanvasUpdateRegistry.PerformUpdate */

static int32_t f_tb_label = -1, f_tv_btn[7], f_tv_credit = -1, f_nv_text = -1, f_nv_tok = -1, f_nv_tcancel = -1,
               f_das_usekey = -1, f_etc_title = -1, f_etc_sublayout = -1, f_etc_desc = -1, f_etc_flavor = -1, f_tb_canhold = -1, f_tb_align = -1,
               f_ctx_hidehold = -1, f_uif_prefabs = -1, f_slv_field = -1, f_cs_field = -1, f_asset_mat = -1,
               f_das_style = -1, f_das_fmat = -1;
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
/* UTF-8 -> il2cpp string. il2cpp_string_new returns "" for input that is not valid UTF-8, so a last character cut by a
   byte cap (snprintf into a fixed buffer) would blank the whole label: that tail is dropped first (ds_i18n.h) */
static Obj str(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s);
    if (!n || !((unsigned char)s[n - 1] & 0x80)) return I.string_new(s);   /* ends in ASCII: nothing to fix */
    char sb[2048], *b = n < sizeof sb ? sb : malloc(n + 1);
    if (!b) return I.string_new("");
    memcpy(b, s, n + 1); dsi_utf8_fix_end(b);
    Obj r = I.string_new(b);
    if (b != sb) free(b);
    return r;
}
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
static void fit_text(Obj t, const char *s);
static void tmp_set(Obj t, const char *s) {
    if (!isa(t, K.TMP_Text)) return;
    if (f_das_usekey >= 0 && isa(t, K.DASText)) AT(t, f_das_usekey, bool) = false;   /* DASText: keep our raw text (no text key) */
    void *m = vmi(t, m_tmp_set); CALL(m, void, Obj, Obj)(t, str(s), m);
    fit_text(t, s);
}

/* ------------------------------------------------------------------ stock look: sprites, fonts, templates */
static const Col C_MAIN = { 0.8f, 0.745f, 0.722f, 1 }, C_SUB = { 0.651f, 0.565f, 0.522f, 1 }, C_WHITE = { 1, 1, 1, 1 },
                 C_ORANGE = { 0.918f, 0.624f, 0.239f, 1 }, C_ROW1 = { 0.212f, 0.196f, 0.227f, 1 }, C_BOXF = { 0.133f, 0.125f, 0.145f, 1 },
                 C_GREY = { 0.45f, 0.42f, 0.45f, 1 }, C_FIELD = { 0.6f, 0.58f, 0.6f, 1 };
enum { S_BOX, S_NBTN, S_CHK10, S_OFF10, S_N };
static const char *const spr_names[S_N] = { "Box", "Button_Normal", "CheckBox_Checked", "CheckBox_Normal" };
/* toggles: the game's dark square check box, 10 x 10 (CheckBox_Normal off, CheckBox_Checked on = the same square with
   a tick), square corners and a brown frame, so an off box shows on the dark rows too. Not the 5 x 5 Settings sprites
   (CheckBox_Selected / CheckBox_Normal 5 x 5, a28dd17): their corner pixels and the off one's centre are see-through,
   so drawn at 20 they looked like round rings (owner 2026-10-20). Only the 10 x 10 CheckBox_Normal is taken. */
static int spr_ok(int k, Obj sp) {
    if ((k != S_CHK10 && k != S_OFF10) || !m_spr_rect) return 1;
    R4 r = CALL(m_spr_rect, R4, Obj)(sp, m_spr_rect);
    return r.w > 9.5f && r.w < 10.5f && r.h > 9.5f && r.h < 10.5f;
}
static uintptr_t h_spr[S_N], h_cinzel, h_body_font, h_body_mat, h_plain_mat, h_title_mat;   /* per language: title font, body font,
                                                                     body / field / small-title materials (the game's look) */
static int64_t frames, scan_last = -100000; static int scans, scan_done, kit_used;
static Obj ui_prefab(int type);
static void prefab_touch(int type) { (void)ui_prefab(type); }
/* the game's font for a style in the current language: LanguageSetting.GetFont(FontStyleType) (EasyRead 0 = body,
   VeryDecorative 2 = titles; zh-Hans, zh-Hant and ja have their own faces). NULL when the method is missing. */
static int kit_ready_fonts;                                          /* templates seen: the game's font tables are up */
static Obj lang_font(int style) {
    if (!m_ls_font || !kit_ready_fonts) return NULL;
    Obj f = CALL(m_ls_font, Obj, int32_t)(style, m_ls_font);
    return alive(f) ? f : NULL;
}
static Obj title_font(void) {                                        /* h_cinzel: the language's title face */
    Obj f = hget(h_cinzel); if (f) return f;
    f = lang_font(2);
    if (f) { hset(&h_cinzel, f); char nm[96]; obj_name(f, nm, sizeof nm); dsl_log("ui: title font \"%s\" (LanguageSetting.GetFont, %s)", nm, dsi_code()); }
    return f;
}
static double clock_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
static void assets_scan(int force) {
    /* at most once a second as well: a hidden soft-restart game runs uncapped (thousands of frames a second) and
       300 frames went by in 0.2 s, so it scanned every sprite 30 times during its boot */
    static double scan_t;
    if (scan_done || (!force && (!kit_used || frames - scan_last < 300 || scans >= 30 || clock_s() - scan_t < 1.0))) return;
    scan_last = frames; scans++; scan_t = clock_s();
    prefab_touch(8); prefab_touch(2); prefab_touch(13); prefab_touch(5);   /* load popup prefabs: their sprites too (as mpmenu) */
    Obj arr = find_all(K.Sprite); int n = arr_n(arr), found = 0;
    for (int i = 0; i < n; i++) {
        Obj sp = arr_at(arr, i); char nm[64]; obj_name(sp, nm, sizeof nm);
        for (int k = 0; k < S_N; k++) if (!hget(h_spr[k]) && !strcmp(nm, spr_names[k]) && spr_ok(k, sp)) { hset(&h_spr[k], sp); found++; }
    }
    if (!title_font() && !m_ls_font) {                                 /* no LanguageSetting.GetFont: Cinzel by name, as before */
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
static Obj asset_mat(Obj font) { return !alive(font) ? NULL : m_asset_mat ? CALL(m_asset_mat, Obj, Obj)(font, m_asset_mat) : f_asset_mat > 0 ? AT(font, f_asset_mat, Obj) : NULL; }
/* The game's look per text role (owner 2026-10-24: "not the same as the rest of the menus"; it replaces the plain-material
   rule of 2026-10-13). Measured in the game's own menus (title column, Options, Load Campaign): body text, buttons, check
   box labels and choice values = the language's body font (LanguageSetting.GetFont(EasyRead)) + its SoftOutline16 material
   (LanguageSetting.GetMaterial(EasyRead, SoftOutline16), "Lora-Regular SDF OutLine16" in English); section heads = the title
   font + its SoftOutline16 material; window titles (24 and up, as the game's Options and Rename titles) and the name fields = the font's own
   plain material. Without LanguageSetting (an older game): the stock window body font (CommonNoticeView Txt_Notice) and the
   plain materials, as before. */
static Obj lang_mat(int style, int mat) {                            /* LanguageSetting.GetMaterial(FontStyleType, FontMaterialType) */
    if (!m_ls_mat || !kit_ready_fonts) return NULL;
    Obj m = CALL(m_ls_mat, Obj, int32_t, int32_t)(style, mat, m_ls_mat);
    return alive(m) ? m : NULL;
}
static int64_t body_last = -1000;
static void body_font(void) {
    if (hget(h_body_mat)) return;
    if (frames - body_last < 120) return; body_last = frames;
    Obj font = lang_font(0); const char *from = "LanguageSetting.GetFont";
    if (!font && !m_ls_font) {
        Obj v = notice_view(); Obj nt = v && f_nv_text >= 0 ? AT(v, f_nv_text, Obj) : NULL;
        font = isa(nt, K.TMP_Text) ? CALL(m_tmp_getfont, Obj, Obj)(nt, m_tmp_getfont) : NULL; from = "the notice box";
    }
    Obj plain = asset_mat(font), m = font ? lang_mat(0, 1) : NULL;
    if (!alive(plain)) return;
    hset(&h_plain_mat, plain); hset(&h_body_mat, m ? m : plain); hset(&h_body_font, font);
    char nm[96], pn[96]; obj_name(hget(h_body_mat), nm, sizeof nm); obj_name(plain, pn, sizeof pn);
    dsl_log("ui: body text font from %s (%s), material \"%s\" (fields \"%s\")", from, dsi_code(), nm, pn);
}
static Obj title_mat(void) {                                         /* section heads: the title font's SoftOutline16 material */
    Obj m = hget(h_title_mat); if (m) return m;
    if (!title_font() || !(m = lang_mat(2, 1))) return NULL;
    hset(&h_title_mat, m); char nm[96]; obj_name(m, nm, sizeof nm); dsl_log("ui: title text material \"%s\" (under 24)", nm);
    return m;
}
/* a clone of a stock DASText (title buttons): the font + material its own style has in the current language, as the
   game's DASText.RefreshOnRunTime sets them (without its text and wrap resets) */
static void das_lang_font(Obj t) {
    if (!isa(t, K.DASText) || f_das_style < 0 || !m_ls_font) return;
    int32_t st = AT(t, f_das_style, int32_t), fm = f_das_fmat >= 0 ? AT(t, f_das_fmat, int32_t) : 0;
    Obj f = lang_font(st); if (!f) return;
    CALL(m_tmp_font, void, Obj, Obj)(t, f, m_tmp_font);
    Obj m = m_ls_mat ? CALL(m_ls_mat, Obj, int32_t, int32_t)(st, fm, m_ls_mat) : NULL;
    if (alive(m) && m_tmp_shmat) CALL(m_tmp_shmat, void, Obj, Obj)(t, m, m_tmp_shmat);
}
static void txt_body_mat(Obj t, uintptr_t hm) {
    if (!isa(t, K.TMP_Text)) return;
    body_font();
    Obj f = hget(h_body_font), m = hget(hm);
    if (f) CALL(m_tmp_font, void, Obj, Obj)(t, f, m_tmp_font);
    if (m && m_tmp_shmat) CALL(m_tmp_shmat, void, Obj, Obj)(t, m, m_tmp_shmat);
}
static void txt_body(Obj t) { txt_body_mat(t, h_body_mat); }        /* body text, buttons, toggles, value rows */
static void txt_field(Obj t) { body_font(); txt_body_mat(t, h_plain_mat); }   /* text fields: plain, as the game's name fields */
static void fit_style(Obj t, float size, int wrap);
static void txt_style(Obj t, int cinzel, float size, int align, int wrap) {
    if (!isa(t, K.TMP_Text)) return;
    Obj f = cinzel ? title_font() : NULL;
    if (f) {
        CALL(m_tmp_font, void, Obj, Obj)(t, f, m_tmp_font);
        Obj m = size < 24 ? title_mat() : NULL; if (!m) m = asset_mat(f);   /* section head: SoftOutline16; window title: plain */
        if (alive(m) && m_tmp_shmat) CALL(m_tmp_shmat, void, Obj, Obj)(t, m, m_tmp_shmat);
    }
    CALL(m_tmp_auto, void, Obj, bool)(t, false, m_tmp_auto);
    CALL(m_tmp_size, void, Obj, float)(t, size, m_tmp_size);
    CALL(m_tmp_align, void, Obj, int)(t, align, m_tmp_align);         /* 513 middle-left, 514 centre, 516 middle-right, 257.. top */
    CALL(m_tmp_wrap, void, Obj, int)(t, wrap, m_tmp_wrap);             /* TextWrappingModes 0 none, 1 normal */
    CALL(m_tmp_overflow, void, Obj, int)(t, 0, m_tmp_overflow);
    CALL(m_tmp_rich, void, Obj, bool)(t, true, m_tmp_rich);
    gr_raycast(t, 0);
    fit_style(t, size, wrap);                                         /* too long for its rect: auto size (text fit) */
}

/* ------------------------------------------------------------------ text fit (multiplayer ext/i18n/CJK_FIT.md)
   How the game fits text: short labels and buttons in a fixed box use TMP auto size (min about 0.75 x max, small buttons
   down to 8-18); long text keeps its size and its box grows (ContentSizeFitter, preferred height). It never changes the
   size per language. Here: every text styled by txt_style keeps its design size. Once per frame, a text whose content or
   rect changed is measured at that size (TMP preferredWidth / preferredHeight). It fits: as before, no auto size (English
   that fits looks exactly as before). It does not fit: auto size from the design size down to fit_min() (0.75 x, the
   game's), and a one-line label wraps when its rect holds two lines. Still too long at 0.75: the game's way for long
   text, the text keeps its design size and wraps to more lines (the line grows; it draws past its rect, centred); a box
   we own grows to DSLUi.text_height. Same code as multiplayer's mpmenu.c. */
#define FIT_N 2048                                                   /* open addressing by object address */
typedef struct { Obj t; uintptr_t h; float size, w, h_; uint32_t hash; uint8_t wrap, on, dirty, multi, stock, one; } FitE;   /* one: never two lines */
typedef struct { float x, y, w, h; } FRect;
static FitE fit_tab[FIT_N];
static int fit_count;
static int fit_ok(void) { return m_tmp_fsmin && m_tmp_fsmax && m_tmp_pw && m_tmp_ph && m_rt_rect && m_tmp_getsize && m_tmp_getauto; }
static float fit_min(float size) { float m = (float)(int)(size * 0.75f + 0.5f); return m < 8 ? (size < 8 ? size : 8) : m; }   /* the game's 0.75 */
static FitE *fit_find(Obj t, int add) {
    if (!t || !fit_ok()) return NULL;
    size_t i = ((uintptr_t)t >> 4) * 2654435761u % FIT_N, tomb = (size_t)-1;
    for (int n = 0; n < FIT_N; n++, i = (i + 1) % FIT_N) {
        FitE *e = &fit_tab[i];
        if (e->t == t) return e;
        if (e->t == (Obj)1) { if (tomb == (size_t)-1) tomb = i; continue; }
        if (!e->t) {
            if (!add || fit_count >= FIT_N * 3 / 4) return NULL;    /* full: that text keeps the old way (no fit) */
            if (tomb != (size_t)-1) e = &fit_tab[tomb];
            memset(e, 0, sizeof *e); e->t = t; e->h = hnew(t); fit_count++;
            return e;
        }
    }
    return NULL;
}
static void fit_drop(FitE *e) { hfree(&e->h); memset(e, 0, sizeof *e); e->t = (Obj)1; fit_count--; }
static uint32_t fit_hash(const char *s) { uint32_t h = 2166136261u; for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 16777619u; return h; }
static void fit_auto(Obj t, int on) { CALL(m_tmp_auto, void, Obj, bool)(t, on != 0, m_tmp_auto); }
static void fit_set(Obj t, int autosize, float size, int wrap) {    /* as styled at this size, wrap on / off */
    fit_auto(t, autosize); CALL(m_tmp_size, void, Obj, float)(t, size, m_tmp_size); CALL(m_tmp_wrap, void, Obj, int)(t, wrap, m_tmp_wrap);
}
typedef struct { float x, y; } FV2;
static FV2 fit_pref(Obj t, float w) {                                /* preferred width / height, wrapped at w (<= 0: its rect) */
    if (w > 0 && m_tmp_prefv) return CALL(m_tmp_prefv, FV2, Obj, float, float)(t, w, 100000.f, m_tmp_prefv);
    void *pw = vmi(t, m_tmp_pw), *ph = vmi(t, m_tmp_ph);
    return (FV2){ CALL(pw, float, Obj)(t, pw), CALL(ph, float, Obj)(t, ph) };
}
static void fit_style(Obj t, float size, int wrap) {
    FitE *e = fit_find(t, 1); if (!e) return;
    CALL(m_tmp_fsmax, void, Obj, float)(t, size, m_tmp_fsmax);       /* the design size */
    CALL(m_tmp_fsmin, void, Obj, float)(t, fit_min(size), m_tmp_fsmin);
    if (e->size == size && e->wrap == (wrap != 0) && e->on) {        /* styled again the same way: keep the fit (no flicker) */
        if (e->on == 4) { fit_set(t, 0, size, 1); return; }           /* grows: design size, more lines */
        fit_auto(t, 1); if (e->on == 2) CALL(m_tmp_wrap, void, Obj, int)(t, 1, m_tmp_wrap);
        return;
    }
    e->size = size; e->wrap = wrap != 0; e->on = 0; e->dirty = 1;
}
static void fit_text(Obj t, const char *s) {
    FitE *e = fit_find(t, 0); if (!e) return;
    uint32_t h = fit_hash(s); if (h != e->hash) { e->hash = h; e->dirty = 1; e->multi = s && strchr(s, '\n') != NULL; }
}
static void fit_one(FitE *e, Obj t, float w, float h) {
    if (w < 4 || h < 4) return;                                       /* not placed yet */
    if (e->on) fit_set(t, 0, e->size, e->wrap);
    void *pw = vmi(t, m_tmp_pw), *ph = vmi(t, m_tmp_ph);
    int over = !e->wrap ? CALL(pw, float, Obj)(t, pw) > w + 1 : CALL(ph, float, Obj)(t, ph) > h * 1.1f + 2;
    e->on = 0;
    if (over) {
        float mn = fit_min(e->size);
        int two = !e->wrap && !e->multi && !e->one && h >= 2.7f * mn;   /* a one-line label that may use two lines */
        fit_set(t, 0, mn, e->wrap || two);                           /* does it fit at 0.75? */
        int fits = e->wrap || two ? fit_pref(t, w).y <= h * 1.1f + 2 : fit_pref(t, 0).x <= w + 1;
        fit_set(t, 0, e->size, e->wrap);
        if (!fits) {                                                 /* the game's long text: design size, more lines */
            e->on = 4; fit_set(t, 0, e->size, 1);
            char tx[64] = ""; utf8_of(tmp_get(t), tx, sizeof tx);
            dsl_log("ui: text fit: \"%s\" is too long for %.0fx%.0f even at %.0f: size %.0f, wraps (%.0f high)", tx, w, h, mn, e->size, fit_pref(t, w).y);
            return;
        }
        e->on = two ? 2 : 1;                                         /* 2: a one-line label that may use two lines */
        CALL(m_tmp_fsmin, void, Obj, float)(t, mn, m_tmp_fsmin);
        CALL(m_tmp_fsmax, void, Obj, float)(t, e->size, m_tmp_fsmax);
        if (e->on == 2) CALL(m_tmp_wrap, void, Obj, int)(t, 1, m_tmp_wrap);
        fit_auto(t, 1);
        char tx[64] = ""; utf8_of(tmp_get(t), tx, sizeof tx);
        dsl_log("ui: text fit: \"%s\" is too long for %.0fx%.0f: auto size %.0f-%.0f%s", tx, w, h, mn, e->size, e->on == 2 ? ", two lines" : "");
    }
}
/* once per frame (after the modules' frame): changed texts now; every text's rect every 15 frames (a later layout) */
static void fit_frame(void) {
    if (!fit_count) return;
    int all = frames % 15 == 0;
    for (int i = 0; i < FIT_N; i++) {
        FitE *e = &fit_tab[i];
        if (!e->t || e->t == (Obj)1 || (!e->dirty && !all)) continue;
        Obj t = hget(e->h);
        if (!t) { fit_drop(e); continue; }
        if (!on_tree(t)) continue;                                    /* measured once it shows */
        Obj rt = tr_of(t); if (!isa(rt, K.RectTransform)) continue;
        FRect r = CALL(m_rt_rect, FRect, Obj)(rt, m_rt_rect);
        if (!e->dirty && r.w == e->w && r.h == e->h_) continue;
        int first = e->dirty; e->w = first ? -1 : r.w; e->h_ = r.h; e->dirty = 0;   /* -1: measured again at the next full pass */
        fit_one(e, t, r.w, r.h);
    }
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
static Obj div_tpl(void);
static int templates_ready(void) {
    if (hget(h_tpl_btn) && hget(h_tpl_img) && hget(h_tpl_txt)) return 1;
    Obj v = title_view(); if (!v || f_tv_credit < 0) return 0;
    Obj src = AT(v, f_tv_credit, Obj), hd = holder();
    if (!isa(src, K.TitleButton) || !hd) return 0;
    kit_ready_fonts = 1;                                             /* the title is up: the game's font tables too */
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
    div_tpl();                                                       /* the ornament line too, while the title is up */
    dsl_log("ui: templates from the title screen: button %s, image %s (%d children removed), text %s",
            hget(h_tpl_btn) ? "ok" : "MISSING", hget(h_tpl_img) ? "ok" : "MISSING", imc ? child_count(imc) : -1, hget(h_tpl_txt) ? "ok" : "MISSING");
    return hget(h_tpl_btn) && hget(h_tpl_img) && hget(h_tpl_txt);
}
/* the title menu's ornament line (owner 2026-10-25: the filter panel's divider = the line between the title's game buttons
   and Settings): the title column's child that is no title button, holds an Image and is wider than tall, cloned once
   into the template holder (so it also shows in a world: Esc > Load) */
static uintptr_t h_tpl_div; static float div_w0, div_h0;
static Obj div_tpl(void) {
    Obj d = hget(h_tpl_div); if (d) return d;
    static int64_t last = -1000; static int said; if (frames - last < 60) return NULL;
    last = frames;
    Obj v = title_view(); if (!v || f_tv_credit < 0 || !m_rt_rect) return NULL;
    Obj col = tr_parent(AT(v, f_tv_credit, Obj)), hd = holder(); if (!col || !hd) return NULL;
    char names[600] = "";
    for (int i = 0, n = child_count(col); i < n; i++) {
        Obj c = child_at(col, i); char nm[64]; obj_name(go_of(c), nm, sizeof nm);
        int btn = comp_get(c, K.TitleButton) != NULL;
        if (strlen(names) < 500) { strlcat(names, nm, sizeof names); strlcat(names, btn ? "(button) " : " ", sizeof names); }
        if (btn || !first_image(tr_of(c), 0) || !isa(tr_of(c), K.RectTransform)) continue;
        FRect r = CALL(m_rt_rect, FRect, Obj)(tr_of(c), m_rt_rect);
        if (r.h <= 0 || r.w < 4 * r.h) continue;
        Obj cl = inst(go_of(c), hd); if (!cl) continue;
        hset(&h_tpl_div, cl); div_w0 = r.w; div_h0 = r.h;
        dsl_log("ui: the title's ornament line \"%s\" (%.0f x %.0f) kept as the divider template", nm, r.w, r.h);
        return cl;
    }
    if (!said++) dsl_log("ui: no ornament line in the title column (children: %s)", names);
    return NULL;
}
DSLObj dsl_ui_menu_divider(DSLObj parent, float x, float y, float w, float *h) {
    if (h) *h = 0;
    Obj d = parent ? div_tpl() : NULL, c = d ? inst(d, (Obj)parent) : NULL; if (!c) return NULL;
    float hh = div_h0 * w / div_w0; place_tl(c, x, y, w, hh);
    if (h) *h = hh;
    return (DSLObj)tr_of(c);
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
/* the boot progress line (dsl_soft.c, A from the first scene to the title, owner 2026-10-17: the screen never looks
   dead). The game's own loading screen draws only once its UI bundle is in (Mac: 7-8 s into the boot, after LoadFont):
   before that this line is all there is under the Unity splash. Its own overlay canvas, plain Image quads (no sprite,
   no font, no template), so it draws from the first scene on. Colours of the soft-restart strip (ui/dsl_progress.c);
   bottom, above the game's NOW LOADING. frac < 0: removed. */
static int kit_ok;
static uintptr_t h_bb, h_bbfill;
static Obj bb_quad(Obj parent, uint32_t rgba, V2 amin, V2 amax, V2 size, V2 pos) {
    Obj g = new_go("dsl_boot_line", parent); Obj im = g ? comp_add(g, K.Image) : NULL;
    if (!im) return NULL;
    img_set(im, NULL, rgba_col(rgba, C_WHITE), 0);
    rt_set(im, amin, amax, (V2){ 0, 0 }, size, pos);
    return im;
}
/* the publisher logos of the game's own splash (Unity's splash sprites "CanOpeners", "WhisperGames", loaded with the
   player settings before the first frame), drawn on our own overlay canvas so that their time on screen follows the
   real load (dsl_soft.c, owner 2026-10-17 evening). Unity's splash then only needs CancelSplashScreen (no
   SplashScreen.Begin / SetTime: on D3D11 SetTime left only the grey background). Same background colour, place and
   size as the stock splash (960x540: first logo box 383,233-577,308). 0 = both sprites found. */
#define LOGO_W 1204.f                                                   /* calibrated against the stock splash */
static uintptr_t h_lg_spr[2], h_lg_root, h_lg_img; static int lg_cur = -1;
int dsl_ui_logos_grab(void) {
    static const char *const want[2] = { "CanOpeners", "WhisperGames" };
    if (!kit_ok || !K.Sprite || !K.Image || !K.Canvas) return -1;
    Obj a = find_all(K.Sprite); int n = arr_n(a), got = 0;
    for (int i = 0; i < n; i++) {
        Obj sp = arr_at(a, i); char nm[80]; obj_name(sp, nm, sizeof nm);
        for (int k = 0; k < 2; k++) if (!h_lg_spr[k] && !strcmp(nm, want[k])) { h_lg_spr[k] = hnew(sp); got++; }
    }
    dsl_log("ui: splash logos: %d of 2 found (%d sprites loaded)", got, n);
    return got == 2 ? 0 : -1;
}
void dsl_ui_logos(int idx, float alpha) {
    Obj root = hget(h_lg_root);
    if (idx < 0) { if (root) { destroy_obj(go_of(root)); dsl_log("ui: splash logos removed"); } hfree(&h_lg_root); hfree(&h_lg_img); lg_cur = -1; return; }
    if (!root) {
        if (!kit_ok || !m_cv_mode) return;
        Obj g = new_go("dsl_boot_logos", NULL); Obj cv = g ? comp_add(g, K.Canvas) : NULL;
        if (!cv) return;
        if (m_obj_dontdestroy) CALL(m_obj_dontdestroy, void, Obj)(g, m_obj_dontdestroy);
        CALL(m_cv_mode, void, Obj, int)(cv, 0, m_cv_mode);
        if (m_cv_order) CALL(m_cv_order, void, Obj, int)(cv, 31990, m_cv_order);          /* under the progress line */
        Obj cs = comp_add(g, K.CanvasScaler);
        if (cs && m_cs_mode) { CALL(m_cs_mode, void, Obj, int)(cs, 1, m_cs_mode); CALL(m_cs_ref, void, Obj, V2)(cs, (V2){ 1920, 1080 }, m_cs_ref);
                               CALL(m_cs_match, void, Obj, float)(cs, 1.0f, m_cs_match); }                /* by height, as the splash */
        root = tr_of(g);
        Obj bg = bb_quad(root, 0x1F1F20FFu, (V2){ 0, 0 }, (V2){ 1, 1 }, (V2){ 0, 0 }, (V2){ 0, 0 });
        Obj lg = new_go("dsl_boot_logo", root); Obj im = lg ? comp_add(lg, K.Image) : NULL;
        if (!bg || !im) { destroy_obj(g); return; }
        rt_set(im, (V2){ 0.5f, 0.5f }, (V2){ 0.5f, 0.5f }, (V2){ 0.5f, 0.5f }, (V2){ LOGO_W, LOGO_W / 2 }, (V2){ 0, 0 });
        hset(&h_lg_root, root); hset(&h_lg_img, im); lg_cur = -1;
        dsl_log("ui: splash logos shown");
    }
    Obj im = hget(h_lg_img); if (!im) return;
    if (idx != lg_cur) { Obj sp = idx < 2 ? hget(h_lg_spr[idx]) : NULL; img_set(im, sp, C_WHITE, 0); lg_cur = idx; }
    gr_color(im, (Col){ 1, 1, 1, alpha < 0 ? 0 : alpha > 1 ? 1 : alpha });
}
void dsl_ui_boot_line(float frac) {
    Obj root = hget(h_bb);
    if (frac < 0) { if (root) { destroy_obj(go_of(root)); dsl_log("ui: boot progress line removed"); } hfree(&h_bb); hfree(&h_bbfill); return; }
    if (!root) {
        if (!kit_ok || !K.Canvas || !K.Image || !m_cv_mode) return;
        Obj g = new_go("dsl_boot_line", NULL); Obj cv = g ? comp_add(g, K.Canvas) : NULL;
        if (!cv) return;
        if (m_obj_dontdestroy) CALL(m_obj_dontdestroy, void, Obj)(g, m_obj_dontdestroy);   /* kept over the boot -> title scene load */
        CALL(m_cv_mode, void, Obj, int)(cv, 0, m_cv_mode);
        if (m_cv_order) CALL(m_cv_order, void, Obj, int)(cv, 32000, m_cv_order);
        Obj cs = comp_add(g, K.CanvasScaler);
        if (cs && m_cs_mode) { CALL(m_cs_mode, void, Obj, int)(cs, 1, m_cs_mode); CALL(m_cs_ref, void, Obj, V2)(cs, (V2){ 1920, 1080 }, m_cs_ref);
                               CALL(m_cs_match, void, Obj, float)(cs, 0.5f, m_cs_match); }
        root = tr_of(g);
        Obj frame = bb_quad(root, 0x9A6A4BFFu, (V2){ 0, 0 }, (V2){ 1, 0 }, (V2){ -256, 10 }, (V2){ 128, 120 });   /* copper */
        Obj track = frame ? bb_quad(frame, 0x2A2530FFu, (V2){ 0, 0 }, (V2){ 1, 1 }, (V2){ 0, -4 }, (V2){ 0, 2 }) : NULL;   /* 6 high */
        Obj fill = track ? bb_quad(track, 0xC98A52FFu, (V2){ 0, 0 }, (V2){ 0, 1 }, (V2){ 0, 0 }, (V2){ 0, 0 }) : NULL;
        if (!fill) { destroy_obj(g); return; }
        hset(&h_bb, root); hset(&h_bbfill, fill);
        dsl_log("ui: boot progress line shown");
    }
    Obj fill = hget(h_bbfill); Obj rt = fill ? tr_of(fill) : NULL;
    if (rt && isa(rt, K.RectTransform)) CALL(m_rt_amax, void, Obj, V2)(rt, (V2){ frac > 1 ? 1 : frac, 1 }, m_rt_amax);
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
enum { W_PANEL = 1, W_TEXT, W_BUTTON, W_TOGGLE, W_VALUE, W_FIELD, W_SCROLL, W_IMAGE, W_SLIDER };
typedef struct W {
    int id, kind, enabled, on, index, n, was_focused;
    int vmin, vstep;                                                /* slider: value = vmin + index * vstep */
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
typedef struct { Obj target; uintptr_t h; char title[160], body[2048]; int below; } TipE;   /* below: dsl_ui_tip_below */
#define TMAX 256
static TipE tips[TMAX]; static int ntips;

static Obj parent_or_layer(DSLObj p) { Obj t = p ? tr_of((Obj)p) : layer(); return t; }
static int ready_basic(void) { kit_used = 1; return templates_ready(); }

/* ------------------------------------------------------------------ widgets */
static void esc_guard_root(DSLObj root);
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
    DSLObj r = w_done(wd, tr_of(im), NULL);
    if (!parent) esc_guard_root(r);                                  /* test copies: a window of ours must have an Escape registration */
    return r;
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
/* the menu button's frame Button_Normal (131 x 26 px, a 1 px line, no 9-slice border) is drawn stretched: a narrow
   button's side lines got thinner than one screen pixel and vanished at 960x540 (bug report Cancel / Send, Refresh). A
   copy of it with a 2 px border, drawn sliced at 2 units per pixel (the title column's buttons: ~2.2), keeps every line. */
static uintptr_t h_bspr, h_bspr_src;
static void btn_frame(Obj im) {
    Obj stock = img_sprite(im), s = hget(h_bspr);
    if (!stock || !m_spr_create7 || !m_spr_tex || !m_spr_texrect || !m_img_type) return;
    if (stock == s) return;
    if (!s || hget(h_bspr_src) != stock) {
        Obj tex = CALL(m_spr_tex, Obj, Obj)(stock, m_spr_tex); R4 r = CALL(m_spr_texrect, R4, Obj)(stock, m_spr_texrect);
        if (!alive(tex) || r.w < 5 || r.h < 5) return;
        s = CALL(m_spr_create7, Obj, Obj, R4, V2, float, uint32_t, int, V4)(tex, r, (V2){ 0.5f, 0.5f }, 50.f, 0, 0, (V4){ 2, 2, 2, 2 }, m_spr_create7);   /* FullRect */
        if (!alive(s)) { dsl_log("ui: button frame: Sprite.Create failed, stretched frame kept"); return; }
        hset(&h_bspr, s); hset(&h_bspr_src, stock);
        dsl_log("ui: button frame: sliced copy of the stock frame (%.0fx%.0f px, border 2)", r.w, r.h);
    }
    CALL(m_img_sprite, void, Obj, Obj)(im, s, m_img_sprite);
    CALL(m_img_type, void, Obj, int)(im, 1, m_img_type);                /* Sliced */
    if (m_img_ppm) CALL(m_img_ppm, void, Obj, float)(im, 1.f, m_img_ppm);
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
    if (bg) { stretch(bg); gr_raycast(bg, 1); btn_frame(bg); }       /* the frame is the click target */
    Obj l = btn_label(bc);
    if (isa(l, K.TMP_Text)) { txt_body(l); txt_style(l, 0, fs, 514, 0); gr_color(l, C_MAIN); stretch(l); tmp_set(l, label); }
    place_tl(bc, x, y, w, h);
    return bc;
}
static float btn_fs(float h) { return h >= 44 ? 18 : h >= 30 ? 16 : 14; }
/* The close rule's guard (owner 2026-10-25: one close control, the game's red X; kit close_x). Test copies only: a text
   "X" button put straight on a window of ours (a root panel) logs UI-RULE close-text-x "<the window's first text>"; the kit test
   (test/kitlook_test.py) fails on it. A text X in a list (a group's delete "x") is not a window's close: not flagged. */
static int guard_on = -1;
static void guard_text(Obj t, char *out, size_t cap, int depth);
static void close_x_guard(DSLObj parent, const char *label) {
    if (guard_on < 0) guard_on = dsl_soft_test_copy() != NULL;
    if (guard_on != 1 || !parent || !label || (strcmp(label, "X") && strcmp(label, "x") && strcmp(label, "\xC3\x97"))) return;
    W *pw = w_of(parent); Obj pr = pw ? w_root(pw) : (Obj)parent;
    if (!pw || pw->kind != W_PANEL || tr_parent(pr) != hget(h_layer)) return;
    char t[200] = ""; guard_text(pr, t, sizeof t, 0);
    for (char *c = t; *c; c++) if (*c == '\n') *c = ' ';
    dsl_log("UI-RULE close-text-x \"%s\": a window of ours closes with a text X (dsl_window_std, or dsl_close_x: the game's red X)", t[0] ? t : "(no text)");
}
static DSLObj k_button(DSLObj parent, float x, float y, float w, float h, const char *label, dsl_ui_cb click, void *ud) {
    if (!ready_basic()) return NULL;
    Obj p = parent_or_layer(parent); if (!p) return NULL;
    W *wd = w_new(W_BUTTON); if (!wd) return NULL;
    Obj b = mk_btn(p, x, y, w, h, label, btn_fs(h), TOKEN(wd->id, P_CLICK), &wd->h_act[0]);
    if (!b) { w_free(nws - 1); return NULL; }
    wd->cb = click; wd->ud = ud; wd->w = w; wd->h = h;
    hset(&wd->h_btn, b); hset(&wd->h_label, btn_label(b));
    close_x_guard(parent, label);
    return w_done(wd, tr_of(b), NULL);
}

/* toggle row: [label ............ [x]] (the button's frame is the row; check box at the right) */
#define TOGGLE_H 36
#define TOGGLE_BOX 20          /* 2x the game's 10 x 10 check box: whole pixels at every window size */
static void toggle_look(W *w) {
    /* the game's dark square: CheckBox_Normal (off) / CheckBox_Checked (on, its tick inside), the game's disabled tint
       (Toggle m_DisabledColor). No CheckBox_Checked loaded: the off square + a light mark inside. */
    Obj ck = hget(w->h_b), box = hget(w->h_a);
    static const Col C_DIS = { 0.784f, 0.784f, 0.784f, 0.502f }, C_TICK = { 0.843f, 0.839f, 0.761f, 1 };   /* d7d6c2: CheckBox_Checked's tick */
    Obj on_s = spr(S_CHK10), off_s = spr(S_OFF10), s = w->on && on_s ? on_s : off_s;
    if (box) img_set(box, s, !w->enabled ? C_DIS : s ? C_WHITE : C_BOXF, 0);
    int tick = w->on && !on_s;
    if (ck) { if (tick) img_set(ck, NULL, !w->enabled ? C_DIS : C_TICK, 0); go_active(ck, tick); }
}
static void toggle_layout(W *w) {
    Obj l = hget(w->h_label), box = hget(w->h_a), ck = hget(w->h_b);
    if (l) { place_tl(l, 12, 0, w->w - 60, TOGGLE_H); txt_style(l, 0, 16, 513, 0); }
    if (box) place_tl(box, w->w - 24 - (TOGGLE_BOX / 2.f), (TOGGLE_H - TOGGLE_BOX) / 2.f, TOGGLE_BOX, TOGGLE_BOX);
    if (ck) place_tl(ck, 6, 6, TOGGLE_BOX - 12, TOGGLE_BOX - 12);
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

/* slider (mod settings): [label   ====o====  value]. The bar is a clone of the game's own SubUI_Slider of the Options
   window (GameOptionView prefab, General: camera movement speed), so it looks and drags like the game's; its own
   onValueChanged listeners are left as they are (none are added) and the value is polled every frame (sliders_frame).
   The Slider counts steps 0..n (wholeNumbers); the value is vmin + index * vstep. NULL when the template is missing. */
static uintptr_t h_tpl_slider;
static Obj slider_tpl(void) {
    Obj t = hget(h_tpl_slider); if (t) return t;
    static int64_t last = -1000; if (frames - last < 60) return NULL; last = frames;
    if (f_gov_general < 0 || f_gs_cam < 0 || f_ss_slider < 0 || !m_sl_get || !m_sl_set || !m_sl_setmin || !m_sl_setmax) return NULL;
    Obj v = ui_prefab(6), gs = isa(v, K.OptionView) ? AT(v, f_gov_general, Obj) : NULL, ss = isa(gs, K.GeneralSetting) ? AT(gs, f_gs_cam, Obj) : NULL;
    Obj hd = holder();
    if (!isa(ss, K.SubSlider) || !hd) { dsl_log("ui: slider template: no Options window prefab slider (%p %p)", v, gs); return NULL; }
    Obj c = inst(go_of(ss), hd); Obj cs = c ? comp_get(c, K.SubSlider) : NULL;
    hset(&h_tpl_slider, cs);
    dsl_log("ui: slider template from the Options window (camera movement speed): %s", cs ? "ok" : "FAILED");
    return cs;
}
static void slider_text(W *w) {
    Obj vt = hget(w->h_c); if (!vt) return;
    char b[32]; snprintf(b, sizeof b, "%d", w->vmin + w->index * w->vstep); tmp_set(vt, b);
}
static void slider_layout(W *w) {
    float lw = w->w * 0.36f, cw = w->w - lw - 12;
    place_tl(hget(w->h_label), 12, 0, lw - 16, VALUE_H);
    place_tl(hget(w->h_b), lw, 0, cw, VALUE_H);
    Obj sl = hget(w->h_a), vt = hget(w->h_c);
    if (sl && m_tr_setparent) place_tl(tr_of(sl), 0, (VALUE_H - 20) / 2.f, cw - 72, 20);
    if (vt && m_tr_setparent) place_tl(tr_of(vt), cw - 64, 0, 64, VALUE_H);
}
static DSLObj k_slider(DSLObj parent, float x, float y, float w, const char *label, int min, int max, int step, int value,
                       dsl_ui_cb change, void *ud) {
    if (!ready_basic()) return NULL;
    if (step < 1) step = 1;
    if (max < min) max = min;
    Obj tpl = slider_tpl(), p = parent_or_layer(parent); if (!tpl || !p) return NULL;
    Obj row = inst(hget(h_tpl_img), p); if (!row) return NULL;
    go_active(row, 1); img_set(row, NULL, C_ROW1, 0); gr_material(row, NULL);
    place_tl(row, x, y, w, VALUE_H);
    W *wd = w_new(W_SLIDER); if (!wd) { destroy_obj(go_of(row)); return NULL; }
    Obj lb = mk_text(row, 0, 0, 10, 10, label, 15, DSL_TXT_LEFT, C_SUB);
    Obj c = inst(go_of(tpl), row); go_active(c, 1);
    Obj cs = c ? comp_get(c, K.SubSlider) : NULL, sl = cs ? AT(cs, f_ss_slider, Obj) : NULL, vt = cs && f_ss_value >= 0 ? AT(cs, f_ss_value, Obj) : NULL;
    if (!isa(sl, K.Slider)) { w_free(nws - 1); destroy_obj(go_of(row)); dsl_log("ui: slider clone has no Slider"); return NULL; }
    if (cs && f_ss_toggle >= 0) go_active(AT(cs, f_ss_toggle, Obj), 0);           /* the mute switch of the sound sliders */
    if (cs && f_ss_toggletext >= 0) go_active(AT(cs, f_ss_toggletext, Obj), 0);
    if (isa(vt, K.DASText) && f_das_usekey >= 0) AT(vt, f_das_usekey, bool) = false;
    /* the prefab is a block (its own title "Camera Movement Speed Scale" above the bar, value at the top right): the bar
       and the value move straight under the clone's root, everything else of it is hidden; slider_layout places them */
    Obj root = tr_of(c), srt = tr_of(sl), vrt = isa(vt, K.TMP_Text) ? tr_of(vt) : NULL;
    if (m_tr_setparent) {
        CALL(m_tr_setparent, void, Obj, Obj, bool)(srt, root, false, m_tr_setparent);
        if (vrt) CALL(m_tr_setparent, void, Obj, Obj, bool)(vrt, root, false, m_tr_setparent);
        for (int i = child_count(root) - 1; i >= 0; i--) { Obj ch = child_at(root, i); if (ch != srt && ch != vrt) go_active(ch, 0); }
        Obj bg = comp_get(root, K.Image); if (bg) gr_color(bg, (Col){ 0, 0, 0, 0 });   /* a frame of the block, if any */
    }
    if (vrt) txt_style(vt, 0, 16, 516, 0);                              /* right, middle; body font */
    hset(&wd->h_label, lb); hset(&wd->h_a, sl); hset(&wd->h_b, tr_of(c)); hset(&wd->h_c, isa(vt, K.TMP_Text) ? vt : NULL);
    int n = (max - min) / step;
    wd->vmin = min; wd->vstep = step; wd->n = n; wd->cb = change; wd->ud = ud; wd->w = w; wd->h = VALUE_H;
    if (m_sl_whole) CALL(m_sl_whole, void, Obj, bool)(sl, true, m_sl_whole);
    CALL(m_sl_setmin, void, Obj, float)(sl, 0, m_sl_setmin);
    CALL(m_sl_setmax, void, Obj, float)(sl, (float)(n > 0 ? n : 1), m_sl_setmax);
    int idx = (value - min) / step; wd->index = idx < 0 ? 0 : idx > n ? n : idx;
    { void *m = vmi(sl, m_sl_set); CALL(m, void, Obj, float)(sl, (float)wd->index, m); }
    slider_layout(wd); slider_text(wd);
    return w_done(wd, tr_of(row), NULL);
}
static int k_slider_get(DSLObj o) { W *w = w_of(o); return w && w->kind == W_SLIDER ? w->vmin + w->index * w->vstep : 0; }
static void k_slider_set(DSLObj o, int value) {
    W *w = w_of(o); if (!w || w->kind != W_SLIDER) return;
    int idx = (value - w->vmin) / w->vstep; w->index = idx < 0 ? 0 : idx > w->n ? w->n : idx;
    Obj sl = hget(w->h_a); if (sl) { void *m = vmi(sl, m_sl_set); CALL(m, void, Obj, float)(sl, (float)w->index, m); }
    slider_text(w);
}
static void sliders_frame(void) {                                    /* the player dragged: new step -> text + callback */
    int fire[32], nf = 0;
    for (int i = 0; i < nws; i++) {
        W *w = ws[i]; if (w->kind != W_SLIDER) continue;
        Obj sl = hget(w->h_a); if (!sl) continue;
        void *m = vmi(sl, m_sl_get); float v = CALL(m, float, Obj)(sl, m);
        int idx = (int)(v + 0.5f); if (idx < 0) idx = 0; if (idx > w->n) idx = w->n;
        if (idx == w->index) continue;
        w->index = idx; slider_text(w);
        if (w->enabled && w->cb && nf < 32) fire[nf++] = w->id;
    }
    for (int i = 0; i < nf; i++) { W *w = w_by_id(fire[i]); if (w && w->cb) w->cb(w->ud, w->rt); }
}
static void k_value_set(DSLObj v, int index) { W *w = w_of(v); if (w && w->kind == W_VALUE && index >= 0 && index < w->n) { w->index = index; value_text(w); } }

/* text field: a clone of a stock TMP_InputField, framed like a menu button (no magnifier), as mpmenu field_make */
static V2 rt_pos(Obj o); static Obj tr_find(Obj t, const char *path);
/* the text area ("Text Area", stretched): the stock save-name field keeps 35.8 units on its left for its magnifier, so the
   typed text started ~36 in. Here in_x on both sides; in_y < 0: the source's height and place (multiplayer field_area) */
static void field_area(Obj f, float in_x, float in_y) {
    Obj ta = tr_find(f, "Text Area"); if (!isa(tr_of(ta), K.RectTransform)) return;
    V2 sd = rt_size(ta), ap = rt_pos(ta);
    rt_set(ta, (V2){ 0, 0 }, (V2){ 1, 1 }, (V2){ 0.5f, 0.5f }, (V2){ -2 * in_x, in_y < 0 ? sd.y : -2 * in_y }, (V2){ 0, in_y < 0 ? ap.y : 0 });
}
static void in_settext(Obj f, const char *s) { if (isa(f, K.TMP_Input)) CALL(m_in_settext, void, Obj, Obj)(f, str(s), m_in_settext); }
static DSLObj k_field(DSLObj parent, float x, float y, float w, float h, const char *placeholder, const char *text, dsl_ui_cb submit, void *ud) {
    if (!ready_basic()) return NULL;
    Obj src = field_tpl(); Obj p = parent_or_layer(parent);
    if (!src || !p) return NULL;
    Obj f = inst(src, p); Obj fc = f ? comp_get(f, K.TMP_Input) : NULL;
    if (!fc) { if (f) destroy_obj(go_of(f)); return NULL; }
    go_active(fc, 1);
    Obj fim = comp_get(fc, K.Image), bim = first_image(tr_of(hget(h_tpl_btn)), 0), bs = img_sprite(bim);
    if (fim && bs) { CALL(m_img_sprite, void, Obj, Obj)(fim, bs, m_img_sprite); gr_color(fim, C_FIELD); btn_frame(fim); }
    float fs = h >= 40 ? 18 : 16;
    Obj tc = m_in_tc ? CALL(m_in_tc, Obj, Obj)(fc, m_in_tc) : NULL;
    if (isa(tc, K.TMP_Text)) { txt_field(tc); txt_style(tc, 0, fs, 513, 0); gr_color(tc, C_MAIN); }
    if (m_in_limit) CALL(m_in_limit, void, Obj, int)(fc, 256, m_in_limit);
    if (m_in_ctype) CALL(m_in_ctype, void, Obj, int)(fc, 0, m_in_ctype);   /* Standard */
    Obj ph = m_in_ph ? CALL(m_in_ph, Obj, Obj)(fc, m_in_ph) : NULL;
    if (isa(ph, K.TMP_Text)) { tmp_set(ph, placeholder ? placeholder : ""); txt_field(ph); txt_style(ph, 0, fs - 2, 513, 0); gr_color(ph, C_SUB); }
    in_settext(fc, text ? text : "");
    field_area(fc, 10, -1);
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
/* loader only (the Settings page's number field): digits only through the field's own validation, max_chars long */
void dsl_ui_field_number(DSLObj f, int max_chars) {
    W *w = w_of(f); Obj fc = w && w->kind == W_FIELD ? hget(w->h_a) : NULL; if (!fc) return;
    if (m_in_ctype) CALL(m_in_ctype, void, Obj, int)(fc, 2, m_in_ctype);   /* ContentType.IntegerNumber */
    if (m_in_limit && max_chars > 0) CALL(m_in_limit, void, Obj, int)(fc, max_chars, m_in_limit);
}
/* loader only (the bug report window, dsl_bugrep.c): a kit field takes many lines (Enter = new line), text from the
   top left, wrapped, max_chars long. Its text keeps its size (no text fit: a long text scrolls inside the field). */
void dsl_ui_field_multiline(DSLObj f, int max_chars) {
    W *w = w_of(f); Obj fc = w && w->kind == W_FIELD ? hget(w->h_a) : NULL; if (!fc) return;
    static void *m_lt; if (!m_lt) m_lt = mi_sig(K.TMP_Input, "set_lineType", 1, NULL);
    if (m_lt && *(void **)m_lt) CALL(m_lt, void, Obj, int)(fc, 2, m_lt);                /* LineType.MultiLineNewline */
    if (m_in_limit && max_chars > 0) CALL(m_in_limit, void, Obj, int)(fc, max_chars, m_in_limit);
    Obj tc = m_in_tc ? CALL(m_in_tc, Obj, Obj)(fc, m_in_tc) : NULL, ph = m_in_ph ? CALL(m_in_ph, Obj, Obj)(fc, m_in_ph) : NULL;
    /* the button frame is drawn stretched (not sliced): at 290 high its top and bottom borders were ~12 units, its sides
       ~4. A tall field gets its frame as two flat colours instead: the border colour, the dark inside 4 units in (the
       colours the frame has on screen); the text area fills it 10 / 8 units in */
    Obj fim = comp_get(fc, K.Image);
    if (fim) {
        img_set(fim, NULL, (Col){ 59 / 255.f, 35 / 255.f, 31 / 255.f, 1 }, 0); gr_material(fim, NULL); gr_raycast(fim, 1);   /* the click target stays */
        Obj in = inst(hget(h_tpl_img), fc);
        if (in) { go_active(in, 1); gr_material(in, NULL); img_set(in, NULL, (Col){ 20 / 255.f, 19 / 255.f, 22 / 255.f, 1 }, 0);
                  rt_set(in, (V2){ 0, 0 }, (V2){ 1, 1 }, (V2){ 0.5f, 0.5f }, (V2){ -8, -8 }, (V2){ 0, 0 }); set_sibling(in, 0); }
    }
    field_area(fc, 10, 8);
    Obj ts[2] = { tc, ph };
    for (int i = 0; i < 2; i++) {
        if (!isa(ts[i], K.TMP_Text)) continue;
        FitE *e = fit_find(ts[i], 0); if (e) fit_drop(e);
        fit_auto(ts[i], 0);
        CALL(m_tmp_align, void, Obj, int)(ts[i], 257, m_tmp_align);                      /* TopLeft */
        CALL(m_tmp_wrap, void, Obj, int)(ts[i], 1, m_tmp_wrap);
    }
}
int dsl_ui_field_focused(DSLObj f) {
    W *w = w_of(f); Obj fc = w && w->kind == W_FIELD ? hget(w->h_a) : NULL;
    return fc && m_in_focused && CALL(m_in_focused, bool, Obj)(fc, m_in_focused);
}

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
/* PNG bytes in memory -> Sprite (bilinear, clamp), as k_image_png; its texture handle to *h_tex, size to *tw / *th */
static Obj png_sprite(const unsigned char *data, size_t n, uintptr_t *h_tex, int *tw, int *th) {
    if (!m_tex_ctor || !m_tex_load || !m_spr_create || !K.Byte || !data || !n) return NULL;
    Obj bytes = I.array_new(K.Byte, n); if (!bytes) return NULL;
    memcpy((char *)bytes + 0x20, data, n);
    Obj tex = I.object_new(K.Texture2D); if (!tex) return NULL;
    CALL(m_tex_ctor, void, Obj, int, int, int, bool)(tex, 2, 2, 4, false, m_tex_ctor);     /* RGBA32, no mips */
    if (!CALL(m_tex_load, bool, Obj, Obj)(tex, bytes, m_tex_load)) return NULL;
    if (m_tex_filter) CALL(m_tex_filter, void, Obj, int)(tex, 1, m_tex_filter);
    if (m_tex_wrap) CALL(m_tex_wrap, void, Obj, int)(tex, 1, m_tex_wrap);
    if (m_tex_apply) CALL(m_tex_apply, void, Obj)(tex, m_tex_apply);
    *tw = m_tex_w ? CALL(m_tex_w, int, Obj)(tex, m_tex_w) : 0; *th = m_tex_h ? CALL(m_tex_h, int, Obj)(tex, m_tex_h) : 0;
    Obj sp = *tw > 0 && *th > 0 ? CALL(m_spr_create, Obj, Obj, R4, V2, float)(tex, (R4){ 0, 0, (float)*tw, (float)*th }, (V2){ 0.5f, 0.5f }, 100.0f, m_spr_create) : NULL;
    if (!alive(sp)) return NULL;
    hset(h_tex, tex);
    return sp;
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
/* placement (owner 2026-10-16 #3, 2026-10-25): the card goes OUTSIDE the panel the element sits in: at the panel's right
   edge, its top level with the element's top; else outside the panel's left edge; else right below the panel, at the
   element's left edge; else above it. A side counts only when the card is inside the screen there and covers neither the
   panel nor a window around it (every ancestor narrower than 3/4 of the screen: the Load window around the filter panel);
   no free side -> the side that covers least. Targets marked with dsl_ui_tip_below (the Load list's filter boxes: a card
   over the panel hid the other boxes, "barely usable", owner 2026-10-25): right below the panel at the panel's left edge
   first, then the free side nearest the element. */
#define TIP_KEEP 8
#define TIP_GAP 8.f
static float tip_panel[4], tip_el[4], tip_keep[TIP_KEEP][4]; static int tip_nkeep, tip_below;
static float screen_w(void) { return m_scr_w ? (float)CALL0(m_scr_w, int)(m_scr_w) : 1920.f; }
/* the panel of an element: the widest ancestor narrower than 3/4 of the screen (title column, Mods window, Load list);
   every such ancestor (and the panel) is a rect the card keeps off */
static void tip_panel_of(Obj target, const float el[4], float out[4]) {
    memcpy(out, el, 4 * sizeof *out); tip_nkeep = 0;
    float sw = screen_w(); Obj t = tr_of(target);
    for (int d = 0; d < 16 && t; d++) {
        t = tr_parent(t); float r[4];
        if (!t || !rect_screen(t, r)) break;
        if (r[2] - r[0] >= sw * 0.75f) break;
        if (r[2] - r[0] >= 1 && r[3] - r[1] >= 1 && tip_nkeep < TIP_KEEP) memcpy(tip_keep[tip_nkeep++], r, sizeof r);
        if (r[0] <= el[0] + 1 && r[2] >= el[2] - 1 && r[2] - r[0] >= out[2] - out[0]) memcpy(out, r, sizeof r);
    }
    if (!tip_nkeep) memcpy(tip_keep[tip_nkeep++], out, 4 * sizeof *out);
}
static float tip_cover(const float c[4]) {                          /* area of the keep-off rects the card covers */
    float a = 0;
    for (int k = 0; k < tip_nkeep; k++) {
        const float *q = tip_keep[k];
        float w = (c[2] < q[2] ? c[2] : q[2]) - (c[0] > q[0] ? c[0] : q[0]), h = (c[3] < q[3] ? c[3] : q[3]) - (c[1] > q[1] ? c[1] : q[1]);
        if (w > 0.5f && h > 0.5f) a += w * h;
    }
    return a;
}
/* the card's top-left corner (x, top; pixels, y up) for a card of w x h */
static void tip_choose(float w, float h, float *ox, float *otop) {
    float sw = screen_w(), sh = m_scr_h ? (float)CALL0(m_scr_h, int)(m_scr_h) : 1080.f;
    const float *P = tip_panel, *E = tip_el;
    float bx = tip_below ? P[0] : E[0];                                 /* below / above: the panel's left edge, else the element's */
    const float X[4] = { P[2] + TIP_GAP, P[0] - TIP_GAP - w, bx, bx }, TOP[4] = { E[3], E[3], P[1] - TIP_GAP, P[3] + TIP_GAP + h };   /* right, left, below, above */
    int pick = -1; float best = 0, cx[4], ct[4], cov[4]; int room[4];
    for (int i = 0; i < 4; i++) {
        float x = X[i], top = TOP[i];
        room[i] = i < 2 ? x >= 0 && x + w <= sw : top <= sh && top - h >= 0;   /* off the screen on its own side: no room */
        if (x + w > sw) x = sw - w;
        if (x < 0) x = 0;
        if (top > sh) top = sh;
        if (top - h < 0) top = h;                                       /* inside the screen (a long card slides up) */
        float c[4] = { x, top - h, x + w, top }; cx[i] = x; ct[i] = top; cov[i] = tip_cover(c);
        if (!room[i] || cov[i] > 0) continue;
        if (!tip_below) { if (pick < 0) pick = i; continue; }          /* the first free side in order */
        float dx = c[0] > E[2] ? c[0] - E[2] : E[0] > c[2] ? E[0] - c[2] : 0, dy = c[1] > E[3] ? c[1] - E[3] : E[1] > c[3] ? E[1] - c[3] : 0;
        float d = i == 2 ? -1 : dx * dx + dy * dy;                      /* below first, then the nearest free side */
        if (pick < 0 || d < best) { pick = i; best = d; }
    }
    if (pick < 0)                                                       /* no free side: the one that covers least */
        for (int i = 0; i < 4; i++) if (pick < 0 || cov[i] < cov[pick] - 0.5f || (room[i] && !room[pick] && cov[i] <= cov[pick] + 0.5f)) pick = i;
    *ox = cx[pick]; *otop = ct[pick];
}
static void tip_card_rect(float r[4]) { Obj etc = hget(h_tip_etc); if (!etc || !rect_screen(etc, r)) r[0] = r[1] = r[2] = r[3] = 0; }
static void tip_show_at(TipE *e, V2 pos) {
    Obj v = tooltip_view(); if (!v) return;
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
/* the stock card places itself two frames after the show (Tooltip_Base.DelayedAlignment: parked, layout, two frames,
   SetTooltipAlignment, _alignmentRoutine = null; no room on the right -> it flips over the element's panel), and a second
   show with the same id is ignored (TooltipUI keeps the session): so the card is MOVED once the stock alignment is done,
   right before the UI is drawn (h_cur_perform: the stock place never shows) */
static int tip_moves, tip_wait; static int64_t tip_frame0;
static void tip_hide(void) {
    if (!tip_shown) return;
    tip_blocks(hget(h_tip_etc), 1);
    tip_shown = 0; tip_moves = 0; hfree(&h_tip_shown_target); tip_cur_title[0] = 0;
    Obj v = tooltip_view(); if (v) CALL(m_tt_hide, void, Obj, Guid)(v, TIP_ID, m_tt_hide);
}
static void tip_show(TipE *e) {
    Obj v = tooltip_view(); if (!v || !tip_ok()) return;
    if (tip_shown) tip_hide();
    raise_view(v, 32050);
    float r[4]; if (!rect_screen(e->target, r)) return;
    memcpy(tip_el, r, sizeof r); tip_panel_of(e->target, r, tip_panel); tip_below = e->below;
    tip_show_at(e, (V2){ tip_panel[2] + TIP_GAP, r[3] });             /* pos = the card's top-left corner (stock first) */
    tip_moves = 3; tip_wait = 60; tip_frame0 = frames;                  /* a move is checked the next frame (up to 3) */
}
/* move a RectTransform by (dx, dy) screen pixels, through world space (its world corners against its screen rect) */
static int rt_move_screen(Obj o, float dx, float dy) {
    Obj rt = tr_of(o); float s[4]; static uintptr_t h_wc;
    if (!m_tr_wpos || !m_tr_setwpos || !isa(rt, K.RectTransform) || !rect_screen(rt, s) || s[2] - s[0] < 1 || s[3] - s[1] < 1) return 0;
    Obj arr = h_wc ? I.gchandle_get_target(h_wc) : NULL;
    if (!arr) { arr = I.array_new(K.Vector3, 4); if (!arr) return 0; h_wc = I.gchandle_new(arr, false); }
    CALL(m_rt_corners, void, Obj, Obj)(rt, arr, m_rt_corners);
    V3 *c = (V3 *)((char *)arr + 0x20), p = CALL(m_tr_wpos, V3, Obj)(rt, m_tr_wpos);
    p.x += dx * (c[2].x - c[0].x) / (s[2] - s[0]); p.y += dy * (c[2].y - c[0].y) / (s[3] - s[1]);
    CALL(m_tr_setwpos, void, Obj, V3)(rt, p, m_tr_setwpos);
    return 1;
}
int dsl_ui_move_screen(DSLObj o, float dx, float dy) { return o ? rt_move_screen((Obj)o, dx, dy) : 0; }
static void tip_place_frame(void) {
    if (!tip_shown || tip_moves <= 0) return;
    Obj etc = hget(h_tip_etc); if (!etc) { tip_moves = 0; return; }
    int busy = f_tb_align >= 0 ? AT(etc, f_tb_align, Obj) != NULL : frames - tip_frame0 < 3;   /* the stock alignment still to run */
    if (busy && --tip_wait > 0) return;
    float c[4]; tip_card_rect(c); float w = c[2] - c[0], h = c[3] - c[1];
    if (w <= 1 || h <= 1) { if (--tip_wait <= 0) tip_moves = 0; return; }
    float x, top; tip_choose(w, h, &x, &top);
    float dx = x - c[0], dy = top - c[3];
    if (dx > -0.75f && dx < 0.75f && dy > -0.75f && dy < 0.75f) { tip_moves = 0; return; }
    if (--tip_moves == 0 || !rt_move_screen(etc, dx, dy)) { tip_moves = 0; dsl_log("ui: hover card '%s' could not be moved (%.0f, %.0f px off)", tip_cur_title, dx, dy); }
}
static void tips_frame(void) {
    if (!ntips && !tip_shown) return;
    if (dsl_soft_input_blocked()) { if (tip_shown) tip_hide(); return; }   /* a soft restart's loading screen covers the title */
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
        dsi_utf8_fix_end(tips[i].title); dsi_utf8_fix_end(tips[i].body);
        return;
    }
    if (!body || ntips >= TMAX) return;
    TipE *e = &tips[ntips++]; memset(e, 0, sizeof *e);
    e->target = target; e->h = hnew(target);
    snprintf(e->title, sizeof e->title, "%s", title ? title : ""); snprintf(e->body, sizeof e->body, "%s", body);
    dsi_utf8_fix_end(e->title); dsi_utf8_fix_end(e->body);
}
/* that target's hover card goes right below its panel first (the Load list's filter boxes, owner 2026-10-25) */
void dsl_ui_tip_below(DSLObj target) { for (int i = 0; i < ntips; i++) if (tips[i].target == target) tips[i].below = 1; }
/* a kit panel as a flat colour (no frame sprite), no click target: bars, lines */
void dsl_ui_flat(DSLObj o, uint32_t rgba) { W *w = w_of(o); Obj r = w ? w_root(w) : (Obj)o; Obj im = r ? comp_get(r, K.Image) : NULL; if (im) img_set(im, NULL, rgba_col(rgba, C_WHITE), 0); }
/* a kit widget as a square of side `size` centred at (cx, cy) of its parent (top-left units), turned by deg degrees
   (45 = a diamond) */
void dsl_ui_turned(DSLObj o, float cx, float cy, float size, float deg) {
    W *w = w_of(o); Obj r = w ? w_root(w) : (Obj)o; Obj t = tr_of(r); if (!t) return;
    rt_set(t, (V2){ 0, 1 }, (V2){ 0, 1 }, (V2){ 0.5f, 0.5f }, (V2){ size, size }, (V2){ cx, -cy });
    if (m_tr_euler) CALL(m_tr_euler, void, Obj, V3)(t, (V3){ 0, 0, deg }, m_tr_euler);
}
/* a kit widget drawn above the game's own views (loading screen, notices): its own canvas, sorting order `order` */
void dsl_ui_raise(DSLObj o, int order) { W *w = w_of(o); Obj r = w ? w_root(w) : (Obj)o; if (r) raise_view(r, order); }
/* test only: hover this target instead of the mouse (NULL = the mouse) */
__attribute__((visibility("default"))) void dsl_ui_test_hover(DSLObj target) { tip_test_target = target; }
/* test only: the shown card's body text as drawn */
__attribute__((visibility("default"))) void dsl_ui_test_tip_body(char *out, size_t cap) {
    Obj etc = hget(h_tip_etc); out[0] = 0;
    if (tip_shown && etc) utf8_of(tmp_get(AT(etc, f_etc_desc, Obj)), out, cap);
}
/* test only: the shown card's screen rect and the panel it was placed against (x0, y0, x1, y1) */
__attribute__((visibility("default"))) int dsl_ui_test_tip_rects(float card[4], float panel[4]) {
    tip_card_rect(card); memcpy(panel, tip_panel, sizeof tip_panel); return tip_shown;
}
/* test only: the shown tooltip targets (on the tree), for a sweep over every hover card of the views open now */
__attribute__((visibility("default"))) int dsl_ui_test_tip_targets(DSLObj *out, int max) {
    int n = 0;
    for (int i = 0; i < ntips && n < max; i++) if (hget(tips[i].h) && on_tree(tips[i].target)) out[n++] = tips[i].target;
    return n;
}
/* test only: the hovered element's rect and the rects the card keeps off (its panel, the windows around it) */
__attribute__((visibility("default"))) int dsl_ui_test_tip_keep(float el[4], float keep[][4], int max) {
    memcpy(el, tip_el, sizeof tip_el); int n = tip_nkeep < max ? tip_nkeep : max;
    for (int i = 0; i < n; i++) memcpy(keep[i], tip_keep[i], sizeof tip_keep[i]);
    return n;
}
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
static struct { int serial; dsl_ui_cb ok, cancel; void *ud; uintptr_t h_ok, h_cancel; char text[1024]; } NB;
/* a notice button label longer than the stock button: the buttons grow to the text + side padding; the stock size
   comes back before the next notice and when this one is answered or closed */
static uintptr_t nfit_lab[2], nfit_txt[2]; static int nfit_tries, nfit_auto[2]; static uintptr_t nfit_grid; static V2 nfit_cell;
#define NFIT_KIDS 12
static uintptr_t nfit_kid[NFIT_KIDS]; static V2 nfit_kidsz[NFIT_KIDS]; static int nfit_nkid;
/* a notice text taller than the stock text rect (520 x 109, the game never sizes it: a long text ran over the frame
   and the buttons): the game's way for long text, the text keeps its size and its box grows. The frame
   (Panel_Notice/Img_BackGroundBox), the text rect and the buttons' top padding (the frame's GridLayoutGroup) grow by
   the extra height; the text keeps its gap to the frame's top, so the buttons stay under it. Taller than the screen
   allows: the text auto-sizes down to the game's 0.75 too. The stock sizes come back with the buttons' (notice_unfit). */
static uintptr_t ngrow_pend, ngrow_box, ngrow_txt, ngrow_pad; static V2 ngrow_boxsz, ngrow_txtsz, ngrow_txtpos;
static int ngrow_top = -1, ngrow_auto; static float ngrow_fs;
static V2 rt_pos(Obj o); static void rt_setpos(Obj o, V2 p); static Obj tr_find(Obj t, const char *path);
static void notice_unfit(void) {
    Obj gb = hget(ngrow_box), gt = hget(ngrow_txt), gp = hget(ngrow_pad);
    if (gb && m_rt_size) CALL(m_rt_size, void, Obj, V2)(tr_of(gb), ngrow_boxsz, m_rt_size);
    if (gt && m_rt_size) { CALL(m_rt_size, void, Obj, V2)(tr_of(gt), ngrow_txtsz, m_rt_size); rt_setpos(gt, ngrow_txtpos); }
    if (gt && ngrow_auto) { CALL(m_tmp_auto, void, Obj, bool)(gt, false, m_tmp_auto); CALL(m_tmp_size, void, Obj, float)(gt, ngrow_fs, m_tmp_size); }
    if (gp && ngrow_top >= 0) CALL(m_ro_settop, void, Obj, int)(gp, ngrow_top, m_ro_settop);
    if (gb) { if (m_lr_force) CALL(m_lr_force, void, Obj)(tr_of(gb), m_lr_force); dsl_log("ui: notice box back to its stock size"); }
    hset(&ngrow_pend, NULL); hset(&ngrow_box, NULL); hset(&ngrow_txt, NULL); hset(&ngrow_pad, NULL); ngrow_top = -1; ngrow_auto = 0;
    for (int i = 0; i < 2; i++) {
        Obj t = hget(nfit_txt[i]); if (t && m_tmp_auto) CALL(m_tmp_auto, void, Obj, bool)(t, nfit_auto[i] != 0, m_tmp_auto);
        hset(&nfit_lab[i], NULL); hset(&nfit_txt[i], NULL);
    }
    Obj g = hget(nfit_grid); if (g && m_grid_cell) CALL(m_grid_cell, void, Obj, V2)(g, nfit_cell, m_grid_cell);
    for (int q = 0; q < nfit_nkid; q++) { Obj ch = hget(nfit_kid[q]); if (ch && m_rt_size) CALL(m_rt_size, void, Obj, V2)(ch, nfit_kidsz[q], m_rt_size); hset(&nfit_kid[q], NULL); }
    nfit_nkid = 0;
    hset(&nfit_grid, NULL); nfit_tries = 0;
}
static int notice_fit(int i, Obj label) {
    Obj b = tr_parent(label); if (!b || !isa(label, K.TMP_Text) || !m_tmp_prefw || !m_rt_rect) return 1;
    R4 r = CALL(m_rt_rect, R4, Obj)(tr_of(b), m_rt_rect);
    float want = CALL(m_tmp_prefw, float, Obj)(label, m_tmp_prefw) + 64;
    if (r.w <= 0) return 0;                                              /* laid out a frame after it shows */
    if (want <= r.w) return 1;
    /* the stock notice lays its buttons out with a GridLayoutGroup (one cell size for both): the cells grow, so both
       buttons keep one width as in the game's other notices; the font keeps its size (no auto-size growth) */
    Obj g = K.GridLayout ? comp_get(tr_parent(b), K.GridLayout) : NULL;
    if (!g || !m_grid_cell || !m_grid_getcell) return 1;
    V2 c = CALL(m_grid_getcell, V2, Obj)(g, m_grid_getcell);
    if (c.x >= want) return 1;
    if (!hget(nfit_grid)) { nfit_cell = c; hset(&nfit_grid, g); }
    CALL(m_grid_cell, void, Obj, V2)(g, (V2){ want, c.y }, m_grid_cell);
    /* a button's frame image is a child with its own fixed width (not stretched): it grows with the cell */
    Obj gp = tr_parent(b);
    for (int k = 0, nk = child_count(gp); k < nk; k++) {
        Obj btn = child_at(gp, k);
        for (int m = 0, nm = child_count(btn); m < nm && m_rt_size; m++) {
            Obj ch = child_at(btn, m); V2 z = rt_size(ch);
            if (ch == tr_of(label) || z.x < c.x - 2 || z.x > c.x + 2) continue;          /* only fixed-width frames */
            int seen = 0; for (int q = 0; q < nfit_nkid; q++) seen |= hget(nfit_kid[q]) == ch;
            if (!seen && nfit_nkid < NFIT_KIDS) { nfit_kidsz[nfit_nkid] = z; hset(&nfit_kid[nfit_nkid++], ch); }
            CALL(m_rt_size, void, Obj, V2)(ch, (V2){ want, z.y }, m_rt_size);
        }
    }
    if (m_tmp_getauto && m_tmp_auto && m_tmp_getsize && m_tmp_size) {
        float fs = CALL(m_tmp_getsize, float, Obj)(label, m_tmp_getsize);
        nfit_auto[i] = CALL(m_tmp_getauto, bool, Obj)(label, m_tmp_getauto); hset(&nfit_txt[i], label);
        CALL(m_tmp_auto, void, Obj, bool)(label, false, m_tmp_auto); CALL(m_tmp_size, void, Obj, float)(label, fs, m_tmp_size);
    }
    dsl_log("ui: notice buttons widened %.0f -> %.0f for \"%s\"", c.x, want, i ? "cancel" : "ok");
    return 1;
}
static int notice_grow(Obj v, Obj nt) {                                  /* 1 = done (grown, fits, or cannot), 0 = not laid out yet */
    Obj box = tr_find(v, "Panel_Notice/Img_BackGroundBox"), g = box && K.GridLayout ? comp_get(box, K.GridLayout) : NULL;
    if (!isa(nt, K.TMP_Text) || !g || !m_lg_getpad || !m_ro_gettop || !m_ro_settop || !m_rt_getpos || !m_rt_rect) return 1;
    R4 r = CALL(m_rt_rect, R4, Obj)(tr_of(nt), m_rt_rect), br = CALL(m_rt_rect, R4, Obj)(tr_of(box), m_rt_rect);
    if (r.w <= 0 || br.h <= 0) return 0;
    float need = fit_pref(nt, r.w).y + 8;                                /* the text at its own size, wrapped at its width */
    if (need <= r.h) return 1;
    float b0[4], t0[4], b1[4], t1[4], c[4] = { 0 };
    if (!rect_screen(box, b0) || !rect_screen(nt, t0) || b0[3] - b0[1] <= 0 || t0[3] - t0[1] <= 0) return 1;
    float sc = (b0[3] - b0[1]) / br.h, d = need - r.h;                   /* screen pixels per box unit; y up */
    Obj cv = K.Canvas ? CALL(m_comp_inparent, Obj, Obj, Obj)(tr_of(box), type_of(K.Canvas), m_comp_inparent) : NULL;
    Obj root = alive(cv) && m_cv_root ? CALL(m_cv_root, Obj, Obj)(cv, m_cv_root) : NULL;
    int capped = 0;
    if (alive(root) && rect_screen(root, c) && c[3] - c[1] > 0) {        /* the frame stays 40 units off the screen's edges */
        float room = (c[3] - c[1]) / sc - 80 - br.h;
        if (d > room) { d = room > 0 ? room : 0; capped = 1; }
    }
    Obj pad = CALL(m_lg_getpad, Obj, Obj)(g, m_lg_getpad); if (!pad) return 1;
    hset(&ngrow_box, box); hset(&ngrow_txt, nt); hset(&ngrow_pad, pad);
    ngrow_boxsz = rt_size(box); ngrow_txtsz = rt_size(nt); ngrow_txtpos = rt_pos(nt);
    ngrow_top = CALL(m_ro_gettop, int, Obj)(pad, m_ro_gettop);
    float gap = b0[3] - t0[3];                                           /* frame top to text top, screen */
    int di = (int)(d + 0.99f);
    CALL(m_rt_size, void, Obj, V2)(tr_of(box), (V2){ ngrow_boxsz.x, ngrow_boxsz.y + di }, m_rt_size);
    CALL(m_ro_settop, void, Obj, int)(pad, ngrow_top + di, m_ro_settop);
    R4 r1 = CALL(m_rt_rect, R4, Obj)(tr_of(nt), m_rt_rect);             /* a text stretched in the frame grew with it */
    if (r1.h < r.h + di) CALL(m_rt_size, void, Obj, V2)(tr_of(nt), (V2){ ngrow_txtsz.x, ngrow_txtsz.y + (r.h + di - r1.h) }, m_rt_size);
    if (rect_screen(box, b1) && rect_screen(nt, t1)) {                   /* the text's top keeps its gap to the frame's top */
        V2 p = rt_pos(nt); p.y += (b1[3] - gap - t1[3]) / sc; rt_setpos(nt, p);
    }
    if (capped && m_tmp_fsmin && m_tmp_fsmax && m_tmp_getsize) {
        ngrow_fs = CALL(m_tmp_getsize, float, Obj)(nt, m_tmp_getsize); ngrow_auto = 1;
        CALL(m_tmp_fsmin, void, Obj, float)(nt, fit_min(ngrow_fs), m_tmp_fsmin); CALL(m_tmp_fsmax, void, Obj, float)(nt, ngrow_fs, m_tmp_fsmax);
        CALL(m_tmp_auto, void, Obj, bool)(nt, true, m_tmp_auto);
    }
    if (m_lr_force) CALL(m_lr_force, void, Obj)(tr_of(box), m_lr_force);
    rect_screen(box, b1); rect_screen(nt, t1);
    dsl_log("ui: notice box grown %.0f -> %.0f high for its text (%.0f)%s; screen frame y %.0f-%.0f, text y %.0f-%.0f",
            br.h, br.h + di, need, capped ? ", screen height reached: the text auto-sizes" : "", b1[1], b1[3], t1[1], t1[3]);
    return 1;
}
static void notice_fit_frame(void) {                                     /* kit tick: the labels set by k_notice */
    if (hget(ngrow_box) && frames % 10 == 0) {                          /* our box closed or replaced (the game's own text): stock sizes */
        Obj v = notice_view(); char cur[1024] = "";
        if (v && on_tree(v)) utf8_of(tmp_get(AT(v, f_nv_text, Obj)), cur, sizeof cur);
        if (!v || !on_tree(v) || strcmp(cur, NB.text)) notice_unfit();
    }
    if (nfit_tries <= 0) return;
    int left = 0;
    for (int i = 0; i < 2; i++) { Obj l = hget(nfit_lab[i]); if (l) { if (notice_fit(i, l)) hset(&nfit_lab[i], NULL); else left = 1; } }
    { Obj t = hget(ngrow_pend), v = t ? notice_view() : NULL; if (t) { if (!v || notice_grow(v, t)) hset(&ngrow_pend, NULL); else left = 1; } }
    nfit_tries = left ? nfit_tries - 1 : 0;
}
/* a long path breaks at its separators: TMP breaks a word with no space only where the line ends, mid-name. A zero-width
   space after each / or \\ of a word longer than 30 bytes (not in a rich-text tag); no room for them: the text as it is */
static void path_breaks(const char *s, char *out, size_t cap) {
    size_t o = 0; const char *ws = s;
    for (const char *p = s; *p; p++) {
        if (*p == ' ' || *p == '\n') ws = p + 1;
        if (o + 4 >= cap) { snprintf(out, cap, "%s", s); return; }
        out[o++] = *p;
        if ((*p == '/' || *p == '\\') && p > ws && p[-1] != '<' && p[1] && p[1] != ' ' && p[1] != '\n' && (size_t)(p - ws) + strcspn(p, " \n") > 30) {
            memcpy(out + o, "\xE2\x80\x8B", 3); o += 3;
        }
    }
    out[o] = 0;
}
static void esc_notice_push(Obj v, int two);
static int nb_esc_off;
static void k_notice(const char *text, const char *ok_label, const char *cancel_label, dsl_ui_cb ok, dsl_ui_cb cancel, void *ud) {
    notice_unfit(); nview_last = -1000; Obj v = notice_view(); nb_esc_off = 0;
    if (!v || !m_nv_select || !m_nv_confirm || f_nv_text < 0) { dsl_log("ui: notice: no CommonNoticeView in this scene: \"%s\" not shown", text ? text : ""); return; }
    NB.serial = (NB.serial + 1) & 0x0FFFFFFF; NB.ok = ok; NB.cancel = cancel; NB.ud = ud;
    path_breaks(text ? text : "", NB.text, sizeof NB.text); dsi_utf8_fix_end(NB.text);   /* the box shows (and k_notice_close compares) this copy */
    Obj a = mk_action(TOKEN(NB.serial, P_OK)), c = cancel_label ? mk_action(TOKEN(NB.serial, P_CANCEL)) : NULL;
    hset(&NB.h_ok, a); hset(&NB.h_cancel, c);
    if (cancel_label) CALL(m_nv_select, void, Obj, Obj, Obj, Obj, bool)(v, str(NB.text), a, c, false, m_nv_select);
    else CALL(m_nv_confirm, void, Obj, Obj, Obj)(v, str(NB.text), a, m_nv_confirm);
    Obj nt = AT(v, f_nv_text, Obj);
    tmp_set(nt, NB.text);
    hset(&ngrow_pend, nt);                                           /* a long text: the box grows (notice_grow) */
    if (isa(nt, K.TMP_Text)) CALL(m_tmp_align, void, Obj, int)(nt, 514, m_tmp_align);
    if (f_nv_tok >= 0) { tmp_set(AT(v, f_nv_tok, Obj), ok_label ? ok_label : TR("OK")); hset(&nfit_lab[0], AT(v, f_nv_tok, Obj)); }
    if (cancel_label && f_nv_tcancel >= 0) { tmp_set(AT(v, f_nv_tcancel, Obj), cancel_label); hset(&nfit_lab[1], AT(v, f_nv_tcancel, Obj)); }
    nfit_tries = 60;
    go_active(v, 1);
    raise_view(v, 32040);
    esc_notice_push(v, cancel_label != NULL);                       /* Escape = its Cancel (OK with one button) */
    dsl_log("ui: notice %d shown (%s): %s", NB.serial, cancel_label ? "two buttons" : "one button", text ? text : "");
}

static void k_notice_close(void) {
    notice_unfit();
    Obj v = notice_view(); if (!v || !NB.text[0] || !on_tree(v)) return;
    char cur[1024] = ""; utf8_of(tmp_get(AT(v, f_nv_text, Obj)), cur, sizeof cur);
    if (strcmp(cur, NB.text)) return;                                   /* another box is up now (the game's own): leave it */
    NB.text[0] = 0; NB.ok = NB.cancel = NULL; NB.ud = NULL; NB.serial = (NB.serial + 1) & 0x0FFFFFFF;   /* a late click on it does nothing */
    go_active(v, 0);
    dsl_log("ui: notice closed by its owner");
}

/* the game's notice box is on screen (any owner): dsl_crash.c asks only when it is free */
int dsl_ui_notice_shown(void) { Obj v = notice_view(); return v && on_tree(v); }

/* ------------------------------------------------------------------ Escape (kit 0.1.28; owner 2026-10-25: "make sure Escape
   works to close our custom windows"). ONE owner of the key for every mod: windows push themselves (esc_push); a press closes
   the top-most one that still shows (the last pushed), exactly as its own Close / Cancel / X (its close callback), one per
   press. The kit's own notice box is pushed here too: Escape = its Cancel (its OK when it has one button), through the stock
   OnClick_Cancle / OnClick_Confirm, so the box closes the game's way and its callback runs as for a click.
   The game must not act on that same press. It reads its keys in KeyInputDispatcher.UpdateInputStream: List.AddRange over
   the iterator <GetInputOnKeyDown>d__N, whose MoveNext (an interface call, never inlined) asks Input.GetKeyDown per bound key.
   The hook there skips KeyCode.Escape while one of these windows shows or the kit used the press this frame: no
   EscapeActionRequested, so no pause menu, no stock popup under ours closed, no construction cancel. The kit reads the key
   itself in its frame tick (Input.GetKeyDown, not through that iterator), so the order of the game's input and the
   EventSystem tick in a frame does not matter.
   A text field that had the keyboard (any TMP_InputField; it takes Escape in EventSystem.Update, before this tick, and has
   let go by now): that press only leaves the field. DSL_ESC_EDIT on the innermost pushed window holding the field = that
   window closes too (an edit row: its Cancel drops the edit). The next press closes the window. With none of these windows
   shown, the game gets Escape as before (multiplayer's own boxes keep their InputEventHandler Escape hook). */
#define ESC_MAX 32
typedef struct { uintptr_t h; DSLObj id; void (*close)(void *); void *ud; int flags, seq, notice; } EscE;   /* id: identity only; notice: the kit notice's serial */
static EscE escs[ESC_MAX]; static int nesc, esc_seq;
static int32_t esc_used = -1;                                        /* the frame whose Escape the kit used */
static uintptr_t esc_foc;                                            /* the TMP_InputField that had the keyboard at the last tick */
static void *m_es_cur, *m_es_selgo, *m_es_setsel, *m_nv_okclick, *m_nv_cancelclick;
static uintptr_t (*x_weak)(Obj, bool);
static int32_t esc_frame(void) { return m_time_frame ? CALL0(m_time_frame, int)(m_time_frame) : -1; }
static void esc_free(int i) { hfree(&escs[i].h); escs[i] = escs[--nesc]; }
static int esc_notice_up(const EscE *e) {                            /* the kit notice of this entry still shows (not answered, not replaced) */
    Obj v = e->notice == NB.serial && NB.text[0] ? hget(e->h) : NULL; if (!v || !on_tree(v)) return 0;
    char cur[1024] = ""; utf8_of(tmp_get(AT(v, f_nv_text, Obj)), cur, sizeof cur);
    return !strcmp(cur, NB.text);
}
static void esc_sweep(void) { for (int i = nesc - 1; i >= 0; i--) if (escs[i].notice ? !esc_notice_up(&escs[i]) : !hget(escs[i].h)) esc_free(i); }
static int esc_shown(const EscE *e) { return e->notice || (!(e->flags & DSL_ESC_HUD) && on_tree(hget(e->h))); }   /* a HUD panel never takes Escape */
static EscE *esc_top(void) {
    esc_sweep();
    EscE *t = NULL;
    for (int i = 0; i < nesc; i++) if (esc_shown(&escs[i]) && (!t || escs[i].seq > t->seq)) t = &escs[i];
    return t;
}
static int k_esc_push(DSLObj win, void (*close)(void *), void *ud, int flags) {
    W *w = w_of(win); Obj o = w ? w_root(w) : (Obj)win;
    if ((!close && !(flags & DSL_ESC_HUD)) || !alive(o)) return -1;
    int i = 0; while (i < nesc && (escs[i].notice || escs[i].id != win)) i++;
    if (i == nesc) {
        if (nesc == ESC_MAX) esc_sweep();
        if (nesc == ESC_MAX) { dsl_log("ui: Escape: more than %d windows pushed: refused", ESC_MAX); return -1; }
        i = nesc++; memset(&escs[i], 0, sizeof escs[i]); escs[i].id = win;
    }
    hfree(&escs[i].h); escs[i].h = x_weak ? x_weak(o, false) : hnew(o);   /* always this object (a new one can sit where a dead one was) */
    escs[i].close = close; escs[i].ud = ud; escs[i].flags = flags; escs[i].seq = ++esc_seq;
    return 0;
}
static void k_esc_drop(DSLObj win) { for (int i = nesc - 1; win && i >= 0; i--) if (!escs[i].notice && escs[i].id == win) esc_free(i); }
static void esc_notice_close(void *ud) {                             /* the box's own button: Cancel (two buttons) or OK */
    Obj v = notice_view(); void *m = ud ? m_nv_cancelclick : m_nv_okclick;
    if (v && on_tree(v) && m) CALL(m, void, Obj)(v, m);
}
static void esc_notice_none(void *ud) { (void)ud; dsl_log("ui: Escape: this notice waits for an answer (Escape does not answer it)"); }
static void esc_notice_push(Obj v, int two) {                        /* k_notice: the box just shown */
    for (int i = nesc - 1; i >= 0; i--) if (escs[i].notice) esc_free(i);
    if (nesc == ESC_MAX || !v) return;
    EscE *e = &escs[nesc++]; memset(e, 0, sizeof *e);
    e->h = hnew(v); e->notice = NB.serial; e->seq = ++esc_seq;
    e->close = nb_esc_off ? esc_notice_none : esc_notice_close; e->ud = two ? (void *)1 : NULL;
}
/* dsl_crash.c: the consent box (Yes / No, a lasting answer) is not answered by a stray Escape; the game still never sees it */
void dsl_ui_notice_no_esc(void) { nb_esc_off = 1; for (int i = 0; i < nesc; i++) if (escs[i].notice) escs[i].close = esc_notice_none; }
static Obj esc_focused(void) {                                       /* the TMP_InputField with the keyboard now, or NULL */
    if (!m_es_cur || !m_es_selgo || !m_in_focused) return NULL;
    Obj es = CALL0(m_es_cur, Obj)(m_es_cur); if (!alive(es)) return NULL;
    Obj go = CALL(m_es_selgo, Obj, Obj)(es, m_es_selgo);
    Obj f = alive(go) ? comp_get(go, K.TMP_Input) : NULL;
    return f && CALL(m_in_focused, bool, Obj)(f, m_in_focused) ? f : NULL;
}
/* the field lets go of the selection too: a field that only lost the keyboard took it back when a notice box showed over
   its window (the next Escape then only "left the field" again instead of answering the box; dsesc-cm, Unit Builder) */
static void esc_deselect(Obj field) {
    Obj es = m_es_cur ? CALL0(m_es_cur, Obj)(m_es_cur) : NULL;
    Obj sel = alive(es) && m_es_selgo ? CALL(m_es_selgo, Obj, Obj)(es, m_es_selgo) : NULL;
    if (sel && sel == go_of(field) && m_es_setsel) CALL(m_es_setsel, void, Obj, Obj)(es, NULL, m_es_setsel);
}
static int esc_holder(Obj field) {                                   /* the innermost shown window holding the field, -1 = none */
    for (Obj t = tr_of(field); t; t = tr_parent(t))
        for (int i = 0; i < nesc; i++) if (!escs[i].notice) { Obj o = hget(escs[i].h); if (o && tr_of(o) == t && on_tree(o)) return i; }
    return -1;
}
/* The rule's guard (owner 2026-10-25: "make sure ... the escape button works like this in all future windows"). Test copies
   only (background-only or DSRIG_CTL=1, dsl_soft_test_copy): every root panel of the kit (parent NULL: a window, its dim
   backdrop, a HUD panel) is checked once it has shown for 30 frames: it must be pushed (itself or a part of it), or be
   made in the same frame as a pushed one (the backdrop of a window), or be pushed as DSL_ESC_HUD. Else one line
   "ESC-MISSING <its first text>"; the esc tests fail on it. No default registration: the kit cannot know a window's own
   Close (the Unit Builder's Cancel asks first, the home page goes Back, Bring Units runs its callback, a HUD has none). */
#define GUARD_MAX 64
static struct { uintptr_t h; int64_t made; int done; } groots[GUARD_MAX]; static int ngroots;
static void esc_guard_root(DSLObj root) {
    if (guard_on < 0) guard_on = dsl_soft_test_copy() != NULL;
    if (!guard_on || !root) return;
    for (int i = ngroots - 1; i >= 0; i--) if (!hget(groots[i].h)) { hfree(&groots[i].h); groots[i] = groots[--ngroots]; }
    if (ngroots == GUARD_MAX) return;
    groots[ngroots].h = x_weak ? x_weak((Obj)root, false) : hnew((Obj)root); groots[ngroots].made = frames; groots[ngroots].done = 0; ngroots++;
}
static int guard_pushed(Obj root) {                                   /* an entry (HUD too) is this root or under it */
    for (int i = 0; i < nesc; i++) {
        if (escs[i].notice) continue;
        for (Obj t = tr_of(hget(escs[i].h)); t; t = tr_parent(t)) if (t == tr_of(root)) return 1;
    }
    return 0;
}
static void guard_text(Obj t, char *out, size_t cap, int depth) {    /* the first non-empty text under t (its title) */
    if (out[0] || !t || depth > 4) return;
    Obj tx = comp_get(t, K.TMP_Text); if (tx) { utf8_of(tmp_get(tx), out, cap); if (out[0]) return; }
    for (int i = 0, n = child_count(t); i < n && !out[0]; i++) guard_text(child_at(t, i), out, cap, depth + 1);
}
static void ui_guard_check(Obj root);                                /* the UI rules' guard (the templates, below) */
static void esc_guard_frame(void) {
    if (guard_on != 1 || frames % 30) return;
    for (int i = 0; i < ngroots; i++) {
        Obj o = hget(groots[i].h);
        if (!o || groots[i].done || frames - groots[i].made < 30 || !on_tree(o)) continue;
        int ok = guard_pushed(o);
        for (int k = 0; k < ngroots && !ok; k++) {                    /* its dim backdrop: made in the same frame as a pushed one */
            Obj q = k != i && groots[k].made == groots[i].made ? hget(groots[k].h) : NULL;
            ok = q && guard_pushed(q);
        }
        groots[i].done = 1;
        ui_guard_check(o);
        if (ok) continue;
        char t[200] = ""; guard_text(o, t, sizeof t, 0);
        for (char *c = t; *c; c++) if (*c == '\n') *c = ' ';
        dsl_log("ESC-MISSING \"%s\": a window of ours shows with no Escape registration (dsl_esc_push its Close; a HUD panel: DSL_ESC_HUD)", t[0] ? t : "(no text)");
    }
}
static void esc_tick(void) {
    Obj had = hget(esc_foc), now = esc_focused();
    int32_t fc = esc_frame();
    if (nesc && m_input_keydown && esc_used != fc && CALL(m_input_keydown, bool, int)(27, m_input_keydown)) {
        EscE *t = esc_top();
        if (t) {
            esc_used = fc;
            Obj field = now ? now : had; int k = field ? esc_holder(field) : -1;
            char nm[96] = "";
            if (field) esc_deselect(field);
            if (field && (k < 0 || !(escs[k].flags & DSL_ESC_EDIT))) dsl_log("ui: Escape left a text field (the window stays)");
            else {
                EscE e = field ? escs[k] : *t;                       /* a copy: close() may drop the entry */
                obj_name(go_of(hget(e.h)), nm, sizeof nm);
                dsl_log("ui: Escape closes %s%s", e.notice ? "the notice box" : nm[0] ? nm : "a window", field ? " (an edit with the keyboard: its Cancel)" : "");
                e.close(e.ud);
            }
        }
    }
    hset(&esc_foc, esc_focused());                                   /* after: a field this press left is free now */
}
/* the game's key-down reader: KeyCode.Escape skipped while ours takes it (one log line per press) */
static bool (*o_kd_next)(Obj, const void *);
static int32_t off_kd_cur = -1, esc_kept = -1;
static bool h_kd_next(Obj it, const void *mi) {
    for (;;) {
        bool r = o_kd_next(it, mi);
        if (!r || AT(it, off_kd_cur, int32_t) != 27) return r;
        int32_t fc = esc_frame();
        if (!(esc_used == fc && fc >= 0) && !esc_top()) return r;
        if (esc_kept != fc) { esc_kept = fc; dsl_log("ui: Escape kept from the game (one of our windows takes it)"); }
    }
}
static void esc_install(void) {
    void *(*nested)(void *, void **) = (void *(*)(void *, void **))dsl_il2cpp_fn("il2cpp_class_get_nested_types");
    const char *(*cname)(void *) = (const char *(*)(void *))dsl_il2cpp_fn("il2cpp_class_get_name");
    *(void **)&x_weak = dsl_il2cpp_fn("il2cpp_gchandle_new_weakref");
    void *k = dsl_find_class("Refactor.Main.InputModule", "KeyInputDispatcher"), *it = NULL, *nk, *mi = NULL;
    while (k && nested && cname && (nk = nested(k, &it)))
        if (!strncmp(cname(nk), "<GetInputOnKeyDown>d__", 22)) { mi = mi_sig(nk, "MoveNext", 0, NULL); off_kd_cur = dsl_field_offset(nk, "<>2__current"); break; }
    void *kES = dsl_find_class("UnityEngine.EventSystems", "EventSystem");
    m_es_cur = kES ? mi_sig(kES, "get_current", 0, NULL) : NULL; m_es_selgo = kES ? mi_sig(kES, "get_currentSelectedGameObject", 0, NULL) : NULL;
    m_es_setsel = kES ? mi_sig(kES, "SetSelectedGameObject", 1, NULL) : NULL;
    m_nv_okclick = K.Notice ? mi_sig(K.Notice, "OnClick_Confirm", 0, NULL) : NULL; m_nv_cancelclick = K.Notice ? mi_sig(K.Notice, "OnClick_Cancle", 0, NULL) : NULL;
    if (!mi || !*(void **)mi || off_kd_cur < 0)
        dsl_log("ui: Escape: the game's key reader not found (MoveNext %p, current %d): an Escape that closes our window also reaches the game", mi, off_kd_cur);
    else if (dsl_hook(DSL_OWNER_LOADER, *(void **)mi, (void *)h_kd_next, (void **)&o_kd_next, "loader ui Escape (KeyInputDispatcher key-down reader)"))
        dsl_log("ui: Escape: key reader hook refused: an Escape that closes our window also reaches the game");
    dsl_log("ui: Escape for our windows ready (fields %s, notice buttons %s)", m_es_cur && m_es_selgo ? "on" : "OFF", m_nv_okclick && m_nv_cancelclick ? "on" : "OFF");
}
/* a module that reads Escape itself (multiplayer's title pages, its in-game boxes) leaves the press alone while the kit
   takes it: one of the pushed windows shows, or the kit used this frame's press. Found by name (Windows: baselib.dll). */
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int dsl_esc_taken(void) { int32_t fc = esc_frame(); return (esc_used == fc && fc >= 0) || esc_top() != NULL; }
__attribute__((visibility("default"))) int dsl_ui_test_typing(void) { return esc_focused() != NULL; }   /* test: a text field has the keyboard */
/* test only: the pushed windows, top last ("name|shown|flags;...") */
__attribute__((visibility("default"))) int dsl_ui_test_esc(char *out, size_t cap) {
    esc_sweep(); size_t o = 0; if (cap) out[0] = 0;
    for (int i = 0; i < nesc && o + 120 < cap; i++) {
        char nm[96] = ""; obj_name(go_of(hget(escs[i].h)), nm, sizeof nm);
        o += (size_t)snprintf(out + o, cap - o, "%s%s|%d|%d|%d", i ? ";" : "", escs[i].notice ? "notice" : nm, esc_shown(&escs[i]), escs[i].flags, escs[i].seq);
    }
    return nesc;
}

/* ------------------------------------------------------------------ the game's look and settings for our windows (kit 0.1.28)
   close_x: the game's red X (owner 2026-10-25: one close control, the game's). scale_follow: a window follows Settings >
   UI Scale (opt-in: Units Laboratory and Bring Units scale themselves). movable: a right-click drag moves a panel, which
   stays on the screen and keeps its place (owner 2026-10-25, MOVE RULE). */
static void tr_scale(Obj o, float s);
static int32_t f_slv_close = -1, f_das_targets = -1, f_das_ttype = -1, f_sel_trans = -1, f_sel_colors = -1;
static int32_t colors_n;                                             /* sizeof(ColorBlock) */
static void *m_usm_inst, *m_usm_scale, *m_input_mb, *m_input_mbd;
static void *K_PopUp;                                                /* Refactor.UI.PopUpUI_Base: the game's windows */
static uintptr_t h_tpl_x;
/* the Load Campaign window's own close button (popup prefab 5, SaveLoadView._closeButton), cloned once into the template
   holder with its Button turned off: a clone only draws; the click goes to the kit button it sits in */
static Obj x_tpl(void) {
    Obj x = hget(h_tpl_x); if (x) return x;
    static int64_t last = -1000; static int said; if (frames - last < 60) return NULL;
    last = frames;
    Obj p = ui_prefab(5), b = p && f_slv_close >= 0 && isa(p, K.SaveLoadView) ? AT(p, f_slv_close, Obj) : NULL, hd = holder();
    if (!alive(b) || !hd) { if (!said++) dsl_log("ui: red X: the Load window's close button not found (prefab %p, field %d): text X stays", p, f_slv_close); return NULL; }
    Obj c = inst(go_of(b), hd); if (!c) return NULL;
    Obj sel = comp_get(c, K.Selectable);
    if (sel && m_beh_enabled) CALL(m_beh_enabled, void, Obj, bool)(sel, false, m_beh_enabled);
    hset(&h_tpl_x, c);
    char nm[64] = "", kids[300] = ""; Obj im = first_image(tr_of(c), 0), sp = img_sprite(im); obj_name(sp, nm, sizeof nm);
    for (int i = 0, n = child_count(c); i < n && strlen(kids) < 250; i++) { char k[64]; obj_name(go_of(child_at(c, i)), k, sizeof k); strlcat(kids, " ", sizeof kids); strlcat(kids, k, sizeof kids); }
    V2 s = rt_size(b);
    dsl_log("ui: red X: the Load window's close button (%.0f x %.0f, sprite \"%s\", children:%s) kept as the template", s.x, s.y, nm, kids[0] ? kids : " none");
    return c;
}
/* the window template (window_std, below): a red X of such a window runs its Close (Escape runs its Back first) */
typedef struct { uintptr_t h, h_back; DSLObj id, back_id; void (*close)(void *); int (*back)(void *); void *ud; int kind; } StdE;
static StdE *std_of(DSLObj win);
static void std_close(DSLObj win);
static EscE *esc_of(DSLObj win) {                                    /* the Escape registration of win (not a notice) */
    EscE *e = NULL;
    for (int i = 0; win && i < nesc; i++) if (!escs[i].notice && escs[i].id == win && (!e || escs[i].seq > e->seq)) e = &escs[i];
    return e;
}
static void x_click(void *ud, DSLObj w) {
    (void)w; if (std_of((DSLObj)ud)) { dsl_log("ui: red X closes a window of the template"); std_close((DSLObj)ud); return; }
    EscE *e = esc_of((DSLObj)ud);
    if (!e || !e->close || (e->flags & DSL_ESC_HUD)) { dsl_log("ui: red X: its window has no Escape registration: nothing closes"); return; }
    EscE c = *e; char nm[96] = ""; obj_name(go_of(hget(c.h)), nm, sizeof nm);   /* a copy: close() drops the entry */
    dsl_log("ui: red X closes %s (its Escape close)", nm[0] ? nm : "a window");
    c.close(c.ud);
}
static DSLObj k_close_x(DSLObj parent, float x, float y, float size, DSLObj win) {
    if (!ready_basic()) return NULL;
    Obj tpl = x_tpl(), p = parent_or_layer(parent); if (!tpl || !p) return NULL;
    W *wd = w_new(W_BUTTON); if (!wd) return NULL;
    Obj b = mk_btn(p, x, y, size, size, "", 14, TOKEN(wd->id, P_CLICK), &wd->h_act[0]);
    Obj c = b ? inst(tpl, b) : NULL;
    if (!c) { if (b) destroy_obj(go_of(b)); w_free(nws - 1); return NULL; }
    Obj l = btn_label(b); if (l) go_active(l, 0);
    Obj bg = first_image(tr_of(b), 0);                               /* the menu frame: off (the copy draws, its images take the click) */
    if (bg && m_beh_enabled) CALL(m_beh_enabled, void, Obj, bool)(bg, false, m_beh_enabled);
    go_active(c, 1); stretch(c);
    Obj im = first_image(tr_of(c), 0); if (im) gr_raycast(im, 1);
    /* the game's hover / press look: the kit button's transition drives the copy's images with the close button's colours */
    Obj src = comp_get(tpl, K.Selectable);
    if (src && f_das_targets >= 0 && f_sel_colors >= 0 && f_sel_trans >= 0 && f_das_ttype >= 0 && colors_n > 0 && colors_n <= 128) {
        Obj cs = comp_get(c, K.Selectable), arr = cs ? AT(cs, f_das_targets, Obj) : NULL;
        if (arr) {
            AT(b, f_das_targets, Obj) = arr;                         /* also held by the copy's own button: no GC gap */
            memcpy((char *)b + f_sel_colors, (char *)src + f_sel_colors, (size_t)colors_n);
            AT(b, f_sel_trans, int32_t) = AT(src, f_sel_trans, int32_t); AT(b, f_das_ttype, int32_t) = AT(src, f_das_ttype, int32_t);
        }
    }
    wd->cb = x_click; wd->ud = win; wd->w = size; wd->h = size;
    hset(&wd->h_btn, b);
    return w_done(wd, tr_of(b), NULL);
}

static float k_ui_scale(void) {
    if (!m_usm_inst || !m_usm_scale) return 1.f;
    Obj m = CALL0(m_usm_inst, Obj)(m_usm_inst); if (!alive(m)) return 1.f;
    float s = CALL(m_usm_scale, float, Obj)(m, m_usm_scale);
    return s >= 0.5f && s <= 2.f ? s : 1.f;
}
#define SF_MAX 32
static struct { uintptr_t h; DSLObj id; } sfs[SF_MAX]; static int nsf; static float sf_u = -1;
static void sf_apply(Obj o, float u) {                               /* UI Scale, less when the window would not fit in its parent */
    Obj p = tr_parent(o); float s = u;
    if (p && m_rt_rect && isa(tr_of(p), K.RectTransform) && isa(tr_of(o), K.RectTransform)) {
        R4 r = CALL(m_rt_rect, R4, Obj)(tr_of(p), m_rt_rect), me = CALL(m_rt_rect, R4, Obj)(tr_of(o), m_rt_rect);
        if (r.w >= 100 && r.h >= 100 && me.w > 1 && me.h > 1) { float f = r.w / me.w, g = r.h / me.h; if (g < f) f = g; if (f < s) s = f; }
    }
    tr_scale(o, s);
}
static int k_scale_follow(DSLObj win) {
    W *w = w_of(win); Obj o = w ? w_root(w) : (Obj)win;
    if (!alive(o) || !isa(tr_of(o), K.RectTransform)) return -1;
    int i = 0; while (i < nsf && sfs[i].id != win) i++;
    if (i == nsf) {
        for (int k = nsf - 1; k >= 0 && nsf == SF_MAX; k--) if (!hget(sfs[k].h)) { hfree(&sfs[k].h); sfs[k] = sfs[--nsf]; }
        if (nsf == SF_MAX) { dsl_log("ui: UI Scale: more than %d windows follow it: refused", SF_MAX); return -1; }
        i = nsf++; memset(&sfs[i], 0, sizeof sfs[i]); sfs[i].id = win;
    }
    hfree(&sfs[i].h); sfs[i].h = x_weak ? x_weak(o, false) : hnew(o);
    sf_apply(o, k_ui_scale());
    return 0;
}
static void sf_frame(void) {
    if (!nsf) return;
    float u = k_ui_scale(); int changed = u != sf_u;
    if (!changed && frames % 30) return;                             /* else: the game window may have been resized */
    if (changed && sf_u > 0) dsl_log("ui: UI Scale %.2f -> %.2f: %d window(s) follow it", sf_u, u, nsf);
    sf_u = u;
    for (int i = nsf - 1; i >= 0; i--) { Obj o = hget(sfs[i].h); if (!o) { hfree(&sfs[i].h); sfs[i] = sfs[--nsf]; continue; } sf_apply(o, u); }
}

#define MV_MAX 32
#define MVP_MAX 128
static struct { uintptr_t h; DSLObj id; char key[64]; int seq; } mvs[MV_MAX]; static int nmv, mv_seq;
static struct { char key[64]; float x, y; } mvp[MVP_MAX]; static int nmvp, mvp_loaded;
static DSLObj mv_id; static int mv_on; static float mv_x0, mv_y0, mv_lx, mv_ly;
static const char *mvp_file(void) { static char p[1200]; if (!p[0]) snprintf(p, sizeof p, "%s/ui_places.tsv", dsl_reg_data_dir()); return p; }
static void mvp_load(void) {
    if (mvp_loaded) return; mvp_loaded = 1;
    FILE *f = fopen(mvp_file(), "r"); if (!f) return;
    char line[256];
    while (nmvp < MVP_MAX && fgets(line, sizeof line, f)) {
        char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL; if (!t2 || t1 - line >= 64 || t1 == line) continue;
        *t1 = 0; snprintf(mvp[nmvp].key, sizeof mvp[nmvp].key, "%s", line); mvp[nmvp].x = strtof(t1 + 1, NULL); mvp[nmvp].y = strtof(t2 + 1, NULL); nmvp++;
    }
    fclose(f);
}
static void mvp_save(void) {
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", mvp_file());
    FILE *f = fopen(tmp, "w"); if (!f) { dsl_log("ui: move: %s cannot be written", mvp_file()); return; }
    for (int i = 0; i < nmvp; i++) fprintf(f, "%s\t%.1f\t%.1f\n", mvp[i].key, mvp[i].x, mvp[i].y);
    if (fclose(f) == 0) dsl_os_replace(tmp, mvp_file());
}
static float scr_h(void) { return m_scr_h ? (float)CALL0(m_scr_h, int)(m_scr_h) : 1080.f; }
static void mv_clamp(Obj o) {                                        /* all of it on the screen (its top-left when it is bigger) */
    float r[4], W = screen_w(), H = scr_h(), dx = 0, dy = 0;
    if (!rect_screen(o, r) || r[2] - r[0] < 1) return;
    if (r[0] < 0 || r[2] - r[0] >= W) dx = -r[0]; else if (r[2] > W) dx = W - r[2];
    if (r[3] > H || r[3] - r[1] >= H) dy = H - r[3]; else if (r[1] < 0) dy = -r[1];
    if (dx != 0 || dy != 0) rt_move_screen(o, dx, dy);
}
static int k_movable(DSLObj panel, const char *key) {
    W *w = w_of(panel); Obj o = w ? w_root(w) : (Obj)panel;
    if (!alive(o) || !isa(tr_of(o), K.RectTransform) || !key || !*key || strlen(key) >= 64 || strchr(key, '\t') || strchr(key, '\n')) return -1;
    int i = 0; while (i < nmv && mvs[i].id != panel) i++;
    if (i == nmv) {
        for (int k = nmv - 1; k >= 0 && nmv == MV_MAX; k--) if (!hget(mvs[k].h)) { hfree(&mvs[k].h); mvs[k] = mvs[--nmv]; }
        if (nmv == MV_MAX) { dsl_log("ui: move: more than %d movable panels: refused", MV_MAX); return -1; }
        i = nmv++; memset(&mvs[i], 0, sizeof mvs[i]); mvs[i].id = panel;
    }
    hfree(&mvs[i].h); mvs[i].h = x_weak ? x_weak(o, false) : hnew(o);
    snprintf(mvs[i].key, sizeof mvs[i].key, "%s", key); mvs[i].seq = ++mv_seq;
    mvp_load();
    for (int k = 0; k < nmvp; k++) if (!strcmp(mvp[k].key, key) && m_rt_pos) {
        CALL(m_rt_pos, void, Obj, V2)(tr_of(o), (V2){ mvp[k].x, mvp[k].y }, m_rt_pos);
        dsl_log("ui: move: %s put back at %.0f, %.0f", key, mvp[k].x, mvp[k].y);
        break;
    }
    mv_clamp(o);
    return 0;
}
static int mv_find(DSLObj id) { for (int i = 0; id && i < nmv; i++) if (mvs[i].id == id) return i; return -1; }
static void mv_keep(int i) {                                         /* the place, under its key */
    Obj o = hget(mvs[i].h); if (!o || !m_rt_getpos) return;
    V2 p = rt_pos(o); int k = 0; while (k < nmvp && strcmp(mvp[k].key, mvs[i].key)) k++;
    if (k == nmvp) { if (nmvp == MVP_MAX) { dsl_log("ui: move: %d places kept: %s not kept", MVP_MAX, mvs[i].key); return; } nmvp++; }
    snprintf(mvp[k].key, sizeof mvp[k].key, "%s", mvs[i].key); mvp[k].x = p.x; mvp[k].y = p.y;
    mvp_save();
    dsl_log("ui: move: %s kept at %.0f, %.0f", mvs[i].key, p.x, p.y);
}
static void mv_frame(void) {
    if (!nmv || !m_input_mb || !m_input_mbd || !m_input_mouse) return;
    V3 m = CALL0(m_input_mouse, V3)(m_input_mouse);
    if (!mv_id && CALL(m_input_mbd, bool, int)(1, m_input_mbd)) {   /* a right press on a movable panel (the top-most: the newest) */
        int best = -1;
        for (int i = nmv - 1; i >= 0; i--) if (!hget(mvs[i].h)) { hfree(&mvs[i].h); mvs[i] = mvs[--nmv]; }
        for (int i = 0; i < nmv; i++) {
            Obj o = hget(mvs[i].h); float r[4];
            if (!o || !on_tree(o) || !rect_screen(o, r) || m.x < r[0] || m.x > r[2] || m.y < r[1] || m.y > r[3]) continue;
            if (best < 0 || mvs[i].seq > mvs[best].seq) best = i;
        }
        if (best >= 0) { mv_id = mvs[best].id; mv_on = 0; mv_x0 = mv_lx = m.x; mv_y0 = mv_ly = m.y; }
    }
    int i = mv_find(mv_id); Obj o = i >= 0 ? hget(mvs[i].h) : NULL;
    if (mv_id && (!o || !CALL(m_input_mb, bool, int)(1, m_input_mb))) {   /* let go (or the panel is gone) */
        if (o && mv_on) mv_keep(i);
        mv_id = NULL; mv_on = 0;
    } else if (o) {
        float dx = m.x - mv_x0, dy = m.y - mv_y0;
        if (!mv_on && dx * dx + dy * dy >= 36) { mv_on = 1; dsl_log("ui: move: %s follows the right-drag", mvs[i].key); }   /* 6 px: a right-click stays a click */
        if (mv_on && (m.x != mv_lx || m.y != mv_ly)) { rt_move_screen(o, m.x - mv_lx, m.y - mv_ly); mv_clamp(o); mv_lx = m.x; mv_ly = m.y; }
    }
    if (frames % 30 == 0)                                            /* the game window was resized: back on the screen */
        for (int k = 0; k < nmv; k++) { Obj q = hget(mvs[k].h); if (q && on_tree(q) && mvs[k].id != mv_id) mv_clamp(q); }
}
/* ------------------------------------------------------------------ the templates (kit 0.1.28; owner 2026-10-25: "templated
   rules / pattern to keep applying everywhere"). window_std: a window or an in-game panel with every owner rule on by
   default. card_toggle: the CARD RULE. rules_own: a window that applies a rule by its own code. The UI-RULE guard (test
   copies, below) logs every window of ours that breaks one. */
#define STD_MAX 32
static StdE stds[STD_MAX]; static int nstd;
static StdE *std_of(DSLObj win) { for (int i = 0; win && i < nstd; i++) if (stds[i].id == win && hget(stds[i].h)) return &stds[i]; return NULL; }
static void std_sweep(void) { for (int i = nstd - 1; i >= 0; i--) if (!hget(stds[i].h)) { hfree(&stds[i].h); hfree(&stds[i].h_back); stds[i] = stds[--nstd]; } }
static void k_destroy(DSLObj o);
static void k_window_close(DSLObj win) {
    StdE *s = std_of(win); DSLObj back = s && hget(s->h_back) ? s->back_id : NULL;
    if (s) { hfree(&s->h); hfree(&s->h_back); *s = stds[--nstd]; }
    if (back) k_destroy(back);
    if (win && (s || w_of(win))) k_destroy(win);
}
static void std_close(DSLObj win) {                                  /* red X, Escape with no sub-page: the window's own Close */
    StdE *s = std_of(win); if (!s) return;
    void (*c)(void *) = s->close; void *ud = s->ud;
    if (c) c(ud); else k_window_close(win);
}
static void std_esc(void *ud) {                                      /* Escape: Back one level first, then the Close */
    StdE *s = std_of((DSLObj)ud); if (!s) return;
    if (s->back && s->back(s->ud)) { dsl_log("ui: Escape: Back one level (a window of the template)"); return; }
    std_close((DSLObj)ud);
}
#define RULE_MAX 64
static struct { uintptr_t h; int rules; } rown[RULE_MAX]; static int nrown;
static int rules_of(Obj root) { int r = 0; for (int i = 0; i < nrown; i++) if (hget(rown[i].h) == root) r |= rown[i].rules; return r; }
static int k_rules_own(DSLObj win, int rules) {
    W *w = w_of(win); Obj o = w ? w_root(w) : (Obj)win; if (!alive(o)) return -1;
    for (int i = nrown - 1; i >= 0; i--) if (!hget(rown[i].h)) { hfree(&rown[i].h); rown[i] = rown[--nrown]; }
    for (int i = 0; i < nrown; i++) if (hget(rown[i].h) == o) { rown[i].rules |= rules; return 0; }
    if (nrown == RULE_MAX) return -1;
    rown[nrown].h = x_weak ? x_weak(o, false) : hnew(o); rown[nrown].rules = rules; nrown++;
    return 0;
}
static DSLObj k_window_std(const DSLWinStd *o, DSLWinParts *out) {
    DSLWinParts p; memset(&p, 0, sizeof p); if (out) *out = p;
    if (!o || o->size < offsetof(DSLWinStd, band_h) || o->w < 40 || o->h < 40) { dsl_log("ui: window_std: bad options"); return NULL; }
    int panel = o->kind == DSL_WIN_PANEL;
    if (panel && (!o->move_key || !*o->move_key)) { dsl_log("ui: window_std: a panel needs move_key (MOVE RULE)"); return NULL; }
    if (!ready_basic()) return NULL;
    std_sweep(); if (nstd == STD_MAX) { dsl_log("ui: window_std: more than %d windows: refused", STD_MAX); return NULL; }
    int ext = o->size >= offsetof(DSLWinStd, title_size) + sizeof o->title_size;
    float bh = ext && o->band_h > 0 ? o->band_h : 72, tx = ext && o->title_x > 0 ? o->title_x : 0, ts = ext && o->title_size > 0 ? o->title_size : 30;
    DSLObj back = NULL;
    if (!panel) {                                                    /* the dim backdrop: the game behind it takes no click */
        back = k_panel(NULL, 0, 0, 100, 100, 0); if (!back) return NULL;
        dsl_ui_flat(back, 0x0000009E); stretch((Obj)back);
        Obj bi = comp_get((Obj)back, K.Image); if (bi) gr_raycast(bi, 1);   /* flat turns clicks off: the backdrop must take them */
    }
    DSLObj win = k_panel(NULL, 0, 0, o->w, o->h, 0);
    if (!win) { if (back) k_destroy(back); return NULL; }
    if (panel) place_tl((Obj)win, o->x, o->y, o->w, o->h);
    else rt_set((Obj)win, (V2){ 0.5f, 0.5f }, (V2){ 0.5f, 0.5f }, (V2){ 0.5f, 0.5f }, (V2){ o->w, o->h }, (V2){ 0, 0 });
    dsl_ui_flat(k_panel(win, 4, 4, o->w - 8, o->h - 8, 0), 0x222025FF);   /* one body colour inside the frame */
    if (o->title) {                                                  /* the header: band, title in the game's title font, copper line */
        dsl_ui_flat(k_panel(win, 4, 4, o->w - 8, bh - 4, 0), 0x2D2A31FF);
        dsl_ui_flat(k_panel(win, 4, bh, o->w - 8, 1, 0), 0xB6835AFF);
        p.title = k_text(win, tx > 0 ? tx : 70, 4, tx > 0 ? o->w - tx - 70 : o->w - 140, bh - 4, o->title, ts, (tx > 0 ? DSL_TXT_LEFT : DSL_TXT_CENTER) | DSL_TXT_TITLE, 0);
        p.body_y = bh + 1;
    }
    StdE *s = &stds[nstd++]; memset(s, 0, sizeof *s);
    s->id = win; s->h = x_weak ? x_weak((Obj)win, false) : hnew((Obj)win); s->close = o->close; s->back = o->back; s->ud = o->ud; s->kind = o->kind;
    if (back) { s->back_id = back; s->h_back = x_weak ? x_weak((Obj)back, false) : hnew((Obj)back); }
    if (!panel || o->close) {                                        /* the game's red X (a text X only when its button is not found) */
        float xs = 38, xy = o->title ? (bh - xs) / 2 : 10;
        p.close_x = k_close_x(win, o->w - 24 - xs, xy, xs, win);
        if (!p.close_x) p.close_x = k_button(win, o->w - 24 - xs, xy, xs, xs, "X", x_click, win);
    }
    if (panel) k_esc_push(win, NULL, NULL, DSL_ESC_HUD);             /* an in-game panel never takes Escape */
    else k_esc_push(win, std_esc, win, 0);
    if (o->flags & DSL_WIN_OWN_SCALE) k_rules_own(win, DSL_RULE_SCALE); else k_scale_follow(win);
    if (panel) k_movable(win, o->move_key);
    p.win = win; p.backdrop = back;
    if (out) *out = p;
    return win;
}

#define CARD_MAX 256
static struct { uintptr_t h; DSLObj id, check; dsl_ui_cb click; void *ud; } cds[CARD_MAX]; static int ncds;
static uintptr_t h_no_targets;                                       /* an empty Image[]: the card's click layer never tints */
static void card_click(void *ud, DSLObj w) {
    (void)w; int i = (int)(intptr_t)ud; if (i < 0 || i >= ncds || !hget(cds[i].h)) return;
    W *t = w_of(cds[i].check);
    if (t && t->kind == W_TOGGLE) { if (!t->enabled) return; t->on = !t->on; toggle_look(t); if (t->cb) t->cb(t->ud, t->rt); }
    else if (cds[i].click) cds[i].click(cds[i].ud, cds[i].id);
}
static void card_quiet(Obj t, Obj keep, int depth) {                 /* texts and images stop taking clicks; buttons, toggles, fields keep theirs */
    if (!t || depth > 12) return;
    for (int i = 0, n = child_count(t); i < n; i++) {
        Obj c = child_at(t, i); if (c == keep || comp_get(c, K.Selectable)) continue;
        Obj g = comp_get(c, K.Graphic); if (g) gr_raycast(g, 0);
        card_quiet(c, keep, depth + 1);
    }
}
static int k_card_toggle(DSLObj card, DSLObj check, dsl_ui_cb click, void *ud) {
    W *cw = w_of(card); Obj root = cw ? w_root(cw) : (Obj)card;
    if (!alive(root) || !isa(tr_of(root), K.RectTransform) || (!check && !click) || !ready_basic()) return -1;
    int i = 0; while (i < ncds && hget(cds[i].h)) i++;
    if (i == ncds) { if (ncds == CARD_MAX) { dsl_log("ui: card_toggle: more than %d cards: refused", CARD_MAX); return -1; } ncds++; }
    hfree(&cds[i].h); cds[i].h = x_weak ? x_weak(root, false) : hnew(root); cds[i].id = card; cds[i].check = check; cds[i].click = click; cds[i].ud = ud;
    W *wd = w_new(W_BUTTON); if (!wd) return -1;
    Obj b = mk_btn(tr_of(root), 0, 0, 10, 10, "", 14, TOKEN(wd->id, P_CLICK), &wd->h_act[0]);
    if (!b) { w_free(nws - 1); return -1; }
    stretch(b); set_sibling(b, 0);                                   /* the whole card, under its content */
    Obj l = btn_label(b); if (l) go_active(l, 0);
    Obj bg = first_image(tr_of(b), 0); if (bg) { img_set(bg, NULL, (Col){ 0, 0, 0, 0 }, 0); gr_raycast(bg, 1); }
    if (!hget(h_no_targets) && K.Image) { Obj a = I.array_new(K.Image, 0); if (a) h_no_targets = hnew(a); }
    if (f_das_targets >= 0 && hget(h_no_targets)) AT(b, f_das_targets, Obj) = hget(h_no_targets);   /* held by a strong handle */
    if (f_sel_trans >= 0) AT(b, f_sel_trans, int32_t) = 0;          /* Selectable.Transition.None */
    Obj rg = comp_get(root, K.Graphic); if (rg) gr_raycast(rg, 0);
    card_quiet(tr_of(root), tr_of(b), 0);
    wd->cb = card_click; wd->ud = (void *)(intptr_t)i;
    hset(&wd->h_btn, b);
    w_done(wd, tr_of(b), NULL);
    return 0;
}

/* The UI rules' guard (owner 2026-10-25: rule + template + guard). Test copies only, once per root panel of ours that has
   an Escape registration (a window or an in-game panel; ESC-MISSING covers the others), 30 frames after it showed:
   "UI-RULE <rule> "<its first text>": <what to use>" when a window has no red X, a window or panel does not follow UI
   Scale (and did not say it applies it itself), an in-game panel (not full screen) cannot be moved, a check box card has
   no full-area click, or a text uses a font that is not the game's. The kit and esc tests fail on any such line. */
static int ui_rule_font_ok(Obj f) {
    if (!alive(f)) return 0;
    Obj ok[5] = { lang_font(0), lang_font(2), hget(h_cinzel), hget(h_body_font), NULL };
    Obj tt = hget(h_tpl_txt); if (tt && m_tmp_getfont) ok[4] = CALL(m_tmp_getfont, Obj, Obj)(tt, m_tmp_getfont);
    for (int i = 0; i < 5; i++) if (ok[i] && ok[i] == f) return 1;
    return 0;
}
static Obj ui_bad_font(Obj t, int depth) {                           /* the first text under t whose font is not the game's */
    if (!t || depth > 14 || !K.TMP_Text || !m_tmp_getfont) return NULL;
    Obj tx = comp_get(t, K.TMP_Text); if (tx && !ui_rule_font_ok(CALL(m_tmp_getfont, Obj, Obj)(tx, m_tmp_getfont))) return tx;
    for (int i = 0, n = child_count(t); i < n; i++) { Obj b = ui_bad_font(child_at(t, i), depth + 1); if (b) return b; }
    return NULL;
}
static void ui_rule(const char *rule, const char *win, const char *what) {
    dsl_log("UI-RULE %s \"%s\": %s", rule, win[0] ? win : "(no text)", what);
}
static void ui_guard_check(Obj root) {
    EscE *e = NULL;
    for (int i = 0; i < nesc && !e; i++) if (!escs[i].notice && hget(escs[i].h) == root) e = &escs[i];
    if (!e) return;                                                  /* a backdrop, or no Escape at all (ESC-MISSING) */
    char t[200] = ""; guard_text(root, t, sizeof t, 0); for (char *c = t; *c; c++) if (*c == '\n') *c = ' ';
    int hud = (e->flags & DSL_ESC_HUD) != 0, own = rules_of(root); float r[4];
    int full = rect_screen(root, r) && r[2] - r[0] >= 0.9f * screen_w() && r[3] - r[1] >= 0.9f * scr_h();
    if (hud && full) return;                                         /* a loading overlay: not a window */
    int x = 0, sc = 0, mv = 0;
    for (int k = 0; k < nws && !x; k++) x = ws[k]->kind == W_BUTTON && ws[k]->cb == x_click && ws[k]->ud == (void *)e->id && hget(ws[k]->h_rt);
    for (int k = 0; k < nsf && !sc; k++) sc = hget(sfs[k].h) == root;
    for (int k = 0; k < nmv && !mv; k++) mv = hget(mvs[k].h) == root;
    if (!hud && !x && !(own & DSL_RULE_X)) ui_rule("red-x", t, "a window of ours has no red X (dsl_window_std, or dsl_close_x)");
    if (!sc && !(own & DSL_RULE_SCALE)) ui_rule("ui-scale", t, "it does not follow UI Scale (dsl_window_std, dsl_scale_follow, or dsl_rules_own DSL_RULE_SCALE when it scales itself)");
    if (hud && !mv && !(own & DSL_RULE_MOVE)) ui_rule("move", t, "an in-game panel cannot be moved (dsl_window_std DSL_WIN_PANEL, or dsl_movable)");
    for (int k = 0; k < nws; k++) {                                  /* a bare check box (no label: a toggle row is its own full row) */
        W *tw = ws[k]; if (tw->kind != W_TOGGLE || !hget(tw->h_rt)) continue;   /* inside a card that is no full-area toggle */
        char lb[64] = ""; Obj lt = hget(tw->h_label); if (lt) utf8_of(tmp_get(lt), lb, sizeof lb); if (lb[0]) continue;
        Obj tr = w_root(tw), par = tr_parent(tr); W *pw = NULL;
        for (int q = 0; q < nws && !pw; q++) if (ws[q]->kind == W_PANEL && w_root(ws[q]) == par) pw = ws[q];
        if (!pw || par == root) continue;
        int under = 0; for (Obj u = par; u && !under; u = tr_parent(u)) under = u == root; if (!under) continue;
        int carded = 0; for (int q = 0; q < ncds && !carded; q++) carded = hget(cds[q].h) == par;
        R4 a = m_rt_rect ? CALL(m_rt_rect, R4, Obj)(tr_of(tr), m_rt_rect) : (R4){ 0 }, b = m_rt_rect ? CALL(m_rt_rect, R4, Obj)(tr_of(par), m_rt_rect) : (R4){ 0 };
        if (!carded && b.w * b.h > 2 * a.w * a.h) { ui_rule("card", t, "a check box card reacts only on its box (dsl_card_toggle)"); break; }
    }
    Obj bad = ui_bad_font(tr_of(root), 0);                         /* every text in the game's own fonts */
    if (bad) {
        char fn[96] = "(none)", w[160]; Obj f = CALL(m_tmp_getfont, Obj, Obj)(bad, m_tmp_getfont); if (alive(f)) obj_name(f, fn, sizeof fn);
        snprintf(w, sizeof w, "a text uses the font \"%s\", not the game's (kit texts: ui->text)", fn); ui_rule("font", t, w);
    }
}
/* test only: a loaded font asset that is not one of the game's fonts above (the guard's probe), NULL = none loaded */
__attribute__((visibility("default"))) DSLObj dsl_ui_test_foreign_font(void) {
    Obj a = K.FontAsset ? find_all(K.FontAsset) : NULL;
    for (int i = 0; i < arr_n(a); i++) if (!ui_rule_font_ok(arr_at(a, i))) return (DSLObj)arr_at(a, i);
    return NULL;
}

/* test only: give a text a font that is not the game's (the guard's "font" probe); 1 = done */
__attribute__((visibility("default"))) int dsl_ui_test_set_foreign_font(DSLObj text) {
    W *w = w_of(text); Obj t = w ? hget(w->h_label) : NULL, f = (Obj)dsl_ui_test_foreign_font();
    if (!isa(t, K.TMP_Text) || !f || !m_tmp_font) return 0;
    CALL(m_tmp_font, void, Obj, Obj)(t, f, m_tmp_font); return 1;
}

/* the loader's own: a stock window of the game shows (Options, Load Campaign, New Game, Credits, a notice ...) */
int dsl_ui_game_window_shown(void) {
    if (!K_PopUp || !m_obj_finds) return 0;
    Obj a = CALL(m_obj_finds, Obj, Obj)(type_of(K_PopUp), m_obj_finds); int n = arr_n(a);
    for (int i = 0; i < n; i++) if (on_tree(arr_at(a, i))) return 1;
    return 0;
}
/* the loader's own (dsl_update.c title corner): an empty group on the screen layer that takes no clicks; its widgets are
   placed in it, and it is scaled as one */
DSLObj dsl_ui_group(DSLObj parent, float x, float y, float w, float h) {
    if (!ready_basic()) return NULL;
    Obj p = parent_or_layer(parent), rt = p ? new_rect("dsl_ui_group", p) : NULL; if (!rt) return NULL;
    place_tl(rt, x, y, w, h);
    W *wd = w_new(W_PANEL); if (!wd) { destroy_obj(go_of(rt)); return NULL; }
    return w_done(wd, rt, NULL);
}
/* test only: the place kept under key (1 = kept) */
__attribute__((visibility("default"))) int dsl_ui_test_place(const char *key, float *x, float *y) {
    mvp_load(); for (int i = 0; i < nmvp; i++) if (!strcmp(mvp[i].key, key)) { *x = mvp[i].x; *y = mvp[i].y; return 1; } return 0;
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
    if (w && (w->kind == W_VALUE || w->kind == W_SLIDER)) h = VALUE_H;
    place_tl(r, x, y, w_, h);
    if (!w) return;
    w->w = w_; w->h = h;
    if (w->kind == W_TOGGLE) toggle_layout(w);
    if (w->kind == W_VALUE) value_layout(w);
    if (w->kind == W_SLIDER) slider_layout(w);
    if (w->kind == W_SCROLL) { V2 s = rt_size(hget(w->h_rt)); CALL(m_rt_size, void, Obj, V2)(hget(w->h_rt), (V2){ w_, s.y < h ? h : s.y }, m_rt_size); }
}
/* preferred height of a text widget's text at width w (0 = its own width), at its design size: a caller grows its box */
static float k_text_height(DSLObj o, float w) {
    W *wd = w_of(o); Obj t = wd ? hget(wd->h_label) : NULL;
    if (!t && o) t = isa((Obj)o, K.TMP_Text) ? (Obj)o : comp_get((Obj)o, K.TMP_Text);
    if (!t || !isa(t, K.TMP_Text)) return 0;
    if (w <= 0 && m_rt_rect) { Obj rt = tr_of(t); if (isa(rt, K.RectTransform)) w = CALL(m_rt_rect, FRect, Obj)(rt, m_rt_rect).w; }
    FitE *e = fit_find(t, 0);
    if (e && e->size >= 1 && w > 0) {                                /* a fitted text: at its design size, wrapped at w, then its fit again */
        fit_set(t, 0, e->size, 1); float ph = fit_pref(t, w).y;
        if (!e->on || e->on == 4) fit_set(t, 0, e->size, e->on == 4 ? 1 : e->wrap);
        else { CALL(m_tmp_wrap, void, Obj, int)(t, e->on == 2 ? 1 : e->wrap, m_tmp_wrap); fit_auto(t, 1); }
        return ph;
    }
    if (m_tmp_prefv && w > 0) return CALL(m_tmp_prefv, FV2, Obj, float, float)(t, w, 100000.f, m_tmp_prefv).y;
    void *ph = m_tmp_ph ? vmi(t, m_tmp_ph) : NULL;
    return ph ? CALL(ph, float, Obj)(t, ph) : 0;
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
    case W_BUTTON: case W_TOGGLE: btn_enable(hget(w->h_btn), on); if (w->kind == W_TOGGLE) toggle_look(w); break;
    case W_VALUE: btn_enable(hget(w->h_a), on); btn_enable(hget(w->h_b), on); gr_color(hget(w->h_c), on ? C_MAIN : C_GREY); break;
    case W_FIELD: { Obj f = hget(w->h_a); if (f && m_sel_inter) CALL(m_sel_inter, void, Obj, bool)(f, on != 0, m_sel_inter); } break;
    case W_SLIDER: { Obj f = hget(w->h_a); if (f && m_sel_inter) CALL(m_sel_inter, void, Obj, bool)(f, on != 0, m_sel_inter); gr_color(hget(w->h_c), on ? C_MAIN : C_GREY); } break;
    default: break;
    }
}
static void k_destroy(DSLObj o) {
    if (!o) return;
    k_esc_drop(o);
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
    .image_png = k_image_png, .tooltip = k_tooltip, .notice = k_notice, .notice_close = k_notice_close, .place = k_place, .set_active = k_set_active,
    .set_enabled = k_set_enabled, .destroy = k_destroy, .clipboard_get = k_clipboard_get, .clipboard_set = k_clipboard_set,
    .text_height = k_text_height, .slider = k_slider, .slider_get = k_slider_get, .slider_set = k_slider_set,
    .esc_push = k_esc_push, .esc_drop = k_esc_drop, .close_x = k_close_x, .ui_scale = k_ui_scale, .scale_follow = k_scale_follow,
    .movable = k_movable, .window_std = k_window_std, .window_close = k_window_close, .card_toggle = k_card_toggle, .rules_own = k_rules_own
};

/* ------------------------------------------------------------------ title screen buttons */
#define TBMAX 16
typedef struct { char owner[64], label[96], tip[600]; int order, seq; void (*click)(void *); void *ud; uintptr_t h_btn, h_view, h_act;
                 float icon; void (*fill)(void *, DSLObj, int); uintptr_t h_box; int shown; } TBE;
static TBE tbs[TBMAX]; static int ntbs, tb_seq;
static uintptr_t h_tview, h_tview_off; static int64_t tview_last = -1000, tview_fast_until;
int dsl_reg_scene(void);                                              /* dsl_reg.c: MainFlow scene, 1 title, 2 world, -1 none */
static Obj title_view(void) {
    Obj v = hget(h_tview); if (v) return v;
    /* in a world there is no title menu: no search. FindObjectOfType walks every object of the scene; after a world load
       (a new EventSystem) the fast window below searched up to 4 times a frame for 300 frames: ~22 fps for ~13 s after
       every save load on the Windows VM (player report ZET-U, 2026-10-20) */
    if (dsl_reg_scene() == 2) return NULL;
    Obj off = hget(h_tview_off);                                      /* seen while hidden: taken on the frame it shows */
    if (off && on_tree(off)) { hset(&h_tview, off); hfree(&h_tview_off); return off; }
    /* every frame before the first title and for a while after a scene change (a new EventSystem): the title found
       on the frame it shows; else every 30 frames */
    if (frames - tview_last < 30 && tview_last >= 0 && hget(h_tpl_btn) && frames > tview_fast_until) return NULL;
    tview_last = frames; v = find_obj(K.TitleMenuView, 0); hset(&h_tview, v);
    if (!v) hset(&h_tview_off, find_obj(K.TitleMenuView, 1));
    return v;
}
__attribute__((visibility("default")))
int dsl_ui_title_add(const char *owner_id, const char *label, int order, const char *tip, void (*click)(void *), void *ud) {
    if (!label || !click) return -1;
    /* the same owner, click and ud again = new text for that button (its owner after a language change): relabel */
    for (int i = 0; i < ntbs; i++) {
        TBE *e = &tbs[i];
        if (e->click != click || e->ud != ud || strcmp(e->owner, owner_id ? owner_id : "?")) continue;
        snprintf(e->label, sizeof e->label, "%s", label); dsi_utf8_fix_end(e->label);
        snprintf(e->tip, sizeof e->tip, "%s", tip ? tip : ""); dsi_utf8_fix_end(e->tip);
        e->order = order;
        Obj b = hget(e->h_btn), l = b ? btn_label(b) : NULL;
        if (l) tmp_set(l, e->label);
        if (b) k_tooltip(tr_of(b), e->label, e->tip[0] ? e->tip : NULL);
        dsl_log("ui: title button of %s relabelled \"%s\"", e->owner, label);
        return 0;
    }
    if (ntbs >= TBMAX) return -1;
    kit_used = 1;
    TBE *e = &tbs[ntbs++]; memset(e, 0, sizeof *e);
    snprintf(e->owner, sizeof e->owner, "%s", owner_id ? owner_id : "?"); snprintf(e->label, sizeof e->label, "%s", label);
    snprintf(e->tip, sizeof e->tip, "%s", tip ? tip : ""); dsi_utf8_fix_end(e->label); dsi_utf8_fix_end(e->tip);
    e->order = order; e->seq = tb_seq++; e->click = click; e->ud = ud;
    dsl_log("ui: title button \"%s\" (%s, order %d) registered", label, e->owner, order);
    return 0;
}
/* a picture box at the left of that button (dsl_sdk.h title_button_icon); made with the button (title_frame) */
__attribute__((visibility("default")))
int dsl_ui_title_icon(const char *owner_id, void (*click)(void *), void *ud, float size, void (*fill)(void *, DSLObj, int)) {
    for (int i = 0; i < ntbs; i++) {
        TBE *e = &tbs[i];
        if (e->click != click || e->ud != ud || strcmp(e->owner, owner_id ? owner_id : "?")) continue;
        e->icon = size > 0 && fill ? size : 0; e->fill = e->icon > 0 ? fill : NULL;
        hfree(&e->h_view);                                            /* made again with its box at the next title frame */
        dsl_log("ui: title button \"%s\": icon box %.0f", e->label, e->icon);
        return 0;
    }
    return -1;
}
/* the box: size x size at the button's left edge, centred in height, as far from the left edge as from the top; the
   label's left edge moves right of it (the text stays centred in what is left). Made with the clone, before the frame
   it is first drawn in, so the button never changes. */
static void tb_icon_make(TBE *e, Obj b) {
    Obj box = inst(hget(h_tpl_img), b); if (!box) { dsl_log("ui: title button \"%s\": icon box failed", e->label); return; }
    go_active(box, 1); gr_material(box, NULL); img_set(box, NULL, (Col){ 0, 0, 0, 0 }, 0);   /* empty, no clicks */
    Obj rt = tr_of(b), l = btn_label(b);
    FRect br = m_rt_rect && isa(rt, K.RectTransform) ? CALL(m_rt_rect, FRect, Obj)(rt, m_rt_rect) : (FRect){ 0, 0, 0, 0 };
    float pad = br.h > e->icon ? (br.h - e->icon) / 2 : 0; if (pad < 2) pad = 2;
    float gap = pad > 6 ? pad : 6;
    rt_set(box, (V2){ 0, 0.5f }, (V2){ 0, 0.5f }, (V2){ 0, 0.5f }, (V2){ e->icon, e->icon }, (V2){ pad, 0 });
    hset(&e->h_box, tr_of(box));
    /* the label's edges in its own offset units: the button's left edge is at -anchorMin.x x width, its right edge at
       (1 - anchorMax.x) x width (the label is a child of the button). Left: right of the box; right: as far in
       as the box's right side, so a long text that fills the label (auto size) keeps clear of the border */
    Obj lt = l ? tr_of(l) : NULL;
    if (lt && m_rt_omin && m_rt_setomin && m_rt_getamin && m_rt_omax && m_rt_setomax && m_rt_getamax && tr_parent(lt) == rt && br.w > 0) {
        V2 o = CALL(m_rt_omin, V2, Obj)(lt, m_rt_omin), a = CALL(m_rt_getamin, V2, Obj)(lt, m_rt_getamin);
        V2 o2 = CALL(m_rt_omax, V2, Obj)(lt, m_rt_omax), a2 = CALL(m_rt_getamax, V2, Obj)(lt, m_rt_getamax);
        float left = -a.x * br.w, want = left + pad + e->icon + gap, right = (1 - a2.x) * br.w, want2 = right - pad - gap;
        if (o.x < want) CALL(m_rt_setomin, void, Obj, V2)(lt, (V2){ want, o.y }, m_rt_setomin);
        if (o2.x > want2) CALL(m_rt_setomax, void, Obj, V2)(lt, (V2){ want2, o2.y }, m_rt_setomax);
        /* the narrower label keeps one line: smaller (down to the game's 0.75) rather than two lines (Spanish) */
        txt_style(l, 0, 22, 514, 0); FitE *fe = fit_find(l, 1); if (fe) fe->one = 1;
        dsl_log("ui: title button \"%s\": icon box %.0f at %.0f (button %.0fx%.0f), label %.0f-%.0f -> %.0f-%.0f",
                e->label, e->icon, pad, br.w, br.h, o.x - left, br.w - (right - o2.x), (o.x < want ? want : o.x) - left,
                br.w - (right - (o2.x > want2 ? want2 : o2.x)));
    } else dsl_log("ui: title button \"%s\": icon box %.0f, label not moved (label %s)", e->label, e->icon, lt ? "not a child of the button" : "missing");
}
static int tb_cmp(const void *a, const void *b) {
    const TBE *x = *(TBE *const *)a, *y = *(TBE *const *)b;
    return x->order != y->order ? (x->order < y->order ? -1 : 1) : x->seq - y->seq;
}
/* the multiplayer module's "Multiplayer" title button: the first title button after the stock Load button that is
   neither a stock button nor ours (multiplayer inserts its clones at Load + 1, "Multiplayer" first: mpmenu.c
   title_build), only while the module "multiplayer" runs. NULL = none. */
static Obj mp_title_button(Obj v, Obj col) {
    if (dsl_mod_state("multiplayer", NULL) != 1 || f_tv_btn[2] < 0) return NULL;
    Obj load = AT(v, f_tv_btn[2], Obj); if (!isa(load, K.TitleButton)) return NULL;
    int from = sibling(load);
    for (int c = from + 1, nc = child_count(col); from >= 0 && c < nc; c++) {
        Obj x = child_at(col, c), tb = comp_get(x, K.TitleButton); if (!tb) continue;
        int other = 0;
        for (int i = 0; i < ntbs; i++) other |= hget(tbs[i].h_btn) == tb;
        for (int i = 0; i < 7; i++) if (f_tv_btn[i] >= 0) other |= AT(v, f_tv_btn[i], Obj) == tb;
        if (!other) return x;
    }
    return NULL;
}
/* every frame on the title: our clones exist for this view, sit in the column right below the stock Load button's
   block (below the multiplayer clones, which are inserted after Load), in order, and show only while the stock buttons
   show (the multiplayer pages hide the stock column) */
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
        TBE *e = ord[i]; Obj b = hget(e->h_btn); int fresh = 0;
        if (!b || hget(e->h_view) != v) {
            if (b) destroy_obj(go_of(b));
            Obj c = inst(credit, col); b = c ? comp_get(c, K.TitleButton) : NULL;
            if (!b) { dsl_log("ui: title button \"%s\": clone failed", e->label); continue; }
            Obj a = mk_action(TOKEN((int)(e - tbs), P_TITLE));          /* the index in tbs[], not in ord[] */
            if (m_tb_init) CALL(m_tb_init, void, Obj, Obj)(b, a, m_tb_init);
            hset(&e->h_act, a);
            Obj l = btn_label(b);
            if (l) { das_lang_font(l); txt_style(l, 0, 22, 514, 1); tmp_set(l, e->label); }   /* the stock menu size (mpmenu title clones) */
            hset(&e->h_btn, b); hset(&e->h_view, v); n = child_count(col);
            if (e->tip[0]) k_tooltip(tr_of(b), e->label, e->tip);
            dsl_log("ui: title button \"%s\" added to the title column (%d children)", e->label, n);
            hfree(&e->h_box); e->shown = 0;
            if (e->icon > 0 && e->fill) { tb_icon_make(e, b); Obj box = hget(e->h_box); if (box) { e->fill(e->ud, (DSLObj)box, 1); fresh = 1; } }
        }
        go_active(b, stock_on);
        int on = on_tree(b); Obj box = hget(e->h_box);              /* shown again (a sub-page's Back): the module's turn */
        if (on && !e->shown && !fresh && box && e->fill) e->fill(e->ud, (DSLObj)box, 0);
        e->shown = on;
    }
    /* order (owner 2026-10-16 #2): right below the multiplayer module's "Multiplayer" button, then by order. That
       button is found by its owner, not by its label (the label is translated): when the module "multiplayer" runs,
       its title clones sit right after the stock Load button, "Multiplayer" first. Without it (owner 2026-10-20: the
       same place whatever runs): right below the stock "Load Campaign" button, where Multiplayer's block would start.
       Last in the column only when even the Load button is missing. */
    Obj prev = mp_title_button(v, col);
    if (!prev && f_tv_btn[2] >= 0) { Obj load = AT(v, f_tv_btn[2], Obj); if (isa(load, K.TitleButton)) prev = load; }
    for (int i = 0; i < ntbs; i++) {
        Obj b = hget(ord[i]->h_btn); if (!b) continue;
        if (!prev) { int want = n - ntbs + i; if (sibling(b) != want) set_sibling(b, want); continue; }
        int ps = sibling(prev), bs = sibling(b);
        if (bs != ps + 1) set_sibling(b, bs < ps ? ps : ps + 1);       /* SetSiblingIndex takes the final index */
        prev = b;
    }
}

/* the title menu is on screen (its stock button column shows; the multiplayer pages hide it) */
static uintptr_t h_lang; static int64_t lang_last = -1000;
static int lang_open(void) {                              /* the game's first-start Language picker (StartLanguageSelectView) */
    Obj l = hget(h_lang);
    if (!l && frames - lang_last >= 30) { lang_last = frames; l = find_obj(K.LangView, 0); hset(&h_lang, l); }
    return l && on_tree(l);
}
int dsl_ui_title_shown(void) {
    Obj v = title_view(); if (!v || !on_tree(v) || lang_open()) return 0;
    for (int i = 0; i < 7; i++) if (f_tv_btn[i] >= 0) { Obj b = AT(v, f_tv_btn[i], Obj); if (b && active_self(b)) return 1; }
    return 0;
}
/* a kit widget of a fixed design size (w x h units) kept inside its parent (the screen layer): anchored at (0.5, ay) of
   the parent, and scaled down (never up) when the parent is narrower or lower than the design (a 4:3 or a tall game
   window: the layer is 1080 high and 1080 x aspect wide). Called again every few frames, it follows a window resize. */
static void tr_scale(Obj o, float s);
void dsl_ui_fit_screen(DSLObj w, float wd, float h, float ay) {
    Obj o = w ? (Obj)w : NULL, p = o ? tr_parent(o) : NULL;
    if (!o || !p || !m_rt_rect || !isa(tr_of(p), K.RectTransform)) return;
    R4 r = CALL(m_rt_rect, R4, Obj)(tr_of(p), m_rt_rect);
    if (r.w < 100 || r.h < 100) return;                                /* not laid out yet */
    float s = r.w / wd, t = r.h / h; if (t < s) s = t; if (s > 1) s = 1;
    rt_set(o, (V2){ 0.5f, ay }, (V2){ 0.5f, ay }, (V2){ 0.5f, ay }, (V2){ wd, h }, (V2){ 0, 0 });
    tr_scale(o, s);
}
/* a kit widget pinned to the top-right corner of its parent (the screen layer): right / top margins, size */
void dsl_ui_place_tr(DSLObj w, float right, float top, float wd, float h) {
    if (w) rt_set((Obj)w, (V2){ 1, 1 }, (V2){ 1, 1 }, (V2){ 1, 1 }, (V2){ wd, h }, (V2){ -right, -top });
}
static void tr_scale(Obj o, float s);
static Obj tr_find(Obj t, const char *path);
/* the Load list's filter panel (dsl_rows.c, owner 2026-10-24): screen rect (pixels, y up) of a kit widget or any
   RectTransform, 1 = shown; the screen's size; a child by path; a widget's scale; a RectTransform's own width */
int dsl_ui_screen_rect(DSLObj o, float r[4]) {
    W *w = w_of(o); Obj t = w ? w_root(w) : (Obj)o;
    if (!t || !alive(t) || !rect_screen(t, r)) return 0;
    return on_tree(t);
}
/* the same for a kit widget kept as a plain pointer (dsl_update.c's corner): 0 once the kit forgot it (destroyed) */
int dsl_ui_widget_rect(DSLObj o, float r[4]) { return w_of(o) ? dsl_ui_screen_rect(o, r) : 0; }
/* the Load list's world rows (dsl_rows.c, owner 2026-10-24: the "Mods:" line never overlaps the name, never wraps):
   the TMP text of a kit widget or any object with a TMP_Text */
static Obj any_tmp(DSLObj o) {
    W *wd = w_of(o); Obj t = wd ? hget(wd->h_label) : NULL;
    if (!t && o && alive((Obj)o)) t = isa((Obj)o, K.TMP_Text) ? (Obj)o : comp_get((Obj)o, K.TMP_Text);
    return isa(t, K.TMP_Text) ? t : NULL;
}
/* its one-line preferred width (local units) at its design size (a fitted kit text: no wrap, no auto size while measured,
   then its fit as it was); any other text: at its current size */
static float text_line_w(DSLObj o, int at_min) {
    Obj t = any_tmp(o); if (!t || !m_tmp_pw) return 0;
    void *pw = vmi(t, m_tmp_pw);
    FitE *e = fit_find(t, 0);
    if (!e || e->size < 1) return CALL(pw, float, Obj)(t, pw);
    fit_set(t, 0, at_min ? fit_min(e->size) : e->size, 0); float x = CALL(pw, float, Obj)(t, pw);
    if (!e->on || e->on == 4) fit_set(t, 0, e->size, e->on == 4 ? 1 : e->wrap);
    else { CALL(m_tmp_wrap, void, Obj, int)(t, e->on == 2 ? 1 : e->wrap, m_tmp_wrap); fit_auto(t, 1); }
    return x;
}
float dsl_ui_text_line_w(DSLObj o) { return text_line_w(o, 0); }
/* ... at the kit's smallest fit size (0.75 of the design size): one line at that width = no wrap (CJK widths do not
   scale exactly with the size, so it is measured, not computed) */
float dsl_ui_text_line_w_min(DSLObj o) { return text_line_w(o, 1); }
/* the text a TMP text holds (rich-text tags included) */
size_t dsl_ui_text_get(DSLObj o, char *out, size_t cap) { Obj t = any_tmp(o); out[0] = 0; if (t) utf8_of(tmp_get(t), out, cap); return strlen(out); }
/* the direct children of a transform (their Transforms), at most max */
int dsl_ui_children(DSLObj parent, DSLObj *out, int max) {
    Obj p = parent && alive((Obj)parent) ? tr_of((Obj)parent) : NULL; int n = p ? child_count(p) : 0, k = 0;
    for (int i = 0; i < n && k < max; i++) { Obj c = child_at(p, i); if (c) out[k++] = (DSLObj)c; }
    return k;
}
int dsl_ui_shown(DSLObj o) { Obj t = o && alive((Obj)o) ? (Obj)o : NULL; return t && on_tree(t); }
/* test (dsl_rows.c lf_state): the size TMP drew the text at (TMP_Text.renderedWidth / renderedHeight, local units) */
int dsl_ui_text_rendered(DSLObj o, float *w, float *h) {
    static void *m_rw, *m_rh; Obj t = any_tmp(o); *w = *h = 0;
    if (!m_rw && K.TMP_Text) { m_rw = mth_(0, K.TMP_Text, "get_renderedWidth", 0, NULL); m_rh = mth_(0, K.TMP_Text, "get_renderedHeight", 0, NULL); }
    if (!t || !m_rw || !m_rh) return 0;
    void *a = vmi(t, m_rw), *b = vmi(t, m_rh); *w = CALL(a, float, Obj)(t, a); *h = CALL(b, float, Obj)(t, b);
    return 1;
}
void dsl_ui_screen_size(float *w, float *h) { *w = screen_w(); *h = m_scr_h ? (float)CALL0(m_scr_h, int)(m_scr_h) : 1080.f; }
DSLObj dsl_ui_child(DSLObj parent, const char *path) { Obj t = parent ? tr_find((Obj)parent, path) : NULL; return alive(t) ? (DSLObj)t : NULL; }
void dsl_ui_set_scale(DSLObj o, float s) { W *w = w_of(o); Obj r = w ? w_root(w) : (Obj)o; if (r) tr_scale(r, s); }
float dsl_ui_width(DSLObj o) {
    Obj rt = o ? tr_of((Obj)o) : NULL;
    return isa(rt, K.RectTransform) && m_rt_rect ? CALL(m_rt_rect, R4, Obj)(rt, m_rt_rect).w : 0;
}

/* ------------------------------------------------------------------ title brand (owner 2026-10-17)
   1. The word "ModLoader" under the game's title logo (TitleMenuUI/Img_BackGroundBox/Img_GameLogo), in the logo's
      look (tools/brand_word.py; the game's logo itself is not changed or copied). Our own Image, a child of the logo,
      drawn at the logo's texel scale (its rect / its sprite's size), centred right under the drawn letters, where the
      version line was. The version line (the logo's child Txt_Version) and the button column move down by the word's
      height, so their gaps stay the game's.
   2. The title menu (logo + button column) always fits, Quit visible, whatever buttons the modules add, and is centred:
      space above the logo = space below the last button. Too tall for the window: first the outer spaces shrink (to
      BR_MARGIN), then the gaps between the buttons (the column's spacing, 12 in the game, down to BR_SPMIN), then the
      whole menu scales down (logo and column together, never under 0.75). No button is hidden. Measured on the screen
      only while the stock title menu shows: at once when the window size or the rows shown change (a sub-page's Back,
      a module's button), else every 10 frames; the rows placed first (LayoutRebuilder), so the numbers are this
      frame's. It runs right before the UI is drawn (CanvasUpdateRegistry.PerformUpdate, after every Update and the
      modules' frame: Multiplayer's Back shows the stock buttons there), so the menu is drawn in its final place on
      the first frame it shows (owner 2026-10-17: the buttons jumped on Back from Multiplayer). A position the game
      sets again is taken as its new base. */
#include "dsl_brand_word.h"
#define BR_GAP 2.f                                                    /* units between the logo's rect and the word (no version line) */
#define BR_MARGIN 16.f                                                /* smallest space above the logo / below the last button, units */
#define BR_SPMIN 4.f                                                  /* smallest gap between two buttons, units */
#define BR_SCMIN 0.75f                                                /* smallest menu scale */
static uintptr_t h_br_view, h_br_logo, h_br_col, h_br_ver, h_br_img, h_br_spr, h_br_tex, h_br_lg;
static V2 br_logo0, br_col0, br_ver0, br_logo_set, br_col_set, br_ver_set;
static float br_drop, br_shift, br_sc = 1, br_sp0 = -1, br_sp = -1; static int br_tw, br_th, br_off; static int64_t br_last = -1000;
static uint32_t br_sig;
static V2 rt_pos(Obj o) { Obj rt = tr_of(o); return isa(rt, K.RectTransform) && m_rt_getpos ? CALL(m_rt_getpos, V2, Obj)(rt, m_rt_getpos) : (V2){ 0, 0 }; }
static void rt_setpos(Obj o, V2 p) { Obj rt = tr_of(o); if (isa(rt, K.RectTransform)) CALL(m_rt_pos, void, Obj, V2)(rt, p, m_rt_pos); }
static void tr_scale(Obj o, float s) { Obj t = tr_of(o); if (t && m_tr_setscale) CALL(m_tr_setscale, void, Obj, V3)(t, (V3){ s, s, 1 }, m_tr_setscale); }
static Obj tr_find(Obj t, const char *path) { t = tr_of(t); return t && m_tr_find ? CALL(m_tr_find, Obj, Obj, Obj)(t, str(path), m_tr_find) : NULL; }
static int v2_near(V2 a, V2 b) { float dx = a.x - b.x, dy = a.y - b.y; return dx > -0.01f && dx < 0.01f && dy > -0.01f && dy < 0.01f; }
static float fabs_(float x) { return x < 0 ? -x : x; }
static void br_apply(void) {
    Obj logo = hget(h_br_logo), col = hget(h_br_col), ver = hget(h_br_ver), lg = hget(h_br_lg);
    /* both scale about the logo's top (logo pivot = its top edge; the column keeps its offset from the logo x scale) */
    br_logo_set = (V2){ br_logo0.x, br_logo0.y + br_shift };
    br_col_set = (V2){ br_col0.x, br_logo_set.y + br_sc * (br_col0.y - br_drop - br_logo0.y) };
    br_ver_set = (V2){ br_ver0.x, br_ver0.y - br_drop };
    if (logo) { rt_setpos(logo, br_logo_set); tr_scale(logo, br_sc); }
    if (col) { rt_setpos(col, br_col_set); tr_scale(col, br_sc); }
    if (ver) rt_setpos(ver, br_ver_set);
    if (lg && m_lg_setsp && br_sp >= 0) CALL(m_lg_setsp, void, Obj, float)(lg, br_sp, m_lg_setsp);
}
static void brand_frame(void) {
    if (br_off || !m_rt_getpos || !m_tr_find || !m_spr_rect) return;
    Obj v = title_view(); if (!v || !on_tree(v) || f_tv_credit < 0 || !hget(h_tpl_img)) return;
    if (hget(h_br_view) != v) {                                       /* this title view's logo, column, version line; our word */
        Obj credit = AT(v, f_tv_credit, Obj), col = isa(credit, K.TitleButton) ? tr_parent(credit) : NULL;
        Obj logo = tr_find(v, "Img_BackGroundBox/Img_GameLogo"), lim = logo ? comp_get(logo, K.Image) : NULL, ls = img_sprite(lim);
        if (!col || !lim || !alive(ls)) { br_off = 1; dsl_log("ui: title brand: the game's logo or button column not found: off"); return; }
        Obj sp = hget(h_br_spr);
        if (!sp) { sp = png_sprite(brand_word_png, sizeof brand_word_png, &h_br_tex, &br_tw, &br_th); hset(&h_br_spr, sp); }
        if (!sp) { br_off = 1; dsl_log("ui: title brand: the word image could not be made: off"); return; }
        R4 sr = CALL(m_spr_rect, R4, Obj)(ls, m_spr_rect); V2 lsz = rt_size(logo);
        if (sr.w < 1 || sr.h < 1 || lsz.x < 1 || lsz.y < 1) { br_off = 1; dsl_log("ui: title brand: logo size %.0fx%.0f / sprite %.0fx%.0f: off", lsz.x, lsz.y, sr.w, sr.h); return; }
        float ww = br_tw * lsz.x / sr.w, wh = br_th * lsz.y / sr.h;     /* the logo's texel scale */
        Obj old = hget(h_br_img); if (old) destroy_obj(go_of(old));
        Obj im = inst(hget(h_tpl_img), logo); if (!im) { br_off = 1; return; }
        go_active(im, 1); gr_material(im, NULL); img_set(im, sp, C_WHITE, 0);
        Obj ver = tr_find(logo, "Txt_Version"), lg = K.HVLayout ? comp_get(col, K.HVLayout) : NULL;
        hset(&h_br_view, v); hset(&h_br_logo, logo); hset(&h_br_col, col); hset(&h_br_ver, ver); hset(&h_br_img, im); hset(&h_br_lg, lg);
        br_logo0 = rt_pos(logo); br_col0 = rt_pos(col); br_ver0 = ver ? rt_pos(ver) : (V2){ 0, 0 };
        br_sp0 = br_sp = lg && m_lg_getsp ? CALL(m_lg_getsp, float, Obj)(lg, m_lg_getsp) : -1;
        /* the word takes the version line's place (the empty band at the bottom of the logo's texture, right under the
           drawn letters); the version line goes below the word: anchored at the logo's bottom edge, pivot bottom */
        float top = ver ? br_ver0.y + rt_size(ver).y : -BR_GAP;
        rt_set(im, (V2){ 0.5f, 0 }, (V2){ 0.5f, 0 }, (V2){ 0.5f, 1 }, (V2){ ww, wh }, (V2){ 0, top });
        br_drop = ver ? wh : wh + BR_GAP; br_shift = 0; br_sc = 1; br_last = -1000;
        br_apply();
        dsl_log("ui: title brand: \"ModLoader\" under the logo (%dx%d texels, %.1fx%.1f units, top %.1f; logo %.0fx%.0f units, sprite %.0fx%.0f), version line%s and column %.1f lower; button gap %.0f",
                br_tw, br_th, ww, wh, top, lsz.x, lsz.y, sr.w, sr.h, ver ? "" : " (none)", br_drop, br_sp0);
    }
    Obj logo = hget(h_br_logo), col = hget(h_br_col), ver = hget(h_br_ver);
    if (!logo || !col) { hfree(&h_br_view); return; }
    /* the game set a position again (its own layout): that is the new base */
    int moved = 0; V2 p;
    if (!v2_near(p = rt_pos(logo), br_logo_set)) { br_logo0 = p; moved = 1; }
    if (!v2_near(p = rt_pos(col), br_col_set)) { br_col0 = p; moved = 1; }
    if (ver && !v2_near(p = rt_pos(ver), br_ver_set)) { br_ver0 = p; moved = 1; }
    if (moved) { dsl_log("ui: title brand: the game moved the logo / column / version line: placed again"); br_apply(); }
    if (!dsl_ui_title_shown() || !m_scr_h) return;
    /* what the menu's height depends on: the window, the rows shown, their heights */
    int n = child_count(col); uint32_t sig = 2166136261u;
    float sw = screen_w(), shh = (float)CALL0(m_scr_h, int)(m_scr_h);
    #define BR_MIX(x) do { uint32_t m_; float f_ = (x); memcpy(&m_, &f_, 4); sig = (sig ^ m_) * 16777619u; } while (0)
    BR_MIX(sw); BR_MIX(shh); BR_MIX((float)n);
    for (int i = 0; i < n; i++) { Obj c = child_at(col, i); int on = c && on_tree(c); BR_MIX(on ? rt_size(c).y : -1.f); }
    #undef BR_MIX
    if (sig == br_sig && frames - br_last < 10) return;
    br_sig = sig; br_last = frames;
    if (m_lr_force) CALL(m_lr_force, void, Obj)(tr_of(col), m_lr_force);   /* the rows' places in this frame, not the last one */
    /* measured: the menu's height now (screen pixels) = sc x (fixed part + gaps x spacing), in the parent's units */
    float rl[4], r[4], bottom = 1e9f; int nact = 0;
    if (!rect_screen(logo, rl)) return;
    for (int i = 0; i < n; i++) { Obj c = child_at(col, i); if (c && on_tree(c) && rect_screen(c, r)) { nact++; if (r[1] < bottom) bottom = r[1]; } }
    float sh = (float)CALL0(m_scr_h, int)(m_scr_h), ppu = (rl[2] - rl[0]) / rt_size(logo).x;   /* pixels per unit at our scale */
    if (!nact || ppu <= 0 || sh <= 0) return;
    float base = ppu / br_sc, hu = sh / base;                          /* pixels per parent unit, screen height in units */
    int ngap = nact > 1 && br_sp >= 0 ? nact - 1 : 0;
    float fixed = (rl[3] - bottom) / ppu - ngap * (br_sp >= 0 ? br_sp : 0), avail = hu - 2 * BR_MARGIN;
    float sp = br_sp0, sc = 1;
    if (ngap && fixed + ngap * sp > avail) {                           /* 1 outer spaces (to BR_MARGIN), 2 button gaps */
        sp = (avail - fixed) / ngap; if (sp < BR_SPMIN) sp = BR_SPMIN; if (sp > br_sp0) sp = br_sp0;
    }
    float h1 = fixed + ngap * (ngap ? sp : 0);
    if (h1 > avail) { sc = avail / h1; if (sc < BR_SCMIN) sc = BR_SCMIN; }   /* 3 the whole menu smaller */
    float gap = (hu - sc * h1) / 2, cur = (sh - rl[3]) / base, d = cur - gap;
    if (fabs_(d) < 0.5f && fabs_(sc - br_sc) < 0.002f && fabs_(sp - br_sp) < 0.05f) return;
    br_shift += d; br_sc = sc; br_sp = ngap ? sp : br_sp;
    br_apply();
    dsl_log("ui: title brand: placed (screen %.0f px = %.0f units; menu %.0f units, %d rows): %.0f units above and below, button gap %.1f, scale %.3f, moved %+.1f",
            sh, hu, sc * h1, nact, gap, br_sp, br_sc, d);
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
            NB.ok = NB.cancel = NULL; NB.serial = (NB.serial + 1) & 0x0FFFFFFF; notice_unfit();
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
static int in_tick; static int32_t last_frame = -1;
/* the game's language changed (the title's Options): fonts of the new language, title clones made again (their stock
   font and material for that language), then the loader relabels its texts and rebuilds an open Mods window */
static void kit_lang_changed(void) {
    hfree(&h_cinzel); hfree(&h_body_font); hfree(&h_body_mat); hfree(&h_plain_mat); hfree(&h_title_mat); body_last = -1000;
    for (int i = 0; i < ntbs; i++) hfree(&tbs[i].h_view);            /* title_frame clones them again */
    dsl_log("ui: language %s: fonts and title buttons renewed", dsi_code());
}
static void (*o_es_update)(Obj, const void *);
static void (*o_cur_perform)(Obj, const void *);
static void tick(void) {
    frames++;
    if (frames % 60 == 0) w_sweep();
    notice_fit_frame();
    assets_scan(0);
    /* templates at the first title seen, even hidden (a soft restart B goes from its title straight into a world; the
       kit could not build widgets there before: Mods window and rows after flow K) */
    if (!hget(h_tpl_btn) && frames % 30 == 0) templates_ready();
    if (ntbs) title_frame();
    if (!o_cur_perform) brand_frame();                              /* else right before the UI is drawn (h_cur_perform) */
    run_queue();
    fields_frame();
    esc_tick();
    esc_guard_frame();
    sf_frame();
    mv_frame();
    sliders_frame();
    tips_frame();
    if (!o_cur_perform) tip_place_frame();                          /* else right before the UI is drawn (h_cur_perform) */
}
/* right before the UI is laid out and drawn, after every Update and the modules' frame: the title menu as it will be
   drawn (our buttons follow the stock ones a module showed or hid this frame; the menu placed for the rows shown) */
static int in_late;
static void h_cur_perform(Obj self, const void *mi) {
    if (kit_ok && !in_late && !in_tick) {
        in_late = 1;
        if (!hget(h_tpl_btn)) templates_ready();                   /* the title's first frame: the word image needs them */
        if (ntbs) title_frame();
        brand_frame();
        tip_place_frame();
        dsl_rows_late();
        in_late = 0;
    }
    o_cur_perform(self, mi);
}
static void h_es_update(Obj self, const void *mi) {
    o_es_update(self, mi);
    int32_t fc = m_time_frame ? CALL0(m_time_frame, int)(m_time_frame) : last_frame + 1;
    if (fc == last_frame || in_tick) return;                        /* several EventSystems: once per frame */
    last_frame = fc; in_tick = 1;
    static uintptr_t h_es; if (!hget(h_es)) { hset(&h_es, self); tview_fast_until = frames + 300; }   /* a new scene (the last EventSystem is gone) */
    if (dsl_lang_poll()) { if (kit_ok) kit_lang_changed(); dsl_reg_lang_changed(); }
    if (kit_ok) tick();
    dsl_frame_hook();
    if (kit_ok) fit_frame();                                        /* last: the modules' texts of this frame are set and placed */
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
    K.Texture2D = klass(UE, "Texture2D"); K.TitleMenuView = klass(RU, "TitleMenuView"); K.LangView = klass(RU, "StartLanguageSelectView"); K.TitleButton = klass(RU, "SubUI_TitleMenuButton");
    K.Notice = klass(RU, "CommonNoticeView"); K.DASText = klass(RU, "DASText"); K.TooltipUI = klass(RU, "TooltipUI");
    K.TooltipETC = klass(RU, "Tooltip_ETC"); K.TipCtx = klass(RU, "TooltipContext"); K.UIFactory = klass("Refactor.Main.UI", "UIFactory");
    K.SaveLoadView = klass("Refactor", "SaveLoadView"); K.CanvasGroup = klass(UE, "CanvasGroup"); K.CampaignSetting = klass(RU, "SubUI_CampaignSetting");
    K.OptionView = dsl_find_class(RU, "GameOptionView"); K.GeneralSetting = dsl_find_class(RU, "SubUI_GeneralSetting");   /* slider template (optional) */
    K.SubSlider = dsl_find_class(RU, "SubUI_Slider"); K.Slider = dsl_find_class(UI, "Slider");
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
    m_tr_euler = OPT(K.Transform, "set_localEulerAngles", 1);
    m_tr_wpos = OPT(K.Transform, "get_position", 0); m_tr_setwpos = OPT(K.Transform, "set_position", 1);   /* hover card moves */
    m_tr_count = MTH(K.Transform, "get_childCount", 0); m_tr_child = MTH(K.Transform, "GetChild", 1);
    m_rt_amin = MTH(K.RectTransform, "set_anchorMin", 1); m_rt_amax = MTH(K.RectTransform, "set_anchorMax", 1);
    m_rt_pivot = MTH(K.RectTransform, "set_pivot", 1); m_rt_size = MTH(K.RectTransform, "set_sizeDelta", 1);
    m_rt_getsize = MTH(K.RectTransform, "get_sizeDelta", 0); m_rt_pos = MTH(K.RectTransform, "set_anchoredPosition", 1);
    m_rt_corners = MTH(K.RectTransform, "GetWorldCorners", 1);
    m_rt_getpos = OPT(K.RectTransform, "get_anchoredPosition", 0); m_tr_find = OPTSIG(K.Transform, "Find", 1, "System.String");   /* title brand */
    m_spr_rect = OPT(K.Sprite, "get_rect", 0); m_spr_tex = OPT(K.Sprite, "get_texture", 0); m_spr_texrect = OPT(K.Sprite, "get_textureRect", 0);
    m_img_ppm = OPT(K.Image, "set_pixelsPerUnitMultiplier", 1); m_scr_h = OPT(klass(UE, "Screen"), "get_height", 0);
    m_tr_setscale = OPT(K.Transform, "set_localScale", 1);
    m_rt_omin = OPT(K.RectTransform, "get_offsetMin", 0); m_rt_setomin = OPT(K.RectTransform, "set_offsetMin", 1);   /* title button icon */
    m_rt_getamin = OPT(K.RectTransform, "get_anchorMin", 0); m_rt_getamax = OPT(K.RectTransform, "get_anchorMax", 0);
    m_rt_omax = OPT(K.RectTransform, "get_offsetMax", 0); m_rt_setomax = OPT(K.RectTransform, "set_offsetMax", 1);
    K.HVLayout = klass(UI, "HorizontalOrVerticalLayoutGroup");
    if (K.HVLayout) { m_lg_setsp = OPT(K.HVLayout, "set_spacing", 1); m_lg_getsp = OPT(K.HVLayout, "get_spacing", 0); }
    m_gr_color = MTH(K.Graphic, "set_color", 1); m_gr_raycast = MTH(K.Graphic, "set_raycastTarget", 1); m_gr_material = OPT(K.Graphic, "set_material", 1);
    m_img_sprite = MTH(K.Image, "set_sprite", 1); m_img_getsprite = MTH(K.Image, "get_sprite", 0); m_img_type = OPT(K.Image, "set_type", 1);
    m_beh_enabled = OPT(kBeh, "set_enabled", 1);
    m_tmp_get = MTH(K.TMP_Text, "get_text", 0); m_tmp_set = MTH(K.TMP_Text, "set_text", 1);
    m_tmp_font = MTH(K.TMP_Text, "set_font", 1); m_tmp_getfont = MTH(K.TMP_Text, "get_font", 0); m_tmp_size = MTH(K.TMP_Text, "set_fontSize", 1);
    m_tmp_auto = MTH(K.TMP_Text, "set_enableAutoSizing", 1); m_tmp_align = MTH(K.TMP_Text, "set_alignment", 1);
    m_tmp_prefw = MTH(K.TMP_Text, "get_preferredWidth", 0);
    K.GridLayout = klass(UI, "GridLayoutGroup");
    if (K.GridLayout) { m_grid_cell = MTH(K.GridLayout, "set_cellSize", 1); m_grid_getcell = MTH(K.GridLayout, "get_cellSize", 0); m_lg_getpad = OPT(K.GridLayout, "get_padding", 0); }
    { void *kRO = dsl_find_class(UE, "RectOffset"); m_ro_gettop = OPT(kRO, "get_top", 0); m_ro_settop = OPT(kRO, "set_top", 1); }
    m_rt_rect = MTH(K.RectTransform, "get_rect", 0); m_tmp_getsize = MTH(K.TMP_Text, "get_fontSize", 0); m_tmp_getauto = MTH(K.TMP_Text, "get_enableAutoSizing", 0);
    m_tmp_wrap = MTH(K.TMP_Text, "set_textWrappingMode", 1); m_tmp_overflow = MTH(K.TMP_Text, "set_overflowMode", 1);
    m_tmp_rich = MTH(K.TMP_Text, "set_richText", 1); m_tmp_shmat = OPT(K.TMP_Text, "set_fontSharedMaterial", 1);
    m_asset_mat = OPT(K.TMP_Asset, "get_material", 0);
    m_tmp_fsmin = OPT(K.TMP_Text, "set_fontSizeMin", 1); m_tmp_fsmax = OPT(K.TMP_Text, "set_fontSizeMax", 1);
    m_tmp_pw = OPT(K.TMP_Text, "get_preferredWidth", 0); m_tmp_ph = OPT(K.TMP_Text, "get_preferredHeight", 0);
    m_tmp_prefv = OPTSIG(K.TMP_Text, "GetPreferredValues", 2, "System.Single,System.Single");
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
    m_spr_create7 = OPTSIG(K.Sprite, "Create", 7, "UnityEngine.Texture2D,UnityEngine.Rect,UnityEngine.Vector2,System.Single,System.UInt32,UnityEngine.SpriteMeshType,UnityEngine.Vector4");
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
    m_scr_w = OPT(klass(UE, "Screen"), "get_width", 0);
    { void *kLS = dsl_find_class("Refactor.Setting", "LanguageSetting");  /* the language's fonts (optional: else as before) */
      m_ls_font = OPT(kLS, "GetFont", 1); m_ls_mat = OPT(kLS, "GetMaterial", 2); }
    f_tb_label = fld(K.TitleButton, "_buttonName");
    { static const char *const nm[7] = { "_continueButton", "_newGameButton", "_loadCampaignButton", "_settingButton", "_creditButton", "_reportBugButton", "_quitButton" };
      for (int i = 0; i < 7; i++) f_tv_btn[i] = fld(K.TitleMenuView, nm[i]); f_tv_credit = f_tv_btn[4]; }
    f_nv_text = fld(K.Notice, "_noticeText"); f_nv_tok = fld(K.Notice, "_txtConfirm"); f_nv_tcancel = fld(K.Notice, "_txtCancle");
    f_das_usekey = fld(K.DASText, "_useTextKey"); f_das_style = fld(K.DASText, "_fontStyle"); f_das_fmat = fld(K.DASText, "_fontMaterial");
    f_etc_title = fld(K.TooltipETC, "_titleText"); f_etc_sublayout = fld(K.TooltipETC, "_subtitleLayout");
    f_etc_desc = fld(K.TooltipETC, "_desText"); f_etc_flavor = fld(K.TooltipETC, "_flavorText"); f_tb_canhold = fld(kTB, "_canShowHoldLayout"); f_tb_align = fld(kTB, "_alignmentRoutine");
    { int32_t o = fld(K.TipCtx, "<HideHoldLayout>k__BackingField"); f_ctx_hidehold = o >= 0x10 ? o - 0x10 : -1;   /* value type: minus the object header */
      ctx_size = K.TipCtx && I.class_value_size ? I.class_value_size(K.TipCtx, NULL) : 0; }
    if (K.OptionView && K.GeneralSetting && K.SubSlider && K.Slider) {
        f_gov_general = dsl_field_offset(K.OptionView, "_generalSetting"); f_gs_cam = dsl_field_offset(K.GeneralSetting, "_cameraMovementSpeedSlider");
        f_ss_slider = dsl_field_offset(K.SubSlider, "_slider"); f_ss_value = dsl_field_offset(K.SubSlider, "_valueText");
        f_ss_toggle = dsl_field_offset(K.SubSlider, "_toggle"); f_ss_toggletext = dsl_field_offset(K.SubSlider, "_toggleText");
        m_sl_setmin = OPT(K.Slider, "set_minValue", 1); m_sl_setmax = OPT(K.Slider, "set_maxValue", 1); m_sl_whole = OPT(K.Slider, "set_wholeNumbers", 1);
        m_sl_get = OPT(K.Slider, "get_value", 0); m_sl_set = OPT(K.Slider, "set_value", 1);
    }
    { void *kUSM = dsl_find_class("Refactor", "UIScaleManager"), *kDAS = dsl_find_class(RU, "DASButton"), *kCB = dsl_find_class(UI, "ColorBlock");
      m_usm_inst = OPT(kUSM, "get_Instance", 0); m_usm_scale = OPT(kUSM, "get_Scale", 0);           /* scale_follow, ui_scale */
      m_input_mb = OPTSIG(kInput, "GetMouseButton", 1, "System.Int32"); m_input_mbd = OPTSIG(kInput, "GetMouseButtonDown", 1, "System.Int32");   /* movable */
      f_slv_close = fld(K.SaveLoadView, "_closeButton"); f_das_targets = fld(kDAS, "_targets"); f_das_ttype = fld(kDAS, "_transitionType");   /* close_x */
      f_sel_trans = fld(K.Selectable, "m_Transition"); f_sel_colors = fld(K.Selectable, "m_Colors");
      colors_n = kCB && I.class_value_size ? I.class_value_size(kCB, NULL) : 0;
      K_PopUp = dsl_find_class(RU, "PopUpUI_Base"); }
    f_uif_prefabs = fld(K.UIFactory, "_prefabs"); f_slv_field = fld(K.SaveLoadView, "_saveNameInputField"); f_cs_field = fld(K.CampaignSetting, "_campaignNameInputField");
    if (miss_core) { dsl_log("ui: %d core game names missing (%s): kit off", miss_core, miss_list); return; }
    if (miss_opt) dsl_log("ui: %d optional names missing (%s): those parts are off", miss_opt, miss_list);
    if (!tip_ok()) dsl_log("ui: game tooltip off (TooltipContext size %#x, HideHoldLayout at %#x)", ctx_size, f_ctx_hidehold);
    void *upd = *(void **)m_es_update;
    if (dsl_hook(DSL_OWNER_LOADER, upd, (void *)h_es_update, (void **)&o_es_update, "loader ui EventSystem.Update")) {
        dsl_log("ui: EventSystem.Update hook refused: kit off (no frame tick)"); return;
    }
    /* optional: the title menu placed right before the UI is drawn (else in the EventSystem tick, as before) */
    { void *kLR = dsl_find_class(UI, "LayoutRebuilder"), *kCUR = dsl_find_class(UI, "CanvasUpdateRegistry");
      m_lr_force = kLR ? mi_sig(kLR, "ForceRebuildLayoutImmediate", 1, NULL) : NULL; if (m_lr_force && !*(void **)m_lr_force) m_lr_force = NULL;
      m_cur_perform = kCUR ? mi_sig(kCUR, "PerformUpdate", 0, NULL) : NULL;
      if (m_cur_perform && *(void **)m_cur_perform
          && dsl_hook(DSL_OWNER_LOADER, *(void **)m_cur_perform, (void *)h_cur_perform, (void **)&o_cur_perform, "loader ui CanvasUpdateRegistry.PerformUpdate"))
          o_cur_perform = NULL;
      dsl_log("ui: title menu placed %s, rows %s", o_cur_perform ? "before the UI is drawn" : "in the EventSystem tick", m_lr_force ? "rebuilt first" : "as last drawn"); }
    esc_install();
    kit_ok = 1;
    dsl_log("ui: kit ready (DSLUi size %zu, tooltip %s, notice %s, fields %s, images %s)", sizeof(DSLUi), tip_ok() ? "on" : "OFF",
            m_nv_select && m_nv_confirm ? "on" : "OFF", m_in_focused ? "on" : "OFF", m_tex_load && m_spr_create ? "on" : "OFF");
}
