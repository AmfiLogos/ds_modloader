// TEST ONLY: a module that adds a tab to the Mods window through the SDK (host->menu_add_tab + host->ui), as Custom
// Modes will. Added only when DSL_UI_TEST_TAB is set.
#include "dsl_sdk.h"
#include <stdio.h>
#include <stdlib.h>
DSL_PLUGIN();
static const DSLHost *H; static DSLObj out; static int clicks;
static void click(void *ud, DSLObj w) { (void)ud; (void)w; char b[64]; snprintf(b, sizeof b, "Last: button %d", ++clicks); H->ui->set_text(out, b); }
static void open_(void *ud, DSLObj root, float w, float h) {
    (void)ud; (void)h; const DSLUi *U = H->ui;
    U->text(root, 0, 0, w, 32, "A module tab (test)", 20, DSL_TXT_LEFT | DSL_TXT_TITLE, 0xD9BE8FFF);
    U->button(root, 0, 50, 220, 44, "Click me", click, NULL);
    static const char *const v[] = { "Off", "Low", "High" };
    U->value_row(root, 0, 110, 540, "A value row", v, 3, 1, NULL, NULL);
    out = U->text(root, 0, 170, 540, 40, "Last: nothing", 16, DSL_TXT_LEFT, 0);
}
DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host;
    static DSLTab t = { "test", "Test Tab", 50, open_, NULL, NULL, NULL, NULL };
    if (getenv("DSL_UI_TEST_TAB") && dsl_menu_add_tab(host, self, &t)) { host->log(self, "menu_add_tab refused"); return -1; }
    return 0;
}
