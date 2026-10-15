// ds_modded.c - CFBundleExecutable "DS Modded" of "Dungeon Settlers Modded.app" (from rig/mp_launcher.c).
// No window of its own. Flow:
//   1. ROOT = $DSMODDED_ROOT or ~/Library/Application Support/DSModLauncher; PROFILE = ROOT/profiles/modded;
//      DATA = PROFILE/data (saves), UHOME = PROFILE/home, mods state = DATA/Mods/.
//   2. loader files: <app>/Contents/Resources/dsml/ -> ROOT/loader/ when VERSION differs (temp folder, xattr -cr,
//      rename into place; the old folder moves to ROOT/retired/, never written over: a running game maps it).
//   3. game copy: PROFILE/copy.json {"build","app"} when that copy exists (pinned), else
//      Contents/MacOS/dsml-helper prepare --root ROOT (last lines "BUILD <id>", "PLAY <path>" or "ERROR <msg>").
//   4. first run: COPY the normal saves (read only); default DATA/Mods/active.json + worlds.json. The Mods window is
//      part of the loader itself (not a module), so no entry of active.json can switch it off.
//   5. Steam (start it in the background when closed), env, execv of the play copy's game binary, cwd = PROFILE.
// DSMODDED_DRY_RUN=1: everything except ensure_steam and execv; prints the env and argv instead (tests).
// DSMP_NO_DIALOG=1: no dialogs. Log: ~/Library/Logs/DungeonSettlersModded.log
#include <mach-o/dyld.h>
#include <CoreFoundation/CoreFoundation.h>
#include <copyfile.h>
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
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char home[PATH_MAX], logpath[PATH_MAX];
extern char **environ;

static void logf_(const char *fmt, ...) {
    FILE *f = fopen(logpath, "a"); if (!f) return;
    time_t t = time(NULL); char ts[32]; strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    fprintf(f, "%s [%d] ", ts, getpid());
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

static void mkdirs(const char *path) {
    char b[PATH_MAX]; snprintf(b, sizeof b, "%s", path);
    for (char *s = b + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(b, 0755); *s = '/'; }
    mkdir(b, 0755);
}

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static void dirname_n(char *p, int n) { while (n-- > 0) { char *s = strrchr(p, '/'); if (s && s != p) *s = 0; } }

static void notice(const char *msg) {
    if (getenv("DSMP_NO_DIALOG")) { logf_("notice (not shown): %s", msg); return; }
    CFStringRef m = CFStringCreateWithCString(NULL, msg, kCFStringEncodingUTF8);
    CFOptionFlags resp;
    CFUserNotificationDisplayAlert(0, kCFUserNotificationStopAlertLevel, NULL, NULL, NULL,
                                   CFSTR("Dungeon Settlers Modded"), m, CFSTR("OK"), NULL, NULL, &resp);
    if (m) CFRelease(m);
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

static int write_atomic(const char *p, const char *text) {
    char t[PATH_MAX]; snprintf(t, sizeof t, "%s.tmp-write", p);
    FILE *f = fopen(t, "w"); if (!f) return -1;
    fputs(text, f);
    if (fclose(f) != 0) return -1;
    return rename(t, p);
}

static void trim(char *s) { size_t n = strlen(s); while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ')) s[--n] = 0; }

/* "key": "value" of a flat JSON object (enough for copy.json, which this stub writes itself) */
static int json_str(const char *j, const char *key, char *out, size_t cap) {
    char k[128]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = j ? strstr(j, k) : NULL; if (!p) return 0;
    p += strlen(k); while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p++ != ':') return 0;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p++ != '"') return 0;
    size_t n = 0;
    for (; *p && *p != '"'; p++) {
        char c = *p;
        if (c == '\\' && p[1]) { p++; c = *p == 'n' ? '\n' : *p; }
        if (n + 1 < cap) out[n++] = c;
    }
    out[n] = 0;
    return *p == '"';
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
        return fail("This copy of Dungeon Settlers Modded is incomplete (no mod loader inside). Download it again.");
    }
    trim(a); if (b) trim(b);
    int same = b && !strcmp(a, b) && exists(dst);
    if (same) { free(a); free(b); return 0; }
    char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s/loader.new-%d", root, getpid());
    if (copyfile(src, tmp, NULL, COPYFILE_ALL | COPYFILE_RECURSIVE) != 0) {
        logf_("copy %s -> %s failed (%s)", src, tmp, strerror(errno));
        free(a); free(b);
        return fail("The mod loader could not be copied (disk full or no write access to Application Support).");
    }
    char *xav[] = { "/usr/bin/xattr", "-cr", tmp, NULL };   /* a downloaded app is quarantined: dyld would refuse */
    if (run_cmd(xav, NULL, 0) != 0) logf_("xattr -cr %s failed", tmp);
    if (exists(dst)) {
        char ret[PATH_MAX], ts[32]; stamp(ts, sizeof ts);
        snprintf(ret, sizeof ret, "%s/retired", root); mkdirs(ret);
        snprintf(ret, sizeof ret, "%s/retired/loader-%s-%d", root, ts, getpid());
        if (rename(dst, ret) != 0) {
            logf_("move %s -> %s failed (%s)", dst, ret, strerror(errno)); free(a); free(b);
            return fail("The mod loader could not be updated (its folder is in use). Quit the game and try again.");
        }
    }
    if (rename(tmp, dst) != 0) {
        logf_("move %s -> %s failed (%s)", tmp, dst, strerror(errno)); free(a); free(b);
        return fail("The mod loader could not be installed. See ~/Library/Logs/DungeonSettlersModded.log.");
    }
    logf_("loader %s installed (was %s)", a, b ? b : "none");
    free(a); free(b);
    return 0;
}

/* 3. the play copy: the remembered one (pinned) or a new one made by dsml-helper */
static int game_copy(const char *app, const char *root, const char *profile, char *play, size_t pcap,
                     char *build, size_t bcap) {
    char cj[PATH_MAX]; snprintf(cj, sizeof cj, "%s/copy.json", profile);
    char *j = slurp(cj);
    if (j && json_str(j, "build", build, bcap) && json_str(j, "app", play, pcap)) {
        char exe[PATH_MAX]; snprintf(exe, sizeof exe, "%s/Contents/MacOS/Dungeon Settlers", play);
        if (exists(exe)) { free(j); return 0; }
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
        return fail(err[0] ? err : "The game copy could not be made. See ~/Library/Logs/DungeonSettlersModded.log.");
    }
    logf_("game copy ready in %ld s: build %s, %s", (long)(time(NULL) - t0), build, play);
    char eb[256], ep[PATH_MAX * 2], txt[PATH_MAX * 3];
    json_esc(build, eb, sizeof eb); json_esc(play, ep, sizeof ep);
    snprintf(txt, sizeof txt, "{\"build\": \"%s\", \"app\": \"%s\"}\n", eb, ep);
    if (write_atomic(cj, txt) != 0) logf_("cannot write %s (%s)", cj, strerror(errno));
    return 0;
}

/* 4. mods state: defaults only; an existing active.json is the player's choice and stays as it is */
static void mods_state(const char *data) {
    char md[PATH_MAX], act[PATH_MAX], wl[PATH_MAX];
    snprintf(md, sizeof md, "%s/Mods", data); mkdirs(md);
    snprintf(act, sizeof act, "%s/active.json", md);
    snprintf(wl, sizeof wl, "%s/worlds.json", md);
    if (!exists(wl) && write_atomic(wl, "{\"worlds\":{}}\n") != 0) logf_("cannot write %s", wl);
    if (!exists(act) && write_atomic(act, "{\"enabled\":[\"multiplayer\"],\"order\":[\"multiplayer\"]}\n") != 0)
        logf_("cannot write %s", act);
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
        notice("Steam could not be started. Install Steam, start it and sign in, then open Dungeon Settlers Modded again.");
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
    notice("Steam did not get ready. Start Steam and sign in, then open Dungeon Settlers Modded again.");
    return 1;
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
    char app[PATH_MAX]; snprintf(app, sizeof app, "%s", real); dirname_n(app, 3);

    char root[PATH_MAX], profile[PATH_MAX], data[PATH_MAX], uhome[PATH_MAX];
    const char *er = getenv("DSMODDED_ROOT");
    if (er && *er) snprintf(root, sizeof root, "%s", er);
    else snprintf(root, sizeof root, "%s/Library/Application Support/DSModLauncher", home);
    snprintf(profile, sizeof profile, "%s/profiles/modded", root);
    snprintf(data, sizeof data, "%s/data", profile);
    snprintf(uhome, sizeof uhome, "%s/home", profile);
    mkdirs(data); mkdirs(uhome);
    if (!exists(data)) { logf_("cannot create %s (%s)", data, strerror(errno));
                         return fail("Dungeon Settlers Modded cannot create its folder in Application Support."); }

    if (sync_loader(app, root)) return 1;
    char play[PATH_MAX], build[128];
    if (game_copy(app, root, profile, play, sizeof play, build, sizeof build)) return 1;

    /* first run: COPY the normal game's saves (read only) */
    char flag[PATH_MAX]; snprintf(flag, sizeof flag, "%s/.copied_from_normal_game", data);
    if (!exists(flag)) {
        char src[PATH_MAX]; snprintf(src, sizeof src, "%s/Library/Application Support/CanOpener/Dungeon Settlers", home);
        const char *items[] = { "Saves", "OutGame.json", "UserSetting.json", "UserCampaignLog.json" };
        for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
            char s[PATH_MAX], d[PATH_MAX];
            snprintf(s, sizeof s, "%s/%s", src, items[i]); snprintf(d, sizeof d, "%s/%s", data, items[i]);
            if (!exists(s) || exists(d)) continue;
            if (copyfile(s, d, NULL, COPYFILE_ALL | COPYFILE_RECURSIVE) != 0) logf_("copy %s failed (%s)", s, strerror(errno));
        }
        FILE *f = fopen(flag, "w"); if (f) { time_t t = time(NULL); fputs(ctime(&t), f); fclose(f); }
        logf_("first run: saves copied from %s", src);
    }
    mods_state(data);

    if (chdir(profile) != 0) logf_("chdir %s failed (%s)", profile, strerror(errno));
    { FILE *f = fopen("steam_appid.txt", "w"); if (f) { fputs("2798330\n", f); fclose(f); } }
    setenv("SteamAppId", "2798330", 1); setenv("SteamGameId", "2798330", 1);
    if (!dry && ensure_steam(play)) return 1;

    char game[PATH_MAX], plog[PATH_MAX], v[PATH_MAX];
    snprintf(game, sizeof game, "%s/Contents/MacOS/Dungeon Settlers", play);
    snprintf(plog, sizeof plog, "%s/player.log", profile);
    setenv("CFFIXED_USER_HOME", uhome, 1);
    setenv("DSRIG_DIR", profile, 1);
    setenv("DSRIG_LIVE", "1", 1);
    if (!getenv("DSRIG_FPS")) setenv("DSRIG_FPS", "60", 1);
    snprintf(v, sizeof v, "%s/loader/dsload.dylib", root); setenv("DYLD_INSERT_LIBRARIES", v, 1);
    snprintf(v, sizeof v, "%s/loader/mods", root); setenv("DSL_MODS_DIR", v, 1);
    snprintf(v, sizeof v, "%s/Mods/active.json", data); setenv("DSL_PROFILE", v, 1);
    setenv("DSL_LOG_DIR", profile, 1);
    snprintf(v, sizeof v, "%s/Mods/worlds.json", data); setenv("DSML_SESSION", v, 1);
    setenv("DSML_BUILD", build, 1);
    setenv("DSML_APP", app, 1);
    setenv("DSML_DATA", data, 1);

    char **av = calloc((size_t)argc + 4, sizeof *av); int k = 0;
    av[k++] = game; av[k++] = "-logFile"; av[k++] = plog;
    for (int i = 1; i < argc; i++) if (strncmp(argv[i], "-psn_", 5)) av[k++] = argv[i];   /* old LaunchServices arg */
    av[k] = NULL;

    if (dry) {
        const char *keys[] = { "SteamAppId", "SteamGameId", "CFFIXED_USER_HOME", "DSRIG_DIR", "DSRIG_LIVE", "DSRIG_FPS",
                               "DYLD_INSERT_LIBRARIES", "DSL_MODS_DIR", "DSL_PROFILE", "DSL_LOG_DIR", "DSML_SESSION",
                               "DSML_BUILD", "DSML_APP", "DSML_DATA" };
        for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) printf("ENV %s=%s\n", keys[i], getenv(keys[i]));
        char cwd[PATH_MAX]; printf("CWD %s\n", getcwd(cwd, sizeof cwd) ? cwd : "?");
        for (int i = 0; i < k; i++) printf("ARGV %s\n", av[i]);
        logf_("dry run: would start %s", game);
        return 0;
    }
    logf_("start %s (app %s, build %s)", game, app, build);
    execv(game, av);
    logf_("exec %s failed (%s)", game, strerror(errno));
    return fail("The game could not be started. See ~/Library/Logs/DungeonSettlersModded.log.");
}
