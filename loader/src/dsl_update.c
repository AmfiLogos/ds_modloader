// dsl_update.c - auto-update of the loader kit and its mods (owner 2026-10-16: "auto-updates for the mod loader and its
// mods, switchable on the title screen, top-right corner").
//
// Check (a thread, once per cold start; never in B of a soft restart): GitHub releases of AmfiLogos/ds_modloader
// (the loader kit: tag v0.1.N, asset Dungeon-Settlers-Modded-Mac.dmg / .zip / -Windows.zip, dsl_uos_kit_asset) and of
// every installed mod's own repo (tag <id>-v0.1.N, asset <id>-0.1.N.zip, the packages of loader/modules/package_mods.sh).
// research/version_table.md (ds_multiplayer): each component has its own number. Newer than the installed release
// (RELEASE file) / the mod.json version, not rolled back before (ROOT/update/bad.txt).
// Every download is checked against its sha256: the asset's GitHub "digest", else a "<asset>.sha256" asset; none = no
// install. Network failure, rate limit, bad JSON: silent (log line), next start tries again.
// Title corner (top right, only at the title while its menu shows): "Auto-update" switch (default on; ROOT/update/
// settings.json, per machine) and one short line or an "Update to 0.1.N" button.
//   On:  download while the game runs (sha256 checked), then ROOT/update/ready.txt; the next start installs it in the
//        starter before the game opens (start/dsl_update_start.c: a small window with a progress bar, no prompt).
//   Off: no download; the button downloads and marks it (ready.txt CONSENT, want.txt): installed at the next start.
// Nothing here moves an installed file. A start that runs a just-installed update has DSML_UPDATE=<label> (the
// starter): at the title it confirms (ROOT/update/pending.txt -> update/old) and shows "Updated to 0.1.N"; an update
// that never gets there is rolled back by the starter after a second try. ROOT/update/notice.txt (the starter's one
// line: an install failed, a rollback) is shown in the corner once.
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
int dsl_soft_wanted(void);
int dsl_ui_title_shown(void);                              /* dsl_ui.c: title menu column on screen */
void dsl_ui_place_tr(DSLObj w, float right, float top, float wd, float h);

#define KIT_REPO "AmfiLogos/ds_modloader"
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
/* the corner's one short line, made into text in the current language every frame (a language change at the title
   shows at once): LN_UPDATED_MODS "Updated: <names>", LN_UPDATED "Updated to <ver>" (arg), LN_NOTICE the starter's
   line (written in the language of that start: shown as it is) */
enum { LN_NONE, LN_UPDATED_MODS, LN_UPDATED, LN_NOTICE };
static int line_kind; static char line_arg[400];
static char confirm_label[200];                            /* DSML_UPDATE of this start (the starter installed it) */
static int consent;                                        /* the player clicked "Update to" */
static int ready_kind;                                     /* set by the download thread: 1 "0.1.N installs ...", 2 mods */

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
/* "v0.1.7", "0.1.10", "0.1.3-test" -> numbers; compare */
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
static void vclean(const char *s, char *out, size_t n) {         /* "v0.1.7" -> "0.1.7" */
    while (*s && (*s < '0' || *s > '9')) s++;
    size_t k = 0; while (s[k] && ((s[k] >= '0' && s[k] <= '9') || s[k] == '.') && k + 1 < n) { out[k] = s[k]; k++; }
    out[k] = 0;
}
static int is_bad(const char *key) {                             /* key "kit 0.1.7" / "custom-modes 0.1.4" */
    char p[1200]; upath(p, sizeof p, "bad.txt"); char *b = slurp_(p, 1 << 16); if (!b) return 0;
    int hit = 0; for (char *l = strtok(b, "\n"); l && !hit; l = strtok(NULL, "\n")) hit = !strcmp(l, key);
    free(b); return hit;
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
                    /* only the mod's own release "<id>-v<version>" counts: old bundle-v0.1.x releases hold mod zips of the
                       old numbers (multiplayer-0.10.0.zip, custom-modes-0.11.0.zip ...) that would win as "newer" */
                    char want_tag[160]; snprintf(want_tag, sizeof want_tag, "%s-v%s", u->id, v);
                    const char *tag = json_str(r, "tag_name"); if (!tag || strcmp(tag, want_tag)) continue;
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
/* what the starter installs at the next start (start/dsl_update_start.c reads it) */
static void ready_write(void) {
    char b[8192]; size_t k = 0; int c = consent || S.dl_wanted;
    if (S.kit_ver[0]) {
        k += (size_t)snprintf(b + k, sizeof b - k, "KIND\tkit\nLABEL\tv%s\nKEY\tkit %s\n%sKIT\t%s\t%s\t%s\n", S.kit_ver, S.kit_ver,
                              c ? "CONSENT\t1\n" : "", S.kit_ver, S.kit_file, S.kit_sha);
    } else {
        char names[300] = "", key[400] = "";
        for (int i = 0; i < S.nmods; i++) if (S.mods[i].ver[0] && S.mods[i].file[0]) {
            size_t n = strlen(names), m = strlen(key);
            snprintf(names + n, sizeof names - n, "%s%s %s", n ? ", " : "", S.mods[i].name, S.mods[i].ver);
            snprintf(key + m, sizeof key - m, "%s%s %s", m ? "," : "", S.mods[i].id, S.mods[i].ver);
        }
        k += (size_t)snprintf(b + k, sizeof b - k, "KIND\tmods\nLABEL\tmods:%s\nKEY\t%s\n%s", names, key, c ? "CONSENT\t1\n" : "");
        for (int i = 0; i < S.nmods && k < sizeof b; i++) if (S.mods[i].ver[0] && S.mods[i].file[0])
            k += (size_t)snprintf(b + k, sizeof b - k, "MOD\t%s\t%s\t%s\t%s\n", S.mods[i].id, S.mods[i].ver, S.mods[i].file, S.mods[i].sha);
    }
    char p[1200]; upath(p, sizeof p, "ready.txt");
    if (write_file(p, b)) { dsl_log("update: cannot write %s", p); return; }
    pthread_mutex_lock(&mu);
    ready_kind = S.kit_ver[0] ? 1 : 2;
    pthread_mutex_unlock(&mu);
    dsl_log("update: ready.txt written (%s%s): installed at the next start", S.kit_ver[0] ? S.kit_ver : "mods", c ? ", Update clicked" : "");
}
/* the update's key ("kit 0.1.5" / "multiplayer 0.1.5,custom-modes 0.1.3"): the "Update to" click is kept in want.txt */
static void update_key(char *out, size_t n) {
    out[0] = 0;
    if (S.kit_ver[0]) { snprintf(out, n, "kit %s", S.kit_ver); return; }
    for (int i = 0; i < S.nmods; i++) if (S.mods[i].ver[0]) { size_t k = strlen(out); snprintf(out + k, n - k, "%s%s %s", k ? "," : "", S.mods[i].id, S.mods[i].ver); }
}
static void download_all(void) {
    pthread_mutex_lock(&mu); S.dl = 1; pthread_mutex_unlock(&mu);
    int bad = 0, any = 0;
    if (S.kit_ver[0]) { any = 1; bad |= download(S.kit_url, S.kit_asset, S.kit_sha, S.kit_file, sizeof S.kit_file) != 0; }
    else for (int i = 0; i < S.nmods; i++) if (S.mods[i].ver[0]) {
        any = 1;
        if (download(S.mods[i].url, S.mods[i].asset, S.mods[i].sha, S.mods[i].file, sizeof S.mods[i].file)) { bad = 1; S.mods[i].file[0] = 0; }
    }
    int got = 0; if (!S.kit_ver[0]) for (int i = 0; i < S.nmods; i++) got += S.mods[i].ver[0] && S.mods[i].file[0];   /* mods: the good ones go on */
    int st = !any ? 0 : S.kit_ver[0] ? (bad ? -1 : 2) : (got ? 2 : -1);
    if (st == 2) ready_write();
    pthread_mutex_lock(&mu); S.dl = st; pthread_mutex_unlock(&mu);
}
static void *check_thread(void *arg) {
    (void)arg;
    check_kit();
    if (!S.kit_ver[0]) check_mods();
    char key[400], p[1200]; update_key(key, sizeof key); upath(p, sizeof p, "want.txt");
    char *w = slurp_(p, 4096); int wanted = w && key[0] && !strncmp(w, key, strlen(key)) && (w[strlen(key)] == '\n' || !w[strlen(key)]); free(w);
    pthread_mutex_lock(&mu); S.checked = 1; if (wanted) { S.dl_wanted = 1; consent = 1; } int want = S.dl_wanted || auto_on; pthread_mutex_unlock(&mu);
    if (wanted) dsl_log("update: %s: Update was clicked before: downloading", key);
    if (want && key[0]) download_all();
    thread_on = 0;
    return NULL;
}
static void *dl_thread(void *arg) { (void)arg; download_all(); thread_on = 0; return NULL; }
static int any_update(void) { if (S.kit_ver[0]) return 1; for (int i = 0; i < S.nmods; i++) if (S.mods[i].ver[0]) return 1; return 0; }
static void start_dl(void) { pthread_t t; thread_on = 1; if (pthread_create(&t, NULL, dl_thread, NULL)) thread_on = 0; else pthread_detach(t); }

/* ------------------------------------------------------------------ the starter's results (title) */
static void moved_old(const char *file, const char *why) {
    char p[1200], q[1400], ts[32]; upath(p, sizeof p, file); stamp(ts, sizeof ts);
    snprintf(q, sizeof q, "%s/old/%s-%s-%s", udir, why, ts, file);
    if (dsl_uos_move(p, q)) dsl_log("update: cannot move %s", p);
}
static void title_results(void) {
    char p[1200]; char *b;
    if (confirm_label[0]) {                                  /* the update this start runs reached the title */
        upath(p, sizeof p, "pending.txt"); b = slurp_(p, 1 << 20);
        char label[200] = ""; const char *l = b ? strstr(b, "LABEL\t") : NULL;
        if (l) { l += 6; size_t k = strcspn(l, "\r\n"); snprintf(label, sizeof label, "%.*s", (int)(k < sizeof label ? k : sizeof label - 1), l); }
        free(b);
        if (label[0] && !strcmp(label, confirm_label)) {
            moved_old("pending.txt", "ok");
            if (!strncmp(label, "mods:", 5)) { line_kind = LN_UPDATED_MODS; snprintf(line_arg, sizeof line_arg, "%s", label + 5); }
            else { line_kind = LN_UPDATED; snprintf(line_arg, sizeof line_arg, "%s", label[0] == 'v' ? label + 1 : label); }
            dsl_log("update: %s confirmed at the title", label);
        } else dsl_log("update: DSML_UPDATE=%s but pending.txt says \"%s\": nothing to confirm", confirm_label, label);
        confirm_label[0] = 0;
    }
    upath(p, sizeof p, "notice.txt");
    if ((b = slurp_(p, 2000))) {                             /* the starter's one line (install failed, rollback) */
        b[strcspn(b, "\r\n")] = 0;
        if (b[0]) { line_kind = LN_NOTICE; snprintf(line_arg, sizeof line_arg, "%s", b); dsi_utf8_fix_end(line_arg); dsl_log("update: starter notice shown: %s", b); }
        free(b); moved_old("notice.txt", "shown");
    }
}

/* ------------------------------------------------------------------ title corner */
static uintptr_t c_tog, c_txt, c_btn;                      /* DSLObj kept as plain pointers (re-made when gone) */
static int c_btn_on; static char c_last_line[600], c_btn_label[160]; static unsigned c_gen;
static void toggle_change(void *ud, DSLObj w) {
    (void)ud; const DSLUi *u = dsl_ui_kit(); if (!u) return;
    auto_on = u->toggle_get(w) != 0; settings_save();
    dsl_log("update: auto-update %s", auto_on ? "ON" : "OFF");
    if (auto_on && S.checked && any_update() && S.dl == 0 && !thread_on) start_dl();
}
static void button_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    char key[400], p[1200], t[500]; update_key(key, sizeof key); upath(p, sizeof p, "want.txt"); snprintf(t, sizeof t, "%s\n", key);
    if (write_file(p, t)) dsl_log("update: cannot write %s", p);
    pthread_mutex_lock(&mu); int st = S.dl; S.dl_wanted = 1; consent = 1; pthread_mutex_unlock(&mu);
    dsl_log("update: \"%s\" clicked: installed at the next start", c_btn_label);
    if (st == 2) ready_write();                               /* downloaded while it was on: mark it */
    else if ((st == 0 || st == -1) && !thread_on) start_dl();
}
static void corner_frame(void) {
    const DSLUi *u = dsl_ui_kit(); if (!u) return;
    int show = dsl_ui_title_shown() && dsl_reg_scene() == 1 && dsl_soft_title_idle();   /* the idle title only: never under a loading screen */
    DSLObj tog = (DSLObj)c_tog, txt = (DSLObj)c_txt, btn = (DSLObj)c_btn;
    if (tog && c_gen != dsi_generation()) {                 /* a language change: made again (label, the new language's font) */
        u->destroy(tog); if (txt) u->destroy(txt); if (btn) u->destroy(btn);
        c_tog = c_txt = c_btn = 0; tog = txt = btn = NULL; c_btn_label[0] = 0;
    }
    if (!show) { if (tog) u->set_active(tog, 0); if (txt) u->set_active(txt, 0); if (btn) u->set_active(btn, 0); return; }
    if (!tog) {
        c_gen = dsi_generation();
        tog = u->toggle(NULL, 0, 0, 260, TR("Auto-update"), auto_on, toggle_change, NULL);
        txt = u->text(NULL, 0, 0, 520, 30, "", 16, DSL_TXT_RIGHT, 0xE8DCC0FF);
        btn = u->button(NULL, 0, 0, 220, 40, TR("Update"), button_click, NULL);
        if (!tog || !txt || !btn) return;
        dsl_ui_place_tr(tog, 24, 56, 260, 40); dsl_ui_place_tr(txt, 24, 100, 520, 30); dsl_ui_place_tr(btn, 24, 100, 220, 40);   /* below the game's copyright line */
        c_tog = (uintptr_t)tog; c_txt = (uintptr_t)txt; c_btn = (uintptr_t)btn; c_last_line[0] = 0; c_btn_on = -1;
        dsl_log("update: title corner shown (auto-update %s)", auto_on ? "on" : "off");
    }
    u->set_active(tog, 1);
    /* the button: Off, an update found, not clicked yet */
    pthread_mutex_lock(&mu);
    int st = S.dl, avail = S.checked && any_update(), clicked = consent || S.dl_wanted, rk = ready_kind; char kv[32];
    snprintf(kv, sizeof kv, "%s", S.kit_ver);
    pthread_mutex_unlock(&mu);
    int want_btn = !auto_on && avail && !clicked && st != 1 && line_kind == LN_NONE;
    char bl[160]; if (kv[0]) snprintf(bl, sizeof bl, TR("Update to %s"), kv); else snprintf(bl, sizeof bl, "%s", TR("Update mods"));
    dsi_utf8_fix_end(bl);
    if (want_btn && strcmp(bl, c_btn_label)) { snprintf(c_btn_label, sizeof c_btn_label, "%s", bl); u->set_text(btn, bl); }
    if (want_btn != c_btn_on) { u->set_active(btn, want_btn); c_btn_on = want_btn; if (want_btn) dsl_log("update: corner button \"%s\" shown", bl); }
    char l[600] = "";
    if (line_kind == LN_NOTICE) snprintf(l, sizeof l, "%s", line_arg);
    else if (line_kind == LN_UPDATED) snprintf(l, sizeof l, TR("Updated to %s"), line_arg);
    else if (line_kind == LN_UPDATED_MODS) snprintf(l, sizeof l, TR("Updated: %s"), line_arg);
    else if (st == 1 && clicked) { if (kv[0]) snprintf(l, sizeof l, TR("Downloading %s..."), kv); else snprintf(l, sizeof l, "%s", TR("Downloading mod updates...")); }
    else if (st == 2 && (auto_on || clicked) && rk == 1) snprintf(l, sizeof l, TR("%s installs the next time you start the game."), kv);
    else if (st == 2 && (auto_on || clicked) && rk == 2) snprintf(l, sizeof l, "%s", TR("Mod updates install the next time you start the game."));
    else if (st == -1 && clicked) snprintf(l, sizeof l, "%s", TR("The download failed. The game tries again next time."));
    dsi_utf8_fix_end(l);
    if (strcmp(l, c_last_line)) { snprintf(c_last_line, sizeof c_last_line, "%s", l); u->set_text(txt, l); if (l[0]) dsl_log("update: corner line: %s", l); }
    u->set_active(txt, l[0] != 0 && !want_btn);
}

/* test (uikit-demo driver, dlsym): the corner's Auto-update switch, NULL before the title showed it */
__attribute__((visibility("default"))) DSLObj dsl_update_test_toggle(void) { return (DSLObj)c_tog; }

/* ------------------------------------------------------------------ entry points (dsl_reg.c) */
void dsl_update_start(void) {
    const char *off = getenv("DSL_UPDATE");
    const char *r = dsl_uos_root(); if (!r || !*r) { dsl_log("update: no ROOT in this start: auto-update off"); return; }
    snprintf(root, sizeof root, "%s", r); snprintf(udir, sizeof udir, "%s/update", root); dsl_uos_mkdirs(udir);
    char od[1200]; upath(od, sizeof od, "old"); dsl_uos_mkdirs(od);
    settings_load();
    if (dsl_uos_release(S.have, sizeof S.have)) S.have[0] = 0;
    if (S.have[0]) setenv("DSML_RELEASE", S.have, 1);            /* the multiplayer mod names it when the mod sets differ */
    { const char *e = getenv("DSML_UPDATE"); if (e && *e) snprintf(confirm_label, sizeof confirm_label, "%s", e); }
    enabled = 1;
    dsl_log("update: release %s, auto-update %s, ROOT %s%s", S.have[0] ? S.have : "unknown", auto_on ? "on" : "off", root,
            confirm_label[0] ? " (this start runs a new update)" : "");
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
    static int results_done;
    if (!results_done && dsl_reg_scene() == 1 && !dsl_soft_busy()) { results_done = 1; title_results(); }
    corner_frame();
    { const char *t = getenv("DSL_UPDATE_TEST"); static int tick;                       /* test: click the button by file */
      if (t && *t == '1' && ++tick % 30 == 0 && c_btn_on == 1) {
          char p[1200]; upath(p, sizeof p, "test_click");
          if (dsl_uos_exists(p)) { char q[1300]; snprintf(q, sizeof q, "%s.done", p); remove(q); dsl_uos_move(p, q); button_click(NULL, NULL); } } }
}
