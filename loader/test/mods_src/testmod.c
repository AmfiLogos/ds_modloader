// Test modules for the host test; MODE picks the behaviour.
#include "dsl_sdk.h"
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
DSL_PLUGIN();
static const DSLHost *H; static DSLPlugin *ME;
static void (*o_update)(void *, const void *), (*o_tick)(void *, const void *);
static int n_update, n_tick;
static void my_update(void *s, const void *mi) { n_update++; o_update(s, mi); if (n_update == 2) H->log(ME, "update hook ran %d times", n_update); }
static void my_tick(void *s, const void *mi) { n_tick++; H->log(ME, "tick hook ran"); o_tick(s, mi); }
static void my_nothing(void *s, const void *mi) { H->log(ME, "BAD HOOK RAN"); }

#if MODE == 1      /* early-mod: all three phases */
DSL_EXPORT int dsl_module_load(const DSLHost *h, DSLPlugin *me) { H = h; ME = me; h->log(me, "load: ga_base=%#lx build=%s", (unsigned long)h->ga_base, h->game_build); return 0; }
DSL_EXPORT int dsl_module_early(const DSLHost *h, DSLPlugin *me) {
    void *u = dlsym(RTLD_DEFAULT, "MainFlow_Update");
    int r = h->hook_rva(me, (uintptr_t)u - h->ga_base, (void *)my_update, (void **)&o_update, "MainFlow.Update");
    h->log(me, "early: ready=%d hook_rva=%d", h->il2cpp_ready(), r);
    return r;
}
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    int a = h->hook_method(me, "Refactor.Runtime", "DungeonBehaviorSimulator", "Tick", 0, (void *)my_tick, (void **)&o_tick);
    void *o; int b = h->hook_method(me, "Refactor.Runtime", "DungeonBehaviorSimulator", "ForeignTarget", 0, (void *)my_nothing, &o);
    h->log(me, "init: tick=%d foreign=%d loaded(floor-logger)=%d", a, b, h->module_loaded("floor-logger"));
    return a;
}
#elif MODE == 2    /* clash: chains over a function floor-logger hooked (never calls orig), then fails: the loader takes it off */
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me; void *o;
    int a = h->hook_method(me, "Refactor.Runtime", "DungeonBehaviorSimulator", "OnEnterSafeFloor", 0, (void *)my_nothing, &o);
    h->log(me, "clash hook=%d", a);
    return 5;
}
#elif MODE >= 7 && MODE <= 10   /* hook chaining (T1h): 7 chain-a, 8 chain-m, 9 chain-b on ChainTarget; 10 skipper on ChainTarget2 */
static void (*o_ch)(void *, const void *), (*o_ch2)(void *, const void *);
static void *t_ch; static int n_ch;
#if MODE == 7
#define UP 'A'
#define UNHOOK_AT 3
#elif MODE == 8
#define UP 'M'
#define UNHOOK_AT 2
#else
#define UP 'B'
#define UNHOOK_AT 0
#endif
static void my_ch(void *s, const void *mi) {
    void (*add)(char) = (void (*)(char))dlsym(RTLD_DEFAULT, "fake_trace_add");
    add(UP); o_ch(s, mi); add(UP + 32);
    if (++n_ch == UNHOOK_AT) H->log(ME, "unhook after call %d: %d", n_ch, H->unhook(ME, t_ch));
}
static int n_ch2;
static void my_ch2(void *s, const void *mi) { n_ch2++; if (MODE != 10) o_ch2(s, mi); }   /* the skipper never calls orig */
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me; t_ch = dlsym(RTLD_DEFAULT, "Chain_Target");
    int r = 0, again = 0, fold = 0;
    if (MODE != 10) r = h->hook_method(me, "Refactor.Runtime", "DungeonBehaviorSimulator", "ChainTarget", 0, (void *)my_ch, (void **)&o_ch);
    if (MODE == 7 || MODE == 10) r |= h->hook_method(me, "Refactor.Runtime", "DungeonBehaviorSimulator", "ChainTarget2", 0, (void *)my_ch2, (void **)&o_ch2);
    if (MODE == 9) {                                     /* refused: the same module twice, another method on the same body */
        void *o; again = h->hook_method(me, "Refactor.Runtime", "DungeonBehaviorSimulator", "ChainTarget", 0, (void *)my_nothing, &o);
        fold = h->hook_method(me, "Refactor.Runtime", "DungeonBehaviorSimulator", "FoldedTarget", 0, (void *)my_nothing, &o);
    }
    h->log(me, "chain hooks=%d again=%d folded=%d", r, again, fold);
    return r;
}
#elif MODE == 5    /* patcher: patch_rva ok, wrong bytes refused, overlap with a hook refused */
DSL_EXPORT int dsl_module_early(const DSLHost *h, DSLPlugin *me) {
    uintptr_t t = (uintptr_t)dlsym(RTLD_DEFAULT, "Patch_Target") - h->ga_base;
    uint32_t old = 0x52800020, neu = 0x52800040, wrong = 0x52800060;               /* mov w0,#1 -> mov w0,#2 */
    int a = h->patch_rva(me, t + 8, &wrong, &neu, 4, "wrong bytes");
    int b = h->patch_rva(me, t, &old, &neu, 4, "mov w0 1->2");
    void *u = dlsym(RTLD_DEFAULT, "MainFlow_Update");
    uint8_t cur[4]; memcpy(cur, u, 4);
    int c = h->patch_rva(me, (uintptr_t)u - h->ga_base, cur, cur, 4, "over a hook");
    h->log(me, "patches: wrong=%d ok=%d over_hook=%d", a, b, c);
    return b;
}
#elif MODE == 6    /* patch then fail: the loader must put the bytes back */
DSL_EXPORT int dsl_module_early(const DSLHost *h, DSLPlugin *me) {
    uintptr_t t = (uintptr_t)dlsym(RTLD_DEFAULT, "Patch_Target") - h->ga_base;
    uint32_t old = 0x528000e0, neu = 0x52800100;                                   /* second mov w0,#7 -> #8 */
    int r = h->patch_rva(me, t + 8, &old, &neu, 4, "temp"); h->log(me, "temp patch=%d, now failing", r); return 9;
}
DSL_EXPORT int dsl_world_check_skip(void) { return 0; }
#else              /* plain: loads, no hooks */
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) { h->log(me, "plain module %s up (table x=%p)", h->plugin_id(me), h->table(me, "x")); return 0; }
#endif
