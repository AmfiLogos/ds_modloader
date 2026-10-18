// The player's game saves in the modded game (owner 2026-10-16 / 10-17: "make sure that the modlauncher connects to my
// regular saves ... so i can move forward on my saves in the modloader"). Two save folders, one Load list:
//   game    DSML_GAME_SAVES = the normal game's own Saves folder (set by the starters). Its worlds are played IN PLACE:
//           read from there and saved back there, no copy, so the same world goes on in both games.
//   modded  <persistentDataPath>/Saves = DSML_DATA/Saves: worlds that exist only in the modded game (started or hosted
//           there, multiplayer guests). The normal game never lists them; dsl_cloud.c backs them up to Steam Cloud.
// Hooks, found by name (Mac and Windows), all in Refactor.SaveLoadHelper: the one place where the game turns a save
// name into a path. CampaignSaveLoader (TryLoad, Save, Delete, DeleteCampaignSaves, KeepOnlyRetainedCampaignSave) and
// the header cache only use these (decompile of build 25846389):
//   GetAllSavePaths()                 the modded files + the game files (a modded file named like a game file: left out)
//   GetCampaignSavePath(name)         the game folder for a game save (the file is there, or a new save of a game world)
//   SaveFile(CampaignSaveData)        a new save name of a game world (manual save, the next autosave) -> game folder
//   TryQuarantineCorruptSaveFile(p,r) never moves a game file out of the game folder (the normal game owns it)
// Start: a modded save with the NAME of a game save (the old first-start copy of app/stub/ds_modded.c, one file per world)
// is moved to <DSML_DATA>/Mods/save_copies/, never deleted. The same bytes: nothing to say. Different bytes (the copy was
// played): one notice at the title says, per world, which file is newer (file times) and where the copy is kept.
// Delete (owner 2026-10-17): a game save is the normal game's file too. After the game's own delete confirm, a game
// world (the world's delete button: SaveLoadView.CompleteDeleteConfirmation) or a game save (the save's remove:
// SaveLoadView.ConfirmRemove) asks once more in the game's notice box: "also used by Dungeon Settlers without mods.
// Delete it there too?" Delete / Cancel. Cancel = nothing deleted. Modded-only saves: the game's flow, no question.
// CampaignSaveLoader.DeleteCampaignSaves of a game world deletes in the game folder (the stock service only reads the
// folder of GetCampaignSavePath(<any name>): routed once to the game folder for that call).
// Warning (owner: "a warning if some mods modify the game in a profound way that playing without the modloader would
// break the game"): loading a game save while a mod with mod.json "save_dependent": true runs asks first, with the
// game's notice box: which mods, Load / Cancel. Kept per world and mod set (worlds.json "save_warned", dsl_reg.c), so it
// is asked once. Hooked at EventQueue.AddEvent(LoadCampaignRequested) (no loading screen yet); dsl_world.c asks the same
// for loads that do not come through the queue.
// Env: DSML_GAME_SAVES (none = this file is off: one save folder as before), DSML_DATA.
#include "dsl_internal.h"
#include "dsl_menu.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif

static char game_d[1100], mod_d[1100], copies_d[1200];
static int on;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;   /* the game may save on a worker thread */
int dsl_saves_listed, dsl_saves_routed, dsl_saves_kept_out, dsl_saves_warned;   /* tests / log */

/* ------------------------------------------------------------------ names */
static int is_json(const char *n) { size_t l = strlen(n); return l > 5 && !strcasecmp(n + l - 5, ".json"); }
/* "__AutoSave_<32 hex>_": the world's guid is in the name (lower case, with dashes); 0 = none */
static int name_guid(const char *f, char *g) {
    const char *a = strstr(f, "__AutoSave_"); if (!a) return 0;
    const char *h = a + 11;
    for (int i = 0; i < 32; i++) if (!h[i] || !strchr("0123456789abcdefABCDEF", h[i])) return 0;
    snprintf(g, 40, "%.8s-%.4s-%.4s-%.4s-%.12s", h, h + 8, h + 12, h + 16, h + 20);
    for (char *c = g; *c; c++) if (*c >= 'A' && *c <= 'F') *c += 32;
    return 1;
}
static void header_str(const char *h, const char *key, char *out, size_t cap) {
    out[0] = 0; char k[64]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = h ? strstr(h, k) : NULL; if (!p) return;
    p = strchr(p + strlen(k), ':'); if (!p) return; p = strchr(p, '"'); if (!p) return; p++;
    size_t i = 0; while (p[i] && p[i] != '"' && i + 1 < cap) { out[i] = p[i]; i++; } out[i] = 0;
}
/* the save's "CampaignSaveHeader" block: read in 256 KB steps, stopped once the block is in (not the whole file) */
static int file_header(const char *path, char *guid, char *clan) {
    guid[0] = clan[0] = 0;
    FILE *fp = fopen(path, "rb"); if (!fp) return 0;
    enum { STEP = 256 << 10, KEEP = 4096 };
    char *b = malloc(STEP + KEEP + 1); size_t have = 0; const char *h = NULL; int ok = 0;
    while (b) {
        size_t r = fread(b + have, 1, STEP, fp); have += r; b[have] = 0;
        if (!h && (h = strstr(b, "\"CampaignSaveHeader\""))) {              /* the block start; read on until its end is in */
            size_t at = (size_t)(h - b); memmove(b, h, have - at); have -= at;
            if (have > KEEP) have = KEEP;
            b[have] = 0; h = b;
        }
        if (h && (strstr(h, "\"SaveTime\"") || !r)) {
            header_str(h, "CampaignGuid", guid, 40); header_str(h, "ClanName", clan, 64);
            for (char *c = guid; *c; c++) if (*c >= 'A' && *c <= 'F') *c += 32;
            ok = strlen(guid) == 36; break;
        }
        if (!r) break;
        if (!h && have > KEEP) { memmove(b, b + have - KEEP, KEEP); have = KEEP; }   /* the key may span two reads */
    }
    free(b); fclose(fp); return ok;
}

/* ------------------------------------------------------------------ the game folder (kept per scan) */
#define MAXG 512
typedef struct { char name[256], guid[40]; long long size, mtime; } GSave;
static GSave G[MAXG]; static int ng;
static char extra[64][256]; static int nextra;          /* new save names of game worlds (SaveFile), routed to the game */
static char gworld[64][40]; static int ngworld;         /* game worlds seen loaded (manual save names have no guid) */

/* mp_* temporary saves (dsl_save_is_mp_temp): never routed to the game folder, even when its world is a game world (bug
   2026-10-18: a guest's full check with the guid of one of its game saves went to the game folder, Steam Cloud synced,
   and was never deleted). */
#define mp_temp dsl_save_is_mp_temp
static GSave *g_find(const char *name) { for (int i = 0; i < ng; i++) if (!strcasecmp(G[i].name, name)) return &G[i]; return NULL; }
static void scan_game_locked(void) {
    static GSave old[MAXG]; int nold = ng; memcpy(old, G, sizeof(GSave) * (size_t)ng);
    ng = 0;
    DIR *d = opendir(game_d); struct dirent *de; if (!d) return;
    while ((de = readdir(d)) && ng < MAXG) {
        if (de->d_name[0] == '.' || !is_json(de->d_name) || mp_temp(de->d_name)) continue;
        char p[1500]; struct stat st; snprintf(p, sizeof p, "%s" SEP "%s", game_d, de->d_name);
        if (stat(p, &st) || !S_ISREG(st.st_mode)) continue;
        GSave *s = &G[ng]; memset(s, 0, sizeof *s);
        snprintf(s->name, sizeof s->name, "%.*s", (int)(strlen(de->d_name) - 5), de->d_name);
        s->size = (long long)st.st_size; s->mtime = (long long)st.st_mtime;
        if (!name_guid(s->name, s->guid)) {                                  /* a manual save: its header, once per version */
            for (int i = 0; i < nold && !s->guid[0]; i++)
                if (!strcmp(old[i].name, s->name) && old[i].size == s->size && old[i].mtime == s->mtime) snprintf(s->guid, 40, "%s", old[i].guid);
            char clan[64]; if (!s->guid[0] && !file_header(p, s->guid, clan)) s->guid[0] = 0;
        }
        ng++;
    }
    closedir(d);
}
static int is_game_locked(const char *name) {
    if (g_find(name)) return 1;
    for (int i = 0; i < nextra; i++) if (!strcasecmp(extra[i], name)) return 1;
    return 0;
}
static int guid_is_game_locked(const char *g) {
    for (int i = 0; i < ng; i++) if (!strcasecmp(G[i].guid, g)) return 1;
    for (int i = 0; i < ngworld; i++) if (!strcasecmp(gworld[i], g)) return 1;
    return 0;
}
/* 1 = this save name is (or will be written as) a save of the game folder */
int dsl_saves_is_game(const char *name) {
    if (!on || !name || !*name) return 0;
    pthread_mutex_lock(&mu); int r = is_game_locked(name); pthread_mutex_unlock(&mu); return r;
}
/* the world guid of a game save ("" if unknown) */
static int game_guid(const char *name, char g[40]) {
    pthread_mutex_lock(&mu); GSave *s = g_find(name); g[0] = 0; if (s) snprintf(g, 40, "%s", s->guid); pthread_mutex_unlock(&mu);
    if (!g[0] && name_guid(name, g)) return 1;
    return g[0] != 0;
}
/* dsl_world.c: a save is being loaded (its guid from the game's own header read): a game world stays a game world for
   every save the game writes next, whatever its name */
void dsl_saves_loading(const char *name, const char *guid) {
    if (!on || !name || !guid || !*guid) return;
    pthread_mutex_lock(&mu);
    if (is_game_locked(name) && !guid_is_game_locked(guid) && ngworld < 64) snprintf(gworld[ngworld++], 40, "%s", guid);
    pthread_mutex_unlock(&mu);
}

/* ------------------------------------------------------------------ il2cpp access */
static Obj (*str_new)(const char *);
static Obj (*arr_new)(void *klass, size_t n);
static size_t (*arr_len)(Obj a);
static void (*wbarrier)(Obj obj, void **at, Obj val);
static void *(*obj_class)(Obj);
static void *k_string;
static void utf8_of(Obj s, char *out, size_t cap) {
    out[0] = 0; if (!s) return;
    int32_t n = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14); size_t k = 0;
    for (int32_t i = 0; i < n && k + 4 < cap; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < n) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
        if (u < 0x80) out[k++] = (char)u;
        else if (u < 0x800) { out[k++] = (char)(0xC0 | u >> 6); out[k++] = (char)(0x80 | (u & 63)); }
        else if (u < 0x10000) { out[k++] = (char)(0xE0 | u >> 12); out[k++] = (char)(0x80 | (u >> 6 & 63)); out[k++] = (char)(0x80 | (u & 63)); }
        else { out[k++] = (char)(0xF0 | u >> 18); out[k++] = (char)(0x80 | (u >> 12 & 63)); out[k++] = (char)(0x80 | (u >> 6 & 63)); out[k++] = (char)(0x80 | (u & 63)); }
    }
    out[k] = 0;
}
/* file name without folder and ".json" */
static void stem_of(const char *path, char *out, size_t cap) {
    const char *b = path, *p;
    for (p = path; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    size_t l = strlen(b); if (l > 5 && !strcasecmp(b + l - 5, ".json")) l -= 5;
    snprintf(out, cap, "%.*s", (int)l, b);
}
static Obj game_path(const char *name) {
    char p[1500]; snprintf(p, sizeof p, "%s" SEP "%s.json", game_d, name); return str_new(p);
}

/* ------------------------------------------------------------------ hooks */
static Obj (*o_all)(const void *mi);
static Obj my_all(const void *mi) {
    Obj a = o_all(mi);
    if (!on) return a;
    size_t na = a && arr_len ? arr_len(a) : 0;
    pthread_mutex_lock(&mu);
    scan_game_locked();
    int keep[1024]; int nk = 0, out = 0;
    for (size_t i = 0; i < na && nk < 1024; i++) {
        Obj s = AT(a, 0x20 + 8 * i, Obj); char p[1200], st[300]; utf8_of(s, p, sizeof p); stem_of(p, st, sizeof st);
        if (g_find(st)) { out++; continue; }                               /* the game's save of that name wins */
        keep[nk++] = (int)i;
    }
    Obj r = arr_new(k_string, (size_t)(nk + ng));
    if (r) {
        int k = 0;
        for (int i = 0; i < nk; i++, k++) wbarrier(r, (void **)((char *)r + 0x20 + 8 * k), AT(a, 0x20 + 8 * keep[i], Obj));
        for (int i = 0; i < ng; i++, k++) wbarrier(r, (void **)((char *)r + 0x20 + 8 * k), game_path(G[i].name));
    }
    int n_game = ng;
    pthread_mutex_unlock(&mu);
    static int last = -1, last_out = -1;
    if (n_game + nk != last || out != last_out) {
        last = n_game + nk; last_out = out;
        dsl_log("saves: list = %d modded + %d game save(s)%s", nk, n_game, out ? " (modded files named like a game save left out)" : "");
    }
    dsl_saves_listed++; dsl_saves_kept_out = out;
    return r ? r : a;
}
static Obj (*o_path)(Obj name, const void *mi);
static _Thread_local int del_route;                                  /* DeleteCampaignSaves of a game world: its folder lookup */
static Obj my_path(Obj name, const void *mi) {
    if (on && del_route) { del_route = 0; char nm[300]; utf8_of(name, nm, sizeof nm); return game_path(nm[0] ? nm : "x"); }
    if (on && name) {
        char nm[300]; utf8_of(name, nm, sizeof nm);
        if (dsl_saves_is_game(nm)) { dsl_saves_routed++; return game_path(nm); }
    }
    return o_path(name, mi);
}
static int32_t off_guid = -1, off_name = -1;             /* in the unboxed CampaignSaveData */
static uint8_t (*o_savefile)(void *data, const void *mi);
static uint8_t my_savefile(void *data, const void *mi) {
    if (on && data && off_guid >= 0 && off_name >= 0) {
        const uint8_t *b = (const uint8_t *)data + off_guid; char g[40], nm[300];
        snprintf(g, sizeof g, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 *(const uint32_t *)b, *(const uint16_t *)(b + 4), *(const uint16_t *)(b + 6), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
        utf8_of(AT(data, off_name, Obj), nm, sizeof nm);
        pthread_mutex_lock(&mu);
        int game = guid_is_game_locked(g) && !mp_temp(nm), known = is_game_locked(nm);
        if (game && !known && nm[0] && nextra < 64) snprintf(extra[nextra++], 256, "%s", nm);
        pthread_mutex_unlock(&mu);
        dsl_log("saves: saving %s (world %s) to the %s folder", nm, g, game ? "game's" : "modded");
    }
    return o_savefile(data, mi);
}
static uint8_t (*o_quar)(Obj path, Obj why, const void *mi);
static uint8_t my_quar(Obj path, Obj why, const void *mi) {
    if (on && path) {
        char p[1200], d[1200]; utf8_of(path, p, sizeof p); snprintf(d, sizeof d, "%s", p);
        char *e = d + strlen(d); while (e > d && e[-1] != '/' && e[-1] != '\\') e--; if (e > d) e[-1] = 0;
        for (char *c = d; *c; c++) if (*c == '\\') *c = '/';
        char g[1100]; snprintf(g, sizeof g, "%s", game_d); for (char *c = g; *c; c++) if (*c == '\\') *c = '/';
        size_t gl = strlen(g); while (gl && g[gl - 1] == '/') g[--gl] = 0;
        if (!strcasecmp(d, g)) {
            char w[400]; utf8_of(why, w, sizeof w);
            dsl_log("saves: the game could not read %s (%s): it stays in the game's folder (not moved)", p, w);
            return 0;
        }
    }
    return o_quar(path, why, mi);
}

/* ------------------------------------------------------------------ start: old first-start copies */
static char differ[8][64], differ_path[8][1600]; static int differ_newer[8]; static int ndiffer; static int differ_told;
static int same_bytes(const char *a, const char *b) {
    FILE *x = fopen(a, "rb"), *y = fopen(b, "rb"); int same = x && y;
    static char p[65536], q[65536];                      /* start only (main thread) */
    while (same) {
        size_t n = fread(p, 1, sizeof p, x), m = fread(q, 1, sizeof q, y);
        if (n != m || memcmp(p, q, n)) same = 0;
        if (!n) break;
    }
    if (x) fclose(x); if (y) fclose(y);
    return same;
}
static void move_old_copies(void) {
    DIR *d = opendir(mod_d); struct dirent *de; if (!d) return;
    static char names[MAXG][256]; int n = 0;             /* start only */
    while ((de = readdir(d)) && n < MAXG) if (de->d_name[0] != '.' && is_json(de->d_name)) snprintf(names[n++], 256, "%s", de->d_name);
    closedir(d);
    for (int i = 0; i < n; i++) {
        char gp[1500], mp[1500]; struct stat st;
        snprintf(gp, sizeof gp, "%s" SEP "%s", game_d, names[i]); snprintf(mp, sizeof mp, "%s" SEP "%s", mod_d, names[i]);
        if (stat(gp, &st) || !S_ISREG(st.st_mode)) continue;
        struct stat ms; int newer = !stat(mp, &ms) && ms.st_mtime > st.st_mtime;   /* the copy last saved after the game save */
        int same = same_bytes(gp, mp);
        mkdir(copies_d, 0755);
        char ts[32], cp[1600]; time_t t = time(NULL); strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", localtime(&t));
        snprintf(cp, sizeof cp, "%s" SEP "%.*s-modded-copy-%s.json", copies_d, (int)(strlen(names[i]) - 5), names[i], ts);
        if (rename(mp, cp)) { dsl_log("saves: %s is in both folders; moving the modded copy aside FAILED: the game's save is listed", names[i]); continue; }
        dsl_log("saves: %s is in both folders (%s%s): the game's save is listed, the modded copy is kept as %s", names[i], same ? "same bytes" : "DIFFERENT",
                same ? "" : newer ? ", the modded copy is NEWER" : ", the game save is newer", cp);
        if (!same && ndiffer < 8) {
            char g[40], clan[64]; if (!file_header(cp, g, clan) || !clan[0]) snprintf(clan, sizeof clan, "%.40s", names[i]);
            snprintf(differ[ndiffer], 64, "%s", clan); snprintf(differ_path[ndiffer], sizeof differ_path[0], "%s", cp); differ_newer[ndiffer++] = newer;
        }
    }
}

/* ------------------------------------------------------------------ warning before a game save loads with deep mods */
int dsl_running_save_dependent(const char **ids, int max, char *key, size_t kcap);   /* dsl_reg.c */
const char *dsl_world_warned(const char *guid);                                      /* dsl_reg.c, worlds.json */
int dsl_world_set_differs(const char *guid);
void dsl_world_set_warned(const char *guid, const char *key);
const char *dsl_mod_name(const char *id);
static void *k_loadreq; static int32_t off_campaign = -1;
static void (*o_add)(Obj self, Obj ev, const void *mi);
static void (*ctor_load)(Obj, Obj, const void *);
static Obj (*obj_new)(void *klass);
static uintptr_t (*gc_new)(Obj, int); static Obj (*gc_get)(uintptr_t); static void (*gc_free)(uintptr_t);
static uintptr_t h_queue;
static char w_name[300], w_guid[40], w_key[512]; static int w_pending, w_frames, w_open, w_pass;
/* 1 = this load needs the question first (a game save, a save_dependent mod runs, not asked for this world and set) */
static int needs_warning(const char *name, char *guid, char *key, size_t kcap) {
    if (!on || !dsl_saves_is_game(name)) return 0;
    const char *ids[64]; int n = dsl_running_save_dependent(ids, 64, key, kcap);
    if (!n) return 0;
    if (!game_guid(name, guid)) snprintf(guid, 40, "save:%s", name);
    if (dsl_world_set_differs(guid)) return 0;      /* the world check refuses it first and loads it with its own set */
    const char *seen = dsl_world_warned(guid);
    if (seen) {                                                               /* every running id already answered for */
        int all = 1;
        for (int i = 0; i < n && all; i++) {
            char s[600]; snprintf(s, sizeof s, "+%s+", seen); char t[200]; snprintf(t, sizeof t, "+%s+", ids[i]);
            all = strstr(s, t) != NULL;
        }
        if (all) return 0;
    }
    return 1;
}
static void ask(void);
/* 1 = held: the question shows (now or in a few frames); the load runs again after Load */
static int hold(const char *name, const char *guid, const char *key, int frames) {
    if (w_open) { dsl_log("saves: load of %s held: the question for %s is still open", name, w_name); return 1; }
    snprintf(w_name, sizeof w_name, "%s", name); snprintf(w_guid, sizeof w_guid, "%s", guid); snprintf(w_key, sizeof w_key, "%s", key);
    w_pending = 1; w_frames = frames; w_open = 1;
    if (!frames) ask();
    return 1;
}
static void requeue(void) {
    Obj mf = dsl_soft_mainflow(), q = h_queue ? gc_get(h_queue) : NULL;
    if (!q && mf) { void *k = dsl_find_class("Refactor.Main", "MainFlow"); int32_t o = k ? dsl_field_offset(k, "_eventQueue") : -1; if (o >= 0) q = AT(mf, o, Obj); }
    if (!q || !obj_new || !ctor_load || !k_loadreq) { dsl_log("saves: cannot load %s again (no event queue)", w_name); return; }
    Obj e = obj_new(k_loadreq); ctor_load(e, str_new(w_name), NULL);
    w_pass = 1; o_add(q, e, NULL); w_pass = 0;
}
static void on_load(void *ud, DSLObj w) {
    (void)ud; (void)w; w_open = 0;
    dsl_world_set_warned(w_guid, w_key); dsl_saves_warned++;
    dsl_log("saves: warning answered Load for %s (world %s, mods %s)", w_name, w_guid, w_key);
    requeue();
}
static void on_cancel(void *ud, DSLObj w) { (void)ud; (void)w; w_open = 0; dsl_log("saves: warning answered Cancel for %s: not loaded", w_name); }
static void ask(void) {
    w_pending = 0;
    const DSLUi *u = dsl_ui_kit(); if (!u) { w_open = 0; dsl_log("saves: no kit: %s not loaded", w_name); return; }
    char list[600] = "", t[600], b[1400];
    char k[512]; snprintf(k, sizeof k, "%s", w_key); int i = 0;
    for (char *sv = NULL, *id = strtok_r(k, "+", &sv); id; id = strtok_r(NULL, "+", &sv), i++) {
        const char *nm = dsl_mod_name(id); if (!nm) nm = id;
        if (!i) snprintf(list, sizeof list, "%s", nm);
        else { snprintf(t, sizeof t, TR("%s, %s"), list, nm); snprintf(list, sizeof list, "%s", t); }
    }
    snprintf(b, sizeof b, TRN("This world is one of your game saves. This mod changes what the world saves: %s.\n\n"
                              "If you play it now, the world may break when you load it later in the game without the mod loader.",
                              "This world is one of your game saves. These mods change what the world saves: %s.\n\n"
                              "If you play it now, the world may break when you load it later in the game without the mod loader.", i), list);
    dsi_utf8_fix_end(b);
    dsl_log("saves: warning shown for %s (world %s, mods %s)", w_name, w_guid, w_key);
    u->notice(b, TR("Load"), TR("Cancel"), on_load, on_cancel, NULL);
}
static void my_add(Obj self, Obj ev, const void *mi) {
    if (on && !w_pass && ev && k_loadreq && obj_class(ev) == k_loadreq && off_campaign >= 0) {
        char nm[300], g[40], key[512]; utf8_of(AT(ev, off_campaign, Obj), nm, sizeof nm);
        if (needs_warning(nm, g, key, sizeof key)) {
            if (gc_new) { if (h_queue) gc_free(h_queue); h_queue = gc_new(self, 0); }
            hold(nm, g, key, 0);
            return;                                                            /* the stock load never starts: no loading screen */
        }
    }
    o_add(self, ev, mi);
}
/* dsl_world.c, LoadCampaign (loads that did not come through the queue): 1 = refuse it, the question follows */
int dsl_saves_load_gate(const char *name) {
    char g[40], key[512];
    if (w_pass || !needs_warning(name, g, key, sizeof key)) return 0;
    return hold(name, g, key, 3);                                            /* after the stock box that closes the load */
}
/* ------------------------------------------------------------------ delete: a game save is the normal game's file too */
typedef struct { uint64_t lo, hi; } DGuid;                /* System.Guid by value (same ABI as the game's method) */
static void guid_str(DGuid v, char g[40]) {
    const uint8_t *b = (const uint8_t *)&v;
    snprintf(g, 40, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             *(const uint32_t *)b, *(const uint16_t *)(b + 4), *(const uint16_t *)(b + 6), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}
static void (*o_complete)(Obj view, DGuid g, int32_t version, uint8_t confirmed, const void *mi);
static void (*o_remove)(Obj view, Obj name, const void *mi);
static Obj (*o_delcamp)(Obj loader, DGuid g, const void *mi);
static void *a_complete, *a_remove;                       /* the game's methods (hooked): the test calls them */
static struct { int open, kind; uintptr_t h_view; DGuid g; int32_t version; char name[300], what[300]; } D;   /* kind 1 world, 2 save */
static void del_yes(void *ud, DSLObj w) {
    (void)ud; (void)w; if (!D.open) return; D.open = 0;
    Obj v = D.h_view ? gc_get(D.h_view) : NULL;
    dsl_log("saves: delete answered Delete for %s: deleted in the game's folder too", D.what);
    if (v && D.kind == 1) o_complete(v, D.g, D.version, 1, NULL);
    else if (v && D.kind == 2) o_remove(v, str_new(D.name), NULL);
    if (D.h_view) { gc_free(D.h_view); D.h_view = 0; }
}
static void del_no(void *ud, DSLObj w) {
    (void)ud; (void)w; if (!D.open) return; D.open = 0;
    Obj v = D.h_view ? gc_get(D.h_view) : NULL;
    dsl_log("saves: delete answered Cancel for %s: nothing deleted", D.what);
    if (v && D.kind == 1) o_complete(v, D.g, D.version, 0, NULL);       /* the view's own "not confirmed" (buttons back) */
    if (D.h_view) { gc_free(D.h_view); D.h_view = 0; }
}
static int del_ask(Obj view, int kind, const char *what) {
    const DSLUi *u = dsl_ui_kit(); if (!u || !gc_new) { dsl_log("saves: no kit: delete of %s not asked, NOT deleted", what); return 0; }
    D.open = 1; D.kind = kind; D.h_view = gc_new(view, 0); snprintf(D.what, sizeof D.what, "%s", what);
    dsl_log("saves: delete question shown for %s", what);
    u->notice(TR("This save is also used by Dungeon Settlers without mods. Delete it there too?"), TR("Delete"), TR("Cancel"), del_yes, del_no, NULL);
    return 1;
}
/* the world's delete button, after the game's own confirm (confirmed = its Yes) */
static void my_complete(Obj view, DGuid g, int32_t version, uint8_t confirmed, const void *mi) {
    if (on && confirmed && view && !D.open) {
        char gs[40]; guid_str(g, gs);
        pthread_mutex_lock(&mu); int game = guid_is_game_locked(gs); pthread_mutex_unlock(&mu);
        if (game) { D.g = g; D.version = version; char w[80]; snprintf(w, sizeof w, "world %s", gs); if (del_ask(view, 1, w)) return; }
    } else if (on && confirmed && D.open) { dsl_log("saves: delete held: a delete question is still open"); o_complete(view, g, version, 0, mi); return; }
    o_complete(view, g, version, confirmed, mi);
}
/* the save's remove, after the game's own confirm */
static void my_remove(Obj view, Obj name, const void *mi) {
    if (on && view && name) {
        char nm[300]; utf8_of(name, nm, sizeof nm);
        pthread_mutex_lock(&mu); int game = g_find(nm) != NULL; pthread_mutex_unlock(&mu);
        if (game && D.open) { dsl_log("saves: remove of %s held: a delete question is still open", nm); return; }
        if (game) { snprintf(D.name, sizeof D.name, "%s", nm); char w[340]; snprintf(w, sizeof w, "save %s", nm); if (del_ask(view, 2, w)) return; }
    }
    o_remove(view, name, mi);
}
/* the world's saves deleted: a game world's are in the game folder (the stock service lists one folder) */
static Obj my_delcamp(Obj loader, DGuid g, const void *mi) {
    char gs[40]; guid_str(g, gs);
    pthread_mutex_lock(&mu); int game = on && guid_is_game_locked(gs); pthread_mutex_unlock(&mu);
    if (game) { del_route = 1; dsl_log("saves: deleting world %s in the game's folder", gs); }
    Obj r = o_delcamp(loader, g, mi);
    del_route = 0;
    return r;
}

/* test requests (dsl_reg.c): the question answered */
void dsl_saves_test_answer(int load) {                    /* no question open: closes the kit's notice (the set-aside one) */
    const DSLUi *u = dsl_ui_kit();
    if (D.open) { if (load) del_yes(NULL, NULL); else del_no(NULL, NULL); }
    else if (w_open) { if (load) on_load(NULL, NULL); else on_cancel(NULL, NULL); } else dsl_log("saves test: no question open: notice closed");
    if (u && u->notice_close) u->notice_close();
}
/* test only (DSL_SOFT_TEST=1): <log dir>/softrestart/saves_request.json
   {"cmd":"del_world","guid":..} = the world's delete answered Yes in the game's own box (the open Load list's view);
   {"cmd":"del_save","name":..}  = the save's remove answered Yes */
static int guid_parse(const char *s, DGuid *out) {
    uint8_t b[16]; int k = 0;
    for (const char *p = s; *p && k < 32; p++) {
        if (*p == '-') continue;
        int v = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 : *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : -1;
        if (v < 0) return 0;
        if (k % 2) b[k / 2] |= (uint8_t)v; else b[k / 2] = (uint8_t)(v << 4);
        k++;
    }
    if (k != 32) return 0;
    uint8_t g[16] = { b[3], b[2], b[1], b[0], b[5], b[4], b[7], b[6], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15] };   /* Guid: int, short, short, 8 bytes */
    memcpy(out, g, 16); return 1;
}
static void test_frame(void) {
    static int t_on = -1; static unsigned f;
    if (t_on < 0) t_on = getenv("DSL_SOFT_TEST") && *getenv("DSL_SOFT_TEST") == '1';
    if (!t_on || ++f % 5) return;
    char p[1300]; snprintf(p, sizeof p, "%s/softrestart/saves_request.json", dsl_log_dir());
    FILE *fp = fopen(p, "rb"); if (!fp) return;
    char t[2048]; size_t n = fread(t, 1, sizeof t - 1, fp); t[n] = 0; fclose(fp); remove(p);
    JNode *j = json_parse(t); const char *c = j ? json_str(j, "cmd") : NULL;
    static Obj (*find)(Obj, uint8_t, const void *); static Obj t_view; static int32_t o_pend = -1, o_ver = -1;
    if (!t_view) {
        void *kv = dsl_find_class("Refactor", "SaveLoadView"), *ko = dsl_find_class("UnityEngine", "Object");
        void *mf = ko ? dsl_find_method(ko, "FindObjectOfType", 2) : NULL; find = mf ? dsl_method_address(mf) : NULL;
        void *(*ct)(void *) = dsl_ga_sym("il2cpp_class_get_type"); Obj (*to)(void *) = dsl_ga_sym("il2cpp_type_get_object");
        if (ct && to && kv) t_view = to(ct(kv));
        o_pend = kv ? dsl_field_offset(kv, "_pendingDeleteCampaign") : -1; o_ver = kv ? dsl_field_offset(kv, "_deleteConfirmationVersion") : -1;
    }
    Obj v = find && t_view ? find(t_view, 0, NULL) : NULL;
    dsl_log("saves test: request %s (view %s)", c ? c : "?", v ? "open" : "MISSING");
    if (v && c && !strcmp(c, "del_world") && a_complete && o_pend >= 0 && o_ver >= 0) {
        DGuid g; if (!json_str(j, "guid") || !guid_parse(json_str(j, "guid"), &g)) { dsl_log("saves test: bad guid"); json_free(j); return; }
        int32_t ver = AT(v, o_ver, int32_t) + 1; AT(v, o_ver, int32_t) = ver; memcpy((char *)v + o_pend, &g, 16);   /* as CheckDeleteCampaign leaves them */
        ((void (*)(Obj, DGuid, int32_t, uint8_t, const void *))a_complete)(v, g, ver, 1, NULL);
    } else if (v && c && !strcmp(c, "del_save") && a_remove && json_str(j, "name"))
        ((void (*)(Obj, Obj, const void *))a_remove)(v, str_new(json_str(j, "name")), NULL);
    if (j) json_free(j);
}

/* the title tick (dsl_reg.c dsl_frame_hook) */
int dsl_reg_scene(void);
void dsl_saves_frame(void) {
    if (!on) return;
    test_frame();
    if (w_pending && --w_frames <= 0) ask();
    if (ndiffer && !differ_told && dsl_reg_scene() == 1 && dsl_soft_title_idle()) {
        const DSLUi *u = dsl_ui_kit(); if (!u) return;
        differ_told = 1;
        /* per world: which file is newer; where the copy is (one copy: its file; more: their folder). The kit's box
           holds 1 KB: lines that do not fit are left out ("and N more"), the folder always shows. */
        static char b[1024]; char t[1800];
        snprintf(b, sizeof b, TRN("The modded game now plays your game saves directly. %d old modded copy of a world was different "
                                  "from your game save. It was set aside, not deleted.",
                                  "The modded game now plays your game saves directly. %d old modded copies of worlds were different "
                                  "from your game saves. They were set aside, not deleted.", ndiffer), ndiffer);
        char where[1700];
        if (ndiffer == 1) snprintf(where, sizeof where, TR("The old modded copy is this file:\n%s"), differ_path[0]);
        else snprintf(where, sizeof where, TR("The old modded copies are in this folder:\n%s"), copies_d);
        int shown = 0;
        for (int i = 0; i < ndiffer; i++) {
            snprintf(t, sizeof t, differ_newer[i] ? TR("%s: the old modded copy is NEWER than your game save.")
                                                  : TR("%s: your game save is newer than the old modded copy."), differ[i]);
            if (strlen(b) + 2 + strlen(t) + 2 + strlen(where) + 40 >= sizeof b) break;
            strlcat(b, "\n\n", sizeof b); strlcat(b, t, sizeof b); shown++;
        }
        if (shown < ndiffer) { snprintf(t, sizeof t, TR("(and %d more)"), ndiffer - shown); strlcat(b, "\n", sizeof b); strlcat(b, t, sizeof b); }
        strlcat(b, "\n\n", sizeof b); strlcat(b, where, sizeof b);
        dsi_utf8_fix_end(b);
        u->notice(b, TR("OK"), NULL, NULL, NULL, NULL);
        int nn = 0; for (int i = 0; i < ndiffer; i++) nn += differ_newer[i];
        dsl_log("saves: told the player about %d set-aside modded copy(ies), %d newer than the game save", ndiffer, nn);
    }
}

/* ------------------------------------------------------------------ install (after il2cpp_init) */
static void *meth(const char *ns, const char *k, const char *m, int argc) {
    void *c = dsl_find_class(ns, k), *mi = c ? dsl_find_method(c, m, argc) : NULL;
    if (!mi) dsl_log("saves: %s.%s.%s missing", ns, k, m);
    return mi ? dsl_method_address(mi) : NULL;
}
static int hk(void *a, void *repl, void **orig, const char *who) {
    if (!a) return -1;
    int r = dsl_hook(DSL_OWNER_LOADER, a, repl, orig, who);
    if (r) dsl_log("saves: hook %s refused", who);
    return r;
}
void dsl_saves_install(void) {
    const char *g = getenv("DSML_GAME_SAVES"), *d = getenv("DSML_DATA");
    if (!g || !*g || !d || !*d) { dsl_log("saves: one save folder (no DSML_GAME_SAVES / DSML_DATA)"); return; }
    snprintf(game_d, sizeof game_d, "%s", g);
    size_t gl = strlen(game_d); while (gl > 1 && (game_d[gl - 1] == '/' || game_d[gl - 1] == '\\')) game_d[--gl] = 0;
    snprintf(mod_d, sizeof mod_d, "%s" SEP "Saves", d);
    snprintf(copies_d, sizeof copies_d, "%s" SEP "Mods" SEP "save_copies", d);
    struct stat st;
    if (stat(game_d, &st) || !S_ISDIR(st.st_mode)) { dsl_log("saves: the game's save folder %s is not there: modded saves only", game_d); return; }
    str_new = dsl_ga_sym("il2cpp_string_new"); arr_new = dsl_ga_sym("il2cpp_array_new"); arr_len = dsl_ga_sym("il2cpp_array_length");
    wbarrier = dsl_ga_sym("il2cpp_gc_wbarrier_set_field"); obj_class = dsl_ga_sym("il2cpp_object_get_class");
    obj_new = dsl_ga_sym("il2cpp_object_new");
    gc_new = dsl_ga_sym("il2cpp_gchandle_new"); gc_get = dsl_ga_sym("il2cpp_gchandle_get_target"); gc_free = dsl_ga_sym("il2cpp_gchandle_free");
    k_string = dsl_find_class("System", "String");
    void *k_data = dsl_find_class("Refactor", "CampaignSaveData"), *k_hdr = dsl_find_class("Refactor", "CampaignSaveHeader");
    int32_t oh = k_data ? dsl_field_offset(k_data, "CampaignSaveHeader") : -1;
    int32_t og = k_hdr ? dsl_field_offset(k_hdr, "CampaignGuid") : -1, on_ = k_hdr ? dsl_field_offset(k_hdr, "SaveName") : -1;
    if (oh >= 0x10 && og >= 0x10 && on_ >= 0x10) { off_guid = oh - 0x10 + og - 0x10; off_name = oh - 0x10 + on_ - 0x10; }   /* value types: offsets count the object header */
    void *a_all = meth("Refactor", "SaveLoadHelper", "GetAllSavePaths", 0), *a_path = meth("Refactor", "SaveLoadHelper", "GetCampaignSavePath", 1);
    void *a_save = meth("Refactor", "SaveLoadHelper", "SaveFile", 1), *a_quar = meth("Refactor", "SaveLoadHelper", "TryQuarantineCorruptSaveFile", 2);
    if (!str_new || !arr_new || !arr_len || !wbarrier || !obj_class || !k_string || !a_all || !a_path || !a_save || off_guid < 0) {
        dsl_log("saves: names missing (offsets guid %d name %d): the game's saves are NOT listed", off_guid, off_name); return;
    }
    /* all four or none: half of them would write a game world into the modded folder */
    if (hk(a_all, (void *)my_all, (void **)&o_all, "loader saves GetAllSavePaths")) return;
    if (hk(a_path, (void *)my_path, (void **)&o_path, "loader saves GetCampaignSavePath")) { dsl_unhook(DSL_OWNER_LOADER, a_all, "saves"); return; }
    if (hk(a_save, (void *)my_savefile, (void **)&o_savefile, "loader saves SaveFile")) { dsl_unhook(DSL_OWNER_LOADER, a_all, "saves"); dsl_unhook(DSL_OWNER_LOADER, a_path, "saves"); return; }
    if (a_quar) hk(a_quar, (void *)my_quar, (void **)&o_quar, "loader saves TryQuarantineCorruptSaveFile");
    move_old_copies();
    pthread_mutex_lock(&mu); scan_game_locked(); int n = ng; pthread_mutex_unlock(&mu);
    on = 1;
    /* the warning: the load request, before the stock loading screen */
    k_loadreq = dsl_find_class("Refactor.Main.Event", "LoadCampaignRequested");
    off_campaign = k_loadreq ? dsl_field_offset(k_loadreq, "CampaignName") : -1;
    void *mc = k_loadreq ? dsl_find_method(k_loadreq, ".ctor", 1) : NULL; ctor_load = mc ? dsl_method_address(mc) : NULL;
    void *a_add = meth("Refactor.Main", "EventQueue", "AddEvent", 1);
    if (!a_add || off_campaign < 0 || hk(a_add, (void *)my_add, (void **)&o_add, "loader saves EventQueue.AddEvent")) {
        o_add = a_add; dsl_log("saves: load-request hook off: the warning comes after the loading screen opens (world check)");
    }
    /* delete: the question after the game's own confirm; a game world's files deleted in the game folder only with it */
    a_complete = meth("Refactor", "SaveLoadView", "CompleteDeleteConfirmation", 3); a_remove = meth("Refactor", "SaveLoadView", "ConfirmRemove", 1);
    void *a_delcamp = meth("Refactor", "CampaignSaveLoader", "DeleteCampaignSaves", 1);
    if (hk(a_complete, (void *)my_complete, (void **)&o_complete, "loader saves SaveLoadView.CompleteDeleteConfirmation")) {
        a_complete = NULL; dsl_log("saves: world delete question off: a game world's delete removes nothing in the game's folder");
    } else if (hk(a_delcamp, (void *)my_delcamp, (void **)&o_delcamp, "loader saves CampaignSaveLoader.DeleteCampaignSaves"))
        dsl_log("saves: world delete routing off: a game world's delete removes nothing in the game's folder");
    if (hk(a_remove, (void *)my_remove, (void **)&o_remove, "loader saves SaveLoadView.ConfirmRemove")) {
        a_remove = NULL; dsl_log("saves: save remove question off");
    }
    dsl_log("saves: on: %d game save(s) in %s are listed and played in place; modded-only saves in %s", n, game_d, mod_d);
}
