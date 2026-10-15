// mp_game: real game binding of mp_core (build 25846389, arm64). Dylib only; see mp_game.h for the
// functions the determinism lane must expose, MP_NOTES.md for the design.
#include "mp_game.h"
#include "mp.h"
#include "ord_api.h"
#include "trn.h"
#include "mp_steam.h"
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

/* GameSpeedSetting (TypeDefIndex 6123): _gameSpeed +0x10, _requestedGameSpeed +0x14,
   _pauseReasons Dictionary<GamePauseReason,int> +0x18, _gameSpeedLockCount +0x20 */
#define VA_GS_SETSPEED   0xD24114   /* bool SetSpeed(GameSpeed speed, SwitchPauseState pauseState) */
#define VA_GS_ADDPAUSE   0xD24848   /* bool AddPauseReason(GamePauseReason reason) */
#define VA_GS_REMPAUSE   0xD249BC   /* bool RemovePauseReason(GamePauseReason reason) */
enum { PS_ROLLBACK = 0, PS_ABSOLUTE = 1 };   /* SwitchPauseState */

#define MP_SAVE_NAME "mp_sync"
#define TI_QUIT_TITLE_REQ   0x6652F28   /* QuitToTitleSceneRequested */
#define VA_QUIT_TITLE_CTOR  0xD61E50    /* .ctor(bool autoSaveBeforeQuit) */
/* Steamworks.NET (in GameAssembly): NativeMethods.SteamAPI_ManualDispatch_GetNextCallback, the P/Invoke stub the
   game's CallbackDispatcher.RunFrame (0x1167898) pulls every callback of its pipe through (dump.cs 25846389). */
#define VA_STEAM_GETNEXT 0x1141AAC  /* bool (HSteamPipe pipe, IntPtr pCallbackMsg) */
/* SystemMenuView.QuitToTitleSceneCallback: the confirmed "Quit to title" (UIInput 0x5c on the view's stream +0x28) */
#define VA_QUIT_TO_TITLE 0xF2F234
void *ord_debug_class(const char *ns, const char *name);    /* ord_game.c */

static mp_config cfg;
static int cfg_ok, start_failed;
static mp_session *S;
/* lan_v9: +1 per started session. A rejoin's new session can get the freed one's address (malloc reuse), so "the
   session pointer changed" misses it and per-session order handlers stay unregistered (run 49: the rejoined guest
   ignored every ui order). Lanes compare this instead. */
static unsigned S_gen;
unsigned mp_game_session_gen(void) { return S_gen; }
static int hooks_done, steam_ok = -1;     /* steam_ok: -1 not tried, 0 no (retried every 2 s), 1 libsteam_api found + logged on */
static double steam_next_try;
static int ended, quit_pending;           /* ended: MP_STEAM_ENDED until mp_steam_ack */
static int played;                        /* this session reached a running campaign (stock quit = leave) */
static double mono_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }
static char ended_msg[160];
static uint64_t join_lobby_id;
static dsnet_steam_host *steam_host;
static dsnet_steam_join *steam_join;      /* joiner: waiting for the Steam bridge's local port */
static Obj gs_seen;                 /* last GameSpeedSetting seen by a hook */
static int showing;                 /* show_speed is calling the original */
static int host_reload;             /* host: reload its own join-point save after this frame (see g_ready) */

static bool (*o_setspeed)(Obj, int, int, void *);
static bool (*o_addpause)(Obj, int, void *);
static bool (*o_rempause)(Obj, int, void *);
static bool (*o_getnext)(int32_t, void *, void *);

static Obj speed_setting(void) { Obj s = mf ? AT(mf, 0xD8, Obj) : NULL; return s ? AT(s, 0x10, Obj) : NULL; }
/* Not ready = loading (join, resync, host reload) or title: the load flow's own SetSpeed / pause calls stay local.
   Before 2026-10-12 they became this player's votes and left both peers paused after a resync. */
static int pass_through(void) { return !S || showing || mp_is_applying(S) || rig_det_in_sim() || !rig_det_mp_ready() || host_reload; }

/* ---------------------------------------------------------------- speed / pause hooks */
/* The speed bar, hotkeys and SetGameSpeedRequested / ChangeGameSpeedRequested all end in SetSpeed.
   From the UI it becomes a vote and the local setting is left alone; mp's show_speed sets it on
   every peer at the same step. */
static bool h_setspeed(Obj gs, int speed, int ps, void *mi) {
    gs_seen = gs;
    if (pass_through()) return o_setspeed(gs, speed, ps, mi);
    rig_log("mp: local SetSpeed(%s, %s) -> vote", mp_speed_name(speed, 0), ps == PS_ABSOLUTE ? "absolute pause" : "toggle");
    if (speed == 0) {
        if (ps == PS_ABSOLUTE) mp_vote_pause(S, 1);
        else mp_toggle_pause(S);
    } else {
        mp_stats st; mp_get_stats(S, &st);
        mp_vote_speed(S, speed);
        if (st.paused) mp_vote_pause(S, 0);                  /* picking a speed also un-pauses, like the game */
    }
    return false;
}
static bool h_addpause(Obj gs, int reason, void *mi) {
    gs_seen = gs;
    if (pass_through()) return o_addpause(gs, reason, mi);
    rig_log("mp: local AddPauseReason(%d)", reason);
    switch (reason) {
    case MP_PAUSE_USER: mp_vote_pause(S, 1); return false;
    case MP_PAUSE_ESSENTIAL_POPUP: case MP_PAUSE_POPUP_TIMELOCK: case MP_PAUSE_CONSECRATION:
        mp_popup_pause(S, reason, 1); return false;          /* never pauses locally; a vote if allow_pause */
    default: return o_addpause(gs, reason, mi);             /* CheatConsole, Loading: local only */
    }
}
static bool h_rempause(Obj gs, int reason, void *mi) {
    gs_seen = gs;
    if (pass_through()) return o_rempause(gs, reason, mi);
    rig_log("mp: local RemovePauseReason(%d)", reason);
    switch (reason) {          /* also removed locally: the add may have been local (before the lock); harmless if absent */
    case MP_PAUSE_USER: mp_vote_pause(S, 0); { int sh = showing; showing = 1; o_rempause(gs, reason, mi); showing = sh; } return false;
    case MP_PAUSE_ESSENTIAL_POPUP: case MP_PAUSE_POPUP_TIMELOCK: case MP_PAUSE_CONSECRATION:
        mp_popup_pause(S, reason, 0); { int sh = showing; showing = 1; o_rempause(gs, reason, mi); showing = sh; } return false;
    default: return o_rempause(gs, reason, mi);
    }
}

/* Every callback the game's Steam dispatcher takes. The game registers no lobby/invite callbacks, so
   GameLobbyJoinRequested (333) / GameRichPresenceJoinRequested (337) would be dropped: hand them to dsnet_steam. */
typedef struct { int32_t user, id; uint8_t *param; int32_t cub; } steam_cbmsg;   /* CallbackMsg_t */
static bool h_getnext(int32_t pipe, void *msg, void *mi) {
    bool r = o_getnext(pipe, msg, mi);
    if (r && msg) {
        steam_cbmsg *m = msg;
        if (m->id == 333 || m->id == 337) dsnet_steam_feed_callback(m->id, m->param, m->cub);
    }
    return r;
}

/* ---------------------------------------------------------------- the world's save lives only on the host (owner 2026-10-13)
   The join-point save travels as mp_sync.json: the game can only load a file in Saves/ whose stem equals its SaveName.
   Every peer deletes that file once its load is done, so it never stays in a save list (or in "Continue").
   A guest never writes a save of an mp world: CampaignSaveLoader.Save (manual save, autosave, save-before-exit; the
   transitions lane already stops guest autosaves earlier) returns false on a guest and while its mp world is loaded. */
#define VA_SAVE        0x814890     /* bool CampaignSaveLoader.Save(string campaignName, bool isAutoSave, string displayName) */
#define VA_TRYREFRESH  0x812AB8     /* void CampaignSaveLoader.TryRefreshSave() */
#define VA_KEEPONLY    0x814E88     /* bool CampaignSaveLoader.KeepOnlyRetainedCampaignSave(Guid campaignGuid, string retainedSaveName) */
static bool (*o_save)(Obj, Obj, bool, Obj, void *);
static bool (*o_keeponly)(Obj, uint64_t, uint64_t, Obj, void *);
static int guest_world;             /* this copy runs a world it received from a host (until back on the title) */
static int sync_file_pending;       /* delete Saves/mp_sync.json once the load that reads it is done */
static int mp_saving;               /* host: inside the join-point save */
static uint64_t saves_blocked, sync_files_deleted, cache_refreshes;

static void sync_file_delete(const char *why) {
    char p[1400]; int n = 0;
    snprintf(p, sizeof p, "%s/data/Saves/%s.json", rig_dir, MP_SAVE_NAME); n += unlink(p) == 0;
    snprintf(p, sizeof p, "%s/data/Saves/%s.json.tmp", rig_dir, MP_SAVE_NAME); n += unlink(p) == 0;
    if (n) { sync_files_deleted++; rig_log("mp: %s.json deleted (%s)", MP_SAVE_NAME, why); }
    /* the save list's header cache (persistentDataPath/SaveHeaderCache.json + the loader's in-memory set) still names
       the file: CampaignSaveLoader.TryRefreshSave -> SaveLoadHelper.RefreshSaveCache drops missing files and rewrites it */
    Obj ld = mf ? AT(mf, 0x180, Obj) : NULL;
    if (n && ld) { F(VA_TRYREFRESH, void (*)(Obj, void *))(ld, NULL); cache_refreshes++; }
}
/* Owners persist with the world (lead 2026-10-13): each host save of the campaign writes dsnet's owner tables to
   <data>/mp_owners/<campaign guid>.bin (outside Saves/, so the save list never shows it); the next host session of
   that campaign imports them (DSNET_OWNERS_LOAD commands, applied on every peer). Keyed by ClanDataContainer.CampaignGuid
   (CampaignDataContainer +0x50 -> +0x10), the same id the game's autosaves use. */
/* Keyed by campaign AND world tick (bug 2026-10-13, pair CD): keyed by campaign only, a host loading an OLDER save of
   the campaign (or a fresh copy of a test save) got the owner table of a later save, so the units were already owned
   and the guest's claims failed. Now a table is only restored into the exact world state it was saved with. */
static int owners_path(char *p, size_t n, int64_t t) {
    Obj c = cdc(); Obj clan = c ? AT(c, 0x50, Obj) : NULL;
    if (!clan || !rig_dir[0] || t < 0) return 0;
    char gs[40]; guid_str(AT(clan, 0x10, Guid), gs);
    snprintf(p, n, "%s/data/mp_owners/%s_%lld.bin", rig_dir, gs, (long long)t);
    return 1;
}
static void owners_store(void) {
    if (!S || !cfg.host) return;
    char p[1400]; if (!owners_path(p, sizeof p, tick())) return;   /* the world tick this save holds */
    dsnet_client *c = mp_client(S);
    size_t n = dsnet_export_owners(c, NULL, 0); if (!n) return;
    uint8_t *b = malloc(n); size_t m = dsnet_export_owners(c, b, n);
    char d[1400]; snprintf(d, sizeof d, "%s/data/mp_owners", rig_dir); mkdir(d, 0755);
    char tmp[1500]; snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "wb");
    if (f && m == n) { fwrite(b, 1, n, f); fclose(f); rename(tmp, p); rig_log("mp: owners saved with the world (%zu bytes)", n); }
    else if (f) fclose(f);
    free(b);
}
static int owners_restored;
static void owners_restore(void) {
    if (owners_restored || !S || !cfg.host) return;
    owners_restored = 1;
    /* The loaded world's tick: the menu loads the save without det, so the campaign runs freely for a moment (about
       60-70 ticks seen) before the session locks it. Take the table saved at the highest tick <= now within 1200 ticks;
       a table from a LATER save of the campaign (the 2026-10-13 bug) has a higher tick and is never taken. */
    int64_t now = tick();
    char p[1400]; if (!owners_path(p, sizeof p, 0)) return;
    char *us = strrchr(p, '_'); if (!us) return;
    size_t pre = (size_t)(us - p) + 1;                       /* ".../mp_owners/<guid>_" */
    char dir[1400]; snprintf(dir, sizeof dir, "%s/data/mp_owners", rig_dir);
    const char *base = p + strlen(dir) + 1; size_t bl = pre - strlen(dir) - 1;
    int64_t best = -1;
    DIR *d = opendir(dir);
    for (struct dirent *e; d && (e = readdir(d)); ) {
        if (strncmp(e->d_name, base, bl)) continue;
        char *end; long long t = strtoll(e->d_name + bl, &end, 10);
        if (end == e->d_name + bl || strcmp(end, ".bin")) continue;
        if (t <= now && now - t <= 1200 && t > best) best = t;
    }
    if (d) closedir(d);
    if (best < 0) { rig_log("mp: no saved owners for this world (now tick %lld)", (long long)now); return; }
    owners_path(p, sizeof p, best);
    FILE *f = fopen(p, "rb"); if (!f) return;
    rig_log("mp: owners saved at tick %lld match this world (now %lld)", (long long)best, (long long)now);
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n); size_t got = fread(b, 1, (size_t)n, f); fclose(f);
    int rc = got == (size_t)n ? dsnet_import_owners(mp_client(S), b, (size_t)n) : -1;
    rig_log("mp: owners restored from the world (%ld bytes): rc %d", n, rc);
    free(b);
}
static bool h_save(Obj ld, Obj name, bool is_auto, Obj disp, void *mi) {
    if (!mp_saving && (guest_world || (S && !cfg.host))) {
        saves_blocked++;
        rig_log("mp: guest save blocked (%s): the world's save lives on the host", is_auto ? "auto" : "manual");
        return false;
    }
    bool ok = o_save(ld, name, is_auto, disp, mi);
    if (ok && !mp_saving) owners_store();
    return ok;
}
/* In iron mode Save(isAutoSave = true) keeps ONLY the save just written and deletes the campaign's others: the
   join-point save must never delete the host's real saves (and mp_sync is deleted after the reload). */
static bool h_keeponly(Obj ld, uint64_t glo, uint64_t ghi, Obj name, void *mi) {
    if (mp_saving) { rig_log("mp: join-point save: iron-mode KeepOnlyRetainedCampaignSave skipped (host saves kept)"); return true; }
    return o_keeponly(ld, glo, ghi, name, mi);
}

/* Bug 2026-10-13 (pair IJ, return to base -> desync -> host reload hung on the title): the game deleted mp_sync.json
   between the reload being queued and the load reading it ("Save became unavailable before campaign transition",
   ErrorLog 03:02:06; our own delete never ran). In iron campaigns the map-change autosave cleans the campaign's other
   saves (KeepOnlyRetainedCampaignSave / CleanupAutoSaves), and mp_sync.json is one of them. Every game delete goes
   through CampaignSaveLoader.Delete(string name): mp_sync is never deleted by the game while a session or an mp load is
   pending (mp deletes it itself once the load is done). */
#define VA_SAVE_DELETE 0x815748     /* void CampaignSaveLoader.Delete(string campaignName) */
static void (*o_delete)(Obj, Obj, void *);
static uint64_t game_deletes_blocked;
static char test_race[16];          /* test: act right after the next host reload is queued (mp_test_race) */
static int is_sync_name(Obj name) {
    if (!name) return 0;
    int32_t n = AT(name, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)name + 0x14);
    static const char w[] = MP_SAVE_NAME;
    if (n != (int32_t)(sizeof w - 1)) return 0;
    for (int i = 0; i < n; i++) { uint16_t ch = c[i]; if (ch >= 'A' && ch <= 'Z') ch += 32; if (ch != (uint16_t)w[i]) return 0; }
    return 1;
}
static void h_delete(Obj ld, Obj name, void *mi) {
    if ((S || sync_file_pending || host_reload) && is_sync_name(name)) {
        game_deletes_blocked++;
        rig_log("mp: the game wanted to delete %s.json during a session / mp load: kept", MP_SAVE_NAME);
        return;
    }
    o_delete(ld, name, mi);
}
/* Second guard: the last join-point save stays in memory until its load is done; if the load still lands on the
   title without a campaign (file gone, preflight failed), it is written again and re-queued (2 tries). */
static uint8_t *retry_json, *retry_det; static size_t retry_n, retry_dl;
static double pending_since; static int load_retries;
static void retry_keep(const uint8_t *det, size_t dl, const uint8_t *json, size_t n) {
    free(retry_json); free(retry_det);
    retry_json = malloc(n); memcpy(retry_json, json, n); retry_n = n;
    retry_det = malloc(dl); memcpy(retry_det, det, dl); retry_dl = dl;
    load_retries = 0; pending_since = 0;
}

/* ---------------------------------------------------------------- mp_game_api */
/* Host reload at the join point (MP_NOTES risk, confirmed 2026-10-12): the save drops in-progress tasks and
   snaps moving units to cells, so a loaded peer differs from the host's memory. The host therefore reloads
   its own save right after the upload; every peer then starts from the same loaded state. */
static uint8_t *host_det;
static int g_ready(void *u) { (void)u; return !host_reload && rig_det_mp_ready(); }
static void g_tick(void *u) { (void)u; rig_det_sim_tick(); }
static void g_scope(void *u, int enter) { (void)u; rig_det_sim_scope(enter); }
static uint64_t g_fp(void *u) { (void)u; uint64_t h[4]; rig_world_hash(rig_det_sim_state(), h, NULL, NULL); return h[0]; }
static int g_move(void *u, const uint8_t guid[16], int32_t x, int32_t y) {
    (void)u; Guid g; memcpy(&g, guid, 16);
    return rig_order_move(g, x, y);
}
static void g_show(void *u, int speed, int paused) {
    (void)u;
    Obj gs = speed_setting(); if (!gs) gs = gs_seen;
    if (!gs || !o_setspeed) return;
    showing = 1;
    if (paused) o_setspeed(gs, 0, PS_ABSOLUTE, NULL);
    else {
        /* The session runs: no local pause reason may hold the local speed at 0 (it drives animations). A popup or
           the Esc / lobby menu opened before the lock added its reason locally; its later removal became a vote. */
        if (o_rempause) {
            static const int reasons[] = { MP_PAUSE_USER, MP_PAUSE_ESSENTIAL_POPUP, MP_PAUSE_POPUP_TIMELOCK, MP_PAUSE_CONSECRATION };
            for (int i = 0; i < 4; i++) o_rempause(gs, reasons[i], NULL);
        }
        o_setspeed(gs, speed, PS_ROLLBACK, NULL);
    }
    showing = 0;
    /* Tell the view, as SystemEventHandler does after its own SetSpeed: ViewIntData(0x431, effective speed) to the
       ViewUpdateDispatcher (mf+0x78). EntityObjectPresenter.OnGameSpeedChanged then sets every unit Animator's speed
       (UnitGraphic.ChangeGameSpeed), FX and paths follow. Without it the host's animators kept the speed of the last
       stock change (0 from the pause before the lock): units slid without walking, dead monsters never fell. */
    Obj vd = mf ? AT(mf, 0x78, Obj) : NULL;
    if (vd) {
        Obj d = obj_new(0x6656D48);                                                       /* ViewIntData */
        F(0xE33AA8, void (*)(Obj, int, int, void *))(d, 0x431, AT(gs, 0x10, int32_t), NULL);
        F(0xE5AC14, void (*)(Obj, Obj, void *))(vd, d, NULL);                             /* ViewUpdateDispatcher.AddViewUpdateCall */
    }
}

/* save = u32 det_len, det state, game save JSON */
static int g_save(void *u, uint8_t **data, size_t *len) {
    (void)u;
    mp_saving = 1;
    int saved = rig_save_campaign(MP_SAVE_NAME);
    mp_saving = 0;
    if (!saved) { rig_log("mp: CampaignSaveLoader.Save failed"); return -1; }
    char p[1400]; snprintf(p, sizeof p, "%s/data/Saves/%s.json", rig_dir, MP_SAVE_NAME);
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint32_t dl = (uint32_t)rig_det_state_size();
    uint8_t *b = malloc(4 + dl + (size_t)n);
    memcpy(b, &dl, 4);
    rig_det_state_get(b + 4);
    size_t got = fread(b + 4 + dl, 1, (size_t)n, f); fclose(f);
    if (got != (size_t)n) { free(b); return -1; }
    *data = b; *len = 4 + dl + (size_t)n;
    if (cfg.host) { free(host_det); host_det = malloc(dl); memcpy(host_det, b + 4, dl); host_reload = 1; }
    if (cfg.host) retry_keep(b + 4, dl, b + 4 + dl, (size_t)n);
    return 0;
}
static int g_load(void *u, const uint8_t *data, size_t len) {
    (void)u;
    uint32_t dl; if (len < 4) return -1;
    memcpy(&dl, data, 4);
    if (dl != rig_det_state_size() || 4 + (size_t)dl > len) { rig_log("mp: det state size %u, want %zu", dl, rig_det_state_size()); return -1; }
    char dir[1400], p[1500], tmp[1600];
    snprintf(dir, sizeof dir, "%s/data/Saves", rig_dir); mkdir(dir, 0755);
    snprintf(p, sizeof p, "%s/%s.json", dir, MP_SAVE_NAME);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "wb"); if (!f) return -1;
    fwrite(data + 4 + dl, 1, len - 4 - dl, f); fclose(f);
    rename(tmp, p);
    rig_det_state_set(data + 4);                 /* applied at the auto-lock after the load */
    retry_keep(data + 4, dl, data + 4 + dl, len - 4 - dl);
    if (!cfg.host) guest_world = 1;
    int ok = rig_load_save_file(p);
    if (ok) sync_file_pending = 1; else sync_file_delete("load failed");
    return ok ? 0 : -1;
}

static const mp_game_api api = { NULL, g_ready, g_tick, g_scope, g_fp, g_move, g_save, g_load, g_show };

/* ---------------------------------------------------------------- entry points */
static void mp_rig_log(const char *line) { rig_log("mp: %s", line); }

/* Quitting the app (stock Quit, Cmd-Q, window close) closes the session first: the socket is closed cleanly and the
   server issues PLAYER_LEAVE at once, instead of the others waiting until the process is gone. */
#define VA_APP_QUIT    0x507B1A0    /* static void UnityEngine.Application.Quit() */
#define VA_APP_QUIT_I  0x507B150    /* static void UnityEngine.Application.Quit(int exitCode) */
static void (*o_app_quit)(void *);
static void (*o_app_quit_i)(int, void *);
static void leave_for_quit(const char *why) {
    if (!S && !steam_join && !steam_host) return;
    rig_log("mp: %s: leaving the session first", why);
    mp_steam_leave();
}
static void h_app_quit(void *mi) { leave_for_quit("Application.Quit"); o_app_quit(mi); }
static void h_app_quit_i(int code, void *mi) { leave_for_quit("Application.Quit(code)"); o_app_quit_i(code, mi); }
static void mp_atexit(void) { if (S) { mp_stop(S); S = NULL; } }

/* The stock "Quit to title" (system menu confirm -> InputEventFactory) and ReturnTitleAction both build a
   QuitToTitleSceneRequested; the load path does not. Building one while in a session = leave NOW (the title takes ~1 s
   to come up; the others must not wait for it). Our own host-left quit runs with no session, so it passes. */
static void (*o_quit_ctor)(Obj, bool, void *);
static void h_quit_ctor(Obj ev, bool autosave, void *mi) {
    o_quit_ctor(ev, autosave, mi);
    if (S || steam_join || steam_host) { rig_log("mp: quit to title requested: leaving the session now"); mp_steam_leave(); }
}

static void install_hooks(void) {
    if (hooks_done) return;
    hooks_done = 1;
    int bad = 0;
    bad |= rig_hook(VA_GS_SETSPEED, (void *)h_setspeed, (void **)&o_setspeed, "GameSpeedSetting.SetSpeed");
    bad |= rig_hook(VA_GS_ADDPAUSE, (void *)h_addpause, (void **)&o_addpause, "GameSpeedSetting.AddPauseReason");
    bad |= rig_hook(VA_GS_REMPAUSE, (void *)h_rempause, (void **)&o_rempause, "GameSpeedSetting.RemovePauseReason");
    if (bad) rig_log("mp: SPEED HOOKS FAILED");
    if (rig_hook(VA_APP_QUIT, (void *)h_app_quit, (void **)&o_app_quit, "Application.Quit") ||
        rig_hook(VA_APP_QUIT_I, (void *)h_app_quit_i, (void **)&o_app_quit_i, "Application.Quit(int)"))
        rig_log("mp: quit hooks failed: a quitting copy leaves only when its process ends");
    if (rig_hook(VA_SAVE_DELETE, (void *)h_delete, (void **)&o_delete, "CampaignSaveLoader.Delete"))
        rig_log("mp: Delete hook failed: an iron-mode autosave can delete a pending mp_sync.json");
    if (rig_hook(VA_QUIT_TITLE_CTOR, (void *)h_quit_ctor, (void **)&o_quit_ctor, "QuitToTitleSceneRequested..ctor"))
        rig_log("mp: quit-to-title hook failed: a guest leaves only when the title shows");
    atexit(mp_atexit);
    if (rig_hook(VA_SAVE, (void *)h_save, (void **)&o_save, "CampaignSaveLoader.Save") ||
        rig_hook(VA_KEEPONLY, (void *)h_keeponly, (void **)&o_keeponly, "CampaignSaveLoader.KeepOnlyRetainedCampaignSave"))
        rig_log("mp: SAVE HOOKS FAILED: a guest could write the mp world, an iron-mode host could lose its saves");
    if (rig_hook(VA_STEAM_GETNEXT, (void *)h_getnext, (void **)&o_getnext, "Steamworks ManualDispatch_GetNextCallback"))
        rig_log("mp: Steam invite hook failed: accepted invites only work through +connect_lobby");
}

/* Hooks always go in: with no session they pass through. mp.json (optional) starts a session at the
   first locked frame; the title-screen buttons start one with mp_game_host / mp_game_join. */
/* ui_player_color: slot -> the world's per-uid colour (owner 2026-10-14: one colour per player, rejoin keeps it) */
extern int (*ui_color_resolver)(int player);
static int color_of_slot(int player) { return S ? dsnet_player_color(mp_client(S), player) : -1; }
void mp_game_install(void) {
    ui_color_resolver = color_of_slot;
    mp_set_log(mp_rig_log);
    install_hooks();
    sync_file_delete("left over from an earlier run");
    char p[1200]; snprintf(p, sizeof p, "%s/mp.json", rig_dir);
    int rc = mp_config_load(p, &cfg);
    if (rc == -1) return;                                    /* no mp.json: lobby buttons only */
    if (rc) { rig_log("mp: %s is not valid (need role host|client, client needs host + port)", p); return; }
    cfg_ok = 1;
    rig_log("mp: mp.json %s %s:%u name %s allow_speedup %d allow_pause %d", cfg.host ? "host" : "client", cfg.host ? "*" : cfg.host_addr,
            cfg.port, cfg.name, cfg.allow_speedup, cfg.allow_pause);
}

/* ---------------------------------------------------------------- lobby (title screen) */
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
/* The game initialises Steam itself (Steamworks.NET, MP app = no Steam gate); the rig test copies never do. */
static int steam_ready(void) {
    if (steam_ok == 1) return 1;
    if (now_s() < steam_next_try) return 0;
    steam_next_try = now_s() + 2;
    int ok = dsnet_steam_load(NULL) == 0 && dsnet_steam_initialized() && dsnet_steam_logged_on();
    if (ok || steam_ok < 0) rig_log("mp: Steam %s", ok ? "available" : dsnet_steam_load(NULL) ? dsnet_steam_error() : "not initialised / not logged on");
    steam_ok = ok;
    return ok;
}

static void end_session(const char *why) {
    rig_log("mp: session ended: %s", why);
    snprintf(ended_msg, sizeof ended_msg, "%s", why); ended = 1;
    if (S) { ord_mp_attach(NULL); mp_stop(S); S = NULL; }
    if (steam_join) { dsnet_steam_join_close(steam_join); steam_join = NULL; }
    if (steam_host) { dsnet_steam_host_stop(steam_host); steam_host = NULL; }
    cfg_ok = 0; start_failed = 0; host_reload = 0; join_lobby_id = 0; played = 0; owners_restored = 0;
}

static const char *reject_text(int why) {                  /* one wording for every refusal: dsnet_reject_text (ux #22) */
    static char t[200];
    const char *w = dsnet_reject_text(why);
    if (!*w) return "Could not connect to the host";
    snprintf(t, sizeof t, "Could not join: %s", w);
    return t;
}

/* Campaign -> title. First choice: the game's own QuitToTitleSceneRequested(autoSaveBeforeQuit = false) on the global
   EventQueue (what ReturnTitleAction sends; async handler). It needs no open menu, unlike SystemMenuView's quit, which
   failed when the system menu had never been built ("no SystemMenuView / input stream"). Only from a campaign scene. */
static int stock_quit_title(void);
static void quit_to_title(void) {
    if (scene() != 2) return;
    Obj eq = mf ? AT(mf, 0x48, Obj) : NULL;
    if (eq) {
        Obj ev = obj_new(TI_QUIT_TITLE_REQ);
        F(VA_QUIT_TITLE_CTOR, void (*)(Obj, bool, void *))(ev, false, NULL);
        F(0xCCFAD4, void (*)(Obj, Obj, void *))(eq, ev, NULL);                 /* EventQueue.AddEvent */
        rig_log("mp: quit to title (QuitToTitleSceneRequested, no autosave)");
        return;
    }
    stock_quit_title();
}
/* the system menu's own confirmed "Quit to title" (UIInput 0x5C -> QuitToTitleSceneRequested); 0 ok, -1 no menu view */
static int stock_quit_title(void) {
    void *k = ord_debug_class("Refactor.UI", "SystemMenuView");
    if (!k) { rig_log("mp: SystemMenuView class not found"); return -1; }
    void *type = F(0x6F8C9C, void *(*)(void *))(k);                       /* il2cpp_class_get_type */
    Obj tobj = F(0x6F97AC, Obj (*)(void *))(type);                         /* il2cpp_type_get_object */
    Obj v = F(0x50FD97C, Obj (*)(Obj, bool, void *))(tobj, true, NULL);     /* Object.FindObjectOfType(type, includeInactive) */
    if (!v || !AT(v, 0x28, Obj)) { rig_log("mp: no SystemMenuView / input stream: cannot quit to title"); return -1; }
    rig_log("mp: quit to title (system menu)");
    F(VA_QUIT_TO_TITLE, void (*)(Obj, void *))(v, NULL);
    return 0;
}

/* Player identity across reconnects (owner rule: a leaver's units stay reserved for the same uid):
   with Steam, uid = "DSMPSTM\0" + SteamID64 (LE); without Steam (LAN / direct TCP), 16 random bytes made once and kept
   in <data>/mp_uid (one per install, so a LAN guest who rejoins gets his units back); dsnet's random id only if that
   file cannot be written. */
static int file_uid(uint8_t u[16]) {
    if (!rig_dir[0]) return 0;
    char p[1300]; snprintf(p, sizeof p, "%s/data/mp_uid", rig_dir);
    FILE *f = fopen(p, "rb");
    if (f) { size_t n = fread(u, 1, 16, f); fclose(f); if (n == 16) return 1; }
    arc4random_buf(u, 16);
    char d[1300]; snprintf(d, sizeof d, "%s/data", rig_dir); mkdir(d, 0755);
    f = fopen(p, "wb"); if (!f) return 0;
    size_t n = fwrite(u, 1, 16, f); fclose(f);
    rig_log("mp: new player id saved in %s", p);
    return n == 16;
}
static void set_uid(void) {
    uint64_t id = steam_ready() ? dsnet_steam_my_id() : 0;
    if (!id) { uint8_t u[16]; dsnet_set_player_uid(file_uid(u) ? u : NULL); return; }
    uint8_t u[16] = { 'D', 'S', 'M', 'P', 'S', 'T', 'M', 0 };
    for (int i = 0; i < 8; i++) u[8 + i] = (uint8_t)(id >> (8 * i));
    dsnet_set_player_uid(u);
}

static int last_reject;              /* enum dsnet_reject of the last failed start, 0 if none */
static int start_now(const mp_config *c) {                   /* game thread (dsnet reads fegetround) */
    int why = 0;
    set_uid();
    S = mp_start(c, &api, &why);
    last_reject = S ? 0 : why;
    if (S) { S_gen++; ord_mp_attach(S); trn_set_role(c->host); rig_det_arm(); }
    if (!S) { rig_log("mp: session did not start (reject reason %d)", why); return why ? -why : -1; }
    return 0;
}

mp_session *mp_game_session(void) { return S; }

/* mpmenu (dlsym): why the last start failed (enum dsnet_reject, e.g. DSNET_REJ_DUP_ID), 0 if none */
__attribute__((visibility("default"))) int mp_game_last_reject(void) { return last_reject; }
/* mpmenu (dlsym): 1 while an mp load is in progress (join, resync, host join-point reload, or its stuck-load retry):
   the game may sit on the title then, which is not a teardown */
__attribute__((visibility("default"))) int mp_game_load_pending(void) {
    if (sync_file_pending || host_reload) return 1;
    if (!S) return 0;
    mp_stats st; mp_get_stats(S, &st);
    return st.loading || st.needs_state;
}

/* host popup "<name> disconnected" (mpui): players waited for, and the choice */
int mp_game_left_players(int *ids, char (*names)[32], int max) { return S && cfg.host ? mp_left_players(S, ids, names, max) : 0; }
void mp_game_left_choice(int keep_playing) { if (S && cfg.host) mp_left_choice(S, keep_playing); }
int mp_game_left_waiting(void) { return S && cfg.host ? mp_left_waiting(S) : 0; }

/* ord_mp.c (dlsym): 1 if the local player may equip `unit` under the host's gear mode. No session = 1. */
__attribute__((visibility("default"))) int mp_gear_check(const uint8_t unit[16]) {
    if (!S) return 1;
    dsnet_client *c = mp_client(S);
    return dsnet_may_equip(c, dsnet_player_id(c), unit) ? 1 : 0;
}

int mp_game_host(const mp_config *c) {
    if (S || steam_join) return -1;
    cfg = *c; cfg.host = 1; cfg_ok = 1; start_failed = 0;
    guest_world = 0;
    if (start_now(&cfg)) { start_failed = 1; return -2; }
    ended = 0;
    if (steam_ready()) {
        steam_host = dsnet_steam_host_start(mp_server_port(S), MP_BUILD_ID, MP_MOD_VERSION, 8);
        rig_log("mp: Steam lobby %s", steam_host ? "being created" : "failed");
    }
    return 0;
}

/* host_addr "steam:<lobby id>" joins through Steam (the bridge gives a local port); anything else is TCP. */
int mp_game_join(const mp_config *c) {
    if (S || steam_join) return -1;
    cfg = *c; cfg.host = 0; cfg_ok = 1; start_failed = 0;
    cfg.need_save = 1;                                       /* a lobby joiner is on the title screen: no campaign */
    if (!strncmp(cfg.host_addr, "steam:", 6)) {
        if (!steam_ready()) return -3;
        uint64_t lobby = strtoull(cfg.host_addr + 6, NULL, 10);
        steam_join = lobby ? dsnet_steam_join_lobby(lobby, MP_BUILD_ID, MP_MOD_VERSION) : NULL;
        if (steam_join) { join_lobby_id = lobby; ended = 0; }
        return steam_join ? 0 : -3;                          /* connects in mp_game_poll once the bridge is up */
    }
    if (start_now(&cfg)) { start_failed = 1; return -2; }
    return 0;
}

int mp_game_steam_invite(void) { return steam_host ? dsnet_steam_host_invite(steam_host) : -1; }

/* Every frame, title screen too (rig_frame): Steam bridge for a joiner, accepted Steam invites. */
void mp_game_poll(void) {
    /* Stock "Quit to title" (or any way back to the title) while in a session = leave it at once, so the others
       get PLAYER_LEAVE now and do not wait for this copy. Loads of an mp save also pass the title (a running campaign
       is closed first), so not while one is in progress. */
    if (S && rig_det_mp_ready() && !host_reload) played = 1;
    if (S && played && scene() == 1 && !host_reload && !sync_file_pending) {
        mp_stats st; mp_get_stats(S, &st);
        if (!st.loading && !st.needs_state) { rig_log("mp: back on the title: leaving the session"); mp_steam_leave(); }
    }
    if (sync_file_pending && rig_det_mp_ready() && !host_reload) {
        sync_file_pending = 0; sync_file_delete("load done");
        free(retry_json); free(retry_det); retry_json = retry_det = NULL; pending_since = 0;
    }
    /* load stuck? On the title (the campaign was closed, then the preflight failed) or still in the old campaign
       (preflight failed before closing it: det holds, never locks). Not while the game is busy loading. */
    if (sync_file_pending && retry_json && (scene() == 1 || scene() == 2) && !busy() && !rig_det_mp_ready()) {
        double now = mono_ms();
        if (!pending_since) pending_since = now;
        char p[1400]; snprintf(p, sizeof p, "%s/data/Saves/%s.json", rig_dir, MP_SAVE_NAME);
        struct stat sb; int gone = stat(p, &sb) != 0;
        if ((gone ? now - pending_since > 3000 : now - pending_since > 20000) && load_retries < 2) {
            load_retries++; pending_since = now;
            FILE *f = fopen(p, "wb");
            if (f) { fwrite(retry_json, 1, retry_n, f); fclose(f); }
            rig_det_state_set(retry_det);
            int ok = rig_load_save_file(p);
            rig_log("mp: mp load stuck (scene %d, %s): written again and re-queued, try %d: %s",
                    scene(), gone ? "file was gone" : "20 s", load_retries, ok ? "queued" : "FAILED");
        }
    } else pending_since = 0;
    if (guest_world && !S && scene() == 1) guest_world = 0;    /* back on the title with no session: own saves again */
    if (S && !rig_det_locked()) mp_frame(S, 0);              /* not locked yet (title / loading): only runs a pending
                                                                save load; steps wait for ready() */
    if (steam_join && !S) {
        uint16_t port = 0;
        int r = dsnet_steam_join_poll(steam_join, &port);
        if (r == 1) {
            mp_config c = cfg; snprintf(c.host_addr, sizeof c.host_addr, "127.0.0.1"); c.port = port;
            int why = 0;
            set_uid();
            S = mp_start(&c, &api, &why);                    /* waits for the WELCOME (connect_timeout_ms) */
            last_reject = S ? 0 : why;
            if (S) { S_gen++; ord_mp_attach(S); trn_set_role(0); rig_det_arm(); }
            else end_session(reject_text(why));
        } else if (r < 0) {
            char m[160]; snprintf(m, sizeof m, "Could not join: %s", dsnet_steam_join_error(steam_join));
            end_session(m);
        }
    } else if (S && !cfg.host && ((steam_join && dsnet_steam_join_poll(steam_join, NULL) < 0) ||
                                   dsnet_is_disconnected(mp_client(S)))) {   /* host gone (Steam link or TCP) */
        /* ux_flow_v10 #2: only a BYE from the server means the host ended it; else our own link dropped and the
           host is waiting for us (auto pause + "Wait / Keep playing" on his side) */
        end_session(dsnet_host_left(mp_client(S)) ? "The host left the game" : "Connection to the host lost");
        quit_pending = 1;
    }
    /* host gone: back to the title as soon as the game is not mid-load (a load in progress is finished first) */
    if (quit_pending) {
        int sc = scene();
        if (sc == 1) { quit_pending = 0; rig_log("mp: on the title after the host left"); }
        else if (sc == 2 && !busy()) {
            static double last_try;
            double now = mono_ms();
            if (now - last_try > 2000) { last_try = now; quit_to_title(); }   /* retried until the title shows */
        }
    }
    if (!S && !steam_join && steam_ready()) {
        uint64_t lobby = dsnet_steam_take_join_request();
        if (lobby) {
            mp_config c; mp_config_defaults(&c);
            if (cfg_ok) c = cfg;
            const char *n = dsnet_steam_my_name(); if (*n) snprintf(c.name, sizeof c.name, "%s", n);
            snprintf(c.host_addr, sizeof c.host_addr, "steam:%llu", (unsigned long long)lobby);
            rig_log("mp: Steam invite accepted, lobby %llu: %d", (unsigned long long)lobby, mp_game_join(&c));
        }
    }
}

/* ---------------------------------------------------------------- Steam menu API (mp_steam.h) */
int mp_steam_host(const char *save, const mp_config *lobby_cfg) {
    if (S || steam_join) return -1;
    if (!steam_ready()) return -3;
    if (save && *save) {
        char p[1400];
        if (strchr(save, '/')) snprintf(p, sizeof p, "%s", save);
        else snprintf(p, sizeof p, "%s/data/Saves/%s%s", rig_dir, save, strstr(save, ".json") ? "" : ".json");
        if (!rig_load_save_file(p)) { rig_log("mp: host save %s did not load", p); return -4; }
    }
    mp_config c; mp_config_defaults(&c);
    if (lobby_cfg) c = *lobby_cfg;
    const char *n = dsnet_steam_my_name(); if (*n && (!lobby_cfg || !lobby_cfg->name[0])) snprintf(c.name, sizeof c.name, "%s", n);
    /* Steam guests come through the bridge (loopback); the same server also takes direct TCP on cfg port
       (owner: "Direct: <ip>:<port>" backup), bound to 0.0.0.0. Port taken -> any free port (mp_server_port). */
    int r = mp_game_host(&c);
    if (r == -2 && c.port) { rig_log("mp: port %u busy, using any free port", c.port); c.port = 0; r = mp_game_host(&c); }
    if (r) return r;
    if (!steam_host) { end_session("Steam lobby could not be created"); return -3; }
    return 0;
}
int mp_steam_invite(void) { return mp_game_steam_invite(); }
int mp_steam_invite_friend(uint64_t id) { return steam_host ? dsnet_steam_host_invite_friend(steam_host, id) : -1; }
int mp_steam_friends(dsnet_steam_friend *out, int max) { return steam_ready() ? dsnet_steam_friends(out, max) : 0; }
int mp_steam_join(uint64_t lobby) {
    mp_config c; mp_config_defaults(&c);
    if (cfg_ok) c = cfg;
    if (!steam_ready()) return -3;
    const char *n = dsnet_steam_my_name(); if (*n) snprintf(c.name, sizeof c.name, "%s", n);
    snprintf(c.host_addr, sizeof c.host_addr, "steam:%llu", (unsigned long long)lobby);
    return mp_game_join(&c);
}
void mp_steam_leave(void) {
    int was = S || steam_join || steam_host;
    end_session("You left the game");
    if (was) ended = 0;                                      /* own choice: no message */
}
void mp_steam_ack(void) { ended = 0; ended_msg[0] = 0; }
/* mpmenu (lan_v8 owner feedback): a guest's "Leave multiplayer" leaves the session and goes to the title at once (the
   world on screen is the host's); the same no-autosave quit as when the host leaves. */
__attribute__((visibility("default"))) void mp_game_leave_to_title(void) {
    int guest = S && !cfg.host;
    mp_steam_leave();
    if (guest) { quit_pending = 1; rig_log("mp: guest left: back to the title"); }
}

void mp_steam_state(mp_steam_info *o) {
    memset(o, 0, sizeof *o);
    o->steam = steam_ok == 1; o->player = -1;
    o->is_host = (S && cfg.host) || steam_host;
    o->lobby = steam_host ? dsnet_steam_host_lobby(steam_host) : join_lobby_id;
    if (o->lobby == ~0ull) o->lobby = 0;
    if (ended && !S && !steam_join) { o->state = MP_STEAM_ENDED; o->phase = MP_PH_FAILED; snprintf(o->msg, sizeof o->msg, "%s", ended_msg); return; }
    if (!S) { o->state = steam_join ? MP_STEAM_JOINING : MP_STEAM_IDLE; o->phase = steam_join ? MP_PH_CONNECTING : MP_PH_IDLE; return; }
    dsnet_client *dc = mp_client(S);
    o->player = dsnet_player_id(dc);
    uint32_t pm = dsnet_present_mask(dc);
    for (int i = 0; i < 32 && o->nplayers < 8; i++) if (pm >> i & 1) {
        const char *n = dsnet_player_name(dc, i);
        o->players[o->nplayers].id = i;
        snprintf(o->players[o->nplayers].name, sizeof o->players[o->nplayers].name, "%s", n ? n : "");
        o->nplayers++;
    }
    const char *hn = dsnet_player_name(dc, 0);
    snprintf(o->host_name, sizeof o->host_name, "%s", hn ? hn : "");
    if (cfg.host) {
        o->state = o->nplayers > 1 ? MP_STEAM_IN_GAME : MP_STEAM_HOSTING;
        o->phase = steam_host && !o->lobby ? MP_PH_CREATING_LOBBY : o->nplayers > 1 ? MP_PH_IN_GAME : MP_PH_LOBBY_READY;
    } else {
        int ready = o->player >= 0 && !dsnet_needs_state(dc) && rig_det_mp_ready();
        o->state = ready ? MP_STEAM_IN_GAME : MP_STEAM_JOINING;
        o->phase = ready ? MP_PH_IN_GAME : o->player < 0 ? MP_PH_CONNECTING : dsnet_needs_state(dc) ? MP_PH_WAITING_SAVE : MP_PH_LOADING;
    }
}

int mp_game_active(void) { return cfg_ok || S; }

int mp_game_frame(void) {
    if (!cfg_ok) return 0;
    if (!S && !start_failed && !steam_join && start_now(&cfg)) start_failed = 1;   /* mp.json session */
    if (!S) return 0;
    if (cfg.host && !owners_restored && rig_det_mp_ready() && !host_reload) owners_restore();   /* before the first tick */
    mp_frame(S, 0);
    trn_flow_frame();                                        /* busy edges -> dsnet, 1x after a map change */
    /* Each time the game becomes ready again (first lock, host's own join-point reload, resync), put the session's
       speed / pause back into the local GameSpeedSetting. The load flow leaves it paused, and mp only re-shows it
       when the session speed changes, so the host kept a paused local speed: animations (Animator speed follows
       the game speed) froze and death animations never played, while the sim ran. */
    static int was_ready;
    int ready_now = rig_det_mp_ready() && !host_reload;
    if (ready_now && !was_ready) {
        mp_stats st; mp_get_stats(S, &st);
        g_show(NULL, st.speed ? st.speed : 1, st.paused);
        Obj gs = speed_setting();
        rig_log("mp: ready again: local speed set to %d%s (now %d)", st.speed ? st.speed : 1, st.paused ? " paused" : "", gs ? AT(gs, 0x10, int32_t) : -1);
    }
    was_ready = ready_now;
    if (host_reload && !trn_flow_busy()) {                   /* after the frame's steps: reload what was sent (never inside a map change) */
        char p[1400]; snprintf(p, sizeof p, "%s/data/Saves/%s.json", rig_dir, MP_SAVE_NAME);
        rig_det_state_set(host_det);
        int ok = rig_load_save_file(p);
        rig_log("mp: host reloads its join-point save: %s", ok ? "queued" : "FAILED");
        if (test_race[0]) {                                  /* reproduce the 2026-10-13 hang: something deletes mp_sync now */
            char p2[1400]; snprintf(p2, sizeof p2, "%s/data/Saves/%s.json", rig_dir, MP_SAVE_NAME);
            if (!strcmp(test_race, "autosave")) {           /* the game's own autosave + cleanup (CampaignSaveHelper.SaveAutoSaveFile) */
                Obj eh = mf ? AT(mf, 0x50, Obj) : NULL; Obj ce = eh ? AT(eh, 0x20, Obj) : NULL; Obj sh = ce ? AT(ce, 0xA8, Obj) : NULL;
                Obj ld = mf ? AT(mf, 0x180, Obj) : NULL; Obj a = ld ? AT(ld, 0x18, Obj) : NULL, cs = a ? AT(a, 0x20, Obj) : NULL;
                bool iron = cs ? AT(cs, 0x24, bool) : false;
                if (cs) AT(cs, 0x24, bool) = true;           /* as during the real map-change autosave (iron flag on) */
                if (sh) F(0xFCB300, bool (*)(Obj, void *))(sh, NULL);
                if (cs) AT(cs, 0x24, bool) = iron;
            } else if (!strcmp(test_race, "game")) F(VA_SAVE_DELETE, void (*)(Obj, Obj, void *))(AT(mf, 0x180, Obj), F(0x6F9738, Obj (*)(const char *))(MP_SAVE_NAME), NULL);
            else unlink(p2);                                 /* "raw": the file is just gone */
            struct stat sb;
            rig_log("mp: test race '%s' after the reload was queued: mp_sync.json %s", test_race, stat(p2, &sb) == 0 ? "still there" : "GONE");
            test_race[0] = 0;
        }
        if (ok) sync_file_pending = 1; else sync_file_delete("reload failed");
        host_reload = 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- socket commands */
static char *merr(const char *m) { Buf b = {0}; bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, m); bput(&b, "}"); return b.p; }
static char *mok(int rc) { Buf b = {0}; bput(&b, "{\"ok\":%s,\"rc\":%d}", rc == 0 ? "true" : "false", rc); return b.p; }

void mp_game_status(Buf *b) {
    if (!cfg_ok) return;
    if (!S) { bput(b, ",\"mp\":{\"started\":false,\"failed\":%s}", start_failed ? "true" : "false"); return; }
    mp_stats st; mp_get_stats(S, &st);
    bput(b, ",\"mp\":{\"started\":true,\"host\":%s,\"player\":%d,\"timer\":%u,\"ticks\":%llu,\"speed\":\"%s\",\"paused\":%s,"
            "\"desynced\":%s,\"needs_state\":%s,\"loading\":%s,\"orders\":%u,\"orders_dropped_owner\":%u,\"orders_bad\":%u,"
            "\"loads\":%u,\"saves\":%u,\"desyncs\":%u,\"allow_pause\":%d,\"allow_speedup\":%d",
         cfg.host ? "true" : "false", st.player, st.timer, (unsigned long long)st.ticks, mp_speed_name(st.speed, 0), st.paused ? "true" : "false",
         st.desynced ? "true" : "false", st.needs_state ? "true" : "false", st.loading ? "true" : "false",
         st.orders_applied, st.orders_dropped_owner, st.orders_bad, st.loads, st.saves, st.desyncs,
         mp_allow_pause(S), mp_allow_speedup(S));
    bput(b, ",\"guest_world\":%s,\"saves_blocked\":%llu,\"sync_files_deleted\":%llu,\"sync_file_pending\":%s,\"cache_refreshes\":%llu,\"game_deletes_blocked\":%llu,\"load_retries\":%d",
         guest_world ? "true" : "false", (unsigned long long)saves_blocked, (unsigned long long)sync_files_deleted, sync_file_pending ? "true" : "false", (unsigned long long)cache_refreshes, (unsigned long long)game_deletes_blocked, load_retries);
    dsnet_client *dc = mp_client(S);
    {   /* owner tables (cross-play check: same on every peer) and why a desync was flagged (bit 1 game, 2 owners) */
        uint64_t hu = 0, hi = 0; int nu = 0, ni = 0;
        dsnet_owner_hashes(dc, &hu, &hi, &nu, &ni);
        bput(b, ",\"own_hash\":\"%016llx\",\"item_hash\":\"%016llx\",\"n_own\":%d,\"n_item\":%d,\"desync_reason\":%d",
             (unsigned long long)hu, (unsigned long long)hi, nu, ni, dsnet_desync_reason(dc));
    }
    uint32_t pm = dsnet_present_mask(dc);
    bput(b, ",\"present\":%u,\"story_pause_mask\":%u,\"busy_mask\":%u,\"votes\":[", pm, dsnet_story_pause_mask(dc), dsnet_busy_mask(dc));
    for (int i = 0, k = 0; i < 32; i++) if (pm & (1u << i)) { int sp = 0, pa = 0; dsnet_vote_of(dc, i, &sp, &pa); bput(b, "%s[%d,%d,%d]", k++ ? "," : "", i, sp, pa); }
    bput(b, "]}");
}

char *mp_game_exec(const char *cmd, const char *line) {
    if (strncmp(cmd, "mp_", 3)) return NULL;
    if (!strncmp(cmd, "mp_steam_", 9)) {
        char sv[512] = ""; double v;
        if (!strcmp(cmd, "mp_steam_host")) { jstr(line, "save", sv, sizeof sv); return mok(mp_steam_host(sv, NULL)); }
        if (!strcmp(cmd, "mp_steam_join")) { if (!jstr(line, "lobby", sv, sizeof sv)) return merr("need lobby"); return mok(mp_steam_join(strtoull(sv, NULL, 10))); }
        if (!strcmp(cmd, "mp_steam_invite")) return mok(mp_steam_invite());
        if (!strcmp(cmd, "mp_steam_invite_friend")) { if (!jstr(line, "steamid", sv, sizeof sv)) return merr("need steamid"); return mok(mp_steam_invite_friend(strtoull(sv, NULL, 10))); }
        if (!strcmp(cmd, "mp_steam_leave")) { mp_steam_leave(); return mok(0); }
        if (!strcmp(cmd, "mp_steam_ack")) { mp_steam_ack(); return mok(0); }
        if (!strcmp(cmd, "mp_steam_state")) {
            mp_steam_info st; mp_steam_state(&st); (void)v;
            static const char *nm[] = { "idle", "hosting", "joining", "in_game", "ended" };
            Buf b = {0};
            bput(&b, "{\"ok\":true,\"state\":\"%s\",\"steam\":%d,\"host\":%d,\"player\":%d,\"lobby\":\"%llu\",\"msg\":",
                 nm[st.state], st.steam, st.is_host, st.player, (unsigned long long)st.lobby);
            bstr(&b, st.msg); bput(&b, ",\"players\":[");
            for (int i = 0; i < st.nplayers; i++) { bput(&b, "%s{\"id\":%d,\"name\":", i ? "," : "", st.players[i].id); bstr(&b, st.players[i].name); bput(&b, "}"); }
            bput(&b, "]}");
            return b.p;
        }
        return merr("unknown mp_steam cmd");
    }
    if (!cfg_ok) return merr("mp off (no mp.json and no lobby session)");
    if (!strcmp(cmd, "mp_status")) { Buf b = {0}; bput(&b, "{\"ok\":true"); mp_game_status(&b); bput(&b, "}"); return b.p; }
    if (!S) return merr("mp session not started (it starts at the first locked frame)");
    char gs[64]; Guid g; double v, x, y;
    if (!strcmp(cmd, "mp_move")) {
        if (!jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g) || !jnum(line, "x", &x) || !jnum(line, "y", &y)) return merr("need guid, x, y");
        return mok(mp_order_move(S, (const uint8_t *)&g, (int32_t)x, (int32_t)y));
    }
    if (!strcmp(cmd, "mp_pause")) { if (!jnum(line, "on", &v)) return merr("need on 0|1"); return mok(mp_vote_pause(S, v != 0)); }
    if (!strcmp(cmd, "mp_speed")) { if (!jnum(line, "speed", &v)) return merr("need speed 1 (normal) or 2 (fast 3.5x)"); return mok(mp_vote_speed(S, (int)v)); }
    if (!strcmp(cmd, "mp_popup")) {
        if (!jnum(line, "reason", &v) || !jnum(line, "add", &x)) return merr("need reason, add");
        mp_popup_pause(S, (int)v, x != 0); return mok(0);
    }
    if (!strcmp(cmd, "mp_claim") || !strcmp(cmd, "mp_release")) {
        if (!jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g)) return merr("need guid");
        return mok(cmd[3] == 'c' ? mp_claim(S, (const uint8_t *)&g) : mp_release(S, (const uint8_t *)&g));
    }
    if (!strcmp(cmd, "mp_resync")) return mok(dsnet_request_resync(mp_client(S)));
    if (!strcmp(cmd, "mp_cut")) { dsnet_test_cut(mp_client(S)); return mok(0); }   /* test: our own link drops (no BYE) */
    if (!strcmp(cmd, "mp_colors")) {                      /* test: palette index + colour per slot (owner 2026-10-14) */
        extern const char *ui_player_color(int player);
        Buf b = {0}; bput(&b, "{\"ok\":true,\"me\":%d,\"slots\":[", dsnet_player_id(mp_client(S)));
        for (int i = 0; i < 8; i++) bput(&b, "%s{\"slot\":%d,\"index\":%d,\"color\":\"%s\"}", i ? "," : "", i, dsnet_player_color(mp_client(S), i), ui_player_color(i));
        bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "mp_import_owners")) {               /* test, host: dsnet_import_owners from a hex blob */
        static char hx[8192]; if (!jstr(line, "hex", hx, sizeof hx)) return merr("need hex");
        size_t n = strlen(hx) / 2; uint8_t *bb = malloc(n ? n : 1);
        for (size_t i = 0; i < n; i++) { unsigned v = 0; sscanf(hx + 2 * i, "%2x", &v); bb[i] = (uint8_t)v; }
        int rc = dsnet_import_owners(mp_client(S), bb, n); free(bb); return mok(rc);
    }
    if (!strcmp(cmd, "mp_quit")) {                        /* test: the stock quit paths */
        char m[16] = "title"; jstr(line, "mode", m, sizeof m);
        if (!strcmp(m, "app")) { F(VA_APP_QUIT, void (*)(void *))(NULL); return mok(0); }   /* through the Application.Quit hook */
        if (!strcmp(m, "stock")) return mok(stock_quit_title());   /* the system menu's confirmed Quit to title */
        quit_to_title(); return mok(0);                   /* QuitToTitleSceneRequested event */
    }
    if (!strcmp(cmd, "mp_test_race")) { jstr(line, "mode", test_race, sizeof test_race); return mok(0); }
    if (!strcmp(cmd, "mp_owner")) {                       /* test: owner of a unit (slot, -1 free, -2 RESERVED) */
        if (!jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g)) return merr("need guid");
        uint8_t u[16]; char nm[32] = ""; int o = dsnet_owner_info(mp_client(S), (const uint8_t *)&g, u, nm);
        Buf b = {0}; bput(&b, "{\"ok\":true,\"owner\":%d,\"name\":", o); bstr(&b, nm); bput(&b, "}"); return b.p;
    }
    if (!strcmp(cmd, "mp_save")) {                        /* test: a normal host save of the world (owners go with it) */
        char nm[128] = "mp_owner_test"; jstr(line, "name", nm, sizeof nm);
        return mok(rig_save_campaign(nm) ? 0 : -1);
    }
    if (!strcmp(cmd, "mp_left")) {
        int ids[8]; char nm[8][32]; int n = mp_game_left_players(ids, nm, 8);
        Buf b = {0}; bput(&b, "{\"ok\":true,\"waiting\":%d,\"players\":[", mp_game_left_waiting());
        for (int i = 0; i < n; i++) { bput(&b, "%s{\"id\":%d,\"name\":", i ? "," : "", ids[i]); bstr(&b, nm[i]); bput(&b, "}"); }
        bput(&b, "]}"); return b.p;
    }
    if (!strcmp(cmd, "mp_left_choice")) { if (!jnum(line, "keep", &v)) return merr("need keep 0|1"); mp_game_left_choice(v != 0); return mok(0); }
    return merr("unknown mp cmd");
}
