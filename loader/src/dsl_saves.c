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
// world (the world's delete button: SaveLoadView.CompleteDeleteConfirmation) or a game save (the save's remove: its
// RemoveFileRequested event at EventQueue.AddEvent; ConfirmRemove is inlined in the game's build) asks once more in the game's notice box: "also used by Dungeon Settlers without mods.
// Delete it there too?" Delete / Cancel. Cancel = nothing deleted. Modded-only saves: the game's flow, no question.
// CampaignSaveLoader.DeleteCampaignSaves of a game world deletes in the game folder (the stock service only reads the
// folder of GetCampaignSavePath(<any name>): routed once to the game folder for that call).
// Warning (owner: "a warning if some mods modify the game in a profound way that playing without the modloader would
// break the game"): loading a game save while a mod with mod.json "save_dependent": true runs asks first, with the
// game's notice box: which mods, Load / Cancel. Kept per world and mod set (worlds.json "save_warned", dsl_reg.c), so it
// is asked once. Hooked at EventQueue.AddEvent(LoadCampaignRequested) (no loading screen yet); dsl_world.c asks the same
// for loads that do not come through the queue.
// Backup (owner, ZET-U 2026-10-18: "make a backup of the original save before the mod changes it ... with the date; tell
// the player where"): opt-in per mod, mod.json "save_backup": true next to "save_dependent" (Hell difficulty: no backup).
// The warning then names the folder, and its Load first makes byte copies of the world's game-save files (the loaded
// file + every other game save of the same world guid) in <DSML_DATA>/Mods/save_backups/<YYYY-MM-DD_HHMM>_<world>/,
// next to save_copies/ and never inside a Saves folder (the game lists Saves recursively). Copies, never a move. A copy
// that fails = the world is NOT loaded, a notice says why. Asked once per world and mod set, so copied once too.
// Env: DSML_GAME_SAVES (none = this file is off: one save folder as before), DSML_DATA.
#include "dsl_internal.h"
#include "dsl_menu.h"
#include "dsl_os.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#endif

typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif

static char game_d[1100], mod_d[1100], copies_d[1200], backups_d[1200];
static int on;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;   /* the game may save on a worker thread */
int dsl_saves_listed, dsl_saves_routed, dsl_saves_kept_out, dsl_saves_warned, dsl_saves_backed_up;   /* tests / log */

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

/* ------------------------------------------------------------------ worlds that are not on the Load list
   (owner 2026-10-18: the player must see that they are hidden, not lost). Counted on disk, no game code:
   - mods off this session (unknown game build, the Windows save-folder redirect failed, the crash guard): the game lists
     one folder only. Windows (no redirect): the game's own folder, so the modded worlds are hidden. Mac copy: the modded
     folder, so the game saves are hidden. Said in the start notice (dsl_main.c); the Load list cannot carry it, since no
     hook is written in that session.
   - this file off with mods on (names missing, a hook refused): the game saves are hidden; a line on the Load list.
   - a game save the game could not read: it stays in the game's folder (my_quar), so it is not listed; same line. */
/* the worlds (distinct CampaignGuid) of a folder's .json saves; mp_* temporary saves are no world, nor a file with no
   readable header (the game cannot load it either). Worlds that also have a save in not_in are left out: they are listed
   from there. */
#define MAXHW 512
static int folder_worlds(const char *dir, char (*g)[40], int max) {
    char nd[1300]; dsl_os_native_path(dir, nd, sizeof nd);
    DIR *d = opendir(nd); struct dirent *de; int n = 0; if (!d) return 0;
    while ((de = readdir(d)) && n < max) {
        if (de->d_name[0] == '.' || !is_json(de->d_name) || mp_temp(de->d_name)) continue;
        char p[1600], nm[300], w[40] = "", clan[64]; struct stat st;
        snprintf(p, sizeof p, "%s" SEP "%s", nd, de->d_name);
        if (stat(p, &st) || !S_ISREG(st.st_mode)) continue;
        snprintf(nm, sizeof nm, "%.*s", (int)(strlen(de->d_name) - 5), de->d_name);
        if (!name_guid(nm, w) && !file_header(p, w, clan)) continue;
        int dup = 0; for (int i = 0; i < n && !dup; i++) dup = !strcmp(g[i], w);
        if (!dup) snprintf(g[n++], 40, "%s", w);
    }
    closedir(d); return n;
}
int dsl_saves_hidden_worlds(const char *dir, const char *not_in) {
    if (!dir || !*dir) return 0;
    static char a[MAXHW][40], b[MAXHW][40];
    int na = folder_worlds(dir, a, MAXHW), nb = not_in && *not_in ? folder_worlds(not_in, b, MAXHW) : 0, k = 0;
    for (int i = 0; i < na; i++) {
        int both = 0; for (int j = 0; j < nb && !both; j++) both = !strcmp(a[i], b[j]);
        k += !both;
    }
    return k;
}
/* mods off this session: the start notice's line on the worlds the Load list will not show ("" = none) */
void dsl_saves_off_note(int win, char *out, size_t cap) {
    out[0] = 0;
    const char *g = getenv("DSML_GAME_SAVES"), *d = getenv("DSML_DATA");
    if (!d || !*d) return;
    char m[1100]; snprintf(m, sizeof m, "%s" SEP "Saves", d);
    int n = win ? dsl_saves_hidden_worlds(m, g) : dsl_saves_hidden_worlds(g, m);
    if (n <= 0) return;
    if (win) snprintf(out, cap, TRN("%d modded world is not in the Load list this session. It is not lost: it stays in the "
                                    "Dungeon Settlers Modded save folder and comes back when the mods are on again.",
                                    "%d modded worlds are not in the Load list this session. They are not lost: they stay in the "
                                    "Dungeon Settlers Modded save folder and come back when the mods are on again.", n), n);
    else snprintf(out, cap, TRN("%d of your game worlds is not in the Load list this session. It is not lost: play it in "
                                "Dungeon Settlers, or here when the mods are on again.",
                                "%d of your game worlds are not in the Load list this session. They are not lost: play them in "
                                "Dungeon Settlers, or here when the mods are on again.", n), n);
    dsi_utf8_fix_end(out);
    dsl_log("saves: mods off: %d %s world(s) not listed this session", n, win ? "modded" : "game");
}
static int off_worlds;                                   /* this file off with mods on: game worlds not listed */
static char unread[16][256]; static int nunread;         /* game saves the game could not read (kept, so not listed) */
void dsl_saves_hidden(int *off, int *unread_n) {
    *off = off_worlds; *unread_n = 0;
    pthread_mutex_lock(&mu);
    for (int i = 0; i < nunread; i++) { struct stat st; char nd[1300]; if (!stat(dsl_os_native_path(unread[i], nd, sizeof nd), &st)) (*unread_n)++; }
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
            pthread_mutex_lock(&mu);                                     /* not listed: the Load list says so (dsl_rows.c) */
            int k = 0; while (k < nunread && strcmp(unread[k], p)) k++;
            if (k == nunread && nunread < 16) snprintf(unread[nunread++], sizeof unread[0], "%s", p);
            pthread_mutex_unlock(&mu);
            return 0;
        }
    }
    return o_quar(path, why, mi);
}

/* ------------------------------------------------------------------ start: old first-start copies */
static char differ[8][64], differ_path[8][1600]; static int differ_newer[8]; static int ndiffer; static int differ_told;
static int same_bytes(const char *a, const char *b) {
    FILE *x = fopen(a, "rb"), *y = fopen(b, "rb"); int same = x && y;
    static char p[65536], q[65536];                      /* main thread only (start, the warning's Load) */
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
const char *dsl_world_warned(const char *guid);                                      /* dsl_reg.c, worlds.json */
int dsl_world_set_differs(const char *guid);
void dsl_world_set_warned(const char *guid, const char *key);
const char *dsl_mod_name(const char *id);
int dsl_running_save_backup(void);
static void *k_loadreq; static int32_t off_campaign = -1;
static void (*o_add)(Obj self, Obj ev, const void *mi);
static void (*ctor_load)(Obj, Obj, const void *);
static Obj (*obj_new)(void *klass);
static uintptr_t (*gc_new)(Obj, int); static Obj (*gc_get)(uintptr_t); static void (*gc_free)(uintptr_t);
static uintptr_t h_queue;
static char w_name[300], w_guid[40], w_key[512]; static int w_pending, w_frames, w_open, w_pass;
static char w_bdir[1600]; static int w_backup;           /* the backup folder named in the question (save_backup mods) */
static char f_text[1024]; static int f_frames;            /* the backup failed: its notice, a few frames after the box closed */
/* 1 = this load needs the question first (a game save, a save_dependent mod runs, not asked for this world and set) */
static int needs_warning(const char *name, char *guid, char *key, size_t kcap) {
    if (!on || !dsl_saves_is_game(name)) return 0;
    if (!game_guid(name, guid)) snprintf(guid, 40, "save:%s", name);
    const char *ids[64]; int n = dsl_world_save_dependent(guid, ids, 64, key, kcap);   /* Hell only for a world on Hell */
    if (!n) return 0;
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
/* ---- the backup (a running mod with "save_backup") */
/* <backups_d>/<YYYY-MM-DD_HHMM>_<world name>: the clan name of the save's header (else the save name), file-safe on Mac
   and Windows (letters, digits, '-', UTF-8 kept; anything else '_') */
static void backup_dir_for(const char *name, char *out, size_t cap) {
    char p[1500], g[40], clan[64] = "", safe[56]; size_t k = 0;
    snprintf(p, sizeof p, "%s" SEP "%s.json", game_d, name);
    file_header(p, g, clan);
    const unsigned char *c = (const unsigned char *)(clan[0] ? clan : name);
    for (; *c && k + 1 < sizeof safe; c++) {
        int ok = (*c >= '0' && *c <= '9') || ((*c | 32) >= 'a' && (*c | 32) <= 'z') || *c == '-' || *c >= 0x80;
        if (!ok && (!k || safe[k - 1] == '_')) continue;              /* no leading or double '_' */
        safe[k++] = ok ? (char)*c : '_';
    }
    while (k && safe[k - 1] == '_') k--;
    safe[k] = 0; dsi_utf8_fix_end(safe);
    if (!safe[0]) snprintf(safe, sizeof safe, "world");
    char ts[32]; time_t t = time(NULL); strftime(ts, sizeof ts, "%Y-%m-%d_%H%M", localtime(&t));
    snprintf(out, cap, "%s" SEP "%s_%s", backups_d, ts, safe);
}
/* 1 = path is dir or inside it (either separator, any case) */
static int inside(const char *path, const char *dir) {
    size_t n = strlen(dir); if (!n) return 0;
    for (size_t i = 0; i < n; i++) {
        char a = path[i], b = dir[i]; if (!a) return 0;
        if (a == '\\') a = '/'; if (b == '\\') b = '/';
        if (a != b && !((a | 32) == (b | 32) && (a | 32) >= 'a' && (a | 32) <= 'z')) return 0;
    }
    return !path[n] || path[n] == '/' || path[n] == '\\';
}
/* one byte copy, then read back and compared; the source's file times kept. 0 = ok, else why */
static int copy_file(const char *src, const char *dst, char *why, size_t wcap) {
    FILE *in = fopen(src, "rb"); if (!in) { snprintf(why, wcap, "%s: %s", src, strerror(errno)); return -1; }
    FILE *out = fopen(dst, "wb"); if (!out) { snprintf(why, wcap, "%s: %s", dst, strerror(errno)); fclose(in); return -1; }
    enum { BUF = 1 << 20 }; char *b = malloc(BUF); int bad = !b; size_t n;
    if (!b) snprintf(why, wcap, "out of memory");
    while (!bad && (n = fread(b, 1, BUF, in)) > 0)
        if (fwrite(b, 1, n, out) != n) { bad = 1; snprintf(why, wcap, "%s: %s", dst, strerror(errno)); }
    if (!bad && ferror(in)) { bad = 1; snprintf(why, wcap, "%s: read error", src); }
    free(b); fclose(in);
    if (fclose(out) && !bad) { bad = 1; snprintf(why, wcap, "%s: %s", dst, strerror(errno)); }
    if (bad) return -1;
    struct stat st;
    if (!stat(src, &st)) { struct utimbuf u = { st.st_atime, st.st_mtime }; utime(dst, &u); }
    if (!same_bytes(src, dst)) { snprintf(why, wcap, "%s: the copy differs from the save file", dst); return -1; }
    return 0;
}
/* the world's game-save files (the loaded one first + every other one with its guid) copied into a new folder.
   0 = ok (*nf files in dir), else why */
static int backup_world(const char *name, const char *guid, char *dir, size_t dcap, int *nf, char *why, size_t wcap) {
    static char files[MAXG][256]; int n = 0; *nf = 0;
    pthread_mutex_lock(&mu);
    scan_game_locked();
    int have = g_find(name) != NULL;
    if (have) snprintf(files[n++], 256, "%s", g_find(name)->name);
    for (int i = 0; i < ng && n < MAXG; i++)
        if (strcasecmp(G[i].name, name) && guid[0] && strncmp(guid, "save:", 5) && !strcasecmp(G[i].guid, guid)) snprintf(files[n++], 256, "%s", G[i].name);
    pthread_mutex_unlock(&mu);
    if (!have) { snprintf(why, wcap, "%s.json is not in %s", name, game_d); return -1; }
    if (inside(dir, game_d) || inside(dir, mod_d)) { snprintf(why, wcap, "%s is inside a Saves folder", dir); return -1; }
    char mods[1200]; snprintf(mods, sizeof mods, "%s", backups_d);
    char *e = mods + strlen(mods); while (e > mods && e[-1] != '/' && e[-1] != '\\') e--; if (e > mods) e[-1] = 0;
    mkdir(mods, 0755); mkdir(backups_d, 0755);
    struct stat st; char base[1600]; snprintf(base, sizeof base, "%s", dir);
    for (int k = 2; !stat(dir, &st) && k < 100; k++) snprintf(dir, dcap, "%s_%d", base, k);   /* never into an older copy */
    if (mkdir(dir, 0755)) { snprintf(why, wcap, "%s: %s", dir, strerror(errno)); return -1; }
    for (int i = 0; i < n; i++) {
        char src[1500], dst[1900];
        snprintf(src, sizeof src, "%s" SEP "%s.json", game_d, files[i]); snprintf(dst, sizeof dst, "%s" SEP "%s.json", dir, files[i]);
        if (copy_file(src, dst, why, wcap)) return -1;
        (*nf)++;
    }
    return 0;
}
static void on_load(void *ud, DSLObj w) {
    (void)ud; (void)w; w_open = 0;
    if (w_backup) {
        char why[1800] = ""; int nf = 0;
        if (backup_world(w_name, w_guid, w_bdir, sizeof w_bdir, &nf, why, sizeof why)) {
            dsl_log("saves: backup of %s FAILED (%d file(s) copied to %s): %s: NOT loaded", w_name, nf, w_bdir, why);
            snprintf(f_text, sizeof f_text, TR("This world was not loaded: its save files could not be copied first.\n\n%s"), why);
            dsi_utf8_fix_end(f_text); f_frames = 3;                   /* after the question's box closed */
            return;                                                    /* not loaded, not marked as answered: asked again */
        }
        dsl_saves_backed_up++;
        dsl_log("saves: backup of %s (world %s): %d file(s) copied to %s", w_name, w_guid, nf, w_bdir);
    }
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
    w_backup = dsl_running_save_backup();
    char shown[2400] = "";                  /* the folder as shown: a zero-width space after each separator, so the box
                                               wraps the long path at a folder name, never in the middle of a word */
    if (w_backup) {          /* shorter warning + the folder: the stock box is a fixed 520 x 109 (longer text runs over its buttons) */
        backup_dir_for(w_name, w_bdir, sizeof w_bdir);
        size_t o = 0;
        for (const char *c = w_bdir; *c && o + 4 < sizeof shown; c++) {
            shown[o++] = *c;
            if (*c == '/' || *c == '\\') { shown[o++] = (char)0xE2; shown[o++] = (char)0x80; shown[o++] = (char)0x8B; }
        }
        shown[o] = 0;
        snprintf(b, sizeof b, TRN("This world is one of your game saves. This mod changes what it saves: %s. "
                                  "It may break later in the game without the mod loader.\n\n"
                                  "Load first keeps a copy of its save files in:\n%s",
                                  "This world is one of your game saves. These mods change what it saves: %s. "
                                  "It may break later in the game without the mod loader.\n\n"
                                  "Load first keeps a copy of its save files in:\n%s", i), list, shown);
    } else
        snprintf(b, sizeof b, TRN("This world is one of your game saves. This mod changes what the world saves: %s.\n\n"
                                  "If you play it now, the world may break when you load it later in the game without the mod loader.",
                                  "This world is one of your game saves. These mods change what the world saves: %s.\n\n"
                                  "If you play it now, the world may break when you load it later in the game without the mod loader.", i), list);
    dsi_utf8_fix_end(b);
    dsl_log("saves: warning shown for %s (world %s, mods %s)%s%s", w_name, w_guid, w_key, w_backup ? ", backup to " : "", w_backup ? w_bdir : "");
    u->notice(b, TR("Load"), TR("Cancel"), on_load, on_cancel, NULL);
}
static int remove_gate(Obj queue, Obj ev);
static void my_add(Obj self, Obj ev, const void *mi) {
    if (ev && remove_gate(self, ev)) return;                                  /* a game save's remove: asked first */
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
static void *meth(const char *ns, const char *k, const char *m, int argc);
typedef struct { uint64_t lo, hi; } DGuid;                /* System.Guid by value (same ABI as the game's method) */
static void guid_str(DGuid v, char g[40]) {
    const uint8_t *b = (const uint8_t *)&v;
    snprintf(g, 40, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             *(const uint32_t *)b, *(const uint16_t *)(b + 4), *(const uint16_t *)(b + 6), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}
static void (*o_complete)(Obj view, DGuid g, int32_t version, uint8_t confirmed, const void *mi);
static Obj (*o_delcamp)(Obj loader, DGuid g, const void *mi);
static void *a_complete, *a_remove;                       /* the game's methods: the test calls them (a_complete hooked) */
/* The save's remove (bug 2026-10-20, a Windows player: "Remove File" in Host a world did not delete): the game's own Yes
   callback (SaveLoadView.<>c__DisplayClass57_0.<CheckRemoveSlot>b__0) has SaveLoadView.ConfirmRemove INLINED (build
   25846389, arm64: UIInput SaveLoadUI_RemoveCampaign built in the callback itself), so the old hook on ConfirmRemove never
   ran on the player's path. Every remove reaches the game as the event RemoveFileRequested(name) through
   IEventQueue.AddEvent (InputDispatcher.<Update>b__8_0, an interface call: never inlined) and then
   InputEventHandler.OnEvent(RemoveFileRequested) -> CampaignSaveLoader.Delete(name). So the question sits on that event:
   a game save's remove event is held (strong GC handle) and the question shows a few frames later from the frame tick
   (the game's box that sent it has closed by then; the kit reuses that same CommonNoticeView). Delete = the held event
   goes into the queue again; Cancel = it is dropped: nothing deleted. Modded-only saves: the stock flow, no question. */
static void *k_rmreq; static int32_t off_rmname = -1; static int rm_pass;
static struct { int open, kind, frames; uintptr_t h_view, h_ev, h_q; DGuid g; int32_t version; char name[300], what[300]; } D;   /* kind 1 world, 2 save */
static void del_free(void) {
    if (D.h_view) { gc_free(D.h_view); D.h_view = 0; }
    if (D.h_ev) { gc_free(D.h_ev); D.h_ev = 0; }
    if (D.h_q) { gc_free(D.h_q); D.h_q = 0; }
}
static void del_yes(void *ud, DSLObj w) {
    (void)ud; (void)w; if (!D.open || D.frames) return; D.open = 0;
    Obj v = D.h_view ? gc_get(D.h_view) : NULL, e = D.h_ev ? gc_get(D.h_ev) : NULL, q = D.h_q ? gc_get(D.h_q) : NULL;
    dsl_log("saves: delete answered Delete for %s: deleted in the game's folder too", D.what);
    if (v && D.kind == 1) o_complete(v, D.g, D.version, 1, NULL);
    else if (D.kind == 2 && e && q) { rm_pass = 1; o_add(q, e, NULL); rm_pass = 0; }        /* the game's own remove goes on */
    else dsl_log("saves: delete of %s: the game's request is gone: NOT deleted", D.what);
    del_free();
}
static void del_no(void *ud, DSLObj w) {
    (void)ud; (void)w; if (!D.open || D.frames) return; D.open = 0;
    Obj v = D.h_view ? gc_get(D.h_view) : NULL;
    dsl_log("saves: delete answered Cancel for %s: nothing deleted", D.what);
    if (v && D.kind == 1) o_complete(v, D.g, D.version, 0, NULL);       /* the view's own "not confirmed" (buttons back) */
    del_free();
}
static void del_show(void) {                              /* the frame tick, D.frames after del_ask */
    const DSLUi *u = dsl_ui_kit();
    if (!u) { dsl_log("saves: no kit: delete of %s not asked, NOT deleted", D.what); D.frames = 0; del_no(NULL, NULL); return; }
    dsl_log("saves: delete question shown for %s", D.what);
    u->notice(TR("This save is also used by Dungeon Settlers without mods. Delete it there too?"), TR("Delete"), TR("Cancel"), del_yes, del_no, NULL);
}
/* 1 = asked (the question shows from the frame tick); 0 = cannot ask */
static int del_ask(Obj view, Obj ev, Obj queue, int kind, const char *what) {
    if (!dsl_ui_kit() || !gc_new) { dsl_log("saves: no kit: delete of %s not asked, NOT deleted", what); return 0; }
    D.open = 1; D.kind = kind; D.frames = 3; snprintf(D.what, sizeof D.what, "%s", what);
    D.h_view = view ? gc_new(view, 0) : 0; D.h_ev = ev ? gc_new(ev, 0) : 0; D.h_q = queue ? gc_new(queue, 0) : 0;
    return 1;
}
/* the world's delete button, after the game's own confirm (confirmed = its Yes) */
static void my_complete(Obj view, DGuid g, int32_t version, uint8_t confirmed, const void *mi) {
    if (on && confirmed && view && !D.open) {
        char gs[40]; guid_str(g, gs);
        pthread_mutex_lock(&mu); int game = guid_is_game_locked(gs); pthread_mutex_unlock(&mu);
        if (game) { D.g = g; D.version = version; char w[80]; snprintf(w, sizeof w, "world %s", gs); if (del_ask(view, NULL, NULL, 1, w)) return; }
    } else if (on && confirmed && D.open) { dsl_log("saves: delete held: a delete question is still open"); o_complete(view, g, version, 0, mi); return; }
    o_complete(view, g, version, confirmed, mi);
}
/* my_add: a save's remove request (RemoveFileRequested). 1 = held or dropped (not passed on) */
static int remove_gate(Obj queue, Obj ev) {
    if (!on || rm_pass || !k_rmreq || off_rmname < 0 || obj_class(ev) != k_rmreq) return 0;
    char nm[300]; utf8_of(AT(ev, off_rmname, Obj), nm, sizeof nm);
    pthread_mutex_lock(&mu); int game = g_find(nm) != NULL; pthread_mutex_unlock(&mu);
    if (!game) return 0;                                                      /* modded-only: the game's flow */
    if (D.open) { dsl_log("saves: remove of %s dropped: a delete question is still open (nothing deleted)", nm); return 1; }
    snprintf(D.name, sizeof D.name, "%s", nm); char w[340]; snprintf(w, sizeof w, "save %s", nm);
    if (!del_ask(NULL, ev, queue, 2, w)) return 1;                            /* cannot ask: NOT deleted */
    dsl_log("saves: remove of game save %s held for the question", nm);
    return 1;
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
    static void *kv; static int32_t o_pend = -1, o_ver = -1;
    if (!kv) {
        kv = dsl_find_class("Refactor", "SaveLoadView");
        o_pend = kv ? dsl_field_offset(kv, "_pendingDeleteCampaign") : -1; o_ver = kv ? dsl_field_offset(kv, "_deleteConfirmationVersion") : -1;
    }
    Obj v = kv ? dsl_view_get(kv, DSL_VIEW_NOW) : NULL;
    dsl_log("saves test: request %s (view %s)", c ? c : "?", v ? "open" : "MISSING");
    if (v && c && !strcmp(c, "del_world") && a_complete && o_pend >= 0 && o_ver >= 0) {
        DGuid g; if (!json_str(j, "guid") || !guid_parse(json_str(j, "guid"), &g)) { dsl_log("saves test: bad guid"); json_free(j); return; }
        int32_t ver = AT(v, o_ver, int32_t) + 1; AT(v, o_ver, int32_t) = ver; memcpy((char *)v + o_pend, &g, 16);   /* as CheckDeleteCampaign leaves them */
        ((void (*)(Obj, DGuid, int32_t, uint8_t, const void *))a_complete)(v, g, ver, 1, NULL);
    } else if (v && c && !strcmp(c, "del_save") && a_remove && json_str(j, "name"))
        ((void (*)(Obj, Obj, const void *))a_remove)(v, str_new(json_str(j, "name")), NULL);
    else if (v && c && !strcmp(c, "del_save_ui") && json_str(j, "name")) {   /* the player's path: select the save, press Remove File
                                                                                (the game's own box follows; the test answers it) */
        static void *a_slot, *a_cur, *a_click;               /* a click on the save row; the selected save; Remove File */
        if (!a_slot) { a_slot = meth("Refactor", "SaveLoadView", "OnClickLoadSlot", 1); a_cur = meth("Refactor", "SaveLoadView", "get_CurrentSelectedSaveName", 0);
                       a_click = meth("Refactor", "SaveLoadView", "OnClickRemove", 0); }
        char cur[300] = ""; if (a_cur) utf8_of(((Obj (*)(Obj, const void *))a_cur)(v, NULL), cur, sizeof cur);
        if (a_slot && a_click) {                             /* a second click on the selected row would LOAD it: one click only */
            if (strcmp(cur, json_str(j, "name"))) ((void (*)(Obj, Obj, const void *))a_slot)(v, str_new(json_str(j, "name")), NULL);
            ((void (*)(Obj, const void *))a_click)(v, NULL);
        }
        dsl_log("saves test: Remove File pressed on %s (was selected: %s)", json_str(j, "name"), cur[0] ? cur : "none");
    }
    if (j) json_free(j);
}

/* the title tick (dsl_reg.c dsl_frame_hook) */
int dsl_reg_scene(void);
/* dsl_world.c: a long notice over the stock box that closed a load (CancelIronModeLoad shows the game's fixed 520 x 109
   box, which never grows: a long text ran over its button). A few frames later the kit's box shows it; that one grows. */
void dsl_saves_notice_later(const char *text) {
    snprintf(f_text, sizeof f_text, "%s", text ? text : ""); dsi_utf8_fix_end(f_text); f_frames = 3;
}
void dsl_saves_frame(void) {
    if (f_frames && !--f_frames) { const DSLUi *u = dsl_ui_kit(); if (u) u->notice(f_text, TR("OK"), NULL, NULL, NULL, NULL); }
    if (!on) return;                                     /* the notice above also shows when this file is off (dsl_world.c) */
    test_frame();
    if (D.open && D.frames && !--D.frames) del_show();
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
        int shown = 0, zw = 40;                  /* room for the kit's line breaks in the path (3 bytes per separator), so the
                                                    path still breaks at a folder name, not in the middle of one */
        for (const char *c = where; *c; c++) zw += (*c == '/' || *c == '\\') * 3;
        for (int i = 0; i < ndiffer; i++) {
            snprintf(t, sizeof t, differ_newer[i] ? TR("%s: the old modded copy is NEWER than your game save.")
                                                  : TR("%s: your game save is newer than the old modded copy."), differ[i]);
            if (strlen(b) + 2 + strlen(t) + 2 + strlen(where) + (size_t)zw >= sizeof b) break;
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
static int install_hooks(void);
void dsl_saves_install(void) {
    const char *g = getenv("DSML_GAME_SAVES"), *d = getenv("DSML_DATA");
    if (!g || !*g || !d || !*d) { dsl_log("saves: one save folder (no DSML_GAME_SAVES / DSML_DATA)"); return; }
    snprintf(game_d, sizeof game_d, "%s", g);
    size_t gl = strlen(game_d); while (gl > 1 && (game_d[gl - 1] == '/' || game_d[gl - 1] == '\\')) game_d[--gl] = 0;
    snprintf(mod_d, sizeof mod_d, "%s" SEP "Saves", d);
    snprintf(copies_d, sizeof copies_d, "%s" SEP "Mods" SEP "save_copies", d);
    snprintf(backups_d, sizeof backups_d, "%s" SEP "Mods" SEP "save_backups", d);
    struct stat st;
    if (stat(game_d, &st) || !S_ISDIR(st.st_mode)) { dsl_log("saves: the game's save folder %s is not there: modded saves only", game_d); return; }
    if (install_hooks()) {                                    /* off: the game saves are not listed; the Load list says how many */
        off_worlds = dsl_saves_hidden_worlds(game_d, mod_d);
        dsl_log("saves: off: %d game world(s) in %s are not listed", off_worlds, game_d);
    }
}
static int install_hooks(void) {
    if (getenv("DSL_TEST_SAVES_OFF") && *getenv("DSL_TEST_SAVES_OFF") == '1') { dsl_log("saves: DSL_TEST_SAVES_OFF=1: hooks not tried"); return -1; }
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
        dsl_log("saves: names missing (offsets guid %d name %d): the game's saves are NOT listed", off_guid, off_name); return -1;
    }
    /* all four or none: half of them would write a game world into the modded folder */
    if (hk(a_all, (void *)my_all, (void **)&o_all, "loader saves GetAllSavePaths")) return -1;
    if (hk(a_path, (void *)my_path, (void **)&o_path, "loader saves GetCampaignSavePath")) { dsl_unhook(DSL_OWNER_LOADER, a_all, "saves"); return -1; }
    if (hk(a_save, (void *)my_savefile, (void **)&o_savefile, "loader saves SaveFile")) { dsl_unhook(DSL_OWNER_LOADER, a_all, "saves"); dsl_unhook(DSL_OWNER_LOADER, a_path, "saves"); return -1; }
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
    k_rmreq = dsl_find_class("Refactor.Main.Event", "RemoveFileRequested");    /* the save's remove: asked at AddEvent (my_add) */
    off_rmname = k_rmreq ? dsl_field_offset(k_rmreq, "CampaignName") : -1;
    if (o_add == a_add || off_rmname < 0) dsl_log("saves: save remove question off: a game save's Remove File deletes it in the game's folder with the game's own box only");
    dsl_log("saves: on: %d game save(s) in %s are listed and played in place; modded-only saves in %s", n, game_d, mod_d);
    return 0;
}

/* ------------------------------------------------------------------ tutorial progress from the normal game (before GameAssembly) */
// Owner 2026-10-20 (v0.1.20 test): "the modded game uses its own profile, so tutorial / hint progress starts from zero
// and advanced players get spammed with tutorial notices". The game keeps that progress in ONE file of its data folder,
// OutGame.json (Refactor.SaveLoadHelper.OutGameSaveFileName, decompile 25846389): "TutorialSaveData" =
// { "OccurredConditions": [...], "CompletedTutorials": [...] } (Refactor.Main.TutorialSaveData), read once into
// TutorialContainer; AlertEvaluator.IsTutorialAlreadySeen and the guide providers ask it. The starters copy OutGame.json
// once, on the first start only (app/stub/ds_modded.c, start/win/ds_modded_win.c), so progress made in the normal game
// later never came over. Now, on every start before any game code runs: every key the normal game's file has and the
// modded file lacks is added to the modded file. Only adds (never removes a key the modded game has), never writes the
// normal game's file. Normal game: dirname(DSML_GAME_SAVES) (Mac ~/Library/Application Support/CanOpener/Dungeon
// Settlers, Windows %USERPROFILE%\AppData\LocalLow\CanOpener\Dungeon Settlers); modded: DSML_DATA. A modded file the
// loader cannot read is left as it is (the game makes its own choice), a missing one becomes a copy of the normal one
// (as the first start does). The rest of the file is written back as read (2-space indent like the game's).
typedef struct { char *b; size_t n, cap; int bad; } JB;
static void jb_put(JB *w, const char *s, size_t l) {
    if (w->bad) return;
    if (w->n + l + 1 > w->cap) { size_t c = (w->cap ? w->cap * 2 : 8192) + l; char *nb = realloc(w->b, c); if (!nb) { w->bad = 1; return; } w->b = nb; w->cap = c; }
    memcpy(w->b + w->n, s, l); w->n += l; w->b[w->n] = 0;
}
static void jb_s(JB *w, const char *s) { jb_put(w, s, strlen(s)); }
static void jb_ind(JB *w, int d) { jb_put(w, "\n", 1); for (int i = 0; i < d; i++) jb_put(w, "  ", 2); }
static void jb_q(JB *w, const char *s) {
    jb_put(w, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char e[8];
        if (*p == '"' || *p == '\\') { e[0] = '\\'; e[1] = (char)*p; jb_put(w, e, 2); }
        else if (*p == '\n') jb_s(w, "\\n"); else if (*p == '\r') jb_s(w, "\\r"); else if (*p == '\t') jb_s(w, "\\t");
        else if (*p < 0x20) { snprintf(e, sizeof e, "\\u%04x", *p); jb_s(w, e); }
        else jb_put(w, (const char *)p, 1);
    }
    jb_put(w, "\"", 1);
}
static const char *const TUT_LISTS[2] = { "OccurredConditions", "CompletedTutorials" };
typedef struct { const char **add[2]; int nadd[2]; } TutAdd;
static void jb_node(JB *w, const JNode *n, int d, const TutAdd *ta, int where);   /* where: 0 other, 1 TutorialSaveData, 2+i list i */
static void jb_extra_list(JB *w, const TutAdd *ta, int i, int d, int first) {
    for (int k = 0; k < ta->nadd[i]; k++) { if (!first || k) jb_put(w, ",", 1); jb_ind(w, d); jb_q(w, ta->add[i][k]); }
}
static void jb_node(JB *w, const JNode *n, int d, const TutAdd *ta, int where) {
    char num[64];
    if (where >= 2 && (!n || n->t != JN_ARR)) {                        /* the list was null (or not a list): a new one */
        int i = where - 2;
        jb_put(w, "[", 1); jb_extra_list(w, ta, i, d + 1, 1); if (ta->nadd[i]) jb_ind(w, d); jb_put(w, "]", 1); return;
    }
    if (where == 1 && (!n || n->t != JN_OBJ)) {
        jb_put(w, "{", 1);
        for (int i = 0; i < 2; i++) { if (i) jb_put(w, ",", 1); jb_ind(w, d + 1); jb_q(w, TUT_LISTS[i]); jb_s(w, ": "); jb_node(w, NULL, d + 1, ta, 2 + i); }
        jb_ind(w, d); jb_put(w, "}", 1); return;
    }
    switch (n->t) {
    case JN_NULL: jb_s(w, "null"); break;
    case JN_BOOL: jb_s(w, n->b ? "true" : "false"); break;
    case JN_NUM:
        if (n->num == (double)(long long)n->num && n->num > -1e15 && n->num < 1e15) snprintf(num, sizeof num, "%lld", (long long)n->num);
        else snprintf(num, sizeof num, "%.17g", n->num);
        jb_s(w, num); break;
    case JN_STR: jb_q(w, n->str ? n->str : ""); break;
    case JN_ARR: {
        int c = 0; jb_put(w, "[", 1);
        for (const JNode *x = n->child; x; x = x->next) { if (c++) jb_s(w, ","); jb_ind(w, d + 1); jb_node(w, x, d + 1, ta, 0); }
        if (where >= 2) { jb_extra_list(w, ta, where - 2, d + 1, c == 0); c += ta->nadd[where - 2]; }
        if (c) jb_ind(w, d);
        jb_put(w, "]", 1); break;
    }
    case JN_OBJ: {
        int c = 0, seen[2] = { 0, 0 }; jb_put(w, "{", 1);
        for (const JNode *x = n->child; x; x = x->next) {
            int sub = 0;
            if (where == -1 && x->key && !strcmp(x->key, "TutorialSaveData")) { sub = 1; seen[0] = 1; }
            if (where == 1) for (int i = 0; i < 2; i++) if (x->key && !strcmp(x->key, TUT_LISTS[i])) { sub = 2 + i; seen[i] = 1; }
            if (c++) jb_s(w, ","); jb_ind(w, d + 1); jb_q(w, x->key ? x->key : ""); jb_s(w, ": "); jb_node(w, x, d + 1, ta, sub);
        }
        if (where == -1 && !seen[0]) { if (c++) jb_s(w, ","); jb_ind(w, d + 1); jb_q(w, "TutorialSaveData"); jb_s(w, ": "); jb_node(w, NULL, d + 1, ta, 1); }
        if (where == 1) for (int i = 0; i < 2; i++) if (!seen[i]) { if (c++) jb_s(w, ","); jb_ind(w, d + 1); jb_q(w, TUT_LISTS[i]); jb_s(w, ": "); jb_node(w, NULL, d + 1, ta, 2 + i); }
        if (c) jb_ind(w, d);
        jb_put(w, "}", 1); break;
    }
    }
}
static char *tut_slurp(const char *p, size_t *len) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    size_t cap = 1 << 16, n = 0; char *b = malloc(cap + 1);
    while (b) { size_t r = fread(b + n, 1, cap - n, f); n += r; if (n < cap) break; if (cap > (64u << 20)) { free(b); b = NULL; break; }
                char *nb = realloc(b, cap * 2 + 1); if (!nb) { free(b); b = NULL; break; } b = nb; cap *= 2; }
    fclose(f); if (!b) return NULL; b[n] = 0; if (len) *len = n; return b;
}
static int tut_has(const JNode *list, const char *s) {
    if (!list || list->t != JN_ARR) return 0;
    for (const JNode *x = list->child; x; x = x->next) if (x->t == JN_STR && x->str && !strcmp(x->str, s)) return 1;
    return 0;
}
static int tut_put(const char *path, const char *text, size_t len) {    /* temp + checks + rename: a failed write keeps the old file */
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp-tutorial", path);
    FILE *f = fopen(tmp, "wb"); if (!f) return -1;
    int bad = fwrite(text, 1, len, f) != len; bad |= fflush(f) != 0; bad |= fclose(f) != 0;
    if (bad) { remove(tmp); return -1; }
    return dsl_os_replace(tmp, path);
}
int dsl_saves_tutorial_added;                                          /* tests / log: keys added this start */
void dsl_saves_tutorial_import(void) {
    const char *g = getenv("DSML_GAME_SAVES"), *d = getenv("DSML_DATA");
    if (!g || !*g || !d || !*d) return;
    char sd[1100], sp[1200], mp[1200];
    snprintf(sd, sizeof sd, "%s", g);
    size_t l = strlen(sd); while (l > 1 && (sd[l - 1] == '/' || sd[l - 1] == '\\')) sd[--l] = 0;
    while (l > 0 && sd[l - 1] != '/' && sd[l - 1] != '\\') sd[--l] = 0;      /* <normal game data>/Saves -> <normal game data>/ */
    if (!l) return;
    snprintf(sp, sizeof sp, "%sOutGame.json", sd); snprintf(mp, sizeof mp, "%s" SEP "OutGame.json", d);
    size_t sl = 0, ml = 0; char *st = tut_slurp(sp, &sl);
    if (!st) return;                                                    /* the normal game has no progress (never played here) */
    JNode *sj = json_parse(st);
    JNode *stut = json_get(sj, "TutorialSaveData");
    if (!stut || stut->t != JN_OBJ) { dsl_log("tutorial: %s has no TutorialSaveData: nothing to bring", sp); json_free(sj); free(st); return; }
    char *mt = tut_slurp(mp, &ml);
    if (!mt) {                                                          /* no modded file yet: the normal game's, as the first start copies it */
        int r = tut_put(mp, st, sl);
        int n = 0; for (int i = 0; i < 2; i++) { JNode *a = json_get(stut, TUT_LISTS[i]); if (a && a->t == JN_ARR) for (JNode *x = a->child; x; x = x->next) n++; }
        if (!r) dsl_saves_tutorial_added = n;
        dsl_log("tutorial: no modded OutGame.json: %s the normal game's (%d tutorial key(s))", r ? "copy FAILED of" : "copied", n);
        json_free(sj); free(st); return;
    }
    JNode *mj = json_parse(mt);
    if (!mj || mj->t != JN_OBJ) { dsl_log("tutorial: the modded OutGame.json cannot be read: left as it is"); json_free(mj); json_free(sj); free(st); free(mt); return; }
    JNode *mtut = json_get(mj, "TutorialSaveData");
    TutAdd ta = { { NULL, NULL }, { 0, 0 } };
    for (int i = 0; i < 2; i++) {
        JNode *sa = json_get(stut, TUT_LISTS[i]), *ma = mtut && mtut->t == JN_OBJ ? json_get(mtut, TUT_LISTS[i]) : NULL;
        if (!sa || sa->t != JN_ARR) continue;
        int cap = 0; for (JNode *x = sa->child; x; x = x->next) cap++;
        ta.add[i] = calloc((size_t)cap + 1, sizeof(char *));
        for (JNode *x = sa->child; x && ta.add[i]; x = x->next) {
            if (x->t != JN_STR || !x->str || !*x->str || tut_has(ma, x->str)) continue;
            int dup = 0; for (int k = 0; k < ta.nadd[i]; k++) if (!strcmp(ta.add[i][k], x->str)) dup = 1;
            if (!dup) ta.add[i][ta.nadd[i]++] = x->str;
        }
    }
    int n = ta.nadd[0] + ta.nadd[1];
    if (n) {
        JB w = { 0 };
        jb_node(&w, mj, 0, &ta, -1);
        int r = w.bad ? -1 : tut_put(mp, w.b, w.n);
        if (!r) dsl_saves_tutorial_added = n;
        dsl_log("tutorial: %s %d seen hint(s) and %d finished tutorial(s) of the normal game into the modded profile (%s)",
                r ? "FAILED to bring" : "brought", ta.nadd[0], ta.nadd[1], mp);
        free(w.b);
    } else dsl_log("tutorial: the modded profile already has every tutorial key of the normal game");
    free(ta.add[0]); free(ta.add[1]);
    json_free(mj); json_free(sj); free(st); free(mt);
}
