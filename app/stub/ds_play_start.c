// ds_play_start.c - "dsml-start", the CFBundleExecutable of the play copy ROOT/games/<build>/play-modded/DungeonSettlers.app
// (dsml-helper puts it there: copy_mac::install_starter; the game binary "Dungeon Settlers" next to it stays as it is, the
// app's stub still execs that binary directly). macOS runs this file when it starts the copy as an app:
//   - the loader's soft restart (B through LaunchServices, loader/start/mac/dsl_soft_mac.c): the DYLD_ variables come
//     again as DSML_KEEP_DYLD_<x>. This file is restricted (a __RESTRICT segment, app/build_app.sh), so dyld injects
//     nothing into it and drops its DYLD_ variables; they are put back and the game binary runs (execv, same pid).
//   - any other start (the Dock icon kept from the running game, Spotlight, Launchpad, Finder): the game would run
//     without mods under the same name (owner 2026-10-24). It hands over to the app "Dungeon Settlers Modded": execv of
//     its stub, same pid, so the icon the player clicked is the game's. The app = the one the stub wrote into
//     ROOT/app_path.txt at its last start, else the one LaunchServices knows by its bundle id. DSMODDED_ROOT = this
//     copy's ROOT, so the stub uses the same folder.
// Log: ~/Library/Logs/DungeonSettlersModded.log (the stub's).
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <mach-o/dyld.h>
#define DSI_IMPLEMENTATION
#include "ds_i18n.h"   /* loader/src: TR() text in the game's language (ROOT/loader/lang/<code>.json) */
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define APP_ID "com.amfilogos.ds-modded"    /* the app's bundle id (app/build_app.sh) */
#define KEEP "DSML_KEEP_"

extern char **environ;
static char home[PATH_MAX], logpath[PATH_MAX];

static void logf_(const char *fmt, ...) {
    FILE *f = fopen(logpath, "a"); if (!f) return;
    time_t t = time(NULL); char ts[32]; strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    fprintf(f, "%s [%d] start: ", ts, getpid());
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

static void cut(char *p) { char *s = strrchr(p, '/'); if (s && s != p) *s = 0; }

static char *slurp(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (1 << 20)) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1); if (!b) { fclose(f); return NULL; }
    size_t r = fread(b, 1, (size_t)n, f); b[r] = 0; fclose(f);
    return b;
}

/* the stub of an app bundle whose id is APP_ID, into exe; 0 = found */
static int stub_of(const char *app, char *exe, size_t cap) {
    if (!*app || strstr(app, "/AppTranslocation/")) return -1;
    CFURLRef u = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)app, (CFIndex)strlen(app), true);
    CFBundleRef b = u ? CFBundleCreate(NULL, u) : NULL;
    if (u) CFRelease(u);
    if (!b) return -1;
    CFStringRef id = CFBundleGetIdentifier(b);
    CFURLRef e = CFBundleCopyExecutableURL(b);
    int ok = id && CFStringCompare(id, CFSTR(APP_ID), 0) == kCFCompareEqualTo && e
             && CFURLGetFileSystemRepresentation(e, true, (UInt8 *)exe, (CFIndex)cap) && access(exe, X_OK) == 0;
    if (e) CFRelease(e);
    CFRelease(b);
    return ok ? 0 : -1;
}

/* the app this copy belongs to: ROOT/app_path.txt (the stub's last start), else LaunchServices by bundle id */
static int find_stub(const char *root, char *exe, size_t cap) {
    if (root[0]) {
        char p[PATH_MAX]; snprintf(p, sizeof p, "%s/app_path.txt", root);
        char *t = slurp(p);
        if (t) {
            t[strcspn(t, "\r\n")] = 0;
            int ok = stub_of(t, exe, cap) == 0;
            if (!ok) logf_("%s names %s: not the app (moved or deleted?)", p, t);
            free(t);
            if (ok) return 0;
        }
    }
    CFArrayRef a = LSCopyApplicationURLsForBundleIdentifier(CFSTR(APP_ID), NULL);
    int found = -1;
    for (CFIndex i = 0; a && i < CFArrayGetCount(a) && found; i++) {
        char app[PATH_MAX];
        if (CFURLGetFileSystemRepresentation(CFArrayGetValueAtIndex(a, i), true, (UInt8 *)app, sizeof app)) found = stub_of(app, exe, cap);
    }
    if (a) CFRelease(a);
    return found;
}

/* msg_en: a TR_NOOP key (translated in loader/lang) */
static void notice(const char *root, const char *msg_en) {
    if (getenv("DSMP_NO_DIALOG")) { logf_("notice (not shown): %s", msg_en); return; }
    char d[PATH_MAX], us[PATH_MAX];                           /* the stub's way: ROOT/loader/lang, the game's language */
    snprintf(d, sizeof d, "%s/loader/lang", root); dsi_set_dir(d);
    snprintf(us, sizeof us, "%s/profiles/modded/data/UserSetting.json", root);
    char *t = slurp(us);
    if (!t) { snprintf(us, sizeof us, "%s/Library/Application Support/CanOpener/Dungeon Settlers/UserSetting.json", home); t = slurp(us); }
    dsi_set_lang(dsi_lang_from_usersetting(t)); free(t);
    char m[2048]; snprintf(m, sizeof m, "%s", TR(msg_en)); dsi_utf8_fix_end(m);
    CFStringRef s = CFStringCreateWithCString(NULL, m, kCFStringEncodingUTF8);
    CFStringRef ok = CFStringCreateWithCString(NULL, TR("OK"), kCFStringEncodingUTF8);
    CFOptionFlags resp;
    CFUserNotificationDisplayAlert(0, kCFUserNotificationStopAlertLevel, NULL, NULL, NULL, CFSTR("Dungeon Settlers Modded"),
                                   s ? s : CFSTR(""), ok ? ok : CFSTR("OK"), NULL, NULL, &resp);
    if (s) CFRelease(s);
    if (ok) CFRelease(ok);
}

int main(int argc, char **argv) {
    const char *h = getenv("HOME");
    if (!h || !*h) { struct passwd *pw = getpwuid(getuid()); h = pw ? pw->pw_dir : "/tmp"; }
    snprintf(home, sizeof home, "%s", h);
    snprintf(logpath, sizeof logpath, "%s/Library/Logs/DungeonSettlersModded.log", home);
    char exe[PATH_MAX], self[PATH_MAX]; uint32_t sz = sizeof exe;
    if (_NSGetExecutablePath(exe, &sz) != 0 || !realpath(exe, self)) { logf_("cannot resolve own path"); return 1; }
    char game[PATH_MAX]; snprintf(game, sizeof game, "%s", self); cut(game);
    snprintf(game + strlen(game), sizeof game - strlen(game), "/Dungeon Settlers");
    char **av = calloc((size_t)argc + 1, sizeof *av);
    if (!av) return 1;

    /* 1. the loader's start: DSML_KEEP_DYLD_<x>=v -> DYLD_<x>=v, then the game binary */
    int n = 0; for (char **e = environ; *e; e++) if (!strncmp(*e, KEEP "DYLD_", sizeof KEEP + 4)) n++;
    if (n) {
        char **kv = calloc((size_t)n, sizeof *kv); int k = 0;
        for (char **e = environ; *e && k < n; e++) if (!strncmp(*e, KEEP "DYLD_", sizeof KEEP + 4)) kv[k++] = strdup(*e);
        for (int i = 0; i < k; i++) {
            char *eq = kv[i] ? strchr(kv[i], '=') : NULL; if (!eq) continue;
            *eq = 0; setenv(kv[i] + sizeof KEEP - 1, eq + 1, 1); unsetenv(kv[i]);
        }
        av[0] = game; for (int i = 1; i < argc; i++) av[i] = argv[i];
        execv(game, av);
        logf_("exec %s failed (%s)", game, strerror(errno));
        return 1;
    }

    /* 2. a direct start: ROOT = 6 folders above this file's folder (ROOT/games/<build>/play-modded/X.app/Contents/MacOS) */
    char root[PATH_MAX]; snprintf(root, sizeof root, "%s", self);
    for (int i = 0; i < 6; i++) cut(root);
    const char *g = strrchr(root, '/');
    if (g && !strcmp(g + 1, "games")) cut(root); else root[0] = 0;
    char stub[PATH_MAX];
    if (find_stub(root, stub, sizeof stub)) {
        logf_("%s started directly: the app Dungeon Settlers Modded was not found", self);
        notice(root, TR_NOOP("Dungeon Settlers Modded was not found. Download it again, then open it."));
        return 1;
    }
    if (root[0]) setenv("DSMODDED_ROOT", root, 1);
    logf_("%s started directly: handing over to %s", self, stub);
    av[0] = stub; for (int i = 1; i < argc; i++) av[i] = argv[i];
    execv(stub, av);
    logf_("exec %s failed (%s)", stub, strerror(errno));
    notice(root, TR_NOOP("The game could not be started. See ~/Library/Logs/DungeonSettlersModded.log."));
    return 1;
}
