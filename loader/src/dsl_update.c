// dsl_update.c - auto-update of the loader kit and its mods (owner 2026-10-16: "auto-updates for the mod loader and its
// mods, switchable on the title screen, top-right corner").
//
// Check (a thread, once per cold start; never in B of a soft restart): GitHub releases of AmfiLogos/ds_modloader
// (the loader kit: tag v0.1.N, asset Dungeon-Settlers-Modded-Mac.dmg / .zip / -Windows.zip, dsl_uos_kit_asset) and of
// every installed mod's own repo (tag <id>-v0.1.N, asset <id>-0.1.N.zip, the packages of loader/modules/package_mods.sh).
// research/version_table.md (ds_multiplayer): each component has its own number. Newer than the installed release
// (RELEASE file) / the mod.json version, not rolled back before (ROOT/update/bad.txt).
// Every download is checked against its sha256 from the release's SIGNED manifest (owner 2026-10-18: signed updates):
// release-manifest.json + .sig, ed25519 by a key in dsl_sig.c, naming the same repo, tag, id and version (manifest_load).
// GitHub's own "digest" is not trusted any more (whoever holds the GitHub account could change both). No manifest, a bad
// signature or any mismatch = no install (log line). Network failure, rate limit, bad JSON: silent (log line), next start
// tries again.
// Title corner (top right, only at the title while its menu shows): "Auto-update mod loader" switch (default on; ROOT/update/
// settings.json, per machine), the version line, one short status line and one button under it (owner 2026-10-20):
//   Found, not downloaded ("Update 0.1.N is available."): "Download update" downloads it now, in both modes.
//   On:  download while the game runs (sha256 checked), then ROOT/update/ready.txt; the next start installs it in the
//        starter before the game opens (start/dsl_update_start.c: a small window with a progress bar, no prompt).
//   Off: no download; "Download update" downloads and marks it (ready.txt CONSENT, want.txt).
//   A failed download: the line says so and "Download update" tries again.
//   Downloaded ("0.1.N installs the next time you start the game."): "Restart and update now" starts the starter
//        (dsl_sos_start_starter: the Mac app's stub / the Windows starter, DSML_RESTART_WAIT=<this pid>) and quits this
//        game; the starter waits until the game ended, installs as at any start (kit and mods before the game loads
//        them), then starts the game. Only at the idle title (no world loaded, nothing to save), not while a multiplayer
//        session is busy, and only when this start has a starter (DSML_APP / DSML_ROOT). It could not start: one line.
// Nothing here moves an installed file. A start that runs a just-installed update has DSML_UPDATE=<label> (the
// starter): at the title it confirms (ROOT/update/pending.txt -> update/old) and shows "Updated to 0.1.N"; an update
// that never gets there is rolled back by the starter after a second try. ROOT/update/notice.txt (the starter's one
// line: an install failed, a rollback) is shown in the corner once.
// Kit first: a kit update carries the mods; mod updates wait for the next start.
// Kit only (dsl_update_start_kit_only): the game build is unknown / not the pinned one (dsl_main.c dsl_core_start), so
// no mod, no hook and no title: the same kit check and download (ready.txt), no mod check, no title corner. A kit the
// starter just installed is confirmed at that start (its loader does nothing more, so nothing of it can crash later).
// Test only: DSL_UPDATE_API=<base URL> (a local release server instead of https://api.github.com), DSL_UPDATE=0 (off),
// DSL_UPDATE_TEST=1 (ROOT/update/test_click = a click on "Download update", ROOT/update/test_restart = a click on
// "Restart and update now").
#include "dsl_internal.h"
#include "dsl_menu.h"
#include "dsl_update_os.h"
#include "dsl_soft_os.h"
#include "../sdk/dsl_sdk.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int dsl_reg_scene(void);                                   /* dsl_reg.c: 1 title, 2 world */
int dsl_soft_wanted(void);
void dsl_soft_prewarm_stop(void);
int dsl_soft_prewarm_state(void);
int dsl_ui_title_shown(void);                              /* dsl_ui.c: title menu column on screen */
void dsl_ui_place_tr(DSLObj w, float right, float top, float wd, float h);

#define KIT_REPO "AmfiLogos/ds_modloader"
static const struct { const char *id, *repo; } MOD_REPOS[] = {
    { "multiplayer", "AmfiLogos/ds_multiplayer" }, { "custom-modes", "AmfiLogos/ds_custommodes" },
    { "gcd-cdr", "AmfiLogos/ds_modloader" }, { "hell-difficulty", "AmfiLogos/ds_modloader" },
    { "zetu-suite", "AmfiLogos/ds_modloader" }, { "sound-fix", "AmfiLogos/ds_modloader" },
    { "multi-bar", "AmfiLogos/ds_modloader" }, { "weapon-slot", "AmfiLogos/ds_modloader" },
    { "locked-slots", "AmfiLogos/ds_modloader" },
    { "dsmp-bridge", "AmfiLogos/ds_multiplayer" },     /* hidden library for BepInEx mods: installed with the mods that need it */
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
static int auto_on = 1, enabled, thread_on, kit_only;
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
static JNode *releases_n(const char *repo, const char *tag_for_file, int per_page) {
    const char *api = getenv("DSL_UPDATE_API"); if (!api || !*api) api = "https://api.github.com";
    char url[600], f[1200], cache[1100];
    snprintf(url, sizeof url, "%s/repos/%s/releases?per_page=%d", api, repo, per_page);
    upath(cache, sizeof cache, "cache"); dsl_uos_mkdirs(cache);
    snprintf(f, sizeof f, "%s/%s.json", cache, tag_for_file);
    if (dsl_uos_fetch(url, f)) { dsl_log("update: %s: no answer (network or rate limit); next start tries again", repo); return NULL; }
    char *b = slurp_(f, 4 << 20); JNode *j = b ? json_parse(b) : NULL; free(b);
    if (!j || j->t != JN_ARR) { dsl_log("update: %s: unexpected answer", repo); json_free(j); return NULL; }
    return j;
}
static JNode *releases(const char *repo, const char *tag_for_file) { return releases_n(repo, tag_for_file, 15); }
/* the release's signed manifest (tools/release_sign/sign_release.sh): release-manifest.json + release-manifest.json.sig
   (base64 ed25519 signature of the exact bytes) by a trusted key (dsl_sig.c), and it names this repo, this release's tag,
   id ("kit" or the mod id) and version ver, the tag's own number: an old signed manifest put under another tag, repo or
   mod is refused. NULL = not usable (logged): nothing of this release is installed. tmp = file stem in ROOT/update/cache
   (each thread its own). */
#define MANIFEST "release-manifest.json"
static JNode *manifest_load(JNode *rel, const char *repo, const char *id, const char *ver, const char *tmp) {
    const char *tag = json_str(rel, "tag_name"), *mu_ = NULL, *su = NULL;
    JNode *as = json_get(rel, "assets");
    for (JNode *a = as ? as->child : NULL; a; a = a->next) {
        const char *n = json_str(a, "name"), *u = json_str(a, "browser_download_url"); if (!n || !u) continue;
        if (!strcmp(n, MANIFEST)) mu_ = u; else if (!strcmp(n, MANIFEST ".sig")) su = u;
    }
    if (!tag || !mu_ || !su) { dsl_log("update: %s %s: no signed manifest: not installed", repo, tag ? tag : "?"); return NULL; }
    char mf[1200], sf[1200], t[200];
    snprintf(t, sizeof t, "cache/%s.json", tmp); upath(mf, sizeof mf, t);
    snprintf(t, sizeof t, "cache/%s.sig", tmp); upath(sf, sizeof sf, t);
    if (dsl_uos_fetch(mu_, mf) || dsl_uos_fetch(su, sf)) { dsl_log("update: %s %s: manifest download failed; next start tries again", repo, tag); return NULL; }
    char *m = NULL, *sg = slurp_(sf, 400); size_t n = 0;
    FILE *f = fopen(mf, "rb");
    if (f) { m = malloc((64 << 10) + 1); n = m ? fread(m, 1, 64 << 10, f) : 0; if (m) m[n] = 0; fclose(f); }
    int key = m && sg ? dsl_sig_check(m, n, sg) : -1;
    free(sg);
    if (key < 0) { dsl_log("update: %s %s: the manifest signature is NOT valid: not installed", repo, tag); free(m); return NULL; }
    JNode *j = json_parse(m); free(m);
    JNode *fmt = json_get(j, "format");
    const char *mr = json_str(j, "repo"), *mt = json_str(j, "tag"), *mi = json_str(j, "id"), *mv = json_str(j, "version");
    char tv[32]; vclean(tag, tv, sizeof tv);
    if (!fmt || fmt->t != JN_NUM || fmt->num != 1 || !mr || strcmp(mr, repo) || !mt || strcmp(mt, tag) || !mi || strcmp(mi, id)
        || !mv || strcmp(mv, ver) || strcmp(tv, ver)) {
        dsl_log("update: %s %s: signed manifest is for %s %s %s %s, not %s %s %s %s: not installed", repo, tag, mr ? mr : "?",
                mt ? mt : "?", mi ? mi : "?", mv ? mv : "?", repo, tag, id, ver);
        json_free(j); return NULL;
    }
    dsl_log("update: %s %s: manifest signed by trusted key %d", repo, tag, key);
    return j;
}
/* the sha256 of asset in a checked manifest; it must be listed for this OS ("any" = every OS). 0 = ok */
static int manifest_sha(JNode *m, const char *asset, char out[65]) {
    JNode *fs = json_get(m, "files");
    for (JNode *e = fs && fs->t == JN_ARR ? fs->child : NULL; e; e = e->next) {
        const char *n = json_str(e, "name"), *p = json_str(e, "platform"), *h = json_str(e, "sha256");
        if (!n || strcmp(n, asset) || !p || (strcmp(p, "any") && strcmp(p, dsl_uos_platform())) || !h || strlen(h) != 64) continue;
        size_t k = 0; while (k < 64 && ((h[k] >= '0' && h[k] <= '9') || (h[k] >= 'a' && h[k] <= 'f'))) k++;
        if (k == 64) { memcpy(out, h, 65); return 0; }
    }
    return -1;
}
static int usable(JNode *rel) {
    JNode *d = json_get(rel, "draft"), *p = json_get(rel, "prerelease");
    return !(d && d->t == JN_BOOL && d->b) && !(p && p->t == JN_BOOL && p->b);
}
static void check_kit(void) {
    JNode *rs = releases(KIT_REPO, "kit"); if (!rs) return;
    char best[32] = "", url[1024] = "", sha[65] = "", an[160] = ""; JNode *br = NULL;
    for (JNode *r = rs->child; r; r = r->next) {               /* the newest release above the installed one (never lower) */
        const char *tag = json_str(r, "tag_name"); if (!tag || !usable(r) || tag[0] != 'v' || tag[1] < '0' || tag[1] > '9') continue;
        char v[32]; vclean(tag, v, sizeof v);
        if (vcmp(v, S.have) <= 0 || (best[0] && vcmp(v, best) <= 0)) continue;
        char key[64]; snprintf(key, sizeof key, "kit %s", v); if (is_bad(key)) continue;
        JNode *as = json_get(r, "assets"); int has = 0;
        for (JNode *a = as ? as->child : NULL; a && !has; a = a->next) has = json_str(a, "name") && dsl_uos_kit_asset(json_str(a, "name"));
        if (has) { snprintf(best, sizeof best, "%s", v); br = r; }
    }
    JNode *m = br ? manifest_load(br, KIT_REPO, "kit", best, "kit_manifest") : NULL;   /* then its signed manifest */
    JNode *as = m ? json_get(br, "assets") : NULL;
    for (JNode *a = as ? as->child : NULL; a; a = a->next) {
        const char *n = json_str(a, "name"), *u = json_str(a, "browser_download_url");
        if (!n || !u || !dsl_uos_kit_asset(n)) continue;
        if (manifest_sha(m, n, sha)) { dsl_log("update: kit %s: %s is not in its signed manifest, skipped", best, n); continue; }
        snprintf(url, sizeof url, "%s", u); snprintf(an, sizeof an, "%s", n);
        break;
    }
    if (!url[0]) best[0] = 0;
    json_free(m); json_free(rs);
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
            /* the newest valid release of the mod (tag "<id>-v<version>", asset "<id>-<version>.zip"; old bundle-v0.1.x
               releases hold mod zips of the old numbers, multiplayer-0.10.0.zip ..., that must never count) */
            char top[32] = "", url[1024] = "", sha[65] = "", an[160] = ""; int own = 0; JNode *tr = NULL;
            size_t il = strlen(u->id);
            for (JNode *r = rs->child; r; r = r->next) {
                if (!usable(r)) continue;
                JNode *as = json_get(r, "assets");
                for (JNode *a = as ? as->child : NULL; a; a = a->next) {
                    const char *n = json_str(a, "name"), *url_ = json_str(a, "browser_download_url");
                    size_t nl = n ? strlen(n) : 0;
                    if (!n || !url_ || nl < il + 6 || strncmp(n, u->id, il) || n[il] != '-' || strcmp(n + nl - 4, ".zip")) continue;
                    char v[32]; snprintf(v, sizeof v, "%.*s", (int)(nl - il - 5), n + il + 1);
                    char want_tag[160]; snprintf(want_tag, sizeof want_tag, "%s-v%s", u->id, v);
                    const char *tag = json_str(r, "tag_name");
                    if (v[0] < '0' || v[0] > '9' || !tag || strcmp(tag, want_tag)) continue;
                    if (!vcmp(v, u->have)) own = 1;
                    if (top[0] && vcmp(v, top) <= 0) continue;
                    char key[128]; snprintf(key, sizeof key, "%s %s", u->id, v); if (is_bad(key)) continue;
                    snprintf(top, sizeof top, "%s", v); snprintf(url, sizeof url, "%s", url_); snprintf(an, sizeof an, "%s", n); tr = r;
                }
            }
            /* newer: the normal update. Repair: the installed version has no release of its own and is above every valid
               one (an old bundle-era build the updater of v0.1.7-v0.1.9 installed, multiplayer 0.10.0 ...): back to the
               newest valid release. Only when the first two numbers differ (0.10.x vs 0.1.x): a release deleted on GitHub
               must never make the loader go back to an older signed version (signed updates: never lower) */
            long hv[6], tv[6]; vparse(u->have, hv); vparse(top, tv);
            int newer = top[0] && vcmp(top, u->have) > 0, repair = top[0] && !own && vcmp(u->have, top) > 0 && (hv[0] != tv[0] || hv[1] != tv[1]);
            if (newer || repair) {                               /* only that release's signed manifest is fetched */
                char tmp[200]; snprintf(tmp, sizeof tmp, "mod_manifest_%s", u->id);
                JNode *m = manifest_load(tr, repo, u->id, top, tmp);
                if (!m || manifest_sha(m, an, sha)) { if (m) dsl_log("update: %s %s: %s is not in its signed manifest", u->id, top, an); newer = repair = 0; }
                json_free(m);
            }
            pthread_mutex_lock(&mu);
            if (newer || repair) { snprintf(u->ver, sizeof u->ver, "%s", top); snprintf(u->url, sizeof u->url, "%s", url);
                                   snprintf(u->sha, sizeof u->sha, "%s", sha); snprintf(u->asset, sizeof u->asset, "%s", an); }
            pthread_mutex_unlock(&mu);
            if (newer) dsl_log("update: mod %s: installed %s, newer %s (%s)", u->id, u->have, top, repo);
            if (repair) dsl_log("update: mod %s: installed %s has no release of its own: back to %s (%s)", u->id, u->have, top, repo);
        }
        json_free(rs);
    }
}
/* download one file to ROOT/update/dl/<asset>, sha256 checked; 0 = ok */
static int download_to(const char *url, const char *asset, const char *sha, const char *sub, char *out, size_t n) {
    char dl[1100], part[1300]; upath(dl, sizeof dl, sub); dsl_uos_mkdirs(dl);
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
static int download(const char *url, const char *asset, const char *sha, char *out, size_t n) { return download_to(url, asset, sha, "dl", out, n); }

/* ------------------------------------------------------------------ for "Get the host's mods" (dsl_hostmods.c, any thread) */
const char *dsl_update_mod_repo(const char *id) {
    for (size_t k = 0; id && k < sizeof MOD_REPOS / sizeof MOD_REPOS[0]; k++) if (!strcmp(MOD_REPOS[k].id, id)) return MOD_REPOS[k].repo;
    return NULL;
}
int dsl_update_ready(void) { return udir[0] != 0; }
/* the exact official release of a mod version: tag "<id>-v<ver>" with the asset "<id>-<ver>.zip" and its sha256 from the
   release's signed manifest. 0 = found, 1 = no such release (or no valid signed manifest), -1 = no answer / not an official id */
int dsl_update_find_mod_release(const char *id, const char *ver, char *url, size_t ucap, char *asset, size_t acap, char sha[65]) {
    const char *repo = dsl_update_mod_repo(id); if (!repo || !udir[0] || !ver || !*ver) return -1;
    char cf[200]; snprintf(cf, sizeof cf, "hostmods_%s", repo); for (char *c = cf; *c; c++) if (*c == '/') *c = '_';
    JNode *rs = releases_n(repo, cf, 100); if (!rs) return -1;
    char want_tag[160], want_asset[160]; snprintf(want_tag, sizeof want_tag, "%s-v%s", id, ver); snprintf(want_asset, sizeof want_asset, "%s-%s.zip", id, ver);
    int r = 1;
    for (JNode *rel = rs->child; rel && r == 1; rel = rel->next) {
        const char *tag = json_str(rel, "tag_name"); if (!tag || strcmp(tag, want_tag) || !usable(rel)) continue;
        JNode *as = json_get(rel, "assets");
        for (JNode *a = as ? as->child : NULL; a; a = a->next) {
            const char *n = json_str(a, "name"), *u = json_str(a, "browser_download_url");
            if (!n || !u || strcmp(n, want_asset)) continue;
            /* the exact version the host runs, signed: it may be older than the guest's own (the player's click) */
            JNode *m = manifest_load(rel, repo, id, ver, "hostmods_manifest"); char s[65];
            int ok = m && !manifest_sha(m, n, s); json_free(m);
            if (!ok) { dsl_log("update: %s %s: %s has no valid signed manifest: not used", id, ver, n); break; }
            snprintf(url, ucap, "%s", u); snprintf(asset, acap, "%s", n); memcpy(sha, s, 65); r = 0; break;
        }
    }
    json_free(rs);
    if (r == 1) dsl_log("update: %s %s: no release %s with %s in %s", id, ver, want_tag, want_asset, repo);
    return r;
}
/* download into ROOT/update/<sub>/<asset>, sha256 checked (a wrong one is never kept under the asset's name); 0 = ok */
int dsl_update_fetch(const char *url, const char *asset, const char *sha, const char *sub, char *out, size_t n) {
    if (!udir[0]) return -1;
    return download_to(url, asset, sha, sub, out, n);
}
const char *dsl_update_dir(void) { return udir; }
int dsl_update_vcmp(const char *a, const char *b) { return vcmp(a, b); }
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
    if (!S.kit_ver[0] && !kit_only) check_mods();
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
static void confirm_pending(void) {                       /* the update this start runs reached the title */
    char p[1200]; char *b;
    if (confirm_label[0]) {
        upath(p, sizeof p, "pending.txt"); b = slurp_(p, 1 << 20);
        char label[200] = ""; const char *l = b ? strstr(b, "LABEL\t") : NULL;
        if (l) { l += 6; size_t k = strcspn(l, "\r\n"); snprintf(label, sizeof label, "%.*s", (int)(k < sizeof label ? k : sizeof label - 1), l); }
        free(b);
        if (label[0] && !strcmp(label, confirm_label)) {
            moved_old("pending.txt", "ok");
            if (!strncmp(label, "mods:", 5)) { line_kind = LN_UPDATED_MODS; snprintf(line_arg, sizeof line_arg, "%s", label + 5); }
            else { line_kind = LN_UPDATED; snprintf(line_arg, sizeof line_arg, "%s", label[0] == 'v' ? label + 1 : label); }
            dsl_log("update: %s confirmed %s", label, kit_only ? "at the start (mods are off: no title)" : "at the title");
        } else dsl_log("update: DSML_UPDATE=%s but pending.txt says \"%s\": nothing to confirm", confirm_label, label);
        confirm_label[0] = 0;
    }
}
static void title_results(void) {
    char p[1200]; char *b;
    confirm_pending();
    upath(p, sizeof p, "notice.txt");
    if ((b = slurp_(p, 2000))) {                             /* the starter's one line (install failed, rollback) */
        b[strcspn(b, "\r\n")] = 0;
        if (b[0]) { line_kind = LN_NOTICE; snprintf(line_arg, sizeof line_arg, "%s", b); dsi_utf8_fix_end(line_arg); dsl_log("update: starter notice shown: %s", b); }
        free(b); moved_old("notice.txt", "shown");
    }
}

/* ------------------------------------------------------------------ title corner */
static uintptr_t c_tog, c_txt, c_btn, c_ver, c_rbtn;       /* DSLObj kept as plain pointers (re-made when gone) */
static int c_btn_on, c_rbtn_on; static char c_last_line[600], c_btn_label[160]; static unsigned c_gen;
static int restarting, restart_failed;                     /* "Restart and update now": 1 waiting for the starter, 2 quitting / it failed */
static time_t restart_t0;
static void toggle_change(void *ud, DSLObj w) {
    (void)ud; const DSLUi *u = dsl_ui_kit(); if (!u) return;
    auto_on = u->toggle_get(w) != 0; settings_save();
    dsl_log("update: auto-update %s", auto_on ? "ON" : "OFF");
    if (auto_on && S.checked && any_update() && S.dl == 0 && !thread_on) start_dl();
}
static void button_click(void *ud, DSLObj w) {             /* "Download update" */
    (void)ud; (void)w;
    char key[400], p[1200], t[500]; update_key(key, sizeof key); upath(p, sizeof p, "want.txt"); snprintf(t, sizeof t, "%s\n", key);
    if (write_file(p, t)) dsl_log("update: cannot write %s", p);
    pthread_mutex_lock(&mu); int st = S.dl; S.dl_wanted = 1; consent = 1; pthread_mutex_unlock(&mu);
    dsl_log("update: \"%s\" clicked (%s)", c_btn_label, st == 2 ? "downloaded already: marked" : "download now");
    if (st == 2) ready_write();                               /* downloaded while it was on: mark it */
    else if ((st == 0 || st == -1) && !thread_on) start_dl();
}
static int restart_ok(void) {                               /* the restart button may show (and act) now */
    return !restarting && !restart_failed && !kit_only && dsl_reg_scene() == 1 && !dsl_soft_busy() && !dsl_busy_reason()
           && dsl_sos_starter_known();
}
static void restart_click(void *ud, DSLObj w) {            /* "Restart and update now" */
    (void)ud; (void)w;
    pthread_mutex_lock(&mu); int st = S.dl; pthread_mutex_unlock(&mu);
    if (st != 2 || !restart_ok()) { dsl_log("update: \"Restart and update now\" refused (download %d, scene %d, busy %s)", st, dsl_reg_scene(), dsl_busy_reason() ? dsl_busy_reason() : "no"); return; }
    if (!auto_on && !(consent || S.dl_wanted)) { consent = 1; ready_write(); }   /* the click is the consent */
    dsl_log("update: \"Restart and update now\" clicked: the starter installs the update, then starts the game");
    char p[1200]; upath(p, sizeof p, "restart_ack");
    if (dsl_uos_exists(p)) { char ts[32], q[1400]; stamp(ts, sizeof ts); snprintf(q, sizeof q, "%s/old/restart_ack-%s", udir, ts); dsl_uos_move(p, q); }
    if (dsl_sos_start_starter()) { restart_failed = 1; return; }
    restarting = 1; restart_t0 = time(NULL);                 /* restart_frame: quits when the starter answers */
}
/* the starter writes ROOT/update/restart_ack (this pid) once it waits for this game: then quit. No answer in 20 s: the
   corner comes back with one line (the starter, if it starts later, finds this game still running and starts nothing) */
static void restart_frame(void) {
    if (restarting != 1) return;
    char p[1200], *b; upath(p, sizeof p, "restart_ack");
    long pid = (b = slurp_(p, 64)) ? atol(b) : 0; free(b);
    if (pid == (long)getpid()) {
        restarting = 2;
        if (dsl_soft_prewarm_state()) dsl_soft_prewarm_stop();   /* a hidden prewarmed game never outlives this one */
        void (*quit)(const void *) = (void (*)(const void *))dsl_find_method_address("UnityEngine", "Application", "Quit", 0);
        dsl_log("update: restart: the starter waits: this game quits now (Application.Quit %s)", quit ? "found" : "not found: exit");
        if (quit) quit(NULL); else exit(0);
    } else if (time(NULL) - restart_t0 > 20) {
        restarting = 0; restart_failed = 1;
        dsl_log("update: restart: the starter did not answer in 20 s: this game goes on");
    }
}
static void corner_frame(void) {
    const DSLUi *u = dsl_ui_kit(); if (!u) return;
    int show = dsl_ui_title_shown() && dsl_reg_scene() == 1 && dsl_soft_title_idle();   /* the idle title only: never under a loading screen */
    DSLObj tog = (DSLObj)c_tog, txt = (DSLObj)c_txt, btn = (DSLObj)c_btn, ver = (DSLObj)c_ver, rbtn = (DSLObj)c_rbtn;
    if (tog && c_gen != dsi_generation()) {                 /* a language change: made again (label, the new language's font) */
        u->destroy(tog); if (txt) u->destroy(txt); if (btn) u->destroy(btn); if (ver) u->destroy(ver); if (rbtn) u->destroy(rbtn);
        c_tog = c_txt = c_btn = c_ver = c_rbtn = 0; tog = txt = btn = ver = rbtn = NULL; c_btn_label[0] = 0;
    }
    if (!show || restarting) {
        if (tog) u->set_active(tog, 0); if (txt) u->set_active(txt, 0); if (btn) u->set_active(btn, 0); if (ver) u->set_active(ver, 0);
        if (rbtn) u->set_active(rbtn, 0);
        c_btn_on = c_rbtn_on = tog ? 0 : -1; return;
    }
    if (!tog) {
        c_gen = dsi_generation();
        tog = u->toggle(NULL, 0, 0, 380, TR("Auto-update mod loader"), auto_on, toggle_change, NULL);   /* owner 2026-10-17: says what it updates */
        txt = u->text(NULL, 0, 0, 520, 30, "", 16, DSL_TXT_RIGHT | DSL_TXT_WRAP, 0xE8DCC0FF);
        btn = u->button(NULL, 0, 0, 260, 40, TR("Download update"), button_click, NULL);
        rbtn = u->button(NULL, 0, 0, 340, 40, TR("Restart and update now"), restart_click, NULL);
        if (!tog || !txt || !btn || !rbtn) return;
        /* the installed kit release, so two players can see they have the same one (owner 2026-10-20). The product name
           is not translated. Unknown release (dev build, no RELEASE file): no line. */
        if (S.have[0]) { char v[96]; snprintf(v, sizeof v, "Dungeon Settlers Modded v%s", S.have); ver = u->text(NULL, 0, 0, 520, 24, v, 14, DSL_TXT_RIGHT, 0xD8CCB0D0); }
        /* below the game's copyright line: switch, version, the status line, then its button */
        dsl_ui_place_tr(tog, 24, 56, 380, 40); if (ver) dsl_ui_place_tr(ver, 24, 98, 520, 24);
        dsl_ui_place_tr(txt, 24, 126, 520, 30); dsl_ui_place_tr(btn, 24, 160, 260, 40); dsl_ui_place_tr(rbtn, 24, 160, 340, 40);
        c_tog = (uintptr_t)tog; c_txt = (uintptr_t)txt; c_btn = (uintptr_t)btn; c_ver = (uintptr_t)ver; c_rbtn = (uintptr_t)rbtn;
        c_last_line[0] = 0; c_btn_on = c_rbtn_on = -1;
        snprintf(c_btn_label, sizeof c_btn_label, "%s", TR("Download update"));
        if (ver) dsl_log("update: title version line: Dungeon Settlers Modded v%s", S.have);
        dsl_log("update: title corner shown (auto-update %s)", auto_on ? "on" : "off");
    }
    u->set_active(tog, 1); if (ver) u->set_active(ver, 1);
    pthread_mutex_lock(&mu);
    int st = S.dl, avail = S.checked && any_update(), clicked = consent || S.dl_wanted, rk = ready_kind; char kv[32];
    snprintf(kv, sizeof kv, "%s", S.kit_ver);
    pthread_mutex_unlock(&mu);
    int ready = st == 2 && (auto_on || clicked);              /* the next start installs it */
    /* "Download update": found and not downloaded yet (or downloaded while Auto-update was on, then turned off), or the
       download failed; never while it runs */
    int want_btn = avail && line_kind == LN_NONE && !thread_on && (st == 0 || st == -1 || (st == 2 && !ready));
    int want_r = avail && line_kind == LN_NONE && ready && restart_ok();
    char l[600] = "";
    if (line_kind == LN_NOTICE) snprintf(l, sizeof l, "%s", line_arg);
    else if (line_kind == LN_UPDATED) snprintf(l, sizeof l, TR("Updated to %s"), line_arg);
    else if (line_kind == LN_UPDATED_MODS) snprintf(l, sizeof l, TR("Updated: %s"), line_arg);
    else if (!avail) l[0] = 0;
    else if (st == 1) { if (kv[0]) snprintf(l, sizeof l, TR("Downloading %s..."), kv); else snprintf(l, sizeof l, "%s", TR("Downloading mod updates...")); }
    else if (ready && restart_failed) snprintf(l, sizeof l, "%s", TR("The game could not restart. The update installs the next time you start the game."));
    else if (ready && rk == 1) snprintf(l, sizeof l, TR("%s installs the next time you start the game."), kv);
    else if (ready && rk == 2) snprintf(l, sizeof l, "%s", TR("Mod updates install the next time you start the game."));
    else if (st == -1) snprintf(l, sizeof l, "%s", TR("The download failed. The game tries again next time."));
    else if (kv[0]) snprintf(l, sizeof l, TR("Update %s is available."), kv);
    else snprintf(l, sizeof l, "%s", TR("Mod updates are available."));
    dsi_utf8_fix_end(l);
    if (strcmp(l, c_last_line)) {
        snprintf(c_last_line, sizeof c_last_line, "%s", l); u->set_text(txt, l); if (l[0]) dsl_log("update: corner line: %s", l);
        /* a long line (the starter's notice, de / ru) wraps: its box grows and the buttons move down under it (it drew
           over them and the version line) */
        float th = l[0] ? u->text_height(txt, 520) : 0; if (th < 30) th = 30;
        dsl_ui_place_tr(txt, 24, 126, 520, th); dsl_ui_place_tr(btn, 24, 130 + th, 260, 40); dsl_ui_place_tr(rbtn, 24, 130 + th, 340, 40);
    }
    u->set_active(txt, l[0] != 0);
    if (want_btn != c_btn_on) { u->set_active(btn, want_btn); c_btn_on = want_btn; if (want_btn) dsl_log("update: corner button \"%s\" shown", c_btn_label); }
    if (want_r != c_rbtn_on) { u->set_active(rbtn, want_r); c_rbtn_on = want_r; if (want_r) dsl_log("update: corner button \"Restart and update now\" shown"); }
}

/* the screen rect (pixels, y up) of the corner's widgets that show now (dsl_rows.c: the Load list's filter panel keeps out
   of it); 0 = none shows */
int dsl_ui_widget_rect(DSLObj o, float r[4]);
int dsl_update_corner_rect(float r[4]) {
    uintptr_t all[5] = { c_tog, c_ver, c_txt, c_btn, c_rbtn }; int n = 0;
    for (int i = 0; i < 5; i++) {
        float q[4]; if (!all[i] || dsl_ui_widget_rect((DSLObj)all[i], q) != 1) continue;
        if (!n++) memcpy(r, q, sizeof q);
        else { if (q[0] < r[0]) r[0] = q[0]; if (q[1] < r[1]) r[1] = q[1]; if (q[2] > r[2]) r[2] = q[2]; if (q[3] > r[3]) r[3] = q[3]; }
    }
    return n > 0;
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
    enabled = !kit_only;                                   /* kit only: no title corner (no frame call either) */
    dsl_log("update: release %s, auto-update %s, ROOT %s%s%s%s", S.have[0] ? S.have : "unknown", auto_on ? "on" : "off", root,
            confirm_label[0] ? " (this start runs a new update)" : "", kit_only ? "; kit only (mods are off this start)" : "",
            dsl_sig_dev() ? "; TEST BUILD: trusts the public dev signing key" : "");
    if (kit_only) confirm_pending();
    if (off && !strcmp(off, "0")) { dsl_log("update: DSL_UPDATE=0: no check"); return; }
    if (dsl_soft_wanted()) return;                         /* B of a soft restart: A checked already */
    for (int i = 0; !kit_only && i < dsl_mods_count() && S.nmods < MAXU; i++) {
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
/* the game build is unknown / not pinned (dsl_main.c): mods are off, but a newer kit may support this build */
void dsl_update_start_kit_only(void) { kit_only = 1; dsl_update_start(); }
/* for the mods-off notice: 1 = this start checks for a newer kit and auto-update installs it; 0 = it does not
   (Auto-update off, DSL_UPDATE=0, no ROOT) */
int dsl_update_will_install(void) {
    const char *off = getenv("DSL_UPDATE"); if (off && !strcmp(off, "0")) return 0;
    const char *r = dsl_uos_root(); if (!r || !*r) return 0;
    char p[1200]; snprintf(p, sizeof p, "%s/update/settings.json", r); char *b = slurp_(p, 4096); int on = 1;
    if (b) { JNode *j = json_parse(b); JNode *a = json_get(j, "auto"); if (a && a->t == JN_BOOL) on = a->b; json_free(j); free(b); }
    return on;
}
void dsl_update_frame(void) {
    if (!enabled) return;
    static int results_done;
    if (!results_done && dsl_reg_scene() == 1 && !dsl_soft_busy()) { results_done = 1; title_results(); }
    corner_frame();
    restart_frame();
    { const char *t = getenv("DSL_UPDATE_TEST"); static int tick;                       /* test: click a button by file */
      if (t && *t == '1' && ++tick % 30 == 0) {
          static const char *f[2] = { "test_click", "test_restart" };
          for (int i = 0; i < 2; i++) {
              char p[1200]; upath(p, sizeof p, f[i]);
              if ((i ? c_rbtn_on : c_btn_on) != 1 || !dsl_uos_exists(p)) continue;
              char q[1300]; snprintf(q, sizeof q, "%s.done", p); remove(q); dsl_uos_move(p, q);
              if (i) restart_click(NULL, NULL); else button_click(NULL, NULL);
              break;
          } } }
}
