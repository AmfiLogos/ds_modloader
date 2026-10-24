// Multi-unit view (mod id multi-bar): with 2 to 4 player units selected, every selected unit gets its own card at the
// bottom of the screen: portrait + name, mood + 5 status bars, its F/G/H behaviour buttons, its status effects, its item
// slot and its full skill bar. A click on card k uses unit k's skill / item through the game's own handlers (checks,
// failure notice, aiming). With 2+ selected the skill keys do nothing (mouse only). 0, 1 or 5+ selected = stock.
// Port of the 2026-10-04 "multibar" v4 (own Mac mini-loader, fixed addresses, owner: "very cool, impressive") to the
// loader SDK: every class, method and field by il2cpp name (Mac and Windows, survives moved addresses).
//
// How it works (unchanged from v4):
//  - bar copies = Object.Instantiate(the real QuickSlotUI view) + UI_Base.InitOnCreateUI + QuickSlotUI.Init, once each;
//    they follow the real bar's slot count (6 with the weapon-slot mod, 5 without) and are refreshed by running the
//    stock presenter methods with the presenter's View / _showingEntity swapped to copy k / unit k for the call;
//  - a click on copy k's slot s rides as Index = s | (k+1) << 8 through the stock click path (QuickSlotUI.OnSelect_SkillSlot,
//    the input factory, the event queue) into PlayerControlEventHandler.OnEvent(UseQuickSlotSkillRequested), where it
//    is decoded and the stock handler's one-unit logic runs for unit k (use_skill_for_unit); hover rides the same way
//    into CombatInfoViewPresenter._tempShowingSkillIndex (AoE preview of unit k's skill);
//  - F/G/H of card k: the option value carries (k+1) << 8 into OnEvent(ToggleBehaviourOption), which then targets unit k;
//  - item slot of card k: the next TryResolveSingleSelectedUnit (within 2 s) answers unit k.
// Multiplayer: inside a session (multiplayer role != none) the module stays stock (scope client; PLAN_v013.md).
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <time.h>
#endif

DSL_PLUGIN();

static const DSLHost *H;
static DSLPlugin *ME;

typedef void *Obj;
typedef struct { float x, y; } V2;
typedef struct { float x, y, z; } V3;
typedef struct { float r, g, b, a; } Color;
typedef struct { float x, y, w, h; } Rect;
typedef struct { int32_t x, y; } V2I;
typedef struct { uint64_t lo, hi; } Guid;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define MC(mi, R, ...) ((R (*)(__VA_ARGS__))(*(void **)(mi)))   /* call through MethodInfo.methodPointer */
#define MAXB 4

/* ======================================================================================== il2cpp API (exports) */
static struct {
    void *(*class_get_methods)(void *, void **);
    const char *(*method_get_name)(void *);
    uint32_t (*method_get_param_count)(void *);
    void *(*method_get_param)(void *, uint32_t);
    uint8_t (*method_is_generic)(void *);
    void *(*class_get_parent)(void *);
    void *(*class_get_nested_types)(void *, void **);
    const char *(*class_get_name)(void *);
    void *(*class_from_type)(void *);
    void *(*class_get_type)(void *);
    Obj (*type_get_object)(void *);
    void *(*class_get_field_from_name)(void *, const char *);
    size_t (*field_get_offset)(void *);
    void *(*field_get_type)(void *);
    void (*field_static_get_value)(void *, void *);
    void *(*object_get_class)(Obj);
    uint8_t (*class_is_assignable_from)(void *, void *);   /* C bool: low byte only (T2) */
    void *(*object_get_virtual_method)(Obj, void *);
    Obj (*object_new)(void *);
    Obj (*array_new)(void *, uintptr_t);
    Obj (*string_new)(const char *);
    Obj (*value_box)(void *, void *);
    uintptr_t (*gchandle_new)(Obj, uint8_t);              /* Il2CppGCHandle is pointer-sized in this Unity (T1) */
    Obj (*gchandle_get_target)(uintptr_t);
    void (*gchandle_free)(uintptr_t);
    void (*wbarrier)(Obj, void **, Obj);
    void (*runtime_class_init)(void *);
} IL;

static int n_missing;
static void miss(const char *what, const char *a, const char *b) { n_missing++; H->log(ME, "multi-bar: not found: %s %s%s%s", what, a, b ? "." : "", b ? b : ""); }

static void *kls(const char *ns, const char *name) { void *k = H->find_class(ns, name); if (!k) miss("class", ns, name); return k; }
static void *nested(void *outer, const char *name) {
    void *it = NULL, *k;
    while (outer && (k = IL.class_get_nested_types(outer, &it))) if (!strcmp(IL.class_get_name(k), name)) return k;
    miss("nested class", outer ? IL.class_get_name(outer) : "?", name);
    return NULL;
}
static void *class_of_type(void *t) { return t ? IL.class_from_type(t) : NULL; }
/* method by name, parameter count and (optional) parameter class names "Type,Transform"; walks the parents; skips
   generic definitions and methods without code */
static void *msig_q(void *k, const char *name, int argc, const char *sig) {
    for (void *c = k; c; c = IL.class_get_parent(c)) {
        void *it = NULL, *m;
        while ((m = IL.class_get_methods(c, &it))) {
            if (strcmp(IL.method_get_name(m), name) || (int)IL.method_get_param_count(m) != argc) continue;
            if (IL.method_is_generic && IL.method_is_generic(m)) continue;
            if (sig) {
                char buf[256] = ""; size_t o = 0;
                for (int i = 0; i < argc; i++) {
                    void *pk = class_of_type(IL.method_get_param(m, (uint32_t)i));
                    o += (size_t)snprintf(buf + o, o < sizeof buf ? sizeof buf - o : 0, "%s%s", i ? "," : "", pk ? IL.class_get_name(pk) : "?");
                }
                if (strcmp(buf, sig)) continue;
            }
            return m;
        }
    }
    return NULL;
}
static void *msig(void *k, const char *name, int argc, const char *sig) {
    void *m = k ? msig_q(k, name, argc, sig) : NULL;
    if (!m || !*(void **)m) { miss("method", k ? IL.class_get_name(k) : "?", name); return NULL; }
    return m;
}
static void *imeth(void *k, const char *name, int argc) {   /* interface method: no code of its own */
    void *m = k ? msig_q(k, name, argc, NULL) : NULL;
    if (!m) miss("interface method", k ? IL.class_get_name(k) : "?", name);
    return m;
}
static void *fld(void *k, const char *name) {
    for (void *c = k; c; c = IL.class_get_parent(c)) { void *f = IL.class_get_field_from_name(c, name); if (f) return f; }
    return NULL;
}
static int32_t fo(void *k, const char *name) {
    void *f = k ? fld(k, name) : NULL;
    if (!f) { miss("field", k ? IL.class_get_name(k) : "?", name); return -1; }
    return (int32_t)IL.field_get_offset(f);
}
static void *ftype_class(void *k, const char *name) { void *f = k ? fld(k, name) : NULL; return f ? class_of_type(IL.field_get_type(f)) : NULL; }
static int enum_v(void *k, const char *name, int dflt) {
    void *f = k ? IL.class_get_field_from_name(k, name) : NULL;
    if (!f) { H->log(ME, "multi-bar: enum %s.%s not found, using %d", k ? IL.class_get_name(k) : "?", name, dflt); return dflt; }
    int32_t v = dflt; IL.field_static_get_value(f, &v);
    return v;
}

/* ======================================================================================== names */
static struct {                                   /* classes */
    void *qsp, *qsui, *dc8, *slot, *itemslot, *pceh, *ev_useqs, *ev_tbo, *drag, *esv, *esus, *opt, *dc13, *affui, *parts,
         *dc43, *pusp, *civp, *unit, *inmap, *rmd, *pdh, *tooltip, *usmc, *cmp, *cdm, *skillbase, *askill, *execres, *vsd,
         *entcomp, *complist, *dastext, *uibase, *image, *hlg, *object, *transform, *sysui, *statutil, *func_mood,
         *dict_is, *bo_array_elem, *vecext;
    void *i_sel, *i_cm, *i_ctxc, *i_usm, *i_pud, *i_qsv, *i_disp;
} K;
static struct {                                   /* methods (MethodInfo*) */
    void *obj_implicit, *obj_inst, *obj_find, *obj_set_name, *obj_tostring, *comp_tr, *comp_go, *comp_get, *go_tr, *go_active,
         *tr_parent, *tr_setparent, *tr_find, *tr_scale, *tr_gsib, *tr_ssib, *rt_amin, *rt_amax, *rt_pivot, *rt_pos_get,
         *rt_pos, *rt_size_get, *rt_size, *rt_rect, *img_sprite, *img_fill, *g_color, *g_raycast, *lg_align, *input_mouse,
         *str_empty;
    void *qsp_refresh_skill, *qsp_refresh_item, *qsui_init, *qsui_getdown, *uibase_init, *das_settext, *unit_isdead,
         *unit_portrait, *name_get, *status_get, *aff_hascat, *info_bo, *skill_basic, *complist_tryreader, *dict_ent_tryget,
         *mood_list, *mood_type, *moodmax, *uiscale_inst, *uiscale_get, *color_preset, *uvdm_inst, *uvdm_portrait,
         *sound_inst, *sound_play, *tip_key, *tip_mood, *func_mood_ctor, *dc43_b0, *affui_set, *affui_off, *affui_frame,
         *opt_init, *opt_toggles, *opt_set, *dict_bo_get, *act_toggle_invoke, *dict_is_ctor, *dict_is_add, *civp_has,
         *civp_show, *civp_hide, *cdm_inst, *cdm_skill, *askill_basic, *askill_target, *askill_check, *exec_convert,
         *usmc_ctor, *usmc_add, *cmp_switch, *cmp_setctx, *cmp_left, *cam_s2w, *vec_logic, *dict_map_get, *vsd_ctor;
    void *sel_count, *sel_all, *sel_player, *cm_mode, *cm_ctx, *ctxc_usm, *usm_infos, *pud_qsv, *qsv_skillat, *qsv_index,
         *disp_add;
    void *ent_instance;                           /* FieldInfo* of the static EntityComponent._instance */
    void *dc8_b0, *dc8_b1, *dc13_b0, *pceh_useqs, *pceh_tbo;   /* hook sites the loader's hook_method cannot name */
} M;
static struct {                                   /* field offsets */
    int32_t qsp_view, qsp_sel, qsp_cm, qsp_inmap, qsp_pud, qsp_showing;
    int32_t qsui_slots, qsui_ammo, qsui_item, qsui_selected, qsui_ammoget, uibase_input, item_keytext, slot_imgsel, slot_keytext, slot_owner;
    int32_t dc8_index, dc8_this, pceh_cmp, pceh_sel, pceh_pud, pceh_map, pceh_maps, pceh_disp, useqs_index, tbo_target,
            tbo_option, tbo_ison, drag_type, drag_owner;
    int32_t esv_portrait, esv_name, esv_status, esv_opt, esv_aff, esv_tabs, esus_head, esus_body, parts_energy, parts_moods;
    int32_t opt_btns, opt_toggle, opt_task, opt_poss, opt_keys, dc13_index, dc13_this, inmap_rt, rmd_player, pdh_units,
            tip_type, dc43_unit, usmc_infos, civp_sel, civp_cm, civp_pud, civp_showing, civp_temp, unit_guid, skill_key,
            ent_components, usi_user, usi_skill;
} O;
static struct {                                   /* enum values, by name (defaults = build 25846389) */
    int ct_status, ct_info, ct_affecter, ct_skill, st_hh, st_hb, st_en, st_hu, st_str, st_mhh, st_mhb, st_men, st_mhu,
        st_mstr, st_mood, cm_default, cm_useskill, tt_self, upd_notice, sfx_click, aff_mentalbreak, tip_mood, cp_head,
        cp_body, drag_skill, find_inactive_incl, ta_lowerleft, ta_lowerright, bo_auto, bo_wait, bo_rest;
} E;
static int has_mood_tip;

/* ======================================================================================== small wrappers */
static void wset(Obj o, int32_t off, Obj v) { void **p = (void **)((char *)o + off); if (IL.wbarrier) IL.wbarrier(o, p, v); else *p = v; }
static int u_alive(Obj o) { return o && MC(M.obj_implicit, uint8_t, Obj, void *)(o, M.obj_implicit); }
static Obj gc_get(uintptr_t h) { return h ? IL.gchandle_get_target(h) : NULL; }
static void set_handle(uintptr_t *h, Obj o) { if (*h) IL.gchandle_free(*h); *h = o ? IL.gchandle_new(o, 0) : 0; }
static int isa(Obj o, void *k) { return o && k && (IL.class_is_assignable_from(k, IL.object_get_class(o)) & 1); }
static Obj type_of(void *k) { return k ? IL.type_get_object(IL.class_get_type(k)) : NULL; }
static Obj str(const char *s) { return IL.string_new(s); }
static int list_n(Obj l) { return l ? AT(l, 0x18, int32_t) : 0; }                  /* List<T>._size */
static Obj list_at(Obj l, int i) { return AT(AT(l, 0x10, char *), 0x20 + 8 * i, Obj); }   /* List<T>._items[i] */
static Guid guid_at(Obj o, int32_t off) { Guid g; memcpy(&g, (char *)o + off, sizeof g); return g; }
static Guid unit_guid(Obj u) { return guid_at(u, O.unit_guid); }
static int unit_dead(Obj u) { return MC(M.unit_isdead, uint8_t, Obj, void *)(u, M.unit_isdead) & 1; }
static void *vm(Obj o, void *imi) { return o && imi ? IL.object_get_virtual_method(o, imi) : NULL; }   /* interface / virtual target */
/* interface calls used here (the implementation's MethodInfo, NULL object or method = 0 / NULL) */
static Obj icall_o(Obj o, void *imi) { void *m = vm(o, imi); return m ? MC(m, Obj, Obj, void *)(o, m) : NULL; }
static int icall_i(Obj o, void *imi) { void *m = vm(o, imi); return m ? MC(m, int32_t, Obj, void *)(o, m) : 0; }
static int icall_b(Obj o, void *imi) { void *m = vm(o, imi); return m ? MC(m, uint8_t, Obj, void *)(o, m) & 1 : 0; }

static Obj go_of(Obj c) { return u_alive(c) ? MC(M.comp_go, Obj, Obj, void *)(c, M.comp_go) : NULL; }
static Obj tr_of(Obj c) {
    if (!u_alive(c)) return NULL;
    if (isa(c, K.transform)) return c;
    return MC(M.comp_tr, Obj, Obj, void *)(c, M.comp_tr);
}
static Obj go_tr(Obj go) { return u_alive(go) ? MC(M.go_tr, Obj, Obj, void *)(go, M.go_tr) : NULL; }
static void go_active(Obj go, int v) { if (u_alive(go)) MC(M.go_active, void, Obj, uint8_t, void *)(go, (uint8_t)v, M.go_active); }
static void comp_active(Obj c, int v) { if (u_alive(c)) go_active(go_of(c), v); }
static Obj instantiate(Obj orig, Obj parent) { return u_alive(orig) ? MC(M.obj_inst, Obj, Obj, Obj, void *)(orig, parent, M.obj_inst) : NULL; }
static Obj tr_parent(Obj t) { return u_alive(t) ? MC(M.tr_parent, Obj, Obj, void *)(t, M.tr_parent) : NULL; }
static void tr_set_parent(Obj t, Obj p) { if (u_alive(t)) MC(M.tr_setparent, void, Obj, Obj, uint8_t, void *)(t, p, 0, M.tr_setparent); }
static Obj tr_find(Obj t, const char *name) { return u_alive(t) ? MC(M.tr_find, Obj, Obj, Obj, void *)(t, str(name), M.tr_find) : NULL; }
static void tr_scale(Obj t, float s) { if (u_alive(t)) MC(M.tr_scale, void, Obj, V3, void *)(t, (V3){ s, s, 1.0f }, M.tr_scale); }
static int tr_sib(Obj t) { return u_alive(t) ? MC(M.tr_gsib, int32_t, Obj, void *)(t, M.tr_gsib) : 0; }
static void tr_set_sib(Obj t, int i) { if (u_alive(t)) MC(M.tr_ssib, void, Obj, int32_t, void *)(t, i, M.tr_ssib); }
static V2 rt_pos(Obj rt) { return u_alive(rt) ? MC(M.rt_pos_get, V2, Obj, void *)(rt, M.rt_pos_get) : (V2){ 0, 0 }; }
static void rt_set_pos(Obj rt, float x, float y) { if (u_alive(rt)) MC(M.rt_pos, void, Obj, V2, void *)(rt, (V2){ x, y }, M.rt_pos); }
static V2 rt_size(Obj rt) { return u_alive(rt) ? MC(M.rt_size_get, V2, Obj, void *)(rt, M.rt_size_get) : (V2){ 0, 0 }; }
static void rt_set_size(Obj rt, float x, float y) { if (u_alive(rt)) MC(M.rt_size, void, Obj, V2, void *)(rt, (V2){ x, y }, M.rt_size); }
static void rt_anchor(Obj rt, float x0, float y0, float x1, float y1, float px, float py) {
    if (!u_alive(rt)) return;
    MC(M.rt_amin, void, Obj, V2, void *)(rt, (V2){ x0, y0 }, M.rt_amin);
    MC(M.rt_amax, void, Obj, V2, void *)(rt, (V2){ x1, y1 }, M.rt_amax);
    MC(M.rt_pivot, void, Obj, V2, void *)(rt, (V2){ px, py }, M.rt_pivot);
}
static Rect rt_rect(Obj rt) { return u_alive(rt) ? MC(M.rt_rect, Rect, Obj, void *)(rt, M.rt_rect) : (Rect){ 0, 0, 0, 0 }; }
static Obj get_comp(Obj c, void *k) { return u_alive(c) && k ? MC(M.comp_get, Obj, Obj, Obj, void *)(c, type_of(k), M.comp_get) : NULL; }
static Obj find_any(void *k) { return k ? MC(M.obj_find, Obj, Obj, int32_t, void *)(type_of(k), E.find_inactive_incl, M.obj_find) : NULL; }
static void set_name(Obj o, const char *n) { if (u_alive(o)) MC(M.obj_set_name, void, Obj, Obj, void *)(o, str(n), M.obj_set_name); }
static void img_sprite(Obj img, Obj spr) { if (u_alive(img)) MC(M.img_sprite, void, Obj, Obj, void *)(img, spr, M.img_sprite); }
static void img_fill(Obj img, float v) { if (u_alive(img)) MC(M.img_fill, void, Obj, float, void *)(img, v, M.img_fill); }
static void graphic_color(Obj g, Color c) { void *m = vm(g, M.g_color); if (m) MC(m, void, Obj, Color, void *)(g, c, m); }
static void graphic_raycast(Obj g, int v) { void *m = vm(g, M.g_raycast); if (m) MC(m, void, Obj, uint8_t, void *)(g, (uint8_t)v, m); }
static void das_text(Obj t, Obj s) { if (u_alive(t)) MC(M.das_settext, void, Obj, Obj, void *)(t, s, M.das_settext); }
static int str_empty(Obj s) { return MC(M.str_empty, uint8_t, Obj, void *)(s, M.str_empty) & 1; }
static Obj image_of(Obj t) { return get_comp(t, K.image); }

/* readers of an entity (EntityComponent.GetReader<T>: _instance._components[entity].TryGetReader(type)) */
static Obj reader(Obj entity, int ctype) {
    Obj ec = NULL; IL.field_static_get_value(M.ent_instance, &ec);
    Obj comps = ec ? AT(ec, O.ent_components, Obj) : NULL;
    if (!comps || !entity) return NULL;
    Obj list = NULL;
    if (!(MC(M.dict_ent_tryget, uint8_t, Obj, Obj, Obj *, void *)(comps, entity, &list, M.dict_ent_tryget) & 1) || !list) return NULL;
    Obj r = NULL;
    if (!(MC(M.complist_tryreader, uint8_t, Obj, int32_t, Obj *, void *)(list, ctype, &r, M.complist_tryreader) & 1)) return NULL;
    return r;
}
static float stat(Obj sr, int type) { return MC(M.status_get, float, Obj, int32_t, void *)(sr, type, M.status_get); }

/* multiplayer: the module is stock inside a session */
typedef struct { uint32_t version, size; int (*role)(void); } mp_api_head;
static const mp_api_head *mp;
static int mp_session(void) { return mp && mp->role && mp->role() != 0; }

static double now(void) {
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9;
#endif
}

/* ======================================================================================== state (GC handles only, T1) */
static uintptr_t h_presenter, h_realview, h_realshow;
static uintptr_t h_copy[MAXB], h_unit[MAXB], h_cardImg[MAXB], h_cardTxt[MAXB], h_opt[MAXB];
static int n_active;
static float panel_y, bar_w = 430;
static int panel_y_known;
static Guid pending_item; static double pending_item_t;
static int swapping;                              /* set while the presenter's View points at a copy */

static Obj copy_k(int k) { Obj c = gc_get(h_copy[k]); return u_alive(c) ? c : NULL; }
static Obj unit_k(int k) { return gc_get(h_unit[k]); }
static int copy_index_of_view(Obj view) {
    if (!view || !n_active) return -1;
    for (int k = 0; k < MAXB; k++) if (h_copy[k] && gc_get(h_copy[k]) == view) return k;
    return -1;
}

/* ======================================================================================== presenter helpers */
/* run a stock presenter method against copy k: View and _showingEntity swapped for the call */
static void with_copy(Obj p, Obj view, Obj unit, void (*fn)(Obj, Obj, const void *), Obj data, const void *mi) {
    Obj v0 = AT(p, O.qsp_view, Obj), e0 = AT(p, O.qsp_showing, Obj);
    swapping = 1;
    wset(p, O.qsp_view, view); wset(p, O.qsp_showing, unit);
    fn(p, data, mi);
    wset(p, O.qsp_view, v0); wset(p, O.qsp_showing, e0);
    swapping = 0;
}
/* a game exception inside with_copy leaves the swap in place: put the real view back at the next hook */
static void unswap(Obj p) {
    if (!swapping || !p) return;
    swapping = 0;
    Obj v = gc_get(h_realview);
    if (u_alive(v)) wset(p, O.qsp_view, v);
    wset(p, O.qsp_showing, gc_get(h_realshow));
    H->log(ME, "multi-bar: a game error stopped a bar refresh; the stock bar was put back");
}
static void p_refresh_skill(Obj p, Obj d, const void *mi) { (void)d; (void)mi; MC(M.qsp_refresh_skill, void, Obj, void *)(p, M.qsp_refresh_skill); }
static void p_refresh_item(Obj p, Obj d, const void *mi) { (void)d; (void)mi; MC(M.qsp_refresh_item, void, Obj, void *)(p, M.qsp_refresh_item); }

/* party portrait order (RuntimeMapData._player._totalPlayerUnits; same order as the portraits on screen) */
static int party_rank(Obj p, Obj unit) {
    Obj inMap = AT(p, O.qsp_inmap, Obj);
    if (!isa(inMap, K.inmap)) return 999;
    Obj rt = AT(inMap, O.inmap_rt, Obj);
    if (!isa(rt, K.rmd)) return 999;
    Obj pdh = AT(rt, O.rmd_player, Obj); if (!pdh) return 999;
    Obj list = AT(pdh, O.pdh_units, Obj);
    for (int i = 0; i < list_n(list); i++) if (list_at(list, i) == unit) return i;
    return 999;
}

/* ======================================================================================== card */
#define CARD_L 106.0f
#define BLOCK_H 154.0f
#define BLOCK_W (CARD_L + bar_w)
static uintptr_t h_esv;
static Obj esv_get(void) {
    Obj v = gc_get(h_esv);
    if (!u_alive(v)) { v = find_any(K.esv); set_handle(&h_esv, v); }
    return u_alive(v) ? v : NULL;
}
static void hide_key_labels(Obj view) {
    Obj slots = AT(view, O.qsui_slots, Obj);
    for (int i = 0; i < list_n(slots); i++) {
        Obj s = list_at(slots, i); if (!s) continue;
        comp_active(AT(s, O.slot_keytext, Obj), 0);
        Obj bg = tr_find(tr_of(s), "Img_KeyBG"); if (bg) go_active(go_of(bg), 0);
    }
    Obj item = AT(view, O.qsui_item, Obj);
    if (item) {                                                /* Y cannot work with 2+ selected: no key on the card's item slot */
        comp_active(AT(item, O.item_keytext, Obj), 0);
        Obj kb = tr_find(tr_of(item), "Img_KeyBG"); if (kb) go_active(go_of(kb), 0);
    }
}
/* Card (owner feedback 2026-10-04 20:20), panel-local units, panel = the bar (430 x 82 with the weapon slot):
 *   left column  x -106..-6 : status block (mood + 5 bars with icons) y 2..42, portrait 80x80 y 46..126
 *   name           x -106.. : y 130..154
 *   top row        y 86..   : F/G/H left (~150), status effects middle (24-unit icons, grow up), item slot right */
static void make_card(int k, Obj panelTr) {
    Obj esv = esv_get(); if (!esv) { H->log(ME, "multi-bar: card: EntitySummaryUIView not found"); return; }
    Obj img = AT(esv, O.esv_portrait, Obj), txt = AT(esv, O.esv_name, Obj);
    if (!u_alive(img) || !u_alive(txt)) return;
    Obj cardTr = go_tr(instantiate(go_of(tr_parent(tr_of(img))), panelTr));   /* "Portrait": frame + image */
    rt_anchor(cardTr, 0, 0, 0, 0, 1, 0);
    rt_set_pos(cardTr, -16, 46); tr_scale(cardTr, 0.8f);
    Obj cimg = image_of(tr_find(cardTr, "Img_Portrait"));
    Obj ctxt = instantiate(txt, panelTr);                      /* cloned DASText */
    Obj txtTr = tr_of(ctxt);
    rt_anchor(txtTr, 0, 0, 0, 0, 0, 0);
    rt_set_size(txtTr, BLOCK_W - 10 < 420 ? BLOCK_W - 10 : 420, 24); rt_set_pos(txtTr, -CARD_L, 130);
    set_handle(&h_cardImg[k], cimg); set_handle(&h_cardTxt[k], ctxt);
}
static void fill_card(int k, Obj unit) {
    Obj img = gc_get(h_cardImg[k]), txt = gc_get(h_cardTxt[k]);
    if (u_alive(img)) {
        Obj aff = reader(unit, E.ct_affecter);
        int stressed = aff ? MC(M.aff_hascat, uint8_t, Obj, int32_t, void *)(aff, E.aff_mentalbreak, M.aff_hascat) & 1 : 0;
        Obj uvdm = MC(M.uvdm_inst, Obj, void *)(M.uvdm_inst);
        Obj id = MC(M.unit_portrait, Obj, Obj, void *)(unit, M.unit_portrait);
        Obj spr = uvdm ? MC(M.uvdm_portrait, Obj, Obj, Obj, uint8_t, void *)(uvdm, id, (uint8_t)stressed, M.uvdm_portrait) : NULL;
        if (stressed && !u_alive(spr) && uvdm) spr = MC(M.uvdm_portrait, Obj, Obj, Obj, uint8_t, void *)(uvdm, id, 0, M.uvdm_portrait);
        img_sprite(img, spr);
    }
    if (u_alive(txt)) das_text(txt, MC(M.name_get, Obj, Obj, void *)(unit, M.name_get));
}

/* ---------------- card status block + status effects (v4 spec/cardstatus.c.txt) */
static const char *kFrame[5] = { "Frame_HeadHealth", "Frame_BodyHealth", "Frame_Energy", "Frame_Hunger", "Frame_Stress" };
static const char *kFill[5]  = { "Img_HeadHealth",   "Img_BodyHealth",   "Img_Energy",   "Img_Hunger",   "Img_Stress" };
typedef struct { uintptr_t fill[5], mood; } CardStatus;
static CardStatus g_cs[MAXB];
static uintptr_t h_parts;
static Obj parts_get(void) {
    Obj v = gc_get(h_parts);
    if (!u_alive(v)) { v = find_any(K.parts); set_handle(&h_parts, v); }
    return u_alive(v) ? v : NULL;
}
static void make_status(int k, Obj panelTr) {
    Obj parts = parts_get(), esv = esv_get();
    if (!parts || !esv || !AT(parts, O.parts_energy, Obj) || !AT(esv, O.esv_status, Obj)) return;
    /* the whole party status panel (bars + mood): Img_Energy -> Frame_Energy -> Panel_StatusPanel */
    Obj srcPanel = tr_parent(tr_parent(tr_of(AT(parts, O.parts_energy, Obj))));
    Obj st = go_tr(instantiate(go_of(srcPanel), panelTr));
    if (!st) return;
    rt_anchor(st, 0, 0, 0, 0, 0, 0);
    rt_set_size(st, 100, 40); rt_set_pos(st, -CARD_L, 2);     /* same left edge as the portrait column */
    for (int i = 0; i < 5; i++) {
        Obj f = tr_find(st, kFrame[i]); if (!f) continue;
        rt_anchor(f, 0, 1, 1, 1, 0.5f, 0.5f);
        rt_set_size(f, -33, 4); rt_set_pos(f, 16.5f, -4 - 8 * i);   /* x 33..100, row centre y -4-8i */
        set_handle(&g_cs[k].fill[i], image_of(tr_find(f, kFill[i])));
    }
    Obj m = tr_find(st, "Icon_Mood");
    if (m) {
        rt_anchor(m, 0, 0.5f, 0, 0.5f, 0, 0.5f);
        rt_set_size(m, 18, 18); rt_set_pos(m, 0, 0);
        set_handle(&g_cs[k].mood, image_of(m));
    }
    /* stat icons: clones of the summary's live icon objects (Image only, raycast off) */
    Obj su = AT(esv, O.esv_status, Obj), sut = tr_of(su);
    Obj hh = AT(su, O.esus_head, Obj), hb = AT(su, O.esus_body, Obj);
    Obj src[5] = { u_alive(hh) ? tr_of(hh) : NULL, u_alive(hb) ? tr_of(hb) : NULL,
                   tr_find(sut, "OnlyPlayerPanel/Layout_Status/Panel_Energy/Frame_Icon"),
                   tr_find(sut, "OnlyPlayerPanel/Layout_Status/Panel_Hungry/Frame_Icon"),
                   tr_find(sut, "OnlyPlayerPanel/Layout_Status/Stress/Frame_Icon") };
    for (int i = 0; i < 5; i++) {
        if (!src[i]) { H->log(ME, "multi-bar: status icon %d missing", i); continue; }
        Obj ic = go_tr(instantiate(go_of(src[i]), st));
        rt_anchor(ic, 0, 1, 0, 1, 0, 0.5f);
        rt_set_size(ic, 10, 8); rt_set_pos(ic, 21, -4 - 8 * i);
        Obj img = image_of(ic);
        if (img && i < 2) graphic_color(img, MC(M.color_preset, Color, int32_t, void *)(i == 0 ? E.cp_head : E.cp_body, M.color_preset));
        if (img) graphic_raycast(img, 0);
    }
}
static void fill_status(int k, Obj unit) {
    Obj sr = reader(unit, E.ct_status);
    if (!sr) return;
    const int cur[5] = { E.st_hh, E.st_hb, E.st_en, E.st_hu, E.st_str }, mx[5] = { E.st_mhh, E.st_mhb, E.st_men, E.st_mhu, E.st_mstr };
    for (int i = 0; i < 5; i++) {
        Obj f = gc_get(g_cs[k].fill[i]); if (!u_alive(f)) continue;
        float m = stat(sr, mx[i]);
        img_fill(f, m > 0 ? stat(sr, cur[i]) / m : 0);
    }
    Obj mi = gc_get(g_cs[k].mood), parts = parts_get();
    if (u_alive(mi) && parts) {
        IL.runtime_class_init(K.sysui); IL.runtime_class_init(K.statutil);
        float mmax = MC(M.moodmax, float, void *)(M.moodmax);
        int type = MC(M.mood_type, int32_t, float, void *)(mmax > 0 ? stat(sr, E.st_mood) / mmax : 0, M.mood_type);
        Obj list = AT(parts, O.parts_moods, Obj);
        if (list && type >= 0 && type < list_n(list)) img_sprite(mi, list_at(list, type));
    }
}
static void set_mood_tooltip(Obj moodImg, Obj unit) {
    if (!has_mood_tip) return;
    Obj fb = get_comp(moodImg, K.tooltip);
    if (!fb) return;
    Obj dc = IL.object_new(K.dc43);
    wset(dc, O.dc43_unit, unit);
    MC(M.tip_key, void, Obj, Obj, void *)(fb, str("STATUS_Mood"), M.tip_key);
    AT(fb, O.tip_type, int32_t) = E.tip_mood;
    Obj fn = IL.object_new(K.func_mood);                        /* Func<MoodTooltipData>(dc, <SetMoodTooltip>b__0): the party card's */
    if (!fn) return;
    MC(M.func_mood_ctor, void, Obj, Obj, void *, void *)(fn, dc, M.dc43_b0, M.func_mood_ctor);
    MC(M.tip_mood, void, Obj, Obj, void *)(fb, fn, M.tip_mood);
}
typedef struct { uintptr_t aui; uint32_t sig; int n; } CardAff;
static CardAff g_ca[MAXB];
static void make_affecters(int k, Obj panelTr, float leftX, float widthV, float c /* icon scale, 0.75 -> 24-unit icons */) {
    Obj esv = esv_get();
    if (!esv || !u_alive(AT(esv, O.esv_aff, Obj))) return;
    Obj aui = instantiate(AT(esv, O.esv_aff, Obj), panelTr);    /* clone of Layout_Affecters (the component) */
    Obj t = tr_of(aui); if (!t) return;
    rt_anchor(t, 0, 1, 0, 1, 0, 0);                              /* bottom-left pivot: grows upward */
    tr_scale(t, c);
    rt_set_size(t, widthV / c, 0);                               /* height = its ContentSizeFitter */
    rt_set_pos(t, leftX, 4);                                     /* just above the bar top */
    MC(M.affui_off, void, Obj, void *)(aui, M.affui_off);       /* start empty */
    set_handle(&g_ca[k].aui, aui); g_ca[k].n = -1;
}
static uint32_t aff_sig(Obj list) {                              /* FNV-1a over count + every Key (UTF-16) */
    uint32_t h = 2166136261u; int n = list_n(list);
    h = (h ^ (uint32_t)n) * 16777619u;
    for (int i = 0; i < n; i++) {
        Obj key = AT(AT(list, 0x10, char *) + 0x20 + 0x18 * i, 0, Obj);   /* AffecterHolder {Key, Stack, Duration, InitialDuration} */
        int len = key ? AT(key, 0x10, int32_t) : 0; const uint16_t *ch = key ? (const uint16_t *)((char *)key + 0x14) : NULL;
        for (int j = 0; j < len; j++) h = (h ^ ch[j]) * 16777619u;
        h = (h ^ 0xFFFF) * 16777619u;
    }
    return h;
}
static void refresh_affecters(int k, Obj unit, int force) {
    Obj aui = gc_get(g_ca[k].aui); if (!u_alive(aui)) return;
    Obj rd = reader(unit, E.ct_affecter);
    if (!rd) { MC(M.affui_off, void, Obj, void *)(aui, M.affui_off); g_ca[k].n = -1; return; }
    Obj list = MC(M.mood_list, Obj, Obj, void *)(rd, M.mood_list);
    uint32_t s = aff_sig(list);
    if (force || s != g_ca[k].sig || g_ca[k].n < 0) {
        MC(M.affui_set, void, Obj, Obj, void *)(aui, list, M.affui_set);     /* sprites, show / hide, tooltip keys */
        g_ca[k].sig = s; g_ca[k].n = list_n(list);
    } else MC(M.affui_frame, void, Obj, Obj, void *)(aui, list, M.affui_frame);   /* duration shade + stack */
}
static void fill_live(int k) { Obj u = unit_k(k); if (u) { fill_status(k, u); refresh_affecters(k, u, 0); } }

/* ---------------- per-card behaviour toggles F / G / H (v4 spec/behaviour.c.txt) */
static Obj opt_k(int k) { Obj o = gc_get(h_opt[k]); return u_alive(o) ? o : NULL; }
static int opt_index(Obj ou) {
    if (!ou || !n_active) return -1;
    for (int k = 0; k < MAXB; k++) if (h_opt[k] && gc_get(h_opt[k]) == ou) return k;
    return -1;
}
static int unit_options(Obj unit) { Obj rd = reader(unit, E.ct_info); return rd ? MC(M.info_bo, int32_t, Obj, void *)(rd, M.info_bo) : -1; }
/* the clone's Txt_Key labels: DASText.SetText, so the clone's later Awake keeps them (v4 spec/fixes.c.txt 2) */
static void opt_fix_key_labels(Obj ou) {
    Obj btns = AT(ou, O.opt_btns, Obj), poss = AT(ou, O.opt_poss, Obj), keys = AT(ou, O.opt_keys, Obj);
    if (!btns || !poss || !keys) return;
    int n = AT(poss, 0x18, int32_t), nb = AT(btns, 0x18, int32_t);      /* arrays: max_length @0x18, data @0x20 */
    for (int i = 0; i < n && i < nb; i++) {
        int o = AT(poss, 0x20 + 4 * i, int32_t);
        Obj b = AT(btns, 0x20 + 8 * i, Obj); if (!u_alive(b)) continue;
        Obj s = NULL;
        if (!(MC(M.dict_bo_get, uint8_t, Obj, int32_t, Obj *, void *)(keys, o, &s, M.dict_bo_get) & 1)) continue;   /* TryGetValue */
        Obj das = get_comp(tr_find(tr_of(b), "Txt_Key"), K.dastext);
        if (das) das_text(das, s);
    }
}
static void make_opt(int k, Obj panelTr) {
    Obj esv = esv_get(); if (!esv) return;
    Obj real = AT(esv, O.esv_opt, Obj);
    if (!u_alive(real) || !AT(real, O.opt_toggle, Obj) || !AT(real, O.opt_keys, Obj)) { H->log(ME, "multi-bar: option UI not ready"); return; }
    Obj ou = instantiate(real, panelTr); if (!ou) return;
    MC(M.opt_init, void, Obj, Obj, Obj, Obj, void *)(ou, AT(real, O.opt_toggle, Obj), AT(real, O.opt_task, Obj), AT(real, O.opt_keys, Obj), M.opt_init);
    Obj poss = IL.array_new(K.bo_array_elem, 3);
    if (!poss) return;
    AT(poss, 0x20, int32_t) = E.bo_auto; AT(poss, 0x24, int32_t) = E.bo_wait; AT(poss, 0x28, int32_t) = E.bo_rest;
    MC(M.opt_toggles, void, Obj, Obj, void *)(ou, poss, M.opt_toggles);
    go_active(go_of(ou), 1);
    Obj t = tr_of(ou);
    V2 sz = rt_size(t);
    rt_anchor(t, 0, 1, 0, 1, 0, 0); rt_set_pos(t, 2, 4);
    if (sz.y > 0) tr_scale(t, 44.0f / sz.y < 1 ? 44.0f / sz.y : 1);   /* top row is ~48 units high */
    opt_fix_key_labels(ou);
    set_handle(&h_opt[k], ou);
}
static void refresh_opts(void) {
    for (int k = 0; k < n_active; k++) {
        Obj ou = opt_k(k), u = unit_k(k); if (!ou || !u) continue;
        int f = unit_options(u); if (f >= 0) MC(M.opt_set, void, Obj, int32_t, void *)(ou, f, M.opt_set);
    }
}

/* ======================================================================================== bar copies */
static Obj real_view(Obj p) {                            /* the stock bar (the presenter's View when no copy is swapped in) */
    Obj real = gc_get(h_realview);
    if (!u_alive(real) && p && !swapping) { real = AT(p, O.qsp_view, Obj); set_handle(&h_realview, real); }
    return u_alive(real) ? real : NULL;
}
static Obj create_copy(Obj p, int k) {
    Obj real = real_view(p);
    if (!real) return NULL;
    Obj copy = instantiate(real, tr_parent(tr_of(real)));
    if (!copy) return NULL;
    MC(M.uibase_init, void, Obj, Obj, void *)(copy, AT(real, O.uibase_input, Obj), M.uibase_init);
    /* no key labels on the copies: Init reads skillKeys[i] per slot, so every index a bar can have is there (null text) */
    Obj keys = IL.object_new(K.dict_is);
    if (!keys) return NULL;
    MC(M.dict_is_ctor, void, Obj, void *)(keys, M.dict_is_ctor);
    for (int i = 0; i < 16; i++) MC(M.dict_is_add, void, Obj, int32_t, Obj, void *)(keys, i, NULL, M.dict_is_add);
    MC(M.qsui_init, void, Obj, Obj, Obj, Obj, void *)(copy, AT(real, O.qsui_ammoget, Obj), keys, NULL, M.qsui_init);
    hide_key_labels(copy);
    MC(M.qsui_getdown, void, Obj, Obj, void *)(copy, NULL, M.qsui_getdown);      /* get-down button off */
    Obj ammo = AT(copy, O.qsui_ammo, Obj);
    for (int i = 0; i < list_n(ammo); i++) comp_active(list_at(ammo, i), 0);
    Obj panelTr = tr_find(tr_of(copy), "Panel_QuickSlot");
    if (panelTr) { V2 sz = rt_size(panelTr); if (sz.x > 100) bar_w = sz.x; }   /* 430 with the weapon slot, 362 without */
    Obj item = AT(copy, O.qsui_item, Obj);
    if (panelTr && u_alive(item)) {                          /* item slot above the bar's right end */
        Obj it = tr_of(item);
        tr_set_parent(it, panelTr);
        rt_anchor(it, 1, 1, 1, 1, 1, 0); rt_set_pos(it, -2, 4);
    }
    if (panelTr) {
        make_card(k, panelTr); make_opt(k, panelTr); make_status(k, panelTr);
        make_affecters(k, panelTr, 156, bar_w - 218, 0.75f);   /* between F/G/H and the item slot (212 wide on a 430 bar) */
    }
    char name[32]; snprintf(name, sizeof name, "QuickSlotUI_Multi%d", k);
    set_name(go_of(copy), name);
    set_handle(&h_copy[k], copy);
    H->log(ME, "multi-bar: bar copy %d made (bar %.0f wide, %d slots)", k, bar_w, list_n(AT(copy, O.qsui_slots, Obj)));
    return copy;
}

/* ---------------- the bottom HUD moves up while the row shows */
typedef struct { const char *cls; int side; void *klass; uintptr_t h; float x0, y0; int saved; } Shift;
static Shift g_shift[] = { { "EntitySummaryUIView", 0 }, { "EntityStatusUI", 1 }, { "EntitySkillUI", 1 }, { "EntityInventoryUI", 1 },
                           { "EntityWorkPlanView", 1 }, { "ControlModeUIView", 0 }, { "GridHoverView", 0 } };
#define NSHIFT (int)(sizeof g_shift / sizeof *g_shift)
static float g_sideDelta, g_shiftH = -1;
static void hud_shift(float h) {                               /* h = 0 puts the stock positions back */
    if (h == g_shiftH) return;
    for (int i = 0; i < NSHIFT; i++) {
        Shift *sh = &g_shift[i];
        Obj v = gc_get(sh->h);
        if (!u_alive(v)) {
            if (h == 0 && !sh->saved) continue;
            if (!sh->klass) sh->klass = H->find_class("Refactor.UI", sh->cls);
            v = find_any(sh->klass); set_handle(&sh->h, v); sh->saved = 0;
        }
        if (!u_alive(v)) continue;
        Obj rt = tr_of(v);
        if (!sh->saved) { V2 p = rt_pos(rt); sh->x0 = p.x; sh->y0 = p.y; sh->saved = 1; }
        rt_set_pos(rt, sh->x0, sh->y0 + (h != 0 && sh->side ? h - g_sideDelta : h));
    }
    g_shiftH = h;
}
static float ui_scale(void) {
    Obj m = MC(M.uiscale_inst, Obj, void *)(M.uiscale_inst);
    return u_alive(m) ? MC(M.uiscale_get, float, Obj, void *)(m, M.uiscale_get) : 1.0f;
}

/* ---------------- the summary panel in multi mode: body off, its tab row at the screen's left edge (v4 spec/fixes.c.txt 1) */
static uintptr_t h_sumPanel, h_tabs;
static int tabs_sib = 7, tabs_moved;
static void summary_multi(int on) {
    Obj esv = esv_get();
    Obj tabs = esv ? tr_of(AT(esv, O.esv_tabs, Obj)) : gc_get(h_tabs);   /* RadioButtonGroup on Layout_Buttons */
    if (!u_alive(tabs)) return;
    Obj root = esv ? tr_of(esv) : NULL;
    Obj hlg = get_comp(tabs, K.hlg);
    if (on && !tabs_moved) {
        if (!root) return;
        Obj panel = tr_parent(tabs);                                    /* "Panel" */
        set_handle(&h_sumPanel, panel); set_handle(&h_tabs, tabs);
        tabs_sib = tr_sib(tabs);
        tr_set_parent(tabs, root);
        rt_anchor(tabs, 0, 0, 0, 0, 0, 0);
        rt_set_size(tabs, 432, 28); rt_set_pos(tabs, -2, 0);
        if (hlg) MC(M.lg_align, void, Obj, int32_t, void *)(hlg, E.ta_lowerleft, M.lg_align);
        go_active(go_of(panel), 0);                                     /* body gone: portrait, name, F/G/H, panels */
        tabs_moved = 1;
    } else if (!on && tabs_moved) {
        Obj panel = gc_get(h_sumPanel);
        if (u_alive(panel)) {
            go_active(go_of(panel), 1);
            tr_set_parent(tabs, panel);
            tr_set_sib(tabs, tabs_sib);
        }
        rt_anchor(tabs, 0, 1, 1, 1, 0.5f, 0);
        rt_set_size(tabs, 0, 28); rt_set_pos(tabs, 0, 0);               /* stock values */
        if (hlg) MC(M.lg_align, void, Obj, int32_t, void *)(hlg, E.ta_lowerright, M.lg_align);
        tabs_moved = 0;
    }
}

/* ---------------- layout: one row, centred, shrunk to the screen width, never above the stock size */
#define GAP 16.0f
#define MARGIN 8.0f
static void layout(void) {
    Obj real = real_view(gc_get(h_presenter));
    if (!real) return;
    Rect r = rt_rect(tr_parent(tr_of(real)));
    float u = ui_scale(), W = r.w / u;                          /* bar copies live in the root scaled by u */
    float total = n_active * BLOCK_W + (n_active - 1) * GAP;
    float s = (W - 2 * MARGIN) / total; if (s > 1) s = 1;
    float x0 = -s * total / 2;
    for (int k = 0; k < n_active; k++) {
        Obj c = copy_k(k); if (!c) continue;
        Obj panelTr = tr_find(tr_of(c), "Panel_QuickSlot"); if (!panelTr) continue;
        if (!panel_y_known) { panel_y = rt_pos(panelTr).y; panel_y_known = 1; }
        tr_scale(panelTr, s);
        rt_set_pos(panelTr, x0 + s * (k * (BLOCK_W + GAP) + CARD_L + bar_w / 2), panel_y);
    }
    float h = u * (panel_y + s * BLOCK_H) + 6;
    g_sideDelta = 192 * u;
    hud_shift(h);
    summary_multi(1);
    static float lastH; if (h != lastH) H->log(ME, "multi-bar: layout n=%d canvas=%.0f u=%.2f scale=%.2f shift=%.0f", n_active, r.w, u, s, h); lastH = h;
}

/* ---------------- show / hide */
static void hide_all(void) {
    for (int k = 0; k < MAXB; k++) { Obj c = copy_k(k); if (c) go_active(go_of(c), 0); set_handle(&h_unit[k], NULL); }
    n_active = 0;
    summary_multi(0);
    if (g_shiftH > 0) hud_shift(0);
}
static void refresh_bar(Obj p, int k) {
    Obj c = copy_k(k), u = unit_k(k); if (!c || !u) return;
    Guid g = unit_guid(u); memcpy((char *)c + O.qsui_selected, &g, sizeof g);   /* QuickSlotUI.SetSelect */
    with_copy(p, c, u, p_refresh_skill, NULL, NULL);
    with_copy(p, c, u, p_refresh_item, NULL, NULL);
}
static void show_multi(Obj p, Obj *units, int n) {
    for (int k = 0; k < n; k++) {
        if (!copy_k(k) && !create_copy(p, k)) { H->log(ME, "multi-bar: bar copy %d failed", k); hide_all(); return; }
        set_handle(&h_unit[k], units[k]);
    }
    for (int k = n; k < MAXB; k++) { Obj c = copy_k(k); if (c) go_active(go_of(c), 0); set_handle(&h_unit[k], NULL); }
    n_active = n;
    for (int k = 0; k < n; k++) {
        go_active(go_of(copy_k(k)), 1);
        refresh_bar(p, k);
        fill_card(k, units[k]);
        Obj mood = gc_get(g_cs[k].mood); if (u_alive(mood)) set_mood_tooltip(mood, units[k]);
        refresh_affecters(k, units[k], 1);
        fill_live(k);
    }
    refresh_opts();
    layout();
}
static int sort_units(Obj p, Obj *in, int nin, Obj *out) {    /* party order, dead units out */
    int rank[MAXB], n = 0;
    for (int i = 0; i < nin && n < MAXB; i++) {
        Obj e = in[i];
        if (!isa(e, K.unit) || unit_dead(e)) continue;
        int r = party_rank(p, e), j = n++;
        while (j > 0 && rank[j - 1] > r) { out[j] = out[j - 1]; rank[j] = rank[j - 1]; j--; }
        out[j] = e; rank[j] = r;
    }
    return n;
}

/* ======================================================================================== hooks: presenter */
typedef void (*fn_p)(Obj, const void *);
typedef void (*fn_pd)(Obj, Obj, const void *);
static fn_p o_init;
static fn_pd o_select, o_frame, o_useskill, o_skillchg, o_behavchg, o_invchg, o_swap, o_civp;
static void reset_state(void) {
    for (int k = 0; k < MAXB; k++) {
        set_handle(&h_copy[k], NULL); set_handle(&h_unit[k], NULL); set_handle(&h_cardImg[k], NULL); set_handle(&h_cardTxt[k], NULL);
        set_handle(&h_opt[k], NULL); set_handle(&g_cs[k].mood, NULL); set_handle(&g_ca[k].aui, NULL);
        for (int i = 0; i < 5; i++) set_handle(&g_cs[k].fill[i], NULL);
    }
    for (int i = 0; i < NSHIFT; i++) { set_handle(&g_shift[i].h, NULL); g_shift[i].saved = 0; }
    set_handle(&h_sumPanel, NULL); set_handle(&h_tabs, NULL); set_handle(&h_esv, NULL); set_handle(&h_parts, NULL);
    n_active = 0; panel_y_known = 0; g_shiftH = -1; tabs_moved = 0; swapping = 0;
}
static void hk_init(Obj p, const void *mi) {
    o_init(p, mi);
    reset_state();
    set_handle(&h_presenter, p);
    set_handle(&h_realview, AT(p, O.qsp_view, Obj));
    set_handle(&h_realshow, NULL);
    H->log(ME, "multi-bar: skill bar presenter ready");
}
static void hk_select(Obj p, Obj data, const void *mi) {
    unswap(p);
    o_select(p, data, mi);
    set_handle(&h_realshow, AT(p, O.qsp_showing, Obj));
    Obj sel = AT(p, O.qsp_sel, Obj);
    if (!sel || mp_session()) { if (n_active || g_shiftH > 0) hide_all(); return; }
    int player = icall_b(sel, M.sel_player), cnt = icall_i(sel, M.sel_count);
    if (!player || cnt < 2 || cnt > MAXB) { if (n_active || g_shiftH > 0) hide_all(); return; }
    Obj list = icall_o(sel, M.sel_all);
    Obj in[MAXB], units[MAXB]; int nin = 0;
    for (int i = 0; i < list_n(list) && nin < MAXB; i++) in[nin++] = list_at(list, i);
    int n = sort_units(p, in, nin, units);
    if (n < 1) { hide_all(); return; }
    show_multi(p, units, n);
}
static void hk_frame(Obj p, Obj data, const void *mi) {
    unswap(p);
    o_frame(p, data, mi);
    if (!n_active) return;
    if (mp_session()) { hide_all(); return; }
    for (int k = 0; k < n_active; k++) {
        Obj c = copy_k(k), u = unit_k(k);
        if (c && u) with_copy(p, c, u, o_frame, data, mi);
        fill_live(k);
    }
}
static void hk_useskill(Obj p, Obj data, const void *mi) {
    unswap(p);
    o_useskill(p, data, mi);
    if (!n_active) return;
    uint8_t on[MAXB][8]; memset(on, 0, sizeof on);
    Obj cm = AT(p, O.qsp_cm, Obj);
    if (cm && icall_i(cm, M.cm_mode) == E.cm_useskill) {
        Obj ctx = icall_o(cm, M.cm_ctx), usc = ctx ? icall_o(ctx, M.ctxc_usm) : NULL, infos = usc ? icall_o(usc, M.usm_infos) : NULL;
        Obj pud = AT(p, O.qsp_pud, Obj), qsv = pud ? icall_o(pud, M.pud_qsv) : NULL;
        void *gi = vm(qsv, M.qsv_index);
        for (int i = 0; i < list_n(infos); i++) {
            char *info = AT(infos, 0x10, char *) + 0x20 + 3 * sizeof(void *) * i;   /* UseSkillInfo {SkillUser, UseSkill, UseItem} */
            Obj user = AT(info, O.usi_user, Obj), skill = AT(info, O.usi_skill, Obj);
            for (int k = 0; k < n_active; k++) {
                if (unit_k(k) != user || !skill) continue;
                if (MC(M.askill_basic, uint8_t, Obj, void *)(skill, M.askill_basic) & 1) { on[k][0] = 1; continue; }
                if (!gi) continue;
                Obj idx = MC(gi, Obj, Obj, Guid, Obj, void *)(qsv, unit_guid(user), AT(skill, O.skill_key, Obj), gi);
                for (int j = 0; j < list_n(idx); j++) {
                    int ui = AT(AT(idx, 0x10, char *), 0x20 + 4 * j, int32_t) + 1;
                    if (ui >= 0 && ui < 8) on[k][ui] = 1;
                }
            }
        }
    }
    for (int k = 0; k < n_active; k++) {
        Obj c = copy_k(k); if (!c) continue;
        Obj slots = AT(c, O.qsui_slots, Obj);
        for (int i = 0; i < list_n(slots) && i < 8; i++) { Obj s = list_at(slots, i); if (s) comp_active(AT(s, O.slot_imgsel, Obj), on[k][i]); }
    }
}
static void refresh_all(Obj p) { for (int k = 0; k < n_active; k++) refresh_bar(p, k); }
static void hk_skillchg(Obj p, Obj d, const void *mi) { unswap(p); o_skillchg(p, d, mi); refresh_all(p); }
static void hk_behavchg(Obj p, Obj d, const void *mi) { unswap(p); o_behavchg(p, d, mi); refresh_all(p); refresh_opts(); }
static void hk_invchg(Obj p, Obj d, const void *mi) { unswap(p); o_invchg(p, d, mi); refresh_all(p); }

/* ======================================================================================== hooks: clicks on copies */
static void (*o_click)(Obj, const void *), (*o_hover)(Obj, uint8_t, const void *);
static void hk_click(Obj dc, const void *mi) {             /* QuickSlotUI.<>c__DisplayClass8_0.<Init>b__0 */
    int k = copy_index_of_view(AT(dc, O.dc8_this, Obj));
    if (k < 0) { o_click(dc, mi); return; }
    int saved = AT(dc, O.dc8_index, int32_t);
    AT(dc, O.dc8_index, int32_t) = (saved & 0xFF) | ((k + 1) << 8);   /* bar number rides in the slot index */
    o_click(dc, mi);
    AT(dc, O.dc8_index, int32_t) = saved;
}
static void hk_hover(Obj dc, uint8_t on, const void *mi) {  /* <Init>b__1: on -> UIInput hover, off */
    int k = copy_index_of_view(AT(dc, O.dc8_this, Obj));
    if (k < 0) { o_hover(dc, on, mi); return; }
    int saved = AT(dc, O.dc8_index, int32_t);
    AT(dc, O.dc8_index, int32_t) = (saved & 0xFF) | ((k + 1) << 8);
    o_hover(dc, on, mi);
    AT(dc, O.dc8_index, int32_t) = saved;
}
static void (*o_itemsel)(Obj, const void *);
static void hk_itemsel(Obj slot, const void *mi) {          /* ItemQuickSlotUI.HandleSelect: the item slot's left click */
    for (int k = 0; k < n_active; k++) {
        Obj c = copy_k(k);
        if (c && AT(c, O.qsui_item, Obj) == slot && unit_k(k)) { pending_item = unit_guid(unit_k(k)); pending_item_t = now(); break; }
    }
    o_itemsel(slot, mi);
}
static int slot_in_copy(Obj slot) {
    for (int k = 0; k < n_active; k++) {
        Obj c = copy_k(k); if (!c) continue;
        Obj l = AT(c, O.qsui_slots, Obj);
        for (int i = 0; i < list_n(l); i++) if (list_at(l, i) == slot) return 1;
    }
    return 0;
}
static void (*o_drop)(Obj, Obj, Obj, const void *);
static void hk_drop(Obj slot, Obj payload, Obj src, const void *mi) {
    /* on a bar copy, a skill may only land on a bar of the same unit */
    if (payload && slot_in_copy(slot) && AT(payload, O.drag_type, int32_t) == E.drag_skill && AT(payload, O.drag_owner, Obj) != AT(slot, O.slot_owner, Obj)) return;
    o_drop(slot, payload, src, mi);
}

/* ======================================================================================== hooks: handlers */
static Obj selected_unit_with_guid(Obj h, Guid g) {
    Obj sel = AT(h, O.pceh_sel, Obj); if (!sel) return NULL;
    Obj list = icall_o(sel, M.sel_all);
    for (int i = 0; i < list_n(list); i++) {
        Obj e = list_at(list, i);
        if (isa(e, K.unit)) { Guid ge = unit_guid(e); if (!memcmp(&ge, &g, sizeof g)) return e; }
    }
    return NULL;
}
static Obj inmap_of(Obj h) {
    Obj dict = AT(h, O.pceh_maps, Obj), im = NULL;
    if (dict) MC(M.dict_map_get, uint8_t, Obj, int32_t, Obj *, void *)(dict, AT(h, O.pceh_map, int32_t), &im, M.dict_map_get);   /* TryGetValue */
    return im;
}
static Obj skill_data(Obj key) { Obj cdm = MC(M.cdm_inst, Obj, void *)(M.cdm_inst); return cdm && key ? MC(M.cdm_skill, Obj, Obj, Obj, void *)(cdm, key, M.cdm_skill) : NULL; }
static Obj new_ctx(void) { Obj c = IL.object_new(K.usmc); if (c) MC(M.usmc_ctor, void, Obj, void *)(c, M.usmc_ctor); return c; }
static void cmp_mode(Obj cmp, int m) { MC(M.cmp_switch, void, Obj, int32_t, void *)(cmp, m, M.cmp_switch); }
static int check_use(Obj sk, Obj unit, Obj im) {
    Obj ec = MC(M.exec_convert, Obj, Obj, Obj, Obj, Obj, Obj, int32_t, void *)(sk, unit, NULL, NULL, NULL, -1, M.exec_convert);
    return MC(M.askill_check, int32_t, Obj, Obj, Obj, uint8_t, void *)(sk, im, ec, 0, M.askill_check);
}
/* PlayerControlEventHandler.OnEvent(UseQuickSlotSkillRequested) for ONE unit (v4 skilluse.c: same checks, context,
   aiming, failure notice). slot: 0 = basic attack, i > 0 = quick slot i-1 (stock rule; the weapon slot is i = 1 with
   the weapon-slot mod). */
static void use_skill_for_unit(Obj h, Obj unit, int slot) {
    Obj sel = AT(h, O.pceh_sel, Obj), cmp = AT(h, O.pceh_cmp, Obj), pud = AT(h, O.pceh_pud, Obj), disp = AT(h, O.pceh_disp, Obj);
    if (!sel || !cmp || !pud || !isa(unit, K.unit)) return;
    if (!icall_b(sel, M.sel_player)) return;
    /* click sound: the stock handler plays it only with one unit selected; a bar click is a one-unit action */
    Obj sm = MC(M.sound_inst, Obj, void *)(M.sound_inst);
    if (u_alive(sm)) MC(M.sound_play, void, Obj, int32_t, V2, void *)(sm, E.sfx_click, (V2){ 0, 0 }, M.sound_play);
    Obj im = inmap_of(h);
    if (!im) return;
    if (slot == 0) {                                            /* basic attack, this unit only */
        Obj rd = reader(unit, E.ct_skill); if (!rd) return;
        Obj sk = skill_data(MC(M.skill_basic, Obj, Obj, void *)(rd, M.skill_basic)); if (!sk) return;
        Obj ctx = new_ctx(); if (!ctx) return;
        if (check_use(sk, unit, im) == 1) MC(M.usmc_add, void, Obj, Obj, Obj, void *)(ctx, unit, sk, M.usmc_add);
        Obj lst = AT(ctx, O.usmc_infos, Obj);
        if (list_n(lst) >= 1) { cmp_mode(cmp, E.cm_useskill); MC(M.cmp_setctx, void, Obj, Obj, void *)(cmp, ctx, M.cmp_setctx); }
        else cmp_mode(cmp, E.cm_default);
        return;
    }
    Obj qv = icall_o(pud, M.pud_qsv); if (!qv) return;
    void *gs = vm(qv, M.qsv_skillat); if (!gs) return;
    Obj key = MC(gs, Obj, Obj, Guid, int32_t, void *)(qv, unit_guid(unit), slot - 1, gs);
    if (str_empty(key)) { cmp_mode(cmp, E.cm_default); return; }
    Obj sk = skill_data(key); if (!sk) return;
    int res = check_use(sk, unit, im);
    if (res == 1) {
        Obj ctx = new_ctx(); if (!ctx) return;
        MC(M.usmc_add, void, Obj, Obj, Obj, void *)(ctx, unit, sk, M.usmc_add);
        cmp_mode(cmp, E.cm_useskill);
        MC(M.cmp_setctx, void, Obj, Obj, void *)(cmp, ctx, M.cmp_setctx);
        if (MC(M.askill_target, int32_t, Obj, void *)(sk, M.askill_target) == E.tt_self) {   /* self target: cast at the mouse at once */
            V3 m = MC(M.input_mouse, V3, void *)(M.input_mouse);
            V2 w = MC(M.cam_s2w, V2, V2, void *)((V2){ m.x, m.y }, M.cam_s2w);
            IL.runtime_class_init(K.vecext);
            V2I pos = MC(M.vec_logic, V2I, V2, void *)(w, M.vec_logic);
            MC(M.cmp_left, void, Obj, V2I, uint8_t, void *)(cmp, pos, 0, M.cmp_left);
        }
        return;
    }
    cmp_mode(cmp, E.cm_default);                               /* failure: the game's red notice */
    int32_t rv = res;
    Obj boxed = IL.value_box(K.execres, &rv);
    void *ts = vm(boxed, M.obj_tostring);
    Obj txt = ts ? MC(ts, Obj, Obj, void *)(boxed, ts) : NULL;
    Obj vd = IL.object_new(K.vsd);
    if (!vd) return;
    MC(M.vsd_ctor, void, Obj, int32_t, Obj, void *)(vd, E.upd_notice, txt, M.vsd_ctor);
    void *d = vm(disp, M.disp_add);
    if (d) MC(d, void, Obj, Obj, void *)(disp, vd, d);
}
static fn_pd o_useqs, o_tbo;
static void hk_useqs(Obj h, Obj msg, const void *mi) {   /* PlayerControlEventHandler.OnEvent(UseQuickSlotSkillRequested) */
    int idx = msg ? AT(msg, O.useqs_index, int32_t) : 0;
    if (idx >> 8) {
        int k = (idx >> 8) - 1, slot = idx & 0xFF;
        Obj c = (k >= 0 && k < MAXB) ? copy_k(k) : NULL;
        Obj u = c ? selected_unit_with_guid(h, guid_at(c, O.qsui_selected)) : NULL;
        H->log(ME, "multi-bar: card %d slot %d: %s", k, slot, u ? "used for its unit" : "unit no longer selected");
        if (u && !mp_session()) use_skill_for_unit(h, u, slot);
        return;
    }
    if (n_active && idx > 0) return;                           /* owner: skill keys off with 2+ selected */
    o_useqs(h, msg, mi);
}
static uint8_t (*o_single)(Obj, Obj *, const void *);
static uint8_t hk_single(Obj h, Obj *out, const void *mi) { /* TryResolveSingleSelectedUnit: the item slot of card k */
    if (pending_item_t > 0 && now() - pending_item_t < 2.0) {
        pending_item_t = 0;
        Obj u = selected_unit_with_guid(h, pending_item);
        if (u) { if (IL.wbarrier) IL.wbarrier(NULL, (void **)out, u); else *out = u; return 1; }
    }
    return o_single(h, out, mi);
}
static void (*o_optclick)(Obj, uint8_t, const void *);
static void hk_optclick(Obj dc, uint8_t isOn, const void *mi) {   /* EntitySummaryOptionUI.<>c__DisplayClass13_0.<Init>b__0 */
    Obj ou = AT(dc, O.dc13_this, Obj);
    int k = opt_index(ou);
    if (k < 0) { o_optclick(dc, isOn, mi); return; }
    int idx = AT(dc, O.dc13_index, int32_t);
    Obj poss = AT(ou, O.opt_poss, Obj);
    if (!poss || idx < 0 || idx >= AT(poss, 0x18, int32_t)) return;
    int opt = AT(poss, 0x20 + 4 * idx, int32_t);
    Obj a = AT(ou, O.opt_toggle, Obj);
    if (opt > 7 || !a) return;
    MC(M.act_toggle_invoke, void, Obj, uint8_t, int32_t, void *)(a, isOn, opt | ((k + 1) << 8), M.act_toggle_invoke);
}
static void hk_tbo(Obj h, Obj msg, const void *mi) {     /* PlayerControlEventHandler.OnEvent(ToggleBehaviourOption) */
    if (msg) {
        int o = AT(msg, O.tbo_option, int32_t);
        if (o >> 8) {
            int k = (o >> 8) - 1;
            AT(msg, O.tbo_option, int32_t) = o & 0xFF;
            Obj u = (k >= 0 && k < MAXB) ? unit_k(k) : NULL;
            if (u) u = selected_unit_with_guid(h, unit_guid(u));
            if (!u || AT(msg, O.tbo_ison, uint8_t) != 1 || mp_session()) { refresh_opts(); return; }   /* IsOn.hasValue */
            wset(msg, O.tbo_target, u);                          /* Target = unit k: the handler's one-unit branch */
        }
    }
    o_tbo(h, msg, mi);
}
static void hk_swap(Obj pres, Obj data, const void *mi) {  /* PlayerUnitShortCutPresenter.SwapShortCut: party order changed */
    o_swap(pres, data, mi);
    Obj p = gc_get(h_presenter);
    if (!p || n_active < 2) return;
    Obj in[MAXB], units[MAXB]; int nin = 0;
    for (int k = 0; k < n_active; k++) { Obj u = unit_k(k); if (u) in[nin++] = u; }
    int n = sort_units(p, in, nin, units);
    if (n >= 1) show_multi(p, units, n);
}
/* AoE hover preview of card k's skill (v4 spec/preview.c.txt) */
static void hk_civp(Obj p, Obj data, const void *mi) {    /* CombatInfoViewPresenter.RefreshOnFrame */
    int t = AT(p, O.civp_temp, int32_t);
    if (t == -1 || !(t >> 8)) { o_civp(p, data, mi); return; }
    int k = (t >> 8) - 1, slot = t & 0xFF;
    Obj cm = AT(p, O.civp_cm, Obj), sel = AT(p, O.civp_sel, Obj);
    int aiming = cm && icall_i(cm, M.cm_mode) == E.cm_useskill;
    Obj u = NULL;                                              /* card k alive, its unit still selected, not dead, slot 0..5 */
    if (!aiming && n_active && k >= 0 && k < n_active && copy_k(k) && slot <= 5) {
        Obj uk = unit_k(k);
        if (uk && !unit_dead(uk)) {
            Obj list = sel ? icall_o(sel, M.sel_all) : NULL;
            for (int i = 0; i < list_n(list); i++) if (list_at(list, i) == uk) { u = uk; break; }
        }
    }
    if (!u) {                                                  /* aiming, or card gone: behave as "mouse left" */
        AT(p, O.civp_temp, int32_t) = -1;
        o_civp(p, data, mi);
        return;
    }
    if (!(MC(M.civp_has, uint8_t, Obj, Obj, int32_t, void *)(p, u, slot, M.civp_has) & 1)) {   /* stock one-unit rule */
        if (AT(p, O.civp_showing, uint8_t)) { AT(p, O.civp_showing, uint8_t) = 0; MC(M.civp_hide, void, Obj, void *)(p, M.civp_hide); }
        return;
    }
    Obj key;
    if (slot == 0) { Obj sr = reader(u, E.ct_skill); if (!sr) return; key = MC(M.skill_basic, Obj, Obj, void *)(sr, M.skill_basic); }
    else {
        Obj pud = AT(p, O.civp_pud, Obj), qsv = pud ? icall_o(pud, M.pud_qsv) : NULL;
        void *g = vm(qsv, M.qsv_skillat); if (!g) return;
        key = MC(g, Obj, Obj, Guid, int32_t, void *)(qsv, unit_guid(u), slot - 1, g);
    }
    if (str_empty(key)) return;
    Obj skill = skill_data(key); if (!skill) return;
    AT(p, O.civp_showing, uint8_t) = 1;                        /* stock RefreshOnFrame hides it once the index goes back to -1 */
    MC(M.civp_show, void, Obj, Obj, Obj, void *)(p, u, skill, M.civp_show);
}

/* ======================================================================================== start */
#define IFN(f, name) (*(void **)&IL.f = H->il2cpp_fn(name))
static int resolve(void) {
    IFN(class_get_methods, "il2cpp_class_get_methods"); IFN(method_get_name, "il2cpp_method_get_name");
    IFN(method_get_param_count, "il2cpp_method_get_param_count"); IFN(method_get_param, "il2cpp_method_get_param");
    IFN(method_is_generic, "il2cpp_method_is_generic"); IFN(class_get_parent, "il2cpp_class_get_parent");
    IFN(class_get_nested_types, "il2cpp_class_get_nested_types"); IFN(class_get_name, "il2cpp_class_get_name");
    IFN(class_from_type, "il2cpp_class_from_type"); IFN(class_get_type, "il2cpp_class_get_type"); IFN(type_get_object, "il2cpp_type_get_object");
    IFN(class_get_field_from_name, "il2cpp_class_get_field_from_name"); IFN(field_get_offset, "il2cpp_field_get_offset");
    IFN(field_get_type, "il2cpp_field_get_type"); IFN(field_static_get_value, "il2cpp_field_static_get_value");
    IFN(object_get_class, "il2cpp_object_get_class"); IFN(class_is_assignable_from, "il2cpp_class_is_assignable_from");
    IFN(object_get_virtual_method, "il2cpp_object_get_virtual_method"); IFN(object_new, "il2cpp_object_new");
    IFN(array_new, "il2cpp_array_new"); IFN(string_new, "il2cpp_string_new"); IFN(value_box, "il2cpp_value_box");
    IFN(gchandle_new, "il2cpp_gchandle_new"); IFN(gchandle_get_target, "il2cpp_gchandle_get_target"); IFN(gchandle_free, "il2cpp_gchandle_free");
    IFN(wbarrier, "il2cpp_gc_wbarrier_set_field"); IFN(runtime_class_init, "il2cpp_runtime_class_init");
    void **f = (void **)&IL;
    for (size_t i = 0; i < sizeof IL / sizeof(void *); i++) if (!f[i]) { H->log(ME, "multi-bar: il2cpp export %zu missing", i); return -1; }

    /* ---- Unity */
    K.object = kls("UnityEngine", "Object"); K.transform = kls("UnityEngine", "Transform");
    void *kcomp = kls("UnityEngine", "Component"), *kgo = kls("UnityEngine", "GameObject"), *krt = kls("UnityEngine", "RectTransform");
    void *kgraphic = kls("UnityEngine.UI", "Graphic"), *klg = kls("UnityEngine.UI", "LayoutGroup"), *kinput = kls("UnityEngine", "Input");
    K.image = kls("UnityEngine.UI", "Image"); K.hlg = kls("UnityEngine.UI", "HorizontalLayoutGroup");
    M.obj_implicit = msig(K.object, "op_Implicit", 1, "Object"); M.obj_inst = msig(K.object, "Instantiate", 2, "Object,Transform");
    M.obj_find = msig(K.object, "FindAnyObjectByType", 2, "Type,FindObjectsInactive"); M.obj_set_name = msig(K.object, "set_name", 1, NULL);
    M.obj_tostring = msig(kls("System", "Object"), "ToString", 0, NULL); M.str_empty = msig(kls("System", "String"), "IsNullOrEmpty", 1, NULL);
    M.comp_tr = msig(kcomp, "get_transform", 0, NULL); M.comp_go = msig(kcomp, "get_gameObject", 0, NULL); M.comp_get = msig(kcomp, "GetComponent", 1, "Type");
    M.go_tr = msig(kgo, "get_transform", 0, NULL); M.go_active = msig(kgo, "SetActive", 1, NULL);
    M.tr_parent = msig(K.transform, "get_parent", 0, NULL); M.tr_setparent = msig(K.transform, "SetParent", 2, "Transform,Boolean");
    M.tr_find = msig(K.transform, "Find", 1, "String"); M.tr_scale = msig(K.transform, "set_localScale", 1, NULL);
    M.tr_gsib = msig(K.transform, "GetSiblingIndex", 0, NULL); M.tr_ssib = msig(K.transform, "SetSiblingIndex", 1, NULL);
    M.rt_amin = msig(krt, "set_anchorMin", 1, NULL); M.rt_amax = msig(krt, "set_anchorMax", 1, NULL); M.rt_pivot = msig(krt, "set_pivot", 1, NULL);
    M.rt_pos_get = msig(krt, "get_anchoredPosition", 0, NULL); M.rt_pos = msig(krt, "set_anchoredPosition", 1, NULL);
    M.rt_size_get = msig(krt, "get_sizeDelta", 0, NULL); M.rt_size = msig(krt, "set_sizeDelta", 1, NULL); M.rt_rect = msig(krt, "get_rect", 0, NULL);
    M.img_sprite = msig(K.image, "set_sprite", 1, NULL); M.img_fill = msig(K.image, "set_fillAmount", 1, NULL);
    M.g_color = msig(kgraphic, "set_color", 1, NULL); M.g_raycast = msig(kgraphic, "set_raycastTarget", 1, NULL);
    M.lg_align = msig(klg, "set_childAlignment", 1, NULL); M.input_mouse = msig(kinput, "get_mousePosition", 0, NULL);
    E.find_inactive_incl = enum_v(kls("UnityEngine", "FindObjectsInactive"), "Include", 1);
    void *kanchor = kls("UnityEngine", "TextAnchor");
    E.ta_lowerleft = enum_v(kanchor, "LowerLeft", 6); E.ta_lowerright = enum_v(kanchor, "LowerRight", 8);

    /* ---- the skill bar */
    K.qsp = kls("Refactor.UI", "QuickSlotUIPresenter"); K.qsui = kls("Refactor.UI", "QuickSlotUI"); K.dc8 = nested(K.qsui, "<>c__DisplayClass8_0");
    K.slot = kls("Refactor.UI", "SkillSlotUI"); K.itemslot = kls("Refactor.UI", "ItemQuickSlotUI"); K.uibase = kls("Refactor.UI", "UI_Base");
    K.dastext = kls("Refactor.UI", "DASText");
    O.qsp_view = fo(K.qsp, "<View>k__BackingField"); O.qsp_sel = fo(K.qsp, "_selectedReader"); O.qsp_cm = fo(K.qsp, "_controlModeReader");
    O.qsp_inmap = fo(K.qsp, "_inMapData"); O.qsp_pud = fo(K.qsp, "_playerUnitData"); O.qsp_showing = fo(K.qsp, "_showingEntity");
    O.qsui_slots = fo(K.qsui, "_skillSlots"); O.qsui_ammo = fo(K.qsui, "_ammoSlots"); O.qsui_item = fo(K.qsui, "_itemQuickSlot");
    O.qsui_selected = fo(K.qsui, "_selected"); O.qsui_ammoget = fo(K.qsui, "_ammoSlotGetter"); O.uibase_input = fo(K.uibase, "_inputStream");
    O.item_keytext = fo(K.itemslot, "_inputKeyText"); O.slot_imgsel = fo(K.slot, "_imgSelected"); O.slot_keytext = fo(K.slot, "_txtInputKey");
    O.slot_owner = fo(K.slot, "_owner"); O.dc8_index = fo(K.dc8, "index"); O.dc8_this = fo(K.dc8, "<>4__this");
    M.qsp_refresh_skill = msig(K.qsp, "RefreshSkillUI", 0, NULL); M.qsp_refresh_item = msig(K.qsp, "RefreshItemQuickSlotUI", 0, NULL);
    M.qsui_init = msig(K.qsui, "Init", 3, NULL); M.qsui_getdown = msig(K.qsui, "InitGetDownButton", 1, NULL);
    M.uibase_init = msig(K.uibase, "InitOnCreateUI", 1, NULL); M.das_settext = msig(K.dastext, "SetText", 1, "String");
    K.dict_is = M.qsui_init ? class_of_type(IL.method_get_param(M.qsui_init, 1)) : NULL;   /* Dictionary<int, string> */
    M.dict_is_ctor = msig(K.dict_is, ".ctor", 0, NULL); M.dict_is_add = msig(K.dict_is, "Add", 2, NULL);

    /* ---- units, readers */
    K.unit = kls("Refactor", "UnitEntity"); O.unit_guid = fo(K.unit, "<Guid>k__BackingField");
    M.unit_isdead = msig(K.unit, "get_IsDead", 0, NULL); M.unit_portrait = msig(K.unit, "get_PortraitID", 0, NULL);
    M.name_get = msig(kls("Refactor", "EntityDisplayNameExtensions"), "GetDisplayName", 1, "UnitEntity");
    K.entcomp = kls("Refactor.Main", "EntityComponent"); K.complist = kls("Refactor.Main", "ComponentList");
    M.ent_instance = K.entcomp ? IL.class_get_field_from_name(K.entcomp, "_instance") : NULL; if (!M.ent_instance) miss("field", "EntityComponent", "_instance");
    O.ent_components = fo(K.entcomp, "_components");
    M.dict_ent_tryget = msig(ftype_class(K.entcomp, "_components"), "TryGetValue", 2, NULL);
    M.complist_tryreader = msig(K.complist, "TryGetReader", 2, NULL);
    void *kct = kls("Refactor.Component", "ComponentType");
    E.ct_status = enum_v(kct, "Status", 0); E.ct_info = enum_v(kct, "EntityInfo", 1); E.ct_affecter = enum_v(kct, "Affecter", 3); E.ct_skill = enum_v(kct, "Skill", 8);
    M.status_get = msig(kls("Refactor", "StatusReader"), "GetValue", 1, NULL);
    M.aff_hascat = msig(kls("Refactor", "AffecterReader"), "HasCategory", 1, NULL);
    M.info_bo = msig(kls("Refactor", "EntityInfoReader"), "GetBehaviourOption", 0, NULL);
    M.skill_basic = msig(kls("Refactor", "SkillReader"), "GetBasicAttackSkill", 0, NULL);
    void *kst = kls("Refactor", "StatType");
    E.st_hh = enum_v(kst, "HealthHead", 13); E.st_hb = enum_v(kst, "HealthBody", 14); E.st_en = enum_v(kst, "Energy", 15);
    E.st_hu = enum_v(kst, "Hunger", 16); E.st_str = enum_v(kst, "Stress", 17); E.st_mhh = enum_v(kst, "MaxHealthHead", 7);
    E.st_mhb = enum_v(kst, "MaxHealthBody", 8); E.st_men = enum_v(kst, "MaxEnergy", 41); E.st_mhu = enum_v(kst, "MaxHunger", 42);
    E.st_mstr = enum_v(kst, "MaxStress", 43); E.st_mood = enum_v(kst, "Mood", 40);
    E.aff_mentalbreak = enum_v(kls("Refactor", "AffecterCategory"), "MentalBreak", 902);

    /* ---- card: summary view, party portrait parts, status, affecters, portrait */
    K.esv = kls("Refactor.UI", "EntitySummaryUIView"); K.esus = kls("Refactor.UI", "EntitySummaryUnitStatusUI");
    K.parts = kls("Refactor.UI", "SubUI_UnitShortCutParts"); K.affui = kls("Refactor.UI", "EntitySummaryAffecterUI");
    O.esv_portrait = fo(K.esv, "_imgPortrait"); O.esv_name = fo(K.esv, "_txtName"); O.esv_status = fo(K.esv, "_playerStatusUI");
    O.esv_opt = fo(K.esv, "_optionUI"); O.esv_aff = fo(K.esv, "_affecetUI"); O.esv_tabs = fo(K.esv, "_toggleButtons");
    O.esus_head = fo(K.esus, "_imgTotalHeadHealthHeart"); O.esus_body = fo(K.esus, "_imgTotalBodyHealthHeart");
    O.parts_energy = fo(K.parts, "_energyBar"); O.parts_moods = fo(K.parts, "_unitMoodIconList");
    M.affui_set = msig(K.affui, "SetUI", 1, NULL); M.affui_off = msig(K.affui, "OffUI", 0, NULL); M.affui_frame = msig(K.affui, "RefreshOnFrame", 1, NULL);
    M.mood_list = msig(kls("Refactor.Component.Status", "MoodAffecterResolver"), "GetActiveExposedList", 1, NULL);
    K.statutil = kls("Refactor.Component.Status", "StatUtility"); M.mood_type = msig(K.statutil, "GetMoodType", 1, NULL);
    K.sysui = nested(kls("Refactor", "SystemVar"), "UI"); M.moodmax = msig(K.sysui, "get_MoodMax", 0, NULL);
    void *kscale = kls("Refactor", "UIScaleManager");
    M.uiscale_inst = msig(kscale, "get_Instance", 0, NULL); M.uiscale_get = msig(kscale, "get_Scale", 0, NULL);
    void *kcp = kls("Refactor", "ColorPreset");
    M.color_preset = msig(kls("Refactor", "ColorExtensions"), "ToColor", 1, "ColorPreset");
    E.cp_head = enum_v(kcp, "Health_AllyHead", 5); E.cp_body = enum_v(kcp, "Health_AllyBody", 6);
    void *kuvdm = kls("Refactor", "UnitVisualDataManager");
    M.uvdm_inst = msig(kuvdm ? IL.class_get_parent(kuvdm) : NULL, "get_Instance", 0, NULL);   /* MonoSingleton<UnitVisualDataManager> */
    M.uvdm_portrait = msig(kuvdm, "GetPortraitSprite", 2, NULL);

    /* ---- F / G / H */
    K.opt = kls("Refactor.UI", "EntitySummaryOptionUI"); K.dc13 = nested(K.opt, "<>c__DisplayClass13_0");
    O.opt_btns = fo(K.opt, "_btns"); O.opt_toggle = fo(K.opt, "_onToggle"); O.opt_task = fo(K.opt, "_onTaskToggle");
    O.opt_poss = fo(K.opt, "_possibleOptions"); O.opt_keys = fo(K.opt, "_optionKeys");
    O.dc13_index = fo(K.dc13, "index"); O.dc13_this = fo(K.dc13, "<>4__this");
    M.opt_init = msig(K.opt, "Init", 3, NULL); M.opt_toggles = msig(K.opt, "InitToggles", 1, NULL); M.opt_set = msig(K.opt, "SetToggle", 1, NULL);
    M.dict_bo_get = msig(ftype_class(K.opt, "_optionKeys"), "TryGetValue", 2, NULL);
    M.act_toggle_invoke = msig(ftype_class(K.opt, "_onToggle"), "Invoke", 2, NULL);
    K.bo_array_elem = kls("Refactor", "BehaviourOption");
    E.bo_auto = enum_v(K.bo_array_elem, "AutoAttack", 1); E.bo_wait = enum_v(K.bo_array_elem, "WaitForOrder", 2); E.bo_rest = enum_v(K.bo_array_elem, "RestFirst", 4);

    /* ---- the handler side */
    K.pceh = kls("Refactor.Main.Event", "PlayerControlEventHandler"); K.ev_useqs = kls("Refactor.Main.Event", "UseQuickSlotSkillRequested");
    K.ev_tbo = kls("Refactor.Main.Event", "ToggleBehaviourOption"); K.drag = kls("Refactor.Main.InputModule", "DragData");
    O.pceh_cmp = fo(K.pceh, "_modeProcessor"); O.pceh_sel = fo(K.pceh, "_selectReader"); O.pceh_pud = fo(K.pceh, "playerUnitData");
    O.pceh_map = fo(K.pceh, "_currentMap"); O.pceh_maps = fo(K.pceh, "_inMapDatas"); O.pceh_disp = fo(K.pceh, "_viewDispatcher");
    O.useqs_index = fo(K.ev_useqs, "Index"); O.tbo_target = fo(K.ev_tbo, "Target"); O.tbo_option = fo(K.ev_tbo, "Option"); O.tbo_ison = fo(K.ev_tbo, "IsOn");
    O.drag_type = fo(K.drag, "Type"); O.drag_owner = fo(K.drag, "Owner");
    E.drag_skill = enum_v(kls("Refactor.Main.InputModule", "DragType"), "skill", 2);
    M.dict_map_get = msig(ftype_class(K.pceh, "_inMapDatas"), "TryGetValue", 2, NULL);
    K.cdm = kls("Refactor", "CombatDataManager"); M.cdm_inst = msig(K.cdm, "get_Instance", 0, NULL); M.cdm_skill = msig(K.cdm, "GetActiveSkillData", 1, NULL);
    K.skillbase = kls("Refactor", "Skill_Base"); O.skill_key = fo(K.skillbase, "<Key>k__BackingField");
    K.askill = kls("Refactor.Combat", "ActiveSkill_Base");
    M.askill_basic = msig(K.askill, "get_IsBasicAttackSkill", 0, NULL); M.askill_target = msig(K.askill, "get_TargetType", 0, NULL);
    M.askill_check = msig(K.askill, "CheckForUseSkill", 3, NULL);
    M.exec_convert = msig(kls("Refactor.Combat", "ExecutionContext"), "Convert", 6, "IExecutionPublisher,IEntity,IEntity,ItemEntity,IEntity,Int32");
    K.execres = kls("Refactor.Combat", "ExecutionCheckResult");
    E.tt_self = enum_v(kls("Refactor.Combat", "TargetType"), "SelfTarget", 3);
    K.usmc = kls("Refactor.Main.Processor", "UseSkillModeContext"); O.usmc_infos = fo(K.usmc, "_useSkillInfos");
    M.usmc_ctor = msig(K.usmc, ".ctor", 0, NULL); M.usmc_add = msig(K.usmc, "AddUseSkill", 2, NULL);
    K.cmp = kls("Refactor.Main.Processor", "ControlModeProcessor");
    M.cmp_switch = msig(K.cmp, "SwitchMode", 1, NULL); M.cmp_setctx = msig(K.cmp, "SetContext", 1, NULL); M.cmp_left = msig(K.cmp, "LeftClick", 2, NULL);
    void *kmode = kls("Refactor", "ControlMode");
    E.cm_default = enum_v(kmode, "Default", 0); E.cm_useskill = enum_v(kmode, "UseSkill", 6);
    M.cam_s2w = msig(kls("Refactor", "MainCamera"), "ScreenToWorldPoint", 1, "Vector2");
    K.vecext = kls("Refactor", "VectorExtensions"); M.vec_logic = msig(K.vecext, "ToLogicalPosition", 1, "Vector2");
    void *ksound = kls("Refactor.Sound", "SoundManager");
    M.sound_inst = msig(ksound ? IL.class_get_parent(ksound) : NULL, "get_Instance", 0, NULL);   /* MonoSingleton<SoundManager> */
    M.sound_play = msig(ksound, "PlaySFXOneTime", 2, NULL);
    E.sfx_click = enum_v(kls("Refactor.Sound", "SFXEnum"), "UI_Button_Click_Small", 237);
    K.vsd = kls("Refactor.View", "ViewStringData"); M.vsd_ctor = msig(K.vsd, ".ctor", 2, NULL);
    E.upd_notice = enum_v(kls("Refactor.View", "UpdateType"), "ShowExecutionResultNotice", 1140);
    void *kusi = kls("Refactor", "UseSkillInfo");
    O.usi_user = fo(kusi, "SkillUser") - 0x10; O.usi_skill = fo(kusi, "UseSkill") - 0x10;   /* value type: offsets count the box header */
    if (O.usi_user != 0 || O.usi_skill != (int32_t)sizeof(void *)) { H->log(ME, "multi-bar: UseSkillInfo layout changed (%d, %d)", O.usi_user, O.usi_skill); n_missing++; }

    /* ---- interfaces */
    K.i_sel = kls("Refactor", "ISelectReader"); K.i_cm = kls("Refactor", "IControlModeReader"); K.i_ctxc = kls("Refactor", "IControlModeContextContainer");
    K.i_usm = kls("Refactor", "IUseSkillModeContext"); K.i_pud = kls("Refactor.Main", "IPlayerUnitData"); K.i_qsv = kls("Refactor.Main", "IQuickSlotViewer");
    K.i_disp = kls("Refactor.View", "IViewDispatcher");
    M.sel_count = imeth(K.i_sel, "GetSelectedCount", 0); M.sel_all = imeth(K.i_sel, "GetAllSelected", 0);
    M.sel_player = imeth(K.i_sel, "get_IsPlayerUnitSelecting", 0);
    M.cm_mode = imeth(K.i_cm, "get_CurrentMode", 0); M.cm_ctx = imeth(K.i_cm, "get_Context", 0);
    M.ctxc_usm = imeth(K.i_ctxc, "GetUseSkillModeContext", 0); M.usm_infos = imeth(K.i_usm, "GetUseSkillInfos", 0);
    M.pud_qsv = imeth(K.i_pud, "get_QuickSlotViewer", 0); M.qsv_skillat = imeth(K.i_qsv, "GetSkillAt", 2);
    M.qsv_index = imeth(K.i_qsv, "GetIndexOfSkill", 2); M.disp_add = imeth(K.i_disp, "AddViewUpdateCall", 1);

    /* ---- party order, inputs */
    K.inmap = kls("Refactor", "InMapDataInterface"); K.rmd = kls("Refactor.Runtime", "RuntimeMapData"); K.pdh = kls("Refactor.Runtime", "PlayerDataHandler");
    O.inmap_rt = fo(K.inmap, "_runtimeData"); O.rmd_player = fo(K.rmd, "_player"); O.pdh_units = fo(K.pdh, "_totalPlayerUnits");
    K.civp = kls("Refactor", "CombatInfoViewPresenter");
    O.civp_sel = fo(K.civp, "_selectedReader"); O.civp_cm = fo(K.civp, "_modeReader"); O.civp_pud = fo(K.civp, "playerUnitData");
    O.civp_showing = fo(K.civp, "_isShowingIndicator"); O.civp_temp = fo(K.civp, "_tempShowingSkillIndex");
    M.dc8_b0 = msig(K.dc8, "<Init>b__0", 0, NULL); M.dc8_b1 = msig(K.dc8, "<Init>b__1", 1, NULL);
    M.dc13_b0 = msig(K.dc13, "<Init>b__0", 1, NULL);
    M.pceh_useqs = msig(K.pceh, "OnEvent", 1, "UseQuickSlotSkillRequested"); M.pceh_tbo = msig(K.pceh, "OnEvent", 1, "ToggleBehaviourOption");
    M.civp_has = msig(K.civp, "HasSkill", 2, NULL); M.civp_show = msig(K.civp, "ShowIndicator", 2, NULL); M.civp_hide = msig(K.civp, "HideIndicator", 0, NULL);
    if (n_missing) return -1;

    /* ---- optional: the mood tooltip of the card (the party card's own tooltip provider) */
    int before = n_missing;
    K.tooltip = kls("Refactor.UI.Util", "TooltipFeedback"); O.tip_type = fo(K.tooltip, "_tooltipType");
    M.tip_key = msig(K.tooltip, "SetTooltipKey", 1, NULL); M.tip_mood = msig(K.tooltip, "SetMoodTooltipProvider", 1, NULL);
    K.func_mood = M.tip_mood ? class_of_type(IL.method_get_param(M.tip_mood, 0)) : NULL;
    M.func_mood_ctor = msig(K.func_mood, ".ctor", 2, NULL);
    K.dc43 = nested(K.parts, "<>c__DisplayClass43_0"); O.dc43_unit = fo(K.dc43, "unitEntity");
    void *b0 = K.dc43 ? msig_q(K.dc43, "<SetMoodTooltip>b__0", 0, NULL) : NULL; M.dc43_b0 = b0; if (!b0) miss("method", "<>c__DisplayClass43_0", "<SetMoodTooltip>b__0");
    E.tip_mood = enum_v(kls("Refactor.UI", "TooltipType"), "Mood", 14);
    has_mood_tip = n_missing == before;
    if (!has_mood_tip) H->log(ME, "multi-bar: the cards' mood icon has no tooltip on this game build");
    n_missing = before;
    return 0;
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    if (resolve()) { H->log(ME, "multi-bar: %d game names not found on this build: the module stays off (stock skill bar)", n_missing); return 1; }
    if (H->module_loaded && H->module_loaded("multiplayer")) {
#ifdef _WIN32
        void *img = (void *)GetModuleHandleA("multiplayer.dll");
        void *get = img ? (void *)GetProcAddress((HMODULE)img, "mp_api_get") : NULL;
#else
        void *img = dlopen("@rpath/multiplayer.dylib", RTLD_NOLOAD | RTLD_LOCAL);
        void *get = img ? dlsym(img, "mp_api_get") : NULL;
#endif
        mp = get ? ((const mp_api_head *(*)(uint32_t))get)(1) : NULL;
        H->log(ME, "multi-bar: multiplayer %s: the view stays stock inside a multiplayer session", mp ? "found" : "API not found");
    }
    int bad = 0;
    bad |= H->hook_method(ME, "Refactor.UI", "QuickSlotUIPresenter", "OnInitializeView", 0, (void *)hk_init, (void **)&o_init);
    bad |= H->hook_method(ME, "Refactor.UI", "QuickSlotUIPresenter", "RefreshOnSelect", 1, (void *)hk_select, (void **)&o_select);
    bad |= H->hook_method(ME, "Refactor.UI", "QuickSlotUIPresenter", "OnFrame", 1, (void *)hk_frame, (void **)&o_frame);
    bad |= H->hook_method(ME, "Refactor.UI", "QuickSlotUIPresenter", "OnUseSkill", 1, (void *)hk_useskill, (void **)&o_useskill);
    bad |= H->hook_method(ME, "Refactor.UI", "QuickSlotUIPresenter", "RefreshOnSkillChanged", 1, (void *)hk_skillchg, (void **)&o_skillchg);
    bad |= H->hook_method(ME, "Refactor.UI", "QuickSlotUIPresenter", "RefreshOnBehaviorChanged", 1, (void *)hk_behavchg, (void **)&o_behavchg);
    bad |= H->hook_method(ME, "Refactor.UI", "QuickSlotUIPresenter", "RefreshOnInventoryChanged", 1, (void *)hk_invchg, (void **)&o_invchg);
    bad |= H->hook(ME, *(void **)M.dc8_b0, (void *)hk_click, (void **)&o_click);
    bad |= H->hook(ME, *(void **)M.dc8_b1, (void *)hk_hover, (void **)&o_hover);
    bad |= H->hook_method(ME, "Refactor.UI", "ItemQuickSlotUI", "HandleSelect", 0, (void *)hk_itemsel, (void **)&o_itemsel);
    bad |= H->hook_method(ME, "Refactor.UI", "SkillSlotUI", "OnDrop", 2, (void *)hk_drop, (void **)&o_drop);
    bad |= H->hook(ME, *(void **)M.pceh_useqs, (void *)hk_useqs, (void **)&o_useqs);
    bad |= H->hook(ME, *(void **)M.pceh_tbo, (void *)hk_tbo, (void **)&o_tbo);
    bad |= H->hook_method(ME, "Refactor.Main.Event", "PlayerControlEventHandler", "TryResolveSingleSelectedUnit", 1, (void *)hk_single, (void **)&o_single);
    bad |= H->hook(ME, *(void **)M.dc13_b0, (void *)hk_optclick, (void **)&o_optclick);
    bad |= H->hook_method(ME, "Refactor.UI", "PlayerUnitShortCutPresenter", "SwapShortCut", 1, (void *)hk_swap, (void **)&o_swap);
    bad |= H->hook_method(ME, "Refactor", "CombatInfoViewPresenter", "RefreshOnFrame", 1, (void *)hk_civp, (void **)&o_civp);
    if (bad || n_missing) { H->log(ME, "multi-bar: a hook was refused: the module stays off (stock skill bar)"); return 2; }
    H->log(ME, "multi-bar: ready (17 hooks, mood tooltip %s)", has_mood_tip ? "on" : "off");
    return 0;
}
