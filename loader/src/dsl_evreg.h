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
#ifndef DSL_EVREG_H
#define DSL_EVREG_H
#include "../sdk/dsl_sdk.h"
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
        s->cb(s->ud, ev);
    }
    evr_depth--;
    evr_compact();
}
#endif
