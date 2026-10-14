/* ord_core.c - wire format, event table and (de)serializers. No game dependencies (see ord_core.h). */
#include "ord_core.h"
#include <stdlib.h>
#include <string.h>

#include "ord_events_gen.h"
const ord_evdef *const ord_event_table = ord_events;
const int ord_event_count = ORD_NEVENTS;

const ord_evdef *ord_event_by_id(uint16_t id) {
    for (int i = 0; i < ORD_NEVENTS; i++) if (ord_events[i].id == id) return &ord_events[i];
    return NULL;
}
const ord_evdef *ord_event_by_name(const char *name) {
    for (int i = 0; i < ORD_NEVENTS; i++) if (!strcmp(ord_events[i].name, name)) return &ord_events[i];
    return NULL;
}

/* ---------------------------------------------------------------- buffers */
static void wput(ord_wbuf *w, const void *src, size_t n) {
    if (w->err) return;
    if (w->n + n > w->cap) {
        size_t c = w->cap ? w->cap * 2 : 256;
        while (c < w->n + n) c *= 2;
        uint8_t *p = realloc(w->p, c);
        if (!p) { w->err = 1; return; }
        w->p = p; w->cap = c;
    }
    memcpy(w->p + w->n, src, n);
    w->n += n;
}
void ow_u8(ord_wbuf *w, uint8_t v) { wput(w, &v, 1); }
void ow_u16(ord_wbuf *w, uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; wput(w, b, 2); }
void ow_i32(ord_wbuf *w, int32_t v) {
    uint32_t u = (uint32_t)v; uint8_t b[4] = { (uint8_t)u, (uint8_t)(u >> 8), (uint8_t)(u >> 16), (uint8_t)(u >> 24) };
    wput(w, b, 4);
}
void ow_guid(ord_wbuf *w, const ord_guid *g) { wput(w, g->b, 16); }
void ow_str(ord_wbuf *w, const char *s) {
    if (!s) { ow_u16(w, 0xFFFF); return; }
    size_t n = strlen(s);
    if (n >= 0xFFFF) { w->err = 1; return; }
    ow_u16(w, (uint16_t)n); wput(w, s, n);
}

static const uint8_t *rget(ord_rbuf *r, size_t n) {
    if (r->err || r->i + n > r->n) { r->err = 1; return NULL; }
    const uint8_t *p = r->p + r->i; r->i += n; return p;
}
uint8_t or_u8(ord_rbuf *r) { const uint8_t *p = rget(r, 1); return p ? p[0] : 0; }
uint16_t or_u16(ord_rbuf *r) { const uint8_t *p = rget(r, 2); return p ? (uint16_t)(p[0] | p[1] << 8) : 0; }
int32_t or_i32(ord_rbuf *r) {
    const uint8_t *p = rget(r, 4);
    return p ? (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24) : 0;
}
void or_guid(ord_rbuf *r, ord_guid *g) { const uint8_t *p = rget(r, 16); if (p) memcpy(g->b, p, 16); else memset(g, 0, 16); }
int or_str(ord_rbuf *r, char *out, size_t cap) {
    uint16_t n = or_u16(r);
    if (r->err) { out[0] = 0; return -1; }
    if (n == 0xFFFF) { out[0] = 0; return -1; }
    const uint8_t *p = rget(r, n);
    if (!p || n >= cap) { r->err = 1; out[0] = 0; return -1; }
    memcpy(out, p, n); out[n] = 0;
    return n;
}

/* ---------------------------------------------------------------- small helpers */
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
static void ow_optguid(ord_wbuf *w, int has, const ord_guid *g) { ow_u8(w, has ? 1 : 0); if (has) ow_guid(w, g); }
static uint8_t or_optguid(ord_rbuf *r, ord_guid *g) {
    uint8_t h = or_u8(r);
    if (h > 1) r->err = 1;
    if (h == 1) or_guid(r, g); else memset(g, 0, 16);
    return h == 1;
}
static void ow_objstr(const ord_env *env, ord_wbuf *w, ord_obj s) {
    char b[ORD_STR];
    if (env->str_get(s, b, sizeof b) < 0) ow_str(w, NULL); else ow_str(w, b);
}
static ord_obj or_objstr(const ord_env *env, ord_rbuf *r) {
    char b[ORD_STR];
    return or_str(r, b, sizeof b) < 0 ? NULL : env->str_new(b);
}
static void ow_ent(const ord_env *env, ord_wbuf *w, ord_obj e) {
    ord_guid g; int ok = e && env->ent_guid(e, &g);
    ow_optguid(w, ok, &g);
}
static ord_obj or_ent(const ord_env *env, ord_rbuf *r, int map) {
    ord_guid g;
    return or_optguid(r, &g) ? env->ent_find(map, &g) : NULL;
}

/* ---------------------------------------------------------------- Tier A events */
int ord_ev_write(const ord_env *env, ord_wbuf *w, const ord_evdef *d, ord_obj ev) {
    for (int i = 0; i < d->nf; i++) {
        const ord_field *f = &d->f[i];
        char *p = (char *)ev + f->off;
        switch (f->kind) {
        case ORD_F_I32: ow_i32(w, *(int32_t *)p); break;
        case ORD_F_U8: ow_u8(w, *(uint8_t *)p ? 1 : 0); break;
        case ORD_F_GUID: ow_guid(w, (ord_guid *)p); break;
        case ORD_F_NGUID: ow_optguid(w, p[0], (ord_guid *)(p + 4)); break;       /* Nullable<Guid>: hasValue@0, value@4 */
        case ORD_F_NI32: ow_u8(w, p[0] ? 1 : 0); if (p[0]) ow_i32(w, *(int32_t *)(p + 4)); break;
        case ORD_F_NU8: ow_u8(w, p[0] ? 1 : 0); if (p[0]) ow_u8(w, p[1] ? 1 : 0); break;
        case ORD_F_STR: ow_objstr(env, w, *(ord_obj *)p); break;
        case ORD_F_ENT: ow_ent(env, w, *(ord_obj *)p); break;
        case ORD_F_LGUID: {
            ord_guid g[ORD_MAX_LIST];
            int n = env->lguid_get(*(ord_obj *)p, g, ORD_MAX_LIST);
            if (n < 0) { ow_u16(w, 0xFFFF); break; }
            ow_u16(w, (uint16_t)n);
            for (int k = 0; k < n; k++) ow_guid(w, &g[k]);
            break;
        }
        case ORD_F_V2I: ow_i32(w, ((int32_t *)p)[0]); ow_i32(w, ((int32_t *)p)[1]); break;
        case ORD_F_TRADE: {
            ord_trade *t = calloc(1, sizeof *t);
            if (!t) return -1;
            ord_obj req = *(ord_obj *)p;
            if (!req || env->trade_get(req, t)) { free(t); ow_u8(w, 0); break; }
            ow_u8(w, 1); ow_i32(w, t->map); ow_guid(w, &t->session); ow_u16(w, (uint16_t)t->n);
            for (int k = 0; k < t->n; k++) {
                ow_guid(w, &t->d[k].slot); ow_i32(w, t->d[k].dest); ow_guid(w, &t->d[k].owner); ow_i32(w, t->d[k].amount);
            }
            free(t);
            break;
        }
        default: return -1;
        }
    }
    return w->err ? -1 : 0;
}

ord_obj ord_ev_read(const ord_env *env, ord_rbuf *r, const ord_evdef *d, int map) {
    ord_obj ev = env->ev_new(d);
    if (!ev) return NULL;
    for (int i = 0; i < d->nf && !r->err; i++) {
        const ord_field *f = &d->f[i];
        char *p = (char *)ev + f->off;
        switch (f->kind) {
        case ORD_F_I32: *(int32_t *)p = or_i32(r); break;
        case ORD_F_U8: *(uint8_t *)p = or_u8(r); break;
        case ORD_F_GUID: or_guid(r, (ord_guid *)p); break;
        case ORD_F_NGUID: p[0] = (char)or_optguid(r, (ord_guid *)(p + 4)); break;
        case ORD_F_NI32: p[0] = (char)(or_u8(r) == 1); if (p[0]) *(int32_t *)(p + 4) = or_i32(r); break;
        case ORD_F_NU8: p[0] = (char)(or_u8(r) == 1); if (p[0]) p[1] = (char)or_u8(r); break;
        case ORD_F_STR: env->set_ref(ev, p, or_objstr(env, r)); break;
        case ORD_F_ENT: env->set_ref(ev, p, or_ent(env, r, map)); break;
        case ORD_F_LGUID: {
            uint16_t n = or_u16(r);
            if (n == 0xFFFF) break;   /* stays null */
            if (n > ORD_MAX_LIST) { r->err = 1; break; }
            ord_guid g[ORD_MAX_LIST];
            for (int k = 0; k < n; k++) or_guid(r, &g[k]);
            if (!r->err) env->set_ref(ev, p, env->lguid_new(g, n));
            break;
        }
        case ORD_F_V2I: ((int32_t *)p)[0] = or_i32(r); ((int32_t *)p)[1] = or_i32(r); break;
        case ORD_F_TRADE: {
            if (!or_u8(r)) break;   /* stays null */
            ord_trade *t = calloc(1, sizeof *t);
            if (!t) { r->err = 1; break; }
            t->map = or_i32(r); or_guid(r, &t->session); t->n = or_u16(r);
            if (t->n > ORD_MAX_DELIV) { r->err = 1; free(t); break; }
            for (int k = 0; k < t->n; k++) {
                or_guid(r, &t->d[k].slot); t->d[k].dest = or_i32(r); or_guid(r, &t->d[k].owner); t->d[k].amount = or_i32(r);
            }
            if (!r->err) env->set_ref(ev, p, env->trade_new(t));
            free(t);
            break;
        }
        default: r->err = 1;
        }
    }
    return r->err ? NULL : ev;
}

/* ---------------------------------------------------------------- Interaction <-> memory
 * Refactor.Control.Interaction (dump.cs TypeDefIndex 5502) + UIInput (InteractionUIInput @0x48). */
enum {
    IA_TYPE = 0x0, IA_SHOW_USER = 0x4, IA_HIDE_TARGET = 0x5, IA_DURABILITY = 0x8, IA_TARGET_TEXT = 0x10,
    IA_INVALID_MSG = 0x18, IA_IS_POSSIBLE = 0x20, IA_USER = 0x28, IA_TARGET = 0x30, IA_ITEM = 0x38,
    IA_INDEX = 0x40, IA_INPUT_TYPE = 0x44, IA_UI = 0x48, IA_TASK_TYPE = 0xD0,
    UI_TYPE = 0x0, UI_P1 = 0x10, UI_P2 = 0x14, UI_P3 = 0x18, UI_PS1 = 0x20, UI_PS2 = 0x28,
    UI_PG1 = 0x30, UI_PG2 = 0x40, UI_PG3 = 0x50, UI_PB1 = 0x70, UI_PB2 = 0x71, UI_PB3 = 0x72
};
static void str_in(const ord_env *env, ord_obj s, char *out, int *isnull) {
    *isnull = env->str_get(s, out, ORD_STR) < 0;
    if (*isnull) out[0] = 0;
}
void ord_ia_from_mem(const ord_env *env, const void *mem, ord_ia *o) {
    const char *m = mem, *u = m + IA_UI;
    memset(o, 0, sizeof *o);
    o->type = AT(m, IA_TYPE, int32_t); o->show_user = AT(m, IA_SHOW_USER, uint8_t); o->hide_target = AT(m, IA_HIDE_TARGET, uint8_t);
    o->durability = AT(m, IA_DURABILITY, int32_t);
    str_in(env, AT(m, IA_TARGET_TEXT, ord_obj), o->target_text, &o->target_text_null);
    str_in(env, AT(m, IA_INVALID_MSG, ord_obj), o->invalid_msg, &o->invalid_msg_null);
    o->is_possible = AT(m, IA_IS_POSSIBLE, uint8_t);
    ord_obj e;
    e = AT(m, IA_USER, ord_obj); o->has_user = e && env->ent_guid(e, &o->user);
    e = AT(m, IA_TARGET, ord_obj); o->has_target = e && env->ent_guid(e, &o->target);
    e = AT(m, IA_ITEM, ord_obj); o->has_item = e && env->ent_guid(e, &o->item);
    o->index = AT(m, IA_INDEX, int32_t); o->input_type = AT(m, IA_INPUT_TYPE, int32_t);
    o->ui_type = AT(u, UI_TYPE, int32_t); o->p1 = AT(u, UI_P1, int32_t); o->p2 = AT(u, UI_P2, int32_t); o->p3 = AT(u, UI_P3, int32_t);
    str_in(env, AT(u, UI_PS1, ord_obj), o->ps1, &o->ps1_null);
    str_in(env, AT(u, UI_PS2, ord_obj), o->ps2, &o->ps2_null);
    memcpy(&o->pg1, u + UI_PG1, 16); memcpy(&o->pg2, u + UI_PG2, 16);
    o->pg3_has = u[UI_PG3] ? 1 : 0; if (o->pg3_has) memcpy(&o->pg3, u + UI_PG3 + 4, 16);
    o->pb1 = u[UI_PB1] ? 1 : 0; o->pb2 = u[UI_PB2] ? 1 : 0; o->pb3 = u[UI_PB3] ? 1 : 0;
    o->task_type = AT(m, IA_TASK_TYPE, int32_t);
}
void ord_ia_to_mem(const ord_env *env, const ord_ia *o, int map, void *mem) {
    char *m = mem, *u = m + IA_UI;
    AT(m, IA_TYPE, int32_t) = o->type; AT(m, IA_SHOW_USER, uint8_t) = o->show_user; AT(m, IA_HIDE_TARGET, uint8_t) = o->hide_target;
    AT(m, IA_DURABILITY, int32_t) = o->durability;
    AT(m, IA_TARGET_TEXT, ord_obj) = o->target_text_null ? NULL : env->str_new(o->target_text);
    AT(m, IA_INVALID_MSG, ord_obj) = o->invalid_msg_null ? NULL : env->str_new(o->invalid_msg);
    AT(m, IA_IS_POSSIBLE, uint8_t) = o->is_possible;
    AT(m, IA_USER, ord_obj) = o->has_user ? env->ent_find(map, &o->user) : NULL;
    AT(m, IA_TARGET, ord_obj) = o->has_target ? env->ent_find(map, &o->target) : NULL;
    AT(m, IA_ITEM, ord_obj) = o->has_item ? env->ent_find(map, &o->item) : NULL;
    AT(m, IA_INDEX, int32_t) = o->index; AT(m, IA_INPUT_TYPE, int32_t) = o->input_type;
    AT(u, UI_TYPE, int32_t) = o->ui_type; AT(u, UI_P1, int32_t) = o->p1; AT(u, UI_P2, int32_t) = o->p2; AT(u, UI_P3, int32_t) = o->p3;
    AT(u, UI_PS1, ord_obj) = o->ps1_null ? NULL : env->str_new(o->ps1);
    AT(u, UI_PS2, ord_obj) = o->ps2_null ? NULL : env->str_new(o->ps2);
    memcpy(u + UI_PG1, &o->pg1, 16); memcpy(u + UI_PG2, &o->pg2, 16);
    u[UI_PG3] = (char)o->pg3_has; if (o->pg3_has) memcpy(u + UI_PG3 + 4, &o->pg3, 16);
    u[UI_PB1] = (char)o->pb1; u[UI_PB2] = (char)o->pb2; u[UI_PB3] = (char)o->pb3;
    AT(m, IA_TASK_TYPE, int32_t) = o->task_type;
}

static void ia_write(ord_wbuf *w, const ord_ia *o) {
    ow_i32(w, o->type); ow_u8(w, o->show_user); ow_u8(w, o->hide_target); ow_i32(w, o->durability);
    ow_str(w, o->target_text_null ? NULL : o->target_text); ow_str(w, o->invalid_msg_null ? NULL : o->invalid_msg);
    ow_u8(w, o->is_possible);
    ow_optguid(w, o->has_user, &o->user); ow_optguid(w, o->has_target, &o->target); ow_optguid(w, o->has_item, &o->item);
    ow_i32(w, o->index); ow_i32(w, o->input_type);
    ow_i32(w, o->ui_type); ow_i32(w, o->p1); ow_i32(w, o->p2); ow_i32(w, o->p3);
    ow_str(w, o->ps1_null ? NULL : o->ps1); ow_str(w, o->ps2_null ? NULL : o->ps2);
    ow_guid(w, &o->pg1); ow_guid(w, &o->pg2); ow_optguid(w, o->pg3_has, &o->pg3);
    ow_u8(w, o->pb1); ow_u8(w, o->pb2); ow_u8(w, o->pb3);
    ow_i32(w, o->task_type);
}
static void ia_read(ord_rbuf *r, ord_ia *o) {
    memset(o, 0, sizeof *o);
    o->type = or_i32(r); o->show_user = or_u8(r); o->hide_target = or_u8(r); o->durability = or_i32(r);
    o->target_text_null = or_str(r, o->target_text, ORD_STR) < 0;
    o->invalid_msg_null = or_str(r, o->invalid_msg, ORD_STR) < 0;
    o->is_possible = or_u8(r);
    o->has_user = or_optguid(r, &o->user); o->has_target = or_optguid(r, &o->target); o->has_item = or_optguid(r, &o->item);
    o->index = or_i32(r); o->input_type = or_i32(r);
    o->ui_type = or_i32(r); o->p1 = or_i32(r); o->p2 = or_i32(r); o->p3 = or_i32(r);
    o->ps1_null = or_str(r, o->ps1, ORD_STR) < 0; o->ps2_null = or_str(r, o->ps2, ORD_STR) < 0;
    or_guid(r, &o->pg1); or_guid(r, &o->pg2); o->pg3_has = or_optguid(r, &o->pg3);
    o->pb1 = or_u8(r); o->pb2 = or_u8(r); o->pb3 = or_u8(r);
    o->task_type = or_i32(r);
}

/* ---------------------------------------------------------------- selector descriptors */
static int sel_entity_kind(uint8_t k) { return k >= ORD_SEL_SPECIFIC && k <= ORD_SEL_CLOSEST_HOSTILE; }
static void sel_write(ord_wbuf *w, const ord_seldesc *s) {
    ow_u8(w, s->kind);
    uint8_t k = s->kind;
    if (k == ORD_SEL_TO_GRID) { ow_u8(w, s->inner); k = s->inner; }
    switch (k) {
    case ORD_SEL_SPECIFIC: case ORD_SEL_SPECIFIC_USABLE: ow_optguid(w, s->has_ent, &s->ent); break;
    case ORD_SEL_FROM_LIST: ow_u16(w, (uint16_t)s->n); for (int i = 0; i < s->n; i++) ow_guid(w, &s->list[i]); break;
    case ORD_SEL_GRID: ow_i32(w, s->x); ow_i32(w, s->y); ow_i32(w, s->flag); break;
    default: break;
    }
}
static void sel_read(ord_rbuf *r, ord_seldesc *s) {
    memset(s, 0, sizeof *s);
    s->kind = or_u8(r);
    uint8_t k = s->kind;
    if (k > ORD_SEL_TO_GRID) { r->err = 1; return; }
    if (k == ORD_SEL_TO_GRID) { s->inner = or_u8(r); k = s->inner; if (!sel_entity_kind(k)) { r->err = 1; return; } }
    switch (k) {
    case ORD_SEL_SPECIFIC: case ORD_SEL_SPECIFIC_USABLE: s->has_ent = or_optguid(r, &s->ent); break;
    case ORD_SEL_FROM_LIST:
        s->n = or_u16(r);
        if (s->n > ORD_MAX_LIST) { r->err = 1; return; }
        for (int i = 0; i < s->n; i++) or_guid(r, &s->list[i]);
        break;
    case ORD_SEL_GRID: s->x = or_i32(r); s->y = or_i32(r); s->flag = or_i32(r); break;
    default: break;
    }
}

/* ---------------------------------------------------------------- messages */
int ord_msg_write(const ord_env *env, ord_wbuf *w, const ord_msg *m) {
    if (m->kind == 0 || m->kind >= ORD_K_MAX) return -1;
    ow_u8(w, ORD_MAGIC); ow_u8(w, ORD_VERSION); ow_u16(w, m->kind);
    int nsel = m->ctx.nsel < 0 ? 0 : m->ctx.nsel > ORD_MAX_SEL ? ORD_MAX_SEL : m->ctx.nsel;
    ow_i32(w, m->ctx.map); ow_i32(w, m->ctx.sel_group); ow_u8(w, (uint8_t)nsel);
    for (int i = 0; i < nsel; i++) ow_guid(w, &m->ctx.sel[i]);
    switch (m->kind) {
    case ORD_K_EVENT:
        if (!m->ev || !m->evobj) return -1;
        ow_u16(w, m->ev->id);
        if (ord_ev_write(env, w, m->ev, m->evobj)) return -1;
        if (m->ev->flags & ORD_EF_RAYCAST) ow_optguid(w, m->has_ray, &m->ray);
        if (m->ev->flags & ORD_EF_PARTY) {
            if (m->nparty < 0 || m->nparty > ORD_MAX_SEL) return -1;
            ow_u8(w, m->party_active ? 1 : 0); ow_u8(w, (uint8_t)m->nparty);
            for (int i = 0; i < m->nparty; i++) ow_guid(w, &m->party[i]);
        }
        break;
    case ORD_K_MOVE_ALL: case ORD_K_WP_RESERVE: ow_i32(w, m->x); ow_i32(w, m->y); break;
    case ORD_K_MOVE_TRY: case ORD_K_WP_CLEAR: break;
    case ORD_K_MOVE_NEAR: ow_guid(w, &m->unit); ow_i32(w, m->x); ow_i32(w, m->y); break;
    case ORD_K_RUN:
        if (m->nact < 0 || m->nact > ORD_MAX_ACTS || m->nnear < 0 || m->nnear > ORD_MAX_LIST) return -1;
        ow_i32(w, m->x); ow_i32(w, m->y);
        ow_u16(w, (uint16_t)m->nact);
        for (int i = 0; i < m->nact; i++) { ow_guid(w, &m->act[i].unit); ia_write(w, &m->act[i].ia); }
        ow_u16(w, (uint16_t)m->nnear);
        for (int i = 0; i < m->nnear; i++) ow_guid(w, &m->near_units[i]);
        break;
    case ORD_K_ACTION: ow_guid(w, &m->act[0].unit); ia_write(w, &m->act[0].ia); break;
    case ORD_K_USE_SKILL:
        ow_optguid(w, m->has_user, &m->user); ow_optguid(w, m->has_target, &m->target);
        ow_str(w, m->key); sel_write(w, &m->sel); ow_optguid(w, m->has_item, &m->item);
        break;
    case ORD_K_AUTO_SKILL: ow_guid(w, &m->unit); ow_str(w, m->key); break;
    case ORD_K_OBSERVER: ow_i32(w, m->input_type); break;
    case ORD_K_MODE_CLICK: case ORD_K_MODE_DRAG:
        if (m->mode < 1 || m->mode > 5) return -1;
        ow_u8(w, (uint8_t)m->mode); ow_i32(w, m->x); ow_i32(w, m->y);
        if (m->kind == ORD_K_MODE_DRAG) { ow_i32(w, m->x2); ow_i32(w, m->y2); } else ow_u8(w, m->shift ? 1 : 0);
        ow_str(w, m->has_ckey ? m->key : NULL); ow_i32(w, m->rotation); ow_i32(w, m->dsx); ow_i32(w, m->dsy);
        break;
    }
    return w->err ? -1 : 0;
}

void ord_owner(const ord_env *env, const ord_msg *m, ord_guid *out) {
    memset(out, 0, sizeof *out);
    switch (m->kind) {
    case ORD_K_EVENT:
        if (m->ev && m->evobj && m->ev->owner >= 0) {
            const ord_field *f = &m->ev->f[m->ev->owner];
            char *p = (char *)m->evobj + f->off;
            if (f->kind == ORD_F_GUID) { memcpy(out, p, 16); return; }
            if (f->kind == ORD_F_ENT && *(ord_obj *)p && env->ent_guid(*(ord_obj *)p, out)) return;
        }
        if (m->ev && (m->ev->flags & ORD_EF_SELOWNER) && m->ctx.nsel > 0) *out = m->ctx.sel[0];
        return;
    case ORD_K_MOVE_ALL: case ORD_K_MOVE_TRY: case ORD_K_WP_RESERVE: case ORD_K_WP_CLEAR:
        if (m->ctx.nsel > 0) *out = m->ctx.sel[0];
        return;
    case ORD_K_RUN:
        if (m->nact > 0) *out = m->act[0].unit;
        else if (m->nnear > 0) *out = m->near_units[0];
        else if (m->ctx.nsel > 0) *out = m->ctx.sel[0];
        return;
    case ORD_K_ACTION: *out = m->act[0].unit; return;
    case ORD_K_MOVE_NEAR: case ORD_K_AUTO_SKILL: *out = m->unit; return;
    case ORD_K_USE_SKILL: if (m->has_user) *out = m->user; return;
    default: return;
    }
}

int ord_map_change_kind(const ord_msg *m) {
    if (m->kind != ORD_K_EVENT || !m->ev || !m->evobj) return 0;
    if (!strcmp(m->ev->name, "GoDownDungeonRequested")) return 3;
    if (!strcmp(m->ev->name, "ChangeMapRequested")) {
        int32_t to = AT(m->evobj, 0x14, int32_t);   /* ChangeMapRequested.ToMap; MapType Base 0, Dungeon 1 */
        return to == 1 ? 1 : to == 0 ? 2 : 0;
    }
    return 0;
}

int ord_msg_read(const ord_env *env, const uint8_t *p, size_t n, ord_msg *m) {
    ord_rbuf r = { p, n, 0, 0 };
    memset(m, 0, sizeof *m);
    if (or_u8(&r) != ORD_MAGIC || or_u8(&r) != ORD_VERSION) return -2;
    m->kind = or_u16(&r);
    if (m->kind == 0 || m->kind >= ORD_K_MAX) return -3;
    m->ctx.map = or_i32(&r); m->ctx.sel_group = or_i32(&r); m->ctx.nsel = or_u8(&r);
    if (m->ctx.nsel > ORD_MAX_SEL) return -4;
    for (int i = 0; i < m->ctx.nsel; i++) or_guid(&r, &m->ctx.sel[i]);
    switch (m->kind) {
    case ORD_K_EVENT: {
        uint16_t id = or_u16(&r);
        m->ev = ord_event_by_id(id);
        if (!m->ev || (m->ev->flags & ORD_EF_BLOCK)) return -5;
        m->evobj = ord_ev_read(env, &r, m->ev, m->ctx.map);
        if (!m->evobj) return -6;
        if (m->ev->flags & ORD_EF_RAYCAST) m->has_ray = or_optguid(&r, &m->ray);
        if (m->ev->flags & ORD_EF_PARTY) {
            m->party_active = or_u8(&r); m->nparty = or_u8(&r);
            if (m->party_active > 1 || m->nparty > ORD_MAX_SEL) return -4;
            for (int i = 0; i < m->nparty; i++) or_guid(&r, &m->party[i]);
        }
        break;
    }
    case ORD_K_MOVE_ALL: case ORD_K_WP_RESERVE: m->x = or_i32(&r); m->y = or_i32(&r); break;
    case ORD_K_MOVE_TRY: case ORD_K_WP_CLEAR: break;
    case ORD_K_MOVE_NEAR: or_guid(&r, &m->unit); m->x = or_i32(&r); m->y = or_i32(&r); break;
    case ORD_K_RUN:
        m->x = or_i32(&r); m->y = or_i32(&r);
        m->nact = or_u16(&r);
        if (m->nact > ORD_MAX_ACTS) return -7;
        for (int i = 0; i < m->nact; i++) { or_guid(&r, &m->act[i].unit); ia_read(&r, &m->act[i].ia); }
        m->nnear = or_u16(&r);
        if (m->nnear > ORD_MAX_LIST) return -7;
        for (int i = 0; i < m->nnear; i++) or_guid(&r, &m->near_units[i]);
        break;
    case ORD_K_ACTION: m->nact = 1; or_guid(&r, &m->act[0].unit); ia_read(&r, &m->act[0].ia); break;
    case ORD_K_USE_SKILL:
        m->has_user = or_optguid(&r, &m->user); m->has_target = or_optguid(&r, &m->target);
        or_str(&r, m->key, sizeof m->key); sel_read(&r, &m->sel); m->has_item = or_optguid(&r, &m->item);
        break;
    case ORD_K_AUTO_SKILL: or_guid(&r, &m->unit); or_str(&r, m->key, sizeof m->key); break;
    case ORD_K_OBSERVER: m->input_type = or_i32(&r); break;
    case ORD_K_MODE_CLICK: case ORD_K_MODE_DRAG:
        m->mode = or_u8(&r);
        if (m->mode < 1 || m->mode > 5) return -4;
        m->x = or_i32(&r); m->y = or_i32(&r);
        if (m->kind == ORD_K_MODE_DRAG) { m->x2 = or_i32(&r); m->y2 = or_i32(&r); } else { m->shift = or_u8(&r); if (m->shift > 1) return -4; }
        m->has_ckey = or_str(&r, m->key, sizeof m->key) >= 0;
        m->rotation = or_i32(&r); m->dsx = or_i32(&r); m->dsy = or_i32(&r);
        break;
    }
    if (r.err) return -8;
    if (r.i != r.n) return -9;   /* trailing bytes: version mismatch */
    return 0;
}
