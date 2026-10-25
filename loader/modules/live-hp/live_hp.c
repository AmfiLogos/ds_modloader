// Live building HP (mod id live-hp): while one building is selected, its durability (HP) number and bar in the bottom
// panel, and its rows in the Inspect window, follow the building's real durability. A dsload code module (runtime hooks,
// GameAssembly file unchanged). Client only: it reads two stats and redraws stock UI; nothing of the game state changes.
// Every game call by il2cpp name (Mac and Windows); build 25846389 (DS_B.0.4.29).
//
// The stock bug (decomp 25846389): EntitySummaryUIPresenter.RefreshSummary fills a building's panel when the selection
// changes (InitBuildingUI or InitFarmUI, then EntitySummaryUIView.UpdateBuildingEntityUI(StatusReader) ->
// EntitySummaryBuildingStatusUI.SetDurabilityUI: the bar and the two texts). Nothing sets them again: the status event
// (UpdateOnStatusChanged) refreshes units only, and the panel's per-frame RefreshOnFrame refreshes a building's work
// text (RefreshProgress) but not its durability. The Inspect window (EntityInspectionPresenter.RefreshBuilding ->
// SubUI_EntityInspectionStatus.SetBuildingStatus) is filled when it opens or the selection changes, never after. So a
// building that takes damage or gets repaired keeps showing the HP it had when the player clicked it.
// The fix: after the presenter's RefreshOnFrame (the panel's own per-frame refresh; this module's only hook), for one
// selected building or farm whose panel shows its durability, read Durability / MaxDurability from the entity's own
// StatusReader (the one GetComponent<StatusReader> gives: EntityComponent._components -> ComponentList.TryGetReader)
// and, when they differ from what was drawn, call the game's own fills again: UpdateBuildingEntityUI(reader), and
// SetBuildingStatus(reader, quality) while the Inspect window shows (quality as RefreshBuilding works it out). A mod
// that hooks one of those two fills (Units Laboratory's DPS Check: number format and fit) runs on every live change.
#include "dsl_sdk.h"
#include <stdint.h>
#include <string.h>

DSL_PLUGIN();

static const DSLHost *H;
static DSLPlugin *ME;
typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define AHDR 0x20                                       /* il2cpp array: klass, monitor, bounds, max_length, items */
#define SETTLE 10                                       /* frames after a new selection / window: one more fill */
#define REREAD 120                                      /* frames: the entity's reader is looked up again */

static struct {
    void *(*object_get_class)(Obj);
    void *(*class_get_parent)(void *);
    void *(*class_get_field_from_name)(void *, const char *);
    size_t (*field_get_offset)(void *);
    void (*field_static_get_value)(void *, void *);
    void *(*class_get_method_from_name)(void *, const char *, int);
    uint8_t (*class_is_assignable_from)(void *, void *); /* C bool: low byte only (Windows) */
    void (*runtime_class_init)(void *);
    Obj (*runtime_invoke)(const void *, Obj, void **, Obj *);
    void *(*object_unbox)(Obj);
    uintptr_t (*gch_weak)(Obj, uint8_t);                 /* GC handles are pointer-sized in this il2cpp */
    Obj (*gch_target)(uintptr_t);
    void (*gch_free)(uintptr_t);
} I;

static void *k_view, *k_sdc, *k_bld, *k_farm, *k_ipres, *k_go;
static void *f_ec_inst, *f_dsm_inst;                   /* EntityComponent._instance, MonoSingleton<DataSheetManager>._instance */
static int32_t f_p_view = -1, f_p_sel = -1, f_p_type = -1, f_p_toggled = -1, f_p_toggles = -1, f_v_bs = -1, f_v_panel = -1,
               f_sdc_list = -1, f_list_items = -1, f_list_size = -1, f_ec_comps = -1, f_guid = -1, f_farm_guid = -1,
               f_bld_data = -1, f_farm_key = -1, f_quality = -1, f_ip_view = -1, f_iv_status = -1, f_cached = -1;
static void *mi_getvalue, *mi_getreader, *mi_update, *mi_setbs, *mi_getbd, *mi_go, *mi_act_h, *mi_act_s;
static void *k_comps, *mi_comps_get, *k_toggles, *mi_toggles_get;   /* the two dictionaries' own TryGetValue */
static int32_t st_dur, st_max, ct_status, ui_inspect, sum_types[4], sum_farm;

static int32_t foff(void *k, const char *name) {        /* instance field offset, base classes included */
    for (void *c = k; c; c = I.class_get_parent(c)) {
        void *f = I.class_get_field_from_name(c, name);
        if (f) return (int32_t)I.field_get_offset(f);
    }
    H->log(ME, "field %s not found", name);
    return -1;
}
static int32_t enum_v(const char *ns, const char *cls, const char *name) {
    void *k = H->find_class(ns, cls), *f = k ? I.class_get_field_from_name(k, name) : NULL;
    int32_t v = -1;
    if (f) I.field_static_get_value(f, &v);
    else H->log(ME, "enum %s.%s not found", cls, name);
    return v;
}
static void *method(void *k, const char *name, int argc) {
    void *m = k ? H->find_method(k, name, argc) : NULL;
    if (!m) H->log(ME, "method %s (%d args) not found", name, argc);
    return m;
}
static int is_a(Obj o, void *k) { return o && k && (I.class_is_assignable_from(k, I.object_get_class(o)) & 1); }
static int alive(Obj o) { return o && AT(o, f_cached, void *) != NULL; }   /* a destroyed Unity object keeps its managed shell */
static int active(Obj o, int hierarchy) {               /* GameObject.activeInHierarchy / activeSelf of a GameObject or a Component */
    if (!alive(o)) return 0;
    Obj g = I.object_get_class(o) == k_go ? o : ((Obj (*)(Obj, const void *))(*(void **)mi_go))(o, mi_go);
    void *mi = hierarchy ? mi_act_h : mi_act_s;
    return alive(g) && (((uint8_t (*)(Obj, const void *))(*(void **)mi))(g, mi) & 1);
}
static Obj invoke(void *mi, Obj self, void **args) {    /* game code: an exception is caught here, never thrown through us */
    Obj exc = NULL, r = I.runtime_invoke(mi, self, args, &exc);
    return exc ? NULL : r;
}
static int invoke_ok(void *mi, Obj self, void **args) { Obj r = invoke(mi, self, args); return r && (*(uint8_t *)I.object_unbox(r) & 1); }
static int stat(Obj sr, int32_t type, float *out) {
    void *a[1] = { &type }; Obj r = invoke(mi_getvalue, sr, a);
    if (!r) return 0;
    *out = *(float *)I.object_unbox(r);
    return 1;
}

/* ---------------------------------------------------------------- what the panel shows */
static Obj selected_one(Obj p, int *kind) {             /* the one selected building (1) or farm (2), else NULL */
    Obj sel = AT(p, f_p_sel, Obj), l = sel && I.object_get_class(sel) == k_sdc ? AT(sel, f_sdc_list, Obj) : NULL;
    if (!l) return NULL;
    if (f_list_size < 0) { void *kl = I.object_get_class(l); f_list_items = foff(kl, "_items"); f_list_size = foff(kl, "_size"); }
    if (f_list_items < 0 || f_list_size < 0 || AT(l, f_list_size, int32_t) != 1) return NULL;
    Obj it = AT(l, f_list_items, Obj), e = it && AT(it, 0x18, uintptr_t) >= 1 ? AT(it, AHDR, Obj) : NULL;
    *kind = is_a(e, k_bld) ? 1 : is_a(e, k_farm) ? 2 : 0;
    return *kind ? e : NULL;
}
static int shows_durability(int32_t type, int kind) {  /* RefreshSummary: Building..Research and Farming call UpdateBuildingEntityUI */
    if (kind == 2) return type == sum_farm;
    for (int i = 0; i < 4; i++) if (type == sum_types[i]) return 1;
    return 0;
}

/* the entity's own StatusReader (EntityComponent._instance._components[entity].TryGetReader(Status)); kept as a weak
   handle with the entity's guid, looked up again on another entity and every REREAD frames */
static uintptr_t h_reader; static uint8_t reader_guid[16]; static unsigned reader_age;
static Obj reader_of(Obj ent, const uint8_t *guid) {
    Obj r = h_reader ? I.gch_target(h_reader) : NULL;
    if (r && !memcmp(reader_guid, guid, 16) && ++reader_age < REREAD) return r;
    if (h_reader) { I.gch_free(h_reader); h_reader = 0; }
    Obj ec = NULL; I.field_static_get_value(f_ec_inst, &ec);
    Obj comps = ec ? AT(ec, f_ec_comps, Obj) : NULL;
    if (!comps) return NULL;
    void *kc = I.object_get_class(comps);
    if (kc != k_comps) { k_comps = kc; mi_comps_get = I.class_get_method_from_name(kc, "TryGetValue", 2); }
    Obj cl = NULL, rd = NULL;
    void *a[2] = { ent, &cl };
    if (!mi_comps_get || !invoke_ok(mi_comps_get, comps, a) || !cl) return NULL;
    int32_t t = ct_status; void *b[2] = { &t, &rd };
    if (!invoke_ok(mi_getreader, cl, b) || !rd) return NULL;
    h_reader = I.gch_weak(rd, 0); memcpy(reader_guid, guid, 16); reader_age = 0;
    return rd;
}

/* the Inspect window's status part while it shows: the presenter's toggled UI is EntityInspect, its view is active */
static uintptr_t h_ip, h_ip_owner;
static Obj inspect_ui(Obj p) {
    if (f_p_toggled < 0 || AT(p, f_p_toggled, int32_t) != ui_inspect) return NULL;
    Obj ip = h_ip && h_ip_owner && I.gch_target(h_ip_owner) == p ? I.gch_target(h_ip) : NULL;
    if (!ip) {                                          /* _togglePresenters[EntityInspect], once per presenter */
        Obj d = AT(p, f_p_toggles, Obj);
        if (!d) return NULL;
        void *kd = I.object_get_class(d);
        if (kd != k_toggles) { k_toggles = kd; mi_toggles_get = I.class_get_method_from_name(kd, "TryGetValue", 2); }
        int32_t key = ui_inspect; void *a[2] = { &key, &ip };
        if (!mi_toggles_get || !invoke_ok(mi_toggles_get, d, a) || !is_a(ip, k_ipres)) return NULL;
        if (h_ip) I.gch_free(h_ip);
        if (h_ip_owner) I.gch_free(h_ip_owner);
        h_ip = I.gch_weak(ip, 0); h_ip_owner = I.gch_weak(p, 0);
    }
    Obj iv = AT(ip, f_ip_view, Obj), st = active(iv, 1) ? AT(iv, f_iv_status, Obj) : NULL;
    return active(st, 1) ? st : NULL;
}
static int32_t quality(Obj ent, int kind) {            /* RefreshBuilding: a BuildingEntity's own table row, else DataSheetManager's */
    Obj d = NULL;
    if (kind == 1) d = AT(ent, f_bld_data, Obj);
    else if (f_dsm_inst && mi_getbd) {
        Obj dsm = NULL; I.field_static_get_value(f_dsm_inst, &dsm);
        void *a[1] = { AT(ent, f_farm_key, Obj) };
        if (alive(dsm)) d = invoke(mi_getbd, dsm, a);
    }
    return d ? AT(d, f_quality, int32_t) : 0;
}

/* ---------------------------------------------------------------- the frame */
typedef struct { uint8_t guid[16]; int on, again; int32_t type, q; float c, m; } Drawn;   /* what a part shows: drawn last */
static Drawn P, W;                                      /* the bottom panel, the Inspect window */
static unsigned n_panel, n_window;
static int fresh(Drawn *x, const uint8_t *guid, int32_t type) {   /* another building (or summary type) than drawn last */
    if (x->on && x->type == type && !memcmp(x->guid, guid, 16)) return 0;
    memcpy(x->guid, guid, 16); x->on = 1; x->type = type; x->again = SETTLE;
    return 1;
}
static int due(Drawn *x, int is_new, float c, float m) { /* new, values changed, or the one more fill SETTLE frames later */
    int d = is_new || c != x->c || m != x->m || (x->again > 0 && --x->again == 0);
    x->c = c; x->m = m;
    return d;
}
static void frame(Obj p) {
    Obj view = AT(p, f_p_view, Obj), ent = NULL;
    int kind = 0;
    if (view && I.object_get_class(view) == k_view && active(view, 1)) ent = selected_one(p, &kind);
    int32_t type = ent ? AT(p, f_p_type, int32_t) : -1;
    Obj bs = ent && shows_durability(type, kind) && active(AT(view, f_v_panel, Obj), 0) ? AT(view, f_v_bs, Obj) : NULL;
    Obj ins = ent ? inspect_ui(p) : NULL;
    if (!alive(bs)) { bs = NULL; P.on = 0; }
    if (!ins) W.on = 0;
    if (!bs && !ins) return;
    const uint8_t *guid = (const uint8_t *)ent + (kind == 1 ? f_guid : f_farm_guid);
    Obj sr = reader_of(ent, guid);
    float c, m;
    if (!sr || !stat(sr, st_dur, &c) || !stat(sr, st_max, &m)) return;
    if (bs && due(&P, fresh(&P, guid, type), c, m)) {
        void *a[1] = { sr };
        invoke(mi_update, view, a);
        if (++n_panel <= 3 || n_panel % 1000 == 0) H->log(ME, "panel: durability %.0f / %.0f drawn (#%u)", c, m, n_panel);
    }
    if (ins) {
        int is_new = fresh(&W, guid, 0);
        if (is_new) W.q = quality(ent, kind);
        if (due(&W, is_new, c, m)) {
            void *a[2] = { sr, &W.q };
            invoke(mi_setbs, ins, a);
            if (++n_window <= 3 || n_window % 1000 == 0) H->log(ME, "Inspect window: durability %.0f / %.0f drawn (#%u)", c, m, n_window);
        }
    }
}
static void (*o_frame)(Obj self, Obj data, const void *mi);
static void h_frame(Obj self, Obj data, const void *mi) {
    o_frame(self, data, mi);
    if (self) frame(self);
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;                               /* API 1 functions only */
#define FN(v, n) *(void **)&I.v = H->il2cpp_fn(n)
    FN(object_get_class, "il2cpp_object_get_class"); FN(class_get_parent, "il2cpp_class_get_parent");
    FN(class_get_field_from_name, "il2cpp_class_get_field_from_name"); FN(field_get_offset, "il2cpp_field_get_offset");
    FN(field_static_get_value, "il2cpp_field_static_get_value"); FN(class_get_method_from_name, "il2cpp_class_get_method_from_name");
    FN(class_is_assignable_from, "il2cpp_class_is_assignable_from"); FN(runtime_class_init, "il2cpp_runtime_class_init");
    FN(runtime_invoke, "il2cpp_runtime_invoke"); FN(object_unbox, "il2cpp_object_unbox");
    FN(gch_weak, "il2cpp_gchandle_new_weakref"); FN(gch_target, "il2cpp_gchandle_get_target"); FN(gch_free, "il2cpp_gchandle_free");
#undef FN
    for (void **f = (void **)&I; f < (void **)(&I + 1); f++) if (!*f) { H->log(ME, "an il2cpp export is missing"); return 2; }
    const char *RU = "Refactor.UI";
    void *k_pres = H->find_class(RU, "EntitySummaryUIPresenter"), *k_ec = H->find_class("Refactor.Main", "EntityComponent"),
         *k_cl = H->find_class("Refactor.Main", "ComponentList"), *k_sr = H->find_class("Refactor", "StatusReader"),
         *k_iview = H->find_class(RU, "EntityInspectionView"),
         *k_istat = H->find_class(RU, "SubUI_EntityInspectionStatus"), *k_btd = H->find_class("Util.Sheet", "BuildingTableData"),
         *k_dsm = H->find_class("Refactor.Util", "DataSheetManager"), *k_comp = H->find_class("UnityEngine", "Component");
    k_view = H->find_class(RU, "EntitySummaryUIView"); k_sdc = H->find_class("Refactor.Main.Processor", "SelectDataContainer");
    k_bld = H->find_class("Refactor", "BuildingEntity"); k_farm = H->find_class("Refactor", "FarmEntity");
    k_ipres = H->find_class(RU, "EntityInspectionPresenter"); k_go = H->find_class("UnityEngine", "GameObject");
    if (!k_pres || !k_ec || !k_cl || !k_sr || !k_view || !k_sdc || !k_bld || !k_farm || !k_ipres || !k_go || !k_iview || !k_istat || !k_btd || !k_comp) {
        H->log(ME, "a game class was not found"); return 3;
    }
    f_p_view = foff(k_pres, "<View>k__BackingField"); f_p_sel = foff(k_pres, "_selectedReader"); f_p_type = foff(k_pres, "_currentType");
    f_p_toggled = foff(k_pres, "_currentToggled"); f_p_toggles = foff(k_pres, "_togglePresenters");
    f_v_bs = foff(k_view, "_buildingStatusUI"); f_v_panel = foff(k_view, "_buildingPanel"); f_sdc_list = foff(k_sdc, "_selectedEntities");
    f_ec_comps = foff(k_ec, "_components"); f_guid = foff(k_bld, "<Guid>k__BackingField"); f_farm_guid = foff(k_farm, "<Guid>k__BackingField");
    f_bld_data = foff(k_bld, "<Data>k__BackingField"); f_farm_key = foff(k_farm, "<Key>k__BackingField"); f_quality = foff(k_btd, "Quality");
    f_ip_view = foff(k_ipres, "<View>k__BackingField"); f_iv_status = foff(k_iview, "_statusUI");
    f_cached = foff(H->find_class("UnityEngine", "Object"), "m_CachedPtr");
    f_ec_inst = I.class_get_field_from_name(k_ec, "_instance");
    void *k_single = k_dsm ? I.class_get_parent(k_dsm) : NULL;        /* Util.MonoSingleton<DataSheetManager> */
    f_dsm_inst = k_single ? I.class_get_field_from_name(k_single, "_instance") : NULL;
    if (k_single) I.runtime_class_init(k_single);
    mi_getvalue = method(k_sr, "GetValue", 1); mi_getreader = method(k_cl, "TryGetReader", 2);
    mi_update = method(k_view, "UpdateBuildingEntityUI", 1); mi_setbs = method(k_istat, "SetBuildingStatus", 2);
    mi_getbd = method(k_dsm, "GetBuildingData", 1); mi_go = method(k_comp, "get_gameObject", 0);
    mi_act_h = method(k_go, "get_activeInHierarchy", 0); mi_act_s = method(k_go, "get_activeSelf", 0);
    st_dur = enum_v("Refactor", "StatType", "Durability"); st_max = enum_v("Refactor", "StatType", "MaxDurability");
    ct_status = enum_v("Refactor.Component", "ComponentType", "Status"); ui_inspect = enum_v(RU, "StaticUIType", "EntityInspect");
    const char *st[4] = { "Building", "Storage", "WorkPlan", "Research" };
    for (int i = 0; i < 4; i++) sum_types[i] = enum_v(RU, "SummaryType", st[i]);
    sum_farm = enum_v(RU, "SummaryType", "Farming");
    int32_t offs[] = { f_p_view, f_p_sel, f_p_type, f_p_toggled, f_p_toggles, f_v_bs, f_v_panel, f_sdc_list, f_ec_comps, f_guid,
                       f_farm_guid, f_bld_data, f_farm_key, f_quality, f_ip_view, f_iv_status, f_cached };
    for (size_t i = 0; i < sizeof offs / sizeof *offs; i++) if (offs[i] < 0) return 4;
    if (!f_ec_inst || !mi_getvalue || !mi_getreader || !mi_update || !mi_setbs || !mi_go || !mi_act_h || !mi_act_s
        || st_dur < 0 || st_max < 0 || ct_status < 0 || ui_inspect < 0 || sum_types[0] < 0 || sum_farm < 0) return 5;
    if (!mi_getbd || !f_dsm_inst) H->log(ME, "DataSheetManager.GetBuildingData not found: a farm's Inspect window gets quality 0");
    I.runtime_class_init(k_ec);
    int r = H->hook_method(ME, "Refactor.UI", "EntitySummaryUIPresenter", "RefreshOnFrame", 1, (void *)h_frame, (void **)&o_frame);
    if (r) { H->log(ME, "hook EntitySummaryUIPresenter.RefreshOnFrame failed (%d)", r); return 6; }
    H->log(ME, "a selected building's HP follows its durability (panel and Inspect window; view +%#x, Durability %d/%d)",
           f_p_view, st_dur, st_max);
    return 0;
}
