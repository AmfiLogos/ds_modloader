// trn.h - transitions lane (build 25846389): deterministic floor generation, multi-frame flow barrier,
// host-only autosave, map-change vote. Design: ~/ds_multiplayer/research/transitions_design.md
#pragma once
#include <stdint.h>
#include <stddef.h>

/* ---- pure logic (no game; unit-tested in trn_test/) ---- */
/* seed of generation attempt `attempt` for a floor: from saved, peer-identical state only */
int32_t trn_floor_seed(int64_t tick, int32_t region, int32_t floor, int32_t cur_seed, int32_t attempt);

/* The map-change VOTE itself is the ui lane's (ui_model.h ui_mapvote_*, order types 4/5). This lane gives it
 * trn_submit_map_change() (trn_mp.c) and the floor/flow/autosave fixes. */

/* ---- game side (dylib only) ---- */
#ifndef TRN_NO_GAME
void  trn_install(void);                          /* from the add-image callback, after rig_det_install() */
char *trn_exec(const char *cmd, const char *line);/* "trn_*" socket commands; NULL = not ours */
int   trn_flow_busy(void);                        /* a multi-frame flow runs: EventQueue async busy || transition || loading */
void  trn_set_role(int is_host);                  /* mp glue: clients never autosave; host diverts the game-hour autosave */
/* trn_mp.c (needs mp_game.h): */
int   trn_submit_map_change(const uint8_t *order, size_t len);   /* ui lane, host on accept: send as a host order */
void  trn_flow_frame(void);                       /* once per MainFlow.Update in multiplayer: busy edges -> dsnet busy
                                                     flag, speed back to 1x after a map change (owner 2026-10-12) */
#endif
