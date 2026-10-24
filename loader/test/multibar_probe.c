// multibar-probe: TEST ONLY module for multibar_test.py (never shipped). Drives the game's own selection and click paths
// from a request file, polled at StandaloneInputModule.UpdateModule (main thread, every frame): <DSRIG_DIR>/multibar_probe_req.txt,
// one line:
//   sel <i>        SelectPlayerUnitRequested(i): select the i-th party unit (a portrait click)
//   add <i>        ToggleSelectUnitRequested(i): Shift + portrait click (add / remove the i-th party unit)
//   click <k> <s>  the Button.onClick of slot s of the multi-unit view's card k ("QuickSlotUI_Multi<k>"): the stock click
//                  path from the button on (SkillSlotUI listener -> QuickSlotUI -> input -> event queue -> handler)
//   key <i>        UseQuickSlotSkillRequested(i): the skill key of bar slot i (stock input path)
//   state          every QuickSlotUI (the real bar + the cards): name, active, slots, shown unit; the summary card's body
// and logs, as they happen, every UseSkillModeContext.AddUseSkill (unit, skill): who the game will use a skill with.
// Each answer is one dsload.log line "multibar-probe: ...". Every game call by il2cpp name.
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
#define MC(mi, R, ...) ((R (*)(__VA_ARGS__))(*(void **)(mi)))

static char req_path[1024];
static void (*o_tick)(Obj, const void *);
static void (*o_add)(Obj, Obj, Obj, const void *);
static void (*add_event)(Obj, Obj, const void *);
static Obj (*obj_new)(void *), (*str_new)(const char *), (*tobj)(void *);
static void *(*ctype)(void *), *(*methods)(void *, void **), *(*nested)(void *, void **);
static const char *(*mname)(void *);
static uint32_t (*pcount)(void *);
static uint8_t (*is_gen)(void *);
static void *mainflow_k, *qsui_k, *esv_k, *usmc_k, *button_k, *unit_k;
static void *m_find_any, *m_find_all, *m_go_find, *m_get_comp, *m_name, *m_go, *m_active_h, *m_active_self, *m_tr, *m_parent,
            *m_tr_find, *m_onclick, *m_invoke, *m_skill_key, *m_go_tr;
static int32_t f_slots, f_selected, f_tabs, f_guid, f_key;

static void *msig(void *k, const char *name, int argc) {
    void *(*parent)(void *) = H->il2cpp_fn("il2cpp_class_get_parent");
    for (void *c = k; c; c = parent(c)) {
        void *it = NULL, *m;
        while ((m = methods(c, &it))) if (!strcmp(mname(m), name) && (int)pcount(m) == argc && !(is_gen && is_gen(m)) && *(void **)m) return m;
    }
    return NULL;
}
static int32_t fofs(void *k, const char *f) { return k ? H->field_offset(k, f) : -1; }
static void guid_str(const uint8_t *g, char *out) {           /* .NET Guid layout -> the usual text */
    snprintf(out, 40, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
             g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}
static void u8(Obj s, char *out, size_t cap) {
    size_t k = 0; int32_t n = s ? AT(s, 0x10, int32_t) : 0;
    for (int32_t i = 0; i < n && k + 1 < cap; i++) { uint16_t c = ((uint16_t *)((char *)s + 0x14))[i]; out[k++] = c < 0x80 ? (char)c : '?'; }
    out[k] = 0;
}
static Obj go_of(Obj c) { return c ? MC(m_go, Obj, Obj, void *)(c, m_go) : NULL; }
static int active_h(Obj c) { Obj g = go_of(c); return g ? MC(m_active_h, uint8_t, Obj, void *)(g, m_active_h) & 1 : 0; }
static void obj_name(Obj o, char *out, size_t cap) { out[0] = 0; if (o) u8(MC(m_name, Obj, Obj, void *)(o, m_name), out, cap); }
static int list_n(Obj l) { return l ? AT(l, 0x18, int32_t) : 0; }
static Obj list_at(Obj l, int i) { return AT(AT(l, 0x10, char *), 0x20 + 8 * i, Obj); }

/* UseSkillModeContext.AddUseSkill(skillUser, useSkill): log who */
static void my_add(Obj ctx, Obj unit, Obj skill, const void *mi) {
    char g[40] = "?", key[96] = "";
    if (unit && f_guid >= 0) guid_str((uint8_t *)unit + f_guid, g);
    if (skill && f_key >= 0) u8(AT(skill, f_key, Obj), key, sizeof key);
    H->log(ME, "multibar-probe: AddUseSkill %s %s", g, key);
    o_add(ctx, unit, skill, mi);
}

static void do_state(void) {
    Obj arr = MC(m_find_all, Obj, Obj, uint8_t, void *)(tobj(ctype(qsui_k)), 1, m_find_all);   /* FindObjectsOfType(Type, true) */
    int n = arr ? (int)AT(arr, 0x18, uintptr_t) : 0;
    char line[1500]; size_t o = 0;
    for (int i = 0; i < n; i++) {
        Obj v = AT(arr, 0x20 + 8 * i, Obj); if (!v) continue;
        char nm[64], g[40]; obj_name(go_of(v), nm, sizeof nm); guid_str((uint8_t *)v + f_selected, g);
        o += (size_t)snprintf(line + o, o < sizeof line ? sizeof line - o : 0, "%s[%s active=%d slots=%d unit=%s]", i ? " " : "", nm, active_h(v),
                              list_n(AT(v, f_slots, Obj)), g);
    }
    Obj esv = MC(m_find_any, Obj, Obj, int32_t, void *)(tobj(ctype(esv_k)), 1, m_find_any);
    int body = -1, tabs_in_body = -1;
    if (esv) {
        Obj root = MC(m_tr, Obj, Obj, void *)(esv, m_tr);
        Obj panel = MC(m_tr_find, Obj, Obj, Obj, void *)(root, str_new("Panel"), m_tr_find);
        if (panel) body = MC(m_active_self, uint8_t, Obj, void *)(go_of(panel), m_active_self) & 1;
        Obj tabs = AT(esv, f_tabs, Obj);
        if (tabs && panel) tabs_in_body = MC(m_parent, Obj, Obj, void *)(MC(m_tr, Obj, Obj, void *)(tabs, m_tr), m_parent) == panel;
    }
    H->log(ME, "multibar-probe: state: bars %s | summary_body=%d tabs_in_body=%d", line, body, tabs_in_body);
}

static void do_click(int k, int s) {
    char nm[32]; snprintf(nm, sizeof nm, "QuickSlotUI_Multi%d", k);
    Obj go = MC(m_go_find, Obj, Obj, void *)(str_new(nm), m_go_find);          /* GameObject.Find: active objects only */
    Obj v = go ? MC(m_get_comp, Obj, Obj, Obj, void *)(MC(m_go_tr, Obj, Obj, void *)(go, m_go_tr), tobj(ctype(qsui_k)), m_get_comp) : NULL;
    Obj slots = v ? AT(v, f_slots, Obj) : NULL;
    if (!slots || s < 0 || s >= list_n(slots)) { H->log(ME, "multibar-probe: click %d %d: no such card / slot (card %s)", k, s, go ? "found" : "not found"); return; }
    Obj b = list_at(slots, s);
    Obj ev = MC(m_onclick, Obj, Obj, void *)(b, m_onclick);
    MC(m_invoke, void, Obj, void *)(ev, m_invoke);
    H->log(ME, "multibar-probe: click %d %d: done", k, s);
}

static Obj new_event(const char *name, int idx) {
    void *k = H->find_class("Refactor.Main.Event", name);
    void *it = NULL, *m, *mi = NULL;
    void *(*param)(void *, uint32_t) = H->il2cpp_fn("il2cpp_method_get_param");
    char *(*tname)(void *) = H->il2cpp_fn("il2cpp_type_get_name");
    while (k && (m = methods(k, &it))) if (!strcmp(mname(m), ".ctor") && pcount(m) == 1 && !strcmp(tname(param(m, 0)), "System.Int32")) mi = m;
    if (!mi) { H->log(ME, "multibar-probe: %s(int) not found", name); return NULL; }
    Obj ev = obj_new(k);
    MC(mi, void, Obj, int32_t, void *)(ev, idx, mi);
    return ev;
}

static void run_req(Obj eq, const char *line) {
    char cmd[32] = ""; int a = 0, b = 0;
    sscanf(line, "%31s %d %d", cmd, &a, &b);
    if (!strcmp(cmd, "state")) { do_state(); return; }
    if (!strcmp(cmd, "click")) { do_click(a, b); return; }
    Obj ev = NULL;
    if (!strcmp(cmd, "sel")) ev = new_event("SelectPlayerUnitRequested", a);
    else if (!strcmp(cmd, "add")) ev = new_event("ToggleSelectUnitRequested", a);
    else if (!strcmp(cmd, "key")) ev = new_event("UseQuickSlotSkillRequested", a);   /* a skill key (stock input) */
    else { H->log(ME, "multibar-probe: unknown request '%s'", line); return; }
    if (ev) add_event(eq, ev, NULL);
    H->log(ME, "multibar-probe: %s: %s", line, ev ? "sent" : "FAILED");
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
    Obj mf = MC(m_find_any, Obj, Obj, int32_t, void *)(tobj(ctype(mainflow_k)), 0, m_find_any);
    int32_t o_eq = fofs(mainflow_k, "_eventQueue");
    Obj eq = mf && o_eq >= 0 ? AT(mf, o_eq, Obj) : NULL;
    if (!eq) { H->log(ME, "multibar-probe: no MainFlow / EventQueue (%d)", o_eq); return; }
    run_req(eq, line);
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    const char *d = getenv("DSRIG_DIR");
    if (!d) { H->log(ME, "multibar-probe: no DSRIG_DIR (test copies only)"); return 1; }
    snprintf(req_path, sizeof req_path, "%s/multibar_probe_req.txt", d);
    *(void **)&obj_new = H->il2cpp_fn("il2cpp_object_new"); *(void **)&str_new = H->il2cpp_fn("il2cpp_string_new");
    *(void **)&tobj = H->il2cpp_fn("il2cpp_type_get_object"); *(void **)&ctype = H->il2cpp_fn("il2cpp_class_get_type");
    *(void **)&methods = H->il2cpp_fn("il2cpp_class_get_methods"); *(void **)&mname = H->il2cpp_fn("il2cpp_method_get_name");
    *(void **)&pcount = H->il2cpp_fn("il2cpp_method_get_param_count"); *(void **)&is_gen = H->il2cpp_fn("il2cpp_method_is_generic");
    *(void **)&nested = H->il2cpp_fn("il2cpp_class_get_nested_types");
    *(void **)&add_event = H->find_method_address("Refactor.Main", "EventQueue", "AddEvent", 1);
    mainflow_k = H->find_class("Refactor.Main", "MainFlow"); qsui_k = H->find_class("Refactor.UI", "QuickSlotUI");
    esv_k = H->find_class("Refactor.UI", "EntitySummaryUIView"); usmc_k = H->find_class("Refactor.Main.Processor", "UseSkillModeContext");
    button_k = H->find_class("UnityEngine.UI", "Button"); unit_k = H->find_class("Refactor", "UnitEntity");
    void *ko = H->find_class("UnityEngine", "Object"), *kc = H->find_class("UnityEngine", "Component"), *kg = H->find_class("UnityEngine", "GameObject");
    void *kt = H->find_class("UnityEngine", "Transform"), *ke = H->find_class("UnityEngine.Events", "UnityEvent");
    m_find_any = msig(ko, "FindAnyObjectByType", 2); m_find_all = msig(ko, "FindObjectsOfType", 2); m_name = msig(ko, "get_name", 0);
    m_go_find = msig(kg, "Find", 1); m_active_h = msig(kg, "get_activeInHierarchy", 0); m_active_self = msig(kg, "get_activeSelf", 0); m_go_tr = msig(kg, "get_transform", 0);
    m_get_comp = msig(kc, "GetComponent", 1); m_go = msig(kc, "get_gameObject", 0); m_tr = msig(kc, "get_transform", 0);
    m_parent = msig(kt, "get_parent", 0); m_tr_find = msig(kt, "Find", 1); m_onclick = msig(button_k, "get_onClick", 0); m_invoke = msig(ke, "Invoke", 0);
    f_slots = fofs(qsui_k, "_skillSlots"); f_selected = fofs(qsui_k, "_selected"); f_tabs = fofs(esv_k, "_toggleButtons");
    f_guid = fofs(unit_k, "<Guid>k__BackingField"); f_key = fofs(H->find_class("Refactor", "Skill_Base"), "<Key>k__BackingField");
    (void)m_skill_key; (void)nested;
    if (!obj_new || !add_event || !mainflow_k || !qsui_k || !m_find_any || !m_find_all || !m_go_find || !m_onclick || !m_invoke || !m_go_tr || f_slots < 0 || f_selected < 0) {
        H->log(ME, "multibar-probe: names missing"); return 4;
    }
    if (H->hook_method(ME, "UnityEngine.EventSystems", "StandaloneInputModule", "UpdateModule", 0, (void *)my_tick, (void **)&o_tick)) return 3;
    if (H->hook_method(ME, "Refactor.Main.Processor", "UseSkillModeContext", "AddUseSkill", 2, (void *)my_add, (void **)&o_add)) return 5;
    H->log(ME, "multibar-probe: ready (%s)", req_path);
    return 0;
}
