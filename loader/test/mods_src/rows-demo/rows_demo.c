// rows-demo: TEST-ONLY module for the loader's API 3 load list + tab + session busy (loader/test/l1_rows_test.py).
// Adds one load-row text ("Demo row <guid start>"), one filter (toggle "Show demo worlds"; off = every world hidden),
// one Mods window tab "Demo", and with ROWS_DEMO_BUSY=1 marks the session busy. Never shipped.
#include "dsl_sdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
DSL_PLUGIN();
static const DSLHost *H; static DSLPlugin *ME; static int opened;
static void row_text(void *ud, const char *guid, const char *save, char *out, size_t cap) { (void)ud; (void)save; snprintf(out, cap, "Demo row %.8s", guid); }
static int show(void *ud, const char *guid, const char *save, int on) { (void)ud; (void)guid; (void)save; return on; }
static const char *label(void *ud) { (void)ud; return "Show demo worlds"; }
static void tab_open(void *ud, DSLObj root, float w, float h) { (void)ud; opened++; H->log(ME, "Demo tab open (%.0fx%.0f) root %p", w, h, root);
    const DSLUi *u = dsl_ui(H); if (u) u->text(root, 10, 10, w - 20, 30, "Demo tab", 18, DSL_TXT_LEFT, 0xFFFFFFFF); }
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me;
    DSLLoadRow r = { row_text, NULL, NULL };
    DSLLoadFilter f = { show, label, NULL, NULL };
    DSLTab t = { "demo", "Demo", 90, tab_open, NULL, NULL, NULL, NULL };
    int a = dsl_loadrow_add(h, me, &r), b = dsl_loadlist_filter(h, me, &f), c = dsl_menu_add_tab(h, me, &t);
    if (getenv("ROWS_DEMO_BUSY")) dsl_set_session_busy(h, me, "a demo session runs");
    h->log(me, "rows-demo: loadrow_add %d, loadlist_filter %d, menu_add_tab %d, ui %s, api size %u", a, b, c, dsl_ui(h) ? "yes" : "no", h->size);
    return (a || b || c) ? 1 : 0;
}
