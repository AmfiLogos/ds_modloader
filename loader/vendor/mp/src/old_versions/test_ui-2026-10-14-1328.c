/* ui_test: checks the pure logic in ui_model.c (no game, no network). */
#include "ui_model.h"
#include <stdio.h>
#include <string.h>

static int test_color_of(int player) { return player == 3 ? 2 : -1; }
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKS(a, b) do { checks++; if (strcmp((a), (b))) { fails++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

/* reference copy of dsnet_client.c sim_apply OWNERSHIP (2026-10-12) to check ui agrees with it */
static int dsnet_rule(int pl, int cur, int owner, const int cnt[UI_MAX_PLAYERS]) {
    int host = pl == 0;
    if (!host && !((cur == -1 || cur == pl) && (owner == pl || owner == -1))) return 0;
    if (owner == -1) return 1;
    int count = cnt[owner] - (cur == owner);
    return count < 3;
}

static void test_claims(void) {
    { int cnt[UI_MAX_PLAYERS] = {0}; cnt[1] = 2; ui_max_owned = 2;      /* host rule: 2 units per player */
      CHECK(ui_portrait_action(1, UI_UNCLAIMED, cnt, 0x3).kind == UI_ACT_DENY_FULL);
      ui_max_owned = 6; CHECK(ui_portrait_action(1, UI_UNCLAIMED, cnt, 0x3).kind == UI_ACT_CLAIM);
      ui_max_owned = UI_MAX_OWNED; }
    int cnt[UI_MAX_PLAYERS] = {0};
    uint32_t present = 0x7;                       /* host, 1, 2 */
    ui_act a;
    /* player 1 claims, releases, is denied a foreign unit and a 4th unit */
    a = ui_portrait_action(1, UI_UNCLAIMED, cnt, present); CHECK(a.kind == UI_ACT_CLAIM && a.target == 1);
    a = ui_portrait_action(1, 1, cnt, present);            CHECK(a.kind == UI_ACT_RELEASE);
    a = ui_portrait_action(1, 2, cnt, present);            CHECK(a.kind == UI_ACT_DENY_OWNED && a.target == 2);
    a = ui_portrait_action(1, 0, cnt, present);            CHECK(a.kind == UI_ACT_DENY_OWNED && a.target == 0);
    cnt[1] = 3;
    a = ui_portrait_action(1, UI_UNCLAIMED, cnt, present); CHECK(a.kind == UI_ACT_DENY_FULL);
    a = ui_portrait_action(1, 1, cnt, present);            CHECK(a.kind == UI_ACT_RELEASE);   /* full may still release */
    /* host cycle: unclaimed -> 0 -> (1 full) 2 -> unclaimed */
    a = ui_portrait_action(0, UI_UNCLAIMED, cnt, present); CHECK(a.kind == UI_ACT_ASSIGN && a.target == 0);
    a = ui_portrait_action(0, 0, cnt, present);            CHECK(a.kind == UI_ACT_ASSIGN && a.target == 2);
    a = ui_portrait_action(0, 2, cnt, present);            CHECK(a.kind == UI_ACT_ASSIGN && a.target == UI_UNCLAIMED);
    a = ui_portrait_action(0, 1, cnt, present);            CHECK(a.kind == UI_ACT_ASSIGN && a.target == 2);  /* host takes from 1 */
    /* absent players are skipped */
    a = ui_portrait_action(0, 0, cnt, 0x5);                CHECK(a.kind == UI_ACT_ASSIGN && a.target == 2);
    /* everyone full: host cannot assign an unclaimed unit */
    int full[UI_MAX_PLAYERS] = {3, 3, 3};
    a = ui_portrait_action(0, UI_UNCLAIMED, full, present); CHECK(a.kind == UI_ACT_DENY_FULL);

    /* admin rule: host without admin cannot take player 1's unit or a reserved one; with admin it can */
    a = ui_portrait_action_admin(0, 1, cnt, present, 0);   CHECK(a.kind == UI_ACT_DENY_OWNED && a.target == 1);
    a = ui_portrait_action_admin(0, UI_RESERVED, cnt, present, 0); CHECK(a.kind == UI_ACT_DENY_OWNED);
    a = ui_portrait_action_admin(0, 1, cnt, present, 1);   CHECK(a.kind == UI_ACT_ASSIGN && a.target == 2);
    a = ui_portrait_action_admin(0, UI_UNCLAIMED, cnt, present, 0); CHECK(a.kind == UI_ACT_ASSIGN && a.target == 0);   /* free: yes */
    a = ui_portrait_action_admin(0, 0, cnt, present, 0);   CHECK(a.kind == UI_ACT_ASSIGN && a.target == 2);           /* own: give away */
    a = ui_portrait_action_admin(2, UI_RESERVED, cnt, present, 0); CHECK(a.kind == UI_ACT_DENY_OWNED);
    /* every action the UI proposes is one dsnet accepts, for all states of a 3-player game */
    int bad = 0, n = 0;
    for (int me = 0; me < 3; me++)
        for (int cur = -1; cur < 3; cur++)
            for (int c0 = 0; c0 <= 3; c0++) for (int c1 = 0; c1 <= 3; c1++) for (int c2 = 0; c2 <= 3; c2++) {
                int c[UI_MAX_PLAYERS] = {c0, c1, c2};
                if (cur >= 0 && c[cur] == 0) continue;            /* cur owner owns at least this unit */
                ui_act x = ui_portrait_action(me, cur, c, present);
                int target = -2;
                if (x.kind == UI_ACT_CLAIM) target = me;
                else if (x.kind == UI_ACT_RELEASE) target = -1;
                else if (x.kind == UI_ACT_ASSIGN) target = x.target;
                if (target == -2) continue;
                n++;
                if (!dsnet_rule(me, cur, target, c) || !ui_ownership_valid(me, cur, target, c)) bad++;
            }
    CHECK(n > 100 && bad == 0);
    /* ui_ownership_valid == dsnet rule everywhere */
    int diff = 0;
    for (int me = 0; me < 3; me++) for (int cur = -1; cur < 3; cur++) for (int nw = -1; nw < 3; nw++)
        for (int k = 0; k <= 3; k++) { int c[UI_MAX_PLAYERS] = {k, 3 - k, 1}; if (dsnet_rule(me, cur, nw, c) != ui_ownership_valid(me, cur, nw, c)) diff++; }
    CHECK(diff == 0);
    /* orders: owner only; unclaimed = host only */
    CHECK(ui_may_order(1, 1)); CHECK(!ui_may_order(1, 2)); CHECK(!ui_may_order(1, UI_UNCLAIMED));
    CHECK(ui_may_order(0, UI_UNCLAIMED)); CHECK(!ui_may_order(0, 1));
}

static void test_votes(void) {
    ui_players p; memset(&p, 0, sizeof p);
    strcpy(p.name[0], "The owner"); strcpy(p.name[1], "B");
    ui_votes v; memset(&v, 0, sizeof v);
    v.present = 0x7; v.allow_speedup = v.allow_pause = 1;
    v.speed[0] = 2; v.speed[1] = 1; v.speed[2] = 2;   /* speeds: 1 Normal, 2 Fast (3.5x) */
    CHECK(ui_effective_speed(&v) == 1);
    CHECK(!ui_is_paused(&v));
    char line[256];
    ui_status_line(UI_NET_PLAYING, &v, &p, 0, line, sizeof line);
    CHECKS(line, "You want Fast \u00B7 waiting for B");
    ui_status_line(UI_NET_PLAYING, &v, &p, 1, line, sizeof line);
    CHECKS(line, "The owner, Player 3 want Fast \u00B7 press >> to agree");
    CHECK(!strstr(line, "2x") && !strstr(line, "3x"));
    v.speed[2] = 0;                                /* no vote yet = Normal */
    ui_status_line(UI_NET_PLAYING, &v, &p, 1, line, sizeof line);
    CHECKS(line, "The owner wants Fast \u00B7 press >> to agree");
    ui_status_line(UI_NET_PLAYING, &v, &p, 0, line, sizeof line);
    CHECKS(line, "You want Fast \u00B7 waiting for B, Player 3");
    v.speed[1] = 2; v.speed[2] = 2;
    CHECK(ui_effective_speed(&v) == 2);
    ui_status_line(UI_NET_PLAYING, &v, &p, 0, line, sizeof line);
    CHECKS(line, "");
    v.speed[1] = 1;
    v.present = 0x5;                               /* player 1 left: their vote no longer counts */
    CHECK(ui_effective_speed(&v) == 2);
    v.present = 0x3;
    v.pause[1] = 1; v.pause[2] = 1;
    CHECK(ui_is_paused(&v));
    char who[64];
    CHECK(ui_pause_voters(&v, &p, who, sizeof who) == 1); CHECKS(who, "B");
    v.pause[0] = 1;
    CHECK(ui_pause_voters(&v, &p, who, sizeof who) == 2); CHECKS(who, "The owner, B");
    ui_status_line(UI_NET_PLAYING, &v, &p, 0, line, sizeof line);
    CHECKS(line, "Paused by the owner, B");
    { char pt[128]; ui_pause_text(&v, &p, pt, sizeof pt); CHECKS(pt, line); }     /* one text for panel + banner (#20) */
    { ui_votes w = v; memset(w.pause, 0, sizeof w.pause); w.paused_ext = 1; w.pause_by1 = 2;   /* auto pause (join / desync) */
      ui_status_line(UI_NET_PLAYING, &w, &p, 0, line, sizeof line); CHECKS(line, "Paused by B");
      char pt[128]; ui_pause_text(&w, &p, pt, sizeof pt); CHECKS(pt, "Paused by B");
      w.pause_by1 = 0; ui_pause_text(&w, &p, pt, sizeof pt); CHECKS(pt, "Paused"); }
    ui_status_line(UI_NET_PLAYING, &v, &p, 0, line, sizeof line);
    /* lobby off: no pause, 1x */
    v.allow_pause = 0; v.allow_speedup = 0;
    CHECK(!ui_is_paused(&v)); CHECK(ui_effective_speed(&v) == 1);
    ui_status_line(UI_NET_PLAYING, &v, &p, 0, line, sizeof line);
    CHECKS(line, "");
    v.story_pause = 0x2;                           /* story popup open: pauses even with pause off */
    CHECK(ui_is_paused(&v));
    ui_status_line(UI_NET_PLAYING, &v, &p, 0, line, sizeof line); CHECKS(line, "Paused for a story event");
    v.allow_pause = 1;
    ui_status_line(UI_NET_PLAYING, &v, &p, 0, line, sizeof line); CHECKS(line, "Paused for a story event (and by the owner, B)");
    v.story_pause = 0x4;                           /* absent player's story hold does not count */
    v.allow_pause = 0;
    CHECK(!ui_is_paused(&v));
    v.story_pause = 0;
    ui_status_line(UI_NET_DESYNC, &v, &p, 0, line, sizeof line);  CHECKS(line, "Resyncing with host...");
    ui_status_line(UI_NET_LOADING, &v, &p, 0, line, sizeof line); CHECKS(line, "Resyncing with host... (loading)");
    ui_status_line(UI_NET_OFF, &v, &p, 0, line, sizeof line);     CHECKS(line, "");
    /* labels and tags */
    char nm[40];
    CHECKS(ui_player_label(&p, 0, nm), "The owner"); CHECKS(ui_player_label(&p, 4, nm), "Player 5");
    CHECKS(ui_player_label(NULL, 0, nm), "Host");
    char tag[96];
    ui_owner_tag(&p, 1, tag, sizeof tag); CHECKS(tag, " <color=#5DADE2>[B]</color>");
    ui_color_resolver = test_color_of; CHECKS(ui_player_color(3), "#E88BD4"); CHECKS(ui_player_color(1), "#5DADE2");   /* slot 3 = uid with colour 2; unknown -> by slot */
    ui_color_resolver = NULL; CHECKS(ui_player_color(3), "#9AA7FF");
    ui_owner_tag(&p, UI_UNCLAIMED, tag, sizeof tag); CHECKS(tag, "");
    char tiny[4]; ui_pause_voters(&v, &p, tiny, sizeof tiny); CHECK(strlen(tiny) < 4);   /* truncates safely */
}

static void test_popups(void) {
    CHECK(ui_pop_kind_of(21) == UI_POP_LEVELUP); CHECK(ui_pop_kind_of(11) == UI_POP_TRADE);
    CHECK(ui_pop_kind_of(22) == UI_POP_STORY);   CHECK(ui_pop_kind_of(19) == UI_POP_OTHER);
    /* level-up / trade: owner only, never pause; unclaimed -> host */
    CHECK(ui_popup_route(UI_POP_LEVELUP, 1, 1) == UI_ROUTE_OPEN_NO_PAUSE);
    CHECK(ui_popup_route(UI_POP_LEVELUP, 0, 1) == UI_ROUTE_SKIP);
    CHECK(ui_popup_route(UI_POP_LEVELUP, 2, 1) == UI_ROUTE_SKIP);
    CHECK(ui_popup_route(UI_POP_LEVELUP, 0, UI_UNCLAIMED) == UI_ROUTE_OPEN_NO_PAUSE);
    CHECK(ui_popup_route(UI_POP_LEVELUP, 1, UI_UNCLAIMED) == UI_ROUTE_SKIP);
    CHECK(ui_popup_route(UI_POP_TRADE, 2, 2) == UI_ROUTE_OPEN_NO_PAUSE);
    CHECK(ui_popup_route(UI_POP_TRADE, 0, 2) == UI_ROUTE_SKIP);
    CHECK(ui_popup_route(UI_POP_TRADE, 0, UI_NO_UNIT) == UI_ROUTE_OPEN_NO_PAUSE);
    /* story: owner chooses, others read only; unclaimed -> host chooses; no unit -> vote */
    CHECK(ui_popup_route(UI_POP_STORY, 1, 1) == UI_ROUTE_STORY_CHOOSER);
    CHECK(ui_popup_route(UI_POP_STORY, 0, 1) == UI_ROUTE_STORY_READONLY);
    CHECK(ui_popup_route(UI_POP_STORY, 0, UI_UNCLAIMED) == UI_ROUTE_STORY_CHOOSER);
    CHECK(ui_popup_route(UI_POP_STORY, 2, UI_UNCLAIMED) == UI_ROUTE_STORY_READONLY);
    CHECK(ui_popup_route(UI_POP_STORY, 2, UI_NO_UNIT) == UI_ROUTE_STORY_VOTE);
    CHECK(ui_popup_route(UI_POP_OTHER, 2, 1) == UI_ROUTE_STOCK);
    /* return-to-base screen: portal user's owner picks; unclaimed / no unit / forced: host */
    CHECK(ui_pop_kind_of(3) == UI_POP_RESULT);
    CHECK(ui_popup_route(UI_POP_RESULT, 1, 1) == UI_ROUTE_RESULT_CHOOSER);
    CHECK(ui_popup_route(UI_POP_RESULT, 0, 1) == UI_ROUTE_RESULT_READONLY);
    CHECK(ui_popup_route(UI_POP_RESULT, 0, UI_UNCLAIMED) == UI_ROUTE_RESULT_CHOOSER);
    CHECK(ui_popup_route(UI_POP_RESULT, 2, UI_NO_UNIT) == UI_ROUTE_RESULT_READONLY);
    CHECK(ui_result_chooser(2, 0) == 2 && ui_result_chooser(2, 1) == 0 && ui_result_chooser(UI_NO_UNIT, 0) == 0);
}

static void test_story(void) {
    const char *k1[] = { "CHOICE_A", "CHOICE_B", "CHOICE_C" }, *k2[] = { "CHOICE_A", "CHOICE_BC" }, *k3[] = { "CHOICE_AC", "HOICE_B" };
    uint32_t n1 = ui_story_node_hash(k1, 3), n2 = ui_story_node_hash(k2, 2), n3 = ui_story_node_hash(k3, 2);
    CHECK(n1 && n2 && n1 != n2 && n2 != n3);
    ui_story s; ui_story_reset(&s, 0, -1);
    /* chooser mode: only player 1 decides */
    CHECK(ui_story_vote(&s, 0, n1, 2, 0x7, 1) == -1);
    CHECK(s.node == n1 && s.chooser == 1);
    CHECK(ui_story_vote(&s, 2, n1, 0, 0x7, 1) == -1);
    CHECK(ui_story_vote(&s, 1, n1, 1, 0x7, 1) == 1);
    CHECK(ui_story_vote(&s, 1, n1, 2, 0x7, 1) == 1);          /* decided stays */
    /* next node, vote mode, 3 players: majority */
    CHECK(ui_story_vote(&s, 1, n2, 0, 0x7, -1) == -1);
    CHECK(s.node == n2 && s.chooser == -1 && s.decided == -1);
    CHECK(ui_story_votes_in(&s, 0x7) == 1);
    CHECK(ui_story_vote(&s, 0, n2, 1, 0x7, -1) == -1);
    CHECK(ui_story_vote(&s, 1, n2, 1, 0x7, -1) == -1);        /* player 1 changes their vote */
    CHECK(ui_story_vote(&s, 2, n2, 0, 0x7, -1) == 1);         /* 1,1,0 -> 1 */
    /* tie of 2: host's choice wins */
    ui_story_reset(&s, n3, -1);
    CHECK(ui_story_vote(&s, 1, n3, 0, 0x3, -1) == -1);
    CHECK(ui_story_vote(&s, 0, n3, 1, 0x3, -1) == 1);
    /* 4 players, 1-1 tie between choices 2 and 3, host in the tie */
    ui_story_reset(&s, n1, -1);
    ui_story_vote(&s, 0, n1, 3, 0xF, -1); ui_story_vote(&s, 1, n1, 2, 0xF, -1); ui_story_vote(&s, 2, n1, 2, 0xF, -1);
    CHECK(ui_story_vote(&s, 3, n1, 3, 0xF, -1) == 3);
    /* 5 players: tie 2-2 between 1 and 2, host voted 0 (not tied) -> lowest tied index */
    ui_story_reset(&s, n1, -1);
    int c5[5] = {0, 1, 1, 2, 2}, r = -1;
    for (int i = 0; i < 5; i++) r = ui_story_vote(&s, i, n1, c5[i], 0x1F, -1);
    CHECK(r == 1);
    /* a player leaves while the others have voted: recheck decides */
    ui_story_reset(&s, n2, -1);
    ui_story_vote(&s, 0, n2, 0, 0x7, -1); ui_story_vote(&s, 1, n2, 1, 0x7, -1);
    CHECK(s.decided == -1);
    CHECK(ui_story_recheck(&s, 0x3) == 0);                    /* 1-1 tie, host's 0 */
    /* absent player's vote is ignored */
    ui_story_reset(&s, n2, -1);
    CHECK(ui_story_vote(&s, 5, n2, 1, 0x3, -1) == -1);
    ui_story_vote(&s, 0, n2, 0, 0x3, -1);
    CHECK(ui_story_vote(&s, 1, n2, 0, 0x3, -1) == 0);
    /* bad input */
    CHECK(ui_story_vote(&s, 40, n1, 0, 0x3, -1) == 0);        /* ignored, keeps the decision */
    CHECK(ui_story_vote(&s, 0, 0, 0, 0x3, -1) == 0);
    /* wire */
    uint8_t a[UI_STORY_ARGS]; uint32_t node; int ch;
    ui_story_args_write(a, n1, 7);
    CHECK(ui_story_args_read(a, sizeof a, &node, &ch) == 0 && node == n1 && ch == 7);
    CHECK(ui_story_args_read(a, 4, &node, &ch) == -1);
    a[4] = 16; CHECK(ui_story_args_read(a, sizeof a, &node, &ch) == -1);
    /* every peer that sees the same commands in the same order reaches the same decision */
    for (int seed = 1; seed < 200; seed++) {
        ui_story x, y; ui_story_reset(&x, 0, -1); ui_story_reset(&y, 99, 2);  /* different local starting state */
        unsigned r2 = (unsigned)seed * 2654435761u; int dx = -1, dy = -1;
        for (int k = 0; k < 12; k++) {
            r2 = r2 * 1103515245u + 12345u;
            int pl = (int)(r2 >> 8) % 4, choice = (int)(r2 >> 16) % 3;
            dx = ui_story_vote(&x, pl, n3, choice, 0xF, -1);
            dy = ui_story_vote(&y, pl, n3, choice, 0xF, -1);
        }
        CHECK(dx == dy);
    }
}

static void test_mapvote(void) {
    ui_mapvote m; ui_mapvote_clear(&m);
    const uint8_t ord[3] = {9, 8, 7};
    /* 3 players: proposer 1 accepts, 0 declines, 2 accepts -> go */
    CHECK(ui_mapvote_propose(&m, 77, 1, UI_MAP_NEXT_FLOOR, ord, 3, 0x7));
    CHECK(ui_mapvote_open(&m) && m.order_len == 3 && m.order[2] == 7);
    CHECK(!ui_mapvote_propose(&m, 78, 2, UI_MAP_RETURN_BASE, ord, 3, 0x7));   /* one at a time */
    CHECK(ui_mapvote_vote(&m, 1, 77, 0, 0x7) == -1 && m.vote[1] == 1);        /* proposer cannot decline */
    CHECK(ui_mapvote_vote(&m, 0, 77, 0, 0x7) == -1);
    CHECK(ui_mapvote_vote(&m, 2, 99, 1, 0x7) == -1);                          /* wrong id ignored */
    CHECK(ui_mapvote_vote(&m, 2, 77, 1, 0x7) == 1);
    CHECK(!ui_mapvote_open(&m));
    CHECK(ui_mapvote_vote(&m, 0, 77, 1, 0x7) == 1);                           /* decided stays */
    /* 2 players, tie 1-1: host's vote wins (host declines) */
    CHECK(ui_mapvote_propose(&m, 80, 1, UI_MAP_ENTER_DUNGEON, NULL, 0, 0x3));
    CHECK(ui_mapvote_vote(&m, 0, 80, 0, 0x3) == 0);
    /* host proposes, 4 players: 2 yes (host + 1), 2 no -> tie, host's yes */
    CHECK(ui_mapvote_propose(&m, 81, 0, UI_MAP_RETURN_BASE, NULL, 0, 0xF));
    ui_mapvote_vote(&m, 1, 81, 1, 0xF); ui_mapvote_vote(&m, 2, 81, 0, 0xF);
    CHECK(ui_mapvote_vote(&m, 3, 81, 0, 0xF) == 1);
    /* alone: decided at once */
    CHECK(ui_mapvote_propose(&m, 82, 0, UI_MAP_NEXT_FLOOR, NULL, 0, 0x1) && m.decided == 1);
    /* a non-voter leaves: recheck decides */
    CHECK(ui_mapvote_propose(&m, 83, 2, UI_MAP_NEXT_FLOOR, NULL, 0, 0x7));
    ui_mapvote_vote(&m, 0, 83, 0, 0x7);
    CHECK(m.decided == -1);
    CHECK(ui_mapvote_recheck(&m, 0x5) == 0);                                  /* 1-1, host said no */
    /* too big */
    static uint8_t big[UI_MAP_MAX_ORDER + 1];
    CHECK(!ui_mapvote_propose(&m, 84, 0, 1, big, sizeof big, 0x1));
    /* wire */
    uint8_t w[64]; uint32_t id; int kind, acc; const uint8_t *o; size_t len;
    size_t n = ui_map_propose_write(w, sizeof w, 0xABCDEF01u, UI_MAP_RETURN_BASE, ord, 3);
    CHECK(n == 10 && ui_map_propose_read(w, n, &id, &kind, &o, &len) == 0 && id == 0xABCDEF01u && kind == 2 && len == 3 && o[0] == 9);
    CHECK(ui_map_propose_read(w, n - 1, &id, &kind, &o, &len) == -1);
    CHECK(ui_map_propose_write(w, 8, 1, 1, ord, 3) == 0);
    ui_map_vote_write(w, 5, 1); CHECK(ui_map_vote_read(w, 5, &id, &acc) == 0 && id == 5 && acc == 1);
    ui_map_vote_write(w, 6, UI_MAP_CANCEL); CHECK(ui_map_vote_read(w, 5, &id, &acc) == 0 && id == 6 && acc == UI_MAP_CANCEL);
    w[4] = 7; CHECK(ui_map_vote_read(w, 5, &id, &acc) == 0 && acc == 1);
    CHECK(ui_map_vote_read(w, 4, &id, &acc) == -1);
    CHECKS(ui_map_kind_text(UI_MAP_NEXT_FLOOR), "go down to the next floor");
    CHECKS(ui_map_kind_text(UI_MAP_RETURN_BASE), "return to the settlement");
    { char k[64]; CHECKS(ui_map_kind_text_floor(UI_MAP_NEXT_FLOOR, 3, k, sizeof k), "go down to floor 3");
      CHECKS(ui_map_kind_text_floor(UI_MAP_RETURN_BASE, 3, k, sizeof k), "return to the settlement");
      CHECKS(ui_map_kind_text_floor(UI_MAP_NEXT_FLOOR, 0, k, sizeof k), "go down to the next floor"); }
    /* cancel (60 s timeout, review #5): only the proposer or the host; decides "no" on every peer */
    { ui_players q; memset(&q, 0, sizeof q); strcpy(q.name[0], "The owner"); strcpy(q.name[1], "Ben"); strcpy(q.name[2], "Cy");
      char wl[128];
      CHECK(ui_mapvote_propose(&m, 90, 1, UI_MAP_NEXT_FLOOR, NULL, 0, 0x7));
      CHECK(ui_mapvote_wait_line(&m, &q, 1, 0x7, wl, sizeof wl) == 1); CHECKS(wl, "Waiting for the owner, Cy (1 of 3 agree)");
      CHECK(ui_mapvote_wait_line(&m, &q, 0, 0x7, wl, sizeof wl) == 0 && !wl[0]);          /* not the proposer */
      ui_mapvote_vote(&m, 0, 90, 1, 0x7);
      CHECK(ui_mapvote_wait_line(&m, &q, 1, 0x7, wl, sizeof wl) == 1); CHECKS(wl, "Waiting for Cy (2 of 3 agree)");
      CHECK(ui_mapvote_vote(&m, 2, 90, UI_MAP_CANCEL, 0x7) == -1 && m.decided == -1);       /* a voter cannot cancel */
      CHECK(ui_mapvote_vote(&m, 1, 90, UI_MAP_CANCEL, 0x7) == 0 && m.cancelled == UI_MAP_CANCEL);
      CHECK(ui_mapvote_wait_line(&m, &q, 1, 0x7, wl, sizeof wl) == 0);
      CHECK(ui_mapvote_propose(&m, 91, 1, UI_MAP_NEXT_FLOOR, NULL, 0, 0x7) && !m.cancelled);
      CHECK(ui_mapvote_vote(&m, 0, 91, UI_MAP_CANCEL, 0x7) == 0 && m.cancelled == UI_MAP_CANCEL);     /* the host may */
      char r[160];
      ui_mapvote_result(&m, &q, 0x7, 2, r, sizeof r); CHECKS(r, "No answer in 60 s: the vote was cancelled");
      CHECK(ui_mapvote_propose(&m, 92, 1, UI_MAP_NEXT_FLOOR, NULL, 0, 0x7));
      ui_mapvote_marks(&m, &q, 0x7, "\u2713", r, sizeof r); CHECKS(r, "<color=#F2C14E>The owner</color> \u2026   <color=#5DADE2>Ben</color> <color=#71ab40>\u2713</color>   <color=#E88BD4>Cy</color> \u2026");
      CHECK(ui_mapvote_vote(&m, 1, 92, UI_MAP_CANCEL_USER, 0x7) == 0);
      ui_mapvote_result(&m, &q, 0x7, 2, r, sizeof r); CHECKS(r, "Ben cancelled the vote");
      CHECK(ui_mapvote_propose(&m, 93, 1, UI_MAP_NEXT_FLOOR, NULL, 0, 0x3));
      CHECK(ui_mapvote_vote(&m, 0, 93, 0, 0x3) == 0);                                          /* 1-1 tie: host said no */
      ui_mapvote_result(&m, &q, 0x3, 2, r, sizeof r); CHECKS(r, "The owner said no: staying on floor 2");
      CHECK(ui_mapvote_propose(&m, 94, 1, UI_MAP_NEXT_FLOOR, NULL, 0, 0x3));
      CHECK(ui_mapvote_vote(&m, 0, 94, 1, 0x3) == 1);
      ui_mapvote_result(&m, &q, 0x3, 2, r, sizeof r); CHECKS(r, "Everyone agreed: going down");
      CHECKS(ui_map_question(UI_MAP_NEXT_FLOOR, 3, r, sizeof r), "Go down to floor 3?");
      CHECKS(ui_map_question(UI_MAP_RETURN_BASE, 0, r, sizeof r), "Return to the settlement?");
      uint8_t w2[5]; uint32_t i2; int a2; ui_map_vote_write(w2, 9, UI_MAP_CANCEL_USER);
      CHECK(ui_map_vote_read(w2, 5, &i2, &a2) == 0 && a2 == UI_MAP_CANCEL_USER); }
    /* loading barrier line */
    ui_players p; memset(&p, 0, sizeof p); strcpy(p.name[2], "C");
    ui_votes v; memset(&v, 0, sizeof v); v.present = 0x7; v.allow_pause = 1; v.allow_speedup = 1;
    v.busy = 0x6; v.pause[0] = 1;
    char line[128];
    ui_status_line(UI_NET_PLAYING, &v, &p, 0, line, sizeof line); CHECKS(line, "Waiting for Player 2, C (loading)");
    ui_status_line(UI_NET_PLAYING, &v, &p, 2, line, sizeof line); CHECKS(line, "Waiting for Player 2 (loading)");
    v.busy = 0x4;
    ui_status_line(UI_NET_PLAYING, &v, &p, 2, line, sizeof line); CHECKS(line, "Paused by Host");   /* own loading not shown */
    v.busy = 0x8;                                                             /* absent player */
    ui_status_line(UI_NET_PLAYING, &v, &p, 0, line, sizeof line); CHECKS(line, "Paused by Host");
}

static void test_story_rules(void) {
    CHECK(ui_story_chooser_rule(UI_EVC_OWNER, 2) == 2 && ui_story_chooser_rule(UI_EVC_OWNER, UI_UNCLAIMED) == 0 && ui_story_chooser_rule(UI_EVC_OWNER, UI_NO_UNIT) == 0);
    CHECK(ui_story_chooser_rule(UI_EVC_HOST, 2) == 0 && ui_story_chooser_rule(UI_EVC_FIRST, 2) == UI_STORY_FIRST && ui_story_chooser_rule(UI_EVC_MAJORITY, 2) == UI_STORY_VOTE);
    ui_story s;
    ui_story_reset(&s, 0, -1);
    CHECK(ui_story_vote(&s, 2, 77, 1, 0x7, UI_STORY_FIRST) == 1);                 /* first answer decides */
    CHECK(ui_story_vote(&s, 0, 77, 0, 0x7, UI_STORY_FIRST) == 1);                 /* later answers change nothing */
    ui_story_reset(&s, 0, -1);
    CHECK(ui_story_vote(&s, 1, 78, 2, 0x7, UI_STORY_VOTE) == -1);                 /* majority: waits for all */
    CHECK(ui_story_close(&s, 0x7) == 2);                                           /* timeout: the votes in decide */
    ui_story_reset(&s, 0, -1);
    CHECK(ui_story_vote(&s, 1, 79, 2, 0x7, UI_STORY_VOTE) == -1);
    CHECK(ui_story_vote(&s, 0, 79, 1, 0x7, UI_STORY_VOTE) == -1);
    CHECK(ui_story_close(&s, 0x7) == 1);                                           /* 1-1 tie: the host's choice */
    ui_story_reset(&s, 0, -1); s.node = 80; s.chooser = UI_STORY_VOTE;
    CHECK(ui_story_close(&s, 0x7) == -1);                                          /* nobody voted: still open */
}

static void test_view(void) {
    int own[6] = {0, 1, -1, 1, 0, 1}, out[6];
    CHECK(ui_local_order(own, 6, 1, out) == 6);
    CHECK(out[0] == 1 && out[1] == 3 && out[2] == 5 && out[3] == 0 && out[4] == 2 && out[5] == 4);
    CHECK(ui_local_order(own, 6, 0, out) == 6 && out[0] == 0 && out[1] == 4 && out[2] == 1);
    CHECK(ui_local_order(own, 6, 3, out) == 6 && out[0] == 0 && out[5] == 5);       /* owns nothing: game order */
    ui_players p; memset(&p, 0, sizeof p); strcpy(p.name[1], "B");
    char l[128];
    ui_pause_line(&p, 1, 0, l, sizeof l); CHECKS(l, "Paused by B");
    ui_pause_line(&p, 0, 1, l, sizeof l); CHECKS(l, "Paused by Host");
    ui_pause_line(&p, -1, 1, l, sizeof l); CHECKS(l, "Paused for a story event");
    ui_pause_line(&p, -1, 0, l, sizeof l); CHECKS(l, "Paused");
    strcpy(p.name[0], "The owner");
    ui_resync_text(&p, 1, 0, l, sizeof l); CHECKS(l, "B's game went out of sync: reloading everyone from the owner's save");
    ui_resync_text(&p, 1, 1, l, sizeof l); CHECKS(l, "Your game went out of sync: reloading everyone from the owner's save");
    ui_owner_tag_world(&p, 1, "\u25CF", l, sizeof l); CHECKS(l, " <color=#5DADE2>\u25CF B</color>");
    ui_owner_tag_world(&p, 1, NULL, l, sizeof l); CHECKS(l, " <color=#5DADE2>B</color>");
    ui_owner_tag_world(&p, -1, "\u25CF", l, sizeof l); CHECKS(l, "");
    p.name[0][0] = 0;
    ui_player_row(&p, 1, 0, 42, 0, l, sizeof l); CHECKS(l, "<color=#5DADE2>\u25CF</color> B  42 ms  in sync");
    ui_player_row(&p, 0, 0, 5, 0, l, sizeof l); CHECKS(l, "<color=#F2C14E>\u25CF</color> Host (you)    in sync");
    ui_player_row(&p, 1, 0, -1, 2, l, sizeof l); CHECK(strstr(l, "out of sync") && strstr(l, " -  "));
}

static void test_live(void) {
    ui_settings s; ui_settings_default(&s);
    CHECK(s.see_through == 0 && s.names == UI_NAMES_OTHERS && !s.list_open);
    ui_settings_parse("{\"see_through\":50,\"names\":2,\"list_open\":1}", &s);
    CHECK(s.see_through == 50 && s.names == UI_NAMES_ALL && s.list_open);
    ui_settings_parse("{\"see_through\":500,\"names\":9}", &s);          /* bad values ignored */
    CHECK(s.see_through == 50 && s.names == UI_NAMES_ALL);
    char j[128]; ui_settings_format(&s, j, sizeof j);
    ui_settings t; ui_settings_default(&t); ui_settings_parse(j, &t);
    CHECK(t.see_through == 50 && t.names == UI_NAMES_ALL && t.list_open == 1);
    { ui_settings u; ui_settings_default(&u); u.pos_set = 1; u.pos_x = -480; u.pos_y = -12; char b2[200]; ui_settings_format(&u, b2, sizeof b2);
      ui_settings w; ui_settings_default(&w); ui_settings_parse(b2, &w); CHECK(w.pos_set == 1 && w.pos_x == -480 && w.pos_y == -12);
      ui_settings_parse("{\"pos_y\":50}", &w); CHECK(w.pos_y == -12); }   /* above the top edge: ignored */
    { ui_settings u; ui_settings_default(&u); CHECK(u.tooltips == 1); u.tooltips = 0; char b3[200]; ui_settings_format(&u, b3, sizeof b3);
      ui_settings w; ui_settings_default(&w); ui_settings_parse(b3, &w); CHECK(w.tooltips == 0); }
    CHECK(ui_see_through_next(0) == 25 && ui_see_through_next(75) == 100 && ui_see_through_next(100) == 0 && ui_see_through_next(30) == 50);
    CHECK(ui_name_tag_visible(UI_NAMES_OTHERS, 1, 0) && !ui_name_tag_visible(UI_NAMES_OTHERS, 0, 0));
    CHECK(ui_name_tag_visible(UI_NAMES_ALL, 0, 0) && !ui_name_tag_visible(UI_NAMES_OFF, 1, 0) && !ui_name_tag_visible(UI_NAMES_ALL, -1, 0));
    /* select all with 2 mine, 2 of player 1, 1 enemy, 1 unclaimed (player 1 is me) */
    int isp[6] = {1, 1, 1, 1, 0, 1}, own[6] = {1, 1, 0, 0, -1, -1}, keep[6];
    CHECK(ui_filter_selection(isp, own, 6, 1, 0, keep) == 3);
    CHECK(keep[0] && keep[1] && !keep[2] && !keep[3] && keep[4] && !keep[5]);
    CHECK(ui_filter_selection(isp, own, 6, 0, 0, keep) == 4 && keep[5] && keep[2] && !keep[0]);   /* host: unclaimed is mine */
    int isp2[2] = {1, 1}, own2[2] = {0, 0};
    CHECK(ui_filter_selection(isp2, own2, 2, 1, 0, keep) == 2);                    /* none of mine: view only */
    CHECK(ui_filter_selection(isp2, own2, 1, 1, 0, keep) == 1);                    /* single: kept */
    ui_players p; memset(&p, 0, sizeof p); strcpy(p.name[0], "The owner"); strcpy(p.name[1], "B");
    char r[512];
    ui_player_row2(&p, 0, 0, 5, 0, r, sizeof r); CHECKS(r, "<color=#F2C14E>The owner</color>");
    ui_player_row2(&p, 1, 0, 42, 0, r, sizeof r); CHECKS(r, "<color=#5DADE2>B</color>  42 ms");
    ui_player_row2(&p, 1, 0, 42, 2, r, sizeof r); CHECK(strstr(r, "out of sync") != NULL);
    ui_list_header(2, 0, r, sizeof r); CHECKS(r, "Players (2)");
    { ui_players q; memset(&q, 0, sizeof q); strcpy(q.name[0], "The owner"); strcpy(q.name[1], "Ben");
      char L[128], R[256];
      ui_player_cols(&q, 0, 1, 10, 0, "\u25CF", L, sizeof L, R, sizeof R); CHECKS(L, "<color=#F2C14E>\u25CF</color> The owner"); CHECKS(R, "host");
      ui_player_cols(&q, 1, 1, 10, 0, NULL, L, sizeof L, R, sizeof R); CHECKS(L, "<color=#5DADE2>\u2022</color> Ben"); CHECKS(R, "");
      ui_player_cols(&q, 1, 0, 130, 0, "\u25CF", L, sizeof L, R, sizeof R); CHECKS(R, "");      /* no ms, no bar glyphs */
      CHECK(ui_conn_level(30, 0, 0) == 3 && ui_conn_level(119, 39, 0) == 3 && ui_conn_level(120, 0, 0) == 2 && ui_conn_level(250, 0, 0) == 2);
      CHECK(ui_conn_level(30, 40, 0) == 2 && ui_conn_level(251, 0, 0) == 1 && ui_conn_level(30, 0, 1) == 1 && ui_conn_level(30, 0, 2) == 1);
      CHECK(ui_conn_level(-1, 0, -1) == 0);
      CHECKS(ui_conn_color(3), "#71ab40"); CHECKS(ui_conn_color(2), "#ea9f3d"); CHECKS(ui_conn_color(1), "#c82e48");
      ui_player_cols(&q, 1, 0, 10, 2, "\u25CF", L, sizeof L, R, sizeof R); CHECKS(R, "<color=#f16071>out of sync</color>");
      ui_player_cols(&q, 0, 1, 10, 1, "\u25CF", L, sizeof L, R, sizeof R); CHECKS(R, "loading"); }
    /* admin mode: host keeps every player unit */
    CHECK(ui_filter_selection(isp, own, 6, 0, 1, keep) == 6);
    CHECK(ui_may_order_admin(0, 1, 1) && !ui_may_order_admin(0, 1, 0) && !ui_may_order_admin(1, 0, 1) && ui_may_order_admin(1, 1, 0));
    /* signal bars */
    CHECK(ui_signal_bars(10, 0) == 4 && ui_signal_bars(60, 0) == 3 && ui_signal_bars(119, 0) == 3 && ui_signal_bars(150, 0) == 2 && ui_signal_bars(250, 0) == 1);
    CHECK(ui_signal_bars(20, 2) == 0 && ui_signal_bars(-1, -1) == 0);
    ui_player_row3(&p, 1, 0, 42, 0, r, sizeof r);
    CHECK(!strncmp(r, "2  <color=#5DADE2>B</color>  ", 29) && strstr(r, "#58D68D") && !strstr(r, "#5D6D7E"));
    ui_player_row3(&p, 1, 0, 150, 0, r, sizeof r); CHECK(strstr(r, "#5D6D7E") != NULL);
    ui_player_row3(&p, 1, 0, 5, 2, r, sizeof r); CHECK(strstr(r, ">X<") != NULL);
    ui_player_row3(&p, 0, 0, 5, 0, r, sizeof r); CHECKS(r, "1  <color=#F2C14E>The owner</color>  ");
}

static void test_address(void) {
    char h[64]; uint16_t p;
    CHECK(ui_parse_address(" 192.168.1.5:7777 ", h, sizeof h, &p, 1) == 0 && !strcmp(h, "192.168.1.5") && p == 7777);
    CHECK(ui_parse_address("myhost", h, sizeof h, &p, 7777) == 0 && !strcmp(h, "myhost") && p == 7777);
    CHECK(ui_parse_address("[::1]:9000", h, sizeof h, &p, 1) == 0 && !strcmp(h, "::1") && p == 9000);
    CHECK(ui_parse_address("fe80::1", h, sizeof h, &p, 7777) == 0 && !strcmp(h, "fe80::1") && p == 7777);
    CHECK(ui_parse_address("host:0", h, sizeof h, &p, 1) == -1);
    CHECK(ui_parse_address("host:70000", h, sizeof h, &p, 1) == -1);
    CHECK(ui_parse_address("host:", h, sizeof h, &p, 1) == -1);
    CHECK(ui_parse_address("ho st:1", h, sizeof h, &p, 1) == -1);
    CHECK(ui_parse_address("", h, sizeof h, &p, 1) == -1);
    CHECK(ui_parse_address(":7777", h, sizeof h, &p, 1) == -1);
    CHECK(ui_parse_address("averyveryverylonghostname.example", h, 8, &p, 1) == -1);
}

int main(void) {
    test_claims(); test_votes(); test_popups(); test_story(); test_mapvote(); test_view(); test_story_rules(); test_live(); test_address();
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
    return fails != 0;
}
