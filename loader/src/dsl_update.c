// dsl_update.c - auto-update of the loader kit and its mods (owner 2026-10-16: "auto-updates for the mod loader and its
// mods, switchable on the title screen, top-right corner").
//
// Check (a thread, once per cold start; never in B of a soft restart): GitHub releases of AmfiLogos/ds_multiplayer
// (the kit: tag v<N.N>, asset Dungeon-Settlers-Modded-Mac.zip / .dmg / -Windows.zip, dsl_uos_kit_asset) and of every
// installed mod's repo (asset <id>-<version>.zip, the packages of loader/modules/package_mods.sh). Newer than the
// installed release (ROOT RELEASE file) / the mod.json version, not rolled back before (ROOT/update/bad.txt).
// Every download is checked against its sha256: the asset's GitHub "digest", else a "<asset>.sha256" asset; none = no
// install. Network failure, rate limit, bad JSON: silent (log line), next start tries again.
// Title corner (top right, only at the title while its menu shows): "Auto-update" switch (default on; ROOT/update/
// settings.json, per machine) and one short line or an "Update to v13.x" button.
//   On:  download at once; at the title with nothing busy (no world, no multiplayer session, no restart): install,
//        then the hidden soft restart (dsl_reg_restart_same). B shows "Updated to v13.x".
//   Off: no download; the button downloads and installs on click.
// Install = dsl_uos_kit_install / dsl_uos_mod_install: the old files move into ROOT/update/backup-<from>-<time>/, the
// move list is ROOT/update/pending.txt. B confirms at the title (pending.txt -> update/old/). A soft restart that fails
// (B never ready): A moves everything back, the version goes into bad.txt. A cold start that finds pending.txt (the
// last update never reached the title): the starter (Mac stub / Windows exe) rolls back the same way. Saves are never
// touched (they are in PROFILE/data, outside every installed folder).
// Kit first: a kit update carries the mods; mod updates wait for the next start.
// Test only: DSL_UPDATE_API=<base URL> (a local release server instead of https://api.github.com), DSL_UPDATE=0 (off),
// DSL_UPDATE_TEST=1 (ROOT/update/test_click = a click on the corner button).
#include "dsl_internal.h"
#include "dsl_menu.h"
#include "dsl_update_os.h"
#include "../sdk/dsl_sdk.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int dsl_reg_scene(void);                                   /* dsl_reg.c: 1 title, 2 world */
int dsl_reg_restart_same(const char *env, char *why, size_t cap);
int dsl_soft_wanted(void);
int dsl_soft_last_failed(void);                            /* dsl_soft.c: the last restart of this process failed */
int dsl_ui_title_shown(void);                              /* dsl_ui.c: title menu column on screen */
void dsl_ui_place_tr(DSLObj w, float right, float top, float wd, float h);

#define KIT_REPO "AmfiLogos/ds_multiplayer"
static const struct { const char *id, *repo; } MOD_REPOS[] = {
    { "multiplayer", "AmfiLogos/ds_multiplayer" }, { "custom-modes", "AmfiLogos/ds_custommodes" },
    { "gcd-cdr", "AmfiLogos/ds_modloader" }, { "hell-difficulty", "AmfiLogos/ds_modloader" },
};

typedef struct { char id[64], name[128], have[32], repo[128], ver[32], url[1024], sha[65], asset[160], file[1100]; } ModUp;
#define MAXU 24
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    int checked;                                           /* the check finished (with or without updates) */
    char have[32];                                         /* installed kit release */
    char kit_ver[32], kit_url[1024], kit_sha[65], kit_asset[160], kit_file[1100];
    ModUp mods[MAXU]; int nmods;                           /* installed mods (have / repo); ver[0] = an update exists */
    int dl;                                                /* 0 none, 1 running, 2 ready, -1 failed */
    int dl_wanted;
} S;
static int auto_on = 1, enabled, thread_on;
static char root[1024], udir[1100];
static char line[200];                                     /* the corner's one short line */
static int restarting;                                     /* A: our update restart runs */
static char confirm_label[64];                             /* B: DSML_UPDATE of this start */
static int busy_install;

/* ------------------------------------------------------------------ small helpers */
static void upath(char *out, size_t n, const char *f) { snprintf(out, n, "%s/%s", udir, f); }
static void stamp(char *out, size_t n) { time_t t = time(NULL); strftime(out, n, "%Y%m%d-%H%M%S", localtime(&t)); }
static char *slurp_(const char *p, size_t max) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    char *b = malloc(max + 1); size_t n = b ? fread(b, 1, max, f) : 0; fclose(f);
    if (b) b[n] = 0; return b;
}
static int write_file(const char *p, const char *s) {
    char t[1200]; snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "wb"); if (!f) return -1;
    fputs(s, f); fclose(f); remove(p);
    return dsl_uos_move(t, p);
}
/* "v13.2", "13.2.1", "0.10.0-test" -> numbers; compare */
static int vparse(const char *s, long v[6]) {
    int n = 0; memset(v, 0, 6 * sizeof *v);
    while (*s && (*s < '0' || *s > '9')) s++;
    while (*s >= '0' && *s <= '9' && n < 6) { v[n++] = strtol(s, (char **)&s, 10); if (*s != '.') break; s++; }
    return n;
}
static int vcmp(const char *a, const char *b) {
    long x[6], y[6]; vparse(a, x); vparse(b, y);
    for (int i = 0; i < 6; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
static void vclean(const char *s, char *out, size_t n) {         /* "v13.2" -> "13.2" */
    while (*s && (*s < '0' || *s > '9')) s++;
    size_t k = 0; while (s[k] && ((s[k] >= '0' && s[k] <= '9') || s[k] == '.') && k + 1 < n) { out[k] = s[k]; k++; }
    out[k] = 0;
}
static int is_bad(const char *key) {                             /* key "kit 13.3" / "custom-modes 0.12.0" */
    char p[1200]; upath(p, sizeof p, "bad.txt"); char *b = slurp_(p, 1 << 16); if (!b) return 0;
    int hit = 0; for (char *l = strtok(b, "\n"); l && !hit; l = strtok(NULL, "\n")) hit = !strcmp(l, key);
    free(b); return hit;
}
static void add_bad(const char *key) {
    char p[1200]; upath(p, sizeof p, "bad.txt"); FILE *f = fopen(p, "ab"); if (f) { fprintf(f, "%s\n", key); fclose(f); }
}
static void settings_load(void) {
    char p[1200]; upath(p, sizeof p, "settings.json"); char *b = slurp_(p, 4096);
    if (b) { JNode *j = json_parse(b); JNode *a = json_get(j, "auto"); if (a && a->t == JN_BOOL) auto_on = a->b; json_free(j); free(b); }
}
static void settings_save(void) {
    char p[1200]; upath(p, sizeof p, "settings.json");
    if (write_file(p, auto_on ? "{\"auto\":true}\n" : "{\"auto\":false}\n")) dsl_log("update: cannot write %s", p);
}

/* ------------------------------------------------------------------ GitHub (thread) */
static JNode *releases(const char *repo, const char *tag_for_file) {
    const char *api = getenv("DSL_UPDATE_API"); if (!api || !*api) api = "https://api.github.com";
    char url[600], f[1200], cache[1100];
    snprintf(url, sizeof url, "%s/repos/%s/releases?per_page=15", api, repo);
    upath(cache, sizeof cache, "cache"); dsl_uos_mkdirs(cache);
    snprintf(f, sizeof f, "%s/%s.json", cache, tag_for_file);
    if (dsl_uos_fetch(url, f)) { dsl_log("update: %s: no answer (network or rate limit); next start tries again", repo); return NULL; }
    char *b = slurp_(f, 4 << 20); JNode *j = b ? json_parse(b) : NULL; free(b);
    if (!j || j->t != JN_ARR) { dsl_log("update: %s: unexpected answer", repo); json_free(j); return NULL; }
    return j;
}
/* the sha256 of an asset: GitHub's "digest" or a "<name>.sha256" asset of the same release */
static int asset_sha(JNode *rel, JNode *asset, char out[65]) {
    const char *d = json_str(asset, "digest");
    if (d && !strncmp(d, "sha256:", 7) && strlen(d + 7) == 64) { snprintf(out, 65, "%s", d + 7); return 0; }
    const char *nm = json_str(asset, "name"); if (!nm) return -1;
    char want[200]; snprintf(want, sizeof want, "%s.sha256", nm);
    JNode *as = json_get(rel, "assets");
    for (JNode *a = as ? as->child : NULL; a; a = a->next) {
        const char *n2 = json_str(a, "name"), *u = json_str(a, "browser_download_url");
        if (!n2 || !u || strcmp(n2, want)) continue;
        char f[1200]; upath(f, sizeof f, "cache/sha.txt");
        if (dsl_uos_fetch(u, f)) return -1;
        char *b = slurp_(f, 4096); int ok = 0;
        if (b) { size_t k = 0; while (k < 64 && ((b[k] >= '0' && b[k] <= '9') || (b[k] >= 'a' && b[k] <= 'f') || (b[k] >= 'A' && b[k] <= 'F'))) k++;
                 if (k == 64) { for (int i = 0; i < 64; i++) out[i] = (char)(b[i] >= 'A' && b[i] <= 'F' ? b[i] + 32 : b[i]); out[64] = 0; ok = 1; } }
        free(b); return ok ? 0 : -1;
    }
    return -1;
}
static int usable(JNode *rel) {
    JNode *d = json_get(rel, "draft"), *p = json_get(rel, "prerelease");
    return !(d && d->t == JN_BOOL && d->b) && !(p && p->t == JN_BOOL && p->b);
}
static void check_kit(void) {
    JNode *rs = releases(KIT_REPO, "kit"); if (!rs) return;
    char best[32] = "", url[1024] = "", sha[65] = "", an[160] = "";
    for (JNode *r = rs->child; r; r = r->next) {
        const char *tag = json_str(r, "tag_name"); if (!tag || !usable(r) || tag[0] != 'v' || tag[1] < '0' || tag[1] > '9') continue;
        char v[32]; vclean(tag, v, sizeof v);
        if (vcmp(v, S.have) <= 0 || (best[0] && vcmp(v, best) <= 0)) continue;
        char key[64]; snprintf(key, sizeof key, "kit %s", v); if (is_bad(key)) continue;
        JNode *as = json_get(r, "assets");
        for (JNode *a = as ? as->child : NULL; a; a = a->next) {
            const char *n = json_str(a, "name"), *u = json_str(a, "browser_download_url");
            if (!n || !u || !dsl_uos_kit_asset(n)) continue;
            char s[65]; if (asset_sha(r, a, s)) { dsl_log("update: kit %s: %s has no sha256, skipped", tag, n); continue; }
            snprintf(best, sizeof best, "%s", v); snprintf(url, sizeof url, "%s", u); snprintf(sha, sizeof sha, "%s", s); snprintf(an, sizeof an, "%s", n);
            break;
        }
    }
    json_free(rs);
    pthread_mutex_lock(&mu);
    if (best[0]) { snprintf(S.kit_ver, sizeof S.kit_ver, "%s", best); snprintf(S.kit_url, sizeof S.kit_url, "%s", url);
                   snprintf(S.kit_sha, sizeof S.kit_sha, "%s", sha); snprintf(S.kit_asset, sizeof S.kit_asset, "%s", an); }
    pthread_mutex_unlock(&mu);
    dsl_log("update: kit: installed %s, newest usable %s", S.have[0] ? S.have : "unknown", best[0] ? best : "(none newer)");
}
static void check_mods(void) {
    char done[MAXU][128]; int nd = 0;
    for (int i = 0; i < S.nmods; i++) {
        const char *repo = S.mods[i].repo; int seen = 0;
        for (int k = 0; k < nd; k++) seen |= !strcmp(done[k], repo);
        if (seen) continue;
        snprintf(done[nd++], sizeof done[0], "%s", repo);
        char tagf[160]; snprintf(tagf, sizeof tagf, "%s", repo); for (char *c = tagf; *c; c++) if (*c == '/') *c = '_';
        JNode *rs = releases(repo, tagf); if (!rs) continue;
        for (int m = 0; m < S.nmods; m++) {
            ModUp *u = &S.mods[m]; if (strcmp(u->repo, repo)) continue;
            char best[32] = "", url[1024] = "", sha[65] = "", an[160] = "";
            size_t il = strlen(u->id);
            for (JNode *r = rs->child; r; r = r->next) {
                if (!usable(r)) continue;
                JNode *as = json_get(r, "assets");
                for (JNode *a = as ? as->child : NULL; a; a = a->next) {
                    const char *n = json_str(a, "name"), *url_ = json_str(a, "browser_download_url");
                    size_t nl = n ? strlen(n) : 0;
                    if (!n || !url_ || nl < il + 6 || strncmp(n, u->id, il) || n[il] != '-' || strcmp(n + nl - 4, ".zip")) continue;
                    char v[32]; snprintf(v, sizeof v, "%.*s", (int)(nl - il - 5), n + il + 1);
                    if (v[0] < '0' || v[0] > '9' || vcmp(v, u->have) <= 0 || (best[0] && vcmp(v, best) <= 0)) continue;
                    char key[128]; snprintf(key, sizeof key, "%s %s", u->id, v); if (is_bad(key)) continue;
                    char s[65]; if (asset_sha(r, a, s)) { dsl_log("update: %s: %s has no sha256, skipped", u->id, n); continue; }
                    snprintf(best, sizeof best, "%s", v); snprintf(url, sizeof url, "%s", url_); snprintf(sha, sizeof sha, "%s", s); snprintf(an, sizeof an, "%s", n);
                }
            }
            pthread_mutex_lock(&mu);
            if (best[0]) { snprintf(u->ver, sizeof u->ver, "%s", best); snprintf(u->url, sizeof u->url, "%s", url);
                           snprintf(u->sha, sizeof u->sha, "%s", sha); snprintf(u->asset, sizeof u->asset, "%s", an); }
            pthread_mutex_unlock(&mu);
            if (best[0]) dsl_log("update: mod %s: installed %s, newer %s (%s)", u->id, u->have, best, repo);
        }
        json_free(rs);
    }
}
/* download one file to ROOT/update/dl/<asset>, sha256 checked; 0 = ok */
static int download(const char *url, const char *asset, const char *sha, char *out, size_t n) {
    char dl[1100], part[1300]; upath(dl, sizeof dl, "dl"); dsl_uos_mkdirs(dl);
    snprintf(out, n, "%s/%s", dl, asset); snprintf(part, sizeof part, "%s.part", out);
    if (dsl_uos_exists(out)) { char h[65]; if (!dsl_uos_sha256(out, h) && !strcmp(h, sha)) return 0; }
    if (dsl_uos_fetch(url, part)) { dsl_log("update: download of %s failed; next start tries again", asset); return -1; }
    char h[65]; if (dsl_uos_sha256(part, h) || strcmp(h, sha)) {
        dsl_log("update: %s: sha256 %s, the release says %s: not installed", asset, h, sha);
        char bad[1300]; snprintf(bad, sizeof bad, "%s.badsha", out); remove(bad); dsl_uos_move(part, bad); return -1;
    }
    remove(out); if (dsl_uos_move(part, out)) return -1;
    dsl_log("update: %s downloaded, sha256 ok", asset);
    return 0;
}
static void download_all(void) {
    pthread_mutex_lock(&mu); S.dl = 1; pthread_mutex_unlock(&mu);
    int bad = 0, any = 0;
    if (S.kit_ver[0]) { any = 1; bad |= download(S.kit_url, S.kit_asset, S.kit_sha, S.kit_file, sizeof S.kit_file) != 0; }
    else for (int i = 0; i < S.nmods; i++) if (S.mods[i].ver[0]) {
        any = 1;
        if (download(S.mods[i].url, S.mods[i].asset, S.mods[i].sha, S.mods[i].file, sizeof S.mods[i].file)) { bad = 1; S.mods[i].ver[0] = 0; }
    }
    int got = 0; if (!S.kit_ver[0]) for (int i = 0; i < S.nmods; i++) got += S.mods[i].ver[0] != 0;   /* mods: the good ones go on */
    pthread_mutex_lock(&mu); S.dl = !any ? 0 : S.kit_ver[0] ? (bad ? -1 : 2) : (got ? 2 : -1); pthread_mutex_unlock(&mu);
}
static void *check_thread(void *arg) {
    (void)arg;
    check_kit();
    if (!S.kit_ver[0]) check_mods();
    pthread_mutex_lock(&mu); S.checked = 1; int want = S.dl_wanted || auto_on; pthread_mutex_unlock(&mu);
    if (want) download_all();
    thread_on = 0;
    return NULL;
}
static void *dl_thread(void *arg) { (void)arg; download_all(); thread_on = 0; return NULL; }
static int any_update(void) { if (S.kit_ver[0]) return 1; for (int i = 0; i < S.nmods; i++) if (S.mods[i].ver[0]) return 1; return 0; }

/* ------------------------------------------------------------------ pending / rollback */
static void rollback(const DslMoves *mv, const char *why_dir) {
    char ts[32], fdir[1300]; stamp(ts, sizeof ts); snprintf(fdir, sizeof fdir, "%s/retired/update-failed-%s", root, ts);
    for (int i = mv->n - 1; i >= 0; i--) {
        char dst[1500]; snprintf(dst, sizeof dst, "%s/%d", fdir, i);
        if (dsl_uos_exists(mv->path[i]) && dsl_uos_move(mv->path[i], dst)) dsl_log("update: rollback: cannot move %s away", mv->path[i]);
        if (mv->backup[i][0] && dsl_uos_move(mv->backup[i], mv->path[i])) dsl_log("update: rollback: cannot put back %s", mv->path[i]);
    }
    dsl_log("update: rolled back %d change(s) (%s)", mv->n, why_dir);
}
static int pending_write(const char *label, const char *key, const DslMoves *mv) {
    size_t cap = 4096 + (size_t)mv->n * 2200; char *b = malloc(cap); if (!b) return -1;
    size_t k = (size_t)snprintf(b, cap, "LABEL\t%s\nKEY\t%s\nFROM\t%s\n", label, key, S.have);
    for (int i = 0; i < mv->n && k < cap; i++) k += (size_t)snprintf(b + k, cap - k, "MOVE\t%s\t%s\n", mv->path[i], mv->backup[i]);
    char p[1200]; upath(p, sizeof p, "pending.txt"); int r = write_file(p, b); free(b); return r;
}
static int pending_read(DslMoves *mv, char *label, size_t ln, char *key, size_t kn) {
    char p[1200]; upath(p, sizeof p, "pending.txt"); char *b = slurp_(p, 1 << 20); if (!b) return -1;
    mv->n = 0; label[0] = key[0] = 0;
    for (char *l = strtok(b, "\n"); l; l = strtok(NULL, "\n")) {
        char *t1 = strchr(l, '\t'); if (!t1) continue; *t1++ = 0;
        if (!strcmp(l, "LABEL")) snprintf(label, ln, "%s", t1);
        else if (!strcmp(l, "KEY")) snprintf(key, kn, "%s", t1);
        else if (!strcmp(l, "MOVE") && mv->n < DSL_UMOVES) {
            char *t2 = strchr(t1, '\t'); if (t2) *t2++ = 0;
            snprintf(mv->path[mv->n], sizeof mv->path[0], "%s", t1); snprintf(mv->backup[mv->n], sizeof mv->backup[0], "%s", t2 ? t2 : ""); mv->n++;
        }
    }
    free(b); return 0;
}
static void pending_done(const char *why) {
    char p[1200], q[1300], ts[32]; upath(p, sizeof p, "pending.txt"); stamp(ts, sizeof ts);
    snprintf(q, sizeof q, "%s/old/pending-%s-%s.txt", udir, why, ts);
    if (dsl_uos_move(p, q)) dsl_log("update: cannot move %s", p);
}

/* ------------------------------------------------------------------ install (main thread, title, nothing busy) */
static void install_now(void) {
    pthread_mutex_lock(&mu); int ok = S.dl == 2; pthread_mutex_unlock(&mu);
    if (!ok) return;
    DslMoves *mv = calloc(1, sizeof *mv); if (!mv) return;
    char ts[32], bdir[1300], why[300] = "", label[64], key[160];
    stamp(ts, sizeof ts); snprintf(bdir, sizeof bdir, "%s/backup-%s-%s", udir, S.have[0] ? S.have : "old", ts);
    int r = 0; char names[160] = "";
    if (S.kit_ver[0]) {
        snprintf(label, sizeof label, "v%s", S.kit_ver); snprintf(key, sizeof key, "kit %s", S.kit_ver);
        r = dsl_uos_kit_install(S.kit_file, bdir, mv, why, sizeof why);
    } else {
        snprintf(label, sizeof label, "mods"); key[0] = 0;
        for (int i = 0; i < S.nmods && !r; i++) if (S.mods[i].ver[0] && S.mods[i].file[0]) {
            char mb[1500]; snprintf(mb, sizeof mb, "%s/%s", bdir, S.mods[i].id);
            r = dsl_uos_mod_install(S.mods[i].file, S.mods[i].id, mb, mv, why, sizeof why);
            if (!r) { size_t k = strlen(key); snprintf(key + k, sizeof key - k, "%s%s %s", k ? "," : "", S.mods[i].id, S.mods[i].ver);
                      k = strlen(names); snprintf(names + k, sizeof names - k, "%s%s %s", k ? ", " : "", S.mods[i].name, S.mods[i].ver); }
        }
    }
    pthread_mutex_lock(&mu); S.dl = 0; pthread_mutex_unlock(&mu);
    if (r) {
        rollback(mv, "install failed");
        dsl_log("update: install of %s failed: %s", label, why);
        snprintf(line, sizeof line, "The update could not be installed. The game tries again next time.");
        free(mv); return;
    }
    if (!S.kit_ver[0]) snprintf(label, sizeof label, "mods:%s", names);
    if (pending_write(label, key, mv)) { rollback(mv, "no pending file"); free(mv); return; }
    char envl[200]; snprintf(envl, sizeof envl, "DSML_UPDATE=%s\n", S.kit_ver[0] ? S.kit_ver : "mods");
    dsl_log("update: %s installed (%d change(s), old files in %s); restarting", label, mv->n, bdir);
    if (dsl_reg_restart_same(envl, why, sizeof why)) {
        dsl_log("update: restart refused (%s): rolling back", why);
        rollback(mv, "restart refused"); pending_done("refused");
        free(mv); return;
    }
    free(mv); restarting = 1;
}

/* ------------------------------------------------------------------ title corner */
static uintptr_t c_tog, c_txt, c_btn;                      /* DSLObj kept as plain pointers (re-made when gone) */
static int c_btn_on; static char c_last_line[200], c_btn_label[64];
static void toggle_change(void *ud, DSLObj w) {
    (void)ud; const DSLUi *u = dsl_ui_kit(); if (!u) return;
    auto_on = u->toggle_get(w) != 0; settings_save();
    dsl_log("update: auto-update %s", auto_on ? "ON" : "OFF");
    if (auto_on && S.checked && any_update() && S.dl == 0 && !thread_on) { pthread_t t; thread_on = 1; if (pthread_create(&t, NULL, dl_thread, NULL)) thread_on = 0; else pthread_detach(t); }
}
static void button_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    pthread_mutex_lock(&mu); int st = S.dl; S.dl_wanted = 1; pthread_mutex_unlock(&mu);
    dsl_log("update: \"%s\" clicked", c_btn_label);
    if (st == 0 || st == -1) { pthread_t t; thread_on = 1; if (pthread_create(&t, NULL, dl_thread, NULL)) thread_on = 0; else pthread_detach(t); }
}
static void corner_frame(void) {
    const DSLUi *u = dsl_ui_kit(); if (!u) return;
    int show = dsl_ui_title_shown() && dsl_reg_scene() == 1;
    DSLObj tog = (DSLObj)c_tog, txt = (DSLObj)c_txt, btn = (DSLObj)c_btn;
    if (!show) { if (tog) u->set_active(tog, 0); if (txt) u->set_active(txt, 0); if (btn) u->set_active(btn, 0); return; }
    if (!tog) {
        tog = u->toggle(NULL, 0, 0, 260, "Auto-update", auto_on, toggle_change, NULL);
        txt = u->text(NULL, 0, 0, 420, 30, "", 16, DSL_TXT_RIGHT, 0xE8DCC0FF);
        btn = u->button(NULL, 0, 0, 220, 40, "Update", button_click, NULL);
        if (!tog || !txt || !btn) return;
        dsl_ui_place_tr(tog, 24, 20, 260, 40); dsl_ui_place_tr(txt, 24, 64, 420, 30); dsl_ui_place_tr(btn, 24, 64, 220, 40);
        u->tooltip(tog, "Auto-update", "On: new versions of the mod loader and the mods install at the title screen.\nOff: you choose when to update.");
        c_tog = (uintptr_t)tog; c_txt = (uintptr_t)txt; c_btn = (uintptr_t)btn; c_last_line[0] = 0; c_btn_on = -1;
        dsl_log("update: title corner shown (auto-update %s)", auto_on ? "on" : "off");
    }
    u->set_active(tog, 1);
    /* the button: Off with an update not yet downloading */
    pthread_mutex_lock(&mu); int st = S.dl, avail = S.checked && any_update(); char kv[32]; snprintf(kv, sizeof kv, "%s", S.kit_ver); pthread_mutex_unlock(&mu);
    int want_btn = !auto_on && avail && st != 1 && st != 2 && !line[0];
    char bl[64]; if (kv[0]) snprintf(bl, sizeof bl, "Update to v%s", kv); else snprintf(bl, sizeof bl, "Update mods");
    if (want_btn && strcmp(bl, c_btn_label)) { snprintf(c_btn_label, sizeof c_btn_label, "%s", bl); u->set_text(btn, bl); }
    if (want_btn != c_btn_on) { u->set_active(btn, want_btn); c_btn_on = want_btn; if (want_btn) dsl_log("update: corner button \"%s\" shown", bl); }
    char l[200] = "";
    if (line[0]) snprintf(l, sizeof l, "%s", line);
    else if (st == 1 && (!auto_on || S.dl_wanted)) snprintf(l, sizeof l, kv[0] ? "Downloading v%s..." : "Downloading mod updates...", kv);
    if (strcmp(l, c_last_line)) { snprintf(c_last_line, sizeof c_last_line, "%s", l); u->set_text(txt, l); }
    u->set_active(txt, l[0] != 0 && !want_btn);
}

/* ------------------------------------------------------------------ entry points (dsl_reg.c) */
void dsl_update_start(void) {
    const char *off = getenv("DSL_UPDATE");
    const char *r = dsl_uos_root(); if (!r || !*r) { dsl_log("update: no ROOT in this start: auto-update off"); return; }
    snprintf(root, sizeof root, "%s", r); snprintf(udir, sizeof udir, "%s/update", root); dsl_uos_mkdirs(udir);
    char od[1200]; upath(od, sizeof od, "old"); dsl_uos_mkdirs(od);
    settings_load();
    if (dsl_uos_release(S.have, sizeof S.have)) S.have[0] = 0;
    if (S.have[0]) setenv("DSML_RELEASE", S.have, 1);            /* the multiplayer mod names it when the mod sets differ */
    { const char *e = getenv("DSML_UPDATE"); if (e && *e && dsl_soft_wanted()) snprintf(confirm_label, sizeof confirm_label, "%s", e); }
    enabled = 1;
    dsl_log("update: release %s, auto-update %s, ROOT %s%s", S.have[0] ? S.have : "unknown", auto_on ? "on" : "off", root,
            confirm_label[0] ? " (this start follows an update)" : "");
    if (off && !strcmp(off, "0")) { dsl_log("update: DSL_UPDATE=0: no check"); return; }
    if (dsl_soft_wanted()) return;                         /* B of a soft restart: A checked already */
    for (int i = 0; i < dsl_mods_count() && S.nmods < MAXU; i++) {
        const DSLModInfo *m = dsl_mods_at(i); if (!m || !m->version) continue;
        const char *repo = NULL;
        for (size_t k = 0; k < sizeof MOD_REPOS / sizeof MOD_REPOS[0]; k++) if (!strcmp(MOD_REPOS[k].id, m->id)) repo = MOD_REPOS[k].repo;
        if (!repo) continue;
        ModUp *u = &S.mods[S.nmods++]; memset(u, 0, sizeof *u);
        snprintf(u->id, sizeof u->id, "%s", m->id); snprintf(u->name, sizeof u->name, "%s", m->name ? m->name : m->id);
        snprintf(u->have, sizeof u->have, "%s", m->version); snprintf(u->repo, sizeof u->repo, "%s", repo);
    }
    pthread_t t; thread_on = 1;
    if (pthread_create(&t, NULL, check_thread, NULL)) { thread_on = 0; dsl_log("update: no thread"); return; }
    pthread_detach(t);
}
void dsl_update_frame(void) {
    if (!enabled) return;
    int at_title = dsl_reg_scene() == 1;
    if (confirm_label[0] && at_title && !dsl_soft_busy()) {          /* B: the update reached the title */
        DslMoves *mv = calloc(1, sizeof *mv); char label[200], key[160];
        if (mv && !pending_read(mv, label, sizeof label, key, sizeof key)) {
            pending_done("ok");
            if (!strncmp(label, "mods:", 5)) snprintf(line, sizeof line, "Updated: %s", label + 5);
            else snprintf(line, sizeof line, "Updated to %s", label);
            dsl_log("update: %s confirmed at the title", label);
        }
        free(mv); confirm_label[0] = 0;
    }
    if (restarting && dsl_soft_last_failed()) {                       /* A: B never got ready */
        DslMoves *mv = calloc(1, sizeof *mv); char label[200], key[160];
        if (mv && !pending_read(mv, label, sizeof label, key, sizeof key)) {
            rollback(mv, "the new version did not start"); pending_done("failed");
            for (char *k = strtok(key, ","); k; k = strtok(NULL, ",")) add_bad(k);
            snprintf(line, sizeof line, "The update did not start. You still have v%s.", S.have[0] ? S.have : "the old version");
            if (!strncmp(label, "mods:", 5)) snprintf(line, sizeof line, "The mod update did not start. The old mods stay.");
        }
        free(mv); restarting = 0;
    }
    if (at_title && !restarting && !busy_install && !dsl_busy_reason() && !dsl_soft_busy()) {
        pthread_mutex_lock(&mu); int ready = S.dl == 2, go = ready && (auto_on || S.dl_wanted); pthread_mutex_unlock(&mu);
        if (go) { busy_install = 1; install_now(); busy_install = 0; }
    }
    corner_frame();
    { const char *t = getenv("DSL_UPDATE_TEST"); static int tick;                       /* test: click the button by file */
      if (t && *t == '1' && ++tick % 30 == 0 && c_btn_on == 1) {
          char p[1200]; upath(p, sizeof p, "test_click");
          if (dsl_uos_exists(p)) { char q[1300]; snprintf(q, sizeof q, "%s.done", p); remove(q); dsl_uos_move(p, q); button_click(NULL, NULL); } } }
}
