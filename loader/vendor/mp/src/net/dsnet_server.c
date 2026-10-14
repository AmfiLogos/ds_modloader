/* Host-side sequencer (rwmt Common/MultiplayerServer.cs + CommandHandler.cs).
 * Runs on its own thread. Stamps commands with its timer, logs and broadcasts them in order,
 * sends time control at net_ms, holds/slows the timer for peers that fall behind. */
#include "dsnet_internal.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <fenv.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CONNS 64
enum { ST_NEW = 0, ST_PLAYING, ST_LOADING, ST_CATCHUP, ST_DESYNCED };

typedef struct sconn {
    dsnet_link link;
    int local, id, status, is_host, closing, busy, resync, ping_ms;
    int patch_wanted, patching, auto_paused;
    double mark_t[DSNET_MARK_SPAM_N]; int mark_i;   /* times of this sender's last ping adds (spam limit) */   /* lan_v10 partial resync: asked / in the issued PATCH_POINT; desync pause sent */
    double last_rx;
    uint8_t uid[16];
    int ticks_behind; uint32_t behind_at;
    float frame_ms;
    char name[32];
    dsnet_blob_rx rx;
} sconn;

struct dsnet_server {
    dsnet_cfg cfg;
    int listen_fd, wake[2];
    uint16_t port;
    pthread_t th;
    volatile int running;
    pthread_mutex_t mu;                      /* guards pending locals and stats */
    int pending_local[8], npending;
    dsnet_server_stats stats;

    sconn *conn[MAX_CONNS];
    uint32_t timer, sent_snapshot, held;
    int started, jp_pending, max_behind_seen, patch_pending;
    int has_world; uint8_t world[48];          /* host's world: id[16] + name[32] (P_WORLD), in every WELCOME */
    float tpt;
    double last_list, last_ping;
    char build_id[32];
    int round_mode;
    /* command log: serialized P_SCMD frames, index = seq */
    uint8_t *log; size_t log_bytes, log_cap;
    size_t *log_off; uint32_t log_n, log_cap_n;
    /* latest join-point save */
    uint8_t *save; uint32_t save_len, save_tick, save_seq_after, blob_id;
};

static int ext_behind(dsnet_server *s, sconn *c) { return c->ticks_behind + (int)(s->timer - c->behind_at); }
static int live(sconn *c) { return c && c->id >= 0 && c->status != ST_LOADING && c->status != ST_NEW; }

static void send_all_live(dsnet_server *s, const uint8_t *b, uint32_t n, int except_id) {
    for (int i = 0; i < MAX_CONNS; i++)
        if (live(s->conn[i]) && s->conn[i]->id != except_id) dsnet_link_send(&s->conn[i]->link, b, n);
}

static void log_append(dsnet_server *s, const uint8_t *b, uint32_t n) {
    if (s->log_n == s->log_cap_n) {
        s->log_cap_n = s->log_cap_n ? s->log_cap_n * 2 : 1024;
        s->log_off = realloc(s->log_off, s->log_cap_n * sizeof *s->log_off);
    }
    if (s->log_bytes + n + 4 > s->log_cap) {
        while (s->log_bytes + n + 4 > s->log_cap) s->log_cap = s->log_cap ? s->log_cap * 2 : 65536;
        s->log = realloc(s->log, s->log_cap);
    }
    s->log_off[s->log_n++] = s->log_bytes;
    put32(s->log + s->log_bytes, n);
    memcpy(s->log + s->log_bytes + 4, b, n);
    s->log_bytes += n + 4;
}
static void send_log_from(dsnet_server *s, sconn *c, uint32_t seq) {
    for (uint32_t i = seq; i < s->log_n; i++) {
        const uint8_t *e = s->log + s->log_off[i];
        dsnet_link_send(&c->link, e + 4, get32(e));
    }
}

/* CommandHandler.Send: stamp tick + player, log, broadcast to everyone in game. */
static void issue(dsnet_server *s, uint8_t type, uint8_t player, uint8_t scope, const uint8_t *pl, uint16_t len) {
    uint8_t b[16 + DSNET_MAX_PAYLOAD], *p = b;
    p = put8(p, P_SCMD); p = put32(p, s->log_n); p = put8(p, type); p = put32(p, s->timer);
    p = put8(p, player); p = put8(p, scope); p = put16(p, len);
    if (len) memcpy(p, pl, len);
    p += len;
    log_append(s, b, p - b);
    send_all_live(s, b, p - b, -1);
}

static sconn *host_conn(dsnet_server *s) {
    for (int i = 0; i < MAX_CONNS; i++) if (s->conn[i] && s->conn[i]->is_host) return s->conn[i];
    return NULL;
}

static void request_join_point(dsnet_server *s) {
    if (s->jp_pending) return;
    if (!host_conn(s)) { dsnet_logf("server: join point wanted but no host"); return; }
    s->jp_pending = 1;
    issue(s, DSNET_JOIN_POINT, DSNET_NO_PLAYER, 0, NULL, 0);
    dsnet_logf("server: join point at timer %u", s->timer);
}

/* Partial resync (lan_v10): one PATCH_POINT for every player that asked since the last one; the payload names them.
   The host uploads its state from inside that command; the server passes the patch to those players only. */
static void maybe_issue_patch(dsnet_server *s) {
    if (s->patch_pending) return;
    if (!host_conn(s)) return;
    uint32_t mask = 0;
    for (int i = 0; i < MAX_CONNS; i++) {
        sconn *c = s->conn[i];
        if (c && c->id >= 0 && c->id < 32 && c->patch_wanted) { mask |= 1u << c->id; c->patch_wanted = 0; c->patching = 1; }
    }
    if (!mask) return;
    uint8_t pl[4]; put32(pl, mask);
    s->patch_pending = 1;
    issue(s, DSNET_PATCH_POINT, DSNET_NO_PLAYER, 0, pl, 4);
    dsnet_logf("server: patch point at timer %u for players %08x", s->timer, mask);
}

/* nobody waits for the pending patch any more (failed / gave up / left): a later request gets a new PATCH_POINT */
static void patch_settle(dsnet_server *s) {
    for (int i = 0; i < MAX_CONNS; i++) if (s->conn[i] && s->conn[i]->patching) return;
    s->patch_pending = 0;
}

static void send_save_to(dsnet_server *s, sconn *c) {
    dsnet_link_send_blob(&c->link, ++s->blob_id, BLOB_SAVE, s->save_tick, s->save_seq_after, s->save, s->save_len);
    send_log_from(s, c, s->save_seq_after);
    c->status = ST_CATCHUP;
    c->ticks_behind = 0; c->behind_at = s->timer;
    dsnet_logf("server: sent save (%u bytes, tick %u) + %u cmds to player %d",
               s->save_len, s->save_tick, s->log_n - s->save_seq_after, c->id);
}

static void drop_conn(dsnet_server *s, int i) {
    sconn *c = s->conn[i];
    int id = c->id;
    dsnet_logf("server: player %d (%s) left", id, c->name);
    free(c->rx.buf);
    dsnet_link_free(&c->link);
    free(c);
    s->conn[i] = NULL;
    if (id >= 0) issue(s, DSNET_PLAYER_LEAVE, id, 0, NULL, 0);
    patch_settle(s);
}

static int auto_pause(dsnet_server *s) { return s->cfg.pause_mode != DSNET_PAUSE_NONE && !s->cfg.no_auto_pause; }

static void on_hello(dsnet_server *s, sconn *c, const uint8_t *p, uint32_t n) {
    if (c->id >= 0) return;
    /* join checks (rwmt ServerJoiningState): protocol, game build, mod version, float rounding mode */
    int rej = 0, bl = n >= 29 ? p[28] : 0;
    if (n < 29 + (uint32_t)bl || get16(p + 1) != DSNET_PROTO) rej = DSNET_REJ_PROTO;
    else if (bl != (int)strlen(s->build_id) || memcmp(p + 29, s->build_id, bl)) rej = DSNET_REJ_BUILD;
    else if (get32(p + 4) != s->cfg.mod_version) rej = DSNET_REJ_MOD;
    else if ((int)get32(p + 8) != s->round_mode) rej = DSNET_REJ_ROUNDING;
    else for (int i = 0; i < MAX_CONNS; i++)
        if (s->conn[i] && s->conn[i]->id >= 0 && !memcmp(s->conn[i]->uid, p + 12, 16)) rej = DSNET_REJ_DUP_ID;
    if (rej) {
        uint8_t r[2] = { P_REJECT, rej };
        dsnet_link_send(&c->link, r, 2);
        c->closing = 1;
        dsnet_logf("server: rejected a client, reason %d", rej);
        return;
    }
    int want_host = c->local && p[3] && !host_conn(s);
    int used[DSNET_MAX_PLAYERS] = { 0 };
    for (int i = 0; i < MAX_CONNS; i++) if (s->conn[i] && s->conn[i]->id >= 0) used[s->conn[i]->id] = 1;
    int id = -1;
    for (int i = want_host ? 0 : 1; i < DSNET_MAX_PLAYERS; i++) if (!used[i]) { id = i; break; }
    if (id < 0) { uint8_t r[2] = { P_REJECT, DSNET_REJ_FULL }; dsnet_link_send(&c->link, r, 2); c->closing = 1; return; }
    c->id = id; c->is_host = want_host;
    snprintf(c->name, sizeof c->name, "%.*s", (int)(n - 29 - bl), (const char *)p + 29 + bl);
    memcpy(c->uid, p + 12, 16);
    int late = s->started;
    uint8_t b[80], *q = b;
    q = put8(q, P_WELCOME); q = put8(q, id); q = put8(q, c->is_host); q = put8(q, late);
    q = put32(q, s->timer); q = put32(q, 0); q = put32(q, s->cfg.fp_interval);
    q = put8(q, s->cfg.allow_speedup != 0); q = put8(q, s->cfg.pause_mode); q = put8(q, s->cfg.gear_mode); q = put8(q, s->cfg.units_per_player); q = put8(q, s->cfg.event_choice); q = put8(q, s->cfg.population);
    q = put8(q, (uint8_t)s->has_world); memcpy(q, s->world, 48); q += 48;
    dsnet_link_send(&c->link, b, q - b);
    c->ticks_behind = 0; c->behind_at = s->timer;
    if (!late) {                       /* before the start: replay the whole log (all at timer 0) */
        send_log_from(s, c, 0);
        c->status = ST_PLAYING;
    } else c->status = ST_LOADING;
    dsnet_logf("server: player %d (%s) joined%s%s", id, c->name, c->is_host ? " as host" : "", late ? " late" : "");
    uint8_t jp[16 + 32]; size_t nl = strlen(c->name);
    memcpy(jp, c->uid, 16); memcpy(jp + 16, c->name, nl);
    issue(s, DSNET_PLAYER_JOIN, id, 0, jp, 16 + nl);
    if (late) {
        uint8_t cause = id;
        if (auto_pause(s)) issue(s, DSNET_PAUSE_ALL, DSNET_NO_PLAYER, 0, &cause, 1);
        request_join_point(s);
    }
}

static void on_frame(dsnet_server *s, sconn *c, const uint8_t *p, uint32_t n) {
    if ((p[0] != P_HELLO && c->id < 0) || c->closing) return;
    switch (p[0]) {
    case P_HELLO: on_hello(s, c, p, n); break;
    case P_CMD: {
        if (n < 5 || 5 + (uint32_t)get16(p + 3) > n) return;
        uint8_t type = p[1];
        if (type != DSNET_GAME_ORDER && type != DSNET_SPEED_VOTE && type != DSNET_PAUSE_VOTE && type != DSNET_OWNERSHIP &&
            type != DSNET_STORY_PAUSE && type != DSNET_EXPEDITION_READY && type != DSNET_ITEM_OWNER &&
            type != DSNET_ADMIN_MODE && type != DSNET_OWNERS_LOAD && type != DSNET_HOLD && type != DSNET_TEMP_CONTROL && type != DSNET_RULES && type != DSNET_LEND) return;
        if ((type == DSNET_ADMIN_MODE || type == DSNET_OWNERS_LOAD || type == DSNET_HOLD || type == DSNET_TEMP_CONTROL || type == DSNET_RULES) && !c->is_host) return;
        if (type == DSNET_RULES) {                 /* host rules: checked, the server's own part applied now, then stamped */
            dsnet_rules r = { 0 };
            if (get16(p + 3) < 4) return;
            r.pause_mode = p[5]; r.allow_speedup = p[6]; r.gear_mode = p[7]; r.units_per_player = p[8];
            r.event_choice = get16(p + 3) >= 5 ? p[9] : s->cfg.event_choice;
            r.population = get16(p + 3) >= 6 ? p[10] : s->cfg.population;
            if (!dsnet_rules_valid(&r)) return;
            s->cfg.pause_mode = r.pause_mode; s->cfg.allow_pause = r.pause_mode != DSNET_PAUSE_NONE; s->cfg.allow_speedup = r.allow_speedup;
            s->cfg.gear_mode = r.gear_mode; s->cfg.units_per_player = r.units_per_player; s->cfg.event_choice = r.event_choice; s->cfg.population = r.population;
            dsnet_logf("server: rules pause %d speed-up %d gear %d units %d", r.pause_mode, r.allow_speedup, r.gear_mode, r.units_per_player);
        }
        if (c->status == ST_LOADING) return;
        /* no votes until back in sync, so an auto pause holds until then */
        if (c->status == ST_DESYNCED && (type == DSNET_PAUSE_VOTE || type == DSNET_SPEED_VOTE)) return;
        /* lobby settings */
        if (type == DSNET_PAUSE_VOTE && s->cfg.pause_mode == DSNET_PAUSE_NONE) return;
        if (type == DSNET_SPEED_VOTE && !s->cfg.allow_speedup && (get16(p + 3) < 1 || p[5] > 1)) return;
        issue(s, type, c->id, p[2], p + 5, get16(p + 3));
        break;
    }
    case P_KEEPALIVE:
        if (n < 9) return;
        c->ticks_behind = (int)(get32(p + 5) - get32(p + 1));
        if (c->ticks_behind < 0) c->ticks_behind = 0;
        c->behind_at = s->timer;
        break;
    case P_FRAMETIME: if (n >= 5) c->frame_ms = get32(p + 1) / 1000.0f; break;
    case P_FULL:      /* host's full-state list -> everyone else (outside the sim) */
        if (c->is_host && n >= 5) send_all_live(s, p, n, c->id);
        break;
    case P_FP:        /* host's opinion -> everyone else (ServerPlayingState.HandleSyncInfo) */
        if (c->is_host && n >= 21) send_all_live(s, p, n, c->id);
        break;
    case P_DESYNC: {
        if (n < 5) return;
        int partial = n >= 6 && p[5];             /* a partial resync comes first: no pause for it (~1 s, no reload) */
        if (c->status == ST_PLAYING || c->status == ST_CATCHUP) {
            c->status = ST_DESYNCED;
            uint8_t cause = c->id;
            if (auto_pause(s) && !partial) { issue(s, DSNET_PAUSE_ALL, DSNET_NO_PLAYER, 0, &cause, 1); c->auto_paused = 1; }
        }
        dsnet_logf("server: player %d desynced at tick %u%s", c->id, get32(p + 1), partial ? " (partial resync first)" : "");
        break;
    }
    case P_PATCH_REQ:
        if (c->status != ST_DESYNCED || c->patching) return;
        c->patch_wanted = 1; c->resync = 1;
        dsnet_logf("server: player %d asks for a partial resync", c->id);
        maybe_issue_patch(s);
        break;
    case P_PATCHED:
        if (n < 2) return;
        c->patching = 0;
        if (p[1] && c->status == ST_DESYNCED) {   /* in sync again: catch up like after a load (P_READY ends it) */
            c->status = ST_CATCHUP; c->resync = 0; c->ticks_behind = 0; c->behind_at = s->timer;
        }
        dsnet_logf("server: player %d partial resync %s", c->id, p[1] ? "applied" : "failed");
        patch_settle(s);
        break;
    case P_RESYNC_REQ:
        if (c->status == ST_DESYNCED && !c->auto_paused && auto_pause(s)) {   /* the partial resync failed: pause now */
            uint8_t cause = c->id;
            issue(s, DSNET_PAUSE_ALL, DSNET_NO_PLAYER, 0, &cause, 1); c->auto_paused = 1;
        }
        c->status = ST_LOADING; c->resync = 1; c->patching = 0; c->patch_wanted = 0;
        patch_settle(s);
        dsnet_logf("server: player %d asks for resync", c->id);
        request_join_point(s);
        break;
    case P_CHAT: {                             /* relay: from, the sender's name, the text (cut to the limit) */
        if (n < 2) return;
        uint32_t tl = n - 1 > DSNET_CHAT_MAX ? DSNET_CHAT_MAX : n - 1;
        uint8_t b[2 + 32 + DSNET_CHAT_MAX];
        b[0] = P_CHAT; b[1] = (uint8_t)c->id; memset(b + 2, 0, 32); memcpy(b + 2, c->name, strnlen(c->name, 31));
        memcpy(b + 34, p + 1, tl);
        for (int i = 0; i < MAX_CONNS; i++) if (s->conn[i] && s->conn[i]->id >= 0 && !s->conn[i]->closing) dsnet_link_send(&s->conn[i]->link, b, 34 + tl);
        break;
    }
    case P_WORLD:
        if (c->is_host && n >= 49) { memcpy(s->world, p + 1, 48); s->world[47] = 0; s->has_world = 1; }
        break;
    case P_VIEW: {                             /* camera view: to the others that are not loading (dropped, not queued) */
        if (n < 21) return;
        uint8_t b[22]; b[0] = P_VIEW; b[1] = (uint8_t)c->id; memcpy(b + 2, p + 1, 20);
        for (int i = 0; i < MAX_CONNS; i++) {
            sconn *o = s->conn[i];
            if (o && o != c && o->id >= 0 && !o->closing && o->status != ST_LOADING && o->status != ST_NEW) dsnet_link_send(&o->link, b, 22);
        }
        break;
    }
    case P_MARK: {                             /* ping add / delete: to the others; adds limited per sender */
        if (n < 12 || (p[1] != DSNET_MARK_ADD && p[1] != DSNET_MARK_DEL)) return;
        if (p[1] == DSNET_MARK_ADD) {
            double now = dsnet_now_ms(), oldest = c->mark_t[c->mark_i];   /* ring of the last N add times */
            if (oldest > 0 && now - oldest < DSNET_MARK_SPAM_MS) return;
            c->mark_t[c->mark_i] = now; c->mark_i = (c->mark_i + 1) % DSNET_MARK_SPAM_N;
        }
        uint8_t b[13]; b[0] = P_MARK; b[1] = (uint8_t)c->id; memcpy(b + 2, p + 1, 11);
        if (p[1] == DSNET_MARK_ADD) b[3] = (uint8_t)c->id;
        for (int i = 0; i < MAX_CONNS; i++) {
            sconn *o = s->conn[i];
            if (o && o != c && o->id >= 0 && !o->closing) dsnet_link_send(&o->link, b, 13);
        }
        break;
    }
    case P_TYPING: {                           /* relay to the others: from, on */
        if (n < 2) return;
        uint8_t b[3] = { P_TYPING, (uint8_t)c->id, (uint8_t)(p[1] != 0) };
        for (int i = 0; i < MAX_CONNS; i++) if (s->conn[i] && s->conn[i] != c && s->conn[i]->id >= 0 && !s->conn[i]->closing) dsnet_link_send(&s->conn[i]->link, b, 3);
        break;
    }
    case P_PONG:
        if (n >= 5) c->ping_ms = (int)((uint32_t)dsnet_now_ms() - get32(p + 1));
        break;
    case P_BUSY:
        if (n >= 2) c->busy = p[1] != 0;
        break;
    case P_READY:
        if (c->status == ST_CATCHUP) {
            c->status = ST_PLAYING; c->ticks_behind = 0; c->behind_at = s->timer; c->resync = 0; c->auto_paused = 0;
            dsnet_logf("server: player %d caught up at timer %u", c->id, s->timer);
        }
        break;
    case P_BLOB: {
        if (!c->is_host) {                         /* a guest's desync report part: relay it whole to the host */
            if (n < 1 + BLOB_HDR || p[5] != BLOB_REPORT || get32(p + 6) > DSNET_MAX_REPORT) return;
            int r = dsnet_blob_feed(&c->rx, p + 1, n - 1);
            if (r < 0) { dsnet_logf("server: bad report fragment from player %d", c->id); return; }
            if (r == 0) return;
            sconn *h = host_conn(s);
            if (h) dsnet_link_send_blob(&h->link, ++s->blob_id, BLOB_REPORT, 0, (uint32_t)c->id, c->rx.buf, c->rx.total);
            dsnet_logf("server: desync report part from player %d (%u bytes) %s", c->id, c->rx.total, h ? "sent to the host" : "dropped (no host)");
            free(c->rx.buf); c->rx.buf = NULL;
            return;
        }
        int r = dsnet_blob_feed(&c->rx, p + 1, n - 1);
        if (r < 0) { dsnet_logf("server: bad blob fragment from host"); return; }
        if (r == 0) return;
        if (c->rx.kind == BLOB_PATCH) {            /* host's state at the PATCH_POINT -> the players in it */
            int sent = 0;
            for (int i = 0; i < MAX_CONNS; i++) {
                sconn *o = s->conn[i];
                if (o && o->id >= 0 && o->patching && !o->is_host) {
                    dsnet_link_send_blob(&o->link, ++s->blob_id, BLOB_PATCH, c->rx.tick, c->rx.seq_after, c->rx.buf, c->rx.total);
                    sent++;
                }
            }
            dsnet_logf("server: patch (%u bytes, tick %u) sent to %d player(s)", c->rx.total, c->rx.tick, sent);
            free(c->rx.buf); c->rx.buf = NULL;
            s->patch_pending = 0;
            maybe_issue_patch(s);                  /* players that asked meanwhile get the next one */
            return;
        }
        if (c->rx.kind != BLOB_SAVE) return;
        free(s->save);
        s->save = c->rx.buf; s->save_len = c->rx.total;
        s->save_tick = c->rx.tick; s->save_seq_after = c->rx.seq_after;
        c->rx.buf = NULL;
        s->jp_pending = 0;
        for (int i = 0; i < MAX_CONNS; i++)
            /* every guest loads it, not only the joiner / resyncing one: the host reloads its own save at the join point
               (a save drops in-progress tasks, mp_game.c), so a guest that kept its memory state would differ from then on
               (3+ players, lan_v10 bug 2026-10-14: the 2nd guest's join desynced the 1st one again and again) */
            if (s->conn[i] && s->conn[i]->id >= 0 && !s->conn[i]->is_host && s->conn[i]->status != ST_NEW) send_save_to(s, s->conn[i]);
        break;
    }
    }
}

static void add_conn(dsnet_server *s, int fd, int local) {
    for (int i = 0; i < MAX_CONNS; i++) if (!s->conn[i]) {
        sconn *c = calloc(1, sizeof *c);
        dsnet_link_init(&c->link, dsnet_fd_transport(fd));
        c->local = local; c->id = -1; c->last_rx = dsnet_now_ms();
        s->conn[i] = c;
        return;
    }
    close(fd);
}

/* MultiplayerServer.Run timer step + TickNet */
static void advance_timer(dsnet_server *s, double *real) {
    for (int ticked = 0; *real > 0 && ticked < 2; ticked++) {
        int any_ok = 0, n_joined = 0, behind = 0, maxb = 0, worst = -1, busy = 0;
        for (int i = 0; i < MAX_CONNS; i++) {
            sconn *c = s->conn[i];
            if (c && c->id >= 0) n_joined++;      /* loading players count for min_players (they need the timer to get a save) */
            if (!c || c->status != ST_PLAYING) continue;
            if (c->busy) busy = 1;
            int e = ext_behind(s, c);
            if (e < s->cfg.ok_behind) any_ok = 1;
            if (e > s->cfg.max_behind) { behind = 1; worst = c->id; }
            if (c->ticks_behind > maxb) maxb = c->ticks_behind;
        }
        if (maxb > s->max_behind_seen) s->max_behind_seen = maxb;
        int can_start = s->started || n_joined >= s->cfg.min_players;
        if (can_start && any_ok && !behind && !busy) {
            s->started = 1;
            s->timer++;
            s->sent_snapshot = s->log_n;
        } else if (s->started) {
            if (behind && s->held % 30 == 0) dsnet_logf("server: timer held at %u, player %d too far behind", s->timer, worst);
            s->held++;
        }
        /* up to 3x slower; a low-fps peer is normally up to a frame's worth of steps behind, so
         * only the backlog above ok_behind counts (rwmt uses maxb/60 with one step per frame) */
        int over = maxb - s->cfg.ok_behind; if (over < 0) over = 0;
        float slow = over / 60.0f; if (slow > 2) slow = 2;
        *real -= s->tpt * (1 + slow);
    }
    if (*real > 0) *real = 0;
}

/* player list for the UI: state, ping, ticks behind; pings once a second */
static void send_players(dsnet_server *s, int ping) {
    uint8_t b[2 + MAX_CONNS * 6], *p = b + 2; int cnt = 0;
    uint8_t pg[5]; put8(pg, P_PING); put32(pg + 1, (uint32_t)dsnet_now_ms());
    for (int i = 0; i < MAX_CONNS; i++) {
        sconn *c = s->conn[i];
        if (!c || c->id < 0) continue;
        if (ping) dsnet_link_send(&c->link, pg, 5);
        int st = c->status == ST_PLAYING ? DSNET_NET_IN_SYNC
               : c->status == ST_DESYNCED || c->resync ? DSNET_NET_RESYNCING : DSNET_NET_LOADING;
        int tb = c->status == ST_PLAYING ? ext_behind(s, c) : 0;
        if (tb < 0) tb = 0;
        if (tb > 65535) tb = 65535;
        p = put8(p, c->id); p = put8(p, st); p = put16(p, c->ping_ms > 65535 ? 65535 : c->ping_ms); p = put16(p, tb);
        cnt++;
    }
    b[0] = P_PLAYERS; b[1] = cnt;
    send_all_live(s, b, p - b, -1);
}

static void net_tick(dsnet_server *s) {
    uint32_t busy = 0;
    for (int i = 0; i < MAX_CONNS; i++) {
        sconn *c = s->conn[i];
        if (c && c->id >= 0 && (c->busy || c->status == ST_LOADING || c->status == ST_CATCHUP || c->status == ST_DESYNCED))
            busy |= 1u << c->id;
    }
    uint8_t b[20], *p = b;
    p = put8(p, P_TIMECTL); p = put32(p, s->timer); p = put32(p, s->sent_snapshot); p = put32(p, (uint32_t)(s->tpt * 1000));
    p = put32(p, busy);
    send_all_live(s, b, p - b, -1);
    float m = 0;
    for (int i = 0; s->cfg.slow_to_frame_time && i < MAX_CONNS; i++)
        if (s->conn[i] && s->conn[i]->status == ST_PLAYING && s->conn[i]->frame_ms > m) m = s->conn[i]->frame_ms;
    if (m < s->cfg.tick_ms) m = s->cfg.tick_ms;
    if (m > s->cfg.tick_ms * 4) m = s->cfg.tick_ms * 4;
    s->tpt = m;
}

static void *server_main(void *arg) {
    dsnet_server *s = arg;
    double last = dsnet_now_ms(), real = 0, next_net = last;
    struct pollfd pf[MAX_CONNS + 2];
    int idx[MAX_CONNS + 2];
    while (s->running) {
        int np = 0;
        pf[np] = (struct pollfd){ s->listen_fd, POLLIN, 0 }; idx[np++] = -1;
        pf[np] = (struct pollfd){ s->wake[0], POLLIN, 0 };   idx[np++] = -2;
        for (int i = 0; i < MAX_CONNS; i++) if (s->conn[i]) {
            short ev = POLLIN | (dsnet_link_has_out(&s->conn[i]->link) ? POLLOUT : 0);
            pf[np] = (struct pollfd){ s->conn[i]->link.tr.poll_fd(s->conn[i]->link.tr.t), ev, 0 }; idx[np++] = i;
        }
        double now = dsnet_now_ms();
        double wait = next_net - now;
        if (-real < wait) wait = -real;
        if (wait < 0) wait = 0;
        if (wait > 10) wait = 10;
        poll(pf, np, (int)wait);

        for (int k = 0; k < np; k++) {
            if (!pf[k].revents) continue;
            if (idx[k] == -1) {
                int fd = accept(s->listen_fd, NULL, NULL);
                if (fd >= 0) { int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one); add_conn(s, fd, 0); }
            } else if (idx[k] == -2) {
                char junk[64]; while (read(s->wake[0], junk, sizeof junk) > 0) {}
            }
        }
        pthread_mutex_lock(&s->mu);
        for (int i = 0; i < s->npending; i++) add_conn(s, s->pending_local[i], 1);
        s->npending = 0;
        pthread_mutex_unlock(&s->mu);

        for (int i = 0; i < MAX_CONNS; i++) {
            sconn *c = s->conn[i];
            if (!c) continue;
            dsnet_link_fill(&c->link);
            const uint8_t *body; uint32_t n;
            double t = dsnet_now_ms();
            while (dsnet_link_next(&c->link, &body, &n)) { on_frame(s, c, body, n); c->last_rx = t; }
            /* silent guest (keep-alive every 100 ms): drop it so it leaves instead of holding the timer */
            int tmo = dsnet_rx_timeout_ms();
            if (c->id >= 0 && tmo && t - c->last_rx > tmo && !c->link.dead) {
                dsnet_logf("server: player %d silent for %d ms, dropped", c->id, (int)(t - c->last_rx));
                c->link.dead = 1;
            }
        }
        now = dsnet_now_ms();
        real += now - last; last = now;
        advance_timer(s, &real);
        if (now >= next_net) {
            net_tick(s);
            next_net += s->cfg.net_ms;
            if (now - s->last_list >= 200) {
                int pg = now - s->last_ping >= 1000;
                send_players(s, pg);
                s->last_list = now; if (pg) s->last_ping = now;
            }
            if (next_net < now) next_net = now + s->cfg.net_ms;
        }
        for (int i = 0; i < MAX_CONNS; i++) {
            if (!s->conn[i]) continue;
            dsnet_link_flush(&s->conn[i]->link);
            if (s->conn[i]->link.dead || (s->conn[i]->closing && !dsnet_link_has_out(&s->conn[i]->link))) drop_conn(s, i);
        }
        pthread_mutex_lock(&s->mu);
        int np2 = 0;
        for (int i = 0; i < MAX_CONNS; i++) if (s->conn[i] && s->conn[i]->id >= 0) np2++;
        s->stats = (dsnet_server_stats){ s->timer, s->log_n, np2, s->held, s->tpt, s->max_behind_seen };
        pthread_mutex_unlock(&s->mu);
    }
    /* stopping: tell every player the host ended the session (P_BYE), flushed for at most 300 ms */
    uint8_t bye[2] = { P_BYE, 1 };
    for (int i = 0; i < MAX_CONNS; i++) if (s->conn[i] && s->conn[i]->id >= 0 && !s->conn[i]->link.dead) dsnet_link_send(&s->conn[i]->link, bye, 2);
    for (double t0 = dsnet_now_ms(); dsnet_now_ms() - t0 < 300; ) {
        int left = 0;
        for (int i = 0; i < MAX_CONNS; i++) if (s->conn[i] && !s->conn[i]->link.dead) { dsnet_link_flush(&s->conn[i]->link); left |= dsnet_link_has_out(&s->conn[i]->link); }
        if (!left) break;
        usleep(5000);
    }
    return NULL;
}

dsnet_cfg dsnet_cfg_defaults(void) { return (dsnet_cfg){ .allow_speedup = 1, .allow_pause = 1, .pause_mode = DSNET_PAUSE_ANYONE }; }

dsnet_server *dsnet_server_start(const dsnet_cfg *cfg) {
    dsnet_server *s = calloc(1, sizeof *s);
    s->cfg = cfg ? *cfg : dsnet_cfg_defaults();
    if (s->cfg.gear_mode < 0 || s->cfg.gear_mode > DSNET_GEAR_OWN_STORAGE) s->cfg.gear_mode = DSNET_GEAR_SHARED_OWN;
    if (!s->cfg.allow_pause || s->cfg.pause_mode < 0 || s->cfg.pause_mode > DSNET_PAUSE_ALL_VOTE) s->cfg.pause_mode = DSNET_PAUSE_NONE;
    if (s->cfg.event_choice < 0 || s->cfg.event_choice > DSNET_EVENT_MAJORITY) s->cfg.event_choice = DSNET_EVENT_OWNER;
    if (s->cfg.population < 0 || s->cfg.population > DSNET_POP_MULTIPLIED) s->cfg.population = DSNET_POP_SHARED;
    { dsnet_rules r = { s->cfg.pause_mode, s->cfg.allow_speedup != 0, s->cfg.gear_mode, s->cfg.units_per_player, s->cfg.event_choice, s->cfg.population };
      if (!dsnet_rules_valid(&r)) s->cfg.units_per_player = DSNET_UNITS_DEFAULT; }
    if (s->cfg.min_players <= 0) s->cfg.min_players = 1;
    if (s->cfg.max_behind <= 0) s->cfg.max_behind = 90;
    if (s->cfg.ok_behind <= 0) s->cfg.ok_behind = 40;
    if (s->cfg.tick_ms <= 0) s->cfg.tick_ms = 1000.0f / 60;
    if (s->cfg.net_ms <= 0) s->cfg.net_ms = 1000.0f / 30;
    if (s->cfg.fp_interval <= 0) s->cfg.fp_interval = 30;
    s->tpt = s->cfg.tick_ms;
    snprintf(s->build_id, sizeof s->build_id, "%s", s->cfg.build_id ? s->cfg.build_id : dsnet_build_id);
    if (!s->cfg.build_id) s->cfg.mod_version = dsnet_mod_version;
    s->round_mode = fegetround();
    pthread_mutex_init(&s->mu, NULL);

    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(s->cfg.port) };
    a.sin_addr.s_addr = s->cfg.bind_addr ? inet_addr(s->cfg.bind_addr) : INADDR_ANY;
    if (bind(s->listen_fd, (struct sockaddr *)&a, sizeof a) || listen(s->listen_fd, 16)) {
        dsnet_logf("server: bind/listen on port %u failed", s->cfg.port);
        close(s->listen_fd); free(s); return NULL;
    }
    socklen_t al = sizeof a; getsockname(s->listen_fd, (struct sockaddr *)&a, &al);
    s->port = ntohs(a.sin_port);
    pipe(s->wake);
    for (int i = 0; i < 2; i++) fcntl(s->wake[i], F_SETFL, O_NONBLOCK);
    fcntl(s->listen_fd, F_SETFL, O_NONBLOCK);
    s->running = 1;
    pthread_create(&s->th, NULL, server_main, s);
    dsnet_logf("server: listening on port %u", s->port);
    return s;
}

uint16_t dsnet_server_port(dsnet_server *s) { return s->port; }

void dsnet_server_get_stats(dsnet_server *s, dsnet_server_stats *out) {
    pthread_mutex_lock(&s->mu); *out = s->stats; pthread_mutex_unlock(&s->mu);
}

/* used by dsnet_client_connect_local */
int dsnet_server_add_local(dsnet_server *s, int fd) {
    pthread_mutex_lock(&s->mu);
    int ok = s->npending < 8;
    if (ok) s->pending_local[s->npending++] = fd;
    pthread_mutex_unlock(&s->mu);
    if (ok) write(s->wake[1], "x", 1);
    return ok ? 0 : -1;
}

void dsnet_server_stop(dsnet_server *s) {
    if (!s) return;
    s->running = 0;
    write(s->wake[1], "x", 1);
    pthread_join(s->th, NULL);
    for (int i = 0; i < MAX_CONNS; i++) if (s->conn[i]) {
        free(s->conn[i]->rx.buf); dsnet_link_free(&s->conn[i]->link); free(s->conn[i]);
    }
    for (int i = 0; i < s->npending; i++) close(s->pending_local[i]);
    close(s->listen_fd); close(s->wake[0]); close(s->wake[1]);
    free(s->log); free(s->log_off); free(s->save);
    pthread_mutex_destroy(&s->mu);
    free(s);
}
