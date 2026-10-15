/* test_ord.c - ord_core round trips with fake game objects (no game needed).
 * For every Tier A event type and every Tier B order kind: fill random values -> serialize -> deserialize
 * -> compare field by field -> serialize again -> identical bytes. Plus truncation / garbage robustness.
 * Run: ./run_tests.sh */
#include "../ord_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------------------------------------------------------------- fake objects: klass tag first, like il2cpp */
enum { K_EV = 1, K_STR, K_ENT, K_LGUID, K_TRADE };
typedef struct { int k; void *pad; char s[ORD_STR]; } FStr;
typedef struct { int k; void *pad; ord_guid g; int map; } FEnt;
typedef struct { int k; void *pad; int n; ord_guid g[ORD_MAX_LIST]; } FList;
typedef struct { int k; void *pad; ord_trade t; } FTrade;

#define NENT 40
static FEnt ents[NENT];
static void *allocs[100000];
static int nallocs;
static void *track(void *p) { if (nallocs < 100000) allocs[nallocs++] = p; return p; }
static void free_all(void) { for (int i = 0; i < nallocs; i++) free(allocs[i]); nallocs = 0; }

static int f_ent_guid(ord_obj e, ord_guid *out) {
    if (!e || ((FEnt *)e)->k != K_ENT) return 0;
    *out = ((FEnt *)e)->g; return 1;
}
static ord_obj f_ent_find(int map, const ord_guid *g) {
    for (int i = 0; i < NENT; i++) if (!memcmp(&ents[i].g, g, 16) && ents[i].map == map) return &ents[i];
    return NULL;
}
static int f_str_get(ord_obj s, char *out, size_t cap) {
    if (!s) { out[0] = 0; return -1; }
    snprintf(out, cap, "%s", ((FStr *)s)->s); return (int)strlen(out);
}
static ord_obj f_str_new(const char *s) { FStr *o = track(calloc(1, sizeof *o)); o->k = K_STR; snprintf(o->s, sizeof o->s, "%s", s); return o; }
static ord_obj f_ev_new(const ord_evdef *d) { (void)d; int *o = track(calloc(1, 512)); o[0] = K_EV; return o; }
static int f_lguid_get(ord_obj l, ord_guid *out, int max) {
    if (!l) return -1;
    FList *f = l; int n = f->n < max ? f->n : max;
    memcpy(out, f->g, sizeof(ord_guid) * (size_t)n); return n;
}
static ord_obj f_lguid_new(const ord_guid *g, int n) {
    FList *f = track(calloc(1, sizeof *f)); f->k = K_LGUID; f->n = n; memcpy(f->g, g, sizeof(ord_guid) * (size_t)n); return f;
}
static int f_trade_get(ord_obj r, ord_trade *t) { *t = ((FTrade *)r)->t; return 0; }
static ord_obj f_trade_new(const ord_trade *t) { FTrade *f = track(calloc(1, sizeof *f)); f->k = K_TRADE; f->t = *t; return f; }
static int setrefs;
static void f_set_ref(ord_obj o, void *slot, ord_obj v) { (void)o; *(ord_obj *)slot = v; setrefs++; }
static const ord_env fenv = { f_ent_guid, f_ent_find, f_str_get, f_str_new, f_ev_new, f_lguid_get, f_lguid_new,
                              f_trade_get, f_trade_new, f_set_ref };

/* ---------------------------------------------------------------- random */
static uint64_t rs = 0x9E3779B97F4A7C15ULL;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)rs; }
static void rguid(ord_guid *g) { for (int i = 0; i < 16; i++) g->b[i] = (uint8_t)rnd(); }
static void rstr(char *s, int maxlen) {
    int n = (int)(rnd() % (unsigned)maxlen);
    for (int i = 0; i < n; i++) s[i] = (char)('a' + rnd() % 26);
    s[n] = 0;
    if (n > 3 && rnd() % 3 == 0) memcpy(s, "\xC3\xA9t\xC3\xA9", 5 < n ? 5 : n);   /* some utf8 */
}
static FEnt *rent(int map) { FEnt *e = &ents[rnd() % NENT]; e->map = map; return e; }

/* ---------------------------------------------------------------- Tier A: every event type */
static void fill_event(const ord_evdef *d, char *ev, int map) {
    for (int i = 0; i < d->nf; i++) {
        char *p = ev + d->f[i].off;
        switch (d->f[i].kind) {
        case ORD_F_I32: *(int32_t *)p = (int32_t)rnd(); break;
        case ORD_F_V2I: ((int32_t *)p)[0] = (int32_t)rnd(); ((int32_t *)p)[1] = (int32_t)rnd(); break;
        case ORD_F_U8: *(uint8_t *)p = (uint8_t)(rnd() & 1); break;
        case ORD_F_GUID: rguid((ord_guid *)p); break;
        case ORD_F_NGUID: p[0] = (char)(rnd() & 1); rguid((ord_guid *)(p + 4)); if (!p[0]) memset(p + 4, 0, 16); break;
        case ORD_F_NI32: p[0] = (char)(rnd() & 1); *(int32_t *)(p + 4) = p[0] ? (int32_t)rnd() : 0; break;
        case ORD_F_NU8: p[0] = (char)(rnd() & 1); p[1] = p[0] ? (char)(rnd() & 1) : 0; break;
        case ORD_F_STR: { if (rnd() % 5 == 0) { *(ord_obj *)p = NULL; break; } char s[64]; rstr(s, 60); *(ord_obj *)p = f_str_new(s); break; }
        case ORD_F_ENT: *(ord_obj *)p = rnd() % 4 ? (ord_obj)rent(map) : NULL; break;
        case ORD_F_LGUID: {
            if (rnd() % 5 == 0) { *(ord_obj *)p = NULL; break; }
            ord_guid g[8]; int n = (int)(rnd() % 8); for (int k = 0; k < n; k++) rguid(&g[k]);
            *(ord_obj *)p = f_lguid_new(g, n); break;
        }
        case ORD_F_TRADE: {
            ord_trade *t = calloc(1, sizeof *t);
            t->map = (int32_t)rnd(); rguid(&t->session); t->n = (int)(rnd() % 6);
            for (int k = 0; k < t->n; k++) { rguid(&t->d[k].slot); t->d[k].dest = (int32_t)rnd(); rguid(&t->d[k].owner); t->d[k].amount = (int32_t)rnd(); }
            *(ord_obj *)p = f_trade_new(t); free(t); break;
        }
        }
    }
}
static int same_field(int kind, const char *a, const char *b) {
    switch (kind) {
    case ORD_F_I32: return *(int32_t *)a == *(int32_t *)b;
    case ORD_F_V2I: return !memcmp(a, b, 8);
    case ORD_F_U8: return *(uint8_t *)a == *(uint8_t *)b;
    case ORD_F_GUID: return !memcmp(a, b, 16);
    case ORD_F_NGUID: return a[0] == b[0] && (!a[0] || !memcmp(a + 4, b + 4, 16));
    case ORD_F_NI32: return a[0] == b[0] && (!a[0] || *(int32_t *)(a + 4) == *(int32_t *)(b + 4));
    case ORD_F_NU8: return a[0] == b[0] && (!a[0] || a[1] == b[1]);
    case ORD_F_STR: {
        ord_obj x = *(ord_obj *)a, y = *(ord_obj *)b;
        return (!x && !y) || (x && y && !strcmp(((FStr *)x)->s, ((FStr *)y)->s));
    }
    case ORD_F_ENT: return *(ord_obj *)a == *(ord_obj *)b;   /* same fake entity resolved back */
    case ORD_F_LGUID: {
        FList *x = *(FList **)a, *y = *(FList **)b;
        return (!x && !y) || (x && y && x->n == y->n && !memcmp(x->g, y->g, sizeof(ord_guid) * (size_t)x->n));
    }
    case ORD_F_TRADE: {
        FTrade *x = *(FTrade **)a, *y = *(FTrade **)b;
        if (!x || !y) return !x && !y;
        if (x->t.map != y->t.map || memcmp(&x->t.session, &y->t.session, 16) || x->t.n != y->t.n) return 0;
        for (int k = 0; k < x->t.n; k++)
            if (memcmp(&x->t.d[k].slot, &y->t.d[k].slot, 16) || x->t.d[k].dest != y->t.d[k].dest ||
                memcmp(&x->t.d[k].owner, &y->t.d[k].owner, 16) || x->t.d[k].amount != y->t.d[k].amount) return 0;
        return 1;
    }
    }
    return 0;
}

static void fill_ctx(ord_ctx *c) {
    c->map = (int32_t)(rnd() % 4); c->sel_group = (int32_t)(rnd() % 4000);
    c->nsel = (int)(rnd() % 5);
    for (int i = 0; i < c->nsel; i++) c->sel[i] = rent(c->map)->g;
}
static int same_ctx(const ord_ctx *a, const ord_ctx *b) {
    return a->map == b->map && a->sel_group == b->sel_group && a->nsel == b->nsel &&
           !memcmp(a->sel, b->sel, sizeof(ord_guid) * (size_t)a->nsel);
}

/* serialize, parse, re-serialize: identical bytes; returns the parsed message (caller frees) */
static ord_msg *roundtrip(const ord_msg *m, const char *what, uint8_t **bytes, size_t *n) {
    ord_wbuf w = { 0 };
    int rc = ord_msg_write(&fenv, &w, m);
    CHECK(rc == 0, "%s: write rc %d", what, rc);
    ord_msg *r = calloc(1, sizeof *r);
    rc = ord_msg_read(&fenv, w.p, w.n, r);
    CHECK(rc == 0, "%s: read rc %d", what, rc);
    ord_wbuf w2 = { 0 };
    rc = ord_msg_write(&fenv, &w2, r);
    CHECK(rc == 0 && w2.n == w.n && !memcmp(w.p, w2.p, w.n), "%s: re-serialized bytes differ (%zu vs %zu)", what, w.n, w2.n);
    free(w2.p);
    *bytes = w.p; *n = w.n;
    return r;
}

/* every truncation must fail cleanly; random corruption must never crash */
static void robustness(const uint8_t *b, size_t n, const char *what) {
    ord_msg *r = calloc(1, sizeof *r);
    for (size_t k = 0; k < n; k++) {
        int rc = ord_msg_read(&fenv, b, k, r);
        CHECK(rc != 0, "%s: truncated to %zu/%zu bytes accepted", what, k, n);
    }
    uint8_t *c = malloc(n + 1);
    for (int t = 0; t < 200; t++) {
        memcpy(c, b, n);
        for (int j = 0; j < 3; j++) c[4 + rnd() % (n > 4 ? n - 4 : 1)] ^= (uint8_t)(1 + rnd() % 255);
        ord_msg_read(&fenv, c, n, r);                 /* result irrelevant: must not crash */
    }
    memcpy(c, b, n); c[n] = 0;
    CHECK(ord_msg_read(&fenv, c, n + 1, r) == -9, "%s: trailing byte accepted", what);
    free(c); free(r);
}

static void test_events(void) {
    for (int i = 0; i < ord_event_count; i++) {
        const ord_evdef *d = &ord_event_table[i];
        if (d->flags & ORD_EF_BLOCK) {
            ord_msg m = { 0 }; m.kind = ORD_K_EVENT; m.ev = d; m.evobj = f_ev_new(d);
            ord_wbuf w = { 0 }; ord_msg_write(&fenv, &w, &m);
            ord_msg *r = calloc(1, sizeof *r);
            CHECK(ord_msg_read(&fenv, w.p, w.n, r) == -5, "%s: blocked event accepted on read", d->name);
            free(r); free(w.p);
            continue;
        }
        for (int rep = 0; rep < 50; rep++) {
            ord_msg *m = calloc(1, sizeof *m);
            m->kind = ORD_K_EVENT; fill_ctx(&m->ctx); m->ev = d;
            m->evobj = f_ev_new(d);
            fill_event(d, m->evobj, m->ctx.map);
            if (d->flags & ORD_EF_RAYCAST) { m->has_ray = (uint8_t)(rnd() & 1); if (m->has_ray) rguid(&m->ray); }
            if (d->flags & ORD_EF_PARTY) { m->party_active = rnd() & 1; m->nparty = (int)(rnd() % 4); for (int k = 0; k < m->nparty; k++) rguid(&m->party[k]); }
            uint8_t *b; size_t n;
            ord_msg *r = roundtrip(m, d->name, &b, &n);
            CHECK(r->ev == d, "%s: event type changed", d->name);
            CHECK(same_ctx(&m->ctx, &r->ctx), "%s: ctx differs", d->name);
            for (int f = 0; f < d->nf && r->evobj; f++)
                CHECK(same_field(d->f[f].kind, (char *)m->evobj + d->f[f].off, (char *)r->evobj + d->f[f].off),
                      "%s: field %d (kind %d @0x%X) differs", d->name, f, d->f[f].kind, d->f[f].off);
            if (d->flags & ORD_EF_RAYCAST)
                CHECK(r->has_ray == m->has_ray && (!m->has_ray || !memcmp(&r->ray, &m->ray, 16)), "%s: raycast differs", d->name);
            if (d->flags & ORD_EF_PARTY)
                CHECK(r->party_active == m->party_active && r->nparty == m->nparty && !memcmp(r->party, m->party, 16 * (size_t)m->nparty), "%s: party differs", d->name);
            ord_guid o1, o2; ord_owner(&fenv, m, &o1); ord_owner(&fenv, r, &o2);
            CHECK(!memcmp(&o1, &o2, 16), "%s: owner differs after round trip", d->name);
            if (rep == 0) robustness(b, n, d->name);
            free(b); free(r); free(m);
        }
    }
}

/* ---------------------------------------------------------------- Interaction memory round trip */
static void fill_ia(ord_ia *o, int map) {
    memset(o, 0, sizeof *o);
    o->type = (int32_t)(rnd() % 60); o->show_user = rnd() & 1; o->hide_target = rnd() & 1; o->durability = (int32_t)rnd();
    o->target_text_null = rnd() % 4 == 0; if (!o->target_text_null) rstr(o->target_text, 40);
    o->invalid_msg_null = rnd() % 4 == 0; if (!o->invalid_msg_null) rstr(o->invalid_msg, 40);
    o->is_possible = rnd() & 1;
    if ((o->has_user = rnd() & 1)) o->user = rent(map)->g;
    if ((o->has_target = rnd() & 1)) o->target = rent(map)->g;
    if ((o->has_item = rnd() & 1)) o->item = rent(map)->g;
    o->index = (int32_t)rnd(); o->input_type = (int32_t)rnd();
    o->ui_type = (int32_t)rnd(); o->p1 = (int32_t)rnd(); o->p2 = (int32_t)rnd(); o->p3 = (int32_t)rnd();
    o->ps1_null = rnd() % 3 == 0; if (!o->ps1_null) rstr(o->ps1, 30);
    o->ps2_null = rnd() % 3 == 0; if (!o->ps2_null) rstr(o->ps2, 30);
    rguid(&o->pg1); rguid(&o->pg2); if ((o->pg3_has = rnd() & 1)) rguid(&o->pg3);
    o->pb1 = rnd() & 1; o->pb2 = rnd() & 1; o->pb3 = rnd() & 1;
    o->task_type = (int32_t)rnd();
}
static void test_interaction_mem(void) {
    for (int rep = 0; rep < 200; rep++) {
        int map = (int)(rnd() % 4);
        ord_ia a, b;
        fill_ia(&a, map);
        uint8_t mem[ORD_IA_SIZE] __attribute__((aligned(8))) = { 0 };
        ord_ia_to_mem(&fenv, &a, map, mem);
        ord_ia_from_mem(&fenv, mem, &b);
        CHECK(!memcmp(&a, &b, sizeof a), "Interaction mem round trip differs (rep %d)", rep);
    }
}

/* ---------------------------------------------------------------- Tier B kinds */
static void fill_sel(ord_seldesc *s, int map) {
    memset(s, 0, sizeof *s);
    s->kind = (uint8_t)(rnd() % 8);
    uint8_t k = s->kind;
    if (k == ORD_SEL_TO_GRID) { s->inner = (uint8_t)(1 + rnd() % 5); k = s->inner; }
    if (k == ORD_SEL_SPECIFIC || k == ORD_SEL_SPECIFIC_USABLE) { s->has_ent = rnd() & 1; if (s->has_ent) s->ent = rent(map)->g; }
    if (k == ORD_SEL_FROM_LIST) { s->n = (int)(rnd() % 6); for (int i = 0; i < s->n; i++) s->list[i] = rent(map)->g; }
    if (k == ORD_SEL_GRID) { s->x = (int32_t)rnd(); s->y = (int32_t)rnd(); s->flag = (int32_t)rnd(); }
}
static void test_tierb(void) {
    static const char *names[] = { "", "EVENT", "MOVE_ALL", "MOVE_TRY", "MOVE_NEAR", "WP_RESERVE", "WP_CLEAR", "RUN",
                                   "ACTION", "USE_SKILL", "AUTO_SKILL", "OBSERVER", "MODE_CLICK", "MODE_DRAG" };
    for (int kind = ORD_K_MOVE_ALL; kind < ORD_K_MAX; kind++) {
        for (int rep = 0; rep < 100; rep++) {
            ord_msg *m = calloc(1, sizeof *m);
            m->kind = (uint16_t)kind; fill_ctx(&m->ctx);
            int map = m->ctx.map;
            switch (kind) {
            case ORD_K_MOVE_ALL: case ORD_K_WP_RESERVE: m->x = (int32_t)rnd(); m->y = (int32_t)rnd(); break;
            case ORD_K_MOVE_NEAR: m->unit = rent(map)->g; m->x = (int32_t)rnd(); m->y = (int32_t)rnd(); break;
            case ORD_K_RUN:
                m->x = (int32_t)rnd(); m->y = (int32_t)rnd();
                m->nact = (int)(rnd() % 6);
                for (int i = 0; i < m->nact; i++) { m->act[i].unit = rent(map)->g; fill_ia(&m->act[i].ia, map); }
                m->nnear = (int)(rnd() % 4);
                for (int i = 0; i < m->nnear; i++) m->near_units[i] = rent(map)->g;
                break;
            case ORD_K_ACTION: m->nact = 1; m->act[0].unit = rent(map)->g; fill_ia(&m->act[0].ia, map); break;
            case ORD_K_USE_SKILL:
                if ((m->has_user = rnd() & 1)) m->user = rent(map)->g;
                if ((m->has_target = rnd() & 1)) m->target = rent(map)->g;
                if ((m->has_item = rnd() & 1)) m->item = rent(map)->g;
                rstr(m->key, 40); fill_sel(&m->sel, map);
                break;
            case ORD_K_AUTO_SKILL: m->unit = rent(map)->g; rstr(m->key, 40); break;
            case ORD_K_OBSERVER: m->input_type = (int32_t)(rnd() % 8); break;
            case ORD_K_MODE_CLICK: case ORD_K_MODE_DRAG:
                m->mode = 1 + (int)(rnd() % 5); m->x = (int32_t)rnd(); m->y = (int32_t)rnd();
                if (kind == ORD_K_MODE_DRAG) { m->x2 = (int32_t)rnd(); m->y2 = (int32_t)rnd(); } else m->shift = rnd() & 1;
                m->has_ckey = rnd() & 1; if (m->has_ckey) rstr(m->key, 40);
                m->rotation = (int32_t)(rnd() % 4); m->dsx = (int32_t)rnd(); m->dsy = (int32_t)rnd();
                break;
            }
            uint8_t *b; size_t n;
            ord_msg *r = roundtrip(m, names[kind], &b, &n);
            CHECK(r->kind == m->kind && same_ctx(&m->ctx, &r->ctx), "%s: kind/ctx differ", names[kind]);
            CHECK(r->x == m->x && r->y == m->y && !memcmp(&r->unit, &m->unit, 16), "%s: pos/unit differ", names[kind]);
            CHECK(r->nact == m->nact && r->nnear == m->nnear, "%s: counts differ", names[kind]);
            for (int i = 0; i < m->nact; i++)
                CHECK(!memcmp(&r->act[i], &m->act[i], sizeof m->act[i]), "%s: act %d differs", names[kind], i);
            CHECK(!memcmp(r->near_units, m->near_units, sizeof(ord_guid) * (size_t)m->nnear), "%s: near list differs", names[kind]);
            CHECK(r->has_user == m->has_user && r->has_target == m->has_target && r->has_item == m->has_item &&
                  !memcmp(&r->user, &m->user, 16) && !memcmp(&r->target, &m->target, 16) && !memcmp(&r->item, &m->item, 16),
                  "%s: skill entities differ", names[kind]);
            CHECK(!strcmp(r->key, m->key), "%s: key differs", names[kind]);
            CHECK(!memcmp(&r->sel, &m->sel, sizeof m->sel), "%s: selector differs", names[kind]);
            CHECK(r->input_type == m->input_type, "%s: input type differs", names[kind]);
            CHECK(r->mode == m->mode && r->x2 == m->x2 && r->y2 == m->y2 && r->shift == m->shift && r->has_ckey == m->has_ckey &&
                  r->rotation == m->rotation && r->dsx == m->dsx && r->dsy == m->dsy, "%s: build-mode fields differ", names[kind]);
            if (rep == 0) robustness(b, n, names[kind]);
            free(b); free(r); free(m);
        }
    }
}

/* ---------------------------------------------------------------- header / version / limits */
static void test_header(void) {
    ord_msg *m = calloc(1, sizeof *m), *r = calloc(1, sizeof *r);
    m->kind = ORD_K_MOVE_TRY;
    ord_wbuf w = { 0 };
    CHECK(ord_msg_write(&fenv, &w, m) == 0, "header write");
    CHECK(w.n >= 4 && w.p[0] == ORD_MAGIC && w.p[1] == ORD_VERSION, "magic/version bytes");
    w.p[1] = ORD_VERSION + 1;
    CHECK(ord_msg_read(&fenv, w.p, w.n, r) == -2, "other wire version rejected");
    w.p[1] = ORD_VERSION; w.p[2] = 99;
    CHECK(ord_msg_read(&fenv, w.p, w.n, r) == -3, "unknown kind rejected");
    free(w.p);
    m->kind = 0; ord_wbuf w2 = { 0 };
    CHECK(ord_msg_write(&fenv, &w2, m) != 0, "kind 0 refused on write");
    free(w2.p);
    m->kind = ORD_K_RUN; m->nact = ORD_MAX_ACTS + 1; ord_wbuf w3 = { 0 };
    CHECK(ord_msg_write(&fenv, &w3, m) != 0, "too many actions refused on write");
    free(w3.p);
    /* stable ids: unique, never 0 */
    for (int i = 0; i < ord_event_count; i++) {
        CHECK(ord_event_table[i].id != 0, "id 0");
        for (int j = i + 1; j < ord_event_count; j++) CHECK(ord_event_table[i].id != ord_event_table[j].id, "duplicate id %d", ord_event_table[i].id);
        CHECK(ord_event_by_id(ord_event_table[i].id) == &ord_event_table[i], "by_id");
        CHECK(ord_event_by_name(ord_event_table[i].name) == &ord_event_table[i], "by_name");
    }
    free(m); free(r);
}

static void test_map_kind(void) {
    ord_msg m = { 0 }; m.kind = ORD_K_EVENT;
    m.ev = ord_event_by_name("ChangeMapRequested"); m.evobj = f_ev_new(m.ev);
    *(int32_t *)((char *)m.evobj + 0x14) = 1; CHECK(ord_map_change_kind(&m) == 1, "to dungeon");
    *(int32_t *)((char *)m.evobj + 0x14) = 0; CHECK(ord_map_change_kind(&m) == 2, "to base");
    *(int32_t *)((char *)m.evobj + 0x14) = -1; CHECK(ord_map_change_kind(&m) == 0, "to none");
    m.ev = ord_event_by_name("GoDownDungeonRequested"); CHECK(ord_map_change_kind(&m) == 3, "next floor");
    m.ev = ord_event_by_name("DirectTaskRequested"); CHECK(ord_map_change_kind(&m) == 0, "not a map change");
    m.kind = ORD_K_MOVE_ALL; CHECK(ord_map_change_kind(&m) == 0, "tier B");
    /* Set 2 flags */
    CHECK(ord_event_by_name("CandidateRecruitRequested")->flags & ORD_EF_RECRUIT, "recruit flag");
    CHECK(ord_event_by_name("ExpeditionTransferItemRequested")->flags & ORD_EF_PARTY, "party flag");
    CHECK(ord_event_by_name("EquipItemRequested")->flags & ORD_EF_EQUIP, "equip flag");
    CHECK(ord_event_by_name("ExpeditionEquipFromGroundRequested")->flags == (ORD_EF_PARTY | ORD_EF_EQUIP), "party+equip flags");
    /* bad build mode refused */
    ord_msg b = { 0 }; b.kind = ORD_K_MODE_CLICK; b.mode = 0; ord_wbuf w = { 0 };
    CHECK(ord_msg_write(&fenv, &w, &b) != 0, "mode 0 refused"); free(w.p);
    const ord_evdef *cm = ord_event_by_name("ChangeMapRequested");
    CHECK(cm && cm->f[1].off == 0x14 && cm->f[1].kind == ORD_F_I32, "ChangeMapRequested.ToMap offset");
}

int main(void) {
    test_map_kind();
    for (int i = 0; i < NENT; i++) { ents[i].k = K_ENT; rguid(&ents[i].g); }
    test_header();
    test_events();
    test_interaction_mem();
    test_tierb();
    free_all();
    printf("%s: %d checks, %d failures (%d event types, %d set_ref stores)\n", fails ? "FAILED" : "OK", checks, fails,
           ord_event_count, setrefs);
    return fails ? 1 : 0;
}
