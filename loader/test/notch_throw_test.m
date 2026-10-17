// notch_throw_test.m: the built-in notch fix (start/mac/dsl_notch_mac.m) on a window that refuses to become main once it
// is borderless (AppKit asserts in makeMainWindow). v0.1.11 let that exception out of apply_frame inside Unity's start:
// the owner's game stopped on a black window. Here the exception would end this process (it runs in a main-queue block).
// Pass: the window ends borderless over the notched screen, the process lives, the next re-apply still runs.
// The window is invisible (alpha 0, ignores the mouse, lowest level) and the app never activates: the owner's screen
// does not change. Needs a screen with a notch; prints SKIP otherwise.
//   clang -fobjc-arc -framework AppKit -I loader/src loader/test/notch_throw_test.m loader/start/mac/dsl_notch_mac.m -o t && ./t
#import <AppKit/AppKit.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char last[600]; static int lines, borderless_lines;
void dsl_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(last, sizeof last, fmt, ap); va_end(ap);
    lines++; if (strstr(last, "borderless over the whole screen")) borderless_lines++;
    printf("log: %s\n", last); fflush(stdout);
}
int dsl_os_batchmode(void) { return 0; }
void dsl_notch_start(void);

@interface PlayerWindow : NSWindow   // the class name the notch code looks for; NSWindow's own canBecomeMainWindow
@end                                  // is NO for a borderless window
@implementation PlayerWindow
@end

int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];   // never active: no Dock / menu bar change
        NSScreen *ns = nil;
        if (@available(macOS 12.0, *)) for (NSScreen *s in NSScreen.screens) if (s.safeAreaInsets.top > 0) ns = s;
        if (!ns) { printf("SKIP no screen with a notch\n"); return 0; }
        NSRect r = NSMakeRect(NSMidX(ns.frame) - 200, NSMidY(ns.frame) - 150, 400, 300);
        PlayerWindow *w = [[PlayerWindow alloc] initWithContentRect:r styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                                            backing:NSBackingStoreBuffered defer:NO screen:ns];
        w.releasedWhenClosed = NO; w.alphaValue = 0; w.ignoresMouseEvents = YES; w.level = kCGDesktopWindowLevel - 1;
        [w orderBack:nil];
        dsl_notch_start();
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:2.0]];   // first apply + 0.5 s re-applies
        int first = lines;
        [w setFrame:r display:NO];                                                          // the game resizes: re-applied
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1.0]];
        BOOL ok = w.styleMask == NSWindowStyleMaskBorderless && NSEqualRects(w.frame, ns.frame) && borderless_lines >= 2 && first > 0;
        printf("%s window %s, frame %s, screen %s, borderless lines %d\n", ok ? "PASS" : "FAIL",
               w.styleMask == NSWindowStyleMaskBorderless ? "borderless" : "not borderless", NSStringFromRect(w.frame).UTF8String,
               NSStringFromRect(ns.frame).UTF8String, borderless_lines);
        [w orderOut:nil];
        return ok ? 0 : 1;
    }
}
