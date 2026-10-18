// dsl_hostmods.c - "Get the host's mods" (v0.1.14). The multiplayer mod's join was refused with DSNET_REJ_MODSET: it asks
// for a plan (dsl_hostmods_plan: the host's gameplay mods next to the guest's installed ones), shows it, and on the
// button calls dsl_hostmods_get. Then, on a thread: every missing / older / newer / other-files mod is downloaded ONLY
// from our official releases (dsl_update.c: tag "<id>-v<ver>", asset "<id>-<ver>.zip", sha256 required; DSL_UPDATE_API
// for tests), never from the host and never for an id that is not in MOD_REPOS; installed with dsl_uos_mod_install
// (moves recorded, old folder into a backup). On the main thread (dsl_hostmods_frame): the mods folder read again, the
// host's gameplay settings taken (one adapter, settings_*), and the normal Apply (pending.json + soft restart) with the
// host's gameplay ids + the guest's own client mods and multiplayer, env_lines passed to the new process.
// Any failure before the new process runs: every move rolled back, settings put back, nothing applied.
// Host lines: <id>@<ver>[:<file sha256s>[:<patch hash>]] per gameplay mod (the multiplayer mod is never in them) and
// <id>/<setting>@<value> per gameplay setting of a mod (lane modsettings, src/dsl_settings.c).
// Test only: DSL_HOSTMODS_TEST=1 = no title check, and the Apply is a log line ("hostmods: test apply: ...").
#include "dsl_internal.h"
#include "dsl_menu.h"
#include "dsl_update_os.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#define HM_EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
#define HM_EXPORT __attribute__((visibility("default")))
#endif

int dsl_reg_scene(void);                                   /* dsl_reg.c: 1 title */
int dsl_mod_hashes(const char *id, char *out, size_t cap); /* dsl_main.c: sorted listed sha256s of a mod folder */

#define HM_MAX 64
typedef struct { char id[64], ver[32], files[2048], set[2048]; } HostMod;
typedef struct {
    char id[64], name[128], host_ver[32], my_ver[32], host_set[2048], my_set[2048];
    int my_on, official; const char *action;
} Row;
typedef struct { char id[64], name[128], ver[32], files[2048], url[1024], asset[160], sha[65], file[1100]; int host_older; } Job;

/* ------------------------------------------------------------------ the gameplay-settings adapter (lane modsettings,
   src/dsl_settings.c): "<mod>/<setting>@<value>" lines. dsl_settings_lines = this game's running values of one mod,
   dsl_settings_take_lines = the host's values become the drafts the next Apply writes into pending.json,
   dsl_settings_undo = drafts dropped (all three in this loader, src/dsl_settings.c). A format change is an edit here only. */
static int settings_can_copy(void) { return 1; }
static int cmp_line(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static void sort_lines(char *s) {                         /* '\n'-joined lines sorted in place */
    char tmp[2048]; snprintf(tmp, sizeof tmp, "%s", s); const char *v[128]; int n = 0;
    for (char *sv = NULL, *t = strtok_r(tmp, "\n", &sv); t && n < 128; t = strtok_r(NULL, "\n", &sv)) v[n++] = t;
    qsort(v, (size_t)n, sizeof *v, cmp_line);
    size_t k = 0; s[0] = 0;
    for (int i = 0; i < n; i++) k += (size_t)snprintf(s + k, k < 2048 ? 2048 - k : 0, "%s%s", i ? "\n" : "", v[i]);
}
static void settings_mine(const char *id, char *out, size_t cap) {      /* cap = 2048 */
    out[0] = 0; if (dsl_settings_lines(id, out, cap) < 0) out[0] = 0;
    sort_lines(out);
}
static int settings_take(const char *host_text) {
    return dsl_settings_take_lines(host_text);
}
static void settings_drop(void) { dsl_settings_undo(); }
/* "<mod>/<setting>@<value>": 1 (mod id into mod) */
static int setting_line(const char *line, char *mod, size_t cap) {
    const char *sl = strchr(line, '/'), *at = strchr(line, '@');
    if (!sl || !at || sl > at || strchr(line, ':')) return 0;
    snprintf(mod, cap, "%.*s", (int)(sl - line), line); return 1;
}

/* ------------------------------------------------------------------ parsing + the plan (main thread) */
static int safe_id(const char *s) {
    if (!*s) return 0;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '_' || *s == '.')) return 0;
    return 1;
}
static int safe_ver(const char *s) {                       /* goes into a file name and a tag: digits, letters, . + - only */
    if (*s < '0' || *s > '9' || strstr(s, "..")) return 0;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '+' || *s == '.')) return 0;
    return 1;
}
/* -1 = bad input */
static int parse_host(const char *lines, int vanilla, HostMod *h, int max) {
    int n = 0; if (vanilla || !lines) return 0;
    for (const char *s = lines; *s;) {
        const char *e = strchr(s, '\n'); size_t l = e ? (size_t)(e - s) : strlen(s);
        char line[4096]; if (l >= sizeof line) return -1;
        memcpy(line, s, l); line[l] = 0; s = e ? e + 1 : s + l;
        while (l && (line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
        if (!*line) continue;
        if (n >= max) return -1;
        char smod[64];
        if (setting_line(line, smod, sizeof smod)) continue;          /* settings: second pass */
        HostMod *m = &h[n]; memset(m, 0, sizeof *m);
        char *at = strchr(line, '@'); if (!at) return -1;
        *at = 0; char *co = strchr(at + 1, ':');
        if (co) { *co = 0; char *c2 = strchr(co + 1, ':'); if (c2) *c2 = 0; snprintf(m->files, sizeof m->files, "%s", co + 1); }   /* patch hash: launcher only */
        if (strlen(line) >= sizeof m->id || strlen(at + 1) >= sizeof m->ver || !safe_id(line) || !*(at + 1)) return -1;
        snprintf(m->id, sizeof m->id, "%s", line); snprintf(m->ver, sizeof m->ver, "%s", at + 1);
        if (!strcmp(m->id, "multiplayer")) continue;        /* never in the set (its HELLO checks build + protocol) */
        int dup = 0; for (int i = 0; i < n; i++) dup |= !strcmp(h[i].id, m->id);
        if (!dup) n++;
    }
    for (const char *s = lines; *s;) {                               /* each mod's setting lines, sorted */
        const char *e = strchr(s, '\n'); size_t l = e ? (size_t)(e - s) : strlen(s);
        char line[300]; if (l >= sizeof line) l = sizeof line - 1;
        memcpy(line, s, l); line[l] = 0; s = e ? e + 1 : s + l;
        while (l && (line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
        char smod[64]; if (!setting_line(line, smod, sizeof smod)) continue;
        for (int i = 0; i < n; i++) if (!strcmp(h[i].id, smod)) {
            size_t k = strlen(h[i].set); snprintf(h[i].set + k, sizeof h[i].set - k, "%s%s", k ? "\n" : "", line);
        }
    }
    for (int i = 0; i < n; i++) sort_lines(h[i].set);
    return n;
}
static const DSLModInfo *mod_find(const char *id) {
    for (int i = 0; i < dsl_mods_count(); i++) { const DSLModInfo *m = dsl_mods_at(i); if (m && !strcmp(m->id, id)) return m; }
    return NULL;
}
/* ------------------------------------------------------------------ names of host mods this copy does not have: the
   mod.json of the official release (the zip the button downloads anyway: same URL, sha256 checked, kept in
   ROOT/update/hostmods/dl), cached as ROOT/update/hostmods/names/<id>-<ver>.json, shown in the player's language
   (i18n.<code>.name, else name). A plan that needs one starts a fetch thread and says "names_pending": 1; the multiplayer
   mod asks again a moment later. A failed fetch (offline, no such release) = the id, as before. */
static pthread_mutex_t net_mu = PTHREAD_MUTEX_INITIALIZER;       /* one release lookup at a time (name fetch, the fix) */
static pthread_mutex_t nq_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { char id[64], ver[32]; int st; } NQ[HM_MAX]; static int nnq, nq_thread;   /* st 0 queued, 1 done, -1 failed */
static void name_file(char *out, size_t n, const char *id, const char *ver) { snprintf(out, n, "%s/hostmods/names/%s-%s.json", dsl_update_dir(), id, ver); }
static int name_from_modjson(const char *path, const char *id, const char *ver, char *out, size_t cap) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    char *b = calloc(1, 1 << 20); if (b && !fread(b, 1, (1 << 20) - 1, f)) b[0] = 0; fclose(f);
    JNode *m = b ? json_parse(b) : NULL; free(b);
    const char *mid = json_str(m, "id"), *mv = json_str(m, "version"), *v = NULL;
    if (mid && mv && !strcmp(mid, id) && !strcmp(mv, ver)) {
        v = json_str(json_get(json_get(m, "i18n"), dsi_code()), "name");
        if (!v || !*v) v = json_str(m, "name");
    }
    int ok = v && *v; if (ok) { snprintf(out, cap, "%s", v); dsi_utf8_fix_end(out); }
    json_free(m); return ok ? 0 : -1;
}
static void *name_thread(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&nq_mu);
        int i = 0; while (i < nnq && NQ[i].st) i++;
        if (i == nnq) { nq_thread = 0; pthread_mutex_unlock(&nq_mu); return NULL; }
        char id[64], ver[32]; snprintf(id, sizeof id, "%s", NQ[i].id); snprintf(ver, sizeof ver, "%s", NQ[i].ver);
        pthread_mutex_unlock(&nq_mu);
        char url[1024], asset[160], sha[65], file[1100], dir[1300], from[1500], to[1300], nm[128]; int ok = 0;
        pthread_mutex_lock(&net_mu);
        if (!dsl_update_find_mod_release(id, ver, url, sizeof url, asset, sizeof asset, sha)
            && !dsl_update_fetch(url, asset, sha, "hostmods/dl", file, sizeof file)) {
            snprintf(dir, sizeof dir, "%s/hostmods/names/unpack-%s-%s", dsl_update_dir(), id, ver);
            snprintf(from, sizeof from, "%s/%s/mod.json", dir, id); name_file(to, sizeof to, id, ver);
            if (!dsl_uos_unpack(file, dir) && !name_from_modjson(from, id, ver, nm, sizeof nm)) { remove(to); ok = !dsl_uos_move(from, to); }
        }
        pthread_mutex_unlock(&net_mu);
        dsl_log("hostmods: name of %s %s from its release: %s", id, ver, ok ? "found" : "not found (the id is shown)");
        pthread_mutex_lock(&nq_mu); NQ[i].st = ok ? 1 : -1; pthread_mutex_unlock(&nq_mu);
    }
}
/* the name of a mod this copy does not have into out; fetch = start the lookup if not cached (the box's plan only);
   1 = still being fetched (out = the id meanwhile) */
static int release_name(const char *id, const char *ver, char *out, size_t cap, int fetch) {
    char p[1300]; snprintf(out, cap, "%s", id);
    const char *ud = dsl_update_dir();
    if (!ud || !*ud || !dsl_update_mod_repo(id)) return 0;
    name_file(p, sizeof p, id, ver);
    if (!name_from_modjson(p, id, ver, out, cap)) return 0;
    if (!fetch || !dsl_update_ready()) return 0;                    /* the button's own plan, or no downloads in this start */
    pthread_mutex_lock(&nq_mu);
    int k = 0; while (k < nnq && (strcmp(NQ[k].id, id) || strcmp(NQ[k].ver, ver))) k++;
    int pending = 0;
    if (k == nnq && nnq < HM_MAX) { snprintf(NQ[k].id, sizeof NQ[k].id, "%s", id); snprintf(NQ[k].ver, sizeof NQ[k].ver, "%s", ver); NQ[k].st = 0; nnq++; }
    if (k < nnq && NQ[k].st == 0) {
        pending = 1;
        if (!nq_thread) { pthread_t t; nq_thread = 1; if (pthread_create(&t, NULL, name_thread, NULL)) { nq_thread = 0; NQ[k].st = -1; pending = 0; } else pthread_detach(t); }
    }
    pthread_mutex_unlock(&nq_mu);
    return pending;
}
static int is_gameplay(const DSLModInfo *m) { return m && m->scope && !strcmp(m->scope, "gameplay") && strcmp(m->id, "multiplayer"); }

static HostMod H[HM_MAX]; static Row R[HM_MAX * 2]; static int nh, nr, names_pending;
/* the rows; returns the number that differ, -1 bad input */
static int make_plan(const char *host_lines, int vanilla, int fetch_names) {
    nh = parse_host(host_lines, vanilla, H, HM_MAX); nr = 0; names_pending = 0;
    if (nh < 0) { nh = 0; return -1; }
    int diff = 0;
    for (int i = 0; i < nh; i++) {
        Row *r = &R[nr++]; memset(r, 0, sizeof *r); const HostMod *h = &H[i];
        const DSLModInfo *m = mod_find(h->id);
        snprintf(r->id, sizeof r->id, "%s", h->id); snprintf(r->host_ver, sizeof r->host_ver, "%s", h->ver);
        if (m && m->name) snprintf(r->name, sizeof r->name, "%s", m->name);
        else names_pending |= release_name(h->id, h->ver, r->name, sizeof r->name, fetch_names);
        snprintf(r->host_set, sizeof r->host_set, "%s", h->set);
        r->official = dsl_update_mod_repo(h->id) != NULL && safe_ver(h->ver);
        if (m) { snprintf(r->my_ver, sizeof r->my_ver, "%s", m->version ? m->version : ""); r->my_on = m->active == 1; settings_mine(h->id, r->my_set, sizeof r->my_set); }
        int c = m ? dsl_update_vcmp(r->my_ver, h->ver) : 0;
        if (!m) r->action = "download";
        else if (c < 0) r->action = "update";
        else if (c > 0) r->action = "host_older";
        else if (strcmp(r->my_ver, h->ver)) r->action = "reinstall";            /* "1.0" vs "1.0.0": the host's exact one */
        else {
            char mine[2048]; dsl_mod_hashes(h->id, mine, sizeof mine);
            if (h->files[0] && mine[0] && strcasecmp(h->files, mine)) r->action = "reinstall";
            else if (!r->my_on) r->action = "turn_on";
            else if (strcmp(r->host_set, r->my_set)) r->action = "settings";
            else r->action = "same";
        }
        int needs_dl = !strcmp(r->action, "download") || !strcmp(r->action, "update") || !strcmp(r->action, "host_older") || !strcmp(r->action, "reinstall");
        if (needs_dl && !r->official) r->action = "not_official";
        diff += strcmp(r->action, "same") != 0;
    }
    for (int i = 0; i < dsl_mods_count() && nr < HM_MAX * 2; i++) {      /* my running gameplay mods the host does not use */
        const DSLModInfo *m = dsl_mods_at(i); if (!m || m->active != 1 || !is_gameplay(m)) continue;
        int in = 0; for (int k = 0; k < nh; k++) in |= !strcmp(H[k].id, m->id);
        if (in) continue;
        Row *r = &R[nr++]; memset(r, 0, sizeof *r);
        snprintf(r->id, sizeof r->id, "%s", m->id); snprintf(r->name, sizeof r->name, "%s", m->name ? m->name : m->id);
        snprintf(r->my_ver, sizeof r->my_ver, "%s", m->version ? m->version : ""); r->my_on = 1;
        r->official = dsl_update_mod_repo(m->id) != NULL; settings_mine(m->id, r->my_set, sizeof r->my_set);
        r->action = "turn_off"; diff++;
    }
    return diff;
}
static int plan_can_fix(int diff) {
    if (diff <= 0) return 0;
    for (int i = 0; i < nr; i++) {
        if (!strcmp(R[i].action, "not_official")) return 0;
        if (strcmp(R[i].action, "settings")) continue;                 /* same version, other gameplay settings: */
        if (!settings_can_copy() || !R[i].host_set[0]) return 0;       /* nothing here can make them equal */
    }
    return 1;
}
static int is_download(const Row *r) {
    return !strcmp(r->action, "download") || !strcmp(r->action, "update") || !strcmp(r->action, "host_older") || !strcmp(r->action, "reinstall");
}
static void jesc(char *out, size_t n, const char *s) {
    size_t k = 0;
    for (; s && *s && k + 7 < n; s++) {
        if (*s == '"' || *s == '\\') { out[k++] = '\\'; out[k++] = *s; }
        else if ((unsigned char)*s < 0x20) k += (size_t)snprintf(out + k, n - k, "\\u%04x", *s);
        else out[k++] = *s;
    }
    out[k] = 0;
}

HM_EXPORT int dsl_hostmods_plan(const char *host_lines, int vanilla, char *out, size_t cap) {
    if (!out || cap < 64) return -1;
    out[0] = 0;
    int diff = make_plan(host_lines, vanilla, 1);
    if (diff < 0) { snprintf(out, cap, "{\"rows\":[],\"can_fix\":0,\"downloads\":0}"); dsl_log("hostmods: plan: bad host lines"); return -1; }
    size_t k = 0; int dls = 0, over = 0;
#define PUT(...) do { int w_ = snprintf(out + k, cap - k, __VA_ARGS__); if (w_ < 0 || (size_t)w_ >= cap - k) { over = 1; break; } k += (size_t)w_; } while (0)
    PUT("{\"rows\":[");
    for (int i = 0; i < nr && !over; i++) {
        const Row *r = &R[i]; char e[6][1300];
        jesc(e[0], sizeof e[0], r->id); jesc(e[1], sizeof e[1], r->name); jesc(e[2], sizeof e[2], r->host_ver);
        jesc(e[3], sizeof e[3], r->my_ver); jesc(e[4], sizeof e[4], r->host_set); jesc(e[5], sizeof e[5], r->my_set);
        PUT("%s{\"id\":\"%s\",\"name\":\"%s\",\"host_ver\":\"%s\",\"my_ver\":\"%s\",\"my_on\":%d,\"official\":%d,\"action\":\"%s\",\"host_set\":\"%s\",\"my_set\":\"%s\"}",
            i ? "," : "", e[0], e[1], e[2], e[3], r->my_on, r->official, r->action, e[4], e[5]);
        dls += is_download(r);
    }
    PUT("],\"can_fix\":%d,\"downloads\":%d,\"names_pending\":%d}", plan_can_fix(diff), dls, names_pending);
#undef PUT
    if (over) { snprintf(out, cap, "{\"rows\":[],\"can_fix\":0,\"downloads\":0}"); dsl_log("hostmods: plan: output buffer too small (%zu)", cap); return -1; }
    dsl_log("hostmods: plan: %d row(s), %d differ, %d download(s), can_fix %d", nr, diff, dls, plan_can_fix(diff));
    return diff;
}

/* ------------------------------------------------------------------ the fix */
enum { E_NONE, E_NOREL, E_HOSTOLD, E_DL, E_INSTALL, E_CHECK, E_FILES, E_APPLY, E_RESTART };
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int st, pct, installed_ok, err; static char err_a[300], err_b[400];
static Job J[HM_MAX]; static int nj;
static char want_ids[HM_MAX][64]; static int nwant;                    /* the host's gameplay ids */
static int nsets, sets_taken; static char host_text[65536];       /* settings rows; the host's lines as given */
static char env_s[2048];
static DslMoves *MV; static FILE *mv_log; static char run_dir[1300];

static void fail(int code, const char *a, const char *b) {
    pthread_mutex_lock(&mu);
    err = code; snprintf(err_a, sizeof err_a, "%s", a ? a : ""); snprintf(err_b, sizeof err_b, "%s", b ? b : ""); st = -1;
    pthread_mutex_unlock(&mu);
}
static void set_state(int s, int p) { pthread_mutex_lock(&mu); st = s; if (p >= 0) pct = p; pthread_mutex_unlock(&mu); }
static void stamp(char *out, size_t n) { time_t t = time(NULL); strftime(out, n, "%Y%m%d-%H%M%S", localtime(&t)); }

/* every recorded move backwards (the newly installed folder into run_dir/retired/<i>, the backup back); never deletes */
static int rollback(void) {
    if (!MV) return 0;
    int bad = 0;
    for (int i = MV->n - 1; i >= 0; i--) {
        const char *p = MV->path[i], *b = MV->backup[i];
        char away[1500]; snprintf(away, sizeof away, "%s/retired/%d", run_dir, i);
        if (b[0]) {
            if (!dsl_uos_exists(b)) continue;
            if (dsl_uos_exists(p) && dsl_uos_move(p, away)) { bad++; continue; }
            if (dsl_uos_move(b, p)) bad++;
        } else if (dsl_uos_exists(p) && dsl_uos_move(p, away)) bad++;
    }
    dsl_log("hostmods: rolled back %d recorded change(s), %d failed", MV->n, bad);
    if (mv_log) { fprintf(mv_log, "ROLLEDBACK\t%d\n", bad); fflush(mv_log); }
    MV->n = 0;
    return bad;
}
static void close_log(void) { if (mv_log) { fclose(mv_log); mv_log = NULL; } if (MV) MV->log = NULL; }

static void *fix_thread(void *arg) {
    (void)arg;
    /* 1: every release found (exact version) and downloaded, sha256 checked; nothing installed before all are here */
    for (int i = 0; i < nj; i++) {
        Job *j = &J[i];
        set_state(1, 100 * i / (nj ? nj : 1));
        pthread_mutex_lock(&net_mu);
        int r = dsl_update_find_mod_release(j->id, j->ver, j->url, sizeof j->url, j->asset, sizeof j->asset, j->sha);
        int d = r ? -1 : dsl_update_fetch(j->url, j->asset, j->sha, "hostmods/dl", j->file, sizeof j->file);
        pthread_mutex_unlock(&net_mu);
        if (r == 1) { fail(j->host_older ? E_HOSTOLD : E_NOREL, j->name, j->ver); return NULL; }
        if (r < 0 || d) { fail(E_DL, j->name, NULL); return NULL; }
        dsl_log("hostmods: %s %s downloaded (sha256 %s)", j->id, j->ver, j->sha);
    }
    /* 2: installed, every move recorded first (run_dir/moves.txt) */
    set_state(2, 100);
    for (int i = 0; i < nj; i++) {
        Job *j = &J[i]; char bk[1500], why[400] = "";
        snprintf(bk, sizeof bk, "%s/backup/%s", run_dir, j->id);
        if (dsl_uos_mod_install(j->file, j->id, j->ver, bk, MV, why, sizeof why)) {
            dsl_log("hostmods: install of %s %s failed: %s", j->id, j->ver, why);
            rollback(); close_log(); fail(E_INSTALL, j->name, why); return NULL;
        }
    }
    if (mv_log) { fprintf(mv_log, "INSTALLED\n"); fflush(mv_log); }
    pthread_mutex_lock(&mu); installed_ok = 1; pthread_mutex_unlock(&mu);   /* the main thread applies (dsl_hostmods_frame) */
    return NULL;
}

HM_EXPORT int dsl_hostmods_get(const char *host_lines, int vanilla, const char *env_lines, char *why, size_t cap) {
    char w[400]; if (!why || !cap) { why = w; cap = sizeof w; }
    why[0] = 0;
    pthread_mutex_lock(&mu); int s = st; pthread_mutex_unlock(&mu);
    if (s > 0) { snprintf(why, cap, "%s", TR("The host's mods are already being installed.")); return -1; }
    int test = getenv("DSL_HOSTMODS_TEST") && !strcmp(getenv("DSL_HOSTMODS_TEST"), "1");
    const char *busy = dsl_busy_reason();
    if (busy) { snprintf(why, cap, "%s", busy); dsi_utf8_fix_end(why); return -1; }
    if (!test && dsl_reg_scene() != 1) { snprintf(why, cap, "%s", TR("Go back to the title screen to change mods.")); return -1; }
    int diff = make_plan(host_lines, vanilla, 0);
    if (diff < 0) { snprintf(why, cap, "%s", TR("The host's mod list could not be read.")); return -1; }
    if (diff == 0) { snprintf(why, cap, "%s", TR("Your mods already match the host's.")); return -1; }
    for (int i = 0; i < nr; i++) if (!strcmp(R[i].action, "not_official")) {
        snprintf(why, cap, TR("%s cannot be downloaded here: it is not one of the mod loader's own mods."), R[i].name); dsi_utf8_fix_end(why);
        dsl_log("hostmods: refused: %s %s is not official", R[i].id, R[i].host_ver); return -1;
    }
    if (!plan_can_fix(diff)) { snprintf(why, cap, "%s", TR("The host's mod settings cannot be copied by this version of the mod loader.")); return -1; }
    nj = 0; nwant = 0; nsets = 0; sets_taken = 0; installed_ok = 0; err = E_NONE; pct = 0;
    for (int i = 0; i < nr; i++) {
        const Row *r = &R[i];
        if (!strcmp(r->action, "turn_off")) continue;
        snprintf(want_ids[nwant++], sizeof want_ids[0], "%s", r->id);
        if (is_download(r)) {
            Job *j = &J[nj++]; memset(j, 0, sizeof *j);
            snprintf(j->id, sizeof j->id, "%s", r->id); snprintf(j->name, sizeof j->name, "%s", r->name);
            snprintf(j->ver, sizeof j->ver, "%s", r->host_ver); j->host_older = !strcmp(r->action, "host_older");
            for (int k = 0; k < nh; k++) if (!strcmp(H[k].id, r->id)) snprintf(j->files, sizeof j->files, "%s", H[k].files);
        }
        nsets += strcmp(r->host_set, r->my_set) != 0;
    }
    snprintf(host_text, sizeof host_text, "%s", vanilla || !host_lines ? "" : host_lines);
    snprintf(env_s, sizeof env_s, "%s", env_lines ? env_lines : "");
    if (env_s[0] && env_s[strlen(env_s) - 1] != '\n' && strlen(env_s) + 1 < sizeof env_s) strcat(env_s, "\n");
    if (nj) {
        const char *ud = dsl_update_dir();
        if (!dsl_update_ready() || !ud || !*ud) { snprintf(why, cap, "%s", TR("Mods cannot be downloaded in this start.")); return -1; }
        char ts[32]; stamp(ts, sizeof ts); snprintf(run_dir, sizeof run_dir, "%s/hostmods/%s", ud, ts); dsl_uos_mkdirs(run_dir);
        if (!MV) MV = calloc(1, sizeof *MV);
        if (!MV) { snprintf(why, cap, "%s", TR("The host's mods could not be installed.")); return -1; }
        memset(MV, 0, sizeof *MV);
        char lp[1400]; snprintf(lp, sizeof lp, "%s/moves.txt", run_dir); mv_log = fopen(lp, "wb"); MV->log = mv_log;
        if (mv_log) { fprintf(mv_log, "HOST\t%s\n", vanilla ? "(vanilla)" : ""); for (int i = 0; i < nh; i++) fprintf(mv_log, "LINE\t%s@%s\n", H[i].id, H[i].ver); fflush(mv_log); }
    } else if (MV) MV->n = 0;
    dsl_log("hostmods: get: %d download(s), %d setting(s), %d host mod(s)%s", nj, nsets, nwant, test ? " (test)" : "");
    set_state(nj ? 1 : 2, 0);
    if (!nj) { pthread_mutex_lock(&mu); installed_ok = 1; pthread_mutex_unlock(&mu); return 0; }
    pthread_t t;
    if (pthread_create(&t, NULL, fix_thread, NULL)) { close_log(); set_state(0, 0); snprintf(why, cap, "%s", TR("The host's mods could not be installed.")); return -1; }
    pthread_detach(t);
    return 0;
}

HM_EXPORT int dsl_hostmods_state(int *p, char *why, size_t cap) {
    pthread_mutex_lock(&mu);
    int s = st, e = err; if (p) *p = pct; char a[300], b[400]; snprintf(a, sizeof a, "%s", err_a); snprintf(b, sizeof b, "%s", err_b);
    pthread_mutex_unlock(&mu);
    if (why && cap) {
        why[0] = 0;
        if (s == -1) {                                              /* made into text here: the player's language now */
            switch (e) {
            case E_NOREL:   snprintf(why, cap, TR("%s %s is not available for download. Your mods stay as they were."), a, b); break;
            case E_HOSTOLD: snprintf(why, cap, TR("The host has an older %s (%s) that is not available for download. The host should update it."), a, b); break;
            case E_DL:      snprintf(why, cap, TR("%s could not be downloaded. Check the internet connection. Your mods stay as they were."), a); break;
            case E_INSTALL: snprintf(why, cap, TR("%s could not be installed (%s). Your mods stay as they were."), a, b); break;
            case E_FILES:   snprintf(why, cap, TR("The host's %s is not the official release. Your mods stay as they were."), a); break;
            case E_APPLY:   snprintf(why, cap, TR("Your mods could not be changed: %s"), b); break;
            default:        snprintf(why, cap, "%s", TR("The host's mods could not be installed. Your mods stay as they were.")); break;
            }
            dsi_utf8_fix_end(why);
        }
    }
    return s;
}

/* undo everything (main thread): moves back, settings back, the mods list read again */
static void undo_all(int code, const char *a, const char *b) {
    int moved = MV && MV->n;
    if (moved) rollback();
    if (sets_taken) { settings_drop(); dsl_log("hostmods: the host's settings drafts dropped"); }
    sets_taken = 0;
    close_log();
    if (moved) dsl_mods_rescan();
    fail(code, a, b);
}
static int soft_wait, test_applied;
/* main thread, every frame (dsl_reg.c dsl_frame_hook) */
void dsl_hostmods_frame(void) {
    pthread_mutex_lock(&mu); int s = st, ok = installed_ok; if (ok) installed_ok = 0; pthread_mutex_unlock(&mu);
    if (s == 3) {                                                   /* the soft restart failed: A is still here */
        if (test_applied) return;
        if (dsl_soft_busy()) { soft_wait = 0; return; }
        if (++soft_wait < 30) return;
        dsl_log("hostmods: the soft restart ended without the new mods: rolling back");
        undo_all(E_RESTART, NULL, NULL); return;
    }
    if (!ok) return;
    if (nj) dsl_mods_rescan();                                      /* the just-installed mods are in the list */
    for (int i = 0; i < nj; i++) {                                  /* installed where this loader reads mods, the host's files */
        const DSLModInfo *m = mod_find(J[i].id);
        if (!m || !m->version || strcmp(m->version, J[i].ver)) {
            dsl_log("hostmods: %s %s is not in the mods folder after the install (%s)", J[i].id, J[i].ver, m && m->version ? m->version : "missing");
            undo_all(E_CHECK, J[i].name, NULL); return;
        }
        char mine[2048]; dsl_mod_hashes(J[i].id, mine, sizeof mine);
        if (J[i].files[0] && mine[0] && strcasecmp(J[i].files, mine)) {
            dsl_log("hostmods: %s %s: the official files differ from the host's (%s / %s)", J[i].id, J[i].ver, mine, J[i].files);
            undo_all(E_FILES, J[i].name, NULL); return;
        }
    }
    if (host_text[0] && settings_can_copy()) {                      /* after the rescan: a new mod's settings are known */
        int took = settings_take(host_text); sets_taken = 1;
        dsl_log("hostmods: %d gameplay setting value(s) taken from the host (%d mod(s) differed)", took, nsets);
    }
    /* the new set: the host's gameplay mods + my client mods and multiplayer that are on */
    const char *ids[HM_MAX * 2]; int n = 0;
    for (int i = 0; i < nwant; i++) ids[n++] = want_ids[i];
    for (int i = 0; i < dsl_mods_count() && n < HM_MAX * 2; i++) {
        const DSLModInfo *m = dsl_mods_at(i);
        if (!m || !m->enabled || (strcmp(m->id, "multiplayer") && strcmp(m->scope, "client"))) continue;
        int dup = 0; for (int j = 0; j < n; j++) dup |= !strcmp(ids[j], m->id);
        if (!dup) ids[n++] = m->id;
    }
    char list[1200] = ""; for (int i = 0; i < n; i++) { if (i) strlcat(list, ",", sizeof list); strlcat(list, ids[i], sizeof list); }
    set_state(3, 100);
    if (mv_log) { fprintf(mv_log, "APPLY\t%s\n", list); fflush(mv_log); }
    close_log();
    if (getenv("DSL_HOSTMODS_TEST") && !strcmp(getenv("DSL_HOSTMODS_TEST"), "1")) {
        char e1[2100]; snprintf(e1, sizeof e1, "%s", env_s); for (char *c = e1; *c; c++) if (*c == '\n') *c = '|';
        dsl_log("hostmods: test apply: %s env %s", list, e1);
        test_applied = 1; return;                                                    /* state stays 3 (no soft restart in a test) */
    }
    char why[400] = "";
    if (dsl_apply_mods_env(ids, n, env_s[0] ? env_s : NULL, why, sizeof why)) {
        dsl_log("hostmods: Apply refused: %s", why);
        undo_all(E_APPLY, NULL, why); return;
    }
    soft_wait = 0;
    dsl_log("hostmods: Apply started: %s", list);
}
