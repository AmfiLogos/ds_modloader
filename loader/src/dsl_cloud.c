// Modded saves: Steam Cloud backup (owner 2026-10-16, item 7 option d).
// The modded-only saves stay in <DSML_DATA>/Saves (never the game's own folder, so the game started from Steam never lists
// them). The player's game saves are played in place from the game's own folder (dsl_saves.c): they are the game's
// Auto-Cloud files and this file never touches them. This file:
//   cloud   a background thread copies them to the player's Steam Cloud through ISteamRemoteStorage (the game's own
//           steam_api, same app id), remote names "dsmodded_<file>". The game's own Auto-Cloud files (Saves/*.json of
//           its folder) are never read, written or deleted; neither is any remote name without our prefix.
//           Each pass: a save changed here -> upload; changed in the cloud (another computer) -> download; missing
//           here and never synced here -> download (a new computer); deleted here after a sync -> deleted in the cloud.
//           Both changed: the newest wins, the other one is kept in <DSML_DATA>/Mods/cloud_backups/.
//           Space: an upload only when the cloud keeps max(1 GB, 20 x the game's own use) free after it and our files
//           stay at most CLOUD_MAX_FILES (app quota 10 GB / 700 files, appinfo ufs); else no upload, one status line.
//           State: <DSML_DATA>/Mods/cloud_state.tsv (name, local mtime, local size, remote time at the last sync).
// Env: DSML_DATA (required), DSL_CLOUD=0 (cloud off), DSL_CLOUD_API=<library> (tests: a fake
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

static char saves_d[1100], mods_d[1100], state_p[1200], backups_d[1200];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
/* the status is kept as a code + its numbers (set on the cloud thread) and made into text by whoever shows it (main
   thread, in the game's language; dsl_cloud_status). The log line stays English. */
enum { CS_STARTS, CS_WAIT_STEAM, CS_OFF_CLOUD, CS_WAIT_FOLDER, CS_PAUSED, CS_FAILED, CS_OK, CS_NO_STEAM, CS_OFF_ENV };
static int st_code = CS_STARTS, st_n; static char st_tm[16];
int dsl_cloud_rdel, dsl_cloud_passes, dsl_cloud_up, dsl_cloud_down, dsl_cloud_del, dsl_cloud_conflicts, dsl_cloud_skipped_space;

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

/* the status texts by code: one text, or a plural pair (%d saves, then %s = the time; TRN_NOOP marks it for the
   extractor). tr = 1: the game's language (TR / TRN; main thread only), 0: English (the log; tests look for
   "cloud: Cloud backup: ") */
static const char *const CS_TEXT[] = {
    [CS_STARTS] = TR_NOOP("Cloud backup starts once Steam is ready."),
    [CS_WAIT_STEAM] = TR_NOOP("Cloud backup is waiting for Steam."),
    [CS_OFF_CLOUD] = TR_NOOP("Cloud backup is off: Steam Cloud is turned off for this game or this account."),
    [CS_WAIT_FOLDER] = TR_NOOP("Cloud backup is waiting: the modded save folder is missing."),
    [CS_NO_STEAM] = TR_NOOP("Cloud backup is off: Steam is not running."),
    [CS_OFF_ENV] = TR_NOOP("Cloud backup is off (DSL_CLOUD=0)."),
};
static const char *const CS_PLURAL[][2] = {
    [CS_PAUSED] = { TRN_NOOP("Cloud backup paused: Steam Cloud is almost full, so %d save is only on this computer. The game's own saves get the space first.",
                             "Cloud backup paused: Steam Cloud is almost full, so %d saves are only on this computer. The game's own saves get the space first.") },
    [CS_FAILED] = { TRN_NOOP("Cloud backup failed for %d save at %s. It will try again in a moment.",
                             "Cloud backup failed for %d saves at %s. It will try again in a moment.") },
    [CS_OK] = { TRN_NOOP("Cloud backup: %d save in your Steam Cloud, checked at %s.",
                         "Cloud backup: %d saves in your Steam Cloud, checked at %s.") },
};
#define CS_COUNT(t) ((int)(sizeof t / sizeof t[0]))
static void status_text(int code, int n, const char *tm, int tr, char *b, size_t cap) {
    if (code >= 0 && code < CS_COUNT(CS_PLURAL) && CS_PLURAL[code][0]) {
        const char *one = CS_PLURAL[code][0], *other = CS_PLURAL[code][1];
        if (code == CS_PAUSED) snprintf(b, cap, tr ? TRN(one, other, n) : n == 1 ? one : other, n);   /* only %d */
        else snprintf(b, cap, tr ? TRN(one, other, n) : n == 1 ? one : other, n, tm);                /* %d then %s */
    } else {
        const char *t = code >= 0 && code < CS_COUNT(CS_TEXT) && CS_TEXT[code] ? CS_TEXT[code] : CS_TEXT[CS_STARTS];
        snprintf(b, cap, "%s", tr ? TR(t) : t);
    }
    if (tr) dsi_utf8_fix_end(b);
}
#undef CS_COUNT
static void set_status(int code, int n, const char *tm) {
    pthread_mutex_lock(&mu);
    int changed = code != st_code || n != st_n || strcmp(tm ? tm : "", st_tm);
    st_code = code; st_n = n; snprintf(st_tm, sizeof st_tm, "%s", tm ? tm : "");
    pthread_mutex_unlock(&mu);
    if (changed) { char b[300]; status_text(code, n, tm ? tm : "", 0, b, sizeof b); dsl_log("cloud: %s", b); }
}
void dsl_cloud_status(char *out, size_t cap) {
    pthread_mutex_lock(&mu); int c = st_code, n = st_n; char tm[16]; snprintf(tm, sizeof tm, "%s", st_tm); pthread_mutex_unlock(&mu);
    status_text(c, n, tm, 1, out, cap);
}

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
    char t[1300]; snprintf(t, sizeof t, "%s.dsl-tmp-%d", p, (int)getpid());   /* unique: two game processes */
    FILE *f = fopen(t, "wb"); if (!f) return -1;
    size_t w = fwrite(b, 1, (size_t)n, f);
    int bad = fflush(f) != 0 || fsync(fileno(f)) != 0; bad |= fclose(f) != 0;
    if (bad || (long)w != n) { remove(t); return -1; }
    if (mtime > 0) { struct utimbuf u = { (time_t)mtime, (time_t)mtime }; utime(t, &u); }
    return rename(t, p);
}
static int copy_file(const char *s, const char *d) {
    long n; char *b = read_all(s, &n); if (!b) return -1;
    struct stat st; int64_t mt = stat(s, &st) ? 0 : (int64_t)st.st_mtime;
    int r = write_all(d, b, n, mt); free(b); return r;
}
static void stamp(char *o, size_t n) { time_t t = time(NULL); strftime(o, n, "%Y%m%d-%H%M%S", localtime(&t)); }
/* keep a copy of `src` (a file) or of `data` in cloud_backups/<stem>-<why>-<tag>.json. tag NULL = the time now; a tag
   (the version's time) makes it one file per version: already there = kept before, nothing written. 0 = the copy exists. */
static int backup(const char *name, const char *why, const char *tag, const char *src, const void *data, long n) {
    mkdir(backups_d, 0755);
    char ts[32], p[1500];
    if (tag) snprintf(ts, sizeof ts, "%s", tag); else stamp(ts, sizeof ts);
    snprintf(p, sizeof p, "%s/%.*s-%s-%s.json", backups_d, (int)(strlen(name) - 5), name, why, ts);
    struct stat st0; if (tag && !stat(p, &st0)) return 0;
    int r = src ? copy_file(src, p) : write_all(p, data, n, 0);
    dsl_log("cloud: kept %s (%s)", p, r ? "FAILED" : "ok");
    return r ? -1 : 0;
}
const char *dsl_cloud_backups_dir(void) { return backups_d; }

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
    char t[1300]; snprintf(t, sizeof t, "%s.tmp-%d", state_p, (int)getpid());
    FILE *f = fopen(t, "w"); if (!f) { dsl_log("cloud: cannot write %s", t); return; }
    int bad = 0;
    for (int i = 0; i < nst; i++) if (st[i].name[0]) bad |= fprintf(f, "%s\t%lld\t%lld\t%lld\n", st[i].name, (long long)st[i].lm, (long long)st[i].ls, (long long)st[i].rts) < 0;
    bad |= fflush(f) != 0; bad |= fsync(fileno(f)) != 0; bad |= fclose(f) != 0;
    if (bad) { dsl_log("cloud: state write failed: the old %s stays", state_p); remove(t); return; }
    rename(t, state_p);
}
/* names compare without case: Steam Cloud and the Mac / Windows file systems may change or ignore it */
static St *state_get(const char *n) { for (int i = 0; i < nst; i++) if (st[i].name[0] && !strcasecmp(st[i].name, n)) return &st[i]; return NULL; }
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
    for (int i = 0; i < nE; i++) if (!strcasecmp(E[i].name, n)) return &E[i];
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

/* one pass at a time across game processes (a soft restart's hidden B runs beside A): <Mods>/cloud.lock, held for
   the pass, freed by the OS if the process dies */
#ifdef _WIN32
static HANDLE lock_take(void) {
    char p[1300]; snprintf(p, sizeof p, "%s/cloud.lock", mods_d);
    wchar_t w[2048];                                    /* UTF-8 path (dsl_u8_win.h) */
    if (dsl_u8_to_w(p, w, 2048)) return INVALID_HANDLE_VALUE;
    return CreateFileW(w, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}
static int lock_ok(HANDLE h) { return h != INVALID_HANDLE_VALUE; }
static void lock_drop(HANDLE h) { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
#else
#include <fcntl.h>
#include <sys/file.h>
static int lock_take(void) {
    char p[1300]; snprintf(p, sizeof p, "%s/cloud.lock", mods_d);
    int fd = open(p, O_CREAT | O_RDWR, 0644); if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB)) { close(fd); return -1; }
    return fd;
}
static int lock_ok(int fd) { return fd >= 0; }
static void lock_drop(int fd) { if (fd >= 0) close(fd); }
#endif
int dsl_cloud_lock_busy;
static int pass_locked(void);
/* 0 = pass done, 1 = not possible now (status says why or another game process is in a pass) */
int dsl_cloud_pass(void) {
    __typeof__(lock_take()) lk = lock_take();
    if (!lock_ok(lk)) { dsl_cloud_lock_busy++; dsl_log("cloud: another game process is syncing: pass skipped"); return 1; }
    int r = pass_locked();
    lock_drop(lk);
    return r;
}
static int pass_locked(void) {
    void *rs = A.get ? A.get() : NULL;
    if (!rs) { set_status(CS_WAIT_STEAM, 0, NULL); return 1; }
    if (!A.acc(rs) || !A.app(rs)) { set_status(CS_OFF_CLOUD, 0, NULL); return 1; }
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
    if (!d) { set_status(CS_WAIT_FOLDER, 0, NULL); return 1; }
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
    int rgone = 0; for (int i = 0; i < nE; i++) { St *s = state_get(E[i].name); if (s && s->rts && E[i].here == 1 && !E[i].there) rgone++; }
    int no_rdelete = rgone > 2 && rgone * 2 > synced;                    /* an empty or failed cloud list: keep everything */
    if (no_rdelete) dsl_log("cloud: %d of %d synced saves are missing in the cloud: nothing moved aside here", rgone, synced);
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
                char tag[32]; snprintf(tag, sizeof tag, "%lld", (long long)e->rts);      /* one copy per cloud version */
                int kept = b && A.read(rs, rn, b, n) == n && !backup(e->name, "cloud", tag, NULL, b, n);
                free(b);
                if (!kept) { fails++; continue; }                                   /* never overwrite without the copy */
                int r = upload(rs, e, &ours_n, &ours_bytes); fails += r < 0; space += r == 1;
            } else {
                char tag[32]; snprintf(tag, sizeof tag, "%lld", (long long)e->mtime);   /* one copy per local version */
                if (backup(e->name, "this-computer", tag, p, NULL, 0)) { fails++; continue; }
                fails += download(rs, e) != 0;
            }
        } else if (e->here && s && s->rts && s->lm == e->mtime && s->ls == e->size) {
            /* synced before, unchanged here, gone from the cloud = deleted on another computer (autosave rotation, a
               world deleted there): moved to cloud_backups, never deleted; skipped when many vanish at once */
            if (no_rdelete) continue;
            char p[1400], tag[32]; snprintf(p, sizeof p, "%s/%s", saves_d, e->name); snprintf(tag, sizeof tag, "%lld", (long long)e->mtime);
            char bp[1500]; mkdir(backups_d, 0755);
            snprintf(bp, sizeof bp, "%s/%.*s-deleted-elsewhere-%s.json", backups_d, (int)(strlen(e->name) - 5), e->name, tag);
            if (rename(p, bp)) { fails++; dsl_log("cloud: %s deleted in the cloud, but moving it aside failed (%s)", e->name, strerror(errno)); continue; }
            state_drop(e->name); dsl_cloud_rdel++;
            dsl_log("cloud: %s was deleted on another computer: moved to %s", e->name, bp);
        } else if (e->here) {                                                 /* new here, or changed here since the sync */
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
        Ent *e = NULL; for (int k = 0; k < nE && !e; k++) if (!strcasecmp(E[k].name, st[i].name)) e = &E[k];
        if (!e) st[i].name[0] = 0;
    }
    state_save();
    char tm[16]; time_t t = time(NULL); strftime(tm, sizeof tm, "%H:%M", localtime(&t));
    if (space) set_status(CS_PAUSED, space, NULL);
    else if (fails) set_status(CS_FAILED, fails, tm);
    else set_status(CS_OK, ours_n, tm);
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
        if (!A.get && api_bind()) { if (tries == 60) set_status(CS_NO_STEAM, 0, NULL); sleep(5); continue; }
        dsl_cloud_pass();
        sleep((unsigned)every);
    }
    return NULL;
}

/* after il2cpp_init and before the game's Steam start (the thread waits for it) */
void dsl_cloud_install(DSLHost *host) {
    (void)host;
    const char *d = getenv("DSML_DATA");
    if (!d || !*d) { dsl_log("cloud: off (no DSML_DATA)"); return; }
    snprintf(saves_d, sizeof saves_d, "%s/Saves", d); snprintf(mods_d, sizeof mods_d, "%s/Mods", d);
    snprintf(state_p, sizeof state_p, "%s/cloud_state.tsv", mods_d); snprintf(backups_d, sizeof backups_d, "%s/cloud_backups", mods_d);
    mkdir(mods_d, 0755);
    const char *off = getenv("DSL_CLOUD");
    if (off && !strcmp(off, "0")) { set_status(CS_OFF_ENV, 0, NULL); return; }
    pthread_t th; if (pthread_create(&th, NULL, cloud_thread, NULL)) { dsl_log("cloud: thread failed"); return; }
    pthread_detach(th);
    dsl_log("cloud: on (%s -> Steam Cloud " PFX "*)", saves_d);
}
