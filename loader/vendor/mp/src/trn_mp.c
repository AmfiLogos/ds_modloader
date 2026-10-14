// trn_mp.c - transitions lane <-> mp glue (dylib only): host sends an accepted map change, busy edges.
#include "mp_game.h"
#include "trn.h"
#include "ord_api.h"
#include <string.h>

void dsnet_set_busy(dsnet_client *c, int busy) __attribute__((weak_import));   /* netcore (design 3.7) */

/* ui lane: the vote passed. `order` is the ord payload the orders lane captured (ChangeMapRequested /
 * GoDownDungeonRequested); it runs as a host order (zero guid = unclaimed = host only) on every peer. */
int trn_submit_map_change(const uint8_t *order, size_t len) {
    mp_session *s = mp_game_session();
    static const uint8_t zero[16];
    if (!s || !order || len > 0xFFFF) return -1;
    return mp_order(s, ORD_MP_TYPE, zero, order, (uint16_t)len);
}

/* Once per frame in multiplayer. The hold itself is in the step loop (mp_core: check ready() after every
 * sim_tick, keep the rest of the step; design 3.3). Here: tell the server we are busy (everyone waits,
 * "waiting for X"), and when our flow ends after a map change, vote 1x (owner: speed resets to 1x; votes are
 * commands, so every peer applies them at the same step; the lowest vote wins at once). */
void trn_flow_frame(void) {
    static int was_busy, was_transition;
    mp_session *s = mp_game_session();
    if (!s) { was_busy = was_transition = 0; return; }
    int b = trn_flow_busy();
    Obj ts = mf ? AT(mf, 0xF8, Obj) : NULL;                       /* MapTransitionState.IsTransitionRunning */
    if (ts && AT(ts, 0x14, bool)) was_transition = 1;
    if (b != was_busy && dsnet_set_busy) dsnet_set_busy(mp_client(s), b);
    if (was_busy && !b && was_transition) { mp_vote_speed(s, MP_SPEED_NORMAL); was_transition = 0; }
    was_busy = b;
}
