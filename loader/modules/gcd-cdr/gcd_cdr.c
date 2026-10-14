// GCD uses cooldown reduction, as a dsload code module (runtime hook, GameAssembly file unchanged).
// Same rule as the owner's byte patch ~/ds_skill_truth/patch_gcd_cdr.py (memory DungeonSettlers_GcdCdrPatch):
// SkillComponent.SetGlobalCooldown() starts the global cooldown with _globalCooldownValue (combat table, 5 s);
// stock never reads stat 28 (CooldownReduction). Here: value / (1 + cdr / 100), then the game's own
// SetGlobalCooldown(float) applies it (it keeps the larger of the running and the new cooldown, like stock).
// Names by il2cpp lookup; the generic GetComponent<StatusReader> (no name lookup possible) from the per-build table.
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdatomic.h>

DSL_PLUGIN();

static const DSLHost *H;
static DSLPlugin *ME;
static void (*orig_set_gcd)(void *self, const void *mi);
static void (*set_gcd_f)(void *self, float duration, const void *mi);
static void *(*get_component)(void *entity, const void *mi);
static float (*get_value)(void *reader, int32_t stat, const void *mi);
static void (*init_md)(void *slot_addr);
static void **mi_slot;
static int32_t off_owner = -1, off_value = -1;
static _Atomic int calls;
#define STAT_COOLDOWN_REDUCTION 28

static void my_set_gcd(void *self, const void *mi) {
    if (!self) { orig_set_gcd(self, mi); return; }
    float gcd = *(float *)((char *)self + off_value), cdr = 0;
    void *owner = *(void **)((char *)self + off_owner);
    if (owner) {
        init_md(mi_slot);                                  /* resolves the MethodInfo once (no-op afterwards) */
        void *reader = get_component(owner, *mi_slot);
        if (reader) cdr = get_value(reader, STAT_COOLDOWN_REDUCTION, NULL);
    }
    float div = 1.0f + cdr * 0.01f, out = div > 0 ? gcd / div : gcd;
    set_gcd_f(self, out, NULL);
    int n = ++calls;
    if (n <= 10 || n % 500 == 0) H->log(ME, "global cooldown #%d: %.3f s, cooldown reduction %.1f -> %.3f s", n, gcd, cdr, out);
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    if (host->size < sizeof(DSLHost)) { host->log(self, "loader too old (needs API 2)"); return 1; }
    const char *ns = "Refactor.Component", *cls = "SkillComponent";
    void *k = H->find_class(ns, cls);
    if (!k) { H->log(ME, "class %s.%s not found", ns, cls); return 2; }
    off_owner = H->field_offset(k, "_owner");
    off_value = H->field_offset(k, "_globalCooldownValue");
    void *mf = H->find_method(k, "SetGlobalCooldown", 1);
    set_gcd_f = mf ? H->method_address(mf) : NULL;
    get_value = H->find_method_address("Refactor", "StatusReader", "GetValue", 1);
    get_component = H->table(ME, "EntityExtensions.GetComponent<object>");
    mi_slot = H->table(ME, "slot.GetComponent<StatusReader>");
    init_md = H->table(ME, "il2cpp_codegen_initialize_runtime_metadata");
    if (off_owner < 0 || off_value < 0 || !set_gcd_f || !get_value || !get_component || !mi_slot || !init_md) {
        H->log(ME, "missing: owner %d value %d SetGlobalCooldown(float) %p GetValue %p table %p %p %p",
               off_owner, off_value, (void *)set_gcd_f, (void *)get_value, (void *)get_component, (void *)mi_slot, (void *)init_md);
        return 3;
    }
    if (H->hook_method(ME, ns, cls, "SetGlobalCooldown", 0, (void *)my_set_gcd, (void **)&orig_set_gcd)) return 4;
    H->log(ME, "global cooldown now uses cooldown reduction (fields _owner +%#x, _globalCooldownValue +%#x)", off_owner, off_value);
    return 0;
}
