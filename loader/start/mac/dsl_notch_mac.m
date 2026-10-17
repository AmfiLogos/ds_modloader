// Notch fix, built into the Mac loader (always on; Windows has no notch). The same behaviour as the old separate
// notchfix.dylib (DungeonSettlersMods builtin "notch-fix", injected into GameAssembly with LC_LOAD_DYLIB): the game's
// PlayerWindow becomes a borderless window exactly as large as its screen's frame (macOS lets only such a window use the
// notch band), native full screen is turned off for it (a native full-screen window sits below the notch with a black
// band), and while the game is the active app the Dock and the menu bar are hidden (presentation options 0xA) so the
// auto-shown menu bar cannot slide over the notch band. Re-applied on window resize / move / key / main / exit full
// screen and app activation, plus every 0.5 s for the first 20 s (40 tries) while Unity sets its window up.
// Only on a screen with a notch (safeAreaInsets.top > 0): on other screens the window is left as the game made it.
// Off: batchmode (no window), DSL_NOTCH=0.
#import <AppKit/AppKit.h>
#include "dsl_internal.h"
#include "dsl_os.h"
#include <stdlib.h>

static BOOL applying;
static int tries;

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
static void apply(void) { apply_frame(); apply_presentation(); }

void dsl_notch_start(void) {
    const char *e = getenv("DSL_NOTCH");
    if ((e && *e == '0') || dsl_os_batchmode()) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        apply();
        for (NSNotificationName n in @[ NSWindowDidResizeNotification, NSWindowDidMoveNotification, NSWindowDidBecomeKeyNotification,
                                       NSWindowDidBecomeMainNotification, NSWindowDidExitFullScreenNotification,
                                       NSApplicationDidBecomeActiveNotification ])
            [NSNotificationCenter.defaultCenter addObserverForName:n object:nil queue:NSOperationQueue.mainQueue
                                                         usingBlock:^(NSNotification *note) { (void)note; apply(); }];
        [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *t) {
            apply();
            if (++tries > 40) [t invalidate];
        }];
    });
}
