// quiet_test: start/mac/dsl_quiet_mac.c in a background-only test app (QuietTest.app, LSBackgroundOnly, activation
// policy Prohibited). Breaks it catches: a test copy's non-game window reaching the screen (guard kills, a window on the
// owner's screen), a test copy opening a URL / Steam (the owner's CrossOver Steam started), and the always-bound interposers
// changing anything while off (every player's game). Every window sits at -20000,-20000 with alpha 0 and
// every command only names a missing file, so nothing shows even if a hook fails. Run: test/quiet/run.sh
#import <AppKit/AppKit.h>
#include <dlfcn.h>
#include <spawn.h>
#include <stdbool.h>
extern char **environ;
@interface PlayerWindow : NSWindow @end
@implementation PlayerWindow @end
static int fails;
#define CHECK(c, what) do { int ok_ = (c); printf("%s %s\n", ok_ ? "ok  " : "FAIL", what); if (!ok_) fails++; } while (0)
static NSWindow *mk(Class c) {
    NSWindow *w = [[c alloc] initWithContentRect:NSMakeRect(-20000, -20000, 205, 127) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    w.alphaValue = 0; w.ignoresMouseEvents = YES; w.releasedWhenClosed = NO; w.title = @"quiet test"; return w;
}
static int onscreen(NSWindow *w) {
    CFArrayRef a = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, (CGWindowID)w.windowNumber); int on = 0;
    if (a && CFArrayGetCount(a)) { CFBooleanRef b = CFDictionaryGetValue(CFArrayGetValueAtIndex(a, 0), kCGWindowIsOnscreen); on = b && CFBooleanGetValue(b); }
    if (a) CFRelease(a); return on;
}
int main(int argc, char **argv) { @autoreleasepool {
    if (argc > 1 && !strcmp(argv[1], "cap")) return 0;          /* run.sh: the loader's copy cap let this copy start */
    int off = argc > 1 && !strcmp(argv[1], "off");
    [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    void *fs = dlopen(argv[0] ? [[[NSString stringWithUTF8String:argv[0]].stringByDeletingLastPathComponent stringByAppendingPathComponent:@"fakesteam.dylib"] UTF8String] : "", RTLD_NOW);
    CHECK(fs != NULL, "fakesteam.dylib loaded after launch");
    void *(*res)(void *, const char *) = dlsym(fs, "fs_resolve");
    void *(*ra)(void) = dlsym(fs, "fs_restart_addr"); void *real = ra(), *seen = res(fs, "SteamAPI_RestartAppIfNecessary");
    if (off) {
        CHECK(seen == real, "off: dlsym gives the real SteamAPI_RestartAppIfNecessary");
        CHECK(system("/usr/bin/true") == 0, "off: system() runs");
        NSWindow *o = mk([NSPanel class]); [o orderFront:nil]; CHECK(o.isVisible, "off: an ordinary window orders in"); [o orderOut:nil];
        return fails ? 1 : 0;
    }
    CHECK(seen && seen != real && ((bool (*)(unsigned))seen)(1) == false, "dlsym SteamAPI_RestartAppIfNecessary from a late image = stub returning false");
    int (*fo)(void) = dlsym(fs, "fs_open");
    CHECK(fo && fo() == 256, "system(\"/usr/bin/open ...\") from a late image is blocked");
    CHECK(system("/usr/bin/open 'steam://run/1'") == 256, "system steam:// blocked (never run)");
    CHECK(popen("open /nonexistent/dslquiet-popen", "r") == NULL, "popen open blocked");
    pid_t p; char *oa[] = { "/usr/bin/open", "/nonexistent/dslquiet-spawn", NULL }, *ta[] = { "/usr/bin/true", NULL };
    CHECK(posix_spawn(&p, oa[0], NULL, NULL, oa, environ) != 0, "posix_spawn /usr/bin/open blocked");
    int st = -1; CHECK(posix_spawn(&p, ta[0], NULL, NULL, ta, environ) == 0 && waitpid(p, &st, 0) == p && st == 0, "posix_spawn of another program still runs");
    CHECK(![[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:@"/nonexistent/dslquiet-ws"]], "NSWorkspace openURL blocked");
    __block int called = 0; __block id gotapp = @1;
    NSWorkspaceOpenConfiguration *cfg = [NSWorkspaceOpenConfiguration configuration]; cfg.activates = NO; cfg.promptsUserIfNeeded = NO;
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"steam://run/1"] configuration:cfg completionHandler:^(NSRunningApplication *a, NSError *e) { called++; gotapp = a; }];
    CHECK(called == 1 && gotapp == nil, "NSWorkspace openURL:configuration: steam:// blocked, completion called with nil");
    called = 0;
    [[NSWorkspace sharedWorkspace] openApplicationAtURL:[NSURL fileURLWithPath:@"/nonexistent/Other.app"] configuration:cfg completionHandler:^(NSRunningApplication *a, NSError *e) { called++; }];
    CHECK(called == 1, "openApplicationAtURL of another app blocked");
    NSWindow *g = mk([PlayerWindow class]); [g orderFront:nil];
    CHECK(g.isVisible, "game window (PlayerWindow) still orders in");
    NSWindow *s = mk([NSPanel class]);
    [s orderFront:nil]; CHECK(!s.isVisible && !onscreen(s), "orderFront: blocked");
    [s makeKeyAndOrderFront:nil]; CHECK(!s.isVisible, "makeKeyAndOrderFront: blocked");
    [s orderFrontRegardless]; CHECK(!s.isVisible, "orderFrontRegardless blocked");
    [s setIsVisible:YES]; CHECK(!s.isVisible, "setIsVisible: blocked");
    [s orderWindow:NSWindowAbove relativeTo:0]; CHECK(!s.isVisible && !onscreen(s), "orderWindow:relativeTo: blocked");
    NSTextView *tv = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 100, 20)]; [s.contentView addSubview:tv];
    [tv.inputContext activate]; CHECK(NSTextInputContext.currentInputContext != tv.inputContext, "text input context never activates");
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.6]];
    CHECK(g.isVisible && !s.isVisible, "after the sweep: game window in, other window out");
    return fails ? 1 : 0;
}}
