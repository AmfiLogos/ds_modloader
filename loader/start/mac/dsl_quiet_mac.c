// Test copies only (LSBackgroundOnly, not DSRIG_LIVE; dsl_start_mac.c test_copy_window calls dsl_quiet_install):
//  1. No window other than the game's own (Unity PlayerWindow) is ever ordered on screen. A hidden copy showed a small
//     205x127 window of its own on the owner's screen twice (2026-10-17, cmrig copy R, guard kill): unknown, maybe an
//     input method window when a text field gets focus. Every order-in path is blocked from creation; the text input
//     context never activates; a main run loop check orders out anything that got through. Each blocked window is
//     logged once (class, size, title, short backtrace) and never shown.
//  2. No outgoing open: NSWorkspace open / launch, LSOpenCFURLRef / LSOpenURLsWithRole, system / popen / posix_spawn
//     of /usr/bin/open or of a URL. libsteam_api's SteamAPI_RestartAppIfNecessary runs system("/usr/bin/open
//     'steam://run/<app>'") and the owner's CrossOver Steam answered steam:// once (2026-10-17 21:47, not proven); the
//     game resolves that call with dlsym, which here gives a stub returning false (keep running, open nothing).
//     Only the copy's own soft restart (NSWorkspace openApplicationAtURL on its own app, dsl_soft_mac.c) still opens.
//  3. The game window itself stays invisible and never goes full screen, whatever modules the copy loads (not in the
//     demo modes DSRIG_SHOWWIN / DSRIG_SPACE). It was left to the multiplayer module's rig layer, so a copy without it
//     showed its window on the owner's screen (2026-10-18 01:35, copy L, guard kill) or went full screen (~1.5 min, maybe
//     switched his Space). From creation every window has alpha 0, the lowest level, no mouse, no shadow and is out of
//     Spaces / Mission Control; no later call lifts that (the soft restart's "show" included). Native full screen, the
//     full-screen style, presentation options, display capture / mode change / fade are refused; with the notch fix off
//     (DSL_NOTCH=0, every test copy except notch_test) the window is at most 1280x800 points. It still sits in the window
//     server's list (Metal keeps presenting frames), as with the multiplayer rig layer, which chains on these hooks.
// Outside test copies every hook passes straight through (the interposers are always bound, inert while off).
#include "dsl_internal.h"
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MSG(ret, ...) ((ret (*)(__VA_ARGS__))objc_msgSend)
#define SEL_(s) sel_registerName(s)
#define QLOG dsl_log

static int q_on;
static char q_own_dir[1024], q_rig_dir[1024];       /* the copy's folder (its app's parent) and DSRIG_DIR: own soft restart */

static const char *utf8(id o) {
    if (!o) return "";
    id d = MSG(id, id, SEL)(o, SEL_("description"));
    const char *s = d ? MSG(const char *, id, SEL)(d, SEL_("UTF8String")) : NULL;
    return s ? s : "?";
}
/* "image!symbol+off < ..." for the callers of the hook (frames 2..), cheap: dladdr only */
static void short_bt(char *out, size_t cap) {
    void *fr[14]; int n = backtrace(fr, 14); size_t k = 0; out[0] = 0;
    for (int i = 2; i < n && k + 8 < cap; i++) {
        Dl_info di; const char *img = "?", *sym = "?"; long off = 0;
        if (dladdr(fr[i], &di)) {
            if (di.dli_fname) { img = strrchr(di.dli_fname, '/'); img = img ? img + 1 : di.dli_fname; }
            if (di.dli_sname) { sym = di.dli_sname; off = (long)((char *)fr[i] - (char *)di.dli_saddr); }
            else off = (long)((char *)fr[i] - (char *)di.dli_fbase);
        }
        int w = snprintf(out + k, cap - k, "%s%s!%s+%ld", k ? " < " : "", img, sym, off);
        if (w < 0) break; k += (size_t)w; if (k >= cap) { out[cap - 1] = 0; break; }
    }
}

/* ------------------------------------------------------------------ windows */
static int allowed_window(id w) {                /* the game's own window (Unity PlayerWindow): the rig manages it */
    static Class pw; if (!pw) pw = objc_getClass("PlayerWindow");
    return pw && MSG(BOOL, id, SEL, Class)(w, SEL_("isKindOfClass:"), pw);
}
static id seen[64]; static int nseen, nblocked;
static void note_block(id w, const char *how, int bt) {
    nblocked++;
    for (int i = 0; i < nseen; i++) if (seen[i] == w) return;    /* each window once */
    if (nseen < 64) seen[nseen++] = w;
    CGRect f = MSG(CGRect, id, SEL)(w, SEL_("frame"));
    char b[1400] = ""; if (bt) short_bt(b, sizeof b);
    QLOG("quiet: window blocked (%s): class %s, %.0fx%.0f at %.0f,%.0f, level %ld, title \"%s\"%s%s", how, object_getClassName(w),
         f.size.width, f.size.height, f.origin.x, f.origin.y, MSG(long, id, SEL)(w, SEL_("level")),
         utf8(MSG(id, id, SEL)(w, SEL_("title"))), bt ? ", from " : "", b);
}
static void hide_now(id w) {
    MSG(void, id, SEL, double)(w, SEL_("setAlphaValue:"), 0.0);
    MSG(void, id, SEL, BOOL)(w, SEL_("setIgnoresMouseEvents:"), YES);
}
static IMP o_ofront, o_okof, o_oreg, o_vis, o_order, o_deep, o_tic;
#define BLOCKED(w) (q_on && !allowed_window(w))
static void my_ofront(id self, SEL s, id a) { if (BLOCKED(self)) { hide_now(self); note_block(self, "orderFront:", 1); return; } ((void (*)(id, SEL, id))o_ofront)(self, s, a); }
static void my_okof(id self, SEL s, id a) { if (BLOCKED(self)) { hide_now(self); note_block(self, "makeKeyAndOrderFront:", 1); return; } ((void (*)(id, SEL, id))o_okof)(self, s, a); }
static void my_oreg(id self, SEL s) { if (BLOCKED(self)) { hide_now(self); note_block(self, "orderFrontRegardless", 1); return; } ((void (*)(id, SEL))o_oreg)(self, s); }
static void my_vis(id self, SEL s, BOOL v) { if (v && BLOCKED(self)) { hide_now(self); note_block(self, "setIsVisible:", 1); return; } ((void (*)(id, SEL, BOOL))o_vis)(self, s, v); }
static void my_order(id self, SEL s, long place, long rel) {     /* NSWindowOut (0) always passes */
    if (place && BLOCKED(self)) { hide_now(self); note_block(self, "orderWindow:relativeTo:", 1); return; }
    ((void (*)(id, SEL, long, long))o_order)(self, s, place, rel);
}
/* AppKit's own order-in step (every public path and private ones end here; order-out has its own method) */
static void my_deep(id self, SEL s, id a) { if (BLOCKED(self)) { hide_now(self); note_block(self, "_reallyDoOrderWindowAboveOrBelow:", 1); return; } ((void (*)(id, SEL, id))o_deep)(self, s, a); }
static void my_tic(id self, SEL s) {
    static int once; if (q_on) { if (!once++) QLOG("quiet: text input context activation blocked (no input method window)"); return; }
    ((void (*)(id, SEL))o_tic)(self, s);
}
/* ------------------------------------------------------------------ the game window: invisible, never full screen */
static int q_hide, q_clamp;                      /* q_hide: not a demo copy; q_clamp: notch fix off (it covers the screen) */
#define LEVEL_HIDDEN (-2147483000L)
#define CB_HIDDEN ((1ul << 3) | (1ul << 4) | (1ul << 6) | (1ul << 9))   /* Transient | Stationary | IgnoresCycle | FullScreenNone */
#define SM_FULLSCREEN (1ul << 14)
#define MAX_W 1280.0
#define MAX_H 800.0
typedef id (*init4_t)(id, SEL, CGRect, unsigned long, unsigned long, BOOL);
typedef id (*init5_t)(id, SEL, CGRect, unsigned long, unsigned long, BOOL, id);
static init4_t o_init4; static init5_t o_init5;
static IMP o_alpha, o_level, o_cb, o_sm, o_fs, o_frame, o_frame3, o_csize, o_pres;
static void once(int *f, const char *what) { if (!(*f)++) QLOG("quiet: %s", what); }
static void hide_game(id w) {
    if (!w) return;
    ((void (*)(id, SEL, double))o_alpha)(w, SEL_("setAlphaValue:"), 0.0);
    ((void (*)(id, SEL, long))o_level)(w, SEL_("setLevel:"), LEVEL_HIDDEN);
    ((void (*)(id, SEL, unsigned long))o_cb)(w, SEL_("setCollectionBehavior:"), CB_HIDDEN);
    MSG(void, id, SEL, BOOL)(w, SEL_("setIgnoresMouseEvents:"), YES);
    MSG(void, id, SEL, BOOL)(w, SEL_("setHasShadow:"), NO);
}
static CGSize clamp(CGSize z, double extra_h, const char *how) {
    if (!q_clamp || (z.width <= MAX_W && z.height <= MAX_H + extra_h)) return z;
    static int n; if (n++ < 3) QLOG("quiet: game window %s %.0fx%.0f refused: at most %.0fx%.0f (test copy, windowed)", how, z.width, z.height, MAX_W, MAX_H);
    if (z.width > MAX_W) z.width = MAX_W;
    if (z.height > MAX_H + extra_h) z.height = MAX_H + extra_h;
    return z;
}
static double title_h(id w) {                    /* frame height minus content height: the title bar */
    CGRect f = MSG(CGRect, id, SEL)(w, SEL_("frame"));
    CGRect c = MSG(CGRect, id, SEL, CGRect)(w, SEL_("contentRectForFrameRect:"), f);
    return f.size.height - c.size.height;
}
static id my_init4(id self, SEL s, CGRect r, unsigned long st, unsigned long b, BOOL d) {
    id w = o_init4(self, s, r, q_hide ? st & ~SM_FULLSCREEN : st, b, d); if (q_hide) hide_game(w); return w;
}
static id my_init5(id self, SEL s, CGRect r, unsigned long st, unsigned long b, BOOL d, id sc) {
    id w = o_init5(self, s, r, q_hide ? st & ~SM_FULLSCREEN : st, b, d, sc); if (q_hide) hide_game(w); return w;
}
static void my_alpha(id self, SEL s, double a) { ((void (*)(id, SEL, double))o_alpha)(self, s, q_hide ? 0.0 : a); }
static void my_level(id self, SEL s, long l) { ((void (*)(id, SEL, long))o_level)(self, s, q_hide ? LEVEL_HIDDEN : l); }
static void my_cb(id self, SEL s, unsigned long b) { ((void (*)(id, SEL, unsigned long))o_cb)(self, s, q_hide ? CB_HIDDEN : b); }
static void my_sm(id self, SEL s, unsigned long m) {
    static int f; if (q_hide && (m & SM_FULLSCREEN)) { once(&f, "full-screen window style refused (test copy)"); m &= ~SM_FULLSCREEN; }
    ((void (*)(id, SEL, unsigned long))o_sm)(self, s, m);
}
static void my_fs(id self, SEL s, id a) {
    static int f; if (q_hide) { once(&f, "native full screen (toggleFullScreen:) refused (test copy)"); return; }
    ((void (*)(id, SEL, id))o_fs)(self, s, a);
}
static void my_frame(id self, SEL s, CGRect r, BOOL d) {
    if (q_hide) r.size = clamp(r.size, title_h(self), "frame");
    ((void (*)(id, SEL, CGRect, BOOL))o_frame)(self, s, r, d);
}
static void my_frame3(id self, SEL s, CGRect r, BOOL d, BOOL an) {
    if (q_hide) r.size = clamp(r.size, title_h(self), "frame");
    ((void (*)(id, SEL, CGRect, BOOL, BOOL))o_frame3)(self, s, r, d, an);
}
static void my_csize(id self, SEL s, CGSize z) { if (q_hide) z = clamp(z, 0, "content size"); ((void (*)(id, SEL, CGSize))o_csize)(self, s, z); }
static void my_pres(id self, SEL s, unsigned long o) {          /* hide Dock / menu bar, kiosk, full-screen options */
    static int f; if (q_hide) { if (o) once(&f, "presentation options refused (test copy: the owner's Dock and menu bar stay)"); return; }
    ((void (*)(id, SEL, unsigned long))o_pres)(self, s, o);
}
/* exclusive full screen paths: capturing a display, changing its mode or fading it would act on the owner's screen */
static int f_cap, f_mode, f_fade;
static CGError q_cap1(CGDirectDisplayID d) { if (q_hide) { once(&f_cap, "display capture refused (test copy)"); return kCGErrorSuccess; } return CGDisplayCapture(d); }
static CGError q_cap2(CGDirectDisplayID d, CGCaptureOptions o) { if (q_hide) { once(&f_cap, "display capture refused (test copy)"); return kCGErrorSuccess; } return CGDisplayCaptureWithOptions(d, o); }
static CGError q_capall(void) { if (q_hide) { once(&f_cap, "display capture refused (test copy)"); return kCGErrorSuccess; } return CGCaptureAllDisplays(); }
static CGError q_capall2(CGCaptureOptions o) { if (q_hide) { once(&f_cap, "display capture refused (test copy)"); return kCGErrorSuccess; } return CGCaptureAllDisplaysWithOptions(o); }
static CGError q_setmode(CGDirectDisplayID d, CGDisplayModeRef m, CFDictionaryRef o) { if (q_hide) { once(&f_mode, "display mode change refused (test copy)"); return kCGErrorSuccess; } return CGDisplaySetDisplayMode(d, m, o); }
static CGError q_cfgmode(CGDisplayConfigRef c, CGDirectDisplayID d, CGDisplayModeRef m, CFDictionaryRef o) { if (q_hide) { once(&f_mode, "display mode change refused (test copy)"); return kCGErrorSuccess; } return CGConfigureDisplayWithDisplayMode(c, d, m, o); }
static CGError q_fadeacq(CGDisplayReservationInterval s, CGDisplayFadeReservationToken *t) { if (q_hide) { once(&f_fade, "display fade refused (test copy)"); return kCGErrorNoneAvailable; } return CGAcquireDisplayFadeReservation(s, t); }
static CGError q_fade(CGDisplayFadeReservationToken t, CGDisplayFadeInterval i, CGDisplayBlendFraction a, CGDisplayBlendFraction b, float r, float g, float bl, boolean_t sy) {
    if (q_hide) { once(&f_fade, "display fade refused (test copy)"); return kCGErrorSuccess; }
    return CGDisplayFade(t, i, a, b, r, g, bl, sy);
}

/* every 0.25 s: a window that got on screen by a path not hooked above goes out at once; the game window hidden again */
static void sweep(CFRunLoopTimerRef t, void *i) {
    (void)t; (void)i;
    id *nsapp = (id *)dlsym(RTLD_DEFAULT, "NSApp"); if (!nsapp || !*nsapp) return;
    id ws = MSG(id, id, SEL)(*nsapp, SEL_("windows")); unsigned long n = ws ? MSG(unsigned long, id, SEL)(ws, SEL_("count")) : 0;
    for (unsigned long k = 0; k < n; k++) {
        id w = MSG(id, id, SEL, unsigned long)(ws, SEL_("objectAtIndex:"), k);
        if (q_hide && (MSG(double, id, SEL)(w, SEL_("alphaValue")) != 0.0 || MSG(long, id, SEL)(w, SEL_("level")) != LEVEL_HIDDEN)) {
            static int m; if (m++ < 3) QLOG("quiet: window %s (alpha %.2f, level %ld) by another path: hidden again", object_getClassName(w),
                                             MSG(double, id, SEL)(w, SEL_("alphaValue")), MSG(long, id, SEL)(w, SEL_("level")));
            hide_game(w);
        }
        if (allowed_window(w) || !MSG(BOOL, id, SEL)(w, SEL_("isVisible"))) continue;
        hide_now(w); MSG(void, id, SEL, id)(w, SEL_("orderOut:"), nil);
        note_block(w, "on screen by another path, ordered out", 0);
    }
}

/* ------------------------------------------------------------------ outgoing opens */
static int own_path(const char *p) {
    size_t a = strlen(q_own_dir), b = strlen(q_rig_dir);
    return p && ((a && !strncmp(p, q_own_dir, a) && (p[a] == '/' || !p[a])) || (b && !strncmp(p, q_rig_dir, b) && (p[b] == '/' || !p[b])));
}
static void note_open(const char *how, const char *what) {
    char b[1400]; short_bt(b, sizeof b);
    QLOG("quiet: outgoing open blocked (%s): %s, from %s", how, what, b);
}
struct blk { void *isa; int flags, reserved; void (*invoke)(void *, ...); };
static void call_done(id h) { if (h) ((void (*)(void *, id, id))((struct blk *)h)->invoke)(h, nil, nil); }   /* (app/nil, error/nil) */
/* every NSWorkspace open... / launch... method: the first argument (URL, URLs, path, app) is logged; returns NO / nil */
static long ws_blocked(id self, SEL s, id a) {
    char w[1200]; snprintf(w, sizeof w, "%s %s", sel_getName(s), utf8(a));
    note_open("NSWorkspace", w); (void)self; return 0;
}
static IMP o_oapp, o_ourlc, o_ourlsc;
static void my_oapp(id self, SEL s, id url, id cfg, id h) {    /* soft restart B = the copy's own app: allowed */
    const char *p = url && MSG(BOOL, id, SEL)(url, SEL_("isFileURL")) ? MSG(const char *, id, SEL)(MSG(id, id, SEL)(url, SEL_("path")), SEL_("UTF8String")) : NULL;
    if (!q_on || own_path(p)) { ((void (*)(id, SEL, id, id, id))o_oapp)(self, s, url, cfg, h); return; }
    char w[1200]; snprintf(w, sizeof w, "openApplicationAtURL %s", utf8(url)); note_open("NSWorkspace", w); call_done(h);
}
static void my_ourlc(id self, SEL s, id url, id cfg, id h) {
    if (!q_on) { ((void (*)(id, SEL, id, id, id))o_ourlc)(self, s, url, cfg, h); return; }
    char w[1200]; snprintf(w, sizeof w, "openURL:configuration: %s", utf8(url)); note_open("NSWorkspace", w); call_done(h);
}
static void my_ourlsc(id self, SEL s, id urls, id app, id cfg, id h) {
    if (!q_on) { ((void (*)(id, SEL, id, id, id, id))o_ourlsc)(self, s, urls, app, cfg, h); return; }
    char w[1200]; snprintf(w, sizeof w, "openURLs %s with %s", utf8(urls), utf8(app)); note_open("NSWorkspace", w); call_done(h);
}

static int cmd_opens(const char *c) {            /* runs open(1), or carries a URL */
    if (!c) return 0;
    if (strstr(c, "://") || strstr(c, "/usr/bin/open")) return 1;
    while (*c == ' ' || *c == '\t' || *c == '(' || *c == '"' || *c == '\'') c++;
    return !strncmp(c, "open ", 5);
}
static int argv_opens(const char *path, char *const argv[]) {
    const char *b = path ? strrchr(path, '/') : NULL; b = b ? b + 1 : path;
    if (b && !strcmp(b, "open")) return 1;
    for (int i = 0; argv && argv[i]; i++) if (cmd_opens(argv[i])) return 1;
    return 0;
}
static void argv_line(char *out, size_t cap, const char *path, char *const argv[]) {
    size_t k = (size_t)snprintf(out, cap, "%s", path ? path : "?");
    for (int i = 1; argv && argv[i] && k + 4 < cap; i++) k += (size_t)snprintf(out + k, cap - k, " %s", argv[i]);
}
static int q_system(const char *c) {
    if (q_on && cmd_opens(c)) { note_open("system", c); return 1 << 8; }   /* as if the command exited 1 */
    return system(c);
}
static FILE *q_popen(const char *c, const char *m) {
    if (q_on && cmd_opens(c)) { note_open("popen", c); return NULL; }
    return popen(c, m);
}
static int q_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at, char *const argv[], char *const envp[]) {
    if (q_on && argv_opens(path, argv)) { char w[1200]; argv_line(w, sizeof w, path, argv); note_open("posix_spawn", w); return 1; }   /* EPERM */
    return posix_spawn(pid, path, fa, at, argv, envp);
}
static int q_spawnp(pid_t *pid, const char *file, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *at, char *const argv[], char *const envp[]) {
    if (q_on && argv_opens(file, argv)) { char w[1200]; argv_line(w, sizeof w, file, argv); note_open("posix_spawnp", w); return 1; }
    return posix_spawnp(pid, file, fa, at, argv, envp);
}
static OSStatus q_lsopen(CFURLRef u, CFURLRef *out) {
    if (q_on) {
        char w[1200] = "?"; CFStringRef s = u ? CFURLGetString(u) : NULL;
        if (s) CFStringGetCString(s, w, sizeof w, kCFStringEncodingUTF8);
        note_open("LSOpenCFURLRef", w); return kLSApplicationNotFoundErr;
    }
    return LSOpenCFURLRef(u, out);
}
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static OSStatus q_lsroles(CFArrayRef urls, LSRolesMask r, const AEKeyDesc *ae, const LSApplicationParameters *ap, ProcessSerialNumber *psn, CFIndex n) {
    if (q_on) {
        char w[1200] = "?"; CFStringRef s = urls && CFArrayGetCount(urls) ? CFURLGetString((CFURLRef)CFArrayGetValueAtIndex(urls, 0)) : NULL;
        if (s) CFStringGetCString(s, w, sizeof w, kCFStringEncodingUTF8);
        note_open("LSOpenURLsWithRole", w); return kLSApplicationNotFoundErr;
    }
    return LSOpenURLsWithRole(urls, r, ae, ap, psn, n);
}
#pragma clang diagnostic pop
/* il2cpp resolves the game's Steam calls with dlsym: SteamAPI_RestartAppIfNecessary becomes this stub in a test copy.
   Every other lookup is a tail call (the caller's own frame goes to dyld, as without the hook). */
static bool q_steam_restart(unsigned int app) { QLOG("quiet: SteamAPI_RestartAppIfNecessary(%u) blocked: returns false (no steam:// open)", app); return false; }
static void *q_dlsym(void *h, const char *name) {
    if (q_on && name && !strcmp(name, "SteamAPI_RestartAppIfNecessary")) return (void *)q_steam_restart;
    __attribute__((musttail)) return dlsym(h, name);
}

/* dyld interposing (dsload is inserted at launch): binds of every image, also those loaded later (libsteam_api) */
#define INTERPOSE(rep, orig) __attribute__((used)) static const struct { const void *r, *o; } ip_##orig __attribute__((section("__DATA,__interpose"))) = { (const void *)rep, (const void *)orig };
INTERPOSE(q_system, system)
INTERPOSE(q_popen, popen)
INTERPOSE(q_spawn, posix_spawn)
INTERPOSE(q_spawnp, posix_spawnp)
INTERPOSE(q_lsopen, LSOpenCFURLRef)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
INTERPOSE(q_lsroles, LSOpenURLsWithRole)
#pragma clang diagnostic pop
INTERPOSE(q_dlsym, dlsym)
INTERPOSE(q_cap1, CGDisplayCapture)
INTERPOSE(q_cap2, CGDisplayCaptureWithOptions)
INTERPOSE(q_capall, CGCaptureAllDisplays)
INTERPOSE(q_capall2, CGCaptureAllDisplaysWithOptions)
INTERPOSE(q_setmode, CGDisplaySetDisplayMode)
INTERPOSE(q_cfgmode, CGConfigureDisplayWithDisplayMode)
INTERPOSE(q_fadeacq, CGAcquireDisplayFadeReservation)
INTERPOSE(q_fade, CGDisplayFade)

static IMP swap(const char *cls, const char *sel, IMP imp) {
    Class c = objc_getClass(cls); Method m = c ? class_getInstanceMethod(c, SEL_(sel)) : NULL;
    if (!m) { QLOG("quiet: %s %s missing", cls, sel); return NULL; }
    return method_setImplementation(m, imp);
}

void dsl_quiet_install(void) {
    if (q_on) return;
    CFBundleRef mb = CFBundleGetMainBundle(); CFURLRef bu = mb ? CFBundleCopyBundleURL(mb) : NULL;
    if (bu) {
        CFURLRef pu = CFURLCreateCopyDeletingLastPathComponent(NULL, bu);
        if (pu && CFURLGetFileSystemRepresentation(pu, true, (UInt8 *)q_own_dir, sizeof q_own_dir)) {
            size_t n = strlen(q_own_dir); if (n > 1 && q_own_dir[n - 1] == '/') q_own_dir[n - 1] = 0;
        }
        if (pu) CFRelease(pu); CFRelease(bu);
    }
    const char *rd = getenv("DSRIG_DIR"); if (rd) snprintf(q_rig_dir, sizeof q_rig_dir, "%s", rd);
    o_ofront = swap("NSWindow", "orderFront:", (IMP)my_ofront);
    o_okof = swap("NSWindow", "makeKeyAndOrderFront:", (IMP)my_okof);
    o_oreg = swap("NSWindow", "orderFrontRegardless", (IMP)my_oreg);
    o_vis = swap("NSWindow", "setIsVisible:", (IMP)my_vis);
    o_order = swap("NSWindow", "orderWindow:relativeTo:", (IMP)my_order);
    o_deep = swap("NSWindow", "_reallyDoOrderWindowAboveOrBelow:", (IMP)my_deep);
    o_tic = swap("NSTextInputContext", "activate", (IMP)my_tic);
    /* the game window (point 3): before Unity makes it; the multiplayer rig layer and the soft restart chain on these */
    q_hide = !getenv("DSRIG_SHOWWIN") && !getenv("DSRIG_SPACE");
    const char *nt = getenv("DSL_NOTCH"); q_clamp = nt && *nt == '0';
    if (q_hide) {
        o_init4 = (init4_t)swap("NSWindow", "initWithContentRect:styleMask:backing:defer:", (IMP)my_init4);
        o_init5 = (init5_t)swap("NSWindow", "initWithContentRect:styleMask:backing:defer:screen:", (IMP)my_init5);
        o_alpha = swap("NSWindow", "setAlphaValue:", (IMP)my_alpha);
        o_level = swap("NSWindow", "setLevel:", (IMP)my_level);
        o_cb = swap("NSWindow", "setCollectionBehavior:", (IMP)my_cb);
        o_sm = swap("NSWindow", "setStyleMask:", (IMP)my_sm);
        o_fs = swap("NSWindow", "toggleFullScreen:", (IMP)my_fs);
        o_frame = swap("NSWindow", "setFrame:display:", (IMP)my_frame);
        o_frame3 = swap("NSWindow", "setFrame:display:animate:", (IMP)my_frame3);
        o_csize = swap("NSWindow", "setContentSize:", (IMP)my_csize);
        o_pres = swap("NSApplication", "setPresentationOptions:", (IMP)my_pres);
        if (!o_init4 || !o_init5 || !o_alpha || !o_level || !o_cb || !o_sm || !o_fs || !o_frame || !o_frame3 || !o_csize || !o_pres) {
            QLOG("quiet: refused: the game window cannot be kept hidden (an AppKit method is missing): this copy does not start");
            _exit(3);
        }
    }
    Class wc = objc_getClass("NSWorkspace"); unsigned n = 0; Method *ms = wc ? class_copyMethodList(wc, &n) : NULL;
    int nws = 0;
    for (unsigned i = 0; i < n; i++) {
        const char *s = sel_getName(method_getName(ms[i]));
        if (!strcmp(s, "openApplicationAtURL:configuration:completionHandler:")) o_oapp = method_setImplementation(ms[i], (IMP)my_oapp);
        else if (!strcmp(s, "openURL:configuration:completionHandler:")) o_ourlc = method_setImplementation(ms[i], (IMP)my_ourlc);
        else if (!strcmp(s, "openURLs:withApplicationAtURL:configuration:completionHandler:")) o_ourlsc = method_setImplementation(ms[i], (IMP)my_ourlsc);
        else if ((!strncmp(s, "open", 4) || !strncmp(s, "_open", 5) || !strncmp(s, "launchApp", 9)) && strchr(s, ':')) method_setImplementation(ms[i], (IMP)ws_blocked);
        else continue;
        nws++;
    }
    free(ms);
    q_on = 1;
    CFRunLoopTimerRef tm = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.25, 0.25, 0, 0, sweep, NULL);
    CFRunLoopAddTimer(CFRunLoopGetMain(), tm, kCFRunLoopCommonModes);
    QLOG("quiet: test copy: only the game window may show, no input method window, no outgoing open (%d NSWorkspace methods; own folder %s)", nws, q_own_dir);
    if (q_hide) QLOG("quiet: game window invisible from creation (alpha 0, lowest level), never full screen%s", q_clamp ? ", at most 1280x800 points" : "");
    else QLOG("quiet: demo copy (DSRIG_SHOWWIN / DSRIG_SPACE): the game window is left to the rig");
}
