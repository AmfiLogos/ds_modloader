/* dsnet internals shared by the server, the client and the link layer. */
#pragma once
#include "dsnet.h"
#include <pthread.h>
#include <stdint.h>
#include <string.h>

/* ---- packet types (first byte of every frame) ---- */
enum {
    P_HELLO = 1,      /* c->s  u16 proto, u8 want_host, u32 mod_version, u32 round_mode, uid[16], u8 build_len, build[], name[] */
    P_WELCOME,        /* s->c  u8 player, u8 is_host, u8 needs_state, u32 timer, u32 sent_cmds, u32 fp_interval, u8 allow_speedup, u8 pause_mode, u8 gear_mode, u8 units_per_player */
    P_CMD,            /* c->s  u8 type, u8 scope, u16 len, payload */
    P_SCMD,           /* s->c  u32 seq, u8 type, u32 tick, u8 player, u8 scope, u16 len, payload */
    P_TIMECTL,        /* s->c  u32 timer, u32 sent_cmds, u32 time_per_tick_us, u32 busy_mask */
    P_KEEPALIVE,      /* c->s  u32 timer, u32 tick_until */
    P_FRAMETIME,      /* c->s  u32 avg frame time us */
    P_FP,             /* host c->s, then s->c relay: u32 tick, u64 game hash, u64 owner-table hash,
                         [u64 x3 section hashes: units, positions, rng+tick] (v10, for the desync report / partial resync) */
    P_DESYNC,         /* c->s  u32 bad_tick, [u8 partial: 1 = a partial resync follows, no auto pause yet] */
    P_RESYNC_REQ,     /* c->s */
    P_READY,          /* c->s  caught up after a load */
    P_BLOB,           /* both  u32 id, u8 kind, u32 total, u32 offset, u32 tick, u32 seq_after, data */
    P_BUSY,           /* c->s  u8 busy (in a barrier: transition / loading flow) */
    P_PING,           /* s->c  u32 server ms; client echoes it back in P_PONG */
    P_PONG,           /* c->s  u32 echoed ms */
    P_PLAYERS,        /* s->c  u8 count, then per player: u8 id, u8 state, u16 ping ms, u16 ticks behind */
    P_REJECT,         /* s->c  u8 reason (enum dsnet_reject), then the server closes */
    P_CHAT,           /* c->s  text (utf8, <= DSNET_CHAT_MAX); s->c u8 from, name[32], text. Outside the sim: not stamped,
                         never in saves or hashes; relayed by the server to every connected player (loading ones too) */
    P_TYPING,         /* c->s  u8 on; s->c u8 from, u8 on. "is typing" (outside the sim, like P_CHAT); not echoed to the sender */
    P_BYE,            /* s->c  u8 reason (1 = the host ended the session), sent when the server stops: a guest tells
                         "the host left" from "my own connection dropped" (no BYE) */
    P_PATCH_REQ,      /* c->s  partial resync wanted (after P_DESYNC with partial 1): the server issues DSNET_PATCH_POINT */
    P_PATCHED,        /* c->s  u8 ok: the partial resync was applied (1, back to catch-up) or failed (0, a full resync follows) */
    P_VIEW,           /* c->s  u32 map_key, f32 cx, cy, half_w, half_h; s->c u8 from + same. Outside the sim (like P_TYPING):
                         relayed to every other player that is not loading (dropped, never queued) */
    P_MARK,           /* c->s  u8 op (1 add, 2 delete), u8 owner, u8 id, u32 map_key, i16 x, i16 y; s->c u8 from + same (add: owner
                         = from). Relayed to all but the sender; the server drops a 5th add from one sender within 5 s */
};
/* SAVE: host -> server -> joiners (join point). PATCH: host -> server -> the players of a PATCH_POINT (partial resync).
   REPORT: guest -> server -> host, outside the sim (desync report part; the server stamps seq_after = the sender's slot). */
enum { BLOB_SAVE = 1, BLOB_PATCH = 2, BLOB_REPORT = 3 };
#define DSNET_MAX_REPORT (8u * 1024 * 1024)
#define BLOB_HDR 22

/* ---- transport: a byte stream; TCP and socketpair now, Steam P2P later ---- */
typedef struct dsnet_transport {
    long (*write)(void *t, const void *buf, size_t n);   /* bytes written, 0 would block, -1 dead */
    long (*read)(void *t, void *buf, size_t n);          /* bytes read, 0 nothing now, -1 dead */
    int  (*poll_fd)(void *t);                            /* fd to poll, or -1 (then loops time out) */
    void (*close)(void *t);
    void *t;
} dsnet_transport;
dsnet_transport dsnet_fd_transport(int fd);              /* sets fd non-blocking */

/* ---- framed link: u32 LE length + body; output queued, flushed by the owning net thread ---- */
typedef struct dsnet_link {
    dsnet_transport tr;
    pthread_mutex_t mu;                /* guards out (any thread may send) */
    uint8_t *out; size_t out_off, out_len, out_cap;
    uint8_t *in;  size_t in_off, in_len, in_cap;   /* net thread only */
    int dead;
} dsnet_link;
void dsnet_link_init(dsnet_link *l, dsnet_transport tr);
void dsnet_link_free(dsnet_link *l);
int  dsnet_link_send(dsnet_link *l, const uint8_t *body, uint32_t len);
int  dsnet_link_send_blob(dsnet_link *l, uint32_t id, uint8_t kind, uint32_t tick, uint32_t seq_after,
                          const uint8_t *data, uint32_t len);
int  dsnet_link_flush(dsnet_link *l);           /* -1 dead */
int  dsnet_link_has_out(dsnet_link *l);
int  dsnet_link_fill(dsnet_link *l);            /* read what is available; -1 dead */
int  dsnet_link_next(dsnet_link *l, const uint8_t **body, uint32_t *len);  /* 1 = got a frame */

typedef struct dsnet_blob_rx {
    uint32_t id, total, got, tick, seq_after;
    uint8_t kind;
    uint8_t *buf;
} dsnet_blob_rx;
/* 1 = blob complete (caller owns buf afterwards), 0 = more to come, -1 = bad fragment */
int dsnet_blob_feed(dsnet_blob_rx *rx, const uint8_t *p, uint32_t n);

/* ---- little-endian put/get ---- */
static inline uint8_t *put8(uint8_t *p, uint8_t v)  { *p = v; return p + 1; }
static inline uint8_t *put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; return p + 2; }
static inline uint8_t *put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = v >> (8 * i); return p + 4; }
static inline uint8_t *put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = v >> (8 * i); return p + 8; }
static inline uint16_t get16(const uint8_t *p) { return p[0] | p[1] << 8; }
static inline uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline uint64_t get64(const uint8_t *p) { return get32(p) | (uint64_t)get32(p + 4) << 32; }

double dsnet_now_ms(void);
/* receive-silence limit (dsnet_set_rx_timeout_ms / env DSNET_RX_TIMEOUT_MS, default 5000, 0 = off). Peers send from
 * their network threads every <= 100 ms, so a frozen game thread never looks like a lost link. */
int    dsnet_rx_timeout_ms(void);
extern char     dsnet_build_id[32];
extern uint32_t dsnet_mod_version;
extern uint8_t  dsnet_uid[16];
extern int      dsnet_uid_set;
void   dsnet_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
