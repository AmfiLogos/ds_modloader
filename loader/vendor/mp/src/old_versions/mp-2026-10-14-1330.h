/* mp - glue between the dsnet lockstep core and the Dungeon Settlers game (build 25846389).
 * No game dependencies here: the game is reached through mp_game_api (filled by mp_game.c in the
 * dylib, by a mock in mp_test/). Notes: ~/ds_multiplayer/src/MP_NOTES.md.
 *
 * One timer step (dsnet "timer") = [commands of the step] -> [0..N world ticks] -> [fingerprint].
 * Commands run outside a world tick; GAME_ORDERs run inside the game's sim scope (sim RNG / ids).
 * Game speed rides on dsnet's SPEED_VOTE as the game's GameSpeed enum. Only two speeds are in play
 * (owner, 2026-10-12): Normal (1, 1x) and Fast (2, 3.5x). VeryFast (3, 7x) exists in code but is not a
 * player option: every vote is clamped to 1..2. Fast = 3.5 world ticks per step on average (half-tick
 * accumulator), the GAMESYSTEM_Campaign_GameSpeed multiplier. Logs/status use mp_speed_name, never "2x". */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net/dsnet.h"

#define MP_BUILD_ID     "25846389"
#define MP_MOD_VERSION  1u

/* GameSpeed / GamePauseReason values of the game (dump.cs TypeDefIndex 1398 / 1401) */
enum { MP_SPEED_PAUSE = 0, MP_SPEED_NORMAL = 1, MP_SPEED_FAST = 2, MP_SPEED_VERYFAST = 3 /* not in play: clamped to FAST */ };
const char *mp_speed_name(int game_speed, int paused);   /* "paused", "normal", "fast 3.5x" */
enum { MP_PAUSE_USER = 0, MP_PAUSE_ESSENTIAL_POPUP = 1, MP_PAUSE_CHEAT_CONSOLE = 2, MP_PAUSE_LOADING = 3,
       MP_PAUSE_POPUP_TIMELOCK = 4, MP_PAUSE_CONSECRATION = 5 };

/* GAME_ORDER payload: u16 type, guid[16], u16 arg_len, args (all little-endian) */
enum { MP_ORDER_MOVE = 1, MP_ORDER_STORY_VOTE = 3, MP_ORDER_MAP_PROPOSE = 4, MP_ORDER_MAP_VOTE = 5, MP_ORDER_MAX = 64 };
/* 3-5: ui lane's global votes (story, map-change propose / vote): no unit-owner check */
#define MP_ORDER_HDR 20

typedef struct mp_config {
    int  host;                  /* role: 1 host, 0 client */
    char host_addr[256];        /* client: where to connect */
    uint16_t port;              /* host: listen port (0 = any); client: host's port */
    char name[64];              /* player name */
    int  allow_speedup, allow_pause;   /* host lobby settings (sent to all in the WELCOME) */
    int  pause_mode;            /* host: DSNET_PAUSE_NONE / _ANYONE (default) / _ALL_VOTE; allow_pause 0 forces NONE */
    int  gear_mode;             /* host lobby: 0 shared storage + equip own units, 1 shared + anyone, 2 own storage.
                                   Sent to every guest in the WELCOME; read it with dsnet_gear_mode(). */
    int  min_players;           /* host: timer starts once this many are in (default 1) */
    float tick_ms;              /* host: ms per timer step (default 1000/60) */
    int  fp_interval;           /* host: fingerprint every N steps (default 30) */
    int  auto_resync;           /* 1 = ask the host for its save when flagged desynced (default 1) */
    int  resync_delay_ms;       /* wait this long after the desync flag before asking (default 300) */
    int  auto_resume;           /* 1 = after an auto pause (join / desync), re-send this player's own run vote
                                   once it is in sync, unless it voted pause itself (default 1) */
    int  connect_timeout_ms;    /* wait for the WELCOME (default 5000) */
    int  need_save;             /* client: has no campaign loaded (menu / Steam join): ask the host for its save
                                   right after the WELCOME, even at step 0 (default 0; mp.json pairs load the same save) */
} mp_config;

void mp_config_defaults(mp_config *c);
/* Read instances/X/mp.json: {"role":"host"|"client","host":"1.2.3.4","port":7777,"name":"The owner",
 * "allow_speedup":true,"allow_pause":true, optional "min_players","tick_ms","fp_interval",
 * "auto_resync","resync_delay_ms","auto_resume","connect_timeout_ms","need_save"}. 0 ok, -1 file, -2 bad/missing role. */
int  mp_config_load(const char *path, mp_config *out);
int  mp_config_parse(const char *json, mp_config *out);

typedef struct mp_game_api {
    void *user;
    int  (*ready)(void *u);                         /* campaign loaded + locked: steps may run */
    void (*sim_tick)(void *u);                      /* exactly one world tick (locked sim cycle) */
    void (*sim_scope)(void *u, int enter);          /* enter/leave the sim RNG + id scope (orders) */
    uint64_t (*fingerprint)(void *u);               /* world hash after a step */
    int  (*order_move)(void *u, const uint8_t guid[16], int32_t x, int32_t y);   /* 0 ok */
    int  (*save)(void *u, uint8_t **data, size_t *len);        /* host: malloc'd game save (incl. det state) */
    int  (*load)(void *u, const uint8_t *data, size_t len);    /* start loading; ready() is 0 until done */
    void (*show_speed)(void *u, int game_speed, int paused);   /* deterministic: same step on every peer */
} mp_game_api;

typedef struct mp_session mp_session;
typedef int (*mp_order_fn)(mp_session *s, int player, const uint8_t guid[16], const uint8_t *args, uint16_t len);

typedef struct mp_stats {
    uint32_t timer;             /* next step */
    uint64_t ticks;             /* world ticks run */
    uint32_t orders_applied, orders_dropped_owner, orders_bad;
    uint32_t loads, saves, desyncs;
    int desynced, needs_state, loading, paused, speed;   /* speed: effective GameSpeed (1..3) */
    int player;
} mp_stats;

void mp_set_log(void (*fn)(const char *line));

/* Session: host = dsnet_server_start + local client; client = TCP. NULL on failure (reason logged;
 * *reject gets enum dsnet_reject when the host refused the join). */
mp_session *mp_start(const mp_config *cfg, const mp_game_api *api, int *reject);
void        mp_stop(mp_session *s);
dsnet_client *mp_client(mp_session *s);
uint16_t      mp_server_port(mp_session *s);   /* host: the port it listens on; 0 on a client */

/* ---- game thread, once per MainFlow.Update while locked ----
 * mp_frame = mp_ticks_to_run_now + per step { mp_apply_cmds, mp_step_ticks x sim_tick, fingerprint }.
 * frame_ms <= 0: measured with the monotonic clock. Returns the number of timer steps run. */
int  mp_frame(mp_session *s, float frame_ms);
/* Building blocks of mp_frame (for a loop that wants to own the steps itself): */
int  mp_ticks_to_run_now(mp_session *s, float frame_ms);   /* timer steps allowed this frame (0 = hold) */
int  mp_apply_cmds(mp_session *s, uint32_t timer);         /* run step `timer`'s commands; -1 not runnable */
int  mp_step_ticks(mp_session *s);                         /* world ticks of the step just applied */
void mp_step_done(mp_session *s, uint32_t timer);          /* fingerprint + timer++ */

/* ---- UI side (game thread): becomes a command, runs on every peer at the server's tick ---- */
int  mp_order(mp_session *s, uint16_t type, const uint8_t guid[16], const void *args, uint16_t len);
int  mp_order_move(mp_session *s, const uint8_t guid[16], int32_t x, int32_t y);
int  mp_vote_speed(mp_session *s, int game_speed);         /* 1 normal, 2 fast; 3 is clamped to 2 */
int  mp_vote_pause(mp_session *s, int pause);              /* this player's own (User) pause */
int  mp_toggle_pause(mp_session *s);
/* Local popup pause reasons (EssentialPopup, PopupTimeLock, ConsecrationNotice). allow_pause on:
 * the first open reason votes pause, the last closed one votes run (unless the user paused).
 * allow_pause off: ignored, the game keeps running. Returns 1 if it became a vote. */
int  mp_popup_pause(mp_session *s, int reason, int add);
/* Players that left and are waited for (dsnet HOLD, reason PLAYER_LEFT: 0 game ticks in every pause mode; ids = hold
 * index). Host popup "<name> disconnected": mp_left_choice(1) "Keep playing" (holds off + temporary control: the host
 * may order their RESERVED units), (0) "Wait for <name>" (stays held until the same uid rejoins, then it ends by itself).
 * The host pressing play also means Keep playing. */
int  mp_left_players(mp_session *s, int *ids, char (*names)[32], int max);
void mp_left_choice(mp_session *s, int keep_playing);
int  mp_left_waiting(mp_session *s);                     /* 1 = "Wait" chosen, still waiting */
int  mp_claim(mp_session *s, const uint8_t guid[16]);     /* ownership (max 3); */
int  mp_release(mp_session *s, const uint8_t guid[16]);
int  mp_assign(mp_session *s, const uint8_t guid[16], int player);  /* host: give a unit to a player */

/* Order dispatch table: MOVE is pre-filled (api->order_move); the orders lane adds the others. */
void mp_set_order_handler(mp_session *s, uint16_t type, mp_order_fn fn);
const mp_game_api *mp_api(mp_session *s);

void mp_get_stats(mp_session *s, mp_stats *out);
int  mp_allow_pause(mp_session *s);
int  mp_allow_speedup(mp_session *s);
int  mp_is_applying(mp_session *s);   /* 1 while commands are being applied (speed hooks pass through) */
uint32_t mp_cur_step(mp_session *s); /* step being applied / next step */
