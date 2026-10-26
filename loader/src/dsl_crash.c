// dsl_crash.c - crash reports, ask first (owner 2026-10-18: "crash reports, ASK FIRST, sent to a small private
// receiver"). Receiver: crashrep/receiver (Vercel), privacy note crashrep/PRIVACY.md.
//
// Consent: one notice at the first idle title while there is no answer (ROOT/crash/consent.json: "yes" / "no" plus a
// random install id, no hardware id); the Mods window's switch "Send crash reports" changes it later. Nothing is sent
// before Yes; No drops what is queued.
// When (all found at a start or at the title, never sent from a world):
//   - crash / freeze: every start writes ROOT/crash/sessions/<pid>.txt (START, TITLE at the first idle title, END at a
//     normal quit: hook on UnityEngine.Application.Internal_ApplicationQuit, and atexit). The next start finds a dead
//     process without END: TITLE seen = crash, never at the title = freeze ("WaitForLastPresentation" / "deadlock" in
//     the game's log = freeze too). A soft restart B counts only once it took over (LIVE); a prewarmed B that A killed
//     does not. The game's own ErrorLog.txt "Application End" after its last "Application Start" vetoes a crash of an A
//     (a normal quit the hooks missed). The logs of that session are copied at once (before the game writes over
//     player.log): dsload.prev.log, player.log, rig.log, ErrorLog.txt (tails).
//   - desync: a new folder in <data>/desync_reports (multiplayer): its README.txt summary (up to 3), plus rig.log.
//   - failed join: a new rig.log line of a join with rc -2 / -3 / -4.
//   - blank view: a new rig.log line "ERROR [view_blank] view: nothing visible for" (multiplayer, Mac and Windows).
// Limits: queue of 5, 3 sent per 24 h (ROOT/crash/sent.txt), gzip at most 3.5 MB (tails cut further when bigger).
// Personal data out before sending: dsl_scrub.c (names of this PC from dsl_uos_identity, Steam's loginusers.vdf, the
// multiplayer names in mp.json / mp_menu.json / mp_history.json).
// Active in a real start (DSML_ROOT, set by both starters) or DSL_CRASHREP=1; DSL_CRASHREP=0 = off.
// Test only: DSL_CRASHREP_TEST=<dir>: <dir>/answer ("yes" / "no": the open consent notice answered), <dir>/shot_<name>
// (a frame to <dir>/<name>.png), <dir>/quit (Application.Quit), <dir>/state.json (written every second).
#include "dsl_internal.h"
#include "dsl_menu.h"
#include "dsl_os.h"
#include "dsl_scrub.h"
#include "dsl_update_os.h"
#include "../sdk/dsl_sdk.h"
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CR_HOST "https://ds-crashrep.vercel.app"
#define CR_HOST_SHORT "ds-crashrep.vercel.app"            /* shown in the consent notice */
#define CR_KEY "dscr1-621ed5ae365845cd8ed0034a"   /* not a secret: the receiver's size cap and rate limits protect it */
#define CR_PER_DAY 3
#define CR_QUEUE_MAX 5
#define CR_GZ_MAX (3500u << 10)

int dsl_reg_scene(void);
int dsl_ui_title_shown(void);
int dsl_ui_notice_shown(void);
void dsl_ui_notice_no_esc(void);                                         /* dsl_ui.c: Escape does not answer the box shown last */
int dsl_soft_b_live(void);

static int on, role_b, live_marked, title_marked, ended, asked, sending;
static int consent = -1;                                   /* 1 yes, 0 no, -1 no answer yet */
static char cdir[1100], sess_path[1200], logdir_s[1100], install_id[33], last_result[200];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ small file helpers */
static void cpath(char *out, size_t n, const char *rel) { snprintf(out, n, "%s/%s", cdir, rel); }
static void mkdirs(const char *path) {
    char p[1200]; snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++) if (*s == '/' || *s == '\\') { char c = *s; *s = 0; mkdir(p, 0755); *s = c; }
    mkdir(p, 0755);
}
/* the last max bytes of a file (all of it when smaller); NULL = no file */
static char *tail(const char *p, size_t max, size_t *len) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); if (sz < 0) sz = 0;
    long from = (size_t)sz > max ? sz - (long)max : 0; fseek(f, from, SEEK_SET);
    size_t want = (size_t)(sz - from); char *b = malloc(want + 1);
    size_t n = b ? fread(b, 1, want, f) : 0; fclose(f);
    if (!b) return NULL;
    b[n] = 0; if (len) *len = n; return b;
}
static int write_all(const char *p, const char *s, size_t n) {
    char t[1300]; snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "wb"); if (!f) return -1;
    size_t w = fwrite(s, 1, n, f); if (fclose(f) || w != n) { remove(t); return -1; }
    return dsl_os_replace(t, p);
}
static void append_line(const char *p, const char *line) { FILE *f = fopen(p, "ab"); if (f) { fprintf(f, "%s\n", line); fclose(f); } }
/* the next line of a text, cut in place (strtok is not safe here: the update thread uses it at the same time) */
static char *next_line(char **cur) {
    char *s = *cur; if (!s) return NULL;
    while (*s == '\r' || *s == '\n') s++;
    if (!*s) { *cur = NULL; return NULL; }
    char *e = s + strcspn(s, "\r\n"); if (*e) { *e = 0; *cur = e + 1; } else *cur = NULL;
    return s;
}
static long long file_size(const char *p) { struct stat st; return stat(p, &st) ? -1 : (long long)st.st_size; }
static void data_dir(char *out, size_t n) {                /* the game's save folder (ErrorLog.txt, desync_reports) */
    const char *dd = getenv("DSML_DATA"), *rd = getenv("DSRIG_DIR");
    if (dd && *dd) snprintf(out, n, "%s", dd); else if (rd && *rd) snprintf(out, n, "%s/data", rd); else snprintf(out, n, "%s/data", logdir_s);
}
static void stamp(char *out, size_t n) { time_t t = time(NULL); strftime(out, n, "%Y%m%d-%H%M%S", localtime(&t)); }
static double mono(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
static void rm_tree(const char *d) {                       /* one queue folder (files only, no sub folders) */
    DIR *dir = opendir(d); struct dirent *e; char names[24][256]; int k = 0;
    while (dir && (e = readdir(dir)) && k < 24) if (e->d_name[0] != '.') snprintf(names[k++], sizeof names[0], "%s", e->d_name);
    if (dir) closedir(dir);
    for (int i = 0; i < k; i++) { char p[1500]; snprintf(p, sizeof p, "%s/%s", d, names[i]); remove(p); }
    rmdir(d);
}

/* ------------------------------------------------------------------ consent */
static void consent_save(void) {
    char p[1200], b[300]; cpath(p, sizeof p, "consent.json");
    int n = snprintf(b, sizeof b, "{\"consent\":\"%s\",\"install\":\"%s\"}\n", consent == 1 ? "yes" : consent == 0 ? "no" : "unasked", install_id);
    if (write_all(p, b, (size_t)n)) dsl_log("crash: cannot write %s", p);
}
static void consent_load(void) {
    char p[1200]; cpath(p, sizeof p, "consent.json");
    char *b = tail(p, 4096, NULL); JNode *j = b ? json_parse(b) : NULL;
    const char *c = json_str(j, "consent"), *id = json_str(j, "install");
    consent = c && !strcmp(c, "yes") ? 1 : c && !strcmp(c, "no") ? 0 : -1;
    int ok = id && strlen(id) == 32; for (int i = 0; ok && i < 32; i++) ok = (id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f');
    if (ok) snprintf(install_id, sizeof install_id, "%s", id);
    json_free(j); free(b);
    if (!ok) {                                            /* a random id, made once per install */
        unsigned char r[16]; dsl_uos_random(r, sizeof r);
        for (int i = 0; i < 16; i++) snprintf(install_id + i * 2, 3, "%02x", r[i]);
        consent_save();
    }
}
static int queue_list(char names[][128], int max) {         /* queue folders, oldest first */
    char d[1200]; cpath(d, sizeof d, "queue"); DIR *dir = opendir(d); struct dirent *e; int k = 0;
    while (dir && (e = readdir(dir))) if (e->d_name[0] != '.' && k < max) snprintf(names[k++], 128, "%s", e->d_name);
    if (dir) closedir(dir);
    for (int i = 1; i < k; i++) for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) { char t[128]; memcpy(t, names[j], 128); memcpy(names[j], names[j - 1], 128); memcpy(names[j - 1], t, 128); }
    return k;
}
static void queue_drop_all(void) {
    char q[32][128]; int k = queue_list(q, 32);
    for (int i = 0; i < k; i++) { char d[1400]; snprintf(d, sizeof d, "%s/queue/%s", cdir, q[i]); rm_tree(d); }
    if (k) dsl_log("crash: %d queued report(s) dropped (crash reports off)", k);
}
int dsl_crash_consent(void) { return on ? consent : -2; }
const char *dsl_crash_privacy_url(void) { return CR_HOST "/privacy"; }
/* for the bug report window (dsl_bugrep.c): the crash folder, this install's random id ("" = crash reports not in this
   start), the receiver and its public key */
const char *dsl_crash_dir(void) { return on ? cdir : ""; }
const char *dsl_crash_install(void) { return on ? install_id : ""; }
const char *dsl_crash_receiver(void) { return CR_HOST; }
const char *dsl_crash_key(void) { return CR_KEY; }
void dsl_crash_set_consent(int yes) {
    if (!on) return;
    pthread_mutex_lock(&mu); consent = yes ? 1 : 0; pthread_mutex_unlock(&mu);
    consent_save();
    dsl_log("crash: crash reports %s", yes ? "ON (the player said yes)" : "OFF (the player said no)");
    if (!yes) queue_drop_all();
}

/* ------------------------------------------------------------------ the queue */
typedef struct { const char *name; char path[1200]; size_t cap; } QFile;
/* a new queue folder with meta.txt and the tails of the files; 0 = queued */
static int queue_add(const char *kind, const char *meta, const QFile *f, int nf) {
    char q[32][128]; if (queue_list(q, 32) >= CR_QUEUE_MAX) { dsl_log("crash: queue full (%d): this %s report is not kept", CR_QUEUE_MAX, kind); return -1; }
    static int seq; char ts[32], d[1400]; stamp(ts, sizeof ts);
    snprintf(d, sizeof d, "%s/queue/%s-%d-%s", cdir, ts, seq++, kind); mkdirs(d);
    char p[1600]; snprintf(p, sizeof p, "%s/meta.txt", d);
    if (write_all(p, meta, strlen(meta))) { dsl_log("crash: cannot write %s", p); return -1; }
    int kept = 0;
    for (int i = 0; i < nf; i++) {
        size_t n = 0; char *b = tail(f[i].path, f[i].cap, &n); if (!b) continue;
        snprintf(p, sizeof p, "%s/%s", d, f[i].name);
        if (!write_all(p, b, n)) kept++;
        free(b);
    }
    dsl_log("crash: %s report queued (%d file(s)): %s", kind, kept, d);
    return 0;
}

/* ------------------------------------------------------------------ the start: the previous session */
typedef struct { long pid; long long start; int title, live, end, b; char rel[32]; } Sess;
static int read_sess(const char *p, Sess *s) {
    char *b = tail(p, 8192, NULL); if (!b) return -1;
    char *cur = b;
    for (char *l; (l = next_line(&cur));) {
        if (!strncmp(l, "START ", 6)) { char r[32] = "", ro[8] = ""; sscanf(l + 6, "%lld %31s %7s", &s->start, r, ro); snprintf(s->rel, sizeof s->rel, "%s", r); s->b = ro[0] == 'B'; }
        else if (!strncmp(l, "TITLE", 5)) s->title = 1;
        else if (!strncmp(l, "LIVE", 4)) s->live = 1;
        else if (!strncmp(l, "END", 3)) s->end = 1;
    }
    free(b); return 0;
}
/* ErrorLog.txt: "Application End" after the last "Application Start" = the game quit normally */
static int errorlog_quit_ok(const char *data) {
    char p[1200]; snprintf(p, sizeof p, "%s/ErrorLog.txt", data);
    char *b = tail(p, 64 << 10, NULL); if (!b) return 0;
    char *ls = NULL, *le = NULL, *x;
    for (x = b; (x = strstr(x, "Application Start")); x++) ls = x;
    for (x = b; (x = strstr(x, "Application End")); x++) le = x;
    int ok = le && (!ls || le > ls); free(b); return ok;
}
static int has_freeze_mark(const char *p) {
    char *b = tail(p, 2u << 20, NULL); if (!b) return 0;
    int f = strstr(b, "WaitForLastPresentation") || strstr(b, "deadlock") || strstr(b, "Deadlock"); free(b); return f;
}
static void check_sessions(void) {
    char d[1200]; cpath(d, sizeof d, "sessions");
    DIR *dir = opendir(d); struct dirent *e; char names[64][64]; int k = 0;
    while (dir && (e = readdir(dir)) && k < 64) if (e->d_name[0] != '.') snprintf(names[k++], sizeof names[0], "%s", e->d_name);
    if (dir) closedir(dir);
    Sess bad = { 0 }; int nbad = 0, nclean = 0;
    for (int i = 0; i < k; i++) {
        char *end; long pid = strtol(names[i], &end, 10);
        if (pid <= 0 || strcmp(end, ".txt")) continue;
        if (pid != (long)getpid() && dsl_uos_pid_alive(pid)) continue;          /* still running (soft restart A, ...) */
        char p[1300]; snprintf(p, sizeof p, "%s/%s", d, names[i]);
        Sess s = { 0 }; s.pid = pid;
        if (read_sess(p, &s)) continue;
        remove(p);
        if (s.b && !s.live) continue;                                            /* a prewarmed B that never took over */
        if (s.end) { nclean++; continue; }
        if (!nbad || s.start > bad.start) bad = s;
        nbad++;
    }
    if (!nbad) { if (nclean) dsl_log("crash: the previous session ended normally"); return; }
    char data[1100]; data_dir(data, sizeof data);
    if (!bad.b && errorlog_quit_ok(data)) { dsl_log("crash: previous session %ld has no END mark, but the game's ErrorLog says Application End: not a crash", bad.pid); return; }
    char plog[1200];
#ifdef _WIN32
    snprintf(plog, sizeof plog, "%s/player.prev.log", logdir_s);   /* the starter moved it there before the game opened it */
#else
    snprintf(plog, sizeof plog, "%s/player.log", logdir_s);        /* the game opens -logFile after this constructor */
#endif
    const char *kind = !bad.title || has_freeze_mark(plog) ? "freeze" : "crash";
    char why[200]; snprintf(why, sizeof why, !bad.title ? "the previous start never reached the title" : "the previous session ended without a normal quit");
    dsl_log("crash: %s: %s (pid %ld, loader %s)", kind, why, bad.pid, bad.rel[0] ? bad.rel : "?");
    if (consent == 0) { dsl_log("crash: crash reports are off: not queued"); return; }
    char meta[1200]; time_t st = (time_t)bad.start; char sts[40]; strftime(sts, sizeof sts, "%Y-%m-%dT%H:%M:%SZ", gmtime(&st));
    snprintf(meta, sizeof meta, "kind: %s\nreason: %s\nsession_start: %s\nsession_loader: %s\nsession_title: %s\nsession_soft_restart_b: %s\n",
             kind, why, sts, bad.rel, bad.title ? "yes" : "no", bad.b ? "yes" : "no");
    QFile f[4] = { { "dsload.log", "", 768u << 10 }, { "player.log", "", 1536u << 10 }, { "rig.log", "", 512u << 10 }, { "ErrorLog.txt", "", 64u << 10 } };
    snprintf(f[0].path, sizeof f[0].path, "%s/dsload.prev.log", logdir_s);
    snprintf(f[1].path, sizeof f[1].path, "%s", plog);
    snprintf(f[2].path, sizeof f[2].path, "%s/rig.log", logdir_s);
    snprintf(f[3].path, sizeof f[3].path, "%s/ErrorLog.txt", data);
    queue_add(kind, meta, f, 4);
}

static void end_mark(const char *how) {
    if (!on || ended) return;
    ended = 1;
    FILE *f = fopen(sess_path, "ab"); if (f) { fprintf(f, "END %s %lld\n", how, (long long)time(NULL)); fclose(f); }
}
static void at_exit_end(void) { end_mark("exit"); }
static void sess_line(const char *what) { FILE *f = fopen(sess_path, "ab"); if (f) { fprintf(f, "%s %lld\n", what, (long long)time(NULL)); fclose(f); } }

/* the start part, right after dsl_soft_snapshot (the log folder and the soft restart role are known; GameAssembly and
   the game's own log are not open yet) */
void dsl_crash_boot(void) {
    const char *e = getenv("DSL_CRASHREP"), *r = getenv("DSML_ROOT"), *ld = dsl_core_logdir();
    if (e && !strcmp(e, "0")) return;
    if (!(e && !strcmp(e, "1")) && !(r && *r)) return;
    if (!ld || !*ld) return;
    snprintf(logdir_s, sizeof logdir_s, "%s", ld);
    snprintf(cdir, sizeof cdir, "%s/crash", r && *r ? r : ld);
    char p[1200]; mkdirs(cdir); cpath(p, sizeof p, "sessions"); mkdirs(p); cpath(p, sizeof p, "queue"); mkdirs(p);
    on = 1; role_b = dsl_soft_wanted();
    consent_load();
    snprintf(sess_path, sizeof sess_path, "%s/sessions/%ld.txt", cdir, (long)getpid());
    if (!role_b) check_sessions();
    FILE *f = fopen(sess_path, "wb");
    if (f) { fprintf(f, "START %lld %s %s\n", (long long)time(NULL), DSL_RELEASE, role_b ? "B" : "A"); fclose(f); }
    atexit(at_exit_end);
    dsl_log("crash: crash reports %s (install %.8s...)%s", consent == 1 ? "on" : consent == 0 ? "off" : "not asked yet", install_id,
            role_b ? "; soft restart B" : "");
}

/* ------------------------------------------------------------------ after il2cpp_init: the quit mark */
static void (*orig_quit)(const void *);
static void my_quit(const void *mi) { end_mark("quit"); orig_quit(mi); }
void dsl_crash_start(void) {
    if (!on) return;
    void *q = dsl_find_method_address("UnityEngine", "Application", "Internal_ApplicationQuit", 0);
    int r = q ? dsl_hook(DSL_OWNER_LOADER, q, (void *)my_quit, (void **)&orig_quit, "loader crash quit mark") : -1;
    dsl_log("crash: quit mark %s", !r ? "on Application.Internal_ApplicationQuit (+ atexit)" : "by atexit only (no hook)");
}

/* ------------------------------------------------------------------ at the title: desync reports and failed joins */
typedef struct { long long rig_off, desync_t; int init; } Scan;
static void scan_load(Scan *s) {
    char p[1200]; cpath(p, sizeof p, "scan.txt"); char *b = tail(p, 1024, NULL);
    memset(s, 0, sizeof *s);
    if (b) { s->init = sscanf(b, "%lld %lld", &s->rig_off, &s->desync_t) == 2; free(b); }
}
static void scan_save(const Scan *s) { char p[1200], b[100]; cpath(p, sizeof p, "scan.txt"); int n = snprintf(b, sizeof b, "%lld %lld\n", s->rig_off, s->desync_t); write_all(p, b, (size_t)n); }
static void scan_mp(void) {
    Scan s; scan_load(&s);
    char rig[1200], data[1100], dd[1200]; snprintf(rig, sizeof rig, "%s/rig.log", logdir_s); data_dir(data, sizeof data);
    snprintf(dd, sizeof dd, "%s/desync_reports", data);
    long long rs = file_size(rig); if (rs < 0) rs = 0;
    /* desync report folders newer than the last scan (their names start with the time; the folder's mtime is used) */
    char dn[3][256]; int nd = 0; long long newest = s.desync_t;
    DIR *dir = opendir(dd); struct dirent *e;
    while (dir && (e = readdir(dir))) {
        if (e->d_name[0] == '.') continue;
        char p[1500]; struct stat st; snprintf(p, sizeof p, "%s/%s", dd, e->d_name);
        if (stat(p, &st)) continue;
        long long t = (long long)st.st_mtime;
        if (t > newest) newest = t;
        if (s.init && t > s.desync_t && nd < 3) snprintf(dn[nd++], sizeof dn[0], "%s", e->d_name);
    }
    if (dir) closedir(dir);
    /* failed joins in the new part of rig.log (it restarts when the log was rotated) */
    char joinl[600] = "", blankl[600] = "";
    if (s.init && s.rig_off > rs) s.rig_off = 0;
    if (s.init && rs > s.rig_off) {
        size_t want = (size_t)(rs - s.rig_off) > (8u << 20) ? (8u << 20) : (size_t)(rs - s.rig_off), n = 0;
        char *b = tail(rig, want, &n);
        char *cur = b;
        for (char *l; (!joinl[0] || !blankl[0]) && (l = next_line(&cur));) {
            /* multiplayer (lane watchfog): no map tile visible for 5 s in a session */
            if (!blankl[0] && strstr(l, "ERROR [view_blank] view: nothing visible for")) { snprintf(blankl, sizeof blankl, "%s", l); continue; }
            if (joinl[0] || !strstr(l, "join")) continue;
            for (const char *c = l; (c = strstr(c, "rc -")); c += 4)
                if ((c[4] == '2' || c[4] == '3' || c[4] == '4') && !(c[5] >= '0' && c[5] <= '9')) { snprintf(joinl, sizeof joinl, "%s", l); break; }
        }
        free(b);
    }
    int first = !s.init; s.init = 1; s.rig_off = rs; s.desync_t = newest; scan_save(&s);
    if (first) { dsl_log("crash: multiplayer scan starts here (rig.log %lld bytes)", rs); return; }
    if (consent == 0 || (!nd && !joinl[0] && !blankl[0])) return;
    char cur[1200], plog[1200]; snprintf(cur, sizeof cur, "%s/dsload.log", logdir_s); snprintf(plog, sizeof plog, "%s/player.log", logdir_s);
    if (nd) {
        char meta[1200]; size_t m = (size_t)snprintf(meta, sizeof meta, "kind: desync\nreason: %d new desync report(s)\n", nd);
        QFile f[6]; int nf = 0;
        for (int i = 0; i < nd; i++) { f[nf] = (QFile){ "desync_summary_1.txt", "", 16u << 10 }; snprintf(f[nf].path, sizeof f[nf].path, "%s/%s/README.txt", dd, dn[i]);
                                       static char nm[3][32]; snprintf(nm[i], sizeof nm[i], "desync_summary_%d.txt", i + 1); f[nf].name = nm[i]; nf++;
                                       m += (size_t)snprintf(meta + m, m < sizeof meta ? sizeof meta - m : 0, "desync_report: %s\n", dn[i]); }
        f[nf] = (QFile){ "rig.log", "", 512u << 10 }; snprintf(f[nf].path, sizeof f[nf].path, "%s", rig); nf++;
        f[nf] = (QFile){ "dsload.log", "", 512u << 10 }; snprintf(f[nf].path, sizeof f[nf].path, "%s", cur); nf++;
        queue_add("desync", meta, f, nf);
    }
    if (joinl[0]) {
        char meta[900]; snprintf(meta, sizeof meta, "kind: join\nreason: a join failed\njoin_line: %s\n", joinl);
        QFile f[3] = { { "rig.log", "", 512u << 10 }, { "dsload.log", "", 512u << 10 }, { "player.log", "", 1024u << 10 } };
        snprintf(f[0].path, sizeof f[0].path, "%s", rig); snprintf(f[1].path, sizeof f[1].path, "%s", cur); snprintf(f[2].path, sizeof f[2].path, "%s", plog);
        queue_add("join", meta, f, 3);
    }
    if (blankl[0]) {
        char meta[900]; snprintf(meta, sizeof meta, "kind: blank\nreason: the game view showed nothing\nblank_line: %s\n", blankl);
        QFile f[3] = { { "rig.log", "", 512u << 10 }, { "dsload.log", "", 512u << 10 }, { "player.log", "", 1024u << 10 } };
        snprintf(f[0].path, sizeof f[0].path, "%s", rig); snprintf(f[1].path, sizeof f[1].path, "%s", cur); snprintf(f[2].path, sizeof f[2].path, "%s", plog);
        queue_add("blank", meta, f, 3);
    }
}

/* ------------------------------------------------------------------ build and send (thread) */
typedef struct { char *p; size_t n, cap; } Tb;
static void tb_put(Tb *t, const char *s, size_t n) {
    if (t->n + n + 1 > t->cap) { size_t c = t->cap ? t->cap : 65536; while (c < t->n + n + 1) c *= 2; char *q = realloc(t->p, c); if (!q) return; t->p = q; t->cap = c; }
    memcpy(t->p + t->n, s, n); t->n += n; t->p[t->n] = 0;
}
static void tb_f(Tb *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void tb_f(Tb *t, const char *fmt, ...) { char b[4096]; va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap); if (n > 0) tb_put(t, b, (size_t)n < sizeof b ? (size_t)n : sizeof b - 1); }
/* one report as gzip (kind out); 0 = ok. div: each file is cut to its last 1/div */
static int build(const char *qdir, const DslScrub *sc, int div, char *kind, size_t kn, unsigned char **gz, size_t *gn) {
    char p[1600]; snprintf(p, sizeof p, "%s/meta.txt", qdir);
    char *meta = tail(p, 64 << 10, NULL); if (!meta) return -1;
    const char *k = strstr(meta, "kind: "); snprintf(kind, kn, "%.*s", k ? (int)strcspn(k + 6, "\r\n") : 5, k ? k + 6 : "crash");
    char os[160] = "", cpu[160] = "", plat[80] = ""; dsl_uos_sysinfo(os, sizeof os, cpu, sizeof cpu, plat, sizeof plat);
    time_t now = time(NULL); char ts[40]; strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    Tb t = { 0 };
    tb_f(&t, "DSCRASH 1\ninstall: %s\nloader: %s\ngame_build: %s\nplatform: %s\nos: %s\ncpu: %s\nsent: %s\n", install_id, DSL_RELEASE,
         dsl_build_id ? dsl_build_id : "unknown", plat, os, cpu, ts);
    tb_f(&t, "mods:");
    for (int i = 0; i < dsl_mods_count(); i++) { const DSLModInfo *m = dsl_mods_at(i); if (m && !m->hidden) tb_f(&t, " %s %s (%s)%s", m->id, m->version ? m->version : "?", m->enabled ? "on" : "off", i + 1 < dsl_mods_count() ? "," : ""); }
    tb_f(&t, "\n%s\n", meta); free(meta);
    DIR *dir = opendir(qdir); struct dirent *e; char names[24][256]; int nn = 0;
    while (dir && (e = readdir(dir)) && nn < 24) if (e->d_name[0] != '.' && strcmp(e->d_name, "meta.txt") && strcmp(e->d_name, "tries")) snprintf(names[nn++], sizeof names[0], "%s", e->d_name);
    if (dir) closedir(dir);
    for (int i = 1; i < nn; i++) for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) { char x[256]; memcpy(x, names[j], 256); memcpy(names[j], names[j - 1], 256); memcpy(names[j - 1], x, 256); }
    for (int i = 0; i < nn; i++) {
        snprintf(p, sizeof p, "%s/%s", qdir, names[i]);
        long long sz = file_size(p); size_t cap = sz > 0 ? (size_t)sz / (size_t)div : 0, n = 0;
        char *b = cap ? tail(p, cap, &n) : NULL;
        tb_f(&t, "===== %s (%zu of %lld bytes, the end of the file) =====\n", names[i], n, sz);
        if (b) { tb_put(&t, b, n); free(b); }
        tb_put(&t, "\n", 1);
    }
    if (!t.p) return -1;
    size_t sn = 0; char *clean = dsl_scrub(sc, t.p, t.n, &sn); free(t.p);
    if (!clean) return -1;
    int r = dsl_gzip(clean, sn, gz, gn); free(clean);
    return r;
}
static int sent_last_day(void) {
    char p[1200]; cpath(p, sizeof p, "sent.txt"); char *b = tail(p, 16 << 10, NULL); if (!b) return 0;
    long long now = (long long)time(NULL); int n = 0;
    char *cur = b;
    for (char *l; (l = next_line(&cur));) { long long t = atoll(l); if (t > now - 86400 && t <= now + 60) n++; }
    free(b); return n;
}
static void set_result(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_result(const char *fmt, ...) { va_list ap; va_start(ap, fmt); pthread_mutex_lock(&mu); vsnprintf(last_result, sizeof last_result, fmt, ap); pthread_mutex_unlock(&mu); va_end(ap); }
static void *send_thread(void *arg) {
    (void)arg;
    DslScrub *sc = malloc(sizeof *sc);
    if (sc) dsl_scrub_collect(sc, logdir_s);
    char q[32][128]; int k = sc ? queue_list(q, 32) : 0;
    for (int i = 0; i < k; i++) {
        pthread_mutex_lock(&mu); int c = consent; pthread_mutex_unlock(&mu);
        if (c != 1) break;
        if (sent_last_day() >= CR_PER_DAY) { dsl_log("crash: %d report(s) sent in the last 24 h: the rest waits", CR_PER_DAY); set_result("daily limit"); break; }
        char d[1400]; snprintf(d, sizeof d, "%s/queue/%s", cdir, q[i]);
        unsigned char *gz = NULL; size_t gn = 0; char kind[32] = "crash"; int r = -1;
        for (int div = 1; div <= 8; div *= 2) {
            free(gz); gz = NULL;
            if ((r = build(d, sc, div, kind, sizeof kind, &gz, &gn))) break;
            if (gn <= CR_GZ_MAX) break;
            r = -1;
        }
        if (r) { dsl_log("crash: %s could not be made into a report: dropped", q[i]); rm_tree(d); free(gz); continue; }
        char out[1200]; cpath(out, sizeof out, "upload.gz");
        if (write_all(out, (const char *)gz, gn)) { free(gz); break; }
        free(gz);
        char h1[100], h2[100], h3[100]; snprintf(h1, sizeof h1, "X-DSCR-Key: %s", CR_KEY); snprintf(h2, sizeof h2, "X-DSCR-Install: %s", install_id);
        snprintf(h3, sizeof h3, "X-DSCR-Kind: %s", kind);
        const char *hs[] = { "Content-Type: application/gzip", h1, h2, h3 };
        if (dsl_uos_post(CR_HOST "/api/report", out, hs, 4)) {
            char tp[1500]; snprintf(tp, sizeof tp, "%s/tries", d); char *tb = tail(tp, 64, NULL); int tries = tb ? atoi(tb) + 1 : 1; free(tb);
            char tv[16]; snprintf(tv, sizeof tv, "%d", tries); write_all(tp, tv, strlen(tv));
            dsl_log("crash: upload of %s failed (try %d of 4); the next start tries again", q[i], tries);
            set_result("upload failed (try %d)", tries);
            if (tries >= 4) rm_tree(d);
            break;
        }
        char sp[1200], line[40]; cpath(sp, sizeof sp, "sent.txt"); snprintf(line, sizeof line, "%lld", (long long)time(NULL)); append_line(sp, line);
        dsl_log("crash: %s report sent (%zu bytes gzip, %d names scrubbed)", kind, gn, sc->n);
        set_result("sent %s %zu", kind, gn);
        rm_tree(d); remove(out);
    }
    free(sc);
    pthread_mutex_lock(&mu); sending = 0; pthread_mutex_unlock(&mu);
    return NULL;
}

/* ------------------------------------------------------------------ the consent notice (main thread) */
static void yes_cb(void *ud, DSLObj w) { (void)ud; (void)w; dsl_crash_set_consent(1); }
static void no_cb(void *ud, DSLObj w) { (void)ud; (void)w; dsl_crash_set_consent(0); }
static void ask(void) {
    const DSLUi *u = dsl_ui_kit(); if (!u || !u->notice) return;
    asked = 1;
    char priv[300], t[1200]; snprintf(priv, sizeof priv, TR("Privacy note: %s"), CR_HOST_SHORT "/privacy");
    snprintf(t, sizeof t, "%s\n%s\n%s", TR("Send crash reports to help fix bugs?"),
             TR("Only after a crash, a freeze, a desync or a failed join: the logs and versions, without names or IP addresses."), priv);
    dsi_utf8_fix_end(t);
    u->notice(t, TR("Yes"), TR("No"), yes_cb, no_cb, NULL);
    dsl_ui_notice_no_esc();                                           /* a stray Escape must not record a lasting No */
    dsl_log("crash: consent notice shown");
}

/* ------------------------------------------------------------------ test driver (DSL_CRASHREP_TEST) */
static void test_frame(void) {
    static const char *dir; static int checked; static double last;
    if (!checked) { checked = 1; dir = getenv("DSL_CRASHREP_TEST"); if (dir && !*dir) dir = NULL; }
    if (!dir) return;
    double t = mono(); if (t - last < 0.5) return; last = t;
    int notice = dsl_ui_notice_shown();
    char p[1300], b[64] = ""; snprintf(p, sizeof p, "%s/answer", dir);
    FILE *f = fopen(p, "rb");
    if (f) { if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f);
             if (notice && asked && consent < 0) { remove(p); const DSLUi *u = dsl_ui_kit(); if (u && u->notice_close) u->notice_close();
                                                     dsl_log("crash test: answer %s", b); if (!strncmp(b, "yes", 3)) yes_cb(NULL, NULL); else no_cb(NULL, NULL); } }
    DIR *d = opendir(dir); struct dirent *e; char shot[128] = "";
    while (d && (e = readdir(d))) if (!strncmp(e->d_name, "shot_", 5) && !strstr(e->d_name, ".")) { snprintf(shot, sizeof shot, "%s", e->d_name + 5); break; }
    if (d) closedir(d);
    if (shot[0]) { char sp[1400], q[1400]; snprintf(q, sizeof q, "%s/shot_%s", dir, shot); remove(q); snprintf(sp, sizeof sp, "%s/%s.png", dir, shot);
                   dsl_log("crash test: shot %s: %s", sp, dsl_soft_shot(sp) ? "FAILED" : "ok"); }
    snprintf(p, sizeof p, "%s/quit", dir);
    if (!access(p, 0)) { remove(p); void (*qf)(const void *) = dsl_find_method_address("UnityEngine", "Application", "Quit", 0);
                         dsl_log("crash test: Application.Quit %s", qf ? "called" : "NOT found"); if (qf) qf(NULL); }
    char q[32][128]; int nq = queue_list(q, 32);
    pthread_mutex_lock(&mu);
    char js[600]; int n = snprintf(js, sizeof js, "{\"consent\":%d,\"asked\":%d,\"notice\":%d,\"queue\":%d,\"sent24h\":%d,\"sending\":%d,\"title\":%d,\"last\":\"%s\"}\n",
                                   consent, asked, notice, nq, sent_last_day(), sending, title_marked, last_result);
    pthread_mutex_unlock(&mu);
    snprintf(p, sizeof p, "%s/state.json", dir); write_all(p, js, (size_t)n);
}

/* every frame (dsl_reg.c frame hook, main thread) */
void dsl_crash_frame(void) {
    if (!on) return;
    if (role_b && !live_marked && dsl_soft_b_live()) { live_marked = 1; sess_line("LIVE"); }
    static double idle_since; static int scanned;
    int idle = dsl_reg_scene() == 1 && dsl_soft_title_idle() && dsl_ui_title_shown();
    test_frame();
    if (dsl_reg_scene() != 1) scanned = 0;
    if (!idle) { idle_since = 0; return; }
    if (!title_marked) { title_marked = 1; sess_line("TITLE"); }
    if (role_b && !live_marked) return;
    double t = mono(); if (!idle_since) idle_since = t;
    if (t - idle_since < 1.5) return;
    static double next_try;
    if (!scanned) { scanned = 1; scan_mp(); next_try = 0; }
    int win = dsl_win_is_open && dsl_win_is_open();
    if (consent < 0 && !asked && !win && !dsl_ui_notice_shown() && !dsl_os_batchmode()) ask();   /* the notice box: asked only here, not every frame (FPS audit 2026-10-26) */
    pthread_mutex_lock(&mu); int go = consent == 1 && !sending && t >= next_try; pthread_mutex_unlock(&mu);
    if (go) {
        char q[8][128]; next_try = t + 60;
        if (queue_list(q, 8) && sent_last_day() < CR_PER_DAY) {
            pthread_t th; pthread_mutex_lock(&mu); sending = 1; pthread_mutex_unlock(&mu);
            if (pthread_create(&th, NULL, send_thread, NULL)) { pthread_mutex_lock(&mu); sending = 0; pthread_mutex_unlock(&mu); }
            else pthread_detach(th);
        } else next_try = t + 1e9;                              /* nothing queued: the next title arrival queues and sends */
    }
}
