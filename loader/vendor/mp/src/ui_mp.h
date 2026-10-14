/* ui_mp - in-game multiplayer interface (build 25846389, arm64). Dylib only.
 * Game side: ui_game.c (hooks + cloned game widgets). Pure logic: ui_model.c (tested in ui_test/).
 * Notes, hook list and what to verify in the game: ~/ds_multiplayer/src/UI_NOTES.md.
 *
 * ---- What ui_game.c gives the rig / glue ----                                                  */
#pragma once
#include <stdint.h>
#include "mp.h"

void ui_install(void);   /* GameAssembly add-image callback, after mp_game_install() and ord_install():
                            installs the UI hooks (22). Always on: with no session every hook passes through. */
void ui_frame(void);     /* main thread, once per MainFlow.Update (title screen too), e.g. at the end of
                            rig_frame(). Status line, owner tags, deferred story click. Cheap. */
/* For ord_mp.c emit(): returns 1 if this player may order the unit `owner` (zero Guid = global order:
 * always 1). 0 = blocked locally; a short notice was shown ("<unit> belongs to <player>"). */
int  ui_check_order(const uint8_t owner[16]);
/* For the transitions lane: call this when a UI-origin map change (enter dungeon / return to base /
 * next floor; kind = UI_MAP_* in ui_model.h) is captured in multiplayer, INSTEAD of sending its order.
 * order/len = the exact order bytes you would have sent (<= 1024). Returns 1 = taken into a vote (do not
 * send), 0 = no session or nothing to vote on (send it as usual). On accept, the host's peer calls
 * trn_submit_map_change(order, len) once, from ui_frame (outside command apply). */
int  ui_map_propose(int kind, const uint8_t *order, size_t len);
/* Socket commands "ui_*" (rig_exec): ui_levelup {unit, building, map} = test level-up on every peer. NULL = not ours. */
char *ui_exec(const char *cmd, const char *line);
/* Teams lane: dim / owner-colour a portrait clone (SubUI_UnitShortCutParts) like the unit bar. */
void ui_portrait_style(void *parts, int owner);
/* Lobby (mpmenu.c): default player name = the Mac account's full name, else the login name. */
const char *ui_default_player_name(void);

/* ---- What ui_game.c needs from other lanes (weak imports: missing = feature degrades, no crash) ----
 * mpglue (mp_game.c):                                                                             */
mp_session *mp_game_session(void) __attribute__((weak_import));     /* current session or NULL */
/* Title-screen lobby: start hosting / joining with this config (role, port/host, name, lobby flags).
 * Host: listen (TCP + Steam lobby if available); the session runs once a campaign is loaded.
 * Client: connect now; the host's save arrives through the resync path. 0 ok, <0 error. */
int mp_game_host(const mp_config *cfg) __attribute__((weak_import));
int mp_game_join(const mp_config *cfg) __attribute__((weak_import));
int mp_game_steam_invite(void) __attribute__((weak_import));        /* Steam overlay invite; <0 no Steam */
/* netcore (dsnet): read-only views of the state dsnet already keeps in simstate.                  */
uint32_t    dsnet_present_mask(dsnet_client *c) __attribute__((weak_import));
int         dsnet_vote_of(dsnet_client *c, int player, int *speed, int *pause) __attribute__((weak_import)); /* 0 ok */
const char *dsnet_player_name(dsnet_client *c, int player) __attribute__((weak_import));  /* needs the name in
                                                                  PLAYER_JOIN's payload; NULL/"" unknown */
/* Story pause (owner 2026-10-12): story popups pause everyone even with "Allow pause: Off".
 * ui sends DSNET_STORY_PAUSE (payload u8 1 = popup open, 0 = closed) through dsnet_submit when
 * allow_pause is 0 (with pause on, the existing EssentialPopup -> mp_popup_pause vote does it).
 * dsnet must: accept type 10 from any playing player regardless of allow_pause (server filter), keep
 * story_pause[player] in simstate (reset on JOIN/LEAVE, saved with the state), make
 * dsnet_ticks_per_step 0 while any present player holds it, and expose it:                         */
#ifndef DSNET_STORY_PAUSE
#define DSNET_STORY_PAUSE 10
#endif
uint32_t dsnet_story_pause_mask(dsnet_client *c) __attribute__((weak_import));
/* transitions lane: send this map-change order now as a normal (host) order. 0 ok. */
int      trn_submit_map_change(const uint8_t *order, size_t len) __attribute__((weak_import));
/* netcore: players still busy in a map-change flow (dsnet_set_busy from transitions_design.md 3.7). */
uint32_t dsnet_busy_mask(dsnet_client *c) __attribute__((weak_import));
/* mp_core: run_order must NOT apply the unit-owner check to the ui order types 3..5
 * (UI_ORDER_STORY_VOTE, UI_ORDER_MAP_PROPOSE, UI_ORDER_MAP_VOTE); their handlers (ui_game.c,
 * registered with mp_set_order_handler) apply their own rules.                                       */
