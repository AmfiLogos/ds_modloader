// Load list service (dsl_sdk.h API 3: loadrow_add, loadlist_filter, loadlist_refresh) + the loader's own lines:
// world rows of the stock Load list get "Mods: <names>  Change", the New Campaign screen "Mods for this world: <names>  Change".
// No hook on the row sites: the multiplayer module hooks SubUI_SaveFileButton.SetData, SubUI_SettlerNameCase.Init and
// SaveLoadView.RefreshLoadPanel until it moves to this API (MASTER_PLAN 5.1 wp-menu), and the engine allows one owner per
// site. So this file polls: every few frames it looks at the open SaveLoadView; when its rows changed it rebuilds our lines.
//   SaveLoadView._campaignCases (List<SubUI_SettlerNameCase>), _saveNameSlotDictionary (save name -> SubUI_SaveFileButton);
//   a world row's guid = the save header's CampaignGuid (CampaignLoadingEventHandler.TryGetSaveHeader) of one of its saves.
// Filters hide a world row and its save rows (SetActive false), then the stock SaveLoadView.RefreshPanelSize.
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
static void *k_slv, *k_case, *k_btn, *k_str, *k_cs;
typedef struct { float x, y, w, h; } URect;
static URect (*rt_rect)(Obj t, const void *mi);
static void dsl_rows_place(Obj cs, DSLObj line, DSLObj chg);
static int dsl_rows_line_chars(Obj cs);
static void dsl_rows_place_newcampaign(Obj cs, DSLObj line, DSLObj btn);
static int32_t o_cases = -1, o_dict = -1, o_slots = -1, o_title_name = -1, o_cur = -1, o_lowload = -1;
static Obj (*find_obj)(Obj type, int inactive, const void *mi);
static int (*alive)(Obj o, const void *mi);
static Obj (*comp_go)(Obj c, const void *mi), (*comp_tr)(Obj c, const void *mi);
static int (*go_tree)(Obj g, const void *mi), (*go_self)(Obj g, const void *mi);
static void (*go_set)(Obj g, int on, const void *mi);
static void (*refresh_size)(Obj slv, const void *mi);
static int (*try_header)(Obj self, Obj name, void *hdr, Obj *set, const void *mi);
static Obj (*str_new)(const char *);
static void *(*obj_class)(Obj);
static Obj t_slv, t_cs;
static uintptr_t (*gc_new)(Obj, int); static Obj (*gc_get)(uintptr_t); static void (*gc_free)(uintptr_t);
static int32_t mf_handler = -1;

static void *cls(const char *ns, const char *n) { void *c = dsl_find_class(ns, n); if (!c) dsl_log("rows: class %s.%s missing", ns, n); return c; }
static int32_t fo(void *k, const char *n) { int32_t o = k ? dsl_field_offset(k, n) : -1; if (o < 0) dsl_log("rows: field %s missing", n); return o; }
static void *ma(void *k, const char *n, int argc) { void *m = k ? dsl_find_method(k, n, argc) : NULL; return m ? dsl_method_address(m) : NULL; }
static int live(Obj o) { return o && alive && alive(o, NULL); }
static int shown(Obj comp) { Obj g = live(comp) ? comp_go(comp, NULL) : NULL; return g && go_tree(g, NULL); }
static void set_on(Obj comp, int on) { Obj g = live(comp) ? comp_go(comp, NULL) : NULL; if (g && go_self(g, NULL) != on) go_set(g, on, NULL); }

void dsl_rows_install(void) {
    k_slv = cls("Refactor", "SaveLoadView"); k_case = cls("Refactor", "SubUI_SettlerNameCase"); k_btn = cls("Refactor.UI", "SubUI_SaveFileButton");
    k_str = cls("System", "String"); k_cs = cls("Refactor.UI", "SubUI_CampaignSetting");
    o_cases = fo(k_slv, "_campaignCases"); o_dict = fo(k_slv, "_saveNameSlotDictionary"); o_lowload = fo(k_slv, "_lowLoadPanel");
    o_slots = fo(k_case, "_saveSlots"); o_title_name = fo(k_case, "_nameText"); o_cur = fo(k_case, "_isCurrentCampaign");
    void *kObj = cls("UnityEngine", "Object"), *kComp = cls("UnityEngine", "Component"), *kGo = cls("UnityEngine", "GameObject");
    find_obj = ma(kObj, "FindObjectOfType", 2); alive = ma(kObj, "op_Implicit", 1);
    comp_go = ma(kComp, "get_gameObject", 0); comp_tr = ma(kComp, "get_transform", 0);
    go_tree = ma(kGo, "get_activeInHierarchy", 0); go_self = ma(kGo, "get_activeSelf", 0); go_set = ma(kGo, "SetActive", 1);
    refresh_size = ma(k_slv, "RefreshPanelSize", 0);
    rt_rect = ma(cls("UnityEngine", "RectTransform"), "get_rect", 0);
    void *kh = cls("Refactor.Main.Event", "CampaignLoadingEventHandler"); try_header = ma(kh, "TryGetSaveHeader", 3);
    void *kmf = cls("Refactor.Main", "MainFlow"); mf_handler = fo(kmf, "_eventHandler");
    str_new = dsl_ga_sym("il2cpp_string_new"); obj_class = dsl_ga_sym("il2cpp_object_get_class");
    gc_new = dsl_ga_sym("il2cpp_gchandle_new"); gc_get = dsl_ga_sym("il2cpp_gchandle_get_target"); gc_free = dsl_ga_sym("il2cpp_gchandle_free");
    void *(*ctype)(void *) = dsl_ga_sym("il2cpp_class_get_type"); Obj (*tobj)(void *) = dsl_ga_sym("il2cpp_type_get_object");
    if (ctype && tobj && k_slv) t_slv = tobj(ctype(k_slv));
    if (ctype && tobj && k_cs) t_cs = tobj(ctype(k_cs));
    ok = find_obj && alive && comp_go && comp_tr && go_tree && go_self && go_set && refresh_size && try_header && str_new && obj_class
         && gc_new && gc_get && gc_free && t_slv && o_cases >= 0 && o_dict >= 0 && o_slots >= 0 && o_title_name >= 0 && mf_handler >= 0;
    dsl_log("rows: load list service %s", ok ? "on (polling the open Load list)" : "OFF (names missing)");
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
    return SG[i].guid;
}

/* ------------------------------------------------------------------ the open list */
#define MAXW 64
typedef struct { uintptr_t cs; char guid[40], save[256], name[160]; DSLObj line, chg; int hid; } WRow;
static WRow W[MAXW]; static int nw;
static uintptr_t h_slv; static uint32_t sig_last; static int list_open;
static DSLObj band;                                     /* our toggles' strip */

static void rows_clear(void) {
    const DSLUi *u = dsl_ui_kit();
    for (int i = 0; i < nw; i++) { if (u && W[i].line) u->destroy(W[i].line); if (u && W[i].chg) u->destroy(W[i].chg); gc_free(W[i].cs); }
    nw = 0;
}
static uint32_t list_sig(Obj slv) {
    Obj l = AT(slv, o_cases, Obj); int n = l ? AT(l, 0x18, int32_t) : 0; Obj a = l ? AT(l, 0x10, Obj) : NULL;
    uint32_t s = (uint32_t)n * 2654435761u;
    for (int i = 0; i < n && a; i++) { Obj c = AT(a, 0x20 + 8 * i, Obj); s = s * 31 + (uint32_t)(uintptr_t)c; if (c && shown(c)) s ^= 0x9e37u + (uint32_t)i; }
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
static void *m_tmp_get;
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
        world_name(cs, w->name, sizeof w->name);
        w->cs = gc_new(cs, 0); nw++;
    }
}

/* the world's mods (owner 2026-10-16 #6, #9): short names on the RIGHT of the world row, as many as fit + "+N";
   the hover card lists every mod, one per line. The multiplayer tag stays on the left, after the world name. */
void dsl_mod_short(const char *id, char *out, size_t cap);
const char *dsl_mod_name(const char *id);
int dsl_reg_scene(void);
static void world_texts(const char *guid, int maxc, char *line, size_t lcap, char *all, size_t acap) {
    int dsl_world_mods_all(const char *guid, const char **ids, int max);
    const char *ids[64]; int n = dsl_world_mods_all(guid, ids, 64);
    all[0] = 0; line[0] = 0;
    if (!n) { snprintf(line, lcap, "<color=#A69085>Mods:</color> none"); snprintf(all, acap, "No mods."); return; }
    int used = 6, k = 0;                                                  /* "Mods: " */
    char shown[600] = "";
    for (int i = 0; i < n; i++) {
        const char *nm = dsl_mod_name(ids[i]);
        char one[300]; snprintf(one, sizeof one, "%s%s", nm ? nm : ids[i], nm ? "" : " (not installed)");
        if (i) strlcat(all, "\n", acap);
        strlcat(all, one, acap);
        char sh[64]; dsl_mod_short(ids[i], sh, sizeof sh);
        int len = (int)strlen(sh) + (k ? 3 : 0), plus = i + 1 < n ? 4 : 0;
        if (k && used + len + plus > maxc) continue;                      /* the first one always shows */
        if (k) strlcat(shown, " \xC2\xB7 ", sizeof shown);
        strlcat(shown, sh, sizeof shown); used += len; k++;
    }
    if (k < n) { char pl[16]; snprintf(pl, sizeof pl, "  +%d", n - k); strlcat(shown, pl, sizeof shown); }
    snprintf(line, lcap, "<color=#A69085>Mods:</color> %s", shown);
}
/* owner item 9: a mod whose saves need the loader (mod.json "save_dependent") gets one warning line at the card's end */
static void world_warning(const char *guid, char *all, size_t acap) {
    int dsl_world_mods_all(const char *guid, const char **ids, int max);
    int dsl_save_warning(const char *const *ids, int n, char *out, size_t cap);
    const char *ids[64]; int n = dsl_world_mods_all(guid, ids, 64); char wr[400];
    if (dsl_save_warning(ids, n, wr, sizeof wr)) { strlcat(all, "\n\n<color=#EA9F3D>", acap); strlcat(all, wr, acap); strlcat(all, "</color>", acap); }
}
static void decorate(void) {
    const DSLUi *u = dsl_ui_kit(); if (!u) return;
    int at_title = dsl_reg_scene() != 2;                                  /* in a world mods do not change (owner #10) */
    for (int i = 0; i < nw; i++) {
        WRow *w = &W[i]; Obj cs = gc_get(w->cs); if (!live(cs) || w->hid) continue;
        char txt[1200], all[1400], one[400];
        world_texts(w->guid, dsl_rows_line_chars(cs), txt, sizeof txt, all, sizeof all);
        for (int r = 0; r < nr; r++) {
            one[0] = 0; R[r].r.text(R[r].r.ud, w->guid, w->save, one, sizeof one);
            if (one[0]) { strlcat(txt, "   ", sizeof txt); strlcat(txt, one, sizeof txt); strlcat(all, "\n", sizeof all); strlcat(all, one, sizeof all); }
        }
        world_warning(w->guid, all, sizeof all);
        Obj tr = comp_tr(cs, NULL);
        if (!w->line) w->line = u->text(tr, 0, 0, 10, 10, txt, 14, DSL_TXT_RIGHT, 0xD9C7B0FF);
        else u->set_text(w->line, txt);
        u->tooltip(w->line, "Mods of this world", all);
        if (!w->chg) { w->chg = u->button(tr, 0, 0, 10, 10, "Change", change_click, (void *)(intptr_t)i);
                       u->tooltip(w->chg, "Change", "Choose the mods this world loads with."); }
        u->set_active(w->chg, at_title);
        dsl_rows_place(cs, w->line, at_title ? w->chg : NULL);
    }
}

/* layout: local units of the row's RectTransform (rect from RectTransform.get_rect) */
/* one line on the right of the world row's header: "Mods: a · b +N  [Change]"; the left half keeps the name + tag */
#define LINE_X 0.5f
#define CHG_W 64.f
static float row_w(Obj cs) { Obj t = comp_tr(cs, NULL); return t && rt_rect ? rt_rect(t, NULL).w : 0; }
static int dsl_rows_line_chars(Obj cs) { float w = row_w(cs) * (1 - LINE_X) - CHG_W - 12; return w > 60 ? (int)(w / 7.0f) : 10; }
static void dsl_rows_place(Obj cs, DSLObj line, DSLObj chg) {
    const DSLUi *u = dsl_ui_kit(); float w = row_w(cs); if (!u || w <= 0) return;
    float x0 = w * LINE_X, bx = chg ? w - CHG_W - 4 : w - 4;
    u->place(line, x0, 1, bx - x0 - 8, 20);
    if (chg) u->place(chg, bx, 2, CHG_W, 18);
}
static void dsl_rows_place_newcampaign(Obj cs, DSLObj line, DSLObj btn) {
    const DSLUi *u = dsl_ui_kit(); Obj t = comp_tr(cs, NULL); if (!u || !t || !rt_rect) return;
    URect r = rt_rect(t, NULL);
    u->place(line, 24, r.h - 52, r.w - 180, 40);
    u->place(btn, r.w - 140, r.h - 50, 110, 34);
}

/* filters: on list open ask mode + label; apply show() per world */
/* worlds WE hid (kept across rebuilds): only those are shown again; a row hidden by someone else stays as it is */
static char we_hid[MAXW][40]; static int nwh;
static int hid_find(const char *g) { for (int i = 0; i < nwh; i++) if (!strcmp(we_hid[i], g)) return i; return -1; }
static void filters_apply(Obj slv) {
    if (!nf) return;
    int changed = 0;
    for (int i = 0; i < nw; i++) {
        WRow *w = &W[i]; Obj cs = gc_get(w->cs); if (!live(cs)) continue;
        int show = 1;
        if (o_cur >= 0 && AT(cs, o_cur, uint8_t)) show = 1;                 /* the world being played always shows */
        else for (int f = 0; f < nf && show; f++) if (F[f].mode != DSL_LIST_OFF) show = F[f].f.show(F[f].f.ud, w->guid, w->save, F[f].on);
        w->hid = !show;
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
    if (!band || !live((Obj)band)) {
        band = u->panel(low, 0, -40, 600, 36, 0x00000000);
        if (!band) return;
    }
    int k = 0;
    for (int f = 0; f < nf; f++) {
        F[f].mode = F[f].f.mode ? F[f].f.mode(F[f].f.ud) : DSL_LIST_LOAD;
        const char *lb = F[f].mode != DSL_LIST_OFF && F[f].f.label ? F[f].f.label(F[f].f.ud) : NULL;
        if (F[f].tog && !live((Obj)F[f].tog)) F[f].tog = NULL;
        if (!lb || !*lb) { if (F[f].tog) u->set_active(F[f].tog, 0); continue; }
        F[f].on = 0;                                                        /* off each time the list opens */
        float x0 = dsl_mod_state("multiplayer", NULL) == 1 ? 310 : 8;      /* MP draws its own toggle on the left until it uses this API */
        if (!F[f].tog) F[f].tog = u->toggle(band, x0 + 250 * k, 4, 230, lb, 0, toggle_change, (void *)(intptr_t)f);
        else { u->set_text(F[f].tog, lb); u->toggle_set(F[f].tog, 0); u->set_active(F[f].tog, 1); }
        k++;
    }
}

/* the New Campaign line */
static DSLObj nc_line, nc_btn; static uintptr_t h_cs;
static void nc_click(void *ud, DSLObj w) { (void)ud; (void)w; dsl_log("rows: New Campaign [Change]"); dsl_newworld_change(); }
static void new_campaign_frame(void) {
    const DSLUi *u = dsl_ui_kit(); if (!u || !t_cs) return;
    Obj cs = h_cs ? gc_get(h_cs) : NULL;
    if (!live(cs)) { if (h_cs) gc_free(h_cs); h_cs = 0; nc_line = nc_btn = NULL; cs = find_obj(t_cs, 0, NULL); if (!cs) return; h_cs = gc_new(cs, 0); }
    if (!shown(cs)) return;
    char t[600]; dsl_newworld_label(t, sizeof t);
    Obj tr = comp_tr(cs, NULL);
    if (!nc_line || !live((Obj)nc_line)) { nc_line = u->text(tr, 0, 0, 10, 10, t, 16, DSL_TXT_LEFT, 0xD9C7B0FF); nc_btn = u->button(tr, 0, 0, 10, 10, "Change", nc_click, NULL); }
    else u->set_text(nc_line, t);
    dsl_rows_place_newcampaign(cs, nc_line, nc_btn);
}

/* test only (dsl_reg.c test requests): the "Mods: ..." line of world row i */
DSLObj dsl_rows_test_line(int i) { return i >= 0 && i < nw ? W[i].line : NULL; }

/* test only: the "Change" of world row i, as a click */
void dsl_rows_test_change(int i) { change_click((void *)(intptr_t)i, NULL); }

void dsl_rows_frame(void) {
    if (!ok) return;
    static unsigned fr; if (++fr % 6) return;                               /* 10 times a second at 60 fps */
    Obj slv = h_slv ? gc_get(h_slv) : NULL;
    if (!live(slv)) { if (h_slv) gc_free(h_slv); h_slv = 0; slv = (fr % 30 == 0) ? find_obj(t_slv, 1, NULL) : NULL; if (slv) h_slv = gc_new(slv, 0); }
    if (fr % 30 == 0) new_campaign_frame();
    int open = live(slv) && shown(slv) && o_lowload >= 0 && shown(AT(slv, o_lowload, Obj));   /* the Load panel (not Save) */
    if (!open) { if (list_open) { list_open = 0; rows_clear(); } return; }
    uint32_t s = list_sig(slv);
    if (!list_open) { list_open = 1; toggles_build(slv); sig_last = 0; }
    if (s != sig_last || dirty) {
        sig_last = s; dirty = 0;
        build_rows(slv);
        filters_apply(slv);
        decorate();
        sig_last = list_sig(slv);
        int hidden = 0; for (int i = 0; i < nw; i++) hidden += W[i].hid;
        dsl_log("rows: Load list rebuilt: %d world row(s) (%d hidden by filters), %d text source(s), %d filter(s)", nw, hidden, nr, nf);
    }
}
