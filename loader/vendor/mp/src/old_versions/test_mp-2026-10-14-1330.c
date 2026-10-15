/* mp tests: mp_core + dsnet against a mock game (fake ticks, fake units), host + clients in one
 * process over TCP. Proves: same ticks everywhere, orders on the same step, ownership, speed and
 * pause votes (lobby on and off, popups), desync -> resync by save transfer, late join, mp.json. */
#include "../mp.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAXT 2048
#define NU 6
#define TICK_MS 5.0f

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; ok = 0; } } while (0)
static double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }
static void sleep_ms(double ms) { if (ms > 0) usleep((useconds_t)(ms * 1000)); }

/* ---------------------------------------------------------------- mock game */
typedef struct { uint8_t guid[16]; int32_t x, y, tx, ty, hp; } unit;
typedef struct { unit u[NU]; uint32_t rng; uint64_t tick; } world;

enum { A_MOVE = 1, A_PAUSE, A_SPEED, A_POPUP, A_CLAIM, A_CORRUPT, A_STORY, A_ADMIN, A_LEFT };
typedef struct { uint32_t at; int kind, a, b, c, done; } action;
typedef struct { uint32_t step; int unit, x, y; } applied;
typedef struct { uint32_t step; int speed, paused; } shown;

typedef struct peer {
    char name[16];
    world w;
    mp_session *s;
    pthread_t th; volatile int stop, done;
    uint32_t target;
    int in_scope, scope_bad, order_outside;
    int ready_in;                    /* frames until ready() after a load */
    int flow_every, flow_len, flow_left, flow_bad;   /* mock map transition: starts on tick % every, lasts len ready() calls */
    uint8_t have[MAXT]; uint64_t hash_at[MAXT]; uint64_t tick_at[MAXT];
    applied ap[512]; int nap;
    shown sh[256]; int nsh;
    action act[32]; int nact;
    int rand_orders; uint32_t r;
    uint32_t load_step; int loads;
    applied story[16]; int nstory;      /* story votes seen: step, player, guid[0], choice */
} peer;

static uint32_t xs(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }
static uint64_t fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001B3ULL; } return h; }
static void world_init(world *w) {
    memset(w, 0, sizeof *w);
    for (int i = 0; i < NU; i++) { memset(w->u[i].guid, 0x10 + i, 16); w->u[i].x = w->u[i].tx = i * 10; w->u[i].y = w->u[i].ty = 5; w->u[i].hp = 100; }
    w->rng = 0x12345678;
}

static int m_ready(void *u) { peer *p = u; if (p->ready_in > 0) { p->ready_in--; return 0; } if (p->flow_left > 0) { p->flow_left--; return 0; } return 1; }
static void m_tick(void *u) {
    world *w = &((peer *)u)->w;
    for (int i = 0; i < NU; i++) {
        unit *x = &w->u[i];
        x->x += (x->tx > x->x) - (x->tx < x->x);
        x->y += (x->ty > x->y) - (x->ty < x->y);
        if (xs(&w->rng) % 10 == 0) x->hp += (int)(xs(&w->rng) % 3) - 1;
    }
    w->tick++;
    peer *p = u;
    if (p->flow_every && w->tick % (uint64_t)p->flow_every == 0) p->flow_left = p->flow_len;
}
static void m_scope(void *u, int enter) { peer *p = u; if (p->in_scope == enter) p->scope_bad++; p->in_scope = enter; }
static uint64_t m_fp(void *u) {
    peer *p = u;
    uint64_t h = fnv(0xCBF29CE484222325ULL, &p->w, sizeof p->w);
    uint32_t t = mp_cur_step(p->s);
    if (t < MAXT) { p->have[t] = 1; p->hash_at[t] = h; p->tick_at[t] = p->w.tick; }
    return h;
}
static int m_move(void *u, const uint8_t g[16], int32_t x, int32_t y) {
    peer *p = u;
    if (!p->in_scope) p->order_outside++;
    if (p->flow_left > 0) p->flow_bad++;
    for (int i = 0; i < NU; i++) if (!memcmp(p->w.u[i].guid, g, 16)) {
        p->w.u[i].tx = x; p->w.u[i].ty = y;
        xs(&p->w.rng);                                       /* orders draw from the sim RNG */
        if (p->nap < 512) p->ap[p->nap++] = (applied){ mp_cur_step(p->s), i, x, y };
        return 0;
    }
    return -1;
}
static int m_save(void *u, uint8_t **d, size_t *n) {
    peer *p = u;
    if (p->flow_left > 0) p->flow_bad++;
    *d = malloc(sizeof p->w + 4096); *n = sizeof p->w + 4096;
    memcpy(*d, &p->w, sizeof p->w);
    for (int i = 0; i < 4096; i++) (*d)[sizeof p->w + i] = (uint8_t)(i * 13);   /* padding: save bigger than one fragment */
    return 0;
}
static int m_load(void *u, const uint8_t *d, size_t n) {
    peer *p = u;
    if (n != sizeof p->w + 4096) return -1;
    for (int i = 0; i < 4096; i++) if (d[sizeof p->w + i] != (uint8_t)(i * 13)) return -1;
    memcpy(&p->w, d, sizeof p->w);
    for (int t = 0; t < MAXT; t++) p->have[t] = 0;           /* history before the load no longer counts */
    p->ready_in = 20;                                        /* the real game loads for ~10-25 s */
    p->loads++;
    return 0;
}
static void m_show(void *u, int speed, int paused) {
    peer *p = u;
    if (p->nsh < 256) p->sh[p->nsh++] = (shown){ mp_cur_step(p->s), speed, paused };
}

/* global votes (types 3-5): record step, player, guid[0], 100 * type + choice */
static int vote_rec(mp_session *s, int type, int player, const uint8_t g[16], const uint8_t *a, uint16_t n) {
    peer *p = mp_api(s)->user;
    if (p->nstory < 16) p->story[p->nstory++] = (applied){ mp_cur_step(s), player, g[0], 100 * type + (n ? a[0] : 0) };
    return 0;
}
static int story_fn(mp_session *s, int pl, const uint8_t g[16], const uint8_t *a, uint16_t n) { return vote_rec(s, 3, pl, g, a, n); }
static int mapprop_fn(mp_session *s, int pl, const uint8_t g[16], const uint8_t *a, uint16_t n) { return vote_rec(s, 4, pl, g, a, n); }
static int mapvote_fn(mp_session *s, int pl, const uint8_t g[16], const uint8_t *a, uint16_t n) { return vote_rec(s, 5, pl, g, a, n); }
static int base_fn(mp_session *s, int pl, const uint8_t g[16], const uint8_t *a, uint16_t n) { return vote_rec(s, 7, pl, g, a, n); }
static void do_action(peer *p, action *a) {
    uint8_t g[16]; memset(g, 0x10 + a->a, 16);
    switch (a->kind) {
    case A_MOVE: mp_order_move(p->s, g, a->b, a->c); break;
    case A_PAUSE: mp_vote_pause(p->s, a->a); break;
    case A_SPEED: if (a->b) { uint8_t v = (uint8_t)a->a; dsnet_submit(mp_client(p->s), DSNET_SPEED_VOTE, 0, &v, 1); } else mp_vote_speed(p->s, a->a); break;
    case A_POPUP: mp_popup_pause(p->s, a->a, a->b); break;
    case A_CLAIM: mp_claim(p->s, g); break;
    case A_CORRUPT: p->w.u[2].hp ^= 1; break;
    case A_LEFT: mp_left_choice(p->s, a->a); break;
    case A_ADMIN: { uint8_t v = (uint8_t)a->a; dsnet_submit(mp_client(p->s), DSNET_ADMIN_MODE, 0, &v, 1); break; }
    case A_STORY: { uint8_t ch = (uint8_t)a->b; if (!(a->c & 1)) memset(g, 0, 16); mp_order(p->s, (uint16_t)(a->c >> 1 ? a->c >> 1 : MP_ORDER_STORY_VOTE), g, &ch, 1); break; }
    }
}

static void *loop(void *arg) {
    peer *p = arg;
    int frame = 0;
    while (!p->stop) {
        mp_frame(p->s, 0);
        mp_stats st; mp_get_stats(p->s, &st);
        for (int i = 0; i < p->nact; i++) if (!p->act[i].done && st.timer >= p->act[i].at) { do_action(p, &p->act[i]); p->act[i].done = 1; }
        if (p->rand_orders && ++frame % p->rand_orders == 0 && !st.needs_state && !st.loading) {
            uint8_t g[16]; memset(g, 0x10 + (int)(xs(&p->r) % NU), 16);
            mp_order_move(p->s, g, (int)(xs(&p->r) % 60), (int)(xs(&p->r) % 20));
        }
        if (st.timer >= p->target) p->done = 1;
        sleep_ms(2);
    }
    return NULL;
}

static mp_game_api mock_api(peer *p) {
    return (mp_game_api){ p, m_ready, m_tick, m_scope, m_fp, m_move, m_save, m_load, m_show };
}
static void act(peer *p, uint32_t at, int kind, int a, int b, int c) {
    int i = p->nact; p->act[i] = (action){ at, kind, a, b, c, 0 };
    __sync_synchronize(); p->nact = i + 1;                   /* safe while the peer's loop runs */
}
static uint32_t timer_of(peer *p) { mp_stats st; mp_get_stats(p->s, &st); return st.timer; }

static peer *peer_new(const char *name, uint32_t target) {
    peer *p = calloc(1, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", name);
    world_init(&p->w); p->target = target; p->r = 2463534242u ^ (uint32_t)(uintptr_t)p;
    return p;
}
static int join(peer *p, const mp_config *c) {
    mp_game_api a = mock_api(p);
    int why;
    p->s = mp_start(c, &a, &why);
    return p->s ? 0 : -1;
}
static mp_config host_cfg(int players, int allow_speedup, int allow_pause) {
    mp_config c; mp_config_defaults(&c);
    c.host = 1; c.port = 0; c.min_players = players; c.tick_ms = TICK_MS; c.fp_interval = 30;
    c.allow_speedup = allow_speedup; c.allow_pause = allow_pause;
    snprintf(c.name, sizeof c.name, "host");
    return c;
}
static mp_config client_cfg(uint16_t port, const char *name) {
    mp_config c; mp_config_defaults(&c);
    c.host = 0; snprintf(c.host_addr, sizeof c.host_addr, "127.0.0.1"); c.port = port;
    snprintf(c.name, sizeof c.name, "%s", name);
    return c;
}
static uint16_t mp_test_port;
static int setup(peer **ps, int n, uint32_t target, int allow_speedup, int allow_pause) {
    for (int i = 0; i < n; i++) { char nm[8]; snprintf(nm, sizeof nm, i ? "c%d" : "host", i); ps[i] = peer_new(nm, target); }
    mp_config hc = host_cfg(n, allow_speedup, allow_pause);
    if (join(ps[0], &hc)) return -1;
    mp_test_port = mp_server_port(ps[0]->s);
    for (int i = 1; i < n; i++) { mp_config cc = client_cfg(mp_test_port, ps[i]->name); if (join(ps[i], &cc)) return -1; }
    return 0;
}
static void go(peer **ps, int n) { for (int i = 0; i < n; i++) pthread_create(&ps[i]->th, NULL, loop, ps[i]); }
static int wait_done(peer **ps, int n, int timeout_ms) {
    double t0 = now_ms();
    for (;;) {
        int all = 1;
        for (int i = 0; i < n; i++) if (!ps[i]->done) all = 0;
        if (all) return 0;
        if (now_ms() - t0 > timeout_ms) return -1;
        sleep_ms(5);
    }
}
static void stop_all(peer **ps, int n) {
    for (int i = 0; i < n; i++) { ps[i]->stop = 1; pthread_join(ps[i]->th, NULL); }
    for (int i = n - 1; i >= 0; i--) { mp_stop(ps[i]->s); free(ps[i]); }
}
/* every step both have must match; returns common count, -1 on a mismatch */
static int compare(peer *a, peer *b, uint32_t *first_bad) {
    int common = 0;
    for (uint32_t t = 0; t < MAXT; t++) {
        if (!a->have[t] || !b->have[t]) continue;
        if (a->hash_at[t] != b->hash_at[t] || a->tick_at[t] != b->tick_at[t]) { if (first_bad) *first_bad = t; return -1; }
        common++;
    }
    return common;
}
/* orders applied before step `upto` (peers keep running a little after the target) */
static int same_applied(peer *a, peer *b, uint32_t upto) {
    int i = 0, j = 0;
    for (;;) {
        while (i < a->nap && a->ap[i].step >= upto) i++;
        while (j < b->nap && b->ap[j].step >= upto) j++;
        if (i == a->nap || j == b->nap) return i == a->nap && j == b->nap;
        if (memcmp(&a->ap[i], &b->ap[j], sizeof(applied))) return 0;
        i++; j++;
    }
}
static int ticks_in(peer *p, uint32_t t) { return t > 0 && p->have[t] && p->have[t - 1] ? (int)(p->tick_at[t] - p->tick_at[t - 1]) : -1; }

/* ---- 1. host + 2 clients: same ticks, orders on the same step, ownership ---- */
static void test_orders(void) {
    int ok = 1; double t0 = now_ms();
    peer *ps[3];
    if (setup(ps, 3, 500, 1, 1)) { printf("  FAIL setup\n"); failures++; return; }
    act(ps[1], 30, A_CLAIM, 0, 0, 0);                 /* c1 owns unit 0 */
    act(ps[2], 80, A_MOVE, 0, 70, 9);                 /* c2 orders c1's unit: dropped everywhere */
    act(ps[1], 100, A_MOVE, 0, 65, 2);                /* owner's order: applied */
    act(ps[2], 120, A_MOVE, 1, 71, 7);                /* unclaimed unit, not the host: dropped */
    act(ps[0], 130, A_MOVE, 1, 72, 7);                /* unclaimed unit, host: applied */
    act(ps[0], 140, A_MOVE, 2, 3, 3);
    act(ps[2], 150, A_STORY, 0, 2, 1);               /* story vote, guid of c1's unit: no owner check */
    act(ps[1], 160, A_STORY, 0, 1, 0);                /* story vote, zero guid */
    act(ps[2], 170, A_STORY, 0, 4, (MP_ORDER_MAP_PROPOSE << 1) | 1);   /* map-change propose, c1's unit guid */
    act(ps[2], 180, A_STORY, 0, 5, MP_ORDER_MAP_VOTE << 1);            /* map-change vote, zero guid */
    act(ps[2], 190, A_STORY, 0, 6, 7 << 1);                            /* base work (type 7), zero owner guid: global */
    act(ps[1], 200, A_STORY, 1, 8, (7 << 1) | 1);                      /* base work naming an unclaimed unit, non-host: dropped */
    for (int i = 0; i < 3; i++) {
        mp_set_order_handler(ps[i]->s, MP_ORDER_STORY_VOTE, story_fn);
        mp_set_order_handler(ps[i]->s, MP_ORDER_MAP_PROPOSE, mapprop_fn);
        mp_set_order_handler(ps[i]->s, MP_ORDER_MAP_VOTE, mapvote_fn);
        mp_set_order_handler(ps[i]->s, 7, base_fn);
    }
    for (int i = 0; i < 3; i++) ps[i]->rand_orders = 9;
    /* random orders may hit unit 0 too: those from c1 apply, others drop, identically on every peer */
    go(ps, 3);
    CHECK(wait_done(ps, 3, 20000) == 0, "timeout");
    mp_stats st[3];
    for (int i = 0; i < 3; i++) mp_get_stats(ps[i]->s, &st[i]);
    for (int i = 1; i < 3; i++) {
        uint32_t bad = 0; int common = compare(ps[0], ps[i], &bad);
        CHECK(common > 450, "%s: %d common steps (first mismatch %u)", ps[i]->name, common, bad);
        CHECK(same_applied(ps[0], ps[i], 500), "%s applied orders differ (%d vs %d)", ps[i]->name, ps[0]->nap, ps[i]->nap);
        CHECK(st[i].orders_dropped_owner >= 1, "%s dropped nothing", ps[i]->name);   /* exact equality: same_applied above (counts differ after the target) */
    }
    CHECK(st[0].orders_dropped_owner >= 1, "c2's order on c1's unit was not dropped");
    int found_owner = 0, found_c2 = 0;
    for (int k = 0; k < ps[0]->nap; k++) {
        if (ps[0]->ap[k].unit == 0 && ps[0]->ap[k].x == 65 && ps[0]->ap[k].y == 2) found_owner = 1;
        if (ps[0]->ap[k].unit == 0 && ps[0]->ap[k].x == 70 && ps[0]->ap[k].y == 9) found_c2 = 1;
    }
    CHECK(found_owner && !found_c2, "owner order applied %d, foreign order applied %d", found_owner, found_c2);
    int unclaimed_client = 0, unclaimed_host = 0;
    for (int k = 0; k < ps[0]->nap; k++) {
        if (ps[0]->ap[k].unit == 1 && ps[0]->ap[k].x == 71) unclaimed_client = 1;
        if (ps[0]->ap[k].unit == 1 && ps[0]->ap[k].x == 72) unclaimed_host = 1;
    }
    /* the five votes come from different peers, so their relative order depends on the network: check the set,
       and that every peer saw them in the same order on the same steps */
    static const int want[5][3] = { { 2, 0x10, 302 }, { 1, 0, 301 }, { 2, 0x10, 404 }, { 2, 0, 505 }, { 2, 0, 706 } };
    int found = 0;
    for (int w = 0; w < 5; w++)
        for (int k = 0; k < ps[0]->nstory; k++)
            if (ps[0]->story[k].unit == want[w][0] && ps[0]->story[k].x == want[w][1] && ps[0]->story[k].y == want[w][2]) { found++; break; }
    for (int i = 0; i < 3; i++)
        CHECK(ps[i]->nstory == 5 && found == 5 && !memcmp(ps[i]->story, ps[0]->story, sizeof(applied) * 5),
              "%s: global votes %d, %d of 5 expected (story x2, map propose, map vote, zero-guid base order from non-owners; unclaimed-unit one dropped)",
              ps[i]->name, ps[i]->nstory, found);
    CHECK(unclaimed_host && !unclaimed_client, "unclaimed unit: host order applied %d, client order applied %d", unclaimed_host, unclaimed_client);
    for (int i = 0; i < 3; i++) CHECK(!ps[i]->scope_bad && !ps[i]->order_outside && !ps[i]->in_scope, "%s: sim scope misuse", ps[i]->name);
    CHECK(ps[0]->nap > 50, "only %d orders applied", ps[0]->nap);
    int bad1 = 0; for (uint32_t t = 1; t < 500; t++) if (ticks_in(ps[0], t) != 1 && ticks_in(ps[0], t) != -1) bad1++;
    CHECK(!bad1, "%d steps did not run exactly 1 tick at Normal speed", bad1);
    stop_all(ps, 3);
    printf("%s orders_ownership: 3 peers, %d orders applied on identical steps, %u foreign dropped on all, story + map-change votes (types 3-5) and zero-guid base orders past the owner check, hashes equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", st[0].orders_applied, st[0].orders_dropped_owner, (now_ms() - t0) / 1000);
}

/* ---- 2. speed + pause votes, popups (lobby on) ---- */
static void test_speed_pause(void) {
    int ok = 1; double t0 = now_ms();
    peer *ps[3];
    if (setup(ps, 3, 520, 1, 1)) { printf("  FAIL setup\n"); failures++; return; }
    act(ps[0], 60, A_SPEED, MP_SPEED_FAST, 0, 0);                               /* all Fast -> 3.5 ticks/step */
    act(ps[1], 60, A_SPEED, MP_SPEED_VERYFAST, 0, 0);                           /* VeryFast vote: clamped to Fast */
    act(ps[2], 60, A_SPEED, MP_SPEED_VERYFAST, 1, 0);                           /* raw SPEED_VOTE 3 (bypasses mp_vote_speed): still Fast */
    act(ps[2], 200, A_PAUSE, 1, 0, 0);                                          /* one pause pauses all */
    act(ps[2], 260, A_PAUSE, 0, 0, 0);
    act(ps[0], 320, A_POPUP, MP_PAUSE_ESSENTIAL_POPUP, 1, 0);                   /* host popup -> pause vote */
    act(ps[0], 330, A_POPUP, MP_PAUSE_POPUP_TIMELOCK, 1, 0);                    /* second popup: no new vote */
    act(ps[0], 360, A_POPUP, MP_PAUSE_ESSENTIAL_POPUP, 0, 0);                   /* one still open: still paused */
    act(ps[0], 380, A_POPUP, MP_PAUSE_POPUP_TIMELOCK, 0, 0);                    /* last closed -> run */
    go(ps, 3);
    CHECK(wait_done(ps, 3, 20000) == 0, "timeout");
    for (int i = 1; i < 3; i++) {
        int common = compare(ps[0], ps[i], NULL);
        CHECK(common > 480, "%s: %d common steps", ps[i]->name, common);
        CHECK(ps[i]->nsh == ps[0]->nsh && !memcmp(ps[i]->sh, ps[0]->sh, sizeof(shown) * (size_t)ps[0]->nsh), "%s: show_speed calls differ", ps[i]->name);
    }
    for (int i = 0; i < 3; i++) for (int k = 0; k < ps[i]->nsh; k++) CHECK(ps[i]->sh[k].speed <= MP_SPEED_FAST, "%s showed VeryFast", ps[i]->name);
    CHECK(!strcmp(mp_speed_name(1, 0), "normal") && !strcmp(mp_speed_name(2, 0), "fast 3.5x") && !strcmp(mp_speed_name(3, 0), "fast 3.5x")
          && !strcmp(mp_speed_name(2, 1), "paused"), "speed names");
    peer *h = ps[0];
    /* classify steps from the host's record */
    uint32_t fast_from = 0, pause1_from = 0, pause1_to = 0, pause2_from = 0, pause2_to = 0;
    for (int k = 0; k < h->nsh; k++) {
        shown s = h->sh[k];
        if (s.speed == 2 && !s.paused && !fast_from) fast_from = s.step;
        else if (s.paused && !pause1_from) pause1_from = s.step;
        else if (!s.paused && pause1_from && !pause1_to) pause1_to = s.step;
        else if (s.paused && pause1_to && !pause2_from) pause2_from = s.step;
        else if (!s.paused && pause2_from && !pause2_to) pause2_to = s.step;
    }
    CHECK(fast_from >= 60 && pause1_from >= 200 && pause1_to >= 260 && pause2_from >= 320 && pause2_to >= 380,
          "speed/pause transitions at fast %u pause %u-%u popup %u-%u", fast_from, pause1_from, pause1_to, pause2_from, pause2_to);
    CHECK(pause2_to > pause2_from + 40, "popup pause ended too early (%u-%u): the second popup should hold it", pause2_from, pause2_to);
    int bad = 0, sum = 0, nfast = 0;
    for (uint32_t t = fast_from + 1; t < pause1_from; t++) { int k = ticks_in(h, t); if (k != 3 && k != 4) bad++; else { sum += k; nfast++; } }
    for (uint32_t t = pause1_from + 1; t < pause1_to; t++) if (ticks_in(h, t) != 0) bad++;
    for (uint32_t t = pause2_from + 1; t < pause2_to; t++) if (ticks_in(h, t) != 0) bad++;
    CHECK(!bad, "%d steps with the wrong tick count", bad);
    CHECK(nfast > 10 && sum * 2 >= nfast * 7 - 1 && sum * 2 <= nfast * 7 + 1, "Fast averaged %d ticks over %d steps, want 3.5/step", sum, nfast);
    stop_all(ps, 3);
    printf("%s speed_pause: VeryFast votes clamped, fast 3.5x from %u (%d ticks / %d steps), vote pause %u-%u, popup pause %u-%u, show_speed same on all (%.1fs)\n",
           ok ? "PASS" : "FAIL", fast_from, sum, nfast, pause1_from, pause1_to, pause2_from, pause2_to, (now_ms() - t0) / 1000);
}

/* ---- 3. lobby: allow_pause and allow_speedup off ---- */
static void test_lobby_off(void) {
    int ok = 1; double t0 = now_ms();
    peer *ps[2];
    if (setup(ps, 2, 300, 0, 0)) { printf("  FAIL setup\n"); failures++; return; }
    act(ps[1], 50, A_PAUSE, 1, 0, 0);
    act(ps[1], 70, A_POPUP, MP_PAUSE_ESSENTIAL_POPUP, 1, 0);
    act(ps[0], 90, A_SPEED, MP_SPEED_VERYFAST, 0, 0);
    act(ps[1], 90, A_SPEED, MP_SPEED_VERYFAST, 0, 0);
    go(ps, 2);
    CHECK(wait_done(ps, 2, 15000) == 0, "timeout");
    CHECK(!mp_allow_pause(ps[1]->s) && !mp_allow_speedup(ps[1]->s), "client did not get the lobby settings");
    int bad = 0;
    for (uint32_t t = 1; t < 300; t++) if (ticks_in(ps[0], t) != 1 && ticks_in(ps[0], t) != -1) bad++;
    CHECK(!bad, "%d steps paused or sped up while the lobby forbids it", bad);
    CHECK(compare(ps[0], ps[1], NULL) > 280, "diverged");
    stop_all(ps, 2);
    printf("%s lobby_off: pause vote, popup and fast vote ignored, 1 tick every step (%.1fs)\n", ok ? "PASS" : "FAIL", (now_ms() - t0) / 1000);
}

/* ---- 4. desync -> freeze -> resync by save transfer -> equal again ---- */
static void test_resync(void) {
    int ok = 1; double t0 = now_ms();
    peer *ps[3];
    if (setup(ps, 3, 700, 1, 1)) { printf("  FAIL setup\n"); failures++; return; }
    act(ps[2], 100, A_CORRUPT, 0, 0, 0);
    for (int i = 0; i < 3; i++) ps[i]->rand_orders = 7;
    go(ps, 3);
    CHECK(wait_done(ps, 3, 25000) == 0, "timeout");
    mp_stats st[3];
    for (int i = 0; i < 3; i++) mp_get_stats(ps[i]->s, &st[i]);
    peer *d = ps[2];
    CHECK(st[2].desyncs == 1 && d->loads == 1 && st[2].loads == 1, "c2 desyncs %u loads %d", st[2].desyncs, d->loads);
    CHECK(st[0].saves == 1, "host saves %u", st[0].saves);
    CHECK(st[0].desyncs == 0 && st[1].desyncs == 0 && st[1].loads == 0, "a healthy peer desynced or loaded");
    CHECK(!st[2].desynced, "c2 still desynced");
    int c1 = compare(ps[0], ps[1], NULL), c2 = compare(ps[0], d, NULL);
    CHECK(c1 > 650, "c1: %d common", c1);
    CHECK(c2 > 300, "c2 after the load: %d common", c2);
    /* auto pause during the resync, then auto resume */
    int paused = 0, resumed = 0;
    for (int k = 0; k < ps[0]->nsh; k++) { if (ps[0]->sh[k].paused) paused = 1; else if (paused) resumed = 1; }
    CHECK(paused && resumed, "auto pause %d resume %d", paused, resumed);
    stop_all(ps, 3);
    printf("%s desync_resync: c2 corrupted at 100 -> flagged, froze, host save sent, loaded, %d equal steps after; auto pause+resume (%.1fs)\n",
           ok ? "PASS" : "FAIL", c2, (now_ms() - t0) / 1000);
}

/* ---- 4b. desync during multi-frame flows (map transitions): no command / join-point save inside a flow ---- */
static void test_resync_in_flow(void) {
    int ok = 1; double t0 = now_ms();
    peer *ps[3];
    if (setup(ps, 3, 700, 1, 1)) { printf("  FAIL setup\n"); failures++; return; }
    int len[3] = { 5, 60, 15 };                              /* each peer's flow takes a different time */
    for (int i = 0; i < 3; i++) { ps[i]->flow_every = 37; ps[i]->flow_len = len[i]; ps[i]->rand_orders = 5; }
    act(ps[2], 111, A_CORRUPT, 0, 0, 0);                     /* tick 111 = a flow start: desync flagged mid-flow */
    go(ps, 3);
    CHECK(wait_done(ps, 3, 30000) == 0, "timeout");
    mp_stats st[3];
    for (int i = 0; i < 3; i++) mp_get_stats(ps[i]->s, &st[i]);
    CHECK(st[2].desyncs == 1 && ps[2]->loads == 1, "c2 desyncs %u loads %d", st[2].desyncs, ps[2]->loads);
    CHECK(!st[2].desynced, "c2 still desynced");
    for (int i = 0; i < 3; i++) CHECK(ps[i]->flow_bad == 0, "peer %d: %d orders / saves ran inside a flow", i, ps[i]->flow_bad);
    int c1 = compare(ps[0], ps[1], NULL), c2 = compare(ps[0], ps[2], NULL);
    CHECK(c1 > 650 && c2 > 300, "common steps c1 %d c2 %d", c1, c2);
    stop_all(ps, 3);
    printf("%s resync_in_flow: flows every 37 ticks (5/60/15 frames), c2 corrupted in a flow, resynced; 0 orders/saves inside flows; %d/%d equal steps (%.1fs)\n",
           ok ? "PASS" : "FAIL", c1, c2, (now_ms() - t0) / 1000);
}

/* ---- 5. late joiner gets the host's save ---- */
static void test_late_join(void) {
    int ok = 1; double t0 = now_ms();
    peer *ps[3];
    if (setup(ps, 2, 600, 1, 1)) { printf("  FAIL setup\n"); failures++; return; }
    for (int i = 0; i < 2; i++) ps[i]->rand_orders = 7;
    go(ps, 2);
    while (!ps[0]->have[150]) sleep_ms(5);
    ps[2] = peer_new("late", 600);
    mp_config cc = client_cfg(mp_test_port, "late");
    CHECK(join(ps[2], &cc) == 0, "join failed");
    mp_stats s0; mp_get_stats(ps[2]->s, &s0);
    CHECK(s0.needs_state, "late joiner should wait for the host's save");
    ps[2]->rand_orders = 7;
    pthread_create(&ps[2]->th, NULL, loop, ps[2]);
    CHECK(wait_done(ps, 3, 25000) == 0, "timeout");
    int c = compare(ps[0], ps[2], NULL);
    CHECK(ps[2]->loads == 1 && c > 300, "late: loads %d, %d common steps", ps[2]->loads, c);
    CHECK(compare(ps[0], ps[1], NULL) > 550, "c1 diverged");
    int paused = 0, resumed = 0;
    for (int k = 0; k < ps[0]->nsh; k++) { if (ps[0]->sh[k].paused) paused = 1; else if (paused) resumed = 1; }
    CHECK(paused && resumed, "auto pause %d resume %d", paused, resumed);
    stop_all(ps, 3);
    printf("%s late_join: joined at ~150, got the save, %d equal steps, auto pause+resume (%.1fs)\n", ok ? "PASS" : "FAIL", c, (now_ms() - t0) / 1000);
}

/* ---- 5b. menu joiner at step 0 (no campaign): need_save asks for the host's save at once ---- */
static void test_step0_join(int min_players) {
    int ok = 1; double t0 = now_ms();
    peer *ps[2];
    ps[0] = peer_new("host", 400); ps[1] = peer_new("menu", 400);
    ps[1]->w.rng ^= 0xDEAD; ps[1]->w.u[0].x = 99;           /* the guest has a different (title-screen) world */
    mp_config hc = host_cfg(min_players, 1, 1);
    if (join(ps[0], &hc)) { printf("  FAIL setup\n"); failures++; return; }
    mp_test_port = mp_server_port(ps[0]->s);
    mp_config cc = client_cfg(mp_test_port, "menu"); cc.need_save = 1;
    CHECK(join(ps[1], &cc) == 0, "join failed");
    for (int i = 0; i < 2; i++) ps[i]->rand_orders = 7;
    go(ps, 2);
    CHECK(wait_done(ps, 2, 15000) == 0, "timeout (guest never got a save?)");
    mp_stats st[2]; for (int i = 0; i < 2; i++) mp_get_stats(ps[i]->s, &st[i]);
    int c = compare(ps[0], ps[1], NULL);
    CHECK(ps[1]->loads == 1 && st[0].saves == 1 && c > 250, "guest loads %d, host saves %u, %d equal steps", ps[1]->loads, st[0].saves, c);
    CHECK(!st[1].desynced && !st[0].desynced, "desynced");
    stop_all(ps, 2);
    printf("%s step0_join(min_players %d): guest with no campaign joined at step 0, got the save, %d equal steps (%.1fs)\n",
           ok ? "PASS" : "FAIL", min_players, c, (now_ms() - t0) / 1000);
}

/* ---- 5c. host Admin mode: the host may order any unit; clients keep the normal rule ---- */
static void test_admin(void) {
    int ok = 1; double t0 = now_ms();
    peer *ps[3];
    if (setup(ps, 3, 300, 1, 1)) { printf("  FAIL setup\n"); failures++; return; }
    act(ps[2], 20, A_ADMIN, 1, 0, 0);                 /* a client's admin command: the server drops it */
    act(ps[0], 50, A_ADMIN, 1, 0, 0);                 /* host turns Admin mode on (DSNET_ADMIN_MODE) */
    act(ps[1], 30, A_CLAIM, 0, 0, 0);                 /* c1 owns unit 0 */
    act(ps[0], 80, A_MOVE, 0, 73, 1);                 /* host on c1's unit: applied (admin) */
    act(ps[2], 100, A_MOVE, 0, 74, 1);                /* c2 on c1's unit: dropped */
    act(ps[2], 110, A_MOVE, 1, 75, 1);                /* c2 on an unclaimed unit: dropped */
    act(ps[0], 120, A_MOVE, 1, 76, 1);                /* host on an unclaimed unit: applied */
    go(ps, 3);
    CHECK(wait_done(ps, 3, 15000) == 0, "timeout");
    for (int i = 0; i < 3; i++) {
        int h0 = 0, c0 = 0, c1 = 0, h1 = 0;
        for (int k = 0; k < ps[i]->nap; k++) {
            applied a = ps[i]->ap[k];
            h0 |= a.unit == 0 && a.x == 73; c0 |= a.unit == 0 && a.x == 74; c1 |= a.unit == 1 && a.x == 75; h1 |= a.unit == 1 && a.x == 76;
        }
        CHECK(h0 && h1 && !c0 && !c1, "%s: host on owned %d, host on unclaimed %d, client on owned %d, client on unclaimed %d", ps[i]->name, h0, h1, c0, c1);
        CHECK(same_applied(ps[0], ps[i], 300), "%s applied orders differ", ps[i]->name);
    }
    for (int i = 0; i < 3; i++) CHECK(dsnet_admin_mode(mp_client(ps[i]->s)), "%s: admin flag off", ps[i]->name);
    stop_all(ps, 3);
    printf("%s admin_mode: host orders another player's unit and an unclaimed one, a client still cannot, same on all peers (%.1fs)\n",
           ok ? "PASS" : "FAIL", (now_ms() - t0) / 1000);
}

/* ---- 6. mp.json ---- */
static void test_config(void) {
    int ok = 1;
    mp_config c;
    CHECK(mp_config_parse("{\"role\":\"host\",\"port\":7777,\"name\":\"The owner\",\"allow_speedup\":false,\"allow_pause\":true}", &c) == 0
          && c.host && c.port == 7777 && !strcmp(c.name, "The owner") && !c.allow_speedup && c.allow_pause && c.auto_resync, "host json");
    CHECK(mp_config_parse("{ \"name\" : \"B \\\"x\\\"\", \"role\" : \"client\", \"host\" : \"10.0.0.2\", \"port\" : 7777, \"tick_ms\": 8.5 }", &c) == 0
          && !c.host && !strcmp(c.host_addr, "10.0.0.2") && c.port == 7777 && c.tick_ms == 8.5f && !strcmp(c.name, "B \"x\"")
          && c.allow_pause && c.allow_speedup, "client json");
    CHECK(mp_config_parse("{\"role\":\"client\",\"port\":7777}", &c) == -2, "client without host accepted");
    CHECK(mp_config_parse("{\"name\":\"role\"}", &c) == -2, "no role accepted");
    CHECK(mp_config_parse("{\"role\":\"host\",\"host\":\"x\"}", &c) == 0 && c.host, "value \"host\" taken for a key");
    CHECK(mp_config_load("/nonexistent/mp.json", &c) == -1, "missing file");
    CHECK(mp_config_parse("{\"role\":\"client\",\"host\":\"h\",\"port\":1,\"need_save\":true}", &c) == 0 && c.need_save, "need_save");
    printf("%s config: mp.json host / client / bad cases\n", ok ? "PASS" : "FAIL");
}

/* ---- 5d. a guest leaves: host auto-pauses, popup data, "Keep playing" -> host orders the reserved units;
 *          the same uid rejoins -> his units back, equal fingerprints; "Wait" holds the pause until the rejoin ---- */
static void peer_leave(peer *p) { p->stop = 1; pthread_join(p->th, NULL); mp_stop(p->s); p->s = NULL; }
static int applied_x(peer *p, int unit, int x) { for (int k = 0; k < p->nap; k++) if (p->ap[k].unit == unit && p->ap[k].x == x) return 1; return 0; }
static int wait_until(int (*f)(peer **), peer **ps, int ms) { double t = now_ms(); while (now_ms() - t < ms) { if (f(ps)) return 1; sleep_ms(5); } return 0; }
static int host_paused(peer **ps) { mp_stats st; mp_get_stats(ps[0]->s, &st); return st.paused; }
static int host_running(peer **ps) { mp_stats st; mp_get_stats(ps[0]->s, &st); return !st.paused; }
static void test_leave_rejoin(void) {
    int ok = 1; double t0 = now_ms();
    peer *ps[3];
    static const uint8_t uid[3][16] = { { 0xA0, 1 }, { 0xA1, 1 }, { 0xA2, 1 } };
    for (int i = 0; i < 3; i++) { char nm[8]; snprintf(nm, sizeof nm, i ? "c%d" : "host", i); ps[i] = peer_new(nm, 100000); }
    mp_config hc = host_cfg(3, 1, 1);
    dsnet_set_player_uid(uid[0]);
    if (join(ps[0], &hc)) { printf("  FAIL setup\n"); failures++; return; }
    mp_test_port = mp_server_port(ps[0]->s);
    for (int i = 1; i < 3; i++) { mp_config cc = client_cfg(mp_test_port, ps[i]->name); dsnet_set_player_uid(uid[i]); CHECK(join(ps[i], &cc) == 0, "join %d", i); }
    act(ps[1], 20, A_CLAIM, 0, 0, 0);                 /* c1 owns unit 0 */
    act(ps[2], 20, A_CLAIM, 1, 0, 0);                 /* c2 owns unit 1 */
    go(ps, 3);
    while (timer_of(ps[0]) < 120) sleep_ms(5);
    uint8_t g0[16]; memset(g0, 0x10, 16);
    /* 1. c1 leaves */
    double tl = now_ms(); peer_leave(ps[1]);
    int paused = wait_until(host_paused, ps, 5000); double pause_ms = now_ms() - tl;
    int ids[4]; char names[4][32];
    int n = mp_left_players(ps[0]->s, ids, names, 4);
    CHECK(paused && n == 1 && !strcmp(names[0], "c1"), "after c1 left: host paused %d (%.0f ms), waiting for %d (%s)", paused, pause_ms, n, n ? names[0] : "-");
    CHECK(dsnet_owner_of(mp_client(ps[0]->s), g0) == DSNET_OWNER_RESERVED, "c1's unit is not RESERVED (%d)", dsnet_owner_of(mp_client(ps[0]->s), g0));
    sleep_ms(200);
    mp_stats sc2; mp_get_stats(ps[2]->s, &sc2);
    CHECK(sc2.paused, "c2 not paused while the host waits");
    {   /* owner tables identical on the peers (status own_hash): held, no commands in flight */
        uint64_t u0, i0, u2, i2; int n0, m0, n2, m2;
        dsnet_owner_hashes(mp_client(ps[0]->s), &u0, &i0, &n0, &m0);
        dsnet_owner_hashes(mp_client(ps[2]->s), &u2, &i2, &n2, &m2);
        CHECK(u0 == u2 && i0 == i2 && n0 == n2 && n0 >= 2, "owner hashes differ: host %016llx/%d c2 %016llx/%d",
              (unsigned long long)u0, n0, (unsigned long long)u2, n2);
    }
    /* RESERVED: nobody may order it, not even the host (admin off) */
    uint32_t t1 = timer_of(ps[0]);
    act(ps[0], t1 + 5, A_MOVE, 0, 80, 1);
    act(ps[2], t1 + 5, A_MOVE, 0, 79, 1);
    while (timer_of(ps[0]) < t1 + 30) sleep_ms(5);
    CHECK(!applied_x(ps[0], 0, 80) && !applied_x(ps[2], 0, 80) && !applied_x(ps[0], 0, 79), "an order on a RESERVED unit was applied before Keep playing");
    /* 2. Keep playing: running again, the host orders c1's reserved unit */
    uint32_t t = timer_of(ps[0]);
    act(ps[0], t + 1, A_LEFT, 1, 0, 0);
    act(ps[0], t + 20, A_MOVE, 0, 81, 1);
    CHECK(wait_until(host_running, ps, 5000), "Keep playing did not resume");
    /* c2 resyncs while the host has temporary control: the control list travels in the mp save header */
    int l2 = ps[2]->loads; dsnet_request_resync(mp_client(ps[2]->s));
    { double tq = now_ms(); while (ps[2]->loads == l2 && now_ms() - tq < 8000) sleep_ms(5); }
    CHECK(ps[2]->loads == l2 + 1, "c2 resync did not load");
    t = timer_of(ps[0]); act(ps[0], t + 20, A_MOVE, 0, 81, 1);
    while (timer_of(ps[0]) < t + 60) sleep_ms(5);
    CHECK(applied_x(ps[0], 0, 81) && applied_x(ps[2], 0, 81), "host order on c1's reserved unit: host %d c2 %d", applied_x(ps[0], 0, 81), applied_x(ps[2], 0, 81));
    /* 3. a new player takes c1's old slot first, then c1 rejoins with the same uid in another slot */
    static const uint8_t uid3[16] = { 0xA3, 1 };
    peer *n3 = peer_new("c3", 100000);
    { mp_config c3 = client_cfg(mp_test_port, "c3"); dsnet_set_player_uid(uid3); CHECK(join(n3, &c3) == 0, "c3 join failed"); }
    pthread_create(&n3->th, NULL, loop, n3);
    { double tq = now_ms(); while (!n3->loads && now_ms() - tq < 8000) sleep_ms(5); }

    peer *r = peer_new("c1b", 100000); r->rand_orders = 0;
    mp_config cc = client_cfg(mp_test_port, "c1"); dsnet_set_player_uid(uid[1]);
    CHECK(join(r, &cc) == 0, "rejoin failed");
    pthread_create(&r->th, NULL, loop, r);
    double tr = now_ms();
    while (!r->loads && now_ms() - tr < 10000) sleep_ms(5);
    while (now_ms() - tr < 10000) { mp_stats a, b; mp_get_stats(ps[0]->s, &a); mp_get_stats(r->s, &b); if (!a.paused && !b.paused && !b.loading && r->loads) break; sleep_ms(5); }
    int me = dsnet_player_id(mp_client(r->s));
    CHECK(me != 1, "c1 got its old slot back (%d); wanted another slot for this test", me);
    CHECK(r->loads == 1 && dsnet_owner_of(mp_client(ps[0]->s), g0) == me && dsnet_owner_of(mp_client(r->s), g0) == me,
          "rejoin: loads %d, unit 0 owner host %d / c1 %d (want slot %d)", r->loads, dsnet_owner_of(mp_client(ps[0]->s), g0), dsnet_owner_of(mp_client(r->s), g0), me);
    t = timer_of(ps[0]);
    act(r, t + 20, A_MOVE, 0, 82, 1);                 /* his order on his unit: applied */
    act(ps[0], t + 25, A_MOVE, 0, 83, 1);             /* host order on it now: dropped */
    while (timer_of(ps[0]) < t + 80 || timer_of(r) < t + 80) sleep_ms(5);
    CHECK(applied_x(ps[0], 0, 82) && applied_x(r, 0, 82) && !applied_x(ps[0], 0, 83) && !applied_x(r, 0, 83),
          "after rejoin: c1 order applied %d/%d, host order applied %d/%d", applied_x(ps[0], 0, 82), applied_x(r, 0, 82), applied_x(ps[0], 0, 83), applied_x(r, 0, 83));
    int common = compare(ps[0], r, NULL);
    CHECK(common > 40, "rejoined c1: %d equal steps with the host", common);
    /* 4. Wait: c2 leaves, the host picks "Wait", stays paused until c2 is back */
    peer_leave(ps[2]);
    CHECK(wait_until(host_paused, ps, 5000), "host did not pause when c2 left");
    act(ps[0], timer_of(ps[0]) + 1, A_LEFT, 0, 0, 0);
    sleep_ms(500);
    CHECK(host_paused(ps) && mp_left_waiting(ps[0]->s), "Wait: host paused %d waiting %d", host_paused(ps), mp_left_waiting(ps[0]->s));
    peer *r2 = peer_new("c2b", 100000);
    mp_config cc2 = client_cfg(mp_test_port, "c2"); dsnet_set_player_uid(uid[2]);
    CHECK(join(r2, &cc2) == 0, "c2 rejoin failed");
    pthread_create(&r2->th, NULL, loop, r2);
    double tw = now_ms(); int resumed = 0;
    while (now_ms() - tw < 10000) { mp_stats a; mp_get_stats(ps[0]->s, &a); if (!a.paused && r2->loads) { resumed = 1; break; } sleep_ms(5); }
    uint8_t g1[16]; memset(g1, 0x11, 16);
    CHECK(resumed && mp_left_players(ps[0]->s, NULL, NULL, 4) == 0 && dsnet_owner_of(mp_client(ps[0]->s), g1) == dsnet_player_id(mp_client(r2->s)),
          "after c2 rejoined: resumed %d, still waiting for %d, unit 1 owner %d", resumed, mp_left_players(ps[0]->s, NULL, NULL, 4), dsnet_owner_of(mp_client(ps[0]->s), g1));
    dsnet_set_player_uid(NULL);
    /* 5. persistence: the host exports the owner tables with the world; a new session imports them */
    uint8_t ob[1 << 16]; size_t on = dsnet_export_owners(mp_client(ps[0]->s), ob, sizeof ob);
    peer *all[4] = { ps[0], r, r2, n3 };
    stop_all(all, 4); free(ps[1]); free(ps[2]);
    peer *h2 = peer_new("host2", 100000);
    mp_config hc2 = host_cfg(1, 1, 1); dsnet_set_player_uid(uid[0]);
    CHECK(join(h2, &hc2) == 0, "new session failed");
    CHECK(on > 0 && on <= sizeof ob && dsnet_import_owners(mp_client(h2->s), ob, on) == 0, "export %zu / import failed", on);
    pthread_create(&h2->th, NULL, loop, h2);
    uint8_t ou[16]; char onm[32] = ""; int ow = -9; double tq = now_ms();
    while (now_ms() - tq < 5000) { ow = dsnet_owner_info(mp_client(h2->s), g0, ou, onm); if (ow == DSNET_OWNER_RESERVED) break; sleep_ms(5); }
    CHECK(ow == DSNET_OWNER_RESERVED && !memcmp(ou, uid[1], 16) && !strcmp(onm, "c1"), "new session: unit 0 owner %d name '%s' (want RESERVED for c1)", ow, onm);
    peer *last[1] = { h2 }; stop_all(last, 1);
    dsnet_set_player_uid(NULL);
    printf("%s leave_rejoin: host paused %.0f ms after c1 left; RESERVED unit refused to all; Keep playing -> host ordered it (also after a c2 resync); c1 rejoined in another slot -> unit back, %d equal steps; Wait held the pause until c2 rejoined; owners restored in a new session (%.1fs)\n",
           ok ? "PASS" : "FAIL", pause_ms, common, (now_ms() - t0) / 1000);
}

/* ---- 5e. pause mode off ("No pause"): a disconnect still holds everyone (dsnet HOLD) until Keep playing ---- */
static void test_leave_nopause(void) {
    int ok = 1; double t0 = now_ms();
    peer *ps[3];
    static const uint8_t uid[3][16] = { { 0xB0, 2 }, { 0xB1, 2 }, { 0xB2, 2 } };
    for (int i = 0; i < 3; i++) { char nm[8]; snprintf(nm, sizeof nm, i ? "c%d" : "host", i); ps[i] = peer_new(nm, 100000); }
    mp_config hc = host_cfg(3, 1, 0);
    dsnet_set_player_uid(uid[0]);
    if (join(ps[0], &hc)) { printf("  FAIL setup\n"); failures++; return; }
    mp_test_port = mp_server_port(ps[0]->s);
    for (int i = 1; i < 3; i++) { mp_config cc = client_cfg(mp_test_port, ps[i]->name); dsnet_set_player_uid(uid[i]); CHECK(join(ps[i], &cc) == 0, "join %d", i); }
    dsnet_set_player_uid(NULL);
    go(ps, 3);
    while (timer_of(ps[0]) < 80) sleep_ms(5);
    peer_leave(ps[1]);
    int held = wait_until(host_paused, ps, 5000);
    sleep_ms(300);
    mp_stats a; mp_get_stats(ps[2]->s, &a);
    uint64_t w0 = ps[2]->w.tick; sleep_ms(300); uint64_t w1 = ps[2]->w.tick;
    CHECK(!mp_allow_pause(ps[0]->s) && held && a.paused && w0 == w1, "No pause mode: host held %d, c2 paused %d, c2 ticks %llu -> %llu",
          held, a.paused, (unsigned long long)w0, (unsigned long long)w1);
    act(ps[0], timer_of(ps[0]) + 1, A_LEFT, 1, 0, 0);
    CHECK(wait_until(host_running, ps, 5000), "Keep playing did not resume in No pause mode");
    peer *all[2] = { ps[0], ps[2] };
    stop_all(all, 2); free(ps[1]);
    printf("%s leave_nopause: with pause mode off a disconnect still holds everyone (0 ticks) until Keep playing (%.1fs)\n", ok ? "PASS" : "FAIL", (now_ms() - t0) / 1000);
}

int main(void) {
    double t0 = now_ms();
    test_config();
    test_orders();
    test_speed_pause();
    test_lobby_off();
    test_resync();
    test_resync_in_flow();
    test_late_join();
    test_admin();
    test_leave_rejoin();
    test_leave_nopause();
    test_step0_join(1);
    test_step0_join(2);   /* dsnet counts a loading guest toward min_players (netcore fix) */
    printf("%s: %d failure(s), %.1fs\n", failures ? "FAILED" : "ALL PASS", failures, (now_ms() - t0) / 1000);
    return failures ? 1 : 0;
}
