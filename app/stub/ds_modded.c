// ds_modded.c - CFBundleExecutable "DS Modded" of "Dungeon Settlers Modded.app" (from rig/mp_launcher.c).
// No window of its own. Flow:
//   1. ROOT = $DSMODDED_ROOT or ~/Library/Application Support/DSModLauncher; PROFILE = ROOT/profiles/modded;
//      DATA = PROFILE/data (saves), UHOME = PROFILE/home, mods state = DATA/Mods/.
//   1b. auto-update (loader start/dsl_update_start.c, #included): a downloaded update is installed here, before the game,
//      with a small window (this binary again: --update-window); a new kit = this stub execs the new app's stub.
//   2. loader files: <app>/Contents/Resources/dsml/ -> ROOT/loader/ when VERSION differs (temp folder, xattr -cr,
//      rename into place; the old folder moves to ROOT/retired/, never written over: a running game maps it).
//   2b. the player's mods: ROOT/mods/<id> (or a <id>-<version>.zip there, unpacked) -> ROOT/loader/mods/<id>.
//   3. game copy: PROFILE/copy.json {"build","app"} when that copy exists (pinned), else
//      Contents/MacOS/dsml-helper prepare --root ROOT (last lines "BUILD <id>", "PLAY <path>" or "ERROR <msg>").
//      A pinned copy without "ident" COPY_IDENT gets its own Local Network identity: dsml-helper ident --app <copy>.
//   4. first run: COPY the normal saves (read only); default DATA/Mods/active.json + worlds.json. The Mods window is
//      part of the loader itself (not a module), so no entry of active.json can switch it off.
//   5. Steam (start it in the background when closed), env, execv of the play copy's game binary, cwd = PROFILE.
// DSMODDED_DRY_RUN=1: everything except ensure_steam and execv; prints the env and argv instead (tests).
// DSMP_NO_DIALOG=1: no dialogs. DSMODDED_NO_STEAM=1 (tests): Steam not checked. DSL_UPDATE_WINDOW=hidden (tests): the
// update window is made but never visible. Log: ~/Library/Logs/DungeonSettlersModded.log
#include <mach-o/dyld.h>
#include <CoreFoundation/CoreFoundation.h>
#define DSI_IMPLEMENTATION   /* before dsl_json.c (its dsl_internal.h includes ds_i18n.h): TR() tables of this stub */
#include "ds_i18n.h"   /* loader/src: TR() text in the game's language (lang/<code>.json) */
#include "../../loader/src/dsl_json.c"                 /* json_parse / json_get / json_str / json_free */
#include <copyfile.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char home[PATH_MAX], logpath[PATH_MAX], self_exe[PATH_MAX];
extern char **environ;

static void logf_(const char *fmt, ...) {
    FILE *f = fopen(logpath, "a"); if (!f) return;
    time_t t = time(NULL); char ts[32]; strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    fprintf(f, "%s [%d] ", ts, getpid());
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

/* the loader's auto-update parts built into this stub (they log through dsl_log) */
#include "../../loader/src/dsl_internal.h"
void dsl_log(const char *fmt, ...) {
    FILE *f = fopen(logpath, "a"); if (!f) return;
    time_t t = time(NULL); char ts[32]; strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    fprintf(f, "%s [%d] ", ts, getpid());
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}
#include "../../loader/start/mac/dsl_update_mac.c"
#include "../../loader/start/dsl_update_start.c"

static void mkdirs(const char *path) {
    char b[PATH_MAX]; snprintf(b, sizeof b, "%s", path);
    for (char *s = b + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(b, 0755); *s = '/'; }
    mkdir(b, 0755);
}

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static void dirname_n(char *p, int n) { while (n-- > 0) { char *s = strrchr(p, '/'); if (s && s != p) *s = 0; } }

static void notice(const char *msg) {
    if (getenv("DSMP_NO_DIALOG")) { logf_("notice (not shown): %s", msg); return; }
    char t[2048]; snprintf(t, sizeof t, "%s", msg); dsi_utf8_fix_end(t);
    CFStringRef m = CFStringCreateWithCString(NULL, t, kCFStringEncodingUTF8);
    CFStringRef ok = CFStringCreateWithCString(NULL, TR("OK"), kCFStringEncodingUTF8);
    CFOptionFlags resp;
    CFUserNotificationDisplayAlert(0, kCFUserNotificationStopAlertLevel, NULL, NULL, NULL,
                                   CFSTR("Dungeon Settlers Modded"), m, ok ? ok : CFSTR("OK"), NULL, NULL, &resp);
    if (m) CFRelease(m);
    if (ok) CFRelease(ok);
}

static int fail(const char *msg) { logf_("error: %s", msg); notice(msg); return 1; }

/* whole small file into a malloc'd string (NULL when missing) */
static char *slurp(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (1 << 22)) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1); if (!b) { fclose(f); return NULL; }
    size_t r = fread(b, 1, (size_t)n, f); b[r] = 0; fclose(f);
    return b;
}

/* the game's language for our dialogs: LanguageType of DATA/UserSetting.json (before the first start: the normal game's
   file, which that start copies in); no file = English. Texts: ROOT/loader/lang (the loader's copy), else the app's own
   Resources/dsml/lang. A missing <code>.json = English. */
static void setup_lang(const char *app, const char *root, const char *data) {
    char d[PATH_MAX], us[PATH_MAX];
    snprintf(d, sizeof d, "%s/loader/lang", root);
    if (!exists(d)) snprintf(d, sizeof d, "%s/Contents/Resources/dsml/lang", app);
    dsi_set_dir(d);
    snprintf(us, sizeof us, "%s/UserSetting.json", data);
    char *t = slurp(us);
    if (!t) { snprintf(us, sizeof us, "%s/Library/Application Support/CanOpener/Dungeon Settlers/UserSetting.json", home); t = slurp(us); }
    int lang = dsi_lang_from_usersetting(t);
    free(t);
    dsi_set_lang(lang);
    logf_("language %d (%s) from %s, texts %s: %d loaded, %d dropped", lang, dsi_code(), exists(us) ? us : "no UserSetting.json", d, dsi_loaded(), dsi_dropped());
}

static int write_atomic(const char *p, const char *text) {
    char t[PATH_MAX]; snprintf(t, sizeof t, "%s.tmp-write", p);
    FILE *f = fopen(t, "w"); if (!f) return -1;
    fputs(text, f);
    if (fclose(f) != 0) return -1;
    return rename(t, p);
}

static void trim(char *s) { size_t n = strlen(s); while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ')) s[--n] = 0; }

/* "key": "value" of a flat JSON object (enough for copy.json, which this stub writes itself) */
/* a top-level string member of a JSON text, read with the loader's own parser (loader/src/dsl_json.c, compiled into
   this file): the same reading as the loader and the Windows starter (a strstr found the first "id" anywhere) */
static int jtop(const char *text, const char *key, char *out, size_t cap) {
    JNode *j = text ? json_parse(text) : NULL; const char *v = json_str(j, key);
    int ok = v != NULL; if (ok) snprintf(out, cap, "%s", v);
    json_free(j); return ok;
}

static void json_esc(const char *s, char *out, size_t cap) {
    size_t n = 0;
    for (; *s && n + 3 < cap; s++) { if (*s == '"' || *s == '\\') out[n++] = '\\'; out[n++] = *s; }
    out[n] = 0;
}

/* run a command, wait; stdout captured into out (may be NULL), stderr appended to the log. Exit status or -1. */
static int run_cmd(char *const av[], char *out, size_t cap) {
    int pfd[2]; if (pipe(pfd) != 0) return -1;
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
    posix_spawn_file_actions_addclose(&fa, pfd[0]);
    posix_spawn_file_actions_addopen(&fa, 2, logpath, O_WRONLY | O_APPEND | O_CREAT, 0644);
    pid_t pid; int r = posix_spawn(&pid, av[0], &fa, NULL, av, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(pfd[1]);
    if (r != 0) { close(pfd[0]); logf_("cannot start %s (%s)", av[0], strerror(r)); return -1; }
    size_t n = 0; char buf[4096]; ssize_t k;
    while ((k = read(pfd[0], buf, sizeof buf)) > 0)
        if (out) for (ssize_t i = 0; i < k && n + 1 < cap; i++) out[n++] = buf[i];
    if (out) out[n] = 0;
    close(pfd[0]);
    int st; if (waitpid(pid, &st, 0) < 0) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void stamp(char *out, size_t cap) {
    time_t t = time(NULL); strftime(out, cap, "%Y%m%d-%H%M%S", localtime(&t));
}

/* 2. loader files from the app (may be translocated / read only) to ROOT/loader when the version differs */
static int sync_loader(const char *app, const char *root) {
    char src[PATH_MAX], sv[PATH_MAX], dst[PATH_MAX], dv[PATH_MAX];
    snprintf(src, sizeof src, "%s/Contents/Resources/dsml", app);
    snprintf(sv, sizeof sv, "%s/VERSION", src);
    snprintf(dst, sizeof dst, "%s/loader", root);
    snprintf(dv, sizeof dv, "%s/VERSION", dst);
    char *a = slurp(sv), *b = slurp(dv);
    if (!a) {
        free(b);
        if (exists(dst)) { logf_("app has no %s: keeping %s", sv, dst); return 0; }
        return fail(TR("This copy of Dungeon Settlers Modded is incomplete (no mod loader inside). Download it again."));
    }
    trim(a); if (b) trim(b);
    int same = b && !strcmp(a, b) && exists(dst);
    if (same) { free(a); free(b); return 0; }
    char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s/loader.new-%d", root, getpid());
    if (copyfile(src, tmp, NULL, COPYFILE_ALL | COPYFILE_RECURSIVE) != 0) {
        logf_("copy %s -> %s failed (%s)", src, tmp, strerror(errno));
        free(a); free(b);
        return fail(TR("The mod loader could not be copied (disk full or no write access to Application Support)."));
    }
    char *xav[] = { "/usr/bin/xattr", "-cr", tmp, NULL };   /* a downloaded app is quarantined: dyld would refuse */
    if (run_cmd(xav, NULL, 0) != 0) logf_("xattr -cr %s failed", tmp);
    if (exists(dst)) {
        char ret[PATH_MAX], ts[32]; stamp(ts, sizeof ts);
        snprintf(ret, sizeof ret, "%s/retired", root); mkdirs(ret);
        snprintf(ret, sizeof ret, "%s/retired/loader-%s-%d", root, ts, getpid());
        if (rename(dst, ret) != 0) {
            logf_("move %s -> %s failed (%s)", dst, ret, strerror(errno)); free(a); free(b);
            return fail(TR("The mod loader could not be updated (its folder is in use). Quit the game and try again."));
        }
    }
    if (rename(tmp, dst) != 0) {
        logf_("move %s -> %s failed (%s)", tmp, dst, strerror(errno)); free(a); free(b);
        return fail(TR("The mod loader could not be installed. See ~/Library/Logs/DungeonSettlersModded.log."));
    }
    logf_("loader %s installed (was %s)", a, b ? b : "none");
    free(a); free(b);
    return 0;
}

/* 1b. the update window: this binary again, `DS Modded --update-window <title>`, a child process (the stub itself never
   touches AppKit: it becomes the game by execv). Lines on its stdin: "P <0..100>" moves the bar, "Q" or EOF closes it.
   AppKit through the Objective-C runtime (dlsym), so the stub links only CoreFoundation. */
typedef struct { double x, y, w, h; } URect;
static void *(*o_cls)(const char *), *(*o_sel)(const char *), *o_send;
#define OMSG(ret, ...) ((ret (*)(void *, void *, ##__VA_ARGS__))o_send)
static void *nsstr(const char *s) { return OMSG(void *, const char *)(o_cls("NSString"), o_sel("stringWithUTF8String:"), s); }
static void *uw_bar;
static void uw_tick(CFRunLoopTimerRef t, void *info) {
    (void)t; (void)info; static char buf[256]; static size_t n;
    for (;;) {
        ssize_t k = read(0, buf + n, sizeof buf - 1 - n);
        if (k == 0) exit(0);                                  /* the stub is gone */
        if (k < 0) return;                                    /* nothing yet (non-blocking) */
        n += (size_t)k; buf[n] = 0;
        char *nl;
        while ((nl = strchr(buf, '\n'))) {
            *nl = 0;
            if (buf[0] == 'Q') exit(0);
            if (buf[0] == 'P') OMSG(void, double)(uw_bar, o_sel("setDoubleValue:"), atof(buf + 1));
            memmove(buf, nl + 1, strlen(nl + 1) + 1); n = strlen(buf);
        }
        if (n >= sizeof buf - 1) n = 0;
    }
}
static int update_window(const char *title) {
    if (!dlopen("/System/Library/Frameworks/AppKit.framework/AppKit", RTLD_NOW)) { logf_("update window: no AppKit"); return 1; }
    o_cls = (void *(*)(const char *))dlsym(RTLD_DEFAULT, "objc_getClass"); o_sel = (void *(*)(const char *))dlsym(RTLD_DEFAULT, "sel_registerName");
    o_send = dlsym(RTLD_DEFAULT, "objc_msgSend");
    if (!o_cls || !o_sel || !o_send) { logf_("update window: no Objective-C runtime"); return 1; }
    const char *hm = getenv("DSL_UPDATE_WINDOW"); int hidden = hm && !strcmp(hm, "hidden");
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    void *app = OMSG(void *)(o_cls("NSApplication"), o_sel("sharedApplication"));
    OMSG(void, long)(app, o_sel("setActivationPolicy:"), 1);                         /* accessory: no second Dock icon */
    void *w = OMSG(void *)(o_cls("NSWindow"), o_sel("alloc"));
    w = OMSG(void *, URect, unsigned long, unsigned long, signed char)(w, o_sel("initWithContentRect:styleMask:backing:defer:"),
                                                                    (URect){ 0, 0, 440, 110 }, 1 /* titled */, 2, 0);
    OMSG(void, void *)(w, o_sel("setTitle:"), nsstr("Dungeon Settlers Modded"));
    void *cv = OMSG(void *)(w, o_sel("contentView"));
    void *lab = OMSG(void *, void *)(o_cls("NSTextField"), o_sel("labelWithString:"), nsstr(title));
    OMSG(void, URect)(lab, o_sel("setFrame:"), (URect){ 24, 60, 392, 22 });
    OMSG(void, void *)(cv, o_sel("addSubview:"), lab);
    uw_bar = OMSG(void *)(o_cls("NSProgressIndicator"), o_sel("alloc"));
    uw_bar = OMSG(void *, URect)(uw_bar, o_sel("initWithFrame:"), (URect){ 24, 26, 392, 20 });
    OMSG(void, unsigned long)(uw_bar, o_sel("setStyle:"), 0);                          /* bar */
    OMSG(void, signed char)(uw_bar, o_sel("setIndeterminate:"), 0);
    OMSG(void, double)(uw_bar, o_sel("setMinValue:"), 0); OMSG(void, double)(uw_bar, o_sel("setMaxValue:"), 100);
    OMSG(void, double)(uw_bar, o_sel("setDoubleValue:"), 3);
    OMSG(void, void *)(cv, o_sel("addSubview:"), uw_bar);
    if (hidden) {                                                                       /* tests: made, never visible */
        OMSG(void, double)(w, o_sel("setAlphaValue:"), 0.0);
        OMSG(void, signed char)(w, o_sel("setIgnoresMouseEvents:"), 1);
        OMSG(void, void *)(w, o_sel("orderBack:"), NULL);
    } else {
        OMSG(void)(w, o_sel("center"));
        OMSG(void, void *)(w, o_sel("makeKeyAndOrderFront:"), NULL);
        OMSG(void, signed char)(app, o_sel("activateIgnoringOtherApps:"), 1);
    }
    logf_("update window up%s: %s", hidden ? " (hidden)" : "", title);
    CFRunLoopTimerRef t = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.05, 0.05, 0, 0, uw_tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetMain(), t, kCFRunLoopCommonModes);
    OMSG(void)(app, o_sel("run"));
    return 0;
}
/* the stub's side of the window */
static pid_t uw_pid; static int uw_fd = -1;
static void uw_open(const char *title) {
    int pfd[2]; if (pipe(pfd)) return;
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pfd[0], 0); posix_spawn_file_actions_addclose(&fa, pfd[1]);
    char *av[] = { self_exe, "--update-window", (char *)title, NULL };
    int r = posix_spawn(&uw_pid, self_exe, &fa, NULL, av, environ);
    posix_spawn_file_actions_destroy(&fa); close(pfd[0]);
    if (r) { close(pfd[1]); uw_pid = 0; logf_("update window: cannot start (%s)", strerror(r)); return; }
    signal(SIGPIPE, SIG_IGN); uw_fd = pfd[1];
}
static void uw_progress(int pct) { if (uw_fd >= 0) dprintf(uw_fd, "P %d\n", pct); }
static void uw_close(void) {
    if (uw_fd < 0) return;
    dprintf(uw_fd, "Q\n"); close(uw_fd); uw_fd = -1;
    for (int i = 0; i < 60; i++) { if (waitpid(uw_pid, NULL, WNOHANG) == uw_pid) { uw_pid = 0; break; } usleep(50000); }
    if (uw_pid) { kill(uw_pid, SIGKILL); waitpid(uw_pid, NULL, 0); uw_pid = 0; }
    signal(SIGPIPE, SIG_DFL);                                /* the game must not inherit SIG_IGN */
}

/* "2.0.0" < "2.1.0" (numbers, a leading v ignored); -1 / 0 / 1 */
static int vcmp(const char *a, const char *b) {
    for (;;) {
        while (*a && (*a < '0' || *a > '9')) a++;
        while (*b && (*b < '0' || *b > '9')) b++;
        if (!*a && !*b) return 0;
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if ((*a < '0' || *a > '9') && (*b < '0' || *b > '9')) return 0;
    }
}

/* 2b. the player's mods: ROOT/mods (the Mods window's "Open mods folder"). A downloaded <id>-<version>.zip there is
   unpacked to ROOT/mods/<id> (the zip moves to ROOT/mods/.installed/, a bad one to .failed/). Every ROOT/mods/<id>
   whose mod.json "id" is <id> is copied into ROOT/loader/mods/<id> when its mod.json differs (marker .player_mod
   inside the copy). A player mod taken out of ROOT/mods leaves ROOT/loader/mods too (the app's own copy of that id,
   when it has one, comes back). Replaced folders move to ROOT/retired/. A new mod is off until the player turns it on. */
static int valid_id(const char *s) {
    if (!*s || *s == '.') return 0;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '.' || *s == '_' || *s == '-')) return 0;
    return 1;
}
static int retire(const char *root, const char *path, const char *label) {
    char ret[PATH_MAX], ts[32]; stamp(ts, sizeof ts);
    snprintf(ret, sizeof ret, "%s/retired", root); mkdirs(ret);
    snprintf(ret, sizeof ret, "%s/retired/%s-%s-%d", root, label, ts, getpid());
    if (rename(path, ret) == 0) return 0;
    logf_("move %s -> %s failed (%s)", path, ret, strerror(errno)); return -1;
}
/* copy dir src to dst (temp + rename; quarantine cleared: dyld refuses a quarantined dylib); marker = .player_mod */
static int copy_in(const char *root, const char *src, const char *dst, const char *label, int marker) {
    char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s.new-%d", dst, getpid());
    if (copyfile(src, tmp, NULL, COPYFILE_ALL | COPYFILE_RECURSIVE) != 0) { logf_("copy %s -> %s failed (%s)", src, tmp, strerror(errno)); return -1; }
    char *xav[] = { "/usr/bin/xattr", "-cr", tmp, NULL };
    if (run_cmd(xav, NULL, 0) != 0) logf_("xattr -cr %s failed", tmp);
    if (marker) { char m[PATH_MAX]; snprintf(m, sizeof m, "%s/.player_mod", tmp); write_atomic(m, "ROOT/mods\n"); }
    if (exists(dst) && retire(root, dst, label)) return -1;
    if (rename(tmp, dst) != 0) { logf_("move %s -> %s failed (%s)", tmp, dst, strerror(errno)); return -1; }
    return 0;
}
/* the folder holding mod.json: dir itself or one folder down; id read into id (empty = none) */
static void find_mod(const char *dir, char *mdir, size_t cap, char *id, size_t icap) {
    char mj[PATH_MAX]; id[0] = 0;
    snprintf(mj, sizeof mj, "%s/mod.json", dir);
    char *j = slurp(mj);
    if (j) { snprintf(mdir, cap, "%s", dir); if (!jtop(j, "id", id, icap)) id[0] = 0; free(j); return; }
    DIR *d = opendir(dir); struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "__MACOSX")) continue;
        snprintf(mj, sizeof mj, "%s/%s/mod.json", dir, e->d_name);
        if (!(j = slurp(mj))) continue;
        snprintf(mdir, cap, "%s/%s", dir, e->d_name); if (!jtop(j, "id", id, icap)) id[0] = 0; free(j); break;
    }
    if (d) closedir(d);
}
static void user_mods(const char *app, const char *root) {
    char um[PATH_MAX], lm[PATH_MAX], p[PATH_MAX], q[PATH_MAX], id[64], mdir[PATH_MAX];
    snprintf(um, sizeof um, "%s/mods", root); mkdirs(um);
    snprintf(lm, sizeof lm, "%s/loader/mods", root);
    {   /* new versions the running game could not take (loader/ui/dsl_pkg.c): ROOT/mods/.pending/<id> -> ROOT/mods/<id> */
        char pd[PATH_MAX]; snprintf(pd, sizeof pd, "%s/.pending", um);
        DIR *d = opendir(pd); struct dirent *e;
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.' || !valid_id(e->d_name)) continue;
            snprintf(p, sizeof p, "%s/%s", pd, e->d_name); snprintf(q, sizeof q, "%s/%s", um, e->d_name);
            if (exists(q) && retire(root, q, e->d_name)) continue;
            if (rename(p, q) == 0) logf_("mods: new version of %s installed (from .pending)", e->d_name);
        }
        if (d) closedir(d);
    }
    struct dirent **ents = NULL; int ne = scandir(um, &ents, NULL, alphasort);
    for (int i = 0; i < ne; i++) {                                     /* zips -> ROOT/mods/<id> */
        const char *n = ents[i]->d_name; size_t l = strlen(n);
        if (n[0] == '.' || l < 5 || strcasecmp(n + l - 4, ".zip")) continue;
        char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s/.unpack-%d", um, getpid());
        snprintf(p, sizeof p, "%s/%s", um, n);
        char *dav[] = { "/usr/bin/ditto", "-x", "-k", p, tmp, NULL };
        int ok = run_cmd(dav, NULL, 0) == 0;
        if (ok) find_mod(tmp, mdir, sizeof mdir, id, sizeof id);
        ok = ok && valid_id(id);
        if (ok) {
            snprintf(q, sizeof q, "%s/%s", um, id);
            ok = !(exists(q) && retire(root, q, id)) && rename(mdir, q) == 0;
        }
        snprintf(q, sizeof q, "%s/%s", um, ok ? ".installed" : ".failed"); mkdirs(q);
        snprintf(q + strlen(q), sizeof q - strlen(q), "/%s", n);
        if (exists(q)) retire(root, q, n);
        if (rename(p, q) != 0) logf_("move %s -> %s failed (%s)", p, q, strerror(errno));
        if (exists(tmp) && rmdir(tmp) != 0) retire(root, tmp, "unpack");     /* empty after a nested zip */
        if (ok) logf_("mods: %s installed as mods/%s", n, id);
        else {
            logf_("mods: %s is not a mod package (no mod.json with a good id)", n);
            char msg[PATH_MAX + 128];
            snprintf(msg, sizeof msg, TR("%s is not a mod for Dungeon Settlers Modded. It was moved to the folder mods/.failed."), n);
            dsi_utf8_fix_end(msg); notice(msg);
        }
    }
    for (int i = 0; i < ne; i++) free(ents[i]);
    free(ents);
    ne = scandir(um, &ents, NULL, alphasort);
    for (int i = 0; i < ne; i++) {                                     /* ROOT/mods/<id> -> ROOT/loader/mods/<id> */
        const char *n = ents[i]->d_name;
        if (n[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s/mod.json", um, n);
        char *a = slurp(p);
        if (!a) continue;
        if (!jtop(a, "id", id, sizeof id) || strcmp(id, n) || !valid_id(id)) { logf_("mods: %s/%s skipped (mod.json id is not the folder name)", um, n); free(a); continue; }
        snprintf(q, sizeof q, "%s/%s/mod.json", lm, n);
        char *b = slurp(q);
        char va[32] = "", vb[32] = "";
        if (b && jtop(a, "version", va, sizeof va) && jtop(b, "version", vb, sizeof vb) && vcmp(va, vb) < 0) {
            logf_("mods: player mod %s %s is older than the loader's %s (auto-update): not copied", n, va, vb); free(a); free(b); continue;
        }
        if (!b || strcmp(a, b)) {
            snprintf(p, sizeof p, "%s/%s", um, n); snprintf(q, sizeof q, "%s/%s", lm, n);
            if (copy_in(root, p, q, n, 1) == 0) logf_("mods: player mod %s copied into the loader", n);
        }
        free(a); free(b);
    }
    for (int i = 0; i < ne; i++) free(ents[i]);
    free(ents);
    ne = scandir(lm, &ents, NULL, alphasort);
    for (int i = 0; i < ne; i++) {                                     /* player mods taken out of ROOT/mods */
        const char *n = ents[i]->d_name;
        snprintf(p, sizeof p, "%s/%s/.player_mod", lm, n); snprintf(q, sizeof q, "%s/%s/mod.json", um, n);
        if (n[0] == '.' || !exists(p) || exists(q)) continue;
        snprintf(p, sizeof p, "%s/%s", lm, n);
        if (retire(root, p, n)) continue;
        logf_("mods: player mod %s removed", n);
        snprintf(q, sizeof q, "%s/Contents/Resources/dsml/mods/%s", app, n);
        if (exists(q) && copy_in(root, q, p, n, 0) == 0) logf_("mods: the app's own %s is back", n);
    }
    for (int i = 0; i < ne; i++) free(ents[i]);
    free(ents);
}

/* copy.json; ident = the copy has dsml-helper's identity (copy_mac::COPY_IDENT) */
#define COPY_IDENT "1"
static void copy_json_write(const char *cj, const char *build, const char *play) {
    char eb[256], ep[PATH_MAX * 2], txt[PATH_MAX * 3];
    json_esc(build, eb, sizeof eb); json_esc(play, ep, sizeof ep);
    snprintf(txt, sizeof txt, "{\"build\": \"%s\", \"app\": \"%s\", \"ident\": \"" COPY_IDENT "\"}\n", eb, ep);
    if (write_atomic(cj, txt) != 0) logf_("cannot write %s (%s)", cj, strerror(errno));
}

/* 3. the play copy: the remembered one (pinned) or a new one made by dsml-helper */
static int game_copy(const char *app, const char *root, const char *profile, char *play, size_t pcap,
                     char *build, size_t bcap) {
    char cj[PATH_MAX]; snprintf(cj, sizeof cj, "%s/copy.json", profile);
    char *j = slurp(cj);
    if (j && jtop(j, "build", build, bcap) && jtop(j, "app", play, pcap)) {
        char exe[PATH_MAX]; snprintf(exe, sizeof exe, "%s/Contents/MacOS/Dungeon Settlers", play);
        char id[16] = "";
        if (exists(exe) && jtop(j, "ident", id, sizeof id) && !strcmp(id, COPY_IDENT)) { free(j); return 0; }
        if (exists(exe)) {
            /* copy made before the Local Network identity: macOS blocked it on the local network without asking */
            free(j);
            char helper[PATH_MAX], out[4096];
            snprintf(helper, sizeof helper, "%s/Contents/MacOS/dsml-helper", app);
            char *av[] = { helper, "ident", "--app", play, NULL };
            int st = run_cmd(av, out, sizeof out);
            if (st == 0 && strstr(out, "IDENT " COPY_IDENT)) { logf_("copy %s: own identity given", play); copy_json_write(cj, build, play); }
            else logf_("copy %s: dsml-helper ident exit %d: %s", play, st, out);   /* the game still starts */
            return 0;
        }
        logf_("remembered copy %s is gone: making a new one", play);
    }
    free(j);
    char helper[PATH_MAX], icon[PATH_MAX];
    snprintf(helper, sizeof helper, "%s/Contents/MacOS/dsml-helper", app);
    snprintf(icon, sizeof icon, "%s/Contents/Resources/AppIcon.icns", app);
    char *av[8]; int k = 0;
    av[k++] = helper; av[k++] = "prepare"; av[k++] = "--root"; av[k++] = (char *)root;
    if (exists(icon)) { av[k++] = "--icon"; av[k++] = icon; }
    av[k] = NULL;
    static char out[65536];
    logf_("preparing the game copy (%s)", helper);
    time_t t0 = time(NULL);
    int st = run_cmd(av, out, sizeof out);
    build[0] = play[0] = 0;
    char err[2048] = "";
    for (char *line = strtok(out, "\n"); line; line = strtok(NULL, "\n")) {
        if (!strncmp(line, "BUILD ", 6)) snprintf(build, bcap, "%s", line + 6);
        else if (!strncmp(line, "PLAY ", 5)) snprintf(play, pcap, "%s", line + 5);
        else if (!strncmp(line, "ERROR ", 6)) snprintf(err, sizeof err, "%s", line + 6);
    }
    if (st != 0 || !build[0] || !play[0]) {
        logf_("dsml-helper exit %d", st);
        return fail(err[0] ? err : TR("The game copy could not be made. See ~/Library/Logs/DungeonSettlersModded.log."));   /* err: dsml-helper's English */
    }
    logf_("game copy ready in %ld s: build %s, %s", (long)(time(NULL) - t0), build, play);
    copy_json_write(cj, build, play);
    return 0;
}

/* 4. mods state: defaults only; an existing active.json is the player's choice and stays as it is.
   Default profile = every mod in ROOT/loader/mods (name order) except those whose mod.json says "default_on": false
   (Hell difficulty ships off), the same list the Windows starter writes (same mod set on both for multiplayer). */
static int default_off(const char *j) {
    const char *p = j ? strstr(j, "\"default_on\"") : NULL; if (!p) return 0;
    p += 12; while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p++ != ':') return 0;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return !strncmp(p, "false", 5);
}
static void mods_state(const char *root, const char *data) {
    char md[PATH_MAX], act[PATH_MAX], wl[PATH_MAX];
    snprintf(md, sizeof md, "%s/Mods", data); mkdirs(md);
    snprintf(act, sizeof act, "%s/active.json", md);
    snprintf(wl, sizeof wl, "%s/worlds.json", md);
    if (!exists(wl) && write_atomic(wl, "{\"worlds\":{}}\n") != 0) logf_("cannot write %s", wl);
    if (exists(act)) return;
    char mods[PATH_MAX], ids[4096] = "", id[64]; int n = 0, off = 0;
    snprintf(mods, sizeof mods, "%s/loader/mods", root);
    struct dirent **ents = NULL; int ne = scandir(mods, &ents, NULL, alphasort);
    for (int i = 0; i < ne; i++) {
        char mj[PATH_MAX]; snprintf(mj, sizeof mj, "%s/%s/mod.json", mods, ents[i]->d_name);
        char *j = ents[i]->d_name[0] != '.' ? slurp(mj) : NULL;
        if (j && jtop(j, "id", id, sizeof id) && *id && !strpbrk(id, "\"\\")) {
            if (default_off(j)) off++;
            else if (strlen(ids) + strlen(id) + 4 < sizeof ids) {
                snprintf(ids + strlen(ids), sizeof ids - strlen(ids), "%s\"%s\"", n ? "," : "", id); n++;
            }
        }
        free(j); free(ents[i]);
    }
    free(ents);
    char txt[sizeof ids * 2 + 64]; snprintf(txt, sizeof txt, "{\"enabled\":[%s],\"order\":[%s]}\n", ids, ids);
    if (write_atomic(act, txt) != 0) logf_("cannot write %s", act);
    else logf_("first start: default mod profile, %d mod(s) on, %d off by default", n, off);
}

/* Asks the game's own libsteam_api in a child process (this process later becomes the game: it must not
   hold a Steam connection). init=0: SteamAPI_IsSteamRunning. init=1: SteamAPI_InitFlat + Shutdown.
   1 = yes, 0 = no, -1 = cannot ask. */
static int steam_probe(const char *lib, int init) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        void *h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
        if (!h) { logf_("dlopen %s: %s", lib, dlerror()); _exit(3); }
        if (!init) { int (*run)(void) = (int (*)(void))dlsym(h, "SteamAPI_IsSteamRunning"); _exit(!run ? 3 : run() ? 0 : 1); }
        int (*ini)(char *) = (int (*)(char *))dlsym(h, "SteamAPI_InitFlat");
        void (*shut)(void) = (void (*)(void))dlsym(h, "SteamAPI_Shutdown");
        if (!ini || !shut) _exit(3);
        char err[1024] = "";
        int r = ini(err);
        if (r == 0) { shut(); _exit(0); }
        logf_("steam probe: init result %d (%s)", r, err);
        _exit(1);
    }
    for (int i = 0; i < 300; i++) {
        int st; pid_t w = waitpid(pid, &st, WNOHANG);
        if (w == pid) return WIFEXITED(st) ? (WEXITSTATUS(st) == 0 ? 1 : WEXITSTATUS(st) == 1 ? 0 : -1) : -1;
        if (w < 0) return -1;
        usleep(50000);
    }
    kill(pid, SIGKILL); waitpid(pid, NULL, 0);
    logf_("steam probe %d timed out", init);
    return 0;
}

/* Steam not running: start it in the background and wait. 0 = go on, 1 = Steam never got ready (notice shown). */
static int ensure_steam(const char *app) {
    char lib[PATH_MAX]; snprintf(lib, sizeof lib, "%s/Contents/PlugIns/libsteam_api.dylib", app);
    if (!exists(lib)) { logf_("no %s: Steam not checked", lib); return 0; }
    int run = steam_probe(lib, 0);
    if (run != 0) { if (run < 0) logf_("cannot ask libsteam_api: Steam not checked"); return 0; }
    const char *sapp = getenv("DSMP_STEAM_APP"); if (!sapp || !*sapp) sapp = "Steam";
    const char *w = getenv("DSMP_STEAM_WAIT"); int wait_s = w ? atoi(w) : 90;
    logf_("Steam is not running: starting %s in the background (wait up to %d s)", sapp, wait_s);
    char *oav[] = { "/usr/bin/open", "-g", "-a", (char *)sapp, NULL };
    pid_t op; int st = 1;
    if (posix_spawn(&op, oav[0], NULL, NULL, oav, environ) == 0) waitpid(op, &st, 0);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) {
        logf_("open -a %s failed (status %d)", sapp, st);
        notice(TR("Steam could not be started. Install Steam, start it and sign in, then open Dungeon Settlers Modded again."));
        return 1;
    }
    time_t t0 = time(NULL);
    while (time(NULL) - t0 < wait_s) {
        sleep(2);
        if (steam_probe(lib, 0) == 1 && steam_probe(lib, 1) == 1) {
            logf_("Steam ready after %ld s", (long)(time(NULL) - t0));
            sleep(1);
            return 0;
        }
    }
    logf_("Steam not ready after %d s", wait_s);
    notice(TR("Steam did not get ready. Start Steam and sign in, then open Dungeon Settlers Modded again."));
    return 1;
}

/* 6. BepInEx (kind "bepinex" mods, lane bepinexmac): when an enabled mod is a BepInEx plugin, install BepInEx at first
   use (app Resources/dsml/install_bepinex.sh -> ROOT/bepinex/mac-arm64/<ver>) and start the game through Doorstop.
   The loader gets the same paths for its soft restarts (DSML_BEPINEX_DIR / _SCRIPT, DSML_DSLOAD). */
#define BIE_VER "0.1.0"
static void bg_notification(const char *text) {             /* non-blocking banner; the game start goes on */
    if (getenv("DSMP_NO_DIALOG")) { logf_("notification (not shown): %s", text); return; }
    /* the text goes inside an AppleScript string: \ and " escaped; cut at a whole character to fit the 600-byte script */
    static const char head[] = "display notification \"", tail[] = "\" with title \"Dungeon Settlers Modded\"";
    char sc[600], t[600], esc[1200];
    size_t room = sizeof sc - (sizeof head - 1) - (sizeof tail - 1), cap = room;   /* bytes for the escaped text + NUL */
    for (;;) {
        snprintf(t, sizeof t, "%s", text); dsi_utf8_clip(t, cap < sizeof t ? cap : sizeof t);
        size_t n = 0;
        for (const char *c = t; *c && n + 2 < sizeof esc; c++) { if (*c == '"' || *c == '\\') esc[n++] = '\\'; esc[n++] = *c; }
        esc[n] = 0;
        if (n < room || cap <= 1) break;
        cap -= n - room + 1;                                   /* escapes made it longer: cut the text more */
    }
    snprintf(sc, sizeof sc, "%s%s%s", head, esc, tail);
    char *av[] = { "/usr/bin/osascript", "-e", sc, NULL }; pid_t pid;
    if (posix_spawn(&pid, av[0], NULL, NULL, av, environ) == 0) logf_("notification: %s", text);
}
/* MelonLoader (kind "melon" mods, lane melon v13): the same way, our arm64 MelonLoader build (app Resources/dsml/
   install_melon.sh -> ROOT/melonloader/mac-arm64/<ver>), Bootstrap after dsload in DYLD_INSERT_LIBRARIES. One .NET host
   per start: a profile with both kinds starts the BepInEx ones (the loader's Mods window refuses such a set). */
#define ML_VER "0.1.0"
/* the enabled mods of one kind ("bepinex" / "melon") in profile order, as "<mods>/<id>:..." folders; an id that is not
   a plain mod id (a-z 0-9 . _ -, as the starters accept) is skipped, never put into a path */
static int kind_dirs(const char *mods, const char *act, const char *kind, char *dirs, size_t cap) {
    char *t = slurp(act); JNode *j = t ? json_parse(t) : NULL; free(t);
    JNode *en = json_get(j, "enabled"); int n = 0; dirs[0] = 0;
    for (JNode *c = en && en->t == JN_ARR ? en->child : NULL; c; c = c->next) {
        if (c->t != JN_STR) continue;
        if (!valid_id(c->str)) { logf_("mods: profile id '%s' is not a mod id: skipped", c->str); continue; }
        char mj[PATH_MAX], k[32]; snprintf(mj, sizeof mj, "%s/%s/mod.json", mods, c->str);
        char *m = slurp(mj); int hit = m && jtop(m, "kind", k, sizeof k) && !strcmp(k, kind); free(m);
        if (!hit) continue;
        if (n++) strlcat(dirs, ":", cap);
        strlcat(dirs, mods, cap); strlcat(dirs, "/", cap); strlcat(dirs, c->str, cap);
    }
    json_free(j); return n;
}
static int melon_launch(const char *root, const char *data, char *dyld, size_t cap, int allowed) {
    char mdir[PATH_MAX], script[PATH_MAX], act[PATH_MAX], mods[PATH_MAX];
    snprintf(mdir, sizeof mdir, "%s/melonloader/mac-arm64/" ML_VER, root);
    snprintf(script, sizeof script, "%s/loader/install_melon.sh", root);
    snprintf(mods, sizeof mods, "%s/loader/mods", root);
    setenv("DSML_MELON_DIR", mdir, 1); setenv("DSML_MELON_SCRIPT", script, 1);
    snprintf(act, sizeof act, "%s/Mods/active.json", data);
    char dirs[8192]; int n = kind_dirs(mods, act, "melon", dirs, sizeof dirs);
    if (!n) return 0;
    if (!allowed) { logf_("melon: %d mod(s) off this start: BepInEx mods run (one .NET host per start)", n); return 0; }
    char probe[PATH_MAX]; snprintf(probe, sizeof probe, "%s/MelonLoader.Bootstrap.dylib", mdir);
    if (!exists(probe)) {
        bg_notification(TR("Preparing mods (first start): downloading MelonLoader (52 MB)."));
        char out[2048] = ""; char *av[] = { "/bin/zsh", script, mdir, NULL };
        int r = run_cmd(av, out, sizeof out);
        if (r != 0) {
            char *er = strstr(out, "ERROR "); char msg[900];
            snprintf(msg, sizeof msg, TR("MelonLoader could not be installed (%s). The game starts without the MelonLoader mods."), er ? er + 6 : TR("unknown error"));
            for (char *c = msg; *c; c++) if (*c == '\n') *c = ' ';
            dsi_utf8_fix_end(msg); logf_("melon install failed: %s", out); notice(msg); return 0;
        }
        logf_("melon installed into %s", mdir);
    }
    char ready[PATH_MAX]; snprintf(ready, sizeof ready, "%s/ds_ready_%s", mdir, getenv("DSML_BUILD") ? getenv("DSML_BUILD") : "");
    if (!exists(ready)) bg_notification(TR("Preparing mods (first start): MelonLoader prepares its files for this game version. About 2 minutes, once."));
    snprintf(dyld + strlen(dyld), cap - strlen(dyld), ":%s/MelonLoader.Bootstrap.dylib", mdir);
    setenv("MELONLOADER_BASEDIR", mdir, 1);
    setenv("MELONLOADER_MOD_DIRS", dirs, 1);
    logf_("melon: %d mod folder(s): %s", n, dirs);
    return n;
}
static int bepinex_launch(const char *root, const char *data, char *dyld, size_t cap) {
    char bdir[PATH_MAX], script[PATH_MAX], act[PATH_MAX], mods[PATH_MAX];
    snprintf(bdir, sizeof bdir, "%s/bepinex/mac-arm64/" BIE_VER, root);
    snprintf(script, sizeof script, "%s/loader/install_bepinex.sh", root);
    snprintf(mods, sizeof mods, "%s/loader/mods", root);
    setenv("DSML_BEPINEX_DIR", bdir, 1); setenv("DSML_BEPINEX_SCRIPT", script, 1);
    snprintf(act, sizeof act, "%s/Mods/active.json", data);
    char dirs[8192]; int n = kind_dirs(mods, act, "bepinex", dirs, sizeof dirs);
    if (!n) return 0;
    char probe[PATH_MAX]; snprintf(probe, sizeof probe, "%s/libdoorstop.dylib", bdir);
    if (!exists(probe)) {
        bg_notification(TR("Preparing mods (first start): downloading BepInEx (31 MB)."));
        char out[2048] = ""; char *av[] = { "/bin/zsh", script, bdir, NULL };
        int r = run_cmd(av, out, sizeof out);
        if (r != 0) {
            char *er = strstr(out, "ERROR "); char msg[900];
            snprintf(msg, sizeof msg, TR("BepInEx could not be installed (%s). The game starts without the BepInEx mods."), er ? er + 6 : TR("unknown error"));
            for (char *c = msg; *c; c++) if (*c == '\n') *c = ' ';
            dsi_utf8_fix_end(msg); logf_("bepinex install failed: %s", out); notice(msg); return 0;
        }
        logf_("bepinex installed into %s", bdir);
    }
    char ready[PATH_MAX]; snprintf(ready, sizeof ready, "%s/ds_ready_%s", bdir, getenv("DSML_BUILD") ? getenv("DSML_BUILD") : "");
    if (!exists(ready)) bg_notification(TR("Preparing mods (first start): BepInEx prepares its files for this game version. About 2 minutes, once."));
    char v[PATH_MAX];
    snprintf(dyld + strlen(dyld), cap - strlen(dyld), ":%s/libdoorstop.dylib", bdir);
    setenv("DOORSTOP_ENABLED", "1", 1); setenv("DOORSTOP_IGNORE_DISABLED_ENV", "0", 1);
    snprintf(v, sizeof v, "%s/BepInEx/core/BepInEx.Unity.IL2CPP.dll", bdir); setenv("DOORSTOP_TARGET_ASSEMBLY", v, 1);
    snprintf(v, sizeof v, "%s/dotnet/libcoreclr.dylib", bdir); setenv("DOORSTOP_CLR_RUNTIME_CORECLR_PATH", v, 1);
    snprintf(v, sizeof v, "%s/dotnet", bdir); setenv("DOORSTOP_CLR_CORLIB_DIR", v, 1);
    setenv("BEPINEX_PLUGIN_DIRS", dirs, 1);
    logf_("bepinex: %d plugin folder(s): %s", n, dirs);
    return n;
}

int main(int argc, char **argv) {
    const char *h = getenv("HOME");
    if (!h || !*h) { struct passwd *pw = getpwuid(getuid()); h = pw ? pw->pw_dir : "/tmp"; }
    snprintf(home, sizeof home, "%s", h);
    snprintf(logpath, sizeof logpath, "%s/Library/Logs/DungeonSettlersModded.log", home);
    int dry = getenv("DSMODDED_DRY_RUN") && !strcmp(getenv("DSMODDED_DRY_RUN"), "1");

    /* real app path: <app>/Contents/MacOS/<stub> */
    char exe[PATH_MAX], real[PATH_MAX]; uint32_t sz = sizeof exe;
    if (_NSGetExecutablePath(exe, &sz) != 0 || !realpath(exe, real)) { logf_("cannot resolve own path"); return 1; }
    snprintf(self_exe, sizeof self_exe, "%s", real);
    if (argc >= 3 && !strcmp(argv[1], "--update-window")) return update_window(argv[2]);
    char app[PATH_MAX]; snprintf(app, sizeof app, "%s", real); dirname_n(app, 3);

    char root[PATH_MAX], profile[PATH_MAX], data[PATH_MAX], uhome[PATH_MAX];
    const char *er = getenv("DSMODDED_ROOT");
    if (er && *er) snprintf(root, sizeof root, "%s", er);
    else snprintf(root, sizeof root, "%s/Library/Application Support/DSModLauncher", home);
    snprintf(profile, sizeof profile, "%s/profiles/modded", root);
    snprintf(data, sizeof data, "%s/data", profile);
    snprintf(uhome, sizeof uhome, "%s/home", profile);
    mkdirs(data); mkdirs(uhome);
    setup_lang(app, root, data);                             /* before the first dialog */
    if (!exists(data)) { logf_("cannot create %s (%s)", data, strerror(errno));
                         return fail(TR("Dungeon Settlers Modded cannot create its folder in Application Support.")); }

    /* 1b. auto-update. ROOT/update/run.lock: the game of this install holds it (this fd stays open through execv) */
    setenv("DSML_ROOT", root, 1); setenv("DSML_APP", app, 1);
    { char up[PATH_MAX]; snprintf(up, sizeof up, "%s/update", root); mkdirs(up); }
    char lk[PATH_MAX]; snprintf(lk, sizeof lk, "%s/update/run.lock", root);
    int lfd = open(lk, O_RDWR | O_CREAT, 0644), running = lfd >= 0 && flock(lfd, LOCK_EX | LOCK_NB) != 0;
    if (running) { logf_("update: a game of this install runs"); close(lfd); lfd = -1; }
    DslUpdWin uw = { uw_open, uw_progress, uw_close };
    char ulabel[200];
    if (dsl_update_starter(root, running, &uw, ulabel, sizeof ulabel)) {
        logf_("update: new kit installed: starting the new app's stub");
        if (lfd >= 0) close(lfd);                            /* the new stub takes the lock itself */
        execv(real, argv);
        logf_("exec %s failed (%s)", real, strerror(errno));
        return fail(TR("Dungeon Settlers Modded was updated but could not start. Open it again."));
    }
    if (ulabel[0]) setenv("DSML_UPDATE", ulabel, 1); else unsetenv("DSML_UPDATE");
    if (sync_loader(app, root)) return 1;
    user_mods(app, root);
    char play[PATH_MAX], build[128];
    if (game_copy(app, root, profile, play, sizeof play, build, sizeof build)) return 1;

    /* first run: COPY the normal game's settings and records (read only). Not its Saves: the loader lists the game's own
       save folder and plays those worlds in place (DSML_GAME_SAVES below, loader/src/dsl_saves.c) */
    char flag[PATH_MAX]; snprintf(flag, sizeof flag, "%s/.copied_from_normal_game", data);
    if (!exists(flag)) {
        char src[PATH_MAX]; snprintf(src, sizeof src, "%s/Library/Application Support/CanOpener/Dungeon Settlers", home);
        const char *items[] = { "OutGame.json", "UserSetting.json", "UserCampaignLog.json" };
        for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
            char s[PATH_MAX], d[PATH_MAX];
            snprintf(s, sizeof s, "%s/%s", src, items[i]); snprintf(d, sizeof d, "%s/%s", data, items[i]);
            if (!exists(s) || exists(d)) continue;
            if (copyfile(s, d, NULL, COPYFILE_ALL | COPYFILE_RECURSIVE) != 0) logf_("copy %s failed (%s)", s, strerror(errno));
        }
        FILE *f = fopen(flag, "w"); if (f) { time_t t = time(NULL); fputs(ctime(&t), f); fclose(f); }
        logf_("first run: settings copied from %s", src);
    }
    mods_state(root, data);

    if (chdir(profile) != 0) logf_("chdir %s failed (%s)", profile, strerror(errno));
    { FILE *f = fopen("steam_appid.txt", "w"); if (f) { fputs("2798330\n", f); fclose(f); } }
    setenv("SteamAppId", "2798330", 1); setenv("SteamGameId", "2798330", 1);
    if (!dry && !getenv("DSMODDED_NO_STEAM") && ensure_steam(play)) return 1;

    char game[PATH_MAX], plog[PATH_MAX], v[PATH_MAX];
    snprintf(game, sizeof game, "%s/Contents/MacOS/Dungeon Settlers", play);
    snprintf(plog, sizeof plog, "%s/player.log", profile);
    setenv("CFFIXED_USER_HOME", uhome, 1);
    setenv("DSRIG_DIR", profile, 1);
    setenv("DSRIG_LIVE", "1", 1);
    if (!getenv("DSRIG_FPS")) setenv("DSRIG_FPS", "60", 1);
    snprintf(v, sizeof v, "%s/loader/dsload.dylib", root); setenv("DSML_DSLOAD", v, 1);
    setenv("DSML_BUILD", build, 1);                          /* bepinex_launch reads it (first-start marker) */
    int nbie = bepinex_launch(root, data, v, sizeof v);
    melon_launch(root, data, v, sizeof v, nbie == 0);
    setenv("DYLD_INSERT_LIBRARIES", v, 1);
    snprintf(v, sizeof v, "%s/loader/mods", root); setenv("DSL_MODS_DIR", v, 1);
    snprintf(v, sizeof v, "%s/mods", root); setenv("DSML_USER_MODS_DIR", v, 1);   /* the Mods window's "Open mods folder" */
    snprintf(v, sizeof v, "%s/Mods/active.json", data); setenv("DSL_PROFILE", v, 1);
    setenv("DSL_LOG_DIR", profile, 1);
    snprintf(v, sizeof v, "%s/Mods/worlds.json", data); setenv("DSML_SESSION", v, 1);
    setenv("DSML_BUILD", build, 1);
    setenv("DSML_APP", app, 1);
    setenv("DSML_DATA", data, 1);
    snprintf(v, sizeof v, "%s/Library/Application Support/CanOpener/Dungeon Settlers/Saves", home);
    setenv("DSML_GAME_SAVES", v, 1);                         /* the game's own saves: listed and played in place */

    char **av = calloc((size_t)argc + 4, sizeof *av); int k = 0;
    av[k++] = game; av[k++] = "-logFile"; av[k++] = plog;
    for (int i = 1; i < argc; i++) if (strncmp(argv[i], "-psn_", 5)) av[k++] = argv[i];   /* old LaunchServices arg */
    av[k] = NULL;

    if (dry) {
        const char *keys[] = { "SteamAppId", "SteamGameId", "CFFIXED_USER_HOME", "DSRIG_DIR", "DSRIG_LIVE", "DSRIG_FPS",
                               "DYLD_INSERT_LIBRARIES", "DSL_MODS_DIR", "DSML_USER_MODS_DIR", "DSL_PROFILE", "DSL_LOG_DIR", "DSML_SESSION",
                               "DSML_BUILD", "DSML_APP", "DSML_DATA", "DSML_GAME_SAVES", "DSML_ROOT", "DSML_DSLOAD", "DSML_BEPINEX_DIR", "DSML_BEPINEX_SCRIPT",
                               "DOORSTOP_ENABLED", "DOORSTOP_TARGET_ASSEMBLY", "DOORSTOP_CLR_RUNTIME_CORECLR_PATH",
                               "DOORSTOP_CLR_CORLIB_DIR", "DOORSTOP_IGNORE_DISABLED_ENV", "BEPINEX_PLUGIN_DIRS",
                               "DSML_MELON_DIR", "DSML_MELON_SCRIPT", "MELONLOADER_BASEDIR", "MELONLOADER_MOD_DIRS" };
        for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) if (getenv(keys[i])) printf("ENV %s=%s\n", keys[i], getenv(keys[i]));
        char cwd[PATH_MAX]; printf("CWD %s\n", getcwd(cwd, sizeof cwd) ? cwd : "?");
        for (int i = 0; i < k; i++) printf("ARGV %s\n", av[i]);
        logf_("dry run: would start %s", game);
        return 0;
    }
    logf_("start %s (app %s, build %s)", game, app, build);
    execv(game, av);
    logf_("exec %s failed (%s)", game, strerror(errno));
    return fail(TR("The game could not be started. See ~/Library/Logs/DungeonSettlersModded.log."));
}
