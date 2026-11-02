// dsl_hostmods.c - "Get the host's mods" (v0.1.14). The multiplayer mod's join was refused with DSNET_REJ_MODSET: it asks
// for a plan (dsl_hostmods_plan: the host's gameplay mods next to the guest's installed ones), shows it, and on the
// button calls dsl_hostmods_get. Then, on a thread: every missing / older / newer / other-files mod is downloaded ONLY
// from our official releases (dsl_update.c: tag "<id>-v<ver>", asset "<id>-<ver>.zip", signed sha256; DSL_UPDATE_API
// for tests), never from the host and never for an id that is not in MOD_REPOS; installed with dsl_uos_mod_install
// (moves recorded, old folder into a backup). On the main thread (dsl_hostmods_frame): the mods folder read again, the
// host's gameplay settings taken (one adapter, settings_*), and the normal Apply (pending.json + soft restart) with the
// host's gameplay ids + the guest's own client mods and multiplayer, env_lines passed to the new process.
// Any failure before the new process runs: every move rolled back, settings put back, nothing applied.
// Host lines: <id>@<ver>[:<file sha256s>[:<patch hash>]] per gameplay mod (the multiplayer mod is never in them) and
// <id>/<setting>@<value> per gameplay setting of a mod (lane modsettings, src/dsl_settings.c).
// Test only: DSL_HOSTMODS_TEST=1 = no title check, and the Apply is a log line ("hostmods: test apply: ...").
//
// TEMPLATE "Get these mods" (owner 2026-11-02, PLAN_v013 item 36): plan + get + rollback here are the ONE helper that brings
// mods to this machine. Two ends share it: JOIN (dsl_hostmods_get: the host's settings become drafts, then Apply + soft
// restart) and PACK (dsl_pack_import: a mod pack code from a friend; the end saves a group, nothing is switched). Never a
// second downloader: test/update/getmods_test.sh fails when another file calls the download / install functions.
// Mod pack code: "DSPACK1." + base64 of '\n'-joined lines + "." + crc32 (8 hex) of those lines. Lines: "kit <version>",
// "name <group name>", then "<id>@<version>" and "<id>/<setting>@<value>", sorted. The reader is strict (a line outside
// this grammar = a damaged code), so a code can never carry a URL, a file name or a path: downloads come only from the
// official releases of MOD_REPOS (dsl_update.c), exactly as for a host.
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
    char id[64], name[128], host_ver[32], my_ver[32], host_set[2048], my_set[2048], why[300];
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
static int is_gameplay(const DSLModInfo *m) { return m && m->scope && !strcmp(m->scope, "gameplay") && !m->feature && strcmp(m->id, "multiplayer"); }

static HostMod H[HM_MAX]; static Row R[HM_MAX * 2]; static int nh, nr, names_pending;
/* the rows; returns the number that differ, -1 bad input (the same rows for a host and a mod pack; plan_can_fix and
   get_start judge them for each end) */
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
        if (m) { snprintf(r->my_ver, sizeof r->my_ver, "%s", m->version ? m->version : ""); r->my_on = m->active == 1 && dsl_mod_in_hello(m->id);   /* Hell runs always, */
                 settings_mine(h->id, r->my_set, sizeof r->my_set); }                                                                                 /* in the set on Hell */
        int c = m ? dsl_update_vcmp(r->my_ver, h->ver) : 0;
        if (!m) r->action = "download";
        else if (c < 0) r->action = "update";
        else if (c > 0) r->action = "host_older";
        else if (strcmp(r->my_ver, h->ver)) r->action = "reinstall";            /* "1.0" vs "1.0.0": the host's exact one */
        else {
            char mine[2048]; dsl_mod_hashes(h->id, mine, sizeof mine);
            char dep[400];
            if (h->files[0] && mine[0] && strcasecmp(h->files, mine)) r->action = "reinstall";
            /* the host's files, on in the profile, but it does not run here (crash guard, failed start, other build):
               "Get the host's mods" changes nothing for it, and its soft restart + rejoin would be refused again and
               again. A missing requirement is left to "turn_on": the host's mods may bring it. */
            else if (m->active == -1 && m->enabled && !strcmp(m->kind, "code") && !dsl_mod_dep_text(h->id, dep, sizeof dep)) {
                r->action = "not_running"; snprintf(r->why, sizeof r->why, "%s", m->reason && *m->reason ? TR(m->reason) : TR("did not load"));
                dsi_utf8_fix_end(r->why);
            }
            else if (!r->my_on) r->action = "turn_on";
            else if (strcmp(r->host_set, r->my_set)) r->action = "settings";
            else r->action = "same";
        }
        int needs_dl = !strcmp(r->action, "download") || !strcmp(r->action, "update") || !strcmp(r->action, "host_older") || !strcmp(r->action, "reinstall");
        if (needs_dl && !r->official) r->action = "not_official";
        diff += strcmp(r->action, "same") != 0;
    }
    for (int i = 0; i < dsl_mods_count() && nr < HM_MAX * 2; i++) {      /* my running gameplay mods the host does not use */
        const DSLModInfo *m = dsl_mods_at(i); if (!m || m->active != 1 || !is_gameplay(m) || !dsl_mod_in_hello(m->id)) continue;
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
static int plan_can_fix(int diff, int pack) {
    if (diff < 0 || (!pack && diff == 0)) return 0;                    /* a pack equal to my mods is still a group to save */
    for (int i = 0; i < nr; i++) {
        if (!strcmp(R[i].action, "not_official") || (!pack && !strcmp(R[i].action, "not_running"))) return 0;
        if (pack || strcmp(R[i].action, "settings")) continue;          /* same version, other gameplay settings: */
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

/* the plan's JSON (the multiplayer mod's box and the Mods window's import window read it); tail = more members ("" none) */
static int plan_json(int diff, int pack, const char *tail, char *out, size_t cap) {
    size_t k = 0; int dls = 0, over = 0;
#define PUT(...) do { int w_ = snprintf(out + k, cap - k, __VA_ARGS__); if (w_ < 0 || (size_t)w_ >= cap - k) { over = 1; break; } k += (size_t)w_; } while (0)
    PUT("{\"rows\":[");
    for (int i = 0; i < nr && !over; i++) {
        const Row *r = &R[i]; char e[7][1300];
        jesc(e[0], sizeof e[0], r->id); jesc(e[1], sizeof e[1], r->name); jesc(e[2], sizeof e[2], r->host_ver);
        jesc(e[3], sizeof e[3], r->my_ver); jesc(e[4], sizeof e[4], r->host_set); jesc(e[5], sizeof e[5], r->my_set); jesc(e[6], sizeof e[6], r->why);
        PUT("%s{\"id\":\"%s\",\"name\":\"%s\",\"host_ver\":\"%s\",\"my_ver\":\"%s\",\"my_on\":%d,\"official\":%d,\"action\":\"%s\",\"host_set\":\"%s\",\"my_set\":\"%s\",\"why\":\"%s\"}",
            i ? "," : "", e[0], e[1], e[2], e[3], r->my_on, r->official, r->action, e[4], e[5], e[6]);
        dls += is_download(r);
    }
    PUT("],\"can_fix\":%d,\"downloads\":%d,\"names_pending\":%d%s}", plan_can_fix(diff, pack), dls, names_pending, tail ? tail : "");
#undef PUT
    if (over) { snprintf(out, cap, "{\"rows\":[],\"can_fix\":0,\"downloads\":0}"); dsl_log("hostmods: plan: output buffer too small (%zu)", cap); return -1; }
    dsl_log("%s: plan: %d row(s), %d differ, %d download(s), can_fix %d", pack ? "pack" : "hostmods", nr, diff, dls, plan_can_fix(diff, pack));
    return diff;
}
HM_EXPORT int dsl_hostmods_plan(const char *host_lines, int vanilla, char *out, size_t cap) {
    if (!out || cap < 64) return -1;
    out[0] = 0;
    int diff = make_plan(host_lines, vanilla, 1);
    if (diff < 0) { snprintf(out, cap, "{\"rows\":[],\"can_fix\":0,\"downloads\":0}"); dsl_log("hostmods: plan: bad host lines"); return -1; }
    return plan_json(diff, 0, "", out, cap);
}

/* ------------------------------------------------------------------ the get: shared by JOIN and PACK */
enum { E_NONE, E_NOREL, E_HOSTOLD, E_DL, E_INSTALL, E_CHECK, E_FILES, E_APPLY, E_RESTART, E_GROUP };
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int st, pct, installed_ok, err; static char err_a[300], err_b[400];
static Job J[HM_MAX]; static int nj;
static char want_ids[HM_MAX][64]; static int nwant;                    /* the host's / the pack's gameplay ids */
static int nsets, sets_taken; static char host_text[65536];       /* settings rows; the host's lines as given */
static char env_s[2048];
static DslMoves *MV; static FILE *mv_log; static char run_dir[1300];
static void run_versions_keep(void);
static int gm_pack;                                                    /* the running get's end: 0 JOIN (Apply), 1 PACK (a group) */
static char pk_group[80], pk_set[2048];                               /* PACK: the group's name and its settings lines */

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
    pthread_mutex_lock(&mu); installed_ok = 1; pthread_mutex_unlock(&mu);   /* the main thread ends it (join_end / pack_end) */
    return NULL;
}

static int get_busy(void) { pthread_mutex_lock(&mu); int s = st; pthread_mutex_unlock(&mu); return s == 1 || s == 2 || s == 3; }
/* the shared start (main thread): the plan again, the refusals, the jobs, the download thread. 0 = started */
static int get_start(const char *host_lines, int vanilla, const char *env_lines, int pack, char *why, size_t cap) {
    if (get_busy()) {
        snprintf(why, cap, "%s", pack ? TR("Mods are already being installed. Wait until that ends.") : TR("The host's mods are already being installed."));
        return -1;
    }
    int test = getenv("DSL_HOSTMODS_TEST") && !strcmp(getenv("DSL_HOSTMODS_TEST"), "1");
    const char *busy = dsl_busy_reason();
    if (busy) { snprintf(why, cap, "%s", busy); dsi_utf8_fix_end(why); return -1; }
    if (!test && dsl_reg_scene() != 1) { snprintf(why, cap, "%s", TR("Go back to the title screen to change mods.")); return -1; }
    int diff = make_plan(host_lines, vanilla, 0);
    if (diff < 0) { snprintf(why, cap, "%s", pack ? TR("This mod pack code is damaged or cut off. Ask your friend to send it again.") : TR("The host's mod list could not be read.")); return -1; }
    if (diff == 0 && !pack) { snprintf(why, cap, "%s", TR("Your mods already match the host's.")); return -1; }
    for (int i = 0; i < nr; i++) if (!strcmp(R[i].action, "not_official")) {
        snprintf(why, cap, TR("%s cannot be downloaded here: it is not one of the mod loader's own mods."), R[i].name); dsi_utf8_fix_end(why);
        dsl_log("%s: refused: %s %s is not official", pack ? "pack" : "hostmods", R[i].id, R[i].host_ver); return -1;
    }
    for (int i = 0; i < nr && !pack; i++) if (!strcmp(R[i].action, "not_running")) {           /* never a soft restart for it */
        snprintf(why, cap, TR("%s is on but did not start on this computer: %s"), R[i].name, R[i].why); dsi_utf8_fix_end(why);
        dsl_log("hostmods: refused: %s %s is on but does not run here (%s)", R[i].id, R[i].my_ver, R[i].why); return -1;
    }
    if (!plan_can_fix(diff, pack)) { snprintf(why, cap, "%s", TR("The host's mod settings cannot be copied by this version of the mod loader.")); return -1; }
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
    gm_pack = pack;
    run_versions_keep();                                            /* before any file moves (main thread) */
    if (nj) {
        const char *ud = dsl_update_dir();
        if (!dsl_update_ready() || !ud || !*ud) { snprintf(why, cap, "%s", TR("Mods cannot be downloaded in this start.")); return -1; }
        char ts[32]; stamp(ts, sizeof ts); snprintf(run_dir, sizeof run_dir, "%s/hostmods/%s", ud, ts); dsl_uos_mkdirs(run_dir);
        if (!MV) MV = calloc(1, sizeof *MV);
        if (!MV) { snprintf(why, cap, "%s", TR("The host's mods could not be installed.")); return -1; }
        memset(MV, 0, sizeof *MV);
        char lp[1400]; snprintf(lp, sizeof lp, "%s/moves.txt", run_dir); mv_log = fopen(lp, "wb"); MV->log = mv_log;
        if (mv_log) {
            if (pack) fprintf(mv_log, "PACK\t%s\n", pk_group); else fprintf(mv_log, "HOST\t%s\n", vanilla ? "(vanilla)" : "");
            for (int i = 0; i < nh; i++) fprintf(mv_log, "LINE\t%s@%s\n", H[i].id, H[i].ver);
            fflush(mv_log);
        }
    } else if (MV) MV->n = 0;
    dsl_log("%s: get: %d download(s), %d setting(s), %d mod(s)%s", pack ? "pack" : "hostmods", nj, nsets, nwant, test ? " (test)" : "");
    set_state(nj ? 1 : 2, 0);
    if (!nj) { pthread_mutex_lock(&mu); installed_ok = 1; pthread_mutex_unlock(&mu); return 0; }
    pthread_t t;
    if (pthread_create(&t, NULL, fix_thread, NULL)) { close_log(); set_state(0, 0); snprintf(why, cap, "%s", TR("The host's mods could not be installed.")); return -1; }
    pthread_detach(t);
    return 0;
}

HM_EXPORT int dsl_hostmods_get(const char *host_lines, int vanilla, const char *env_lines, char *why, size_t cap) {
    char w[400]; if (!why || !cap) { why = w; cap = sizeof w; }
    why[0] = 0;
    return get_start(host_lines, vanilla, env_lines, 0, why, cap);
}

HM_EXPORT int dsl_hostmods_state(int *p, char *why, size_t cap) {
    pthread_mutex_lock(&mu);
    int s = st, e = err; if (p) *p = pct; char a[300], b[400]; snprintf(a, sizeof a, "%s", err_a); snprintf(b, sizeof b, "%s", err_b);
    pthread_mutex_unlock(&mu);
    if (why && cap) {
        why[0] = 0;
        if (s == -1) {                                              /* made into text here: the player's language now */
            switch (e) {
            case E_NOREL:   if (gm_pack) snprintf(why, cap, TR("%s %s is not available for download. Ask your friend to update it and send a new code. Your mods stay as they were."), a, b);
                            else snprintf(why, cap, TR("%s %s is not available for download. Your mods stay as they were."), a, b); break;
            case E_HOSTOLD: if (gm_pack) snprintf(why, cap, TR("%s %s is not available for download. Ask your friend to update it and send a new code. Your mods stay as they were."), a, b);
                            else snprintf(why, cap, TR("The host has an older %s (%s) that is not available for download. The host should update it."), a, b); break;
            case E_DL:      snprintf(why, cap, TR("%s could not be downloaded. Check the internet connection. Your mods stay as they were."), a); break;
            case E_INSTALL: snprintf(why, cap, TR("%s could not be installed (%s). Your mods stay as they were."), a, b); break;
            case E_FILES:   snprintf(why, cap, TR("The host's %s is not the official release. Your mods stay as they were."), a); break;
            case E_APPLY:   snprintf(why, cap, TR("Your mods could not be changed: %s"), b); break;
            case E_GROUP:   snprintf(why, cap, TR("The group \"%s\" could not be saved. Your mods stay as they were."), a); break;
            default:        snprintf(why, cap, "%s", gm_pack ? TR("The mods could not be installed. Your mods stay as they were.")
                                                             : TR("The host's mods could not be installed. Your mods stay as they were.")); break;
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
/* the versions this game started with (before the first install of this process): a mod that is on and runs while its
   files on disk are now another version (a pack import replaced them) is "stale" until the next Apply */
static char rv_id[HM_MAX * 2][64], rv_ver[HM_MAX * 2][32]; static int nrv = -1;
static void run_versions_keep(void) {
    if (nrv >= 0) return;
    nrv = 0;
    for (int i = 0; i < dsl_mods_count() && nrv < HM_MAX * 2; i++) {
        const DSLModInfo *m = dsl_mods_at(i);
        if (!m || !m->enabled || m->active != 1 || !m->version) continue;
        snprintf(rv_id[nrv], sizeof rv_id[0], "%s", m->id); snprintf(rv_ver[nrv], sizeof rv_ver[0], "%s", m->version); nrv++;
    }
}
int dsl_mod_stale(const char *id) {
    const DSLModInfo *m = id ? mod_find(id) : NULL;
    if (!m || !m->enabled || !m->version) return 0;
    for (int i = 0; i < nrv; i++) if (!strcmp(rv_id[i], id)) return strcmp(rv_ver[i], m->version) != 0;
    return 0;
}
/* the mods folder read again; every downloaded mod is there at its version, with the host's files. 0 = ok, else undone */
static int check_installed(void) {
    if (nj) dsl_mods_rescan();                                      /* the just-installed mods are in the list */
    for (int i = 0; i < nj; i++) {                                  /* installed where this loader reads mods, the host's files */
        const DSLModInfo *m = mod_find(J[i].id);
        if (!m || !m->version || strcmp(m->version, J[i].ver)) {
            dsl_log("hostmods: %s %s is not in the mods folder after the install (%s)", J[i].id, J[i].ver, m && m->version ? m->version : "missing");
            undo_all(E_CHECK, J[i].name, NULL); return -1;
        }
        char mine[2048]; dsl_mod_hashes(J[i].id, mine, sizeof mine);
        if (J[i].files[0] && mine[0] && strcasecmp(J[i].files, mine)) {
            dsl_log("hostmods: %s %s: the official files differ from the host's (%s / %s)", J[i].id, J[i].ver, mine, J[i].files);
            undo_all(E_FILES, J[i].name, NULL); return -1;
        }
    }
    return 0;
}
/* the new set: the host's / pack's gameplay mods + my client mods and launcher features (multiplayer, Units Laboratory) that are on */
static int final_ids(const char **ids, int max) {
    int n = 0;
    for (int i = 0; i < nwant && n < max; i++) ids[n++] = want_ids[i];
    for (int i = 0; i < dsl_mods_count() && n < max; i++) {
        const DSLModInfo *m = dsl_mods_at(i);
        if (!m || !m->enabled || (strcmp(m->id, "multiplayer") && strcmp(m->scope, "client") && !m->feature)) continue;
        int dup = 0; for (int j = 0; j < n; j++) dup |= !strcmp(ids[j], m->id);
        if (!dup) ids[n++] = m->id;
    }
    return n;
}
static int soft_wait, test_applied;
static void join_end(void) {
    if (check_installed()) return;
    if (host_text[0] && settings_can_copy()) {                      /* after the rescan: a new mod's settings are known */
        int took = settings_take(host_text); sets_taken = 1;
        dsl_log("hostmods: %d gameplay setting value(s) taken from the host (%d mod(s) differed)", took, nsets);
    }
    const char *ids[HM_MAX * 2]; int n = final_ids(ids, HM_MAX * 2);
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
/* PACK: the group (the pack's gameplay ids + my client / launcher mods that are on + the pack's settings); nothing switched */
static void pack_end(void) {
    if (check_installed()) return;
    const char *ids[HM_MAX * 2]; int n = final_ids(ids, HM_MAX * 2);
    if (dsl_group_save_ex(pk_group, ids, n, pk_set)) {
        dsl_log("pack: the group \"%s\" could not be saved: every change is rolled back", pk_group);
        undo_all(E_GROUP, pk_group, NULL); return;
    }
    if (mv_log) { fprintf(mv_log, "GROUP\t%s\n", pk_group); fflush(mv_log); }
    close_log();
    if (MV) MV->n = 0;                                              /* done: the old folders stay in run_dir/backup */
    set_state(4, 100);
    int nl = pk_set[0] ? 1 : 0; for (const char *c = pk_set; *c; c++) nl += *c == '\n';
    dsl_log("pack: saved as the group \"%s\" (%d mod(s), %d download(s), %d setting(s))", pk_group, n, nj, nl);
}
/* main thread, every frame (dsl_reg.c dsl_frame_hook) */
void dsl_hostmods_frame(void) {
    pthread_mutex_lock(&mu); int s = st, ok = installed_ok;
    int win = gm_pack && dsl_win_is_open && dsl_win_is_open();       /* PACK with the Mods window open: the window ends it */
    if (ok && !win) installed_ok = 0;
    pthread_mutex_unlock(&mu);
    if (s == 3 && !gm_pack) {                                       /* the soft restart failed: A is still here */
        if (test_applied) return;
        if (dsl_soft_busy()) { soft_wait = 0; return; }
        if (++soft_wait < 30) return;
        dsl_log("hostmods: the soft restart ended without the new mods: rolling back");
        undo_all(E_RESTART, NULL, NULL); return;
    }
    if (!ok || win) return;
    if (gm_pack) pack_end(); else join_end();
}

/* ------------------------------------------------------------------ mod packs: the code (see the top of this file) */
#define PACK_FMT 1
#define PACK_MAX 16384                                              /* bytes of lines */
static const char *my_kit(void) { static const char *k = DSL_RELEASE; return k; }   /* the kit release ("dev": a build without RELEASE) */
static uint32_t crc32_of(const unsigned char *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64_val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}
static size_t b64_enc(const unsigned char *in, size_t n, char *out, size_t cap) {          /* 0 = no room */
    size_t k = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0u) | (i + 2 < n ? (uint32_t)in[i + 2] : 0u);
        if (k + 5 > cap) return 0;
        out[k++] = B64[v >> 18 & 63]; out[k++] = B64[v >> 12 & 63];
        out[k++] = i + 1 < n ? B64[v >> 6 & 63] : '=';
        out[k++] = i + 2 < n ? B64[v & 63] : '=';
    }
    out[k] = 0; return k;
}
static long b64_dec(const char *in, size_t n, unsigned char *out, size_t cap) {             /* -1 = not base64 */
    if (!n || n % 4) return -1;
    size_t k = 0;
    for (size_t i = 0; i < n; i += 4) {
        int a = b64_val(in[i]), b = b64_val(in[i + 1]);
        int c = in[i + 2] == '=' ? -2 : b64_val(in[i + 2]), d = in[i + 3] == '=' ? -2 : b64_val(in[i + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1 || (c == -2 && d != -2) || ((c == -2 || d == -2) && i + 4 != n)) return -1;
        uint32_t v = (uint32_t)a << 18 | (uint32_t)b << 12 | (uint32_t)(c < 0 ? 0 : c) << 6 | (uint32_t)(d < 0 ? 0 : d);
        if (k + 3 > cap) return -1;
        out[k++] = (unsigned char)(v >> 16); if (c >= 0) out[k++] = (unsigned char)(v >> 8); if (d >= 0) out[k++] = (unsigned char)v;
    }
    return (long)k;
}
static int word_ok(const char *s, size_t max) {                     /* a setting id or value: A-Z a-z 0-9 _ . + - */
    if (!*s || strlen(s) >= max) return 0;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '.' || *s == '+' || *s == '-')) return 0;
    return 1;
}
static int utf8_ok(const unsigned char *s) {
    while (*s) {
        int n = *s < 0x80 ? 0 : (*s & 0xE0) == 0xC0 ? 1 : (*s & 0xF0) == 0xE0 ? 2 : (*s & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0 || (n == 1 && *s < 0xC2)) return 0;
        s++;
        for (int i = 0; i < n; i++, s++) if ((*s & 0xC0) != 0x80) return 0;
    }
    return 1;
}
/* a group name for a code: no control characters, no rich-text brackets, at most 60 bytes (whole characters) */
static void name_clean(const char *in, char *out, size_t cap) {
    size_t k = 0; if (cap > 61) cap = 61;
    for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p && k + 1 < cap; ) {
        int n = *p < 0x80 ? 1 : (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 0;
        if (!n) { p++; continue; }
        if (k + (size_t)n + 1 > cap) break;
        if (n == 1) out[k++] = (*p < 0x20 || *p == 0x7F || *p == '<' || *p == '>') ? ' ' : (char)*p;
        else { int ok = 1; for (int i = 1; i < n; i++) ok &= (p[i] & 0xC0) == 0x80; if (ok) { memcpy(out + k, p, (size_t)n); k += (size_t)n; } else { p++; continue; } }
        p += n;
    }
    out[k] = 0;
    size_t a = 0; while (out[a] == ' ') a++;
    memmove(out, out + a, strlen(out + a) + 1); k = strlen(out); while (k && out[k - 1] == ' ') out[--k] = 0;
}
static int name_ok(const char *s) {
    if (!*s || strlen(s) > 60 || *s == ' ' || !utf8_ok((const unsigned char *)s)) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) if (*p < 0x20 || *p == 0x7F || *p == '<' || *p == '>') return 0;
    return 1;
}

typedef struct { char name[64], kit[32], body[PACK_MAX]; int mods, sets; } Pack;
enum { PK_OK = 0, PK_NOTCODE, PK_DAMAGED, PK_NEWFMT, PK_NEWKIT };
/* the code in `text` (the first "DSPACK" in it; spaces and line breaks inside it are skipped: a chat app may wrap it) */
static int pack_read(const char *text, Pack *pk) {
    memset(pk, 0, sizeof *pk);
    const char *p = text ? strstr(text, "DSPACK") : NULL; if (!p) return PK_NOTCODE;
    p += 6; long fmt = 0; int nd = 0;
    while (*p >= '0' && *p <= '9' && nd < 4) { fmt = fmt * 10 + (*p - '0'); p++; nd++; }
    if (!nd || *p != '.') return PK_DAMAGED;
    if (fmt > PACK_FMT) return PK_NEWFMT;
    if (fmt != PACK_FMT) return PK_DAMAGED;
    static char b[PACK_MAX * 4 / 3 + 8]; size_t bn = 0;
    for (p++; *p && *p != '.'; p++) {
        if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') continue;
        if ((b64_val(*p) < 0 && *p != '=') || bn + 1 >= sizeof b) return PK_DAMAGED;
        b[bn++] = *p;
    }
    if (*p != '.') return PK_DAMAGED;                                /* cut off */
    p++;
    char hx[9];
    for (int i = 0; i < 8; i++) { char c = p[i]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return PK_DAMAGED; hx[i] = (char)(c >= 'A' && c <= 'F' ? c + 32 : c); }
    hx[8] = 0;
    static unsigned char pay[PACK_MAX + 1];
    long pn = b64_dec(b, bn, pay, PACK_MAX);
    if (pn <= 0) return PK_DAMAGED;
    char want[9]; snprintf(want, sizeof want, "%08x", (unsigned)crc32_of(pay, (size_t)pn));
    if (strcmp(want, hx) || memchr(pay, 0, (size_t)pn)) return PK_DAMAGED;
    pay[pn] = 0;
    char ids[HM_MAX][64]; int ni = 0, nl = 0; size_t bk = 0;
    for (char *sv = NULL, *ln = strtok_r((char *)pay, "\n", &sv); ln; ln = strtok_r(NULL, "\n", &sv)) {
        size_t l = strlen(ln); if (l && ln[l - 1] == '\r') ln[--l] = 0;
        if (!l) continue;
        if (++nl > 600) return PK_DAMAGED;
        if (!strncmp(ln, "kit ", 4)) {
            if (pk->kit[0] || strlen(ln + 4) >= sizeof pk->kit || (strcmp(ln + 4, "dev") && !safe_ver(ln + 4))) return PK_DAMAGED;
            snprintf(pk->kit, sizeof pk->kit, "%s", ln + 4); continue;
        }
        if (!strncmp(ln, "name ", 5)) {
            if (pk->name[0] || !name_ok(ln + 5)) return PK_DAMAGED;
            snprintf(pk->name, sizeof pk->name, "%s", ln + 5); continue;
        }
        char *at = strchr(ln, '@'), *sl = strchr(ln, '/');
        if (!at || strchr(at + 1, '@') || strchr(ln, ':')) return PK_DAMAGED;
        *at = 0;
        if (sl && sl < at) {                                            /* <id>/<setting>@<value> */
            *sl = 0;
            int ok = safe_id(ln) && strlen(ln) < 64 && word_ok(sl + 1, 48) && word_ok(at + 1, 48);
            *sl = '/';
            if (!ok) return PK_DAMAGED;
            pk->sets++;
        } else {                                                        /* <id>@<version> */
            if (sl || !safe_id(ln) || strlen(ln) >= 64 || strlen(at + 1) >= 32 || !safe_ver(at + 1) || ni >= HM_MAX) return PK_DAMAGED;
            for (int i = 0; i < ni; i++) if (!strcmp(ids[i], ln)) return PK_DAMAGED;
            snprintf(ids[ni++], sizeof ids[0], "%s", ln); pk->mods++;
        }
        *at = '@';
        int w = snprintf(pk->body + bk, sizeof pk->body - bk, "%s%s", bk ? "\n" : "", ln);
        if (w < 0 || (size_t)w >= sizeof pk->body - bk) return PK_DAMAGED;
        bk += (size_t)w;
    }
    for (const char *s = pk->body; *s; ) {                              /* a setting line names a mod of the pack */
        const char *e = strchr(s, '\n'); size_t l = e ? (size_t)(e - s) : strlen(s);
        const char *sl = memchr(s, '/', l), *at = memchr(s, '@', l);
        if (sl && at && sl < at) { int in = 0; for (int i = 0; i < ni; i++) in |= strlen(ids[i]) == (size_t)(sl - s) && !strncmp(ids[i], s, (size_t)(sl - s)); if (!in) return PK_DAMAGED; }
        s = e ? e + 1 : s + l;
    }
    if (pk->kit[0] && strcmp(pk->kit, "dev") && strcmp(my_kit(), "dev") && dsl_update_vcmp(pk->kit, my_kit()) > 0) return PK_NEWKIT;
    return PK_OK;
}
static void pack_why(int r, const Pack *pk, char *out, size_t cap) {
    switch (r) {
    case PK_NOTCODE: snprintf(out, cap, "%s", TR("This is not a mod pack code. Copy the whole code your friend sent (it starts with DSPACK), then paste it here.")); break;
    case PK_NEWFMT:  snprintf(out, cap, "%s", TR("This mod pack code was made with a newer mod loader. Update the mod loader (top right of the title screen), then paste the code again.")); break;
    case PK_NEWKIT:  snprintf(out, cap, TR("This mod pack was made with a newer mod loader (%s). You have %s. Update the mod loader (top right of the title screen), then paste the code again."), pk->kit, my_kit()); break;
    default:         snprintf(out, cap, "%s", TR("This mod pack code is damaged or cut off. Ask your friend to send it again.")); break;
    }
    dsi_utf8_fix_end(out);
}
static int cmp_cstr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
/* the group a pack is saved as: its name ("Mod pack" without one); the same name with other mods or settings: " (2)" ... */
static void pack_group_name(const Pack *pk, const char *set, char *out, size_t cap) {
    const char *ids[HM_MAX * 2]; int n = final_ids(ids, HM_MAX * 2);
    qsort(ids, (size_t)n, sizeof *ids, cmp_cstr);
    char base[64]; snprintf(base, sizeof base, "%s", pk->name[0] ? pk->name : TR("Mod pack")); dsi_utf8_fix_end(base);
    for (int k = 1; k < 100; k++) {
        char nm[80]; if (k == 1) snprintf(nm, sizeof nm, "%s", base); else snprintf(nm, sizeof nm, "%s (%d)", base, k);
        int g = 0, ng = dsl_groups_count(); while (g < ng && strcmp(dsl_group_name(g), nm)) g++;
        if (g == ng) { snprintf(out, cap, "%s", nm); return; }
        const char *gi[HM_MAX * 2]; int gn = dsl_group_mods(g, gi, HM_MAX * 2);         /* the same pack again: that group */
        qsort(gi, (size_t)gn, sizeof *gi, cmp_cstr);
        int same = gn == n && !strcmp(dsl_group_settings(g), set);
        for (int i = 0; same && i < n; i++) same = !strcmp(ids[i], gi[i]);
        if (same) { snprintf(out, cap, "%s", nm); return; }
    }
    snprintf(out, cap, "%s", base);
}
static void pack_settings(const Pack *pk, char *out, size_t cap) {  /* the pack's setting lines (the group's "settings") */
    size_t k = 0; out[0] = 0;
    for (const char *s = pk->body; *s; ) {
        const char *e = strchr(s, '\n'); size_t l = e ? (size_t)(e - s) : strlen(s);
        const char *sl = memchr(s, '/', l), *at = memchr(s, '@', l);
        if (sl && at && sl < at && k + l + 2 < cap) k += (size_t)snprintf(out + k, cap - k, "%s%.*s", k ? "\n" : "", (int)l, s);
        s = e ? e + 1 : s + l;
    }
}

/* the code of these switches (Share): gameplay mods only, as a host sends them (no launcher feature, no multiplayer, no
   custom difficulty: never a switch, never in a group; no client mod: only the player's own screen) */
int dsl_pack_code(const char *const *ids, int n, const char *name, char *out, size_t cap, char *why, size_t wcap) {
    char w[400]; if (!why || !wcap) { why = w; wcap = sizeof w; }
    why[0] = 0; if (!out || cap < 64) return -1; out[0] = 0;
    static char ln[HM_MAX * 8][160]; const char *lp[HM_MAX * 8]; int nl = 0, nm_ = 0;
    char seen[HM_MAX][64]; int ns = 0;
    for (int i = 0; i < n; i++) {
        const DSLModInfo *m = ids && ids[i] ? mod_find(ids[i]) : NULL;
        if (!m || m->hidden || !is_gameplay(m) || dsl_mod_is_difficulty(m->id)) continue;
        int dup = 0; for (int k = 0; k < ns; k++) dup |= !strcmp(seen[k], m->id);
        if (dup) continue;
        if (ns >= HM_MAX) { snprintf(why, wcap, "%s", TR("Too many mods are on to share them as one mod pack.")); return -1; }
        snprintf(seen[ns++], sizeof seen[0], "%s", m->id);
        if (!safe_id(m->id) || strlen(m->id) >= 64 || !m->version || !safe_ver(m->version) || strlen(m->version) >= 32) {
            snprintf(why, wcap, TR("%s has no usable version number, so it cannot be in a mod pack."), m->name ? m->name : m->id); dsi_utf8_fix_end(why);
            dsl_log("pack: share refused: %s version '%s'", m->id, m->version ? m->version : ""); return -1;
        }
        if (nl >= HM_MAX * 8) break;
        snprintf(ln[nl], sizeof ln[0], "%s@%s", m->id, m->version); lp[nl] = ln[nl]; nl++; nm_++;
        static char sl[8192]; dsl_settings_lines_edit(m->id, sl, sizeof sl);
        for (char *sv = NULL, *t = strtok_r(sl, "\n", &sv); t && nl < HM_MAX * 8; t = strtok_r(NULL, "\n", &sv)) {
            char c[160]; snprintf(c, sizeof c, "%s", t); char *s1 = strchr(c, '/'), *a1 = s1 ? strchr(s1, '@') : NULL;
            if (!s1 || !a1) continue;
            *a1 = 0; int ok = word_ok(s1 + 1, 48) && word_ok(a1 + 1, 48);
            if (!ok) { dsl_log("pack: setting line %s skipped (not shareable)", t); continue; }
            snprintf(ln[nl], sizeof ln[0], "%s", t); lp[nl] = ln[nl]; nl++;
        }
    }
    qsort(lp, (size_t)nl, sizeof *lp, cmp_cstr);
    static char pay[PACK_MAX]; size_t k = 0; char nm[64]; name_clean(name, nm, sizeof nm);
    k += (size_t)snprintf(pay + k, sizeof pay - k, "kit %s", my_kit());
    if (nm[0]) k += (size_t)snprintf(pay + k, sizeof pay - k, "\nname %s", nm);
    for (int i = 0; i < nl && k < sizeof pay; i++) k += (size_t)snprintf(pay + k, sizeof pay - k, "\n%s", lp[i]);
    static char b64[PACK_MAX * 4 / 3 + 8];
    if (k >= sizeof pay || !b64_enc((const unsigned char *)pay, k, b64, sizeof b64) || strlen(b64) + 24 > cap) {
        snprintf(why, wcap, "%s", TR("Too many mods are on to share them as one mod pack.")); return -1;
    }
    snprintf(out, cap, "DSPACK%d.%s.%08x", PACK_FMT, b64, (unsigned)crc32_of((const unsigned char *)pay, k));
    dsl_log("pack: code made: %d mod(s), %d line(s), %zu characters%s%s", nm_, nl, strlen(out), nm[0] ? ", group " : "", nm);
    return nm_;
}

static Pack PK, PKP;                                                /* PK: the import's pack; PKP: the plan window's */
/* the plan of a code (the Import window): the host-mods rows + the pack's name, kit, the group it becomes, a refusal */
int dsl_pack_plan(const char *code, char *out, size_t cap) {
    if (!out || cap < 256) return -1;
    char why[700] = "", e1[1500];
    int r = pack_read(code, &PKP);
    if (!r && make_plan(PKP.body, 0, 1) < 0) r = PK_DAMAGED;
    if (r) {
        pack_why(r, &PKP, why, sizeof why); jesc(e1, sizeof e1, why);
        snprintf(out, cap, "{\"rows\":[],\"can_fix\":0,\"downloads\":0,\"names_pending\":0,\"name\":\"\",\"kit\":\"\",\"group\":\"\",\"error\":\"%s\"}", e1);
        dsl_log("pack: plan refused (%d): %s", r, why); return -1;
    }
    int diff = make_plan(PKP.body, 0, 1);
    for (int i = 0; i < nr && !why[0]; i++) if (!strcmp(R[i].action, "not_official")) {
        snprintf(why, sizeof why, TR("%s cannot be downloaded here: it is not one of the mod loader's own mods."), R[i].name); dsi_utf8_fix_end(why);
    }
    static char set[2048]; pack_settings(&PKP, set, sizeof set);
    char grp[80], e2[200], e3[200], e4[100], tail[2400];
    pack_group_name(&PKP, set, grp, sizeof grp);
    jesc(e1, sizeof e1, why); jesc(e2, sizeof e2, PKP.name); jesc(e3, sizeof e3, grp); jesc(e4, sizeof e4, PKP.kit);
    snprintf(tail, sizeof tail, ",\"name\":\"%s\",\"kit\":\"%s\",\"group\":\"%s\",\"error\":\"%s\"", e2, e4, e3, e1);
    return plan_json(diff, 1, tail, out, cap);
}
/* Import: the shared get; its end saves the group (pack_end) */
int dsl_pack_import(const char *code, char *why, size_t cap) {
    char w[700]; if (!why || !cap) { why = w; cap = sizeof w; }
    why[0] = 0;
    if (get_busy()) { snprintf(why, cap, "%s", TR("Mods are already being installed. Wait until that ends.")); return -1; }
    int r = pack_read(code, &PK);
    if (r) { pack_why(r, &PK, why, cap); dsl_log("pack: import refused (%d): %s", r, why); return -1; }
    char set[sizeof pk_set], grp[sizeof pk_group]; pack_settings(&PK, set, sizeof set); pack_group_name(&PK, set, grp, sizeof grp);
    snprintf(pk_set, sizeof pk_set, "%s", set); snprintf(pk_group, sizeof pk_group, "%s", grp);   /* not while a get runs (above) */
    int g = get_start(PK.body, 0, NULL, 1, why, cap);
    dsl_log("pack: import of \"%s\" (%d mod(s), %d setting(s)) as the group \"%s\": %s", PK.name, PK.mods, PK.sets, pk_group, g ? why : "started");
    return g;
}
int dsl_pack_state(int *p, char *text, size_t cap, char *group, size_t gcap) {
    char why[700] = ""; int pc = 0;
    if (group && gcap) snprintf(group, gcap, "%s", gm_pack ? pk_group : "");
    if (!gm_pack) { if (p) *p = 0; if (text && cap) text[0] = 0; return 0; }
    int s = dsl_hostmods_state(&pc, why, sizeof why);
    if (p) *p = pc;
    if (text && cap) {
        if (s == 1) snprintf(text, cap, TR("Downloading the mods (%d %%)..."), pc);
        else if (s == 2 || s == 3) snprintf(text, cap, "%s", TR("Installing the mods..."));
        else if (s == 4) snprintf(text, cap, TR("Saved as the group \"%s\". Pick it in Groups, then click Apply."), pk_group);
        else snprintf(text, cap, "%s", why);
        dsi_utf8_fix_end(text);
    }
    return s;
}
int dsl_pack_ready(void) { pthread_mutex_lock(&mu); int r = gm_pack && installed_ok; pthread_mutex_unlock(&mu); return r; }
void dsl_pack_finish(void) {
    pthread_mutex_lock(&mu); int ok = gm_pack && installed_ok; if (ok) installed_ok = 0; pthread_mutex_unlock(&mu);
    if (ok) pack_end();
}
