// dsl_evreg.h - the registry and dispatch of the loader's EVENTS (SDK API 7: on_event / off_event; owner 2026-11-02,
// PLAN_v013 items 3 + 32, tModLoader study idea 1). ONE implementation: included by src/dsl_events.c (the game glue:
// hook sites, the kit tick) and by the host test test/events/ev_test.c. No game code here.
//
// The loader owns ONE game hook per event and calls every module that subscribed, in subscription order (= load order).
// tModLoader's shape (HookList: only the mods that listen are called, no allocation per call), in C:
//   - a subscription = (module, type, flags, class filter, callback, ud); the same one twice is refused;
//   - a module that failed or was turned off gets no more calls: its state is asked at each call (evr_running);
//   - re-entrant: a callback may subscribe, unsubscribe or cause another event (a window it opens): a nested send walks
//     the list as it is then; an unsubscribe only marks the entry, the list is compacted when no send runs;
//   - frame filter: DSL_EVF_TITLE = only while the MainFlow scene is the title, DSL_EVF_WORLD = only in a world, both = in
//     either; never while a scene or map loads (ev->loading); none = every frame; view filter: the view's class is
//     `klass` or derives from it.
// The includer defines, BEFORE including this file:
//   static int evr_running(DSLPlugin *self);              1 = the module still runs (NULL = the loader itself)
//   static int evr_class_is(void *view_klass, void *want); 1 = view_klass is want or derives from it
//   static const char *evr_name(DSLPlugin *self);         the module id for log lines
//   static void evr_log(const char *fmt, ...);            a dsload.log line
//   static uint64_t evr_clock_ns(void);                   a monotonic clock in ns (the time meter; read only when evr_meter)
#ifndef DSL_EVREG_H
#define DSL_EVREG_H
#include "../sdk/dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EVR_MAX 256
typedef struct { DSLPlugin *self; int type, flags; void *klass; dsl_event_cb cb; void *ud; int on; } EvrSub;
static EvrSub EVR[EVR_MAX];
static int evr_n, evr_depth, evr_dirty;
static unsigned evr_calls[DSL_EV_LAST + 1];                     /* callbacks run, per type (test lines) */

static const char *evr_type_name(int t) {
    static const char *const n[] = { "?", "frame", "scene", "world loaded", "world closed", "view created", "view shown",
                                     "view hidden", "language" };
    return t > 0 && t <= DSL_EV_LAST ? n[t] : "?";
}
/* ------------------------------------------------------------------ the per-mod time meter (tModLoader study idea 2)
   Test copies only (the includer sets evr_meter = 1; a player's copy never reads the clock here). The clock is read
   before and after each callback; the time is EXCLUSIVE: the callbacks of an event a callback caused (a window it
   opened) are charged to their own modules, not to it. evr_meter_tick(now, state) once a frame, before the frame's
   events: state 0 = loading (measured, never judged), 1 = the title, 2 = a world. A module whose callbacks take more
   than EVR_SLOW_MS_S ms per second over one EVR_WIN_S window (after the state's first EVR_WARM_S) logs once per state
   "SLOW-MOD <id> <ms per s> <state> (...)": the tests fail on it (loader/test/events_test.py, scan_rate_test.py).
   Each state's end, and every EVR_SUM_S in a long state, logs
   "modtime: <state> <secs> s: <id> <ms per s> ms/s (<calls> calls, max <ms> ms), ..." ("none" = no callback ran).
   WHY 20 ms per s: 2 % of the main thread; a frame callback reaches it at 0.33 ms a frame at 60 fps. */
#define EVR_SLOW_MS_S 20.0
#define EVR_WIN_S 10.0
#define EVR_WARM_S 5.0
#define EVR_SUM_S 30.0
#define EVM_MAX 40
typedef struct { DSLPlugin *self; uint64_t win_ns, sum_ns, max_ns; unsigned sum_calls; int flagged; } EvrMeter;
static EvrMeter EVM[EVM_MAX];
static int evm_n, evr_meter, evm_state = -1;
static uint64_t evm_child, evm_t_win, evm_t_sum;
static unsigned evm_slow;                                       /* SLOW-MOD lines logged (test lines) */
static const char *const evm_state_name[] = { "loading", "title", "world" };
static EvrMeter *evm_of(DSLPlugin *self) {
    for (int i = 0; i < evm_n; i++) if (EVM[i].self == self) return &EVM[i];
    if (evm_n >= EVM_MAX) return NULL;
    EvrMeter *m = &EVM[evm_n++]; memset(m, 0, sizeof *m); m->self = self;
    return m;
}
static void evm_call(DSLPlugin *who, dsl_event_cb cb, void *ud, const DSLEvent *ev) {
    uint64_t saved = evm_child; evm_child = 0;                  /* the time of the events this callback causes */
    uint64_t t0 = evr_clock_ns();
    cb(ud, ev);
    uint64_t dt = evr_clock_ns() - t0, own = dt > evm_child ? dt - evm_child : 0;
    evm_child = saved + dt;
    EvrMeter *m = evm_of(who); if (!m) return;
    m->win_ns += own; m->sum_ns += own; m->sum_calls++;
    if (own > m->max_ns) m->max_ns = own;
}
static void evm_summary(uint64_t now) {
    char b[1200]; size_t o = 0; double secs = (double)(now - evm_t_sum) / 1e9;
    b[0] = 0;
    for (int i = 0; i < evm_n; i++) {
        EvrMeter *m = &EVM[i];
        if (!m->sum_calls) continue;
        if (o + 80 < sizeof b)
            o += (size_t)snprintf(b + o, sizeof b - o, "%s%s %.2f ms/s (%u calls, max %.2f ms)", o ? ", " : "", evr_name(m->self),
                                  secs > 0 ? (double)m->sum_ns / 1e6 / secs : 0.0, m->sum_calls, (double)m->max_ns / 1e6);
        m->sum_ns = m->max_ns = 0; m->sum_calls = 0;
    }
    evr_log("modtime: %s %.1f s: %s", evm_state_name[evm_state], secs, o ? b : "none");
    evm_t_sum = now;
}
static void evr_meter_tick(uint64_t now, int state) {
    if (!evr_meter) return;
    if (state < 0 || state > 2) state = 0;
    if (state != evm_state) {
        if (evm_state >= 0) evm_summary(now);
        for (int i = 0; i < evm_n; i++) { EVM[i].win_ns = 0; EVM[i].flagged = 0; }
        evm_state = state; evm_t_sum = now; evm_t_win = now + (uint64_t)(EVR_WARM_S * 1e9);
        return;
    }
    if (now - evm_t_sum >= (uint64_t)(EVR_SUM_S * 1e9)) evm_summary(now);
    if (now <= evm_t_win) { for (int i = 0; i < evm_n; i++) EVM[i].win_ns = 0; return; }   /* the state's first seconds */
    double w = (double)(now - evm_t_win) / 1e9;
    if (w < EVR_WIN_S) return;
    for (int i = 0; i < evm_n; i++) {
        EvrMeter *m = &EVM[i]; double r = (double)m->win_ns / 1e6 / w;
        if (state && r > EVR_SLOW_MS_S && !m->flagged) {
            m->flagged = 1; evm_slow++;
            evr_log("SLOW-MOD %s %.0f %s (%.1f ms of event callbacks per second over %.0f s, limit %.0f; max one call %.2f ms): "
                    "an event callback must be cheap; move the work out of the frame or do it once",
                    evr_name(m->self), r, evm_state_name[state], r, w, EVR_SLOW_MS_S, (double)m->max_ns / 1e6);
        }
        m->win_ns = 0;
    }
    evm_t_win = now;
}
static void evr_compact(void) {
    if (evr_depth || !evr_dirty) return;
    int k = 0;
    for (int i = 0; i < evr_n; i++) if (EVR[i].on) EVR[k++] = EVR[i];
    evr_n = k; evr_dirty = 0;
}
/* on_event: 0 = subscribed; -1 = refused (logged): unknown type, no callback, the same subscription again, list full */
static int evr_add(DSLPlugin *self, int type, int flags, void *klass, dsl_event_cb cb, void *ud) {
    if (type < 1 || type > DSL_EV_LAST || !cb) { evr_log("events: %s: subscription refused (type %d%s)", evr_name(self), type, cb ? "" : ", no callback"); return -1; }
    for (int i = 0; i < evr_n; i++)
        if (EVR[i].on && EVR[i].self == self && EVR[i].type == type && EVR[i].cb == cb && EVR[i].ud == ud) {
            evr_log("events: %s: already listens to %s with this callback: refused", evr_name(self), evr_type_name(type)); return -1; }
    evr_compact();
    if (evr_n >= EVR_MAX) { evr_log("events: %s: subscription refused: %d subscriptions in all", evr_name(self), EVR_MAX); return -1; }
    EVR[evr_n++] = (EvrSub){ self, type, flags, klass, cb, ud, 1 };
    evr_log("events: %s listens to %s%s%s", evr_name(self), evr_type_name(type),
            type == DSL_EV_FRAME ? ((flags & 3) == 1 ? " (title)" : (flags & 3) == 2 ? " (world)" : (flags & 3) == 3 ? " (title or world)" : " (every frame)") : "",
            klass ? " (one class)" : "");
    return 0;
}
/* off_event: cb NULL = every subscription of that module to that type (type 0 = to every type); returns how many */
static int evr_remove(DSLPlugin *self, int type, dsl_event_cb cb, void *ud) {
    int n = 0;
    for (int i = 0; i < evr_n; i++) {
        EvrSub *s = &EVR[i];
        if (!s->on || s->self != self || (type && s->type != type) || (cb && (s->cb != cb || s->ud != ud))) continue;
        s->on = 0; evr_dirty = 1; n++;
    }
    evr_compact();
    return n;
}
/* send one event to every module that listens (ev->type set). The list as it is now: a subscription made inside a
   callback gets the next event, one removed inside a callback gets no more. */
static void evr_send(const DSLEvent *ev) {
    int t = ev->type, n = evr_n;
    evr_depth++;
    for (int i = 0; i < n && i < evr_n; i++) {
        EvrSub *s = &EVR[i];
        if (!s->on || s->type != t) continue;
        if (t == DSL_EV_FRAME && (s->flags & 3)) {
            int want = !ev->loading && (((s->flags & DSL_EVF_TITLE) && ev->scene == 1) || ((s->flags & DSL_EVF_WORLD) && ev->scene == 2));
            if (!want) continue;
        }
        if (s->klass && (t == DSL_EV_VIEW_CREATED || t == DSL_EV_VIEW_SHOWN || t == DSL_EV_VIEW_HIDDEN) && !evr_class_is(ev->klass, s->klass)) continue;
        if (!evr_running(s->self)) continue;
        evr_calls[t]++;
        if (evr_meter) evm_call(s->self, s->cb, s->ud, ev);
        else s->cb(s->ud, ev);
    }
    evr_depth--;
    evr_compact();
}
#endif
