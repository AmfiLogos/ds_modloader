// zetu-probe: TEST ONLY module for zetu_ui_test.py (never shipped). Drives the game's own flows from a request file,
// polled at StandaloneInputModule.UpdateModule (EventSystem.Update ticks it every frame, main thread; EventQueue.Update
// and MainFlow.Update belong to the multiplayer module): <DSRIG_DIR>/zetu_probe_req.txt, one line:
//   recruit            the game's Recruit button: candidates refreshed for free (RecruitHelper.ForceRefreshRecruitCandidates),
//                      gold / population room made if short, then CandidateRecruitRequested(0, Base, MapSizeMax/2-(0,2), Player)
//   select_all         SelectAllUnitRequested (the "select all units" key; a toggle)
//   toggle <n>         ToggleEntityUIRequested(StaticUIType n): 6 Status, 7 Skill, 8 Inventory (the HUD buttons / keys)
//   result             ShowDungeonResultRequested (the dungeon's "Return to base" button: the result screen)
//   risks              the risk rows of the expedition window's supplies step (SubUI_CheckInventoryPanelUI._riskRows)
//   tree <Class> <d>   layout of the live Refactor.UI.<Class> view: name, active, anchors, pivot, position, size, scale,
//                      layout components, to depth d
// Each answer is one dsload.log line "zetu-probe: <cmd>: ...". Every game call by il2cpp name.
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DSL_PLUGIN();
static const DSLHost *H;
static DSLPlugin *ME;
typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))

static void (*o_tick)(Obj, const void *);
static Obj (*find_obj)(Obj, const void *);
static void *find_obj_mi, *mainflow_k;
static void (*add_event)(Obj, Obj, const void *);
static Obj (*obj_new)(void *);
static char req_path[1024];

static Obj new_event(const char *name, int argc, void **mi_out) {
    void *k = H->find_class("Refactor.Main.Event", name);
    void *mi = k ? H->find_method(k, ".ctor", argc) : NULL;
    if (!k || !mi) { H->log(ME, "zetu-probe: %s not found", name); return NULL; }
    *mi_out = mi;
    return obj_new(k);
}
static int32_t fofs(const char *ns, const char *cls, const char *f) {
    void *k = H->find_class(ns, cls);
    return k ? H->field_offset(k, f) : -1;
}

static void do_recruit(Obj eq) {
    Obj mf = AT(eq, 0x10, Obj);                                         /* EventQueue._mainFlow */
    int32_t o_cd = fofs("Refactor.Main", "MainFlow", "_campaignData"), o_hs = fofs("Refactor.Main", "MainFlow", "_campaignHelperSet");
    int32_t o_clan = fofs("Refactor.Main", "CampaignDataContainer", "ClanDataContainer");
    int32_t o_rh = fofs("Refactor.Main", "CampaignHelperSet", "_recruitHelper");
    if (!mf || o_cd < 0 || o_hs < 0 || o_clan < 0 || o_rh < 0) { H->log(ME, "zetu-probe: recruit: fields missing %d %d %d %d", o_cd, o_hs, o_clan, o_rh); return; }
    Obj cd = AT(mf, o_cd, Obj), hs = AT(mf, o_hs, Obj);
    Obj clan = cd ? AT(cd, o_clan, Obj) : NULL, rh = hs ? AT(hs, o_rh, Obj) : NULL;
    if (!clan || !rh) { H->log(ME, "zetu-probe: recruit: no clan / helper"); return; }
    void *kr = H->find_class("Refactor.Main.Event", "RecruitHelper");
    void *mr = kr ? H->find_method(kr, "ForceRefreshRecruitCandidates", 0) : NULL;
    int refreshed = mr ? ((uint8_t (*)(Obj, const void *))H->method_address(mr))(rh, mr) : -1;
    int32_t *gold = &AT(clan, 0x2C, int32_t), *cur = &AT(clan, 0x38, int32_t), *maxp = &AT(clan, 0x3C, int32_t);
    Obj list = AT(clan, 0x48, Obj);
    int ncand = list ? AT(list, 0x18, int32_t) : -1;
    H->log(ME, "zetu-probe: recruit: refreshed %d, candidates %d, gold %d, population %d/%d", refreshed, ncand, *gold, *cur, *maxp);
    if (*gold < 100000) *gold = 100000;
    if (*maxp <= *cur) *maxp = *cur + 1;
    /* the Recruit button's spawn cell: SystemVar.Campaign.MapSizeMax / 2 - (0, 2) (InputEventFactory, UIInputType 139) */
    void *(*nested)(void *, void **) = H->il2cpp_fn("il2cpp_class_get_nested_types");
    const char *(*cname)(void *) = H->il2cpp_fn("il2cpp_class_get_name");
    void *(*field)(void *, const char *) = H->il2cpp_fn("il2cpp_class_get_field_from_name");
    void (*fs_get)(void *, void *) = H->il2cpp_fn("il2cpp_field_static_get_value");
    void *o = H->find_class("Refactor", "SystemVar"), *it = NULL, *k, *camp = NULL;
    while (o && (k = nested(o, &it))) if (!strcmp(cname(k), "Campaign")) camp = k;
    int32_t ms[2] = { 0, 0 };
    void *f = camp ? field(camp, "<MapSizeMax>k__BackingField") : NULL;
    if (f) fs_get(f, ms);
    int32_t pos[2] = { ms[0] / 2, ms[1] / 2 - 2 };
    void *mi; Obj ev = new_event("CandidateRecruitRequested", 4, &mi);
    if (!ev) return;
    ((void (*)(Obj, int32_t, int32_t, uint64_t, int32_t, const void *))H->method_address(mi))(ev, 0, 0, (uint64_t)(uint32_t)pos[0] | (uint64_t)(uint32_t)pos[1] << 32, 1, mi);
    add_event(eq, ev, NULL);
    H->log(ME, "zetu-probe: recruit: CandidateRecruitRequested(0, Base, %d,%d, Player) sent (map size %d x %d)", pos[0], pos[1], ms[0], ms[1]);
}


/* ---------------------------------------------------------------- tree: the layout of a live view (debugging) */
typedef struct { float x, y; } V2;
typedef struct { float x, y, z; } V3;
static void *msig(void *k, const char *name, int argc) {
    void *(*methods)(void *, void **) = H->il2cpp_fn("il2cpp_class_get_methods");
    const char *(*mname)(void *) = H->il2cpp_fn("il2cpp_method_get_name");
    uint32_t (*pc)(void *) = H->il2cpp_fn("il2cpp_method_get_param_count");
    void *(*parent)(void *) = H->il2cpp_fn("il2cpp_class_get_parent");
    for (void *c = k; c; c = parent(c)) { void *it = NULL, *m; while ((m = methods(c, &it))) if (!strcmp(mname(m), name) && (int)pc(m) == argc) return m; }
    return NULL;
}
#define MC(mi, R, ...) ((R (*)(__VA_ARGS__, const void *))(*(void **)(mi)))
static void u8(Obj s, char *o, size_t cap) { int n = s ? AT(s, 0x10, int32_t) : 0; size_t k = 0; for (int i = 0; i < n && k + 1 < cap; i++) { uint16_t c = ((uint16_t *)((char *)s + 0x14))[i]; o[k++] = c < 0x80 ? (char)c : '?'; } o[k] = 0; }
static void tree(Obj t, int d, int maxd) {
    void *kt = H->find_class("UnityEngine", "Transform"), *kr = H->find_class("UnityEngine", "RectTransform"), *kc = H->find_class("UnityEngine", "Component");
    void *ko = H->find_class("UnityEngine", "Object"), *kg = H->find_class("UnityEngine", "GameObject");
    void *(*ocls)(Obj) = H->il2cpp_fn("il2cpp_object_get_class"); int (*isa)(void *, void *) = H->il2cpp_fn("il2cpp_class_is_assignable_from");
    const char *(*cname)(void *) = H->il2cpp_fn("il2cpp_class_get_name");
    static void *m_name, *m_go, *m_self, *m_amin, *m_amax, *m_piv, *m_pos, *m_size, *m_scale, *m_count, *m_child, *m_comps;
    if (!m_name) { m_name = msig(ko, "get_name", 0); m_go = msig(kc, "get_gameObject", 0); m_self = msig(kg, "get_activeSelf", 0);
        m_amin = msig(kr, "get_anchorMin", 0); m_amax = msig(kr, "get_anchorMax", 0); m_piv = msig(kr, "get_pivot", 0); m_pos = msig(kr, "get_anchoredPosition", 0);
        m_size = msig(kr, "get_sizeDelta", 0); m_scale = msig(kt, "get_localScale", 0); m_count = msig(kt, "get_childCount", 0); m_child = msig(kt, "GetChild", 1);
        void *it = NULL, *m; void *(*methods)(void *, void **) = H->il2cpp_fn("il2cpp_class_get_methods"); const char *(*mname)(void *) = H->il2cpp_fn("il2cpp_method_get_name");
        uint32_t (*pc)(void *) = H->il2cpp_fn("il2cpp_method_get_param_count"); uint8_t (*gen)(void *) = H->il2cpp_fn("il2cpp_method_is_generic");
        while ((m = methods(kc, &it))) if (!strcmp(mname(m), "GetComponents") && pc(m) == 1 && !gen(m)) { m_comps = m; break; } }
    Obj go = MC(m_go, Obj, Obj)(t, m_go); char nm[80]; u8(MC(m_name, Obj, Obj)(go, m_name), nm, sizeof nm);
    char line[700]; int o = snprintf(line, sizeof line, "%*s%s%s", d * 2, "", nm, MC(m_self, uint8_t, Obj)(go, m_self) ? "" : " (off)");
    if (isa(kr, ocls(t))) {
        V2 a = MC(m_amin, V2, Obj)(t, m_amin), b = MC(m_amax, V2, Obj)(t, m_amax), p = MC(m_piv, V2, Obj)(t, m_piv), q = MC(m_pos, V2, Obj)(t, m_pos), z = MC(m_size, V2, Obj)(t, m_size);
        V3 sc = MC(m_scale, V3, Obj)(t, m_scale);
        o += snprintf(line + o, sizeof line - o, " a(%.2f,%.2f)-(%.2f,%.2f) pv(%.2f,%.2f) pos(%.0f,%.0f) size(%.0fx%.0f) sc %.2f", a.x, a.y, b.x, b.y, p.x, p.y, q.x, q.y, z.x, z.y, sc.x);
    }
    if (m_comps) {                                             /* Component[] GetComponents(Type) with typeof(Component) */
        Obj (*tobj)(void *) = H->il2cpp_fn("il2cpp_type_get_object"); void *(*ctype)(void *) = H->il2cpp_fn("il2cpp_class_get_type");
        Obj arr = MC(m_comps, Obj, Obj, Obj)(t, tobj(ctype(kc)), m_comps);
        int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0;
        for (int i = 0; i < n && o < (int)sizeof line - 40; i++) { Obj c = AT(arr, 0x20 + 8 * i, Obj); const char *cn = c ? cname(ocls(c)) : "?";
            if (strcmp(cn, "RectTransform") && strcmp(cn, "CanvasRenderer")) o += snprintf(line + o, sizeof line - o, " [%s]", cn); }
    }
    H->log(ME, "tree %s", line);
    if (d >= maxd) return;
    int n = MC(m_count, int32_t, Obj)(t, m_count);
    for (int i = 0; i < n && i < 30; i++) tree(MC(m_child, Obj, Obj, int32_t)(t, i, m_child), d + 1, maxd);
}

static void run_req(Obj eq, const char *line) {
    char cmd[32] = "", cls[64] = ""; int arg = 0;
    sscanf(line, "%31s %d", cmd, &arg);
    if (!strcmp(cmd, "risks")) {
        void *k = H->find_class("Refactor.UI", "SubUI_CheckInventoryPanelUI");
        Obj (*tobj)(void *) = H->il2cpp_fn("il2cpp_type_get_object"); void *(*ctype)(void *) = H->il2cpp_fn("il2cpp_class_get_type");
        Obj v = k ? find_obj(tobj(ctype(k)), find_obj_mi) : NULL;
        int32_t off = k ? H->field_offset(k, "_riskRows") : -1;
        Obj l = v && off >= 0 ? AT(v, off, Obj) : NULL;
        int n = l ? AT(l, 0x18, int32_t) : 0, shown = 0;
        void *mg = msig(H->find_class("TMPro", "TMP_Text"), "get_text", 0), *mgo = msig(H->find_class("UnityEngine", "Component"), "get_gameObject", 0);
        void *mself = msig(H->find_class("UnityEngine", "GameObject"), "get_activeSelf", 0);
        void *(*vm)(Obj, void *) = H->il2cpp_fn("il2cpp_object_get_virtual_method");
        for (int i = 0; i < n; i++) {
            Obj t = AT(AT(l, 0x10, Obj), 0x20 + 8 * i, Obj); if (!t) continue;
            if (!MC(mself, uint8_t, Obj)(MC(mgo, Obj, Obj)(t, mgo), mself)) continue;
            void *g = vm(t, mg); char tx[300]; u8(MC(g, Obj, Obj)(t, g), tx, sizeof tx);
            H->log(ME, "risk row %d: %s", i, tx); shown++;
        }
        H->log(ME, "zetu-probe: risks: %d shown of %d rows%s", shown, n, v ? "" : " (no supplies step open)"); return;
    }
    if (!strcmp(cmd, "tree")) {
        int dep = 6; sscanf(line, "%*s %63s %d", cls, &dep);
        void *k = H->find_class("Refactor.UI", cls);
        Obj (*tobj)(void *) = H->il2cpp_fn("il2cpp_type_get_object"); void *(*ctype)(void *) = H->il2cpp_fn("il2cpp_class_get_type");
        Obj v = k ? find_obj(tobj(ctype(k)), find_obj_mi) : NULL;
        void *mt = v ? msig(H->find_class("UnityEngine", "Component"), "get_transform", 0) : NULL;
        if (!v || !mt) { H->log(ME, "zetu-probe: tree %s: not found", cls); return; }
        tree(MC(mt, Obj, Obj)(v, mt), 0, dep);
        H->log(ME, "zetu-probe: tree %s done", cls); return;
    }
    void *mi; Obj ev = NULL;
    if (!strcmp(cmd, "recruit")) { do_recruit(eq); return; }
    if (!strcmp(cmd, "select_all")) { if ((ev = new_event("SelectAllUnitRequested", 0, &mi))) ((void (*)(Obj, const void *))H->method_address(mi))(ev, mi); }
    else if (!strcmp(cmd, "result")) { if ((ev = new_event("ShowDungeonResultRequested", 0, &mi))) ((void (*)(Obj, const void *))H->method_address(mi))(ev, mi); }
    else if (!strcmp(cmd, "toggle")) { if ((ev = new_event("ToggleEntityUIRequested", 1, &mi))) ((void (*)(Obj, int32_t, const void *))H->method_address(mi))(ev, arg, mi); }
    else { H->log(ME, "zetu-probe: unknown request '%s'", line); return; }
    if (ev) add_event(eq, ev, NULL);
    H->log(ME, "zetu-probe: %s: %s", line, ev ? "sent" : "FAILED");
}

static unsigned frames;
static void my_tick(Obj self, const void *mi) {
    o_tick(self, mi);
    if (++frames % 10) return;
    FILE *f = fopen(req_path, "r");
    if (!f) return;
    char line[256] = ""; if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f);
    char done[1100]; snprintf(done, sizeof done, "%s.done", req_path); rename(req_path, done);
    line[strcspn(line, "\r\n")] = 0;
    /* the EventQueue: MainFlow (Object.FindObjectOfType(typeof(MainFlow))) +0x48 */
    Obj (*tobj)(void *) = H->il2cpp_fn("il2cpp_type_get_object");
    void *(*ctype)(void *) = H->il2cpp_fn("il2cpp_class_get_type");
    Obj mf = find_obj(tobj(ctype(mainflow_k)), find_obj_mi);
    Obj eq = mf ? AT(mf, 0x48, Obj) : NULL;
    if (!eq) { H->log(ME, "zetu-probe: no MainFlow / EventQueue"); return; }
    run_req(eq, line);
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    const char *d = getenv("DSRIG_DIR");
    if (!d) { H->log(ME, "zetu-probe: no DSRIG_DIR (test copies only)"); return 1; }
    snprintf(req_path, sizeof req_path, "%s/zetu_probe_req.txt", d);
    *(void **)&obj_new = H->il2cpp_fn("il2cpp_object_new");
    *(void **)&add_event = H->find_method_address("Refactor.Main", "EventQueue", "AddEvent", 1);
    if (!obj_new || !add_event) return 2;
    mainflow_k = H->find_class("Refactor.Main", "MainFlow");
    void *ko = H->find_class("UnityEngine", "Object"), *it = NULL, *m;
    void *(*methods)(void *, void **) = H->il2cpp_fn("il2cpp_class_get_methods");
    const char *(*mname)(void *) = H->il2cpp_fn("il2cpp_method_get_name");
    uint32_t (*pc)(void *) = H->il2cpp_fn("il2cpp_method_get_param_count");
    uint8_t (*gen)(void *) = H->il2cpp_fn("il2cpp_method_is_generic");
    while (ko && (m = methods(ko, &it)))
        if (!strcmp(mname(m), "FindObjectOfType") && pc(m) == 1 && !(gen && gen(m))) { find_obj_mi = m; break; }
    if (!find_obj_mi || !mainflow_k) return 4;
    *(void **)&find_obj = *(void **)find_obj_mi;
    if (H->hook_method(ME, "UnityEngine.EventSystems", "StandaloneInputModule", "UpdateModule", 0, (void *)my_tick, (void **)&o_tick)) return 3;
    H->log(ME, "zetu-probe: ready (%s)", req_path);
    return 0;
}
