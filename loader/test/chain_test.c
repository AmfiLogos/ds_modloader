/* Hook chain rules of src/dsl_chain.h without a game (review 2026-11-02, findings 4 and 6). Built twice by run_tests.sh:
   -DCH_FOLD_RISK=1 (the Windows rule, compiled on the Mac) and the Mac default.
   Breaks caught:
   - a thread entering a just-installed hook calls a NULL orig (crash): *orig must be written BEFORE the slot or a
     veneer points at the new hook (first link, a link over another, a re-armed link);
   - Windows: a mod's hook without a method name chains with another mod's hook on one body, so one mod's code runs for
     another method that MSVC folded into the same body (~660 bodies shared with a multiplayer hook);
   - Windows: a mod can no longer chain over the loader's own hook (test/win/chain_vm.py chains over EventSystem.Update),
     or two mods hooking the same method by name are refused. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define DSL_OWNER_LOADER (-1)
static char lastlog[512];
static void dsl_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(lastlog, sizeof lastlog, fmt, ap); va_end(ap); }
static const char *dsl_owner_name(int o) { static char b[4][16]; static int k; k = (k + 1) & 3; snprintf(b[k], sizeof b[k], o < 0 ? "loader" : "mod%d", o); return b[k]; }
/* the moment any slot or veneer starts pointing at the new hook: what *orig holds then */
static void *watch_repl, **watch_orig, *orig_then; static int stores;
static void on_store(void *volatile *p, void *v) { (void)p; if (watch_repl && v == watch_repl) { stores++; if (stores == 1) orig_then = *watch_orig; } }
#define CH_ON_STORE(p, v) on_store((p), (v))
static uint64_t ven_mem[64][3]; static int nven;       /* fake veneers: { counter, next, code } */
static int ch_veneer(uintptr_t *code, volatile uint64_t **cnt, void *volatile **next) {
    if (nven >= 64) return -1;
    uint64_t *m = ven_mem[nven++]; *code = (uintptr_t)&m[2]; *cnt = (volatile uint64_t *)&m[0]; *next = (void *volatile *)&m[1];
    return 0;
}
#include "dsl_chain.h"

static int fails;
static void t(const char *what, int ok) { printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, ok ? "" : " | last log: ", ok ? "" : lastlog); fails += !ok; }
static ChainSite S[16]; static void *slots[16]; static int nsite;
static ChainSite *site(void) {
    ChainSite *cs = &S[nsite]; memset(cs, 0, sizeof *cs);
    cs->slot = (void *volatile *)&slots[nsite]; cs->orig_code = (void *)(uintptr_t)(0x1000 + nsite); cs->rva = 0x500000 + 0x100 * (uintptr_t)nsite;
    nsite++; return cs;
}
/* one hook; orig starts NULL; returns ch_add's result and whether *orig was already the final value when the hook became reachable */
static int add(ChainSite *cs, int owner, uintptr_t repl, const char *method, void **orig, int *ordered) {
    char who[32]; snprintf(who, sizeof who, "%s hook", dsl_owner_name(owner));
    *orig = NULL; watch_repl = (void *)repl; watch_orig = orig; orig_then = NULL; stores = 0;
    int r = ch_add(cs, owner, (void *)repl, method, who, orig);
    if (ordered) *ordered = r == 0 && stores > 0 && *orig && orig_then == *orig;
    watch_repl = NULL;
    return r;
}

int main(void) {
    const char *upd = "UnityEngine.EventSystems.EventSystem.Update/0";
    void *o1, *o2, *o3; int ord;
    ChainSite *a = site();
    int r = add(a, 1, 0xA1, upd, &o1, &ord);
    t("first link: *orig (the original) is set before the slot points at the hook", r == 0 && ord && o1 == a->orig_code);
    r = add(a, 2, 0xA2, upd, &o2, &ord);
    t("a link over another: *orig (its veneer) is set before the chain points at it", r == 0 && ord && o2 != a->orig_code);
    ch_remove(a, 2);
    r = add(a, 2, 0xA2, upd, &o3, &ord);
    t("a re-armed link: *orig is set before the chain points at it", r == 0 && ord);
    if (CH_FOLD_RISK) {                                 /* the Windows rule */
        void *o;
        ChainSite *d = site(); add(d, 5, 0xD1, "", &o, NULL);           /* multiplayer: hook_rva / tables, no method name */
        r = add(d, 6, 0xD2, "Refactor.Some.Getter/0", &o, NULL);
        t("Windows: a mod's hook_method over another mod's hook without a method name = refused", r == -1 && strstr(lastlog, "no method name"));
        ChainSite *e = site(); add(e, 6, 0xE1, "Refactor.Some.Getter/0", &o, NULL);
        r = add(e, 5, 0xE2, NULL, &o, NULL);
        t("Windows: a mod's hook without a method name over another mod's hook_method = refused", r == -1 && strstr(lastlog, "no method name"));
        ChainSite *f = site(); add(f, 5, 0xF1, NULL, &o, NULL);
        r = add(f, 6, 0xF2, NULL, &o, NULL);
        t("Windows: two mods' hooks without method names on one body = refused", r == -1);
        ChainSite *g = site(); add(g, DSL_OWNER_LOADER, 0x61, NULL, &o, NULL);   /* the loader's EventSystem.Update tick */
        r = add(g, 7, 0x62, upd, &o, &ord);
        t("Windows: a mod chains over the loader's own hook (chain_vm.py)", r == 0 && ord);
        ChainSite *h = site(); add(h, 8, 0x71, "Refactor.A.B/0", &o, NULL);
        r = add(h, 9, 0x72, "Refactor.A.B/0", &o, NULL);
        t("Windows: two mods, the same method by name = chained", r == 0);
    } else {
        void *o; ChainSite *m = site(); add(m, 5, 0x81, NULL, &o, NULL);
        r = add(m, 6, 0x82, NULL, &o, NULL);
        t("Mac: two mods' hooks without method names on one function still chain (no folded bodies)", r == 0);
    }
    (void)ch_link_of; (void)ch_check;
    return fails != 0;
}
