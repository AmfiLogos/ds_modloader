// ev_test.c - host test of the loader's event registry (src/dsl_evreg.h, SDK API 7), no game. Run by test/run_tests.sh.
// The real breaks it catches: a module that failed or was turned off still gets calls (its code may be half set up: a
// crash in every player's game); a callback that unsubscribes (one-shot) or subscribes during a send makes the
// dispatcher skip another module's call, call a removed one, or loop; the title / world frame filter or the class
// filter sends events to the wrong modules; the same subscription twice doubles the calls.
// The time meter (test copies): a mod whose event callbacks are slow is not named (SLOW-MOD, the guard of every rig test
// goes blind), a fast mod is named, or a mod is charged for the callbacks of an event it caused (the wrong mod blamed).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../sdk/dsl_sdk.h"

struct DSLPlugin { int running; const char *name; };
static int evr_running(DSLPlugin *self) { return !self || self->running; }
static int evr_class_is(void *vk, void *want) { return vk == want || (want == (void *)1 && vk == (void *)2); }   /* class 2 derives from 1 */
static const char *evr_name(DSLPlugin *self) { return self ? self->name : "loader"; }
static char logbuf[4096];
static void evr_log(const char *fmt, ...) {
    char l[600]; va_list ap; va_start(ap, fmt); vsnprintf(l, sizeof l, fmt, ap); va_end(ap);
    strncat(logbuf, l, sizeof logbuf - strlen(logbuf) - 2); strcat(logbuf, "\n");
}
static unsigned long long fake_now;                             /* the fake clock (ns): callbacks "take time" by moving it */
static uint64_t evr_clock_ns(void) { return fake_now; }
#include "../../src/dsl_evreg.h"

static int fails;
#define CHECK(c, what) do { if (c) printf("ok %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
static char trace[512];
static void put(const char *s) { strncat(trace, s, sizeof trace - strlen(trace) - 1); }
static DSLPlugin A = { 1, "a" }, B = { 1, "b" }, C = { 1, "c" };
static void cb_a(void *ud, const DSLEvent *e) { (void)ud; (void)e; put("a"); }
static void cb_b(void *ud, const DSLEvent *e) { (void)ud; (void)e; put("b"); }
static void cb_c(void *ud, const DSLEvent *e) { (void)ud; (void)e; put("c"); }
static void cb_once(void *ud, const DSLEvent *e) { (void)e; put("o"); evr_remove((DSLPlugin *)ud, DSL_EV_SCENE, cb_once, ud); }
static void cb_adder(void *ud, const DSLEvent *e) { (void)ud; (void)e; put("+"); evr_add(&C, DSL_EV_LANG, 0, NULL, cb_c, NULL); }
static void send_l(int type, int scene, void *klass, int loading) { DSLEvent e; memset(&e, 0, sizeof e); e.size = sizeof e; e.type = type; e.scene = scene; e.klass = klass; e.loading = loading; trace[0] = 0; evr_send(&e); }
static void send(int type, int scene, void *klass) { send_l(type, scene, klass, 0); }

/* time meter: S (slow) costs 1 ms a frame, F (fast) 0.05 ms, N opens a window (a nested VIEW_SHOWN that costs W 3 ms) */
static DSLPlugin S = { 1, "slow" }, F = { 1, "fast" }, N = { 1, "nester" }, W = { 1, "window" };
static void cb_cost(void *ud, const DSLEvent *e) { (void)e; fake_now += (unsigned long long)(uintptr_t)ud; }
static void cb_nest(void *ud, const DSLEvent *e) {
    (void)ud; fake_now += 100000;                               /* 0.1 ms of its own */
    if (e->frame % 6 == 0) { DSLEvent v; memset(&v, 0, sizeof v); v.size = sizeof v; v.type = DSL_EV_VIEW_SHOWN; v.scene = e->scene; evr_send(&v); }
}
/* run secs seconds of 60 frames at the scene, ticking the meter first like dsl_events_frame */
static void run_frames(int secs, int scene, int loading) {
    for (int i = 0; i < secs * 60; i++) {
        evr_meter_tick(fake_now, loading ? 0 : scene);
        DSLEvent e; memset(&e, 0, sizeof e); e.size = sizeof e; e.type = DSL_EV_FRAME; e.scene = scene; e.loading = loading; e.frame = (uint64_t)i;
        evr_send(&e);
        fake_now += 16666667ull;                                /* the rest of the frame (the game) */
    }
}
static int meter_test(void) {
    evr_remove(&A, 0, NULL, NULL); evr_remove(&B, 0, NULL, NULL); evr_remove(&C, 0, NULL, NULL);
    evr_meter = 1; fake_now = 1000000000ull; logbuf[0] = 0;
    evr_add(&S, DSL_EV_FRAME, 0, NULL, cb_cost, (void *)(uintptr_t)1000000);   /* 60 ms per s */
    evr_add(&F, DSL_EV_FRAME, 0, NULL, cb_cost, (void *)(uintptr_t)50000);     /* 3 ms per s */
    run_frames(30, 0, 1);                                       /* a scene load: measured, not judged */
    CHECK(!strstr(logbuf, "SLOW-MOD"), "meter: nothing judged while loading");
    run_frames(20, DSL_SCENE_WORLD, 0);
    CHECK(strstr(logbuf, "SLOW-MOD slow ") && strstr(logbuf, " world ("), "meter: a mod at 60 ms per s in a world is named (SLOW-MOD slow ... world)");
    CHECK(!strstr(logbuf, "SLOW-MOD fast"), "meter: a mod at 3 ms per s is not named");
    const char *a = strstr(logbuf, "SLOW-MOD slow "); CHECK(a && strstr(a + 1, "SLOW-MOD slow ") == NULL, "meter: once per state");
    run_frames(1, DSL_SCENE_TITLE, 0);                          /* the world's end: its summary line */
    const char *ml = strstr(logbuf, "modtime: world "), *ms = ml ? strstr(ml, " slow ") : NULL, *mf = ml ? strstr(ml, " fast ") : NULL;
    double vs = ms ? atof(ms + 6) : 0, vf = mf ? atof(mf + 6) : 0;   /* 1 ms and 0.05 ms in frames of ~17.7 ms: ~56 and ~2.8 */
    CHECK(vs > 50 && vs < 62 && vf > 2 && vf < 3.5, "meter: modtime line with each module's ms per s");
    evr_remove(&S, 0, NULL, NULL); evr_remove(&F, 0, NULL, NULL);
    /* nested: N's frame callback opens a window every 6th frame; W's view callback costs 3 ms = 30 ms per s. W is the
       slow one, N (6 ms per s of its own) must not be charged for W's time */
    logbuf[0] = 0;
    evr_add(&N, DSL_EV_FRAME, 0, NULL, cb_nest, NULL);
    evr_add(&W, DSL_EV_VIEW_SHOWN, 0, NULL, cb_cost, (void *)(uintptr_t)3000000);
    run_frames(20, DSL_SCENE_TITLE, 0);
    CHECK(strstr(logbuf, "SLOW-MOD window ") && strstr(logbuf, " title ("), "meter nested: the mod whose callback is slow is named");
    CHECK(!strstr(logbuf, "SLOW-MOD nester"), "meter nested: the mod that caused the event is not charged for it");
    evr_remove(&N, 0, NULL, NULL); evr_remove(&W, 0, NULL, NULL);
    evr_meter = 0;
    return 0;
}

int main(void) {
    /* order + a failed module */
    evr_add(&A, DSL_EV_SCENE, 0, NULL, cb_a, NULL); evr_add(&B, DSL_EV_SCENE, 0, NULL, cb_b, NULL);
    send(DSL_EV_SCENE, 1, NULL); CHECK(!strcmp(trace, "ab"), "every listener, in subscription order");
    B.running = 0; send(DSL_EV_SCENE, 1, NULL); CHECK(!strcmp(trace, "a"), "a failed / turned-off module gets no call");
    B.running = 1;
    CHECK(evr_add(&A, DSL_EV_SCENE, 0, NULL, cb_a, NULL) == -1, "the same subscription twice is refused");
    send(DSL_EV_SCENE, 1, NULL); CHECK(!strcmp(trace, "ab"), "no double call after a refused duplicate");
    /* unsubscribe inside a callback (one-shot) */
    evr_remove(&A, DSL_EV_SCENE, NULL, NULL);
    evr_add(&A, DSL_EV_SCENE, 0, NULL, cb_once, &A); evr_add(&C, DSL_EV_SCENE, 0, NULL, cb_c, NULL);
    send(DSL_EV_SCENE, 1, NULL); CHECK(!strcmp(trace, "boc"), "a one-shot removing itself: the others still called");
    send(DSL_EV_SCENE, 1, NULL); CHECK(!strcmp(trace, "bc"), "the one-shot is not called again");
    /* subscribe inside a callback: no loop, called from the next event on */
    evr_add(&A, DSL_EV_LANG, 0, NULL, cb_adder, NULL);
    send(DSL_EV_LANG, 1, NULL); CHECK(!strcmp(trace, "+"), "a subscription made during a send waits for the next event");
    send(DSL_EV_LANG, 1, NULL); CHECK(!strcmp(trace, "+c"), "then it is called (once: the duplicate add is refused)");
    /* frame filter */
    evr_add(&A, DSL_EV_FRAME, DSL_EVF_TITLE, NULL, cb_a, NULL); evr_add(&B, DSL_EV_FRAME, DSL_EVF_WORLD, NULL, cb_b, NULL);
    evr_add(&C, DSL_EV_FRAME, 0, NULL, cb_c, NULL);
    send(DSL_EV_FRAME, 1, NULL); CHECK(!strcmp(trace, "ac"), "frame: title filter at the title");
    send(DSL_EV_FRAME, 2, NULL); CHECK(!strcmp(trace, "bc"), "frame: world filter in a world");
    send(DSL_EV_FRAME, 9, NULL); CHECK(!strcmp(trace, "c"), "frame: another scene = only the unfiltered listener");
    send_l(DSL_EV_FRAME, 1, NULL, 1); CHECK(!strcmp(trace, "c"), "frame: a load from the title = only the unfiltered listener");
    /* class filter */
    evr_add(&A, DSL_EV_VIEW_SHOWN, 0, (void *)1, cb_a, NULL); evr_add(&B, DSL_EV_VIEW_SHOWN, 0, (void *)3, cb_b, NULL);
    evr_add(&C, DSL_EV_VIEW_SHOWN, 0, NULL, cb_c, NULL);
    send(DSL_EV_VIEW_SHOWN, 2, (void *)2); CHECK(!strcmp(trace, "ac"), "view: a subclass matches, another class does not");
    /* off_event: every subscription of a module */
    CHECK(evr_remove(&C, 0, NULL, NULL) == 4, "off_event(type 0, NULL) removes all of a module's subscriptions");
    send(DSL_EV_FRAME, 1, NULL); CHECK(!strcmp(trace, "a"), "a removed module is not called");
    meter_test();
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
