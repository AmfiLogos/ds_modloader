// zetu-suite, the screens of an expedition with more than 4 units (included by zetu.c: one compile unit per module).
// Only runs when max_units != 4 (zetu_ui_init from party_init). Every class, method and field by il2cpp name.
//
// The stock prefabs hold 4 of each per-unit element, placed by hand (the result screen: a grid); the game's loops stop at the
// list / array size, so without this units 5..N are simply missing (no crash). Here the 4th element is copied until
// there are N, the copies go into the game's own list / array (so the stock code fills, shows and wires them like
// the first 4), and the screen is laid out again so every element stays readable (UX review 2026-10-25, Z1-Z4):
//   - supplies step: SubUI_CheckInventoryPanelUI._panels (EntityInventoryPanelUI[]), before ExpeditionPlanView.Init
//     (Init wires the callbacks of every panel of the array, by index), and the empty-slot boxes behind them. The
//     panel area is one stock panel high (500 of the window's 816), so a second row or more than 5 panels in the
//     row would be half size: 5 units = one row (scale 0.81); 6..8 = pages of 4 at the stock size and place, with
//     two of the kit's game buttons "1-4" / "5-8" in the window's title bar (a gold line under the open page's);
//   - region step, the selected units' portraits: SubUI_SelectRegionUI._bottomtransferUnitImages and
//     _bottomtransferUnitSelectedOutlines (List<Image>), same time;
//   - region step, the count "Expedition Members (n/4)": the "/4" is a literal of SubUI_SelectRegionUI.SetUnit (and
//     " (0/4)" of InitOnActivate; the multiplayer module hooks both), and every call of SetUnit is followed by
//     SubUI_ExpeditionPlanLowerPanel.OnSetUnit (InitOnActivate too, before its SetUnit), so the text is rewritten
//     to "/N" after that;
//   - result screen: DungeonResultView._portraitUIList (List<SubUI_DungeonResultPortrait>), before SetPlayerData; with
//     5..8 units the portraits go in two lines (4 + the rest, centred) at the stock size and the window grows by one
//     line (its top part moves up and its bottom part down by half a line each).
// Proof on a hidden test copy: test/zetu_ui_test.py (8 units: expedition, supplies, dungeon, windows, result; the
// sizes and places measured on screen by the test probe).
// The Inventory / Status / Skill windows of selected units grow the same way (their panel rows: see "unit windows").

#include <stdio.h>

static const char *const ui_ns = "Refactor.UI";
typedef struct { float x, y; } ZV2;
typedef struct { float x, y, z; } ZV3;
typedef struct { float r, g, b, a; } ZC4;
typedef void *ZObj;
#define ZAT(o, off, T) (*(T *)((char *)(o) + (off)))
#define ZCALL(mi, R, ...) ((R (*)(__VA_ARGS__, const void *))(*(void **)(mi)))

static struct {
    void *(*class_get_methods)(void *, void **);
    const char *(*method_get_name)(void *);
    uint32_t (*method_get_param_count)(void *);
    void *(*method_get_param)(void *, uint32_t);
    char *(*type_get_name)(void *);
    void (*il_free)(void *);
    void *(*class_get_parent)(void *);
    void *(*object_get_class)(ZObj);
    uint8_t (*class_is_assignable_from)(void *, void *);       /* C bool: only the low byte is set (Windows: sete al) */
    void *(*class_get_type)(void *);
    ZObj (*type_get_object)(void *);
    ZObj (*array_new)(void *, uintptr_t);
    ZObj (*string_new_utf16)(const uint16_t *, int32_t);
    void *(*object_get_virtual_method)(ZObj, void *);
    void (*wbarrier)(ZObj, void **, ZObj);
} ZI;
static void *zk_object, *zk_component, *zk_transform, *zk_rect, *zk_tmp, *zk_panel, *zk_image, *zk_result_portrait;
static void *zm_implicit, *zm_inst, *zm_get_tr, *zm_get_go, *zm_go_active, *zm_go_self, *zm_tr_parent, *zm_tr_find, *zm_name,
    *zm_get_comp, *zm_pos_get, *zm_pos_set, *zm_size_get, *zm_pivot_get, *zm_scale_get, *zm_scale_set, *zm_tmp_get, *zm_tmp_set,
    *zm_sib_get, *zm_sib_set, *zm_tr_count, *zm_tr_child;
static int32_t zf_view_select = -1, zf_view_cip = -1, zf_cip_panels = -1, zf_sel_text = -1, zf_sel_imgs = -1, zf_sel_lines = -1,
               zf_res_list = -1;

static void *zmi_sig(void *k, const char *name, int argc, const char *sig) {
    for (void *c = k; c; c = ZI.class_get_parent(c)) {
        void *it = NULL, *m;
        while ((m = ZI.class_get_methods(c, &it))) {
            if (strcmp(ZI.method_get_name(m), name) || (int)ZI.method_get_param_count(m) != argc) continue;
            if (!sig) return m;
            char buf[512] = ""; size_t o = 0;
            for (int i = 0; i < argc; i++) {
                char *tn = ZI.type_get_name(ZI.method_get_param(m, (uint32_t)i));
                o += (size_t)snprintf(buf + o, o < sizeof buf ? sizeof buf - o : 0, "%s%s", i ? "," : "", tn ? tn : "?");
                if (tn && ZI.il_free) ZI.il_free(tn);
            }
            if (!strcmp(buf, sig)) return m;
        }
    }
    return NULL;
}
static int z_alive(ZObj o) { return o && ZCALL(zm_implicit, uint8_t, ZObj)(o, zm_implicit); }
static int z_isa(ZObj o, void *k) { return k && z_alive(o) && ZI.class_is_assignable_from(k, ZI.object_get_class(o)); }
static ZObj z_tr(ZObj c) {                                     /* Transform of a component (a Transform is its own) */
    if (!z_alive(c)) return NULL;
    if (z_isa(c, zk_transform)) return c;
    return ZCALL(zm_get_tr, ZObj, ZObj)(c, zm_get_tr);
}
static ZObj z_go(ZObj c) { return z_alive(c) ? ZCALL(zm_get_go, ZObj, ZObj)(c, zm_get_go) : NULL; }
static ZObj z_parent(ZObj t) { return z_alive(t) ? ZCALL(zm_tr_parent, ZObj, ZObj)(t, zm_tr_parent) : NULL; }
static int z_active(ZObj c) { ZObj g = z_go(c); return g && ZCALL(zm_go_self, uint8_t, ZObj)(g, zm_go_self); }
static ZV2 z_pos(ZObj t) { return z_isa(t, zk_rect) ? ZCALL(zm_pos_get, ZV2, ZObj)(t, zm_pos_get) : (ZV2){ 0, 0 }; }
static void z_setpos(ZObj t, ZV2 p) { if (z_isa(t, zk_rect)) ZCALL(zm_pos_set, void, ZObj, ZV2)(t, p, zm_pos_set); }
static ZV2 z_size(ZObj t) { return z_isa(t, zk_rect) ? ZCALL(zm_size_get, ZV2, ZObj)(t, zm_size_get) : (ZV2){ 0, 0 }; }
static ZV2 z_pivot(ZObj t) { return z_isa(t, zk_rect) ? ZCALL(zm_pivot_get, ZV2, ZObj)(t, zm_pivot_get) : (ZV2){ 0.5f, 0.5f }; }
static float z_scale(ZObj t) { return z_alive(t) ? ZCALL(zm_scale_get, ZV3, ZObj)(t, zm_scale_get).x : 1; }
static void z_setscale(ZObj t, float s) { if (z_alive(t)) ZCALL(zm_scale_set, void, ZObj, ZV3)(t, (ZV3){ s, s, 1 }, zm_scale_set); }
static ZObj z_type(void *k) { return k ? ZI.type_get_object(ZI.class_get_type(k)) : NULL; }
static ZObj z_comp(ZObj c, void *k) {                          /* GetComponent(Type) on the same GameObject */
    return z_alive(c) && k ? ZCALL(zm_get_comp, ZObj, ZObj, ZObj)(c, z_type(k), zm_get_comp) : NULL;
}
static void z_setref(ZObj obj, int32_t off, ZObj v) {          /* a managed reference field (GC write barrier) */
    void **p = (void **)((char *)obj + off);
    if (ZI.wbarrier) ZI.wbarrier(obj, p, v); else *p = v;
}
static void z_name(ZObj o, char *out, size_t cap) {
    out[0] = 0;
    ZObj s = z_alive(o) ? ZCALL(zm_name, ZObj, ZObj)(o, zm_name) : NULL;
    int32_t n = s ? ZAT(s, 0x10, int32_t) : 0; size_t k = 0;
    for (int32_t i = 0; i < n && k + 1 < cap; i++) { uint16_t c = ((uint16_t *)((char *)s + 0x14))[i]; out[k++] = c < 0x80 ? (char)c : '?'; }
    out[k] = 0;
}

/* -------------------------------------------------------------------------------------------- copies of a slot */
/* The slot of element e (an Image / panel script): its ancestor that is a direct child of `row` (the common parent
   of the 4 stock elements). path: the names from the slot down to e ("" = e is on the slot itself). */
static ZObj z_slot_of(ZObj e, ZObj row, char *path, size_t cap) {
    ZObj names[16]; int n = 0;
    ZObj t = z_tr(e);
    while (t && n < 16) { ZObj p = z_parent(t); if (p == row) break; names[n++] = t; t = p; }
    if (!t || n >= 16) return NULL;
    path[0] = 0; size_t o = 0;
    /* t = the slot; names[0..n-1] = e .. the slot's child (bottom-up) */
    for (int i = n - 1; i >= 0; i--) {
        char nm[96]; z_name(names[i], nm, sizeof nm);
        o += (size_t)snprintf(path + o, o < cap ? cap - o : 0, "%s%s", o ? "/" : "", nm);
    }
    return t;
}
static ZObj z_row_of(ZObj a, ZObj b) {                          /* lowest common ancestor of two elements */
    for (ZObj p = z_parent(z_tr(a)); p; p = z_parent(p))
        for (ZObj q = z_parent(z_tr(b)); q; q = z_parent(q))
            if (p == q) return p;
    return NULL;
}
static ZObj z_find(ZObj slot, const char *path) {
    if (!path[0]) return slot;
    static ZObj (*str_new)(const char *);
    if (!str_new) *(void **)&str_new = H->il2cpp_fn("il2cpp_string_new");
    return ZCALL(zm_tr_find, ZObj, ZObj, ZObj)(slot, str_new(path), zm_tr_find);
}
/* a copy of `slot` under the same parent, right after the last stock slot in sibling order (drawn like them) */
static ZObj z_copy(ZObj slot, int sib) {
    ZObj c = ZCALL(zm_inst, ZObj, ZObj, ZObj)(slot, z_parent(slot), zm_inst);
    ZObj t = z_tr(c);
    if (t && sib >= 0) ZCALL(zm_sib_set, void, ZObj, int32_t)(t, sib, zm_sib_set);
    return t;
}

/* Lay out n slots (the first `stock` of them placed by the prefab) in one row with the stock row's span (left edge of
   the first to right edge of the last stock slot) and top edge: smaller, same order. Slots are placed by hand with
   their own anchors and pivots (the region step anchors two at the left and two at the right of their row), so every
   rect is taken in the row's own space: x from its left edge, y from its bottom edge. A GridLayoutGroup row (the
   result screen) is made one line long enough for n and scaled instead. Returns the scale; *overlap = 1 when two
   slots would overlap. */
typedef struct { float x, y, w, h; } ZRect;
static void *zk_grid, *zm_rebuild, *zm_rect, *zm_size_set, *zm_amin_get;
static ZRect z_rect(ZObj t) { return z_isa(t, zk_rect) ? ZCALL(zm_rect, ZRect, ZObj)(t, zm_rect) : (ZRect){ 0, 0, 0, 0 }; }
static ZV2 z_amin(ZObj t) { return z_isa(t, zk_rect) ? ZCALL(zm_amin_get, ZV2, ZObj)(t, zm_amin_get) : (ZV2){ 0, 0 }; }
static float z_layout_row(ZObj *slot, int n, int stock, int *overlap, char *info, size_t cap) {
    ZObj row = z_parent(slot[0]);
    *overlap = 0;
    if (z_comp(row, zk_grid)) {                                 /* result portraits: a grid of zero-size cells */
        ZCALL(zm_rebuild, void, ZObj)(row, zm_rebuild);
        float x0 = z_pos(slot[0]).x, dx = z_pos(slot[1]).x - x0;
        ZV2 rs = z_size(row), rp = z_pos(row), pv = z_pivot(row); float sc = z_scale(row);
        if (!(dx > 0)) { snprintf(info, cap, "grid pitch %.1f: not laid out", dx); *overlap = 1; return 0; }
        float s = (float)stock / (float)n, w2 = rs.x + (float)(n - stock) * dx;
        float c_old = rp.x - pv.x * rs.x * sc + sc * (x0 + (float)(stock - 1) * dx / 2);
        ZCALL(zm_size_set, void, ZObj, ZV2)(row, (ZV2){ w2, rs.y }, zm_size_set);
        z_setscale(row, sc * s);
        z_setpos(row, (ZV2){ c_old + pv.x * w2 * sc * s - sc * s * (x0 + (float)(n - 1) * dx / 2), rp.y });
        ZCALL(zm_rebuild, void, ZObj)(row, zm_rebuild);
        for (int i = 1; i < n; i++) {                          /* one line, in order, one pitch apart */
            ZV2 a = z_pos(slot[i - 1]), b = z_pos(slot[i]);
            if (b.y != a.y || b.x - a.x < dx - 0.5f) *overlap = 1;
        }
        snprintf(info, cap, "grid, pitch %.0f, %d in one line, row %.0f wide at scale %.3f", dx, n, w2, sc * s);
        return s;
    }
    ZRect rr = z_rect(row);
    float L[8], T[8], w = 0, h = 0;
    for (int i = 0; i < stock; i++) {
        ZV2 a = z_amin(slot[i]), p = z_pos(slot[i]), pv = z_pivot(slot[i]), sz = z_size(slot[i]); float sc = z_scale(slot[i]);
        L[i] = a.x * rr.w + p.x - pv.x * sz.x * sc;
        T[i] = a.y * rr.h + p.y + (1 - pv.y) * sz.y * sc;
        if (!i) { w = sz.x * sc; h = sz.y * sc; }
    }
    float dx = stock > 1 ? (L[stock - 1] - L[0]) / (float)(stock - 1) : w;
    if (!(dx > 0) || !(w > 0)) { snprintf(info, cap, "stock pitch %.1f / width %.1f: not laid out", dx, w); *overlap = 1; return 0; }
    float span = (float)(stock - 1) * dx + w, s = span / ((float)(n - 1) * dx + w);
    if (s > 1) s = 1;
    for (int i = 0; i < n; i++) {
        ZV2 a = z_amin(slot[i]), pv = z_pivot(slot[i]), sz = z_size(slot[i]);
        float sc1 = z_scale(slot[i]) * s;                       /* a copy: the size and scale of the slot it copies */
        z_setscale(slot[i], sc1);
        z_setpos(slot[i], (ZV2){ L[0] + (float)i * dx * s - a.x * rr.w + pv.x * sz.x * sc1, T[0] - a.y * rr.h - (1 - pv.y) * sz.y * sc1 });
    }
    float prev_r = -1e9f;                                       /* check: the placed rects, left to right */
    for (int i = 0; i < n; i++) {
        ZV2 a = z_amin(slot[i]), p = z_pos(slot[i]), pv = z_pivot(slot[i]), sz = z_size(slot[i]); float sc = z_scale(slot[i]);
        float l = a.x * rr.w + p.x - pv.x * sz.x * sc;
        if (l < prev_r - 0.5f) *overlap = 1;
        prev_r = l + sz.x * sc;
    }
    snprintf(info, cap, "stock %d from x %.0f, pitch %.0f, %.0fx%.0f; now %d at scale %.3f, x %.0f..%.0f of %.0f",
             stock, L[0], dx, w, h, n, s, L[0], prev_r, rr.w);
    return s;
}

/* weak GC handles (an object kept past this frame: re-fetched and checked alive at each use) */
static uintptr_t zh_new(ZObj o) {
    static uintptr_t (*nw)(ZObj, uint8_t);
    if (!nw) *(void **)&nw = H->il2cpp_fn("il2cpp_gchandle_new_weakref");
    return o && nw ? nw(o, 0) : 0;
}
static ZObj zh_get(uintptr_t h) {
    static ZObj (*get)(uintptr_t);
    if (!get) *(void **)&get = H->il2cpp_fn("il2cpp_gchandle_get_target");
    ZObj o = h && get ? get(h) : NULL;
    return z_alive(o) ? o : NULL;
}
static void zh_set(uintptr_t *h, ZObj o) {
    static void (*fr)(uintptr_t);
    if (!fr) *(void **)&fr = H->il2cpp_fn("il2cpp_gchandle_free");
    if (*h && fr) fr(*h);
    *h = zh_new(o);
}
static void *zm_enabled_set, *zm_name_set, *zm_amax_get, *zm_find_type;
static void z_enable(ZObj beh, int on) { if (z_alive(beh)) ZCALL(zm_enabled_set, void, ZObj, uint8_t)(beh, (uint8_t)(on != 0), zm_enabled_set); }
static ZV2 z_amax(ZObj t) { return z_isa(t, zk_rect) ? ZCALL(zm_amax_get, ZV2, ZObj)(t, zm_amax_get) : (ZV2){ 0, 0 }; }
static void z_setname(ZObj o, const char *s) {
    static ZObj (*str_new)(const char *);
    if (!str_new) *(void **)&str_new = H->il2cpp_fn("il2cpp_string_new");
    ZObj g = z_go(o);
    if (g && str_new) ZCALL(zm_name_set, void, ZObj, ZObj)(g, str_new(s), zm_name_set);
}
static ZObj z_find_live(void *k) {                                /* Object.FindObjectOfType(Type): active objects only */
    return k ? ZCALL(zm_find_type, ZObj, ZObj)(z_type(k), zm_find_type) : NULL;
}

/* ---------------------------------------------------------------------------------------- supplies + portraits */
static int list_count(ZObj l) { return l ? ZAT(l, 0x18, int32_t) : 0; }
static ZObj list_item(ZObj l, int i) { ZObj a = l ? ZAT(l, 0x10, ZObj) : NULL; return a && i < list_count(l) ? ZAT(a, 0x20 + 8 * i, ZObj) : NULL; }
static void list_add(ZObj l, ZObj v) {                          /* the list's own Add (its class: List<T> of the game) */
    void *m = H->find_method(ZI.object_get_class(l), "Add", 1);
    if (m) ZCALL(m, void, ZObj, ZObj)(l, v, m);
}

/* Supplies step pages (6..8 units): page p shows slots 4p..4p+3 at the places of the 4 stock slots; the other page's
   slots and empty-slot boxes get scale 0 (the game sets the panels' active state itself: not touched). */
#define SP_PAGE_MIN_SCALE 0.75f                                 /* below this one row of all panels is too small */
#define SP_BX 70.f                                             /* the page buttons: x of the first, pitch, size, top */
#define SP_BP 96.f
#define SP_BW 88.f
#define SP_BH 36.f
#define SP_BY -62.f
static uintptr_t sp_slot[8], sp_bg[8], sp_mark_h;
static DSLObj sp_mark;
static float sp_sc[8], sp_bgsc[8];
static int sp_n, sp_nbg, sp_page;
static void sp_apply(void) {
    for (int i = 0; i < sp_n; i++) { ZObj t = zh_get(sp_slot[i]); if (t) z_setscale(t, i / 4 == sp_page ? sp_sc[i] : 0); }
    for (int i = 0; i < sp_nbg; i++) { ZObj t = zh_get(sp_bg[i]); if (t) z_setscale(t, i / 4 == sp_page ? sp_bgsc[i] : 0); }
    const DSLUi *U = dsl_ui(H);                                 /* the open page: the gold line under its button */
    if (U && sp_mark && zh_get(sp_mark_h) && DSL_UI_HAS(U, place)) U->place(sp_mark, SP_BX + sp_page * SP_BP, SP_BY + SP_BH + 2, SP_BW, 3);
}
static void sp_click(void *ud, DSLObj w) {
    (void)w;
    sp_page = (int)(intptr_t)ud;
    sp_apply();
    H->log(ME, "supplies: page %d (units %d..%d)", sp_page + 1, sp_page * 4 + 1, sp_page * 4 + 4 < sp_n ? sp_page * 4 + 4 : sp_n);
}
/* left / top edge of a slot in its row's space (x from the row's left edge, y from its bottom edge), and the
   anchoredPosition that puts slot t's left / top edge there (its own anchors, pivot, size and scale) */
static void z_lt(ZObj t, ZRect rr, float *L, float *T) {
    ZV2 a = z_amin(t), p = z_pos(t), pv = z_pivot(t), sz = z_size(t); float sc = z_scale(t);
    *L = a.x * rr.w + p.x - pv.x * sz.x * sc;
    *T = a.y * rr.h + p.y + (1 - pv.y) * sz.y * sc;
}
static void z_put_lt(ZObj t, ZRect rr, float L, float T) {
    ZV2 a = z_amin(t), pv = z_pivot(t), sz = z_size(t); float sc = z_scale(t);
    z_setpos(t, (ZV2){ L - a.x * rr.w + pv.x * sz.x * sc, T - a.y * rr.h - (1 - pv.y) * sz.y * sc });
}
/* slots n0..n-1 (copies) at the places of slots 0..3: pages of 4 */
static void z_page_slots(ZObj *slot, int n) {
    ZRect rr = z_rect(z_parent(slot[0]));
    for (int i = 4; i < n; i++) { float L, T; z_lt(slot[i - 4], rr, &L, &T); z_put_lt(slot[i], rr, L, T); }
}
static void sp_buttons(ZObj cip) {
    const DSLUi *U = dsl_ui(H);
    if (!U || !DSL_UI_HAS(U, button)) { H->log(ME, "supplies: no widget kit (older loader): pages without buttons"); return; }
    ZObj rt = z_tr(cip);
    char lb[2][8]; snprintf(lb[0], sizeof lb[0], "1-4"); snprintf(lb[1], sizeof lb[1], "5-%d", sp_n);
    for (int p = 0; p < 2; p++) {
        /* the window's title bar, right of its (i) button: the title and the (i) / X buttons sit 58 below the window
           top, the middle panel (this one's parent) starts 102 below it; its x 0 = the window's x 18 */
        ZObj b = (ZObj)U->button((DSLObj)rt, SP_BX + p * SP_BP, SP_BY, SP_BW, SP_BH, lb[p], sp_click, (void *)(intptr_t)p);
        if (!b) continue;
        z_setname(b, p ? "zetu_page_2" : "zetu_page_1");
        /* its label is a DASText: the step is hidden now, and DASText.Awake (first shown) sets the text from its own
           _text (empty in the kit's clone) -> put the label there too with the game's SetText */
        static void *k_tb, *k_das, *m_das_set; static int32_t f_lbl = -1;
        if (!k_tb) { k_tb = H->find_class(ui_ns, "SubUI_TitleMenuButton"); k_das = H->find_class(ui_ns, "DASText");
                     f_lbl = k_tb ? H->field_offset(k_tb, "_buttonName") : -1; m_das_set = k_das ? H->find_method(k_das, "SetText", 1) : NULL; }
        ZObj tb = k_tb ? z_comp(b, k_tb) : NULL, lt = tb && f_lbl >= 0 ? ZAT(tb, f_lbl, ZObj) : NULL;
        static ZObj (*str_new)(const char *);
        if (!str_new) *(void **)&str_new = H->il2cpp_fn("il2cpp_string_new");
        if (z_isa(lt, k_das) && m_das_set && str_new) ZCALL(m_das_set, void, ZObj, ZObj)(lt, str_new(lb[p]), m_das_set);
        else H->log(ME, "supplies: page button %d: no DASText label (%p)", p + 1, lt);
    }
    /* the open page's mark: a flat gold line under its button, as under the open tab of the loader's Mods window */
    sp_mark = U->panel((DSLObj)rt, SP_BX, SP_BY + SP_BH + 2, SP_BW, 3, 0);
    zh_set(&sp_mark_h, (ZObj)sp_mark);
    ZObj im = sp_mark ? z_comp((ZObj)sp_mark, zk_image) : NULL;
    static void *m_spr, *m_col;
    if (!m_spr) { m_spr = zmi_sig(zk_image, "set_sprite", 1, NULL); m_col = zmi_sig(H->find_class("UnityEngine.UI", "Graphic"), "set_color", 1, NULL); }
    if (im && m_spr && m_col) {
        ZCALL(m_spr, void, ZObj, ZObj)(im, NULL, m_spr);
        ZCALL(m_col, void, ZObj, ZC4)(im, (ZC4){ 0xD9 / 255.f, 0xBE / 255.f, 0x8F / 255.f, 1 }, m_col);   /* the kit's gold D9BE8F */
    }
}

static int grow_panels(ZObj view) {
    ZObj cip = ZAT(view, zf_view_cip, ZObj);
    ZObj arr = cip ? ZAT(cip, zf_cip_panels, ZObj) : NULL;
    int n = arr ? (int)ZAT(arr, 0x18, uintptr_t) : 0;
    if (n >= s_max_units) { if (sp_n && zh_get(sp_slot[0])) { sp_page = 0; sp_apply(); } return 0; }   /* grown (the view is pooled): page 1 at each opening */
    if (n < 2) { H->log(ME, "supplies: %d stock panels, left as is", n); return -1; }
    ZObj e[8] = { 0 }, slot[8] = { 0 };
    for (int i = 0; i < n && i < 8; i++) e[i] = ZAT(arr, 0x20 + 8 * i, ZObj);
    ZObj row = z_row_of(e[0], e[1]);
    char path[256] = "", p2[256];
    for (int i = 0; i < n; i++) slot[i] = z_slot_of(e[i], row, i == n - 1 ? path : p2, sizeof path);
    if (!row || !slot[n - 1]) { H->log(ME, "supplies: panel row not found"); return -1; }
    int sib = ZCALL(zm_sib_get, int32_t, ZObj)(slot[n - 1], zm_sib_get);
    ZObj na = ZI.array_new(zk_panel, (uintptr_t)s_max_units);
    for (int i = 0; i < n; i++) z_setref(na, 0x20 + 8 * i, e[i]);
    for (int i = n; i < s_max_units; i++) {
        slot[i] = z_copy(slot[n - 1], ++sib);
        ZObj pe = slot[i] ? z_find(slot[i], path) : NULL;
        e[i] = pe ? z_comp(pe, zk_panel) : NULL;
        if (!e[i]) { H->log(ME, "supplies: copy %d has no EntityInventoryPanelUI (path '%s')", i, path); return -1; }
        z_setref(na, 0x20 + 8 * i, e[i]);
    }
    z_setref(cip, zf_cip_panels, na);
    /* one row of all panels only while it stays big enough (5 units: 0.81); else pages of 4 at the stock size */
    ZRect rr = z_rect(row); float L0, T0, L1, T1;
    z_lt(slot[0], rr, &L0, &T0); z_lt(slot[n - 1], rr, &L1, &T1);
    float w0 = z_size(slot[0]).x * z_scale(slot[0]), dx = n > 1 ? (L1 - L0) / (float)(n - 1) : w0;
    float s1 = dx > 0 ? ((float)(n - 1) * dx + w0) / ((float)(s_max_units - 1) * dx + w0) : 0;
    int paged = n == 4 && s1 < SP_PAGE_MIN_SCALE;
    int ov = 0; char info[256];
    char rn[64]; z_name(row, rn, sizeof rn);
    if (paged) {
        z_page_slots(slot, s_max_units);
        snprintf(info, sizeof info, "pages of 4 at the stock size (one row would be %.3f)", s1);
    } else z_layout_row(slot, s_max_units, n, &ov, info, sizeof info);
    H->log(ME, "supplies: %d unit panels in %s (%d stock + %d copies), %s, %s", s_max_units, rn, n, s_max_units - n, info, ov ? "OVERLAP" : "no overlap");
    /* the empty-slot boxes behind the panels ("Not Selected", Panel_InventoriesBG: decoration, one per stock panel) */
    ZObj bgrow = z_find(z_parent(row), "Panel_InventoriesBG");
    int nb = bgrow ? ZCALL(zm_tr_count, int32_t, ZObj)(bgrow, zm_tr_count) : 0;
    ZObj bg[8] = { 0 };
    if (nb == n) {
        for (int i = 0; i < n; i++) bg[i] = ZCALL(zm_tr_child, ZObj, ZObj, int32_t)(bgrow, i, zm_tr_child);
        for (int i = n; i < s_max_units; i++) bg[i] = z_copy(bg[n - 1], i);
        if (paged) z_page_slots(bg, s_max_units);
        else {
            z_layout_row(bg, s_max_units, n, &ov, info, sizeof info);
            H->log(ME, "supplies: %d empty-slot boxes, one row: %s, %s", s_max_units, info, ov ? "OVERLAP" : "no overlap");
        }
    } else { H->log(ME, "supplies: Panel_InventoriesBG has %d boxes (not %d): left as is", nb, n); nb = 0; }
    if (paged) {
        sp_n = s_max_units; sp_nbg = nb ? s_max_units : 0; sp_page = 0;
        for (int i = 0; i < sp_n; i++) { zh_set(&sp_slot[i], slot[i]); sp_sc[i] = z_scale(slot[i]); }
        for (int i = 0; i < sp_nbg; i++) { zh_set(&sp_bg[i], bg[i]); sp_bgsc[i] = z_scale(bg[i]); }
        sp_buttons(cip);
        sp_apply();
    }
    return 0;
}

/* the selected units' portraits of the region step: images (0xB8) and their outlines (0xC0), same slots or not */
static int grow_portraits(ZObj sel) {
    ZObj imgs = ZAT(sel, zf_sel_imgs, ZObj), lines = ZAT(sel, zf_sel_lines, ZObj);
    int n = list_count(imgs);
    if (n >= s_max_units) return 0;
    if (n < 2 || list_count(lines) != n) { H->log(ME, "portraits: %d images / %d outlines, left as is", n, list_count(lines)); return -1; }
    ZObj row = z_row_of(list_item(imgs, 0), list_item(imgs, 1));
    ZObj slot[8] = { 0 };
    char pi[256] = "", pl[256] = "", tmp[256];
    for (int i = 0; i < n; i++) slot[i] = z_slot_of(list_item(imgs, i), row, i == n - 1 ? pi : tmp, sizeof pi);
    ZObj lslot = z_slot_of(list_item(lines, n - 1), row, pl, sizeof pl);   /* outline in the same slot as its image? */
    int shared = lslot && lslot == slot[n - 1];
    ZObj lrow = shared ? row : z_row_of(list_item(lines, 0), list_item(lines, 1));
    ZObj ls[8] = { 0 };
    if (!shared) for (int i = 0; i < n; i++) ls[i] = z_slot_of(list_item(lines, i), lrow, i == n - 1 ? pl : tmp, sizeof pl);
    if (!row || !slot[n - 1] || (!shared && !ls[n - 1])) { H->log(ME, "portraits: row not found"); return -1; }
    int sib = ZCALL(zm_sib_get, int32_t, ZObj)(slot[n - 1], zm_sib_get), lsib = shared ? 0 : ZCALL(zm_sib_get, int32_t, ZObj)(ls[n - 1], zm_sib_get);
    for (int i = n; i < s_max_units; i++) {
        slot[i] = z_copy(slot[n - 1], ++sib);
        if (!shared) { ls[i] = z_copy(ls[n - 1], ++lsib); if (lrow == row) sib++; }
        ZObj im = slot[i] ? z_comp(z_find(slot[i], pi), zk_image) : NULL;
        ZObj ln = shared ? z_comp(z_find(slot[i], pl), zk_image) : (ls[i] ? z_comp(z_find(ls[i], pl), zk_image) : NULL);
        if (!im || !ln) { H->log(ME, "portraits: copy %d incomplete (image '%s' %p, outline '%s' %p)", i, pi, im, pl, ln); return -1; }
        list_add(imgs, im); list_add(lines, ln);
    }
    int ov, ov2 = 0; char info[256], info2[256] = "";
    z_layout_row(slot, s_max_units, n, &ov, info, sizeof info);
    if (!shared) z_layout_row(ls, s_max_units, n, &ov2, info2, sizeof info2);
    H->log(ME, "portraits: %d (%d stock + %d copies), outlines %s; one row: %s%s%s, %s", list_count(imgs), n, s_max_units - n,
           shared ? "in the same slots" : "own slots", info, shared ? "" : "; outlines: ", info2, ov || ov2 ? "OVERLAP" : "no overlap");
    return 0;
}

static uintptr_t s_sel_h;                                       /* the count text of the region step (gc handle) */
typedef void (*plan_init_fn)(ZObj, ZObj, ZObj, ZObj, ZObj, ZObj, ZObj, ZObj, ZObj, const void *);
static plan_init_fn o_plan_init;                                /* Init(8 Actions): the view's callbacks */
static void my_plan_init(ZObj view, ZObj a, ZObj b, ZObj c, ZObj d, ZObj e, ZObj f, ZObj g, ZObj h, const void *mi) {
    grow_panels(view);                                          /* copies first: the stock Init wires every panel */
    ZObj sel = ZAT(view, zf_view_select, ZObj);
    if (sel) grow_portraits(sel);
    ZObj tx = sel ? ZAT(sel, zf_sel_text, ZObj) : NULL;         /* one view per game (pooled): its count text */
    uintptr_t (*hn)(ZObj, int) = H->il2cpp_fn("il2cpp_gchandle_new");
    void (*hf)(uintptr_t) = H->il2cpp_fn("il2cpp_gchandle_free");
    if (s_sel_h && hf) hf(s_sel_h);
    s_sel_h = tx && hn ? hn(tx, 0) : 0;
    o_plan_init(view, a, b, c, d, e, f, g, h, mi);
}

/* "(n/4)" -> "(n/N)" in the count text of the region step, after every SetUnit (LowerPanel.OnSetUnit follows each) */
static void fix_count_text(void) {
    ZObj (*hget)(uintptr_t) = H->il2cpp_fn("il2cpp_gchandle_get_target");
    ZObj tx = s_sel_h && hget ? hget(s_sel_h) : NULL;
    if (!z_isa(tx, zk_tmp)) { static int nt; if (nt++ < 3) H->log(ME, "count text: not found (handle %d)", s_sel_h != 0); return; }
    void *g = ZI.object_get_virtual_method(tx, zm_tmp_get), *st = ZI.object_get_virtual_method(tx, zm_tmp_set);
    ZObj s = ZCALL(g, ZObj, ZObj)(tx, g);
    int32_t n = s ? ZAT(s, 0x10, int32_t) : 0;
    if (n < 3 || n > 500) return;
    uint16_t buf[512]; memcpy(buf, (char *)s + 0x14, (size_t)n * 2);
    for (int32_t i = n - 3; i >= 0; i--)
        if (buf[i] == '/' && buf[i + 1] == '0' + party_stock && buf[i + 2] == ')') {
            buf[i + 1] = (uint16_t)('0' + s_max_units);
            ZCALL(st, void, ZObj, ZObj)(tx, ZI.string_new_utf16(buf, n), st);
            char a[96]; int k = 0;
            for (int32_t j = 0; j < n && k < 95; j++) a[k++] = buf[j] < 0x80 ? (char)buf[j] : '?';
            a[k] = 0;
            static int said; if (said++ < 40) H->log(ME, "count text now '%s'", a);
            return;
        }
    static int miss; if (miss++ < 3) H->log(ME, "count text: no \"/%d)\" in it (%d chars)", party_stock, n);
}
static void (*o_lower_setunit)(ZObj, int32_t, const void *);
static void my_lower_setunit(ZObj lower, int32_t count, const void *mi) {
    o_lower_setunit(lower, count, mi);
    fix_count_text();
    static int said; if (said++ < 40) H->log(ME, "expedition: %d selected", count);
}
/* ------------------------------------------------------------------------------------------------ result screen */
/* SetPlayerData(List<UnitEntity> units, List<int> transferList, List<UnitEntity> deadUnits, List<string> missingKeys) */
static void (*o_result_set)(ZObj, ZObj, ZObj, ZObj, ZObj, const void *);
static void grow_result(ZObj view) {
    ZObj l = ZAT(view, zf_res_list, ZObj);
    int n = list_count(l);
    if (n >= s_max_units || n < 2) return;
    ZObj row = z_row_of(list_item(l, 0), list_item(l, 1));
    ZObj slot[8] = { 0 }; char path[256] = "", tmp[256];
    for (int i = 0; i < n; i++) slot[i] = z_slot_of(list_item(l, i), row, i == n - 1 ? path : tmp, sizeof path);
    if (!row || !slot[n - 1]) { H->log(ME, "result: portrait row not found"); return; }
    int sib = ZCALL(zm_sib_get, int32_t, ZObj)(slot[n - 1], zm_sib_get);
    for (int i = n; i < s_max_units; i++) {
        slot[i] = z_copy(slot[n - 1], ++sib);
        ZObj p = slot[i] ? z_comp(z_find(slot[i], path), zk_result_portrait) : NULL;
        if (!p) { H->log(ME, "result: copy %d has no SubUI_DungeonResultPortrait", i); return; }
        list_add(l, p);
    }
    H->log(ME, "result: %d portraits (%d stock + %d copies)", list_count(l), n, s_max_units - n);
}
/* 5..8 shown: the grid's first line (the game's own places) for portraits 0..3, the rest one line below (centred
   under it), the grid off so it keeps them there; the window (the grid's parent box) grows by that line: its centre-
   anchored children (count text bar, the portraits) move up half a line like its top-anchored ones (title, count,
   (i), X), its bottom-anchored ones (region picture, buttons) go down half a line with the box's bottom edge. */
static uintptr_t s_res_box_h;
static float s_res_grown;
static void grow_box(ZObj box, float d) {
    ZV2 sz = z_size(box);
    ZCALL(zm_size_set, void, ZObj, ZV2)(box, (ZV2){ sz.x, sz.y + d }, zm_size_set);
    int nc = ZCALL(zm_tr_count, int32_t, ZObj)(box, zm_tr_count);
    for (int i = 0; i < nc; i++) {
        ZObj c = ZCALL(zm_tr_child, ZObj, ZObj, int32_t)(box, i, zm_tr_child);
        ZV2 a = z_amin(c), b = z_amax(c);
        if (a.y == 0.5f && b.y == 0.5f) { ZV2 p = z_pos(c); z_setpos(c, (ZV2){ p.x, p.y + d / 2 }); }
    }
}
static void layout_result(ZObj view, int used) {
    ZObj l = ZAT(view, zf_res_list, ZObj);
    int n = list_count(l);
    ZObj s0 = n > 1 ? z_tr(list_item(l, 0)) : NULL, s1 = n > 1 ? z_tr(list_item(l, 1)) : NULL, cs = z_parent(s0), box = z_parent(cs);
    ZObj grid = z_comp(cs, zk_grid);
    if (!s0 || !s1 || !grid || !box) return;
    ZObj gbox = zh_get(s_res_box_h);
    if (gbox != box) { zh_set(&s_res_box_h, box); s_res_grown = 0; }   /* a new window */
    int two = used > party_stock && used <= 8;
    z_enable(grid, 1);                                          /* the game's own line first: the stock 4 alone */
    if (two) for (int i = 4; i < n; i++) { ZObj g = z_go(list_item(l, i)); if (g) ZCALL(zm_go_active, void, ZObj, uint8_t)(g, 0, zm_go_active); }
    ZCALL(zm_rebuild, void, ZObj)(cs, zm_rebuild);
    if (two) for (int i = 4; i < n && i < used; i++) { ZObj g = z_go(list_item(l, i)); if (g) ZCALL(zm_go_active, void, ZObj, uint8_t)(g, 1, zm_go_active); }
    float d = 0;
    if (two) {
        ZV2 p0 = z_pos(s0), p1 = z_pos(s1);
        float dx = p1.x - p0.x;
        ZObj bg = z_find(s0, "Img_Background");
        ZV2 bs = bg ? z_size(bg) : (ZV2){ 0, 0 };
        float dy = bs.y > 0 && dx > bs.x ? bs.y + (dx - bs.x) : 150;   /* the portrait's frame + the line's own gap */
        if (dx > 0) {
            int m = used - 4;
            z_enable(grid, 0);
            for (int i = 0; i < n; i++) {
                ZObj t = z_tr(list_item(l, i));
                if (i < 4) z_setpos(t, (ZV2){ p0.x + (float)i * dx, p0.y });
                else z_setpos(t, (ZV2){ p0.x + (float)(i - 4) * dx + (float)(4 - m) * dx / 2, p0.y - dy });
            }
            d = dy * z_scale(cs);
        }
    }
    if (d != s_res_grown) { grow_box(box, d - s_res_grown); s_res_grown = d; }
    ZV2 bsz = z_size(box);
    H->log(ME, "result: %d shown, %s, window %.0f high", used, d > 0 ? "two lines" : "the game's line", bsz.y);
}
static void my_result_set(ZObj view, ZObj units, ZObj idx, ZObj dead, ZObj keys, const void *mi) {
    grow_result(view);
    o_result_set(view, units, idx, dead, keys, mi);
    ZObj l = ZAT(view, zf_res_list, ZObj); int shown = 0, used = list_count(units) + list_count(dead);
    for (int i = party_stock; i < list_count(l); i++) {         /* the copies: shown for the units the stock loop filled */
        ZObj g = z_go(list_item(l, i));
        if (g) ZCALL(zm_go_active, void, ZObj, uint8_t)(g, i < used, zm_go_active);
    }
    for (int i = 0; i < list_count(l); i++) shown += z_active(list_item(l, i));
    layout_result(view, shown);
    H->log(ME, "result: %d units + %d dead, %d portraits shown", list_count(units), list_count(dead), shown);
}

/* ------------------------------------------------------------------------- unit windows: Inventory, Status, Skill
   Each view holds 4 panels (EntityInventoryUI / EntityStatusUI / EntitySkillUI._panels) in a HorizontalLayoutGroup row;
   its presenter fills panel i with selected unit i, up to PlayerPartyMax, and blanks the rest. The panel copies go into
   _panels before the view's Init (Init / InitUI wire every panel of the array); the layout group places them. After
   each refresh the row is fitted, starting from the game's own layout each time (the row back at its prefab place
   and scale: the game, and the multi-bar mod with 2..4 units, move the whole window, so the stock corner is measured
   again, never kept):
   - up to 4 shown: the game's row; scaled down (only) so its right edge stays inside the screen with the stock margin;
   - 5..8 shown: never over the pause / speed row at the top (CampaignInfoView's clock row: at UI Scale 1.4 the
     game's own top is above it) nor over the alert list at the right (AlertUIView's rows). One row while its scale
     stays >= 0.75; else two rows (4 + the rest) from the game's top, as large as the screen allows down to its bottom
     edge: right of the bottom-left unit card (its Status / Skill / Inventory buttons stay free) when that keeps them
     at >= 0.7 (UI Scale 1.0), else from the game's left edge over the card (UI Scale 1.4: no room beside it). */
#define UW_ONE_ROW_MIN 0.75f
#define UW_TWO_ROW_MIN 0.7f
typedef struct { const char *view, *panel; void *k_panel; int32_t f_panels; uintptr_t h_view; ZV2 row_pos0; int have_pos0; char last[224]; } zwin;
static zwin zw[3] = { { .view = "EntityInventoryUI", .panel = "EntityInventoryPanelUI" },
                      { .view = "EntityStatusUI", .panel = "EntityStatusPanelUI" },
                      { .view = "EntitySkillUI", .panel = "EntitySkillPanelUI" } };
static void *zk_v3, *zm_corners, *zm_lossy, *zm_screen_w, *zm_screen_h, *zk_hlg, *zk_alert_view, *zk_info_view, *zk_summary_view;
static uintptr_t zh_alert, zh_clock, zh_card;                   /* the alert rows' column, the pause / speed row, the unit card */
static void z_corners(ZObj rt, ZV3 out[4]) {                   /* world corners = screen pixels (overlay canvas) */
    ZObj a = ZI.array_new(zk_v3, 4);
    ZCALL(zm_corners, void, ZObj, ZObj)(rt, a, zm_corners);
    memcpy(out, (char *)a + 0x20, sizeof(ZV3) * 4);
}
static void grow_window(int w, ZObj view) {
    zwin *z = &zw[w];
    uintptr_t (*hn)(ZObj, int) = H->il2cpp_fn("il2cpp_gchandle_new");
    ZObj (*hget)(uintptr_t) = H->il2cpp_fn("il2cpp_gchandle_get_target");
    void (*hf)(uintptr_t) = H->il2cpp_fn("il2cpp_gchandle_free");
    ZObj arr = ZAT(view, z->f_panels, ZObj);
    int n = arr ? (int)ZAT(arr, 0x18, uintptr_t) : 0;
    if (hn && hget && hf && (!z->h_view || hget(z->h_view) != view)) {   /* a new view (one per map load) */
        if (z->h_view) hf(z->h_view);
        z->h_view = hn(view, 0); z->have_pos0 = 0; z->last[0] = 0;
        ZObj row = n > 0 ? z_parent(z_tr(ZAT(arr, 0x20, ZObj))) : NULL;
        if (row) { z->row_pos0 = z_pos(row); z->have_pos0 = 1; }   /* the prefab's place (nothing moved it yet) */
    }
    if (n >= s_max_units) return;                               /* grown (InitUI runs more than once) */
    if (n < 2) { H->log(ME, "%s: %d panels, left as is", z->view, n); return; }
    ZObj last = z_tr(ZAT(arr, 0x20 + 8 * (n - 1), ZObj)), row = z_parent(last);
    if (!row || z_parent(z_tr(ZAT(arr, 0x20, ZObj))) != row) { H->log(ME, "%s: panels not in one row, left as is", z->view); return; }
    int sib = ZCALL(zm_sib_get, int32_t, ZObj)(last, zm_sib_get);
    ZObj na = ZI.array_new(z->k_panel, (uintptr_t)s_max_units);
    for (int i = 0; i < n; i++) z_setref(na, 0x20 + 8 * i, ZAT(arr, 0x20 + 8 * i, ZObj));
    for (int i = n; i < s_max_units; i++) {
        ZObj c = z_copy(last, ++sib), p = c ? z_comp(c, z->k_panel) : NULL;
        if (!p) { H->log(ME, "%s: copy %d has no %s", z->view, i, z->panel); return; }
        z_setref(na, 0x20 + 8 * i, p);
    }
    z_setref(view, z->f_panels, na);
    H->log(ME, "%s: %d panels (%d stock + %d copies)", z->view, s_max_units, n, s_max_units - n);
}
/* a HUD part of the live view of class k, found again when the view was made again (map load) */
static ZObj hud_part(uintptr_t *h, void *k, const char *path) {
    ZObj t = zh_get(*h);
    if (!t) {
        ZObj v = z_find_live(k);
        t = v ? z_find(z_tr(v), path) : NULL;
        zh_set(h, t);
    }
    return t && z_active(t) ? t : NULL;
}
static void fit_window(int w) {
    zwin *z = &zw[w];
    ZObj (*hget)(uintptr_t) = H->il2cpp_fn("il2cpp_gchandle_get_target");
    ZObj view = z->h_view && hget ? hget(z->h_view) : NULL;
    ZObj arr = z_alive(view) ? ZAT(view, z->f_panels, ZObj) : NULL;
    int n = arr ? (int)ZAT(arr, 0x18, uintptr_t) : 0, shown = 0;
    if (n < 2 || n > 8) return;
    ZObj pn[8];
    for (int i = 0; i < n; i++) { pn[i] = z_tr(ZAT(arr, 0x20 + 8 * i, ZObj)); shown += z_active(pn[i]); }
    ZObj p0 = pn[0], row = z_parent(p0), hlg = z_comp(row, zk_hlg);
    if (!row || !shown) return;
    /* the game's own layout: layout group on, row at its prefab place and scale */
    if (hlg) z_enable(hlg, 1);
    z_setscale(row, 1);
    if (z->have_pos0) z_setpos(row, z->row_pos0);
    ZCALL(zm_rebuild, void, ZObj)(row, zm_rebuild);
    ZV3 c0[4], c1[4];
    z_corners(p0, c0);
    float tl_x = c0[1].x, top = c0[1].y, pw = c0[2].x - c0[0].x, ph = c0[1].y - c0[0].y, dx = pw;
    if (shown > 1) { z_corners(pn[1], c1); dx = c1[0].x - c0[0].x; }
    if (!(pw > 0) || !(ph > 0) || !(dx >= pw)) return;
    float sw = (float)((int32_t (*)(const void *))*(void **)zm_screen_w)(zm_screen_w);
    float margin = tl_x > 8 ? tl_x : 8, gap = dx - pw, right = sw - margin, bottom = 4;
    int rows = 1, cols = shown;
    float s, w1 = (float)shown * dx - gap;
    char why[128] = "";
    if (shown <= party_stock) {                                 /* the game's 4: only kept inside the screen */
        s = w1 > right - tl_x ? (right - tl_x) / w1 : 1;
    } else {
        ZObj al = hud_part(&zh_alert, zk_alert_view, "ViewPort/AlertContent"), ck = hud_part(&zh_clock, zk_info_view, "SubUI_CampaignClockUI");
        ZV3 c[4];
        if (al) { z_corners(al, c); if (c[0].x - margin < right) { right = c[0].x - margin; snprintf(why, sizeof why, ", left of the alerts"); } }
        if (ck) { z_corners(ck, c); if (top > c[0].y - 2) { top = c[0].y - 2; strncat(why, ", under the pause / speed row", sizeof why - strlen(why) - 1); } }
        float s1 = (right - tl_x) / w1, sh1 = (top - bottom) / ph;
        if (sh1 < s1) s1 = sh1;
        if (s1 > 1) s1 = 1;
        int c2 = (shown + 1) / 2;
        float w2 = (float)c2 * dx - gap, sh2 = (top - bottom) / (2 * ph + gap);
        float s2 = (right - tl_x) / w2; if (sh2 < s2) s2 = sh2; if (s2 > 1) s2 = 1;
        ZObj cd = hud_part(&zh_card, zk_summary_view, "Panel");
        float lc = 0, sc2 = 0;                                  /* beside the unit card */
        if (cd) { z_corners(cd, c); lc = c[2].x + margin; sc2 = (right - lc) / w2; if (sh2 < sc2) sc2 = sh2; if (sc2 > 1) sc2 = 1; }
        if (s1 >= UW_ONE_ROW_MIN || (s1 >= s2 && s1 >= sc2)) s = s1;
        else if (cd && sc2 >= UW_TWO_ROW_MIN) { s = sc2; rows = 2; cols = c2; tl_x = lc; strncat(why, ", right of the unit card", sizeof why - strlen(why) - 1); }
        else { s = s2; rows = 2; cols = c2; }
    }
    if (rows == 2) {                                            /* two rows: the layout group off, the panels placed here */
        ZV2 lp[8], sz = z_size(p0);
        for (int c = 0; c < cols; c++) lp[c] = z_pos(pn[c]);
        float lgap = (lp[1].x - lp[0].x) - sz.x;               /* the layout group's spacing, in the row's units */
        if (hlg) z_enable(hlg, 0);
        for (int i = 0; i < n; i++) z_setpos(pn[i], (ZV2){ lp[i % cols].x, lp[0].y - (float)(i / cols) * (sz.y + lgap) });
    }
    z_setscale(row, s);
    if (rows == 1) ZCALL(zm_rebuild, void, ZObj)(row, zm_rebuild);
    z_corners(p0, c0);                                          /* the top-left corner where it belongs */
    float ps = ZCALL(zm_lossy, ZV3, ZObj)(z_parent(row), zm_lossy).x;
    ZV2 pos = z_pos(row);
    if (ps > 0) z_setpos(row, (ZV2){ pos.x + (tl_x - c0[1].x) / ps, pos.y + (top - c0[1].y) / ps });
    char line[224];
    float sh = (float)((int32_t (*)(const void *))*(void **)zm_screen_h)(zm_screen_h);
    snprintf(line, sizeof line, "%d units shown, %d row%s of %d, row scale %.3f, top %.0f px%s", shown, rows, rows > 1 ? "s" : "", cols, s, sh - top, why);
    if (strcmp(line, z->last)) {
        ZV3 cl[4]; z_corners(pn[shown - 1 < n ? shown - 1 : n - 1], cl);
        H->log(ME, "%s: %s (x %.0f..%.0f, bottom %.0f of %.0fx%.0f px)", z->view, line, tl_x, rows > 1 ? tl_x + s * ((float)cols * dx - gap) : cl[2].x, sh - cl[0].y, sw, sh);
        snprintf(z->last, sizeof z->last, "%s", line);
    }
}
static void (*o_inv_init)(ZObj, ZObj, const void *);
static void my_inv_init(ZObj v, ZObj a, const void *mi) { grow_window(0, v); o_inv_init(v, a, mi); }
static void (*o_status_init)(ZObj, const void *);
static void my_status_init(ZObj v, const void *mi) { grow_window(1, v); o_status_init(v, mi); }
static void (*o_skill_init)(ZObj, ZObj, ZObj, const void *);
static void my_skill_init(ZObj v, ZObj a, ZObj b, const void *mi) { grow_window(2, v); o_skill_init(v, a, b, mi); }
/* presenters: fill the panels (now up to N), then fit the row */
static void (*o_inv_set)(ZObj, const void *);
static void my_inv_set(ZObj p, const void *mi) { o_inv_set(p, mi); fit_window(0); }
static void (*o_status_refresh)(ZObj, ZObj, const void *);
static void my_status_refresh(ZObj p, ZObj a, const void *mi) { o_status_refresh(p, a, mi); fit_window(1); }
static int32_t zf_skill_idx = -1;
static void (*o_skill_refresh)(ZObj, ZObj, const void *);
static void my_skill_refresh(ZObj p, ZObj a, const void *mi) {
    /* the presenter's int[] _skillTreeIndices (selected tree per panel) is sized by PlayerPartyMax in its ctor */
    ZObj ia = ZAT(p, zf_skill_idx, ZObj);
    int n = ia ? (int)ZAT(ia, 0x18, uintptr_t) : 0;
    if (n < s_max_units) {
        ZObj na = ZI.array_new(H->find_class("System", "Int32"), (uintptr_t)s_max_units);
        if (n) memcpy((char *)na + 0x20, (char *)ia + 0x20, (size_t)n * 4);
        z_setref(p, zf_skill_idx, na);
        H->log(ME, "EntitySkillUIPresenter: skill tree indices %d -> %d", n, s_max_units);
    }
    o_skill_refresh(p, a, mi);
    fit_window(2);
}

static int zetu_ui_init(void) {
#define ZFN(f, n) *(void **)&ZI.f = H->il2cpp_fn(n)
    ZFN(class_get_methods, "il2cpp_class_get_methods"); ZFN(method_get_name, "il2cpp_method_get_name");
    ZFN(method_get_param_count, "il2cpp_method_get_param_count"); ZFN(method_get_param, "il2cpp_method_get_param");
    ZFN(type_get_name, "il2cpp_type_get_name"); ZFN(il_free, "il2cpp_free"); ZFN(class_get_parent, "il2cpp_class_get_parent");
    ZFN(object_get_class, "il2cpp_object_get_class"); ZFN(class_is_assignable_from, "il2cpp_class_is_assignable_from");
    ZFN(class_get_type, "il2cpp_class_get_type"); ZFN(type_get_object, "il2cpp_type_get_object"); ZFN(array_new, "il2cpp_array_new");
    ZFN(string_new_utf16, "il2cpp_string_new_utf16"); ZFN(object_get_virtual_method, "il2cpp_object_get_virtual_method");
    ZFN(wbarrier, "il2cpp_gc_wbarrier_set_field");
#undef ZFN
    if (!ZI.class_get_methods || !ZI.method_get_name || !ZI.method_get_param_count || !ZI.method_get_param || !ZI.type_get_name
        || !ZI.class_get_parent || !ZI.object_get_class || !ZI.class_is_assignable_from || !ZI.class_get_type || !ZI.type_get_object
        || !ZI.array_new || !ZI.string_new_utf16 || !ZI.object_get_virtual_method) { H->log(ME, "expedition UI: il2cpp exports missing"); return 20; }
    zk_object = H->find_class("UnityEngine", "Object"); zk_component = H->find_class("UnityEngine", "Component");
    zk_transform = H->find_class("UnityEngine", "Transform"); zk_rect = H->find_class("UnityEngine", "RectTransform");
    zk_tmp = H->find_class("TMPro", "TMP_Text"); zk_image = H->find_class("UnityEngine.UI", "Image");
    const char *ui = "Refactor.UI";
    zk_panel = H->find_class(ui, "EntityInventoryPanelUI"); zk_result_portrait = H->find_class(ui, "SubUI_DungeonResultPortrait");
    void *k_view = H->find_class(ui, "ExpeditionPlanView"), *k_cip = H->find_class(ui, "SubUI_CheckInventoryPanelUI");
    void *k_sel = H->find_class(ui, "SubUI_SelectRegionUI"), *k_res = H->find_class(ui, "DungeonResultView");
    zm_implicit = zmi_sig(zk_object, "op_Implicit", 1, NULL);
    zm_inst = zmi_sig(zk_object, "Instantiate", 2, "UnityEngine.Object,UnityEngine.Transform");
    zm_name = zmi_sig(zk_object, "get_name", 0, NULL);
    zm_get_tr = zmi_sig(zk_component, "get_transform", 0, NULL); zm_get_go = zmi_sig(zk_component, "get_gameObject", 0, NULL);
    zm_get_comp = zmi_sig(zk_component, "GetComponent", 1, "System.Type");
    zm_go_active = zmi_sig(H->find_class("UnityEngine", "GameObject"), "SetActive", 1, NULL);
    zm_go_self = zmi_sig(H->find_class("UnityEngine", "GameObject"), "get_activeSelf", 0, NULL);
    zm_tr_parent = zmi_sig(zk_transform, "get_parent", 0, NULL); zm_tr_find = zmi_sig(zk_transform, "Find", 1, "System.String");
    zm_tr_count = zmi_sig(zk_transform, "get_childCount", 0, NULL); zm_tr_child = zmi_sig(zk_transform, "GetChild", 1, NULL);
    zm_sib_get = zmi_sig(zk_transform, "GetSiblingIndex", 0, NULL); zm_sib_set = zmi_sig(zk_transform, "SetSiblingIndex", 1, NULL);
    zm_scale_get = zmi_sig(zk_transform, "get_localScale", 0, NULL); zm_scale_set = zmi_sig(zk_transform, "set_localScale", 1, NULL);
    zm_pos_get = zmi_sig(zk_rect, "get_anchoredPosition", 0, NULL); zm_pos_set = zmi_sig(zk_rect, "set_anchoredPosition", 1, NULL);
    zm_size_get = zmi_sig(zk_rect, "get_sizeDelta", 0, NULL); zm_pivot_get = zmi_sig(zk_rect, "get_pivot", 0, NULL);
    zk_grid = H->find_class("UnityEngine.UI", "GridLayoutGroup");
    zm_rect = zmi_sig(zk_rect, "get_rect", 0, NULL); zm_size_set = zmi_sig(zk_rect, "set_sizeDelta", 1, NULL);
    zm_amin_get = zmi_sig(zk_rect, "get_anchorMin", 0, NULL); zm_amax_get = zmi_sig(zk_rect, "get_anchorMax", 0, NULL);
    zm_enabled_set = zmi_sig(H->find_class("UnityEngine", "Behaviour"), "set_enabled", 1, NULL);
    zm_name_set = zmi_sig(zk_object, "set_name", 1, NULL);
    zm_find_type = zmi_sig(zk_object, "FindObjectOfType", 1, "System.Type");
    zm_rebuild = zmi_sig(H->find_class("UnityEngine.UI", "LayoutRebuilder"), "ForceRebuildLayoutImmediate", 1, NULL);
    zm_tmp_get = zmi_sig(zk_tmp, "get_text", 0, NULL); zm_tmp_set = zmi_sig(zk_tmp, "set_text", 1, NULL);
    zf_view_select = k_view ? H->field_offset(k_view, "_selectRegionUI") : -1;
    zf_view_cip = k_view ? H->field_offset(k_view, "_checkInventoryPanelUI") : -1;
    zf_cip_panels = k_cip ? H->field_offset(k_cip, "_panels") : -1;
    zf_sel_text = k_sel ? H->field_offset(k_sel, "_txtTransferUnitInfo") : -1;
    zf_sel_imgs = k_sel ? H->field_offset(k_sel, "_bottomtransferUnitImages") : -1;
    zf_sel_lines = k_sel ? H->field_offset(k_sel, "_bottomtransferUnitSelectedOutlines") : -1;
    zf_res_list = k_res ? H->field_offset(k_res, "_portraitUIList") : -1;
    void *ms[] = { zm_implicit, zm_inst, zm_name, zm_get_tr, zm_get_go, zm_get_comp, zm_go_active, zm_go_self, zm_tr_parent, zm_tr_find,
                   zm_sib_get, zm_sib_set, zm_scale_get, zm_scale_set, zm_pos_get, zm_pos_set, zm_size_get, zm_pivot_get, zm_tmp_get, zm_tmp_set, zm_rebuild, zm_rect, zm_size_set, zm_amin_get, zm_tr_count, zm_tr_child,
                   zm_amax_get, zm_enabled_set, zm_name_set, zm_find_type };
    for (size_t i = 0; i < sizeof ms / sizeof *ms; i++)
        if (!ms[i]) { H->log(ME, "expedition UI: Unity method #%zu not found", i); return 21; }
    if (!zk_grid || !zk_panel || !zk_result_portrait || !zk_image || !zk_tmp || zf_view_select < 0 || zf_view_cip < 0 || zf_cip_panels < 0
        || zf_sel_text < 0 || zf_sel_imgs < 0 || zf_sel_lines < 0 || zf_res_list < 0) {
        H->log(ME, "expedition UI: classes / fields missing (%d %d %d %d %d %d %d)", zf_view_select, zf_view_cip, zf_cip_panels,
               zf_sel_text, zf_sel_imgs, zf_sel_lines, zf_res_list);
        return 22;
    }
    zk_v3 = H->find_class("UnityEngine", "Vector3");
    zm_corners = zmi_sig(zk_rect, "GetWorldCorners", 1, NULL); zm_lossy = zmi_sig(zk_transform, "get_lossyScale", 0, NULL);
    zm_screen_w = zmi_sig(H->find_class("UnityEngine", "Screen"), "get_width", 0, NULL);
    zm_screen_h = zmi_sig(H->find_class("UnityEngine", "Screen"), "get_height", 0, NULL);
    zk_hlg = H->find_class("UnityEngine.UI", "HorizontalLayoutGroup");
    zk_alert_view = H->find_class(ui, "AlertUIView"); zk_info_view = H->find_class(ui, "CampaignInfoView");   /* missing: no limit */
    zk_summary_view = H->find_class(ui, "EntitySummaryUIView");
    for (int i = 0; i < 3; i++) {
        void *kv = H->find_class(ui, zw[i].view);
        zw[i].k_panel = H->find_class(ui, zw[i].panel);
        zw[i].f_panels = kv ? H->field_offset(kv, "_panels") : -1;
        if (!zw[i].k_panel || zw[i].f_panels < 0) { H->log(ME, "unit windows: %s / %s missing", zw[i].view, zw[i].panel); return 24; }
    }
    void *k_skp = H->find_class(ui, "EntitySkillUIPresenter");
    zf_skill_idx = k_skp ? H->field_offset(k_skp, "_skillTreeIndices") : -1;
    if (!zk_v3 || !zm_corners || !zm_lossy || !zm_screen_w || !zm_screen_h || !zk_hlg || zf_skill_idx < 0) { H->log(ME, "unit windows: Unity methods / fields missing"); return 25; }
    if (hook_named(ui, "EntityInventoryUI", "Init", 1, (void *)my_inv_init, (void **)&o_inv_init)
        || hook_named(ui, "EntityStatusUI", "Init", 0, (void *)my_status_init, (void **)&o_status_init)
        || hook_named(ui, "EntitySkillUI", "InitUI", 2, (void *)my_skill_init, (void **)&o_skill_init)
        || hook_named(ui, "EntityInventoryPresenter", "SetUnitUI", 0, (void *)my_inv_set, (void **)&o_inv_set)
        || hook_named(ui, "EntityStatusUIPresenter", "RefreshSelectedEntityUI", 1, (void *)my_status_refresh, (void **)&o_status_refresh)
        || hook_named(ui, "EntitySkillUIPresenter", "RefreshSelectedEntityUI", 1, (void *)my_skill_refresh, (void **)&o_skill_refresh))
        return 26;
    if (hook_named(ui, "ExpeditionPlanView", "Init", 8, (void *)my_plan_init, (void **)&o_plan_init)
        || hook_named(ui, "SubUI_ExpeditionPlanLowerPanel", "OnSetUnit", 1, (void *)my_lower_setunit, (void **)&o_lower_setunit)
        || hook_named(ui, "DungeonResultView", "SetPlayerData", 4, (void *)my_result_set, (void **)&o_result_set))
        return 23;
    H->log(ME, "expedition UI: %d unit panels, portraits and result portraits; count text /%d; unit windows %d panels", s_max_units, s_max_units, s_max_units);
    return 0;
}
