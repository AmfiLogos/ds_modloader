// Mac OS part of the auto-update (src/dsl_update_os.h). ROOT = $DSML_ROOT (the app's stub), else two folders above
// DSL_MODS_DIR (ROOT/loader/mods). Kit: the release's Mac zip (or .dmg) holds "Dungeon Settlers Modded.app"; it
// replaces the app at $DSML_APP (what is inside the bundle is swapped, the bundle folder stays for the Dock; quarantine
// cleared the way the .dmg install leaves it, signature checked) and ROOT/loader (its Contents/Resources/dsml; the player's own mods, marker .player_mod, are carried over).
// Mod: ROOT/loader/mods/<id> replaced by the package's <id> folder. Tools: /usr/bin/curl, ditto, hdiutil, xattr, codesign.
// Built twice: into dsload.dylib (check + download) and, #included, into the app's stub (install, app/stub/ds_modded.c).
#include "../../src/dsl_internal.h"
#include "../../src/dsl_update_os.h"
#include <CommonCrypto/CommonDigest.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
static char root_s[1024];

const char *dsl_uos_root(void) {
    if (root_s[0]) return root_s;
    const char *r = getenv("DSML_ROOT");
    if (r && *r) { snprintf(root_s, sizeof root_s, "%s", r); return root_s; }
    const char *m = getenv("DSL_MODS_DIR"); if (!m || !*m) return "";
    snprintf(root_s, sizeof root_s, "%s", m);
    for (int i = 0; i < 2; i++) { char *s = strrchr(root_s, '/'); if (s && s != root_s) *s = 0; }
    return root_s;
}
const char *dsl_uos_platform(void) { return "mac-arm64"; }
int dsl_uos_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
int dsl_uos_mkdirs(const char *path) {
    char p[1100]; snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    return mkdir(p, 0755) == 0 || errno == EEXIST ? 0 : -1;
}
/* run a tool, wait; no DYLD_ variables for it (the game's own injection list); 0 = exit 0 */
static int run(char *const av[]) {
    int n = 0; while (environ[n]) n++;
    char **env = calloc((size_t)n + 1, sizeof *env); int k = 0;
    for (int i = 0; i < n; i++) if (strncmp(environ[i], "DYLD_", 5)) env[k++] = environ[i];
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid; int r = posix_spawn(&pid, av[0], &fa, NULL, av, env);
    posix_spawn_file_actions_destroy(&fa); free(env);
    if (r) { dsl_log("update: cannot start %s (%s)", av[0], strerror(r)); return -1; }
    int st; while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}
int dsl_uos_move(const char *from, const char *to) {
    char par[1100]; snprintf(par, sizeof par, "%s", to); char *s = strrchr(par, '/'); if (s) { *s = 0; dsl_uos_mkdirs(par); }
    if (rename(from, to) == 0) return 0;
    if (errno != EXDEV) { dsl_log("update: move %s -> %s failed (%s)", from, to, strerror(errno)); return -1; }
    char *av[] = { "/bin/mv", (char *)from, (char *)to, NULL };            /* another volume: copy + remove */
    return run(av);
}
int dsl_uos_release(char *out, size_t cap) {
    char p[1200]; snprintf(p, sizeof p, "%s/loader/RELEASE", dsl_uos_root());
    FILE *f = fopen(p, "r"); if (!f) return -1;
    if (!fgets(out, (int)cap, f)) out[0] = 0; fclose(f);
    out[strcspn(out, "\r\n ")] = 0; return out[0] ? 0 : -1;
}
int dsl_uos_fetch(const char *url, const char *out_path) {
    char *av[] = { "/usr/bin/curl", "-fsSL", "--max-time", "300", "--connect-timeout", "15", "-A", "DungeonSettlersModded",
                   "-H", "Accept: application/vnd.github+json", "-o", (char *)out_path, (char *)url, NULL };
    return run(av);
}
int dsl_uos_sha256(const char *path, char hex[65]) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    CC_SHA256_CTX c; CC_SHA256_Init(&c); unsigned char b[65536]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) CC_SHA256_Update(&c, b, (CC_LONG)n);
    fclose(f); unsigned char d[32]; CC_SHA256_Final(d, &c);
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", d[i]);
    return 0;
}
int dsl_uos_kit_asset(const char *n) { return !strcmp(n, "Dungeon-Settlers-Modded-Mac.zip") || !strcmp(n, "Dungeon-Settlers-Modded-Mac.dmg"); }

/* recorded first (pending file), then the old path into the backup (when there is one), then the new one into place */
static int swap_in(const char *newp, const char *path, const char *bk, DslMoves *mv) {
    int old = dsl_uos_exists(path);
    if (dsl_umove_add(mv, path, old ? bk : "")) return -1;
    if (old && dsl_uos_move(path, bk)) return -1;
    return dsl_uos_move(newp, path);
}
/* the app: the bundle FOLDER stays (the Dock and Finder aliases follow its file id, owner 2026-10-23), only what is in it
   is swapped, each child through swap_in; the old children go to <bk>/<child>, so <bk> is a whole old app again */
static int swap_bundle(const char *newp, const char *path, const char *bk, DslMoves *mv) {
    if (!dsl_uos_exists(path)) return swap_in(newp, path, bk, mv);
    char a[1600], b[1600], c[1600], nm[2][64][256]; int nn[2] = { 0, 0 }, r = 0;
    for (int k = 0; k < 2; k++) {                            /* names first: nothing moves while a folder is read */
        DIR *d = opendir(k ? newp : path); struct dirent *e; if (!d) return -1;
        while ((e = readdir(d)))
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && nn[k] < 64) snprintf(nm[k][nn[k]++], 256, "%s", e->d_name);
        closedir(d);
    }
    for (int k = 0; k < 2 && !r; k++)                        /* 0: old children the new app lacks; 1: the new app's */
        for (int i = 0; i < nn[k] && !r; i++) {
            snprintf(a, sizeof a, "%s/%s", newp, nm[k][i]); snprintf(b, sizeof b, "%s/%s", path, nm[k][i]);
            snprintf(c, sizeof c, "%s/%s", bk, nm[k][i]);
            if (k) r = swap_in(a, b, c, mv);
            else if (!dsl_uos_exists(a)) r = dsl_umove_add(mv, b, c) || dsl_uos_move(b, c);
        }
    if (r) return -1;
    utimes(path, NULL);                                      /* LaunchServices reads Info.plist again */
    char *xq[] = { "/usr/bin/xattr", "-d", "com.apple.quarantine", (char *)path, NULL }; run(xq);   /* as the new app had none */
    char *ls[] = { "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister", "-f", (char *)path, NULL };
    run(ls);
    return 0;
}
static void prog(DslMoves *mv, int pct) { if (mv->progress) mv->progress(pct); }
/* first line of a small text file ("" when missing) */
static void first_line(const char *p, char *out, size_t n) {
    out[0] = 0; FILE *f = fopen(p, "r"); if (!f) return;
    if (!fgets(out, (int)n, f)) out[0] = 0;
    fclose(f); out[strcspn(out, "\r\n ")] = 0;
}
static int unpack(const char *file, const char *dst) {
    dsl_uos_mkdirs(dst);
    size_t l = strlen(file);
    if (l > 4 && !strcmp(file + l - 4, ".dmg")) {
        char mnt[1200]; snprintf(mnt, sizeof mnt, "%s.mnt", dst); dsl_uos_mkdirs(mnt);
        char *at[] = { "/usr/bin/hdiutil", "attach", "-nobrowse", "-readonly", "-noautoopen", "-mountpoint", mnt, (char *)file, NULL };
        if (run(at)) return -1;
        char *cp[] = { "/usr/bin/ditto", mnt, (char *)dst, NULL }; int r = run(cp);
        char *de[] = { "/usr/bin/hdiutil", "detach", "-quiet", mnt, NULL }; run(de);
        return r;
    }
    char *av[] = { "/usr/bin/ditto", "-x", "-k", (char *)file, (char *)dst, NULL };
    return run(av);
}
int dsl_uos_unpack(const char *zip, const char *dst) { return unpack(zip, dst); }
/* the unpacked new app once it is no longer needed: its empty folder goes; one left with files (not installed) loses
   ".app". A whole app there (same name, same bundle id) was listed by Spotlight and Launchpad and could be the one macOS
   starts for the bundle id (owner 2026-10-24); so is the old app's backup below */
static void drop_new(const char *napp) {
    if (!napp[0] || rmdir(napp) == 0 || !dsl_uos_exists(napp)) return;
    char t[1500]; snprintf(t, sizeof t, "%s", napp); size_t l = strlen(t);
    if (l > 4 && !strcmp(t + l - 4, ".app")) t[l - 4] = 0;
    strlcat(t, " (not installed)", sizeof t);
    if (rename(napp, t)) dsl_log("update: move %s -> %s failed (%s)", napp, t, strerror(errno));
}
int dsl_uos_kit_install(const char *file, const char *ver, const char *backup, DslMoves *mv, char *why, size_t wn) {
    const char *app = getenv("DSML_APP"), *root = dsl_uos_root();
    if (!app || !*app || !dsl_uos_exists(app)) { snprintf(why, wn, TR("the app's place is not known in this start")); return -1; }
    if (strstr(app, "/AppTranslocation/")) { snprintf(why, wn, TR("the app runs from a temporary place: move it into Applications")); return -1; }
    char par[1100]; snprintf(par, sizeof par, "%s", app); char *s = strrchr(par, '/'); if (s) *s = 0;
    if (access(par, W_OK)) { snprintf(why, wn, TR("no write access to the app's folder")); return -1; }
    char u[1200]; snprintf(u, sizeof u, "%s.new", backup);
    prog(mv, 10);
    if (unpack(file, u)) { snprintf(why, wn, TR("the download could not be unpacked")); return -1; }
    prog(mv, 40);
    char napp[1400] = ""; DIR *d = opendir(u); struct dirent *e;
    while (d && (e = readdir(d))) { size_t l = strlen(e->d_name); if (l > 4 && !strcmp(e->d_name + l - 4, ".app")) { snprintf(napp, sizeof napp, "%s/%s", u, e->d_name); break; } }
    if (d) closedir(d);
    char t[1600]; snprintf(t, sizeof t, "%s/Contents/Resources/dsml/dsload.dylib", napp);
    if (!napp[0] || !dsl_uos_exists(t)) { snprintf(why, wn, TR("the download holds no Dungeon Settlers Modded app")); drop_new(napp); return -1; }
    char rel[64]; snprintf(t, sizeof t, "%s/Contents/Resources/dsml/RELEASE", napp); first_line(t, rel, sizeof rel);
    if (strcmp(rel, ver)) {
        dsl_log("update: the v%s download says RELEASE %s", ver, rel[0] ? rel : "(none)");
        snprintf(why, wn, TR("the download's version number is wrong")); drop_new(napp); return -1;
    }
    char *xa[] = { "/usr/bin/xattr", "-cr", napp, NULL }; run(xa);
    char *cs[] = { "/usr/bin/codesign", "-v", "--deep", "--strict", napp, NULL };
    if (run(cs)) { snprintf(why, wn, TR("the new app's signature is broken")); drop_new(napp); return -1; }
    prog(mv, 60);
    /* the new ROOT/loader: the app's dsml folder + the player's own mods of the old one */
    char nl[1300], ol[1200], src[1600]; snprintf(nl, sizeof nl, "%s/loader.update", u); snprintf(ol, sizeof ol, "%s/loader", root);
    snprintf(src, sizeof src, "%s/Contents/Resources/dsml", napp);
    char *cp[] = { "/usr/bin/ditto", src, nl, NULL };
    if (run(cp)) { snprintf(why, wn, TR("the new mod loader could not be copied")); drop_new(napp); return -1; }
    char om[1300]; snprintf(om, sizeof om, "%s/mods", ol); d = opendir(om);
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char mk[1600], dst[1600], from[1600]; snprintf(mk, sizeof mk, "%s/%s/.player_mod", om, e->d_name);
        snprintf(dst, sizeof dst, "%s/mods/%s", nl, e->d_name); snprintf(from, sizeof from, "%s/%s", om, e->d_name);
        if (!dsl_uos_exists(mk) || dsl_uos_exists(dst)) continue;
        char *c2[] = { "/usr/bin/ditto", from, dst, NULL }; if (run(c2)) dsl_log("update: player mod %s not carried over", e->d_name);
    }
    if (d) closedir(d);
    char *x2[] = { "/usr/bin/xattr", "-cr", nl, NULL }; run(x2);
    prog(mv, 80);
    /* the old app's files go to <backup>/app/<name> WITHOUT ".app" (drop_new); a rollback moves them back */
    char bk[1600], base[300]; snprintf(base, sizeof base, "%s", strrchr(app, '/') + 1);
    size_t bl = strlen(base); if (bl > 4 && !strcmp(base + bl - 4, ".app")) base[bl - 4] = 0;
    snprintf(bk, sizeof bk, "%s/app/%s", backup, base);
    int sw = swap_bundle(napp, app, bk, mv);
    drop_new(napp);                                          /* empty now: its files are in place (or rolled back) */
    if (sw) { snprintf(why, wn, TR("the app could not be replaced")); return -1; }
    snprintf(bk, sizeof bk, "%s/loader", backup);
    if (swap_in(nl, ol, bk, mv)) { snprintf(why, wn, TR("the mod loader folder could not be replaced")); return -1; }
    dsl_log("update: Mac kit installed: %s and %s (old ones in %s)", app, ol, backup);
    return 0;
}
int dsl_uos_mod_install(const char *zip, const char *id, const char *ver, const char *backup, DslMoves *mv, char *why, size_t wn) {
    char u[1200]; snprintf(u, sizeof u, "%s.new", backup);
    if (unpack(zip, u)) { snprintf(why, wn, TR("the %s download could not be unpacked"), id); return -1; }
    char nm[1400], t[1500]; snprintf(nm, sizeof nm, "%s/%s", u, id); snprintf(t, sizeof t, "%s/mod.json", nm);
    char *j = NULL; FILE *f = fopen(t, "rb");
    if (f) { j = calloc(1, 1 << 20); if (j) fread(j, 1, (1 << 20) - 1, f); fclose(f); }
    JNode *m = j ? json_parse(j) : NULL; free(j);
    const char *mid = json_str(m, "id"), *mv_ = json_str(m, "version");
    int ok = mid && !strcmp(mid, id), vok = mv_ && !strcmp(mv_, ver); json_free(m);
    if (!ok) { snprintf(why, wn, TR("the %s download is not that mod"), id); return -1; }
    if (!vok) { snprintf(why, wn, TR("the %s download's version number is wrong"), id); return -1; }
    char *xa[] = { "/usr/bin/xattr", "-cr", nm, NULL }; run(xa);
    char dst[1300], bk[1300]; snprintf(dst, sizeof dst, "%s/loader/mods/%s", dsl_uos_root(), id); snprintf(bk, sizeof bk, "%s/%s", backup, id);
    if (swap_in(nm, dst, bk, mv)) { snprintf(why, wn, TR("the mod %s could not be replaced"), id); return -1; }
    dsl_log("update: mod %s installed into %s", id, dst);
    return 0;
}

/* ------------------------------------------------------------------ crash reports (src/dsl_crash.c) */
int dsl_uos_post(const char *url, const char *file, const char *const *headers, int nh) {
    char at[1200]; snprintf(at, sizeof at, "@%s", file);
    char *av[48]; int k = 0;
    const char *fixed[] = { "/usr/bin/curl", "-fsS", "--max-time", "90", "--connect-timeout", "15", "-A", "DungeonSettlersModded", "-X", "POST" };
    for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++) av[k++] = (char *)fixed[i];
    for (int i = 0; i < nh && k < 40; i++) { av[k++] = "-H"; av[k++] = (char *)headers[i]; }
    av[k++] = "--data-binary"; av[k++] = at; av[k++] = (char *)url; av[k] = NULL;
    return run(av);                                       /* curl -f: exit 22 on HTTP >= 400 */
}
/* bug reports (src/dsl_bugrep.c): the HTTP status (-1 = no answer) and the start of the answer's body */
int dsl_uos_post_resp(const char *url, const char *file, const char *const *headers, int nh, char *resp, size_t rcap) {
    char at[1200], ro[1200], ho[1200]; snprintf(at, sizeof at, "@%s", file); snprintf(ro, sizeof ro, "%s.resp", file); snprintf(ho, sizeof ho, "%s.head", file);
    remove(ro); remove(ho);
    char *av[48]; int k = 0;
    const char *fixed[] = { "/usr/bin/curl", "-sS", "--max-time", "90", "--connect-timeout", "15", "-A", "DungeonSettlersModded", "-X", "POST" };
    for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++) av[k++] = (char *)fixed[i];
    for (int i = 0; i < nh && k < 38; i++) { av[k++] = "-H"; av[k++] = (char *)headers[i]; }
    av[k++] = "-o"; av[k++] = ro; av[k++] = "-D"; av[k++] = ho;
    av[k++] = "--data-binary"; av[k++] = at; av[k++] = (char *)url; av[k] = NULL;
    int rr = run(av), code = -1;
    if (resp && rcap) resp[0] = 0;
    FILE *f = fopen(ho, "rb");
    if (f) { char l[512]; while (fgets(l, sizeof l, f)) if (!strncmp(l, "HTTP/", 5)) { const char *s = strchr(l, ' '); if (s) code = atoi(s + 1); } fclose(f); }
    f = resp && rcap ? fopen(ro, "rb") : NULL;
    if (f) { size_t n = fread(resp, 1, rcap - 1, f); resp[n] = 0; fclose(f); }
    remove(ro); remove(ho);
    if (code <= 0 && rr) dsl_log("bugrep: upload: no answer from the receiver");
    return code;
}
static void sc_str(const char *name, char *out, size_t n) { size_t l = n; out[0] = 0; if (sysctlbyname(name, out, &l, NULL, 0)) out[0] = 0; else out[n - 1] = 0; }
void dsl_uos_sysinfo(char *os, size_t on, char *cpu, size_t cn, char *plat, size_t pn) {
    char v[64], b[64]; sc_str("kern.osproductversion", v, sizeof v); sc_str("kern.osversion", b, sizeof b);
    snprintf(os, on, "macOS %s (%s)", v[0] ? v : "?", b[0] ? b : "?");
    sc_str("machdep.cpu.brand_string", cpu, cn);
    int tr = 0; size_t tl = sizeof tr; if (sysctlbyname("sysctl.proc_translated", &tr, &tl, NULL, 0)) tr = 0;
#ifdef __x86_64__
    snprintf(plat, pn, "%s", tr ? "mac-x64 (Rosetta on an Apple chip)" : "mac-x64");
#else
    snprintf(plat, pn, "mac-arm64");
#endif
}
int dsl_uos_pid_alive(long pid) {                         /* a zombie (exited, not reaped yet) is not alive */
    if (pid <= 0 || (kill((pid_t)pid, 0) && errno != EPERM)) return 0;
    struct kinfo_proc kp; size_t l = sizeof kp; int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
    if (sysctl(mib, 4, &kp, &l, NULL, 0) || !l) return 1;
    return kp.kp_proc.p_stat != SZOMB;
}
void dsl_uos_random(unsigned char *buf, size_t n) { arc4random_buf(buf, n); }
void dsl_uos_identity(void (*add)(void *ud, const char *s), void *ud) {
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        add(ud, pw->pw_name); add(ud, pw->pw_dir);
        if (pw->pw_gecos && pw->pw_gecos[0]) {                    /* the full name, and each word of it */
            char g[256]; snprintf(g, sizeof g, "%s", pw->pw_gecos); g[strcspn(g, ",")] = 0; add(ud, g);
            for (char *w = g, *sp; *w; w = sp + 1) { sp = strchr(w, ' '); if (sp) *sp = 0; add(ud, w); if (!sp) break; }
        }
    }
    const char *e[] = { "USER", "LOGNAME", "HOME" };
    for (int i = 0; i < 3; i++) { const char *v = getenv(e[i]); if (v && *v) add(ud, v); }
    char h[256]; if (!gethostname(h, sizeof h)) { h[sizeof h - 1] = 0; add(ud, h); h[strcspn(h, ".")] = 0; add(ud, h); }
}
int dsl_uos_steam_dir(char *out, size_t n) {
    struct passwd *pw = getpwuid(getuid()); if (!pw || !pw->pw_dir) return -1;
    snprintf(out, n, "%s/Library/Application Support/Steam", pw->pw_dir);
    return dsl_uos_exists(out) ? 0 : -1;
}
