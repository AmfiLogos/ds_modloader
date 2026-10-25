// dsl_newgame.c - the New Game screen's custom difficulty switches (owner 2026-10-25: "a toggle in the difficulty selection
// screen, to its right ... which then keeps turned on the custom difficulty for the newly created world"; then: "We can keep
// it always on & have the toggle just activate its display. It's not evident to me that not loading a game in hell mode
// should require a reload.").
//
// A custom difficulty mod (mod.json "difficulty": {"value": N}; Hell difficulty: CombatDifficulty 5) runs whenever it is
// installed (dsl_deps.h). One switch per such mod in a "Custom difficulty" panel right of the New Game settings' combat
// difficulty row: it only shows or hides level N in the combat and management selectors. No restart. The choice is kept in
// <data>/Mods/newgame.json {"show": {"<id>": bool}}, off by default. Hidden: the selector's "+" at N-1 is locked the way the
// game locks it at its top level (SubUI_CampaignSetting.SetDifficultyIncreaseLocked after the presenter's own
// RefreshDifficultyIncreaseButtonState, hooked), and a level N that got there anyway (switch turned off while N is chosen,
// a key) falls back to N-1 at once. A world made on N uses the mod, any other world does not (dsl_reg.c: by the world's
// level, read from its save).
//
// Game access by name only: CreateCampaignPresenter (OnActivateView hooked: the live presenter, weak GC handle;
// RefreshDifficultyIncreaseButtonState hooked: the lock), its fields, CreateCampaignView / SubUI_CampaignSetting methods. The
// same code on Mac and Windows (field offsets by name, C# bool returns read as uint8_t).
//
// Test only (DSL_SOFT_TEST, dsl_reg.c reg_request.json {"cmd":"newgame","op":...}): open / next / start / set / toggle /
// state; state writes <log dir>/softrestart/newgame_state.json.
#include "../src/dsl_internal.h"
#include "../src/dsl_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define CALL(mi, R, ...) ((R (*)(__VA_ARGS__, const void *))(*(void **)(mi)))

int dsl_mod_difficulty(const char *id);                 /* dsl_reg.c: mod.json "difficulty" value, 0 = none */

static const DSLUi *U;
static int ok;
static void *k_pres, *k_view, *k_set, *k_in;
static int32_t o_view = -1, o_vt = -1, o_name = -1, o_cd = -1, o_md = -1, o_iron = -1, o_before = -1, o_magic = -1,
               o_lock = -1, o_seed = -1, o_unlocked = -1;
static int32_t o_setpanel = -1, o_inc = -1, o_inctip = -1, o_minc = -1, o_minctip = -1, o_nameinput = -1, o_seedinput = -1,
               o_cdtitle = -1, o_mdtitle = -1;
static void *m_next, *m_refresh, *m_forced, *m_ironinter, *m_lockset, *m_namechg, *m_seedchg, *m_cdtext, *m_mdtext, *m_irontext,
            *m_magictext, *m_setname, *m_setseed, *m_newgame, *m_intext, *m_alive, *m_find, *m_comp_go, *m_comp_tr,
            *m_go_tree, *m_parent, *m_lockinc, *m_inter;
static Obj t_title;                                      /* System.Type of TitleMenuView (FindObjectOfType, test "open") */
static Obj (*str_new)(const char *);
static uintptr_t (*gc_weak)(Obj, uint8_t); static uintptr_t (*gc_new)(Obj, uint8_t); static Obj (*gc_get)(uintptr_t); static void (*gc_free)(uintptr_t);

static void *cls(const char *ns, const char *n) { void *c = dsl_find_class(ns, n); if (!c) dsl_log("newgame: class %s.%s missing", ns, n); return c; }
static int32_t fo(void *k, const char *n) { int32_t o = k ? dsl_field_offset(k, n) : -1; if (o < 0) dsl_log("newgame: field %s missing", n); return o; }
static void *mt(void *k, const char *n, int argc) { void *m = k ? dsl_find_method(k, n, argc) : NULL; if (!m) dsl_log("newgame: method %s (%d) missing", n, argc); return m; }
static int live(Obj o) { return o && m_alive && CALL(m_alive, uint8_t, Obj)(o, m_alive); }
static int shown(Obj comp) { Obj g = live(comp) ? CALL(m_comp_go, Obj, Obj)(comp, m_comp_go) : NULL; return g && CALL(m_go_tree, uint8_t, Obj)(g, m_go_tree); }
static Obj tr_of(Obj comp) { return live(comp) ? CALL(m_comp_tr, Obj, Obj)(comp, m_comp_tr) : NULL; }
static void utf8_of(Obj s, char *out, size_t cap) {
    out[0] = 0; if (!s) return;
    int32_t n = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14); size_t k = 0;
    for (int32_t i = 0; i < n && k + 4 < cap; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < n) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
        if (u < 0x80) out[k++] = (char)u;
        else if (u < 0x800) { out[k++] = (char)(0xC0 | u >> 6); out[k++] = (char)(0x80 | (u & 63)); }
        else if (u < 0x10000) { out[k++] = (char)(0xE0 | u >> 12); out[k++] = (char)(0x80 | (u >> 6 & 63)); out[k++] = (char)(0x80 | (u & 63)); }
        else { out[k++] = (char)(0xF0 | u >> 18); out[k++] = (char)(0x80 | (u >> 12 & 63)); out[k++] = (char)(0x80 | (u >> 6 & 63)); out[k++] = (char)(0x80 | (u & 63)); }
    }
    out[k] = 0;
}

/* ------------------------------------------------------------------ the live presenter (OnActivateView hooked) */
static uintptr_t h_pres;
static void (*o_activate)(Obj self, const void *mi);
static void h_activate(Obj self, const void *mi) {
    o_activate(self, mi);
    if (gc_free && h_pres) gc_free(h_pres);
    h_pres = self && gc_weak ? gc_weak(self, 0) : 0;
}
static Obj presenter(void) { return h_pres && gc_get ? gc_get(h_pres) : NULL; }
static Obj view_of(Obj p) { Obj v = p && o_view >= 0 ? AT(p, o_view, Obj) : NULL; return live(v) ? v : NULL; }
static Obj setpanel_of(Obj v) { Obj s = v && o_setpanel >= 0 ? AT(v, o_setpanel, Obj) : NULL; return live(s) ? s : NULL; }

/* ------------------------------------------------------------------ the difficulty mods and their "show" switches */
#define MAXD 4
static struct { char id[64]; int value; uintptr_t h_tog; } D[MAXD]; static int nd;
static void hold(uintptr_t *h, DSLObj o);
static void diff_scan(void) {
    for (int k = 0; k < MAXD; k++) hold(&D[k].h_tog, NULL);            /* the old panel's toggles (destroyed with it) */
    nd = 0;
    for (int i = 0; i < dsl_mods_count() && nd < MAXD; i++) {
        const DSLModInfo *m = dsl_mods_at(i); int v = m ? dsl_mod_difficulty(m->id) : 0;
        if (!m || v <= 0 || m->hidden || strcmp(m->scope, "gameplay")) continue;
        snprintf(D[nd].id, sizeof D[nd].id, "%s", m->id); D[nd].value = v; nd++;
    }
}
static const DSLModInfo *mod_of(const char *id) {
    for (int i = 0; i < dsl_mods_count(); i++) { const DSLModInfo *m = dsl_mods_at(i); if (m && !strcmp(m->id, id)) return m; }
    return NULL;
}
/* <data>/Mods/newgame.json {"show": {"<id>": bool}}: absent = off (owner: "the toggle state is saved, off by default") */
static struct { char id[64]; int on; } SH[16]; static int nsh, sh_read;
static void show_path(char *p, size_t n) { snprintf(p, n, "%s/newgame.json", dsl_reg_data_dir()); }
static void show_load(void) {
    if (sh_read) return; sh_read = 1;
    char p[1300]; show_path(p, sizeof p);
    FILE *f = fopen(p, "rb"); if (!f) return;
    char b[4096]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;
    JNode *j = json_parse(b), *s = json_get(j, "show");
    if (s && s->t == JN_OBJ) for (JNode *c = s->child; c && nsh < 16; c = c->next)
        if (c->key && c->t == JN_BOOL) { snprintf(SH[nsh].id, sizeof SH[nsh].id, "%s", c->key); SH[nsh].on = c->b != 0; nsh++; }
    json_free(j);
}
static int show_get(const char *id) { show_load(); for (int i = 0; i < nsh; i++) if (!strcmp(SH[i].id, id)) return SH[i].on; return 0; }
static int show_set(const char *id, int on) {
    show_load();
    int i = 0; while (i < nsh && strcmp(SH[i].id, id)) i++;
    if (i == nsh) { if (nsh >= 16) return -1; snprintf(SH[nsh].id, sizeof SH[nsh].id, "%s", id); nsh++; }
    SH[i].on = on != 0;
    char p[1300], tmp[1340]; show_path(p, sizeof p); snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "w"); int bad = !f;
    if (f) {
        bad |= fprintf(f, "{\"show\": {") < 0;
        for (int k = 0; k < nsh; k++) bad |= fprintf(f, "%s\"%s\": %s", k ? ", " : "", SH[k].id, SH[k].on ? "true" : "false") < 0;   /* ids: a-z 0-9 - */
        bad |= fprintf(f, "}}\n") < 0; bad |= fclose(f) != 0;
    }
    if (bad || rename(tmp, p)) { dsl_log("newgame: %s not saved", p); return -1; }
    return 0;
}
static int running(const char *id) { const DSLModInfo *m = mod_of(id); return m && m->active == 1; }
static int unlocked(Obj p) { return p && o_unlocked >= 0 && (AT(p, o_unlocked, uint8_t) & 1); }
/* the game's top level with the mods that run: 4 (Devastation), or a running custom level (Hell 5) */
static int top_level(void) {
    int mx = 4;
    for (int k = 0; k < nd; k++) if (D[k].value > mx && running(D[k].id)) mx = D[k].value;
    return mx;
}
/* the highest level the selectors offer: 4, or a running custom level whose switch is on; 3 while Devastation is locked */
static int max_level(Obj p) {
    int mx = 4;
    for (int k = 0; k < nd; k++) if (D[k].value > mx && running(D[k].id) && show_get(D[k].id)) mx = D[k].value;
    if (p && !unlocked(p) && mx > 3) mx = 3;
    return mx;
}

/* ------------------------------------------------------------------ the screen's choices */
typedef struct { int c, m, i, f, g, l; char name[256], seed[128]; } Choices;
static void input_text(Obj set, int32_t off, char *out, size_t cap) {
    Obj in = set && off >= 0 ? AT(set, off, Obj) : NULL; out[0] = 0;
    if (live(in) && m_intext) utf8_of(CALL(m_intext, Obj, Obj)(in, m_intext), out, cap);
}
static void choices_of(Obj p, Choices *x) {
    memset(x, 0, sizeof *x);
    x->c = AT(p, o_cd, int32_t); x->m = AT(p, o_md, int32_t);
    x->i = AT(p, o_iron, uint8_t) & 1; x->f = AT(p, o_before, uint8_t) & 1; x->g = AT(p, o_magic, uint8_t) & 1; x->l = AT(p, o_lock, uint8_t) & 1;
    Obj set = setpanel_of(view_of(p));
    input_text(set, o_nameinput, x->name, sizeof x->name); if (!x->name[0]) utf8_of(AT(p, o_name, Obj), x->name, sizeof x->name);
    input_text(set, o_seedinput, x->seed, sizeof x->seed); if (!x->seed[0]) utf8_of(AT(p, o_seed, Obj), x->seed, sizeof x->seed);
}
/* the screen shows these choices (levels above what it offers now: its highest): the presenter's fields as its own
   OnActivateView sets them, then the panel's setters; names = 0 leaves the name and seed alone */
static void choices_apply(Obj p, const Choices *x, int names, int *c_out, int *m_out) {
    Obj v = view_of(p), set = setpanel_of(v); if (!v || !set) return;
    int was = AT(p, o_cd, int32_t);
    int mx = max_level(p), c = x->c > mx ? mx : x->c < 0 ? 0 : x->c, m = x->m > mx ? mx : x->m < 0 ? 0 : x->m;
    int forced = m_forced && CALL(m_forced, uint8_t, Obj, int32_t)(p, c, m_forced) != 0, was_forced = was >= 4;
    int iron = forced ? 1 : was_forced ? x->f : x->i, before = forced && !was_forced ? x->i : x->f;   /* ChangeCampaignCombatDifficulty's rule */
    AT(p, o_cd, int32_t) = c; AT(p, o_md, int32_t) = m;
    AT(p, o_iron, uint8_t) = (uint8_t)iron; AT(p, o_before, uint8_t) = (uint8_t)before;
    AT(p, o_magic, uint8_t) = (uint8_t)x->g; AT(p, o_lock, uint8_t) = (uint8_t)x->l;
    CALL(m_cdtext, void, Obj, int32_t)(set, c, m_cdtext);
    CALL(m_mdtext, void, Obj, int32_t)(set, m, m_mdtext);
    CALL(m_refresh, void, Obj)(p, m_refresh);                          /* hooked: the "+" lock of a hidden level follows */
    CALL(m_irontext, void, Obj, uint8_t)(set, (uint8_t)iron, m_irontext);
    CALL(m_ironinter, void, Obj, uint8_t)(v, (uint8_t)!forced, m_ironinter);
    CALL(m_magictext, void, Obj, uint8_t)(set, (uint8_t)x->g, m_magictext);
    CALL(m_lockset, void, Obj, uint8_t)(v, (uint8_t)x->l, m_lockset);
    if (names && x->name[0]) { Obj s = str_new(x->name); CALL(m_setname, void, Obj, Obj)(set, s, m_setname); CALL(m_namechg, void, Obj, Obj)(v, s, m_namechg); }
    if (names && x->seed[0]) { Obj s = str_new(x->seed); CALL(m_setseed, void, Obj, Obj)(set, s, m_setseed); CALL(m_seedchg, void, Obj, Obj)(v, s, m_seedchg); }
    if (c_out) *c_out = c;
    if (m_out) *m_out = m;
}
/* a hidden level chosen anyway (its switch went off while it was chosen, a key): the highest level shown */
static void clamp(Obj p) {
    int mx = max_level(p);
    if (!p || (AT(p, o_cd, int32_t) <= mx && AT(p, o_md, int32_t) <= mx) || !setpanel_of(view_of(p))) return;
    Choices x; choices_of(p, &x); int c = 0, m = 0; choices_apply(p, &x, 0, &c, &m);
    dsl_log("newgame: a hidden level was chosen: combat %d, management %d now", c, m);
}

/* ------------------------------------------------------------------ the "+" lock (RefreshDifficultyIncreaseButtonState hooked) */
static void (*o_refresh)(Obj self, const void *mi);
static void lock_inc(Obj set, int32_t ob, int32_t ot) {
    Obj b = ob >= 0 ? AT(set, ob, Obj) : NULL, t = ot >= 0 ? AT(set, ot, Obj) : NULL;
    if (live(b)) CALL(m_lockinc, void, Obj, Obj, Obj, uint8_t, Obj)(set, b, t, 1, str_new(""), m_lockinc);   /* as at the game's top level */
}
static void h_refresh(Obj self, const void *mi) {
    o_refresh(self, mi);
    if (!self || !unlocked(self)) return;                              /* Devastation locked: the game's own lock and its card */
    int mx = max_level(self); if (mx >= top_level()) return;           /* nothing hidden */
    Obj set = setpanel_of(view_of(self)); if (!set) return;
    if (AT(self, o_cd, int32_t) >= mx) lock_inc(set, o_inc, o_inctip);
    if (AT(self, o_md, int32_t) >= mx) lock_inc(set, o_minc, o_minctip);
}

/* ------------------------------------------------------------------ the switches */
static DSLObj held(uintptr_t *h) { Obj o = *h && gc_get ? gc_get(*h) : NULL; if (!live(o)) { if (*h && gc_free) gc_free(*h); *h = 0; return NULL; } return (DSLObj)o; }
static void hold(uintptr_t *h, DSLObj o) { if (*h && gc_free) gc_free(*h); *h = o && gc_new ? gc_new((Obj)o, 0) : 0; }
static void tog_click(void *ud, DSLObj w) {
    int k = (int)(intptr_t)ud; if (k < 0 || k >= nd) return;
    int on = U->toggle_get(w);                                         /* the kit flips the box before this call */
    if (on && !running(D[k].id)) { U->toggle_set(w, 0); dsl_log("newgame: %s does not run: its level cannot show", D[k].id); return; }
    if (show_set(D[k].id, on)) { U->toggle_set(w, !on); return; }
    Obj p = presenter();
    dsl_log("newgame: %s %s on the New Game screen (level %d %s, no restart)", D[k].id, on ? "ON" : "OFF", D[k].value, on ? "shown" : "hidden");
    if (p && setpanel_of(view_of(p))) { clamp(p); CALL(m_refresh, void, Obj)(p, m_refresh); }   /* the "+" lock follows */
}
static void tog_tip(int k, DSLObj w) {
    const DSLModInfo *m = mod_of(D[k].id); const char *dn = dsl_mod_difficulty_name(D[k].id); char body[900];
    snprintf(body, sizeof body, TR("Shows %s in the combat and management difficulty lists. A world made on %s keeps it. Other worlds do not change."), dn, dn);
    if (m && m->active != 1) {
        char t[400]; snprintf(t, sizeof t, TR("Not running: %s"), TR(m->reason && *m->reason ? m->reason : "did not load"));
        strlcat(body, "\n", sizeof body); strlcat(body, t, sizeof body);
    }
    dsi_utf8_fix_end(body);
    U->tooltip(w, m ? m->name : D[k].id, body);
}
/* the switches' panel: outside the New Game window, on its right, top level with the combat difficulty row (it follows the
   row while the settings scroll, inside the list's height), like the Load list's filter panel (dsl_rows.c). A child of the
   settings panel: it shows and goes with it. Local units of that panel from screen rects. */
#define PN_W 300.f
#define PN_PAD 14.f
#define PN_ROW 42.f
#define PN_GAP 16.f
static uintptr_t h_pn; static unsigned pn_gen; static float pn_w = PN_W, pn_ht, pn_last[5];
float dsl_newgame_pn_rect[4], dsl_newgame_frame_rect[4], dsl_newgame_row_rect[4];   /* test: screen rects at the last placing */
static void pn_drop(void) {
    DSLObj p = held(&h_pn); if (p) U->destroy(p);
    hold(&h_pn, NULL); for (int k = 0; k < MAXD; k++) hold(&D[k].h_tog, NULL);
}
static DSLObj pn_build(Obj set) {
    DSLObj pn = held(&h_pn);
    if (pn && pn_gen != dsi_generation()) { pn_drop(); pn = NULL; }   /* a language change: built again */
    if (pn) return pn;
    diff_scan(); if (!nd) return NULL;
    pn = U->panel(tr_of(set), 0, 0, PN_W, 100, 0);                     /* the game's dark Box panel */
    if (!pn) return NULL;
    hold(&h_pn, pn); pn_gen = dsi_generation(); memset(pn_last, 0, sizeof pn_last);
    DSLObj tt = U->text(pn, PN_PAD, 10, PN_W - 2 * PN_PAD, 30, TR("Custom difficulty"), 20, DSL_TXT_LEFT | DSL_TXT_TITLE, 0xD9BE8FFF);
    float need = (tt ? dsl_ui_text_line_w(tt) : 0) + 2 * PN_PAD, y = 48;
    DSLObj w[MAXD] = { 0 };
    for (int k = 0; k < nd; k++) {
        const DSLModInfo *m = mod_of(D[k].id); if (!m) continue;
        w[k] = U->toggle(pn, PN_PAD, y, PN_W - 2 * PN_PAD, m->name, running(D[k].id) && show_get(D[k].id), tog_click, (void *)(intptr_t)k);
        if (!w[k]) continue;
        hold(&D[k].h_tog, w[k]); tog_tip(k, w[k]);
        if (!running(D[k].id)) U->set_enabled(w[k], 0);
        float lw = dsl_ui_text_line_w(w[k]) + 76 + 2 * PN_PAD; if (lw > need) need = lw;   /* label + the box, one line */
        y += PN_ROW;
    }
    pn_w = need < PN_W ? PN_W : need > 2 * PN_W ? 2 * PN_W : need;
    pn_ht = y + PN_PAD - (PN_ROW - 36);
    if (tt) U->place(tt, PN_PAD, 10, pn_w - 2 * PN_PAD, 30);
    for (int k = 0, yy = 48; k < nd; k++) if (w[k]) { U->place(w[k], PN_PAD, (float)yy, pn_w - 2 * PN_PAD, 36); yy += (int)PN_ROW; }
    dsl_log("newgame: Custom difficulty panel made on the New Game screen (%d switch(es), width %.0f)", nd, pn_w);
    return pn;
}
static void pn_place(Obj set, DSLObj pn) {
    Obj str = tr_of(set), fr = str ? CALL(m_parent, Obj, Obj)(str, m_parent) : NULL;   /* the window (Panel_CreateCampaignPanel) */
    Obj row = o_inc >= 0 ? tr_of(AT(set, o_inc, Obj)) : NULL;                          /* arrow -> its content -> the row */
    for (int i = 0; i < 2 && row; i++) { Obj up = CALL(m_parent, Obj, Obj)(row, m_parent); if (!up || up == str) break; row = up; }
    float P[4], F[4], R[4], L[4], C[4], sw, sh;
    if (!str || dsl_ui_screen_rect((DSLObj)str, P) != 1) return;
    if (!fr || dsl_ui_screen_rect((DSLObj)fr, F) != 1) memcpy(F, P, sizeof F);
    if (!row || dsl_ui_screen_rect((DSLObj)row, R) != 1) memcpy(R, P, sizeof R);
    DSLObj vp = dsl_ui_child((DSLObj)str, "Viewport"); if (!vp || dsl_ui_screen_rect(vp, L) != 1) memcpy(L, P, sizeof L);
    float lw = dsl_ui_width((DSLObj)str), s = lw > 50 ? (P[2] - P[0]) / lw : 0; if (s <= 0) return;   /* px per unit; not laid out yet */
    dsl_ui_screen_size(&sw, &sh);
    float m = 8 * s, x0 = F[2] + PN_GAP * s, k = (sw - m - x0) / (pn_w * s);
    if (k > 1) k = 1;
    if (k < 0.5f) k = 0.5f;
    float pw = pn_w * s * k, ph = pn_ht * s * k, top = R[3];
    if (top > L[3]) top = L[3];                                        /* the row scrolled up out of the list: the list's top */
    if (top - ph < L[1]) top = L[1] + ph;                              /* ... down: the list's bottom */
    if (dsl_update_corner_rect(C) && x0 < C[2] && x0 + pw > C[0] && top > C[1] - PN_GAP * s && top - ph < C[3]) top = C[1] - PN_GAP * s;
    if (top - ph < m) top = m + ph;
    if (top > sh - m) top = sh - m;
    float v[5] = { (x0 - P[0]) / s, (P[3] - top) / s, pn_w, pn_ht, k }; int same = 1;
    for (int i = 0; i < 5; i++) { float d = v[i] - pn_last[i]; same &= d > -0.5f && d < 0.5f; }
    if (same) return;
    memcpy(pn_last, v, sizeof v);
    U->place(pn, v[0], v[1], v[2], v[3]); dsl_ui_set_scale(pn, k);
    float pr[4] = { x0, top - ph, x0 + pw, top }; memcpy(dsl_newgame_pn_rect, pr, sizeof pr);
    memcpy(dsl_newgame_frame_rect, F, sizeof F); memcpy(dsl_newgame_row_rect, R, sizeof R);
}
static void toggles_frame(void) {
    static unsigned f; if (++f % 2) return;
    Obj p = presenter(), v = view_of(p), set = setpanel_of(v);
    if (!set || !shown(set) || AT(p, o_vt, int32_t) != 2) return;      /* a child of the settings panel: hidden with it */
    clamp(p);
    DSLObj pn = pn_build(set); if (!pn) return;
    pn_place(set, pn);
}
/* dsl_reg.c (the New Campaign line "Mods for this world"): the levels chosen on the settings step now; 0 = not there */
int dsl_newgame_levels(int *combat, int *management) {
    if (!ok) return 0;
    Obj p = presenter(), v = view_of(p);
    if (!v || AT(p, o_vt, int32_t) != 2) return 0;
    *combat = AT(p, o_cd, int32_t); *management = AT(p, o_md, int32_t);
    return 1;
}

/* ------------------------------------------------------------------ install + frame */
void dsl_newgame_install(void) {
    U = dsl_ui_kit();
    k_pres = cls("Refactor.UI", "CreateCampaignPresenter"); k_view = cls("Refactor.UI", "CreateCampaignView");
    k_set = cls("Refactor.UI", "SubUI_CampaignSetting"); k_in = cls("TMPro", "TMP_InputField");
    void *k_title = cls("Refactor.UI", "TitleMenuView");
    o_view = fo(k_pres, "<View>k__BackingField"); o_vt = fo(k_pres, "_currentViewType");
    o_name = fo(k_pres, "_campaignName"); o_cd = fo(k_pres, "_combatDifficulty"); o_md = fo(k_pres, "_managementDifficulty");
    o_iron = fo(k_pres, "_isIronModeEnabled"); o_before = fo(k_pres, "_ironModeEnabledBeforeForcedDifficulty");
    o_magic = fo(k_pres, "_isMagicSaturationEnabled"); o_lock = fo(k_pres, "_isLockConsecrationEnabled"); o_seed = fo(k_pres, "_seed");
    o_unlocked = fo(k_pres, "_isDownfallDifficultyUnlocked");
    o_setpanel = fo(k_view, "_campaignSettingPanel");
    o_inc = fo(k_set, "_increaseCombatDifficultyButton"); o_inctip = fo(k_set, "_combatDifficultyIncreaseTooltipFeedback");
    o_minc = fo(k_set, "_increaseManagementDifficultyButton"); o_minctip = fo(k_set, "_managementDifficultyIncreaseTooltipFeedback");
    o_nameinput = fo(k_set, "_campaignNameInputField"); o_seedinput = fo(k_set, "_currentSeedText");
    o_cdtitle = fo(k_set, "_combatDifficultyTitleText"); o_mdtitle = fo(k_set, "_managementDifficultyTitleText");
    m_next = mt(k_pres, "OnClickNextButton", 0); m_refresh = mt(k_pres, "RefreshDifficultyIncreaseButtonState", 0); m_forced = mt(k_pres, "IsIronModeForced", 1);
    m_ironinter = mt(k_view, "SetIronModeInteractable", 1); m_lockset = mt(k_view, "SetLockConsecrationEnabled", 1);
    m_namechg = mt(k_view, "OnCampaignNameChanged", 1); m_seedchg = mt(k_view, "OnCampaignSeedChanged", 1);
    m_cdtext = mt(k_set, "SetCombatDifficultyText", 1); m_mdtext = mt(k_set, "SetManagementDifficultyText", 1);
    m_irontext = mt(k_set, "SetIronModeText", 1); m_magictext = mt(k_set, "SetMagicSaturationText", 1);
    m_setname = mt(k_set, "SetCampaignName", 1); m_setseed = mt(k_set, "SetSeed", 1); m_lockinc = mt(k_set, "SetDifficultyIncreaseLocked", 4);
    m_newgame = mt(k_title, "OnNewGamePressed", 0); m_intext = mt(k_in, "get_text", 0);
    m_inter = mt(cls("UnityEngine.UI", "Selectable"), "IsInteractable", 0);
    void *kObj = cls("UnityEngine", "Object"), *kComp = cls("UnityEngine", "Component"), *kGo = cls("UnityEngine", "GameObject");
    m_alive = mt(kObj, "op_Implicit", 1); m_find = mt(kObj, "FindObjectOfType", 2);
    m_comp_go = mt(kComp, "get_gameObject", 0); m_comp_tr = mt(kComp, "get_transform", 0); m_go_tree = mt(kGo, "get_activeInHierarchy", 0);
    m_parent = mt(cls("UnityEngine", "Transform"), "get_parent", 0);
    str_new = dsl_ga_sym("il2cpp_string_new");
    gc_weak = dsl_ga_sym("il2cpp_gchandle_new_weakref"); gc_new = dsl_ga_sym("il2cpp_gchandle_new");
    gc_get = dsl_ga_sym("il2cpp_gchandle_get_target"); gc_free = dsl_ga_sym("il2cpp_gchandle_free");
    void *(*ctype)(void *) = dsl_ga_sym("il2cpp_class_get_type"); Obj (*tobj)(void *) = dsl_ga_sym("il2cpp_type_get_object");
    if (ctype && tobj && k_title) t_title = tobj(ctype(k_title));
    ok = U && o_view >= 0 && o_vt >= 0 && o_name >= 0 && o_cd >= 0 && o_md >= 0 && o_iron >= 0 && o_before >= 0 && o_magic >= 0
         && o_lock >= 0 && o_seed >= 0 && o_unlocked >= 0 && o_setpanel >= 0 && o_inc >= 0 && o_inctip >= 0 && o_minc >= 0 && o_minctip >= 0
         && m_next && m_refresh && m_forced && m_ironinter && m_lockset && m_namechg && m_seedchg && m_cdtext && m_mdtext && m_irontext
         && m_magictext && m_setname && m_setseed && m_lockinc && m_intext && m_alive && m_comp_go && m_comp_tr
         && m_go_tree && m_parent && str_new && gc_weak && gc_new && gc_get && gc_free;
    if (!ok) { dsl_log("newgame: custom difficulty switches OFF (names missing)"); return; }
    void *t = dsl_find_method_address("Refactor.UI", "CreateCampaignPresenter", "OnActivateView", 0);
    int r = t ? dsl_hook(DSL_OWNER_LOADER, t, (void *)h_activate, (void **)&o_activate, "loader New Game screen difficulty toggle") : -1;
    if (r) { ok = 0; dsl_log("newgame: custom difficulty switches OFF (CreateCampaignPresenter.OnActivateView not hooked)"); return; }
    diff_scan();
    void *rf = dsl_find_method_address("Refactor.UI", "CreateCampaignPresenter", "RefreshDifficultyIncreaseButtonState", 0);
    if (nd && (!rf || dsl_hook(DSL_OWNER_LOADER, rf, (void *)h_refresh, (void **)&o_refresh, "loader New Game screen custom difficulty lock"))) {
        dsl_unhook(DSL_OWNER_LOADER, t, "newgame"); ok = 0;                /* a hidden level would still be offered: no switches */
        dsl_log("newgame: custom difficulty switches OFF (RefreshDifficultyIncreaseButtonState not hooked)"); return;
    }
    char sv[200] = ""; for (int k = 0; k < nd; k++) { char one[90]; snprintf(one, sizeof one, "%s%s %s", k ? ", " : "", D[k].id, show_get(D[k].id) ? "shown" : "hidden"); strlcat(sv, one, sizeof sv); }
    dsl_log("newgame: custom difficulty switches on (%d difficulty mod(s)%s%s)", nd, nd ? ": " : "", sv);
}
void dsl_newgame_frame(void) {
    if (!ok) return;
    toggles_frame();
}

/* ------------------------------------------------------------------ test only (dsl_reg.c, DSL_SOFT_TEST) */
static void jesc(char *out, size_t n, const char *s) {
    size_t k = 0;
    for (; s && *s && k + 7 < n; s++) {
        if (*s == '"' || *s == '\\') { out[k++] = '\\'; out[k++] = *s; }
        else if ((unsigned char)*s < 0x20) k += (size_t)snprintf(out + k, n - k, "\\u%04x", *s);
        else out[k++] = *s;
    }
    out[k] = 0;
}
static int inter(Obj set, int32_t ob) { Obj b = set && ob >= 0 ? AT(set, ob, Obj) : NULL; return live(b) && m_inter ? CALL(m_inter, uint8_t, Obj)(b, m_inter) != 0 : -1; }
static void state_write(void) {
    char pth[1300], tmp[1340]; snprintf(pth, sizeof pth, "%s/softrestart/newgame_state.json", dsl_log_dir()); snprintf(tmp, sizeof tmp, "%s.tmp", pth);
    FILE *f = fopen(tmp, "w"); if (!f) return;
    Obj p = presenter(), v = view_of(p), set = setpanel_of(v);
    Choices x; memset(&x, 0, sizeof x); if (p && v) choices_of(p, &x);
    char e1[600], e2[300], cdt[200] = "", mdt[200] = "", e3[400], e4[400];
    if (set && o_cdtitle >= 0) dsl_ui_text_get((DSLObj)AT(set, o_cdtitle, Obj), cdt, sizeof cdt);
    if (set && o_mdtitle >= 0) dsl_ui_text_get((DSLObj)AT(set, o_mdtitle, Obj), mdt, sizeof mdt);
    jesc(e1, sizeof e1, x.name); jesc(e2, sizeof e2, x.seed); jesc(e3, sizeof e3, cdt); jesc(e4, sizeof e4, mdt);
    fprintf(f, "{\"open\": %d, \"settings_shown\": %d, \"view_type\": %d, \"combat\": %d, \"management\": %d, \"iron\": %d, \"iron_before\": %d, "
               "\"magic\": %d, \"lock\": %d, \"name\": \"%s\", \"seed\": \"%s\", \"combat_text\": \"%s\", \"management_text\": \"%s\", \"max_level\": %d, "
               "\"top_level\": %d, \"unlocked\": %d, \"inc_combat\": %d, \"inc_management\": %d, \"busy\": %d, \"toggles\": [",
            v && shown(v), set && shown(set), p && v ? AT(p, o_vt, int32_t) : -1, x.c, x.m, x.i, x.f, x.g, x.l, e1, e2, e3, e4, max_level(p),
            top_level(), unlocked(p), inter(set, o_inc), inter(set, o_minc), dsl_busy_reason() != NULL);
    for (int k = 0; k < nd; k++) {
        DSLObj w = held(&D[k].h_tog); float r[4] = { 0 }; char lb[200] = "", el[400];
        int sh = w ? dsl_ui_screen_rect(w, r) : 0; if (w) dsl_ui_text_get(w, lb, sizeof lb); jesc(el, sizeof el, lb);
        fprintf(f, "%s{\"id\": \"%s\", \"exists\": %d, \"shown\": %d, \"on\": %d, \"saved\": %d, \"running\": %d, \"label\": \"%s\", \"rect\": [%.1f, %.1f, %.1f, %.1f], "
                   "\"text_w\": %.1f, \"width\": %.1f}",
                k ? ", " : "", D[k].id, w != NULL, sh, w ? U->toggle_get(w) : -1, show_get(D[k].id), running(D[k].id), el, r[0], r[1], r[2], r[3],
                w ? dsl_ui_text_line_w(w) : 0, w ? dsl_ui_width(w) : 0);
    }
    float P[4] = { 0 }; Obj ptr = set ? tr_of(set) : NULL; if (ptr) dsl_ui_screen_rect((DSLObj)ptr, P);
    float *S = dsl_newgame_pn_rect, *F = dsl_newgame_frame_rect, *W = dsl_newgame_row_rect; float sw, sh; dsl_ui_screen_size(&sw, &sh);
    fprintf(f, "], \"panel\": [%.1f, %.1f, %.1f, %.1f], \"side\": [%.1f, %.1f, %.1f, %.1f], \"window\": [%.1f, %.1f, %.1f, %.1f], \"row\": [%.1f, %.1f, %.1f, %.1f], "
               "\"side_shown\": %d, \"screen\": [%.0f, %.0f]}\n", P[0], P[1], P[2], P[3], S[0], S[1], S[2], S[3], F[0], F[1], F[2], F[3], W[0], W[1], W[2], W[3],
            held(&h_pn) ? dsl_ui_shown(held(&h_pn)) : 0, sw, sh);
    fclose(f); rename(tmp, pth);
}
void dsl_newgame_test_cmd(const JNode *j) {
    const char *op = json_str(j, "op"); Obj p = presenter(), v = view_of(p);
    if (!ok || !op) { dsl_log("newgame test: %s refused (switches off or no op)", op ? op : "?"); return; }
    if (!strcmp(op, "open")) {
        Obj tv = m_find && t_title ? CALL(m_find, Obj, Obj, uint8_t)(t_title, 0, m_find) : NULL;
        if (live(tv) && m_newgame) CALL(m_newgame, void, Obj)(tv, m_newgame);
        dsl_log("newgame test: open (title view %s)", live(tv) ? "found" : "MISSING");
    } else if (!strcmp(op, "next") && v && AT(p, o_vt, int32_t) != 2) {
        CALL(m_next, void, Obj)(p, m_next); dsl_log("newgame test: next (view type now %d)", AT(p, o_vt, int32_t));
    } else if (!strcmp(op, "start") && v && AT(p, o_vt, int32_t) == 2) {
        dsl_log("newgame test: start (combat %d, management %d)", AT(p, o_cd, int32_t), AT(p, o_md, int32_t)); CALL(m_next, void, Obj)(p, m_next);
    } else if (!strcmp(op, "set") && v) {
        Choices x; choices_of(p, &x);
        JNode *n;
        if ((n = json_get(j, "combat")) && n->t == JN_NUM) x.c = (int)n->num;
        if ((n = json_get(j, "management")) && n->t == JN_NUM) x.m = (int)n->num;
        if ((n = json_get(j, "magic")) && n->t == JN_NUM) x.g = n->num != 0;
        if ((n = json_get(j, "lock")) && n->t == JN_NUM) x.l = n->num != 0;
        if ((n = json_get(j, "iron")) && n->t == JN_NUM) x.i = x.f = n->num != 0;
        if (json_str(j, "name")) snprintf(x.name, sizeof x.name, "%s", json_str(j, "name"));
        if (json_str(j, "seed")) snprintf(x.seed, sizeof x.seed, "%s", json_str(j, "seed"));
        int c = 0, m = 0; choices_apply(p, &x, 1, &c, &m);
        dsl_log("newgame test: set combat %d management %d", c, m);
    } else if (!strcmp(op, "force")) {                                 /* a level written past the selectors (a key path): clamp() */
        JNode *n = json_get(j, "combat"); if (p && n && n->t == JN_NUM) AT(p, o_cd, int32_t) = (int)n->num;
        dsl_log("newgame test: force combat %d", p ? AT(p, o_cd, int32_t) : -1);
    } else if (!strcmp(op, "toggle")) {
        JNode *n = json_get(j, "i"); int k = n && n->t == JN_NUM ? (int)n->num : 0;
        DSLObj w = k >= 0 && k < nd ? held(&D[k].h_tog) : NULL;
        if (w) { U->toggle_set(w, !U->toggle_get(w)); tog_click((void *)(intptr_t)k, w); }   /* as the kit does on a click */
        dsl_log("newgame test: toggle %d %s", k, w ? "clicked" : "MISSING");
    } else if (strcmp(op, "state")) dsl_log("newgame test: %s refused (screen %s, view type %d)", op, v ? "open" : "closed", v ? AT(p, o_vt, int32_t) : -1);
    state_write();
}
