// dsl_pkg.c - mod packages installed while the game runs (owner v13.2: "a refresh function if a new mod is added").
// Same rules as the starters (app/stub/ds_modded.c user_mods, start/win/ds_modded_win.c install_packages):
//   a <id>-<version>.zip in the player's mods folder is unpacked to <folder>/<id> (mod.json at the top or one folder
//   down, its "id" = a-z 0-9 . _ -); the old folder of that id moves to ROOT/retired; the zip moves to .installed
//   (bad: .failed). Mac: the player's folder is ROOT/mods (DSML_USER_MODS_DIR) and a mod is copied from there into the
//   loader's folder ROOT/loader/mods/<id> (marker .player_mod, quarantine cleared), as the stub does. Windows: the
//   player's folder IS the loader's folder (ROOT\mods).
// A new mod is off. Turning it on uses the normal Apply (restart behind the loading screen at the title).
#include "../src/dsl_internal.h"
#include "../src/dsl_menu.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#define mkdir_(p) _mkdir(p)
int bie_unzip_file(const char *zip, const char *dest);   /* start/win/bie_win.c (in baselib.dll) */
#else
#include <copyfile.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#define mkdir_(p) mkdir(p, 0755)
#endif

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static char *slurp(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (1 << 20)) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1); size_t r = fread(b, 1, (size_t)n, f); fclose(f); b[r] = 0; return b;
}
static int valid_id(const char *s) {
    if (!s || !*s || *s == '.' || strlen(s) > 60) return 0;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '.' || *s == '_' || *s == '-')) return 0;
    return 1;
}
static int mod_id(const char *dir, char *id, size_t cap) {             /* dir/mod.json with a good id */
    char p[1400]; snprintf(p, sizeof p, "%s/mod.json", dir);
    char *t = slurp(p); if (!t) return 0;
    JNode *m = json_parse(t); free(t);
    const char *s = json_str(m, "id"); int ok = valid_id(s);
    if (ok) snprintf(id, cap, "%s", s);
    json_free(m); return ok;
}
static void stamp(char *out, size_t n) { time_t t = time(NULL); struct tm tm; 
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    strftime(out, n, "%Y%m%d-%H%M%S", &tm); }
static char root_dir[1200];                                             /* ROOT: parent of the player's mods folder */
static int retire(const char *path, const char *label) {
    char rd[1300], dst[1500], ts[32]; stamp(ts, sizeof ts);
    snprintf(rd, sizeof rd, "%s/retired", root_dir); mkdir_(rd);
    snprintf(dst, sizeof dst, "%s/%s-%s-%d", rd, label, ts, (int)getpid());
    if (rename(path, dst) == 0) return 0;
    dsl_log("pkg: move %s -> %s failed", path, dst); return -1;
}
#ifndef _WIN32
static int run(char *const av[]) {
    pid_t pid; int st = 0;
    if (posix_spawn(&pid, av[0], NULL, NULL, av, environ)) return -1;
    while (waitpid(pid, &st, 0) < 0) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
#endif
static int unzip(const char *zip, const char *dest) {
#ifdef _WIN32
    mkdir_(dest); return bie_unzip_file(zip, dest) > 0 ? 0 : -1;
#else
    char *av[] = { "/usr/bin/ditto", "-x", "-k", (char *)zip, (char *)dest, NULL }; return run(av);
#endif
}

/* one zip -> <um>/<id>. 1 = installed (id in id); 2 = staged in <um>/.pending/<id>: the running game holds the old
   version (Windows locks a loaded DLL, so its folder cannot move), the starter swaps it in at the next start;
   0 = not a mod package; -1 = a mod, but it could not be put in place (disk / permissions) */
static int install_zip(const char *um, const char *name, char *id, size_t icap) {
    char zp[1400], tmp[1400], src[1500] = "", dst[1400], keep[1400], kz[1600], ts[32];
    snprintf(zp, sizeof zp, "%s/%s", um, name); snprintf(tmp, sizeof tmp, "%s/.unpack-%d", um, (int)getpid());
    id[0] = 0;
    if (exists(tmp)) retire(tmp, "unpack");
    int ok = unzip(zp, tmp) == 0;
    if (ok && mod_id(tmp, id, icap)) snprintf(src, sizeof src, "%s", tmp);
    else if (ok) {                                                      /* one folder down */
        DIR *d = opendir(tmp); struct dirent *e;
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.' || !strcmp(e->d_name, "__MACOSX")) continue;
            char c[1500]; snprintf(c, sizeof c, "%s/%s", tmp, e->d_name);
            if (mod_id(c, id, icap)) { snprintf(src, sizeof src, "%s", c); break; }
        }
        if (d) closedir(d);
    }
    int is_mod = src[0] != 0, res = 0;
    if (is_mod) {
        snprintf(dst, sizeof dst, "%s/%s", um, id);
        /* test only: DSL_PKG_TEST_INUSE=<id> acts as Windows does for a loaded DLL (wine lets the folder move) */
        const char *tin = getenv("DSL_PKG_TEST_INUSE"); int inuse = tin && *tin && exists(dst) && !strcmp(tin, id);
        if (!inuse && !(exists(dst) && retire(dst, id)) && rename(src, dst) == 0) res = 1;
        else {                                                         /* the old version is in use: next start */
            char pd[1400], pdst[1500]; snprintf(pd, sizeof pd, "%s/.pending", um); mkdir_(pd);
            snprintf(pdst, sizeof pdst, "%s/%s", pd, id);
            if (exists(pdst)) retire(pdst, "pending");
            res = rename(src, pdst) == 0 ? 2 : -1;
        }
    }
    ok = res > 0;
    stamp(ts, sizeof ts);
    snprintf(keep, sizeof keep, "%s/%s", um, ok ? ".installed" : ".failed"); mkdir_(keep);
    snprintf(kz, sizeof kz, "%s/%s", keep, name);
    if (exists(kz)) retire(kz, name);
    if (rename(zp, kz)) dsl_log("pkg: cannot move %s to %s", zp, keep);
    if (exists(tmp) && rmdir(tmp)) retire(tmp, "unpack");               /* what is left of a nested zip */
    if (res == 0) id[0] = 0;
    return res;
}

#ifndef _WIN32
/* Mac: ROOT/mods/<id> -> ROOT/loader/mods/<id> when its mod.json differs (the stub's copy_in) */
static int copy_in(const char *src, const char *dst, const char *id) {
    char tmp[1500]; snprintf(tmp, sizeof tmp, "%s.new-%d", dst, (int)getpid());
    if (copyfile(src, tmp, NULL, COPYFILE_ALL | COPYFILE_RECURSIVE)) { dsl_log("pkg: copy %s failed", src); return -1; }
    char *av[] = { "/usr/bin/xattr", "-cr", tmp, NULL }; run(av);      /* dyld refuses a quarantined dylib */
    char m[1600]; snprintf(m, sizeof m, "%s/.player_mod", tmp);
    FILE *f = fopen(m, "w"); if (f) { fputs("ROOT/mods\n", f); fclose(f); }
    if (exists(dst) && retire(dst, id)) return -1;
    return rename(tmp, dst);
}
#endif

/* install every new zip of the player's mods folder; returns how many mods are new or changed (and the Mods list was
   read again), -1 = no mods folder. fail: one line for the player when a zip was not a mod (else ""). */
void dsl_mods_rescan(void);                                            /* src/dsl_reg.c */
int dsl_pkg_refresh(char *fail, size_t fcap) {
    if (fail && fcap) fail[0] = 0;
    const char *lm = dsl_mods_dir(), *um = getenv("DSML_USER_MODS_DIR");
    if (!um || !*um) um = lm;
    if (!um || !*um) return -1;
    snprintf(root_dir, sizeof root_dir, "%s", um);
    { char *s = strrchr(root_dir, '/'), *b = strrchr(root_dir, '\\'); if (b > s) s = b; if (s) *s = 0; }
    mkdir_(um);
    int changed = 0, bad = 0, staged = 0, stuck = 0; char first_bad[300] = "", first_staged[300] = "", first_stuck[300] = "";
    DIR *d = opendir(um); struct dirent *e; char names[64][256]; int nz = 0;
    while (d && (e = readdir(d)) && nz < 64) {
        size_t l = strlen(e->d_name);
        if (e->d_name[0] != '.' && l > 4 && !strcasecmp(e->d_name + l - 4, ".zip")) snprintf(names[nz++], 256, "%s", e->d_name);
    }
    if (d) closedir(d);
    for (int i = 0; i < nz; i++) {
        char id[64];
        int r = install_zip(um, names[i], id, sizeof id);
        if (r == 1) {
            if (!strcmp(um, lm ? lm : "")) changed++;                     /* Mac: counted when copied into the loader below */
            dsl_log("pkg: %s installed as %s", names[i], id);
        }
        else if (r == 2) { if (!staged++) snprintf(first_staged, sizeof first_staged, "%s", names[i]); dsl_log("pkg: %s: %s is in use, the new version waits in .pending for the next start", names[i], id); }
        else if (r < 0) { if (!stuck++) snprintf(first_stuck, sizeof first_stuck, "%s", names[i]); dsl_log("pkg: %s (%s) could not be put in place (moved to .failed)", names[i], id); }
        else { if (!bad++) snprintf(first_bad, sizeof first_bad, "%s", names[i]); dsl_log("pkg: %s is not a mod package (moved to .failed)", names[i]); }
    }
#ifndef _WIN32
    if (lm && *lm && strcmp(um, lm)) {                                   /* Mac: into the loader's folder */
        d = opendir(um);
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            char dir[1400], id[64], a_p[1500], b_p[1500], dst[1400];
            snprintf(dir, sizeof dir, "%s/%s", um, e->d_name);
            if (!mod_id(dir, id, sizeof id) || strcmp(id, e->d_name)) continue;
            snprintf(a_p, sizeof a_p, "%s/mod.json", dir); snprintf(dst, sizeof dst, "%s/%s", lm, id); snprintf(b_p, sizeof b_p, "%s/mod.json", dst);
            char *a = slurp(a_p), *b = slurp(b_p);
            if (a && (!b || strcmp(a, b))) {
                if (copy_in(dir, dst, id) == 0) { changed++; dsl_log("pkg: player mod %s copied into the loader", id); }
            }
            free(a); free(b);
        }
        if (d) closedir(d);
    }
#endif
    if (stuck && fail) snprintf(fail, fcap, "%s could not be installed (the mods folder could not be changed). It was moved to the folder mods/.failed.", first_stuck);
    else if (staged == 1 && fail) snprintf(fail, fcap, "%s is ready. The new version installs the next time the game starts.", first_staged);
    else if (staged > 1 && fail) snprintf(fail, fcap, "%d mods are ready. The new versions install the next time the game starts.", staged);
    else if (bad && fail) {
        if (bad == 1) snprintf(fail, fcap, "%s is not a mod for Dungeon Settlers Modded. It was moved to the folder mods/.failed.", first_bad);
        else snprintf(fail, fcap, "%d files are not mods for Dungeon Settlers Modded. They were moved to the folder mods/.failed.", bad);
    }
    if (changed) dsl_mods_rescan();
    return changed;
}
