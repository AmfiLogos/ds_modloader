// zetu-suite, the screens of an expedition with more than 4 units (included by zetu.c: one compile unit per module).
// Only runs when max_units != 4 (zetu_ui_init from party_init). Every class, method and field by il2cpp name.
//
// The stock prefabs hold 4 of each per-unit element, placed by hand (the result screen: a grid); the game's loops stop at the
// list / array size, so without this units 5..N are simply missing (no crash). Here the 4th element is copied until
// there are N, the copies go into the game's own list / array (so the stock code fills, shows and wires them like
// the first 4), and the row is laid out again at a smaller scale inside the width the 4 stock elements used:
//   - supplies step: SubUI_CheckInventoryPanelUI._panels (EntityInventoryPanelUI[]), before ExpeditionPlanView.Init
//     (Init wires the callbacks of every panel of the array, by index), and the empty-slot boxes behind them;
//   - region step, the selected units' portraits: SubUI_SelectRegionUI._bottomtransferUnitImages and
//     _bottomtransferUnitSelectedOutlines (List<Image>), same time;
//   - region step, the count "Expedition Members (n/4)": the "/4" is a literal of SubUI_SelectRegionUI.SetUnit (and
//     " (0/4)" of InitOnActivate; the multiplayer module hooks both), and every call of SetUnit is followed by
//     SubUI_ExpeditionPlanLowerPanel.OnSetUnit (InitOnActivate too, before its SetUnit), so the text is rewritten
//     to "/N" after that;
//   - result screen: DungeonResultView._portraitUIList (List<SubUI_DungeonResultPortrait>), before SetPlayerData; its
//     GridLayoutGroup row is made one line long and scaled (the view is made again for each opening).
// Proof on a hidden test copy: test/zetu_ui_test.py (8 units: expedition, supplies, dungeon, windows, result).
// The Inventory / Status / Skill windows keep the stock 4 (zetu.c: party_stock_enter around them): with 8 selected
// they show the first 4, as before.

#include <stdio.h>

typedef struct { float x, y; } ZV2;
typedef struct { float x, y, z; } ZV3;
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
    int (*class_is_assignable_from)(void *, void *);
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

/* ---------------------------------------------------------------------------------------- supplies + portraits */
static int list_count(ZObj l) { return l ? ZAT(l, 0x18, int32_t) : 0; }
static ZObj list_item(ZObj l, int i) { ZObj a = l ? ZAT(l, 0x10, ZObj) : NULL; return a && i < list_count(l) ? ZAT(a, 0x20 + 8 * i, ZObj) : NULL; }
static void list_add(ZObj l, ZObj v) {                          /* the list's own Add (its class: List<T> of the game) */
    void *m = H->find_method(ZI.object_get_class(l), "Add", 1);
    if (m) ZCALL(m, void, ZObj, ZObj)(l, v, m);
}

static int grow_panels(ZObj view) {
    ZObj cip = ZAT(view, zf_view_cip, ZObj);
    ZObj arr = cip ? ZAT(cip, zf_cip_panels, ZObj) : NULL;
    int n = arr ? (int)ZAT(arr, 0x18, uintptr_t) : 0;
    if (n >= s_max_units) return 0;                             /* already grown (the view is pooled) */
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
    int ov; char info[256];
    z_layout_row(slot, s_max_units, n, &ov, info, sizeof info);
    char rn[64]; z_name(row, rn, sizeof rn);
    H->log(ME, "supplies: %d unit panels in %s (%d stock + %d copies), one row: %s, %s", s_max_units, rn, n, s_max_units - n, info, ov ? "OVERLAP" : "no overlap");
    /* the empty-slot boxes behind the panels ("Not Selected", Panel_InventoriesBG: decoration, one per stock panel) */
    ZObj bgrow = z_find(z_parent(row), "Panel_InventoriesBG");
    int nb = bgrow ? ZCALL(zm_tr_count, int32_t, ZObj)(bgrow, zm_tr_count) : 0;
    if (nb == n) {
        ZObj bg[8];
        for (int i = 0; i < n; i++) bg[i] = ZCALL(zm_tr_child, ZObj, ZObj, int32_t)(bgrow, i, zm_tr_child);
        for (int i = n; i < s_max_units; i++) bg[i] = z_copy(bg[n - 1], i);
        z_layout_row(bg, s_max_units, n, &ov, info, sizeof info);
        H->log(ME, "supplies: %d empty-slot boxes, one row: %s, %s", s_max_units, info, ov ? "OVERLAP" : "no overlap");
    } else H->log(ME, "supplies: Panel_InventoriesBG has %d boxes (not %d): left as is", nb, n);
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
    int ov; char info[256];
    z_layout_row(slot, s_max_units, n, &ov, info, sizeof info);
    H->log(ME, "result: %d portraits (%d stock + %d copies), one row: %s, %s", list_count(l), n, s_max_units - n, info, ov ? "OVERLAP" : "no overlap");
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
    H->log(ME, "result: %d units + %d dead, %d portraits shown", list_count(units), list_count(dead), shown);
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
    zm_amin_get = zmi_sig(zk_rect, "get_anchorMin", 0, NULL);
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
                   zm_sib_get, zm_sib_set, zm_scale_get, zm_scale_set, zm_pos_get, zm_pos_set, zm_size_get, zm_pivot_get, zm_tmp_get, zm_tmp_set, zm_rebuild, zm_rect, zm_size_set, zm_amin_get, zm_tr_count, zm_tr_child };
    for (size_t i = 0; i < sizeof ms / sizeof *ms; i++)
        if (!ms[i]) { H->log(ME, "expedition UI: Unity method #%zu not found", i); return 21; }
    if (!zk_grid || !zk_panel || !zk_result_portrait || !zk_image || !zk_tmp || zf_view_select < 0 || zf_view_cip < 0 || zf_cip_panels < 0
        || zf_sel_text < 0 || zf_sel_imgs < 0 || zf_sel_lines < 0 || zf_res_list < 0) {
        H->log(ME, "expedition UI: classes / fields missing (%d %d %d %d %d %d %d)", zf_view_select, zf_view_cip, zf_cip_panels,
               zf_sel_text, zf_sel_imgs, zf_sel_lines, zf_res_list);
        return 22;
    }
    if (hook_named(ui, "ExpeditionPlanView", "Init", 8, (void *)my_plan_init, (void **)&o_plan_init)
        || hook_named(ui, "SubUI_ExpeditionPlanLowerPanel", "OnSetUnit", 1, (void *)my_lower_setunit, (void **)&o_lower_setunit)
        || hook_named(ui, "DungeonResultView", "SetPlayerData", 4, (void *)my_result_set, (void **)&o_result_set))
        return 23;
    H->log(ME, "expedition UI: %d unit panels, portraits and result portraits; count text /%d", s_max_units, s_max_units);
    return 0;
}
