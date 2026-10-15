/* mp_game: the real game side of mp (dylib only). Joins mp_core to the rig + determinism layer.
 *
 * ---- What mp_game.c needs from the determinism lane (rig_det.c / rig_cmd.c), not yet exposed ----
 * All main thread. Names are proposals; if you pick other names, tell the mp lane.
 */
#pragma once
#include "rig.h"
#include "mp.h"

int    rig_det_mp_ready(void);            /* locked && runnable() (campaign loaded, not busy, not hold) */
void   rig_det_sim_tick(void);            /* exactly one sim_cycle(); no target / per_frame logic */
void   rig_det_sim_scope(int enter);      /* 1: save the view RNG, switch to sim_state, in_sim = 1 (no tick);
                                             0: in_sim = 0, store sim_state, restore the view RNG.
                                             Orders are built inside this scope so their RNG draws / Guids
                                             come from the sim streams, like inject_orders inside sim_cycle. */
int    rig_det_in_sim(void);              /* in_sim (speed hooks let sim-origin calls through) */
size_t rig_det_state_size(void);          /* bytes of the deterministic state not in the game save */
void   rig_det_state_get(void *out);      /* sim_state, seed, guid_sim_n, sr_sim_n (and any future counter) */
void   rig_det_state_set(const void *in); /* locked: apply now. Not locked: keep it and apply it at the next
                                             (auto-)lock INSTEAD of Random.InitState(seed) + zeroed counters */
int    rig_order_move(Guid g, int x, int y);   /* body of inject_orders for one MOVE (MoveTask + DirectOrderRequested
                                                  + TickFlowHandler.EnqueueEvent on cur_map()); 0 ok, -1 unit/flow missing */
int    rig_save_campaign(const char *name);    /* CampaignSaveLoader.Save 0x814890 (this = mf+0x180, campaignName = name,
                                                  isAutoSave = iron-mode flag, displayName = name) -> writes
                                                  data/Saves/<name>.json synchronously; 1 ok. See MP_NOTES.md. */
int    rig_load_save_file(const char *path);   /* c_load_save {"file":path,"det":1} without JSON: copy into
                                                  data/Saves/<SaveName>.json, rig_det_on_load(), LoadCampaignRequested.
                                                  rig_det_mp_ready() must read 0 from this call until the auto-lock. */

/* ---- What mp_game.c gives the determinism lane / rig ---- */
void  mp_game_install(void);              /* from the GameAssembly add-image callback, after rig_det_install():
                                             hooks GameSpeedSetting.SetSpeed / AddPauseReason /
                                             RemovePauseReason (pass through with no session), reads <rig_dir>/mp.json if any. */
int   mp_game_active(void);               /* valid mp.json, or a lobby session started */
void  mp_game_poll(void);               /* every MainFlow.Update, title screen too (rig_frame): Steam joiner bridge,
                                             accepted Steam invites */
/* Title-screen lobby (ui lane, ui_mp.h): start without mp.json. host_addr "steam:<lobby id>" = Steam join. */
mp_session *mp_game_session(void);
int   mp_gear_check(const uint8_t unit[16]);
/* Host popup "<name> disconnected" (mpui): players the host waits for (0 = no popup); choice 1 "Keep playing",
   0 "Wait for <name>" (paused until the same player rejoins). mp_game_left_waiting: "Wait" chosen. */
int   mp_game_left_players(int *ids, char (*names)[32], int max);
void  mp_game_left_choice(int keep_playing);
int   mp_game_left_waiting(void);
int   mp_game_last_reject(void);      /* enum dsnet_reject of the last failed start, 0 if none */
int   mp_game_load_pending(void);     /* 1 while an mp load runs (title may show; not a teardown) */   /* 1 = local player may equip this unit (host gear mode, dsnet_may_equip) */
int   mp_game_host(const mp_config *cfg); /* listen (TCP + Steam lobby if Steam is up); 0 ok */
int   mp_game_join(const mp_config *cfg); /* TCP now, or Steam (connects in mp_game_poll); 0 ok, -3 no Steam */
int   mp_game_steam_invite(void);         /* overlay invite while hosting; -1 no Steam lobby */
int   mp_game_frame(void);                /* from rig_det_frame when locked, INSTEAD of its own target loop.
                                             Starts the session on the first call. Returns 1 = mp drove the sim. */
char *mp_game_exec(const char *cmd, const char *line);   /* socket commands mp_*; NULL = not an mp command */
void  mp_game_status(Buf *b);             /* extra status fields for c_status */
