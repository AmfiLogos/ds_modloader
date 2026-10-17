// Notch fix, built into the Mac loader (always on; Windows has no notch). The same behaviour as the old separate
// notchfix.dylib (DungeonSettlersMods builtin "notch-fix", injected into GameAssembly with LC_LOAD_DYLIB): the game's
// PlayerWindow becomes a borderless window exactly as large as its screen's frame (macOS lets only such a window use the
// notch band), native full screen is turned off for it (a native full-screen window sits below the notch with a black
// band), and while the game is the active app the Dock and the menu bar are hidden (presentation options 0xA) so the
// auto-shown menu bar cannot slide over the notch band. Re-applied on window resize / move / key / main / exit full
// screen and app activation, plus every 0.5 s for the first 20 s (40 tries) while Unity sets its window up. The
// borderless game window stays able to be the key window (keyboard) and the main window.
// Only on a screen with a notch (safeAreaInsets.top > 0): on other screens the window is left as the game made it.
// Off: batchmode (no window), DSL_NOTCH=0.
#import <AppKit/AppKit.h>
#include "dsl_internal.h"
#include "dsl_os.h"
#include <objc/runtime.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

static BOOL applying;
static int tries;

/* NSWindow answers canBecomeKeyWindow / canBecomeMainWindow NO for a borderless window, and Unity's PlayerWindow
   (6000.0.58) does not override them: once the game lost the key window (another app in front, e.g. the Mods window's
   "open mods folder" showing Finder), it never got it back and the keyboard was dead until the game restarted
   (owner, 2026-10-18 00:13). The game window stays able to be key / main, except in a hidden soft restart B
   (dsl_soft_mac.c: never key / main while hidden) and when another module swapped NSWindow's answer (the test rig's
   window hiding in multiplayer: never key / main). */
int dsl_sos_windows_hidden(void);
static BOOL pw_can_become(id self, SEL s) {
    BOOL (*cur)(id, SEL) = (BOOL (*)(id, SEL))class_getMethodImplementation([NSWindow class], s);
    if (cur(self, s)) return YES;
    if (dsl_sos_windows_hidden()) return NO;
    Dl_info d, me; const char *img = dladdr((void *)cur, &d) && d.dli_fname ? d.dli_fname : "";
    return strstr(img, "/AppKit.framework/") || (dladdr((void *)pw_can_become, &me) && me.dli_fname && !strcmp(img, me.dli_fname));
}
static void keep_key_able(NSWindow *w) {
    static BOOL done; if (done) return; done = YES;
    Class c = [w class];                                                  /* PlayerWindow (not a KVO subclass) */
    for (NSString *sel in @[ @"canBecomeKeyWindow", @"canBecomeMainWindow" ]) {
        SEL s = NSSelectorFromString(sel);
        BOOL added = class_addMethod(c, s, (IMP)pw_can_become, "c@:");       /* NO: the game's class has its own */
        dsl_log("notch: %s %s for %s", sel.UTF8String, added ? "kept on while borderless" : "is the game's own", class_getName(c));
    }
}
/* the keyboard goes to the key window's first responder: what it is after each change (changes only, first 12) */
static void log_keys(NSWindow *w, const char *why) {
    static int n; static char last[300]; char cur[300];
    NSWindow *k = NSApp.keyWindow;
    snprintf(cur, sizeof cur, "key window %s (the game's %d, can be key %d), first responder %s, app active %d",
             k ? object_getClassName(k) : "none", k == w, w.canBecomeKeyWindow, w.firstResponder ? object_getClassName(w.firstResponder) : "none",
             NSApp.isActive);
    if (!strcmp(cur, last) || n >= 12) return;
    snprintf(last, sizeof last, "%s", cur); n++;
    dsl_log("notch: keys (%s): %s%s", why, cur, n == 12 ? "; further changes not logged" : "");
}

static NSWindow *player_window(void) {
    Class pw = NSClassFromString(@"PlayerWindow");
    for (NSWindow *w in NSApp.windows) if (pw && [w isKindOfClass:pw]) return w;
    return NSApp.mainWindow ? NSApp.mainWindow : NSApp.keyWindow;
}
/* Unity makes PlayerWindowView the first responder when it creates the window. Changing the style mask rebuilds the
   frame view and resets the first responder to the window itself, and AppKit sends mouseMoved: to the first responder:
   the game got no mouse moves (cursor stuck) until the first click. Give it back to the view; never take it from a
   real view. */
static void restore_first_responder(NSWindow *w) {
    if (w.firstResponder != w) return;
    Class pv = NSClassFromString(@"PlayerWindowView");
    NSView *target = nil;
    NSMutableArray<NSView *> *todo = [NSMutableArray array];
    if (pv && w.contentView) [todo addObject:w.contentView];
    while (todo.count && !target) {
        NSView *v = todo.firstObject; [todo removeObjectAtIndex:0];
        if ([v isKindOfClass:pv]) target = v; else [todo addObjectsFromArray:v.subviews];
    }
    if (!target) target = w.contentView;
    if (target && [w makeFirstResponder:target]) {
        static int m; if (++m <= 3) dsl_log("notch: mouse input back to %s", object_getClassName(target));
    }
}
static BOOL has_notch(NSScreen *s) {
    if (@available(macOS 12.0, *)) return s && s.safeAreaInsets.top > 0;
    return NO;
}

static void apply_frame(void) {
    if (applying) return;
    NSWindow *w = player_window();
    if (!w) return;
    NSScreen *s = w.screen ? w.screen : NSScreen.mainScreen;
    if (!has_notch(s)) return;
    applying = YES;
    /* no exception may leave this function: it runs inside notifications Unity posts during its own start, and an
       exception unwinding through Unity stops the start (black window, v0.1.11 on a notched screen) */
    @try {
        NSWindowCollectionBehavior cb = w.collectionBehavior;
        if (!(cb & NSWindowCollectionBehaviorFullScreenNone)) {
            w.collectionBehavior = (cb & ~(NSWindowCollectionBehaviorFullScreenPrimary | NSWindowCollectionBehaviorFullScreenAuxiliary))
                                   | NSWindowCollectionBehaviorFullScreenNone;
            static int m; if (++m <= 3) dsl_log("notch: native full screen off for the game window");
        }
        if (w.styleMask & NSWindowStyleMaskFullScreen) {           /* leave native full screen first; the exit notification re-applies */
            [w toggleFullScreen:nil];
            dsl_log("notch: the game window was in native full screen: leaving it");
            return;
        }
        NSRect f = s.frame;
        if (w.styleMask != NSWindowStyleMaskBorderless || !NSEqualRects(w.frame, f)) {
            keep_key_able(w);
            w.styleMask = NSWindowStyleMaskBorderless;
            w.acceptsMouseMovedEvents = YES;
            [w setFrame:f display:YES];
            [w makeKeyAndOrderFront:nil];
            if (w.canBecomeMainWindow) [w makeMainWindow];         /* a borderless NSWindow may refuse: AppKit asserts */
            static int n;                                           /* the game may set its size again: log the first 5 */
            if (++n <= 5) dsl_log("notch: game window borderless over the whole screen {%.0f,%.0f %.0fx%.0f} (notch band %.0f pt)%s",
                                  w.frame.origin.x, w.frame.origin.y, w.frame.size.width, w.frame.size.height, s.safeAreaInsets.top,
                                  n == 5 ? "; further re-applies not logged" : "");
        }
        restore_first_responder(w);
    } @catch (NSException *e) {
        static int k; if (++k <= 3) dsl_log("notch: window change refused by AppKit (%s: %s); the game goes on", e.name.UTF8String, e.reason.UTF8String);
    } @finally {
        applying = NO;
    }
}
static void apply_presentation(void) {
    if (!NSApp.isActive) return;
    NSWindow *w = player_window();
    if (!w || !has_notch(w.screen ? w.screen : NSScreen.mainScreen)) return;
    NSApplicationPresentationOptions want = NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar;
    @try { if (NSApp.presentationOptions != want) NSApp.presentationOptions = want; } @catch (id e) { (void)e; }
}
static void apply(const char *why) {
    apply_frame(); apply_presentation();
    @try { NSWindow *w = player_window(); if (w) log_keys(w, why); } @catch (id e) { (void)e; }
}

void dsl_notch_start(void) {
    const char *e = getenv("DSL_NOTCH");
    if ((e && *e == '0') || dsl_os_batchmode()) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        apply("start");
        for (NSNotificationName n in @[ NSWindowDidResizeNotification, NSWindowDidMoveNotification, NSWindowDidBecomeKeyNotification,
                                       NSWindowDidBecomeMainNotification, NSWindowDidExitFullScreenNotification,
                                       NSApplicationDidBecomeActiveNotification ])
            [NSNotificationCenter.defaultCenter addObserverForName:n object:nil queue:NSOperationQueue.mainQueue
                                                         usingBlock:^(NSNotification *note) { apply(note.name.UTF8String); }];
        for (NSNotificationName n in @[ NSWindowDidResignKeyNotification, NSApplicationDidResignActiveNotification ])   /* log only */
            [NSNotificationCenter.defaultCenter addObserverForName:n object:nil queue:NSOperationQueue.mainQueue
                                                         usingBlock:^(NSNotification *note) {
                @try { NSWindow *w = player_window(); if (w) log_keys(w, note.name.UTF8String); } @catch (id x) { (void)x; }
            }];
        [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *t) {
            apply("timer");
            if (++tries > 40) [t invalidate];
        }];
    });
}
