// Window hiding (used when the game runs WITH graphics). Every NSWindow is made invisible the moment
// it is created: alpha 0, ignores the mouse, never key/main, excluded from window menus; the app never
// activates. The window still exists on screen for the compositor, so Metal keeps presenting frames.
// Pure objc runtime calls (no Objective-C source).
#include "rig.h"
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreGraphics/CoreGraphics.h>
#include <stdlib.h>
#include <string.h>

typedef id (*init4_t)(id, SEL, CGRect, unsigned long, unsigned long, BOOL);
typedef id (*init5_t)(id, SEL, CGRect, unsigned long, unsigned long, BOOL, id);
static init4_t o_init4; static init5_t o_init5;
static IMP o_alpha;

static void hide(id w) {
    if (!w) return;
    ((void (*)(id, SEL, double))o_alpha)(w, sel_registerName("setAlphaValue:"), 0.0);
    ((void (*)(id, SEL, BOOL))objc_msgSend)(w, sel_registerName("setIgnoresMouseEvents:"), YES);
    ((void (*)(id, SEL, BOOL))objc_msgSend)(w, sel_registerName("setExcludedFromWindowsMenu:"), YES);
    ((void (*)(id, SEL, BOOL))objc_msgSend)(w, sel_registerName("setHasShadow:"), NO);
    ((void (*)(id, SEL, long))objc_msgSend)(w, sel_registerName("setLevel:"), (long)-2147483000);   /* far below every normal window */
    ((void (*)(id, SEL, unsigned long))objc_msgSend)(w, sel_registerName("setSharingType:"), 0);        /* NSWindowSharingNone */
    /* NSWindowCollectionBehaviorTransient | IgnoresCycle | Stationary: not in Mission Control / Cmd-` */
    ((void (*)(id, SEL, unsigned long))objc_msgSend)(w, sel_registerName("setCollectionBehavior:"), (1u << 3) | (1u << 6) | (1u << 4));
}
static id my_init4(id self, SEL s, CGRect r, unsigned long st, unsigned long b, BOOL d) { id w = o_init4(self, s, r, st, b, d); hide(w); return w; }
static id my_init5(id self, SEL s, CGRect r, unsigned long st, unsigned long b, BOOL d, id sc) { id w = o_init5(self, s, r, st, b, d, sc); hide(w); return w; }
static void my_alpha(id self, SEL s, double a) { (void)a; ((void (*)(id, SEL, double))o_alpha)(self, s, 0.0); }
static void noop1(id self, SEL s, id a) { (void)self; (void)s; (void)a; }
static void noopb(id self, SEL s, BOOL a) { (void)self; (void)s; (void)a; }
static void noop0(id self, SEL s) { (void)self; (void)s; }
static BOOL no0(id self, SEL s) { (void)self; (void)s; return NO; }

static void swap(const char *cls, const char *sel, IMP imp, IMP *orig) {
    Class c = objc_getClass(cls); if (!c) { rig_log("win: class %s missing", cls); return; }
    Method m = class_getInstanceMethod(c, sel_registerName(sel));
    if (!m) { rig_log("win: %s %s missing", cls, sel); return; }
    IMP o = method_setImplementation(m, imp);
    if (orig) *orig = o;
}

void rig_win_install(void) {
    swap("NSWindow", "initWithContentRect:styleMask:backing:defer:", (IMP)my_init4, (IMP*)&o_init4);
    swap("NSWindow", "initWithContentRect:styleMask:backing:defer:screen:", (IMP)my_init5, (IMP*)&o_init5);
    swap("NSWindow", "setAlphaValue:", (IMP)my_alpha, &o_alpha);
    swap("NSWindow", "makeKeyWindow", (IMP)noop0, NULL);
    swap("NSWindow", "makeMainWindow", (IMP)noop0, NULL);
    swap("NSWindow", "canBecomeKeyWindow", (IMP)no0, NULL);
    swap("NSWindow", "canBecomeMainWindow", (IMP)no0, NULL);
    swap("NSWindow", "toggleFullScreen:", (IMP)noop1, NULL);
    swap("NSApplication", "activateIgnoringOtherApps:", (IMP)noopb, NULL);
    swap("NSApplication", "activate", (IMP)noop0, NULL);
    swap("NSRunningApplication", "activateWithOptions:", (IMP)no0, NULL);
    rig_log("win: NSWindow/NSApplication hiding installed");
}

/* every frame: re-hide every window of the app (catches windows made by other init paths) */
void rig_win_frame(void) {
    id app = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    if (!app) return;
    id wins = ((id (*)(id, SEL))objc_msgSend)(app, sel_registerName("windows"));
    unsigned long n = ((unsigned long (*)(id, SEL))objc_msgSend)(wins, sel_registerName("count"));
    for (unsigned long i = 0; i < n; i++) {
        id w = ((id (*)(id, SEL, unsigned long))objc_msgSend)(wins, sel_registerName("objectAtIndex:"), i);
        double a = ((double (*)(id, SEL))objc_msgSend)(w, sel_registerName("alphaValue"));
        if (a != 0.0) { rig_log("win: window %lu had alpha %.2f, hiding again", i, a); hide(w); }
    }
}

/* Demo only (rig.py --show, never in tests): DSRIG_SHOWWIN=left|right puts the game's window on that half of the
   main screen's visible frame. Called every 30 frames; a window that is not there (Unity resized it) is put back. */
typedef struct { double x, y, w, h; } NSRectD;
void rig_win_place(void) {
    const char *side = getenv("DSRIG_SHOWWIN");
    if (!side) return;
    id app = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    id scr = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSScreen"), sel_registerName("mainScreen"));
    if (!app || !scr) return;
    id wins = ((id (*)(id, SEL))objc_msgSend)(app, sel_registerName("windows"));
    unsigned long n = ((unsigned long (*)(id, SEL))objc_msgSend)(wins, sel_registerName("count"));
    if (!n) return;
    NSRectD vf = ((NSRectD (*)(id, SEL))objc_msgSend)(scr, sel_registerName("visibleFrame"));
    NSRectD r = { vf.x + (strcmp(side, "right") ? 0 : vf.w / 2), vf.y, vf.w / 2, vf.h };
    int moved = 0;
    for (unsigned long i = 0; i < n; i++) {
        id w = ((id (*)(id, SEL, unsigned long))objc_msgSend)(wins, sel_registerName("objectAtIndex:"), i);
        NSRectD f = ((NSRectD (*)(id, SEL))objc_msgSend)(w, sel_registerName("frame"));
        if (f.x == r.x && f.y == r.y && f.w == r.w && f.h == r.h) continue;
        ((void (*)(id, SEL, NSRectD, BOOL))objc_msgSend)(w, sel_registerName("setFrame:display:"), r, YES);
        moved++;
    }
    if (moved) rig_log("win: %d window(s) placed on the %s half (%.0f,%.0f %.0fx%.0f)", moved, side, r.x, r.y, r.w, r.h);
    /* re-applied every call: Unity resizes the window to its resolution setting after start */
}

/* ---- Demo on another desktop (DSRIG_SPACE=<managed space id>, with DSRIG_SHOWWIN). Every window is created
   with alpha 0, moved to that space through the window server (SkyLight, our own connection), and only made
   opaque once CGSCopySpacesForWindows confirms it is there. The app is never activated (activation no-ops as in
   hidden mode), so the owner's current desktop never shows it and never switches. */
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>
typedef int (*cgs_conn_t)(void);
typedef void (*cgs_move_t)(int, CFArrayRef, uint64_t);
typedef CFArrayRef (*cgs_spaces_t)(int, int, CFArrayRef);
static cgs_conn_t f_conn; static cgs_move_t f_move; static cgs_spaces_t f_spaces;
static uint64_t target_space;
static int space_ready(void) {
    if (!f_conn) {
        void *h = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY);
        if (!h) return 0;
        f_conn = (cgs_conn_t)dlsym(h, "SLSMainConnectionID");
        f_move = (cgs_move_t)dlsym(h, "SLSMoveWindowsToManagedSpace");
        f_spaces = (cgs_spaces_t)dlsym(h, "SLSCopySpacesForWindows");
    }
    return f_conn && f_move && f_spaces;
}
static CFArrayRef wid_array(long wid) {
    int v = (int)wid; CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &v);
    CFArrayRef a = CFArrayCreate(NULL, (const void **)&n, 1, &kCFTypeArrayCallBacks); CFRelease(n); return a;
}
/* 1 = the window is on the target space (and only there) */
static int on_target(long wid) {
    CFArrayRef a = wid_array(wid); CFArrayRef sp = f_spaces(f_conn(), 7, a); CFRelease(a);
    int ok = 0;
    if (sp) {
        CFIndex n = CFArrayGetCount(sp);
        for (CFIndex i = 0; i < n; i++) { int64_t v = 0; CFNumberGetValue(CFArrayGetValueAtIndex(sp, i), kCFNumberSInt64Type, &v); if ((uint64_t)v == target_space) ok = 1; else { ok = 0; break; } }
        CFRelease(sp);
    }
    return ok;
}
static void to_space(id w) {
    long wid = ((long (*)(id, SEL))objc_msgSend)(w, sel_registerName("windowNumber"));
    if (wid <= 0 || !space_ready()) return;
    if (!on_target(wid)) { CFArrayRef a = wid_array(wid); f_move(f_conn(), a, target_space); CFRelease(a); }
    if (on_target(wid)) {
        double al = ((double (*)(id, SEL))objc_msgSend)(w, sel_registerName("alphaValue"));
        if (al != 1.0) { ((void (*)(id, SEL, double))o_alpha)(w, sel_registerName("setAlphaValue:"), 1.0); rig_log("win: window %ld is on space %llu: made opaque", wid, (unsigned long long)target_space); }
    } else ((void (*)(id, SEL, double))o_alpha)(w, sel_registerName("setAlphaValue:"), 0.0);
}
static id sp_init4(id self, SEL s, CGRect r, unsigned long st, unsigned long b, BOOL d) {
    id w = o_init4(self, s, r, st, b, d); ((void (*)(id, SEL, double))o_alpha)(w, sel_registerName("setAlphaValue:"), 0.0); to_space(w); return w;
}
static id sp_init5(id self, SEL s, CGRect r, unsigned long st, unsigned long b, BOOL d, id sc) {
    id w = o_init5(self, s, r, st, b, d, sc); ((void (*)(id, SEL, double))o_alpha)(w, sel_registerName("setAlphaValue:"), 0.0); to_space(w); return w;
}
static void sp_alpha(id self, SEL s, double a) {                 /* opaque only when confirmed on the target space */
    long wid = ((long (*)(id, SEL))objc_msgSend)(self, sel_registerName("windowNumber"));
    ((void (*)(id, SEL, double))o_alpha)(self, s, (wid > 0 && space_ready() && on_target(wid)) ? a : 0.0);
}
void rig_win_space_install(void) {
    const char *e = getenv("DSRIG_SPACE");
    target_space = e ? strtoull(e, NULL, 10) : 0;
    if (!target_space) return;
    swap("NSWindow", "initWithContentRect:styleMask:backing:defer:", (IMP)sp_init4, (IMP*)&o_init4);
    swap("NSWindow", "initWithContentRect:styleMask:backing:defer:screen:", (IMP)sp_init5, (IMP*)&o_init5);
    swap("NSWindow", "setAlphaValue:", (IMP)sp_alpha, &o_alpha);
    swap("NSWindow", "makeKeyWindow", (IMP)noop0, NULL);
    swap("NSWindow", "makeMainWindow", (IMP)noop0, NULL);
    swap("NSApplication", "activateIgnoringOtherApps:", (IMP)noopb, NULL);
    swap("NSApplication", "activate", (IMP)noop0, NULL);
    swap("NSRunningApplication", "activateWithOptions:", (IMP)no0, NULL);
    rig_log("win: demo windows go to space %llu (SkyLight %s)", (unsigned long long)target_space, space_ready() ? "ok" : "MISSING");
}
/* every 30 frames: windows created by other paths, or moved back */
void rig_win_space_frame(void) {
    if (!target_space) return;
    id app = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    if (!app) return;
    id wins = ((id (*)(id, SEL))objc_msgSend)(app, sel_registerName("windows"));
    unsigned long n = ((unsigned long (*)(id, SEL))objc_msgSend)(wins, sel_registerName("count"));
    for (unsigned long i = 0; i < n; i++) to_space(((id (*)(id, SEL, unsigned long))objc_msgSend)(wins, sel_registerName("objectAtIndex:"), i));
}
