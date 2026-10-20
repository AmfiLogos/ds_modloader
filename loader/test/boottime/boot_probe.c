// boot_probe.c - test only, never shipped. Start-up marker for boot_compare.py, the same in every case (stock or loader):
// on the main thread, between frames (a main run loop observer, at most every 50 ms), it finds the game's MainFlow object
// through the il2cpp API that GameAssembly exports (Object.FindFirstObjectByType, until found), then reads two fields, and
// writes to $BOOTPROBE_OUT: the process start time (kernel), then each change of MainFlow._currentScene / _bootReady with
// the wall time. Title ready = scene 1 (Title) and _bootReady 1 (the game's own flag that the title is usable).
// It hooks nothing in GameAssembly. Built with -DHIDE (stock cases, no dsload) it also keeps the copy off the screen
// (every NSWindow invisible at creation, the app never activates; same calls as ds_multiplayer src/rig_win.c) and never
// opens Steam (SteamAPI_RestartAppIfNecessary stub, system("...open...") refused).
// Build: clang -arch arm64 -O2 [-DHIDE] -dynamiclib -framework AppKit -lobjc -o boot_probe.dylib boot_probe.c && codesign -f -s - boot_probe.dylib
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/time.h>

static double now(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec / 1e6; }
static FILE *out;
static void emit(const char *what, int a, int b) { if (out) { fprintf(out, "%.3f %s %d %d\n", now(), what, a, b); fflush(out); } }

/* ---- il2cpp (resolved once GameAssembly is loaded) */
typedef void *P;
static P (*domain_get)(void); static P *(*get_assemblies)(P, size_t *); static P (*asm_image)(P);
static P (*class_from_name)(P, const char *, const char *); static P (*field_from_name)(P, const char *); static size_t (*field_offset)(P);
static P (*class_type)(P); static P (*type_object)(P); static P (*invoke)(P, P, void **, P *);
static P (*get_methods)(P, void **); static const char *(*method_name)(P); static uint32_t (*param_count)(P); static P (*method_param)(P, uint32_t);
static char *(*type_name)(P); static int (*is_generic)(P);
static P mf_class, find_m, type_obj, mf; static long off_scene = -1, off_boot = -1; static int last_scene = -99, last_boot = -1, gave_up;
static P cls(const char *ns, const char *n) {
    size_t k = 0; P *as = get_assemblies(domain_get(), &k);
    for (size_t i = 0; i < k; i++) { P c = class_from_name(asm_image(as[i]), ns, n); if (c) return c; }
    return NULL;
}
static void *ga;                                           /* GameAssembly.dylib (dlopen'd RTLD_LOCAL by Unity: not in RTLD_DEFAULT) */
static void *sym(const char *n) { return dlsym(ga, n); }
static int il2cpp_ready(void) {
    if (invoke) return 1;
    if (!ga) {
        for (uint32_t i = 0, n = _dyld_image_count(); i < n; i++) {
            const char *nm = _dyld_get_image_name(i); size_t l = nm ? strlen(nm) : 0;
            if (l > 19 && !strcmp(nm + l - 19, "/GameAssembly.dylib")) { ga = dlopen(nm, RTLD_NOLOAD | RTLD_LAZY); break; }
        }
        if (!ga) return 0;
        emit("gameassembly", 0, 0);
    }
    if (!(domain_get = sym("il2cpp_domain_get"))) return 0;
    get_assemblies = sym("il2cpp_domain_get_assemblies"); asm_image = sym("il2cpp_assembly_get_image");
    class_from_name = sym("il2cpp_class_from_name"); field_from_name = sym("il2cpp_class_get_field_from_name");
    field_offset = sym("il2cpp_field_get_offset"); class_type = sym("il2cpp_class_get_type");
    type_object = sym("il2cpp_type_get_object"); get_methods = sym("il2cpp_class_get_methods"); method_name = sym("il2cpp_method_get_name");
    param_count = sym("il2cpp_method_get_param_count"); method_param = sym("il2cpp_method_get_param"); type_name = sym("il2cpp_type_get_name");
    is_generic = sym("il2cpp_method_is_generic");
    P inv = sym("il2cpp_runtime_invoke");
    if (!get_assemblies || !asm_image || !class_from_name || !field_from_name || !field_offset || !class_type || !type_object || !get_methods || !method_name || !param_count || !method_param || !type_name || !is_generic || !inv) return 0;
    invoke = inv; return 1;
}
static void tick(CFRunLoopObserverRef o, CFRunLoopActivity act, void *ud) {
    (void)o; (void)act; (void)ud;
    static double last; static int ticks; double t = now(); if (t - last < 0.05 || gave_up) return; last = t;
    if (!ticks++) emit("first-tick", 0, 0);
    if (!il2cpp_ready()) return;
    if (!mf_class) {                                       /* the game's assemblies are in once Assembly-CSharp has MainFlow */
        size_t k = 0; if (!get_assemblies(domain_get(), &k) || k < 10) return;
        if (!(mf_class = cls("Refactor.Main", "MainFlow"))) return;
        P f1 = field_from_name(mf_class, "_currentScene"), f2 = field_from_name(mf_class, "_bootReady");
        P uo = cls("UnityEngine", "Object");
        void *it = NULL; P m;                              /* FindFirstObjectByType(System.Type): not the generic <T>(FindObjectsInactive) */
        while (uo && !find_m && (m = get_methods(uo, &it)))
            if (!strcmp(method_name(m), "FindFirstObjectByType") && param_count(m) == 1 && !is_generic(m)) {
                char *tn = type_name(method_param(m, 0)); if (tn && !strcmp(tn, "System.Type")) find_m = m;
            }
        if (!f1 || !f2 || !find_m) { emit("probe-missing", !!f1 + 2 * !!f2, !!find_m); gave_up = 1; return; }
        off_scene = (long)field_offset(f1); off_boot = (long)field_offset(f2);
        type_obj = type_object(class_type(mf_class)); emit("il2cpp-ready", (int)off_scene, (int)off_boot);
    }
    if (!mf) { P exc = NULL; void *args[1] = { type_obj }; P r = invoke(find_m, NULL, args, &exc);
        static int ne; if (exc && ne++ < 3) emit("find-exception", ne, 0);
        if (exc || !r) return; mf = r; emit("mainflow", 0, 0); }
    int sc = *(int32_t *)((char *)mf + off_scene), bo = *(uint8_t *)((char *)mf + off_boot);
    if (sc != last_scene || bo != last_boot) { emit("state", sc, bo); last_scene = sc; last_boot = bo; }
}

#ifdef HIDE
typedef id (*init4_t)(id, SEL, CGRect, unsigned long, unsigned long, BOOL);
typedef id (*init5_t)(id, SEL, CGRect, unsigned long, unsigned long, BOOL, id);
static init4_t o_init4; static init5_t o_init5; static IMP o_alpha, o_policy, o_finish;
#define MSG(ret, ...) ((ret (*)(__VA_ARGS__))objc_msgSend)
static void hide(id w) {
    if (!w) return;
    ((void (*)(id, SEL, double))o_alpha)(w, sel_registerName("setAlphaValue:"), 0.0);
    MSG(void, id, SEL, BOOL)(w, sel_registerName("setIgnoresMouseEvents:"), YES);
    MSG(void, id, SEL, BOOL)(w, sel_registerName("setHasShadow:"), NO);
    MSG(void, id, SEL, long)(w, sel_registerName("setLevel:"), (long)-2147483000);
    MSG(void, id, SEL, unsigned long)(w, sel_registerName("setCollectionBehavior:"), (1u << 3) | (1u << 6) | (1u << 4));
}
static id my_init4(id s, SEL c, CGRect r, unsigned long st, unsigned long b, BOOL d) { id w = o_init4(s, c, r, st, b, d); hide(w); return w; }
static id my_init5(id s, SEL c, CGRect r, unsigned long st, unsigned long b, BOOL d, id sc) { id w = o_init5(s, c, r, st, b, d, sc); hide(w); return w; }
static void my_alpha(id s, SEL c, double a) { (void)a; ((void (*)(id, SEL, double))o_alpha)(s, c, 0.0); }
static void noop0(id s, SEL c) { (void)s; (void)c; }
static void noopb(id s, SEL c, BOOL a) { (void)s; (void)c; (void)a; }
static void noop1(id s, SEL c, id a) { (void)s; (void)c; (void)a; }
static BOOL no0(id s, SEL c) { (void)s; (void)c; return NO; }
static BOOL my_policy(id s, SEL c, long p) { (void)p; return ((BOOL (*)(id, SEL, long))o_policy)(s, c, 2); }
static void my_finish(id s, SEL c) { ((BOOL (*)(id, SEL, long))o_policy)(s, sel_registerName("setActivationPolicy:"), 2); ((void (*)(id, SEL))o_finish)(s, c); }
static void swap(const char *cls_, const char *sel, IMP imp, IMP *orig) {
    Class k = objc_getClass(cls_); Method m = k ? class_getInstanceMethod(k, sel_registerName(sel)) : NULL;
    if (m) { IMP o = method_setImplementation(m, imp); if (orig) *orig = o; }
}
static int steam_restart_stub(unsigned int app) { (void)app; return 0; }
static void *my_dlsym(void *h, const char *n) { if (n && !strcmp(n, "SteamAPI_RestartAppIfNecessary")) return (void *)steam_restart_stub; return dlsym(h, n); }
static int my_system(const char *c) { if (c && strstr(c, "open")) return 0; return system(c); }
__attribute__((used, section("__DATA,__interpose"))) static const struct { const void *n, *o; } ip[] = {
    { (const void *)my_dlsym, (const void *)dlsym }, { (const void *)my_system, (const void *)system } };
static void hide_install(void) {
    swap("NSWindow", "initWithContentRect:styleMask:backing:defer:", (IMP)my_init4, (IMP *)&o_init4);
    swap("NSWindow", "initWithContentRect:styleMask:backing:defer:screen:", (IMP)my_init5, (IMP *)&o_init5);
    swap("NSWindow", "setAlphaValue:", (IMP)my_alpha, &o_alpha);
    swap("NSWindow", "makeKeyWindow", (IMP)noop0, NULL);
    swap("NSWindow", "makeMainWindow", (IMP)noop0, NULL);
    swap("NSWindow", "canBecomeKeyWindow", (IMP)no0, NULL);
    swap("NSWindow", "canBecomeMainWindow", (IMP)no0, NULL);
    swap("NSWindow", "toggleFullScreen:", (IMP)noop1, NULL);
    swap("NSApplication", "activateIgnoringOtherApps:", (IMP)noopb, NULL);
    swap("NSApplication", "activate", (IMP)noop0, NULL);
    swap("NSRunningApplication", "activateWithOptions:", (IMP)no0, NULL);
    swap("NSApplication", "setActivationPolicy:", (IMP)my_policy, &o_policy);
    swap("NSApplication", "finishLaunching", (IMP)my_finish, &o_finish);
}
#endif

__attribute__((constructor)) static void probe_init(void) {
#ifdef HIDE
    unsetenv("DYLD_INSERT_LIBRARIES");
    hide_install();
#endif
    const char *p = getenv("BOOTPROBE_OUT"); if (!p || !*p) return;
    out = fopen(p, "a"); unsetenv("BOOTPROBE_OUT");       /* child processes (soft restart prewarm) write nothing */
    struct kinfo_proc kp; size_t len = sizeof kp; int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    if (out && !sysctl(mib, 4, &kp, &len, NULL, 0))
        fprintf(out, "%.3f process-start %d 0\n", kp.kp_proc.p_starttime.tv_sec + kp.kp_proc.p_starttime.tv_usec / 1e6, getpid());
    emit("probe-init", 0, 0);
    CFRunLoopObserverRef o = CFRunLoopObserverCreate(NULL, kCFRunLoopBeforeWaiting | kCFRunLoopAfterWaiting | kCFRunLoopBeforeTimers, true, 0, tick, NULL);
    CFRunLoopAddObserver(CFRunLoopGetMain(), o, kCFRunLoopCommonModes);
}
