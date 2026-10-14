/* Client side (rwmt Client/Patches/TickPatch.cs, Client/Session/MultiplayerSession.cs,
 * Client/Desyncs/SyncCoordinator.cs). A network thread receives into a queue; the game thread
 * steps the timer through dsnet_can_run_tick / dsnet_take_cmds_for_tick. */
#include "dsnet_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <fenv.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int dsnet_server_add_local(dsnet_server *s, int fd);

#define FP_RING   64
#define SIM_MAGIC 0x44534E31u      /* "DSN1": dsnet's own state at the head of every save */

typedef struct qcmd { dsnet_cmd c; uint8_t *data; } qcmd;
typedef struct fp { uint32_t tick; uint64_t hash, owners; int set; uint64_t sec[3], deep; } fp;   /* deep: 0 = not computed at this step */   /* sec: units, positions, rng (0 = not sent) */

/* State changed only by executed commands, so it is identical on every peer. */
typedef struct tag { uint8_t guid[16]; uint8_t owner; } tag;

typedef struct simstate {
    uint32_t present;                          /* bit per player */
    uint8_t speed[DSNET_MAX_PLAYERS], pause[DSNET_MAX_PLAYERS], story[DSNET_MAX_PLAYERS];
    uint32_t pause_order[DSNET_MAX_PLAYERS], pause_counter;   /* when each current pause vote began */
    uint8_t pause_src[DSNET_MAX_PLAYERS];                     /* who it counts for (auto pause: the cause) */
    char name[DSNET_MAX_PLAYERS][32];
    uint8_t admin;                               /* host admin mode */
    struct { uint8_t uid[16]; char name[32]; uint8_t slot, temp, color; } known[DSNET_MAX_KNOWN];   /* players by stable id;
                                                     temp: host has temporary control while he is away;
                                                     color: palette index, kept for the world (owner 2026-10-14) */
    struct { uint8_t reason, uid[16]; } hold[DSNET_MAX_HOLDS]; int nhold;   /* host holds (0 ticks while any) */
    int nknown;                                  /* tag owners below are indexes into known[] */
    tag own[DSNET_MAX_UNITS]; int nown;          /* unit -> owner */
    tag item[DSNET_MAX_ITEMS]; int nitem;        /* item -> owner (gear OWN_STORAGE) */
    uint8_t ready[DSNET_MAX_PLAYERS], party_n[DSNET_MAX_PLAYERS];
    uint8_t party[DSNET_MAX_PLAYERS][DSNET_PARTY_MAX][16];
    uint8_t rules[6];                            /* pause_mode, allow_speedup, gear_mode, units_per_player, event_choice, population */
    tag ctrl[DSNET_MAX_UNITS]; int nctrl;        /* lent units: unit -> controller (known[] index), DSNET_LEND */
    uint32_t lend_seq;                           /* lend events (local, not saved): count + the last one */
    struct { uint8_t unit[16]; int owner, from, to, why; } lend_last;
    uint32_t refused[DSNET_MAX_PLAYERS];         /* claims refused by the unit limit (local counter, not saved) */
} simstate;

struct dsnet_client {
    dsnet_link link;
    pthread_t th;
    volatile int running;
    pthread_mutex_t mu;
    pthread_cond_t cv;

    int welcomed, player, is_host, needs_state, disconnected, rejected, host_left;
    int has_world; uint8_t world_id[16]; char world_name[32];   /* host's world from the WELCOME (lan_v10 F17) */
    dsnet_chat_msg chat[128]; uint32_t chat_head, chat_tail;   /* chat ring (outside the sim) */
    double typing_until[DSNET_MAX_PLAYERS];                     /* "is typing" (ms clock), 0 = not */
    double typing_sent; int typing_on;                          /* own: last "on" sent, state */
    struct { int set; uint32_t map; float r[4]; double at; } view[DSNET_MAX_PLAYERS];   /* friends' views (outside the sim) */
    dsnet_mark_ev marks[64]; uint32_t mark_head, mark_tail;                            /* ping events ring */
    int allow_speedup, pause_mode, gear_mode, busy;
    uint32_t busy_mask;
    struct { uint8_t state; uint16_t ping, behind; } net[DSNET_MAX_PLAYERS];
    uint32_t fp_interval;
    uint32_t tick_until, remote_timer, remote_sent, received;
    float tpt_ms;
    qcmd *q; size_t qh, qn, qcap;
    simstate sim;
    uint32_t timer, cur_tick, cur_seq;
    int in_join_point;
    int exec_player;               /* issuer of the command being delivered, -1 outside callbacks */

    fp mine[FP_RING], host[FP_RING];
    int desynced, desync_reason; uint32_t bad_tick, last_good;
    dsnet_desync_info dinfo;       /* both sides' hashes at the bad step (lan_v10 report) */
    uint32_t fp_floor;             /* fingerprints of steps below this are not compared (after a partial resync / load) */
    uint32_t deep_checks, deep_fails, deep_last_ok, deep_last_bad;   /* deep hash compares (lan_v10) */
    int partial_ok;                /* P_DESYNC says "a partial resync follows" (no auto pause yet) */
    int patch;                     /* 0 none, 1 asked (runs on while desynced), 2 stopped at our PATCH_POINT */
    int in_patch_point;            /* host: inside the PATCH_POINT callback (dsnet_upload_patch allowed) */
    uint32_t patch_tick, patches;
    dsnet_patch_cb patch_cb; void *patch_user;
    uint8_t *patch_data; uint32_t patch_len, patch_data_tick; int has_patch;   /* may arrive before we reach the point */
    struct { uint8_t *data; size_t len; int from; } reports[4]; int nreports;   /* host: guests' report parts */
    uint8_t *full; size_t full_len; uint32_t full_tick; int has_full;             /* guest: host's newest full-state list */

    dsnet_load_cb load_cb; void *load_user;
    dsnet_blob_rx rx;
    uint8_t *pending; uint32_t pending_len, pending_tick; int has_pending;
    int catching_up, tc_seen;   /* tc_seen: a time control arrived since the last save */

    float real_time, avg_frame;
    double last_ka, last_ft, last_rx;
};

static void send_simple(dsnet_client *c, uint8_t type, uint32_t v) {
    uint8_t b[5], *p = b; p = put8(p, type); p = put32(p, v);
    dsnet_link_send(&c->link, b, p - b);
}

/* ---- fingerprints ---- */
static void fp_compare(dsnet_client *c, uint32_t tick) {
    fp *m = &c->mine[(tick / c->fp_interval) % FP_RING], *h = &c->host[(tick / c->fp_interval) % FP_RING];
    if (!m->set || !h->set || m->tick != tick || h->tick != tick || c->desynced || tick < c->fp_floor) return;
    if (m->deep && h->deep && m->hash == h->hash && m->owners == h->owners) {   /* the wider hash, where the fingerprint is
                                       equal (a fingerprint difference is a desync anyway): counted only, the game decides */
        c->deep_checks++;
        if (m->deep == h->deep) c->deep_last_ok = tick; else { c->deep_fails++; c->deep_last_bad = tick; }
    }
    if (m->hash == h->hash && m->owners == h->owners) { c->last_good = tick; return; }
    c->desynced = 1; c->bad_tick = tick;
    c->dinfo = (dsnet_desync_info){ tick, c->last_good, { m->hash, m->sec[0], m->sec[1], m->sec[2], m->owners },
                                    { h->hash, h->sec[0], h->sec[1], h->sec[2], h->owners }, m->deep, h->deep };
    c->desync_reason = (m->hash != h->hash ? DSNET_DESYNC_GAME : 0) | (m->owners != h->owners ? DSNET_DESYNC_OWNERS : 0);
    dsnet_logf("client %d: DESYNC at tick %u (last good %u): game %016llx/%016llx owners %016llx/%016llx", c->player, tick,
               c->last_good, (unsigned long long)m->hash, (unsigned long long)h->hash,
               (unsigned long long)m->owners, (unsigned long long)h->owners);
    uint8_t b[6] = { P_DESYNC }; put32(b + 1, tick); b[5] = c->partial_ok != 0;
    dsnet_link_send(&c->link, b, 6);
}

/* ---- network thread ---- */
static void process_timectl(dsnet_client *c) {     /* MultiplayerSession.ProcessTimeControl */
    if (c->received >= c->remote_sent && c->remote_timer > c->tick_until) c->tick_until = c->remote_timer;
}

static void on_frame(dsnet_client *c, const uint8_t *p, uint32_t n) {
    switch (p[0]) {
    case P_PING:
        if (n >= 5) { uint8_t b[5] = { P_PONG, p[1], p[2], p[3], p[4] }; dsnet_link_send(&c->link, b, 5); }
        break;
    case P_PLAYERS:
        if (n < 2 || n < 2 + 6u * p[1]) return;
        memset(c->net, 0, sizeof c->net);
        for (int i = 0; i < p[1]; i++) {
            const uint8_t *e = p + 2 + 6 * i;
            if (e[0] < DSNET_MAX_PLAYERS) { c->net[e[0]].state = e[1]; c->net[e[0]].ping = get16(e + 2); c->net[e[0]].behind = get16(e + 4); }
        }
        break;
    case P_CHAT:
        if (n >= 34) {
            dsnet_chat_msg *m = &c->chat[c->chat_tail++ % 128];
            if (c->chat_tail - c->chat_head > 128) c->chat_head = c->chat_tail - 128;   /* full: drop the oldest */
            m->player = p[1]; m->kind = DSNET_CHAT_TEXT; memcpy(m->name, p + 2, 31); m->name[31] = 0;
            uint32_t tl = n - 34 > DSNET_CHAT_MAX ? DSNET_CHAT_MAX : n - 34; memcpy(m->text, p + 34, tl); m->text[tl] = 0;
            if (p[1] < DSNET_MAX_PLAYERS) c->typing_until[p[1]] = 0;          /* the line arrived: no longer typing */
        }
        break;
    case P_FULL:
        if (n >= 5 && !c->is_host) {
            free(c->full); c->full = malloc(n - 5 + 1);
            if (c->full) { memcpy(c->full, p + 5, n - 5); c->full[n - 5] = 0; c->full_len = n - 5; c->full_tick = get32(p + 1); c->has_full = 1; }
        }
        break;
    case P_VIEW:
        if (n >= 22 && p[1] < DSNET_MAX_PLAYERS) {
            int f = p[1]; c->view[f].set = 1; c->view[f].map = get32(p + 2);
            for (int i = 0; i < 4; i++) { uint32_t u = get32(p + 6 + 4 * i); memcpy(&c->view[f].r[i], &u, 4); }
            c->view[f].at = dsnet_now_ms();
        }
        break;
    case P_MARK:
        if (n >= 13) {
            dsnet_mark_ev *m = &c->marks[c->mark_tail++ % 64];
            if (c->mark_tail - c->mark_head > 64) c->mark_head = c->mark_tail - 64;   /* full: drop the oldest */
            m->from = p[1]; m->op = p[2]; m->owner = p[3]; m->id = p[4]; m->map_key = get32(p + 5);
            m->x = (int16_t)get16(p + 9); m->y = (int16_t)get16(p + 11);
        }
        break;
    case P_TYPING:
        if (n >= 3 && p[1] < DSNET_MAX_PLAYERS) c->typing_until[p[1]] = p[2] ? dsnet_now_ms() + DSNET_TYPING_MS : 0;
        break;
    case P_BYE:                                              /* the host ended the session (not our own link) */
        c->host_left = 1;
        dsnet_logf("client %d: the host ended the session", c->player);
        break;
    case P_REJECT:
        if (n >= 2) c->rejected = p[1];
        dsnet_logf("client: rejected by server, reason %d", c->rejected);
        pthread_cond_broadcast(&c->cv);
        break;
    case P_WELCOME:
        if (n < 19) return;
        c->player = p[1]; c->is_host = p[2]; c->needs_state = p[3];
        c->timer = get32(p + 4); c->tick_until = c->timer;
        c->received = c->remote_sent = get32(p + 8);
        c->fp_interval = get32(p + 12) ? get32(p + 12) : 30;
        c->allow_speedup = p[16]; c->pause_mode = p[17]; c->gear_mode = n >= 19 ? p[18] : 0;
        c->sim.rules[0] = p[17]; c->sim.rules[1] = p[16]; c->sim.rules[2] = n >= 19 ? p[18] : 0;   /* a save replaces them */
        c->sim.rules[3] = n >= 20 && p[19] ? p[19] : DSNET_UNITS_DEFAULT;
        c->sim.rules[4] = n >= 21 && p[20] <= DSNET_EVENT_MAJORITY ? p[20] : DSNET_EVENT_OWNER;
        c->sim.rules[5] = n >= 22 && p[21] <= DSNET_POP_MULTIPLIED ? p[21] : DSNET_POP_SHARED;
        if (n >= 71 && p[22]) { c->has_world = 1; memcpy(c->world_id, p + 23, 16); memcpy(c->world_name, p + 39, 32); c->world_name[31] = 0; }
        c->welcomed = 1;
        pthread_cond_broadcast(&c->cv);
        break;
    case P_SCMD: {
        if (n < 14) return;
        uint32_t seq = get32(p + 1);
        if (seq < c->received) return;
        if (seq != c->received) dsnet_logf("client %d: command gap %u -> %u", c->player, c->received, seq);
        uint16_t len = get16(p + 12);
        if (14 + (uint32_t)len > n) return;
        if (c->qn == c->qcap) {
            if (c->qh) { memmove(c->q, c->q + c->qh, (c->qn - c->qh) * sizeof *c->q); c->qn -= c->qh; c->qh = 0; }
            if (c->qn == c->qcap) { c->qcap = c->qcap ? c->qcap * 2 : 256; c->q = realloc(c->q, c->qcap * sizeof *c->q); }
        }
        qcmd *e = &c->q[c->qn++];
        e->data = len ? malloc(len) : NULL;
        if (len) memcpy(e->data, p + 14, len);
        e->c = (dsnet_cmd){ p[5], get32(p + 6), p[10], p[11], len, e->data, seq };
        c->received = seq + 1;
        process_timectl(c);
        break;
    }
    case P_TIMECTL:
        if (n < 13) return;
        c->remote_timer = get32(p + 1); c->remote_sent = get32(p + 5); c->tpt_ms = get32(p + 9) / 1000.0f;
        c->tc_seen = 1;
        if (n >= 17) c->busy_mask = get32(p + 13);
        process_timectl(c);
        break;
    case P_FP: {
        if (n < 21 || c->is_host || c->needs_state) return;
        uint32_t tick = get32(p + 1);
        if (tick < c->fp_floor) return;
        fp *h = &c->host[(tick / c->fp_interval) % FP_RING];
        *h = (fp){ tick, get64(p + 5), get64(p + 13), 1, { 0, 0, 0 }, 0 };
        if (n >= 45) for (int i = 0; i < 3; i++) h->sec[i] = get64(p + 21 + 8 * i);
        if (n >= 53) h->deep = get64(p + 45);
        fp_compare(c, tick);
        break;
    }
    case P_BLOB: {
        int r = dsnet_blob_feed(&c->rx, p + 1, n - 1);
        if (r < 0) { dsnet_logf("client %d: bad blob fragment", c->player); return; }
        if (r == 0) return;
        if (c->rx.kind == BLOB_PATCH) {          /* our partial resync: applied on the game thread (dsnet_can_run_tick) */
            if ((c->patch == 1 && c->rx.tick >= c->timer) || (c->patch == 2 && c->rx.tick == c->patch_tick)) {
                free(c->patch_data);
                c->patch_data = c->rx.buf; c->patch_len = c->rx.total; c->patch_data_tick = c->rx.tick; c->has_patch = 1;
                dsnet_logf("client %d: patch received (%u bytes) for tick %u", c->player, c->patch_len, c->rx.tick);
            } else { dsnet_logf("client %d: patch for tick %u ignored (state %d, tick %u)", c->player, c->rx.tick, c->patch, c->patch_tick); free(c->rx.buf); }
            c->rx.buf = NULL;
            return;
        }
        if (c->rx.kind == BLOB_REPORT) {         /* host: a guest's desync report part (seq_after = its slot) */
            if (c->is_host && c->nreports < 4) {
                c->reports[c->nreports].data = c->rx.buf; c->reports[c->nreports].len = c->rx.total;
                c->reports[c->nreports].from = (int)c->rx.seq_after; c->nreports++;
                dsnet_logf("client %d: desync report part from player %u (%u bytes)", c->player, c->rx.seq_after, c->rx.total);
            } else free(c->rx.buf);
            c->rx.buf = NULL;
            return;
        }
        if (c->rx.kind != BLOB_SAVE) return;
        /* save arrived: everything from here on is the log after the join point */
        for (size_t i = c->qh; i < c->qn; i++) free(c->q[i].data);
        c->qh = c->qn = 0;
        free(c->pending);
        c->pending = c->rx.buf; c->pending_len = c->rx.total; c->pending_tick = c->rx.tick;
        c->rx.buf = NULL;
        c->received = c->remote_sent = c->rx.seq_after;
        c->timer = c->tick_until = c->remote_timer = c->rx.tick;
        c->has_pending = 1; c->tc_seen = 0;
        c->patch = 0; c->has_patch = 0; free(c->patch_data); c->patch_data = NULL;   /* a full resync replaces a partial one */
        dsnet_logf("client %d: save received (%u bytes) for tick %u", c->player, c->pending_len, c->pending_tick);
        break;
    }
    }
}

static void *client_main(void *arg) {
    dsnet_client *c = arg;
    while (c->running) {
        struct pollfd pf = { c->link.tr.poll_fd(c->link.tr.t), POLLIN | (dsnet_link_has_out(&c->link) ? POLLOUT : 0), 0 };
        poll(&pf, 1, 4);
        dsnet_link_fill(&c->link);
        const uint8_t *body; uint32_t n;
        pthread_mutex_lock(&c->mu);
        double now = dsnet_now_ms();
        while (dsnet_link_next(&c->link, &body, &n)) { on_frame(c, body, n); c->last_rx = now; }
        /* silent cut (no FIN/RST): the server sends every ~33 ms, so a long silence means the link is gone */
        int tmo = dsnet_rx_timeout_ms();
        if (c->welcomed && tmo && now - c->last_rx > tmo && !c->link.dead) {
            dsnet_logf("client %d: nothing received for %d ms, link gone", c->player, (int)(now - c->last_rx));
            c->link.dead = 1;
        }
        if (c->welcomed && now - c->last_ka >= 100) {       /* keep-alive: how far behind tickUntil we are */
            uint8_t b[9], *p = b; p = put8(p, P_KEEPALIVE); p = put32(p, c->timer); p = put32(p, c->tick_until);
            dsnet_link_send(&c->link, b, p - b);
            c->last_ka = now;
        }
        if (c->welcomed && c->avg_frame > 0 && now - c->last_ft >= 32) {
            send_simple(c, P_FRAMETIME, (uint32_t)(c->avg_frame * 1000));
            c->last_ft = now;
        }
        pthread_mutex_unlock(&c->mu);
        if (dsnet_link_flush(&c->link) < 0 || c->link.dead) {
            pthread_mutex_lock(&c->mu);
            if (!c->disconnected) dsnet_logf("client %d: disconnected", c->player);
            c->disconnected = 1;
            pthread_cond_broadcast(&c->cv);
            pthread_mutex_unlock(&c->mu);
            break;
        }
    }
    return NULL;
}

static dsnet_client *client_new(int fd, const char *name, int want_host) {
    dsnet_client *c = calloc(1, sizeof *c);
    dsnet_link_init(&c->link, dsnet_fd_transport(fd));
    pthread_mutex_init(&c->mu, NULL);
    pthread_cond_init(&c->cv, NULL);
    c->player = -1; c->exec_player = -1; c->fp_interval = 30; c->tpt_ms = 1000.0f / 60;
    uint8_t b[96], *p = b;
    size_t nl = name ? strlen(name) : 0; if (nl > 31) nl = 31;
    size_t bl = strlen(dsnet_build_id);
    p = put8(p, P_HELLO); p = put16(p, DSNET_PROTO); p = put8(p, want_host);
    p = put32(p, dsnet_mod_version); p = put32(p, (uint32_t)fegetround());
    if (dsnet_uid_set) memcpy(p, dsnet_uid, 16); else arc4random_buf(p, 16);
    p += 16;
    p = put8(p, bl); memcpy(p, dsnet_build_id, bl); p += bl;
    memcpy(p, name, nl); p += nl;
    dsnet_link_send(&c->link, b, p - b);
    c->last_rx = dsnet_now_ms();
    c->running = 1;
    pthread_create(&c->th, NULL, client_main, c);
    return c;
}

dsnet_client *dsnet_client_connect_local(dsnet_server *s, const char *name) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) return NULL;
    if (dsnet_server_add_local(s, sv[0])) { close(sv[0]); close(sv[1]); return NULL; }
    return client_new(sv[1], name, 1);
}

/* ---- TCP connect with a time limit ----
 * tcp_connect: name lookup (numeric first: no DNS for "1.2.3.4"), then a non-blocking connect polled in 50 ms slices
 * until the deadline or *cancel. Returns a blocking, connected fd, or an enum dsnet_connect_error. */
static double conn_left(double deadline) { return deadline - dsnet_now_ms(); }
static int tcp_connect(const char *host, uint16_t port, double deadline, volatile int *cancel) {
    char ps[8]; snprintf(ps, sizeof ps, "%u", port);
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM, .ai_flags = AI_NUMERICHOST }, *res = NULL;
    if (getaddrinfo(host, ps, &hints, &res)) {
        hints.ai_flags = 0;
        if (getaddrinfo(host, ps, &hints, &res) || !res) return DSNET_CONN_BAD_ADDRESS;
    }
    if (conn_left(deadline) <= 0 || (cancel && *cancel)) { freeaddrinfo(res); return DSNET_CONN_TIMEOUT; }
    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd < 0) { freeaddrinfo(res); return DSNET_CONN_UNREACHABLE; }
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    int fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int r = connect(fd, res->ai_addr, res->ai_addrlen), err = r ? errno : 0;
    freeaddrinfo(res);
    if (r && err == EINPROGRESS) {
        err = ETIMEDOUT;
        for (;;) {
            double left = conn_left(deadline);
            if (left <= 0 || (cancel && *cancel)) break;
            struct pollfd pf = { fd, POLLOUT, 0 };
            int n = poll(&pf, 1, left < 50 ? (int)left + 1 : 50);
            if (n > 0) { socklen_t l = sizeof err; if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l)) err = errno; break; }
            if (n < 0 && errno != EINTR) { err = errno; break; }
        }
    }
    if (err) {
        close(fd);
        return err == ECONNREFUSED ? DSNET_CONN_REFUSED : err == ETIMEDOUT ? DSNET_CONN_TIMEOUT : DSNET_CONN_UNREACHABLE;
    }
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

struct dsnet_connector {
    pthread_mutex_t mu;
    int refs, state, fd, adopted;     /* state: 0 trying, 1 connected, < 0 enum dsnet_connect_error */
    volatile int cancel;
    double deadline;
    char host[256]; uint16_t port;
};
static void connector_unref(dsnet_connector *k) {
    pthread_mutex_lock(&k->mu); int last = --k->refs == 0; pthread_mutex_unlock(&k->mu);
    if (!last) return;
    if (k->fd >= 0 && !k->adopted) close(k->fd);
    pthread_mutex_destroy(&k->mu); free(k);
}
static void *connector_main(void *arg) {
    dsnet_connector *k = arg;
    int r = tcp_connect(k->host, k->port, k->deadline, &k->cancel);
    pthread_mutex_lock(&k->mu);
    if (k->state == 0 && !k->cancel) { if (r >= 0) { k->fd = r; k->state = 1; } else k->state = r; }
    else if (r >= 0) close(r);                               /* gave up (timeout seen by the poll, or cancelled) */
    pthread_mutex_unlock(&k->mu);
    connector_unref(k);
    return NULL;
}
dsnet_connector *dsnet_connect_start(const char *host, uint16_t port, int timeout_ms) {
    dsnet_connector *k = calloc(1, sizeof *k);
    if (!k) return NULL;
    pthread_mutex_init(&k->mu, NULL);
    k->refs = 2; k->fd = -1;
    k->deadline = dsnet_now_ms() + (timeout_ms > 0 ? timeout_ms : DSNET_CONNECT_TIMEOUT_MS);
    snprintf(k->host, sizeof k->host, "%s", host ? host : ""); k->port = port;
    pthread_t th;
    if (pthread_create(&th, NULL, connector_main, k)) { k->state = DSNET_CONN_UNREACHABLE; k->refs = 1; return k; }
    pthread_detach(th);
    return k;
}
int dsnet_connect_poll(dsnet_connector *k) {
    if (!k) return DSNET_CONN_UNREACHABLE;
    pthread_mutex_lock(&k->mu);
    if (k->state == 0 && conn_left(k->deadline) < -250) k->state = DSNET_CONN_TIMEOUT;   /* stuck in the name lookup */
    int r = k->state;
    pthread_mutex_unlock(&k->mu);
    return r;
}
/* one adopted socket, taken by the next dsnet_client_connect_tcp to the same host + port */
static pthread_mutex_t adopt_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { int fd; char host[256]; uint16_t port; } adopted = { -1, "", 0 };
int dsnet_connect_adopt(dsnet_connector *k) {
    if (!k) return -1;
    pthread_mutex_lock(&k->mu);
    int ok = k->state == 1 && k->fd >= 0 && !k->adopted;
    if (ok) {
        pthread_mutex_lock(&adopt_mu);
        if (adopted.fd >= 0) close(adopted.fd);
        adopted.fd = k->fd; snprintf(adopted.host, sizeof adopted.host, "%s", k->host); adopted.port = k->port;
        pthread_mutex_unlock(&adopt_mu);
        k->adopted = 1;
    }
    pthread_mutex_unlock(&k->mu);
    return ok ? 0 : -1;
}
void dsnet_connect_free(dsnet_connector *k) {
    if (!k) return;
    pthread_mutex_lock(&k->mu); k->cancel = 1; pthread_mutex_unlock(&k->mu);
    connector_unref(k);
}

dsnet_client *dsnet_client_connect_tcp(const char *host, uint16_t port, const char *name) {
    int fd = -1;
    pthread_mutex_lock(&adopt_mu);
    if (adopted.fd >= 0 && adopted.port == port && !strcmp(adopted.host, host)) { fd = adopted.fd; adopted.fd = -1; }
    pthread_mutex_unlock(&adopt_mu);
    if (fd < 0) fd = tcp_connect(host, port, dsnet_now_ms() + DSNET_CONNECT_TIMEOUT_MS, NULL);
    if (fd < 0) { dsnet_logf("client: cannot connect to %s:%u (%d)", host, port, fd); return NULL; }
    return client_new(fd, name, 0);
}

void dsnet_client_close(dsnet_client *c) {
    if (!c) return;
    c->running = 0;
    pthread_join(c->th, NULL);
    dsnet_link_flush(&c->link);
    dsnet_link_free(&c->link);
    for (size_t i = c->qh; i < c->qn; i++) free(c->q[i].data);
    free(c->q); free(c->rx.buf); free(c->pending); free(c->patch_data); free(c->full);
    for (int i = 0; i < c->nreports; i++) free(c->reports[i].data);
    pthread_mutex_destroy(&c->mu); pthread_cond_destroy(&c->cv);
    free(c);
}

void dsnet_set_load_cb(dsnet_client *c, dsnet_load_cb cb, void *user) {
    pthread_mutex_lock(&c->mu); c->load_cb = cb; c->load_user = user; pthread_mutex_unlock(&c->mu);
}

int dsnet_wait_welcome(dsnet_client *c, int timeout_ms) {
    double end = dsnet_now_ms() + timeout_ms;
    pthread_mutex_lock(&c->mu);
    while (!c->welcomed && !c->disconnected && !c->rejected && dsnet_now_ms() < end) {
        struct timespec ts = { 0, 5 * 1000000 };
        pthread_cond_timedwait_relative_np(&c->cv, &c->mu, &ts);
    }
    int r = c->welcomed ? 0 : c->rejected ? -2 : -1;
    pthread_mutex_unlock(&c->mu);
    return r;
}

const char *dsnet_reject_text(int why) {
    switch (why) {
    case DSNET_REJ_PROTO: return "the multiplayer version is different from the host's: both players need the same Dungeon Settlers MP app";
    case DSNET_REJ_BUILD: return "your game version is different from the host's: update both games";
    case DSNET_REJ_MOD: return "the mod version is different from the host's: both players need the same Dungeon Settlers MP app";
    case DSNET_REJ_ROUNDING: return "your Mac does math differently from the host's Mac, so the two games would drift apart";
    case DSNET_REJ_FULL: return "the game is full";
    case DSNET_REJ_DUP_ID: return "you are already in this game (same Steam account or same copy of the game)";
    case DSNET_REJ_OTHER_WORLD: return "the host is playing another world now";
    default: return "";
    }
}

int dsnet_reject_reason(dsnet_client *c) {
    pthread_mutex_lock(&c->mu); int r = c->rejected; pthread_mutex_unlock(&c->mu); return r;
}

/* ---- game thread API ---- */
int dsnet_submit(dsnet_client *c, uint8_t type, uint8_t scope, const void *payload, uint16_t len) {
    if (len > DSNET_MAX_PAYLOAD) return -1;
    uint8_t b[8 + DSNET_MAX_PAYLOAD], *p = b;
    p = put8(p, P_CMD); p = put8(p, type); p = put8(p, scope); p = put16(p, len);
    if (len) memcpy(p, payload, len);
    return dsnet_link_send(&c->link, b, 5 + len);
}

/* dsnet's state in front of a save: fixed part, then only the used table entries. Returns its size. */
static size_t sim_serialize(const simstate *s, uint8_t *out) {
    uint8_t *p = out;
    p = put32(p, SIM_MAGIC); p = put32(p, s->present);
    memcpy(p, s->speed, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    memcpy(p, s->pause, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    memcpy(p, s->story, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) p = put32(p, s->pause_order[i]);
    memcpy(p, s->pause_src, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    p = put32(p, s->pause_counter);
    memcpy(p, s->name, sizeof s->name); p += sizeof s->name;
    *p++ = s->admin;
    memcpy(p, s->rules, 6); p += 6;
    *p++ = s->nknown;
    for (int i = 0; i < s->nknown; i++) { memcpy(p, s->known[i].uid, 16); memcpy(p + 16, s->known[i].name, 32); p[48] = s->known[i].slot; p[49] = s->known[i].temp; p[50] = s->known[i].color; p += 51; }
    *p++ = s->nhold;
    for (int i = 0; i < s->nhold; i++) { *p++ = s->hold[i].reason; memcpy(p, s->hold[i].uid, 16); p += 16; }
    p = put16(p, s->nown);
    for (int i = 0; i < s->nown; i++) { memcpy(p, s->own[i].guid, 16); p += 16; *p++ = s->own[i].owner; }
    p = put16(p, s->nitem);
    for (int i = 0; i < s->nitem; i++) { memcpy(p, s->item[i].guid, 16); p += 16; *p++ = s->item[i].owner; }
    memcpy(p, s->ready, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    memcpy(p, s->party_n, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) { memcpy(p, s->party[i], 16 * s->party_n[i]); p += 16 * s->party_n[i]; }
    p = put16(p, s->nctrl);
    for (int i = 0; i < s->nctrl; i++) { memcpy(p, s->ctrl[i].guid, 16); p += 16; *p++ = s->ctrl[i].owner; }
    return (size_t)(p - out);
}
#define SIM_FIXED (4 + 4 + 3 * DSNET_MAX_PLAYERS + 5 * DSNET_MAX_PLAYERS + 4 + 32 * DSNET_MAX_PLAYERS + 1 + 6)
#define SIM_BYTES (SIM_FIXED + 1 + DSNET_MAX_KNOWN * 51 + 1 + DSNET_MAX_HOLDS * 17 + 2 + DSNET_MAX_UNITS * 17 + 2 + DSNET_MAX_ITEMS * 17 \
                   + 2 * DSNET_MAX_PLAYERS + DSNET_MAX_PLAYERS * DSNET_PARTY_MAX * 16 + 2 + DSNET_MAX_UNITS * 17)   /* maximum */
_Static_assert(SIM_BYTES == DSNET_SAVE_OVERHEAD, "update DSNET_SAVE_OVERHEAD");

/* returns bytes consumed, or -1 */
static long sim_deserialize(simstate *s, const uint8_t *p0, size_t n) {
    const uint8_t *p = p0, *end = p0 + n;
    if (n < SIM_FIXED + 3 || get32(p) != SIM_MAGIC) return -1;
    s->present = get32(p + 4); p += 8;
    memcpy(s->speed, p, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    memcpy(s->pause, p, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    memcpy(s->story, p, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) { s->pause_order[i] = get32(p); p += 4; }
    memcpy(s->pause_src, p, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    s->pause_counter = get32(p); p += 4;
    memcpy(s->name, p, sizeof s->name); p += sizeof s->name;
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) s->name[i][31] = 0;
    s->admin = *p++;
    memcpy(s->rules, p, 6); p += 6;
    if (s->rules[4] > DSNET_EVENT_MAJORITY) s->rules[4] = DSNET_EVENT_OWNER;
    if (s->rules[5] > DSNET_POP_MULTIPLIED) s->rules[5] = DSNET_POP_SHARED;
    if (!s->rules[3]) s->rules[3] = DSNET_UNITS_DEFAULT;
    s->nknown = *p++;
    if (s->nknown > DSNET_MAX_KNOWN || end - p < s->nknown * 51 + 1) return -1;
    for (int i = 0; i < s->nknown; i++) {
        memcpy(s->known[i].uid, p, 16); memcpy(s->known[i].name, p + 16, 32); s->known[i].name[31] = 0; s->known[i].slot = p[48]; s->known[i].temp = p[49]; s->known[i].color = p[50] % DSNET_COLORS; p += 51;
    }
    s->nhold = *p++;
    if (s->nhold > DSNET_MAX_HOLDS || end - p < s->nhold * 17 + 2) return -1;
    for (int i = 0; i < s->nhold; i++) { s->hold[i].reason = p[0]; memcpy(s->hold[i].uid, p + 1, 16); p += 17; }
    s->nown = get16(p); p += 2;
    if (s->nown > DSNET_MAX_UNITS || end - p < s->nown * 17 + 2) return -1;
    for (int i = 0; i < s->nown; i++) { memcpy(s->own[i].guid, p, 16); s->own[i].owner = p[16]; p += 17; }
    s->nitem = get16(p); p += 2;
    if (s->nitem > DSNET_MAX_ITEMS || end - p < s->nitem * 17 + 2 * DSNET_MAX_PLAYERS) return -1;
    for (int i = 0; i < s->nitem; i++) { memcpy(s->item[i].guid, p, 16); s->item[i].owner = p[16]; p += 17; }
    memcpy(s->ready, p, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    memcpy(s->party_n, p, DSNET_MAX_PLAYERS); p += DSNET_MAX_PLAYERS;
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) {
        if (s->party_n[i] > DSNET_PARTY_MAX || end - p < 16 * s->party_n[i]) return -1;
        memcpy(s->party[i], p, 16 * s->party_n[i]); p += 16 * s->party_n[i];
    }
    if (end - p < 2) return -1;
    s->nctrl = get16(p); p += 2;
    if (s->nctrl > DSNET_MAX_UNITS || end - p < s->nctrl * 17) return -1;
    for (int i = 0; i < s->nctrl; i++) { memcpy(s->ctrl[i].guid, p, 16); s->ctrl[i].owner = p[16]; p += 17; }
    return (long)(p - p0);
}

int dsnet_can_run_tick(dsnet_client *c, uint32_t timer) {
    pthread_mutex_lock(&c->mu);
    if (c->has_pending) {          /* load the save on the game thread */
        uint8_t *data = c->pending; uint32_t len = c->pending_len, tick = c->pending_tick;
        c->pending = NULL; c->has_pending = 0;
        long used = sim_deserialize(&c->sim, data, len);
        memset(c->mine, 0, sizeof c->mine); memset(c->host, 0, sizeof c->host);
        c->desynced = 0; c->needs_state = 0; c->catching_up = 1; c->real_time = 0; c->fp_floor = tick;
        dsnet_load_cb cb = c->load_cb; void *u = c->load_user;
        pthread_mutex_unlock(&c->mu);
        if (used < 0) dsnet_logf("client %d: save has no dsnet header", c->player);
        else if (cb) cb(tick, data + used, len - used, u);
        free(data);
        return 0;                  /* the game re-reads its timer (dsnet_timer) */
    }
    if (c->has_patch && c->patch == 2 && c->patch_data_tick == c->patch_tick) {   /* our partial resync arrived: apply on the game thread, then go on */
        uint8_t *data = c->patch_data; uint32_t len = c->patch_len, tick = c->patch_tick;
        c->patch_data = NULL; c->has_patch = 0;
        simstate *keep = malloc(sizeof *keep);
        if (keep) *keep = c->sim;
        long used = sim_deserialize(&c->sim, data, len);   /* the host's owners / votes / rules at the same place */
        dsnet_patch_cb cb = c->patch_cb; void *u = c->patch_user;
        pthread_mutex_unlock(&c->mu);
        int rc = used < 0 || !cb ? -1 : cb(tick, data + used, len - used, u);
        pthread_mutex_lock(&c->mu);
        c->patch = 0;
        if (rc == 0) {
            c->desynced = 0; c->desync_reason = 0; c->patches++;
            memset(c->mine, 0, sizeof c->mine); memset(c->host, 0, sizeof c->host);
            c->fp_floor = tick;                       /* the steps before the patch were diverged on purpose */
            c->catching_up = 1; c->real_time = 0;     /* the others went on meanwhile: P_READY once caught up */
        } else if (used >= 0 && keep) {
            c->sim = *keep;                           /* a full resync is next; keep what we had until then */
        }
        free(keep);
        uint8_t b[2] = { P_PATCHED, rc == 0 };
        dsnet_link_send(&c->link, b, 2);
        dsnet_logf("client %d: partial resync at tick %u %s", c->player, tick, rc == 0 ? "applied" : "FAILED (full resync next)");
        pthread_mutex_unlock(&c->mu);
        free(data);
        return 0;
    }
    int ok = c->welcomed && !c->needs_state && (!c->desynced || c->patch == 1) && c->patch != 2 && timer < c->tick_until;
    pthread_mutex_unlock(&c->mu);
    return ok;
}

static int tag_find(const tag *t, int n, const uint8_t *g) {
    for (int i = 0; i < n; i++) if (!memcmp(t[i].guid, g, 16)) return i;
    return -1;
}
#define SLOT_ABSENT 0xFE

/* known players: tags store an index into known[], a player's current slot lives there */
static int reg_of_slot(const simstate *s, int slot) {
    for (int i = 0; i < s->nknown; i++) if (s->known[i].slot == slot) return i;
    return -1;
}
/* a player's colour (palette index) is picked once, at his first join in this world, and saved with the world:
   the lowest index no other known player of this world has (all taken: by order). Same on every peer. */
static uint8_t free_color(const simstate *s, int self) {
    unsigned used = 0;
    for (int k = 0; k < s->nknown; k++) if (k != self) used |= 1u << (s->known[k].color % DSNET_COLORS);
    for (int c = 0; c < DSNET_COLORS; c++) if (!(used >> c & 1u)) return (uint8_t)c;
    return (uint8_t)(self % DSNET_COLORS);
}
static int reg_of_uid(simstate *s, const uint8_t *uid, int add) {
    for (int i = 0; i < s->nknown; i++) if (!memcmp(s->known[i].uid, uid, 16)) return i;
    if (!add || s->nknown == DSNET_MAX_KNOWN) return -1;
    int i = s->nknown++;
    memcpy(s->known[i].uid, uid, 16); s->known[i].name[0] = 0; s->known[i].slot = SLOT_ABSENT; s->known[i].temp = 0;
    s->known[i].color = free_color(s, i);
    return i;
}
/* owner as the API shows it: slot if here, DSNET_OWNER_RESERVED if away, -1 free */
static int reg_view(const simstate *s, int reg) {
    if (reg < 0 || reg >= s->nknown) return -1;
    return s->known[reg].slot < DSNET_MAX_PLAYERS ? s->known[reg].slot : DSNET_OWNER_RESERVED;
}
static int tag_reg(const tag *t, int n, const uint8_t *g) { int i = tag_find(t, n, g); return i >= 0 ? t[i].owner : -1; }
static int unit_owner(simstate *s, const uint8_t *g) { return reg_view(s, tag_reg(s->own, s->nown, g)); }

/* set / clear an owner tag (no rule check); reg = index into known[] or DSNET_NO_PLAYER */
static void tag_set(tag *t, int *n, int cap, const uint8_t *g, int reg) {
    int i = tag_find(t, *n, g);
    if (reg == DSNET_NO_PLAYER || reg < 0) { if (i >= 0) t[i] = t[--*n]; return; }
    if (reg >= DSNET_MAX_KNOWN) return;
    if (i < 0) { if (*n == cap) return; i = (*n)++; memcpy(t[i].guid, g, 16); }
    t[i].owner = reg;
}

/* rule shared by units and items: players claim free / release own; the host may assign free and his own
 * ones to anyone; another player's ones, or RESERVED ones (owner away), only in admin mode */
static void tag_cmd(simstate *s, tag *t, int *n, int cap, uint8_t issuer, const uint8_t *g, uint8_t owner) {
    int cur = reg_view(s, tag_reg(t, *n, g));                 /* slot, -1 free, -2 reserved */
    int reg = owner == DSNET_NO_PLAYER ? DSNET_NO_PLAYER : reg_of_slot(s, owner);
    if (reg < 0) return;                                      /* new owner must be here */
    if (issuer == 0) { if (!s->admin && cur != -1 && cur != 0) return; }
    else if (!((cur == -1 || cur == issuer) && (owner == issuer || owner == DSNET_NO_PLAYER))) return;
    tag_set(t, n, cap, g, reg);
}

static void clear_ready(simstate *s, int pl) { s->ready[pl] = 0; s->party_n[pl] = 0; }

/* a unit changed owner: drop it from other players' marks and clear their ready */
static void fix_marks(simstate *s, const uint8_t *g, int owner) {
    for (int pl = 0; pl < DSNET_MAX_PLAYERS; pl++) {
        if (pl == owner) continue;
        for (int k = 0; k < s->party_n[pl]; k++) if (!memcmp(s->party[pl][k], g, 16)) {
            memmove(s->party[pl][k], s->party[pl][k + 1], 16 * (s->party_n[pl] - k - 1));
            s->party_n[pl]--; s->ready[pl] = 0;
            break;
        }
    }
}
static int owned_count(simstate *s, int slot) {
    int reg = reg_of_slot(s, slot), n = 0;
    if (reg < 0) return 0;
    for (int i = 0; i < s->nown; i++) if (s->own[i].owner == reg) n++;
    return n;
}
static int rules_ok(const uint8_t *r) {
    return r[0] <= DSNET_PAUSE_ALL_VOTE && r[1] <= 1 && r[2] <= DSNET_GEAR_OWN_STORAGE
        && (r[3] == 1 || r[3] == 2 || r[3] == 3 || r[3] == 4 || r[3] == 6);
}
/* the units-per-player rule; 0 (a state without the rule set) = the default, never "no units" (uxgame found get/set trap) */
#define UNITS_RULE(s) ((s)->rules[3] ? (s)->rules[3] : DSNET_UNITS_DEFAULT)
/* lending ends for units whose owner changed (owner tag no longer the lender's) and for a leaving borrower */
static void lend_event(simstate *s, const uint8_t *g, int owner, int from, int to, int why) {
    s->lend_seq++; memcpy(s->lend_last.unit, g, 16);
    s->lend_last.owner = owner; s->lend_last.from = from; s->lend_last.to = to; s->lend_last.why = why;
}
static void ctrl_drop_reg(simstate *s, int reg) {
    for (int i = s->nctrl; i-- > 0; ) if (s->ctrl[i].owner == reg) {
        int bslot = s->known[reg].slot < DSNET_MAX_PLAYERS ? s->known[reg].slot : -1, ow = unit_owner(s, s->ctrl[i].guid);
        lend_event(s, s->ctrl[i].guid, ow, bslot, ow < 0 ? 0 : ow, DSNET_LEND_RETURNED);
        s->ctrl[i] = s->ctrl[--s->nctrl];
    }
}
static void unit_set(simstate *s, const uint8_t *g, int slot) {
    if (unit_owner(s, g) != slot && owned_count(s, slot) >= UNITS_RULE(s)) { s->refused[slot]++; return; }   /* recruit: stays free */
    tag_set(s->own, &s->nown, DSNET_MAX_UNITS, g, reg_of_slot(s, slot));
    fix_marks(s, g, unit_owner(s, g));
}

/* EXPEDITION_START payload when every present player is ready; resets the ready flags (marks kept). */
static int build_start(simstate *s, uint8_t *out) {
    if (!s->present) return 0;
    int units = 0;
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) if (s->present >> i & 1) { if (!s->ready[i]) return 0; units += s->party_n[i]; }
    if (!units) return 0;                /* everyone ready but nobody goes: no start (the ready flags stay) */
    uint8_t *p = out + 1; int cnt = 0;
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) if (s->present >> i & 1) {
        *p++ = i; *p++ = s->party_n[i];
        memcpy(p, s->party[i], s->party_n[i] * 16); p += s->party_n[i] * 16;
        cnt++; s->ready[i] = 0;          /* marks stay for the next run */
    }
    out[0] = cnt;
    return (int)(p - out);
}

/* Effects dsnet itself keeps track of; runs in command order on every peer. */
static void sim_apply(simstate *s, const dsnet_cmd *cmd) {
    uint8_t pl = cmd->player;
    switch (cmd->type) {
    case DSNET_PLAYER_JOIN:
        if (pl < DSNET_MAX_PLAYERS) {
            s->present |= 1u << pl; s->speed[pl] = 1; s->pause[pl] = 0; s->story[pl] = 0; clear_ready(s, pl);
            int n = cmd->len > 16 ? cmd->len - 16 : 0; if (n > 31) n = 31;
            if (cmd->len >= 16) memcpy(s->name[pl], cmd->payload + 16, n);
            s->name[pl][n] = 0;
            for (int k = 0; k < s->nknown; k++) if (s->known[k].slot == pl) s->known[k].slot = SLOT_ABSENT;   /* stale */
            int reg = cmd->len >= 16 ? reg_of_uid(s, cmd->payload, 1) : -1;   /* known id: his units are his again */
            if (reg >= 0) {
                s->known[reg].slot = pl; s->known[reg].temp = 0;      /* he is back: temporary control ends */
                memcpy(s->known[reg].name, s->name[pl], 32);
                for (int k = s->nhold; k-- > 0; )                    /* and "waiting for him" holds end */
                    if (s->hold[k].reason == DSNET_HOLD_PLAYER_LEFT && !memcmp(s->hold[k].uid, cmd->payload, 16)) s->hold[k] = s->hold[--s->nhold];
            }
        }
        break;
    case DSNET_PLAYER_LEAVE:
        if (pl < DSNET_MAX_PLAYERS) {
            s->present &= ~(1u << pl); s->speed[pl] = 1; s->pause[pl] = 0; s->story[pl] = 0; s->name[pl][0] = 0; clear_ready(s, pl);
            int reg = reg_of_slot(s, pl);
            if (reg >= 0) ctrl_drop_reg(s, reg);                    /* units lent to him go back to their owners */
            if (reg >= 0) s->known[reg].slot = SLOT_ABSENT;         /* his units and items: reserved for him */
        }
        break;
    case DSNET_STORY_PAUSE:
        if (pl < DSNET_MAX_PLAYERS && cmd->len >= 1) s->story[pl] = cmd->payload[0] ? 1 : 0;
        break;
    case DSNET_PAUSE_ALL: {          /* votes already pausing keep their place in the order */
        uint8_t cause = cmd->len >= 1 ? cmd->payload[0] : DSNET_NO_PLAYER;
        s->pause_counter++;
        for (int i = 0; i < DSNET_MAX_PLAYERS; i++) if ((s->present >> i & 1) && !s->pause[i]) {
            s->pause[i] = 1; s->pause_order[i] = s->pause_counter; s->pause_src[i] = cause;
        }
        break;
    }
    case DSNET_SPEED_VOTE:
        if (pl < DSNET_MAX_PLAYERS && cmd->len >= 1 && cmd->payload[0] >= 1) s->speed[pl] = cmd->payload[0];
        break;
    case DSNET_PAUSE_VOTE:
        if (pl < DSNET_MAX_PLAYERS && cmd->len >= 1) {
            if (cmd->payload[0] && !s->pause[pl]) { s->pause_order[pl] = ++s->pause_counter; s->pause_src[pl] = pl; }
            s->pause[pl] = cmd->payload[0] ? 1 : 0;
        }
        break;
    case DSNET_OWNERSHIP:            /* host may assign anyone; others only claim free / release own */
        if (cmd->len >= 17 && cmd->payload[16] != DSNET_NO_PLAYER && unit_owner(s, cmd->payload) != cmd->payload[16]
            && owned_count(s, cmd->payload[16]) >= UNITS_RULE(s)) {          /* units-per-player rule: no new unit */
            if (pl < DSNET_MAX_PLAYERS) s->refused[pl]++;
            break;
        }
        if (cmd->len >= 17) {
            int before = unit_owner(s, cmd->payload);
            tag_cmd(s, s->own, &s->nown, DSNET_MAX_UNITS, pl, cmd->payload, cmd->payload[16]);
            if (unit_owner(s, cmd->payload) != before) tag_set(s->ctrl, &s->nctrl, DSNET_MAX_UNITS, cmd->payload, DSNET_NO_PLAYER);   /* new owner: not lent */
            fix_marks(s, cmd->payload, unit_owner(s, cmd->payload));
        }
        break;
    case DSNET_HOLD: {
        if (pl != 0 || cmd->len < 18) break;
        int k = 0;
        while (k < s->nhold && !(s->hold[k].reason == cmd->payload[1] && !memcmp(s->hold[k].uid, cmd->payload + 2, 16))) k++;
        if (cmd->payload[0]) {
            if (k == s->nhold && s->nhold < DSNET_MAX_HOLDS) { s->hold[k].reason = cmd->payload[1]; memcpy(s->hold[k].uid, cmd->payload + 2, 16); s->nhold++; }
        } else if (k < s->nhold) s->hold[k] = s->hold[--s->nhold];
        break;
    }
    case DSNET_TEMP_CONTROL: {
        if (pl != 0 || cmd->len < 17) break;
        int reg = reg_of_uid(s, cmd->payload, 0);
        if (reg >= 0) s->known[reg].temp = cmd->payload[16] && s->known[reg].slot == SLOT_ABSENT;
        break;
    }
    case DSNET_ADMIN_MODE:
        if (pl == 0 && cmd->len >= 1) s->admin = cmd->payload[0] != 0;
        break;
    case DSNET_LEND: {
        if (cmd->len < 17 || pl >= DSNET_MAX_PLAYERS) break;
        const uint8_t *g = cmd->payload; int to = cmd->payload[16];
        int owner = unit_owner(s, g), lender = owner == -1 ? 0 : owner;   /* unclaimed units: the host lends them */
        int cur = reg_view(s, tag_reg(s->ctrl, s->nctrl, g));            /* current borrower (slot) or -1 */
        if (to == DSNET_NO_PLAYER || (to == lender && pl == lender)) {   /* take back (lender) or give back (borrower) */
            if (cur >= 0 && (pl == lender || pl == cur)) {
                tag_set(s->ctrl, &s->nctrl, DSNET_MAX_UNITS, g, DSNET_NO_PLAYER);
                lend_event(s, g, owner, cur, lender, pl == cur && pl != lender ? DSNET_LEND_GIVEN_BACK : DSNET_LEND_TAKEN_BACK);
            }
        } else if (pl == lender && owner != DSNET_OWNER_RESERVED && to != lender && to < DSNET_MAX_PLAYERS && (s->present >> to & 1)) {
            tag_set(s->ctrl, &s->nctrl, DSNET_MAX_UNITS, g, reg_of_slot(s, to));
            lend_event(s, g, owner, cur >= 0 ? cur : lender, to, DSNET_LEND_LENT);
        }
        break;
    }
    case DSNET_RULES:
        if (pl == 0 && cmd->len >= 4 && rules_ok(cmd->payload)) {
            memcpy(s->rules, cmd->payload, 4);
            if (cmd->len >= 5 && cmd->payload[4] <= DSNET_EVENT_MAJORITY) s->rules[4] = cmd->payload[4];
            if (cmd->len >= 6 && cmd->payload[5] <= DSNET_POP_MULTIPLIED) s->rules[5] = cmd->payload[5];
        }
        break;
    case DSNET_ITEM_OWNER:
        if (cmd->len >= 17) tag_cmd(s, s->item, &s->nitem, DSNET_MAX_ITEMS, pl, cmd->payload, cmd->payload[16]);
        break;
    case DSNET_OWNERS_LOAD: {        /* host restores owners saved with the world: u8 kind, u16 count, entries */
        if (pl != 0 || cmd->len < 3) break;
        int kind = cmd->payload[0], cnt = get16(cmd->payload + 1);
        const uint8_t *e = cmd->payload + 3;
        int esz = kind == 0 ? 48 : kind == 3 ? 17 : 32;
        if (kind > 3 || cmd->len < 3 + cnt * esz) break;
        for (int k = 0; k < cnt; k++, e += esz) {
            if (kind == 3) {                                       /* saved colour wins; a clash moves the other one */
                int rg = reg_of_uid(s, e, 1);
                if (rg < 0) continue;
                s->known[rg].color = e[16] % DSNET_COLORS;
                for (int j = 0; j < s->nknown; j++)
                    if (j != rg && s->known[j].color == s->known[rg].color) s->known[j].color = free_color(s, j);
            } else if (kind == 0) {
                int rg = reg_of_uid(s, e, 1);
                if (rg >= 0 && s->known[rg].slot == SLOT_ABSENT) { memcpy(s->known[rg].name, e + 16, 32); s->known[rg].name[31] = 0; }
            } else {
                int rg = reg_of_uid(s, e + 16, 1);
                if (kind == 1) { tag_set(s->own, &s->nown, DSNET_MAX_UNITS, e, rg); fix_marks(s, e, unit_owner(s, e)); tag_set(s->ctrl, &s->nctrl, DSNET_MAX_UNITS, e, DSNET_NO_PLAYER); }
                else tag_set(s->item, &s->nitem, DSNET_MAX_ITEMS, e, rg);
            }
        }
        break;
    }
    case DSNET_EXPEDITION_READY: {
        if (pl >= DSNET_MAX_PLAYERS || !(s->present >> pl & 1) || cmd->len < 2) break;
        int ready = cmd->payload[0] != 0, n = cmd->payload[1];
        if (n > DSNET_PARTY_MAX || n > UNITS_RULE(s) || cmd->len < 2 + 16 * n) break;   /* ready with 0 units: a viewer approves */
        const uint8_t *g = cmd->payload + 2;
        int okp = 1;
        for (int k = 0; k < n && okp; k++) {
            if (unit_owner(s, g + 16 * k) != pl) okp = 0;
            for (int j = 0; j < k; j++) if (!memcmp(g + 16 * j, g + 16 * k, 16)) okp = 0;
        }
        if (!okp) break;
        s->ready[pl] = ready; s->party_n[pl] = n;
        memcpy(s->party[pl], g, 16 * n);
        break;
    }
    }
}

/* chat system lines from the join / leave commands (lock held): "joined", "reconnected" (a known uid), "left" */
static void chat_system(dsnet_client *c, const dsnet_cmd *cmd) {
    if (cmd->player >= DSNET_MAX_PLAYERS) return;
    dsnet_chat_msg *m = &c->chat[c->chat_tail % 128];
    m->player = cmd->player; m->text[0] = 0;
    if (cmd->type == DSNET_PLAYER_JOIN) {
        int nl = cmd->len > 16 ? cmd->len - 16 : 0; if (nl > 31) nl = 31;
        memcpy(m->name, cmd->payload + 16, nl); m->name[nl] = 0;
        m->kind = cmd->len >= 16 && reg_of_uid(&c->sim, cmd->payload, 0) >= 0 ? DSNET_CHAT_RECONNECTED : DSNET_CHAT_JOINED;
    } else { memcpy(m->name, c->sim.name[cmd->player], 31); m->name[31] = 0; m->kind = DSNET_CHAT_LEFT; }
    c->chat_tail++;
    if (c->chat_tail - c->chat_head > 128) c->chat_head = c->chat_tail - 128;
}
int dsnet_chat_send(dsnet_client *c, const char *text) {
    size_t n = text ? strlen(text) : 0;
    if (!n || !c->welcomed) return -1;
    if (n > DSNET_CHAT_MAX) { n = DSNET_CHAT_MAX; while (n && ((unsigned char)text[n] & 0xC0) == 0x80) n--; }   /* not inside a UTF-8 char */
    uint8_t b[1 + DSNET_CHAT_MAX]; b[0] = P_CHAT; memcpy(b + 1, text, n);
    return dsnet_link_send(&c->link, b, 1 + n) ? -1 : 0;
}
int dsnet_chat_typing(dsnet_client *c, int on) {
    if (!c || !c->welcomed) return -1;
    double now = dsnet_now_ms();
    if (on && c->typing_on && now - c->typing_sent < 1000) return 0;      /* at most once a second */
    if (!on && !c->typing_on) return 0;
    c->typing_on = on != 0; if (on) c->typing_sent = now;
    uint8_t b[2] = { P_TYPING, (uint8_t)(on != 0) };
    return dsnet_link_send(&c->link, b, 2) ? -1 : 0;
}
int dsnet_set_world(dsnet_client *c, const uint8_t id[16], const char *name) {
    if (!c || !c->is_host) return -1;
    uint8_t b[49]; b[0] = P_WORLD; memcpy(b + 1, id, 16); memset(b + 17, 0, 32);
    if (name) memcpy(b + 17, name, strnlen(name, 31));
    pthread_mutex_lock(&c->mu); c->has_world = 1; memcpy(c->world_id, id, 16); memcpy(c->world_name, b + 17, 32); pthread_mutex_unlock(&c->mu);
    return dsnet_link_send(&c->link, b, 49) ? -1 : 0;
}
int dsnet_host_world(dsnet_client *c, uint8_t id[16], char name[32]) {
    pthread_mutex_lock(&c->mu);
    int r = c->has_world ? 0 : -1;
    if (!r) { if (id) memcpy(id, c->world_id, 16); if (name) memcpy(name, c->world_name, 32); }
    pthread_mutex_unlock(&c->mu);
    return r;
}
int dsnet_view_send(dsnet_client *c, uint32_t map_key, float cx, float cy, float half_w, float half_h) {
    if (!c || !c->welcomed) return -1;
    uint8_t b[21], *p = b; float v[4] = { cx, cy, half_w, half_h };
    p = put8(p, P_VIEW); p = put32(p, map_key);
    for (int i = 0; i < 4; i++) { uint32_t u; memcpy(&u, &v[i], 4); p = put32(p, u); }
    return dsnet_link_send(&c->link, b, 21) ? -1 : 0;
}
int dsnet_view_of(dsnet_client *c, int player, uint32_t *map_key, float out[4], double *age_ms) {
    if (!c || player < 0 || player >= DSNET_MAX_PLAYERS) return -1;
    pthread_mutex_lock(&c->mu);
    int r = c->view[player].set ? 0 : -1;
    if (!r) {
        if (map_key) *map_key = c->view[player].map;
        if (out) memcpy(out, c->view[player].r, sizeof c->view[player].r);
        if (age_ms) *age_ms = dsnet_now_ms() - c->view[player].at;
    }
    pthread_mutex_unlock(&c->mu);
    return r;
}
int dsnet_mark_send(dsnet_client *c, int op, int owner, int id, uint32_t map_key, int x, int y) {
    if (!c || !c->welcomed || (op != DSNET_MARK_ADD && op != DSNET_MARK_DEL)) return -1;
    uint8_t b[12], *p = b;
    p = put8(p, P_MARK); p = put8(p, (uint8_t)op); p = put8(p, (uint8_t)owner); p = put8(p, (uint8_t)id);
    p = put32(p, map_key); p = put16(p, (uint16_t)(int16_t)x); p = put16(p, (uint16_t)(int16_t)y);
    return dsnet_link_send(&c->link, b, 12) ? -1 : 0;
}
int dsnet_mark_poll(dsnet_client *c, dsnet_mark_ev *out) {
    pthread_mutex_lock(&c->mu);
    int ok = c->mark_head != c->mark_tail;
    if (ok) *out = c->marks[c->mark_head++ % 64];
    pthread_mutex_unlock(&c->mu);
    return ok;
}
int dsnet_chat_typing_of(dsnet_client *c, int player) {
    if (!c || player < 0 || player >= DSNET_MAX_PLAYERS) return 0;
    pthread_mutex_lock(&c->mu);
    int t = c->typing_until[player] > dsnet_now_ms();
    pthread_mutex_unlock(&c->mu);
    return t;
}
int dsnet_chat_poll(dsnet_client *c, dsnet_chat_msg *out) {
    pthread_mutex_lock(&c->mu);
    int ok = c->chat_head != c->chat_tail;
    if (ok) *out = c->chat[c->chat_head++ % 128];
    pthread_mutex_unlock(&c->mu);
    return ok;
}
int dsnet_take_cmds_for_tick(dsnet_client *c, uint32_t timer, dsnet_cmd_cb cb, void *user) {
    int n = 0;
    for (;;) {
        pthread_mutex_lock(&c->mu);
        c->cur_tick = timer;
        if (c->qh == c->qn || c->q[c->qh].c.tick > timer) {
            c->timer = timer + 1;
            if (c->catching_up && c->tc_seen && c->timer + 5 >= c->tick_until) {
                c->catching_up = 0;
                send_simple(c, P_READY, c->timer);
            }
            pthread_mutex_unlock(&c->mu);
            break;
        }
        qcmd e = c->q[c->qh++];
        if (e.c.tick < timer) dsnet_logf("client %d: command seq %u for tick %u run late at %u", c->player, e.c.seq, e.c.tick, timer);
        if (e.c.type == DSNET_PLAYER_JOIN || e.c.type == DSNET_PLAYER_LEAVE) chat_system(c, &e.c);   /* before: knows "rejoin" */
        if ((e.c.type == DSNET_PLAYER_JOIN || e.c.type == DSNET_PLAYER_LEAVE) && e.c.player < DSNET_MAX_PLAYERS) c->view[e.c.player].set = 0;   /* his view is gone */
        sim_apply(&c->sim, &e.c);
        int start_len = 0;
        uint8_t *start = NULL;
        if (e.c.type == DSNET_EXPEDITION_READY || e.c.type == DSNET_PLAYER_LEAVE) {
            start = malloc(1 + DSNET_MAX_PLAYERS * (2 + DSNET_PARTY_MAX * 16));
            start_len = build_start(&c->sim, start);
        }
        c->cur_seq = e.c.seq;
        c->in_join_point = e.c.type == DSNET_JOIN_POINT;
        c->in_patch_point = e.c.type == DSNET_PATCH_POINT && c->is_host;
        int stop = 0;                 /* our own PATCH_POINT: the rest of this step waits for the patch */
        if (e.c.type == DSNET_PATCH_POINT && !c->is_host && c->patch == 1 && e.c.len >= 4 && c->player >= 0 &&
            c->player < 32 && (get32(e.c.payload) >> c->player & 1)) {
            c->patch = 2; c->patch_tick = timer; stop = 1;
            if (c->has_patch && c->patch_data_tick != timer) { free(c->patch_data); c->patch_data = NULL; c->has_patch = 0; }
        }
        c->exec_player = e.c.player;
        pthread_mutex_unlock(&c->mu);
        if (cb && e.c.type != DSNET_FINGERPRINT) { cb(&e.c, user); n++; }
        if (start_len) {
            dsnet_cmd sc = { DSNET_EXPEDITION_START, e.c.tick, DSNET_NO_PLAYER, e.c.scope, start_len, start, e.c.seq };
            c->exec_player = DSNET_NO_PLAYER;
            if (cb) { cb(&sc, user); n++; }
        }
        free(start);
        c->in_join_point = 0; c->in_patch_point = 0;
        c->exec_player = -1;
        free(e.data);
        if (stop) break;
    }
    return n;
}

/* Vote pause under the lobby's pause mode; *by = who caused it (see dsnet_pause_by). Lock held. */
static int vote_paused(dsnet_client *c, int *by) {
    int any = 0, all = 1, n = 0, best = -1;
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) if (c->sim.present >> i & 1) {
        n++;
        if (c->sim.pause[i]) {
            any = 1;
            if (best < 0 || c->sim.pause_order[i] < c->sim.pause_order[best]) best = i;
        } else all = 0;
    }
    int pm = c->sim.rules[0];
    int paused = pm == DSNET_PAUSE_ANYONE ? any : pm == DSNET_PAUSE_ALL_VOTE ? n && all : 0;
    if (by) *by = paused && best >= 0 ? (c->sim.pause_src[best] == DSNET_NO_PLAYER ? best : c->sim.pause_src[best]) : -1;
    return paused;
}

int dsnet_ticks_per_step(dsnet_client *c) {
    pthread_mutex_lock(&c->mu);
    int speed = 0, paused = vote_paused(c, NULL) || c->sim.nhold;   /* holds pause in every mode */
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) if (c->sim.present >> i & 1) {
        if (c->sim.story[i]) paused = 1;
        if (!speed || c->sim.speed[i] < speed) speed = c->sim.speed[i];
    }
    if (!c->sim.rules[1] && speed > 1) speed = 1;                    /* speed-up turned off: votes stay, 1x */
    pthread_mutex_unlock(&c->mu);
    return paused ? 0 : speed ? speed : 1;
}

int dsnet_pause_by(dsnet_client *c) {
    pthread_mutex_lock(&c->mu);
    int by; vote_paused(c, &by);
    pthread_mutex_unlock(&c->mu);
    return by;
}

int dsnet_steps_this_frame(dsnet_client *c, float frame_ms) {
    pthread_mutex_lock(&c->mu);
    int behind = (int)(c->tick_until - c->timer), n = 0;
    if (!c->welcomed || c->needs_state || (c->desynced && c->patch != 1) || c->patch == 2 || c->has_pending) n = 0;
    else if (c->catching_up) n = behind < 0 ? 0 : behind > 200 ? 200 : behind;
    else {
        /* Several steps per frame, so a low-fps peer keeps real speed (TickPatch replay path):
         * accumulate real time, run a step per step time, keep the remainder. A long stall counts
         * as at most 1 s; the carried backlog is capped at 4 steps; never more than tickUntil allows. */
        c->real_time += frame_ms > 1000 ? 1000 : frame_ms < 0 ? 0 : frame_ms;
        /* keep a 3-7 step buffer: slower when short, faster when long (TickPatch.cs:73-76) */
        float stpt = c->tpt_ms * (behind <= 3 ? 1.2f : behind >= 7 ? 0.8f : 1.0f);
        if (stpt < 1) stpt = 1;
        while (n < behind && c->real_time >= stpt) { n++; c->real_time -= stpt; }
        if (c->real_time > 4 * stpt) c->real_time = 4 * stpt;
    }
    pthread_mutex_unlock(&c->mu);
    return n;
}

void dsnet_report_frame_time(dsnet_client *c, float ms) {
    pthread_mutex_lock(&c->mu);
    c->avg_frame = c->avg_frame > 0 ? (c->avg_frame + ms) / 2 : ms;
    pthread_mutex_unlock(&c->mu);
}

/* order-independent hash of an owner table (owner by stable uid; table order after swap-removes does not matter) */
static uint64_t tag_hash(const simstate *s, const tag *t, int n) {
    uint64_t h = 0;
    for (int i = 0; i < n; i++) {
        uint64_t x = 0xCBF29CE484222325ULL;
        for (int k = 0; k < 16; k++) { x ^= t[i].guid[k]; x *= 0x100000001B3ULL; }
        const uint8_t *u = s->known[t[i].owner].uid;
        for (int k = 0; k < 16; k++) { x ^= u[k]; x *= 0x100000001B3ULL; }
        h += x;
    }
    return h;
}
static uint64_t owners_hash(const simstate *s) {
    return tag_hash(s, s->own, s->nown) * 31 + tag_hash(s, s->item, s->nitem) + (uint64_t)s->nown * 1000003u + s->nitem
         + tag_hash(s, s->ctrl, s->nctrl) * 17 + (uint64_t)s->nctrl * 7919u;
}

void dsnet_owner_hashes(dsnet_client *c, uint64_t *units, uint64_t *items, int *nunits, int *nitems) {
    pthread_mutex_lock(&c->mu);
    if (units) *units = tag_hash(&c->sim, c->sim.own, c->sim.nown);
    if (items) *items = tag_hash(&c->sim, c->sim.item, c->sim.nitem);
    if (nunits) *nunits = c->sim.nown;
    if (nitems) *nitems = c->sim.nitem;
    pthread_mutex_unlock(&c->mu);
}

int dsnet_desync_reason(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->desynced ? c->desync_reason : 0; pthread_mutex_unlock(&c->mu); return r; }

void dsnet_report_fingerprint_deep(dsnet_client *c, uint32_t tick, uint64_t hash, const uint64_t sec[3], uint64_t deep) {
    pthread_mutex_lock(&c->mu);
    if (c->fp_interval && tick % c->fp_interval == 0) {
        uint64_t own = owners_hash(&c->sim);
        if (c->is_host) {
            uint8_t b[53], *p = b; p = put8(p, P_FP); p = put32(p, tick); p = put64(p, hash); p = put64(p, own);
            for (int i = 0; i < 3; i++) p = put64(p, sec ? sec[i] : 0);
            p = put64(p, deep);
            dsnet_link_send(&c->link, b, p - b);
        } else {
            fp *m = &c->mine[(tick / c->fp_interval) % FP_RING];
            *m = (fp){ tick, hash, own, 1, { 0, 0, 0 }, deep };
            if (sec) memcpy(m->sec, sec, sizeof m->sec);
            fp_compare(c, tick);
        }
    }
    pthread_mutex_unlock(&c->mu);
}
void dsnet_report_fingerprint_ex(dsnet_client *c, uint32_t tick, uint64_t hash, const uint64_t sec[3]) { dsnet_report_fingerprint_deep(c, tick, hash, sec, 0); }
void dsnet_report_fingerprint(dsnet_client *c, uint32_t tick, uint64_t hash) { dsnet_report_fingerprint_ex(c, tick, hash, NULL); }
void dsnet_deep_status(dsnet_client *c, uint32_t *checks, uint32_t *fails, uint32_t *last_ok, uint32_t *last_bad) {
    pthread_mutex_lock(&c->mu);
    if (checks) *checks = c->deep_checks;
    if (fails) *fails = c->deep_fails;
    if (last_ok) *last_ok = c->deep_last_ok;
    if (last_bad) *last_bad = c->deep_last_bad;
    pthread_mutex_unlock(&c->mu);
}
int dsnet_force_desync(dsnet_client *c, uint32_t tick, int partial) {
    pthread_mutex_lock(&c->mu);
    int ok = !c->desynced && !c->is_host && !c->needs_state;
    if (ok) {
        c->desynced = 1; c->bad_tick = tick; c->desync_reason = DSNET_DESYNC_GAME;
        memset(&c->dinfo, 0, sizeof c->dinfo); c->dinfo.bad_tick = tick; c->dinfo.last_good = c->last_good;
        fp *m = c->fp_interval ? &c->mine[(tick / c->fp_interval) % FP_RING] : NULL, *h = c->fp_interval ? &c->host[(tick / c->fp_interval) % FP_RING] : NULL;
        if (m && h && m->tick == tick && h->tick == tick) {     /* the report shows both sides (the fingerprint was equal) */
            uint64_t mi[5] = { m->hash, m->sec[0], m->sec[1], m->sec[2], m->owners }, ho[5] = { h->hash, h->sec[0], h->sec[1], h->sec[2], h->owners };
            memcpy(c->dinfo.mine, mi, sizeof mi); memcpy(c->dinfo.host, ho, sizeof ho);
            c->dinfo.deep_mine = m->deep; c->dinfo.deep_host = h->deep;
        }
        uint8_t b[6] = { P_DESYNC }; put32(b + 1, tick); b[5] = partial && c->partial_ok;   /* 0: the server pauses, full resync */
        dsnet_link_send(&c->link, b, 6);
        dsnet_logf("client %d: desync forced by the game at tick %u", c->player, tick);
    }
    pthread_mutex_unlock(&c->mu);
    return ok ? 0 : -1;
}
uint32_t dsnet_fp_interval(dsnet_client *c) { pthread_mutex_lock(&c->mu); uint32_t r = c->fp_interval; pthread_mutex_unlock(&c->mu); return r; }
int dsnet_desync_get(dsnet_client *c, dsnet_desync_info *out) {
    pthread_mutex_lock(&c->mu);
    int r = c->desynced ? 0 : -1;
    if (!r && out) *out = c->dinfo;
    pthread_mutex_unlock(&c->mu);
    return r;
}

int dsnet_player_id(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->welcomed ? c->player : -1; pthread_mutex_unlock(&c->mu); return r; }
int dsnet_is_host(dsnet_client *c) { return c->is_host; }
uint32_t dsnet_busy_mask(dsnet_client *c) { pthread_mutex_lock(&c->mu); uint32_t r = c->busy_mask; pthread_mutex_unlock(&c->mu); return r; }

void dsnet_set_busy(dsnet_client *c, int busy) {
    pthread_mutex_lock(&c->mu);
    int changed = c->busy != (busy != 0);
    c->busy = busy != 0;
    pthread_mutex_unlock(&c->mu);
    if (changed) { uint8_t b[2] = { P_BUSY, busy != 0 }; dsnet_link_send(&c->link, b, 2); }
}

uint32_t dsnet_story_pause_mask(dsnet_client *c) {
    pthread_mutex_lock(&c->mu);
    uint32_t r = 0;
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) if ((c->sim.present >> i & 1) && c->sim.story[i]) r |= 1u << i;
    pthread_mutex_unlock(&c->mu);
    return r;
}

uint32_t dsnet_present_mask(dsnet_client *c) { pthread_mutex_lock(&c->mu); uint32_t r = c->sim.present; pthread_mutex_unlock(&c->mu); return r; }

int dsnet_vote_of(dsnet_client *c, int player, int *speed, int *pause) {
    if (player < 0 || player >= DSNET_MAX_PLAYERS) return -1;
    pthread_mutex_lock(&c->mu);
    int r = c->sim.present >> player & 1 ? 0 : -1;
    if (!r) { if (speed) *speed = c->sim.speed[player]; if (pause) *pause = c->sim.pause[player]; }
    pthread_mutex_unlock(&c->mu);
    return r;
}

const char *dsnet_player_name(dsnet_client *c, int player) {
    if (player < 0 || player >= DSNET_MAX_PLAYERS) return NULL;
    pthread_mutex_lock(&c->mu);
    const char *r = c->sim.present >> player & 1 ? c->sim.name[player] : NULL;
    pthread_mutex_unlock(&c->mu);
    return r;
}

int dsnet_allow_speedup(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->sim.rules[1]; pthread_mutex_unlock(&c->mu); return r; }
int dsnet_allow_pause(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->sim.rules[0] != DSNET_PAUSE_NONE; pthread_mutex_unlock(&c->mu); return r; }
int dsnet_pause_mode(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->sim.rules[0]; pthread_mutex_unlock(&c->mu); return r; }
void dsnet_rules_get(dsnet_client *c, dsnet_rules *o) {
    pthread_mutex_lock(&c->mu);
    o->pause_mode = c->sim.rules[0]; o->allow_speedup = c->sim.rules[1]; o->gear_mode = c->sim.rules[2]; o->units_per_player = UNITS_RULE(&c->sim);
    o->event_choice = c->sim.rules[4]; o->population = c->sim.rules[5];
    pthread_mutex_unlock(&c->mu);
}
int dsnet_rules_valid(const dsnet_rules *r) {
    if (!r || r->pause_mode < 0 || r->pause_mode > 255 || r->gear_mode < 0 || r->gear_mode > 255 || r->units_per_player < 0 || r->units_per_player > 255) return 0;
    uint8_t b[4] = { (uint8_t)r->pause_mode, r->allow_speedup != 0, (uint8_t)r->gear_mode, (uint8_t)r->units_per_player };
    return rules_ok(b) && r->event_choice >= 0 && r->event_choice <= DSNET_EVENT_MAJORITY && r->population >= 0 && r->population <= DSNET_POP_MULTIPLIED;
}
int dsnet_set_rules(dsnet_client *c, const dsnet_rules *r) {
    if (!dsnet_rules_valid(r)) return -1;
    uint8_t b[6] = { (uint8_t)r->pause_mode, r->allow_speedup != 0, (uint8_t)r->gear_mode, (uint8_t)r->units_per_player, (uint8_t)r->event_choice, (uint8_t)r->population };
    return dsnet_submit(c, DSNET_RULES, 0, b, 6);
}
int dsnet_population_cap(int mode, int base_cap, int nplayers, int rank, int *pool) {
    if (base_cap < 0) base_cap = 0;
    if (nplayers < 1) nplayers = 1;
    if (rank < 0) rank = 0;
    int cap, pl;
    switch (mode) {
    case DSNET_POP_PER_PLAYER: cap = base_cap; pl = 0; break;
    case DSNET_POP_SPLIT:      cap = base_cap / nplayers + (rank < base_cap % nplayers ? 1 : 0); pl = 0; break;
    case DSNET_POP_MULTIPLIED: cap = base_cap * nplayers; pl = 1; break;
    default:                   cap = base_cap; pl = 1; break;     /* SHARED (stock) */
    }
    if (pool) *pool = pl;
    return cap;
}
int dsnet_may_recruit(dsnet_client *c, int player, int base_cap, int clan_count, int own_count, int *cap, int *pool) {
    return dsnet_recruit_check(c, player, base_cap, clan_count, own_count, cap, pool) == DSNET_RECRUIT_OK;
}
int dsnet_recruit_check(dsnet_client *c, int player, int base_cap, int clan_count, int own_count, int *cap, int *pool) {
    pthread_mutex_lock(&c->mu);
    int n = 0, rank = 0, mode = c->sim.rules[5], units = UNITS_RULE(&c->sim);
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) if (c->sim.present >> i & 1) { if (i < player) rank++; n++; }
    pthread_mutex_unlock(&c->mu);
    int pl = 1, cp = dsnet_population_cap(mode, base_cap, n, rank, &pl);
    if (cap) *cap = cp;
    if (pool) *pool = pl;
    if ((pl ? clan_count : own_count) >= cp) return DSNET_RECRUIT_POP_FULL;   /* overflow stays: only no new hire */
    if (own_count >= units) return DSNET_RECRUIT_UNITS_FULL;
    return DSNET_RECRUIT_OK;
}
int dsnet_lend(dsnet_client *c, const uint8_t unit[16], int to_slot) {
    uint8_t b[17]; memcpy(b, unit, 16); b[16] = (uint8_t)to_slot;
    return dsnet_submit(c, DSNET_LEND, 0, b, 17);
}
int dsnet_give_back(dsnet_client *c, const uint8_t unit[16]) { return dsnet_lend(c, unit, DSNET_NO_PLAYER); }
int dsnet_lent_to(dsnet_client *c, const uint8_t unit[16]) {
    pthread_mutex_lock(&c->mu); int r = reg_view(&c->sim, tag_reg(c->sim.ctrl, c->sim.nctrl, unit)); pthread_mutex_unlock(&c->mu);
    return r >= 0 ? r : -1;
}
int dsnet_controller_of(dsnet_client *c, const uint8_t unit[16]) {
    pthread_mutex_lock(&c->mu);
    int r = reg_view(&c->sim, tag_reg(c->sim.ctrl, c->sim.nctrl, unit));
    if (r < 0) r = unit_owner(&c->sim, unit);
    pthread_mutex_unlock(&c->mu);
    return r;
}
uint32_t dsnet_lend_seq(dsnet_client *c) { pthread_mutex_lock(&c->mu); uint32_t r = c->sim.lend_seq; pthread_mutex_unlock(&c->mu); return r; }
int dsnet_lend_last(dsnet_client *c, uint8_t unit[16], int *owner, int *from, int *to, int *why) {
    pthread_mutex_lock(&c->mu);
    int ok = c->sim.lend_seq ? 0 : -1;
    if (!ok) {
        if (unit) memcpy(unit, c->sim.lend_last.unit, 16);
        if (owner) *owner = c->sim.lend_last.owner; if (from) *from = c->sim.lend_last.from;
        if (to) *to = c->sim.lend_last.to; if (why) *why = c->sim.lend_last.why;
    }
    pthread_mutex_unlock(&c->mu);
    return ok;
}
int dsnet_lent_count(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->sim.nctrl; pthread_mutex_unlock(&c->mu); return r; }
int dsnet_units_per_player(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = UNITS_RULE(&c->sim); pthread_mutex_unlock(&c->mu); return r; }
int dsnet_owned_count(dsnet_client *c, int player) {
    if (player < 0 || player >= DSNET_MAX_PLAYERS) return 0;
    pthread_mutex_lock(&c->mu); int r = owned_count(&c->sim, player); pthread_mutex_unlock(&c->mu); return r;
}
uint32_t dsnet_claim_refusals(dsnet_client *c, int player) {
    if (player < 0 || player >= DSNET_MAX_PLAYERS) return 0;
    pthread_mutex_lock(&c->mu); uint32_t r = c->sim.refused[player]; pthread_mutex_unlock(&c->mu); return r;
}
uint32_t dsnet_tick_until(dsnet_client *c) { pthread_mutex_lock(&c->mu); uint32_t r = c->tick_until; pthread_mutex_unlock(&c->mu); return r; }
uint32_t dsnet_timer(dsnet_client *c) { pthread_mutex_lock(&c->mu); uint32_t r = c->timer; pthread_mutex_unlock(&c->mu); return r; }
int dsnet_needs_state(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->needs_state || c->has_pending; pthread_mutex_unlock(&c->mu); return r; }

int dsnet_is_desynced(dsnet_client *c, uint32_t *bad_tick, uint32_t *last_good_tick) {
    pthread_mutex_lock(&c->mu);
    int r = c->desynced;
    if (bad_tick) *bad_tick = c->bad_tick;
    if (last_good_tick) *last_good_tick = c->last_good;
    pthread_mutex_unlock(&c->mu);
    return r;
}

int dsnet_owner_of(dsnet_client *c, const uint8_t guid[16]) {
    pthread_mutex_lock(&c->mu);
    int r = unit_owner(&c->sim, guid);
    pthread_mutex_unlock(&c->mu);
    return r;
}

int dsnet_player_color(dsnet_client *c, int player) {
    pthread_mutex_lock(&c->mu);
    int reg = player >= 0 && player < DSNET_MAX_PLAYERS ? reg_of_slot(&c->sim, player) : -1, r = reg >= 0 ? c->sim.known[reg].color : -1;
    pthread_mutex_unlock(&c->mu);
    return r;
}
int dsnet_owner_info(dsnet_client *c, const uint8_t guid[16], uint8_t uid[16], char name[32]) {
    pthread_mutex_lock(&c->mu);
    int reg = tag_reg(c->sim.own, c->sim.nown, guid), r = reg_view(&c->sim, reg);
    if (reg >= 0) { if (uid) memcpy(uid, c->sim.known[reg].uid, 16); if (name) memcpy(name, c->sim.known[reg].name, 32); }
    pthread_mutex_unlock(&c->mu);
    return r;
}

int dsnet_player_uid(dsnet_client *c, int player, uint8_t uid[16]) {
    pthread_mutex_lock(&c->mu);
    int reg = player >= 0 && player < DSNET_MAX_PLAYERS ? reg_of_slot(&c->sim, player) : -1;
    if (reg >= 0 && uid) memcpy(uid, c->sim.known[reg].uid, 16);
    pthread_mutex_unlock(&c->mu);
    return reg >= 0 ? 0 : -1;
}

size_t dsnet_export_owners(dsnet_client *c, uint8_t *buf, size_t cap) {
    pthread_mutex_lock(&c->mu);
    simstate *s = &c->sim;
    size_t need = 3 + s->nknown * 48 + 3 + s->nown * 32 + 3 + s->nitem * 32 + 3 + s->nknown * 17;
    if (buf && cap >= need) {
        uint8_t *p = buf;
        *p++ = 0; p = put16(p, s->nknown);
        for (int i = 0; i < s->nknown; i++) { memcpy(p, s->known[i].uid, 16); memcpy(p + 16, s->known[i].name, 32); p += 48; }
        *p++ = 1; p = put16(p, s->nown);
        for (int i = 0; i < s->nown; i++) { memcpy(p, s->own[i].guid, 16); memcpy(p + 16, s->known[s->own[i].owner].uid, 16); p += 32; }
        *p++ = 2; p = put16(p, s->nitem);
        for (int i = 0; i < s->nitem; i++) { memcpy(p, s->item[i].guid, 16); memcpy(p + 16, s->known[s->item[i].owner].uid, 16); p += 32; }
        *p++ = 3; p = put16(p, s->nknown);                  /* player colours (uid + palette index) */
        for (int i = 0; i < s->nknown; i++) { memcpy(p, s->known[i].uid, 16); p[16] = s->known[i].color; p += 17; }
    }
    pthread_mutex_unlock(&c->mu);
    return need;
}

int dsnet_import_owners(dsnet_client *c, const uint8_t *data, size_t len) {
    if (!c->is_host) return -1;
    size_t off = 0;
    while (off + 3 <= len) {                    /* each section split into commands that fit a payload */
        int kind = data[off], cnt = get16(data + off + 1), esz = kind == 0 ? 48 : kind == 3 ? 17 : 32;
        if (kind > 3 || off + 3 + (size_t)cnt * esz > len) return -1;
        const uint8_t *e = data + off + 3;
        int per = (DSNET_MAX_PAYLOAD - 3) / esz;
        for (int k = 0; k < cnt; k += per) {
            int m = cnt - k < per ? cnt - k : per;
            uint8_t *b = malloc(3 + m * esz);
            b[0] = kind; put16(b + 1, m); memcpy(b + 3, e + (size_t)k * esz, (size_t)m * esz);
            int rc = dsnet_submit(c, DSNET_OWNERS_LOAD, 0, b, 3 + m * esz);
            free(b);
            if (rc) return -1;
        }
        off += 3 + (size_t)cnt * esz;
    }
    return off == len ? 0 : -1;
}

int dsnet_request_resync(dsnet_client *c) {
    pthread_mutex_lock(&c->mu);
    c->needs_state = 1;
    pthread_mutex_unlock(&c->mu);
    uint8_t b = P_RESYNC_REQ;
    return dsnet_link_send(&c->link, &b, 1);
}

void dsnet_set_partial_resync(dsnet_client *c, int on) { pthread_mutex_lock(&c->mu); c->partial_ok = on != 0; pthread_mutex_unlock(&c->mu); }
void dsnet_set_patch_cb(dsnet_client *c, dsnet_patch_cb cb, void *user) {
    pthread_mutex_lock(&c->mu); c->patch_cb = cb; c->patch_user = user; pthread_mutex_unlock(&c->mu);
}
int dsnet_request_patch(dsnet_client *c) {
    pthread_mutex_lock(&c->mu);
    int ok = c->desynced && !c->patch && !c->needs_state && !c->is_host;
    if (ok) c->patch = 1;
    pthread_mutex_unlock(&c->mu);
    if (!ok) return -1;
    uint8_t b = P_PATCH_REQ;
    return dsnet_link_send(&c->link, &b, 1);
}
int dsnet_patch_state(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->patch; pthread_mutex_unlock(&c->mu); return r; }
uint32_t dsnet_patch_count(dsnet_client *c) { pthread_mutex_lock(&c->mu); uint32_t r = c->patches; pthread_mutex_unlock(&c->mu); return r; }
void dsnet_patch_abort(dsnet_client *c) {
    pthread_mutex_lock(&c->mu);
    int was = c->patch;
    c->patch = 0; c->has_patch = 0; free(c->patch_data); c->patch_data = NULL;
    pthread_mutex_unlock(&c->mu);
    if (was) { uint8_t b[2] = { P_PATCHED, 0 }; dsnet_link_send(&c->link, b, 2); }
}
int dsnet_upload_patch(dsnet_client *c, const uint8_t *data, size_t len) {
    if (!c->is_host || !c->in_patch_point || len > DSNET_MAX_BLOB - SIM_BYTES) return -1;
    uint8_t *buf = malloc(SIM_BYTES + len);
    if (!buf) return -1;
    pthread_mutex_lock(&c->mu);
    size_t hl = sim_serialize(&c->sim, buf);
    uint32_t tick = c->cur_tick, seq_after = c->cur_seq + 1;
    pthread_mutex_unlock(&c->mu);
    memcpy(buf + hl, data, len);
    static uint32_t patch_id;
    int r = dsnet_link_send_blob(&c->link, 0x40000000u | ++patch_id, BLOB_PATCH, tick, seq_after, buf, (uint32_t)(hl + len));
    free(buf);
    return r;
}
int dsnet_send_report(dsnet_client *c, const uint8_t *data, size_t len) {
    if (c->is_host || len > DSNET_MAX_REPORT) return -1;
    static uint32_t report_id;
    return dsnet_link_send_blob(&c->link, 0x20000000u | ++report_id, BLOB_REPORT, 0, 0, data, (uint32_t)len);
}
int dsnet_send_full(dsnet_client *c, uint32_t tick, const uint8_t *data, size_t len) {
    if (!c->is_host || len > (8u << 20)) return -1;
    uint8_t *b = malloc(5 + len); if (!b) return -1;
    b[0] = P_FULL; put32(b + 1, tick); memcpy(b + 5, data, len);
    int r = dsnet_link_send(&c->link, b, (uint32_t)(5 + len));
    free(b);
    return r;
}
int dsnet_full_take(dsnet_client *c, uint32_t *tick, uint8_t **data, size_t *len) {
    pthread_mutex_lock(&c->mu);
    int r = c->has_full;
    if (r) { *tick = c->full_tick; *data = c->full; *len = c->full_len; c->full = NULL; c->has_full = 0; }
    pthread_mutex_unlock(&c->mu);
    return r;
}
int dsnet_report_take(dsnet_client *c, uint8_t **data, size_t *len, int *from) {
    pthread_mutex_lock(&c->mu);
    int r = c->nreports > 0;
    if (r) {
        *data = c->reports[0].data; *len = c->reports[0].len; if (from) *from = c->reports[0].from;
        memmove(c->reports, c->reports + 1, sizeof c->reports[0] * (size_t)(c->nreports - 1)); c->nreports--;
    }
    pthread_mutex_unlock(&c->mu);
    return r;
}

int dsnet_upload_save(dsnet_client *c, const uint8_t *data, size_t len) {
    if (!c->is_host || !c->in_join_point || len > DSNET_MAX_BLOB) return -1;
    uint8_t *head = malloc(SIM_BYTES);
    if (!head) return -1;
    pthread_mutex_lock(&c->mu);
    size_t hl = sim_serialize(&c->sim, head);
    uint32_t tick = c->cur_tick, seq_after = c->cur_seq + 1;
    pthread_mutex_unlock(&c->mu);
    uint8_t *buf = len + hl <= DSNET_MAX_BLOB ? malloc(len + hl) : NULL;
    if (!buf) { free(head); return -1; }
    memcpy(buf, head, hl); free(head);
    memcpy(buf + hl, data, len);
    static uint32_t blob_id;
    int r = dsnet_link_send_blob(&c->link, ++blob_id, BLOB_SAVE, tick, seq_after, buf, len + hl);
    free(buf);
    return r;
}

/* ---- bench, expedition, recruits, gear ---- */
int dsnet_set_recruit_owner(dsnet_client *c, const uint8_t guid[16]) {
    if (c->exec_player < 0 || c->exec_player >= DSNET_MAX_PLAYERS) return -1;   /* only inside a command callback */
    pthread_mutex_lock(&c->mu);
    unit_set(&c->sim, guid, c->exec_player);
    pthread_mutex_unlock(&c->mu);
    return 0;
}

int dsnet_expedition_ready(dsnet_client *c, int ready, const uint8_t (*guids)[16], int n) {
    if (n < 0 || n > DSNET_PARTY_MAX) return -1;
    uint8_t b[2 + DSNET_PARTY_MAX * 16];
    b[0] = ready != 0; b[1] = n; if (n) memcpy(b + 2, guids, 16 * n);
    return dsnet_submit(c, DSNET_EXPEDITION_READY, 0, b, 2 + 16 * n);
}

int dsnet_marked_of(dsnet_client *c, int player, uint8_t out[3][16]) {
    if (player < 0 || player >= DSNET_MAX_PLAYERS) return -1;
    pthread_mutex_lock(&c->mu);
    int n = c->sim.present >> player & 1 ? c->sim.party_n[player] : -1;
    if (n > 0 && out) memcpy(out, c->sim.party[player], 16 * (n < 3 ? n : 3));   /* old callers hold 3 */
    pthread_mutex_unlock(&c->mu);
    return n;
}
int dsnet_marked_list(dsnet_client *c, int player, uint8_t (*out)[16], int max) {
    if (player < 0 || player >= DSNET_MAX_PLAYERS) return -1;
    pthread_mutex_lock(&c->mu);
    int n = c->sim.present >> player & 1 ? c->sim.party_n[player] : -1;
    if (n > 0 && out && max > 0) memcpy(out, c->sim.party[player], 16 * (n < max ? n : max));
    pthread_mutex_unlock(&c->mu);
    return n;
}

int dsnet_owned_list(dsnet_client *c, int player, uint8_t (*out)[16], int max) {
    int n = 0;
    pthread_mutex_lock(&c->mu);
    for (int i = 0; i < c->sim.nown; i++) if (reg_view(&c->sim, c->sim.own[i].owner) == player) {
        if (out && n < max) memcpy(out[n], c->sim.own[i].guid, 16);
        n++;
    }
    pthread_mutex_unlock(&c->mu);
    return n;
}

uint32_t dsnet_ready_mask(dsnet_client *c) {
    pthread_mutex_lock(&c->mu);
    uint32_t r = 0;
    for (int i = 0; i < DSNET_MAX_PLAYERS; i++) if ((c->sim.present >> i & 1) && c->sim.ready[i]) r |= 1u << i;
    pthread_mutex_unlock(&c->mu);
    return r;
}

int dsnet_party_ok(dsnet_client *c, const uint8_t (*guids)[16], int n) {
    int count[DSNET_MAX_PLAYERS] = { 0 }, ok = 0;
    pthread_mutex_lock(&c->mu);
    for (int k = 0; k < n; k++) {
        int o = unit_owner(&c->sim, guids[k]);
        if (o == DSNET_OWNER_RESERVED) continue;     /* not anyone's here */
        if (o < 0) o = 0;                        /* unclaimed units count for the host */
        if (++count[o] > UNITS_RULE(&c->sim)) ok = -1;
    }
    pthread_mutex_unlock(&c->mu);
    return ok;
}

int dsnet_gear_mode(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->sim.rules[2]; pthread_mutex_unlock(&c->mu); return r; }

int dsnet_may_equip(dsnet_client *c, int player, const uint8_t unit[16]) {
    pthread_mutex_lock(&c->mu);
    int o = unit_owner(&c->sim, unit);
    int adm = player == 0 && c->sim.admin;
    int r = o == DSNET_OWNER_RESERVED ? adm                 /* reserved for an absent player */
          : c->sim.rules[2] == DSNET_GEAR_SHARED_ANY || adm ? 1 : o == player || (o < 0 && player == 0);
    pthread_mutex_unlock(&c->mu);
    return r;
}

int dsnet_item_owner(dsnet_client *c, const uint8_t item[16]) {
    pthread_mutex_lock(&c->mu);
    int r = reg_view(&c->sim, tag_reg(c->sim.item, c->sim.nitem, item));
    pthread_mutex_unlock(&c->mu);
    return r;
}

int dsnet_item_set_owner(dsnet_client *c, const uint8_t item[16], int owner) {
    if (owner < 0) owner = DSNET_NO_PLAYER;
    if (c->exec_player >= 0) {                   /* inside a command: deterministic local update */
        pthread_mutex_lock(&c->mu);
        tag_set(c->sim.item, &c->sim.nitem, DSNET_MAX_ITEMS, item, owner == DSNET_NO_PLAYER ? DSNET_NO_PLAYER : reg_of_slot(&c->sim, owner));
        pthread_mutex_unlock(&c->mu);
        return 0;
    }
    uint8_t b[17]; memcpy(b, item, 16); b[16] = owner;
    return dsnet_submit(c, DSNET_ITEM_OWNER, 0, b, 17);
}

int dsnet_may_use_item(dsnet_client *c, int player, const uint8_t item[16]) {
    if (dsnet_gear_mode(c) != DSNET_GEAR_OWN_STORAGE) return 1;
    int o = dsnet_item_owner(c, item);
    return o == -1 || o == player || (o == DSNET_OWNER_RESERVED && player == 0 && dsnet_admin_mode(c));
}

int dsnet_player_net(dsnet_client *c, int player, int *ping_ms, int *ticks_behind, int *state) {
    if (player < 0 || player >= DSNET_MAX_PLAYERS) { if (state) *state = DSNET_NET_ABSENT; return -1; }
    pthread_mutex_lock(&c->mu);
    int st = c->net[player].state;
    if (ping_ms) *ping_ms = c->net[player].ping;
    if (ticks_behind) *ticks_behind = c->net[player].behind;
    if (state) *state = st;
    pthread_mutex_unlock(&c->mu);
    return st == DSNET_NET_ABSENT ? -1 : 0;
}

int dsnet_is_disconnected(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->disconnected; pthread_mutex_unlock(&c->mu); return r; }
int dsnet_host_left(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->host_left; pthread_mutex_unlock(&c->mu); return r; }
void dsnet_test_cut(dsnet_client *c) { pthread_mutex_lock(&c->mu); c->link.dead = 1; pthread_mutex_unlock(&c->mu); }

size_t dsnet_save_overhead(dsnet_client *c) {
    uint8_t *b = malloc(SIM_BYTES);
    if (!b) return SIM_BYTES;
    pthread_mutex_lock(&c->mu);
    size_t n = sim_serialize(&c->sim, b);
    pthread_mutex_unlock(&c->mu);
    free(b);
    return n;
}

int dsnet_admin_mode(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->sim.admin; pthread_mutex_unlock(&c->mu); return r; }

/* ---- holds and temporary control ---- */
int dsnet_hold(dsnet_client *c, int on, int reason, const uint8_t uid[16]) {
    uint8_t b[18] = { on != 0, (uint8_t)reason };
    if (uid) memcpy(b + 2, uid, 16);
    return dsnet_submit(c, DSNET_HOLD, 0, b, 18);
}
int dsnet_hold_count(dsnet_client *c) { pthread_mutex_lock(&c->mu); int r = c->sim.nhold; pthread_mutex_unlock(&c->mu); return r; }
int dsnet_hold_get(dsnet_client *c, int i, int *reason, uint8_t uid[16], char name[32]) {
    pthread_mutex_lock(&c->mu);
    int ok = i >= 0 && i < c->sim.nhold;
    if (ok) {
        if (reason) *reason = c->sim.hold[i].reason;
        if (uid) memcpy(uid, c->sim.hold[i].uid, 16);
        if (name) {
            int reg = reg_of_uid(&c->sim, c->sim.hold[i].uid, 0);
            if (reg >= 0) memcpy(name, c->sim.known[reg].name, 32); else name[0] = 0;
        }
    }
    pthread_mutex_unlock(&c->mu);
    return ok ? 0 : -1;
}
int dsnet_set_temp_control(dsnet_client *c, const uint8_t uid[16], int on) {
    uint8_t b[17]; memcpy(b, uid, 16); b[16] = on != 0;
    return dsnet_submit(c, DSNET_TEMP_CONTROL, 0, b, 17);
}
int dsnet_temp_control(dsnet_client *c, const uint8_t uid[16]) {
    pthread_mutex_lock(&c->mu);
    int reg = reg_of_uid(&c->sim, uid, 0), r = reg >= 0 && c->sim.known[reg].temp;
    pthread_mutex_unlock(&c->mu);
    return r;
}
int dsnet_may_order(dsnet_client *c, int player, const uint8_t unit[16]) {
    pthread_mutex_lock(&c->mu);
    int reg = tag_reg(c->sim.own, c->sim.nown, unit), o = reg_view(&c->sim, reg);
    int host = player == 0, adm = host && c->sim.admin;
    int r = o == player || adm || (o == -1 && host) || (o == DSNET_OWNER_RESERVED && host && c->sim.known[reg].temp);
    int ctl = reg_view(&c->sim, tag_reg(c->sim.ctrl, c->sim.nctrl, unit));    /* lent: only the borrower (and admin) */
    if (ctl >= 0) r = ctl == player || adm;
    pthread_mutex_unlock(&c->mu);
    return r;
}
