/* teams_test: pure logic of the Teams screen (ui_teams_model.c). make -f Makefile.teams test */
#include "ui_teams_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void g_of(uint8_t g[16], int i) { memset(g, 0, 16); g[0] = (uint8_t)(i + 1); g[15] = 0xAB; }
static void setup(tm_state *s, int me, uint32_t present, int nunits) {
    memset(s, 0, sizeof *s);
    s->me = me; s->present = present; s->nunits = nunits;
    for (int i = 0; i < nunits; i++) { g_of(s->guid[i], i); s->owner[i] = TM_FREE; }
}

/* ---- reference "server" rules (what dsnet must do; same as the request sent to netcore) */
static void ref_ownership(tm_state *s, int issuer, int unit, int new_owner) {
    int cur = s->owner[unit];
    if (issuer != 0 && !((cur == TM_FREE || cur == issuer) && (new_owner == issuer || new_owner == TM_FREE))) return;
    s->owner[unit] = new_owner;
    if (cur >= 0 && cur != new_owner && tm_is_marked(s, cur, s->guid[unit])) {   /* lose the unit: lose the mark + ready */
        int n = s->nmarked[cur];
        for (int k = 0; k < n; k++)
            if (!memcmp(s->marked[cur][k], s->guid[unit], 16)) { memmove(s->marked[cur][k], s->marked[cur][k + 1], (size_t)(n - k - 1) * 16); n--; break; }
        s->nmarked[cur] = n;
        s->ready &= ~(1u << cur);
    }
}
static void ref_ready(tm_state *s, int issuer, const uint8_t *payload, size_t len) {
    int ready, n; uint8_t m[TM_MAX_MARK][16];
    if (tm_ready_read(payload, len, &ready, m, &n)) return;
    if (!tm_ready_valid(s, issuer, (const uint8_t (*)[16])m, n)) return;
    if (ready && n == 0) return;
    s->nmarked[issuer] = n; memcpy(s->marked[issuer], m, sizeof m);
    if (ready) s->ready |= 1u << issuer; else s->ready &= ~(1u << issuer);
}
/* run a local action through the reference server (what ui_teams.c would send) */
static void apply(tm_state *world, int issuer, tm_action a) {
    uint8_t buf[TM_READY_MAX_PAYLOAD]; size_t n;
    switch (a.kind) {
    case TM_CLAIM: ref_ownership(world, issuer, a.unit, issuer); break;
    case TM_RELEASE: ref_ownership(world, issuer, a.unit, TM_FREE); break;
    case TM_ASSIGN: ref_ownership(world, issuer, a.unit, a.target); break;
    case TM_MARKS: n = tm_ready_write(buf, sizeof buf, 0, (const uint8_t (*)[16])a.marks, a.nmarks); ref_ready(world, issuer, buf, n); break;
    case TM_READY: case TM_UNREADY:
        n = tm_ready_write(buf, sizeof buf, a.kind == TM_READY, (const uint8_t (*)[16])a.marks, a.nmarks); ref_ready(world, issuer, buf, n); break;
    default: break;
    }
}
static tm_state view(const tm_state *w, int me) { tm_state v = *w; v.me = me; v.admin = 1; return v; }

static void test_layout(void) {
    tm_state s; tm_column c[TM_MAX_PLAYERS + 2];
    setup(&s, 1, 0x5, 6);                  /* players 0 and 2 present; 1 left */
    s.owner[0] = 0; s.owner[1] = 2; s.owner[2] = 1; s.owner[4] = 2;
    int nc = tm_layout(&s, c, TM_MAX_PLAYERS + 2);
    CHECK(nc == 4);
    CHECK(c[0].player == 0 && c[0].n == 1 && c[0].unit[0] == 0);
    CHECK(c[1].player == 2 && c[1].n == 2 && c[1].unit[0] == 1 && c[1].unit[1] == 4);
    CHECK(c[2].player == 1 && c[2].n == 1);            /* left, still owns a unit */
    CHECK(c[3].player == TM_FREE && c[3].n == 2 && c[3].unit[0] == 3 && c[3].unit[1] == 5);
    int total = 0; for (int k = 0; k < nc; k++) total += c[k].n;
    CHECK(total == s.nunits);
    setup(&s, 0, 0x1, 0);
    nc = tm_layout(&s, c, 8);
    CHECK(nc == 2 && c[0].player == 0 && c[1].player == TM_FREE && c[1].n == 0);
}

static void test_player_clicks(void) {
    tm_state w; setup(&w, 0, 0x7, 8);
    tm_sel pk = TM_SEL_INIT;
    tm_state v = view(&w, 1);
    tm_action a = tm_click_unit(&v, 0, 0, &pk); CHECK(a.kind == TM_CLAIM); apply(&w, 1, a);
    CHECK(w.owner[0] == 1);
    for (int i = 1; i < 6; i++) { v = view(&w, 1); a = tm_click_unit(&v, i, 0, &pk); CHECK(a.kind == TM_CLAIM); apply(&w, 1, a); }
    for (int i = 0; i < 6; i++) CHECK(w.owner[i] == 1);    /* bench: no cap */
    v = view(&w, 2);
    a = tm_click_unit(&v, 0, 0, &pk); CHECK(a.kind == TM_DENY_OWNED && a.target == 1);
    a = tm_click_unit(&v, 0, 1, &pk); CHECK(a.kind == TM_DENY_NOT_OWN);
    /* marks: up to 3 */
    for (int i = 0; i < 3; i++) { v = view(&w, 1); a = tm_click_unit(&v, i, 1, &pk); CHECK(a.kind == TM_MARKS && a.nmarks == i + 1); apply(&w, 1, a); }
    CHECK(w.nmarked[1] == 3);
    v = view(&w, 1); a = tm_click_unit(&v, 3, 1, &pk); CHECK(a.kind == TM_DENY_FULL);
    a = tm_click_unit(&v, 1, 1, &pk); CHECK(a.kind == TM_MARKS && a.nmarks == 2); apply(&w, 1, a);   /* unmark the middle one */
    CHECK(w.nmarked[1] == 2 && tm_is_marked(&w, 1, w.guid[0]) && tm_is_marked(&w, 1, w.guid[2]) && !tm_is_marked(&w, 1, w.guid[1]));
    /* ready, then a mark change clears it */
    v = view(&w, 1); a = tm_click_ready(&v); CHECK(a.kind == TM_READY && a.nmarks == 2); apply(&w, 1, a);
    CHECK(w.ready == 0x2);
    v = view(&w, 1); a = tm_click_unit(&v, 4, 1, &pk); CHECK(a.kind == TM_MARKS); apply(&w, 1, a);
    CHECK(w.ready == 0 && w.nmarked[1] == 3);
    /* releasing a marked unit drops the mark and the ready flag */
    v = view(&w, 1); a = tm_click_ready(&v); apply(&w, 1, a); CHECK(w.ready == 0x2);
    v = view(&w, 1); a = tm_click_unit(&v, 0, 0, &pk); CHECK(a.kind == TM_RELEASE); apply(&w, 1, a);
    CHECK(w.owner[0] == TM_FREE && w.nmarked[1] == 2 && w.ready == 0);
    /* ready with no marks */
    v = view(&w, 2); a = tm_click_ready(&v); CHECK(a.kind == TM_DENY_NO_MARKS);
    /* forged marks are refused by the shared rule */
    uint8_t m[1][16]; memcpy(m[0], w.guid[3], 16);
    CHECK(!tm_ready_valid(&w, 2, (const uint8_t (*)[16])m, 1));
    CHECK(tm_ready_valid(&w, 1, (const uint8_t (*)[16])m, 1));
    uint8_t d[2][16]; memcpy(d[0], w.guid[3], 16); memcpy(d[1], w.guid[3], 16);
    CHECK(!tm_ready_valid(&w, 1, (const uint8_t (*)[16])d, 2));
}

static void test_host(void) {
    tm_state w; setup(&w, 0, 0x7, 5);
    tm_sel pk = TM_SEL_INIT;
    tm_state v = view(&w, 0);
    /* plain left-click: the host takes and frees like everyone (owner test 2026-10-13) */
    tm_action a = tm_click_unit(&v, 2, 0, &pk); CHECK(a.kind == TM_CLAIM && a.target == 0); apply(&w, 0, a);
    CHECK(w.owner[2] == 0);
    v = view(&w, 0); a = tm_click_unit(&v, 2, 1, &pk); CHECK(a.kind == TM_MARKS && a.nmarks == 1);   /* host can mark it */
    v = view(&w, 0); a = tm_click_unit(&v, 2, 0, &pk); CHECK(a.kind == TM_RELEASE); apply(&w, 0, a);
    CHECK(w.owner[2] == TM_FREE);
    /* target column: header, then units move there */
    v = view(&w, 0); a = tm_click_header(&v, 1, &pk); CHECK(a.kind == TM_TARGET && a.target == 1 && pk.target == 1);
    a = tm_click_unit(&v, 2, 0, &pk); CHECK(a.kind == TM_ASSIGN && a.unit == 2 && a.target == 1); apply(&w, 0, a);
    v = view(&w, 0); a = tm_click_unit(&v, 3, 0, &pk); CHECK(a.kind == TM_ASSIGN && a.target == 1); apply(&w, 0, a);
    CHECK(w.owner[2] == 1 && w.owner[3] == 1);
    v = view(&w, 0); a = tm_click_unit(&v, 3, 0, &pk); CHECK(a.kind == TM_NONE);           /* already there */
    a = tm_click_header(&v, 1, &pk); CHECK(a.kind == TM_TARGET && pk.target == TM_NO_TARGET);   /* same header: off */
    /* pick another player's unit, drop it on a header */
    v = view(&w, 0); a = tm_click_unit(&v, 2, 0, &pk); CHECK(a.kind == TM_PICK && pk.picked == 2 && a.target == 1);
    a = tm_click_header(&v, TM_FREE, &pk); CHECK(a.kind == TM_ASSIGN && a.target == TM_FREE && pk.picked == -1); apply(&w, 0, a);
    CHECK(w.owner[2] == TM_FREE);
    v = view(&w, 0); a = tm_click_unit(&v, 3, 0, &pk); a = tm_click_unit(&v, 3, 0, &pk); CHECK(a.kind == TM_UNPICK && pk.picked == -1);
    v = view(&w, 0); a = tm_click_unit(&v, 3, 0, &pk); a = tm_click_header(&v, 1, &pk); CHECK(a.kind == TM_UNPICK);   /* same column */
    v = view(&w, 0); a = tm_click_unit(&v, 3, 0, &pk); a = tm_click_header(&v, 5, &pk); CHECK(a.kind == TM_NONE && pk.picked == -1);   /* absent */
    a = tm_click_header(&v, 5, &pk); CHECK(a.kind == TM_NONE && pk.target == TM_NO_TARGET);
    v = view(&w, 1); a = tm_click_header(&v, 1, &pk); CHECK(a.kind == TM_NONE);           /* not host */
    v = view(&w, 0); v.in_dungeon = 1;
    a = tm_click_unit(&v, 0, 0, &pk); CHECK(a.kind == TM_DENY_LOCKED);
    a = tm_click_ready(&v); CHECK(a.kind == TM_DENY_LOCKED);
    a = tm_click_header(&v, 0, &pk); CHECK(a.kind == TM_DENY_LOCKED);
}

static void test_gate(void) {
    tm_state w; setup(&w, 0, 0x7, 6);
    w.owner[0] = 0; w.owner[1] = 1; w.owner[2] = 1;           /* player 2 owns nothing: still waited for (dsnet rule) */
    CHECK(!tm_all_ready(&w) && tm_waiting_mask(&w) == 0x7);
    w.present = 0x3;
    tm_sel pk = TM_SEL_INIT; tm_state v; tm_action a;
    v = view(&w, 0); a = tm_click_unit(&v, 0, 1, &pk); apply(&w, 0, a);
    v = view(&w, 0); a = tm_click_ready(&v); apply(&w, 0, a);
    CHECK(!tm_all_ready(&w) && tm_waiting_mask(&w) == 0x2);
    v = view(&w, 1); a = tm_click_unit(&v, 1, 1, &pk); apply(&w, 1, a);
    v = view(&w, 1); a = tm_click_unit(&v, 2, 1, &pk); apply(&w, 1, a);
    v = view(&w, 1); a = tm_click_ready(&v); apply(&w, 1, a);
    CHECK(tm_all_ready(&w));
    uint8_t party[9][16]; int n = tm_party(&w, party, 9);
    CHECK(n == 3 && !memcmp(party[0], w.guid[0], 16) && !memcmp(party[1], w.guid[1], 16) && !memcmp(party[2], w.guid[2], 16));
    w.present = 0x7;                                          /* player 2 joins with no unit: blocks */
    CHECK(!tm_all_ready(&w) && tm_waiting_mask(&w) == 0x4);
    w.present = 0x1;                                          /* player 1 leaves: only the host's party */
    CHECK(tm_all_ready(&w) && tm_party(&w, party, 9) == 1);
    w.ready = 0;
    CHECK(!tm_all_ready(&w));
}

static void test_wire(void) {
    uint8_t m[3][16], r[3][16], buf[TM_READY_MAX_PAYLOAD];
    for (int k = 0; k < 3; k++) g_of(m[k], k + 10);
    size_t n = tm_ready_write(buf, sizeof buf, 1, (const uint8_t (*)[16])m, 3);
    CHECK(n == 50);
    int ready, cnt;
    CHECK(tm_ready_read(buf, n, &ready, r, &cnt) == 0 && ready == 1 && cnt == 3 && !memcmp(r, m, sizeof m));
    CHECK(tm_ready_read(buf, n - 1, &ready, r, &cnt) != 0);
    buf[1] = 4; CHECK(tm_ready_read(buf, n, &ready, r, &cnt) != 0);
    CHECK(tm_ready_write(buf, 10, 1, (const uint8_t (*)[16])m, 3) == 0);
    CHECK(tm_ready_write(buf, sizeof buf, 0, (const uint8_t (*)[16])m, 0) == 2);
}

/* random clicks from 3 players through the reference server: invariants always hold */
static void test_random(void) {
    srand(7);
    for (int round = 0; round < 300; round++) {
        tm_state w; setup(&w, 0, 0x7, 1 + rand() % 12);
        tm_sel pk[3] = {TM_SEL_INIT, TM_SEL_INIT, TM_SEL_INIT};
        for (int step = 0; step < 200; step++) {
            int p = rand() % 3; tm_state v = view(&w, p);
            tm_action a; int r = rand() % 10;
            if (r < 6) a = tm_click_unit(&v, rand() % w.nunits, rand() % 2, &pk[p]);
            else if (r < 8) a = tm_click_header(&v, (rand() % 4) - 1, &pk[p]);
            else a = tm_click_ready(&v);
            apply(&w, p, a);
            for (int q = 0; q < 3; q++) {
                CHECK(w.nmarked[q] >= 0 && w.nmarked[q] <= TM_MAX_MARK);
                CHECK(tm_ready_valid(&w, q, (const uint8_t (*)[16])w.marked[q], w.nmarked[q]));
                if (w.ready >> q & 1u) CHECK(w.nmarked[q] >= 1);
            }
            uint8_t party[9][16]; CHECK(tm_party(&w, party, 9) <= 9);
        }
    }
}

static void test_reserved(void) {
    tm_state w; setup(&w, 1, 0x3, 3);
    w.owner[2] = TM_RESERVED;
    tm_column c[8]; int nc = tm_layout(&w, c, 8);
    CHECK(nc == 4 && c[2].player == TM_FREE && c[3].player == TM_RESERVED && c[3].n == 1 && c[3].unit[0] == 2);
    tm_sel pk = TM_SEL_INIT;
    tm_action a = tm_click_unit(&w, 2, 0, &pk); CHECK(a.kind == TM_DENY_OWNED && a.target == TM_RESERVED);
    tm_state h = w; h.me = 0; h.admin = 1;
    a = tm_click_unit(&h, 2, 0, &pk); CHECK(a.kind == TM_PICK && pk.picked == 2);
    a = tm_click_header(&h, 1, &pk); CHECK(a.kind == TM_ASSIGN && a.target == 1);
    h.admin = 0; pk = (tm_sel)TM_SEL_INIT;
    a = tm_click_unit(&h, 2, 0, &pk); CHECK(a.kind == TM_DENY_OWNED);
}

static void test_admin_off(void) {
    tm_state w; setup(&w, 0, 0x3, 3);
    w.owner[1] = 1;
    tm_sel pk = TM_SEL_INIT;
    tm_action a = tm_click_unit(&w, 1, 0, &pk); CHECK(a.kind == TM_DENY_OWNED && pk.picked == -1);   /* admin off: like a player */
    a = tm_click_unit(&w, 0, 0, &pk); CHECK(a.kind == TM_CLAIM);
    a = tm_click_header(&w, 1, &pk); CHECK(a.kind == TM_TARGET && pk.target == 1);          /* may still give FREE units */
    a = tm_click_unit(&w, 2, 0, &pk); CHECK(a.kind == TM_ASSIGN && a.target == 1);
    a = tm_click_unit(&w, 1, 0, &pk); CHECK(a.kind == TM_DENY_OWNED);                       /* but not another player's */
    a = tm_click_header(&w, TM_RESERVED, &pk); CHECK(a.kind == TM_NONE);
}

static void test_panel(void) {
    tm_state w; setup(&w, 1, 0x3, 8);
    w.owner[0] = 1; w.owner[1] = 0; w.owner[2] = 1;            /* 0, 2 mine; 1 host's; 3..7 free */
    tm_panel_res r; uint8_t pn[8][16];
    int ids[6] = {1, 0, 3, 2, 4, 5};                            /* the guest picked 6 in the stock panel */
    for (int i = 0; i < 6; i++) memcpy(pn[i], w.guid[ids[i]], 16);
    tm_panel_party(&w, (const uint8_t (*)[16])pn, 6, &r);
    CHECK(r.nmarks == 3 && !memcmp(r.marks[0], w.guid[0], 16) && !memcmp(r.marks[1], w.guid[3], 16) && !memcmp(r.marks[2], w.guid[2], 16));
    CHECK(r.nclaim == 1 && !memcmp(r.claim[0], w.guid[3], 16) && r.over == 2 && r.foreign == 1 && !r.kept_old);
    /* the shared rule accepts it once the claim ran */
    tm_state w2 = w; w2.owner[3] = 1;
    CHECK(tm_ready_valid(&w2, 1, (const uint8_t (*)[16])r.marks, r.nmarks));
    /* nothing of mine in the panel: keep my marks */
    memcpy(w.marked[1][0], w.guid[2], 16); w.nmarked[1] = 1;
    memcpy(pn[0], w.guid[1], 16);
    tm_panel_party(&w, (const uint8_t (*)[16])pn, 1, &r);
    CHECK(r.nmarks == 1 && r.kept_old && r.foreign == 1 && !memcmp(r.marks[0], w.guid[2], 16));
    memcpy(pn[1], w.guid[1], 16);                                /* duplicates count once */
    memcpy(pn[0], w.guid[4], 16); memcpy(pn[1], w.guid[4], 16);
    tm_panel_party(&w, (const uint8_t (*)[16])pn, 2, &r);
    CHECK(r.nmarks == 1 && r.nclaim == 1);
}

static void test_proposal(void) {
    tm_prop p = {0}; uint32_t pr = 0x3;                       /* host 0 + player 1 */
    CHECK(tm_prop_propose(&p, 2, 1, pr) == TM_PE_NONE && !p.active);                 /* absent player */
    CHECK(tm_prop_propose(&p, 1, 2, pr) == TM_PE_STARTED && p.active && p.proposer == 1 && p.region == 2);
    CHECK(tm_prop_propose(&p, 0, 3, pr) == TM_PE_NONE && p.proposer == 1 && p.region == 2);   /* one at a time */
    CHECK(tm_prop_propose(&p, 1, 4, pr) == TM_PE_UPDATED && p.region == 4);
    CHECK(tm_prop_answer(&p, 1, TM_ANS_DECLINE, pr) == TM_PE_NONE && p.active);       /* the proposer cancels, not declines */
    CHECK(tm_prop_answer(&p, 0, TM_ANS_CANCEL, pr) == TM_PE_NONE && p.active);        /* only the proposer cancels */
    CHECK(tm_prop_answer(&p, 1, TM_ANS_TIMEOUT, pr) == TM_PE_NONE && p.active);       /* only the host times out */
    CHECK(tm_prop_answer(&p, 0, TM_ANS_DECLINE, pr) == TM_PE_DECLINED && !p.active);
    CHECK(tm_prop_answer(&p, 0, TM_ANS_DECLINE, pr) == TM_PE_NONE);                   /* nothing active */
    tm_prop_propose(&p, 1, 0, pr);
    CHECK(tm_prop_answer(&p, 1, TM_ANS_CANCEL, pr) == TM_PE_CANCELLED && !p.active);
    tm_prop_propose(&p, 1, 0, pr);
    CHECK(tm_prop_answer(&p, 0, TM_ANS_TIMEOUT, pr) == TM_PE_TIMEOUT && !p.active);
    tm_prop_propose(&p, 1, 0, pr);
    CHECK(tm_prop_check(&p, 0x3) == TM_PE_NONE && p.active);
    CHECK(tm_prop_check(&p, 0x1) == TM_PE_PROPOSER_LEFT && !p.active);
    /* waiting = present players not ready; members = marked units of present players they still own */
    tm_state s; setup(&s, 0, 0x3, 6);
    s.owner[0] = 0; s.owner[1] = 1; s.owner[2] = 1; s.owner[3] = 1;
    memcpy(s.marked[0][0], s.guid[0], 16); s.nmarked[0] = 1;
    memcpy(s.marked[1][0], s.guid[1], 16); memcpy(s.marked[1][1], s.guid[2], 16); memcpy(s.marked[1][2], s.guid[4], 16); s.nmarked[1] = 3;
    CHECK(tm_party_members(&s) == 3);                                                 /* guid 4 is free: not counted */
    tm_prop_propose(&p, 1, 0, 0x3); s.ready = 0x2;
    CHECK(tm_prop_waiting(&p, &s) == 0x1);
    s.ready = 0x3; CHECK(tm_prop_waiting(&p, &s) == 0);
    p.active = 0; s.ready = 0; CHECK(tm_prop_waiting(&p, &s) == 0);
    /* the host rule "units per player" is the mark limit */
    tm_state m; setup(&m, 0, 0x1, 8); tm_sel sel = TM_SEL_INIT;
    for (int i = 0; i < 8; i++) m.owner[i] = 0;
    for (int lim = 1; lim <= 6; lim++) {
        if (lim == 5) continue;
        m.max_mark = lim; m.nmarked[0] = 0;
        for (int i = 0; i < 8; i++) {
            tm_action a = tm_click_unit(&m, i, 1, &sel);
            if (i < lim) { CHECK(a.kind == TM_MARKS && a.nmarks == i + 1); memcpy(m.marked[0], a.marks, sizeof a.marks); m.nmarked[0] = a.nmarks; }
            else CHECK(a.kind == TM_DENY_FULL);
        }
        CHECK(tm_ready_valid(&m, 0, (const uint8_t (*)[16])m.marked[0], lim) && !tm_ready_valid(&m, 0, (const uint8_t (*)[16])m.guid, lim + 1));
    }
    m.max_mark = 0; CHECK(tm_mark_limit(&m) == 3); m.max_mark = 9; CHECK(tm_mark_limit(&m) == TM_MAX_MARK);
}
static void test_region(void) {
    int pick[TM_MAX_PLAYERS]; for (int i = 0; i < TM_MAX_PLAYERS; i++) pick[i] = -1;
    CHECK(tm_region_vote(pick, 0x7) == -1);
    pick[1] = 3; CHECK(tm_region_vote(pick, 0x7) == 3);            /* one pick */
    pick[2] = 3; pick[0] = 1; CHECK(tm_region_vote(pick, 0x7) == 3);   /* majority beats the host */
    pick[2] = 2; CHECK(tm_region_vote(pick, 0x7) == 1);            /* 3-way tie: host's pick */
    pick[0] = -1; CHECK(tm_region_vote(pick, 0x7) == 2);           /* tie without the host: lowest */
    pick[0] = 4; pick[1] = 2; pick[2] = 3; pick[3] = 3; CHECK(tm_region_vote(pick, 0xF) == 3);
    CHECK(tm_region_vote(pick, 0x7) == 4);                          /* player 3 absent: 3-way tie -> host */
    pick[0] = 2; CHECK(tm_region_vote(pick, 0xE) == 3);             /* host absent: his pick does not count */
    int av[3] = {0, 2, 5};
    CHECK(tm_region_step(av, 3, 2, 1) == 5 && tm_region_step(av, 3, 5, 1) == 0 && tm_region_step(av, 3, 0, -1) == 5);
    CHECK(tm_region_step(av, 3, -1, 1) == 0 && tm_region_step(av, 3, 7, -1) == 5 && tm_region_step(av, 0, 4, 1) == 4);
}

int main(void) {
    test_proposal(); test_region(); test_panel(); test_admin_off(); test_reserved();
    test_layout(); test_player_clicks(); test_host(); test_gate(); test_wire(); test_random();
    printf("teams_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
