// Load list service (dsl_sdk.h API 3: loadrow_add, loadlist_filter, loadlist_refresh) + the loader's own lines:
// world rows of the stock Load list get "Mods: <names>  Change" (the New Game screen's mods: ui/dsl_newgame.c).
// No hook on the row sites: the multiplayer module hooks SubUI_SaveFileButton.SetData, SubUI_SettlerNameCase.Init and
// SaveLoadView.RefreshLoadPanel until it moves to this API (MASTER_PLAN 5.1 wp-menu), and the engine allows one owner per
// site. So this file polls: every few frames it looks at the open SaveLoadView; when its rows changed it rebuilds our lines.
//   SaveLoadView._campaignCases (List<SubUI_SettlerNameCase>), _saveNameSlotDictionary (save name -> SubUI_SaveFileButton);
//   a world row's guid = the save header's CampaignGuid (CampaignLoadingEventHandler.TryGetSaveHeader) of one of its saves.
// Filters hide a world row and its save rows (SetActive false), then the stock SaveLoadView.RefreshPanelSize.
// The filter panel (owner 2026-10-24) on the right of the Load window: which kinds of worlds show, and "Latest save only".
#include "dsl_internal.h"
#include "dsl_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))

/* ------------------------------------------------------------------ registry */
#define MAXR 16
static struct { DSLLoadRow r; char owner[64]; } R[MAXR]; static int nr;
static struct { DSLLoadFilter f; char owner[64]; int on, mode; DSLObj tog; } F[MAXR]; static int nf;
static int dirty;
int dsl_rows_add(const char *owner, const DSLLoadRow *r) {
    if (!r || !r->text || nr >= MAXR) return -1;
    R[nr].r = *r; snprintf(R[nr].owner, sizeof R[nr].owner, "%s", owner ? owner : "?"); nr++;
    dsl_log("rows: load-row text added by %s", owner); dirty = 1; return 0;
}
int dsl_rows_filter(const char *owner, const DSLLoadFilter *f) {
    if (!f || !f->show || nf >= MAXR) return -1;
    memset(&F[nf], 0, sizeof F[nf]);
    F[nf].f = *f; snprintf(F[nf].owner, sizeof F[nf].owner, "%s", owner ? owner : "?"); nf++;
    dsl_log("rows: load-list filter added by %s", owner); dirty = 1; return 0;
}
void dsl_rows_refresh(void) { dirty = 1; }
/* test only (dsl_reg.c test_frame): every filter toggle on, as a click would do */
void dsl_rows_test_toggles(int on) {
    const DSLUi *u = dsl_ui_kit();
    for (int i = 0; i < nf; i++) { F[i].on = on; if (u && F[i].tog) u->toggle_set(F[i].tog, on); }
    dirty = 1;
}

/* ------------------------------------------------------------------ game access by name */
static int ok;
static void *k_slv, *k_case, *k_btn, *k_str;
typedef struct { float x, y, w, h; } URect;
static URect (*rt_rect)(Obj t, const void *mi);
static int32_t o_cases = -1, o_dict = -1, o_slots = -1, o_title_name = -1, o_cur = -1, o_lowload = -1;
static Obj (*find_obj)(Obj type, int inactive, const void *mi);
static uint8_t (*alive)(Obj o, const void *mi);
static Obj (*comp_go)(Obj c, const void *mi), (*comp_tr)(Obj c, const void *mi), (*go_trf)(Obj g, const void *mi);
static uint8_t (*go_tree)(Obj g, const void *mi), (*go_self)(Obj g, const void *mi);
static void (*go_set)(Obj g, int on, const void *mi);
static void (*refresh_size)(Obj slv, const void *mi);
static uint8_t (*try_header)(Obj self, Obj name, void *hdr, Obj *set, const void *mi);
static Obj (*str_new)(const char *);
static void *(*obj_class)(Obj);
static Obj t_slv;
static uintptr_t (*gc_new)(Obj, int); static Obj (*gc_get)(uintptr_t); static void (*gc_free)(uintptr_t);
static int32_t mf_handler = -1;
static int32_t o_parent = -1;                            /* SaveLoadView._saveSlotParent (the hidden-worlds lines) */
static int32_t o_issel = -1;                             /* SubUI_SaveFileButton._isSelected (the selected save) */
static void (*unselect_all)(Obj slv, const void *mi), (*lock_buttons)(Obj slv, const void *mi);
static int32_t o_bcount = -1;                            /* SubUI_SettlerNameCase._buttonCount (the save rows its height counts) */
static void (*case_refresh)(Obj cs, const void *mi);     /* SubUI_SettlerNameCase.RefreshSize */
static void (*set_sib)(Obj t, int i, const void *mi);
static int (*get_sib)(Obj t, const void *mi);
static void (*set_last)(Obj t, const void *mi);

static void *cls(const char *ns, const char *n) { void *c = dsl_find_class(ns, n); if (!c) dsl_log("rows: class %s.%s missing", ns, n); return c; }
static int32_t fo(void *k, const char *n) { int32_t o = k ? dsl_field_offset(k, n) : -1; if (o < 0) dsl_log("rows: field %s missing", n); return o; }
static void *ma(void *k, const char *n, int argc) { void *m = k ? dsl_find_method(k, n, argc) : NULL; return m ? dsl_method_address(m) : NULL; }
static int live(Obj o) { return o && alive && alive(o, NULL); }
static int shown(Obj comp) { Obj g = live(comp) ? comp_go(comp, NULL) : NULL; return g && go_tree(g, NULL); }
static void set_on(Obj comp, int on) { Obj g = live(comp) ? comp_go(comp, NULL) : NULL; if (g && go_self(g, NULL) != on) go_set(g, on, NULL); }

static void guard_install(void);
void dsl_rows_install(void) {
    k_slv = cls("Refactor", "SaveLoadView"); k_case = cls("Refactor", "SubUI_SettlerNameCase"); k_btn = cls("Refactor.UI", "SubUI_SaveFileButton");
    k_str = cls("System", "String");
    o_cases = fo(k_slv, "_campaignCases"); o_dict = fo(k_slv, "_saveNameSlotDictionary"); o_lowload = fo(k_slv, "_lowLoadPanel");
    o_slots = fo(k_case, "_saveSlots"); o_title_name = fo(k_case, "_nameText"); o_cur = fo(k_case, "_isCurrentCampaign");
    void *kObj = cls("UnityEngine", "Object"), *kComp = cls("UnityEngine", "Component"), *kGo = cls("UnityEngine", "GameObject");
    find_obj = ma(kObj, "FindObjectOfType", 2); alive = ma(kObj, "op_Implicit", 1);
    comp_go = ma(kComp, "get_gameObject", 0); comp_tr = ma(kComp, "get_transform", 0);
    go_trf = ma(kGo, "get_transform", 0);
    go_tree = ma(kGo, "get_activeInHierarchy", 0); go_self = ma(kGo, "get_activeSelf", 0); go_set = ma(kGo, "SetActive", 1);
    refresh_size = ma(k_slv, "RefreshPanelSize", 0);
    o_parent = fo(k_slv, "_saveSlotParent");                               /* the list's rows (the hidden-worlds lines) */
    void *kTr = cls("UnityEngine", "Transform");
    set_sib = ma(kTr, "SetSiblingIndex", 1); get_sib = ma(kTr, "GetSiblingIndex", 0); set_last = ma(kTr, "SetAsLastSibling", 0);
    rt_rect = ma(cls("UnityEngine", "RectTransform"), "get_rect", 0);
    o_issel = fo(k_btn, "_isSelected"); unselect_all = ma(k_slv, "UnselectAllSlots", 0); lock_buttons = ma(k_slv, "LockLoadButtons", 0);
    o_bcount = fo(k_case, "_buttonCount"); case_refresh = ma(k_case, "RefreshSize", 0);
    void *kh = cls("Refactor.Main.Event", "CampaignLoadingEventHandler"); try_header = ma(kh, "TryGetSaveHeader", 3);
    void *kmf = cls("Refactor.Main", "MainFlow"); mf_handler = fo(kmf, "_eventHandler");
    str_new = dsl_ga_sym("il2cpp_string_new"); obj_class = dsl_ga_sym("il2cpp_object_get_class");
    gc_new = dsl_ga_sym("il2cpp_gchandle_new"); gc_get = dsl_ga_sym("il2cpp_gchandle_get_target"); gc_free = dsl_ga_sym("il2cpp_gchandle_free");
    void *(*ctype)(void *) = dsl_ga_sym("il2cpp_class_get_type"); Obj (*tobj)(void *) = dsl_ga_sym("il2cpp_type_get_object");
    if (ctype && tobj && k_slv) t_slv = tobj(ctype(k_slv));
    ok = find_obj && alive && comp_go && comp_tr && go_tree && go_self && go_set && refresh_size && try_header && str_new && obj_class
         && gc_new && gc_get && gc_free && t_slv && o_cases >= 0 && o_dict >= 0 && o_slots >= 0 && o_title_name >= 0 && mf_handler >= 0;
    dsl_log("rows: load list service %s", ok ? "on (polling the open Load list)" : "OFF (names missing)");
    if (ok) guard_install();
}

/* ------------------------------------------------------------------ strings */
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
static void jesc_(char *out, size_t n, const char *s) {                   /* test state file (lf_state_write) */
    size_t k = 0;
    for (; s && *s && k + 7 < n; s++) {
        if (*s == '"' || *s == '\\') { out[k++] = '\\'; out[k++] = *s; }
        else if ((unsigned char)*s < 0x20) k += (size_t)snprintf(out + k, n - k, "\\u%04x", *s);
        else out[k++] = *s;
    }
    out[k] = 0;
}
static void guid_text(const uint8_t *b, char *out) {
    snprintf(out, 40, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             *(const uint32_t *)b, *(const uint16_t *)(b + 4), *(const uint16_t *)(b + 6), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

/* save name -> guid (headers read once per name) */
#define MAXS 256
static struct { char name[256], guid[40]; } SG[MAXS]; static int nsg;
static const char *guid_of_save(const char *name) {
    for (int i = 0; i < nsg; i++) if (!strcmp(SG[i].name, name)) return SG[i].guid;
    Obj mf = dsl_soft_mainflow(), h = mf ? AT(mf, mf_handler, Obj) : NULL, lh = h ? AT(h, 0x28, Obj) : NULL;   /* EventHandler +0x28 = CampaignLoadingEventHandler */
    if (!lh) return NULL;
    uint8_t hdr[0x100] = {0}; Obj set = NULL;
    if (!try_header(lh, str_new(name), hdr, &set, NULL)) return NULL;
    int i = nsg < MAXS ? nsg++ : MAXS - 1;
    snprintf(SG[i].name, sizeof SG[i].name, "%s", name); guid_text(hdr, SG[i].guid);
    { int dsl_hdr_levels(const uint8_t *hdr, int *c, int *m); int c, m;   /* custom difficulty: a world uses Hell only on Hell */
      if (dsl_hdr_levels(hdr, &c, &m)) dsl_world_note_level(SG[i].guid, c, m); }
    return SG[i].guid;
}

/* ------------------------------------------------------------------ the open list */
#define MAXW 64
typedef struct { uintptr_t cs; char guid[40], save[256], name[160]; DSLObj line, chg; int hid, game, kind, why; } WRow;   /* why: 1 the panel hid it, 2 a toggle under the list */
static WRow W[MAXW]; static int nw;
static uintptr_t h_slv; static uint32_t sig_last; static int list_open;
static DSLObj band;                                     /* our toggles' strip */

static void rows_clear(void) {
    const DSLUi *u = dsl_ui_kit();
    for (int i = 0; i < nw; i++) { if (u && W[i].line) u->destroy(W[i].line); if (u && W[i].chg) u->destroy(W[i].chg); gc_free(W[i].cs); }
    nw = 0;
}
static void *m_tmp_get; static Obj (*tmp_get_f)(Obj, const void *);
static uint32_t list_sig(Obj slv) {
    Obj l = AT(slv, o_cases, Obj); int n = l ? AT(l, 0x18, int32_t) : 0; Obj a = l ? AT(l, 0x10, Obj) : NULL;
    if (!m_tmp_get) { void *k = dsl_find_class("TMPro", "TMP_Text"); m_tmp_get = k ? dsl_find_method(k, "get_text", 0) : NULL; if (m_tmp_get) tmp_get_f = dsl_method_address(m_tmp_get); }
    uint32_t s = (uint32_t)n * 2654435761u;
    for (int i = 0; i < n && a; i++) {
        Obj c = AT(a, 0x20 + 8 * i, Obj); s = s * 31 + (uint32_t)(uintptr_t)c;
        if (!c || !shown(c)) continue;
        s ^= 0x9e37u + (uint32_t)i;
        Obj nt = o_title_name >= 0 ? AT(c, o_title_name, Obj) : NULL;      /* its name text: a module rewrote it (Remove tag) = again */
        if (tmp_get_f && live(nt)) s = s * 131 + (uint32_t)(uintptr_t)tmp_get_f(nt, m_tmp_get);
    }
    Obj d = AT(slv, o_dict, Obj); s = s * 31 + (d ? (uint32_t)AT(d, 0x20, int32_t) : 0);
    return s;
}
static void change_click(void *ud, DSLObj w) {
    (void)w; int i = (int)(intptr_t)ud; if (i < 0 || i >= nw) return;
    dsl_log("rows: [Change] world %s", W[i].guid);
    dsl_world_change(W[i].guid, W[i].name[0] ? W[i].name : W[i].save);   /* the world, not one of its saves (owner v13 #4) */
}
static void toggle_change(void *ud, DSLObj w) {
    int i = (int)(intptr_t)ud; const DSLUi *u = dsl_ui_kit(); if (!u || i < 0 || i >= nf) return;
    F[i].on = u->toggle_get(w); dsl_log("rows: filter of %s: toggle %s", F[i].owner, F[i].on ? "on" : "off"); dirty = 1;
}

/* the world's name as its row shows it ("Elven Expedition"): the row's _nameText without rich-text tags and without
   what other modules add after two spaces (the multiplayer tag) */
static void world_name(Obj cs, char *out, size_t cap) {
    out[0] = 0;
    if (!m_tmp_get) { void *k = dsl_find_class("TMPro", "TMP_Text"); m_tmp_get = k ? dsl_find_method(k, "get_text", 0) : NULL; }
    Obj t = AT(cs, o_title_name, Obj); if (!t || !m_tmp_get) return;
    char raw[600]; utf8_of(((Obj (*)(Obj, const void *))dsl_method_address(m_tmp_get))(t, m_tmp_get), raw, sizeof raw);
    size_t k = 0; int in_tag = 0;
    for (const char *p = raw; *p && k + 1 < cap; p++) {
        if (*p == '<') { in_tag = 1; continue; }
        if (in_tag) { if (*p == '>') in_tag = 0; continue; }
        if (p[0] == ' ' && p[1] == ' ') break;
        out[k++] = *p;
    }
    while (k && out[k - 1] == ' ') k--;
    out[k] = 0;
}

/* map world rows to guids through the save dictionary */
static void build_rows(Obj slv) {
    rows_clear();
    Obj l = AT(slv, o_cases, Obj); int n = l ? AT(l, 0x18, int32_t) : 0; Obj a = l ? AT(l, 0x10, Obj) : NULL;
    Obj d = AT(slv, o_dict, Obj), ents = d ? AT(d, 0x18, Obj) : NULL; int dn = d ? AT(d, 0x20, int32_t) : 0;
    for (int i = 0; i < n && a && nw < MAXW; i++) {
        Obj cs = AT(a, 0x20 + 8 * i, Obj); if (!live(cs)) continue;
        Obj slots = AT(cs, o_slots, Obj); int sn = slots ? AT(slots, 0x18, int32_t) : 0; Obj sa = slots ? AT(slots, 0x10, Obj) : NULL;
        WRow *w = &W[nw]; memset(w, 0, sizeof *w);
        for (int e = 0; e < dn && ents && !w->guid[0]; e++) {               /* Entry { uint hash; int next; string key; value } */
            char *en = (char *)ents + 0x20 + 24 * e;
            if (AT(en, 4, int32_t) < -1) continue;                          /* free entry */
            Obj key = AT(en, 8, Obj), val = AT(en, 16, Obj);
            if (!key || !val || obj_class(key) != k_str || obj_class(val) != k_btn) continue;
            Obj vg = comp_go(val, NULL); int mine = 0;
            for (int s = 0; s < sn && sa && !mine; s++) mine = AT(sa, 0x20 + 8 * s, Obj) == vg;
            if (!mine) continue;
            utf8_of(key, w->save, sizeof w->save);
            const char *g = guid_of_save(w->save); if (g) snprintf(w->guid, sizeof w->guid, "%s", g);
        }
        if (!w->guid[0]) continue;                                         /* a world row with no save (new save slot only) */
        w->game = dsl_saves_is_game(w->save);                              /* one of the player's game saves (dsl_saves.c) */
        world_name(cs, w->name, sizeof w->name);
        w->cs = gc_new(cs, 0); nw++;
    }
}

/* the world's mods (owner 2026-10-16 #6, #9): short names on the RIGHT of the world row, as many as fit + "+N";
   the hover card lists every mod, one per line. The multiplayer tag stays on the left, after the world name. */
void dsl_mod_short(const char *id, char *out, size_t cap);
const char *dsl_mod_name(const char *id);
int dsl_reg_scene(void);
/* launcher features (owner 2026-10-20) are not the world's mods: not on the row; the card names the ones that are on in a
   block of their own (owner v13 #5 wanted Multiplayer in the card) */
static void features_card(char *all, size_t acap) {
    const char *f[64]; int nf = dsl_features_on(f, 64); if (!nf) return;
    strlcat(all, "\n\n<color=#A69085>", acap); strlcat(all, TR("Launcher features (every world):"), acap); strlcat(all, "</color>", acap);
    for (int i = 0; i < nf; i++) { const char *nm = dsl_mod_name(f[i]); strlcat(all, "\n", acap); strlcat(all, nm ? nm : f[i], acap); }
}
/* the row's line with at most kmax mod names (then "+N"); cut > 0 = the first name cut to that many columns + "…";
   kmax 0 = "Mods: +N" only. all = the hover card (every mod, one per line). Returns the number of mods. */
static void cut_cols(char *s, size_t cap, int cols) {                             /* whole characters up to cols columns, then an ellipsis */
    size_t b = 0; int c = 0;
    while (s[b]) { unsigned char u = (unsigned char)s[b]; int w = u >= 0xE0 ? 2 : 1; if (c + w > cols) break; c += w; b++; while (s[b] && ((unsigned char)s[b] & 0xC0) == 0x80) b++; }
    if (!s[b]) return;
    while (b && s[b - 1] == ' ') b--;
    if (b + 4 <= cap) snprintf(s + b, cap - b, "\xE2\x80\xA6");
}
static int world_texts(const char *guid, int kmax, int cut, char *line, size_t lcap, char *all, size_t acap) {
    int dsl_world_mods_all(const char *guid, const char **ids, int max);
    const char *ids[64]; int n = dsl_world_mods_all(guid, ids, 64);
    all[0] = 0; line[0] = 0;
    if (!n) { snprintf(line, lcap, "<color=#A69085>%s</color> %s", TR("Mods:"), TR("none")); snprintf(all, acap, "%s", TR("No mods."));
              features_card(all, acap); return 0; }
    char shown[600] = ""; int k = 0;
    for (int i = 0; i < n; i++) {
        const char *nm = dsl_mod_name(ids[i]);
        char one[300]; if (nm) snprintf(one, sizeof one, "%s", nm); else snprintf(one, sizeof one, TR("%s (not installed)"), ids[i]);
        if (i) strlcat(all, "\n", acap);
        strlcat(all, one, acap);
        if (k >= kmax) continue;
        char sh[128]; dsl_mod_short(ids[i], sh, sizeof sh);
        if (!k && cut > 0) cut_cols(sh, sizeof sh, cut);
        if (k) strlcat(shown, " \xC2\xB7 ", sizeof shown);
        strlcat(shown, sh, sizeof shown); k++;
    }
    if (k < n) { char pl[16]; snprintf(pl, sizeof pl, k ? "  +%d" : "+%d", n - k); strlcat(shown, pl, sizeof shown); }
    snprintf(line, lcap, "<color=#A69085>%s</color> %s", TR("Mods:"), shown); dsi_utf8_fix_end(line);
    features_card(all, acap);
    return n;
}
/* owner item 9: a mod whose saves need the loader (mod.json "save_dependent") gets one warning line at the card's end */
static void world_warning(const char *guid, char *all, size_t acap) {
    int dsl_world_mods_all(const char *guid, const char **ids, int max);
    int dsl_save_warning(const char *const *ids, int n, char *out, size_t cap);
    const char *ids[64]; int n = dsl_world_mods_all(guid, ids, 64); char wr[400];
    n += dsl_features_on(ids + n, 64 - n);                                /* a feature's save warning still counts */
    if (dsl_save_warning(ids, n, wr, sizeof wr)) { strlcat(all, "\n\n<color=#EA9F3D>", acap); strlcat(all, wr, acap); strlcat(all, "</color>", acap); }
}
static float row_line_place(Obj cs, DSLObj line, DSLObj chg);
/* one line, never two (owner 2026-10-24: Russian names took 3 lines and ran out of the row): the most mod names that fit
   on one line at the kit's smallest size (0.75 of 14), then the first name cut with "…", then "+N" only; the other
   modules' row texts only when they still fit. The hover card always lists every mod. */
static int line_fits(DSLObj line, float lw) { float x = dsl_ui_text_line_w_min(line); return x > 0 && x <= lw - 2; }
static int line_fit(const char *guid, const char *extra, DSLObj line, float lw, char *all, size_t acap) {   /* 0 = empty */
    const DSLUi *u = dsl_ui_kit(); char txt[1200];
    if (lw < 40) { u->set_text(line, ""); world_texts(guid, 0, 0, txt, sizeof txt, all, acap); return 0; }   /* no room left of [Change] */
    int n = world_texts(guid, 64, 0, txt, sizeof txt, all, acap), tries = 0;
    for (int pass = 0; pass < 2; pass++) {                                /* with the other modules' texts, then without */
        const char *ex = pass ? "" : extra;
        for (int k = n ? n : 1, cut = 0; ; tries++) {
            world_texts(guid, k, cut, txt, sizeof txt, all, acap); strlcat(txt, ex, sizeof txt); dsi_utf8_fix_end(txt);
            u->set_text(line, txt);
            if (line_fits(line, lw)) { if (k < n || cut || pass) dsl_log("rows: Mods line of %s shortened to fit %.0f units: %d of %d name(s)%s%s", guid, lw, k, n, cut ? ", first cut" : "", pass ? ", module texts left out" : ""); return 1; }
            if (!n) break;
            if (k > 1) k--;
            else if (k == 1 && (cut == 0 || cut > 4)) cut = cut ? cut - 2 : 14;
            else if (k == 1) { k = 0; cut = 0; }
            else break;
        }
    }
    dsl_log("rows: Mods line of %s does not fit %.0f units even as \"+N\" (%d tries): no line, the mods are in the [Change] card", guid, lw, tries);
    u->set_text(line, ""); return 0;
}
static void decorate(void) {
    const DSLUi *u = dsl_ui_kit(); if (!u) return;
    int at_title = dsl_reg_scene() != 2;                                  /* in a world mods do not change (owner #10) */
    for (int i = 0; i < nw; i++) {
        WRow *w = &W[i]; Obj cs = gc_get(w->cs); if (!live(cs) || w->hid) continue;
        char extra[800] = "", all[1400], one[400], txt[1200];
        int n0 = world_texts(w->guid, 64, 0, txt, sizeof txt, all, sizeof all); (void)n0;
        for (int r = 0; r < nr; r++) {
            one[0] = 0; R[r].r.text(R[r].r.ud, w->guid, w->save, one, sizeof one);
            if (one[0]) { strlcat(extra, "   ", sizeof extra); strlcat(extra, one, sizeof extra); }
        }
        Obj tr = comp_tr(cs, NULL);
        if (!w->line) w->line = u->text(tr, 0, 0, 10, 10, "", 14, DSL_TXT_RIGHT, 0xD9C7B0FF);
        if (!w->chg) { w->chg = u->button(tr, 0, 0, 10, 10, TR("Change"), change_click, (void *)(intptr_t)i);
                       u->tooltip(w->chg, TR("Change"), TR("Choose the mods this world loads with.")); }
        u->set_active(w->chg, at_title);
        float lw = row_line_place(cs, w->line, at_title ? w->chg : NULL);
        int shown_line = line_fit(w->guid, extra, w->line, lw, all, sizeof all);
        for (int r = 0; r < nr; r++) {                                    /* the card: every module text too */
            one[0] = 0; R[r].r.text(R[r].r.ud, w->guid, w->save, one, sizeof one);
            if (one[0]) { strlcat(all, "\n", sizeof all); strlcat(all, one, sizeof all); }
        }
        world_warning(w->guid, all, sizeof all);
        dsi_utf8_fix_end(all); u->tooltip(w->line, TR("Mods of this world"), all);
        if (at_title) {                                                   /* no line (a long multiplayer tag): [Change]'s card lists them */
            char cb[1800]; snprintf(cb, sizeof cb, "%s", TR("Choose the mods this world loads with."));
            if (!shown_line) { strlcat(cb, "\n\n", sizeof cb); strlcat(cb, all, sizeof cb); dsi_utf8_fix_end(cb); }
            u->tooltip(w->chg, TR("Change"), cb);
        }
    }
}

/* layout: local units of the row's RectTransform (rect from RectTransform.get_rect) */
/* one line on the right of the world row's header: "Mods: a · b +N  [Change]"; the left half keeps the name + tag */
#define LINE_X 0.5f
#define CHG_W 64.f
static float row_w(Obj cs) { Obj t = comp_tr(cs, NULL); return t && rt_rect ? rt_rect(t, NULL).w : 0; }
/* the right end (row-local units) of what the header shows on its left: the world name as drawn, with what the
   multiplayer mod writes into it (its "Multiplayer" tag, "with <names>"), and that mod's "Remove tag" link (a child of
   the row, "<u>...</u>") when it sits on the line's height. The "Mods:" line starts after it (it ran over "with <names>",
   owner 2026-10-24). Screen y is up: the line's band is 1..21 units under the row's top. */
static float header_left_end(Obj cs, DSLObj line, DSLObj chg, float w) {
    Obj tr = comp_tr(cs, NULL); float C[4];
    if (!tr || dsl_ui_screen_rect((DSLObj)tr, C) != 1 || C[2] - C[0] < 1) return 0;
    float s = (C[2] - C[0]) / w, end = 0;
    Obj nt = o_title_name >= 0 ? AT(cs, o_title_name, Obj) : NULL; Obj ntr = live(nt) ? comp_tr(nt, NULL) : NULL; float N[4];
    if (ntr && dsl_ui_screen_rect((DSLObj)ntr, N) == 1) {
        float nw = dsl_ui_width((DSLObj)ntr), pw = dsl_ui_text_line_w((DSLObj)nt), sn = nw > 1 ? (N[2] - N[0]) / nw : s;
        end = (N[0] + pw * sn - C[0]) / s;                                  /* left-aligned; drawn past its rect when longer */
    }
    DSLObj ch[64]; int k = dsl_ui_children((DSLObj)tr, ch, 64);
    for (int i = 0; i < k; i++) {
        if (ch[i] == line || ch[i] == chg || !dsl_ui_shown(ch[i])) continue;
        char t[16]; if (!dsl_ui_text_get(ch[i], t, sizeof t) || strncmp(t, "<u>", 3)) continue;
        float R4[4], ytop = C[3] - 1 * s, ybot = C[3] - 21 * s;              /* only on the line's own height (it may sit lower) */
        if (dsl_ui_screen_rect(ch[i], R4) == 1 && R4[1] < ytop - 2 * s && R4[3] > ybot + 2 * s) { float e = (R4[2] - C[0]) / s; if (e > end) end = e; }
    }
    return end < w ? end : w;
}
/* "Mods: ..." from after the left part (at least the row's middle) to [Change]; returns the line's width */
static float row_line_place(Obj cs, DSLObj line, DSLObj chg) {
    const DSLUi *u = dsl_ui_kit(); float w = row_w(cs); if (!u || w <= 0) return 0;
    float x0 = w * LINE_X, bx = chg ? w - CHG_W - 4 : w - 4, e = header_left_end(cs, line, chg, w);
    if (e + 12 > x0) x0 = e + 12;
    float lw = bx - x0 - 8; if (lw < 0) lw = 0;
    u->place(line, x0, 1, lw > 1 ? lw : 1, 20);
    if (chg) u->place(chg, bx, 2, CHG_W, 18);
    return lw;
}

static DSLObj held(uintptr_t *h); static void hold(uintptr_t *h, DSLObj o);   /* below (GC-held lines) */
/* ------------------------------------------------------------------ the filter panel (owner 2026-10-24)
   "add filters for worlds, on the right of the load campaign window ... vanilla & vanilla+ worlds (with changes not
   modifying gameplay), modded, & multiplayer worlds. Also make a toggle to show multiple saves or only the latest one."
   Each world is of ONE kind: Multiplayer (the multiplayer mod's tag, its export dsl_loadlist_world_mp), else Modded (a mod
   that changes the gameplay, or one not installed: dsl_world_is_modded), else Vanilla & Vanilla+ (no mod, or only fixes
   (mod.json "category": "fix"), mods of scope "client" and mods with "vanilla_plus", 2026-10-25). A world shows when the box of its kind is ticked; the world being played always shows. Defaults = the
   list as it was: Vanilla & Vanilla+ and Modded on, Multiplayer off (the multiplayer mod's own box under the list was off
   at every opening: no saved value to carry over).
   Latest save only: each world shows its first save row (the game lists a world's saves newest first); the world row's
   height counts the save rows it shows (_buttonCount + its RefreshSize), so no empty band is left.
   Shown on the plain Load list (title Load Campaign, Esc Load). A module that filters the open list itself
   (dsl_loadlist_own: multiplayer's Your save / Expedition pick) keeps it away. Choices: <data>/Mods/loadlist.json.
   Data guard: a selected save that a filter or Latest save only hides is unselected at once and Load / Remove File are
   locked (SaveLoadView.UnselectAllSlots + LockLoadButtons): they only ever act on a save the player sees.
   Place: outside the Load window, on its right, level with the list's top, never over the title corner (Auto-update
   mod loader); a screen too narrow for it scales it down (never under half). */
enum { LK_VAN, LK_MOD, LK_MP, LK_N, LK_TAGS };           /* the kinds; then the view options: LK_N Latest save only, LK_TAGS Show tags */
static const char *const lf_key[LK_TAGS + 1] = { "vanilla", "modded", "multiplayer", "latest_only", "show_tags" };
static int lf_on[LK_N] = { 1, 1, 0 }, lf_latest, lf_tags = 1, lf_loaded, lf_panel, lt_applied;   /* Show tags on by default (owner 2026-10-25) */
static int *lf_val(int k) { return k < LK_N ? &lf_on[k] : k == LK_N ? &lf_latest : &lf_tags; }
static int (*lf_is_mp)(const char *guid);                /* NULL: no multiplayer mod (or an older one): no Multiplayer box */
static uintptr_t pn_h, pn_fr_h, pn_tog_h[LK_TAGS + 1];   /* panel, its parent (the Load window), boxes (LK_N, LK_TAGS: the view options) */
static float pn_ht, pn_last[5]; static unsigned pn_gen; static int pn_mp;
int dsl_rows_guard_n;                                     /* test: selections the guard cleared */
/* the multiplayer mod asks (its loader-symbol lookup; Windows: baselib.dll's exports): 1 = this loader has the panel, so the
   mod's own "Show multiplayer worlds" box under the single-player list goes */
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int dsl_loadlist_panel(void) { return 1; }
__attribute__((weak)) int dsl_update_corner_rect(float r[4]) { (void)r; return 0; }   /* builds without dsl_update.c */

static void lf_path(char *p, size_t cap) { snprintf(p, cap, "%s/loadlist.json", dsl_reg_data_dir()); }
static void lf_load(void) {
    if (lf_loaded) return;
    lf_loaded = 1;
    char p[1300]; lf_path(p, sizeof p);
    FILE *f = fopen(p, "rb"); if (!f) return;
    char b[1024]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;
    JNode *j = json_parse(b); if (!j) { dsl_log("rows: filters: %s is not JSON: the defaults", p); return; }
    for (int i = 0; i <= LK_TAGS; i++) {
        JNode *v = json_get(j, lf_key[i]); if (!v || v->t != JN_BOOL) continue;
        *lf_val(i) = v->b != 0;
    }
    json_free(j);
    dsl_log("rows: filters read: vanilla %d, modded %d, multiplayer %d, latest only %d, show tags %d", lf_on[0], lf_on[1], lf_on[2], lf_latest, lf_tags);
}
static void lf_save(void) {
    char p[1300], tmp[1340], b[256]; lf_path(p, sizeof p); snprintf(tmp, sizeof tmp, "%s.tmp", p);
    snprintf(b, sizeof b, "{\"vanilla\": %s, \"modded\": %s, \"multiplayer\": %s, \"latest_only\": %s, \"show_tags\": %s}\n",
             lf_on[0] ? "true" : "false", lf_on[1] ? "true" : "false", lf_on[2] ? "true" : "false", lf_latest ? "true" : "false", lf_tags ? "true" : "false");
    FILE *f = fopen(tmp, "w"); int bad = !f || fputs(b, f) < 0; if (f) bad |= fclose(f) != 0;
    if (bad || rename(tmp, p)) dsl_log("rows: filters not saved (%s)", p);
}
/* 1 = a module filters the open list itself now (multiplayer: Your save, the Expedition pick) */
static int lf_own(void) {
    void *f[8]; int n = dsl_module_syms("dsl_loadlist_own", f, 8);
    for (int i = 0; i < n; i++) if (((int (*)(void))f[i])() > 0) return 1;
    return 0;
}
static void lf_syms(void) {
    void *f[8]; int n = dsl_module_syms("dsl_loadlist_world_mp", f, 8);
    lf_is_mp = n ? (int (*)(const char *))f[0] : NULL;
}
/* owner 2026-10-25: Vanilla & Vanilla+, Modded and Multiplayer all off = every world shows (not an empty list) */
static int lf_all_off(void) { return !lf_on[LK_VAN] && !lf_on[LK_MOD] && (!lf_is_mp || !lf_on[LK_MP]); }
static int world_kind(const WRow *w) {
    if (lf_is_mp && lf_is_mp(w->guid) > 0) return LK_MP;
    return dsl_world_is_modded(w->guid) ? LK_MOD : LK_VAN;
}

/* the save row's height in its world row: _buttonCount = the rows it shows, then the stock RefreshSize */
static int case_fit(Obj cs, int n) {
    if (o_bcount < 0 || !case_refresh || AT(cs, o_bcount, int32_t) == n) return 0;
    AT(cs, o_bcount, int32_t) = n; case_refresh(cs, NULL); return 1;
}
/* Latest save only: save rows after the first off (an open world), the world row's height to match; off again = the
   stock state (every save row of an open world on). The height of EVERY world row, hidden ones too (owner 2026-10-25:
   a row hidden by a filter while Latest save only went off kept the height of one save row; shown again, its second
   save row lay under the next row or under the hidden-worlds line). Returns 1 when a row changed. */
#define MAXFIT 64
static char fitted[MAXFIT][40]; static int nfit;          /* worlds whose height we set to one save row */
static int latest_apply(void) {
    int want = lf_panel && lf_latest, changed = 0;
    if (!want && !lt_applied && !nfit) return 0;
    for (int i = 0; i < nw; i++) {
        Obj cs = gc_get(W[i].cs); if (!live(cs)) continue;
        Obj slots = AT(cs, o_slots, Obj); int sn = slots ? AT(slots, 0x18, int32_t) : 0; Obj sa = slots ? AT(slots, 0x10, Obj) : NULL;
        int f = -1; for (int k = 0; k < nfit; k++) if (!strcmp(fitted[k], W[i].guid)) f = k;
        if (want && sn > 1) { changed |= case_fit(cs, 1); if (f < 0 && nfit < MAXFIT) snprintf(fitted[nfit++], 40, "%s", W[i].guid); }
        else if (f >= 0) { changed |= case_fit(cs, sn); memmove(fitted[f], fitted[f + 1], (size_t)(nfit - f - 1) * sizeof fitted[0]); nfit--; }
        if (W[i].hid) continue;                                            /* a hidden row's save rows stay off (filters_apply) */
        int open = o_cur >= 0 && AT(cs, o_cur + 1, uint8_t);              /* _isNameCaseOpen */
        for (int s = 1; s < sn && sa; s++) {
            Obj sg = AT(sa, 0x20 + 8 * s, Obj); if (!sg || !alive(sg, NULL)) continue;
            int on = open && !want;
            if ((go_self(sg, NULL) != 0) != on) { go_set(sg, on, NULL); changed = 1; }
        }
    }
    lt_applied = want;
    return changed;
}

/* a save row (its GameObject): world row wi, its place si in that world; the dictionary entry of a save name */
static void row_of(Obj vg, int *wi, int *si) {
    *wi = *si = -1;
    for (int i = 0; i < nw && *wi < 0; i++) {
        Obj cs = gc_get(W[i].cs); if (!live(cs)) continue;
        Obj slots = AT(cs, o_slots, Obj); int sn = slots ? AT(slots, 0x18, int32_t) : 0; Obj sa = slots ? AT(slots, 0x10, Obj) : NULL;
        for (int s = 0; s < sn && sa; s++) if (AT(sa, 0x20 + 8 * s, Obj) == vg) { *wi = i; *si = s; break; }
    }
}
/* the save button of a name (want == NULL: the selected one, SubUI_SaveFileButton._isSelected); its name into name */
static Obj save_button(Obj slv, const char *want, char *name, size_t cap) {
    name[0] = 0;
    Obj d = AT(slv, o_dict, Obj), ents = d ? AT(d, 0x18, Obj) : NULL; int dn = d ? AT(d, 0x20, int32_t) : 0;
    for (int e = 0; e < dn && ents; e++) {                                /* Entry { uint hash; int next; string key; value } */
        char *en = (char *)ents + 0x20 + 24 * e;
        if (AT(en, 4, int32_t) < -1) continue;
        Obj key = AT(en, 8, Obj), val = AT(en, 16, Obj);
        if (!key || !val || obj_class(val) != k_btn || !live(val)) continue;
        if (!want && (o_issel < 0 || !AT(val, o_issel, uint8_t))) continue;
        utf8_of(key, name, cap);
        if (want && strcmp(name, want)) continue;
        return val;
    }
    name[0] = 0; return NULL;
}
/* 1 = the open Load list shows this save button's row hidden by the filters (its world, or Latest save only) */
static int row_hidden(Obj btn) {
    int wi, si; row_of(comp_go(btn, NULL), &wi, &si);
    return wi >= 0 && (W[wi].hid || (lf_panel && lf_latest && si > 0));
}
static void sel_clear(Obj slv) { if (unselect_all) unselect_all(slv, NULL); if (lock_buttons) lock_buttons(slv, NULL); dsl_rows_guard_n++; }
/* the data guard, what the player sees: a selected save the filters hide is unselected, Load / Remove File locked */
static void sel_guard(Obj slv, const char *why) {
    char nm[256]; Obj b = save_button(slv, NULL, nm, sizeof nm);
    if (!b || !row_hidden(b)) return;
    sel_clear(slv);
    dsl_log("rows: the selected save %s is hidden by the filters (%s): unselected, Load and Remove File locked", nm, why);
}
/* the data guard, what the game does: Load and Remove File act on SaveLoadPresenter._selectedSaveName (set by a click on a
   save row; the view's unselect does not change it). A click on either while that save is hidden by the filters does
   nothing (no box, no load): nothing can act on a save the player cannot see. */
static int32_t o_selname = -1;                           /* SaveLoadPresenter._selectedSaveName */
static void (*o_click_remove)(Obj self, Obj data, const void *mi), (*o_click_load)(Obj self, Obj data, const void *mi);
static int act_refused(Obj self, const char *what) {
    Obj slv = list_open && h_slv ? gc_get(h_slv) : NULL;
    if (!live(slv) || o_selname < 0 || !self) return 0;
    char want[300], nm[300]; utf8_of(AT(self, o_selname, Obj), want, sizeof want); if (!want[0]) return 0;
    Obj b = save_button(slv, want, nm, sizeof nm);
    if (!b || !row_hidden(b)) return 0;
    sel_clear(slv);
    dsl_log("rows: %s refused: the selected save %s is hidden by the filters", what, want);
    return 1;
}
static void h_click_remove(Obj self, Obj data, const void *mi) { if (!act_refused(self, "Remove File")) o_click_remove(self, data, mi); }
static void h_click_load(Obj self, Obj data, const void *mi) { if (!act_refused(self, "Load")) o_click_load(self, data, mi); }
static void (*o_open_load)(Obj self, Obj data, const void *mi), (*o_refresh)(Obj self, Obj data, const void *mi);
static void h_open_load(Obj self, Obj data, const void *mi), h_refresh(Obj self, Obj data, const void *mi);
static void guard_install(void) {
    void *kp = cls("Refactor.UI", "SaveLoadPresenter"); o_selname = fo(kp, "_selectedSaveName");
    void *a_rm = dsl_find_method_address("Refactor.UI", "SaveLoadPresenter", "ClickRemoveButton", 1);
    void *a_ld = dsl_find_method_address("Refactor.UI", "SaveLoadPresenter", "ClickLoadButton", 1);
    int r1 = o_selname < 0 || !a_rm ? -1 : dsl_hook(DSL_OWNER_LOADER, a_rm, (void *)h_click_remove, (void **)&o_click_remove, "loader rows SaveLoadPresenter.ClickRemoveButton");
    int r2 = o_selname < 0 || !a_ld ? -1 : dsl_hook(DSL_OWNER_LOADER, a_ld, (void *)h_click_load, (void **)&o_click_load, "loader rows SaveLoadPresenter.ClickLoadButton");
    dsl_log("rows: load guard (Remove File / Load on a hidden save): %s, %s", r1 ? "Remove File hook OFF" : "Remove File on", r2 ? "Load hook OFF" : "Load on");
    void *a_op = dsl_find_method_address("Refactor.UI", "SaveLoadPresenter", "OpenLoad", 1);
    void *a_rf = dsl_find_method_address("Refactor.UI", "SaveLoadPresenter", "Refresh", 1);
    int r3 = !a_op ? -1 : dsl_hook(DSL_OWNER_LOADER, a_op, (void *)h_open_load, (void **)&o_open_load, "loader rows SaveLoadPresenter.OpenLoad");
    int r4 = !a_rf ? -1 : dsl_hook(DSL_OWNER_LOADER, a_rf, (void *)h_refresh, (void **)&o_refresh, "loader rows SaveLoadPresenter.Refresh");
    dsl_log("rows: filters before the first frame: OpenLoad %s, Refresh %s", r3 ? "hook OFF (the poll)" : "on", r4 ? "hook OFF (the poll)" : "on");
}

static const char *pn_label(int k) {
    return k == LK_VAN ? TR("Vanilla & Vanilla+") : k == LK_MOD ? TR("Modded") : k == LK_MP ? TR("Multiplayer") : k == LK_N ? TR("Latest save only") : TR("Show tags");
}
static const char *pn_tip(int k) {
    return k == LK_VAN ? TR("Worlds with no mods, or only fixes, mods that do not change the gameplay and small Vanilla+ mods such as Weapon skill slot.")
         : k == LK_MOD ? TR("Worlds with at least one mod that changes the gameplay. Fixes and Vanilla+ mods do not count.")
         : k == LK_MP ? TR("Worlds you played with friends.")
         : k == LK_N ? TR("Each world shows only its newest save. Off: every save.")
         : TR("Tags after the world names: Modded, and Multiplayer for worlds you played with friends or Solo for the others. Off: the names only.");
}
static void rows_rebuild(Obj slv, const char *why);
static void pn_change(void *ud, DSLObj w) {
    int k = (int)(intptr_t)ud; const DSLUi *u = dsl_ui_kit(); if (!u || k < 0 || k > LK_TAGS) return;
    int on = u->toggle_get(w) != 0;
    *lf_val(k) = on;
    lf_save();
    dsl_log("rows: filter panel: %s %s", lf_key[k], on ? "on" : "off");
    Obj slv = h_slv ? gc_get(h_slv) : NULL;
    if (list_open && live(slv)) rows_rebuild(slv, "filter changed"); else dirty = 1;   /* at once: the guard runs before any other click */
}
#define PN_W 320.f
#define PN_PAD 14.f
#define PN_ROW 42.f
#define PN_GAP 16.f
static void pn_drop(void) {
    const DSLUi *u = dsl_ui_kit(); DSLObj p = held(&pn_h);
    if (u && p) u->destroy(p);
    if (pn_h) { gc_free(pn_h); pn_h = 0; }
    for (int k = 0; k <= LK_TAGS; k++) if (pn_tog_h[k]) { gc_free(pn_tog_h[k]); pn_tog_h[k] = 0; }
}
/* made once per Load window (its child: it shows and goes with the window), again after a language change */
static DSLObj pn_build(Obj slv) {
    const DSLUi *u = dsl_ui_kit(); if (!u) return NULL;
    DSLObj pn = held(&pn_h);
    if (pn && (pn_gen != dsi_generation() || pn_mp != (lf_is_mp != NULL))) { pn_drop(); pn = NULL; }
    if (pn) return pn;
    DSLObj fr = dsl_ui_child((DSLObj)comp_tr(slv, NULL), "Panel_SaveLoadUI");
    if (!fr) fr = (DSLObj)comp_tr(slv, NULL);
    pn = u->panel(fr, 0, 0, PN_W, 100, 0);                                /* the game's dark Box panel */
    if (!pn) return NULL;
    hold(&pn_h, pn); hold(&pn_fr_h, fr); pn_gen = dsi_generation(); pn_mp = lf_is_mp != NULL;
    u->text(pn, PN_PAD, 10, PN_W - 2 * PN_PAD, 30, TR("Filters"), 20, DSL_TXT_LEFT | DSL_TXT_TITLE, 0xD9BE8FFF);
    float y = 48;
    for (int k = 0; k <= LK_TAGS; k++) {
        if (k == LK_MP && !lf_is_mp) continue;                          /* no multiplayer mod: its worlds are sorted by their mods */
        if (k == LK_N) {                                                /* the view options apart from the kinds: the title menu's ornament line */
            float dh = 0; DSLObj dv = dsl_ui_menu_divider(pn, PN_PAD, y + 2, PN_W - 2 * PN_PAD, &dh);
            if (!dv && (dv = u->panel(pn, PN_PAD + 10, y + 6, PN_W - 2 * PN_PAD - 20, 1, 0x8C7356FF))) { dsl_ui_flat(dv, 0x8C7356FF); dh = 9; }
            y += (dh > 4 ? dh : 9) + 10;
        }
        DSLObj tg = u->toggle(pn, PN_PAD, y, PN_W - 2 * PN_PAD, pn_label(k), *lf_val(k), pn_change, (void *)(intptr_t)k);
        if (tg) { u->tooltip(tg, pn_label(k), pn_tip(k)); dsl_ui_tip_below(tg); }   /* never over the panel or the Load window */
        hold(&pn_tog_h[k], tg); y += PN_ROW;
    }
    pn_ht = y + PN_PAD - (PN_ROW - 36);
    memset(pn_last, 0, sizeof pn_last);
    dsl_log("rows: filter panel made (%s box: %s)", lf_is_mp ? "with the Multiplayer" : "no Multiplayer", fr == (DSLObj)comp_tr(slv, NULL) ? "on the view" : "Panel_SaveLoadUI");
    return pn;
}
/* x, y, scale from the screen: right of the Load window (Panel_SaveLoadUI), top level with the list's top, out of the title
   corner, inside the screen. Local units of the window (its own scale, UI scale included, is in s). */
float dsl_rows_pn_rect[4], dsl_rows_pn_scale;            /* test: the panel's screen rect and scale at the last placing */
static void pn_place(void) {
    const DSLUi *u = dsl_ui_kit(); DSLObj pn = held(&pn_h), fr = held(&pn_fr_h); if (!u || !pn || !fr) return;
    float P[4], L[4], C[4], sw, sh;
    if (dsl_ui_screen_rect(fr, P) != 1) return;
    float lw = dsl_ui_width(fr), s = lw > 50 ? (P[2] - P[0]) / lw : 0; if (s <= 0) return;
    dsl_ui_screen_size(&sw, &sh);
    DSLObj sv = dsl_ui_child(fr, "ScrollView_SettingData");
    float top = sv && dsl_ui_screen_rect(sv, L) == 1 ? L[3] : P[3] - 110 * s, m = 8 * s;
    float x0 = P[2] + PN_GAP * s, k = (sw - m - x0) / (PN_W * s);
    if (k > 1) k = 1;
    if (k < 0.5f) k = 0.5f;
    float pw = PN_W * s * k, ph = pn_ht * s * k;
    if (dsl_update_corner_rect(C) && x0 < C[2] && x0 + pw > C[0] && top > C[1] - PN_GAP * s && top - ph < C[3]) top = C[1] - PN_GAP * s;
    if (top - ph < m) top = m + ph;
    if (top > sh - m) top = sh - m;
    float v[5] = { lw + PN_GAP, (P[3] - top) / s, PN_W, pn_ht, k }; int same = 1;
    for (int i = 0; i < 5; i++) { float d = v[i] - pn_last[i]; same &= d > -0.5f && d < 0.5f; }
    if (same) return;
    memcpy(pn_last, v, sizeof v);
    u->place(pn, v[0], v[1], v[2], v[3]); dsl_ui_set_scale(pn, k);
    dsl_rows_pn_rect[0] = x0; dsl_rows_pn_rect[1] = top - ph; dsl_rows_pn_rect[2] = x0 + pw; dsl_rows_pn_rect[3] = top; dsl_rows_pn_scale = k;
    dsl_log("rows: filter panel placed: screen %.0fx%.0f, window %.0f..%.0f (%.2f px/unit), panel %.0f,%.0f..%.0f,%.0f scale %.2f",
            sw, sh, P[0], P[2], s, x0, top - ph, x0 + pw, top, k);
}
static void pn_frame(Obj slv) {
    const DSLUi *u = dsl_ui_kit(); if (!u) return;
    if (!lf_panel) { DSLObj p = held(&pn_h); if (p) u->set_active(p, 0); return; }
    DSLObj p = pn_build(slv); if (!p) return;
    u->set_active(p, 1);
    for (int k = 0; k <= LK_TAGS; k++) { DSLObj t = held(&pn_tog_h[k]); if (t && u->toggle_get(t) != *lf_val(k)) u->toggle_set(t, *lf_val(k)); }
    pn_place();
}

/* filters: on list open ask mode + label; apply show() per world (the panel's kinds first, then the toggles under the list) */
/* worlds WE hid (kept across rebuilds): only those are shown again; a row hidden by someone else stays as it is */
static char we_hid[MAXW][40]; static int nwh;
static int hid_find(const char *g) { for (int i = 0; i < nwh; i++) if (!strcmp(we_hid[i], g)) return i; return -1; }
static void filters_apply(Obj slv) {
    if (!nf && !lf_panel && !nwh) return;
    int changed = 0;
    for (int i = 0; i < nw; i++) {
        WRow *w = &W[i]; Obj cs = gc_get(w->cs); if (!live(cs)) continue;
        int show = 1, why = 0;
        w->kind = world_kind(w);
        if (o_cur >= 0 && AT(cs, o_cur, uint8_t)) show = 1;                 /* the world being played always shows */
        else {
            if (lf_panel && !lf_on[w->kind] && !lf_all_off()) { show = 0; why = 1; }
            for (int f = 0; f < nf && show; f++) if (F[f].mode != DSL_LIST_OFF && !F[f].f.show(F[f].f.ud, w->guid, w->save, F[f].on)) { show = 0; why = 2; }
        }
        w->hid = !show; w->why = why;
        Obj g = comp_go(cs, NULL); if (!g || go_self(g, NULL) == show) continue;   /* rows are rebuilt: compare with the game object */
        int k = hid_find(w->guid);
        if (show && k < 0) { w->hid = 1; continue; }                        /* hidden by another filter (multiplayer): not ours */
        if (show) { memmove(we_hid[k], we_hid[k + 1], (size_t)(nwh - k - 1) * sizeof we_hid[0]); nwh--; }
        else if (k < 0 && nwh < MAXW) snprintf(we_hid[nwh++], 40, "%s", w->guid);
        changed = 1;
        set_on(cs, show);
        int open = AT(cs, o_cur + 1, uint8_t);                              /* _isNameCaseOpen (after _isCurrentCampaign) */
        Obj slots = AT(cs, o_slots, Obj); int sn = slots ? AT(slots, 0x18, int32_t) : 0; Obj sa = slots ? AT(slots, 0x10, Obj) : NULL;
        for (int s = 0; s < sn && sa; s++) { Obj sg = AT(sa, 0x20 + 8 * s, Obj); if (sg && alive(sg, NULL)) go_set(sg, show && open, NULL); }
    }
    if (changed) refresh_size(slv, NULL);
}
static void toggles_build(Obj slv) {
    const DSLUi *u = dsl_ui_kit(); if (!u || !nf) return;
    Obj low = o_lowload >= 0 ? AT(slv, o_lowload, Obj) : NULL; if (!live(low)) return;
    /* the strip only while one of our toggles shows, and see-through: a filter without a label (custom-modes' test-world
       filter) left the strip alone on top of the multiplayer toggle, and rgba 0 = the kit's default tint = the game's
       dark Box panel: an empty dark bar where the "Show single-player worlds" box should be (owner 2026-10-20) */
    int want = 0;
    for (int f = 0; f < nf && !want; f++) {
        int m = F[f].f.mode ? F[f].f.mode(F[f].f.ud) : DSL_LIST_LOAD;
        const char *lb = m != DSL_LIST_OFF && F[f].f.label ? F[f].f.label(F[f].f.ud) : NULL;
        want = lb && *lb;
    }
    if (band && !live((Obj)band)) band = NULL;
    if (!want) {
        if (band) u->set_active(band, 0);
        for (int f = 0; f < nf; f++) if (F[f].tog && live((Obj)F[f].tog)) u->set_active(F[f].tog, 0);
        return;
    }
    if (!band) {
        band = u->panel(low, 0, -40, 600, 36, 0xFFFFFF00);                 /* alpha 0 (0 itself = the default tint) */
        if (!band) return;
    } else u->set_active(band, 1);
    int k = 0;
    for (int f = 0; f < nf; f++) {
        F[f].mode = F[f].f.mode ? F[f].f.mode(F[f].f.ud) : DSL_LIST_LOAD;
        const char *lb = F[f].mode != DSL_LIST_OFF && F[f].f.label ? F[f].f.label(F[f].f.ud) : NULL;
        if (F[f].tog && !live((Obj)F[f].tog)) F[f].tog = NULL;
        if (!lb || !*lb) { if (F[f].tog) u->set_active(F[f].tog, 0); continue; }
        F[f].on = 0;                                                        /* off each time the list opens */
        float x0 = dsl_mod_state("multiplayer", NULL) == 1 && (!lf_is_mp || lf_own()) ? 310 : 8;   /* MP draws its own toggle on the left (not when the filter panel shows) */
        if (!F[f].tog) F[f].tog = u->toggle(band, x0 + 250 * k, 4, 230, lb, 0, toggle_change, (void *)(intptr_t)f);
        else { u->set_text(F[f].tog, lb); u->toggle_set(F[f].tog, 0); u->set_active(F[f].tog, 1); }
        k++;
    }
}

/* one list, as the normal game shows it (owner 2026-10-25: the two group headings "Game saves: also in Dungeon Settlers" /
   "Modded saves: only in ..." of 2026-10-17 are gone): the stock SaveLoadView.RefreshLoadPanel order is kept, worlds
   OrderByDescending by their newest save time, a world's saves OrderByDescending by save time. Counted for the log only. */
/* a line we made, through its GC handle: NULL once Unity destroyed it (scene change), the handle freed then */
static DSLObj held(uintptr_t *h) { Obj o = *h ? gc_get(*h) : NULL; if (!live(o)) { if (*h) gc_free(*h); *h = 0; return NULL; } return (DSLObj)o; }
static void hold(uintptr_t *h, DSLObj o) { if (*h) gc_free(*h); *h = o ? gc_new((Obj)o, 0) : 0; }
int dsl_rows_groups[2];                                  /* test: shown world rows of game saves / modded-only saves */
static void groups_count(void) {
    int n[2] = { 0, 0 };
    for (int i = 0; i < nw; i++) { Obj cs = gc_get(W[i].cs); if (live(cs) && !W[i].hid && shown(cs)) n[W[i].game ? 0 : 1]++; }
    dsl_rows_groups[0] = n[0]; dsl_rows_groups[1] = n[1];
}

/* worlds that are not on the list (owner 2026-10-18: the player must see they are hidden, not lost): one line where they
   would be, with how many and why. Top (where the game saves would be): the game's worlds this list cannot show
   (dsl_saves.c: its hooks off this session, or a save the game could not read). Bottom: world rows a filter hides, the
   kind from the module that hides them (dsl_sdk.h dsl_loadlist_hidden: the multiplayer list filter), else the loader's
   own filter toggles; with no such export the line says "a filter". */
static uintptr_t hid_h[2];                               /* 0 top, 1 bottom (GC handles, see held()) */
int dsl_rows_hidden[2];                                  /* test: worlds named by the top / bottom line at the last rebuild */
static void hid_show(int k, Obj content, float wd, const char *txt) {
    const DSLUi *u = dsl_ui_kit();
    DSLObj l = held(&hid_h[k]);
    if (!txt[0]) { if (l) u->set_active(l, 0); return; }
    if (!l) hold(&hid_h[k], l = u->text(content, 0, 0, wd - 24, 30, txt, 16, DSL_TXT_LEFT | DSL_TXT_WRAP, 0xC9B79CFF));
    else u->set_text(l, txt);
    if (!l) return;
    float h = u->size >= offsetof(DSLUi, text_height) + sizeof u->text_height && u->text_height ? u->text_height(l, wd - 24) : 0;
    u->place(l, 0, 0, wd - 24, (h > 22 ? h : 22) + 12);
    u->set_active(l, 1);
}
static void hidden_apply(Obj slv) {
    const DSLUi *u = dsl_ui_kit();
    Obj content = o_parent >= 0 ? AT(slv, o_parent, Obj) : NULL;
    if (!u || !set_sib || !get_sib || !set_last || !live(content)) return;
    float wd = rt_rect ? rt_rect(content, NULL).w : 600; if (wd < 100) wd = 600;
    char top[1400] = "", end[900] = "", one[600];
    int off, unread; dsl_saves_hidden(&off, &unread);
    if (off > 0) snprintf(top, sizeof top, TRN("%d of your game worlds is not listed: the mod loader could not connect your game saves "
                                               "this session. It is safe in Dungeon Settlers.",
                                               "%d of your game worlds are not listed: the mod loader could not connect your game saves "
                                               "this session. They are safe in Dungeon Settlers.", off), off);
    if (unread > 0) {
        snprintf(one, sizeof one, TRN("%d game save could not be read by the game, so it is not listed. The file stays in your save folder.",
                                      "%d game saves could not be read by the game, so they are not listed. The files stay in your save folder.", unread), unread);
        if (top[0]) strlcat(top, "\n", sizeof top);
        strlcat(top, one, sizeof top);
    }
    int ours = 0, panel = 0, others = 0;                                    /* inactive world rows: our toggles' / the panel's / someone else's */
    for (int i = 0; i < nw; i++) {
        Obj cs = gc_get(W[i].cs); Obj g = live(cs) ? comp_go(cs, NULL) : NULL;
        if (!g || go_self(g, NULL)) continue;
        if (hid_find(W[i].guid) < 0) others++; else if (W[i].why == 1) panel++; else ours++;
    }
    int kind = 0;
    if (others) {
        void *f[8]; int nf_ = dsl_module_syms("dsl_loadlist_hidden", f, 8);
        for (int i = 0; i < nf_ && !kind; i++) { int k = 0; if (((int (*)(int *))f[i])(&k) > 0 && (k == 1 || k == 2)) kind = k; }
    }
    if (others && kind == 1) snprintf(end, sizeof end, TRN("%d multiplayer world is hidden. Tick the box under the list to show it.",
                                                           "%d multiplayer worlds are hidden. Tick the box under the list to show them.", others), others);
    else if (others && kind == 2) snprintf(end, sizeof end, TRN("%d single-player world is hidden. Tick the box under the list to show it.",
                                                                "%d single-player worlds are hidden. Tick the box under the list to show them.", others), others);
    if (panel) {                                                            /* the filter panel (owner 2026-10-24) */
        snprintf(one, sizeof one, TRN("%d world is hidden by the filters on the right.", "%d worlds are hidden by the filters on the right.", panel), panel);
        if (end[0]) strlcat(end, "\n", sizeof end);
        strlcat(end, one, sizeof end);
    }
    int gen = ours + (kind ? 0 : others);
    if (gen) {
        snprintf(one, sizeof one, TRN("%d world is hidden by a filter under the list.", "%d worlds are hidden by a filter under the list.", gen), gen);
        if (end[0]) strlcat(end, "\n", sizeof end);
        strlcat(end, one, sizeof end);
    }
    dsi_utf8_fix_end(top); dsi_utf8_fix_end(end);
    hid_show(0, content, wd, top); hid_show(1, content, wd, end);
    DSLObj hid[2] = { held(&hid_h[0]), held(&hid_h[1]) };
    if (top[0] && hid[0]) {                                                 /* before the first row */
        int base = get_sib((Obj)hid[0], NULL);                             /* its own place too: moving down shifts the rest up */
        for (int i = 0; i < nw; i++) { Obj cs = gc_get(W[i].cs); if (live(cs)) { int s = get_sib(comp_tr(cs, NULL), NULL); if (s < base) base = s; } }
        set_sib((Obj)hid[0], base == 1 << 30 ? 0 : base, NULL);
    }
    if (end[0] && hid[1]) set_last((Obj)hid[1], NULL);
    refresh_size(slv, NULL);
    int h0 = off + unread, h1 = others + ours + panel;
    if (h0 != dsl_rows_hidden[0] || h1 != dsl_rows_hidden[1])
        dsl_log("rows: hidden-worlds lines: top %d (game worlds not listed %d, unreadable %d), bottom %d (kind %d, %d by other filters, %d by ours, %d by the panel)",
                h0, off, unread, h1, kind, others, ours, panel);
    dsl_rows_hidden[0] = top[0] ? h0 : 0; dsl_rows_hidden[1] = end[0] ? h1 : 0;
}
static void hidden_off(void) {
    const DSLUi *u = dsl_ui_kit();
    for (int k = 0; k < 2; k++) { DSLObj l = u ? held(&hid_h[k]) : NULL; if (l) u->set_active(l, 0); }
}

/* test only (dsl_reg.c test requests): the "Mods: ..." line of world row i */
DSLObj dsl_rows_test_line(int i) { return i >= 0 && i < nw ? W[i].line : NULL; }

/* test only: the filter panel's box k (0 Vanilla & Vanilla+, 1 Modded, 2 Multiplayer, 3 Latest save only, 4 Show tags), for a hover */
DSLObj dsl_rows_test_pn(int k) { return k >= 0 && k <= LK_TAGS && pn_tog_h[k] ? held(&pn_tog_h[k]) : NULL; }

/* test only: the "Change" of world row i, as a click */
void dsl_rows_test_change(int i) { change_click((void *)(intptr_t)i, NULL); }

/* test only (dsl_reg.c reg_request.json, DSL_SOFT_TEST): {"cmd":"lf", "vanilla"|"modded"|"multiplayer"|"latest_only": 0/1}
   = the panel's boxes clicked; {"cmd":"lf_select","name":..} = a click on that save row (only when it is not selected: a
   second click loads); {"cmd":"lf_remove"} = Remove File pressed (the game's own box follows); {"cmd":"lf_state"}.
   Each writes <log dir>/softrestart/lf_state.json: the panel, the window, the corner, the screen, every world row (kind,
   shown, save rows shown), the selected save, Load / Remove File clickable, the guard's count. */
static void lf_state_write(Obj slv) {
    static void *m_inter; static int32_t o_load = -1, o_rm = -1;
    if (!m_inter) { void *k = dsl_find_class("UnityEngine.UI", "Selectable"); void *m = k ? dsl_find_method(k, "IsInteractable", 0) : NULL; m_inter = m ? dsl_method_address(m) : NULL;
                    o_load = fo(k_slv, "_loadButton"); o_rm = fo(k_slv, "_removeButton"); }
    char p[1300]; snprintf(p, sizeof p, "%s/softrestart/lf_state.json", dsl_log_dir());
    char tmp[1340]; snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "w"); if (!f) return;
    float pr[4] = { 0 }, fr[4] = { 0 }, cr[4] = { 0 }, sw = 0, sh = 0; int pshown = 0, fshown = 0, cshown = 0;
    DSLObj pn = pn_h ? held(&pn_h) : NULL, frm = pn_fr_h ? held(&pn_fr_h) : NULL;
    if (pn) pshown = dsl_ui_screen_rect(pn, pr) == 1;
    if (frm) fshown = dsl_ui_screen_rect(frm, fr) == 1;
    cshown = dsl_update_corner_rect(cr); dsl_ui_screen_size(&sw, &sh);
    char nm[256] = ""; int wi = -1, si = -1; Obj sb = live(slv) ? save_button(slv, NULL, nm, sizeof nm) : NULL; if (sb) row_of(comp_go(sb, NULL), &wi, &si);
    char psel[300] = ""; (void)psel;
    uint8_t (*inter)(Obj, const void *) = m_inter;
    Obj lb = live(slv) && o_load >= 0 ? AT(slv, o_load, Obj) : NULL, rb = live(slv) && o_rm >= 0 ? AT(slv, o_rm, Obj) : NULL;
    int lok = inter && live(lb) ? inter(lb, NULL) != 0 : -1, rok = inter && live(rb) ? inter(rb, NULL) != 0 : -1;
    fprintf(f, "{\"open\": %d, \"panel_on\": %d, \"panel_shown\": %d, \"panel\": [%.1f, %.1f, %.1f, %.1f], \"scale\": %.3f, \"window_shown\": %d, "
               "\"window\": [%.1f, %.1f, %.1f, %.1f], \"corner_shown\": %d, \"corner\": [%.1f, %.1f, %.1f, %.1f], \"screen\": [%.0f, %.0f], ",
            list_open, lf_panel, pshown, pr[0], pr[1], pr[2], pr[3], dsl_rows_pn_scale, fshown, fr[0], fr[1], fr[2], fr[3], cshown, cr[0], cr[1], cr[2], cr[3], sw, sh);
    fprintf(f, "\"boxes\": {\"vanilla\": %d, \"modded\": %d, \"multiplayer\": %d, \"latest_only\": %d, \"show_tags\": %d, \"mp_box\": %d}, ",
            lf_on[0], lf_on[1], lf_on[2], lf_latest, lf_tags, pn_tog_h[LK_MP] && held(&pn_tog_h[LK_MP]) ? 1 : 0);
    char esc[600]; jesc_(esc, sizeof esc, nm);
    fprintf(f, "\"selected\": \"%s\", \"selected_world\": %d, \"selected_row\": %d, \"load_clickable\": %d, \"remove_clickable\": %d, \"guard\": %d, \"hidden_line\": %d, \"worlds\": [",
            esc, wi, si, lok, rok, dsl_rows_guard_n, dsl_rows_hidden[1]);
    for (int i = 0; i < nw; i++) {
        Obj cs = gc_get(W[i].cs); int on = live(cs) && shown(cs), sh_ = 0, sn = 0;
        Obj slots = live(cs) ? AT(cs, o_slots, Obj) : NULL; Obj sa = slots ? AT(slots, 0x10, Obj) : NULL; sn = slots ? AT(slots, 0x18, int32_t) : 0;
        char names[1200] = "", list[2400] = "";
        Obj d = live(slv) ? AT(slv, o_dict, Obj) : NULL, ents = d ? AT(d, 0x18, Obj) : NULL; int dn = d ? AT(d, 0x20, int32_t) : 0;
        char srs[1200] = "";                                               /* the shown save rows' screen rects */
        for (int k = 0; k < sn && sa; k++) {
            Obj sg = AT(sa, 0x20 + 8 * k, Obj); if (!sg || !alive(sg, NULL) || !go_tree(sg, NULL)) continue;
            sh_++;
            { float q[4]; DSLObj gt = go_trf ? (DSLObj)go_trf(sg, NULL) : NULL;   /* sg is the save row's GameObject */
              if (gt && dsl_ui_screen_rect(gt, q) == 1) snprintf(srs + strlen(srs), sizeof srs - strlen(srs), "%s[%.1f, %.1f, %.1f, %.1f]", srs[0] ? ", " : "", q[0], q[1], q[2], q[3]); }
            for (int e = 0; e < dn && ents; e++) {                         /* its save name (the dictionary's key) */
                char *en = (char *)ents + 0x20 + 24 * e; if (AT(en, 4, int32_t) < -1) continue;
                Obj key = AT(en, 8, Obj), val = AT(en, 16, Obj);
                if (!key || !val || obj_class(val) != k_btn || comp_go(val, NULL) != sg) continue;
                char u8[300], es[600]; utf8_of(key, u8, sizeof u8); jesc_(es, sizeof es, u8);
                strlcat(list, list[0] ? ", \"" : "\"", sizeof list); strlcat(list, es, sizeof list); strlcat(list, "\"", sizeof list); break;
            }
        }
        jesc_(names, sizeof names, W[i].name); char g[60]; jesc_(g, sizeof g, W[i].guid);
        /* the "Mods:" line: its rect, where the header's left part ends, the size TMP drew its text at (screen px) */
        float LR[4] = { 0 }, C[4] = { 0 }, le = 0, rw = 0, rh = 0, lw_ = 0; int lon = 0;
        if (on && W[i].line && dsl_ui_screen_rect(W[i].line, LR) == 1) {
            lon = 1; float w = row_w(cs); Obj ctr = comp_tr(cs, NULL);
            if (w > 0 && dsl_ui_screen_rect((DSLObj)ctr, C) == 1) le = C[0] + header_left_end(cs, W[i].line, W[i].chg, w) * (C[2] - C[0]) / w;
            lw_ = dsl_ui_width(W[i].line); float sx = lw_ > 0 ? (LR[2] - LR[0]) / lw_ : 1;
            if (dsl_ui_text_rendered(W[i].line, &rw, &rh)) { rw *= sx; rh *= sx; }
        }
        char lt[700] = "", lte[1400] = ""; if (lon) { dsl_ui_text_get(W[i].line, lt, sizeof lt); jesc_(lte, sizeof lte, lt); }
        float RR[4] = { 0 }; if (on) dsl_ui_screen_rect((DSLObj)comp_tr(cs, NULL), RR);
        char ntx[700] = "", nte[1400] = ""; Obj nt = on && o_title_name >= 0 ? AT(cs, o_title_name, Obj) : NULL;
        if (live(nt)) { dsl_ui_text_get((DSLObj)nt, ntx, sizeof ntx); jesc_(nte, sizeof nte, ntx); }
        int rm_on = 0; float RM[4] = { 0 }, CH[4] = { 0 }, px = 0;          /* the multiplayer mod's "Remove tag", [Change], px per row unit */
        { DSLObj ch[64]; int k = on ? dsl_ui_children((DSLObj)comp_tr(cs, NULL), ch, 64) : 0; char t4[16];
          for (int q = 0; q < k; q++) if (dsl_ui_shown(ch[q]) && dsl_ui_text_get(ch[q], t4, sizeof t4) && !strncmp(t4, "<u>", 3)) { rm_on = 1; dsl_ui_screen_rect(ch[q], RM); } }
        if (on && W[i].chg && dsl_ui_shown(W[i].chg)) dsl_ui_screen_rect(W[i].chg, CH);
        { float w_ = on ? row_w(cs) : 0; if (w_ > 0) px = (RR[2] - RR[0]) / w_; }
        fprintf(f, "%s{\"guid\": \"%s\", \"name\": \"%s\", \"kind\": %d, \"shown\": %d, \"saves_shown\": %d, \"saves\": %d, \"button_count\": %d, \"shown_names\": [%s], "
                   "\"line_shown\": %d, \"line_rect\": [%.1f, %.1f, %.1f, %.1f], \"left_end\": %.1f, \"rendered\": [%.1f, %.1f], \"line_text\": \"%s\", "
                   "\"row_rect\": [%.1f, %.1f, %.1f, %.1f], \"save_rects\": [%s], \"name_text\": \"%s\", \"remove_tag\": %d, "
                   "\"rm_rect\": [%.1f, %.1f, %.1f, %.1f], \"chg_rect\": [%.1f, %.1f, %.1f, %.1f], \"px\": %.4f}",
                i ? ", " : "", g, names, W[i].kind, on, sh_, sn, live(cs) && o_bcount >= 0 ? AT(cs, o_bcount, int32_t) : -1, list,
                lon, LR[0], LR[1], LR[2], LR[3], le, rw, rh, lte, RR[0], RR[1], RR[2], RR[3], srs, nte, rm_on,
                RM[0], RM[1], RM[2], RM[3], CH[0], CH[1], CH[2], CH[3], px);
    }
    fprintf(f, "], \"hid_lines\": [");
    for (int k = 0; k < 2; k++) {                                         /* top / bottom hidden-worlds line */
        DSLObj l = hid_h[k] ? held(&hid_h[k]) : NULL; float q[4] = { 0 }; int lsh = l && dsl_ui_screen_rect(l, q) == 1;
        char tx[900] = "", te[1800] = ""; if (lsh) { dsl_ui_text_get(l, tx, sizeof tx); jesc_(te, sizeof te, tx); }
        fprintf(f, "%s{\"shown\": %d, \"rect\": [%.1f, %.1f, %.1f, %.1f], \"text\": \"%s\"}", k ? ", " : "", lsh, q[0], q[1], q[2], q[3], te);
    }
    fprintf(f, "]}\n"); fclose(f); rename(tmp, p);
}
void dsl_rows_test_cmd(const char *c, const JNode *j) {
    Obj slv = h_slv ? gc_get(h_slv) : NULL; const DSLUi *u = dsl_ui_kit();
    static void *a_slot, *a_cur, *a_rm;
    if (!a_slot) { a_slot = ma(k_slv, "OnClickLoadSlot", 1); a_cur = ma(k_slv, "get_CurrentSelectedSaveName", 0); a_rm = ma(k_slv, "OnClickRemove", 0); }
    if (!strcmp(c, "lf")) {
        for (int k = 0; k <= LK_TAGS; k++) {
            JNode *v = json_get(j, lf_key[k]); if (!v || (v->t != JN_BOOL && v->t != JN_NUM)) continue;
            int on = v->t == JN_BOOL ? v->b != 0 : v->num != 0;
            DSLObj t = pn_tog_h[k] ? held(&pn_tog_h[k]) : NULL;
            if (t && u) { u->toggle_set(t, on); pn_change((void *)(intptr_t)k, t); }
            else dsl_log("rows test: %s: no box on the panel", lf_key[k]);
        }
    } else if (!strcmp(c, "lf_select") && live(slv) && a_slot && a_cur && json_str(j, "name")) {
        char cur[300] = ""; utf8_of(((Obj (*)(Obj, const void *))a_cur)(slv, NULL), cur, sizeof cur);
        if (strcmp(cur, json_str(j, "name"))) ((void (*)(Obj, Obj, const void *))a_slot)(slv, str_new(json_str(j, "name")), NULL);
        dsl_log("rows test: save row %s clicked (was selected: %s)", json_str(j, "name"), cur[0] ? cur : "none");
    } else if (!strcmp(c, "lf_remove") && live(slv) && a_rm && a_cur) {
        char cur[300] = ""; utf8_of(((Obj (*)(Obj, const void *))a_cur)(slv, NULL), cur, sizeof cur);
        ((void (*)(Obj, const void *))a_rm)(slv, NULL);
        dsl_log("rows test: Remove File pressed (selected: %s)", cur[0] ? cur : "none");
    }
    lf_state_write(slv);
}

/* ------------------------------------------------------------------ tags (owner 2026-10-25, "Show tags" on the panel)
   On: the world row's name, then "Modded" (dsl_world_is_modded: the Modded box's rule, Vanilla+ mods do not count) and the
   multiplayer mod's tag ("Multiplayer  with <names>": that mod writes it into the same name text at the row's Init, with
   its "Remove tag" link, a child of the row). Off: the name alone and no link. The multiplayer part is kept per world while
   it is off (the mod writes it only at the row's Init); a world the multiplayer mod no longer tags (Remove tag) loses it. */
#define TAG_MARK "  <size=75%><color=#E3A65B>"              /* our "Modded" tag (its colour marks it in any language) */
#define SOLO_MARK "  <size=75%><color=#8FC4FF><link=\"dsl.solo\">"   /* our "Solo" tag: the multiplayer tag's look, marked by its link id */
#define SOLO_END "</link></color></size>"
static struct { char guid[40], mp[600]; } TG[MAXW]; static int ntg;
static Obj tag_link(Obj cs) {                             /* the multiplayer mod's "Remove tag" (<u>...</u>), a child of the row */
    DSLObj ch[64]; int k = dsl_ui_children((DSLObj)comp_tr(cs, NULL), ch, 64); char t4[16];
    for (int q = 0; q < k; q++) if (dsl_ui_text_get(ch[q], t4, sizeof t4) && !strncmp(t4, "<u>", 3)) return (Obj)ch[q];
    return NULL;
}
/* "Remove tag" on the title line (owner, next version after v0.1.27: it sat under [Change], over the world's detail box):
   right after the multiplayer tag, its centre on the line's band (1..21 units under the row's top, as the "Mods:" line);
   no room before the "Mods:" line's minimum and [Change] = the tag's "with <names>" part cut ("..."), then left out. */
#define RM_GAP 10.f
#define MODS_MIN 64.f                                       /* room the "Mods:" line keeps at least ("Mods: +N") */
static int cols_of(const char *s) { int c = 0; for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) c += (unsigned char)*s >= 0xE0 ? 2 : 1; return c; }
static float row_line_left_max(Obj cs) { float w = row_w(cs); return (dsl_reg_scene() != 2 ? w - CHG_W - 4 : w - 4) - 8 - MODS_MIN; }
/* the name text set to name + modtag + the multiplayer part, its "with" part shortened until the text and the link end by
   lim (row units); returns the text's right end in row units (-1: not measured) */
static float tag_fit(Obj cs, Obj nt, const char *head, const char *mp, float lnk, float lim, char *out, size_t cap) {
    const DSLUi *u = dsl_ui_kit(); float C[4], N[4], w = row_w(cs), nwd = dsl_ui_width((DSLObj)nt);
    if (w <= 0 || nwd <= 1 || dsl_ui_screen_rect((DSLObj)comp_tr(cs, NULL), C) != 1 || dsl_ui_screen_rect((DSLObj)comp_tr(nt, NULL), N) != 1) { snprintf(out, cap, "%s%s", head, mp); return -1; }
    float s = (C[2] - C[0]) / w, sn = (N[2] - N[0]) / nwd, nl = (N[0] - C[0]) / s, right = -1;
    const char *wc = strstr(mp, "<color=#A69085>"), *we = wc ? strstr(wc, "</color>") : NULL;   /* the "  with <names>" part */
    char inner[600] = ""; if (wc && we) snprintf(inner, sizeof inner, "%.*s", (int)(we - wc - 15), wc + 15);
    const char *in = inner; while (*in == ' ') in++;
    int full = cols_of(in);
    for (int cols = full + 1; ; cols = cols > 10 ? cols - 3 : cols - 1) {   /* the whole text, then shorter, then none */
        char v[700], cut[600];
        if (cols > full) snprintf(v, sizeof v, "%s", mp);
        else if (cols >= 4) { snprintf(cut, sizeof cut, "%s", in); cut_cols(cut, sizeof cut, cols); snprintf(v, sizeof v, "%.*s  %s%s", (int)(wc - mp + 15), mp, cut, we); }
        else if (wc && we) snprintf(v, sizeof v, "%.*s%s", (int)(wc - mp), mp, we + 8);
        else snprintf(v, sizeof v, "%s", mp);
        snprintf(out, cap, "%s%s", head, v); dsi_utf8_fix_end(out);
        u->set_text((DSLObj)nt, out);
        right = nl + dsl_ui_text_line_w((DSLObj)nt) * sn / s;
        if (right + RM_GAP + lnk <= lim || cols < 4 || !wc || !we) break;
    }
    return right;
}
static void tags_apply(void) {
    const DSLUi *u = dsl_ui_kit(); if (!u || o_title_name < 0) return;
    char mt[200], so[200]; snprintf(mt, sizeof mt, "%s%s</color></size>", TAG_MARK, TR("Modded"));
    snprintf(so, sizeof so, "%s%s%s", SOLO_MARK, TR("Solo"), SOLO_END);
    int nmod = 0, nmp = 0, nso = 0, show = lf_tags || !lf_panel;         /* no panel (a module's own list): the tags as before */
    for (int i = 0; i < nw; i++) {
        WRow *w = &W[i]; Obj cs = gc_get(w->cs), nt = live(cs) ? AT(cs, o_title_name, Obj) : NULL;
        char cur[1200], rest[1200], name[1200], mp[600] = "", want[1800];
        if (!live(nt) || !dsl_ui_text_get((DSLObj)nt, cur, sizeof cur)) continue;
        snprintf(rest, sizeof rest, "%s", cur);
        char *m = strstr(rest, TAG_MARK), *me = m ? strstr(m, "</color></size>") : NULL;
        if (me) memmove(m, me + 15, strlen(me + 15) + 1);              /* our tags out: name + the multiplayer part */
        m = strstr(rest, SOLO_MARK); me = m ? strstr(m, SOLO_END) : NULL;
        if (me) memmove(m, me + strlen(SOLO_END), strlen(me + strlen(SOLO_END)) + 1);
        char *sp = strstr(rest, "  ");
        snprintf(name, sizeof name, "%.*s", sp ? (int)(sp - rest) : (int)strlen(rest), rest);
        if (sp) snprintf(mp, sizeof mp, "%s", sp);
        int k = -1; for (int q = 0; q < ntg; q++) if (!strcmp(TG[q].guid, w->guid)) k = q;
        int is_mp = lf_is_mp ? lf_is_mp(w->guid) > 0 : -1;
        if (mp[0] && is_mp != 0) { if (k < 0 && ntg < MAXW) k = ntg++; if (k >= 0) { snprintf(TG[k].guid, 40, "%s", w->guid); snprintf(TG[k].mp, sizeof TG[k].mp, "%s", mp); } }
        else if (!mp[0] && is_mp > 0 && k >= 0) snprintf(mp, sizeof mp, "%s", TG[k].mp);
        else if (is_mp == 0) { mp[0] = 0; if (k >= 0) TG[k] = TG[--ntg]; }
        int mod = dsl_world_is_modded(w->guid), solo = show && lf_is_mp && !mp[0];   /* owner 2026-10-25: Solo = no multiplayer tag */
        snprintf(want, sizeof want, "%s%s%s", name, show && mod ? mt : "", solo ? so : show ? mp : "");
        nmod += show && mod; nmp += show && mp[0]; nso += solo;
        Obj rm = tag_link(cs); int on = rm && show && mp[0];
        if (rm) set_on(rm, on);
        float R[4], C[4], wr = row_w(cs);
        if (!on || wr <= 0 || dsl_ui_screen_rect((DSLObj)rm, R) != 1 || dsl_ui_screen_rect((DSLObj)comp_tr(cs, NULL), C) != 1) {
            if (strcmp(want, cur)) u->set_text((DSLObj)nt, want);
            continue;
        }
        char head[1300], fit[1800]; snprintf(head, sizeof head, "%s%s", name, mod ? mt : "");
        float s = (C[2] - C[0]) / wr, lnk = (R[2] - R[0]) / s;
        float right = tag_fit(cs, nt, head, mp, lnk, row_line_left_max(cs), fit, sizeof fit);
        if (strcmp(fit, want)) dsl_log("rows: tag of %s shortened to fit the title line before \"Mods:\" and [Change]", w->guid);
        if (right < 0) continue;
        float x = C[0] + (right + RM_GAP) * s, yc = C[3] - 11 * s;    /* on the title line, right after the tag */
        float dx = x - R[0], dy = yc - (R[1] + R[3]) / 2;
        if (dx > 0.5f || dx < -0.5f || dy > 0.5f || dy < -0.5f) dsl_ui_move_screen((DSLObj)rm, dx, dy);
    }
    static int last = -1; int sig = show * 1000000 + nmod * 10000 + nmp * 100 + nso;
    if (sig != last) { last = sig; dsl_log("rows: tags %s: %d Modded, %d Multiplayer, %d Solo", show ? "on" : "off", nmod, nmp, nso); }
}
static void rows_rebuild(Obj slv, const char *why) {
    sig_last = list_sig(slv); dirty = 0;
    build_rows(slv);
    filters_apply(slv);
    if (latest_apply()) refresh_size(slv, NULL);
    sel_guard(slv, why);
    tags_apply();
    decorate();
    groups_count();
    hidden_apply(slv);
    sig_last = list_sig(slv);
    int hidden = 0, kinds[LK_N] = { 0 }; for (int i = 0; i < nw; i++) { hidden += W[i].hid; kinds[W[i].kind]++; }
    dsl_log("rows: Load list rebuilt (%s): %d world row(s) (%d hidden by filters), %d text source(s), %d filter(s); groups: %d game, %d modded; "
            "hidden lines: %d top, %d bottom; panel %s (kinds %d vanilla, %d modded, %d multiplayer; latest only %d)",
            why, nw, hidden, nr, nf, dsl_rows_groups[0], dsl_rows_groups[1], dsl_rows_hidden[0], dsl_rows_hidden[1],
            lf_panel ? "on" : "off", kinds[LK_VAN], kinds[LK_MOD], kinds[LK_MP], lf_panel && lf_latest);
}
/* the open Load list: opened -> panel + rows; changed -> rows again; closed -> ours off */
static void list_poll(Obj slv) {
    int open = live(slv) && shown(slv) && o_lowload >= 0 && shown(AT(slv, o_lowload, Obj));   /* the Load panel (not Save) */
    if (!open) {
        if (list_open) { list_open = 0; rows_clear(); hidden_off(); lt_applied = 0; }
        const DSLUi *u = dsl_ui_kit(); DSLObj p = pn_h ? held(&pn_h) : NULL; if (u && p) u->set_active(p, 0);   /* the Save panel: never */
        return;
    }
    uint32_t s = list_sig(slv);
    if (!list_open) { list_open = 1; lf_load(); lf_syms(); toggles_build(slv); sig_last = 0; }
    int pnl = !lf_own(); if (pnl != lf_panel) { lf_panel = pnl; dirty = 1; }
    pn_frame(slv);
    if (s != sig_last || dirty) rows_rebuild(slv, sig_last ? "list changed" : "list opened");
    else sel_guard(slv, "poll");
}
static Obj slv_find(void) {                               /* the shown SaveLoadView first (the title's and the multiplayer page's) */
    Obj slv = h_slv ? gc_get(h_slv) : NULL;
    if (live(slv) && shown(slv)) return slv;
    Obj a = find_obj(t_slv, 0, NULL); if (!a) a = find_obj(t_slv, 1, NULL);
    if (a && a != slv) { if (h_slv) gc_free(h_slv); h_slv = gc_new(a, 0); }
    return a ? a : (live(slv) ? slv : NULL);
}
/* owner 2026-10-25: on the first open after a start the whole list showed for a moment, then the filters hid worlds (the
   new SaveLoadView was found by the 30-frame search, then the 6-frame poll). Now the panel and the rows are made right after
   the game filled the list (SaveLoadPresenter.OpenLoad / Refresh, called through its update table: never inlined), and
   every frame right before the UI is drawn (dsl_rows_late): the first frame the player sees is filtered. */
static void h_open_load(Obj self, Obj data, const void *mi) { o_open_load(self, data, mi); Obj s = slv_find(); if (s) list_poll(s); }
static void h_refresh(Obj self, Obj data, const void *mi) { o_refresh(self, data, mi); Obj s = slv_find(); if (s) list_poll(s); }
static void open_monitor(void);
void dsl_rows_late(void) {
    if (!ok) return;
    Obj slv = h_slv ? gc_get(h_slv) : NULL;
    if (live(slv)) {
        int open = shown(slv) && o_lowload >= 0 && shown(AT(slv, o_lowload, Obj));
        if (open != list_open || (open && (dirty || list_sig(slv) != sig_last))) list_poll(slv);
    }
    open_monitor();
}
/* test only (DSL_SOFT_TEST=1 and DSL_ROWS_OPEN_TEST=1, never in a player's start): the 30 frames after each opening of the
   Load list, right before they are drawn: how many world rows show that the filters hide ("rows test: open frame") */
static void open_monitor(void) {
    static int on = -1; static uintptr_t hv; static int was, k;
    if (on < 0) on = getenv("DSL_SOFT_TEST") && *getenv("DSL_SOFT_TEST") == '1' && getenv("DSL_ROWS_OPEN_TEST") && *getenv("DSL_ROWS_OPEN_TEST") == '1';
    if (!on) return;
    Obj slv = hv ? gc_get(hv) : NULL;
    if (!live(slv) || !shown(slv)) { Obj a = find_obj(t_slv, 0, NULL); if (a) { if (hv) gc_free(hv); hv = gc_new(a, 0); slv = a; } }
    int open = live(slv) && shown(slv) && o_lowload >= 0 && shown(AT(slv, o_lowload, Obj));
    if (open && !was) k = 0;
    was = open;
    if (!open || k >= 30) return;
    lf_load(); int panel = !lf_own();
    Obj l = AT(slv, o_cases, Obj); int n = l ? AT(l, 0x18, int32_t) : 0; Obj a = l ? AT(l, 0x10, Obj) : NULL;
    Obj d = AT(slv, o_dict, Obj), ents = d ? AT(d, 0x18, Obj) : NULL; int dn = d ? AT(d, 0x20, int32_t) : 0;
    int drawn = 0, bad = 0;
    for (int i = 0; i < n && a; i++) {
        Obj cs = AT(a, 0x20 + 8 * i, Obj); if (!live(cs) || !shown(cs)) continue;
        drawn++;
        Obj slots = AT(cs, o_slots, Obj); int sn = slots ? AT(slots, 0x18, int32_t) : 0; Obj sa = slots ? AT(slots, 0x10, Obj) : NULL;
        WRow w; memset(&w, 0, sizeof w);
        for (int e = 0; e < dn && ents && !w.guid[0]; e++) {
            char *en = (char *)ents + 0x20 + 24 * e; if (AT(en, 4, int32_t) < -1) continue;
            Obj key = AT(en, 8, Obj), val = AT(en, 16, Obj);
            if (!key || !val || obj_class(key) != k_str || obj_class(val) != k_btn) continue;
            Obj vg = comp_go(val, NULL); int mine = 0;
            for (int s = 0; s < sn && sa && !mine; s++) mine = AT(sa, 0x20 + 8 * s, Obj) == vg;
            if (!mine) continue;
            char sv[256]; utf8_of(key, sv, sizeof sv); const char *g = guid_of_save(sv); if (g) snprintf(w.guid, sizeof w.guid, "%s", g);
        }
        if (!w.guid[0] || (o_cur >= 0 && AT(cs, o_cur, uint8_t))) continue;
        if (!lf_is_mp) lf_syms();
        int kind = world_kind(&w);
        bad += panel && !lf_on[kind] && !lf_all_off();
    }
    dsl_log("rows test: open frame %d: %d world row(s) drawn, %d of them hidden by the filters", k, drawn, bad);
    k++;
}
void dsl_rows_frame(void) {
    if (!ok) return;
    static unsigned fr; ++fr;
    if (list_open && lt_applied && h_slv) {                                 /* every frame: a world opened by the player shows its */
        Obj slv = gc_get(h_slv);                                            /* older saves for one frame at most */
        if (live(slv) && latest_apply()) refresh_size(slv, NULL);
    }
    if (fr % 6) return;                                                     /* 10 times a second at 60 fps */
    Obj slv = h_slv ? gc_get(h_slv) : NULL;
    if (!live(slv)) { if (h_slv) gc_free(h_slv); h_slv = 0; slv = (fr % 30 == 0) ? find_obj(t_slv, 1, NULL) : NULL; if (slv) h_slv = gc_new(slv, 0); }
    if (slv) list_poll(slv);
    else if (list_open) list_poll(NULL);
}
