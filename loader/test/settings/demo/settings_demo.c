// TEST ONLY: a module with settings (mod.json "settings"), for the Settings page rig proof (../ui_settings_test.py).
// Logs the values it reads at load and when a local setting changes: the proof that the SDK calls give what the page set.
#include "dsl_sdk.h"
DSL_PLUGIN();
static const DSLHost *H; static DSLPlugin *S;
static void say(const char *when) {
    H->log(S, "settings-demo %s: gold_mult=%d hard_rule=%d difficulty=%s show_bars=%d", when, dsl_get_int(H, S, "gold_mult", -1),
           dsl_get_bool(H, S, "hard_rule", -1), dsl_get_choice(H, S, "difficulty", "?"), dsl_get_bool(H, S, "show_bars", -1));
}
static void changed(void *ud, const char *id) { (void)ud; H->log(S, "settings-demo: local setting %s changed", id); say("now"); }
DSL_EXPORT int dsl_module_load(const DSLHost *h, DSLPlugin *self) {
    H = h; S = self; say("at load");
    dsl_on_setting_change(h, self, changed, NULL);
    return 0;
}
