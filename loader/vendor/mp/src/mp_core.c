/* mp core: session, tick gate, command dispatch, fingerprint, resync, speed/pause votes.
 * Game access only through mp_game_api. See mp.h and MP_NOTES.md. */
#include "mp.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <dlfcn.h>

#define MP_MAGIC 0x504D5344u     /* "DSMP" */
#define MP_HDR   16              /* magic u32, version u16, half_acc u8, shown_speed u8, shown_paused u8, pad */
#define MP_PATCH_TIMEOUT_MS 5000 /* partial resync not done by then (host slow / patch lost): full reload */
#define MP_PROBATION_STEPS 600   /* ~10 s after a partial resync: any new difference (fingerprint or deep hash) = full reload.
                                    release9 2026-10-14: a corrupted RNG stream changed hidden state that resurfaced 2-5 s
                                    after a proven patch; one partial per incident, the next one reloads everything */

struct mp_session {
    mp_config cfg;
    mp_game_api api;
    dsnet_server *srv;
    dsnet_client *c;
    mp_order_fn handlers[MP_ORDER_MAX];
    uint32_t timer;              /* next step to run */
    uint32_t cur;                /* step being applied */
    int applying;
    int step_orders;                     /* game orders applied in the current step (paused-step rule in mp_frame) */
    int step_ticks;              /* world ticks of the step just applied */
    float pace_acc;              /* smooth pacing: wall-clock ms not yet turned into steps */
    int mid_ticks;               /* world ticks of step mid_timer still to run (a flow went busy mid-step) */
    uint32_t mid_timer;
    int half_acc;                /* half-tick remainder (Fast = 3.5 ticks/step) */
    int shown_speed, shown_paused;
    int loading;                 /* game load started by a save transfer; waiting for api.ready */
    int desync_seen; double desync_at; int resync_sent;
    int patch_asked; double patch_at;    /* lan_v10 partial resync asked (guest), when */
    uint32_t probation_until;            /* a new desync before this step, right after a partial resync: full reload */
    int deep_wait; uint32_t deep_after;  /* a partial resync at step deep_after waits for its deep check */
    int force_full;                      /* the deep check failed: this desync goes straight to the full reload */
    uint32_t deep_bad_seen;
    uint8_t *full_mine; size_t full_mine_len; uint32_t full_mine_tick;   /* guest: own full-state list, waiting for the host's */
    uint8_t *full_host; size_t full_host_len; uint32_t full_host_tick;
    char full_diff[2048];                /* newest full-state difference (for the log and the desync report) */
    int user_pause;              /* this player's own User pause vote */
    int popups[6];               /* open local popup pause reasons */
    int popup_voted;             /* the popups produced a pause vote */
    int resume_pending;          /* PAUSE_ALL seen: re-send own run vote once in sync */
    /* host: players that left and are waited for (owner 2026-10-13: auto pause + "Wait for <name>" / "Keep playing") */
    uint8_t slot_uid[DSNET_MAX_PLAYERS][16]; char slot_name[DSNET_MAX_PLAYERS][32]; uint32_t slot_known;
    int hold_waiting;            /* host chose "Wait" (local; the holds themselves are dsnet state) */
    double last_frame;
    mp_stats st;
};

static void (*log_fn)(const char *);
static char other_world[32];             /* name of the world a refused join found instead (F17) */
const char *mp_last_other_world(void) { return other_world; }
void mp_set_log(void (*fn)(const char *line)) { log_fn = fn; }
static void mlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void mlog(const char *fmt, ...) {
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (log_fn) log_fn(b); else if (getenv("MP_LOG")) fprintf(stderr, "mp: %s\n", b);
}
static double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }

/* half ticks per step for GameSpeed 0..3 = 2 x [0, 1, 3.5, 7] (GAMESYSTEM_Campaign_GameSpeed) */
static const int half_per_step[4] = { 0, 2, 7, 14 };   /* [3] unused: votes are clamped to Fast */

/* ---------------------------------------------------------------- orders */
static int order_move_default(mp_session *s, int player, const uint8_t guid[16], const uint8_t *a, uint16_t n) {
    (void)player;
    if (n < 8 || !s->api.order_move) return -1;
    int32_t x, y; memcpy(&x, a, 4); memcpy(&y, a + 4, 4);
    return s->api.order_move(s->api.user, guid, x, y);
}

static void run_order(mp_session *s, const dsnet_cmd *cmd) {
    const uint8_t *p = cmd->payload;
    if (cmd->len < MP_ORDER_HDR) { s->st.orders_bad++; return; }
    uint16_t type = (uint16_t)(p[0] | p[1] << 8), alen = (uint16_t)(p[18] | p[19] << 8);
    if (type >= MP_ORDER_MAX || !s->handlers[type] || MP_ORDER_HDR + alen > cmd->len) { s->st.orders_bad++; return; }
    static const uint8_t zero_guid[16];
    int global = type == MP_ORDER_STORY_VOTE || type == MP_ORDER_MAP_PROPOSE || type == MP_ORDER_MAP_VOTE ||
                 !memcmp(p + 2, zero_guid, 16);   /* zero owner = order about no unit (base work: open to everyone) */
    /* dsnet_may_order (netcore): own unit -> its owner; unclaimed -> host; RESERVED (owner away) -> host only in Admin mode
       or with temporary control after "Keep playing"; Admin mode -> host, any unit. dsnet state: same on every peer. */
    int allowed = global || dsnet_may_order(s->c, cmd->player, p + 2);
    if (!allowed) {
        s->st.orders_dropped_owner++;
        return;
    }
    if (s->api.sim_scope) s->api.sim_scope(s->api.user, 1);
    int rc = s->handlers[type](s, cmd->player, p + 2, p + MP_ORDER_HDR, alen);
    if (s->api.sim_scope) s->api.sim_scope(s->api.user, 0);
    if (rc) s->st.orders_bad++; else s->st.orders_applied++;
}

/* ---------------------------------------------------------------- save transfer */
static void host_upload(mp_session *s) {
    uint8_t *g = NULL; size_t n = 0;
    if (!s->api.save || s->api.save(s->api.user, &g, &n) || !g) { mlog("join point at step %u: game save failed", s->cur); free(g); return; }
    uint8_t *b = malloc(MP_HDR + n);
    uint32_t m = MP_MAGIC; memcpy(b, &m, 4);
    b[4] = 1; b[5] = 0; b[6] = (uint8_t)s->half_acc; b[7] = (uint8_t)s->shown_speed; b[8] = (uint8_t)s->shown_paused;
    memset(b + 9, 0, MP_HDR - 9);
    memcpy(b + MP_HDR, g, n);
    free(g);
    int rc = dsnet_upload_save(s->c, b, MP_HDR + n);
    free(b);
    s->st.saves++;
    mlog("join point at step %u: uploaded %zu-byte save (rc %d)", s->cur, n, rc);
}

static void on_load(uint32_t tick, const uint8_t *data, size_t len, void *user) {
    mp_session *s = user;
    uint32_t m = 0;
    if (len >= MP_HDR) memcpy(&m, data, 4);
    if (m != MP_MAGIC || data[4] != 1) { mlog("save at step %u has no mp header: ignored", tick); return; }
    size_t off = MP_HDR;
    s->half_acc = data[6]; s->shown_speed = data[7]; s->shown_paused = data[8];
    s->timer = tick;
    s->loading = 1; s->desync_seen = 0; s->resync_sent = 0; s->mid_ticks = 0; s->force_full = 0; s->deep_wait = 0;
    free(s->full_mine); s->full_mine = NULL; free(s->full_host); s->full_host = NULL;   /* steps restart from the save */
    if (s->cfg.auto_resume) s->resume_pending = 1;          /* the PAUSE_ALL before the join point is inside the save */
    s->st.loads++;
    int rc = s->api.load ? s->api.load(s->api.user, data + off, len - off) : -1;
    mlog("loading host save (%zu bytes) at step %u (rc %d)", len - off, tick, rc);
}

/* ---------------------------------------------------------------- partial resync (lan_v10)
   A guest flagged desynced first asks for a partial resync (dsnet PATCH_POINT): at that step the host sends the state the
   fingerprint covers, the guest changes only what differs, in place, and dsnet takes it back into sync only when the game
   says its four section hashes now equal the host's (api.patch). No pause, no reload: the guest stalls ~one round trip
   and catches up. If the game refuses (too large / not patchable), the proof fails, it takes too long, or a new desync
   comes within 3 fingerprints of a partial one: the full reload (save transfer) as before. */
static void host_patch(mp_session *s) {
    uint8_t *g = NULL; size_t n = 0;
    if (!s->api.snap || s->api.snap(s->api.user, &g, &n) || !g) { mlog("patch point at step %u: no snapshot", s->cur); free(g); return; }
    int rc = dsnet_upload_patch(s->c, g, n);
    free(g);
    mlog("patch point at step %u: sent %zu-byte snapshot (rc %d)", s->cur, n, rc);
}
static int on_patch(uint32_t tick, const uint8_t *data, size_t len, void *user) {
    mp_session *s = user;
    double t0 = now_ms();
    int rc = s->api.patch ? s->api.patch(s->api.user, data, len) : -1;
    if (rc == 0) {
        s->st.partials++;
        s->probation_until = tick + MP_PROBATION_STEPS;
        if (s->api.deep) { s->deep_wait = 1; s->deep_after = tick; }
        mlog("PARTIAL RESYNC at step %u: equal to the host again (%.0f ms after the desync flag, apply %.1f ms)", tick,
             t0 - s->desync_at, now_ms() - t0);
    } else {
        s->st.partial_fails++;
        mlog("partial resync at step %u refused / not equal: full resync next", tick);
    }
    return rc;
}
static void ask_full_resync(mp_session *s, const char *why) {
    s->resync_sent = 1;
    mlog("asking the host for its save (full resync, %s): rc %d", why, dsnet_request_resync(s->c));
}

/* ---------------------------------------------------------------- full-state check (lan_v10, review finding 2)
   Every cfg.full_check_steps steps both peers serialize the world the way the save does (api.full_sections: one hash per
   save section and per second-level key); the host sends its list outside the sim, the guest compares. Covers what the
   fingerprints do not (buildings, storage, tasks, research, events...). A difference is a desync (full reload) and the
   differing sections / keys go to the log and the desync report. */
typedef struct { const char *name; size_t nl; const char *hash; } fline;
static int fl_cmp(const void *a, const void *b) {
    const fline *x = a, *y = b; size_t n = x->nl < y->nl ? x->nl : y->nl;
    int c = memcmp(x->name, y->name, n); return c ? c : (x->nl > y->nl) - (x->nl < y->nl);
}
static int fl_parse(char *t, fline **out) {           /* "name hash" lines (t is modified) */
    int cap = 256, n = 0; fline *v = malloc(sizeof *v * cap);
    for (char *ln = strtok(t, "\n"); ln; ln = strtok(NULL, "\n")) {
        char *sp = strrchr(ln, ' '); if (!sp) continue;
        if (n == cap) { cap *= 2; v = realloc(v, sizeof *v * cap); }
        v[n++] = (fline){ ln, (size_t)(sp - ln), sp + 1 };
    }
    qsort(v, n, sizeof *v, fl_cmp);
    *out = v; return n;
}
/* 0 equal; else the number of differing lines, listed in out (top-level sections first) */
static int full_compare(const uint8_t *host, size_t hn, const uint8_t *mine, size_t mn, char *out, size_t on) {
    char *a = malloc(hn + 1), *b = malloc(mn + 1); memcpy(a, host, hn); a[hn] = 0; memcpy(b, mine, mn); b[mn] = 0;
    fline *x, *y; int nx = fl_parse(a, &x), ny = fl_parse(b, &y), d = 0; size_t o = 0; out[0] = 0;
    for (int pass = 0; pass < 2; pass++) {             /* pass 0: top-level sections, pass 1: "section/key" lines */
        int i = 0, j = 0;
        while (i < nx || j < ny) {
            int c = i >= nx ? 1 : j >= ny ? -1 : fl_cmp(&x[i], &y[j]);
            const fline *f = c <= 0 ? &x[i] : &y[j];
            int sub = memchr(f->name, '/', f->nl) != NULL;
            int differ = c != 0 || strcmp(x[i].hash, y[j].hash);
            if (differ && sub == pass) {
                d++;
                if (o + f->nl + 40 < on) o += (size_t)snprintf(out + o, on - o, "%s%.*s%s", o ? ", " : "", (int)f->nl, f->name,
                                                              c < 0 ? " (host only)" : c > 0 ? " (guest only)" : "");
            }
            if (c <= 0) i++;
            if (c >= 0) j++;
        }
    }
    free(x); free(y); free(a); free(b);
    return d;
}
const char *mp_full_diff(mp_session *s) { return s ? s->full_diff : ""; }
static void full_step(mp_session *s, uint32_t timer) {
    uint8_t *d = NULL; size_t n = 0;
    double t0 = now_ms();
    if (s->api.full_sections(s->api.user, &d, &n) || !d) { mlog("full-state check at step %u: no data", timer); free(d); return; }
    double ms = now_ms() - t0; if (ms > s->st.full_ms_max) s->st.full_ms_max = ms;
    if (dsnet_is_host(s->c)) { dsnet_send_full(s->c, timer, d, n); free(d); mlog("full-state check at step %u: %zu bytes sent (%.0f ms)", timer, n, ms); return; }
    free(s->full_mine); s->full_mine = d; s->full_mine_len = n; s->full_mine_tick = timer;
}
static void full_housekeeping(mp_session *s) {
    uint32_t t; uint8_t *d; size_t n;
    if (dsnet_full_take(s->c, &t, &d, &n)) { free(s->full_host); s->full_host = d; s->full_host_len = n; s->full_host_tick = t; }
    if (!s->full_mine || !s->full_host || s->full_mine_tick != s->full_host_tick) return;
    char diff[2048];
    int nd = full_compare(s->full_host, s->full_host_len, s->full_mine, s->full_mine_len, diff, sizeof diff);
    uint32_t tick = s->full_mine_tick;
    free(s->full_mine); s->full_mine = NULL; free(s->full_host); s->full_host = NULL;
    if (!nd) { s->st.full_ok++; mlog("full-state check at step %u: equal", tick); return; }
    uint32_t fb = 0;
    if (s->desync_seen && dsnet_is_desynced(s->c, &fb, NULL) && fb <= tick) {
        /* the fingerprint already flagged this step (release gate 2026-10-14, desync_partial pos#4: corruption at a check
           step): same incident, its resync (partial first) covers it. Forcing a full resync here left force_full set after
           the partial succeeded, so the NEXT desync skipped the partial resync. */
        mlog("full-state check at step %u differs (%d) during the desync flagged at step %u: left to that resync", tick, nd, fb);
        return;
    }
    s->st.full_fails++;
    snprintf(s->full_diff, sizeof s->full_diff, "step %u: %d difference(s): %s", tick, nd, diff);
    mlog("FULL-STATE CHECK DIFFERS at step %u (fingerprints equal): %s", tick, s->full_diff);
    s->force_full = 1;
    if (dsnet_force_desync(s->c, tick, 0) == 0) mlog("full-state difference: desync flagged, full resync next");
}

/* ---------------------------------------------------------------- leave / rejoin
   Owner 2026-10-13: a disconnect always pauses (every pause mode) and the host chooses "Wait for <name>" or "Keep playing".
   The host sends dsnet's HOLD (reason PLAYER_LEFT, the leaver's uid): 0 game ticks while any hold is on, on every peer;
   it ends by itself when that uid rejoins. "Keep playing" = HOLD off + TEMP_CONTROL on (the host may order his RESERVED
   units until he is back). The popup list is dsnet's hold list, so it is the same everywhere. */
static void player_join(mp_session *s, const dsnet_cmd *cmd) {
    int p = cmd->player;
    if (p < DSNET_MAX_PLAYERS && cmd->len >= 16) {
        memcpy(s->slot_uid[p], cmd->payload, 16);
        int nl = cmd->len - 16 > 31 ? 31 : cmd->len - 16;
        memcpy(s->slot_name[p], cmd->payload + 16, (size_t)nl); s->slot_name[p][nl] = 0;
        s->slot_known |= 1u << p;
    }
    mlog("player %d (%s) joined (step %u)", p, p < DSNET_MAX_PLAYERS ? s->slot_name[p] : "?", cmd->tick);
}
static void player_leave(mp_session *s, const dsnet_cmd *cmd) {
    int p = cmd->player;
    mlog("player %d (%s) left (step %u)", p, p < DSNET_MAX_PLAYERS ? s->slot_name[p] : "?", cmd->tick);
    if (!dsnet_is_host(s->c) || p == dsnet_player_id(s->c) || p >= DSNET_MAX_PLAYERS || !(s->slot_known >> p & 1)) return;
    s->hold_waiting = 0;
    int rc = dsnet_hold(s->c, 1, DSNET_HOLD_PLAYER_LEFT, s->slot_uid[p]);
    mlog("hold: waiting for %s (rc %d)", s->slot_name[p], rc);
}
int mp_left_players(mp_session *s, int *ids, char (*names)[32], int max) {
    int n = 0, cnt = dsnet_hold_count(s->c);
    for (int i = 0; i < cnt && n < max; i++) {
        int reason; uint8_t uid[16]; char nm[32] = "";
        if (dsnet_hold_get(s->c, i, &reason, uid, nm) < 0 || reason != DSNET_HOLD_PLAYER_LEFT) continue;
        if (ids) ids[n] = i;
        if (names) snprintf(names[n], 32, "%s", nm[0] ? nm : "A player");
        n++;
    }
    return n;
}
void mp_left_choice(mp_session *s, int keep_playing) {
    if (!dsnet_is_host(s->c)) return;
    if (!keep_playing) { s->hold_waiting = 1; mlog("waiting for the players who left"); return; }
    int cnt = dsnet_hold_count(s->c);
    uint8_t uids[8][16]; int n = 0;
    for (int i = 0; i < cnt && n < 8; i++) {
        int reason; char nm[32];
        if (dsnet_hold_get(s->c, i, &reason, uids[n], nm) == 0 && reason == DSNET_HOLD_PLAYER_LEFT) n++;
    }
    for (int i = 0; i < n; i++) { dsnet_set_temp_control(s->c, uids[i], 1); dsnet_hold(s->c, 0, DSNET_HOLD_PLAYER_LEFT, uids[i]); }
    s->hold_waiting = 0;
    mlog("keep playing: %d hold(s) off, temporary control on", n);
}
int mp_left_waiting(mp_session *s) { return s->hold_waiting && mp_left_players(s, NULL, NULL, 8) > 0; }

/* ---------------------------------------------------------------- commands of a step */
static void on_cmd(const dsnet_cmd *cmd, void *user) {
    mp_session *s = user;
    switch (cmd->type) {
    case DSNET_GAME_ORDER: run_order(s, cmd); s->step_orders++; break;
    case DSNET_JOIN_POINT: if (dsnet_is_host(s->c)) host_upload(s); break;
    case DSNET_PATCH_POINT: if (dsnet_is_host(s->c)) host_patch(s); break;   /* guests: dsnet stops after it */
    case DSNET_PAUSE_ALL:
        if (s->cfg.auto_resume) s->resume_pending = 1;
        mlog("auto pause at step %u", cmd->tick);
        break;
    case DSNET_PLAYER_JOIN: player_join(s, cmd); break;
    case DSNET_PLAYER_LEAVE: player_leave(s, cmd); break;
    case DSNET_EXPEDITION_START: {   /* everyone ready (Teams screen, ui_teams.c): same step on every peer */
        static int (*start)(const uint8_t *, size_t); static int looked;
        if (!looked) { looked = 1; start = (int (*)(const uint8_t *, size_t))dlsym(RTLD_DEFAULT, "ui_teams_expedition_start"); }
        mlog("expedition start at step %u (%u bytes)%s", cmd->tick, cmd->len, start ? "" : " - no ui_teams in this build");
        if (start) start(cmd->payload, cmd->len);
        break;
    }
    default: break;            /* votes and ownership: dsnet keeps that state */
    }
}

int mp_ticks_to_run_now(mp_session *s, float frame_ms) {
    if (!s || !s->c) return 0;
    if (frame_ms <= 0) { double t = now_ms(); frame_ms = s->last_frame ? (float)(t - s->last_frame) : 16.7f; s->last_frame = t; }
    dsnet_can_run_tick(s->c, s->timer);                 /* runs a pending save load (on_load) */
    if (s->loading) {
        if (!s->api.ready || !s->api.ready(s->api.user)) return 0;
        s->loading = 0;
        s->timer = dsnet_timer(s->c);
        if (s->api.show_speed) s->api.show_speed(s->api.user, s->shown_speed, s->shown_paused);
        mlog("load done, continuing at step %u", s->timer);
    }
    if (s->api.ready && !s->api.ready(s->api.user)) return 0;
    dsnet_report_frame_time(s->c, frame_ms);
    /* Smooth pacing (2026-10-13, guest jitter): dsnet's own pacer gave the guest 0 or 2 steps in ~19 % of
       frames at 60 fps (host 8 %), and the unit view snaps per step. Here: a wall-clock step accumulator with a
       gentle rate trim from the buffer size (behind = steps the server already allows), carry capped at 2 steps.
       dsnet still decides how far we may go (never past tickUntil); catch-up after a load runs in bursts. */
    uint32_t until = dsnet_tick_until(s->c);
    int behind = (int)(until - s->timer);
    if (behind <= 0) { s->pace_acc = 0; return 0; }
    if (behind > 30) return behind > 200 ? 200 : behind;               /* catching up (load, long stall) */
    float step = s->cfg.tick_ms > 0 ? s->cfg.tick_ms : 1000.0f / 60.0f;
    float trim = behind >= 8 ? 1.15f : behind <= 2 ? 0.9f : 1.0f;      /* keep a 3-7 step buffer */
    s->pace_acc += (frame_ms > 250 ? 250 : frame_ms) * trim;
    int n = (int)(s->pace_acc / step);
    if (n > behind) n = behind;
    s->pace_acc -= n * step;
    if (s->pace_acc > 2 * step) s->pace_acc = 2 * step;
    return n;
}

int mp_apply_cmds(mp_session *s, uint32_t timer) {
    if (!dsnet_can_run_tick(s->c, timer)) return -1;
    s->cur = timer; s->applying = 1; s->step_orders = 0;
    int n = dsnet_take_cmds_for_tick(s->c, timer, on_cmd, s);
    s->applying = 0;
    if (s->loading) return n;                            /* (never: loads arrive in can_run_tick) */
    if (dsnet_patch_state(s->c) == 2) return -2;         /* stopped at our PATCH_POINT: the rest of the step after the patch */
    int v = dsnet_ticks_per_step(s->c);                  /* 0 paused, else lowest GameSpeed vote */
    int paused = v == 0, speed = v < 1 ? (s->shown_speed ? s->shown_speed : 1) : v > MP_SPEED_FAST ? MP_SPEED_FAST : v;   /* VeryFast is not in play */
    if (!paused) { s->half_acc += half_per_step[speed]; s->step_ticks = s->half_acc / 2; s->half_acc %= 2; }
    else s->step_ticks = 0;
    if (speed != s->shown_speed || paused != s->shown_paused) {
        s->shown_speed = speed; s->shown_paused = paused;
        if (s->api.show_speed) s->api.show_speed(s->api.user, speed, paused);
    }
    return n;
}

int mp_step_ticks(mp_session *s) { return s->step_ticks; }

void mp_step_done(mp_session *s, uint32_t timer) {
    uint32_t iv = dsnet_fp_interval(s->c);
    if (s->api.fingerprint4) {                          /* dsnet compares every fp_interval-th step (with the sections) */
        uint64_t h[4]; s->api.fingerprint4(s->api.user, h);
        uint64_t deep = s->api.deep && iv && timer % (iv * MP_DEEP_EVERY) == 0 ? s->api.deep(s->api.user) | 1 : 0;
        dsnet_report_fingerprint_deep(s->c, timer, h[0], h + 1, deep);
    } else if (s->api.fingerprint) dsnet_report_fingerprint(s->c, timer, s->api.fingerprint(s->api.user));
    if (s->api.fp_step && iv && timer % iv == 0) s->api.fp_step(s->api.user, timer);   /* snapshot for a desync report */
    if (s->api.full_sections && s->cfg.full_check_steps > 0 && timer && timer % (uint32_t)s->cfg.full_check_steps == 0) full_step(s, timer);
    s->timer = timer + 1;
}

static void housekeeping(mp_session *s) {
    uint32_t bad, good;
    if (dsnet_is_desynced(s->c, &bad, &good)) {
        if (!s->desync_seen) {
            s->desync_seen = 1; s->desync_at = now_ms(); s->st.desyncs++;
            dsnet_desync_info di; memset(&di, 0, sizeof di); dsnet_desync_get(s->c, &di);
            mlog("DESYNC: fingerprint differs at step %u (last good %u) at step %u; sections units %s positions %s rng %s owners %s",
                 bad, good, s->timer, di.mine[1] == di.host[1] ? "ok" : "DIFF", di.mine[2] == di.host[2] ? "ok" : "DIFF",
                 di.mine[3] == di.host[3] ? "ok" : "DIFF", di.mine[4] == di.host[4] ? "ok" : "DIFF");
            if (s->api.report_part) {                    /* desync report: this guest's part goes to the host (outside the sim) */
                uint8_t *rp = NULL; size_t rn = 0;
                if (s->api.report_part(s->api.user, &di, &rp, &rn) == 0 && rp) {
                    int rc = dsnet_send_report(s->c, rp, rn);
                    if (!rc) s->st.reports_sent++;
                    mlog("desync report part sent to the host (%zu bytes, rc %d)", rn, rc);
                }
                free(rp);
            }
            if (s->force_full) mlog("the deep check failed: full resync");
            else if (s->cfg.auto_resync && s->cfg.partial_resync && s->api.patch && bad >= s->probation_until) {
                s->patch_asked = 1; s->patch_at = now_ms();
                mlog("asking for a partial resync: rc %d", dsnet_request_patch(s->c));
            } else if (s->cfg.auto_resync && s->cfg.partial_resync && s->api.patch)
                mlog("desync again within %d steps of a partial resync: full resync", MP_PROBATION_STEPS);
        }
        if (s->patch_asked) {
            int ps = dsnet_patch_state(s->c);
            if (ps == 0) { s->patch_asked = 0; ask_full_resync(s, "the partial resync failed"); }
            else if (now_ms() - s->patch_at > MP_PATCH_TIMEOUT_MS) {
                dsnet_patch_abort(s->c); s->patch_asked = 0; s->st.partial_fails++;
                ask_full_resync(s, "the partial resync took too long");
            }
        } else if (s->cfg.auto_resync && !s->resync_sent && now_ms() - s->desync_at >= s->cfg.resync_delay_ms)
            ask_full_resync(s, "resync");
    } else if (s->desync_seen && !s->loading && !dsnet_needs_state(s->c)) {   /* back in sync without a load: partial */
        s->desync_seen = 0; s->patch_asked = 0; s->resync_sent = 0;
    }
    if (!dsnet_is_host(s->c) && s->api.full_sections) full_housekeeping(s);
    if (!dsnet_is_host(s->c) && s->api.deep) {           /* the deep check (lan_v10): within ~2 s of each partial resync */
        uint32_t checks, fails, last_ok, last_bad, iv = dsnet_fp_interval(s->c); if (!iv) iv = 30;
        dsnet_deep_status(s->c, &checks, &fails, &last_ok, &last_bad);
        if (s->deep_wait && last_bad > s->deep_after) {
            s->deep_wait = 0; s->st.deep_fails++; s->force_full = 1; s->deep_bad_seen = last_bad;
            mlog("DEEP CHECK FAILED at step %u after the partial resync at step %u: full resync", last_bad, s->deep_after);
            if (dsnet_force_desync(s->c, last_bad, 0) && !s->resync_sent) ask_full_resync(s, "deep check failed");
        } else if (s->deep_wait && last_ok > s->deep_after) {
            s->deep_wait = 0; s->st.deep_ok++;
            mlog("deep check equal at step %u (partial resync at step %u)", last_ok, s->deep_after);
        } else if (s->deep_wait && s->timer > s->deep_after + 3 * MP_DEEP_EVERY * iv) {
            s->deep_wait = 0; s->st.deep_missing++;
            mlog("deep check after the partial resync at step %u: none compared by step %u", s->deep_after, s->timer);
        }
        if (last_bad > s->deep_bad_seen) {                /* outside a partial resync's window: a real difference the fingerprint
                                                             does not see (yet), e.g. a body-part hp the cached total hides: a desync */
            s->st.deep_other++; s->deep_bad_seen = last_bad;
            int recent = last_bad < s->probation_until;   /* right after a partial resync: not a second partial */
            if (recent) s->force_full = 1;
            int rc = dsnet_force_desync(s->c, last_bad, !recent);
            mlog("deep hash differs at step %u while the fingerprint is equal: desync flagged (rc %d)", last_bad, rc);
        }
    }
    if (dsnet_is_host(s->c) && s->api.report_save) {     /* a guest's desync report part arrived: write the report */
        uint8_t *rp; size_t rn; int from;
        while (dsnet_report_take(s->c, &rp, &rn, &from)) {
            s->api.report_save(s->api.user, from, rp, rn);
            s->st.reports_saved++;
            free(rp);
        }
    }
    if (s->resume_pending && !s->loading && !dsnet_is_desynced(s->c, NULL, NULL) && !dsnet_needs_state(s->c)) {
        s->resume_pending = 0;
        if (!s->user_pause && !s->popup_voted) { uint8_t v = 0; dsnet_submit(s->c, DSNET_PAUSE_VOTE, 0, &v, 1); }
    }
}

/* ready() is checked before every world tick: a multi-frame flow (map change, floor generation) that starts
   inside a tick holds the rest of the step until it is done (transitions design 3.3). */
static int run_ticks(mp_session *s, uint32_t t, int k) {
    for (; k > 0; k--) {
        if (s->api.ready && !s->api.ready(s->api.user)) { s->mid_ticks = k; s->mid_timer = t; return 0; }
        s->api.sim_tick(s->api.user); s->st.ticks++;
    }
    s->mid_ticks = 0;
    mp_step_done(s, t);
    return 1;
}

int mp_frame(mp_session *s, float frame_ms) {
    if (!s || !s->c) return 0;
    if (s->mid_ticks > 0) {                              /* finish the held step first */
        if (!run_ticks(s, s->mid_timer, s->mid_ticks)) { housekeeping(s); return 0; }
    }
    int n = mp_ticks_to_run_now(s, frame_ms), done = 0;
    for (int i = 0; i < n; i++) {
        uint32_t t = s->timer;
        /* a flow that started in the previous step's last tick: hold BEFORE this step's commands, or a join point
           (host save + reload) or an order would run in the middle of a map transition (lead bug 2026-10-12) */
        if (s->api.ready && !s->api.ready(s->api.user)) break;
        if (mp_apply_cmds(s, t) < 0) break;
        if (!run_ticks(s, t, s->step_ticks)) break;
        done++;
        /* A paused step (0 ticks) that applied a game order ends this frame's steps. Its follow-up (a map change:
           CampaignLoadingEventHandler, started by the frame's EventQueue.Update after this loop) runs in no sim tick,
           so ready() only drops on the NEXT frame. Without this stop a 10 fps peer ran more 0-tick steps (each one
           fingerprinted) before its transition began than a 30 fps peer: the same step hashed the old map on one peer
           and the new one on the other = a false DESYNC right after the expedition start (2026-10-14). */
        if (s->step_orders && s->step_ticks == 0) break;
    }
    housekeeping(s);
    return done;
}

/* ---------------------------------------------------------------- UI side */
int mp_order(mp_session *s, uint16_t type, const uint8_t guid[16], const void *args, uint16_t len) {
    if (!s || len > DSNET_MAX_PAYLOAD - MP_ORDER_HDR) return -1;
    uint8_t b[MP_ORDER_HDR + 256], *p = len <= 256 ? b : malloc(MP_ORDER_HDR + len);
    p[0] = (uint8_t)type; p[1] = (uint8_t)(type >> 8);
    memcpy(p + 2, guid, 16);
    p[18] = (uint8_t)len; p[19] = (uint8_t)(len >> 8);
    if (len) memcpy(p + MP_ORDER_HDR, args, len);
    int rc = dsnet_submit(s->c, DSNET_GAME_ORDER, 0, p, (uint16_t)(MP_ORDER_HDR + len));
    if (p != b) free(p);
    return rc;
}
int mp_order_move(mp_session *s, const uint8_t guid[16], int32_t x, int32_t y) {
    uint8_t a[8]; memcpy(a, &x, 4); memcpy(a + 4, &y, 4);
    return mp_order(s, MP_ORDER_MOVE, guid, a, 8);
}
const char *mp_speed_name(int game_speed, int paused) {
    if (paused || game_speed == MP_SPEED_PAUSE) return "paused";
    return game_speed == MP_SPEED_NORMAL ? "normal" : "fast 3.5x";   /* 2 and the clamped 3 */
}
int mp_vote_speed(mp_session *s, int game_speed) {
    if (game_speed < 1 || game_speed > 3) return -1;
    if (game_speed > MP_SPEED_FAST) game_speed = MP_SPEED_FAST;      /* VeryFast is not a player option */
    uint8_t v = (uint8_t)game_speed;
    return dsnet_submit(s->c, DSNET_SPEED_VOTE, 0, &v, 1);
}
static int send_pause(mp_session *s, int pause) { uint8_t v = pause ? 1 : 0; return dsnet_submit(s->c, DSNET_PAUSE_VOTE, 0, &v, 1); }
int mp_vote_pause(mp_session *s, int pause) {
    s->user_pause = pause ? 1 : 0;
    s->resume_pending = 0;
    if (!pause && dsnet_is_host(s->c) && mp_left_players(s, NULL, NULL, 8)) mp_left_choice(s, 1);   /* host pressed play = Keep playing */
    if (!pause && s->popup_voted) return 0;              /* a popup still holds the pause */
    return send_pause(s, pause);
}
int mp_toggle_pause(mp_session *s) { return mp_vote_pause(s, !s->user_pause); }
int mp_popup_pause(mp_session *s, int reason, int add) {
    if (reason != MP_PAUSE_ESSENTIAL_POPUP && reason != MP_PAUSE_POPUP_TIMELOCK && reason != MP_PAUSE_CONSECRATION) return 0;
    s->popups[reason] += add ? 1 : -1;
    if (s->popups[reason] < 0) s->popups[reason] = 0;
    if (!dsnet_allow_pause(s->c)) return 0;              /* lobby: popups never pause */
    int open = 0;
    for (int i = 0; i < 6; i++) open += s->popups[i];
    if (open && !s->popup_voted) { s->popup_voted = 1; if (!s->user_pause) send_pause(s, 1); return 1; }
    if (!open && s->popup_voted) { s->popup_voted = 0; if (!s->user_pause) send_pause(s, 0); return 1; }
    return 0;
}
static int ownership(mp_session *s, const uint8_t guid[16], int owner) {
    uint8_t b[17]; memcpy(b, guid, 16); b[16] = (uint8_t)owner;
    return dsnet_submit(s->c, DSNET_OWNERSHIP, 0, b, 17);
}
int mp_claim(mp_session *s, const uint8_t guid[16]) { return ownership(s, guid, dsnet_player_id(s->c)); }
int mp_release(mp_session *s, const uint8_t guid[16]) { return ownership(s, guid, DSNET_NO_PLAYER); }
int mp_assign(mp_session *s, const uint8_t guid[16], int player) { return ownership(s, guid, player); }

void mp_set_order_handler(mp_session *s, uint16_t type, mp_order_fn fn) { if (type < MP_ORDER_MAX) s->handlers[type] = fn; }
const mp_game_api *mp_api(mp_session *s) { return &s->api; }
int mp_allow_pause(mp_session *s) { return dsnet_allow_pause(s->c); }
int mp_allow_speedup(mp_session *s) { return dsnet_allow_speedup(s->c); }
int mp_is_applying(mp_session *s) { return s && s->applying; }
uint32_t mp_cur_step(mp_session *s) { return s->applying ? s->cur : s->timer; }
dsnet_client *mp_client(mp_session *s) { return s->c; }
uint16_t mp_server_port(mp_session *s) { return s->srv ? dsnet_server_port(s->srv) : 0; }

void mp_get_stats(mp_session *s, mp_stats *o) {
    *o = s->st;
    o->timer = s->timer;
    o->desynced = dsnet_is_desynced(s->c, NULL, NULL);
    o->needs_state = dsnet_needs_state(s->c);
    o->loading = s->loading;
    o->paused = s->shown_paused; o->speed = s->shown_speed;
    o->player = dsnet_player_id(s->c);
}

/* ---------------------------------------------------------------- session */
mp_session *mp_start(const mp_config *cfg, const mp_game_api *api, int *reject) {
    if (reject) *reject = 0;
    mp_session *s = calloc(1, sizeof *s);
    s->cfg = *cfg; s->api = *api;
    s->handlers[MP_ORDER_MOVE] = order_move_default;
    s->shown_speed = 1;
    dsnet_set_identity(MP_BUILD_ID, MP_MOD_VERSION);
    {   /* launcher session: gameplay mod set + match rule in the HELLO; the host's server refuses a joiner that differs */
        int van; char ms[65]; static char mods[DSNET_MODLINES_MAX];
        int rc = mp_session_mods(&van, ms, mods, sizeof mods);
        dsnet_set_modset(van ? DSNET_MATCH_VANILLA : DSNET_MATCH_MODS, ms, mods);
        if (!rc) { int nm = 0; for (const char *q = mods; *q; q++) nm += *q == '\n'; if (mods[0]) nm++;
                   mlog("launcher session: rule %s, %d gameplay mod(s), set %.12s", van ? "vanilla" : "mods", nm, ms[0] ? ms : "-"); }
    }
    if (cfg->host) {
        dsnet_cfg d = dsnet_cfg_defaults();
        d.port = cfg->port;
        d.allow_speedup = cfg->allow_speedup; d.allow_pause = cfg->allow_pause; d.pause_mode = cfg->pause_mode; d.gear_mode = cfg->gear_mode;
        d.shared_fog = cfg->shared_fog;
        if (cfg->min_players > 0) d.min_players = cfg->min_players;
        if (cfg->tick_ms > 0) { d.tick_ms = cfg->tick_ms; d.net_ms = cfg->tick_ms * 2; }
        if (cfg->fp_interval > 0) d.fp_interval = cfg->fp_interval;
        d.build_id = MP_BUILD_ID; d.mod_version = MP_MOD_VERSION;
        s->srv = dsnet_server_start(&d);
        if (!s->srv) { mlog("host: cannot listen on port %u", cfg->port); free(s); return NULL; }
        s->c = dsnet_client_connect_local(s->srv, cfg->name);
        mlog("host: listening on port %u", dsnet_server_port(s->srv));
    } else {
        s->c = dsnet_client_connect_tcp(cfg->host_addr, cfg->port, cfg->name);
        if (!s->c) { mlog("client: cannot connect to %s:%u", cfg->host_addr, cfg->port); free(s); return NULL; }
    }
    if (!s->c) { mp_stop(s); return NULL; }
    dsnet_set_load_cb(s->c, on_load, s);
    dsnet_set_patch_cb(s->c, on_patch, s);
    dsnet_set_partial_resync(s->c, cfg->partial_resync && cfg->auto_resync && api->patch);
    int w = dsnet_wait_welcome(s->c, cfg->connect_timeout_ms > 0 ? cfg->connect_timeout_ms : 5000);
    if (w) {
        int why = dsnet_reject_reason(s->c);
        if (reject) *reject = why;
        mlog("join %s (reject reason %d)", w == -2 ? "refused" : "timed out", why);
        mp_stop(s); return NULL;
    }
    if (!cfg->host && cfg->expect_world[0]) {          /* F17: a Recent worlds row must not land in another world */
        uint8_t id[16]; char nm[32];
        if (dsnet_host_world(s->c, id, nm) == 0) {
            char gs[40]; static const char hx[] = "0123456789abcdef"; int k = 0;
            /* System.Guid text order: Data1 (4 bytes LE), Data2, Data3 (2 LE each), then 8 bytes as they are */
            static const int ord[16] = { 3, 2, 1, 0, -1, 5, 4, -1, 7, 6, -1, 8, 9, -1, 10, 11 };
            for (int i = 0; i < 16; i++) { if (ord[i] < 0) { gs[k++] = '-'; continue; } gs[k++] = hx[id[ord[i]] >> 4]; gs[k++] = hx[id[ord[i]] & 15]; }
            for (int i = 12; i < 16; i++) { gs[k++] = hx[id[i] >> 4]; gs[k++] = hx[id[i] & 15]; }
            gs[k] = 0;
            if (strcasecmp(gs, cfg->expect_world)) {
                snprintf(other_world, sizeof other_world, "%s", nm);
                mlog("join refused here: the host runs world %s (%s), this join was for %s", gs, nm, cfg->expect_world);
                if (reject) *reject = DSNET_REJ_OTHER_WORLD;
                mp_stop(s); return NULL;
            }
        }
    }
    if (!cfg->host && cfg->need_save && !dsnet_needs_state(s->c)) {   /* joined before the timer moved: dsnet sends
                                                                         no join point, but this player has no campaign */
        int rc = dsnet_request_resync(s->c);
        mlog("no campaign here: asking the host for its save (rc %d)", rc);
    }
    s->timer = dsnet_timer(s->c);
    mlog("%s: player %d, allow_speedup %d, allow_pause %d, %s", cfg->host ? "host" : "client", dsnet_player_id(s->c),
         dsnet_allow_speedup(s->c), dsnet_allow_pause(s->c), dsnet_needs_state(s->c) ? "waiting for the host's save" : "starting at step 0");
    return s;
}

void mp_stop(mp_session *s) {
    if (!s) return;
    if (s->c) dsnet_client_close(s->c);
    if (s->srv) dsnet_server_stop(s->srv);
    free(s->full_mine); free(s->full_host);
    free(s);
}
