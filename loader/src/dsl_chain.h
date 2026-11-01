// Hook chaining: several modules on one game function (owner 2026-10-24: "allow multiple mods modifying similar
// functions"). ONE implementation for both engines (src/dsl_engine.c Mac, start/win/dsl_engine_win.c Windows).
//
// Every hook is a link of its site. Links run newest first: the site entry jumps to the newest active link, whose
// `orig` jumps to the next older active link, ... the oldest calls the game's original code. Load order = install
// order, so a mod loaded later wraps the mods loaded before it.
//   - The first link of a site gets the plain original (Mac stub+16, Windows MinHook trampoline): a site with one
//     owner costs nothing more than before.
//   - A link installed over an active link gets its own counting veneer:  counter += 1 ; jump [next]
//     `next` = the next older active link's replacement, or the original. Its `orig` pointer is that veneer.
//   - A site with an extra link also gets a counting veneer at its entry (slot -> veneer -> newest link).
//   - Unhook (a mod's own call, a failed phase, the crash guard's module removal) = the link is marked inactive and
//     every veneer `next` of the site is recomputed oldest first, then the entry. A call still in flight in the
//     removed mod keeps going down the chain: veneers are never freed or reused for another site.
//   - Skip guard: a link whose own veneer count does not move while N calls came into it never called `orig`;
//     every older mod on that function did not run. Logged once per link (not blocked: a mod may skip on purpose).
// Refused (logged): the same module twice on one function; two different methods (both names known, hook_method)
// that share one body (Windows MSVC identical-code folding: one mod's code would run for the other method).
//
// The including engine defines, BEFORE including this file:
//   static int ch_veneer(uintptr_t *code, volatile uint64_t **cnt, void *volatile **next);   0 = ok
// and calls every ch_* function with its engine lock held.
#ifndef DSL_CHAIN_H
#define DSL_CHAIN_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CH_MAXL 2048          /* links in the process (every hook is one) */
#define CH_PER_SITE 8         /* links listed per site (inactive ones without a veneer are dropped when full) */
#define CH_SKIP_N 50          /* calls into a link with none passed down = "did not call the original" */

typedef struct {
    void *repl; int owner; int active; char name[48]; char method[96];
    uintptr_t ven; volatile uint64_t *cnt; void *volatile *next;     /* its orig veneer (0 = it calls the original) */
    uint64_t base_out; int skip_logged;
} ChainLink;
typedef struct {
    void *volatile *slot; void *orig_code; uintptr_t rva;            /* the site's slot, its original code */
    uintptr_t ven; volatile uint64_t *ven_cnt; void *volatile *ven_next;   /* entry veneer (0 = none yet) */
    uint64_t base_in;
    int lk[CH_PER_SITE]; int nlk;                                     /* links, oldest first */
} ChainSite;

static ChainLink CL[CH_MAXL];
static int ncl;
static int ch_chained_sites;                                          /* sites with an entry veneer (skip checks run) */

static void ch_store(void *volatile *p, void *v) { __atomic_store_n((void **)p, v, __ATOMIC_RELEASE); }
static uint64_t ch_load_cnt(volatile uint64_t *c) { return c ? __atomic_load_n((uint64_t *)c, __ATOMIC_RELAXED) : 0; }

static int ch_top(const ChainSite *cs) { for (int i = cs->nlk - 1; i >= 0; i--) if (CL[cs->lk[i]].active) return i; return -1; }
static int ch_nactive(const ChainSite *cs) { int n = 0; for (int i = 0; i < cs->nlk; i++) n += CL[cs->lk[i]].active; return n; }
/* the code a link at list position pos calls as its original: the next older active link, else the game */
static void *ch_below(const ChainSite *cs, int pos) {
    for (int i = pos - 1; i >= 0; i--) if (CL[cs->lk[i]].active) return CL[cs->lk[i]].repl;
    return cs->orig_code;
}
static const ChainLink *ch_link_of(const ChainSite *cs, int owner) {
    for (int i = 0; i < cs->nlk; i++) if (CL[cs->lk[i]].active && CL[cs->lk[i]].owner == owner) return &CL[cs->lk[i]];
    return NULL;
}
/* "b > a > game" (newest first) */
static const char *ch_order(const ChainSite *cs, char *buf, size_t n) {
    size_t k = 0; buf[0] = 0;
    for (int i = cs->nlk - 1; i >= 0; i--) if (CL[cs->lk[i]].active && k + 4 < n)
        k += (size_t)snprintf(buf + k, n - k, "%s > ", dsl_owner_name(CL[cs->lk[i]].owner));
    if (k < n) snprintf(buf + k, n - k, "game");
    return buf;
}

/* recompute every veneer `next` (oldest first), then the entry; reset the skip-guard counts */
static void ch_relink(ChainSite *cs) {
    for (int i = 0; i < cs->nlk; i++) {
        ChainLink *l = &CL[cs->lk[i]];
        if (l->next) ch_store(l->next, ch_below(cs, i));
        l->base_out = ch_load_cnt(l->cnt);
    }
    int t = ch_top(cs);
    void *entry = t < 0 ? NULL : CL[cs->lk[t]].repl;
    if (entry && cs->ven) { ch_store(cs->ven_next, entry); entry = (void *)cs->ven; }
    ch_store(cs->slot, entry);
    cs->base_in = ch_load_cnt(cs->ven_cnt);
}

/* add a link; 0 = ok (*orig set), -1 = refused (logged) */
static int ch_add(ChainSite *cs, int owner, void *repl, const char *method, const char *who, void **orig) {
    for (int i = 0; i < cs->nlk; i++) {
        const ChainLink *l = &CL[cs->lk[i]];
        if (!l->active) continue;
        if (l->owner == owner) {
            dsl_log("%s: hook refused: %#lx is already hooked by this module (%s)%s", who, (unsigned long)cs->rva, l->name,
#ifdef _WIN32
                    " (on Windows two methods can share one body: MSVC identical-code folding)"
#else
                    ""
#endif
                    );
            return -1;
        }
        if (method && *method && l->method[0] && strcmp(method, l->method)) {
            dsl_log("%s: hook refused: %#lx is %s here and already hooked by %s (%s) as %s: two methods share one body "
                    "(identical-code folding), chaining would run one mod's code for the other method", who, (unsigned long)cs->rva,
                    method, dsl_owner_name(l->owner), l->name, l->method);
            return -1;
        }
    }
    int last = cs->nlk - 1;
    if (last >= 0 && !CL[cs->lk[last]].active && CL[cs->lk[last]].owner == owner) {   /* the newest link, off: re-arm in place */
        ChainLink *l = &CL[cs->lk[last]];
        l->repl = repl; l->active = 1; l->skip_logged = 0;
        snprintf(l->name, sizeof l->name, "%s", who);
        snprintf(l->method, sizeof l->method, "%s", method ? method : "");
        if (orig) *orig = l->ven ? (void *)l->ven : cs->orig_code;
        ch_relink(cs);
        dsl_log("%s: hook %#lx re-armed", who, (unsigned long)cs->rva);
        return 0;
    }
    if (cs->nlk >= CH_PER_SITE) {                                     /* drop old inactive links nothing points to */
        int k = 0;
        for (int i = 0; i < cs->nlk; i++) if (CL[cs->lk[i]].active || CL[cs->lk[i]].ven) cs->lk[k++] = cs->lk[i];
        cs->nlk = k;
    }
    if (cs->nlk >= CH_PER_SITE || ncl >= CH_MAXL) { dsl_log("%s: hook refused: %#lx has too many hooks", who, (unsigned long)cs->rva); return -1; }
    int prev = ch_top(cs), under = prev >= 0;                         /* an active link below: chain over it */
    uintptr_t v = 0; volatile uint64_t *vc = NULL; void *volatile *vn = NULL;
    if (under) {
        if (!cs->ven) {
            if (ch_veneer(&cs->ven, &cs->ven_cnt, &cs->ven_next)) { cs->ven = 0; dsl_log("%s: hook refused: %#lx: no room for a chain veneer", who, (unsigned long)cs->rva); return -1; }
            ch_store(cs->ven_next, CL[cs->lk[prev]].repl);           /* valid before anything jumps to it */
            ch_chained_sites++;
        }
        if (ch_veneer(&v, &vc, &vn)) { dsl_log("%s: hook refused: %#lx: no room for a chain veneer", who, (unsigned long)cs->rva); return -1; }
        ch_store(vn, ch_below(cs, cs->nlk));
    }
    ChainLink *l = &CL[ncl];
    memset(l, 0, sizeof *l);
    l->repl = repl; l->owner = owner; l->active = 1; l->ven = v; l->cnt = vc; l->next = vn;
    snprintf(l->name, sizeof l->name, "%s", who);
    snprintf(l->method, sizeof l->method, "%s", method ? method : "");
    cs->lk[cs->nlk++] = ncl++;
    ch_relink(cs);
    if (orig) *orig = v ? (void *)v : cs->orig_code;
    if (under) { char o[256]; dsl_log("%s: %#lx chained over %s: runs %s", who, (unsigned long)cs->rva, dsl_owner_name(CL[cs->lk[prev]].owner), ch_order(cs, o, sizeof o)); }
    return 0;
}

/* take an owner's link off; 1 = removed */
static int ch_remove(ChainSite *cs, int owner) {
    for (int i = 0; i < cs->nlk; i++) {
        ChainLink *l = &CL[cs->lk[i]];
        if (!l->active || l->owner != owner) continue;
        l->active = 0;
        int others = ch_nactive(cs);
        ch_relink(cs);
        if (others || cs->ven) { char o[256]; dsl_log("%s: %#lx: %s taken off the chain: runs %s", l->name, (unsigned long)cs->rva, dsl_owner_name(owner), ch_order(cs, o, sizeof o)); }
        return 1;
    }
    return 0;
}

/* skip guard (engine lock held): log once a link that passed none of >= CH_SKIP_N calls to the mods below it */
static void ch_check(ChainSite *cs) {
    if (!cs->ven) return;
    uint64_t in = ch_load_cnt(cs->ven_cnt) - cs->base_in;
    for (int i = cs->nlk - 1; i >= 0; i--) {
        ChainLink *l = &CL[cs->lk[i]];
        if (!l->active) continue;
        if (!l->cnt || ch_below(cs, i) == cs->orig_code) return;      /* nothing of a mod below it */
        uint64_t out = ch_load_cnt(l->cnt) - l->base_out;
        if (in >= CH_SKIP_N && out == 0 && !l->skip_logged) {
            char below[256]; size_t k = 0; below[0] = 0;
            for (int j = i - 1; j >= 0; j--) if (CL[cs->lk[j]].active && k + 4 < sizeof below)
                k += (size_t)snprintf(below + k, sizeof below - k, "%s%s", k ? ", " : "", dsl_owner_name(CL[cs->lk[j]].owner));
            dsl_log("chain %#lx: %s (%s) did not call the original in %llu calls: %s below it did not run", (unsigned long)cs->rva,
                    dsl_owner_name(l->owner), l->name, (unsigned long long)in, below);
            l->skip_logged = 1;
        }
        in = out;
    }
}
#endif
