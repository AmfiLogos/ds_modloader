// Test only (never shipped): hook chaining in the real game. Chains over the loader's own EventSystem.Update hook
// (the kit's per-frame tick), always calls orig, takes itself off after 600 frames. Break caught: a chained call into
// a real game function crashes, or the loader's tick under it stops (control channel / UI kit dead) after the unhook.
#include "dsl_sdk.h"
DSL_PLUGIN();
static const DSLHost *H; static DSLPlugin *ME;
static void (*o_upd)(void *, const void *); static void *t_upd; static int n;
static void my_upd(void *s, const void *mi) {
    o_upd(s, mi);
    if (++n == 600) H->log(ME, "chainprobe: %d frames through the chain, unhook=%d", n, H->unhook(ME, t_upd));
}
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me;
    t_upd = h->find_method_address ? h->find_method_address("UnityEngine.EventSystems", "EventSystem", "Update", 0) : 0;
    int r = h->hook_method(me, "UnityEngine.EventSystems", "EventSystem", "Update", 0, (void *)my_upd, (void **)&o_upd);
    h->log(me, "chainprobe: hook=%d target=%p", r, t_upd);
    return 0;
}
