// Modded saves: Steam Cloud backup + import from the game (owner 2026-10-16, item 7 option d).
// The modded saves stay in <DSML_DATA>/Saves (never the game's own folder, so the game started from Steam never lists
// them). This file:
//   cloud   a background thread copies them to the player's Steam Cloud through ISteamRemoteStorage (the game's own
//           steam_api, same app id), remote names "dsmodded_<file>". The game's own Auto-Cloud files (Saves/*.json of
//           its folder) are never read, written or deleted; neither is any remote name without our prefix.
//           Each pass: a save changed here -> upload; changed in the cloud (another computer) -> download; missing
//           here and never synced here -> download (a new computer); deleted here after a sync -> deleted in the cloud.
//           Both changed: the newest wins, the other one is kept in <DSML_DATA>/Mods/cloud_backups/.
//           Space: an upload only when the cloud keeps max(1 GB, 20 x the game's own use) free after it and our files
//           stay at most CLOUD_MAX_FILES (app quota 10 GB / 700 files, appinfo ufs); else no upload, one status line.
//           State: <DSML_DATA>/Mods/cloud_state.tsv (name, local mtime, local size, remote time at the last sync).
//   import  the Mods window's "Saves" tab lists the worlds of the game's own save folder (DSML_GAME_SAVES, set by the
//           start part) and imports (copies) one world's saves into the modded saves. The game's file is only read.
// Env: DSML_DATA (required), DSML_GAME_SAVES (import), DSL_CLOUD=0 (cloud off), DSL_CLOUD_API=<library> (tests: a fake
// steam_api instead of the game's), DSL_CLOUD_EVERY=<seconds between passes, default 30>.
#include "dsl_internal.h"
#include "dsl_menu.h"
#include "dsl_os.h"
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <sys/utime.h>
#else
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <utime.h>
#endif

#define PFX "dsmodded_"
#define CLOUD_MAX_FILES 300
#define MIN_FREE 1000000000ULL
#define MAXF 512

static char saves_d[1100], mods_d[1100], state_p[1200], backups_d[1200], game_saves[1100];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static char status_s[300] = "Cloud backup starts once Steam is ready.";
int dsl_cloud_passes, dsl_cloud_up, dsl_cloud_down, dsl_cloud_del, dsl_cloud_conflicts, dsl_cloud_skipped_space;

static struct {
    void *(*get)(void);
    bool (*write)(void *, const char *, const void *, int32_t);
    int32_t (*read)(void *, const char *, void *, int32_t);
    int32_t (*size)(void *, const char *);
    int64_t (*ts)(void *, const char *);
    bool (*del)(void *, const char *);
    int32_t (*count)(void *);
    const char *(*name)(void *, int, int32_t *);
    bool (*quota)(void *, uint64_t *, uint64_t *);
    bool (*acc)(void *);
    bool (*app)(void *);
} A;

static void set_status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_status(const char *fmt, ...) {
    char b[300]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    pthread_mutex_lock(&mu);
    int changed = strcmp(b, status_s) != 0; snprintf(status_s, sizeof status_s, "%s", b);
    pthread_mutex_unlock(&mu);
    if (changed) dsl_log("cloud: %s", b);
}
void dsl_cloud_status(char *out, size_t cap) { pthread_mutex_lock(&mu); snprintf(out, cap, "%s", status_s); pthread_mutex_unlock(&mu); }

/* ------------------------------------------------------------------ files */
static int is_json(const char *n) { size_t l = strlen(n); return l > 5 && !strcmp(n + l - 5, ".json"); }
static char *read_all(const char *p, long *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long k = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = k >= 0 && k < (100L << 20) ? malloc((size_t)k + 1) : NULL;
    if (!b) { fclose(f); return NULL; }
    size_t r = fread(b, 1, (size_t)k, f); fclose(f); b[r] = 0;
    if ((long)r != k) { free(b); return NULL; }
    *n = k; return b;
}
static int write_all(const char *p, const void *b, long n, int64_t mtime) {
    char t[1300]; snprintf(t, sizeof t, "%s.dsl-tmp", p);
    FILE *f = fopen(t, "wb"); if (!f) return -1;
    size_t w = fwrite(b, 1, (size_t)n, f);
    if (fclose(f) != 0 || (long)w != n) return -1;
    if (mtime > 0) { struct utimbuf u = { (time_t)mtime, (time_t)mtime }; utime(t, &u); }
    return rename(t, p);
}
static int copy_file(const char *s, const char *d) {
    long n; char *b = read_all(s, &n); if (!b) return -1;
    struct stat st; int64_t mt = stat(s, &st) ? 0 : (int64_t)st.st_mtime;
    int r = write_all(d, b, n, mt); free(b); return r;
}
static void stamp(char *o, size_t n) { time_t t = time(NULL); strftime(o, n, "%Y%m%d-%H%M%S", localtime(&t)); }
/* keep a copy of `src` (a file) or of `data` in cloud_backups/<stem>-<why>-<time>.json */
static void backup(const char *name, const char *why, const char *src, const void *data, long n) {
    mkdir(backups_d, 0755);
    char ts[32], p[1500]; stamp(ts, sizeof ts);
    snprintf(p, sizeof p, "%s/%.*s-%s-%s.json", backups_d, (int)(strlen(name) - 5), name, why, ts);
    int r = src ? copy_file(src, p) : write_all(p, data, n, 0);
    dsl_log("cloud: kept %s (%s)", p, r ? "FAILED" : "ok");
}

/* ------------------------------------------------------------------ state */
typedef struct { char name[256]; int64_t lm, ls, rts; int seen; } St;
static St st[MAXF]; static int nst;
static void state_load(void) {
    nst = 0; FILE *f = fopen(state_p, "r"); if (!f) return;
    char line[600];
    while (nst < MAXF && fgets(line, sizeof line, f)) {
        char *t1 = strchr(line, '\t'); if (!t1) continue; *t1 = 0;
        long long a, b, c; if (sscanf(t1 + 1, "%lld\t%lld\t%lld", &a, &b, &c) != 3) continue;
        snprintf(st[nst].name, sizeof st[nst].name, "%s", line); st[nst].lm = a; st[nst].ls = b; st[nst].rts = c; nst++;
    }
    fclose(f);
}
static void state_save(void) {
    char t[1300]; snprintf(t, sizeof t, "%s.tmp", state_p);
    FILE *f = fopen(t, "w"); if (!f) return;
    for (int i = 0; i < nst; i++) if (st[i].name[0]) fprintf(f, "%s\t%lld\t%lld\t%lld\n", st[i].name, (long long)st[i].lm, (long long)st[i].ls, (long long)st[i].rts);
    fclose(f); rename(t, state_p);
}
static St *state_get(const char *n) { for (int i = 0; i < nst; i++) if (st[i].name[0] && !strcmp(st[i].name, n)) return &st[i]; return NULL; }
static void state_set(const char *n, int64_t lm, int64_t ls, int64_t rts) {
    St *s = state_get(n);
    if (!s && nst < MAXF) { s = &st[nst++]; snprintf(s->name, sizeof s->name, "%s", n); }
    if (s) { s->lm = lm; s->ls = ls; s->rts = rts; }
}
static void state_drop(const char *n) { St *s = state_get(n); if (s) s->name[0] = 0; }

/* ------------------------------------------------------------------ one pass */
typedef struct { char name[256]; int64_t mtime, size, rts, rsize; int here, there; } Ent;
static Ent E[MAXF]; static int nE;
static Ent *ent(const char *n) {
    for (int i = 0; i < nE; i++) if (!strcmp(E[i].name, n)) return &E[i];
    if (nE >= MAXF) return NULL;
    Ent *e = &E[nE++]; memset(e, 0, sizeof *e); snprintf(e->name, sizeof e->name, "%s", n); return e;
}
static int download(void *rs, Ent *e) {
    char rn[300], p[1400]; snprintf(rn, sizeof rn, PFX "%s", e->name); snprintf(p, sizeof p, "%s/%s", saves_d, e->name);
    int32_t n = A.size(rs, rn); if (n <= 0) return -1;
    char *b = malloc((size_t)n); if (!b) return -1;
    int32_t r = A.read(rs, rn, b, n);
    int ok = r == n && !write_all(p, b, n, e->rts);
    free(b);
    if (!ok) { dsl_log("cloud: download %s FAILED", e->name); return -1; }
    struct stat s; if (!stat(p, &s)) state_set(e->name, (int64_t)s.st_mtime, (int64_t)s.st_size, e->rts);
    dsl_cloud_down++; dsl_log("cloud: downloaded %s (%d bytes)", e->name, (int)n);
    return 0;
}
static int room(void *rs, int64_t size, int ours_n, uint64_t ours_bytes) {
    uint64_t total = 0, avail = 0;
    if (!A.quota(rs, &total, &avail)) return 0;
    uint64_t used = total > avail ? total - avail : 0, game = used > ours_bytes ? used - ours_bytes : 0;
    uint64_t keep = game * 20 > MIN_FREE ? game * 20 : MIN_FREE;
    return ours_n < CLOUD_MAX_FILES && avail > keep && avail - keep > (uint64_t)size;
}
static int upload(void *rs, Ent *e, int *ours_n, uint64_t *ours_bytes) {
    char rn[300], p[1400]; snprintf(rn, sizeof rn, PFX "%s", e->name); snprintf(p, sizeof p, "%s/%s", saves_d, e->name);
    if (!room(rs, e->size, *ours_n + !e->there, *ours_bytes)) { dsl_cloud_skipped_space++; return 1; }
    long n; char *b = read_all(p, &n); if (!b) return -1;
    bool ok = A.write(rs, rn, b, (int32_t)n); free(b);
    if (!ok) { dsl_log("cloud: upload %s FAILED", e->name); return -1; }
    if (!e->there) { (*ours_n)++; *ours_bytes += (uint64_t)n; }
    e->rts = A.ts(rs, rn); e->there = 1;
    state_set(e->name, e->mtime, e->size, e->rts);
    dsl_cloud_up++; dsl_log("cloud: uploaded %s (%ld bytes)", e->name, n);
    return 0;
}
static int same_content(void *rs, Ent *e) {
    if (e->size != e->rsize) return 0;
    char rn[300], p[1400]; snprintf(rn, sizeof rn, PFX "%s", e->name); snprintf(p, sizeof p, "%s/%s", saves_d, e->name);
    long n; char *a = read_all(p, &n); if (!a) return 0;
    char *b = malloc((size_t)n + 1); int same = b && A.read(rs, rn, b, (int32_t)n) == n && !memcmp(a, b, (size_t)n);
    free(a); free(b); return same;
}

/* 0 = pass done, 1 = not possible now (status says why) */
int dsl_cloud_pass(void) {
    void *rs = A.get ? A.get() : NULL;
    if (!rs) { set_status("Cloud backup is waiting for Steam."); return 1; }
    if (!A.acc(rs) || !A.app(rs)) { set_status("Cloud backup is off: Steam Cloud is turned off for this game or this account."); return 1; }
    dsl_cloud_passes++;
    state_load(); nE = 0;
    int ours_n = 0; uint64_t ours_bytes = 0;
    int32_t cnt = A.count(rs);
    for (int i = 0; i < cnt; i++) {
        int32_t sz = 0; const char *rn = A.name(rs, i, &sz);
        if (!rn || strncmp(rn, PFX, strlen(PFX)) || !is_json(rn + strlen(PFX))) continue;
        Ent *e = ent(rn + strlen(PFX)); if (!e) break;
        e->there = 1; e->rsize = sz; e->rts = A.ts(rs, rn); ours_n++; ours_bytes += (uint64_t)sz;
    }
    time_t now = time(NULL); int busy = 0;
    DIR *d = opendir(saves_d); struct dirent *de;
    if (!d) { set_status("Cloud backup is waiting: the modded save folder is missing."); return 1; }
    {
        while ((de = readdir(d))) {
            if (de->d_name[0] == '.' || !is_json(de->d_name)) continue;
            char p[1400]; struct stat s; snprintf(p, sizeof p, "%s/%s", saves_d, de->d_name);
            if (stat(p, &s) || !S_ISREG(s.st_mode)) continue;
            Ent *e = ent(de->d_name); if (!e) break;
            e->here = 1; e->mtime = (int64_t)s.st_mtime; e->size = (int64_t)s.st_size;
            St *k = state_get(de->d_name);                                   /* being written (not our own last sync): next pass */
            if (now - s.st_mtime < 5 && !(k && k->lm == e->mtime && k->ls == e->size)) { e->here = 2; busy++; }
        }
        closedir(d);
    }
    /* many synced saves gone at once looks like a lost folder, not a player's choice: no cloud delete this time */
    int gone = 0, synced = 0;
    for (int i = 0; i < nE; i++) { St *s = state_get(E[i].name); if (s) { synced++; if (!E[i].here && E[i].there && s->rts == E[i].rts) gone++; } }
    int no_delete = gone > 2 && gone * 2 > synced;
    if (no_delete) dsl_log("cloud: %d of %d synced saves are missing here: nothing deleted in the cloud", gone, synced);
    int fails = 0, space = 0;
    for (int i = 0; i < nE; i++) {
        Ent *e = &E[i]; St *s = state_get(e->name);
        if (e->here == 2) continue;
        if (e->here && e->there) {
            int lchg = !s || s->lm != e->mtime || s->ls != e->size, rchg = !s || s->rts != e->rts || s->ls != e->rsize;
            if (!lchg && !rchg) continue;
            if (lchg && !rchg) { int r = upload(rs, e, &ours_n, &ours_bytes); fails += r < 0; space += r == 1; continue; }
            if (!lchg && rchg) { fails += download(rs, e) != 0; continue; }
            if (same_content(rs, e)) { state_set(e->name, e->mtime, e->size, e->rts); continue; }
            dsl_cloud_conflicts++;
            char p[1400]; snprintf(p, sizeof p, "%s/%s", saves_d, e->name);
            if (e->mtime >= e->rts) {                                            /* this computer's copy is newer */
                char rn[300]; snprintf(rn, sizeof rn, PFX "%s", e->name);
                int32_t n = A.size(rs, rn); char *b = n > 0 ? malloc((size_t)n) : NULL;
                if (b && A.read(rs, rn, b, n) == n) backup(e->name, "cloud", NULL, b, n);
                free(b);
                int r = upload(rs, e, &ours_n, &ours_bytes); fails += r < 0; space += r == 1;
            } else { backup(e->name, "this-computer", p, NULL, 0); fails += download(rs, e) != 0; }
        } else if (e->here) {
            int r = upload(rs, e, &ours_n, &ours_bytes); fails += r < 0; space += r == 1;
        } else if (e->there) {
            if (s && s->rts == e->rts && no_delete) continue;
            if (s && s->rts == e->rts) {                                       /* synced, then deleted here */
                char rn[300]; snprintf(rn, sizeof rn, PFX "%s", e->name);
                if (A.del(rs, rn)) { state_drop(e->name); dsl_cloud_del++; ours_n--; dsl_log("cloud: %s deleted here: removed from the cloud", e->name); }
                else fails++;
            } else fails += download(rs, e) != 0;                             /* new computer, or changed elsewhere */
        }
    }
    for (int i = 0; i < nst; i++) if (st[i].name[0]) {                       /* gone on both sides */
        Ent *e = NULL; for (int k = 0; k < nE && !e; k++) if (!strcmp(E[k].name, st[i].name)) e = &E[k];
        if (!e) st[i].name[0] = 0;
    }
    state_save();
    char tm[16]; time_t t = time(NULL); strftime(tm, sizeof tm, "%H:%M", localtime(&t));
    if (space) set_status("Cloud backup paused: Steam Cloud is almost full, so %d save%s only on this computer. The game's own saves get the space first.", space, space == 1 ? " is" : "s are");
    else if (fails) set_status("Cloud backup failed for %d save%s at %s. It will try again in a moment.", fails, fails == 1 ? "" : "s", tm);
    else set_status("Cloud backup: %d save%s in your Steam Cloud, checked at %s.", ours_n, ours_n == 1 ? "" : "s", tm);
    (void)busy;
    return 0;
}

/* ------------------------------------------------------------------ steam_api */
static void *api_lib(void) {
    const char *t = getenv("DSL_CLOUD_API");
    if (t && *t) return dsl_os_dlopen(t);
#ifdef _WIN32
    return (void *)GetModuleHandleA("steam_api64.dll");
#else
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char *n = _dyld_get_image_name(i);
        if (n && strstr(n, "libsteam_api.dylib")) return dlopen(n, RTLD_NOW | RTLD_NOLOAD);
    }
    return NULL;
#endif
}
static int api_bind(void) {
    void *h = api_lib(); if (!h) return -1;
#define B(f, n) *(void **)&A.f = dsl_os_dlsym(h, n); if (!A.f) { dsl_log("cloud: %s missing", n); return -1; }
    B(get, "SteamAPI_SteamRemoteStorage_v016");
    B(write, "SteamAPI_ISteamRemoteStorage_FileWrite"); B(read, "SteamAPI_ISteamRemoteStorage_FileRead");
    B(size, "SteamAPI_ISteamRemoteStorage_GetFileSize"); B(ts, "SteamAPI_ISteamRemoteStorage_GetFileTimestamp");
    B(del, "SteamAPI_ISteamRemoteStorage_FileDelete"); B(count, "SteamAPI_ISteamRemoteStorage_GetFileCount");
    B(name, "SteamAPI_ISteamRemoteStorage_GetFileNameAndSize"); B(quota, "SteamAPI_ISteamRemoteStorage_GetQuota");
    B(acc, "SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount"); B(app, "SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp");
#undef B
    return 0;
}
int dsl_cloud_test_bind(void) { return A.get ? 0 : api_bind(); }   /* loader/test/cloud */
static void *cloud_thread(void *arg) {
    (void)arg;
    int every = getenv("DSL_CLOUD_EVERY") ? atoi(getenv("DSL_CLOUD_EVERY")) : 30; if (every < 1) every = 30;
    for (int tries = 0; ; tries++) {
        if (!A.get && api_bind()) { if (tries == 60) set_status("Cloud backup is off: Steam is not running."); sleep(5); continue; }
        dsl_cloud_pass();
        sleep((unsigned)every);
    }
    return NULL;
}

/* ------------------------------------------------------------------ import (Mods window tab "Saves") */
typedef struct { char guid[40], clan[64], when[24]; int n; } World;
static World W[64]; static int nW;
static char import_msg[200];
static void header_str(const char *h, const char *key, char *out, size_t cap) {
    out[0] = 0; char k[64]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = h ? strstr(h, k) : NULL; if (!p) return;
    p = strchr(p + strlen(k), ':'); if (!p) return; p = strchr(p, '"'); if (!p) return; p++;
    size_t i = 0; while (p[i] && p[i] != '"' && i + 1 < cap) { out[i] = p[i]; i++; } out[i] = 0;
}
static int file_header(const char *dir, const char *f, char *guid, char *clan, char *when) {
    char p[1400]; snprintf(p, sizeof p, "%s/%s", dir, f);
    long n; char *b = read_all(p, &n); if (!b) return 0;
    const char *h = strstr(b, "\"CampaignSaveHeader\"");
    header_str(h, "CampaignGuid", guid, 40); header_str(h, "ClanName", clan, 64); header_str(h, "SaveTime", when, 24);
    free(b); return strlen(guid) == 36;
}
static void scan_game(void) {
    nW = 0; DIR *d = game_saves[0] ? opendir(game_saves) : NULL; struct dirent *de; if (!d) return;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' || !is_json(de->d_name)) continue;
        char g[40], c[64], w[24]; if (!file_header(game_saves, de->d_name, g, c, w)) continue;
        int i = 0; while (i < nW && strcmp(W[i].guid, g)) i++;
        if (i == nW) { if (nW == 64) continue; nW++; memset(&W[i], 0, sizeof W[i]); snprintf(W[i].guid, 40, "%s", g); snprintf(W[i].clan, 64, "%s", c); }
        W[i].n++; if (strcmp(w, W[i].when) > 0) snprintf(W[i].when, 24, "%s", w);
    }
    closedir(d);
}
/* copy every save of world g from the game's folder into the modded saves; returns files copied (-1 = error) */
int dsl_cloud_import(const char *g) {
    DIR *d = game_saves[0] ? opendir(game_saves) : NULL; struct dirent *de; if (!d) return -1;
    int n = 0, bad = 0; mkdir(saves_d, 0755);
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' || !is_json(de->d_name)) continue;
        char fg[40], c[64], w[24]; if (!file_header(game_saves, de->d_name, fg, c, w) || strcmp(fg, g)) continue;
        char s[1400], t[1400]; snprintf(s, sizeof s, "%s/%s", game_saves, de->d_name); snprintf(t, sizeof t, "%s/%s", saves_d, de->d_name);
        long a, b; char *x = read_all(s, &a), *y = read_all(t, &b);
        int same = x && y && a == b && !memcmp(x, y, (size_t)a);
        if (y && !same) backup(de->d_name, "before-import", t, NULL, 0);
        if (x && !same) { struct stat ss; int64_t mt = stat(s, &ss) ? 0 : (int64_t)ss.st_mtime; if (write_all(t, x, a, mt)) bad++; else n++; }
        free(x); free(y);
    }
    closedir(d);
    dsl_log("cloud: import of world %s: %d save(s) copied, %d failed (the game's own files are unchanged)", g, n, bad);
    return bad ? -1 : n;
}

static DSLObj tab_root, status_t, msg_t;
static float tab_w, tab_h;
static void tab_fill(void);
static void on_import(void *ud, DSLObj w) {
    (void)w; int i = (int)(intptr_t)ud; if (i < 0 || i >= nW) return;
    int n = dsl_cloud_import(W[i].guid);
    if (n < 0) snprintf(import_msg, sizeof import_msg, "Could not import %s. The details are in dsload.log.", W[i].clan);
    else if (n == 0) snprintf(import_msg, sizeof import_msg, "%s is already in your modded saves.", W[i].clan);
    else snprintf(import_msg, sizeof import_msg, "%s imported (%d save%s). You can open it from Load.", W[i].clan, n, n == 1 ? "" : "s");
    const DSLUi *u = dsl_ui_kit(); if (u && msg_t) u->set_text(msg_t, import_msg);
}
static void tab_open(void *ud, DSLObj root, float w, float h) {
    (void)ud; tab_root = root; tab_w = w; tab_h = h; import_msg[0] = 0; tab_fill();
}
static void tab_fill(void) {
    const DSLUi *u = dsl_ui_kit(); if (!u || !tab_root) return;
    char s[300]; dsl_cloud_status(s, sizeof s);
    float x = 16, y = 12, w = tab_w - 32;
    status_t = u->text(tab_root, x, y, w, 44, s, 18, DSL_TXT_LEFT | DSL_TXT_WRAP, 0xFFFFFFFF); y += 54;
    u->text(tab_root, x, y, w, 30, "Import a world from the game", 22, DSL_TXT_LEFT | DSL_TXT_TITLE, 0xFFE9C46A); y += 32;
    u->text(tab_root, x, y, w, 40, "This copies the world. The game's own save is not changed, and your modded saves never show up in the game's Load list.", 16, DSL_TXT_LEFT | DSL_TXT_WRAP, 0xFFCCCCCC); y += 46;
    msg_t = u->text(tab_root, x, y, w, 24, import_msg, 16, DSL_TXT_LEFT, 0xFF8FC4FF); y += 30;
    scan_game();
    if (!nW) { u->text(tab_root, x, y, w, 30, game_saves[0] ? "No saves found in the game's folder." : "The game's save folder is not known here.", 18, DSL_TXT_LEFT, 0xFFCCCCCC); return; }
    DSLObj c = u->scroll(tab_root, x, y, w, tab_h - y - 12); float ry = 0;
    for (int i = 0; i < nW; i++) {
        char line[200]; snprintf(line, sizeof line, "%s   %d save%s, last %s", W[i].clan, W[i].n, W[i].n == 1 ? "" : "s", W[i].when);
        u->text(c, 0, ry + 6, w - 150, 30, line, 18, DSL_TXT_LEFT, 0xFFFFFFFF);
        u->button(c, w - 140, ry, 130, 40, "Import", on_import, (void *)(intptr_t)i);
        ry += 48;
    }
    u->scroll_height(c, ry);
}
static void tab_update(void *ud) {
    (void)ud; static int k; if (++k % 60) return;
    const DSLUi *u = dsl_ui_kit(); if (!u || !status_t) return;
    char s[300]; dsl_cloud_status(s, sizeof s); u->set_text(status_t, s);
}
static void tab_close(void *ud) { (void)ud; tab_root = status_t = msg_t = NULL; }

/* after il2cpp_init (the tab) and before the game's Steam start (the thread waits for it) */
void dsl_cloud_install(DSLHost *host) {
    const char *d = getenv("DSML_DATA");
    if (!d || !*d) { dsl_log("cloud: off (no DSML_DATA)"); return; }
    snprintf(saves_d, sizeof saves_d, "%s/Saves", d); snprintf(mods_d, sizeof mods_d, "%s/Mods", d);
    snprintf(state_p, sizeof state_p, "%s/cloud_state.tsv", mods_d); snprintf(backups_d, sizeof backups_d, "%s/cloud_backups", mods_d);
    const char *g = getenv("DSML_GAME_SAVES"); if (g && *g) snprintf(game_saves, sizeof game_saves, "%s", g);
    mkdir(mods_d, 0755);
    static const DSLTab t = { "saves", "Saves", 5, tab_open, tab_update, tab_close, NULL, NULL };
    if (host && host->menu_add_tab) host->menu_add_tab(NULL, &t);
    const char *off = getenv("DSL_CLOUD");
    if (off && !strcmp(off, "0")) { set_status("Cloud backup is off (DSL_CLOUD=0)."); return; }
    pthread_t th; if (pthread_create(&th, NULL, cloud_thread, NULL)) { dsl_log("cloud: thread failed"); return; }
    pthread_detach(th);
    dsl_log("cloud: on (%s -> Steam Cloud " PFX "*, import from %s)", saves_d, game_saves[0] ? game_saves : "(none)");
}
