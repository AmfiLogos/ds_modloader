// ev_test.c - host test of the loader's event registry (src/dsl_evreg.h, SDK API 7), no game. Run by test/run_tests.sh.
// The real breaks it catches: a module that failed or was turned off still gets calls (its code may be half set up: a
// crash in every player's game); a callback that unsubscribes (one-shot) or subscribes during a send makes the
// dispatcher skip another module's call, call a removed one, or loop; the title / world frame filter or the class
// filter sends events to the wrong modules; the same subscription twice doubles the calls.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../../sdk/dsl_sdk.h"

struct DSLPlugin { int running; const char *name; };
static int evr_running(DSLPlugin *self) { return !self || self->running; }
static int evr_class_is(void *vk, void *want) { return vk == want || (want == (void *)1 && vk == (void *)2); }   /* class 2 derives from 1 */
static const char *evr_name(DSLPlugin *self) { return self ? self->name : "loader"; }
static void evr_log(const char *fmt, ...) { (void)fmt; }
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
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
