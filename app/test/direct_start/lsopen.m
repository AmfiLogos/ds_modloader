// lsopen.m - lsopen <app> [KEY=VALUE]...: starts <app> through LaunchServices the way the Dock does (a new instance,
// never activated, not in Recent Items), with those variables. Waits up to 15 s for LaunchServices' answer.
#import <AppKit/AppKit.h>
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc < 2) return 2;
        NSWorkspaceOpenConfiguration *c = [NSWorkspaceOpenConfiguration configuration];
        c.createsNewApplicationInstance = YES; c.activates = NO; c.addsToRecentItems = NO; c.promptsUserIfNeeded = NO;
        NSMutableDictionary *env = [NSMutableDictionary dictionary];
        for (int i = 2; i < argc; i++) {
            char *eq = strchr(argv[i], '='); if (!eq) continue;
            env[[[NSString alloc] initWithBytes:argv[i] length:(NSUInteger)(eq - argv[i]) encoding:NSUTF8StringEncoding]] = @(eq + 1);
        }
        c.environment = env;
        dispatch_semaphore_t s = dispatch_semaphore_create(0);
        [[NSWorkspace sharedWorkspace] openApplicationAtURL:[NSURL fileURLWithPath:@(argv[1])] configuration:c
                                          completionHandler:^(NSRunningApplication *a, NSError *e) {
            printf("%s\n", a ? [[NSString stringWithFormat:@"PID %d", a.processIdentifier] UTF8String] : "NO ANSWER");
            dispatch_semaphore_signal(s);
        }];
        dispatch_semaphore_wait(s, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC));
    }
    return 0;
}
