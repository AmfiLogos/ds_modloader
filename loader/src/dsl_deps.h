// Mod dependencies: one rule for mod.json "requires", "optional" and "conflicts" (loader/NOTES.md "Dependencies").
// Header only (static functions) so that every part decides the same way: the loader core (dsl_main.c: code modules,
// and the BepInEx set this process runs), the Mods window (dsl_reg.c: Apply), the Windows starter and soft restart
// (start/win/bie_win.c) and the Mac stub (app/stub/ds_modded.c).
// An entry: "id", "id>=1.2.0", "id>1", "id<=2", "id<2", "id=1.0.0", "id!=1.1" (dotted numbers); "loader>=x" = the
// loader's internal API (DSL_LOADER_VERSION), requires only.
//   requires   runs only when each named mod is installed, switched on, can run itself, and its version fits
//   optional   the named mod may be missing or off; when it is on, its version must fit, else this mod does not run
//   conflicts  never runs together with the named mod (when its version fits): the one later in the profile order is off
// Checked kinds: "code" and "bepinex". Other kinds (data / patch / builtin: the launcher applies them; melon) are never
// switched off here, but count as the mods others need.
#pragma once
#include "dsl_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { DSL_DEP_OK, DSL_DEP_MISSING, DSL_DEP_OFF, DSL_DEP_NOT_RUNNING, DSL_DEP_VERSION, DSL_DEP_OPT_VERSION, DSL_DEP_LOADER,
       DSL_DEP_CONFLICT };
typedef struct {
    char id[64], version[32], kind[16];
    const JNode *mf;          /* its mod.json (requires / optional / conflicts); NULL = nothing to check */
    int on;                   /* switched on (and, for the caller, able to run) */
    int fixed;                /* given by the launcher (profile "provided"): always on, any version fits */
    int order;                /* profile order: lower = earlier */
    int ok, why;              /* result: ok = may run; why = DSL_DEP_* when on and not ok */
    char arg[96], have[32];   /* the entry that failed (as written in mod.json), the version found */
} DslDep;

static inline int dsl_dep_vcmp(const char *a, const char *b) {          /* dotted numeric compare */
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if (!isdigit((unsigned char)*a) && !isdigit((unsigned char)*b)) break;
    }
    return 0;
}
/* "id", "id>=1.2", "id<2", "id=1.0.0" -> id + op (3 bytes) + version (32 bytes) */
static inline void dsl_dep_split(const char *s, char *id, size_t n, char *op, char *ver) {
    size_t i = 0;
    while (s[i] && !strchr("<>=!", s[i]) && i + 1 < n) { id[i] = s[i]; i++; }
    id[i] = 0;
    while (i > 0 && id[i - 1] == ' ') id[--i] = 0;
    op[0] = 0; ver[0] = 0;
    const char *p = s + strlen(id);
    while (*p == ' ') p++;
    int k = 0; while (*p && strchr("<>=!", *p) && k < 2) op[k++] = *p++;
    op[k] = 0;
    while (*p == ' ') p++;
    snprintf(ver, 32, "%s", p);
}
static inline int dsl_dep_ver_ok(const char *have, const char *op, const char *want) {
    if (!op[0]) return 1;
    int c = dsl_dep_vcmp(have, want);
    if (!strcmp(op, ">=")) return c >= 0;
    if (!strcmp(op, ">")) return c > 0;
    if (!strcmp(op, "<=")) return c <= 0;
    if (!strcmp(op, "<")) return c < 0;
    if (!strcmp(op, "=") || !strcmp(op, "==")) return c == 0;
    if (!strcmp(op, "!=")) return c != 0;
    return 0;
}
static inline int dsl_dep_checked(const DslDep *d) { return !d->fixed && d->mf && (!strcmp(d->kind, "code") || !strcmp(d->kind, "bepinex")); }
static inline DslDep *dsl_dep_find(DslDep *d, int n, const char *id) {
    for (int i = 0; i < n; i++) if (!strcmp(d[i].id, id)) return &d[i];
    return NULL;
}
/* mod.json "difficulty": {"value": N}: a custom difficulty mod (Hell difficulty: 5). It runs whenever it is installed, switched
   on or not (owner 2026-10-25: "keep it always on & have the toggle just activate its display"); only worlds on level N use it
   (dsl_reg.c). 0 = not one. */
static inline int dsl_mf_difficulty(const JNode *mf) {
    const JNode *v = json_get(json_get(mf, "difficulty"), "value");
    return v && v->t == JN_NUM && v->num > 0 ? (int)v->num : 0;
}
/* one installed mod (or, mf NULL and fixed, one the launcher provides) into d[*n]. en / order: the profile's "enabled"
   and "order" arrays (en NULL = no profile: every mod is on, the loader's rule). idx: its place in the folder list. */
static inline DslDep *dsl_dep_add(DslDep *d, int *n, int cap, const char *id, const JNode *mf, const JNode *en, const JNode *order, int idx) {
    if (*n >= cap || !id || dsl_dep_find(d, *n, id)) return NULL;
    DslDep *x = &d[(*n)++]; memset(x, 0, sizeof *x);
    snprintf(x->id, sizeof x->id, "%s", id);
    const char *v = json_str(mf, "version"), *k = json_str(mf, "kind");
    if (!k) k = json_str(mf, "type");
    snprintf(x->version, sizeof x->version, "%s", v ? v : "");
    snprintf(x->kind, sizeof x->kind, "%s", k ? k : "code");
    x->mf = mf;
    int pos = -1, epos = -1, i = 0;
    if (order && order->t == JN_ARR) for (const JNode *c = order->child; c; c = c->next, i++) if (c->t == JN_STR && !strcmp(c->str, id)) { pos = i; break; }
    i = 0;
    if (en && en->t == JN_ARR) for (const JNode *c = en->child; c; c = c->next, i++) if (c->t == JN_STR && !strcmp(c->str, id)) { epos = i; break; }
    x->on = !(en && en->t == JN_ARR) || epos >= 0 || dsl_mf_difficulty(mf);
    x->order = pos >= 0 ? pos : epos >= 0 ? epos : 1000 + idx;
    return x;
}
static inline void dsl_dep_fail(DslDep *x, int why, const char *arg, const char *have) {
    x->ok = 0; x->why = why;
    snprintf(x->arg, sizeof x->arg, "%s", arg ? arg : "");
    snprintf(x->have, sizeof x->have, "%s", have ? have : "");
}
/* decide every entry's ok / why. loader_ver: DSL_LOADER_VERSION. log: printf-style (NULL = quiet); the lines are the ones
   the loader always wrote ("CONFLICT: ...", "<id>: needs <entry>, which ...: not loaded"). */
static inline void dsl_deps_resolve(DslDep *d, int n, const char *loader_ver, void (*log)(const char *fmt, ...)) {
    for (int i = 0; i < n; i++) { d[i].ok = d[i].on || d[i].fixed; d[i].why = DSL_DEP_OK; d[i].arg[0] = d[i].have[0] = 0; }
    /* conflicts (each mod's own list; the one later in the profile order is dropped, a provided one never) */
    for (int i = 0; i < n; i++) {
        if (!d[i].ok || !dsl_dep_checked(&d[i])) continue;
        const JNode *cf = json_get(d[i].mf, "conflicts");
        if (!cf || cf->t != JN_ARR) continue;
        for (const JNode *c = cf->child; c; c = c->next) {
            if (c->t != JN_STR) continue;
            char cid[64], op[3], ver[32]; dsl_dep_split(c->str, cid, sizeof cid, op, ver);
            DslDep *o = dsl_dep_find(d, n, cid);
            if (!o || o == &d[i] || !o->ok || (!o->fixed && !dsl_dep_ver_ok(o->version, op, ver))) continue;
            DslDep *drop = o->fixed || !dsl_dep_checked(o) || o->order < d[i].order ? &d[i] : o;
            DslDep *keep = drop == o ? &d[i] : o;
            dsl_dep_fail(drop, DSL_DEP_CONFLICT, keep->id, NULL);
            if (log) log("CONFLICT: %s and %s cannot run together; %s is not loaded", d[i].id, cid, drop->id);
            if (drop == &d[i]) break;
        }
    }
    /* requires + optional: repeat until stable (a dropped mod can break another's requirement) */
    for (int changed = 1; changed; ) {
        changed = 0;
        for (int i = 0; i < n; i++) {
            if (!d[i].ok || !dsl_dep_checked(&d[i])) continue;
            for (int opt = 0; opt < 2 && d[i].ok; opt++) {
                const JNode *rq = json_get(d[i].mf, opt ? "optional" : "requires");
                if (!rq || rq->t != JN_ARR) continue;
                for (const JNode *c = rq->child; c; c = c->next) {
                    if (c->t != JN_STR) continue;
                    char rid[64], op[3], ver[32]; dsl_dep_split(c->str, rid, sizeof rid, op, ver);
                    int why = DSL_DEP_OK; const char *have = NULL, *lw = NULL;
                    DslDep *o = dsl_dep_find(d, n, rid);
                    if (!opt && !strcmp(rid, "loader")) { if (!dsl_dep_ver_ok(loader_ver, op, ver)) { why = DSL_DEP_LOADER; have = loader_ver; lw = "loader version"; } }
                    else if (opt) { if (o && o->ok && !o->fixed && !dsl_dep_ver_ok(o->version, op, ver)) { why = DSL_DEP_OPT_VERSION; have = o->version; lw = "wrong version (optional)"; } }
                    else if (!o) { why = DSL_DEP_MISSING; lw = "is not installed / enabled"; }
                    else if (!o->on && !o->fixed) { why = DSL_DEP_OFF; lw = "is not installed / enabled"; }
                    else if (!o->ok) { why = DSL_DEP_NOT_RUNNING; lw = "is not loaded"; }
                    else if (!o->fixed && !dsl_dep_ver_ok(o->version, op, ver)) { why = DSL_DEP_VERSION; have = o->version; lw = "wrong version"; }
                    if (why == DSL_DEP_OK) continue;
                    dsl_dep_fail(&d[i], why, c->str, have); changed = 1;
                    if (log) log("%s: needs %s, which %s: not loaded", d[i].id, c->str, lw);
                    break;
                }
            }
        }
    }
}
/* the player's text for a mod that does not run because of a dependency (current language); "" when none */
static inline const char *dsl_dep_text(const DslDep *x, char *out, size_t cap) {
    char a[128] = ""; size_t k = 0;                                   /* "Core>=0.5.0" -> "Core >= 0.5.0" */
    for (const char *s = x->arg; *s && k + 4 < sizeof a; s++) {
        int isop = strchr("<>=!", *s) != NULL, prev = s > x->arg && strchr("<>=!", s[-1]) != NULL;
        if (isop && !prev && k && a[k - 1] != ' ') a[k++] = ' ';
        if (!isop && prev && *s != ' ') a[k++] = ' ';
        a[k++] = *s;
    }
    a[k] = 0;
    out[0] = 0;
    switch (x->ok ? DSL_DEP_OK : x->why) {
    case DSL_DEP_MISSING:     snprintf(out, cap, TR("needs %s, which is not installed"), a); break;
    case DSL_DEP_OFF:         snprintf(out, cap, TR("needs %s, which is turned off"), a); break;
    case DSL_DEP_NOT_RUNNING: snprintf(out, cap, TR("needs %s, which cannot run"), a); break;
    case DSL_DEP_VERSION:     snprintf(out, cap, TR("needs %s, but version %s is installed"), a, x->have[0] ? x->have : "?"); break;
    case DSL_DEP_OPT_VERSION: snprintf(out, cap, TR("works only with %s, but version %s is installed"), a, x->have[0] ? x->have : "?"); break;
    case DSL_DEP_LOADER:      snprintf(out, cap, "%s", TR("needs a newer version of Dungeon Settlers Modded")); break;
    case DSL_DEP_CONFLICT:    snprintf(out, cap, TR("cannot run together with %s"), a); break;
    }
    dsi_utf8_fix_end(out);
    return out;
}
