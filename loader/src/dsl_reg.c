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
#include "dsl_menu.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

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
        const char *sc = json_str(m, "scope"); x->scope = xstrdup(sc && !strcmp(sc, "client") ? "client" : "gameplay");
        JNode *h = json_get(m, "hidden"); x->hidden = h && h->t == JN_BOOL && h->b;
        const char *kd = json_str(m, "kind"); if (!kd) kd = json_str(m, "type");
        x->kind = xstrdup(kd ? kd : "code");
        int on = 1;
        if (en && en->t == JN_ARR) { on = 0; for (JNode *c = en->child; c; c = c->next) if (c->t == JN_STR && !strcmp(c->str, id)) on = 1; }
        x->enabled = on;
        const char *why = NULL; int st = dsl_mod_state(id, &why);
        const char *kind = json_str(m, "kind");
        if (kind && !strcmp(kind, "bepinex")) {             /* BepInEx plugin: the Windows start part runs it (start/win/bie_win.c) */
#ifdef _WIN32
            const char *dd = getenv("DOORSTOP_DISABLE");
            int runs = on && !(dd && *dd);
            st = runs ? 1 : 0; why = runs ? "" : on ? "BepInEx is off this start (see starter.log)" : "off";
#else
            st = 0; why = "BepInEx plugins run on Windows only";
#endif
        }
        x->active = st == 1 ? 1 : (on ? -1 : 0);
        x->reason = xstrdup(st == 1 ? "" : !on ? "off" : (why ? why : "not loaded (see dsload.log)"));
        json_free(m);
    }
    json_free(prof);
}
int dsl_mods_count(void) { mods_scan(); return nmi; }
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
static void worlds_load(void) {
    if (worlds_doc) return;
    char *t = slurp(worlds_path(), 8 << 20); worlds_doc = t ? json_parse(t) : NULL; free(t);
}
static const char *world_key(const char *guid) {
    worlds_load();
    JNode *w = json_get(worlds_doc, "worlds");
    if (w && w->t == JN_OBJ) for (JNode *c = w->child; c; c = c->next) if (c->key && !strcasecmp(c->key, guid) && c->t == JN_STR) return c->str;
    return NULL;
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
    /* "modset_lines": <id>@<version> of each running gameplay mod in the key, for the names in a join refusal */
    PUT("%s\"modset_lines\":[", first ? "" : ","); first = 0;
    { int nl = 0; for (int i = 0; i < nmi; i++) if (MI[i].active == 1 && !strcmp(MI[i].scope, "gameplay") && strcmp(MI[i].id, "multiplayer")) {
        jesc(e1, sizeof e1, MI[i].id); jesc(e2, sizeof e2, MI[i].version ? MI[i].version : "");
        PUT("%s\"%s@%s\"", nl++ ? "," : "", e1, e2); } }
    PUT("]");
    jesc(e2, sizeof e2, strcmp(run_key, "vanilla") ? run_key : ""); PUT(",\"modset\":\"%s\",\"worlds\":{", e2);
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
static int apply_ids(const char *const *ids, int n, const char *env, char *why, size_t cap) {
    const char *busy = dsl_busy_reason();
    if (busy) { snprintf(why, cap, "%s", busy); dsl_log("reg: Apply refused: %s", busy); return -1; }
    /* keep client mods the player cannot see (hidden) as they are */
    const char *all[MAXM]; int k = 0;
    for (int i = 0; i < n && k < MAXM; i++) all[k++] = ids[i];
    for (int i = 0; i < nmi && k < MAXM; i++) if (MI[i].hidden && MI[i].enabled) { int dup = 0; for (int j = 0; j < k; j++) dup |= !strcmp(all[j], MI[i].id); if (!dup) all[k++] = MI[i].id; }
    char p[1200]; dpath(p, sizeof p, "pending.json");
    if (write_profile(p, all, k)) { snprintf(why, cap, "cannot write %s", p); return -1; }
    char kk[512]; key_of(all, k, kk, sizeof kk);
    char envl[1400]; snprintf(envl, sizeof envl, "DSML_MODSET=%s\n%s", kk, env ? env : "");
    dsl_log("reg: Apply: %d mod(s) (%s) -> soft restart", k, kk);
    char g[40]; Obj mf = mainflow();
    if (!env && mf && off_scene >= 0 && AT(mf, off_scene, int32_t) == 2 && world_guid(g)) {   /* in a world: it keeps the new set */
        const char *old = world_key(g); char was[512]; snprintf(was, sizeof was, "%s", old ? old : "");
        worlds_save(g, kk); dsl_log("reg: world %s now uses %s (was %s)", g, kk, was[0] ? was : "unregistered");
    }
    if (dsl_soft_apply(p, envl)) { snprintf(why, cap, "the game cannot restart right now (loading, or a restart already runs)"); return -1; }
    return 0;
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
    if (strcmp(b, "vanilla")) for (char *sv = NULL, *t = strtok_r(b, "+", &sv); t && n < MAXM; t = strtok_r(NULL, "+", &sv)) ids[n++] = t;
    for (int i = 0; i < nmi && n < MAXM; i++) if (MI[i].enabled && !strcmp(MI[i].scope, "client")) ids[n++] = MI[i].id;   /* keep client mods */
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
static void test_frame(void) {
    static int on = -1; static unsigned f;
    if (on < 0) on = getenv("DSL_SOFT_TEST") && *getenv("DSL_SOFT_TEST") == '1';
    if (!on || ++f % 5) return;
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
    json_free(j);
}

/* ------------------------------------------------------------------ frame + install */
void dsl_frame_hook(void) {
    commit_pending();
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
    if (h->ui) dsl_ui_title_add("loader", "Mods", 100, "Turn mods on or off, save mod groups, and open the mod tabs.", on_title_mods, NULL);
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
    if (!old || strcmp(old, strcmp(run_key, "vanilla") ? run_key : "") || !json_get(worlds_doc, "modset_lines")) worlds_save(NULL, NULL);
    dsl_log("reg: running gameplay set %s (%d installed mod(s))", run_key, nmi);
}
