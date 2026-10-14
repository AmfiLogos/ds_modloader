/* ord_api.h - player-order capture and replay for the lockstep mod (Dungeon Settlers build 25846389).
 * Owner: orders lane. Notes: ~/ds_multiplayer/src/ORD_NOTES.md. Spec: research/orders_catalog.md "Set 1".
 *
 * Capture: the UI's state-changing calls (Tier A: command events at InputDispatcher.<Update>b__8_0;
 * Tier B: MoveOrder / WaypointOrder / ActionOrder / UseSkillMode.UseSkill / ...) are serialized,
 * swallowed locally and handed to the emit callback. Replay: ord_apply() rebuilds the call on every
 * peer (the issuer included) at the step the server stamped, with the issuer's selection swapped in.
 * With no emit callback set, everything passes through (single player, rig tests).
 *
 * All functions: Unity main thread. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define ORD_MP_TYPE 2   /* mp order type used for every ord payload (mp.h: MP_ORDER_MOVE = 1) */

/* owner: Guid of the unit the order is about (first selected / acting unit), zero = none (global order).
 * mp uses it for the ownership check. payload: ord wire bytes (ord_core.h), len <= 60000 - 20. */
typedef void (*ord_emit_fn)(void *user, const uint8_t owner[16], const uint8_t *payload, size_t len);

void ord_install(void);                       /* hooks; GameAssembly add-image callback, after rig_det_install() */
void ord_capture(ord_emit_fn fn, void *user); /* NULL = capture off (pass-through) */
int  ord_apply(int player, const uint8_t *payload, size_t len);   /* 0 ok, <0 bad payload / target missing */
void ord_set_ticking(int on);                 /* det layer: 1 while a world tick runs (captures are off) */
int  ord_executing(void);                     /* 1 while ord_apply runs */
/* Set 2 hooks for the netcore / mpglue lanes (NULL = off):
 * recruit: called on EVERY peer during the replay of CandidateRecruitRequested, once per new unit, with the
 *   issuing player (the same on all peers). ord_mp.c: the host sends mp_assign(unit, player).
 * gear: called before an equip-type command is sent, once per unit Guid it touches; 0 = blocked locally. */
void ord_set_recruit_hook(void (*fn)(const uint8_t unit[16], int player));
void ord_set_gear_check(int (*fn)(const uint8_t unit[16]));
/* Item ownership (gear mode "own storage"). may_use: 1 = this player may take/equip/use the item.
 * tag: give an untagged item to a player; in_replay = 1 inside an order replay (every peer calls it, applies at
 * once), 0 from the simulation (pickup, crafting: only the host should send it). me: local player id.
 * unit_owner: owner of a unit (-1 none). Any member may be NULL. */
typedef struct ord_item_api {
    int  (*may_use)(int player, const uint8_t item[16]);
    void (*tag)(const uint8_t item[16], int player, int in_replay);
    int  (*me)(void);
    int  (*unit_owner)(const uint8_t unit[16]);
    int  (*may_order)(const uint8_t unit[16], int player);   /* lan_v9: dsnet_may_order (own, lent, admin, host's temp control) */
} ord_item_api;
void ord_set_item_api(const ord_item_api *a);

/* status for c_status / logs */
typedef struct ord_stats {
    uint32_t captured, applied, apply_failed, blocked, passed_local, unknown_types;
} ord_stats;
void ord_get_stats(ord_stats *out);

/* ord_mp.c: glue to mp_core (mp.h). Sets the mp handler for ORD_MP_TYPE and the emit callback. */
struct mp_session;
void ord_mp_attach(struct mp_session *s);     /* after mp_start; NULL detaches (capture off) */
