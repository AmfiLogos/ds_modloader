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
static Obj t_set;                                        /* System.Type of SubUI_CampaignSetting (the mods' panel without the switches) */
static int side_ok;                                      /* the side column can be drawn (the mods panel; the switches need ok too) */
static Obj t_sr; static void *m_getcomp, *m_sr_vpos;      /* test "scroll": the settings list's ScrollRect */
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
/* ------------------------------------------------------------------ the side column: Custom difficulty + the world's mods
   Owner 2026-10-25: "Custom difficulty toggle moves at the same time as the scroll" (the panel followed the combat row);
   "Mods for this world appear atop the interface - should likely be below or on the side below custom difficulty" (the line
   lay over the settings list). Both panels sit outside the New Game window on its right, one width, children of the
   settings panel (they show and go with it): the Custom difficulty panel (its switches), its top level with the settings
   list's top whatever the scroll; right under it "Mods of this world" (the mods a world made here plays with, one name per
   line, at most MP_LINES lines: then the first names and "+N", the hover card has them all; Change = the Mods window). The
   Load list filter panel's placing rules (dsl_rows.c): never over the title corner, inside the screen, scaled down when the
   screen is too narrow or too short for it. Local units of the settings panel from screen rects. */
#define PN_W 300.f
#define PN_PAD 14.f
#define PN_ROW 42.f
#define PN_GAP 16.f
#define MP_BODY_Y 48.f
#define MP_BTN_W 110.f
#define MP_BTN_H 34.f
#define MP_LINES 7
static uintptr_t h_pn, h_pn_tt, h_mp, h_mp_tt, h_mp_body, h_mp_btn, h_found;
static unsigned pn_gen, mp_gen; static float pn_need, pn_ht, mp_need, mp_btn_w = MP_BTN_W, mp_bh, mp_ht, col_w, col_last[7];
static char mp_all[1200], mp_text[1300]; static int mp_n, mp_k, mp_stale = 1, mp_lines = MP_LINES;   /* mp_lines: test "mods_lines" */
static float mp_fill_w; static int mp_fill_lines;
float dsl_newgame_pn_rect[4], dsl_newgame_mp_rect[4], dsl_newgame_frame_rect[4], dsl_newgame_col_scale;   /* test: the last placing */
static void pn_drop(void) {
    DSLObj p = held(&h_pn); if (p) U->destroy(p);
    hold(&h_pn, NULL); hold(&h_pn_tt, NULL); for (int k = 0; k < MAXD; k++) hold(&D[k].h_tog, NULL);
}
/* the Custom difficulty panel: its title and one switch per difficulty mod; pn_need = the width its texts need on one line */
static DSLObj pn_build(Obj set) {
    DSLObj pn = held(&h_pn);
    if (pn && pn_gen != dsi_generation()) { pn_drop(); pn = NULL; }   /* a language change: built again */
    if (pn) return pn;
    diff_scan(); if (!nd) return NULL;
    pn = U->panel(tr_of(set), 0, 0, PN_W, 100, 0);                     /* the game's dark Box panel */
    if (!pn) return NULL;
    hold(&h_pn, pn); pn_gen = dsi_generation(); col_w = 0;
    DSLObj tt = U->text(pn, PN_PAD, 10, PN_W - 2 * PN_PAD, 30, TR("Custom difficulty"), 20, DSL_TXT_LEFT | DSL_TXT_TITLE, 0xD9BE8FFF);
    hold(&h_pn_tt, tt);
    float need = (tt ? dsl_ui_text_line_w(tt) : 0) + 2 * PN_PAD, y = 48;
    for (int k = 0; k < nd; k++) {
        const DSLModInfo *m = mod_of(D[k].id); if (!m) continue;
        DSLObj w = U->toggle(pn, PN_PAD, y, PN_W - 2 * PN_PAD, m->name, running(D[k].id) && show_get(D[k].id), tog_click, (void *)(intptr_t)k);
        if (!w) continue;
        hold(&D[k].h_tog, w); tog_tip(k, w);
        if (!running(D[k].id)) U->set_enabled(w, 0);
        float lw = dsl_ui_text_line_w(w) + 76 + 2 * PN_PAD; if (lw > need) need = lw;   /* label + the box, one line */
        y += PN_ROW;
    }
    pn_need = need; pn_ht = y + PN_PAD - (PN_ROW - 36);
    dsl_log("newgame: Custom difficulty panel made on the New Game screen (%d switch(es), width needed %.0f)", nd, need);
    return pn;
}
static void pn_layout(float w) {
    DSLObj tt = held(&h_pn_tt); if (tt) U->place(tt, PN_PAD, 10, w - 2 * PN_PAD, 30);
    for (int k = 0, y = 48; k < nd; k++) { DSLObj t = held(&D[k].h_tog); if (t) { U->place(t, PN_PAD, (float)y, w - 2 * PN_PAD, 36); y += (int)PN_ROW; } }
}
static void mp_click(void *ud, DSLObj w) { (void)ud; (void)w; dsl_log("newgame: Mods of this world [Change]"); dsl_newworld_change(); }
static void mp_drop(void) {
    DSLObj p = held(&h_mp); if (p) U->destroy(p);
    hold(&h_mp, NULL); hold(&h_mp_tt, NULL); hold(&h_mp_body, NULL); hold(&h_mp_btn, NULL);
}
/* the "Mods of this world" panel: its title, the names, Change; mp_need = the width its title and button need */
static DSLObj mp_build(Obj set) {
    DSLObj mp = held(&h_mp);
    if (mp && mp_gen != dsi_generation()) { mp_drop(); mp = NULL; }
    if (mp) return mp;
    mp = U->panel(tr_of(set), 0, 0, PN_W, 100, 0);
    if (!mp) return NULL;
    hold(&h_mp, mp); mp_gen = dsi_generation(); col_w = 0; mp_stale = 1; mp_fill_w = 0;
    DSLObj tt = U->text(mp, PN_PAD, 10, PN_W - 2 * PN_PAD, 30, TR("Mods of this world"), 20, DSL_TXT_LEFT | DSL_TXT_TITLE, 0xD9BE8FFF);
    DSLObj body = U->text(mp, PN_PAD, MP_BODY_Y, PN_W - 2 * PN_PAD, 24, "", 16, DSL_TXT_LEFT, 0xD9C7B0FF);
    DSLObj btn = U->button(mp, PN_W - PN_PAD - MP_BTN_W, MP_BODY_Y + 34, MP_BTN_W, MP_BTN_H, TR("Change"), mp_click, NULL);
    hold(&h_mp_tt, tt); hold(&h_mp_body, body); hold(&h_mp_btn, btn);
    float bl = btn ? dsl_ui_text_line_w(btn) + 32 : 0;                 /* the button's label + its frame */
    mp_btn_w = bl > MP_BTN_W ? bl : MP_BTN_W;
    mp_need = (tt ? dsl_ui_text_line_w(tt) : 0) + 2 * PN_PAD;
    if (mp_btn_w + 2 * PN_PAD > mp_need) mp_need = mp_btn_w + 2 * PN_PAD;
    dsl_log("newgame: Mods of this world panel made on the New Game screen (width needed %.0f)", mp_need);
    return mp;
}
static float text_h(DSLObj t, float w) { return U->text_height ? U->text_height(t, w) : 0; }
/* the names at width w: every one (long ones wrap), else the first k and "+N" so the text keeps to mp_lines lines (the Load
   list's "+N" rule); k = 0: "+N" alone. Returns 1 when the text or its height changed. */
static int mp_fill(float w) {
    DSLObj body = held(&h_mp_body); if (!body) return 0;
    if (!mp_stale && w == mp_fill_w && mp_lines == mp_fill_lines) return 0;
    mp_stale = 0; mp_fill_w = w; mp_fill_lines = mp_lines;
    U->set_text(body, mp_all);
    float one = text_h(body, 100000.f) / (mp_n > 1 ? mp_n : 1), max = one * mp_lines + 0.5f, h = text_h(body, w);   /* one line in its own font */
    char t[1300]; snprintf(t, sizeof t, "%s", mp_all); int k = mp_n;
    while (k > 0 && h > max) {
        k--;
        size_t n = 0; int c = 0;
        while (mp_all[n] && c < k && n < sizeof t - 16) { if (mp_all[n] == '\n') c++; n++; }   /* the first k names, each with its line break */
        memcpy(t, mp_all, n); snprintf(t + n, sizeof t - n, "+%d", mp_n - k);
        U->set_text(body, t); h = text_h(body, w);
    }
    if (k < mp_n) dsl_log("newgame: Mods of this world: %d of %d name(s), then +%d (the hover card lists them all)", k, mp_n, mp_n - k);
    U->tooltip(body, TR("Mods of this world"), k < mp_n ? mp_all : NULL);
    float bh = h > one ? h : one; int changed = strcmp(t, mp_text) || bh != mp_bh;
    snprintf(mp_text, sizeof mp_text, "%s", t); mp_bh = bh; mp_k = k;
    return changed;
}
static void mp_layout(float w) {
    DSLObj tt = held(&h_mp_tt), body = held(&h_mp_body), btn = held(&h_mp_btn);
    if (tt) U->place(tt, PN_PAD, 10, w - 2 * PN_PAD, 30);
    if (body) U->place(body, PN_PAD, MP_BODY_Y, w - 2 * PN_PAD, mp_bh);
    if (btn) U->place(btn, w - PN_PAD - mp_btn_w, MP_BODY_Y + mp_bh + 10, mp_btn_w, MP_BTN_H);
    mp_ht = MP_BODY_Y + mp_bh + 10 + MP_BTN_H + PN_PAD;
}
/* both panels from the screen: x right of the window, top level with the list's top, under the title corner when they would
   meet it, inside the screen (moved up, then smaller when too tall) */
static void col_place(Obj set, DSLObj pn, DSLObj mp) {
    float need = pn ? pn_need : 0; if (mp && mp_need > need) need = mp_need;
    float W = need < PN_W ? PN_W : need > 2 * PN_W ? 2 * PN_W : need; int relaid = 0;
    if (W != col_w) { col_w = W; if (pn) pn_layout(W); mp_fill_w = 0; relaid = 1; }
    if (mp && (mp_fill(W - 2 * PN_PAD) || relaid)) mp_layout(W);
    Obj str = tr_of(set), fr = str ? CALL(m_parent, Obj, Obj)(str, m_parent) : NULL;   /* the window (Panel_CreateCampaignPanel) */
    float P[4], F[4], L[4], C[4], sw, sh;
    if (!str || dsl_ui_screen_rect((DSLObj)str, P) != 1) return;
    if (!fr || dsl_ui_screen_rect((DSLObj)fr, F) != 1) memcpy(F, P, sizeof F);
    DSLObj vp = dsl_ui_child((DSLObj)str, "Viewport"); if (!vp || dsl_ui_screen_rect(vp, L) != 1) memcpy(L, P, sizeof L);
    float lw = dsl_ui_width((DSLObj)str), s = lw > 50 ? (P[2] - P[0]) / lw : 0; if (s <= 0) return;   /* px per unit; not laid out yet */
    dsl_ui_screen_size(&sw, &sh);
    float ph = pn ? pn_ht : 0, gap = pn && mp ? PN_GAP : 0, H = ph + gap + (mp ? mp_ht : 0);
    float m = 8 * s, x0 = F[2] + PN_GAP * s, k = (sw - m - x0) / (W * s);
    if (k > 1) k = 1;
    float top = L[3], tmax = sh - m;                                   /* the list's top: the list scrolls, the column does not */
    if (dsl_update_corner_rect(C) && x0 < C[2] && x0 + W * s * k > C[0] && C[1] - PN_GAP * s < tmax) tmax = C[1] - PN_GAP * s;
    if (top > tmax) top = tmax;
    if (top - H * s * k < m) top = m + H * s * k < tmax ? m + H * s * k : tmax;   /* too low: up, never over the corner */
    if (top - H * s * k < m && H > 0) k = (top - m) / (H * s);         /* still too tall: smaller */
    if (k <= 0.05f) return;
    float v[7] = { (x0 - P[0]) / s, (P[3] - top) / s, W, ph, (P[3] - top) / s + (ph + gap) * k, mp ? mp_ht : 0, k }; int same = !relaid;
    for (int i = 0; i < 7; i++) { float d = v[i] - col_last[i]; same &= d > -0.5f && d < 0.5f; }
    if (same) return;
    memcpy(col_last, v, sizeof v);
    if (pn) { U->place(pn, v[0], v[1], W, ph); dsl_ui_set_scale(pn, k); }
    if (mp) { U->place(mp, v[0], v[4], W, v[5]); dsl_ui_set_scale(mp, k); }
    float pr[4] = { x0, top - ph * s * k, x0 + W * s * k, top }, mt = top - (ph + gap) * s * k, mr[4] = { x0, mt - v[5] * s * k, x0 + W * s * k, mt };
    memcpy(dsl_newgame_pn_rect, pn ? pr : (float[4]){ 0 }, sizeof pr); memcpy(dsl_newgame_mp_rect, mp ? mr : (float[4]){ 0 }, sizeof mr);
    memcpy(dsl_newgame_frame_rect, F, sizeof F); dsl_newgame_col_scale = k;
    dsl_log("newgame: side panels placed: screen %.0fx%.0f, window right %.0f (%.2f px/unit), column %.0f,%.0f..%.0f,%.0f, width %.0f, scale %.2f%s",
            sw, sh, F[2], s, x0, top - H * s * k, x0 + W * s * k, top, W, k, k < 0.5f ? " (small: little room right of the window)" : "");
}
/* the settings step's panel (SubUI_CampaignSetting) while it shows: the live presenter's; without the switches (a name
   missing) the one FindObjectOfType finds, looked for every 30 frames, so the mods still show */
static Obj side_set(unsigned f) {
    if (ok) { Obj p = presenter(), set = setpanel_of(view_of(p)); return set && shown(set) && AT(p, o_vt, int32_t) == 2 ? set : NULL; }
    Obj s = (Obj)held(&h_found);
    if (!s && f % 30 == 0 && m_find && t_set) { s = CALL(m_find, Obj, Obj, uint8_t)(t_set, 0, m_find); if (live(s)) hold(&h_found, (DSLObj)s); else s = NULL; }
    return s && shown(s) ? s : NULL;
}
static void side_frame(void) {
    static unsigned f; if (++f % 2) return;
    Obj set = side_set(f); if (!set) return;                           /* children of the settings panel: hidden with it */
    if (ok) clamp(presenter());
    DSLObj pn = ok ? pn_build(set) : NULL, mp = mp_build(set);
    if (mp && (mp_stale || f % 30 == 0)) {                             /* the names follow the levels chosen (Hell) */
        char all[sizeof mp_all]; int n = dsl_newworld_names(all, sizeof all);
        if (n != mp_n || strcmp(all, mp_all)) { snprintf(mp_all, sizeof mp_all, "%s", all); mp_n = n; mp_stale = 1; }
    }
    if (pn || mp) col_place(set, pn, mp);
}
/* dsl_reg.c (the New Game screen's "Mods of this world"): the levels chosen on the settings step now; 0 = not there */
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
    if (ctype && tobj && k_set) t_set = tobj(ctype(k_set));
    side_ok = U && m_alive && m_comp_go && m_comp_tr && m_go_tree && m_parent && gc_new && gc_get && gc_free;
    if (!side_ok) dsl_log("newgame: side panels OFF (names missing)");
    void *k_sr = dsl_find_class("UnityEngine.UI", "ScrollRect");       /* test only */
    if (ctype && tobj && k_sr) t_sr = tobj(ctype(k_sr));
    m_getcomp = kComp ? dsl_find_method(kComp, "GetComponentInChildren", 1) : NULL; m_sr_vpos = k_sr ? dsl_find_method(k_sr, "set_verticalNormalizedPosition", 1) : NULL;
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
    if (!side_ok) return;
    side_frame();
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
    float *S = dsl_newgame_pn_rect, *F = dsl_newgame_frame_rect, W[4] = { 0 }; float sw, sh; dsl_ui_screen_size(&sw, &sh);
    Obj row = set && o_inc >= 0 ? tr_of(AT(set, o_inc, Obj)) : NULL;                  /* the combat row: arrow -> its content -> the row */
    for (int i = 0; i < 2 && row; i++) { Obj up = CALL(m_parent, Obj, Obj)(row, m_parent); if (!up || up == ptr) break; row = up; }
    if (row) dsl_ui_screen_rect((DSLObj)row, W);
    float SR[4] = { 0 }, L[4] = { 0 }, FR[4] = { 0 }, C[4] = { 0 }, M[4] = { 0 };             /* screen rects now (not the last placing) */
    DSLObj pn = held(&h_pn); int pn_on = pn && dsl_ui_screen_rect(pn, SR) == 1 && dsl_ui_shown(pn);
    DSLObj vp = ptr ? dsl_ui_child((DSLObj)ptr, "Viewport") : NULL; if (vp) dsl_ui_screen_rect(vp, L);
    Obj fr = ptr ? CALL(m_parent, Obj, Obj)(ptr, m_parent) : NULL; if (fr) dsl_ui_screen_rect((DSLObj)fr, FR);
    DSLObj mp = held(&h_mp), body = held(&h_mp_body), btn = held(&h_mp_btn);
    int c_on = dsl_update_corner_rect(C), m_on = mp && dsl_ui_screen_rect(mp, M) == 1 && dsl_ui_shown(mp);
    /* the mods' text inside its box (TMP rendered size vs the box, local units) and Change inside the panel */
    float rw = 0, rh = 0, B[4] = { 0 }; int fit = 0, btn_in = 0; char mt[1300] = "", em[2700];
    if (body && dsl_ui_text_rendered(body, &rw, &rh)) fit = rh <= mp_bh + 1 && rw <= dsl_ui_width(body) + 1;
    if (btn && m_on && dsl_ui_screen_rect(btn, B) == 1) btn_in = B[0] >= M[0] - 0.5f && B[2] <= M[2] + 0.5f && B[1] >= M[1] - 0.5f && B[3] <= M[3] + 0.5f && dsl_ui_shown(btn);
    if (body) dsl_ui_text_get(body, mt, sizeof mt); jesc(em, sizeof em, mt);
    fprintf(f, "], \"panel\": [%.1f, %.1f, %.1f, %.1f], \"side\": [%.1f, %.1f, %.1f, %.1f], \"window\": [%.1f, %.1f, %.1f, %.1f], \"row\": [%.1f, %.1f, %.1f, %.1f], "
               "\"side_shown\": %d, \"screen\": [%.0f, %.0f], ", P[0], P[1], P[2], P[3], S[0], S[1], S[2], S[3], F[0], F[1], F[2], F[3], W[0], W[1], W[2], W[3],
            pn ? dsl_ui_shown(pn) : 0, sw, sh);
    fprintf(f, "\"side_now\": [%.1f, %.1f, %.1f, %.1f], \"side_now_shown\": %d, \"list\": [%.1f, %.1f, %.1f, %.1f], \"frame\": [%.1f, %.1f, %.1f, %.1f], "
               "\"corner\": [%.1f, %.1f, %.1f, %.1f], \"corner_shown\": %d, \"mods\": [%.1f, %.1f, %.1f, %.1f], \"mods_shown\": %d, \"side_scale\": %.3f, "
               "\"mods_fit\": {\"ok\": %d, \"btn_in\": %d, \"rendered\": [%.1f, %.1f], \"box\": [%.1f, %.1f], \"names\": %d, \"shown_names\": %d, \"text\": \"%s\"}}\n",
            SR[0], SR[1], SR[2], SR[3], pn_on, L[0], L[1], L[2], L[3], FR[0], FR[1], FR[2], FR[3], C[0], C[1], C[2], C[3], c_on, M[0], M[1], M[2], M[3], m_on,
            dsl_newgame_col_scale, fit, btn_in, rw, rh, body ? dsl_ui_width(body) : 0, mp_bh, mp_n, mp_k, em);
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
    } else if (!strcmp(op, "scroll")) {                                /* the settings list scrolled: 1 = top, 0 = bottom */
        Obj str = tr_of(setpanel_of(v)), sr = str && t_sr && m_getcomp && m_sr_vpos ? CALL(m_getcomp, Obj, Obj, Obj)(str, t_sr, m_getcomp) : NULL;
        JNode *n = json_get(j, "pos"); float pos = n && n->t == JN_NUM ? (float)n->num : 1;
        if (live(sr)) CALL(m_sr_vpos, void, Obj, float)(sr, pos, m_sr_vpos);
        dsl_log("newgame test: scroll %.2f (%s)", pos, live(sr) ? "ScrollRect" : "no ScrollRect");
    } else if (!strcmp(op, "mods_lines")) {                            /* the most lines of names (a long list's "+N") */
        JNode *n = json_get(j, "n"); mp_lines = n && n->t == JN_NUM && n->num >= 1 ? (int)n->num : MP_LINES; mp_stale = 1;
        dsl_log("newgame test: mods_lines %d", mp_lines);
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
