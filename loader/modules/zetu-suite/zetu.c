// ZET-U Mod Suite (mod id zetu-suite): two gameplay settings, as a dsload code module (runtime hooks, GameAssembly
// file unchanged). Every hook is found by il2cpp name (Mac and Windows); build 25846389 (DS_B.0.4.29).
//
// 1. "skills_no_weapon" (toggle, default on): skills of a skill tree work without that tree's weapon.
//    The game checks a skill's RequiredEquipment ("Equipment_Blunt", ...) in ONE place for use (player and AI):
//    Refactor.Combat.EquipmentCondition.CheckCondition looks through the equipment slots, and when none matches asks
//    Refactor.SkillReader.HasEquipBypass(key) (the game's own bypass: Fire Battle Mage, Nature Spirit, Brick).
//    Learning is never blocked by the weapon in the stock game; SkillLearnRequirementValidator.Validate asks the same
//    bypass for its "you need X, continue anyway?" text and the tooltip's red line. Here HasEquipBypass answers yes
//    for every "Equipment_*" key. Prerequisites, levels, skill points, ammo (arrows) and the weapon passives
//    (Healing Touch, Two-Hand Grip, Shield Handling) stay stock: they do not go through this method.
// 2. "max_units" (int 4..8, default 4): units on one expedition. The game's value is the static int
//    Refactor.SystemVar.Campaign.<PlayerPartyMax>k__BackingField, set by Campaign.Init from GameSystemTable row
//    GAMESYSTEM_Campaign_PlayerPartyMax (4.0). Every reader inlines the static (no getter call), so Campaign.Init is
//    hooked and the static set to the setting after it. Readers that index fixed 4-panel prefabs (the Inventory,
//    Status, Skill and Summary windows of selected units) run with the stock value (party_stock_enter/leave), so they
//    show 4 units at most as before instead of an index error; the expedition risk "fewer units than the maximum"
//    keeps the stock threshold. The expedition planning screens grow to the setting in zetu_ui.c.
#include "dsl_sdk.h"
#include "zetu_settings.h"
#include <stdint.h>
#include <string.h>

DSL_PLUGIN();

static const DSLHost *H;
static DSLPlugin *ME;
static int s_free_skills, s_max_units;

/* ------------------------------------------------------------------ 1. weapon requirement */
static uint8_t (*orig_bypass)(void *self, void *key, const void *mi);
static int bypass_n;
static int is_equipment_key(void *s) {                 /* il2cpp string: length @0x10, UTF-16 chars @0x14 */
    if (!s) return 0;
    static const uint16_t w[10] = { 'E', 'q', 'u', 'i', 'p', 'm', 'e', 'n', 't', '_' };
    return *(int32_t *)((char *)s + 0x10) > 10 && !memcmp((char *)s + 0x14, w, sizeof w);
}
static uint8_t my_bypass(void *self, void *key, const void *mi) {
    if (is_equipment_key(key)) {
        if (++bypass_n <= 5) H->log(ME, "weapon requirement skipped (#%d)", bypass_n);
        return 1;
    }
    return orig_bypass(self, key, mi);
}

/* ------------------------------------------------------------------ 2. party size */
static void *k_campaign, *f_party;
static void (*fs_get)(void *field, void *out), (*fs_set)(void *field, void *in);
static void (*orig_campaign_init)(const void *mi);
static int party_stock = 4, party_depth;

static int party_get(void) { int32_t v = 0; fs_get(f_party, &v); return v; }
static void party_put(int v) { int32_t x = v; fs_set(f_party, &x); }
/* a reader built for the stock count runs: the stock value until it returns (main thread; nests) */
static void party_stock_enter(void) { if (party_depth++ == 0) party_put(party_stock); }
static void party_stock_leave(void) { if (--party_depth == 0) party_put(s_max_units); }

static void my_campaign_init(const void *mi) {
    orig_campaign_init(mi);
    int v = party_get();
    if (v > 0 && v != s_max_units) party_stock = v;
    party_put(s_max_units);
    H->log(ME, "Campaign.Init: PlayerPartyMax %d (table) -> %d", v, s_max_units);
}

/* wrappers: the same body for each window method, by argument count */
#define WRAP0(name) static void (*o_##name)(void *, const void *); \
    static void w_##name(void *a, const void *mi) { party_stock_enter(); o_##name(a, mi); party_stock_leave(); }
#define WRAP1(name, T) static void (*o_##name)(void *, T, const void *); \
    static void w_##name(void *a, T b, const void *mi) { party_stock_enter(); o_##name(a, b, mi); party_stock_leave(); }
WRAP0(inv_set_unit_ui)
WRAP1(status_refresh, void *)
WRAP0(status_clear)
WRAP1(skill_refresh, void *)
WRAP1(summary_refresh, int32_t)
/* Calculate returns ExpeditionPlanSummaryData, a 24-byte struct: returned through memory (arm64 x8, Win64 hidden
   first argument), so the wrapper returns the same struct type and the compiler passes the result address on */
typedef struct { void *rows, *risk_messages, *risks; } plan_summary;
static plan_summary (*o_risk_calc)(void *, void *, void *, void *, const void *);
static plan_summary w_risk_calc(void *self, void *sel, void *all, void *slots, const void *mi) {
    party_stock_enter(); plan_summary r = o_risk_calc(self, sel, all, slots, mi); party_stock_leave(); return r;
}

static void *nested_class(const char *ns, const char *outer, const char *name) {
    void *(*nested)(void *, void **) = H->il2cpp_fn("il2cpp_class_get_nested_types");
    const char *(*cname)(void *) = H->il2cpp_fn("il2cpp_class_get_name");
    void *o = H->find_class(ns, outer), *it = NULL, *k;
    if (!o || !nested || !cname) return NULL;
    while ((k = nested(o, &it))) { const char *n = cname(k); if (n && !strcmp(n, name)) return k; }
    return NULL;
}

static int hook_named(const char *ns, const char *cls, const char *m, int argc, void *repl, void **orig) {
    int r = H->hook_method(ME, ns, cls, m, argc, repl, orig);
    if (r) H->log(ME, "hook %s.%s.%s(%d args) failed (%d)", ns, cls, m, argc, r);
    return r;
}

#include "zetu_ui.c"

static int party_init(void) {
    void *(*field)(void *, const char *) = H->il2cpp_fn("il2cpp_class_get_field_from_name");
    void (*cinit)(void *) = H->il2cpp_fn("il2cpp_runtime_class_init");
    *(void **)&fs_get = H->il2cpp_fn("il2cpp_field_static_get_value");
    *(void **)&fs_set = H->il2cpp_fn("il2cpp_field_static_set_value");
    k_campaign = nested_class("Refactor", "SystemVar", "Campaign");
    f_party = k_campaign && field ? field(k_campaign, "<PlayerPartyMax>k__BackingField") : NULL;
    void *mi = k_campaign ? H->find_method(k_campaign, "Init", 0) : NULL;
    void *init = mi ? H->method_address(mi) : NULL;
    if (!f_party || !fs_get || !fs_set || !cinit || !init) {
        H->log(ME, "SystemVar.Campaign.PlayerPartyMax not found (class %p field %p Init %p)", k_campaign, f_party, init);
        return 10;
    }
    cinit(k_campaign);
    if (H->hook(ME, init, (void *)my_campaign_init, (void **)&orig_campaign_init)) return 11;
    const char *ui = "Refactor.UI";
    if (hook_named(ui, "EntityInventoryPresenter", "SetUnitUI", 0, (void *)w_inv_set_unit_ui, (void **)&o_inv_set_unit_ui)
        || hook_named(ui, "EntityStatusUIPresenter", "RefreshSelectedEntityUI", 1, (void *)w_status_refresh, (void **)&o_status_refresh)
        || hook_named(ui, "EntityStatusUIPresenter", "ClearShowingUnits", 0, (void *)w_status_clear, (void **)&o_status_clear)
        || hook_named(ui, "EntitySkillUIPresenter", "RefreshSelectedEntityUI", 1, (void *)w_skill_refresh, (void **)&o_skill_refresh)
        || hook_named(ui, "EntitySummaryUIPresenter", "RefreshSummary", 1, (void *)w_summary_refresh, (void **)&o_summary_refresh)
        || hook_named(ui, "ExpeditionPlanSummaryCalculator", "Calculate", 3, (void *)w_risk_calc, (void **)&o_risk_calc))
        return 12;
    int v = party_get();                               /* Campaign.Init may have run already (tables loaded) */
    if (v > 0) { if (v != s_max_units) party_stock = v; party_put(s_max_units); }
    H->log(ME, "units in the dungeon: %d (game value now %d)", s_max_units, v);
    return zetu_ui_init();
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    if (host->size < offsetof(DSLHost, data_dir) + sizeof host->data_dir) { host->log(self, "loader too old (needs API 3)"); return 1; }
    s_free_skills = dsl_get_bool(host, self, "skills_no_weapon", 1);
    s_max_units = dsl_get_int(host, self, "max_units", 4);
    if (s_max_units < 4) s_max_units = 4;
    if (s_max_units > 8) s_max_units = 8;
    host->log(self, "settings: skills without weapon requirement %s, max units in the dungeon %d",
              s_free_skills ? "on" : "off", s_max_units);
    if (s_free_skills && hook_named("Refactor", "SkillReader", "HasEquipBypass", 1, (void *)my_bypass, (void **)&orig_bypass)) return 2;
    if (s_max_units != 4) { int r = party_init(); if (r) return r; }   /* 4 = the stock table value: nothing to change */
    return 0;
}
