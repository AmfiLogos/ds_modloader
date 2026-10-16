// Mac OS part of the soft restart (src/dsl_soft_os.h).
// B: from its first NSWindow on, every window is invisible (alpha 0, lowest level, no mouse, never key/main), the app
// never activates and has no Dock icon (Accessory policy). At "go": window on A's frame, normal level, alpha 1, front,
// Regular policy, activate. A: yields activation to B, orders its windows out.
#include "dsl_internal.h"
#include "dsl_soft_os.h"
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <dispatch/dispatch.h>
#include <errno.h>

extern char **environ;
#define MSG(ret, ...) ((ret (*)(__VA_ARGS__))objc_msgSend)
#define SEL_(s) sel_registerName(s)

static char **env0; static char exe0[1024], cwd0[1024];
static char **argv0; static int argc0;
static int hidden, stage_on; static double stage_x, stage_y;
static pid_t b_pid; static int b_ls;                 /* b_ls: B started by LaunchServices (not our child) */

double dsl_sos_proc_start(void) {
    struct kinfo_proc kp; size_t n = sizeof kp; int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    if (sysctl(mib, 4, &kp, &n, NULL, 0) == 0) return kp.kp_proc.p_starttime.tv_sec + kp.kp_proc.p_starttime.tv_usec / 1e6;
    struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec / 1e6;
}

/* constructor time, BEFORE dsload edits DYLD_INSERT_LIBRARIES */
void dsl_sos_snapshot(void) {
    int n = 0; while (environ[n]) n++;
    env0 = calloc((size_t)n + 1, sizeof *env0);
    for (int i = 0; i < n; i++) env0[i] = strdup(environ[i]);
    uint32_t sz = sizeof exe0; if (_NSGetExecutablePath(exe0, &sz)) exe0[0] = 0;
    if (!getcwd(cwd0, sizeof cwd0)) cwd0[0] = 0;
    argc0 = *_NSGetArgc(); char **av = *_NSGetArgv();
    argv0 = calloc((size_t)argc0 + 1, sizeof *argv0);
    for (int i = 0; i < argc0; i++) argv0[i] = strdup(av[i]);
}
void dsl_sos_unsetenv(const char *name) { unsetenv(name); }

/* ------------------------------------------------------------------ windows (objc runtime only) */
typedef id (*init4_t)(id, SEL, CGRect, unsigned long, unsigned long, BOOL);
typedef id (*init5_t)(id, SEL, CGRect, unsigned long, unsigned long, BOOL, id);
static init4_t o_init4; static init5_t o_init5;
static IMP o_alpha, o_mkkey, o_mkmain, o_cankey, o_canmain, o_act1, o_act0, o_ract, o_fs, o_setframe, o_setorigin, o_constrain;
#define LEVEL_HIDDEN (-2147483000L)
static void stage_window(id w);
static void hide_window(id w) {
    if (!w) return;
    ((void (*)(id, SEL, double))o_alpha)(w, SEL_("setAlphaValue:"), 0.0);
    MSG(void, id, SEL, BOOL)(w, SEL_("setIgnoresMouseEvents:"), YES);
    MSG(void, id, SEL, BOOL)(w, SEL_("setHasShadow:"), NO);
    MSG(void, id, SEL, long)(w, SEL_("setLevel:"), LEVEL_HIDDEN);
    /* Transient | IgnoresCycle | Stationary: not in Mission Control / Cmd-` while hidden */
    MSG(void, id, SEL, unsigned long)(w, SEL_("setCollectionBehavior:"), (1u << 3) | (1u << 6) | (1u << 4));
}
static void show_window(id w) {
    MSG(void, id, SEL, unsigned long)(w, SEL_("setCollectionBehavior:"), 0);
    MSG(void, id, SEL, long)(w, SEL_("setLevel:"), 0L);
    MSG(void, id, SEL, BOOL)(w, SEL_("setIgnoresMouseEvents:"), NO);
    MSG(void, id, SEL, BOOL)(w, SEL_("setHasShadow:"), YES);
    ((void (*)(id, SEL, double))o_alpha)(w, SEL_("setAlphaValue:"), 1.0);
}
static int no_activate_g;
static id my_init4(id self, SEL s, CGRect r, unsigned long st, unsigned long b, BOOL d) {
    id w = o_init4(self, s, r, st, b, d); if (hidden) hide_window(w); if (stage_on) stage_window(w); return w;
}
static id my_init5(id self, SEL s, CGRect r, unsigned long st, unsigned long b, BOOL d, id sc) {
    id w = o_init5(self, s, r, st, b, d, sc); if (hidden) hide_window(w); if (stage_on) stage_window(w); return w;
}
static void my_alpha(id self, SEL s, double a) { ((void (*)(id, SEL, double))o_alpha)(self, s, hidden ? 0.0 : a); }
static void my_mkkey(id self, SEL s) { if (!hidden) ((void (*)(id, SEL))o_mkkey)(self, s); }
static void my_mkmain(id self, SEL s) { if (!hidden) ((void (*)(id, SEL))o_mkmain)(self, s); }
static BOOL my_cankey(id self, SEL s) { return hidden ? NO : ((BOOL (*)(id, SEL))o_cankey)(self, s); }
static BOOL my_canmain(id self, SEL s) { return hidden ? NO : ((BOOL (*)(id, SEL))o_canmain)(self, s); }
static void my_act1(id self, SEL s, BOOL a) { if (!hidden && !no_activate_g) ((void (*)(id, SEL, BOOL))o_act1)(self, s, a); }
static void my_act0(id self, SEL s) { if (!hidden && !no_activate_g) ((void (*)(id, SEL))o_act0)(self, s); }
static BOOL my_ract(id self, SEL s, unsigned long o) { return (!hidden && !no_activate_g) ? ((BOOL (*)(id, SEL, unsigned long))o_ract)(self, s, o) : NO; }
static void my_fs(id self, SEL s, id a) { if (!hidden) ((void (*)(id, SEL, id))o_fs)(self, s, a); }
/* test stage: every window keeps its size but sits at (stage_x, stage_y), far off every display */
static void my_setframe(id self, SEL s, CGRect r, BOOL d) { if (stage_on) { r.origin.x = stage_x; r.origin.y = stage_y; } ((void (*)(id, SEL, CGRect, BOOL))o_setframe)(self, s, r, d); }
static void my_setorigin(id self, SEL s, CGPoint p) { if (stage_on) { p.x = stage_x; p.y = stage_y; } ((void (*)(id, SEL, CGPoint))o_setorigin)(self, s, p); }
static CGRect my_constrain(id self, SEL s, CGRect r, id sc) { return stage_on ? r : ((CGRect (*)(id, SEL, CGRect, id))o_constrain)(self, s, r, sc); }
static void stage_window(id w) {
    CGRect f = MSG(CGRect, id, SEL)(w, SEL_("frame"));
    f.origin.x = stage_x; f.origin.y = stage_y;
    ((void (*)(id, SEL, CGRect, BOOL))o_setframe)(w, SEL_("setFrame:display:"), f, NO);
}
static void swap_m(const char *cls, const char *sel, IMP imp, IMP *orig, int cls_method) {
    Class c = objc_getClass(cls); if (!c) { dsl_log("soft: class %s missing", cls); return; }
    Method m = cls_method ? class_getClassMethod(c, SEL_(sel)) : class_getInstanceMethod(c, SEL_(sel));
    if (!m) { dsl_log("soft: %s %s missing", cls, sel); return; }
    IMP o = method_setImplementation(m, imp); if (orig) *orig = o;
}
static id app(void) { return MSG(id, Class, SEL)(objc_getClass("NSApplication"), SEL_("sharedApplication")); }
static id windows(unsigned long *n) {
    id a = app(); if (!a) { *n = 0; return NULL; }
    id ws = MSG(id, id, SEL)(a, SEL_("windows")); *n = ws ? MSG(unsigned long, id, SEL)(ws, SEL_("count")) : 0; return ws;
}
static id win_at(id ws, unsigned long i) { return MSG(id, id, SEL, unsigned long)(ws, SEL_("objectAtIndex:"), i); }
/* the game window = the largest window of the app */
static id game_window(void) {
    unsigned long n; id ws = windows(&n), best = NULL; double ba = 0;
    for (unsigned long i = 0; i < n; i++) {
        id w = win_at(ws, i); CGRect f = MSG(CGRect, id, SEL)(w, SEL_("frame"));
        if (f.size.width * f.size.height > ba) { ba = f.size.width * f.size.height; best = w; }
    }
    return best;
}
static void set_policy(long p) { id a = app(); if (a) MSG(BOOL, id, SEL, long)(a, SEL_("setActivationPolicy:"), p); }   /* 0 Regular, 1 Accessory */
static long get_policy(void) { id a = app(); return a ? MSG(long, id, SEL)(a, SEL_("activationPolicy")) : -1; }

/* NSApplication init in B: Accessory before the app finishes launching (no Dock icon, no menu bar) */
static IMP o_finish;
static void my_finish(id self, SEL s) {
    if (hidden) { MSG(BOOL, id, SEL, long)(self, SEL_("setActivationPolicy:"), 1L); dsl_log("soft: B: activation policy Accessory before finishLaunching"); }
    ((void (*)(id, SEL))o_finish)(self, s);
}

void dsl_sos_windows_install(int hide, int stage, double sx, double sy) {
    hidden = hide; stage_on = stage; stage_x = sx; stage_y = sy;
    no_activate_g = getenv("DSL_SOFT_NOACTIVATE") && *getenv("DSL_SOFT_NOACTIVATE") == '1';
    if (hide) {                                      /* register with LaunchServices as a no-Dock app before AppKit checks in */
        typedef struct { uint32_t hi, lo; } PSN;
        int (*tpt)(const PSN *, uint32_t) = (int (*)(const PSN *, uint32_t))dlsym(RTLD_DEFAULT, "TransformProcessType");
        if (!tpt) { void *h = dlopen("/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices", RTLD_LAZY); if (h) tpt = (int (*)(const PSN *, uint32_t))dlsym(h, "TransformProcessType"); }
        PSN me = { 0, 2 };                           /* kCurrentProcess */
        int e = tpt ? tpt(&me, 4) : -1;              /* kProcessTransformToUIElementApplication */
        dsl_log("soft: B registered as UIElement before AppKit (TransformProcessType %d)", e);
    }
    if (!hide && !stage) return;
    swap_m("NSWindow", "initWithContentRect:styleMask:backing:defer:", (IMP)my_init4, (IMP *)&o_init4, 0);
    swap_m("NSWindow", "initWithContentRect:styleMask:backing:defer:screen:", (IMP)my_init5, (IMP *)&o_init5, 0);
    swap_m("NSWindow", "setAlphaValue:", (IMP)my_alpha, &o_alpha, 0);
    swap_m("NSWindow", "makeKeyWindow", (IMP)my_mkkey, &o_mkkey, 0);
    swap_m("NSWindow", "makeMainWindow", (IMP)my_mkmain, &o_mkmain, 0);
    swap_m("NSWindow", "canBecomeKeyWindow", (IMP)my_cankey, &o_cankey, 0);
    swap_m("NSWindow", "canBecomeMainWindow", (IMP)my_canmain, &o_canmain, 0);
    swap_m("NSWindow", "toggleFullScreen:", (IMP)my_fs, &o_fs, 0);
    swap_m("NSWindow", "setFrame:display:", (IMP)my_setframe, &o_setframe, 0);
    swap_m("NSWindow", "setFrameOrigin:", (IMP)my_setorigin, &o_setorigin, 0);
    swap_m("NSWindow", "constrainFrameRect:toScreen:", (IMP)my_constrain, &o_constrain, 0);
    swap_m("NSApplication", "activateIgnoringOtherApps:", (IMP)my_act1, &o_act1, 0);
    swap_m("NSApplication", "activate", (IMP)my_act0, &o_act0, 0);
    swap_m("NSRunningApplication", "activateWithOptions:", (IMP)my_ract, &o_ract, 0);
    swap_m("NSApplication", "finishLaunching", (IMP)my_finish, &o_finish, 0);
}
void dsl_sos_set_hidden(int on) { hidden = on; }

void dsl_sos_frame_log(FILE *ff) {
    unsigned long n; id ws = windows(&n);
    fprintf(ff, " policy %ld", get_policy());
    for (unsigned long i = 0; i < n; i++) {
        id w = win_at(ws, i); CGRect f = MSG(CGRect, id, SEL)(w, SEL_("frame"));
        fprintf(ff, " | win %ld alpha %.2f vis %d level %ld key %d occl %lu %.0f,%.0f %.0fx%.0f",
                MSG(long, id, SEL)(w, SEL_("windowNumber")), MSG(double, id, SEL)(w, SEL_("alphaValue")),
                (int)MSG(BOOL, id, SEL)(w, SEL_("isVisible")), MSG(long, id, SEL)(w, SEL_("level")),
                (int)MSG(BOOL, id, SEL)(w, SEL_("isKeyWindow")), MSG(unsigned long, id, SEL)(w, SEL_("occlusionState")),
                f.origin.x, f.origin.y, f.size.width, f.size.height);
    }
}

void dsl_sos_stage_check(void) {
    if (!stage_on) return;
    unsigned long n; id ws = windows(&n);
    for (unsigned long i = 0; i < n; i++) {
        id w = win_at(ws, i); CGRect f = MSG(CGRect, id, SEL)(w, SEL_("frame"));
        if ((f.origin.x != stage_x || f.origin.y != stage_y) && MSG(double, id, SEL)(w, SEL_("alphaValue")) > 0) {
            ((void (*)(id, SEL, double))o_alpha)(w, SEL_("setAlphaValue:"), 0.0); stage_window(w);
            if (!hidden) ((void (*)(id, SEL, double))o_alpha)(w, SEL_("setAlphaValue:"), 1.0);
            dsl_log("soft test: window off the stage (%.0f,%.0f): alpha 0, moved back", f.origin.x, f.origin.y);
        }
    }
}

int dsl_sos_game_frame(double fr[4]) {
    id w = game_window(); if (!w) return 0;
    CGRect f = MSG(CGRect, id, SEL)(w, SEL_("frame"));
    fr[0] = f.origin.x; fr[1] = f.origin.y; fr[2] = f.size.width; fr[3] = f.size.height;
    return 1;
}

/* ------------------------------------------------------------------ B process */
static void reap(void);
/* B goes through LaunchServices (a new app instance, its own launchd job), not posix_spawn. A posix_spawn child stays in
   A's launchd job, so the XPC helpers macOS starts for B (AutoFill: com.apple.SafariPlatformSupport.Helper) are booked
   on A's app. When A quits, A's Dock tile then "runs in the background" as long as B lives, and macOS 27 shows
   "App Running in Background: DungeonSettlers ..." about 30 s later (proof: app/stub/NOTES_bg.md).
   LaunchServices starts apps in "/": DSL_SOFT_CWD carries A's start folder (dsl_start_mac.c goes there). */
extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *);
static id nsstr(const char *s) { return MSG(id, Class, SEL, const char *)(objc_getClass("NSString"), SEL_("stringWithUTF8String:"), s); }
static volatile pid_t ls_pid;
static pid_t ls_launch(const char *exe, char **env, char **argv) {
    char appp[1024]; snprintf(appp, sizeof appp, "%s", exe);
    char *m = strstr(appp, "/Contents/MacOS/"); if (!m) return -1;
    *m = 0;
    Class ws_c = objc_getClass("NSWorkspace"), cf_c = objc_getClass("NSWorkspaceOpenConfiguration");   /* macOS 10.15+ */
    if (!ws_c || !cf_c) return -1;
    void *pool = objc_autoreleasePoolPush();
    id cfg = MSG(id, Class, SEL)(cf_c, SEL_("configuration"));
    MSG(void, id, SEL, BOOL)(cfg, SEL_("setCreatesNewApplicationInstance:"), YES);
    MSG(void, id, SEL, BOOL)(cfg, SEL_("setActivates:"), NO);
    MSG(void, id, SEL, BOOL)(cfg, SEL_("setAddsToRecentItems:"), NO);
    MSG(void, id, SEL, BOOL)(cfg, SEL_("setPromptsUserIfNeeded:"), NO);
    id args = MSG(id, Class, SEL)(objc_getClass("NSMutableArray"), SEL_("array"));
    for (int i = 1; argv && argv[i]; i++) MSG(void, id, SEL, id)(args, SEL_("addObject:"), nsstr(argv[i]));
    id envd = MSG(id, Class, SEL)(objc_getClass("NSMutableDictionary"), SEL_("dictionary"));
    for (int i = 0; env[i]; i++) {
        const char *eq = strchr(env[i], '='); if (!eq || eq == env[i]) continue;
        char key[512]; snprintf(key, sizeof key, "%.*s", (int)(eq - env[i]), env[i]);
        id v = nsstr(eq + 1), kk = nsstr(key);
        if (v && kk) MSG(void, id, SEL, id, id)(envd, SEL_("setObject:forKey:"), v, kk);
    }
    MSG(void, id, SEL, id)(cfg, SEL_("setArguments:"), args);
    MSG(void, id, SEL, id)(cfg, SEL_("setEnvironment:"), envd);
    id url = MSG(id, Class, SEL, id)(objc_getClass("NSURL"), SEL_("fileURLWithPath:"), nsstr(appp));
    static dispatch_semaphore_t sem; if (!sem) sem = dispatch_semaphore_create(0);
    while (dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW) == 0) {}          /* a late answer of an earlier call */
    ls_pid = -1;
    void (^done)(id, id) = ^(id ra, id err) {                                 /* AppKit calls it on a concurrent queue */
        ls_pid = ra ? (pid_t)MSG(int, id, SEL)(ra, SEL_("processIdentifier")) : -1;
        if (!ra && err) { id d = MSG(id, id, SEL)(err, SEL_("description")); dsl_log("soft: A: LaunchServices: %s", d ? MSG(const char *, id, SEL)(d, SEL_("UTF8String")) : "?"); }
        dispatch_semaphore_signal(sem);
    };
    id ws = MSG(id, Class, SEL)(ws_c, SEL_("sharedWorkspace"));
    MSG(void, id, SEL, id, id, id)(ws, SEL_("openApplicationAtURL:configuration:completionHandler:"), url, cfg, (id)done);
    long w = dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
    objc_autoreleasePoolPop(pool);
    if (w) { dsl_log("soft: A: LaunchServices gave no answer in 10 s"); return -1; }
    return ls_pid;
}

int dsl_sos_spawn(const char *profile, const char *handoff, const char *env_lines) {
    reap();
    /* environment = the one this process started with, with DSL_PROFILE and DSL_SOFT_HANDOFF replaced */
    int n = 0; while (env0 && env0[n]) n++;
    char **env = calloc((size_t)n + 4, sizeof *env); int k = 0;
    for (int i = 0; i < n; i++) if (strncmp(env0[i], "DSL_PROFILE=", 12) && strncmp(env0[i], "DSL_SOFT_HANDOFF=", 17)) env[k++] = env0[i];
    char e1[1300], e2[1300]; snprintf(e1, sizeof e1, "DSL_PROFILE=%s", profile); snprintf(e2, sizeof e2, "DSL_SOFT_HANDOFF=%s", handoff);
    env[k++] = e1; env[k++] = e2;
    /* caller's overrides "K=V" lines (e.g. DSML_MODSET of the new set) replace the same keys */
    char *ov = strdup(env_lines ? env_lines : ""), *sv = NULL; char **extra = calloc(64, sizeof *extra); int ne = 0;
    for (char *l = strtok_r(ov, "\n", &sv); l && ne < 63; l = strtok_r(NULL, "\n", &sv)) {
        char *eq = strchr(l, '='); if (!eq || eq == l) continue;
        size_t kl = (size_t)(eq - l) + 1;
        for (int i = 0; i < k; i++) if (!strncmp(env[i], l, kl)) { env[i] = env[--k]; i--; }
        extra[ne++] = l;
    }
    env = realloc(env, ((size_t)k + (size_t)ne + 2) * sizeof *env);
    for (int i = 0; i < ne; i++) env[k++] = extra[i];
    char e3[1100]; snprintf(e3, sizeof e3, "DSL_SOFT_CWD=%s", cwd0);
    for (int i = 0; i < k; i++) if (!strncmp(env[i], "DSL_SOFT_CWD=", 13)) { env[i] = env[--k]; i--; }
    if (cwd0[0]) env[k++] = e3;
    env[k] = NULL;
    posix_spawnattr_t at; posix_spawnattr_init(&at);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSID);
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    if (cwd0[0]) posix_spawn_file_actions_addchdir_np(&fa, cwd0);       /* B starts in A's start folder */
    const char *exe = exe0;                                               /* DSL_SOFT_EXE=<path>: the new set's own play copy */
    for (int i = 0; env[i]; i++) if (!strncmp(env[i], "DSL_SOFT_EXE=", 13) && env[i][13]) exe = env[i] + 13;
    pid_t lp = ls_launch(exe, env, argv0);
    if (lp > 0) {
        b_pid = lp; b_ls = 1;
        posix_spawnattr_destroy(&at); posix_spawn_file_actions_destroy(&fa);
        dsl_log("soft: A: B started by LaunchServices, pid %d (%s, profile %s)", b_pid, exe, profile);
        free(env); free(extra); free(ov);
        return 0;
    }
    dsl_log("soft: A: LaunchServices could not start B: posix_spawn instead (macOS may then show \"running in background\")");
    b_ls = 0;
    int r = posix_spawn(&b_pid, exe, &fa, &at, argv0, env);
    posix_spawnattr_destroy(&at); posix_spawn_file_actions_destroy(&fa);
    if (r) { dsl_log("soft: A: posix_spawn %s failed (%d)", exe, r); b_pid = 0; free(env); free(extra); free(ov); return -1; }
    dsl_log("soft: A: B started, pid %d (%s, profile %s)", b_pid, exe, profile);
    free(env); free(extra); free(ov);
    return 0;
}
/* killed children (a replaced prewarm B, a failed B) are reaped later without blocking the game's main thread */
static pid_t dead[16]; static int ndead;
static void reap(void) {
    for (int i = 0; i < ndead; i++) { int st; pid_t r = waitpid(dead[i], &st, WNOHANG); if (r == dead[i] || (r < 0 && errno == ECHILD)) dead[i--] = dead[--ndead]; }
}
long dsl_sos_child_pid(void) { return b_pid; }
int dsl_sos_child_exited(void) {
    reap();
    if (b_pid <= 0) return 0;
    if (b_ls) { if (kill(b_pid, 0) != 0 && errno == ESRCH) { b_pid = 0; return 1; } return 0; }
    int st; if (waitpid(b_pid, &st, WNOHANG) == b_pid) { b_pid = 0; return 1; }
    return 0;
}
void dsl_sos_child_kill(void) {
    if (b_pid <= 0) return;
    kill(b_pid, SIGKILL); dsl_log("soft: A: B pid %d killed", b_pid);
    int st; if (waitpid(b_pid, &st, WNOHANG) != b_pid && ndead < 16) dead[ndead++] = b_pid;
    b_pid = 0; reap();
}
int dsl_sos_pid_alive(long pid) { return pid > 0 && kill((pid_t)pid, 0) == 0; }

void dsl_sos_yield(void) {                 /* macOS 14+: activation is given, not taken */
    if (no_activate_g || b_pid <= 0) return;
    id ra = MSG(id, Class, SEL, int)(objc_getClass("NSRunningApplication"), SEL_("runningApplicationWithProcessIdentifier:"), b_pid);
    id a = app();
    if (ra && a && class_respondsToSelector(object_getClass(a), SEL_("yieldActivationToApplication:")))
        MSG(void, id, SEL, id)(a, SEL_("yieldActivationToApplication:"), ra);
}

void dsl_sos_show(const double *fr, int activate) {
    id w = game_window();
    if (w && fr && !stage_on) {
        CGRect f = CGRectMake(fr[0], fr[1], fr[2], fr[3]);
        ((void (*)(id, SEL, CGRect, BOOL))o_setframe)(w, SEL_("setFrame:display:"), f, YES);
    }
    hidden = 0;
    if (activate && !no_activate_g) set_policy(0);
    /* test stage without activation: the window goes to the back of the window list. On top (orderFrontRegardless,
       or the level change from hidden to normal) the system picked this copy as the front app for a moment when
       focus moved between other apps (test guard kills, 2026-10-16) */
    if (w) {
        show_window(w);
        if (activate || !stage_on) MSG(void, id, SEL, id)(w, SEL_("orderFrontRegardless"), nil);
        else MSG(void, id, SEL, id)(w, SEL_("orderBack:"), nil);
    }
    if (activate && !no_activate_g) {
        id a = app();
        if (a && class_respondsToSelector(object_getClass(a), SEL_("activate"))) ((void (*)(id, SEL))o_act0)(a, SEL_("activate"));
        else if (a) ((void (*)(id, SEL, BOOL))o_act1)(a, SEL_("activateIgnoringOtherApps:"), YES);
        if (w) MSG(void, id, SEL, id)(w, SEL_("makeKeyAndOrderFront:"), nil);
    }
}

void dsl_sos_order_out(void) {
    unsigned long n; id ws = windows(&n);
    for (unsigned long i = 0; i < n; i++) MSG(void, id, SEL, id)(win_at(ws, i), SEL_("orderOut:"), nil);
}

/* ------------------------------------------------------------------ main-thread tick (between Unity frames) */
static void (*tick_fn)(void);
static void tick(CFRunLoopTimerRef tm, void *info) { (void)tm; (void)info; if (tick_fn) tick_fn(); }
void dsl_sos_timer(void (*fn)(void)) {
    tick_fn = fn;
    CFRunLoopTimerRef tm = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.1, 1.0 / 60, 0, 0, tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetMain(), tm, kCFRunLoopCommonModes);
}
