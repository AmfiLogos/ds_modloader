/* ord_mp.c - glue between the orders lane (ord_api.h) and mp_core (mp.h): every ord payload travels as
 * one mp order of type ORD_MP_TYPE; mp_core does the ownership check on the owner Guid and wraps the
 * handler in the sim scope. */
#include "ord_api.h"
#include "mp.h"
#include "rig.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int ui_check_order(const uint8_t owner[16]) __attribute__((weak_import));
static void emit(void *user, const uint8_t owner[16], const uint8_t *payload, size_t len) {
    if (ui_check_order && !ui_check_order(owner)) { rig_log("ord: order on a unit this player does not own: blocked locally"); return; }
    if (len > DSNET_MAX_PAYLOAD - MP_ORDER_HDR || mp_order(user, ORD_MP_TYPE, owner, payload, (uint16_t)len))
        rig_log("ord: mp_order refused a %zu-byte order", len);
}
/* POPULATION rule (owner 2026-10-13, lan_v8; dsnet_may_recruit): checked inside the replayed recruit order, so every
 * peer decides the same. A hire above the player's cap is refused (no unit, a message on the hirer's screen). When the
 * rule lets the clan go above the game's own cap (Per player, Multiplied), the clan's MaxPopulation
 * (ClanDataContainer +0x3C, campaign data +0x50) is raised for this one replay so the stock recruit check accepts, then
 * restored. Nobody is ever removed (overflow after a rule change or a leave only stops new hires).
 * Wire peek (ord_core.h v2): 'O', ver, u16 kind (1 = event), i32 map, i32 group, u8 nsel + nsel guids, u16 event id. */
void ui_notice(const char *text) __attribute__((weak_import));
#define ORD_EV_CANDIDATE_RECRUIT 52                    /* ord_events_gen.h: CandidateRecruitRequested */
static int is_recruit(const uint8_t *p, size_t n) {
    if (n < 15 || p[0] != 'O' || (p[2] | p[3] << 8) != 1) return 0;
    size_t off = 13 + 16u * p[12];
    return off + 2 <= n && (p[off] | p[off + 1] << 8) == ORD_EV_CANDIDATE_RECRUIT;
}
static const char *pop_mode_name(int m) {
    return m == DSNET_POP_PER_PLAYER ? "Per player" : m == DSNET_POP_SPLIT ? "Split equally" : m == DSNET_POP_MULTIPLIED ? "Multiplied" : "Shared";
}
static int handler(mp_session *s, int player, const uint8_t guid[16], const uint8_t *args, uint16_t len) {
    (void)guid;
    Obj c0 = cdc(), clan = c0 ? AT(c0, 0x50, Obj) : NULL;
    if (!is_recruit(args, len) || !clan) return ord_apply(player, args, len);
    dsnet_client *c = mp_client(s);
    int base = AT(clan, 0x3C, int32_t), count = AT(clan, 0x38, int32_t), own = dsnet_owned_count(c, player), cap = 0, pool = 1;
    dsnet_rules r; dsnet_rules_get(c, &r);
    int why = dsnet_recruit_check(c, player, base, count, own, &cap, &pool);   /* both rules: population, then units per player */
    if (why != DSNET_RECRUIT_OK) {
        rig_log("ord: recruit by player %d refused (%d): population %s %d/%d (rule %s), owns %d", player, why, pool ? "clan" : "own", pool ? count : own, cap, pop_mode_name(r.population), own);
        if (player == dsnet_player_id(c) && ui_notice) {
            char t[160];
            if (why == DSNET_RECRUIT_UNITS_FULL) snprintf(t, sizeof t, "You already have %d units: host rule Units per player", own);
            else if (pool) snprintf(t, sizeof t, "Population full (%d/%d): host rule %s", count, cap, pop_mode_name(r.population));
            else snprintf(t, sizeof t, "Population full for you (%d/%d): host rule %s", own, cap, pop_mode_name(r.population));
            ui_notice(t);
        }
        return 0;                                      /* consumed: same on every peer */
    }
    int need = count + 1 > base ? count + 1 : base;    /* the stock check: CurrentPopulation < MaxPopulation */
    /* every own-cap / pool rule: free units (nobody's) also fill the clan, so Split can pass the game's cap too */
    int raised = need != base && r.population != DSNET_POP_SHARED;
    if (raised) AT(clan, 0x3C, int32_t) = need;
    int rc = ord_apply(player, args, len);
    if (raised) AT(clan, 0x3C, int32_t) = base;
    rig_log("ord: recruit by player %d allowed (%s %d/%d, rule %s%s)", player, pool ? "clan" : "own", pool ? count : own, cap, pop_mode_name(r.population), raised ? ", cap raised for the replay" : "");
    return rc;
}
/* Recruits belong to the recruiter: the replay runs on every peer; only the host sends the OWNERSHIP
 * command (stamped after this step), so all peers apply it at the same tick. */
static mp_session *cur;
static void on_recruit(const uint8_t unit[16], int player) {
    if (cur && dsnet_is_host(mp_client(cur)) && player >= 0 && mp_assign(cur, unit, player))
        rig_log("ord: mp_assign of a recruit to player %d failed", player);
}
/* item ownership (gear mode own storage): dsnet keeps the item -> owner table in its sim state */
void ui_notice(const char *text) __attribute__((weak_import));
static int it_me(void) { return cur ? dsnet_player_id(mp_client(cur)) : 0; }
static int it_may_use(int player, const uint8_t item[16]) {
    if (!cur) return 1;
    dsnet_client *c = mp_client(cur);
    int ok = dsnet_may_use_item(c, player, item);
    /* ord asks only for this player's own UI actions (capture); review #29: say whose item it is */
    if (!ok && player == it_me() && ui_notice) {
        int o = dsnet_item_owner(c, item);
        const char *n = o >= 0 ? dsnet_player_name(c, o) : NULL;
        char t[96]; snprintf(t, sizeof t, "This item belongs to %s", n && n[0] ? n : "another player");
        ui_notice(t);
    }
    return ok;
}
/* the unit's OWNER (item tags: owner decision 2026-10-14, a lent unit's pick-ups go to its owner's storage, never the
 * borrower's). Who may ORDER the unit is it_may_order below (dsnet_may_order: the borrower while lent, Admin, ...). */
static int it_unit_owner(const uint8_t unit[16]) { return cur ? dsnet_owner_of(mp_client(cur), unit) : -1; }
static void it_tag(const uint8_t item[16], int player, int in_replay) {
    if (!cur) return;
    dsnet_client *c = mp_client(cur);
    if (dsnet_gear_mode(c) != DSNET_GEAR_OWN_STORAGE || dsnet_item_owner(c, item) >= 0) return;   /* only free items */
    /* inside a command (an order replay, or a sim event raised while a command applies, e.g. a pick-up event run at
       once by EventQueue) every peer runs this same code and dsnet_item_set_owner updates locally: all peers apply it.
       From the free-running simulation only the host sends the command. Run 48: the host alone updated inside a
       command (the pick-up debug order) and the guest's fingerprint differed one fingerprint later (DESYNC). */
    int in_cmd = in_replay || mp_is_applying(cur);
    if (in_cmd || dsnet_is_host(c)) {
        Guid g; memcpy(&g, item, 16); char gs[40]; guid_str(g, gs);
        rig_log("ord: item %s -> player %d (%s)", gs, player, in_replay ? "order" : in_cmd ? "simulation, in a command" : "simulation");
        dsnet_item_set_owner(c, item, player);
    }
}
static int it_may_order(const uint8_t unit[16], int player) { return cur ? dsnet_may_order(mp_client(cur), player, unit) : 1; }
static const ord_item_api item_api = { it_may_use, it_tag, it_me, it_unit_owner, it_may_order };

void ord_mp_attach(struct mp_session *s) {
    cur = s;
    if (!s) { ord_capture(NULL, NULL); ord_set_recruit_hook(NULL); ord_set_gear_check(NULL); ord_set_item_api(NULL); return; }
    ord_set_item_api(&item_api);
    mp_set_order_handler(s, ORD_MP_TYPE, handler);
    ord_set_recruit_hook(on_recruit);
    /* gear mode (host lobby option): provided by netcore / mpglue as `int mp_gear_check(const uint8_t unit[16])` */
    ord_set_gear_check((int (*)(const uint8_t *))dlsym(RTLD_DEFAULT, "mp_gear_check"));
    ord_capture(emit, s);
}
