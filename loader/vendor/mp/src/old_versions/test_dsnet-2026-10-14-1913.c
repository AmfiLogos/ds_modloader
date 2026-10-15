/* dsnet tests: a deterministic toy sim driven by dsnet, host + clients in one process and as
 * separate processes over TCP, with latency/jitter, a slow peer, pause/speed votes, ownership,
 * a forced desync + resync by save transfer, and a late joiner with a 64 MB save. */
#include "../dsnet.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fenv.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
#define MAXT 4096
#define TICK_MS 5.0f
#define FRAME_MS 2

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; ok = 0; } } while (0)

static double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }
static void sleep_ms(double ms) { if (ms > 0) usleep((useconds_t)(ms * 1000)); }

/* ---- toy game: integer state, changed per tick and by orders ---- */
typedef struct toy { uint64_t s[8]; uint32_t game_ticks; } toy;
static void toy_tick(toy *t) {
    for (int i = 0; i < 8; i++) t->s[i] = t->s[i] * 6364136223846793005ULL + 1442695040888963407ULL + (t->s[(i + 1) & 7] >> 7);
    t->game_ticks++;
}
static uint64_t toy_hash(const toy *t) {
    uint64_t h = 1469598103934665603ULL;
    const uint8_t *p = (const uint8_t *)t;
    for (size_t i = 0; i < sizeof *t; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

typedef struct script_item { uint32_t at; uint8_t type, len; uint8_t data[128]; int done; } script_item;
typedef struct exec_item { uint32_t timer; uint8_t type, player, val; } exec_item;

typedef struct peer {
    char name[16];
    dsnet_client *c;
    toy st;
    uint32_t timer, target;
    uint64_t hash_at[MAXT]; uint8_t tps_at[MAXT], have[MAXT]; int8_t by_at[MAXT];
    pthread_t th; volatile int stop, done;
    uint32_t rng;
    int orders_every, orders_sent, orders_seen;
    uint32_t stall_at; int stall_ms, stalled, fake_frame_ms; uint32_t fake_until;
    uint32_t corrupt_at; int corrupted;
    int auto_resync; double desync_seen_at; uint32_t desync_bad, desync_good, timer_at_desync, timer_after_wait;
    int resync_requested, loads; uint32_t load_tick;
    uint32_t story_mask_seen, busy_seen;
    int frame_ms;                 /* frame period, default FRAME_MS */
    int starts; uint32_t start_tick; uint64_t start_hash; int start_len; uint8_t start_buf[200];
    int recruit_rc_outside;
    int watch_player; uint32_t net_seen;
    int split_owner;                          /* test: this peer alone tags a unit when order F2 runs (owner split) */      /* states of watch_player seen through dsnet_player_net */
    double t_at60, t_at300;
    uint32_t busy_at; int busy_ms, busy_done; double busy_since;
    int want_run, vote_while_desynced, sent_desync_vote;   /* auto-pause policy of the toy game */
    size_t save_pad; int save_bad, upload_rc, oversize_rc;
    int lossy_save;               /* host: a save loses s[7] (like in-progress tasks) and the host reloads it itself */
    int partial, patch_fail, patch_tried, patch_rc_host, reports_sent; uint32_t patch_tick, patch_count;   /* lan_v10 */
    uint8_t *report; size_t report_len; int report_from;                                                  /* host: got */
    script_item script[24]; int nscript;
    exec_item exec[64]; int nexec;
} peer;

static uint32_t xs(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

static void on_cmd(const dsnet_cmd *cmd, void *user) {
    peer *p = user;
    switch (cmd->type) {
    case DSNET_EXPEDITION_START:
        p->starts++; p->start_tick = cmd->tick; p->start_len = cmd->len;
        memcpy(p->start_buf, cmd->payload, cmd->len < 200 ? cmd->len : 200);
        break;
    case DSNET_GAME_ORDER:
        if (cmd->len >= 2 && cmd->payload[0] == 0xF2) {
            uint8_t g[16]; memset(g, 0xF2, 16);
            if (p->split_owner) dsnet_set_recruit_owner(p->c, g);   /* a local-only owner change: must be caught */
            break;
        }
        if (cmd->len >= 2 && (cmd->payload[0] == 0xF0 || cmd->payload[0] == 0xF1)) {
            /* toy recruit (F0) / craft (F1): the new thing's guid comes from the order itself */
            uint8_t g[16]; memset(g, cmd->payload[1], 16); g[0] = cmd->payload[0];
            if (cmd->payload[0] == 0xF0) dsnet_set_recruit_owner(p->c, g);
            else dsnet_item_set_owner(p->c, g, cmd->player);
            break;
        }
        if (cmd->len >= 5) {
            int32_t v; memcpy(&v, cmd->payload + 1, 4);
            p->st.s[cmd->payload[0] & 7] += (uint64_t)(int64_t)v * 31 + cmd->player;
            p->orders_seen++;
        }
        break;
    case DSNET_STORY_PAUSE:
        p->story_mask_seen |= dsnet_story_pause_mask(p->c);
        if (p->nexec < 64) p->exec[p->nexec++] = (exec_item){ cmd->tick, cmd->type, cmd->player, cmd->len ? cmd->payload[0] : 0 };
        break;
    case DSNET_PAUSE_ALL:
        p->want_run = 1;     /* vote run again once this peer is in sync */
        /* fall through */
    case DSNET_PAUSE_VOTE: case DSNET_SPEED_VOTE: case DSNET_RULES:
        if (p->nexec < 64) p->exec[p->nexec++] = (exec_item){ cmd->tick, cmd->type, cmd->player, cmd->len ? cmd->payload[0] : 0 };
        break;
    case DSNET_PATCH_POINT:                     /* partial resync: the host sends its toy state from this exact place */
        if (dsnet_is_host(p->c)) p->patch_rc_host = dsnet_upload_patch(p->c, (const uint8_t *)&p->st, sizeof p->st);
        break;
    case DSNET_JOIN_POINT:
        if (dsnet_is_host(p->c)) {
            p->oversize_rc = dsnet_upload_save(p->c, (const uint8_t *)p, DSNET_MAX_BLOB);   /* must refuse */
            size_t n = sizeof p->st + p->save_pad;
            uint8_t *b = malloc(n);
            if (p->lossy_save) p->st.s[7] = 0;     /* the host reloads its own (lossy) save, as mp_game.c does */
            memcpy(b, &p->st, sizeof p->st);
            for (size_t i = 0; i < p->save_pad; i++) b[sizeof p->st + i] = (uint8_t)(i * 7 + (i >> 10));
            p->upload_rc = dsnet_upload_save(p->c, b, n);
            free(b);
        }
        break;
    }
}

static void on_load(uint32_t tick, const uint8_t *data, size_t len, void *user) {
    peer *p = user;
    if (len < sizeof p->st) { p->save_bad = 1; return; }
    memcpy(&p->st, data, sizeof p->st);
    for (size_t i = sizeof p->st; i < len; i++)
        if (data[i] != (uint8_t)((i - sizeof p->st) * 7 + ((i - sizeof p->st) >> 10))) { p->save_bad = 1; break; }
    uint32_t from = p->corrupted && p->corrupt_at < tick ? p->corrupt_at : tick;
    for (uint32_t t = from; t < MAXT; t++) p->have[t] = 0;
    p->timer = tick; p->loads++; p->load_tick = tick;
    p->want_run = 1;
}

static int on_patch(uint32_t tick, const uint8_t *data, size_t len, void *user) {
    peer *p = user;
    p->patch_tried = 1;
    if (p->patch_fail || len != sizeof p->st) return -1;
    memcpy(&p->st, data, sizeof p->st);           /* "apply in place" */
    if (toy_hash(&p->st) != toy_hash((const toy *)data)) return -1;
    uint32_t from = p->corrupted && p->corrupt_at < tick ? p->corrupt_at : tick;
    for (uint32_t t = from; t < tick && t < MAXT; t++) p->have[t] = 0;   /* the diverged steps are not compared */
    p->patch_tick = tick; p->patch_count++;
    return 0;
}

static void *game_loop(void *arg) {
    peer *p = arg;
    double last = now_ms();
    int frame = 0;
    while (!p->stop) {
        double t0 = now_ms();
        float fms = (float)(t0 - last); last = t0;
        if (p->stall_at && !p->stalled && p->timer >= p->stall_at) {   /* slow peer: game thread freezes */
            p->stalled = 1; sleep_ms(p->stall_ms);
            p->fake_until = p->timer + 100;
        }
        dsnet_report_frame_time(p->c, p->fake_until > p->timer ? (float)p->fake_frame_ms : fms);
        p->busy_seen |= dsnet_busy_mask(p->c);
        if (p->watch_player) { int st; dsnet_player_net(p->c, p->watch_player, NULL, NULL, &st); p->net_seen |= 1u << st; }
        if (p->busy_at && !p->busy_done && p->timer >= p->busy_at) {   /* barrier: hold here, keep the frame loop */
            if (!p->busy_since) { p->busy_since = now_ms(); dsnet_set_busy(p->c, 1); }
            if (now_ms() - p->busy_since < p->busy_ms) { dsnet_can_run_tick(p->c, p->timer); sleep_ms(FRAME_MS); continue; }
            dsnet_set_busy(p->c, 0); p->busy_done = 1;
        }
        dsnet_can_run_tick(p->c, p->timer);          /* also runs a pending load */
        int n = dsnet_steps_this_frame(p->c, fms);
        for (int i = 0; i < n; i++) {
            if (!dsnet_can_run_tick(p->c, p->timer)) break;
            uint32_t t = p->timer;
            dsnet_take_cmds_for_tick(p->c, t, on_cmd, p);
            if (dsnet_patch_state(p->c) == 2) break;  /* stopped at our patch point: the rest of step t after the patch */
            int tps = dsnet_ticks_per_step(p->c);
            for (int k = 0; k < tps; k++) toy_tick(&p->st);
            if (p->corrupt_at && t == p->corrupt_at && !p->corrupted) { p->st.s[3] ^= 1; p->corrupted = 1; }
            uint64_t h = toy_hash(&p->st);
            if (t < MAXT) { p->hash_at[t] = h; p->tps_at[t] = tps; p->have[t] = 1; p->by_at[t] = dsnet_pause_by(p->c); }
            dsnet_report_fingerprint(p->c, t, h);
            p->timer = t + 1;
        }
        for (int i = 0; i < p->nscript; i++) {
            script_item *s = &p->script[i];
            if (!s->done && p->timer >= s->at) { dsnet_submit(p->c, s->type, 0, s->data, s->len); s->done = 1; }
        }
        if (p->orders_every && ++frame % p->orders_every == 0 && !dsnet_needs_state(p->c)) {
            uint8_t o[5]; o[0] = xs(&p->rng) & 7; int32_t v = (int32_t)(xs(&p->rng) % 1000) - 500; memcpy(o + 1, &v, 4);
            if (dsnet_submit(p->c, DSNET_GAME_ORDER, 0, o, 5) == 0) p->orders_sent++;
        }
        uint32_t bad, good;
        if (dsnet_is_desynced(p->c, &bad, &good) && !p->desync_seen_at) {
            p->desync_seen_at = now_ms(); p->desync_bad = bad; p->desync_good = good; p->timer_at_desync = p->timer;
        }
        if (p->want_run && !dsnet_is_desynced(p->c, NULL, NULL) && !dsnet_needs_state(p->c)) {
            uint8_t v = 0; dsnet_submit(p->c, DSNET_PAUSE_VOTE, 0, &v, 1); p->want_run = 0;
        }
        if (p->desync_seen_at && p->vote_while_desynced && !p->sent_desync_vote) {   /* server must drop this */
            uint8_t v = 0; dsnet_submit(p->c, DSNET_PAUSE_VOTE, 0, &v, 1); p->sent_desync_vote = 1;
        }
        if (p->partial && p->desync_seen_at && !p->reports_sent) {   /* report part + partial resync at once */
            uint8_t rep[3000]; for (int i = 0; i < 3000; i++) rep[i] = (uint8_t)(i * 13);
            dsnet_desync_info di; dsnet_desync_get(p->c, &di); memcpy(rep, &di, sizeof di);
            dsnet_send_report(p->c, rep, sizeof rep); p->reports_sent = 1;
            dsnet_request_patch(p->c);
        }
        if (dsnet_is_host(p->c) && !p->report) {
            uint8_t *d; size_t n; int from;
            if (dsnet_report_take(p->c, &d, &n, &from)) { p->report = d; p->report_len = n; p->report_from = from; }
        }
        if (p->desync_seen_at && p->auto_resync && !p->resync_requested && now_ms() - p->desync_seen_at > 300 &&
            (!p->partial || (p->patch_tried && dsnet_patch_state(p->c) == 0 && dsnet_is_desynced(p->c, NULL, NULL)))) {
            p->timer_after_wait = p->timer;
            dsnet_request_resync(p->c); p->resync_requested = 1;
        }
        if (!p->t_at60 && p->timer >= 60) p->t_at60 = now_ms();
        if (!p->t_at300 && p->timer >= 300) p->t_at300 = now_ms();
        if (p->timer >= p->target) p->done = 1;
        double el = now_ms() - t0;
        sleep_ms((p->frame_ms ? p->frame_ms : FRAME_MS) - el);
    }
    return NULL;
}

static peer *peer_new(const char *name, dsnet_client *c, uint32_t target) {
    peer *p = calloc(1, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", name);
    p->c = c; p->target = target; p->rng = 2463534242u ^ (uint32_t)(uintptr_t)p;
    dsnet_set_load_cb(c, on_load, p);
    dsnet_set_patch_cb(c, on_patch, p);
    return p;
}
static void peer_go(peer *p) { pthread_create(&p->th, NULL, game_loop, p); }
static void script_add(peer *p, uint32_t at, uint8_t type, const void *d, uint8_t len) {
    script_item *s = &p->script[p->nscript++];
    s->at = at; s->type = type; s->len = len; memcpy(s->data, d, len);
}
static void vote(peer *p, uint32_t at, uint8_t type, uint8_t v) { script_add(p, at, type, &v, 1); }

static int wait_done(peer **ps, int n, int timeout_ms) {
    double end = now_ms() + timeout_ms;
    for (;;) {
        int all = 1;
        for (int i = 0; i < n; i++) if (!ps[i]->done) all = 0;
        if (all) return 0;
        if (now_ms() > end) return -1;
        sleep_ms(10);
    }
}
static void stop_all(peer **ps, int n, dsnet_server *s) {
    for (int i = 0; i < n; i++) { ps[i]->stop = 1; pthread_join(ps[i]->th, NULL); }
    for (int i = 0; i < n; i++) dsnet_client_close(ps[i]->c);
    if (s) dsnet_server_stop(s);
    for (int i = 0; i < n; i++) free(ps[i]);
}

/* every tick both peers recorded must have the same hash */
static int compare(peer *a, peer *b, int *common) {
    int same = 1, n = 0;
    for (uint32_t t = 0; t < MAXT; t++) if (a->have[t] && b->have[t]) {
        n++;
        if (a->hash_at[t] != b->hash_at[t] || a->tps_at[t] != b->tps_at[t]) {
            if (same) printf("  %s vs %s differ at tick %u\n", a->name, b->name, t);
            same = 0;
        }
    }
    if (common) *common = n;
    return same;
}

static dsnet_cfg test_cfg(int players) {
    dsnet_cfg c = dsnet_cfg_defaults();
    c.min_players = players; c.tick_ms = TICK_MS; c.net_ms = 2 * TICK_MS; c.fp_interval = 30; c.bind_addr = "127.0.0.1";
    return c;
}

/* start host + n-1 TCP clients (optionally through proxies) */
static uint16_t proxy_start(uint16_t up, int base_ms, int jitter_ms);
typedef struct proxy proxy;
static proxy *last_proxy;
static void proxy_freeze(proxy *x);
static int setup_cfg(dsnet_server **srv, peer **ps, int n, uint32_t target, const int *lat, const int *jit, const dsnet_cfg *base) {
    dsnet_cfg cfg = base ? *base : test_cfg(n);
    *srv = dsnet_server_start(&cfg);
    if (!*srv) return -1;
    uint16_t port = dsnet_server_port(*srv);
    ps[0] = peer_new("host", dsnet_client_connect_local(*srv, "host"), target);
    if (dsnet_wait_welcome(ps[0]->c, 2000)) return -1;
    for (int i = 1; i < n; i++) {
        char nm[16]; snprintf(nm, sizeof nm, "c%d", i);
        uint16_t pp = lat && lat[i] ? proxy_start(port, lat[i], jit[i]) : port;
        dsnet_client *c = dsnet_client_connect_tcp("127.0.0.1", pp, nm);
        if (!c) return -1;
        ps[i] = peer_new(nm, c, target);
        if (dsnet_wait_welcome(c, 2000)) return -1;
    }
    return 0;
}

static int setup(dsnet_server **srv, peer **ps, int n, uint32_t target, const int *lat, const int *jit) {
    return setup_cfg(srv, ps, n, target, lat, jit, NULL);
}

static int all_equal(peer **ps, int n, int min_common) {
    int ok = 1;
    for (int i = 1; i < n; i++) {
        int common;
        CHECK(compare(ps[0], ps[i], &common), "%s diverged from host", ps[i]->name);
        CHECK(common >= min_common, "%s only %d common ticks", ps[i]->name, common);
    }
    return ok;
}

/* ---- 1. basic, in-process, plus ownership rules ---- */
static void test_basic(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    if (setup(&srv, ps, 3, 600, NULL, NULL)) { printf("  FAIL setup\n"); failures++; return; }
    uint8_t g[6][17];
    for (int i = 0; i < 6; i++) { memset(g[i], 0xA0 + i, 16); }
    /* c1 claims A; c2 claims A (taken); c1 claims B, C, D (D refused: units per player 3 by default); host gives E to c2; c1 releases B */
    g[0][16] = 1; script_add(ps[1], 50, DSNET_OWNERSHIP, g[0], 17);
    g[1][16] = 2; memcpy(g[1], g[0], 16); script_add(ps[2], 80, DSNET_OWNERSHIP, g[1], 17);
    g[2][16] = 1; script_add(ps[1], 100, DSNET_OWNERSHIP, g[2], 17);
    g[3][16] = 1; script_add(ps[1], 101, DSNET_OWNERSHIP, g[3], 17);
    g[4][16] = 1; script_add(ps[1], 102, DSNET_OWNERSHIP, g[4], 17);
    g[5][16] = 2; script_add(ps[0], 120, DSNET_OWNERSHIP, g[5], 17);
    uint8_t rel[17]; memcpy(rel, g[2], 16); rel[16] = DSNET_NO_PLAYER; script_add(ps[1], 200, DSNET_OWNERSHIP, rel, 17);
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 5; peer_go(ps[i]); }
    CHECK(wait_done(ps, 3, 15000) == 0, "timeout");
    sleep_ms(50);
    for (int i = 0; i < 3; i++) {
        uint8_t ga[16]; memset(ga, 0xA0, 16);
        CHECK(dsnet_owner_of(ps[i]->c, g[0]) == 1, "%s: A owner %d", ps[i]->name, dsnet_owner_of(ps[i]->c, g[0]));
        CHECK(dsnet_owner_of(ps[i]->c, g[2]) == -1, "%s: B should be released", ps[i]->name);
        CHECK(dsnet_owner_of(ps[i]->c, g[3]) == 1, "%s: C owner", ps[i]->name);
        CHECK(dsnet_owner_of(ps[i]->c, g[4]) == -1, "%s: D: a 4th unit must be refused (limit 3)", ps[i]->name);
        CHECK(dsnet_owner_of(ps[i]->c, g[5]) == 2, "%s: E owner", ps[i]->name);
        CHECK(!dsnet_is_desynced(ps[i]->c, NULL, NULL), "%s desynced", ps[i]->name);
        CHECK(ps[i]->orders_sent > 20, "%s sent few orders", ps[i]->name);
    }
    all_equal(ps, 3, 550);
    int seen0 = ps[0]->orders_seen, sent = ps[0]->orders_sent + ps[1]->orders_sent + ps[2]->orders_sent;
    stop_all(ps, 3, srv);
    printf("%s basic_inproc: 3 peers, 600 ticks, %d orders sent, host ran %d, hashes equal, ownership ok (%.1fs)\n",
           ok ? "PASS" : "FAIL", sent, seen0, (now_ms() - t0) / 1000);
}

/* ---- 2. latency + jitter through a delaying TCP proxy ---- */
static void test_latency(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    int lat[3] = { 0, 30, 60 }, jit[3] = { 0, 20, 40 };
    if (setup(&srv, ps, 3, 600, lat, jit)) { printf("  FAIL setup\n"); failures++; return; }
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 4; peer_go(ps[i]); }
    CHECK(wait_done(ps, 3, 20000) == 0, "timeout");
    for (int i = 0; i < 3; i++) CHECK(!dsnet_is_desynced(ps[i]->c, NULL, NULL), "%s desynced", ps[i]->name);
    int pg[3], tb[3], st[3];
    for (int k = 0; k < 3; k++) CHECK(dsnet_player_net(ps[1]->c, k, &pg[k], &tb[k], &st[k]) == 0 && st[k] == DSNET_NET_IN_SYNC, "player %d net state", k);
    CHECK(pg[0] < 20 && pg[1] >= 15 && pg[1] < 200 && pg[2] >= 30 && pg[2] < 300, "pings %d/%d/%d ms", pg[0], pg[1], pg[2]);
    CHECK(tb[0] < 60 && tb[1] < 60 && tb[2] < 60, "ticks behind %d/%d/%d", tb[0], tb[1], tb[2]);
    int ast; CHECK(dsnet_player_net(ps[1]->c, 9, NULL, NULL, &ast) == -1 && ast == DSNET_NET_ABSENT, "absent player");
    all_equal(ps, 3, 550);
    stop_all(ps, 3, srv);
    printf("%s latency_jitter (pings seen by c1: %d/%d/%d ms): c1 30+-20 ms, c2 60+-40 ms each way, 600 ticks, hashes equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", pg[0], pg[1], pg[2], (now_ms() - t0) / 1000);
}

/* ---- 3. slow peer: game thread stalls, then reports long frames ---- */
static peer *mon_peers[3]; static volatile int mon_stop; static int mon_maxdiff; static float mon_maxtpt; static dsnet_server *mon_srv;
static void *monitor(void *a) {
    (void)a;
    while (!mon_stop) {
        int d = (int)mon_peers[0]->timer - (int)mon_peers[2]->timer;
        if (d > mon_maxdiff) mon_maxdiff = d;
        dsnet_server_stats st; dsnet_server_get_stats(mon_srv, &st);
        if (st.time_per_tick_ms > mon_maxtpt) mon_maxtpt = st.time_per_tick_ms;
        sleep_ms(5);
    }
    return NULL;
}
static void test_slow_peer(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    dsnet_cfg cfg = test_cfg(3); cfg.slow_to_frame_time = 1;
    if (setup_cfg(&srv, ps, 3, 700, NULL, NULL, &cfg)) { printf("  FAIL setup\n"); failures++; return; }
    ps[2]->stall_at = 200; ps[2]->stall_ms = 1000; ps[2]->fake_frame_ms = 30;   /* server built with slow_to_frame_time */
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 6; mon_peers[i] = ps[i]; peer_go(ps[i]); }
    mon_srv = srv; mon_stop = 0; mon_maxdiff = 0; mon_maxtpt = 0;
    pthread_t mt; pthread_create(&mt, NULL, monitor, NULL);
    CHECK(wait_done(ps, 3, 25000) == 0, "timeout");
    mon_stop = 1; pthread_join(mt, NULL);
    dsnet_server_stats st; dsnet_server_get_stats(srv, &st);
    /* without the hold the host would run 1000/5 = 200 ticks ahead */
    CHECK(mon_maxdiff < 140, "host ran %d ticks ahead of the stalled peer", mon_maxdiff);
    CHECK(st.held_steps > 10, "server never held (held=%u)", st.held_steps);
    CHECK(mon_maxtpt >= 3 * TICK_MS, "time per tick did not follow the slow frames (max %.1f ms)", mon_maxtpt);
    for (int i = 0; i < 3; i++) CHECK(!dsnet_is_desynced(ps[i]->c, NULL, NULL), "%s desynced", ps[i]->name);
    all_equal(ps, 3, 650);
    stop_all(ps, 3, srv);
    printf("%s slow_peer: 1 s stall -> max lead %d ticks (limit 90 + slack), held %u steps, time/tick up to %.1f ms, hashes equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", mon_maxdiff, st.held_steps, mon_maxtpt, (now_ms() - t0) / 1000);
}

/* ---- 4. pause vote (any pauses, all must vote run) + speed vote (lowest wins) ---- */
static uint32_t exec_at(peer *p, uint8_t type, uint8_t player, uint8_t val, int nth) {
    for (int i = 0; i < p->nexec; i++)
        if (p->exec[i].type == type && p->exec[i].player == player && p->exec[i].val == val && nth-- == 0) return p->exec[i].timer;
    return 0;
}
static void test_pause(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    if (setup(&srv, ps, 3, 680, NULL, NULL)) { printf("  FAIL setup\n"); failures++; return; }
    vote(ps[1], 100, DSNET_PAUSE_VOTE, 1);
    vote(ps[2], 150, DSNET_PAUSE_VOTE, 0);   /* c1 still pausing -> stays paused */
    vote(ps[1], 250, DSNET_PAUSE_VOTE, 0);
    vote(ps[2], 300, DSNET_PAUSE_VOTE, 1);
    vote(ps[1], 330, DSNET_PAUSE_VOTE, 1);
    vote(ps[2], 380, DSNET_PAUSE_VOTE, 0);   /* c1 still pausing */
    vote(ps[1], 450, DSNET_PAUSE_VOTE, 0);
    vote(ps[0], 500, DSNET_SPEED_VOTE, 3);
    vote(ps[1], 505, DSNET_SPEED_VOTE, 3);
    vote(ps[2], 560, DSNET_SPEED_VOTE, 2);
    vote(ps[2], 620, DSNET_SPEED_VOTE, 3);
    for (int i = 0; i < 3; i++) peer_go(ps[i]);
    CHECK(wait_done(ps, 3, 20000) == 0, "timeout");
    peer *h = ps[0];
    uint32_t p1 = exec_at(h, DSNET_PAUSE_VOTE, 1, 1, 0), r2 = exec_at(h, DSNET_PAUSE_VOTE, 2, 0, 0), r1 = exec_at(h, DSNET_PAUSE_VOTE, 1, 0, 0);
    uint32_t p2b = exec_at(h, DSNET_PAUSE_VOTE, 2, 1, 0), p1b = exec_at(h, DSNET_PAUSE_VOTE, 1, 1, 1);
    uint32_t r2b = exec_at(h, DSNET_PAUSE_VOTE, 2, 0, 1), r1b = exec_at(h, DSNET_PAUSE_VOTE, 1, 0, 1);
    uint32_t s0 = exec_at(h, DSNET_SPEED_VOTE, 0, 3, 0), s1 = exec_at(h, DSNET_SPEED_VOTE, 1, 3, 0);
    uint32_t s2 = exec_at(h, DSNET_SPEED_VOTE, 2, 2, 0), s3 = exec_at(h, DSNET_SPEED_VOTE, 2, 3, 0);
    CHECK(p1 && r2 && r1 && p2b && p1b && r2b && r1b && s0 && s1 && s2 && s3, "a vote never executed");
    int bad = 0;
    for (uint32_t t = 1; t < 680; t++) {
        int want = (t >= p1 && t < r1) || (t >= p2b && t < r1b) ? 0 : t >= s3 ? 3 : t >= s2 ? 2 : 1;
        if (h->tps_at[t] != want) { if (!bad) printf("  tick %u: %d game ticks, want %d\n", t, h->tps_at[t], want); bad++; }
    }
    CHECK(!bad, "%d ticks with wrong speed", bad);
    CHECK(r2 > p1 && r2 < r1 && r2b < r1b, "votes out of expected order");
    CHECK(h->hash_at[r1 - 1] == h->hash_at[p1], "state changed while paused");
    /* pause_by: c1 alone; then c2 paused first (c1 second) -> c2, after c2's run vote -> c1 */
    int bybad = 0;
    for (uint32_t t = 1; t < 680; t++) {
        int want = t >= p1 && t < r1 ? 1 : t >= p2b && t < r2b ? 2 : t >= r2b && t < r1b ? 1 : -1;
        if (h->by_at[t] != want) { if (!bybad) printf("  tick %u: pause_by %d, want %d\n", t, h->by_at[t], want); bybad++; }
    }
    CHECK(!bybad, "%d ticks with wrong pause_by", bybad);
    CHECK(dsnet_pause_mode(h->c) == DSNET_PAUSE_ANYONE && dsnet_allow_pause(h->c), "mode %d", dsnet_pause_mode(h->c));
    all_equal(ps, 3, 640);
    stop_all(ps, 3, srv);
    printf("%s pause_speed_vote (mode anyone, pause_by ok): paused %u-%u (c2's run vote at %u ignored) and %u-%u, speed 1->2->3 at %u/%u, all peers same (%.1fs)\n",
           ok ? "PASS" : "FAIL", p1, r1, r2, p2b, r1b, s2, s3, (now_ms() - t0) / 1000);
}

/* ---- 5. forced desync, detection, freeze, resync by save transfer ---- */
static void test_desync(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    if (setup(&srv, ps, 3, 900, NULL, NULL)) { printf("  FAIL setup\n"); failures++; return; }
    ps[2]->corrupt_at = 100; ps[2]->auto_resync = 1; ps[2]->vote_while_desynced = 1;
    ps[0]->save_pad = 4 << 20; ps[0]->watch_player = 2;
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 5; peer_go(ps[i]); }
    CHECK(wait_done(ps, 3, 25000) == 0, "timeout");
    CHECK((ps[0]->net_seen & (1u << DSNET_NET_RESYNCING)) && (ps[0]->net_seen & (1u << DSNET_NET_IN_SYNC)), "host saw c2 states %x", ps[0]->net_seen);
    size_t ov = dsnet_save_overhead(ps[0]->c);
    CHECK(ov < 2048, "save overhead %zu bytes with almost empty tables", ov);
    peer *d = ps[2];
    CHECK(d->desync_seen_at > 0, "desync never detected");
    CHECK(d->desync_bad == 120 && d->desync_good == 90, "bad tick %u last good %u, want 120 / 90", d->desync_bad, d->desync_good);
    CHECK(d->timer_after_wait == d->timer_at_desync, "kept ticking while desynced (%u -> %u)", d->timer_at_desync, d->timer_after_wait);
    CHECK(d->loads == 1 && !d->save_bad, "loads %d, save_bad %d", d->loads, d->save_bad);
    CHECK(ps[0]->upload_rc == 0 && ps[0]->oversize_rc == -1, "upload rc %d oversize rc %d", ps[0]->upload_rc, ps[0]->oversize_rc);
    CHECK(!dsnet_is_desynced(d->c, NULL, NULL), "still desynced after resync");
    CHECK(!ps[1]->desync_seen_at && !ps[0]->desync_seen_at, "a healthy peer flagged desync");
    int common;
    CHECK(compare(ps[0], ps[1], &common) && common > 850, "c1 diverged");
    CHECK(compare(ps[0], d, &common) && common > 500, "c2 diverged after resync (%d common)", common);
    /* auto pause on desync: paused from PAUSE_ALL until c2, back in sync, votes run; its vote while desynced is dropped */
    uint32_t pa = exec_at(ps[0], DSNET_PAUSE_ALL, DSNET_NO_PLAYER, 2, 0), run2 = exec_at(ps[0], DSNET_PAUSE_VOTE, 2, 0, 0);
    CHECK(pa && pa < d->load_tick, "no auto pause before the resync (pause at %u)", pa);
    CHECK(run2 >= d->load_tick, "c2's run vote ran at %u, before it was back in sync (load %u)", run2, d->load_tick);
    int bad = 0;
    for (uint32_t t = pa; t < run2 + 5 && t < MAXT; t++) if (ps[0]->tps_at[t] != (t < run2 ? 0 : 1)) bad++;
    CHECK(!bad && ps[0]->tps_at[pa - 1] == 1, "%d ticks with wrong pause state around %u-%u", bad, pa, run2);
    CHECK(ps[0]->by_at[pa] == 2 && ps[0]->by_at[run2 - 1] == 2 && ps[0]->by_at[run2] == -1, "pause_by %d/%d/%d, want 2/2/-1",
          ps[0]->by_at[pa], ps[0]->by_at[run2 - 1], ps[0]->by_at[run2]);
    stop_all(ps, 3, srv);
    printf("%s desync_resync: corrupt at 100 -> flagged at fingerprint 120 (last good 90), frozen at timer %u, "
           "auto pause %u-%u (desynced vote dropped), 4 MB save loaded at tick %u, then equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", d->timer_at_desync, pa, run2, d->load_tick, (now_ms() - t0) / 1000);
}

/* ---- 5b. lan_v10: forced desync -> partial resync (no pause, no load), report part to the host; and the fallback ---- */
static void test_partial(int fail) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    if (setup(&srv, ps, 3, 900, NULL, NULL)) { printf("  FAIL setup\n"); failures++; return; }
    peer *d = ps[2];
    d->corrupt_at = 100; d->partial = 1; d->auto_resync = 1; d->patch_fail = fail;
    dsnet_set_partial_resync(d->c, 1);
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 5; peer_go(ps[i]); }
    CHECK(wait_done(ps, 3, 25000) == 0, "timeout");
    dsnet_desync_info di; memset(&di, 0, sizeof di);
    if (ps[0]->report && ps[0]->report_len >= sizeof di) memcpy(&di, ps[0]->report, sizeof di);
    CHECK(d->desync_seen_at > 0 && d->desync_bad == 120 && d->desync_good == 90, "flag at %u (good %u)", d->desync_bad, d->desync_good);
    CHECK(ps[0]->report && ps[0]->report_from == 2 && ps[0]->report_len == 3000 && di.bad_tick == 120 && di.last_good == 90 &&
          di.mine[0] != di.host[0] && di.mine[4] == di.host[4] && ps[0]->report[2999] == (uint8_t)(2999 * 13),
          "report part at the host: from %d, %zu bytes, bad %u good %u", ps[0]->report_from, ps[0]->report_len, di.bad_tick, di.last_good);
    CHECK(ps[0]->patch_rc_host == 0, "host upload rc %d", ps[0]->patch_rc_host);
    uint32_t pa = exec_at(ps[0], DSNET_PAUSE_ALL, DSNET_NO_PLAYER, 2, 0);
    int common;
    if (!fail) {
        CHECK(d->patch_count == 1 && dsnet_patch_count(d->c) == 1 && d->loads == 0, "patches %u (dsnet %u), loads %d", d->patch_count, dsnet_patch_count(d->c), d->loads);
        CHECK(d->patch_tick > 120 && d->patch_tick < 200, "patched at step %u", d->patch_tick);
        CHECK(!pa, "partial resync paused everyone (PAUSE_ALL at %u)", pa);
        int paused = 0; for (uint32_t t = 0; t < 900; t++) if (ps[0]->have[t] && ps[0]->tps_at[t] == 0) paused++;
        CHECK(!paused, "%d host steps paused", paused);
    } else {
        CHECK(d->patch_tried && d->patch_count == 0 && d->loads == 1, "fallback: tried %d patches %u loads %d", d->patch_tried, d->patch_count, d->loads);
        CHECK(pa && pa <= d->load_tick, "fallback: no auto pause before the full resync (pause at %u, load %u)", pa, d->load_tick);
    }
    CHECK(!dsnet_is_desynced(d->c, NULL, NULL), "still desynced");
    CHECK(!ps[1]->desync_seen_at && !ps[0]->desync_seen_at, "a healthy peer flagged desync");
    CHECK(compare(ps[0], ps[1], &common) && common > 850, "c1 diverged");
    CHECK(compare(ps[0], d, &common) && common > 600, "c2 differs after the resync (%d common)", common);
    uint32_t pt = d->patch_tick, lt = d->load_tick;
    free(ps[0]->report);
    stop_all(ps, 3, srv);
    if (!fail)
        printf("%s partial_resync: corrupt at 100 -> flagged at 120, report part (3000 bytes) at the host, patch applied at step %u, "
               "no pause, no load, equal after (%.1fs)\n", ok ? "PASS" : "FAIL", pt, (now_ms() - t0) / 1000);
    else
        printf("%s partial_resync_fallback: game refused the patch -> auto pause + full resync (load at %u), equal after (%.1fs)\n",
               ok ? "PASS" : "FAIL", lt, (now_ms() - t0) / 1000);
}

static void peer_drop(peer *p);
/* ---- 5c. lan_v10 co-op markers: views + pings relayed outside the sim; spam limit; view cleared on leave ---- */
static void test_marks(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    if (setup(&srv, ps, 3, 500, NULL, NULL)) { printf("  FAIL setup\n"); failures++; return; }
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 5; peer_go(ps[i]); }
    while (ps[0]->timer < 50) sleep_ms(5);
    int s1 = dsnet_player_id(ps[1]->c), s2 = dsnet_player_id(ps[2]->c);
    CHECK(dsnet_view_send(ps[1]->c, 7, 10.5f, -3.25f, 8, 4.5f) == 0 && dsnet_view_send(ps[2]->c, 1, 1, 2, 3, 4) == 0, "view send");
    int adds = 0;
    for (int i = 0; i < 6; i++) adds += dsnet_mark_send(ps[1]->c, DSNET_MARK_ADD, 0xEE, i, 7, 100 + i, -20 - i) == 0;
    CHECK(dsnet_mark_send(ps[1]->c, DSNET_MARK_DEL, s2, 9, 7, 1, 2) == 0 && dsnet_mark_send(ps[1]->c, DSNET_MARK_DEL, s1, 0, 7, 100, -20) == 0, "delete send");
    sleep_ms(300);
    uint32_t mk; float r[4]; double age;
    CHECK(dsnet_view_of(ps[0]->c, s1, &mk, r, &age) == 0 && mk == 7 && r[0] == 10.5f && r[1] == -3.25f && r[2] == 8 && r[3] == 4.5f && age >= 0 && age < 1000,
          "host: c1's view");
    CHECK(dsnet_view_of(ps[2]->c, s1, &mk, r, NULL) == 0 && dsnet_view_of(ps[1]->c, s1, NULL, NULL, NULL) == -1, "c2 got it, the sender did not");
    int got[3][3] = { { 0 } }; dsnet_mark_ev ev; int bad = 0;
    for (int i = 0; i < 3; i++) while (dsnet_mark_poll(ps[i]->c, &ev)) {
        got[i][ev.op]++;
        if (ev.from != s1) bad++;
        if (ev.op == DSNET_MARK_ADD && (ev.owner != s1 || ev.map_key != 7 || ev.x != 100 + ev.id || ev.y != -20 - ev.id)) bad++;
    }
    CHECK(adds == 6 && got[0][1] == 4 && got[2][1] == 4 && got[0][2] == 2 && got[2][2] == 2 && !got[1][1] && !got[1][2] && !bad,
          "pings: host %d adds %d dels, c2 %d/%d, sender %d/%d, bad %d (want 4/2, 4/2, 0/0)", got[0][1], got[0][2], got[2][1], got[2][2], got[1][1], got[1][2], bad);
    uint32_t tl = ps[0]->timer;
    peer_drop(ps[2]); ps[2] = NULL;
    while (ps[0]->timer < tl + 40) sleep_ms(5);
    CHECK(dsnet_view_of(ps[0]->c, s2, NULL, NULL, NULL) == -1 && dsnet_view_of(ps[0]->c, s1, NULL, NULL, NULL) == 0, "view of the leaver not cleared");
    CHECK(wait_done(ps, 2, 20000) == 0, "timeout");
    int common;
    CHECK(compare(ps[0], ps[1], &common) && common > 450, "sim differs (%d common)", common);
    CHECK(!ps[0]->desync_seen_at && !ps[1]->desync_seen_at, "desync flagged");
    stop_all(ps, 2, srv);
    printf("%s marks: views relayed to the others only, 6 ping adds -> 4 relayed (spam limit), 2 deletes, view cleared on leave, sim equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", (now_ms() - t0) / 1000);
}

/* ---- 6b. lan_v10 bug: a 3rd player joins; the save is lossy and the host reloads it -> every guest must load it too ---- */
static void test_late_join3(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_cfg cfg = test_cfg(3);
    dsnet_server *srv; peer *ps[4];
    if (setup_cfg(&srv, ps, 3, 700, NULL, NULL, &cfg)) { printf("  FAIL setup\n"); failures++; return; }
    ps[0]->lossy_save = 1;
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 5; peer_go(ps[i]); }
    while (ps[0]->timer < 150) sleep_ms(5);
    ps[3] = peer_new("late", dsnet_client_connect_tcp("127.0.0.1", dsnet_server_port(srv), "late"), 700);
    CHECK(dsnet_wait_welcome(ps[3]->c, 2000) == 0, "no welcome");
    ps[3]->orders_every = 5; peer_go(ps[3]);
    CHECK(wait_done(ps, 4, 25000) == 0, "timeout");
    int common;
    for (int i = 1; i < 4; i++) {
        CHECK(!dsnet_is_desynced(ps[i]->c, NULL, NULL) && !ps[i]->desync_seen_at, "%s desynced", ps[i]->name);
        CHECK(ps[i]->loads == 1, "%s loads %d (want 1: every guest loads the join-point save)", ps[i]->name, ps[i]->loads);
        CHECK(compare(ps[0], ps[i], &common) && common > 300, "%s diverged (%d common)", ps[i]->name, common);
    }
    stop_all(ps, 4, srv);
    printf("%s late_join_3_players: lossy save + host reload at the join point, both earlier guests loaded it too, all equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", (now_ms() - t0) / 1000);
}

/* ---- 6. late joiner gets a 64 MB save ---- */
static void test_late_join(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_cfg cfg = test_cfg(2);
    dsnet_server *srv = dsnet_server_start(&cfg);
    peer *ps[3];
    ps[0] = peer_new("host", dsnet_client_connect_local(srv, "host"), 700);
    dsnet_wait_welcome(ps[0]->c, 2000);
    ps[1] = peer_new("c1", dsnet_client_connect_tcp("127.0.0.1", dsnet_server_port(srv), "c1"), 700);
    dsnet_wait_welcome(ps[1]->c, 2000);
    ps[0]->save_pad = DSNET_MAX_BLOB - DSNET_SAVE_OVERHEAD - sizeof(toy);    /* fits under the 64 MB cap */
    ps[0]->watch_player = 2;
    vote(ps[0], 50, DSNET_ADMIN_MODE, 1);                   /* must reach the late joiner through the save */
    for (int i = 0; i < 2; i++) { ps[i]->orders_every = 5; peer_go(ps[i]); }
    while (ps[0]->timer < 150) sleep_ms(5);
    double tj = now_ms();
    ps[2] = peer_new("late", dsnet_client_connect_tcp("127.0.0.1", dsnet_server_port(srv), "late"), 700);
    CHECK(dsnet_wait_welcome(ps[2]->c, 2000) == 0, "no welcome");
    CHECK(dsnet_needs_state(ps[2]->c), "late joiner should wait for a save");
    ps[2]->orders_every = 5; peer_go(ps[2]);
    while (!ps[2]->loads && now_ms() - tj < 15000) sleep_ms(5);
    double tl = now_ms() - tj;
    CHECK(wait_done(ps, 3, 25000) == 0, "timeout");
    CHECK(ps[0]->upload_rc == 0, "upload rc %d", ps[0]->upload_rc);
    CHECK(ps[2]->loads == 1 && !ps[2]->save_bad, "loads %d save_bad %d", ps[2]->loads, ps[2]->save_bad);
    for (int i = 0; i < 3; i++) CHECK(!dsnet_is_desynced(ps[i]->c, NULL, NULL), "%s desynced", ps[i]->name);
    int common;
    CHECK(compare(ps[0], ps[1], &common) && common > 650, "c1 diverged");
    CHECK(compare(ps[0], ps[2], &common) && common > 300, "late joiner diverged (%d common)", common);
    /* lobby getters: the late joiner got names/votes of earlier players through the save */
    for (int i = 0; i < 3; i++) {
        const char *want[3] = { "host", "c1", "late" };
        CHECK(dsnet_present_mask(ps[i]->c) == 7, "%s: mask %x", ps[i]->name, dsnet_present_mask(ps[i]->c));
        for (int k = 0; k < 3; k++) {
            const char *nm = dsnet_player_name(ps[i]->c, k);
            int sp = -1, pz = -1;
            CHECK(nm && !strcmp(nm, want[k]), "%s: name of %d is %s", ps[i]->name, k, nm ? nm : "NULL");
            CHECK(dsnet_vote_of(ps[i]->c, k, &sp, &pz) == 0 && sp == 1 && pz == 0, "%s: vote of %d = %d/%d", ps[i]->name, k, sp, pz);
        }
        CHECK(dsnet_player_name(ps[i]->c, 5) == NULL && dsnet_vote_of(ps[i]->c, 5, NULL, NULL) == -1, "absent player reported");
    }
    CHECK(ps[0]->busy_seen & 4, "host never saw the late joiner as busy");
    CHECK(dsnet_admin_mode(ps[2]->c) == 1 && dsnet_admin_mode(ps[1]->c) == 1, "admin mode not in the save");
    CHECK(ps[0]->net_seen & (1u << DSNET_NET_LOADING), "host never saw the late joiner as loading (%x)", ps[0]->net_seen);
    uint32_t lt = ps[2]->load_tick;
    uint32_t pa = exec_at(ps[0], DSNET_PAUSE_ALL, DSNET_NO_PLAYER, 2, 0), run2 = exec_at(ps[0], DSNET_PAUSE_VOTE, 2, 0, 0);
    CHECK(pa && pa <= lt && run2 >= lt, "auto pause %u, joiner run vote %u, load %u", pa, run2, lt);
    int bad = 0;
    for (uint32_t t = pa; t < run2 + 5 && t < MAXT; t++) if (ps[0]->tps_at[t] != (t < run2 ? 0 : 1)) bad++;
    CHECK(pa && !bad && ps[0]->tps_at[pa - 1] == 1, "%d ticks with wrong pause state around %u-%u", bad, pa, run2);
    stop_all(ps, 3, srv);
    printf("%s late_join_64mb: joined at ~150, auto pause %u-%u, 64 MB save loaded at tick %u %.0f ms after connect, then equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", pa, run2, lt, tl, (now_ms() - t0) / 1000);
}

/* ---- 7. join checks: build id, mod version, float rounding mode ---- */
static void test_handshake(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_cfg cfg = test_cfg(1);
    cfg.build_id = "b25846389"; cfg.mod_version = 7;
    dsnet_server *srv = dsnet_server_start(&cfg);
    uint16_t port = dsnet_server_port(srv);
    struct { const char *build; uint32_t mod; int round; int want; const char *what; } cases[] = {
        { "b25846389", 7, FE_TONEAREST,  0,                  "matching" },
        { "b99999999", 7, FE_TONEAREST,  DSNET_REJ_BUILD,    "other build" },
        { "b25846389", 8, FE_TONEAREST,  DSNET_REJ_MOD,      "other mod version" },
        { "b25846389", 7, FE_TOWARDZERO, DSNET_REJ_ROUNDING, "other rounding mode" },
    };
    char res[160] = ""; int pos = 0;
    for (int i = 0; i < 4; i++) {
        dsnet_set_identity(cases[i].build, cases[i].mod);
        int saved = fegetround(); fesetround(cases[i].round);   /* read on this (connecting) thread */
        dsnet_client *c = dsnet_client_connect_tcp("127.0.0.1", port, "hs");
        fesetround(saved);
        int r = dsnet_wait_welcome(c, 2000), why = dsnet_reject_reason(c);
        if (cases[i].want) CHECK(r == -2 && why == cases[i].want, "%s: rc %d reason %d", cases[i].what, r, why);
        else CHECK(r == 0 && why == 0, "%s: rc %d reason %d", cases[i].what, r, why);
        sleep_ms(50);
        CHECK(dsnet_is_disconnected(c) == (cases[i].want != 0), "%s: disconnected %d", cases[i].what, dsnet_is_disconnected(c));
        pos += snprintf(res + pos, sizeof res - pos, "%s%s=%s", i ? ", " : "", cases[i].what, r == 0 ? "in" : "rejected");
        dsnet_client_close(c);
    }
    dsnet_set_identity("", 0);
    dsnet_server_stop(srv);
    printf("%s join_checks: %s (%.1fs)\n", ok ? "PASS" : "FAIL", res, (now_ms() - t0) / 1000);
}

/* ---- 8. lobby: allow_speedup = 0 caps the speed at 1x ---- */
static void test_no_speedup(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    dsnet_cfg cfg = test_cfg(3); cfg.allow_speedup = 0;
    if (setup_cfg(&srv, ps, 3, 300, NULL, NULL, &cfg)) { printf("  FAIL setup\n"); failures++; return; }
    for (int i = 0; i < 3; i++) { vote(ps[i], 50 + i, DSNET_SPEED_VOTE, 3); ps[i]->orders_every = 5; }
    vote(ps[1], 100, DSNET_SPEED_VOTE, 1);                      /* 1x is still allowed */
    vote(ps[2], 150, DSNET_PAUSE_VOTE, 1); vote(ps[2], 200, DSNET_PAUSE_VOTE, 0);   /* pause still allowed */
    for (int i = 0; i < 3; i++) peer_go(ps[i]);
    CHECK(wait_done(ps, 3, 15000) == 0, "timeout");
    for (int i = 0; i < 3; i++) CHECK(dsnet_allow_speedup(ps[i]->c) == 0 && dsnet_allow_pause(ps[i]->c) == 1, "%s: lobby settings not received", ps[i]->name);
    int fast = 0, paused = 0;
    for (uint32_t t = 1; t < 300; t++) { if (ps[0]->tps_at[t] > 1) fast++; if (ps[0]->tps_at[t] == 0) paused++; }
    CHECK(!fast, "%d ticks above 1x", fast);
    CHECK(!exec_at(ps[0], DSNET_SPEED_VOTE, 0, 3, 0) && exec_at(ps[0], DSNET_SPEED_VOTE, 1, 1, 0), "speed votes not filtered");
    CHECK(paused > 20, "pause vote should still work (%d paused ticks)", paused);
    all_equal(ps, 3, 280);
    stop_all(ps, 3, srv);
    int sp = 0, pz = -1;
    CHECK(dsnet_vote_of(ps[0]->c, 1, &sp, &pz) == 0 && sp == 1 && pz == 0, "c1 vote %d/%d", sp, pz);
    CHECK(dsnet_vote_of(ps[0]->c, 0, &sp, NULL) == 0 && sp == 1, "host's dropped 3x vote shows as %d", sp);
    printf("%s lobby_no_speedup: 3x votes dropped, 1x vote kept, 0 ticks above 1x, pause still works (%d ticks), hashes equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", paused, (now_ms() - t0) / 1000);
}

/* ---- 9. lobby: allow_pause = 0: no pause votes, no auto pause; desynced + late player catch up while others play ---- */
static void test_no_pause(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[4];
    dsnet_cfg cfg = test_cfg(3); cfg.pause_mode = DSNET_PAUSE_NONE;
    if (setup_cfg(&srv, ps, 3, 900, NULL, NULL, &cfg)) { printf("  FAIL setup\n"); failures++; return; }
    ps[2]->corrupt_at = 100; ps[2]->auto_resync = 1; ps[2]->vote_while_desynced = 1;
    ps[0]->save_pad = 4 << 20;
    vote(ps[1], 50, DSNET_PAUSE_VOTE, 1);                       /* must be dropped */
    vote(ps[1], 500, DSNET_STORY_PAUSE, 1);                     /* story popup: allowed even so */
    vote(ps[1], 560, DSNET_STORY_PAUSE, 0);
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 5; peer_go(ps[i]); }
    while (ps[0]->timer < 300) sleep_ms(5);
    ps[3] = peer_new("late", dsnet_client_connect_tcp("127.0.0.1", dsnet_server_port(srv), "late"), 900);
    CHECK(dsnet_wait_welcome(ps[3]->c, 2000) == 0, "no welcome");
    ps[3]->orders_every = 5; peer_go(ps[3]);
    CHECK(wait_done(ps, 4, 25000) == 0, "timeout");
    uint32_t so = exec_at(ps[0], DSNET_STORY_PAUSE, 1, 1, 0), sc = exec_at(ps[0], DSNET_STORY_PAUSE, 1, 0, 0);
    CHECK(so >= 500 && sc > so, "story pause open %u close %u", so, sc);
    int paused = 0;
    for (uint32_t t = 1; t < 900; t++) if (ps[0]->tps_at[t] != (t >= so && t < sc ? 0 : 1)) paused++;
    CHECK(!paused, "%d host ticks with the wrong pause state (story window %u-%u)", paused, so, sc);
    for (int i = 0; i < 4; i++) CHECK(ps[i]->story_mask_seen == 2 && dsnet_story_pause_mask(ps[i]->c) == 0, "%s: story mask seen %x now %x",
                                      ps[i]->name, ps[i]->story_mask_seen, dsnet_story_pause_mask(ps[i]->c));
    CHECK(!exec_at(ps[0], DSNET_PAUSE_ALL, DSNET_NO_PLAYER, 0, 0) && !exec_at(ps[0], DSNET_PAUSE_VOTE, 1, 1, 0), "a pause got through");
    for (int i = 0; i < 4; i++) CHECK(dsnet_allow_pause(ps[i]->c) == 0 && dsnet_pause_mode(ps[i]->c) == DSNET_PAUSE_NONE, "%s: pause mode not received", ps[i]->name);
    int by = 0; for (uint32_t t = 1; t < 900; t++) if (ps[0]->by_at[t] != -1) by++;
    CHECK(!by, "%d ticks with pause_by set in mode none (story pause must give -1)", by);
    /* every join-point save goes to every guest (lan_v10): the resyncing peer may also load the late joiner's one */
    CHECK(ps[2]->desync_seen_at && ps[2]->loads >= 1 && ps[3]->loads == 1, "desync %d loads %d/%d", ps[2]->desync_seen_at > 0, ps[2]->loads, ps[3]->loads);
    int common;
    CHECK(compare(ps[0], ps[1], &common) && common > 850, "c1 diverged");
    CHECK(compare(ps[0], ps[2], &common) && common > 500, "resynced peer diverged (%d common)", common);
    CHECK(compare(ps[0], ps[3], &common) && common > 400, "late joiner diverged (%d common)", common);
    for (int i = 0; i < 4; i++) CHECK(!dsnet_is_desynced(ps[i]->c, NULL, NULL), "%s desynced at the end", ps[i]->name);
    uint32_t l2 = ps[2]->load_tick, l3 = ps[3]->load_tick;
    stop_all(ps, 4, srv);
    printf("%s lobby_no_pause (mode none): pause vote dropped, no auto pause, story pause %u-%u ran 0 ticks; desynced peer reloaded at %u and late joiner at %u caught up "
           "while host ran every other tick at 1x, hashes equal (%.1fs)\n", ok ? "PASS" : "FAIL", so, sc, l2, l3, (now_ms() - t0) / 1000);
}

/* ---- 10. barrier: a busy peer stops the timer for everyone; busy mask for the UI ---- */
static void test_busy(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    if (setup(&srv, ps, 3, 500, NULL, NULL)) { printf("  FAIL setup\n"); failures++; return; }
    ps[1]->busy_at = 200; ps[1]->busy_ms = 600;
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 5; peer_go(ps[i]); }
    int lead = 0;
    double end = now_ms() + 20000;
    while (!(ps[0]->done && ps[1]->done && ps[2]->done) && now_ms() < end) {
        int d = (int)ps[0]->timer - (int)ps[1]->timer;
        if (ps[1]->busy_since && !ps[1]->busy_done && d > lead) lead = d;
        sleep_ms(2);
    }
    CHECK(ps[0]->done && ps[1]->done && ps[2]->done, "timeout");
    dsnet_server_stats st; dsnet_server_get_stats(srv, &st);
    /* 600 ms busy = 120 steps; the hold must stop the others long before max_behind (90) */
    CHECK(lead < 30, "host ran %d steps ahead of the busy peer", lead);
    CHECK(st.held_steps > 50, "timer not held (%u)", st.held_steps);
    for (int i = 0; i < 3; i++) CHECK(ps[i]->busy_seen == 2, "%s saw busy mask %x", ps[i]->name, ps[i]->busy_seen);
    for (int i = 0; i < 3; i++) CHECK(dsnet_busy_mask(ps[i]->c) == 0, "%s: busy mask not cleared", ps[i]->name);
    all_equal(ps, 3, 480);
    stop_all(ps, 3, srv);
    printf("%s barrier_busy: c1 busy 600 ms at step 200 -> host lead max %d steps, held %u, busy mask seen by all, hashes equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", lead, st.held_steps, (now_ms() - t0) / 1000);
}

/* ---- 11. pause mode ALL: paused only when every player votes pause; any run vote resumes ---- */
static void test_pause_all_mode(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    dsnet_cfg cfg = test_cfg(3); cfg.pause_mode = DSNET_PAUSE_ALL_VOTE;
    if (setup_cfg(&srv, ps, 3, 420, NULL, NULL, &cfg)) { printf("  FAIL setup\n"); failures++; return; }
    vote(ps[1], 100, DSNET_PAUSE_VOTE, 1);
    vote(ps[2], 120, DSNET_PAUSE_VOTE, 1);
    vote(ps[0], 140, DSNET_PAUSE_VOTE, 1);   /* now all three -> paused, caused by c1 (first) */
    vote(ps[2], 200, DSNET_PAUSE_VOTE, 0);   /* one run vote resumes */
    vote(ps[0], 260, DSNET_STORY_PAUSE, 1);  /* story pause still pauses */
    vote(ps[0], 300, DSNET_STORY_PAUSE, 0);
    for (int i = 0; i < 3; i++) peer_go(ps[i]);
    CHECK(wait_done(ps, 3, 15000) == 0, "timeout");
    peer *h = ps[0];
    uint32_t a = exec_at(h, DSNET_PAUSE_VOTE, 0, 1, 0), r = exec_at(h, DSNET_PAUSE_VOTE, 2, 0, 0);
    uint32_t so = exec_at(h, DSNET_STORY_PAUSE, 0, 1, 0), sc = exec_at(h, DSNET_STORY_PAUSE, 0, 0, 0);
    CHECK(a && r > a && so > r && sc > so, "votes %u %u %u %u", a, r, so, sc);
    int bad = 0;
    for (uint32_t t = 1; t < 420; t++) {
        int pz = (t >= a && t < r) || (t >= so && t < sc);
        int by = t >= a && t < r ? 1 : -1;
        if ((h->tps_at[t] == 0) != pz || h->by_at[t] != by) { if (!bad) printf("  tick %u: tps %d by %d\n", t, h->tps_at[t], h->by_at[t]); bad++; }
    }
    CHECK(!bad, "%d ticks wrong (paused only %u-%u by c1, story %u-%u)", bad, a, r, so, sc);
    for (int i = 0; i < 3; i++) CHECK(dsnet_pause_mode(ps[i]->c) == DSNET_PAUSE_ALL_VOTE, "%s mode", ps[i]->name);
    all_equal(ps, 3, 400);
    stop_all(ps, 3, srv);
    printf("%s pause_mode_all: 1 and 2 of 3 pause votes ran on; paused %u-%u when all 3 voted (pause_by c1), one run vote resumed; story %u-%u paused (by -1) (%.1fs)\n",
           ok ? "PASS" : "FAIL", a, r, so, sc, (now_ms() - t0) / 1000);
}

/* ---- 12. bench + expedition ready + recruit owner ---- */
static void guid(uint8_t g[16], uint8_t v) { memset(g, v, 16); }
static void test_expedition(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[4];
    dsnet_cfg ec = test_cfg(4); ec.units_per_player = 6;                 /* bench of 5: the largest unit rule */
    if (setup_cfg(&srv, ps, 4, 400, NULL, NULL, &ec)) { printf("  FAIL setup\n"); failures++; return; }
    uint8_t b[128];
    /* c1 owns 5 units (bench), c2 owns 2, host 1, the leaver 2 */
    for (int k = 0; k < 5; k++) { guid(b, 0x10 + k); b[16] = 1; script_add(ps[1], 20 + k, DSNET_OWNERSHIP, b, 17); }
    for (int k = 0; k < 2; k++) { guid(b, 0x20 + k); b[16] = 2; script_add(ps[2], 20 + k, DSNET_OWNERSHIP, b, 17); }
    for (int k = 0; k < 2; k++) { guid(b, 0x40 + k); b[16] = 3; script_add(ps[3], 20 + k, DSNET_OWNERSHIP, b, 17); }
    guid(b, 0x30); b[16] = 0; script_add(ps[0], 20, DSNET_OWNERSHIP, b, 17);
    /* refused: 7 units (over the largest party), a foreign unit, a duplicate; ready with no units is allowed (viewer) */
    b[0] = 1; b[1] = 7; for (int k = 0; k < 7; k++) guid(b + 2 + 16 * k, 0x10 + k); script_add(ps[1], 60, DSNET_EXPEDITION_READY, b, 114);
    b[0] = 1; b[1] = 2; guid(b + 2, 0x10); guid(b + 18, 0x20); script_add(ps[1], 64, DSNET_EXPEDITION_READY, b, 34);
    b[0] = 1; b[1] = 2; guid(b + 2, 0x10); guid(b + 18, 0x10); script_add(ps[1], 68, DSNET_EXPEDITION_READY, b, 34);
    b[0] = 1; b[1] = 0; script_add(ps[1], 72, DSNET_EXPEDITION_READY, b, 2);
    b[0] = 0; b[1] = 2; guid(b + 2, 0x12); guid(b + 18, 0x13); script_add(ps[1], 80, DSNET_EXPEDITION_READY, b, 34);   /* marks only */
    b[0] = 1; b[1] = 3; guid(b + 2, 0x12); guid(b + 18, 0x13); guid(b + 34, 0x14); script_add(ps[1], 90, DSNET_EXPEDITION_READY, b, 50);
    guid(b, 0x10); b[16] = 0; script_add(ps[0], 100, DSNET_OWNERSHIP, b, 17);   /* admin off: host may not take c1's unit */
    guid(b, 0x31); b[16] = 1; script_add(ps[0], 102, DSNET_OWNERSHIP, b, 17);   /* ... but may give a free one */
    vote(ps[0], 110, DSNET_ADMIN_MODE, 1);
    guid(b, 0x14); b[16] = 2; script_add(ps[0], 120, DSNET_OWNERSHIP, b, 17);   /* admin on: host gives c1's marked unit to c2 */
    b[0] = 1; b[1] = 2; guid(b + 2, 0x20); guid(b + 18, 0x21); script_add(ps[2], 130, DSNET_EXPEDITION_READY, b, 34);
    b[0] = 1; b[1] = 3; guid(b + 2, 0x12); guid(b + 18, 0x13); guid(b + 34, 0x11); script_add(ps[1], 150, DSNET_EXPEDITION_READY, b, 50);
    b[0] = 1; b[1] = 1; guid(b + 2, 0x30); script_add(ps[0], 170, DSNET_EXPEDITION_READY, b, 18);   /* last one -> start */
    /* recruits: c2 and host recruit; the new units belong to them */
    uint8_t rc[2] = { 0xF0, 0x41 }; script_add(ps[2], 200, DSNET_GAME_ORDER, rc, 2);
    uint8_t rh[2] = { 0xF0, 0x42 }; script_add(ps[0], 210, DSNET_GAME_ORDER, rh, 2);
    for (int i = 0; i < 4; i++) peer_go(ps[i]);
    uint32_t m_marks = 0, m_after_move = 0, m_before_host = 0; int marks_n = -1, owned_leaver = -1;
    while (!(ps[0]->done && ps[1]->done && ps[2]->done)) {
        uint32_t t = ps[0]->timer;
        if (ps[3] && t >= 100) {                     /* the leaver quits; his units become unclaimed */
            uint8_t lv[2][16];
            owned_leaver = dsnet_owned_list(ps[0]->c, 3, lv, 2);
            ps[3]->stop = 1; pthread_join(ps[3]->th, NULL); dsnet_client_close(ps[3]->c); free(ps[3]); ps[3] = NULL;
        }
        if (t > 86 && t < 92) { uint8_t mk[3][16]; marks_n = dsnet_marked_of(ps[0]->c, 1, mk); m_marks |= dsnet_ready_mask(ps[0]->c); }
        if (t > 140 && t < 148) m_after_move |= dsnet_ready_mask(ps[0]->c);
        if (t > 160 && t < 168) m_before_host |= dsnet_ready_mask(ps[0]->c);
        sleep_ms(1);
    }
    uint8_t g[16]; guid(g, 0);
    ps[0]->recruit_rc_outside = dsnet_set_recruit_owner(ps[0]->c, g);
    CHECK(ps[0]->recruit_rc_outside == -1, "recruit owner outside a callback must fail");
    CHECK(owned_leaver == 2, "leaver owned %d before leaving", owned_leaver);
    CHECK(marks_n == 2 && !(m_marks & 2), "marks without ready: n %d, ready mask %x", marks_n, m_marks);
    CHECK(m_after_move == 4, "after the host moved c1's marked unit: ready mask %x, want 4 (c1 cleared)", m_after_move);
    CHECK(m_before_host == 6, "ready mask before the host was ready: %x, want 6", m_before_host);
    uint8_t want[200], *w = want; *w++ = 3;
    *w++ = 0; *w++ = 1; guid(w, 0x30); w += 16;
    *w++ = 1; *w++ = 3; guid(w, 0x12); guid(w + 16, 0x13); guid(w + 32, 0x11); w += 48;
    *w++ = 2; *w++ = 2; guid(w, 0x20); guid(w + 16, 0x21); w += 32;
    for (int i = 0; i < 3; i++) {
        peer *q = ps[i];
        CHECK(q->starts == 1, "%s: %d starts", q->name, q->starts);
        CHECK(q->start_tick == ps[0]->start_tick, "%s: start at %u vs host %u", q->name, q->start_tick, ps[0]->start_tick);
        CHECK(q->start_len == w - want && !memcmp(q->start_buf, want, w - want), "%s: start payload wrong (len %d)", q->name, q->start_len);
        CHECK(dsnet_ready_mask(q->c) == 0, "%s: ready flags not reset", q->name);
        uint8_t mk[3][16]; CHECK(dsnet_marked_of(q->c, 1, mk) == 3 && mk[2][0] == 0x11, "%s: marks not kept after start", q->name);
        CHECK(dsnet_marked_of(q->c, 3, mk) == -1, "%s: leaver still present", q->name);
        uint8_t u[16];
        guid(u, 0x13); CHECK(dsnet_owner_of(q->c, u) == 1, "%s: bench unit", q->name);
        guid(u, 0x10); CHECK(dsnet_owner_of(q->c, u) == 1, "%s: host took c1's unit without admin mode", q->name);
        guid(u, 0x31); CHECK(dsnet_owner_of(q->c, u) == 1, "%s: host could not give a free unit", q->name);
        guid(u, 0x14); CHECK(dsnet_owner_of(q->c, u) == 2, "%s: reassigned unit", q->name);
        guid(u, 0x40); CHECK(dsnet_owner_of(q->c, u) == DSNET_OWNER_RESERVED, "%s: leaver's unit not reserved (%d)", q->name, dsnet_owner_of(q->c, u));
        uint8_t ol[8][16];
        CHECK(dsnet_owned_list(q->c, 1, ol, 8) == 5 && dsnet_owned_list(q->c, 2, ol, 8) == 4 && dsnet_owned_list(q->c, 3, ol, 8) == 0,
              "%s: owned counts %d/%d/%d", q->name, dsnet_owned_list(q->c, 1, ol, 8), dsnet_owned_list(q->c, 2, ol, 8), dsnet_owned_list(q->c, 3, ol, 8));
        uint8_t r1[16]; memset(r1, 0x41, 16); r1[0] = 0xF0; CHECK(dsnet_owner_of(q->c, r1) == 2, "%s: c2's recruit owner %d", q->name, dsnet_owner_of(q->c, r1));
        memset(r1, 0x42, 16); r1[0] = 0xF0; CHECK(dsnet_owner_of(q->c, r1) == 0, "%s: host's recruit owner", q->name);
        uint8_t party[5][16];
        for (int k = 0; k < 4; k++) guid(party[k], 0x10 + k);
        CHECK(dsnet_party_ok(q->c, (const uint8_t (*)[16])party, 4) == 0, "%s: 4 of c1's units pass under the 6-unit rule", q->name);
        guid(party[3], 0x20); guid(party[4], 0x30);
        CHECK(dsnet_party_ok(q->c, (const uint8_t (*)[16])party, 5) == 0, "%s: 3+1+1 must pass", q->name);
    }
    uint64_t oh[3], ih[3];
    for (int i = 0; i < 3; i++) dsnet_owner_hashes(ps[i]->c, &oh[i], &ih[i], NULL, NULL);
    CHECK(oh[0] == oh[1] && oh[0] == oh[2] && ih[0] == ih[2], "owner hashes differ between peers");
    all_equal(ps, 3, 380);
    uint32_t st = ps[0]->start_tick;
    stop_all(ps, 3, srv);
    printf("%s expedition_bench_recruit: bench of 5; bad parties refused; marks without ready; moved unit clears ready; leaver's units reserved; "
           "start once at step %u on all peers with 3+2+1, marks kept; recruits owned by recruiter; party check ok (%.1fs)\n",
           ok ? "PASS" : "FAIL", st, (now_ms() - t0) / 1000);
}

/* ---- 13. gear modes: who may equip which unit, item owner tags ---- */
static void test_gear(void) {
    int ok = 1; double t0 = now_ms();
    char res[200] = ""; int pos = 0;
    for (int mode = 0; mode < 3; mode++) {
        dsnet_server *srv; peer *ps[2];
        dsnet_cfg cfg = test_cfg(2); cfg.gear_mode = mode;
        if (setup_cfg(&srv, ps, 2, 150, NULL, NULL, &cfg)) { printf("  FAIL setup\n"); failures++; return; }
        uint8_t b[17];
        guid(b, 0x51); b[16] = 1; script_add(ps[1], 10, DSNET_OWNERSHIP, b, 17);       /* c1's unit */
        guid(b, 0x50); b[16] = 0; script_add(ps[0], 10, DSNET_OWNERSHIP, b, 17);       /* host's unit; 0x52 unclaimed */
        guid(b, 0x61); b[16] = 1; script_add(ps[1], 20, DSNET_ITEM_OWNER, b, 17);      /* c1 tags a free item */
        guid(b, 0x62); b[16] = 0; script_add(ps[0], 20, DSNET_ITEM_OWNER, b, 17);      /* host tags one */
        guid(b, 0x62); b[16] = 1; script_add(ps[1], 40, DSNET_ITEM_OWNER, b, 17);      /* c1 grabs host's: refused */
        guid(b, 0x63); b[16] = 0; script_add(ps[0], 50, DSNET_ITEM_OWNER, b, 17);
        guid(b, 0x63); b[16] = 1; script_add(ps[0], 60, DSNET_ITEM_OWNER, b, 17);      /* host may give it to c1 */
        uint8_t cr[2] = { 0xF1, 0x64 }; script_add(ps[1], 70, DSNET_GAME_ORDER, cr, 2); /* c1 crafts -> his */
        guid(b, 0x61); b[16] = 0; script_add(ps[0], 75, DSNET_ITEM_OWNER, b, 17);      /* admin off: dropped */
        if (mode == 0) { vote(ps[0], 80, DSNET_ADMIN_MODE, 1); vote(ps[1], 90, DSNET_ADMIN_MODE, 0); }  /* c1's is dropped */
        guid(b, 0x51); b[16] = 0; script_add(ps[0], 100, DSNET_OWNERSHIP, b, 17);      /* only with admin: c1's unit -> host */
        guid(b, 0x61); b[16] = 0; script_add(ps[0], 105, DSNET_ITEM_OWNER, b, 17);     /* only with admin: c1's item -> host */
        for (int i = 0; i < 2; i++) peer_go(ps[i]);
        CHECK(wait_done(ps, 2, 10000) == 0, "timeout");
        for (int i = 0; i < 2; i++) {
            dsnet_client *c = ps[i]->c;
            uint8_t u0[16], u1[16], u2[16], i1[16], i2[16], i3[16], i4[16], i9[16];
            guid(u0, 0x50); guid(u1, 0x51); guid(u2, 0x52); guid(i1, 0x61); guid(i2, 0x62); guid(i3, 0x63); guid(i9, 0x69);
            memset(i4, 0x64, 16); i4[0] = 0xF1;
            CHECK(dsnet_gear_mode(c) == mode, "mode %d not received", mode);
            int any = mode == DSNET_GEAR_SHARED_ANY;
            int adm = mode == 0;               /* admin mode switched on by the host in the mode-0 run */
            CHECK(dsnet_admin_mode(c) == adm, "mode %d: admin %d", mode, dsnet_admin_mode(c));
            CHECK(dsnet_may_equip(c, 1, u1) == (any || !adm) && dsnet_may_equip(c, 1, u0) == any && dsnet_may_equip(c, 1, u2) == any
                  && dsnet_may_equip(c, 0, u2) == 1 && dsnet_may_equip(c, 0, u1) == (any || adm), "mode %d: equip rules", mode);
            CHECK(dsnet_item_owner(c, i1) == (adm ? 0 : 1) && dsnet_owner_of(c, u1) == (adm ? 0 : 1), "mode %d: admin re-own %d %d",
                  mode, dsnet_item_owner(c, i1), dsnet_owner_of(c, u1));
            CHECK(dsnet_item_owner(c, i2) == 0 && dsnet_item_owner(c, i3) == 1
                  && dsnet_item_owner(c, i4) == 1 && dsnet_item_owner(c, i9) == -1, "mode %d: item tags %d %d %d %d", mode,
                  dsnet_item_owner(c, i1), dsnet_item_owner(c, i2), dsnet_item_owner(c, i3), dsnet_item_owner(c, i4));
            int own = mode == DSNET_GEAR_OWN_STORAGE;
            CHECK(dsnet_may_use_item(c, 1, i1) == 1 && dsnet_may_use_item(c, 1, i2) == !own && dsnet_may_use_item(c, 0, i4) == !own
                  && dsnet_may_use_item(c, 0, i9) == 1, "mode %d: item use rules", mode);
        }
        all_equal(ps, 2, 140);
        stop_all(ps, 2, srv);
        pos += snprintf(res + pos, sizeof res - pos, "%smode %d ok", mode ? ", " : "", mode);
    }
    printf("%s gear_modes: %s, admin on by host / off by c1 dropped (equip own/unclaimed-by-host vs any; item tags claim/refuse/host-give/craft; own-storage use) (%.1fs)\n",
           ok ? "PASS" : "FAIL", res, (now_ms() - t0) / 1000);
}

/* ---- 14. guest with no campaign: asks for the save before the start; min_players counts him ---- */
static void test_prestart_save(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_cfg cfg = test_cfg(2);
    dsnet_server *srv = dsnet_server_start(&cfg);
    peer *ps[2];
    ps[0] = peer_new("host", dsnet_client_connect_local(srv, "host"), 300);
    dsnet_wait_welcome(ps[0]->c, 2000);
    ps[0]->save_pad = 1 << 20;
    peer_go(ps[0]);
    sleep_ms(100);
    dsnet_server_stats st; dsnet_server_get_stats(srv, &st);
    CHECK(st.timer == 0, "timer started with 1 of 2 players (%u)", st.timer);
    ps[1] = peer_new("guest", dsnet_client_connect_tcp("127.0.0.1", dsnet_server_port(srv), "guest"), 300);
    CHECK(dsnet_wait_welcome(ps[1]->c, 2000) == 0, "no welcome");
    dsnet_request_resync(ps[1]->c);          /* no campaign yet: wants the host's save */
    ps[0]->orders_every = 5; ps[1]->orders_every = 5;
    peer_go(ps[1]);
    CHECK(wait_done(ps, 2, 15000) == 0, "timeout: timer never started or save never came (host timer %u)", ps[0]->timer);
    CHECK(ps[1]->loads == 1 && !ps[1]->save_bad, "guest loads %d", ps[1]->loads);
    int common;
    CHECK(compare(ps[0], ps[1], &common) && common > 250, "guest diverged (%d common)", common);
    uint32_t lt = ps[1]->load_tick;
    stop_all(ps, 2, srv);
    printf("%s prestart_save: min_players 2, guest waiting for a save counts; timer started, 1 MB save loaded at step %u, then equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", lt, (now_ms() - t0) / 1000);
}

/* ---- 15. silent cut: guest and server both notice within ~2 s; the others go on ---- */
static void test_silent_cut(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    int lat[3] = { 0, 0, 1 }, jit[3] = { 0, 0, 0 };
    if (setup(&srv, ps, 3, 600, lat, jit)) { printf("  FAIL setup\n"); failures++; return; }
    proxy *px = last_proxy;
    dsnet_set_rx_timeout_ms(2000);           /* short limit to keep the test fast */
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 5; peer_go(ps[i]); }
    while (ps[0]->timer < 150) sleep_ms(2);
    proxy_freeze(px);
    double tf = now_ms(), t_cli = 0, t_srv = 0;
    while (now_ms() - tf < 5000 && (!t_cli || !t_srv)) {
        if (!t_cli && dsnet_is_disconnected(ps[2]->c)) t_cli = now_ms() - tf;
        if (!t_srv && !(dsnet_present_mask(ps[0]->c) & 4)) t_srv = now_ms() - tf;
        sleep_ms(5);
    }
    peer *live[2] = { ps[0], ps[1] };
    CHECK(wait_done(live, 2, 15000) == 0, "host / c1 did not go on after the cut");
    CHECK(t_cli >= 1500 && t_cli <= 3000, "guest noticed after %.0f ms", t_cli);
    CHECK(t_srv >= 1500 && t_srv <= 3000, "server dropped the guest after %.0f ms", t_srv);
    CHECK(!dsnet_is_disconnected(ps[0]->c) && !dsnet_is_disconnected(ps[1]->c), "a healthy client disconnected");
    CHECK(!dsnet_host_left(ps[2]->c), "a cut guest was told the host left (ux_flow_v10 #2)");
    int common;
    CHECK(compare(ps[0], ps[1], &common) && common > 550, "c1 diverged");
    CHECK(compare(ps[0], ps[2], &common), "cut guest diverged before the cut");
    stop_all(ps, 3, srv);
    dsnet_set_rx_timeout_ms(5000);
    printf("%s silent_cut: proxy frozen at step 150 -> guest disconnected after %.0f ms, server dropped it after %.0f ms, others ran on (%.1fs)\n",
           ok ? "PASS" : "FAIL", t_cli, t_srv, (now_ms() - t0) / 1000);
}

/* ---- the host ends the session: guests get P_BYE (dsnet_host_left), unlike their own link dropping (ux_flow_v10 #2) ---- */
static void test_bye(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[2];
    if (setup(&srv, ps, 2, 100000, NULL, NULL)) { printf("  FAIL setup\n"); failures++; return; }
    for (int i = 0; i < 2; i++) peer_go(ps[i]);
    while (ps[1]->timer < 60 && now_ms() - t0 < 5000) sleep_ms(2);
    CHECK(!dsnet_host_left(ps[1]->c), "BYE before the end");
    dsnet_server_stop(srv);
    double tf = now_ms();
    while (!dsnet_is_disconnected(ps[1]->c) && now_ms() - tf < 3000) sleep_ms(2);
    double dt = now_ms() - tf;
    CHECK(dsnet_is_disconnected(ps[1]->c) && dsnet_host_left(ps[1]->c), "guest: disconnected %d, host_left %d", dsnet_is_disconnected(ps[1]->c), dsnet_host_left(ps[1]->c));
    stop_all(ps, 2, NULL);
    printf("%s bye: server stop -> guest disconnected after %.0f ms with host_left set (own cut: not set, silent_cut) (%.1fs)\n",
           ok ? "PASS" : "FAIL", dt, (now_ms() - t0) / 1000);
}

/* ---- 16. stable player ids: reserved while away, back after rejoin in another slot, admin release, world export ---- */
static dsnet_client *connect_as(dsnet_server *srv, const char *name, uint8_t id, int local) {
    uint8_t uid[16]; memset(uid, id, 16);
    dsnet_set_player_uid(uid);
    dsnet_client *c = local ? dsnet_client_connect_local(srv, name) : dsnet_client_connect_tcp("127.0.0.1", dsnet_server_port(srv), name);
    dsnet_set_player_uid(NULL);
    return c;
}
static void peer_drop(peer *p) { p->stop = 1; pthread_join(p->th, NULL); dsnet_client_close(p->c); free(p); }
static void test_identity(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_cfg cfg = test_cfg(3);
    dsnet_server *srv = dsnet_server_start(&cfg);
    peer *h = peer_new("host", connect_as(srv, "host", 0xA0, 1), 420);  dsnet_wait_welcome(h->c, 2000);
    peer *c1 = peer_new("c1", connect_as(srv, "c1", 0xA1, 0), 420);       dsnet_wait_welcome(c1->c, 2000);
    peer *c2 = peer_new("c2", connect_as(srv, "c2", 0xA2, 0), 420);       dsnet_wait_welcome(c2->c, 2000);
    /* a second connection with c1's id is refused */
    dsnet_client *dup = connect_as(srv, "c1again", 0xA1, 0);
    CHECK(dsnet_wait_welcome(dup, 2000) == -2 && dsnet_reject_reason(dup) == DSNET_REJ_DUP_ID, "duplicate id not refused (%d)", dsnet_reject_reason(dup));
    dsnet_client_close(dup);
    uint8_t b[17];
    guid(b, 0x71); b[16] = 1; script_add(c1, 20, DSNET_OWNERSHIP, b, 17);
    guid(b, 0x72); b[16] = 1; script_add(c1, 21, DSNET_OWNERSHIP, b, 17);
    guid(b, 0x73); b[16] = 2; script_add(c2, 20, DSNET_OWNERSHIP, b, 17);
    guid(b, 0x81); b[16] = 2; script_add(c2, 22, DSNET_ITEM_OWNER, b, 17);
    guid(b, 0x73); b[16] = 1; script_add(c1, 130, DSNET_OWNERSHIP, b, 17);      /* reserved: refused */
    guid(b, 0x73); b[16] = 1; script_add(h, 135, DSNET_OWNERSHIP, b, 17);       /* host, admin off: refused */
    guid(b, 0x75); b[16] = DSNET_NO_PLAYER; script_add(h, 320, DSNET_OWNERSHIP, b, 17);   /* c3's, reserved, admin off: refused */
    vote(h, 330, DSNET_ADMIN_MODE, 1);
    guid(b, 0x76); b[16] = DSNET_NO_PLAYER; script_add(h, 340, DSNET_OWNERSHIP, b, 17);   /* admin: release reserved */
    guid(b, 0x71); b[16] = 0; script_add(h, 350, DSNET_OWNERSHIP, b, 17);                 /* admin: take c1's */
    peer_go(h); peer_go(c1); peer_go(c2);
    peer *c3 = NULL, *c2b = NULL;
    int reserved_seen = 0, reserved_refused = 1, r76 = 0; char rname[32] = "";
    while (!(h->done && c1->done && (!c2b || c2b->done))) {
        uint32_t t = h->timer;
        if (c2 && t >= 100) { peer_drop(c2); c2 = NULL; }
        if (!c2 && !c3 && t >= 150 && !c2b) {                      /* someone new takes slot 2 */
            c3 = peer_new("c3", connect_as(srv, "c3", 0xA3, 0), 420); dsnet_wait_welcome(c3->c, 2000);
            guid(b, 0x75); b[16] = 2; script_add(c3, 230, DSNET_OWNERSHIP, b, 17);
            guid(b, 0x76); b[16] = 2; script_add(c3, 231, DSNET_OWNERSHIP, b, 17);
            peer_go(c3);
        }
        if (t > 120 && t < 145) {
            uint8_t g[16]; guid(g, 0x73);
            if (dsnet_owner_info(h->c, g, NULL, rname) == DSNET_OWNER_RESERVED) reserved_seen = 1;
        }
        if (t > 145 && t < 190) { uint8_t g[16]; guid(g, 0x73); if (dsnet_owner_of(h->c, g) != DSNET_OWNER_RESERVED) reserved_refused = 0; }
        if (c3 && !c2b && t >= 200) {                               /* c2 comes back: he gets slot 3 now */
            c2b = peer_new("c2b", connect_as(srv, "c2", 0xA2, 0), 420); dsnet_wait_welcome(c2b->c, 2000);
            peer_go(c2b);
        }
        if (c3 && t >= 300 && c3->timer > 240) { peer_drop(c3); c3 = NULL; }
        if (!c3 && c2b && t > 315 && t < 335) { uint8_t g[16]; guid(g, 0x76); if (dsnet_owner_of(h->c, g) == DSNET_OWNER_RESERVED) r76 = 1; }
        sleep_ms(2);
        if (now_ms() - t0 > 25000) { CHECK(0, "timeout"); break; }
    }
    CHECK(reserved_seen && !strcmp(rname, "c2"), "0x73 not shown as reserved for c2 (name '%s')", rname);
    CHECK(reserved_refused, "someone took c2's reserved unit");
    CHECK(r76, "0x76 was never reserved before the admin release");
    peer *all[3] = { h, c1, c2b };
    for (int i = 0; i < 3; i++) {
        dsnet_client *c = all[i]->c; uint8_t g[16]; char nm[32];
        guid(g, 0x73); CHECK(dsnet_owner_of(c, g) == 3 && dsnet_owner_info(c, g, NULL, nm) == 3 && !strcmp(nm, "c2"),
                             "%s: rejoined c2 (slot 3) does not own 0x73 (%d)", all[i]->name, dsnet_owner_of(c, g));
        guid(g, 0x81); CHECK(dsnet_item_owner(c, g) == 3, "%s: c2's item owner %d", all[i]->name, dsnet_item_owner(c, g));
        guid(g, 0x75); CHECK(dsnet_owner_of(c, g) == DSNET_OWNER_RESERVED && dsnet_owner_info(c, g, NULL, nm) == -2 && !strcmp(nm, "c3"),
                             "%s: 0x75 should stay reserved for c3 (%d '%s')", all[i]->name, dsnet_owner_of(c, g), nm);
        guid(g, 0x76); CHECK(dsnet_owner_of(c, g) == -1, "%s: admin release of a reserved unit failed (%d)", all[i]->name, dsnet_owner_of(c, g));
        guid(g, 0x71); CHECK(dsnet_owner_of(c, g) == 0, "%s: admin take failed", all[i]->name);
        CHECK(dsnet_may_equip(c, 1, (guid(g, 0x75), g)) == 0, "%s: reserved unit equippable", all[i]->name);
        /* colours follow the uid (owner 2026-10-14): host 0, c1 1, c2 2 (c3 took 3); c2 back in slot 3 keeps 2 */
        CHECK(dsnet_player_color(c, 0) == 0 && dsnet_player_color(c, 1) == 1 && dsnet_player_color(c, 3) == 2 && dsnet_player_color(c, 2) == -1,
              "%s: colours by uid %d %d %d %d", all[i]->name, dsnet_player_color(c, 0), dsnet_player_color(c, 1), dsnet_player_color(c, 2), dsnet_player_color(c, 3));
    }
    int common;
    CHECK(compare(h, c1, &common) && common > 380, "c1 diverged");
    CHECK(compare(h, c2b, &common) && common > 100, "rejoined c2 diverged (%d)", common);
    /* export with the world, new session: c2 is back first (slot 1 now), c1 is away -> reserved */
    size_t need = dsnet_export_owners(h->c, NULL, 0);
    uint8_t *blob = malloc(need);
    CHECK(dsnet_export_owners(h->c, blob, need) == need, "export size");
    h->stop = 1; c1->stop = 1; c2b->stop = 1;
    peer *ps1[3] = { h, c1, c2b };
    stop_all(ps1, 3, srv);
    dsnet_cfg cfg2 = test_cfg(2);
    dsnet_server *srv2 = dsnet_server_start(&cfg2);
    peer *h2 = peer_new("host2", connect_as(srv2, "host", 0xA0, 1), 120); dsnet_wait_welcome(h2->c, 2000);
    CHECK(dsnet_import_owners(h2->c, blob, need) == 0, "import failed");
    free(blob);
    peer *g2 = peer_new("c2again", connect_as(srv2, "c2", 0xA2, 0), 120); dsnet_wait_welcome(g2->c, 2000);
    peer *ps2[2] = { h2, g2 };
    peer_go(h2); peer_go(g2);
    CHECK(wait_done(ps2, 2, 10000) == 0, "session 2 timeout");
    for (int i = 0; i < 2; i++) {
        dsnet_client *c = ps2[i]->c; uint8_t g[16]; char nm[32];
        guid(g, 0x73); CHECK(dsnet_owner_of(c, g) == 1, "%s: c2 (slot 1) lost 0x73 across sessions (%d)", ps2[i]->name, dsnet_owner_of(c, g));
        guid(g, 0x72); CHECK(dsnet_owner_info(c, g, NULL, nm) == DSNET_OWNER_RESERVED && !strcmp(nm, "c1"), "%s: c1's unit not reserved for c1", ps2[i]->name);
        guid(g, 0x71); CHECK(dsnet_owner_of(c, g) == 0, "%s: host's unit", ps2[i]->name);
        CHECK(dsnet_player_color(c, 0) == 0 && dsnet_player_color(c, 1) == 2, "%s: colours across sessions: host %d, c2 in slot 1 %d (want 0, 2)",
              ps2[i]->name, dsnet_player_color(c, 0), dsnet_player_color(c, 1));
    }
    all_equal(ps2, 2, 100);
    stop_all(ps2, 2, srv2);
    printf("%s player_identity: leaver's unit reserved (\"Reserved for c2\"), claims refused; rejoin as slot 3 -> units + items back; "
           "admin release/take; duplicate id refused; export -> new session, owners restored; colours follow the uid (%.1fs)\n", ok ? "PASS" : "FAIL", (now_ms() - t0) / 1000);
}

/* ---- 17. owner-table split is a desync (reason OWNERS), recovered by resync ---- */
static void test_owner_split(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    if (setup(&srv, ps, 3, 500, NULL, NULL)) { printf("  FAIL setup\n"); failures++; return; }
    ps[2]->split_owner = 1; ps[2]->auto_resync = 1;
    uint8_t sp[2] = { 0xF2, 0 }; script_add(ps[1], 100, DSNET_GAME_ORDER, sp, 2);
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 5; peer_go(ps[i]); }
    while (!ps[2]->desync_seen_at && now_ms() - t0 < 10000) sleep_ms(2);
    int reason = dsnet_desync_reason(ps[2]->c);
    CHECK(ps[2]->desync_seen_at, "owner split never flagged");
    CHECK(reason == DSNET_DESYNC_OWNERS, "reason %d, want owners only (game hash equal)", reason);
    CHECK(!ps[1]->desync_seen_at, "a healthy peer flagged desync");
    CHECK(wait_done(ps, 3, 20000) == 0, "timeout");
    CHECK(ps[2]->loads == 1 && !dsnet_is_desynced(ps[2]->c, NULL, NULL), "no recovery (loads %d)", ps[2]->loads);
    uint64_t u[3], it[3]; int nu[3], ni[3];
    for (int i = 0; i < 3; i++) dsnet_owner_hashes(ps[i]->c, &u[i], &it[i], &nu[i], &ni[i]);
    CHECK(u[0] == u[1] && u[0] == u[2] && nu[0] == nu[2] && it[0] == it[2], "owner tables still differ after resync");
    all_equal(ps, 3, 300);
    uint32_t bt = ps[2]->desync_bad;
    stop_all(ps, 3, srv);
    printf("%s owner_split: one peer's local owner change flagged at fingerprint %u (reason owners), resync made the tables equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", bt, (now_ms() - t0) / 1000);
}

/* ---- 18. disconnect hold + "Keep playing" temporary control, in pause mode NONE ---- */
static void test_hold(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_cfg cfg = test_cfg(3); cfg.pause_mode = DSNET_PAUSE_NONE;
    dsnet_server *srv = dsnet_server_start(&cfg);
    peer *h = peer_new("host", connect_as(srv, "host", 0xB0, 1), 420);  dsnet_wait_welcome(h->c, 2000);
    peer *c1 = peer_new("c1", connect_as(srv, "c1", 0xB1, 0), 420);       dsnet_wait_welcome(c1->c, 2000);
    peer *c2 = peer_new("c2", connect_as(srv, "c2", 0xB2, 0), 420);       dsnet_wait_welcome(c2->c, 2000);
    uint8_t b[17], uid2[16]; memset(uid2, 0xB2, 16);
    guid(b, 0x91); b[16] = 2; script_add(c2, 20, DSNET_OWNERSHIP, b, 17);
    peer_go(h); peer_go(c1); peer_go(c2);
    peer *c2b = NULL;
    int step = 0; uint32_t t_hold = 0, t_keep = 0, t_hold2 = 0, t_back = 0;
    int temp_ok = 0, c1_no = 1, name_ok = 0, story_clear = 1;
    while (!(h->done && c1->done && c2b && c2b->done)) {
        uint32_t t = h->timer; uint8_t g[16]; guid(g, 0x91);
        if (step == 0 && t >= 100) { peer_drop(c2); c2 = NULL; step = 1; }
        if (step == 1 && t >= 110) { dsnet_hold(h->c, 1, DSNET_HOLD_PLAYER_LEFT, uid2); t_hold = t; step = 2; }   /* "waiting for c2" */
        if (step == 2 && t >= 130) {
            char nm[32]; int rs;
            if (dsnet_hold_count(h->c) == 1 && dsnet_hold_get(h->c, 0, &rs, NULL, nm) == 0 && rs == DSNET_HOLD_PLAYER_LEFT && !strcmp(nm, "c2")) name_ok = 1;
            if (dsnet_story_pause_mask(h->c) || dsnet_pause_by(h->c) != -1) story_clear = 0;
            CHECK(dsnet_may_order(h->c, 0, g) == 0, "host may order a reserved unit before Keep playing");
            dsnet_set_temp_control(h->c, uid2, 1);                 /* "Keep playing" */
            dsnet_hold(h->c, 0, DSNET_HOLD_PLAYER_LEFT, uid2); t_keep = t; step = 3;
        }
        if (step == 3 && t >= 170) {
            if (dsnet_temp_control(h->c, uid2) && dsnet_may_order(h->c, 0, g) && !dsnet_may_equip(h->c, 0, g)) temp_ok = 1;
            if (dsnet_may_order(c1->c, 1, g)) c1_no = 0;
            dsnet_hold(h->c, 1, DSNET_HOLD_PLAYER_LEFT, uid2); t_hold2 = t; step = 4;    /* hold again, ends on rejoin */
        }
        if (step == 4 && t >= 200) {
            c2b = peer_new("c2b", connect_as(srv, "c2", 0xB2, 0), 420); dsnet_wait_welcome(c2b->c, 2000);
            peer_go(c2b); t_back = t; step = 5;
        }
        sleep_ms(2);
        if (now_ms() - t0 > 25000) { CHECK(0, "timeout"); break; }
    }
    uint8_t g[16]; guid(g, 0x91);
    uint32_t hx = exec_at(h, DSNET_HOLD, 0, 1, 0);
    int paused1 = 1, ran = 1;
    for (uint32_t t = t_hold + 8; t < t_keep; t++) if (h->tps_at[t]) paused1 = 0;
    for (uint32_t t = t_keep + 8; t < t_hold2; t++) if (h->tps_at[t] != 1) ran = 0;
    int unpaused_end = h->tps_at[400] == 1;
    CHECK(paused1, "hold did not pause in mode NONE (%u-%u)", t_hold, t_keep);
    CHECK(ran, "Keep playing did not resume");
    CHECK(name_ok && story_clear, "hold not reported as \"waiting for c2\" apart from story pauses");
    CHECK(temp_ok && c1_no, "temporary control: host order %d, c1 order blocked %d", temp_ok, c1_no);
    CHECK(unpaused_end && dsnet_hold_count(h->c) == 0, "rejoin did not end the hold");
    for (int i = 0; i < 3; i++) {
        peer *q = i == 0 ? h : i == 1 ? c1 : c2b;
        CHECK(!dsnet_temp_control(q->c, uid2), "%s: temporary control not ended by rejoin", q->name);
        int slot2 = dsnet_player_id(c2b->c);
        CHECK(dsnet_may_order(q->c, 0, g) == 0 && dsnet_owner_of(q->c, g) == slot2 && dsnet_may_order(q->c, slot2, g),
              "%s: after rejoin owner %d (c2 is slot %d)", q->name, dsnet_owner_of(q->c, g), slot2);
    }
    int common;
    CHECK(compare(h, c1, &common) && common > 380, "c1 diverged");
    CHECK(compare(h, c2b, &common) && common > 100, "c2 diverged after rejoin");
    (void)hx;
    peer *ps[3] = { h, c1, c2b };
    stop_all(ps, 3, srv);
    printf("%s hold_temp_control (mode none): c2 left -> hold \"waiting for c2\" paused %u-%u; Keep playing -> host orders c2's reserved unit (no equip), c1 cannot; "
           "rejoin at %u ended hold + temporary control (%.1fs)\n", ok ? "PASS" : "FAIL", t_hold, t_keep, t_back, (now_ms() - t0) / 1000);
}

/* ---- 18. frozen game threads (map load, big save) with a live link: nobody disconnects (default 5 s limit) ---- */
static void test_freeze8(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    if (setup(&srv, ps, 3, 400, NULL, NULL)) { printf("  FAIL setup\n"); failures++; return; }
    ps[0]->stall_at = 150; ps[0]->stall_ms = 8000;     /* host game thread blocked 8 s, no dsnet calls */
    ps[2]->stall_at = 160; ps[2]->stall_ms = 8000;     /* and a guest's */
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 5; peer_go(ps[i]); }
    CHECK(wait_done(ps, 3, 30000) == 0, "timeout");
    for (int i = 0; i < 3; i++) CHECK(!dsnet_is_disconnected(ps[i]->c), "%s disconnected during a game-thread freeze", ps[i]->name);
    CHECK(dsnet_present_mask(ps[0]->c) == 7, "a player was dropped (mask %x)", dsnet_present_mask(ps[0]->c));
    all_equal(ps, 3, 380);
    stop_all(ps, 3, srv);
    printf("%s freeze_8s: host and guest game threads blocked 8 s with a live link (limit %d ms) -> nobody dropped, all reached step 400, hashes equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", 5000, (now_ms() - t0) / 1000);
}

/* ---- 17. low frame rate: a 10 fps and a 2 fps peer still run 60 steps/s ---- */
static void test_low_fps(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[3];
    dsnet_cfg cfg = test_cfg(3); cfg.tick_ms = 1000.0f / 60; cfg.net_ms = 1000.0f / 30;
    if (setup_cfg(&srv, ps, 3, 330, NULL, NULL, &cfg)) { printf("  FAIL setup\n"); failures++; return; }
    ps[0]->frame_ms = 16; ps[1]->frame_ms = 100; ps[2]->frame_ms = 500;
    vote(ps[0], 1, DSNET_SPEED_VOTE, 3); vote(ps[1], 1, DSNET_SPEED_VOTE, 3); vote(ps[2], 1, DSNET_SPEED_VOTE, 3);
    for (int i = 0; i < 3; i++) { ps[i]->orders_every = 1; peer_go(ps[i]); }
    CHECK(wait_done(ps, 3, 20000) == 0, "timeout");
    double r[3];
    for (int i = 0; i < 3; i++) {
        r[i] = 240.0 / ((ps[i]->t_at300 - ps[i]->t_at60) / 1000);
        CHECK(r[i] > 50 && r[i] < 70, "%s: %.1f steps/s, want ~60", ps[i]->name, r[i]);
    }
    int fast = 0; for (uint32_t t = 100; t < 300; t++) if (ps[1]->tps_at[t] == 3) fast++;
    CHECK(fast == 200, "fast speed not applied on the 10 fps peer (%d)", fast);
    all_equal(ps, 3, 300);
    stop_all(ps, 3, srv);
    printf("%s low_fps: steps/s host(60 fps) %.1f, 10 fps %.1f, 2 fps %.1f; fast = %.0f game ticks/s on 10 fps, hashes equal (%.1fs)\n",
           ok ? "PASS" : "FAIL", r[0], r[1], r[2], r[1] * 3, (now_ms() - t0) / 1000);
}

/* ---- 12. separate processes over TCP ---- */
static int role_main(const char *role, uint16_t port, int players, uint32_t target, const char *out) {
    dsnet_server *srv = NULL; dsnet_client *c;
    if (!strcmp(role, "host")) {
        dsnet_cfg cfg = test_cfg(players);
        srv = dsnet_server_start(&cfg);
        printf("PORT %u\n", dsnet_server_port(srv)); fflush(stdout);
        c = dsnet_client_connect_local(srv, "host");
    } else c = dsnet_client_connect_tcp("127.0.0.1", port, role);
    if (!c || dsnet_wait_welcome(c, 5000)) return 2;
    peer *p = peer_new(role, c, target);
    p->orders_every = 5;
    peer_go(p);
    double end = now_ms() + 20000;
    while (!p->done && now_ms() < end) sleep_ms(10);
    sleep_ms(300);                 /* let the slowest peer get there before we leave */
    int desync = dsnet_is_desynced(c, NULL, NULL), done = p->done;
    FILE *f = fopen(out, "w");
    for (uint32_t t = 0; t < target; t++) if (p->have[t]) fprintf(f, "%u %016llx %u\n", t, (unsigned long long)p->hash_at[t], p->tps_at[t]);
    fclose(f);
    peer *ps[1] = { p };
    stop_all(ps, 1, srv);
    return desync ? 3 : done ? 0 : 4;
}

static void test_multiproc(const char *self) {
    int ok = 1; double t0 = now_ms();
    char dir[] = "/tmp/dsnet_mp_XXXXXX";
    const char *tmp = getenv("TMPDIR_DSNET");
    char base[256];
    if (tmp) snprintf(base, sizeof base, "%s", tmp); else { mkdtemp(dir); snprintf(base, sizeof base, "%s", dir); }
    char outs[3][300];
    for (int i = 0; i < 3; i++) snprintf(outs[i], sizeof outs[i], "%s/peer%d.txt", base, i);
    int pfd[2]; pipe(pfd);
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
    pid_t pids[3];
    char *ha[] = { (char *)self, "--role", "host", "--players", "3", "--target", "400", "--out", outs[0], NULL };
    posix_spawn(&pids[0], self, &fa, NULL, ha, environ);
    close(pfd[1]);
    char line[64] = { 0 }; FILE *pf = fdopen(pfd[0], "r");
    unsigned port = 0;
    if (fgets(line, sizeof line, pf)) sscanf(line, "PORT %u", &port);
    CHECK(port, "host process gave no port");
    char ps[8]; snprintf(ps, sizeof ps, "%u", port);
    for (int i = 1; i < 3; i++) {
        char nm[8]; snprintf(nm, sizeof nm, "p%d", i);
        char *ca[] = { (char *)self, "--role", nm, "--port", ps, "--target", "400", "--out", outs[i], NULL };
        posix_spawn(&pids[i], self, NULL, NULL, ca, environ);
    }
    for (int i = 0; i < 3; i++) {
        int st; waitpid(pids[i], &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "process %d exit %d", i, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    }
    fclose(pf);
    /* compare common lines */
    static char hs[3][4096][40];
    int lines[3] = { 0 };
    for (int i = 0; i < 3; i++) {
        FILE *f = fopen(outs[i], "r"); if (!f) { CHECK(0, "no output %d", i); continue; }
        unsigned t; char h[24]; unsigned tps;
        while (fscanf(f, "%u %23s %u", &t, h, &tps) == 3) if (t < 4096) { snprintf(hs[i][t], 40, "%s/%u", h, tps); lines[i]++; }
        fclose(f);
    }
    int common = 0, diff = 0;
    for (int t = 0; t < 4096; t++) if (hs[0][t][0] && hs[1][t][0] && hs[2][t][0]) {
        common++;
        if (strcmp(hs[0][t], hs[1][t]) || strcmp(hs[0][t], hs[2][t])) diff++;
    }
    CHECK(common > 350 && !diff, "%d common ticks, %d differ", common, diff);
    printf("%s multiprocess_tcp: 3 processes, %d common ticks, %d differ (%.1fs)\n", ok ? "PASS" : "FAIL", common, diff, (now_ms() - t0) / 1000);
}

/* ---- latency proxy: forwards both ways, each chunk delayed base +- jitter, order kept ---- */
typedef struct chunk { double due; size_t n; struct chunk *next; uint8_t b[]; } chunk;
struct proxy { int lfd; uint16_t up; int base, jit; uint32_t rng; volatile int frozen; };
static void proxy_freeze(proxy *x) { x->frozen = 1; }
static void *proxy_main(void *a) {
    proxy *x = a;
    int dn = accept(x->lfd, NULL, NULL);
    int upfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(x->up) };
    sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    connect(upfd, (struct sockaddr *)&sa, sizeof sa);
    int one = 1;
    setsockopt(dn, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    setsockopt(upfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    int fds[2] = { dn, upfd };
    chunk *head[2] = { 0 }, *tail[2] = { 0 };
    double last_due[2] = { 0 };
    for (;;) {
        if (x->frozen) { sleep_ms(5); continue; }      /* silent cut: nothing moves, no FIN/RST */
        struct pollfd pf[2] = { { dn, POLLIN, 0 }, { upfd, POLLIN, 0 } };
        poll(pf, 2, 1);
        for (int d = 0; d < 2; d++) if (pf[d].revents) {
            uint8_t buf[65536];
            ssize_t r = recv(fds[d], buf, sizeof buf, 0);
            if (r <= 0) { close(dn); close(upfd); return NULL; }
            chunk *c = malloc(sizeof *c + r);
            double due = now_ms() + x->base + (int)(xs(&x->rng) % (2 * x->jit + 1)) - x->jit;
            if (due < last_due[d]) due = last_due[d];
            last_due[d] = due;
            c->due = due; c->n = r; c->next = NULL; memcpy(c->b, buf, r);
            if (tail[d]) tail[d]->next = c; else head[d] = c;
            tail[d] = c;
        }
        double now = now_ms();
        for (int d = 0; d < 2; d++)
            while (head[d] && head[d]->due <= now) {
                chunk *c = head[d];
                size_t off = 0;
                while (off < c->n) { ssize_t w = send(fds[1 - d], c->b + off, c->n - off, 0); if (w <= 0) { close(dn); close(upfd); return NULL; } off += w; }
                head[d] = c->next; if (!head[d]) tail[d] = NULL;
                free(c);
            }
    }
}
static uint16_t proxy_start(uint16_t up, int base_ms, int jitter_ms) {
    proxy *x = calloc(1, sizeof *x);
    x->up = up; x->base = base_ms; x->jit = jitter_ms; x->rng = 88172645u + up + base_ms;
    x->lfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET };
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(x->lfd, (struct sockaddr *)&a, sizeof a); listen(x->lfd, 1);
    socklen_t al = sizeof a; getsockname(x->lfd, (struct sockaddr *)&a, &al);
    last_proxy = x;
    pthread_t t; pthread_create(&t, NULL, proxy_main, x); pthread_detach(t);
    return ntohs(a.sin_port);
}

/* ---- chat: outside the sim (relayed, never stamped), system lines from join / leave ---- */
static int chat_drain(dsnet_client *c, dsnet_chat_msg *out, int max) { int n = 0; dsnet_chat_msg m; while (dsnet_chat_poll(c, &m)) if (n < max) out[n++] = m; return n; }
/* "is typing": c1 types -> host and c2 see it (not c1); c1's line clears it; typing then silence clears after DSNET_TYPING_MS;
 * an explicit stop clears at once; at most one "on" a second; the sim is untouched */
static void test_typing(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_cfg cfg = test_cfg(3);
    dsnet_server *srv = dsnet_server_start(&cfg);
    peer *h = peer_new("host", connect_as(srv, "host", 0xF0, 1), 600); dsnet_wait_welcome(h->c, 2000);
    peer *c1 = peer_new("c1", connect_as(srv, "c1", 0xF1, 0), 600);    dsnet_wait_welcome(c1->c, 2000);
    peer *c2 = peer_new("c2", connect_as(srv, "c2", 0xF2, 0), 600);    dsnet_wait_welcome(c2->c, 2000);
    peer_go(h); peer_go(c1); peer_go(c2);
    sleep_ms(300);
    CHECK(dsnet_chat_typing(c1->c, 1) == 0, "typing send"); sleep_ms(200);
    int sh = dsnet_chat_typing_of(h->c, 1), s2 = dsnet_chat_typing_of(c2->c, 1), self = dsnet_chat_typing_of(c1->c, 1);
    CHECK(sh && s2 && !self, "c1 typing seen: host %d c2 %d self %d (want 1 1 0)", sh, s2, self);
    dsnet_chat_send(c1->c, "done typing"); sleep_ms(200);
    CHECK(!dsnet_chat_typing_of(h->c, 1) && !dsnet_chat_typing_of(c2->c, 1), "the chat line did not clear typing");
    dsnet_chat_typing(c1->c, 0); dsnet_chat_typing(c1->c, 1); sleep_ms(200);
    CHECK(dsnet_chat_typing_of(h->c, 1), "typing again not seen");
    sleep_ms(DSNET_TYPING_MS - 1000);
    CHECK(dsnet_chat_typing_of(h->c, 1), "cleared too early");
    sleep_ms(1400);
    CHECK(!dsnet_chat_typing_of(h->c, 1), "not cleared after %d ms of silence", DSNET_TYPING_MS);
    dsnet_chat_typing(c1->c, 1); sleep_ms(150); dsnet_chat_typing(c1->c, 0); sleep_ms(150);
    CHECK(!dsnet_chat_typing_of(h->c, 1), "stop did not clear");
    dsnet_chat_typing(c2->c, 1); for (int i = 0; i < 5; i++) { sleep_ms(100); dsnet_chat_typing(c2->c, 1); }   /* 0.5 s of keys: one send */
    sleep_ms(3800);
    CHECK(!dsnet_chat_typing_of(h->c, 2), "repeats inside a second were sent (kept alive past %d ms)", DSNET_TYPING_MS);
    while (!(h->done && c1->done && c2->done)) { sleep_ms(5); if (now_ms() - t0 > 30000) { CHECK(0, "timeout"); break; } }
    int common;
    CHECK(compare(h, c1, &common) && common > 500, "c1 diverged (typing must not touch the sim)");
    peer *ps[3] = { h, c1, c2 };
    stop_all(ps, 3, srv);
    printf("%s typing: seen by the others not self, a line clears it, silence clears after %d ms, stop clears, 1 send a second, sim unchanged (%.1fs)\n",
           ok ? "PASS" : "FAIL", DSNET_TYPING_MS, (now_ms() - t0) / 1000);
}
static void test_chat(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_cfg cfg = test_cfg(3);
    dsnet_server *srv = dsnet_server_start(&cfg);
    peer *h = peer_new("host", connect_as(srv, "host", 0xF0, 1), 400); dsnet_wait_welcome(h->c, 2000);
    peer *c1 = peer_new("c1", connect_as(srv, "c1", 0xF1, 0), 400);    dsnet_wait_welcome(c1->c, 2000);
    peer *c2 = peer_new("c2", connect_as(srv, "c2", 0xF2, 0), 400);    dsnet_wait_welcome(c2->c, 2000);
    peer_go(h); peer_go(c1); peer_go(c2);
    char longmsg[260]; memset(longmsg, 'x', 259); longmsg[259] = 0;
    int step = 0; peer *c2b = NULL, *c3 = NULL;
    while (!(h->done && c1->done && c2b && c2b->done && c3 && c3->done)) {
        uint32_t t = h->timer;
        if (step == 0 && t >= 20) { CHECK(dsnet_chat_send(c1->c, "hi from c1") == 0, "c1 send"); dsnet_chat_send(h->c, longmsg); CHECK(dsnet_chat_send(c1->c, "") == -1, "empty sent"); step = 1; }
        if (step == 1 && t >= 60) { peer_drop(c2); c2 = NULL; step = 2; }
        if (step == 2 && t >= 90) { c2b = peer_new("c2", connect_as(srv, "c2", 0xF2, 0), 400); dsnet_wait_welcome(c2b->c, 2000); c2b->auto_resync = 1; peer_go(c2b); step = 3; }
        if (step == 3 && t >= 120) { c3 = peer_new("c3", connect_as(srv, "c3", 0xF3, 0), 400); dsnet_wait_welcome(c3->c, 2000); c3->auto_resync = 1; peer_go(c3);
                                     dsnet_chat_send(c1->c, "while c3 loads"); step = 4; }
        sleep_ms(2);
        if (now_ms() - t0 > 30000) { CHECK(0, "timeout"); break; }
    }
    sleep_ms(100);
    dsnet_chat_msg m[64]; int n = chat_drain(h->c, m, 64);
    int hi = 0, lng = 0, left = 0, rec = 0, joined3 = 0, during = 0;
    for (int i = 0; i < n; i++) {
        if (m[i].kind == DSNET_CHAT_TEXT && m[i].player == 1 && !strcmp(m[i].text, "hi from c1") && !strcmp(m[i].name, "c1")) hi = 1;
        if (m[i].kind == DSNET_CHAT_TEXT && m[i].player == 0 && strlen(m[i].text) == DSNET_CHAT_MAX) lng = 1;
        if (m[i].kind == DSNET_CHAT_LEFT && !strcmp(m[i].name, "c2")) left = 1;
        if (m[i].kind == DSNET_CHAT_RECONNECTED && !strcmp(m[i].name, "c2")) rec = 1;
        if (m[i].kind == DSNET_CHAT_JOINED && !strcmp(m[i].name, "c3")) joined3 = 1;
    }
    dsnet_chat_msg m3[64]; int n3 = chat_drain(c3->c, m3, 64);
    for (int i = 0; i < n3; i++) if (m3[i].kind == DSNET_CHAT_TEXT && !strcmp(m3[i].text, "while c3 loads")) during = 1;
    CHECK(hi && lng, "host got: c1's line %d, the long one cut to 200 %d", hi, lng);
    CHECK(left && rec && joined3, "system lines: left %d, reconnected %d, joined %d", left, rec, joined3);
    CHECK(during, "a player still loading got the chat line");
    int common;
    CHECK(compare(h, c1, &common) && common > 380, "c1 diverged (chat must not touch the sim)");
    peer *ps[4] = { h, c1, c2b, c3 };
    stop_all(ps, 4, srv);
    printf("%s chat: relayed lines (name, 200-byte cut, empty refused), left / reconnected / joined lines, received while loading, sim unchanged (%.1fs)\n",
           ok ? "PASS" : "FAIL", (now_ms() - t0) / 1000);
}

/* ---- population caps (PLAN.md "POPULATION rule") ----
 * Pure helper: every mode, remainder split, 1 player, 0 cap. Session: SPLIT 8 over 3 players = 3/3/2 by slot; c1 leaves
 * -> 4/4 (nobody removed: c2 at 3 may still hire up to 4; a player above his new cap only cannot hire); a rule change
 * mid-game to MULTIPLIED -> one pool of 16; every peer computes the same. */
static void test_population(void) {
    int ok = 1; double t0 = now_ms(), pool = 0; int pl = -1;
    struct { int mode, base, n, rank, cap, pool; } cs[] = {
        { DSNET_POP_SHARED, 8, 3, 2, 8, 1 }, { DSNET_POP_PER_PLAYER, 8, 3, 2, 8, 0 },
        { DSNET_POP_SPLIT, 8, 3, 0, 3, 0 }, { DSNET_POP_SPLIT, 8, 3, 1, 3, 0 }, { DSNET_POP_SPLIT, 8, 3, 2, 2, 0 },
        { DSNET_POP_SPLIT, 9, 3, 2, 3, 0 }, { DSNET_POP_SPLIT, 2, 3, 2, 0, 0 }, { DSNET_POP_SPLIT, 8, 1, 0, 8, 0 },
        { DSNET_POP_MULTIPLIED, 8, 3, 0, 24, 1 }, { DSNET_POP_MULTIPLIED, 8, 0, 0, 8, 1 }, { DSNET_POP_SHARED, 0, 2, 0, 0, 1 },
    };
    for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
        int c = dsnet_population_cap(cs[i].mode, cs[i].base, cs[i].n, cs[i].rank, &pl);
        CHECK(c == cs[i].cap && pl == cs[i].pool, "case %zu: mode %d base %d n %d rank %d -> cap %d pool %d (want %d %d)", i, cs[i].mode, cs[i].base, cs[i].n, cs[i].rank, c, pl, cs[i].cap, cs[i].pool);
    }
    (void)pool;
    dsnet_cfg cfg = test_cfg(3); cfg.population = DSNET_POP_SPLIT;
    dsnet_server *srv = dsnet_server_start(&cfg);
    peer *h = peer_new("host", connect_as(srv, "host", 0xE0, 1), 300); dsnet_wait_welcome(h->c, 2000);
    peer *c1 = peer_new("c1", connect_as(srv, "c1", 0xE1, 0), 300);    dsnet_wait_welcome(c1->c, 2000);
    peer *c2 = peer_new("c2", connect_as(srv, "c2", 0xE2, 0), 300);    dsnet_wait_welcome(c2->c, 2000);
    peer_go(h); peer_go(c1); peer_go(c2);
    int cap0[3] = { -1, -1, -1 }, cap2 = -1, may2 = -1, mayh = -1, capm = -1, poolm = -1, step = 0, c2cap_c2 = -1;
    while (!(h->done && c2->done)) {
        uint32_t t = h->timer;
        if (step == 0 && t >= 40) {
            for (int p = 0; p < 3; p++) dsnet_may_recruit(h->c, p, 8, 5, 0, &cap0[p], NULL);
            dsnet_may_recruit(c2->c, 2, 8, 5, 0, &c2cap_c2, NULL);
            step = 1;
        }
        if (step == 1 && t >= 60) { peer_drop(c1); c1 = NULL; step = 2; }
        if (step == 2 && t >= 100) {
            may2 = dsnet_may_recruit(h->c, 2, 8, 9, 2, &cap2, NULL);      /* c2 is now rank 1 of 2: cap 4, owns 2 -> yes */
            mayh = dsnet_may_recruit(h->c, 0, 8, 9, 5, NULL, NULL);        /* host owns 5 > cap 4: overflow kept, no hire */
            dsnet_rules r; dsnet_rules_get(h->c, &r); r.population = DSNET_POP_MULTIPLIED; dsnet_set_rules(h->c, &r);
            step = 3;
        }
        if (step == 3 && t >= 140) { dsnet_may_recruit(c2->c, 2, 8, 15, 0, &capm, &poolm); step = 4; }
        sleep_ms(2);
        if (now_ms() - t0 > 20000) { CHECK(0, "timeout"); break; }
    }
    CHECK(cap0[0] == 3 && cap0[1] == 3 && cap0[2] == 2 && c2cap_c2 == 2, "SPLIT 8 / 3: caps %d %d %d (c2 sees %d), want 3 3 2", cap0[0], cap0[1], cap0[2], c2cap_c2);
    CHECK(cap2 == 4 && may2 == 1 && mayh == 0, "after c1 left: c2 cap %d may %d, host over cap may %d (want 4, 1, 0)", cap2, may2, mayh);
    CHECK(capm == 16 && poolm == 1, "MULTIPLIED mid-game: cap %d pool %d (want 16 1)", capm, poolm);
    /* AND with units per player (default 3): pool room but 3 own units -> UNITS_FULL; pool full -> POP_FULL first */
    CHECK(dsnet_recruit_check(h->c, 2, 8, 5, 3, NULL, NULL) == DSNET_RECRUIT_UNITS_FULL, "units limit not applied");
    CHECK(dsnet_recruit_check(h->c, 2, 8, 16, 3, NULL, NULL) == DSNET_RECRUIT_POP_FULL, "population cap not first");
    CHECK(dsnet_recruit_check(h->c, 2, 8, 5, 2, NULL, NULL) == DSNET_RECRUIT_OK && dsnet_may_recruit(h->c, 2, 8, 5, 2, NULL, NULL), "room under both");
    peer *ps[2] = { h, c2 };
    stop_all(ps, 2, srv);
    printf("%s population: helper cases (all modes, remainder, 1 player, cap 0); SPLIT 3/3/2 -> 4/4 after a leave (overflow kept); "
           "MULTIPLIED mid-game = 16 pooled (%.1fs)\n", ok ? "PASS" : "FAIL", (now_ms() - t0) / 1000);
}

/* ---- ready with 0 units (a viewer approves); the start needs at least 1 unit in total ---- */
static void test_ready_zero(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_server *srv; peer *ps[2];
    if (setup(&srv, ps, 2, 300, NULL, NULL)) { printf("  FAIL setup\n"); failures++; return; }
    uint8_t b[40];
    guid(b, 0x55); b[16] = 0; script_add(ps[0], 10, DSNET_OWNERSHIP, b, 17);
    b[0] = 1; b[1] = 0; script_add(ps[1], 20, DSNET_EXPEDITION_READY, b, 2);     /* c1 approves as a viewer */
    b[0] = 1; b[1] = 0; script_add(ps[0], 30, DSNET_EXPEDITION_READY, b, 2);     /* host too: 0 units in total, no start */
    b[0] = 1; b[1] = 1; guid(b + 2, 0x55); script_add(ps[0], 60, DSNET_EXPEDITION_READY, b, 18);   /* host takes 1: start */
    peer_go(ps[0]); peer_go(ps[1]);
    uint32_t mask40 = 0; int starts40 = -1;
    while (!(ps[0]->done && ps[1]->done)) {
        if (ps[0]->timer > 40 && ps[0]->timer < 50) { mask40 = dsnet_ready_mask(ps[0]->c); starts40 = ps[0]->starts; }
        sleep_ms(2);
        if (now_ms() - t0 > 20000) { CHECK(0, "timeout"); break; }
    }
    CHECK(mask40 == 3 && starts40 == 0, "all ready with 0 units: mask %x, starts %d (want 3, 0)", mask40, starts40);
    uint8_t want[40] = { 2, 0, 1 }; guid(want + 3, 0x55); want[19] = 1; want[20] = 0;
    for (int i = 0; i < 2; i++) {
        CHECK(ps[i]->starts == 1 && ps[i]->start_tick == ps[0]->start_tick && ps[i]->start_len == 21 && !memcmp(ps[i]->start_buf, want, 21),
              "%s: starts %d len %d", ps[i]->name, ps[i]->starts, ps[i]->start_len);
    }
    all_equal(ps, 2, 280);
    stop_all(ps, 2, srv);
    printf("%s ready_zero: a viewer is ready with 0 units; all ready with 0 units does not start; 1 unit starts (host 1 + viewer 0) (%.1fs)\n",
           ok ? "PASS" : "FAIL", (now_ms() - t0) / 1000);
}

/* ---- lend control (DSNET_LEND): ownership stays; lend / take back / give back; a leaving borrower returns it ----
 * c1 owns U. c2 tries to lend U (not his): ignored. c1 lends U to c2 (40): only c2 may order it, owner still c1.
 * c2 gives it back (80). c1 lends again (100), takes it back (120). c1 lends again (140); a late joiner (150) gets the lend
 * from the save; c2 leaves (180): control is back with c1. Host lends an unclaimed unit H to c1 (60). Owner hashes equal. */
static void test_lend(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_cfg cfg = test_cfg(3);
    dsnet_server *srv = dsnet_server_start(&cfg);
    peer *h = peer_new("host", connect_as(srv, "host", 0xD0, 1), 400); dsnet_wait_welcome(h->c, 2000);
    peer *c1 = peer_new("c1", connect_as(srv, "c1", 0xD1, 0), 400);    dsnet_wait_welcome(c1->c, 2000);
    peer *c2 = peer_new("c2", connect_as(srv, "c2", 0xD2, 0), 400);    dsnet_wait_welcome(c2->c, 2000);
    uint8_t u[16], hu[16], b[17]; guid(u, 0x77); guid(hu, 0x78);
    memcpy(b, u, 16); b[16] = 1; script_add(c1, 20, DSNET_OWNERSHIP, b, 17);
    memcpy(b, u, 16); b[16] = 0; script_add(c2, 30, DSNET_LEND, b, 17);           /* not his: ignored */
    memcpy(b, u, 16); b[16] = 2; script_add(c1, 40, DSNET_LEND, b, 17);
    memcpy(b, hu, 16); b[16] = 1; script_add(h, 60, DSNET_LEND, b, 17);           /* host lends an unclaimed unit */
    memcpy(b, u, 16); b[16] = DSNET_NO_PLAYER; script_add(c2, 80, DSNET_LEND, b, 17);   /* borrower gives back */
    memcpy(b, u, 16); b[16] = 2; script_add(c1, 100, DSNET_LEND, b, 17);
    memcpy(b, u, 16); b[16] = DSNET_NO_PLAYER; script_add(c1, 120, DSNET_LEND, b, 17);  /* owner takes back */
    memcpy(b, u, 16); b[16] = 2; script_add(c1, 140, DSNET_LEND, b, 17);
    peer_go(h); peer_go(c1); peer_go(c2);
    int s35 = -9, s50[3] = { -9, -9, -9 }, o50 = -9, s90 = -9, s110 = -9, s130 = -9, s160 = -9, h70 = -9, c3_ctl = -9, after = -9, after_order = -9;
    peer *c3 = NULL; int step = 0;
    while (!(h->done && c1->done && c3 && c3->done)) {
        uint32_t t = h->timer;
        if (step == 0 && t >= 35) { s35 = dsnet_lent_to(h->c, u); step = 1; }
        if (step == 1 && t >= 50) {
            s50[0] = dsnet_lent_to(h->c, u); s50[1] = dsnet_may_order(h->c, 2, u); s50[2] = dsnet_may_order(h->c, 1, u);
            o50 = dsnet_owner_of(c2->c, u); step = 2;
        }
        if (step == 2 && t >= 70) { h70 = dsnet_lent_to(c2->c, hu) == 1 && dsnet_may_order(c2->c, 1, hu) && !dsnet_may_order(c2->c, 0, hu); step = 3; }
        if (step == 3 && t >= 90) { s90 = dsnet_lent_to(c1->c, u); step = 4; }
        if (step == 4 && t >= 110) { s110 = dsnet_lent_to(c1->c, u); step = 5; }
        if (step == 5 && t >= 130) { s130 = dsnet_lent_to(c1->c, u); step = 6; }
        if (step == 6 && t >= 150) { c3 = peer_new("c3", connect_as(srv, "c3", 0xD3, 0), 400); dsnet_wait_welcome(c3->c, 2000); c3->auto_resync = 1; peer_go(c3); step = 7; }
        if (step == 7 && t >= 175) { s160 = dsnet_lent_to(h->c, u); c3_ctl = dsnet_lent_to(c3->c, u); step = 8; }
        if (step == 8 && t >= 180) { peer_drop(c2); c2 = NULL; step = 9; }
        if (step == 9 && t >= 200) { after = dsnet_lent_to(h->c, u); after_order = dsnet_may_order(c1->c, 1, u); step = 10; }
        sleep_ms(2);
        if (now_ms() - t0 > 30000) { CHECK(0, "timeout"); break; }
    }
    CHECK(s35 == -1, "a non-owner's lend was applied (%d)", s35);
    CHECK(s50[0] == 2 && s50[1] == 1 && s50[2] == 0 && o50 == 1, "lent: controller %d, borrower orders %d, owner orders %d, owner %d", s50[0], s50[1], s50[2], o50);
    CHECK(h70 == 1, "host's unclaimed unit lent to c1");
    CHECK(s90 == -1 && s110 == 2 && s130 == -1 && s160 == 2, "give back %d, lend %d, take back %d, lend %d", s90, s110, s130, s160);
    CHECK(c3_ctl == 2, "late joiner did not get the lend from the save (%d)", c3_ctl);
    CHECK(after == -1 && after_order == 1, "borrower left: controller %d, owner orders %d", after, after_order);
    CHECK(dsnet_controller_of(h->c, u) == 1 && dsnet_controller_of(h->c, hu) == 1 && dsnet_owner_of(h->c, hu) == -1, "controller_of: owner when not lent, borrower when lent");
    { uint8_t lu[16]; int ow, fr, to, why;
      CHECK(dsnet_lend_seq(h->c) == dsnet_lend_seq(c1->c) && dsnet_lend_seq(h->c) == 7, "lend events %u / %u (want 7)", dsnet_lend_seq(h->c), dsnet_lend_seq(c1->c));
      CHECK(dsnet_lend_last(c1->c, lu, &ow, &fr, &to, &why) == 0 && why == DSNET_LEND_RETURNED && fr == 2 && to == 1 && ow == 1 && !memcmp(lu, u, 16),
            "last event why %d from %d to %d owner %d", why, fr, to, ow); }
    uint64_t oh[3]; peer *ps3[3] = { h, c1, c3 };
    for (int i = 0; i < 3; i++) dsnet_owner_hashes(ps3[i]->c, &oh[i], NULL, NULL, NULL);
    CHECK(dsnet_lent_count(h->c) == 1 && dsnet_lent_count(c1->c) == 1 && dsnet_lent_count(c3->c) == 1, "lent counts %d/%d/%d (host's unit to c1 stays)",
          dsnet_lent_count(h->c), dsnet_lent_count(c1->c), dsnet_lent_count(c3->c));
    for (int i = 0; i < 3; i++) CHECK(!dsnet_is_desynced(ps3[i]->c, NULL, NULL), "%s desynced (owner hash with lends)", ps3[i]->name);
    int common;
    CHECK(compare(h, c1, &common) && common > 380, "c1 diverged");
    CHECK(compare(h, c3, &common) && common > 100, "c3 diverged");
    stop_all(ps3, 3, srv);
    printf("%s lend: lend / give back / take back, only the borrower orders, host lends unclaimed, late joiner has it, borrower leaving returns it (%.1fs)\n",
           ok ? "PASS" : "FAIL", (now_ms() - t0) / 1000);
}

/* ---- host rules (DSNET_RULES): pause mode, speed-up, gear, units per player; changed in play, same step everywhere ----
 * c1 claims 4 units under the default limit 3 (4th refused). Host rules at 60: pause off, speed-up off, gear any, 2 units:
 * c1's pause vote and c2's 3x vote are dropped by the server; c1's 5th claim and a 3-unit party are refused; a 2-unit party
 * is accepted. Host rules at 150: pause anyone, speed-up on, own units, 6: c1's claim at 170 is accepted. A late joiner gets
 * the rules from the save. All peers apply each RULES at the same step and stay in sync. */
static void test_rules(void) {
    int ok = 1; double t0 = now_ms();
    dsnet_cfg cfg = test_cfg(3);
    dsnet_server *srv = dsnet_server_start(&cfg);
    peer *h = peer_new("host", connect_as(srv, "host", 0xC0, 1), 400); dsnet_wait_welcome(h->c, 2000);
    peer *c1 = peer_new("c1", connect_as(srv, "c1", 0xC1, 0), 400);    dsnet_wait_welcome(c1->c, 2000);
    peer *c2 = peer_new("c2", connect_as(srv, "c2", 0xC2, 0), 400);    dsnet_wait_welcome(c2->c, 2000);
    dsnet_rules r0; dsnet_rules_get(c1->c, &r0);
    CHECK(r0.units_per_player == 3 && r0.pause_mode == DSNET_PAUSE_ANYONE && r0.allow_speedup == 1 && r0.event_choice == DSNET_EVENT_OWNER, "WELCOME rules %d/%d/%d/%d",
          r0.pause_mode, r0.allow_speedup, r0.gear_mode, r0.units_per_player);
    uint8_t b[17];
    for (int i = 0; i < 4; i++) { guid(b, 0xA1 + i); b[16] = 1; script_add(c1, 20 + i, DSNET_OWNERSHIP, b, 17); }
    vote(c1, 80, DSNET_PAUSE_VOTE, 1);                       /* pause off: dropped */
    vote(c2, 90, DSNET_SPEED_VOTE, 3);                       /* speed-up off: dropped */
    guid(b, 0xA5); b[16] = 1; script_add(c1, 100, DSNET_OWNERSHIP, b, 17);    /* owns 3 > 2: refused */
    { uint8_t pb[2 + 3 * 16]; pb[0] = 0; pb[1] = 3; for (int k = 0; k < 3; k++) guid(pb + 2 + 16 * k, 0xA1 + k);
      script_add(c1, 110, DSNET_EXPEDITION_READY, pb, sizeof pb);                   /* 3 > 2: dropped */
      pb[1] = 2; script_add(c1, 112, DSNET_EXPEDITION_READY, pb, 2 + 2 * 16); }     /* 2: kept */
    guid(b, 0xA5); b[16] = 1; script_add(c1, 170, DSNET_OWNERSHIP, b, 17);    /* limit 6 again: accepted */
    peer_go(h); peer_go(c1); peer_go(c2);
    int step = 0; peer *c3 = NULL;
    dsnet_rules ra = { DSNET_PAUSE_NONE, 0, DSNET_GEAR_SHARED_ANY, 2, DSNET_EVENT_FIRST, DSNET_POP_SPLIT }, rb = { DSNET_PAUSE_ANYONE, 1, DSNET_GEAR_SHARED_OWN, 6, DSNET_EVENT_MAJORITY, DSNET_POP_MULTIPLIED },
                bad = { 1, 1, 0, 5, 0, 0 };
    int bad_rc = 0, guest_rc = 0;
    while (!(h->done && c1->done && c2->done && c3 && c3->done)) {
        uint32_t t = h->timer;
        if (step == 0 && t >= 60) { bad_rc = dsnet_set_rules(h->c, &bad); guest_rc = dsnet_set_rules(c2->c, &rb); dsnet_set_rules(h->c, &ra); step = 1; }
        if (step == 1 && t >= 150) { dsnet_set_rules(h->c, &rb); step = 2; }
        if (step == 2 && t >= 200) { c3 = peer_new("c3", connect_as(srv, "c3", 0xC3, 0), 400); dsnet_wait_welcome(c3->c, 2000); c3->auto_resync = 1; peer_go(c3); step = 3; }
        sleep_ms(2);
        if (now_ms() - t0 > 30000) { CHECK(0, "timeout"); break; }
    }
    CHECK(bad_rc == -1, "units 5 accepted by dsnet_set_rules (rc %d)", bad_rc);
    (void)guest_rc;                                          /* sent, but the server drops it (not the host) */
    uint32_t ta = exec_at(h, DSNET_RULES, 0, DSNET_PAUSE_NONE, 0), tb = exec_at(h, DSNET_RULES, 0, DSNET_PAUSE_ANYONE, 0);
    CHECK(ta && tb && exec_at(c1, DSNET_RULES, 0, DSNET_PAUSE_NONE, 0) == ta && exec_at(c2, DSNET_RULES, 0, DSNET_PAUSE_NONE, 0) == ta
          && exec_at(c1, DSNET_RULES, 0, DSNET_PAUSE_ANYONE, 0) == tb, "RULES not at the same step (%u, %u)", ta, tb);
    CHECK(!exec_at(h, DSNET_RULES, 2, DSNET_PAUSE_ANYONE, 0), "a guest's RULES was stamped");
    CHECK(!exec_at(h, DSNET_PAUSE_VOTE, 1, 1, 0) && !exec_at(h, DSNET_SPEED_VOTE, 2, 3, 0), "votes not dropped under the new rules");
    int still = 1; for (uint32_t t = ta + 5; t < tb; t++) if (h->tps_at[t] != 1) still = 0;
    CHECK(still, "not 1 tick per step between the rule changes");
    uint8_t g5[16]; guid(g5, 0xA5); uint8_t g4[16]; guid(g4, 0xA4);
    peer *ps4[4] = { h, c1, c2, c3 };
    for (int i = 0; i < 4; i++) {
        peer *q = ps4[i]; dsnet_rules r; dsnet_rules_get(q->c, &r);
        CHECK(r.pause_mode == rb.pause_mode && r.allow_speedup == 1 && r.gear_mode == rb.gear_mode && r.units_per_player == 6 && r.event_choice == DSNET_EVENT_MAJORITY && r.population == DSNET_POP_MULTIPLIED,
              "%s: rules %d/%d/%d/%d at the end", q->name, r.pause_mode, r.allow_speedup, r.gear_mode, r.units_per_player);
        CHECK(dsnet_owned_count(q->c, 1) == 4 && dsnet_owner_of(q->c, g4) == -1 && dsnet_owner_of(q->c, g5) == 1,
              "%s: c1 owns %d (4th free %d, A5 %d)", q->name, dsnet_owned_count(q->c, 1), dsnet_owner_of(q->c, g4), dsnet_owner_of(q->c, g5));
        if (q != c3) CHECK(dsnet_claim_refusals(q->c, 1) == 2, "%s: refusals %u (want 2)", q->name, dsnet_claim_refusals(q->c, 1));
        uint8_t mk[6][16]; CHECK(dsnet_marked_list(q->c, 1, mk, 6) == 2, "%s: marks %d (want 2)", q->name, dsnet_marked_list(q->c, 1, mk, 6));
    }
    int common;
    CHECK(compare(h, c1, &common) && common > 380, "c1 diverged");
    CHECK(compare(h, c2, &common) && common > 380, "c2 diverged");
    CHECK(compare(h, c3, &common) && common > 100, "c3 diverged");
    stop_all(ps4, 4, srv);
    printf("%s rules: limit 3 refused a 4th claim; rules at %u (pause off, speed-up off, any gear, 2 units) dropped pause/3x votes, a 5th "
           "claim and a 3-unit party; rules at %u (6 units) accepted it; late joiner has the rules; bad values / guest refused (%.1fs)\n",
           ok ? "PASS" : "FAIL", ta, tb, (now_ms() - t0) / 1000);
}

/* ---- join by address: the connect never blocks the game thread (ux review #1) ----
 * An address that does not answer gives DSNET_CONN_TIMEOUT within the limit, while every poll returns at once;
 * a closed port gives REFUSED; a bad name gives BAD_ADDRESS; a live server is adopted and the client joins on that
 * socket; a cancel (free while trying) returns at once. */
static void test_connect(void) {
    int ok = 1; double t0 = now_ms();
    struct { const char *host; int want; const char *what; } cases[] = {
        { "10.255.255.1", DSNET_CONN_TIMEOUT, "no answer" },      /* non-routable: SYN goes nowhere */
        { "no-such-host.invalid", DSNET_CONN_BAD_ADDRESS, "bad name" },
    };
    char res[256] = ""; int pos = 0;
    for (int i = 0; i < 2; i++) {
        double s = now_ms(), worst = 0; int r = 0;
        dsnet_connector *k = dsnet_connect_start(cases[i].host, 47777, 1500);
        while ((r = dsnet_connect_poll(k)) == 0 && now_ms() - s < 5000) {
            double a = now_ms(); r = dsnet_connect_poll(k); double d = now_ms() - a; if (d > worst) worst = d;
            if (r) break;
            sleep_ms(5);
        }
        double took = now_ms() - s;
        /* some networks answer "unreachable" at once instead of staying silent: also fine, just not slow */
        int good = r == cases[i].want || (cases[i].want == DSNET_CONN_TIMEOUT && r == DSNET_CONN_UNREACHABLE);
        CHECK(good, "%s: result %d, want %d", cases[i].what, r, cases[i].want);
        CHECK(took < 1500 + 400, "%s: took %.0f ms (limit 1500)", cases[i].what, took);
        CHECK(worst < 20, "%s: a poll blocked %.1f ms", cases[i].what, worst);
        pos += snprintf(res + pos, sizeof res - pos, "%s%s=%d in %.0f ms (worst poll %.2f ms)", i ? ", " : "", cases[i].what, r, took, worst);
        dsnet_connect_free(k);
    }
    /* refused: a port that was just closed */
    int lfd = socket(AF_INET, SOCK_STREAM, 0); struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    socklen_t al = sizeof a; bind(lfd, (struct sockaddr *)&a, al); getsockname(lfd, (struct sockaddr *)&a, &al); close(lfd);
    {
        dsnet_connector *k = dsnet_connect_start("127.0.0.1", ntohs(a.sin_port), 1500); double s = now_ms(); int r;
        while (!(r = dsnet_connect_poll(k)) && now_ms() - s < 3000) sleep_ms(2);
        CHECK(r == DSNET_CONN_REFUSED, "closed port: result %d", r);
        pos += snprintf(res + pos, sizeof res - pos, ", closed port=%d in %.0f ms", r, now_ms() - s);
        dsnet_connect_free(k);
    }
    /* cancel while trying: free returns at once */
    {
        dsnet_connector *k = dsnet_connect_start("10.255.255.1", 47777, 5000); sleep_ms(30);
        double s = now_ms(); dsnet_connect_free(k); double d = now_ms() - s;
        CHECK(d < 20, "cancel took %.1f ms", d);
        pos += snprintf(res + pos, sizeof res - pos, ", cancel %.2f ms", d);
    }
    /* live server: connect, adopt, join on that socket */
    {
        dsnet_cfg cfg = test_cfg(1);
        dsnet_server *srv = dsnet_server_start(&cfg);
        uint16_t port = dsnet_server_port(srv);
        dsnet_connector *k = dsnet_connect_start("127.0.0.1", port, 2000); double s = now_ms(); int r;
        while (!(r = dsnet_connect_poll(k)) && now_ms() - s < 3000) sleep_ms(2);
        CHECK(r == 1, "live server: result %d", r);
        CHECK(dsnet_connect_adopt(k) == 0, "adopt failed");
        dsnet_connect_free(k);
        dsnet_client *c = dsnet_client_connect_tcp("127.0.0.1", port, "adopted");
        int w = c ? dsnet_wait_welcome(c, 2000) : -9;
        CHECK(w == 0, "adopted socket: welcome %d", w);
        pos += snprintf(res + pos, sizeof res - pos, ", live server joined=%d", w);
        if (c) dsnet_client_close(c);
        dsnet_server_stop(srv);
    }
    printf("%s connect: %s (%.1fs)\n", ok ? "PASS" : "FAIL", res, (now_ms() - t0) / 1000);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *role = NULL, *out = NULL, *only = NULL;
    int players = 3; unsigned port = 0, target = 400;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--role")) role = argv[i + 1];
        else if (!strcmp(argv[i], "--port")) port = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--players")) players = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--target")) target = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--out")) out = argv[i + 1];
        else if (!strcmp(argv[i], "--only")) only = argv[i + 1];
    }
    if (role) return role_main(role, port, players, target, out);
    double t0 = now_ms();
    if (only && !strcmp(only, "freeze8")) { test_freeze8(); return failures ? 1 : 0; }
    /* the long, mostly sleeping tests run beside the others to keep make test under 60 s */
    const char *side[] = { "freeze8", "lowfps" };
    pid_t kids[2] = { 0, 0 };
    if (!only) for (int i = 0; i < 2; i++) { char *fa[] = { argv[0], "--only", (char *)side[i], NULL }; posix_spawn(&kids[i], argv[0], NULL, NULL, fa, environ); }
#define RUN(name, fn) if (!only || !strcmp(only, name)) fn
    RUN("basic", test_basic());
    RUN("latency", test_latency());
    RUN("slow", test_slow_peer());
    RUN("pause", test_pause());
    RUN("desync", test_desync());
    RUN("partial", test_partial(0));
    RUN("partialfail", test_partial(1));
    RUN("marks", test_marks());
    RUN("late3", test_late_join3());
    RUN("late", test_late_join());
    RUN("handshake", test_handshake());
    RUN("connect", test_connect());
    RUN("rules", test_rules());
    RUN("lend", test_lend());
    RUN("readyzero", test_ready_zero());
    RUN("population", test_population());
    RUN("chat", test_chat());
    RUN("typing", test_typing());
    RUN("nospeed", test_no_speedup());
    RUN("nopause", test_no_pause());
    RUN("busy", test_busy());
    RUN("pauseall", test_pause_all_mode());
    RUN("expedition", test_expedition());
    RUN("gear", test_gear());
    RUN("prestart", test_prestart_save());
    RUN("silentcut", test_silent_cut());
    RUN("bye", test_bye());
    RUN("identity", test_identity());
    RUN("hold", test_hold());
    RUN("ownersplit", test_owner_split());
    if (only && !strcmp(only, "lowfps")) test_low_fps();
    RUN("multiproc", test_multiproc(argv[0]));
    for (int i = 0; i < 2; i++) if (kids[i]) {
        int st; waitpid(kids[i], &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st)) { printf("FAIL %s child process (status %d)\n", side[i], st); failures++; }
    }
    printf("%s: %d failure(s), %.1fs total\n", failures ? "FAILED" : "ALL PASS", failures, (now_ms() - t0) / 1000);
    return failures ? 1 : 0;
}
