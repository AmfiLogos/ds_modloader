// Loader core, API 3 services (~/ds_multiplayer/research/MASTER_PLAN.md 2.2 A, flows J and K):
//   - session busy, Mods window tabs, title buttons (the loader's own "Mods"), menu_open
//   - installed mods for the Mods tab (every <mods>/<id>/mod.json, enabled or not), groups, Apply
//   - Apply = <data>/Mods/pending.json + soft restart (dsl_soft.c); the new process makes it active.json once it is on
//     screen (commit only on success; a failed restart leaves active.json as it was)
//   - mods per world: <data>/Mods/worlds.json {"modset": <running key>, "worlds": {guid: key}}; key = the sorted
//     gameplay mod ids joined by "+" ("vanilla" if none). A world is registered with the running key the first time it
//     is open in this process; a world with another key: dsl_world.c refuses the load, then this file asks
//     "This world uses: <names>. Load it with them?" and Load = soft restart straight into that save (flow K).
// Everything here runs on the main thread (dsl_frame_hook from the kit's EventSystem.Update tick, or host calls made
// by modules from their own main-thread code).
#include "dsl_internal.h"
#include "dsl_os.h"
#include "dsl_menu.h"
#include "dsl_bie.h"
#include "dsl_ml.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
static int put_file(const char *path, const char *text) {
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w"); if (!f) return -1;
    fputs(text, f); fflush(f); fsync(fileno(f)); fclose(f);
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

/* ------------------------------------------------------------------ installed mods */
#define MAXM 64
static DSLModInfo MI[MAXM]; static int nmi;
static char MI_short[MAXM][48];                         /* mod.json "short_name" (world rows), else "" */
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
        x->name = xstrdup(json_str(m, "name") ? json_str(m, "name") : id);
        x->version = xstrdup(json_str(m, "version"));
        x->description = xstrdup(json_str(m, "description"));
        const char *kd = json_str(m, "kind"); if (!kd) kd = json_str(m, "type");
        x->kind = xstrdup(kd ? kd : "code");
        snprintf(MI_short[nmi - 1], sizeof MI_short[0], "%s", json_str(m, "short_name") ? json_str(m, "short_name") : "");
        const char *sc = json_str(m, "scope"); x->scope = xstrdup(sc && !strcmp(sc, "client") ? "client" : "gameplay");
        JNode *h = json_get(m, "hidden"); x->hidden = h && h->t == JN_BOOL && h->b;
        int on = 1;
        if (en && en->t == JN_ARR) { on = 0; for (JNode *c = en->child; c; c = c->next) if (c->t == JN_STR && !strcmp(c->str, id)) on = 1; }
        x->enabled = on;
        const char *why = NULL; int st = dsl_mod_state(id, &why);
        if (!strcmp(x->kind, "bepinex")) {                               /* BepInEx plugin: run by the OS start part */
#ifdef _WIN32
            const char *dd = getenv("DOORSTOP_DISABLE");                 /* start/win/bie_win.c (lane L3) */
            int runs = on && !(dd && *dd);
            const char *br = getenv("DSL_BIE_REASON");
            st = runs ? 1 : 0; why = runs ? "" : !on ? "off" : (br && *br) ? br : "BepInEx did not start";
#else
            char pd[1300]; snprintf(pd, sizeof pd, "%s/%s", md, id);   /* Mac: start/mac/dsl_bie_mac.c, its folder in this start */
            st = dsl_bie_running_dir(pd) ? 1 : 0;
            why = dsl_bie_state() < 0 ? "BepInEx is not available in this start" : "BepInEx plugin: not running in this start";
#endif
        } else if (dsl_net_kind(x->kind) == DSL_NET_ML) {               /* MelonLoader mod (lane melon): run by the OS start part */
            char pd[1300]; snprintf(pd, sizeof pd, "%s/%s", md, id);
            const char *mr = getenv("DSL_ML_REASON");
            st = dsl_ml_running_dir(pd) ? 1 : 0;
            why = mr && *mr ? mr : dsl_ml_state() < 0 ? "MelonLoader is not available in this start" : "MelonLoader mod: not running in this start";
        }
        x->active = st == 1 ? 1 : (on ? -1 : 0);
        x->reason = xstrdup(st == 1 ? "" : !on ? "off" : (why ? why : "did not load"));
        json_free(m);
    }
    json_free(prof);
}
int dsl_mods_count(void) { mods_scan(); return nmi; }
/* the short name of a mod for the world rows: mod.json "short_name", else the name cut at a word before 16 characters */
void dsl_mod_short(const char *id, char *out, size_t cap) {
    mods_scan(); out[0] = 0;
    for (int i = 0; i < nmi; i++) if (!strcmp(MI[i].id, id)) {
        if (MI_short[i][0]) { snprintf(out, cap, "%s", MI_short[i]); return; }
        snprintf(out, cap, "%s", MI[i].name);
        if (strlen(out) > 18) { size_t c = 16; while (c > 6 && out[c] != ' ') c--; if (out[c] != ' ') c = 16; snprintf(out + c, cap - c, "..."); }
        return;
    }
    snprintf(out, cap, "%s", id);
}
const char *dsl_mod_name(const char *id) { const DSLModInfo *m = NULL; mods_scan(); for (int i = 0; i < nmi; i++) if (!strcmp(MI[i].id, id)) m = &MI[i]; return m ? m->name : NULL; }
const DSLModInfo *dsl_mods_at(int i) { mods_scan(); return i >= 0 && i < nmi ? &MI[i] : NULL; }
static const DSLModInfo *mod_by_id(const char *id) { mods_scan(); for (int i = 0; i < nmi; i++) if (!strcmp(MI[i].id, id)) return &MI[i]; return NULL; }

/* running gameplay key: sorted ids of the gameplay modules that run in this process. The multiplayer mod itself is
   not in it (its HELLO already checks build + protocol), so a world's key and the join check are the same with or
   without it, and a player without the loader (old friend kit) can still join a host that runs only multiplayer
   (cross-platform finding, ds_launcher NOTES_winlauncher.md). Ids only: the same key on Mac and Windows. */
static char run_key[512];
static int cmpstr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static void key_of(const char *const *ids, int n, char *out, size_t cap) {
    const char *v[MAXM]; int k = 0;
    for (int i = 0; i < n && k < MAXM; i++) {
        if (!strcmp(ids[i], "multiplayer")) continue;
        const DSLModInfo *m = mod_by_id(ids[i]); if (!m || !strcmp(m->scope, "gameplay")) v[k++] = ids[i];
    }
    qsort(v, (size_t)k, sizeof *v, cmpstr);
    out[0] = 0;
    for (int i = 0; i < k; i++) { if (i) strlcat(out, "+", cap); strlcat(out, v[i], cap); }
    if (!k) snprintf(out, cap, "vanilla");
}
/* a stored key in today's form: sorted, without "multiplayer" (left out since l3-win dba8193; older worlds.json files
   have it), "vanilla" when empty. Keeps worlds registered before that change working. */
static void key_norm(const char *in, char *out, size_t cap) {
    char b[512]; snprintf(b, sizeof b, "%s", in ? in : ""); const char *v[MAXM]; int k = 0;
    for (char *sv = NULL, *t = strtok_r(b, "+", &sv); t && k < MAXM; t = strtok_r(NULL, "+", &sv))
        if (*t && strcmp(t, "multiplayer") && strcmp(t, "vanilla")) v[k++] = t;
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
/* "Hell difficulty, Multiplayer" for a key */
static void key_names(const char *key, char *out, size_t cap) {
    out[0] = 0;
    if (!strcmp(key, "vanilla")) { snprintf(out, cap, "none"); return; }
    char b[512]; snprintf(b, sizeof b, "%s", key);
    int i = 0;
    for (char *sv = NULL, *t = strtok_r(b, "+", &sv); t; t = strtok_r(NULL, "+", &sv), i++) {
        const DSLModInfo *m = mod_by_id(t);
        if (i) strlcat(out, ", ", cap);
        strlcat(out, m ? m->name : t, cap);
        if (!m) strlcat(out, " (not installed)", cap);
    }
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
/* world keys written before the multiplayer mod left the key ("multiplayer", "a+multiplayer"): same key without it */
static void strip_mp(JNode *c) {
    if (!c || c->t != JN_STR || !c->str || !strstr(c->str, "multiplayer")) return;
    char out[512] = ""; char b[512]; snprintf(b, sizeof b, "%s", c->str);
    for (char *t = strtok(b, "+"); t; t = strtok(NULL, "+")) if (strcmp(t, "multiplayer")) { if (out[0]) strlcat(out, "+", sizeof out); strlcat(out, t, sizeof out); }
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
static void worlds_load(void) {
    if (worlds_doc) return;
    char *t = slurp(worlds_path(), 8 << 20); worlds_doc = t ? json_parse(t) : NULL; free(t);
    JNode *w = json_get(worlds_doc, "worlds");
    if (w && w->t == JN_OBJ) { for (JNode *c = w->child; c; c = c->next) strip_mp(c); migrate_save_keys(w); }
}
static const char *world_key(const char *guid) {
    worlds_load();
    JNode *w = json_get(worlds_doc, "worlds");
    static char nk[512];
    if (w && w->t == JN_OBJ) for (JNode *c = w->child; c; c = c->next) if (c->key && !strcasecmp(c->key, guid) && c->t == JN_STR) { key_norm(c->str, nk, sizeof nk); return nk; }
    return NULL;
}
/* modset_lines (JSON members) and the HELLO hash of the running gameplay set; see worlds_save */
static void set_lines(char *out, size_t cap, char ms[65]) {
    static char ln[MAXM][2048]; const char *lp[MAXM]; int nl = 0; char hs[1800], e1[2100];
    for (int i = 0; i < nmi && nl < MAXM; i++) if (MI[i].active == 1 && !strcmp(MI[i].scope, "gameplay") && strcmp(MI[i].id, "multiplayer")) {
        extern int dsl_mod_hashes(const char *id, char *out, size_t cap);
        dsl_mod_hashes(MI[i].id, hs, sizeof hs);
        snprintf(ln[nl], sizeof ln[nl], "%s@%s%s%s", MI[i].id, MI[i].version ? MI[i].version : "", hs[0] ? ":" : "", hs); lp[nl] = ln[nl]; nl++;
    }
    qsort(lp, (size_t)nl, sizeof *lp, cmpstr);
    static char joined[MAXM * 2048]; joined[0] = 0; out[0] = 0;
    for (int i = 0; i < nl; i++) {
        jesc(e1, sizeof e1, lp[i]); size_t k = strlen(out); snprintf(out + k, cap - k, "%s\"%s\"", i ? "," : "", e1);
        if (i) strlcat(joined, "\n", sizeof joined); strlcat(joined, lp[i], sizeof joined);
    }
    ms[0] = 0; if (nl) dsl_os_sha256_buf(joined, strlen(joined), ms);
}
static void worlds_save(const char *add_guid, const char *add_key) {
    worlds_load();
    size_t cap = 1 << 16, n = 0; char *b = malloc(cap); char e1[600], e2[600];
#define PUT(...) do { if (cap - n < 4096) { cap *= 2; b = realloc(b, cap); } int k_ = snprintf(b + n, cap - n, __VA_ARGS__); if (k_ > 0) n += (size_t)k_; } while (0)
    JNode *st = worlds_doc;
    PUT("{");
    int first = 1;
    if (st && st->t == JN_OBJ) for (JNode *c = st->child; c; c = c->next) {     /* keep every other member as it is */
        if (!c->key || !strcmp(c->key, "worlds") || !strcmp(c->key, "modset") || !strcmp(c->key, "modset_lines")) continue;
        jesc(e1, sizeof e1, c->key); PUT("%s\"%s\":", first ? "" : ",", e1); first = 0;
        jdump(c, &b, &n, &cap);
    }
    /* top-level "modset" = what the multiplayer HELLO sends: "" when no gameplay mod (its "none"), else the key */
    /* "modset_lines": <id>@<version>:<sha256s of every platform's files> of each running gameplay mod (multiplayer
       left out), sorted; "modset" = sha256 of those lines ('\n'-joined), "" = no gameplay mod. That is what the
       multiplayer HELLO sends and compares: the same on Mac and Windows for the same mods, names in a refusal. */
    { static char lj[MAXM * 2100]; char ms[65]; set_lines(lj, sizeof lj, ms);
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

int dsl_world_label(const char *guid, char *out, size_t cap) {
    const char *k = guid ? world_key(guid) : NULL;
    char nm[400]; key_names(k ? k : run_key, nm, sizeof nm);
    snprintf(out, cap, "Mods: %s", nm);
    return k != NULL;
}
static char world_ids_buf[512];
int dsl_world_mods(const char *guid, const char **ids, int max) {
    const char *k = guid ? world_key(guid) : NULL;
    if (!k) k = run_key;                                              /* unknown world = the mods that run now (lead 2026-10-15) */
    if (!strcmp(k, "vanilla")) return 0;
    snprintf(world_ids_buf, sizeof world_ids_buf, "%s", k); int n = 0;
    for (char *sv = NULL, *t = strtok_r(world_ids_buf, "+", &sv); t && n < max; t = strtok_r(NULL, "+", &sv)) ids[n++] = t;
    return n;
}
/* every mod that is on when this world plays (owner v13 #5): its gameplay set, plus the mods the set leaves out
   because they load with any world (multiplayer, client mods), when they are on */
int dsl_world_mods_all(const char *guid, const char **ids, int max) {
    int n = dsl_world_mods(guid, ids, max);
    for (int i = 0; i < nmi && n < max; i++) {
        if (!MI[i].enabled || (strcmp(MI[i].id, "multiplayer") && strcmp(MI[i].scope, "client"))) continue;
        int dup = 0; for (int j = 0; j < n; j++) dup |= !strcmp(ids[j], MI[i].id);
        if (!dup) ids[n++] = MI[i].id;
    }
    return n;
}
int dsl_world_set_mods(const char *guid, const char *const *ids, int n) {
    if (!guid || !*guid) return -1;
    char k[512]; key_of(ids, n, k, sizeof k);
    worlds_save(guid, k); dsl_log("reg: world %s set to %s", guid, k);
    dsl_rows_refresh(); return 0;
}
void dsl_world_change(const char *guid, const char *save_name) {
    if (dsl_win_open_world) dsl_win_open_world(guid, save_name);
    else if (dsl_win_open) dsl_win_open(NULL);
    else dsl_log("reg: world [Change]: no Mods window in this build");
}
void dsl_newworld_label(char *out, size_t cap) { char nm[400]; key_names(run_key, nm, sizeof nm); snprintf(out, cap, "Mods for this world: %s", nm); }
void dsl_newworld_change(void) { if (dsl_win_open) dsl_win_open(NULL); else dsl_log("reg: New Campaign [Change]: no Mods window in this build"); }

static Obj mainflow(void);
static int32_t off_scene = -1, off_cd = -1;
static int world_guid(char *out);
/* ------------------------------------------------------------------ profiles + Apply (flow J) */
static char session_why[MAXP_REG][200];
const char *dsl_busy_reason(void) {
    for (int i = 0; i < MAXP_REG; i++) if (session_why[i][0]) return session_why[i];
    if (dsl_soft_busy()) return "the game is restarting with new mods";      /* A during a restart, B until swap + fade in are done */
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
    snprintf(b + k, cap - k, "]}\n");
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
#define TITLE_ONLY "Go back to the title screen to change mods."
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
    if (write_profile(p, all, k)) { dsl_log("reg: cannot write %s", p); snprintf(why, cap, "the mod settings could not be saved"); return -1; }
    key_of(all, k, kk, kcap);
    /* BepInEx plugins or MelonLoader mods of the new set (profile order; one .NET host per set, src/dsl_ml.h): install at
       first use, the host's env for the new process */
    const char *kinds[MAXM]; for (int i = 0; i < k; i++) { const DSLModInfo *m = mod_by_id(all[i]); kinds[i] = m ? m->kind : "code"; }
    int nk = dsl_net_check(kinds, k, why, cap);
    if (nk < 0) return -1;
    *kind_out = nk;
    char pdirs[MAXM][1300]; const char *pd[MAXM]; int nb = 0;
    for (int i = 0; i < k; i++) if (nk && dsl_net_kind(kinds[i]) == nk) { snprintf(pdirs[nb], sizeof pdirs[nb], "%s/%s", dsl_mods_dir(), all[i]); pd[nb] = pdirs[nb]; nb++; }
    int bs = nk ? dsl_net_state(nk) : 2;
    const char *hn = dsl_net_name(nk);
    if (nb && bs < 0) { dsl_log("reg: no %s folder in this start", hn); snprintf(why, cap, "%s mods need the Dungeon Settlers Modded app", hn); return -1; }
    if (nb && bs == 0) return 1;
    char bie[4096] = ""; if (dsl_net_env(nk, pd, nb, bie, sizeof bie)) bie[0] = 0;
    int first = nb && bs == 1;                                         /* interop generated in the new process: ~1.5 min */
    *first_out = first;
    snprintf(envl, ecap, "DSML_MODSET=%s\n%s%s%s", kk, bie, first ? "DSL_SOFT_TIMEOUT=300\n" : "", env ? env : "");
    return 0;
}
static int apply_ids(const char *const *ids, int n, const char *env, char *why, size_t cap) {
    const char *busy = dsl_busy_reason();
    if (busy) { snprintf(why, cap, "%s", busy); dsl_log("reg: Apply refused: %s", busy); return -1; }
    if (dsl_reg_scene() != 1) { snprintf(why, cap, TITLE_ONLY); dsl_log("reg: Apply refused: not at the title (scene %d)", dsl_reg_scene()); return -1; }
    char p[1200], envl[6000], kk[512]; int first = 0, nk = 0;
    int r = prepare(ids, n, env, p, sizeof p, envl, sizeof envl, kk, sizeof kk, &first, &nk, why, cap);
    if (r < 0) { dsl_log("reg: Apply refused: %s", why); return -1; }
    if (r == 1) {                                                      /* first use: download, then this Apply runs again */
        const char *hn = dsl_net_name(nk);
        if (dsl_net_install_start(nk)) { snprintf(why, cap, "the %s download could not start", hn); return -1; }
        bie_pending_set(ids, n, env); bp_kind = nk;
        const DSLUi *u = dsl_ui_kit(); char t[300];
        snprintf(t, sizeof t, "Preparing mods (first start)\nDownloading %s (%s). The game restarts with the new mods when it is ready.", hn, dsl_net_size(nk));
        if (u) u->notice(t, "OK", NULL, NULL, NULL, NULL);
        dsl_log("reg: Apply waits for the %s download", hn); return 0;
    }
    if (first) { setenv("DSL_SOFT_TIMEOUT", "300", 1); tip_first_run = nk; } else { unsetenv("DSL_SOFT_TIMEOUT"); tip_first_run = 0; }
    dsl_log("reg: Apply: %s -> soft restart%s", kk, dsl_soft_prewarm_state() ? " (a prewarmed game waits)" : "");
    if (dsl_soft_apply(p, envl)) { snprintf(why, cap, "the game cannot restart right now (loading, or a restart already runs)"); return -1; }
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
    for (int i = 0; i < nmi; i++) {
        if (MI[i].hidden) continue;
        int in = 0; for (int j = 0; j < dr_n; j++) in |= !strcmp(dr_ids[j], MI[i].id);
        if (in != (MI[i].enabled != 0)) return 0;
    }
    return 1;
}
static void prewarm_frame(void) {
    if (dr_n < 0 || dr_started || mono_s() - dr_t < 1.5) return;
    dr_started = 1;
    const char *off = getenv("DSL_PREWARM"); if (off && !strcmp(off, "0")) return;
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
    if (strcmp(b, "vanilla")) for (char *sv = NULL, *t = strtok_r(b, "+", &sv); t && n < MAXM; t = strtok_r(NULL, "+", &sv)) if (strcmp(t, "multiplayer")) ids[n++] = t;
    for (int i = 0; i < nmi && n < MAXM; i++)                           /* keep client mods and multiplayer (not in the key) */
        if (MI[i].enabled && (!strcmp(MI[i].scope, "client") || !strcmp(MI[i].id, "multiplayer"))) {
            int dup = 0; for (int j = 0; j < n; j++) dup |= !strcmp(ids[j], MI[i].id);
            if (!dup) ids[n++] = MI[i].id;
        }
    char env[700]; snprintf(env, sizeof env, "DSL_SOFT_LOAD=%s\n", k_save);
    char why[300] = "";
    dsl_log("reg: world %s: load it with its mods (%s)", k_guid, k_key);
    if (apply_ids(ids, n, env, why, sizeof why)) {
        const DSLUi *u = dsl_ui_kit(); char t[500]; snprintf(t, sizeof t, "The world cannot be loaded now: %s", why);
        if (u) u->notice(t, "OK", NULL, NULL, NULL, NULL);
    }
}
static void k_cancel(void *ud, DSLObj w) { (void)ud; (void)w; dsl_log("reg: world %s: load cancelled", k_guid); }
static void k_frame(void) {
    if (!k_pending || ++k_frames < 3) return;                           /* after the stock refusal notice is up */
    k_pending = 0;
    const DSLUi *u = dsl_ui_kit(); if (!u) return;
    char nm[400]; key_names(k_key, nm, sizeof nm);
    int missing = strstr(nm, "(not installed)") != NULL;
    char t[700];
    if (missing) { snprintf(t, sizeof t, "This world uses: %s.\nInstall the missing mods to load it.", nm); u->notice(t, "OK", NULL, NULL, NULL, NULL); return; }
    const char *busy = dsl_busy_reason();
    if (busy) { snprintf(t, sizeof t, "This world uses: %s.\nIt cannot be loaded now: %s", nm, busy); u->notice(t, "OK", NULL, NULL, NULL, NULL); return; }
    snprintf(t, sizeof t, "This world uses: %s.\nLoad it with them?", nm);
    u->notice(t, "Load", "Cancel", k_ok, k_cancel, NULL);
}

static char seen_guid[40];
static void world_frame(void) {
    static unsigned f; if (++f % 30) return;
    Obj mf = mainflow(); if (!mf || off_scene < 0 || AT(mf, off_scene, int32_t) != 2) { seen_guid[0] = 0; return; }
    char g[40]; if (!world_guid(g) || !strcmp(g, seen_guid)) return;
    snprintf(seen_guid, sizeof seen_guid, "%s", g);
    const char *k = world_key(g);
    if (!k) { worlds_save(g, run_key); dsl_log("reg: world %s registered with this mod set (%s)", g, run_key); }
    else if (strcmp(k, run_key)) dsl_log("reg: world %s is open with %s, registered with %s (multiplayer guest or a launcher load)", g, run_key, k);
}

/* ------------------------------------------------------------------ test only (DSL_SOFT_TEST=1, never in a player's start) */
/* <log dir>/softrestart/reg_request.json: {"cmd":"k_ok"} = the "Load it with them?" box answered Load; {"cmd":"apply",
   "ids":[..]} = Mods tab Apply; {"cmd":"mods"} = the title "Mods" click; {"cmd":"world_set","guid":..,"ids":[..]} */
/* v13 checks: {"cmd":"title"} logs the title column; {"cmd":"hover","what":"title:<label>" | "row:<i>" | "win:<control>"}
   hovers that element (no mouse) and logs the hover card's rect and its panel's rect 20 frames later */
DSLObj dsl_ui_test_title(const char *find, char *out, size_t cap);
void dsl_ui_test_hover(DSLObj target);
int dsl_ui_test_tip(char *title, size_t cap);
int dsl_ui_test_tip_rects(float card[4], float panel[4]);
void dsl_ui_test_tip_body(char *out, size_t cap);
size_t dsl_ui_test_label(DSLObj o, char *out, size_t cap);
DSLObj dsl_rows_test_line(int i);
/* weak default: builds without loader/ui (host tests) link; ui/dsl_win.c gives the real one */
__attribute__((weak)) DSLObj dsl_win_test_control(const char *name) { (void)name; return NULL; }
static int hv_wait; static char hv_what[120];
static void hover_test(const char *what) {
    DSLObj t = NULL; char b[2000] = "";
    if (!strncmp(what, "title:", 6)) t = dsl_ui_test_title(what + 6, b, sizeof b);
    else if (!strncmp(what, "row:", 4)) { t = dsl_rows_test_line(atoi(what + 4)); if (t) dsl_ui_test_label(t, b, sizeof b); }
    else if (!strncmp(what, "win:", 4)) t = dsl_win_test_control(what + 4);
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
    if (on) hover_frame();
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
    else if (c && !strcmp(c, "shot")) dsl_log("reg test: shot %s: %s", json_str(j, "path"), dsl_soft_shot(json_str(j, "path")) ? "FAILED" : "ok");
    else if (c && !strcmp(c, "toggles")) { void dsl_rows_test_toggles(int); JNode *o = json_get(j, "on"); dsl_rows_test_toggles(o && o->t == JN_BOOL ? o->b : 1); }
    else if (c && !strcmp(c, "world_set")) dsl_world_set_mods(json_str(j, "guid"), ids, n);
    else if (c && !strcmp(c, "change")) { void dsl_rows_test_change(int); dsl_rows_test_change((int)(json_get(j, "row") ? json_get(j, "row")->num : 0)); }
    else if (c && !strcmp(c, "save_file")) {                           /* the game's own manual save (SaveFileRequested) */
        void *k = dsl_find_class("Refactor.Main.Event", "SaveFileRequested"); void *ctor = k ? dsl_find_method(k, ".ctor", 1) : NULL;
        void *kq = dsl_find_class("Refactor.Main", "EventQueue"); void *add = kq ? dsl_find_method(kq, "AddEvent", 1) : NULL;
        void *kmf = dsl_find_class("Refactor.Main", "MainFlow"); int32_t oq = kmf ? dsl_field_offset(kmf, "_eventQueue") : -1;
        Obj (*onew)(void *) = dsl_ga_sym("il2cpp_object_new"); Obj (*snew)(const char *) = dsl_ga_sym("il2cpp_string_new");
        Obj mf = mainflow(), q = mf && oq >= 0 ? AT(mf, oq, Obj) : NULL, ev = q && ctor && add && onew ? onew(k) : NULL;
        if (ev) { ((void (*)(Obj, Obj, const void *))dsl_method_address(ctor))(ev, snew(json_str(j, "name") ? json_str(j, "name") : "Manual"), ctor);
                  ((void (*)(Obj, Obj, const void *))dsl_method_address(add))(q, ev, add); }
        dsl_log("reg test: save_file %s: %s", json_str(j, "name") ? json_str(j, "name") : "Manual", ev ? "queued" : "FAILED");
    }
    else if (c && !strcmp(c, "draft")) dsl_mods_draft(ids, json_get(j, "drop") ? -1 : n);
    else if (c && !strcmp(c, "title")) { char b[2000]; dsl_ui_test_title(NULL, b, sizeof b); dsl_log("reg test: title %s", b); }
    else if (c && !strcmp(c, "hover")) hover_test(json_str(j, "what") ? json_str(j, "what") : "");
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
                const DSLUi *u = dsl_ui_kit(); char t[500]; snprintf(t, sizeof t, "The mods could not be applied: %s", w2);
                if (u) u->notice(t, "OK", NULL, NULL, NULL, NULL);
            }
        } else if (r < 0) {
            bp_n = -1; const DSLUi *u = dsl_ui_kit(); char t[500]; if (u && u->notice_close) u->notice_close(); snprintf(t, sizeof t, "%s could not be installed: %s\nThe mods stay as they were.", dsl_net_name(bp_kind), why);
            if (u) u->notice(t, "OK", NULL, NULL, NULL, NULL);
        }
    }
    if (tip_first_run && dsl_soft_busy() && f % 30 == 0) {
        char t[300]; snprintf(t, sizeof t, "Preparing mods (first start): %s prepares its files for this game version. This takes about 2 minutes, once.", dsl_net_name(tip_first_run));
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
    test_frame();
    dsl_rows_frame();
    if (dsl_win_frame) dsl_win_frame();
}

static const char *data_dir_get(void) { return dsl_reg_data_dir(); }
/* before dsl_plugin_init of the modules (they may add tabs / rows / buttons there) */
void dsl_reg_host(DSLHost *h) {
    h->set_session_busy = session_busy_set; h->session_busy = session_busy_get;
    h->menu_add_tab = tab_add; h->menu_open = menu_open; h->title_add_button = title_add;
    h->loadrow_add = row_add; h->loadlist_filter = row_filter; h->loadlist_refresh = dsl_rows_refresh;
    h->data_dir = data_dir_get;
}
void dsl_reg_ready(DSLHost *h) {
    dsl_ui_install();
    h->ui = dsl_ui_kit();
    dsl_rows_install();
    void *kmf = dsl_find_class("Refactor.Main", "MainFlow");
    if (kmf) { off_scene = dsl_field_offset(kmf, "_currentScene"); off_cd = dsl_field_offset(kmf, "_campaignData"); }
    if (h->ui && dsl_win_init) dsl_win_init();
    if (h->ui) dsl_ui_title_add("loader", "Mods", 100, NULL, on_title_mods, NULL);   /* no hover card (owner 2026-10-16 #1) */
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
      if (!(given && *given) && (worlds_migrated || !old || strcmp(old, ms) || !json_get(worlds_doc, "modset_lines"))) worlds_save(NULL, NULL); }
    int nb = 0, nm = 0; for (int i = 0; i < nmi; i++) { nb += !strcmp(MI[i].kind, "bepinex") && MI[i].active == 1; nm += !strcmp(MI[i].kind, "melon") && MI[i].active == 1; }
    dsl_log("reg: running gameplay set %s (%d installed mod(s), %d BepInEx plugin(s) running, BepInEx state %d, %d MelonLoader mod(s) running, MelonLoader state %d)",
            run_key, nmi, nb, dsl_bie_state(), nm, dsl_ml_state());
}
