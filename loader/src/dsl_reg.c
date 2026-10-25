// Loader core, API 3 services (~/ds_multiplayer/research/MASTER_PLAN.md 2.2 A, flows J and K):
//   - session busy, Mods window tabs, title buttons (the loader's own "Mods"), menu_open
//   - installed mods for the Mods tab (every <mods>/<id>/mod.json, enabled or not), groups, Apply
//   - Apply = <data>/Mods/pending.json + soft restart (dsl_soft.c); the new process makes it active.json once it is on
//     screen (commit only on success; a failed restart leaves active.json as it was)
//   - mods per world: <data>/Mods/worlds.json {"modset": <running key>, "worlds": {guid: key}}; key = the sorted
//     gameplay mod ids joined by "+" ("vanilla" if none), launcher features left out (see dsl_mod_is_feature). A world is registered with the running key the first time it
//     is open in this process; a world with another key: dsl_world.c refuses the load, then this file asks
//     "This world uses: <names>. Load it with them?" and Load = soft restart straight into that save (flow K).
//   - fixes (mod.json "category": "fix", owner 2026-10-24): "On in all worlds" adds a fix to every world's set unless the
//     world turned it off (worlds.json "fix_off"); key_eff gives the set a world plays with (see "fixes" below)
// Everything here runs on the main thread (dsl_frame_hook from the kit's EventSystem.Update tick, or host calls made
// by modules from their own main-thread code).
#include "dsl_internal.h"
#include "dsl_os.h"
#include "dsl_menu.h"
#include "dsl_bie.h"
#include "dsl_ml.h"
#include "dsl_deps.h"
#include "dsl_bugrep.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))

/* ------------------------------------------------------------------ small helpers */
static char *slurp(const char *p, size_t max) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    char *b = malloc(max + 1); size_t n = fread(b, 1, max, f); fclose(f); b[n] = 0; return b;
}
/* temp (unique per process) + every write checked + fsync + rename: a failed write never replaces the old file */
static int put_file(const char *path, const char *text) {
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp-%d", path, (int)getpid());
    FILE *f = fopen(tmp, "w"); if (!f) return -1;
    int bad = fputs(text, f) < 0; bad |= fflush(f) != 0; bad |= fsync(fileno(f)) != 0; bad |= fclose(f) != 0;
    if (bad) { dsl_log("reg: write of %s failed: the old file stays", path); remove(tmp); return -1; }
    return rename(tmp, path);
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
static char *xstrdup(const char *s) { return strdup(s ? s : ""); }

/* launcher features (owner 2026-10-20): mods started from the title (Multiplayer button, Units Laboratory), never from a
   world. mod.json "category": "feature" | "game" (default "game"). The official ones whose released mod.json has no
   "category" yet are named here, so worlds saved with them load without "This world uses other mods". */
static int feature_builtin(const char *id) { return id && (!strcmp(id, "multiplayer") || !strcmp(id, "custom-modes")); }

/* ------------------------------------------------------------------ paths */
static char data_dir_s[1100];                           /* <game data>/Mods */
const char *dsl_reg_data_dir(void) {
    if (data_dir_s[0]) return data_dir_s;
    const char *d = getenv("DSML_DATA");
    if (d && *d) snprintf(data_dir_s, sizeof data_dir_s, "%s/Mods", d);
    else {                                              /* the profile's folder (active.json lives in <data>/Mods) */
        const char *p = dsl_profile_file();
        const char *sl = p ? strrchr(p, '/') : NULL;
        if (sl) snprintf(data_dir_s, sizeof data_dir_s, "%.*s", (int)(sl - p), p);
        else snprintf(data_dir_s, sizeof data_dir_s, "%s/Mods", dsl_log_dir());
    }
    mkdir(data_dir_s, 0755);
    return data_dir_s;
}
static void dpath(char *out, size_t n, const char *f) { snprintf(out, n, "%s/%s", dsl_reg_data_dir(), f); }
static const char *worlds_path(void) {
    static char p[1200];
    if (!p[0]) { const char *s = getenv("DSML_SESSION"); if (s && *s) snprintf(p, sizeof p, "%s", s); else dpath(p, sizeof p, "worlds.json"); }
    return p;
}

/* DSL_BIE_REASON / DSL_ML_REASON from the Windows starter: UTF-8 with bytes >= 0x80 and '%' as %XX (start/win/bie_win.c
   bie_env_text: the ANSI environment keeps it whole on any code page). Plain text passes as it is. */
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static const char *env_text(const char *name) {
    static char out[2][1200]; static int k;
    const char *e = getenv(name); if (!e || !*e) return e;
    char *o = out[k ^= 1]; size_t n = 0;
    for (; *e && n + 1 < sizeof out[0]; e++) {
        if (*e == '%' && hexv(e[1]) >= 0 && hexv(e[2]) >= 0) { o[n++] = (char)(hexv(e[1]) * 16 + hexv(e[2])); e += 2; }
        else o[n++] = *e;
    }
    o[n] = 0; dsi_utf8_fix_end(o);
    return o;
}

/* ------------------------------------------------------------------ installed mods */
#define MAXM 64
static DSLModInfo MI[MAXM]; static int nmi;
static char MI_short[MAXM][128];                         /* mod.json "short_name" (world rows), else "" */
/* mod.json name / short_name / description per language: [0] the top-level (English) text, [l] the optional
   "i18n": {"<code>": {"name", "short_name", "description"}} (NULL = English). Not part of the mod-set hash (that hashes
   id, version and the listed files only), so a translation fix never splits players. */
static struct { char *name[DSI_LANGS], *shortn[DSI_LANGS], *desc[DSI_LANGS]; } MT[MAXM];
static int MI_dep[MAXM];                                /* not running because of mod.json requires / optional / conflicts */
static void mods_lang(void) {                           /* the current language's texts into MI[] (stable pointers) */
    int L = dsi_lang();
    for (int i = 0; i < nmi; i++) {
        char dt[400];                                   /* a dependency reason is a formatted text: made again */
        if (MI_dep[i] && dsl_mod_dep_text(MI[i].id, dt, sizeof dt)) { free((void *)MI[i].reason); MI[i].reason = xstrdup(dt); }
        MI[i].name = MT[i].name[L] ? MT[i].name[L] : MT[i].name[0];
        MI[i].description = MT[i].desc[L] ? MT[i].desc[L] : MT[i].desc[0];
        snprintf(MI_short[i], sizeof MI_short[i], "%s", MT[i].shortn[L] ? MT[i].shortn[L] : MT[i].shortn[0]);
        dsi_utf8_fix_end(MI_short[i]);
    }
}
static int MI_savedep[MAXM];                            /* mod.json "save_dependent": saves need the loader (owner item 9) */
static int MI_savebak[MAXM];                            /* mod.json "save_backup": copy a game save before it loads (ZET-U) */
static int MI_diff[MAXM];                               /* mod.json "difficulty": {"value": N}: adds CombatDifficulty N (Hell 5) */
static char MI_dname[MAXM][32];                         /* its "difficulty": {"name"} ("Hell"), else the mod's short name */
static int MI_profon[MAXM];                             /* switched on in the profile (a difficulty mod runs anyway: dsl_deps.h) */
static int cmpstr_ids(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static int mods_scanned;
static void mods_scan(void) {
    if (mods_scanned) return; mods_scanned = 1;
    const char *md = dsl_mods_dir();
    DIR *d = md ? opendir(md) : NULL; if (!d) return;
    char *pt = slurp(dsl_profile_file(), 1 << 20); JNode *prof = pt ? json_parse(pt) : NULL; free(pt);
    JNode *en = json_get(prof, "enabled");
    struct dirent *e; char names[MAXM][256]; int nn = 0;
    while ((e = readdir(d)) && nn < MAXM) if (e->d_name[0] != '.') snprintf(names[nn++], 256, "%s", e->d_name);
    closedir(d);
    qsort(names, (size_t)nn, 256, (int (*)(const void *, const void *))strcmp);
    for (int k = 0; k < nn && nmi < MAXM; k++) {
        char mp[1400]; snprintf(mp, sizeof mp, "%s/%s/mod.json", md, names[k]);
        char *t = slurp(mp, 1 << 20); if (!t) continue;
        JNode *m = json_parse(t); free(t);
        const char *id = json_str(m, "id");
        if (!m || !id || strcmp(id, names[k])) { json_free(m); continue; }
        DSLModInfo *x = &MI[nmi++];
        x->id = xstrdup(id);
        MT[nmi - 1].name[0] = xstrdup(json_str(m, "name") ? json_str(m, "name") : id);
        MT[nmi - 1].desc[0] = xstrdup(json_str(m, "description"));
        MT[nmi - 1].shortn[0] = xstrdup(json_str(m, "short_name"));
        { JNode *tr = json_get(m, "i18n");
          for (int l = 1; l < DSI_LANGS; l++) {
              JNode *t = json_get(tr, dsi_code_of(l)); const char *v;
              if ((v = json_str(t, "name")) && *v) MT[nmi - 1].name[l] = xstrdup(v);
              if ((v = json_str(t, "short_name")) && *v) MT[nmi - 1].shortn[l] = xstrdup(v);
              if ((v = json_str(t, "description")) && *v) MT[nmi - 1].desc[l] = xstrdup(v);
          } }
        x->version = xstrdup(json_str(m, "version"));
        const char *kd = json_str(m, "kind"); if (!kd) kd = json_str(m, "type");
        x->kind = xstrdup(kd ? kd : "code");
        const char *sc = json_str(m, "scope"); x->scope = xstrdup(sc && !strcmp(sc, "client") ? "client" : "gameplay");
        { const char *ca = json_str(m, "category"); x->feature = ca ? !strcmp(ca, "feature") : feature_builtin(id); x->fix = ca && !strcmp(ca, "fix"); }
        JNode *h = json_get(m, "hidden"); x->hidden = h && h->t == JN_BOOL && h->b;
        JNode *vp = json_get(m, "vanilla_plus"); x->vanilla_plus = vp && vp->t == JN_BOOL && vp->b;
        JNode *sd = json_get(m, "save_dependent"); MI_savedep[nmi - 1] = sd && sd->t == JN_BOOL && sd->b;
        JNode *sb = json_get(m, "save_backup"); MI_savebak[nmi - 1] = sb && sb->t == JN_BOOL && sb->b;
        MI_diff[nmi - 1] = dsl_mf_difficulty(m);
        { const char *dn = json_str(json_get(m, "difficulty"), "name"); snprintf(MI_dname[nmi - 1], sizeof MI_dname[0], "%s", dn && *dn ? dn : id); }
        int on = 1;
        if (en && en->t == JN_ARR) { on = 0; for (JNode *c = en->child; c; c = c->next) if (c->t == JN_STR && !strcmp(c->str, id)) on = 1; }
        MI_profon[nmi - 1] = on;
        if (MI_diff[nmi - 1]) on = 1;                                  /* a custom difficulty runs whenever installed */
        x->enabled = on;
        const char *why = NULL; int st = dsl_mod_state(id, &why);
        if (!strcmp(x->kind, "bepinex")) {                               /* BepInEx plugin: run by the OS start part */
#ifdef _WIN32
            /* the starter sets DOORSTOP_DISABLE=1 when no BepInEx mod is on (start/win/bie_win.c, lane L3). Doorstop itself
               sets it to "TRUE" in the game process once its hooks are in (windows/entrypoint.c): that means it runs */
            const char *dd = getenv("DOORSTOP_DISABLE");
            int runs = on && !(dd && *dd && strcmp(dd, "TRUE"));
            const char *br = env_text("DSL_BIE_REASON");
            st = runs ? 1 : 0; why = runs ? "" : !on ? "off" : (br && *br) ? br : TR_NOOP("BepInEx did not start");   /* reasons: English keys, the Mods window translates */
#else
            char pd[1300]; snprintf(pd, sizeof pd, "%s/%s", md, id);   /* Mac: start/mac/dsl_bie_mac.c, its folder in this start */
            st = dsl_bie_running_dir(pd) ? 1 : 0;
            why = dsl_bie_state() < 0 ? TR_NOOP("BepInEx is not available in this start") : TR_NOOP("BepInEx plugin: not running in this start");
#endif
        } else if (dsl_net_kind(x->kind) == DSL_NET_ML) {               /* MelonLoader mod (lane melon): run by the OS start part */
            char pd[1300]; snprintf(pd, sizeof pd, "%s/%s", md, id);
            const char *mr = env_text("DSL_ML_REASON");
            st = dsl_ml_running_dir(pd) ? 1 : 0;
            why = mr && *mr ? mr : dsl_ml_state() < 0 ? TR_NOOP("MelonLoader is not available in this start") : TR_NOOP("MelonLoader mod: not running in this start");
        }
        char dt[400];                                   /* requires / optional / conflicts (dsl_deps.h): the exact reason */
        MI_dep[nmi - 1] = on && dsl_mod_dep_text(id, dt, sizeof dt);
        if (MI_dep[nmi - 1]) { st = 0; why = dt; }
        x->active = st == 1 ? 1 : (on ? -1 : 0);
        x->reason = xstrdup(st == 1 ? "" : !on ? "off" : (why ? why : TR_NOOP("did not load")));
        json_free(m);
    }
    json_free(prof);
    mods_lang();
}
int dsl_mods_count(void) { mods_scan(); return nmi; }
/* dsl_main.c dsl_mod_dep_text: an installed mod's name in the current language (the reason line names it as the list does) */
const char *dsl_mod_name_of(const char *id) {
    int L = dsi_lang();
    for (int i = 0; id && i < nmi; i++) if (MI[i].id && !strcmp(MI[i].id, id)) return MT[i].name[L] ? MT[i].name[L] : MT[i].name[0];
    return NULL;
}
/* read the mods folder again (a mod package installed while the game runs, ui/dsl_pkg.c): the list for the Mods window.
   What runs does not change (the running key, loaded modules); old DSLModInfo pointers are invalid afterwards. */
void dsl_mods_rescan(void) {
    for (int i = 0; i < nmi; i++) { free((void *)MI[i].id); free((void *)MI[i].version); free((void *)MI[i].kind); free((void *)MI[i].scope); free((void *)MI[i].reason);
        for (int l = 0; l < DSI_LANGS; l++) { free(MT[i].name[l]); free(MT[i].shortn[l]); free(MT[i].desc[l]); } }
    memset(MI, 0, sizeof MI); memset(MI_short, 0, sizeof MI_short); memset(MT, 0, sizeof MT); nmi = 0; mods_scanned = 0;
    mods_scan();
    dsl_settings_rescan(); dsl_moddata_rescan();
    dsl_log("reg: mods folder read again: %d installed mod(s)", nmi);
}
/* the short name of a mod for the world rows: mod.json "short_name", else the name cut at a word before 16 columns.
   Columns, not bytes: 1 per character, 2 for a 3- or 4-byte UTF-8 character (kana, CJK, Hangul are about twice as wide),
   so a Russian or Japanese name is not cut at half the length of an English one. */
void dsl_mod_short(const char *id, char *out, size_t cap) {
    mods_scan(); out[0] = 0;
    for (int i = 0; i < nmi; i++) if (!strcmp(MI[i].id, id)) {
        if (MI_short[i][0]) { snprintf(out, cap, "%s", MI_short[i]); return; }
        snprintf(out, cap, "%s", MI[i].name);
        int cols = 0; for (const unsigned char *p = (const unsigned char *)out; *p; p++) if ((*p & 0xC0) != 0x80) cols += *p >= 0xE0 ? 2 : 1;
        if (cols > 18) {
            size_t b = 0, keep = 0, sp = 0; int col = 0;
            while (out[b]) {                                              /* keep whole characters up to column 16 */
                unsigned char u = (unsigned char)out[b]; int w = u >= 0xE0 ? 2 : 1;
                if (col + w > 16) break;
                if (u == ' ' && col >= 6) sp = b;                         /* the last word break after column 6 */
                col += w; b++; while (out[b] && ((unsigned char)out[b] & 0xC0) == 0x80) b++;
                keep = b;
            }
            if (out[keep] == ' ') sp = keep;
            size_t c = sp ? sp : keep;
            snprintf(out + c, cap - c, "...");
        }
        return;
    }
    snprintf(out, cap, "%s", id);
}
/* mod.json "save_dependent": true = a save made with this mod on may not work in the game started without the loader */
int dsl_mod_save_dependent(const char *id) { mods_scan(); for (int i = 0; i < nmi; i++) if (!strcmp(MI[i].id, id)) return MI_savedep[i]; return 0; }
/* mod.json "difficulty": {"value": N}: the mod adds that difficulty level (the New Game screen's toggle, ui/dsl_newgame.c); 0 = none */
int dsl_mod_difficulty(const char *id) { mods_scan(); for (int i = 0; i < nmi; i++) if (!strcmp(MI[i].id, id)) return MI_diff[i]; return 0; }
/* its level's name for texts ("Hell"): mod.json "difficulty": {"name"}, else the id */
const char *dsl_mod_difficulty_name(const char *id) { mods_scan(); for (int i = 0; i < nmi; i++) if (!strcmp(MI[i].id, id)) return MI_dname[i]; return id; }
/* the player's warning for these mods (owner item 9): "Saves made with A and B may not work without the mod loader."
   out = "" and 0 when none of them is save_dependent; else the number of such mods */
const char *dsl_mod_name(const char *id);
int dsl_save_warning(const char *const *ids, int n, char *out, size_t cap) {
    const char *v[MAXM]; int k = 0;
    for (int i = 0; i < n && k < MAXM; i++) if (dsl_mod_save_dependent(ids[i])) v[k++] = ids[i];
    out[0] = 0; if (!k) return 0;
    char list[600] = "", t[600];
    for (int i = 0; i < k; i++) {
        const char *nm = dsl_mod_name(v[i]); if (!nm) nm = v[i];
        if (!i) { snprintf(list, sizeof list, "%s", nm); continue; }
        snprintf(t, sizeof t, i + 1 == k ? TR("%s and %s") : TR("%s, %s"), list, nm); snprintf(list, sizeof list, "%s", t);
    }
    snprintf(out, cap, TR("Saves made with %s may not work without the mod loader."), list);
    dsi_utf8_fix_end(out);
    return k;
}
/* 1 = a running save_dependent mod also asks for mod.json "save_backup": true (dsl_saves.c: the warning's Load first
   copies the world's game-save files to <DSML_DATA>/Mods/save_backups/). Opt-in per mod: Hell difficulty has no backup. */
int dsl_running_save_backup(void) {
    mods_scan();
    for (int i = 0; i < nmi; i++) if (MI[i].active == 1 && MI_savedep[i] && MI_savebak[i]) return 1;
    return 0;
}
const char *dsl_mod_name(const char *id) { const DSLModInfo *m = NULL; mods_scan(); for (int i = 0; i < nmi; i++) if (!strcmp(MI[i].id, id)) m = &MI[i]; return m ? m->name : NULL; }
const DSLModInfo *dsl_mods_at(int i) { mods_scan(); return i >= 0 && i < nmi ? &MI[i] : NULL; }
static const DSLModInfo *mod_by_id(const char *id) { mods_scan(); for (int i = 0; i < nmi; i++) if (!strcmp(MI[i].id, id)) return &MI[i]; return NULL; }
/* 1 = a launcher feature: on for every world, never in a world's key (installed: its mod.json; else the official list) */
int dsl_mod_is_feature(const char *id) { const DSLModInfo *m = id ? mod_by_id(id) : NULL; return m ? m->feature : feature_builtin(id); }

/* running gameplay key: sorted ids of the gameplay modules that run in this process. The multiplayer mod itself is
   not in it (its HELLO already checks build + protocol), so a world's key and the join check are the same with or
   without it, and a player without the loader (old friend kit) can still join a host that runs only multiplayer
   (cross-platform finding, ds_launcher NOTES_winlauncher.md). Ids only: the same key on Mac and Windows. */
static char run_key[512];
static int cmpstr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static void key_of(const char *const *ids, int n, char *out, size_t cap) {
    const char *v[MAXM]; int k = 0;
    for (int i = 0; i < n && k < MAXM; i++) {
        if (dsl_mod_is_feature(ids[i])) continue;
        const DSLModInfo *m = mod_by_id(ids[i]); if (!m || !strcmp(m->scope, "gameplay")) v[k++] = ids[i];
    }
    qsort(v, (size_t)k, sizeof *v, cmpstr);
    out[0] = 0;
    for (int i = 0; i < k; i++) { if (i) strlcat(out, "+", cap); strlcat(out, v[i], cap); }
    if (!k) snprintf(out, cap, "vanilla");
}
/* a stored key in today's form: sorted, without launcher features ("multiplayer" left out since l3-win 3dd9646, every
   feature since 2026-10-20; older worlds.json files have them), "vanilla" when empty. Keeps those worlds working. */
static void key_norm(const char *in, char *out, size_t cap) {
    char b[512]; snprintf(b, sizeof b, "%s", in ? in : ""); const char *v[MAXM]; int k = 0;
    for (char *sv = NULL, *t = strtok_r(b, "+", &sv); t && k < MAXM; t = strtok_r(NULL, "+", &sv))
        if (*t && !dsl_mod_is_feature(t) && strcmp(t, "vanilla")) v[k++] = t;
    qsort(v, (size_t)k, sizeof *v, cmpstr);
    out[0] = 0; for (int i = 0; i < k; i++) { if (i) strlcat(out, "+", cap); strlcat(out, v[i], cap); }
    if (!k) snprintf(out, cap, "vanilla");
}
int dsl_key_same(const char *a, const char *b) {
    char x[512], y[512]; key_norm(a, x, sizeof x); key_norm(b, y, sizeof y); return !strcmp(x, y);
}
static void running_key(void) {
    const char *v[MAXM]; int k = 0;
    for (int i = 0; i < nmi; i++) if (MI[i].active == 1 && !strcmp(MI[i].scope, "gameplay")) v[k++] = MI[i].id;
    key_of(v, k, run_key, sizeof run_key);
}
/* "Hell difficulty, Multiplayer" for a key; returns how many of its mods are not installed */
static int key_names(const char *key, char *out, size_t cap) {
    out[0] = 0;
    if (!strcmp(key, "vanilla")) { snprintf(out, cap, "%s", TR("none")); return 0; }
    char b[512]; snprintf(b, sizeof b, "%s", key);
    int i = 0, missing = 0; char one[300], t2[1200];
    for (char *sv = NULL, *t = strtok_r(b, "+", &sv); t; t = strtok_r(NULL, "+", &sv), i++) {
        const DSLModInfo *m = mod_by_id(t);
        if (m) snprintf(one, sizeof one, "%s", m->name); else { snprintf(one, sizeof one, TR("%s (not installed)"), t); missing++; }
        if (!i) snprintf(t2, sizeof t2, "%s", one); else snprintf(t2, sizeof t2, TR("%s, %s"), out, one);
        snprintf(out, cap, "%s", t2);
    }
    dsi_utf8_fix_end(out);
    return missing;
}

/* ------------------------------------------------------------------ JSON out (any node, for members we keep) */
static void jput(char **b, size_t *n, size_t *cap, const char *t) {
    size_t L = strlen(t); if (*n + L + 1 > *cap) { *cap = (*n + L + 1) * 2; *b = realloc(*b, *cap); }
    memcpy(*b + *n, t, L + 1); *n += L;
}
static void jdump(const JNode *j, char **b, size_t *n, size_t *cap) {
    char tmp[64];
    switch (j ? j->t : JN_NULL) {
    case JN_NULL: jput(b, n, cap, "null"); break;
    case JN_BOOL: jput(b, n, cap, j->b ? "true" : "false"); break;
    case JN_NUM: snprintf(tmp, sizeof tmp, "%.17g", j->num); jput(b, n, cap, tmp); break;
    case JN_STR: { size_t L = strlen(j->str) * 6 + 3; char *e = malloc(L); jesc(e, L, j->str); jput(b, n, cap, "\""); jput(b, n, cap, e); jput(b, n, cap, "\""); free(e); break; }
    case JN_ARR: case JN_OBJ: {
        jput(b, n, cap, j->t == JN_ARR ? "[" : "{");
        for (const JNode *c = j->child; c; c = c->next) {
            if (c != j->child) jput(b, n, cap, ",");
            if (j->t == JN_OBJ) { size_t L = strlen(c->key ? c->key : "") * 6 + 3; char *e = malloc(L); jesc(e, L, c->key ? c->key : ""); jput(b, n, cap, "\""); jput(b, n, cap, e); jput(b, n, cap, "\":"); free(e); }
            jdump(c, b, n, cap);
        }
        jput(b, n, cap, j->t == JN_ARR ? "]" : "}"); break; }
    }
}

/* ------------------------------------------------------------------ worlds.json */
static JNode *worlds_doc;
static int worlds_migrated;
/* world keys written with a launcher feature in them ("multiplayer", "custom-modes+gcd-cdr"): same key without it */
static void strip_mp(JNode *c) {
    if (!c || c->t != JN_STR || !c->str) return;
    char out[512] = ""; char b[512]; snprintf(b, sizeof b, "%s", c->str);
    for (char *t = strtok(b, "+"); t; t = strtok(NULL, "+")) if (!dsl_mod_is_feature(t)) { if (out[0]) strlcat(out, "+", sizeof out); strlcat(out, t, sizeof out); }
    if (!out[0]) snprintf(out, sizeof out, "vanilla");
    if (strcmp(out, c->str)) { free(c->str); c->str = strdup(out); worlds_migrated = 1; }
}
/* mods belong to the WORLD (owner 2026-10-16 v13 #4): a key that names one save file ("<Clan>__AutoSave_<32 hex>_0",
   any "<name>_<32 hex>_<n>") becomes its world's guid. Two entries for one world: a guid entry wins (written by this
   code, so newer); between two save entries the one whose save file is newer wins. */
static int save_key_guid(const char *k, char out[40]) {
    size_t L = strlen(k);
    for (size_t i = 0; i + 32 <= L; i++) {
        if (i && k[i - 1] != '_') continue;
        int hex = 1; for (int j = 0; j < 32 && hex; j++) hex = strchr("0123456789abcdefABCDEF", k[i + j]) != NULL;
        if (!hex || (i + 32 < L && k[i + 32] != '_')) continue;
        const char *h = k + i;
        snprintf(out, 40, "%.8s-%.4s-%.4s-%.4s-%.12s", h, h + 8, h + 12, h + 16, h + 20);
        for (char *p = out; *p; p++) if (*p >= 'A' && *p <= 'F') *p += 32;
        return 1;
    }
    return 0;
}
static double save_mtime(const char *name) {
    const char *d = getenv("DSML_DATA"); if (!d || !*d) return 0;
    char p[1400]; struct stat st; snprintf(p, sizeof p, "%s/Saves/%s.json", d, name);
    return stat(p, &st) ? 0 : (double)st.st_mtime;
}
static void migrate_save_keys(JNode *w) {
    for (JNode *c = w->child; c; c = c->next) {
        char g[40]; if (!c->key || c->t != JN_STR || !save_key_guid(c->key, g) || !strcasecmp(c->key, g)) continue;
        JNode *other = NULL;
        for (JNode *o = w->child; o; o = o->next) if (o != c && o->t == JN_STR && o->key) {
            char g2[40]; if (!strcasecmp(o->key, g) || (save_key_guid(o->key, g2) && !strcasecmp(g2, g) && strcasecmp(o->key, g2))) { other = o; break; }
        }
        int lose = 0;
        if (other) {
            char g2[40]; int other_is_guid = !(save_key_guid(other->key, g2) && strcasecmp(other->key, g2));
            lose = other_is_guid || save_mtime(other->key) > save_mtime(c->key);
        }
        dsl_log("worlds: per-save entry %s -> world %s%s", c->key, g, lose ? " (another entry of that world is newer: dropped)" : "");
        if (lose) c->t = JN_NULL;                                      /* worlds_save keeps strings only */
        else { free(c->key); c->key = strdup(g); }
        worlds_migrated = 1;
    }
}
/* worlds.json there but not readable / not JSON: never rewritten this session (the mod sets of the worlds would be lost);
   a corrupt one is kept as worlds.json.bad-<time>; loads are refused with a message (dsl_world.c) */
static int worlds_bad; static char worlds_bad_path[1300];
int dsl_worlds_bad(const char **kept) { if (kept) *kept = worlds_bad_path; return worlds_bad; }
static void worlds_load(void) {
    if (worlds_doc || worlds_bad) return;
    const char *wp = worlds_path();
    char *t = slurp(wp, 8 << 20); worlds_doc = t ? json_parse(t) : NULL;
    if (!worlds_doc && !access(wp, 0)) {
        worlds_bad = 1;
        if (t) {                                                        /* read but not JSON: set it aside, keep it */
            char ts[32]; time_t now = time(NULL); strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", localtime(&now));
            snprintf(worlds_bad_path, sizeof worlds_bad_path, "%s.bad-%s", wp, ts);
            if (rename(wp, worlds_bad_path)) snprintf(worlds_bad_path, sizeof worlds_bad_path, "%s", wp);
        } else snprintf(worlds_bad_path, sizeof worlds_bad_path, "%s", wp);
        dsl_log("worlds: %s could not be %s: kept as %s; not written this session, loads refused", wp, t ? "parsed" : "read", worlds_bad_path);
    }
    free(t);
    JNode *w = json_get(worlds_doc, "worlds");
    if (w && w->t == JN_OBJ) { for (JNode *c = w->child; c; c = c->next) strip_mp(c); migrate_save_keys(w); }
}
/* ------------------------------------------------------------------ custom difficulty (owner 2026-10-25)
   A mod with mod.json "difficulty": {"value": N} (Hell difficulty: 5) runs whenever it is installed (dsl_deps.h); the New Game
   screen's toggle only shows or hides level N (ui/dsl_newgame.c). Its presence changes nothing for a world on the stock levels
   0..4 (loader/NOTES.md "Hell always loaded": every patch, the staff strip, the environment swap and the tables checked).
   So a world uses it only when its combat OR management level is N, read from the world's save (CampaignSaveHeader, any
   old world too) or from the open world: only then it is in the world's set (label, Modded filter, world check / flow K,
   save warning, multiplayer set). Every other world plays and joins as without it. Levels are kept per world guid while
   the game runs (save headers read by the Load list, the world check, the save warning; the open world's own setting). */
#define MAXLV 512
static struct { char guid[40]; int c, m; } LV[MAXLV]; static int nlv, lv_next;
void dsl_world_note_level(const char *guid, int c, int m) {
    if (!guid || !*guid) return;
    for (int i = 0; i < nlv; i++) if (!strcasecmp(LV[i].guid, guid)) { LV[i].c = c; LV[i].m = m; return; }
    int k = nlv < MAXLV ? nlv++ : (lv_next++ % MAXLV);               /* full: the oldest entries go first */
    snprintf(LV[k].guid, sizeof LV[k].guid, "%s", guid); LV[k].c = c; LV[k].m = m;
}
static int mi_index(const char *id) { for (int i = 0; i < nmi; i++) if (!strcmp(MI[i].id, id)) return i; return -1; }
/* 1 = the world plays with installed mod i (a difficulty mod: the world is on its level), 0 = not, -1 = its level is not
   known yet (callers keep what the stored key says) */
static int world_uses_diff(const char *guid, int i) {
    if (i < 0 || !MI_diff[i]) return -1;
    for (int k = 0; guid && k < nlv; k++) if (!strcasecmp(LV[k].guid, guid)) return LV[k].c == MI_diff[i] || LV[k].m == MI_diff[i];
    return -1;
}
static int world_level_known(const char *guid) { for (int k = 0; guid && k < nlv; k++) if (!strcasecmp(LV[k].guid, guid)) return 1; return 0; }
/* the running mods whose saves need the loader (mod.json "save_dependent"; the dsl_saves.c warning) as that world uses them
   (a custom difficulty only on its level: a world not on Hell saves as the stock game does): ids sorted, key = "a+b"; count */
int dsl_world_save_dependent(const char *guid, const char **ids, int max, char *key, size_t kcap) {
    mods_scan(); int n = 0;
    for (int i = 0; i < nmi && n < max; i++) if (MI[i].active == 1 && MI_savedep[i] && (!MI_diff[i] || world_uses_diff(guid, i) == 1)) ids[n++] = MI[i].id;
    qsort(ids, (size_t)n, sizeof *ids, cmpstr_ids);
    if (key && kcap) { key[0] = 0; for (int i = 0; i < n; i++) { if (i) strlcat(key, "+", kcap); strlcat(key, ids[i], kcap); } }
    return n;
}
int dsl_mod_is_difficulty(const char *id) { mods_scan(); int i = id ? mi_index(id) : -1; return i >= 0 && MI_diff[i] > 0; }
/* key with its custom difficulty mods set for that world: an installed difficulty mod is in it only when the world is on its
   level (running = 1: and it runs here, for "the set this game plays the world with"); a world whose level is not known yet
   keeps what `key` says. Other ids as they are. */
static void key_diff(const char *guid, const char *key, int running, char *out, size_t cap) {
    char b[512]; snprintf(b, sizeof b, "%s", key ? key : ""); const char *v[MAXM]; int k = 0;
    mods_scan();
    for (char *sv = NULL, *t = strtok_r(b, "+", &sv); t && k < MAXM; t = strtok_r(NULL, "+", &sv)) {
        if (!*t || !strcmp(t, "vanilla")) continue;
        int i = mi_index(t);
        if (i >= 0 && MI_diff[i] && world_uses_diff(guid, i) == 0) continue;
        v[k++] = t;
    }
    for (int i = 0; i < nmi && k < MAXM; i++) {
        if (!MI_diff[i] || world_uses_diff(guid, i) != 1 || (running && MI[i].active != 1)) continue;
        int dup = 0; for (int j = 0; j < k; j++) dup |= !strcmp(v[j], MI[i].id);
        if (!dup) v[k++] = MI[i].id;
    }
    qsort(v, (size_t)k, sizeof *v, cmpstr);
    out[0] = 0; for (int i = 0; i < k; i++) { if (i) strlcat(out, "+", cap); strlcat(out, v[i], cap); }
    if (!k) snprintf(out, cap, "vanilla");
}

/* ------------------------------------------------------------------ fixes (owner 2026-10-24)
   mod.json "category": "fix" = a fix for a bug of the game (Global cooldown fix, Sound settings fix). Per world like a game
   mod, but in its own tab of the Mods window, and a world with only fixes (and client mods) stays "Vanilla & Vanilla+" on
   the Load list. "On in all worlds" (Fixes tab, default on; <data>/Mods/fixes.json {"all_worlds": {"<id>": bool}}): every
   world plays with that fix, old saves and new ones, except a world whose Change window turned it off (worlds.json
   "fix_off": {guid: "<ids joined by +>"}). The set a world plays with = its stored key + the fixes on in all worlds - its
   own offs (key_eff). world_key gives that set to every reader, and dsl_world.c's check asks key_eff, so the Load list
   label, the filter kind, the world check / flow K and the multiplayer set (the running set after flow K) agree.
   Fixes of scope "client" are never in a key (on or off for all worlds together with their switch). */
static char fx_un[MAXM][64]; static int fx_nun, fx_read;            /* fixes NOT on in all worlds (unticked) */
static void fixes_load(void) {
    if (fx_read) return; fx_read = 1;
    char p[1200]; dpath(p, sizeof p, "fixes.json");
    char *t = slurp(p, 1 << 16); JNode *j = t ? json_parse(t) : NULL; free(t);
    JNode *a = json_get(j, "all_worlds");
    if (a && a->t == JN_OBJ) for (JNode *c = a->child; c && fx_nun < MAXM; c = c->next)
        if (c->key && c->t == JN_BOOL && !c->b) snprintf(fx_un[fx_nun++], 64, "%s", c->key);
    json_free(j);
}
int dsl_fix_all_worlds(const char *id) {
    fixes_load(); if (!id) return 0;
    for (int i = 0; i < fx_nun; i++) if (!strcmp(fx_un[i], id)) return 0;
    return 1;
}
int dsl_fix_set_all_worlds(const char *id, int on) {
    if (!id || !*id) return -1;
    fixes_load();
    int i = 0; while (i < fx_nun && strcmp(fx_un[i], id)) i++;
    if (on && i < fx_nun) { memmove(fx_un[i], fx_un[i + 1], (size_t)(fx_nun - i - 1) * sizeof fx_un[0]); fx_nun--; }
    if (!on && i == fx_nun) { if (fx_nun >= MAXM) return -1; snprintf(fx_un[fx_nun++], 64, "%s", id); }
    /* every installed fix of scope gameplay with its state, plus the unticked ones that are not installed now */
    char b[8192], e[200]; size_t k = 0; int n = 0;
    k += (size_t)snprintf(b + k, sizeof b - k, "{\"all_worlds\": {");
    mods_scan();
    for (int m = 0; m < nmi && k + 300 < sizeof b; m++) if (MI[m].fix && !strcmp(MI[m].scope, "gameplay")) {
        jesc(e, sizeof e, MI[m].id); k += (size_t)snprintf(b + k, sizeof b - k, "%s\"%s\": %s", n++ ? ", " : "", e, dsl_fix_all_worlds(MI[m].id) ? "true" : "false"); }
    for (int u = 0; u < fx_nun && k + 300 < sizeof b; u++) if (!mod_by_id(fx_un[u])) {
        jesc(e, sizeof e, fx_un[u]); k += (size_t)snprintf(b + k, sizeof b - k, "%s\"%s\": false", n++ ? ", " : "", e); }
    snprintf(b + k, sizeof b - k, "}}\n");
    char p[1200]; dpath(p, sizeof p, "fixes.json");
    int r = put_file(p, b);
    dsl_log("reg: fix %s %s in all worlds%s", id, on ? "on" : "not on", r ? " (fixes.json NOT written)" : "");
    if (!r) dsl_rows_refresh();
    return r;
}
static int in_plus(const char *list, const char *id) {             /* "a+b+c" holds id */
    size_t L = strlen(id);
    for (const char *p = list; p && *p; ) {
        const char *e = strchr(p, '+'); size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n == L && !strncmp(p, id, L)) return 1;
        p = e ? e + 1 : NULL;
    }
    return 0;
}
static void plus_add(char *list, size_t cap, const char *id) { if (in_plus(list, id)) return; if (*list) strlcat(list, "+", cap); strlcat(list, id, cap); }
/* a fix every world plays with unless it turned it off: ticked, scope gameplay, and not failing in this start (a fix that
   cannot run here would make every load ask "Load it with them?" again and again). A fix in the running key counts even
   when it failed: after flow K the key is the set asked for (DSML_MODSET), and the world must match it. */
static int fix_default(int i) {
    return MI[i].fix && !strcmp(MI[i].scope, "gameplay") && (MI[i].active != -1 || in_plus(run_key, MI[i].id)) && dsl_fix_all_worlds(MI[i].id);
}
static const char *world_offs(const char *guid) {                  /* worlds.json "fix_off": fixes the world turned off */
    worlds_load();
    JNode *w = json_get(worlds_doc, "fix_off");
    if (w && w->t == JN_OBJ && guid) for (JNode *c = w->child; c; c = c->next)
        if (c->key && !strcasecmp(c->key, guid) && c->t == JN_STR && c->str) return c->str;
    return "";
}
/* the next worlds_save writes it ("" = none) */
static void world_offs_put(const char *guid, const char *ids) {
    worlds_load();
    if (worlds_bad || !guid) return;
    if (!worlds_doc) { worlds_doc = calloc(1, sizeof *worlds_doc); worlds_doc->t = JN_OBJ; }
    JNode *w = json_get(worlds_doc, "fix_off");
    if (!w) {
        if (!*ids) return;
        w = calloc(1, sizeof *w); w->t = JN_OBJ; w->key = strdup("fix_off");
        JNode **e = &worlds_doc->child; while (*e) e = &(*e)->next; *e = w;
    }
    JNode *c = w->child; while (c && !(c->key && !strcasecmp(c->key, guid))) c = c->next;
    if (!c) { if (!*ids) return; c = calloc(1, sizeof *c); c->key = strdup(guid); JNode **e = &w->child; while (*e) e = &(*e)->next; *e = c; }
    if (c->t == JN_STR) free(c->str);
    c->t = JN_STR; c->str = strdup(ids);
}
/* the set a world plays with: its stored key + the fixes on in all worlds - its own offs; a custom difficulty mod only when
   the world is on its level (in or out of the stored key; level not known yet = as the stored key says) */
static void key_eff(const char *guid, const char *stored, char *out, size_t cap) {
    char b[512], offs[512]; snprintf(b, sizeof b, "%s", stored ? stored : ""); snprintf(offs, sizeof offs, "%s", world_offs(guid));
    const char *v[MAXM]; int k = 0;
    mods_scan();
    for (char *sv = NULL, *t = strtok_r(b, "+", &sv); t && k < MAXM; t = strtok_r(NULL, "+", &sv))
        if (*t && strcmp(t, "vanilla") && !dsl_mod_is_feature(t) && !in_plus(offs, t) && world_uses_diff(guid, mi_index(t)) != 0) v[k++] = t;
    for (int i = 0; i < nmi && k < MAXM; i++) {
        if (!(fix_default(i) && !in_plus(offs, MI[i].id)) && world_uses_diff(guid, i) != 1) continue;   /* + a world on Hell: Hell */
        int dup = 0; for (int j = 0; j < k; j++) dup |= !strcmp(v[j], MI[i].id);
        if (!dup) v[k++] = MI[i].id;
    }
    qsort(v, (size_t)k, sizeof *v, cmpstr);
    out[0] = 0; for (int i = 0; i < k; i++) { if (i) strlcat(out, "+", cap); strlcat(out, v[i], cap); }
    if (!k) snprintf(out, cap, "vanilla");
}
void dsl_world_key_eff(const char *guid, const char *stored, char *out, size_t cap) { key_eff(guid, stored, out, cap); }
/* the set the world plays with (key_eff of its stored key); NULL = not registered */
static const char *world_key(const char *guid) {
    worlds_load();
    JNode *w = json_get(worlds_doc, "worlds");
    static char nk[512];
    if (w && w->t == JN_OBJ) for (JNode *c = w->child; c; c = c->next) if (c->key && !strcasecmp(c->key, guid) && c->t == JN_STR) { key_eff(guid, c->str, nk, sizeof nk); return nk; }
    return NULL;
}
/* modset_lines (JSON members) and the HELLO hash of the running gameplay set; see worlds_save. Launcher features
   (category "feature": multiplayer, Units Laboratory) are never in it (owner 2026-10-20: a launcher mod never blocks a
   join); multiplayer's own HELLO checks its build + protocol. */
/* A custom difficulty mod is in the set only for the world the set is about (hello_guid: the open world, or the one that
   starts loading; dsl_world_hello): when that world is on its level. At the title (no world) only when the profile switches
   it on: "Get the host's mods" does that for a host on Hell (dsl_hostmods.c), so that guest joins with it. */
static char hello_guid[40], hello_ms[65]; static int hello_own;
static int hello_has(int i) {
    if (!MI_diff[i]) return 1;
    return hello_guid[0] ? world_uses_diff(hello_guid, i) == 1 : MI_profon[i];
}
/* 1 = that mod is in this game's multiplayer set now (dsl_hostmods.c: what the host compares) */
int dsl_mod_in_hello(const char *id) {
    mods_scan(); int i = id ? mi_index(id) : -1;
    return i >= 0 && MI[i].active == 1 && !strcmp(MI[i].scope, "gameplay") && !MI[i].feature && hello_has(i);
}
#define MAXL (MAXM * 4)
/* the HELLO lines of running gameplay mod i: "<id>@<version>:<sha256s>", then its gameplay settings, one line each
   ("<id>/<setting>@<value>": another value = another set, mod settings) */
static int mod_lines(int i, char (*ln)[2048], const char **lp, int nl) {
    extern int dsl_mod_hashes(const char *id, char *out, size_t cap);
    char hs[1800]; dsl_mod_hashes(MI[i].id, hs, sizeof hs);
    snprintf(ln[nl], sizeof ln[nl], "%s@%s%s%s", MI[i].id, MI[i].version ? MI[i].version : "", hs[0] ? ":" : "", hs); lp[nl] = ln[nl]; nl++;
    static char sl[8192]; dsl_settings_lines(MI[i].id, sl, sizeof sl);
    for (char *sv = NULL, *t = strtok_r(sl, "\n", &sv); t && nl < MAXL; t = strtok_r(NULL, "\n", &sv)) { snprintf(ln[nl], sizeof ln[nl], "%s", t); lp[nl] = ln[nl]; nl++; }
    return nl;
}
static int hello_mod(int i) { return MI[i].active == 1 && !strcmp(MI[i].scope, "gameplay") && !MI[i].feature && strcmp(MI[i].id, "multiplayer"); }
static void set_lines(char *out, size_t cap, char ms[65]) {
    static char ln[MAXL][2048]; const char *lp[MAXL]; int nl = 0; char e1[2100];
    for (int i = 0; i < nmi && nl < MAXL; i++) if (hello_mod(i) && hello_has(i)) nl = mod_lines(i, ln, lp, nl);
    qsort(lp, (size_t)nl, sizeof *lp, cmpstr);
    static char joined[MAXL * 2048]; joined[0] = 0; out[0] = 0;
    for (int i = 0; i < nl; i++) {
        jesc(e1, sizeof e1, lp[i]); size_t k = strlen(out); snprintf(out + k, cap - k, "%s\"%s\"", i ? "," : "", e1);
        if (i) strlcat(joined, "\n", sizeof joined); strlcat(joined, lp[i], sizeof joined);
    }
    ms[0] = 0; if (nl) dsl_os_sha256_buf(joined, strlen(joined), ms);
}
static void worlds_save(const char *add_guid, const char *add_key) {
    worlds_load();
    if (worlds_bad) { dsl_log("worlds: not saved (worlds.json could not be read at start)"); return; }
    size_t cap = 1 << 16, n = 0; char *b = malloc(cap); char e1[600], e2[600];
#define PUT(...) do { if (cap - n < 4096) { cap *= 2; b = realloc(b, cap); } int k_ = snprintf(b + n, cap - n, __VA_ARGS__); if (k_ > 0) n += (size_t)k_; } while (0)
    JNode *st = worlds_doc;
    PUT("{");
    int first = 1;
    if (st && st->t == JN_OBJ) for (JNode *c = st->child; c; c = c->next) {     /* keep every other member as it is */
        if (!c->key || !strcmp(c->key, "worlds") || !strcmp(c->key, "modset") || !strcmp(c->key, "modset_lines")) continue;
        jesc(e1, sizeof e1, c->key); PUT("%s\"%s\":", first ? "" : ",", e1); first = 0;
        if (!strcmp(c->key, "fix_off") && c->t == JN_OBJ) {                   /* a world's offs: "" = none, left out */
            int k = 0; PUT("{");
            for (JNode *o = c->child; o; o = o->next) if (o->key && o->t == JN_STR && o->str && *o->str) {
                jesc(e1, sizeof e1, o->key); jesc(e2, sizeof e2, o->str); PUT("%s\"%s\":\"%s\"", k++ ? "," : "", e1, e2); }
            PUT("}");
        } else jdump(c, &b, &n, &cap);
    }
    /* top-level "modset" = what the multiplayer HELLO sends: "" when no gameplay mod (its "none"), else the key */
    /* "modset_lines": <id>@<version>:<sha256s of every platform's files> of each running gameplay mod (multiplayer
       left out), sorted; "modset" = sha256 of those lines ('\n'-joined), "" = no gameplay mod. That is what the
       multiplayer HELLO sends and compares: the same on Mac and Windows for the same mods, names in a refusal. */
    { static char lj[MAXM * 2100]; char ms[65]; set_lines(lj, sizeof lj, ms); snprintf(hello_ms, sizeof hello_ms, "%s", ms);
      PUT("%s\"modset_lines\":[%s],\"modset\":\"%s\",\"worlds\":{", first ? "" : ",", lj, ms); first = 0; }
    int k = 0; JNode *w = json_get(st, "worlds");
    if (w && w->t == JN_OBJ) for (JNode *c = w->child; c; c = c->next) {
        if (!c->key || c->t != JN_STR || (add_guid && !strcasecmp(c->key, add_guid))) continue;
        jesc(e1, sizeof e1, c->key); jesc(e2, sizeof e2, c->str); PUT("%s\n \"%s\":\"%s\"", k++ ? "," : "", e1, e2);
    }
    if (add_guid) { jesc(e1, sizeof e1, add_guid); jesc(e2, sizeof e2, add_key); PUT("%s\n \"%s\":\"%s\"", k++ ? "," : "", e1, e2); }
    PUT("\n}}\n");
#undef PUT
    if (put_file(worlds_path(), b)) dsl_log("worlds: cannot write %s", worlds_path());
    free(b);
    json_free(worlds_doc); worlds_doc = NULL;
}

/* a game save loaded with save_dependent mods (dsl_saves.c): the answer, per world, in worlds.json "save_warned":
   {guid: "<sorted mod ids>"}; asked again only when a mod that is not in that list runs */
const char *dsl_world_warned(const char *guid) {
    worlds_load();
    JNode *w = json_get(worlds_doc, "save_warned");
    static char v[512];
    if (w && w->t == JN_OBJ) for (JNode *c = w->child; c; c = c->next)
        if (c->key && guid && !strcasecmp(c->key, guid) && c->t == JN_STR) { snprintf(v, sizeof v, "%s", c->str); return v; }
    return NULL;
}
/* 1 = the world is registered with another gameplay set than the running one: dsl_world.c refuses it (flow K) */
int dsl_world_set_differs(const char *guid) {
    const char *k = guid ? world_key(guid) : NULL; const char *mine = getenv("DSML_MODSET");
    return k && !dsl_world_key_match(guid, k, mine && *mine ? mine : run_key);
}
/* the world's set `eff` (dsl_world_key_eff) against this game's set `mine` (DSML_MODSET), both as for that world: a custom
   difficulty mod that runs here counts only when the world is on its level (owner 2026-10-25: a world not on Hell loads with
   no restart in a game that has Hell) */
int dsl_world_key_match(const char *guid, const char *eff, const char *mine) {
    char m2[512]; key_diff(guid, mine, 1, m2, sizeof m2);
    return dsl_key_same(eff, m2);
}
/* the multiplayer set is about this world now ("" = none: the title): worlds.json "modset" / "modset_lines" written again when
   that changes it (a host on a Hell world sends Hell, on any other world not; multiplayer reads them when a session starts) */
void dsl_world_hello(const char *guid) {
    if (!strcmp(hello_guid, guid ? guid : "")) return;
    snprintf(hello_guid, sizeof hello_guid, "%s", guid ? guid : "");
    if (!hello_own) return;                                            /* an old launcher's session file: kept as it is */
    static char lj[MAXM * 2100]; char ms[65]; set_lines(lj, sizeof lj, ms);
    if (strcmp(ms, hello_ms)) { worlds_save(NULL, NULL); dsl_log("reg: multiplayer set for %s: %s", hello_guid[0] ? hello_guid : "the title", ms[0] ? ms : "none"); }
}
/* multiplayer's HELLO (owner 2026-10-25: a guest with Hell joins a host on a Hell world with no refusal, no restart): the
   lines of the custom difficulty mods that run here but are not in the multiplayer set now (Hell at the title, or on a
   world not on Hell), as set_lines writes them, sorted, '\n'-joined. The host's server lets the guest join when its own
   lines plus some of these are exactly the host's (the host's world is on that level); the world the guest then loads
   puts them in its set (world_frame). An old launcher's session file (not ours): none. Returns how many lines. */
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int dsl_mp_optional_lines(char *out, size_t cap) {
    if (!out || !cap) return -1;
    out[0] = 0; mods_scan();
    if (!hello_own) return 0;
    static char ln[MAXL][2048]; const char *lp[MAXL]; int nl = 0;
    for (int i = 0; i < nmi && nl < MAXL; i++) if (MI_diff[i] && hello_mod(i) && !hello_has(i)) nl = mod_lines(i, ln, lp, nl);
    qsort(lp, (size_t)nl, sizeof *lp, cmpstr);
    for (int i = 0; i < nl; i++) { if (i) strlcat(out, "\n", cap); strlcat(out, lp[i], cap); }
    return nl;
}
void dsl_world_set_warned(const char *guid, const char *key) {
    worlds_load();
    if (worlds_bad || !guid || !key) return;
    if (!worlds_doc) { worlds_doc = calloc(1, sizeof *worlds_doc); worlds_doc->t = JN_OBJ; }
    JNode *w = json_get(worlds_doc, "save_warned");
    if (!w) {
        w = calloc(1, sizeof *w); w->t = JN_OBJ; w->key = strdup("save_warned");
        JNode **e = &worlds_doc->child; while (*e) e = &(*e)->next; *e = w;
    }
    JNode *c = w->child; while (c && !(c->key && !strcasecmp(c->key, guid))) c = c->next;
    if (!c) { c = calloc(1, sizeof *c); c->key = strdup(guid); JNode **e = &w->child; while (*e) e = &(*e)->next; *e = c; }
    if (c->t == JN_STR) free(c->str);
    c->t = JN_STR; c->str = strdup(key);
    worlds_save(NULL, NULL);
    dsl_log("worlds: save warning answered for world %s (mods %s)", guid, key);
}
int dsl_world_label(const char *guid, char *out, size_t cap) {
    const char *k = guid ? world_key(guid) : NULL;
    char rk[512]; key_diff(guid, run_key, 1, rk, sizeof rk);         /* unknown world = the mods that run now, for its level */
    char nm[400]; key_names(k ? k : rk, nm, sizeof nm);
    snprintf(out, cap, TR("Mods: %s"), nm); dsi_utf8_fix_end(out);
    return k != NULL;
}
static char world_ids_buf[512];
int dsl_world_mods(const char *guid, const char **ids, int max) {
    const char *k = guid ? world_key(guid) : NULL;
    static char rk[512];
    if (!k) { key_diff(guid, run_key, 1, rk, sizeof rk); k = rk; }  /* unknown world = the mods that run now (lead 2026-10-15), for its level */
    if (!strcmp(k, "vanilla")) return 0;
    snprintf(world_ids_buf, sizeof world_ids_buf, "%s", k); int n = 0;
    for (char *sv = NULL, *t = strtok_r(world_ids_buf, "+", &sv); t && n < max; t = strtok_r(NULL, "+", &sv)) ids[n++] = t;
    return n;
}
/* the game mods that are on when this world plays (owner v13 #5): its gameplay set, plus its client game mods that are
   on. Launcher features are not world mods (owner 2026-10-20): dsl_features_on lists them. */
int dsl_world_mods_all(const char *guid, const char **ids, int max) {
    int n = dsl_world_mods(guid, ids, max);
    for (int i = 0; i < nmi && n < max; i++) {
        if (!MI[i].enabled || MI[i].feature || strcmp(MI[i].scope, "client")) continue;
        int dup = 0; for (int j = 0; j < n; j++) dup |= !strcmp(ids[j], MI[i].id);
        if (!dup) ids[n++] = MI[i].id;
    }
    return n;
}
/* the Load list's filter panel (dsl_rows.c, owner 2026-10-24): 1 = the world plays with a mod that changes the gameplay
   (mod.json scope "gameplay", or a mod that is not installed: unknown counts as gameplay); 0 = no mod, or only fixes,
   mods that change nothing in the gameplay and mods with "vanilla_plus" (owner 2026-10-25) ("Vanilla & Vanilla+").
   Launcher features are not world mods. The world kind only: the world check, flow K and the HELLO hash use the set. */
int dsl_world_is_modded(const char *guid) {
    const char *ids[MAXM]; int n = dsl_world_mods(guid, ids, MAXM);
    for (int i = 0; i < n; i++) {
        if (dsl_mod_is_feature(ids[i])) continue;
        const DSLModInfo *m = mod_by_id(ids[i]);
        if (m && (m->fix || m->vanilla_plus)) continue;               /* a fix for a bug of the game (owner 2026-10-24), Vanilla+ (10-25) */
        if (!m || strcmp(m->scope, "client")) return 1;
    }
    return 0;
}
/* the launcher features that are on (the same for every world) */
int dsl_features_on(const char **ids, int max) {
    int n = 0; mods_scan();
    for (int i = 0; i < nmi && n < max; i++) if (MI[i].enabled && !MI[i].hidden && MI[i].feature) ids[n++] = MI[i].id;
    return n;
}
int dsl_world_set_mods(const char *guid, const char *const *ids, int n) {
    if (!guid || !*guid) return -1;
    char k0[512], k[512]; key_of(ids, n, k0, sizeof k0);
    { const char *wk = world_key(guid); char d[512];                  /* custom difficulty: by the world's level, not a switch */
      if (!world_level_known(guid) && wk) {                            /* level not known: as the world had it */
          snprintf(d, sizeof d, "%s", k0);
          for (int i = 0; i < nmi; i++) if (MI_diff[i] && in_plus(wk, MI[i].id)) plus_add(d, sizeof d, MI[i].id);
          key_norm(d, k0, sizeof k0);
      }
      key_diff(guid, k0, 0, k, sizeof k); }
    /* fixes: one switched off here becomes the world's own off when it is on in all worlds or was on in this world (the
       world plays as the window showed it); an off stays while the fix stays off; on = no off. Offs of fixes not
       installed now are kept. */
    char offs[512] = "", old[512], was[512]; snprintf(old, sizeof old, "%s", world_offs(guid));
    { const char *wk = world_key(guid); snprintf(was, sizeof was, "%s", wk ? wk : run_key); }
    for (char *sv = NULL, *t = strtok_r(old, "+", &sv); t; t = strtok_r(NULL, "+", &sv)) {
        const DSLModInfo *m = mod_by_id(t);
        int on = 0; for (int j = 0; j < n; j++) on |= !strcmp(ids[j], t);
        if (*t && (!m || (m->fix && !on))) plus_add(offs, sizeof offs, t);
    }
    for (int i = 0; i < nmi; i++) {
        if (!MI[i].fix || strcmp(MI[i].scope, "gameplay")) continue;
        int on = 0; for (int j = 0; j < n; j++) on |= !strcmp(ids[j], MI[i].id);
        if (!on && (in_plus(was, MI[i].id) || dsl_fix_all_worlds(MI[i].id))) plus_add(offs, sizeof offs, MI[i].id);
    }
    world_offs_put(guid, offs);
    worlds_save(guid, k); dsl_log("reg: world %s set to %s%s%s", guid, k, offs[0] ? ", fixes off: " : "", offs);
    dsl_rows_refresh(); return 0;
}
/* the multiplayer mod's single-player copy of a world (owner 2026-10-25): the copy `to` plays with the mods of `from` (its
   stored set and its fixes turned off; an unregistered `from` = the set that runs now, as world_frame would register it).
   0 ok, -1 bad call / worlds.json unreadable. */
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int dsl_world_copy_mods(const char *from, const char *to) {
    if (!from || !*from || !to || !*to) return -1;
    worlds_load();
    if (worlds_bad) return -1;
    char key[512], offs[512] = "";
    JNode *w = json_get(worlds_doc, "worlds"); const char *k = NULL;
    if (w && w->t == JN_OBJ) for (JNode *c = w->child; c; c = c->next) if (c->key && !strcasecmp(c->key, from) && c->t == JN_STR) { k = c->str; break; }
    char rk[512]; key_diff(from, run_key, 1, rk, sizeof rk);         /* custom difficulty: as `from` uses it (Hell only on Hell) */
    snprintf(key, sizeof key, "%s", k ? k : rk);
    for (int i = 0; i < nlv; i++) if (!strcasecmp(LV[i].guid, from)) { dsl_world_note_level(to, LV[i].c, LV[i].m); break; }   /* same world data, same level */
    if (k) snprintf(offs, sizeof offs, "%s", world_offs(from));
    else for (int i = 0; i < nmi; i++) if (fix_default(i) && !in_plus(run_key, MI[i].id)) plus_add(offs, sizeof offs, MI[i].id);
    world_offs_put(to, offs);
    worlds_save(to, key);
    dsl_log("reg: world %s copied from %s: mods %s%s%s", to, from, key, offs[0] ? ", fixes off: " : "", offs);
    dsl_rows_refresh();
    return 0;
}
void dsl_world_change(const char *guid, const char *save_name) {
    if (dsl_win_open_world) dsl_win_open_world(guid, save_name);
    else if (dsl_win_open) dsl_win_open(NULL);
    else dsl_log("reg: world [Change]: no Mods window in this build");
}
int dsl_newgame_levels(int *combat, int *management);
/* the New Game screen's "Mods of this world" (ui/dsl_newgame.c): the running set as the world made there will use it (Hell
   only when a level chosen is Hell), one name per line ("none" when empty); returns how many */
int dsl_newworld_names(char *out, size_t cap) {
    int c = -1, m = -1; dsl_newgame_levels(&c, &m); dsl_world_note_level("new", c, m);
    char k[512]; key_diff("new", run_key, 1, k, sizeof k);
    out[0] = 0;
    if (!strcmp(k, "vanilla")) { snprintf(out, cap, "%s", TR("none")); return 0; }
    int n = 0; char one[300];
    for (char *sv = NULL, *t = strtok_r(k, "+", &sv); t; t = strtok_r(NULL, "+", &sv), n++) {
        const DSLModInfo *mi = mod_by_id(t);
        if (mi) snprintf(one, sizeof one, "%s", mi->name); else snprintf(one, sizeof one, TR("%s (not installed)"), t);
        if (n) strlcat(out, "\n", cap);
        strlcat(out, one, cap);
    }
    dsi_utf8_fix_end(out);
    return n;
}
void dsl_newworld_change(void) { if (dsl_win_open) dsl_win_open(NULL); else dsl_log("reg: New Campaign [Change]: no Mods window in this build"); }

static Obj mainflow(void);
static int32_t off_scene = -1, off_cd = -1;
static int world_guid(char *out);
/* ------------------------------------------------------------------ profiles + Apply (flow J) */
static char session_why[MAXP_REG][200];
const char *dsl_busy_reason(void) {
    for (int i = 0; i < MAXP_REG; i++) if (session_why[i][0]) return session_why[i];
    if (dsl_soft_busy()) return TR("mods are being applied");      /* A during a restart, B until swap + fade in are done */
    return NULL;
}
static void session_busy_set(DSLPlugin *self, const char *reason) {
    int i = dsl_plugin_index(self); if (i < 0 || i >= MAXP_REG) return;
    int was = session_why[i][0] != 0;
    snprintf(session_why[i], sizeof session_why[i], "%s", reason ? reason : "");
    if (was != (reason != NULL)) dsl_log("reg: session busy %s by %s%s%s", reason ? "SET" : "cleared", dsl_plugin_name(self), reason ? ": " : "", reason ? reason : "");
}
static const char *session_busy_get(void) { return dsl_busy_reason(); }

/* write a profile with these ids enabled; order: the running order first, then the rest */
static int write_profile(const char *path, const char *const *ids, int n) {
    size_t cap = 8192, k = 0; char *b = malloc(cap); char e[200];
    k += (size_t)snprintf(b + k, cap - k, "{\"enabled\":[");
    for (int i = 0; i < n; i++) { jesc(e, sizeof e, ids[i]); k += (size_t)snprintf(b + k, cap - k, "%s\"%s\"", i ? "," : "", e); }
    k += (size_t)snprintf(b + k, cap - k, "],\"order\":[");
    for (int i = 0; i < n; i++) { jesc(e, sizeof e, ids[i]); k += (size_t)snprintf(b + k, cap - k, "%s\"%s\"", i ? "," : "", e); }
    k += (size_t)snprintf(b + k, cap - k, "]");
    /* the gameplay values the new game starts with (mod settings: the Settings page's drafts, else the running ones) */
    char *st = malloc(1 << 16);
    if (st && dsl_settings_profile_member(st, 1 << 16) > 0) { size_t L = strlen(st); if (k + L + 8 > cap) { cap = k + L + 8; b = realloc(b, cap); } k += (size_t)snprintf(b + k, cap - k, ",%s", st); }
    free(st);
    snprintf(b + k, cap - k, "}\n");
    int r = put_file(path, b); free(b); return r;
}
static int tip_first_run;
static char bp_ids[MAXM][64], bp_env[700]; static int bp_n = -1, bp_kind;   /* bp_kind: the .NET host being installed */
static void bie_pending_set(const char *const *ids, int n, const char *env) {
    bp_n = 0; for (int i = 0; i < n && i < MAXM; i++) snprintf(bp_ids[bp_n++], 64, "%s", ids[i]);
    snprintf(bp_env, sizeof bp_env, "%s", env ? env : "");
}
/* soft restart (dsl_soft.c): Apply only at the title (owner 2026-10-16 #10); prewarm = a hidden new game started while
   the Mods window has unapplied changes, so Apply only swaps */
extern int dsl_soft_prewarm(const char *profile, const char *env_lines);
extern void dsl_soft_prewarm_stop(void);
extern int dsl_soft_prewarm_state(void);
int dsl_reg_scene(void) { Obj mf = mainflow(); return mf && off_scene >= 0 ? AT(mf, off_scene, int32_t) : -1; }
#define TITLE_ONLY TR("Go back to the title screen to change mods.")
/* the set to start: pending.json + the env lines for the new process. 0 = ready, 1 = its .NET host (*kind_out: BepInEx or
   MelonLoader, src/dsl_ml.h) must be downloaded first,
   -1 = refused (why). Apply and prewarm build both the same way, so a prewarmed game matches the Apply. */
static int prepare(const char *const *ids, int n, const char *env, char *p, size_t pcap, char *envl, size_t ecap,
                   char *kk, size_t kcap, int *first_out, int *kind_out, char *why, size_t cap) {
    /* keep client mods the player cannot see (hidden) as they are */
    const char *all[MAXM]; int k = 0;
    for (int i = 0; i < n && k < MAXM; i++) all[k++] = ids[i];
    for (int i = 0; i < nmi && k < MAXM; i++) if (MI[i].hidden && MI[i].enabled) { int dup = 0; for (int j = 0; j < k; j++) dup |= !strcmp(all[j], MI[i].id); if (!dup) all[k++] = MI[i].id; }
    dpath(p, pcap, "pending.json");
    if (write_profile(p, all, k)) { dsl_log("reg: cannot write %s", p); snprintf(why, cap, "%s", TR("the mod settings could not be saved")); return -1; }
    key_of(all, k, kk, kcap);
    /* BepInEx plugins or MelonLoader mods of the new set (profile order; one .NET host per set, src/dsl_ml.h): install at
       first use, the host's env for the new process */
    /* a mod whose requires / optional / conflicts fail in the new set does not run there (dsl_deps.h): no .NET host for it */
    int dok[MAXM]; dsl_deps_for_set(all, k, dok);
    const char *kinds[MAXM];
    for (int i = 0; i < k; i++) { const DSLModInfo *m = mod_by_id(all[i]); kinds[i] = !dok[i] ? "off" : m ? m->kind : "code"; if (!dok[i]) dsl_log("reg: %s: its dependencies fail in the new set: not run", all[i]); }
    int nk = dsl_net_check(kinds, k, why, cap);
    if (nk < 0) return -1;
    *kind_out = nk;
    char pdirs[MAXM][1300]; const char *pd[MAXM]; int nb = 0;
    for (int i = 0; i < k; i++) if (nk && dsl_net_kind(kinds[i]) == nk) { snprintf(pdirs[nb], sizeof pdirs[nb], "%s/%s", dsl_mods_dir(), all[i]); pd[nb] = pdirs[nb]; nb++; }
    int bs = nk ? dsl_net_state(nk) : 2;
    const char *hn = dsl_net_name(nk);
    if (nb && bs < 0) { dsl_log("reg: no %s folder in this start", hn); snprintf(why, cap, TR("%s mods need the Dungeon Settlers Modded app"), hn); return -1; }
    if (nb && bs == 0) return 1;
    char bie[4096] = ""; if (dsl_net_env(nk, pd, nb, bie, sizeof bie)) bie[0] = 0;
    int first = nb && bs == 1;                                         /* interop generated in the new process: ~1.5 min */
    *first_out = first;
    snprintf(envl, ecap, "DSML_MODSET=%s\nDSL_MODSET_OWN=1\n%s%s%s", kk, bie, first ? "DSL_SOFT_TIMEOUT=300\n" : "", env ? env : "");
    return 0;
}
static int apply_ids(const char *const *ids, int n, const char *env, char *why, size_t cap) {
    const char *busy = dsl_busy_reason();
    if (busy) { snprintf(why, cap, "%s", busy); dsl_log("reg: Apply refused: %s", busy); return -1; }
    if (dsl_reg_scene() != 1) { snprintf(why, cap, "%s", TITLE_ONLY); dsl_log("reg: Apply refused: not at the title (scene %d)", dsl_reg_scene()); return -1; }
    char p[1200], envl[6000], kk[512]; int first = 0, nk = 0;
    int r = prepare(ids, n, env, p, sizeof p, envl, sizeof envl, kk, sizeof kk, &first, &nk, why, cap);
    if (r < 0) { dsl_log("reg: Apply refused: %s", why); return -1; }
    if (r == 1) {                                                      /* first use: download, then this Apply runs again */
        const char *hn = dsl_net_name(nk);
        if (dsl_net_install_start(nk)) { snprintf(why, cap, TR("the %s download could not start"), hn); return -1; }
        bie_pending_set(ids, n, env); bp_kind = nk;
        const DSLUi *u = dsl_ui_kit(); char t[300];
        snprintf(t, sizeof t, TR("Preparing mods (first start)\nDownloading %s (%s). The new mods are applied when it is ready."), hn, dsl_net_size(nk)); dsi_utf8_fix_end(t);
        if (u) u->notice(t, TR("OK"), NULL, NULL, NULL, NULL);
        dsl_log("reg: Apply waits for the %s download", hn); return 0;
    }
    if (first) { setenv("DSL_SOFT_TIMEOUT", "300", 1); tip_first_run = nk; } else { unsetenv("DSL_SOFT_TIMEOUT"); tip_first_run = 0; }
    dsl_log("reg: Apply: %s -> soft restart%s", kk, dsl_soft_prewarm_state() ? " (a prewarmed game waits)" : "");
    if (dsl_soft_apply(p, envl)) { snprintf(why, cap, "%s", TR("mods cannot be applied right now (the game is loading, or mods are being applied)")); return -1; }
    return 0;
}

/* prewarm: the Mods window reports its switches (dsl_mods_draft); 1.5 s after the last change a hidden game starts
   with that set. A newer change replaces it; the window closed without Apply, or the set back to the running one,
   stops it. Default on (owner's choice logged in MORNING_QUESTIONS.md, v13 loaderui); DSL_PREWARM=0 turns it off. */
static char dr_ids[MAXM][64]; static int dr_n = -1, dr_started; static double dr_t;
static double mono_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
void dsl_mods_draft(const char *const *ids, int n) {
    if (n < 0) { if (dr_n >= 0 || dsl_soft_prewarm_state()) dsl_log("reg: prewarm: draft dropped"); dr_n = -1; if (dsl_soft_prewarm_state()) dsl_soft_prewarm_stop(); return; }
    dr_n = 0; for (int i = 0; i < n && i < MAXM; i++) snprintf(dr_ids[dr_n++], 64, "%s", ids[i]);
    dr_t = mono_s(); dr_started = 0;
}
static int draft_is_running(void) {
    if (dsl_settings_pending(NULL)) return 0;                         /* a gameplay setting changed: another set */
    for (int i = 0; i < nmi; i++) {
        if (MI[i].hidden || MI_diff[i]) continue;                     /* a custom difficulty runs anyway (no switch) */
        int in = 0; for (int j = 0; j < dr_n; j++) in |= !strcmp(dr_ids[j], MI[i].id);
        if (in != (MI[i].enabled != 0)) return 0;
    }
    return 1;
}
static void prewarm_frame(void) {
    if (dr_n < 0 || dr_started || mono_s() - dr_t < 1.5) return;
    dr_started = 1;
    if (!dsl_fastboot_part("DSL_PREWARM")) return;                 /* "0" off; unset: Windows off, Mac on (dsl_fastboot.c) */
    if (draft_is_running()) { if (dsl_soft_prewarm_state()) { dsl_log("reg: prewarm: switches back to the running set: stopped"); dsl_soft_prewarm_stop(); } return; }
    if (dsl_busy_reason() || dsl_reg_scene() != 1) return;
    const char *ids[MAXM]; for (int i = 0; i < dr_n; i++) ids[i] = dr_ids[i];
    char p[1200], envl[6000], kk[512], why[300] = ""; int first = 0, nk = 0;
    int r = prepare(ids, dr_n, NULL, p, sizeof p, envl, sizeof envl, kk, sizeof kk, &first, &nk, why, sizeof why);
    if (r != 0 || first) { dsl_log("reg: prewarm: not for this set (%s%s)", r < 0 ? why : dsl_net_name(nk), r < 0 ? "" : " prepares first"); if (dsl_soft_prewarm_state()) dsl_soft_prewarm_stop(); return; }
    if (dsl_soft_prewarm(p, envl)) dsl_log("reg: prewarm: refused for %s", kk);
    else dsl_log("reg: prewarm: a hidden game starts with %s", kk);
}
int dsl_apply_mods(const char *const *ids, int n, char *why, size_t cap) {
    char w[300] = ""; if (!why) { why = w; cap = sizeof w; }
    return apply_ids(ids, n, NULL, why, cap);
}
/* the same with env lines for the new process ("Get the host's mods": DSMP_REJOIN=...) */
int dsl_apply_mods_env(const char *const *ids, int n, const char *env, char *why, size_t cap) {
    char w[300] = ""; if (!why) { why = w; cap = sizeof w; }
    return apply_ids(ids, n, env, why, cap);
}
/* B of a soft restart started from pending.json: once on screen, pending.json becomes active.json */
static void commit_pending(void) {
    static int done; if (done || dsl_soft_wanted()) return; done = 1;
    const char *p = dsl_profile_file(); if (!p) return;
    size_t L = strlen(p); if (L < 12 || strcmp(p + L - 12, "pending.json")) return;
    char act[1200]; dpath(act, sizeof act, "active.json");
    char *t = slurp(p, 1 << 20); if (!t) return;
    if (put_file(act, t)) dsl_log("reg: cannot write %s", act);
    else dsl_log("reg: the new mods are on: pending.json -> active.json");
    free(t);
    dsl_settings_commit();                                             /* the Apply's gameplay settings -> settings.json */
}

/* ------------------------------------------------------------------ groups */
#define MAXG 32
static struct { char name[80]; char ids[MAXM][64]; int n; } G[MAXG]; static int ng, groups_read;
static void groups_load(void) {
    if (groups_read) return; groups_read = 1;
    char p[1200]; dpath(p, sizeof p, "groups.json");
    char *t = slurp(p, 1 << 20); JNode *j = t ? json_parse(t) : NULL; free(t);
    JNode *a = json_get(j, "groups");
    if (a && a->t == JN_ARR) for (JNode *g = a->child; g && ng < MAXG; g = g->next) {
        const char *nm = json_str(g, "name"); JNode *m = json_get(g, "mods"); if (!nm) continue;
        snprintf(G[ng].name, sizeof G[ng].name, "%s", nm); G[ng].n = 0;
        if (m && m->t == JN_ARR) for (JNode *c = m->child; c && G[ng].n < MAXM; c = c->next) if (c->t == JN_STR) snprintf(G[ng].ids[G[ng].n++], 64, "%s", c->str);
        ng++;
    }
    json_free(j);
}
static int groups_save(void) {
    size_t cap = 1 << 16, k = 0; char *b = malloc(cap); char e[200];
    k += (size_t)snprintf(b + k, cap - k, "{\"groups\":[");
    for (int i = 0; i < ng; i++) {
        jesc(e, sizeof e, G[i].name); k += (size_t)snprintf(b + k, cap - k, "%s\n {\"name\":\"%s\",\"mods\":[", i ? "," : "", e);
        for (int j = 0; j < G[i].n; j++) { jesc(e, sizeof e, G[i].ids[j]); k += (size_t)snprintf(b + k, cap - k, "%s\"%s\"", j ? "," : "", e); }
        k += (size_t)snprintf(b + k, cap - k, "]}");
    }
    snprintf(b + k, cap - k, "\n]}\n");
    char p[1200]; dpath(p, sizeof p, "groups.json");
    int r = put_file(p, b); free(b); return r;
}
int dsl_groups_count(void) { groups_load(); return ng; }
const char *dsl_group_name(int i) { groups_load(); return i >= 0 && i < ng ? G[i].name : NULL; }
int dsl_group_mods(int i, const char **ids, int max) {
    groups_load(); if (i < 0 || i >= ng) return 0;
    int n = 0; for (; n < G[i].n && n < max; n++) ids[n] = G[i].ids[n];
    return n;
}
int dsl_group_save(const char *name, const char *const *ids, int n) {
    groups_load(); if (!name || !*name) return -1;
    int i = 0; while (i < ng && strcmp(G[i].name, name)) i++;
    if (i == ng) { if (ng >= MAXG) return -1; ng++; }
    snprintf(G[i].name, sizeof G[i].name, "%s", name); G[i].n = 0;
    for (int j = 0; j < n && j < MAXM; j++) snprintf(G[i].ids[G[i].n++], 64, "%s", ids[j]);
    dsl_log("reg: group '%s' saved (%d mod(s))", name, n);
    return groups_save();
}
int dsl_group_delete(const char *name) {
    groups_load();
    for (int i = 0; i < ng; i++) if (!strcmp(G[i].name, name)) {
        memmove(&G[i], &G[i + 1], (size_t)(ng - i - 1) * sizeof G[0]); ng--;
        dsl_log("reg: group '%s' deleted", name);
        return groups_save();
    }
    return -1;
}

/* ------------------------------------------------------------------ tabs */
#define MAXTAB 16
static DSLTab T[MAXTAB]; static char T_owner[MAXTAB][64]; static int ntab;
static int tab_add(DSLPlugin *self, const DSLTab *t) {
    if (!t || !t->id || !t->title || !t->open || ntab >= MAXTAB) return -1;
    for (int i = 0; i < ntab; i++) if (!strcmp(T[i].id, t->id)) { dsl_log("reg: tab '%s' refused: id taken by %s", t->id, T_owner[i]); return -1; }
    int at = ntab; while (at > 0 && T[at - 1].order > t->order) at--;
    memmove(&T[at + 1], &T[at], (size_t)(ntab - at) * sizeof T[0]); memmove(T_owner[at + 1], T_owner[at], (size_t)(ntab - at) * sizeof T_owner[0]);
    T[at] = *t; T[at].id = xstrdup(t->id); T[at].title = xstrdup(t->title);
    snprintf(T_owner[at], sizeof T_owner[at], "%s", dsl_plugin_name(self));
    ntab++;
    dsl_log("reg: tab '%s' (%s, order %d) added by %s", t->title, t->id, t->order, T_owner[at]);
    return 0;
}
int dsl_tab_count(void) { return ntab; }
const DSLTab *dsl_tab_at(int i) { return i >= 0 && i < ntab ? &T[i] : NULL; }
const char *dsl_tab_owner(int i) { return i >= 0 && i < ntab ? T_owner[i] : NULL; }
static int menu_open(const char *tab) {
    if (!dsl_win_open) { dsl_log("reg: menu_open(%s): no Mods window in this build", tab ? tab : "mods"); return -1; }
    dsl_win_open(tab); return 0;
}

/* ------------------------------------------------------------------ title buttons */
static void on_title_mods(void *ud) { (void)ud; dsl_log("reg: title Mods clicked"); menu_open(NULL); }
static int title_add(DSLPlugin *self, const char *label, int order, const char *tip, void (*click)(void *), void *ud) {
    if (!label || !click) return -1;
    return dsl_ui_title_add(dsl_plugin_name(self), label, order, tip, click, ud);
}
static int title_icon(DSLPlugin *self, void (*click)(void *), void *ud, float size, void (*fill)(void *, DSLObj, int)) {
    if (!click) return -1;
    return dsl_ui_title_icon(dsl_plugin_name(self), click, ud, size, fill);
}

/* ------------------------------------------------------------------ load rows (dsl_rows.c) */
static int row_add(DSLPlugin *self, const DSLLoadRow *r) { return dsl_rows_add(dsl_plugin_name(self), r); }
static int row_filter(DSLPlugin *self, const DSLLoadFilter *f) { return dsl_rows_filter(dsl_plugin_name(self), f); }

/* ------------------------------------------------------------------ the open world: register it; flow K */
static int world_guid(char *out) {                                     /* ClanDataContainer.CampaignGuid of the open world */
    Obj mf = mainflow(); if (!mf || off_cd < 0) return 0;
    Obj cd = AT(mf, off_cd, Obj), cl = cd ? AT(cd, 0x50, Obj) : NULL; if (!cl) return 0;
    const uint8_t *b = (const uint8_t *)cl + 0x10; int any = 0; for (int i = 0; i < 16; i++) any |= b[i];
    if (!any) return 0;
    snprintf(out, 40, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             *(const uint32_t *)b, *(const uint16_t *)(b + 4), *(const uint16_t *)(b + 6), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return 1;
}
static Obj mainflow(void) { return dsl_soft_mainflow(); }

/* flow K: a refused world load -> ask, then soft restart into it with the world's mods */
static char k_save[512], k_guid[40], k_key[512]; static int k_pending; static int k_frames;
void dsl_world_mismatch(const char *save_name, const char *guid, const char *key) {
    snprintf(k_save, sizeof k_save, "%s", save_name); snprintf(k_guid, sizeof k_guid, "%s", guid); snprintf(k_key, sizeof k_key, "%s", key);
    k_pending = 1; k_frames = 0;
}
static void k_ok(void *ud, DSLObj w) {
    (void)ud; (void)w;
    const char *ids[MAXM]; int n = 0; char b[512]; snprintf(b, sizeof b, "%s", k_key);
    if (strcmp(b, "vanilla")) for (char *sv = NULL, *t = strtok_r(b, "+", &sv); t && n < MAXM; t = strtok_r(NULL, "+", &sv)) if (!dsl_mod_is_feature(t)) ids[n++] = t;
    for (int i = 0; i < nmi && n < MAXM; i++)                           /* keep client mods and launcher features (not in the key) */
        if (MI[i].enabled && (!strcmp(MI[i].scope, "client") || MI[i].feature)) {
            int dup = 0; for (int j = 0; j < n; j++) dup |= !strcmp(ids[j], MI[i].id);
            if (!dup) ids[n++] = MI[i].id;
        }
    char env[700]; snprintf(env, sizeof env, "DSL_SOFT_LOAD=%s\n", k_save);
    char why[300] = "";
    dsl_log("reg: world %s: load it with its mods (%s)", k_guid, k_key);
    if (apply_ids(ids, n, env, why, sizeof why)) {
        const DSLUi *u = dsl_ui_kit(); char t[500]; snprintf(t, sizeof t, TR("The world cannot be loaded now: %s"), why); dsi_utf8_fix_end(t);
        if (u) u->notice(t, TR("OK"), NULL, NULL, NULL, NULL);
    }
}
static void k_cancel(void *ud, DSLObj w) { (void)ud; (void)w; dsl_log("reg: world %s: load cancelled", k_guid); }
static void k_frame(void) {
    if (!k_pending || ++k_frames < 3) return;                           /* after the stock refusal notice is up */
    k_pending = 0;
    const DSLUi *u = dsl_ui_kit(); if (!u) return;
    char nm[400]; int missing = key_names(k_key, nm, sizeof nm) > 0;
    char t[700];
    if (missing) { snprintf(t, sizeof t, TR("This world uses: %s.\nInstall the missing mods to load it."), nm); dsi_utf8_fix_end(t); u->notice(t, TR("OK"), NULL, NULL, NULL, NULL); return; }
    const char *busy = dsl_busy_reason();
    if (busy) { snprintf(t, sizeof t, TR("This world uses: %s.\nIt cannot be loaded now: %s"), nm, busy); dsi_utf8_fix_end(t); u->notice(t, TR("OK"), NULL, NULL, NULL, NULL); return; }
    snprintf(t, sizeof t, TR("This world uses: %s.\nLoad it with them?"), nm); dsi_utf8_fix_end(t);
    u->notice(t, TR("Load"), TR("Cancel"), k_ok, k_cancel, NULL);
}

static char seen_guid[40];
static int32_t off_cset = -1, off_start = -1, off_lc = -1, off_lm = -1;   /* MainFlow._campaignSetting -> its CampaignStartSetting */
static int open_levels(int *c, int *m) {                                  /* the open world's combat / management level */
    Obj mf = mainflow(); if (!mf || off_cset < 0 || off_start < 0 || off_lc < 0 || off_lm < 0) return 0;
    Obj cs = AT(mf, off_cset, Obj), st = cs ? AT(cs, off_start, Obj) : NULL; if (!st) return 0;
    *c = AT(st, off_lc, int32_t); *m = AT(st, off_lm, int32_t);
    return 1;
}
int dsl_world_is_temporary(const char *guid);                          /* dsl_world.c: a module's temporary world */
static void world_frame(void) {
    static unsigned f; if (++f % 5) return;
    Obj mf = mainflow(); int sc = mf && off_scene >= 0 ? AT(mf, off_scene, int32_t) : -1;
    if (sc == 1) dsl_world_hello("");                                     /* the title: the multiplayer set as the profile says */
    if (sc != 2) { seen_guid[0] = 0; return; }
    char g[40]; if (!world_guid(g) || !strcmp(g, seen_guid)) return;
    snprintf(seen_guid, sizeof seen_guid, "%s", g);
    int c, m; if (open_levels(&c, &m)) dsl_world_note_level(g, c, m);
    dsl_world_hello(g);
    if (dsl_world_is_temporary(g)) { dsl_log("reg: world %s is a mod's temporary world: not registered", g); return; }
    char rk[512]; key_diff(g, run_key, 1, rk, sizeof rk);                 /* the running set as this world uses it (Hell: only on Hell) */
    const char *k = world_key(g);
    if (!k) {                                       /* a fix on in all worlds that does not run now: off for this world, so
                                                       the world keeps the set it was made with */
        char offs[512] = ""; for (int i = 0; i < nmi; i++) if (fix_default(i) && !in_plus(run_key, MI[i].id)) plus_add(offs, sizeof offs, MI[i].id);
        world_offs_put(g, offs);
        worlds_save(g, rk); dsl_log("reg: world %s registered with this mod set (%s)%s%s", g, rk, offs[0] ? ", fixes off: " : "", offs);
    }
    else if (!dsl_key_same(k, rk)) dsl_log("reg: world %s is open with %s, registered with %s (multiplayer guest or a launcher load)", g, rk, k);
}

/* ------------------------------------------------------------------ test only (DSL_SOFT_TEST=1, never in a player's start) */
/* <log dir>/softrestart/reg_request.json: {"cmd":"k_ok"} = the "Load it with them?" box answered Load; {"cmd":"apply",
   "ids":[..]} = Mods tab Apply; {"cmd":"mods"} = the title "Mods" click; {"cmd":"world_set","guid":..,"ids":[..]};
   {"cmd":"warn_load"} / {"cmd":"warn_cancel"} = the game-save warning (dsl_saves.c) answered */
/* v13 checks: {"cmd":"title"} logs the title column; {"cmd":"hover","what":"title:<label>" | "row:<i>" | "win:<control>" |
   "pn:<k>" (the Load list's filter box k)}
   hovers that element (no mouse) and logs the hover card's rect and its panel's rect 20 frames later */
DSLObj dsl_ui_test_title(const char *find, char *out, size_t cap);
void dsl_ui_test_hover(DSLObj target);
int dsl_ui_test_tip(char *title, size_t cap);
int dsl_ui_test_tip_rects(float card[4], float panel[4]);
void dsl_ui_test_tip_body(char *out, size_t cap);
size_t dsl_ui_test_label(DSLObj o, char *out, size_t cap);
DSLObj dsl_rows_test_line(int i);
DSLObj dsl_rows_test_pn(int k);
/* weak default: builds without loader/ui (host tests) link; ui/dsl_progress.c gives the real one */
__attribute__((weak)) void dsl_progress_frame(void) {}
/* weak default: builds without loader/ui (host tests) link; ui/dsl_pkg.c gives the real one */
__attribute__((weak)) int dsl_pkg_refresh(char *fail, size_t fcap) { if (fail && fcap) fail[0] = 0; return -2; }
/* weak defaults: builds without dsl_saves.c (host tests) link */
__attribute__((weak)) void dsl_saves_test_answer(int load) { (void)load; }
__attribute__((weak)) void dsl_saves_frame(void) {}
/* weak default: builds without loader/ui (host tests) link; ui/dsl_win.c gives the real one */
__attribute__((weak)) DSLObj dsl_win_test_control(const char *name) { (void)name; return NULL; }
/* weak defaults: builds without loader/ui (host tests) link; ui/dsl_newgame.c gives the real ones */
__attribute__((weak)) void dsl_newgame_install(void) {}
__attribute__((weak)) void dsl_newgame_frame(void) {}
__attribute__((weak)) void dsl_newgame_test_cmd(const JNode *j) { (void)j; }
__attribute__((weak)) int dsl_newgame_levels(int *combat, int *management) { (void)combat; (void)management; return 0; }
int dsl_ui_test_tip_targets(DSLObj *out, int max);
int dsl_ui_test_tip_keep(float el[4], float keep[][4], int max);
static int hv_wait; static char hv_what[120];
/* {"cmd":"tipsweep"}: every hover card of the views open now, one after the other (12 frames each: the stock card places
   itself 2 frames after the show, the kit moves it then); logs each card's rect, its panel and the rects it keeps off */
static DSLObj sw_t[96]; static int sw_n, sw_i = -1, sw_wait;
static void sweep_frame(void) {
    if (sw_i < 0 || --sw_wait > 0) return;
    if (sw_i > 0) {
        float c[4], p[4], e[4], k[8][4]; char tt[200] = "", ks[600] = ""; int on = dsl_ui_test_tip_rects(c, p); dsl_ui_test_tip(tt, sizeof tt);
        int nk = dsl_ui_test_tip_keep(e, k, 8);
        for (int i = 0; i < nk; i++) snprintf(ks + strlen(ks), sizeof ks - strlen(ks), "%s%.0f,%.0f,%.0f,%.0f", i ? ";" : "", k[i][0], k[i][1], k[i][2], k[i][3]);
        dsl_log("reg test: tipsweep %d/%d shown %d title '%s' el %.0f,%.0f,%.0f,%.0f card %.0f,%.0f,%.0f,%.0f panel %.0f,%.0f,%.0f,%.0f keep %s", sw_i, sw_n, on, tt,
                e[0], e[1], e[2], e[3], c[0], c[1], c[2], c[3], p[0], p[1], p[2], p[3], ks);
    }
    if (sw_i >= sw_n) { dsl_ui_test_hover(NULL); dsl_log("reg test: tipsweep done %d", sw_n); sw_i = -1; return; }
    dsl_ui_test_hover(sw_t[sw_i++]); sw_wait = 12;
}
static void hover_test(const char *what) {
    DSLObj t = NULL; char b[2000] = "";
    if (!strncmp(what, "title:", 6)) t = dsl_ui_test_title(what + 6, b, sizeof b);
    else if (!strncmp(what, "row:", 4)) { t = dsl_rows_test_line(atoi(what + 4)); if (t) dsl_ui_test_label(t, b, sizeof b); }
    else if (!strncmp(what, "win:", 4)) t = dsl_win_test_control(what + 4);
    else if (!strncmp(what, "pn:", 3)) { t = dsl_rows_test_pn(atoi(what + 3)); if (t) dsl_ui_test_label(t, b, sizeof b); }
    dsl_log("reg test: hover %s: target %s%s%s", what, t ? "found" : "MISSING", b[0] ? ", text: " : "", b);
    if (!t) return;
    dsl_ui_test_hover(t); hv_wait = 140; snprintf(hv_what, sizeof hv_what, "%s", what);   /* rects at 20 frames, held 2 s for a shot */
}
static void hover_frame(void) {
    if (!hv_wait) return;
    if (--hv_wait == 0) { dsl_ui_test_hover(NULL); return; }
    if (hv_wait != 120) return;
    float c[4], p[4]; char tt[200] = ""; int on = dsl_ui_test_tip_rects(c, p); dsl_ui_test_tip(tt, sizeof tt);
    { char bd[1400]; dsl_ui_test_tip_body(bd, sizeof bd); for (char *q = bd; *q; q++) if (*q == '\n') *q = '|';
      dsl_log("reg test: tip %s body '%s'", hv_what, bd); }
    dsl_log("reg test: tip %s shown %d title '%s' card %.0f,%.0f,%.0f,%.0f panel %.0f,%.0f,%.0f,%.0f", hv_what, on, tt, c[0], c[1], c[2], c[3], p[0], p[1], p[2], p[3]);
}
static void test_frame(void) {
    static int on = -1; static unsigned f;
    if (on < 0) on = getenv("DSL_SOFT_TEST") && *getenv("DSL_SOFT_TEST") == '1';
    if (on) { hover_frame(); sweep_frame(); }
    if (!on || ++f % 5 || dsl_soft_wanted()) return;                  /* a hidden B (prewarm) leaves the requests to A */
    char p[1300]; snprintf(p, sizeof p, "%s/softrestart/reg_request.json", dsl_log_dir());
    char *t = slurp(p, 1 << 16); if (!t) return;
    unlink(p);
    JNode *j = json_parse(t); free(t);
    const char *c = json_str(j, "cmd"); const char *ids[MAXM]; int n = 0;
    JNode *a = json_get(j, "ids"); if (a && a->t == JN_ARR) for (JNode *x = a->child; x && n < MAXM; x = x->next) if (x->t == JN_STR) ids[n++] = x->str;
    dsl_log("reg test: request %s", c ? c : "?");
    if (c && !strcmp(c, "k_ok")) k_ok(NULL, NULL);
    else if (c && !strcmp(c, "apply")) { char why[300] = ""; if (dsl_apply_mods(ids, n, why, sizeof why)) dsl_log("reg test: apply refused: %s", why); }
    else if (c && !strcmp(c, "mods")) on_title_mods(NULL);
    else if (c && !strcmp(c, "tipsweep")) { sw_n = dsl_ui_test_tip_targets(sw_t, 96); sw_i = 0; sw_wait = 1; dsl_log("reg test: tipsweep of %d hover card(s)", sw_n); }
    else if (c && !strcmp(c, "shot")) dsl_log("reg test: shot %s: %s", json_str(j, "path"), dsl_soft_shot(json_str(j, "path")) ? "FAILED" : "ok");
    else if (c && !strcmp(c, "toggles")) { void dsl_rows_test_toggles(int); JNode *o = json_get(j, "on"); dsl_rows_test_toggles(o && o->t == JN_BOOL ? o->b : 1); }
    else if (c && !strcmp(c, "world_set")) dsl_world_set_mods(json_str(j, "guid"), ids, n);
    else if (c && !strcmp(c, "change")) { void dsl_rows_test_change(int); dsl_rows_test_change((int)(json_get(j, "row") ? json_get(j, "row")->num : 0)); }
    else if (c && !strcmp(c, "save_file")) {                           /* the game's own manual save: CampaignSaveLoader.Save */
        void *kmf = dsl_find_class("Refactor.Main", "MainFlow"); int32_t ol = kmf ? dsl_field_offset(kmf, "_campaignSaveLoader") : -1;
        void *kl = dsl_find_class("Refactor", "CampaignSaveLoader");
        void *ms = kl ? dsl_find_method(kl, "Save", 3) : NULL;
        Obj (*snew)(const char *) = dsl_ga_sym("il2cpp_string_new");
        Obj mf = mainflow(), ld = mf && ol >= 0 ? AT(mf, ol, Obj) : NULL; const char *nm = json_str(j, "name") ? json_str(j, "name") : "Manual";
        uint8_t (*sv)(Obj, Obj, uint8_t, Obj, const void *) = ms ? dsl_method_address(ms) : NULL;
        int ok = ld && sv && snew ? sv(ld, snew(nm), 0, snew(nm), ms) : -1;
        if (ok == 0) ok = sv(ld, snew(nm), 1, snew(nm), ms) ? 2 : 0;    /* Ironman worlds take only the autosave kind (as the multiplayer save does) */
        dsl_log("reg test: save_file %s: %s", nm, ok < 0 ? "FAILED (names)" : ok == 2 ? "saved (Ironman: autosave kind)" : ok ? "saved" : "the game refused");
    }
    else if (c && !strcmp(c, "draft")) dsl_mods_draft(ids, json_get(j, "drop") ? -1 : n);
    else if (c && !strcmp(c, "pkg_refresh")) {                         /* Refresh without the window (Windows test, -nographics) */
        char f[400] = ""; int k = dsl_pkg_refresh(f, sizeof f); char l[1200] = "";
        for (int i = 0; i < nmi; i++) { strlcat(l, i ? "," : "", sizeof l); strlcat(l, MI[i].id, sizeof l); strlcat(l, MI[i].enabled ? ":on" : ":off", sizeof l); }
        dsl_log("reg test: pkg_refresh %d new; fail '%s'; list %s", k, f, l);
    }
    else if (c && !strcmp(c, "warn_load")) dsl_saves_test_answer(1);     /* the game-save warning answered Load */
    else if (c && !strcmp(c, "warn_cancel")) dsl_saves_test_answer(0);   /* ... answered Cancel */
    else if (c && !strcmp(c, "title")) { char b[2000]; dsl_ui_test_title(NULL, b, sizeof b); dsl_log("reg test: title %s", b); }
    else if (c && !strcmp(c, "hover")) hover_test(json_str(j, "what") ? json_str(j, "what") : "");
    else if (c && (!strcmp(c, "lf") || !strncmp(c, "lf_", 3))) { void dsl_rows_test_cmd(const char *, const JNode *); dsl_rows_test_cmd(c, j); }   /* the Load list's filter panel */
    else if (c && !strcmp(c, "newgame")) dsl_newgame_test_cmd(j);     /* the New Game screen and its difficulty toggle */
    json_free(j);
}

/* ------------------------------------------------------------------ frame + install */
/* BepInEx: a waiting Apply after the download; "Preparing mods (first start)" on the loading screen; first start done */
static void *(*il_str2)(const char *);
static void loading_tip(const char *text) {
    static void *k_fade, *k_tmp; static int32_t o_lui = -1, o_tip = -1, o_key = -1; static void (*set_text)(void *, void *, const void *);
    static void *(*find)(void *, int, const void *); static void *t_fade;
    if (!k_fade) {
        k_fade = dsl_find_class("Refactor.UI", "ScreenFadeEffectView"); void *k_lui = dsl_find_class("Refactor", "LoadingUI");
        void *k_das = dsl_find_class("Refactor.UI", "DASText"); k_tmp = dsl_find_class("TMPro", "TMP_Text");
        o_lui = k_fade ? dsl_field_offset(k_fade, "_loadingUI") : -1; o_tip = k_lui ? dsl_field_offset(k_lui, "_tipText") : -1;
        o_key = k_das ? dsl_field_offset(k_das, "_useTextKey") : -1;
        void *m = k_tmp ? dsl_find_method(k_tmp, "set_text", 1) : NULL; set_text = m ? dsl_method_address(m) : NULL;
        void *ko = dsl_find_class("UnityEngine", "Object"); void *mf = ko ? dsl_find_method(ko, "FindObjectOfType", 2) : NULL; find = mf ? dsl_method_address(mf) : NULL;
        void *(*ct)(void *) = dsl_ga_sym("il2cpp_class_get_type"); void *(*to)(void *) = dsl_ga_sym("il2cpp_type_get_object");
        if (ct && to && k_fade) t_fade = to(ct(k_fade));
        il_str2 = dsl_ga_sym("il2cpp_string_new");
    }
    static int said;
    if (!find || !t_fade || o_lui < 0 || o_tip < 0 || !set_text || !il_str2) { if (!said++) dsl_log("reg: loading tip off (names: find %p type %p lui %d tip %d set %p)", find, t_fade, o_lui, o_tip, set_text); return; }
    Obj f = find(t_fade, 0, NULL), lui = f ? AT(f, o_lui, Obj) : NULL, tip = lui ? AT(lui, o_tip, Obj) : NULL;
    if (said < 2) { said = 2; dsl_log("reg: loading tip: view %p loading ui %p tip text %p (useTextKey at %d)", f, lui, tip, o_key); }
    if (!tip) return;
    if (o_key >= 0) AT(tip, o_key, uint8_t) = 0;                       /* plain text, not a text key */
    set_text(tip, il_str2(text), NULL);
}
static void bie_frame(void) {
    static unsigned f; ++f;
    if (bp_n >= 0 && f % 15 == 0) {
        char why[300] = ""; int r = dsl_net_install_poll(bp_kind, why, sizeof why);
        if (r == 0) {
            const char *ids[MAXM]; for (int i = 0; i < bp_n; i++) ids[i] = bp_ids[i];
            int n = bp_n; bp_n = -1; char w2[300] = "";
            dsl_log("reg: %s ready: the waiting Apply runs", dsl_net_name(bp_kind));
            { const DSLUi *u = dsl_ui_kit(); if (u && u->notice_close) u->notice_close(); }   /* the "Downloading" box */
            if (apply_ids(ids, n, bp_env[0] ? bp_env : NULL, w2, sizeof w2)) {
                const DSLUi *u = dsl_ui_kit(); char t[500]; snprintf(t, sizeof t, TR("The mods could not be applied: %s"), w2); dsi_utf8_fix_end(t);
                if (u) u->notice(t, TR("OK"), NULL, NULL, NULL, NULL);
            }
        } else if (r < 0) {
            bp_n = -1; const DSLUi *u = dsl_ui_kit(); char t[500]; if (u && u->notice_close) u->notice_close(); snprintf(t, sizeof t, TR("%s could not be installed: %s\nThe mods stay as they were."), dsl_net_name(bp_kind), why); dsi_utf8_fix_end(t);
            if (u) u->notice(t, TR("OK"), NULL, NULL, NULL, NULL);
        }
    }
    if (tip_first_run && dsl_soft_busy() && f % 30 == 0) {
        char t[300]; snprintf(t, sizeof t, TR("Preparing mods (first start): %s prepares its files for this game version. This takes about 2 minutes, once."), dsl_net_name(tip_first_run)); dsi_utf8_fix_end(t);
        loading_tip(t);
    }
    Obj mf = mainflow();
    if (f % 60 == 0 && mf && off_scene >= 0 && AT(mf, off_scene, int32_t) == 1 && !dsl_soft_wanted()) dsl_net_mark_ready();
}

void dsl_frame_hook(void) {
    commit_pending();
    bie_frame();
    prewarm_frame();
    world_frame();
    k_frame();
    dsl_saves_frame();
    test_frame();
    dsl_rows_frame();
    dsl_update_frame();
    dsl_crash_frame();
    dsl_bugrep_frame();
    dsl_hostmods_frame();
    if (dsl_win_frame) dsl_win_frame();
    dsl_newgame_frame();
    dsl_progress_frame();
}

/* the game's language changed (dsl_ui.c tick, after the kit renewed its fonts): mod texts, the loader's title button
   ("Mods"), an open Mods window built again */
void dsl_reg_lang_changed(void) {
    if (mods_scanned) mods_lang();
    if (dsl_ui_kit()) dsl_ui_title_add("loader", TR("Mods"), 100, NULL, on_title_mods, NULL);
    if (dsl_win_relang) dsl_win_relang();
}
static const char *data_dir_get(void) {                 /* for modules: in the form their own fopen opens */
    static char n[1100];
    if (!n[0]) dsl_os_native_path(dsl_reg_data_dir(), n, sizeof n);
    return n;
}
/* before dsl_plugin_init of the modules (they may add tabs / rows / buttons there) */
void dsl_reg_host(DSLHost *h) {
    h->set_session_busy = session_busy_set; h->session_busy = session_busy_get;
    h->menu_add_tab = tab_add; h->menu_open = menu_open; h->title_add_button = title_add; h->title_button_icon = title_icon;
    h->loadrow_add = row_add; h->loadlist_filter = row_filter; h->loadlist_refresh = dsl_rows_refresh;
    h->data_dir = data_dir_get;
}
void dsl_reg_ready(DSLHost *h) {
    dsl_ui_install();
    h->ui = dsl_ui_kit();
    dsl_rows_install();
    void *kmf = dsl_find_class("Refactor.Main", "MainFlow");
    if (kmf) { off_scene = dsl_field_offset(kmf, "_currentScene"); off_cd = dsl_field_offset(kmf, "_campaignData"); off_cset = dsl_field_offset(kmf, "_campaignSetting"); }
    { void *kc = dsl_find_class("Refactor.Main", "CampaignSettingContainer"), *ks = dsl_find_class("Refactor.Main", "CampaignStartSetting");
      if (kc) off_start = dsl_field_offset(kc, "<CampaignStartSetting>k__BackingField");
      if (ks) { off_lc = dsl_field_offset(ks, "_combatDifficulty"); off_lm = dsl_field_offset(ks, "_managementDifficulty"); }
      if (off_cset < 0 || off_start < 0 || off_lc < 0 || off_lm < 0) dsl_log("reg: the open world's difficulty fields not found (%d %d %d %d)", off_cset, off_start, off_lc, off_lm); }
    if (h->ui && dsl_win_init) dsl_win_init();
    if (h->ui) dsl_newgame_install();                                  /* the New Game screen's difficulty toggle */
    if (h->ui) dsl_bugrep_install();                                   /* before the modules: the loader owns the Bug Report buttons */
    if (h->ui) dsl_ui_title_add("loader", TR("Mods"), 100, NULL, on_title_mods, NULL);   /* no hover card (owner 2026-10-16 #1) */
    dsl_log("reg: ready (data %s, kit %s, Mods window %s)", dsl_reg_data_dir(), h->ui ? "on" : "OFF", dsl_win_open ? "on" : "not built");
}
/* after dsl_plugin_init of the modules: the running set is known */
void dsl_reg_after_init(void) {
    mods_scan();
    running_key();
    const char *given = getenv("DSML_MODSET");                         /* an old launcher passes the key itself */
    if (given && *given) snprintf(run_key, sizeof run_key, "%s", given);
    else setenv("DSML_MODSET", run_key, 1);                             /* dsl_world.c compares with it */
    worlds_load();
    const char *old = json_str(worlds_doc, "modset");
    { static char lj[MAXM * 2100]; char ms[65]; set_lines(lj, sizeof lj, ms);
      /* an old launcher that passes DSML_MODSET owns its session file: kept as it is */
      int own = !(given && *given) || (getenv("DSL_MODSET_OWN") && !strcmp(getenv("DSL_MODSET_OWN"), "1"));   /* B of our soft restart */
      hello_own = own; snprintf(hello_ms, sizeof hello_ms, "%s", old && own ? old : ms);
      if (own && (worlds_migrated || !old || strcmp(old, ms) || !json_get(worlds_doc, "modset_lines"))) worlds_save(NULL, NULL); }
    if (dsl_soft_wanted()) {                                           /* the new game of a soft restart: its first progress step */
        extern void dsl_soft_progress(int pct, const char *key, const char *arg);
        char t[512] = ""; int k = 0;
        for (int i = 0; i < nmi; i++) if (MI[i].active == 1 && !MI[i].hidden && !MI_diff[i]) { char sh[128]; dsl_mod_short(MI[i].id, sh, sizeof sh); if (k++) strlcat(t, ", ", sizeof t); strlcat(t, sh, sizeof t); }
        dsi_utf8_fix_end(t);
        if (k) dsl_soft_progress(10, TR_NOOP("Loading %s"), t); else dsl_soft_progress(10, TR_NOOP("Loading mods"), NULL);
    }
    int nb = 0, nm = 0; for (int i = 0; i < nmi; i++) { nb += !strcmp(MI[i].kind, "bepinex") && MI[i].active == 1; nm += !strcmp(MI[i].kind, "melon") && MI[i].active == 1; }
    dsl_log("reg: running gameplay set %s (%d installed mod(s), %d BepInEx plugin(s) running, BepInEx state %d, %d MelonLoader mod(s) running, MelonLoader state %d)",
            run_key, nmi, nb, dsl_bie_state(), nm, dsl_ml_state());
    { char fx[600] = ""; for (int i = 0; i < nmi; i++) if (MI[i].fix) {
          char one[160]; snprintf(one, sizeof one, "%s%s (%s)", fx[0] ? ", " : "", MI[i].id,
                                  strcmp(MI[i].scope, "gameplay") ? "client: the same in all worlds" : fix_default(i) ? "on in all worlds" : "per world");
          strlcat(fx, one, sizeof fx); }
      if (fx[0]) dsl_log("reg: fixes: %s", fx); }
    for (int i = 0; i < nmi; i++) if (MI_diff[i])
        dsl_log("reg: custom difficulty %s (level %d, %s): %s; only worlds on that level use it; at the title the multiplayer set %s it",
                MI[i].id, MI_diff[i], MI_dname[i], MI[i].active == 1 ? "runs" : "does NOT run", MI_profon[i] ? "has" : "leaves out");
    dsl_moddata_after_init();                                          /* data folder per mod + moddata.json */
    dsl_update_start();
    dsl_crash_start();
}
