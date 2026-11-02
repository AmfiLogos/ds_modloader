// The Mods window's switch rule (src/dsl_deps.h dsl_deps_turn_on / dsl_deps_turn_off; owner 2026-11-02, tModLoader
// study idea 5). Each case is a real break: a mod switched on without what it requires does not run; a mod switched off
// under another that requires it stops that one too; a cycle must not hang the Mods window.
#include "dsl_deps.h"
const char *dsi_tr(const char *s) { return s; }
void dsi_utf8_fix_end(char *s) { (void)s; }

static int fails;
#define CHECK(c, m) do { if (c) printf("ok %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

static DslDep D[16]; static int nd; static JNode *J[16];
static void mod(const char *id, const char *mf, int on) {
    J[nd] = json_parse(mf);
    DslDep *x = dsl_dep_add(D, &nd, 16, id, J[nd], NULL, NULL, nd);
    x->on = on;
}
static int idx(const char *id) { DslDep *x = dsl_dep_find(D, nd, id); return x ? (int)(x - D) : -1; }
static void reset(void) { for (int i = 0; i < nd; i++) json_free(J[i]); nd = 0; }

int main(void) {
    int add[16]; char miss[8][96]; int nmiss;
    /* A requires B, B requires C>=1.0 (and the loader); all off but A (just clicked) */
    mod("a", "{\"id\":\"a\",\"kind\":\"code\",\"requires\":[\"b\",\"loader>=1.0\"]}", 1);
    mod("b", "{\"id\":\"b\",\"kind\":\"code\",\"requires\":[\"c>=1.0\"]}", 0);
    mod("c", "{\"id\":\"c\",\"kind\":\"code\",\"version\":\"1.2.0\"}", 0);
    mod("d", "{\"id\":\"d\",\"kind\":\"code\",\"optional\":[\"c\"]}", 0);
    int n = dsl_deps_turn_on(D, nd, idx("a"), add, miss, &nmiss, 8);
    CHECK(n == 2 && add[idx("b")] && add[idx("c")] && !add[idx("d")] && nmiss == 0, "on: what A requires and what that requires are turned on (never an optional one, never 'loader')");
    reset();
    /* A requires X (not installed) and P (launcher-provided) */
    mod("a", "{\"id\":\"a\",\"kind\":\"code\",\"requires\":[\"x>=2\",\"p\"]}", 1);
    { DslDep *p = dsl_dep_add(D, &nd, 16, "p", NULL, NULL, NULL, 9); p->fixed = 1; p->on = 1; J[nd - 1] = NULL; }
    n = dsl_deps_turn_on(D, nd, idx("a"), add, miss, &nmiss, 8);
    CHECK(n == 0 && nmiss == 1 && !strcmp(miss[0], "x>=2"), "on: a required mod that is not installed is named as written; nothing else switched");
    reset();
    /* switching C off: B requires C, A requires B (both on), D only optional on C */
    mod("a", "{\"id\":\"a\",\"kind\":\"code\",\"requires\":[\"b\"]}", 1);
    mod("b", "{\"id\":\"b\",\"kind\":\"code\",\"requires\":[\"c\"]}", 1);
    mod("c", "{\"id\":\"c\",\"kind\":\"code\"}", 0);
    mod("d", "{\"id\":\"d\",\"kind\":\"code\",\"optional\":[\"c\"]}", 1);
    mod("e", "{\"id\":\"e\",\"kind\":\"code\",\"requires\":[\"c\"]}", 0);
    n = dsl_deps_turn_off(D, nd, idx("c"), add);
    CHECK(n == 2 && add[idx("a")] && add[idx("b")] && !add[idx("d")] && !add[idx("e")], "off: every mod that is on and needs it (also through another mod) is offered; optional and off ones are not");
    reset();
    /* a cycle (A requires B, B requires A) ends */
    mod("a", "{\"id\":\"a\",\"kind\":\"code\",\"requires\":[\"b\"]}", 1);
    mod("b", "{\"id\":\"b\",\"kind\":\"code\",\"requires\":[\"a\"]}", 0);
    n = dsl_deps_turn_on(D, nd, idx("a"), add, miss, &nmiss, 8);
    int m = dsl_deps_turn_off(D, nd, idx("b"), add);
    CHECK(n == 1 && m == 1, "a cycle ends (on and off)");
    reset();
    printf(fails ? "FAIL %d\n" : "PASS\n", fails);
    return fails != 0;
}
