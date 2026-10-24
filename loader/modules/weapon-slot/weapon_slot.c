// Weapon skill slot (mod id weapon-slot): the skill bar gets a sixth slot, between the basic attack and the 4 free
// slots, that always holds the equipped weapon's skill (the 2nd entry of the weapon's Equipment_ItemDefaultSkills).
// It follows the weapon, the player cannot change it, no key reaches it (mouse click + the skill's own auto-use).
// Runtime hooks found by il2cpp name (Mac and Windows), GameAssembly and the game's UI bundle unchanged.
// Build 25846389 (DS_B.0.4.29). Port of the old binary patch (memory DungeonSettlers_WeaponSlotMod, 2026-10-04).
//
// Data: QuickSlotData._quickSlots grows from 4 to 5; the weapon skill is quickSlots[4] (WS). Indexes 0..3 stay the
// stock free slots, so keys (UseSkill_1..4 -> UI 1..4), the skill-tree "set slot" menu and the right-click menus of
// the free slots work unchanged. The stock code is bounded by the array length (never by MAX_SLOT = 4) except:
//   - the inlined `new string[4]` (QuickSlotData ctor in OnSpawned / container Deserialize, the save copy in
//     Serialize): arrays are grown after the stock code made them, and Serialize runs on 4-long views;
//   - the free-slot search (TryRegisterQuickSlot): runs on 4 slots; a weapon skill goes to WS instead;
//   - the Set handler's duplicate removal and every index write: WS is never written by game events (AddQuickSlotAt /
//     RemoveQuickSlotAt refuse index 4); only this module writes it (equip, unequip, load);
//   - skill reset (PlayerUnitDataContainer.ResetSkill clears every slot): WS is kept (the weapon skill stays active);
//   - unequip: WS is cleared after ProcessUnequip (its IsRegistered check still sees WS, so the skill is removed);
//   - AI auto-use (DecideSkill takes the first usable skill in slot order): it sees WS first, as in the old mod.
// Saves: 5 slots per unit. A 4-slot save (stock game, or the loader without this mod) is migrated when it loads: the
// weapon skill moves from its free slot to WS, the other skills keep their place. A save of the old binary mod
// (weapon skill at index 0) is turned into this layout. The stock game reads the first 4 slots of a 5-slot save
// (QuickSlotData.Deserialize copies Math.Min of the two lengths), so only the weapon slot is lost there.
// UI: QuickSlotUI._skillSlots gets a 6th SkillSlotUI appended (UI_W = 5 = quickSlots[4]), a runtime copy of the first
// free slot, placed between the basic attack and the free slots (owner's layout, 2026-10-04: panel 362 -> 430 px).
// Its key box is hidden, it cannot be dragged from, dropped on or right-clicked.
#include "dsl_sdk.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

DSL_PLUGIN();

static const DSLHost *H;
static DSLPlugin *ME;
typedef void *Obj;
typedef struct { uint64_t lo, hi; } WGuid;              /* System.Guid by value (16 bytes, no float) */
typedef struct { float x, y; } V2;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define CALL(mi, R, ...) ((R (*)(__VA_ARGS__, const void *))(*(void **)(mi)))
#define WS 4                                            /* the weapon slot in QuickSlotData._quickSlots */
#define NSLOT 5
#define UI_W 5                                          /* its SkillSlotUI index (UI 0 = basic attack, UI i = slot i-1) */
#define AHDR 0x20                                       /* il2cpp array: klass, monitor, bounds, max_length, items */
#define OHDR 0x10                                       /* il2cpp object header: value-type field offsets include it */

static struct {
    void *(*object_get_class)(Obj);
    void *(*class_get_parent)(void *);
    void *(*class_get_field_from_name)(void *, const char *);
    size_t (*field_get_offset)(void *);
    void *(*field_get_type)(void *);
    void *(*class_from_type)(void *);
    void *(*class_get_element_class)(void *);
    int32_t (*class_value_size)(void *, uint32_t *);
    void (*field_static_get_value)(void *, void *);
    void *(*class_get_method_from_name)(void *, const char *, int);
    void *(*class_get_methods)(void *, void **);
    const char *(*method_get_name)(void *);
    uint32_t (*method_get_param_count)(void *);
    void *(*method_get_param)(void *, uint32_t);
    char *(*type_get_name)(void *);
    void (*il_free)(void *);
    uint8_t (*class_is_assignable_from)(void *, void *);   /* C bool: low byte only (Windows) */
    Obj (*array_new)(void *, uintptr_t);
    Obj (*string_new)(const char *);
    void (*wbarrier)(Obj, void **, Obj);
    uintptr_t (*gch_weak)(Obj, uint8_t);                    /* GC handles are pointer-sized in this il2cpp */
    Obj (*gch_target)(uintptr_t);
    void (*gch_free)(uintptr_t);
} I;

/* ------------------------------------------------------------------------------------------------ small helpers */
static int logn(int *n, int max) { return ++*n <= max; }
static int32_t foff(void *k, const char *name) {        /* instance field offset, base classes included */
    for (void *c = k; c; c = I.class_get_parent(c)) {
        void *f = I.class_get_field_from_name(c, name);
        if (f) return (int32_t)I.field_get_offset(f);
    }
    return -1;
}
static void *ffield(void *k, const char *name) {
    for (void *c = k; c; c = I.class_get_parent(c)) { void *f = I.class_get_field_from_name(c, name); if (f) return f; }
    return NULL;
}
static void setref(Obj o, void *slot, Obj v) { I.wbarrier(o, (void **)slot, v); }
static uintptr_t alen(Obj a) { return a ? AT(a, 0x18, uintptr_t) : 0; }
static Obj aget(Obj a, uintptr_t i) { return ((Obj *)((char *)a + AHDR))[i]; }
static void aset(Obj a, uintptr_t i, Obj v) { setref(a, (char *)a + AHDR + i * sizeof(Obj), v); }
static int32_t slen(Obj s) { return s ? AT(s, 0x10, int32_t) : 0; }
static int str_eq(Obj a, Obj b) {
    if (a == b) return 1;
    int32_t n = slen(a);
    return a && b && n == slen(b) && !memcmp((char *)a + 0x14, (char *)b + 0x14, (size_t)n * 2);
}
static void str_c(Obj s, char *out, size_t cap) {
    size_t k = 0; int32_t n = slen(s);
    for (int32_t i = 0; s && i < n && k + 1 < cap; i++) { uint16_t c = ((uint16_t *)((char *)s + 0x14))[i]; out[k++] = c < 0x80 ? (char)c : '?'; }
    out[k] = 0;
}
static int str_is(Obj s, const char *c) {
    int32_t n = slen(s);
    if (!s || (size_t)n != strlen(c)) return 0;
    for (int32_t i = 0; i < n; i++) if (((uint16_t *)((char *)s + 0x14))[i] != (uint8_t)c[i]) return 0;
    return 1;
}
static void slots_c(Obj a, char *out, size_t cap) {     /* "[a, -, b, ...]" for the log */
    size_t o = (size_t)snprintf(out, cap, "[");
    for (uintptr_t i = 0; i < alen(a) && o < cap; i++) {
        char s[48]; Obj v = aget(a, i); str_c(v, s, sizeof s);
        o += (size_t)snprintf(out + o, cap - o, "%s%s", i ? ", " : "", slen(v) ? s : "-");
    }
    if (o < cap) snprintf(out + o, cap - o, "]");
}
static void guid_c(const void *g, char *out) { const uint8_t *b = g; for (int i = 0; i < 4; i++) sprintf(out + 2 * i, "%02x", b[3 - i]); }

/* Dictionary<K, V> (corefx layout: _entries, _count; entry {hashCode, next, key, value}, free entry hashCode < 0).
   Layout per dictionary class, from its field types; the classes this module reads are checked at init. */
typedef struct { void *k; int32_t ents, cnt, esz, hash, key, val; } DL;
static DL dls[8]; static int ndl;
static const DL *dict_lk(void *k) {
    if (!k) return NULL;
    for (int i = 0; i < ndl; i++) if (dls[i].k == k) return dls[i].esz > 0 ? &dls[i] : NULL;
    if (ndl >= 8) return NULL;
    DL *L = &dls[ndl++]; memset(L, 0, sizeof *L); L->k = k; L->esz = -1;
    void *fe = ffield(k, "_entries");
    void *ac = fe ? I.class_from_type(I.field_get_type(fe)) : NULL, *ec = ac ? I.class_get_element_class(ac) : NULL;
    L->ents = foff(k, "_entries"); L->cnt = foff(k, "_count");
    if (!ec || L->ents <= 0 || L->cnt <= 0) { H->log(ME, "dictionary layout: fields not found"); return NULL; }
    L->hash = foff(ec, "hashCode") - OHDR; L->key = foff(ec, "key") - OHDR; L->val = foff(ec, "value") - OHDR;
    L->esz = I.class_value_size(ec, NULL);              /* the entry struct (an array of them is packed) */
    if (L->hash != 0 || L->key != 8 || L->val < L->key + 8 || L->esz < L->val + 8) {
        H->log(ME, "dictionary layout unexpected (hash %d key %d value %d size %d)", L->hash, L->key, L->val, L->esz);
        L->esz = -1; return NULL;
    }
    return L;
}
static const DL *dict_l(Obj d) { return d ? dict_lk(I.object_get_class(d)) : NULL; }
static void *ftype_class(void *k, const char *name) { void *f = ffield(k, name); return f ? I.class_from_type(I.field_get_type(f)) : NULL; }
#define DICT_FOR(d, L, e)                                                                                      \
    for (Obj _ea = (L) ? AT(d, (L)->ents, Obj) : NULL; _ea; _ea = NULL)                                        \
        for (int32_t _i = 0, _n = AT(d, (L)->cnt, int32_t); _i < _n && (uintptr_t)_i < alen(_ea); _i++)       \
            for (char *e = (char *)_ea + AHDR + (size_t)_i * (size_t)(L)->esz; e; e = NULL)                    \
                if (AT(e, (L)->hash, int32_t) >= 0)

/* List<T>: _items, _size */
static int32_t f_list_items = -1, f_list_size = -1;
static int list_n(Obj l) { return l ? AT(l, f_list_size, int32_t) : 0; }
static Obj list_at(Obj l, int i) { Obj it = AT(l, f_list_items, Obj); return it && (uintptr_t)i < alen(it) ? aget(it, (uintptr_t)i) : NULL; }

/* ------------------------------------------------------------------------------------------------ game classes */
static void *k_string, *k_object;
static int32_t f_qsd_slots = -1;                        /* QuickSlotData._quickSlots */
static int32_t f_qsc_dict = -1;                         /* UnitQuickSlotContainer._unitQuickSlot (Dictionary<Guid, QuickSlotData>) */
static int32_t f_pudc_qsc = -1, f_pudc_skc = -1;        /* PlayerUnitDataContainer._unitQuickSlot / _unitSkillTrees */
static int32_t f_skc_dict = -1;                         /* UnitSkillContainer._skillTreeDatas (Dictionary<Guid, SkillTreeData>) */
static int32_t f_std_extra = -1;                        /* SkillTreeData._extraSkills (List<string>) */
static int32_t f_qssd_slots = -1;                       /* QuickSlotSaveData.QuickSlots inside the struct (0) */

static Obj qsd_of(Obj qsc, const WGuid *g) {            /* the QuickSlotData of a unit, or NULL */
    Obj d = qsc ? AT(qsc, f_qsc_dict, Obj) : NULL;
    const DL *L = dict_l(d);
    DICT_FOR(d, L, e) if (!memcmp(e + L->key, g, 16)) return AT(e, L->val, Obj);
    return NULL;
}
static Obj qsd_grow(Obj q) {                            /* the unit's array, 5 long (the stock code makes 4) */
    Obj a = q ? AT(q, f_qsd_slots, Obj) : NULL;
    if (!a || alen(a) >= NSLOT) return a;
    Obj n = I.array_new(k_string, NSLOT);
    if (!n) return a;
    for (uintptr_t i = 0; i < alen(a); i++) aset(n, i, aget(a, i));
    setref(q, &AT(q, f_qsd_slots, Obj), n);
    return n;
}
static void grow_all(Obj qsc) {
    Obj d = qsc ? AT(qsc, f_qsc_dict, Obj) : NULL;
    const DL *L = dict_l(d);
    DICT_FOR(d, L, e) qsd_grow(AT(e, L->val, Obj));
}

/* The skills that are a weapon's own skill: Equipment_ItemDefaultSkills[1] of every item (8 in this build, one per
   weapon type; none of them is in a skill tree). Read once from DataSheetManager._itemTable.tableDatas. */
#define MAXW 64
static char wsk[MAXW][64]; static int nwsk = -1;
static void weapon_skills_load(void) {
    if (nwsk >= 0) return;
    void *k_dsm = H->find_class("Refactor.Util", "DataSheetManager"), *k_itd = H->find_class("Util.Sheet", "ItemTableData");
    void *single = k_dsm ? I.class_get_parent(k_dsm) : NULL;
    void *f_inst = single ? I.class_get_field_from_name(single, "_instance") : NULL;
    Obj dsm = NULL;
    if (f_inst) I.field_static_get_value(f_inst, &dsm);
    int32_t f_it = k_dsm ? foff(k_dsm, "_itemTable") : -1, f_skills = k_itd ? foff(k_itd, "Equipment_ItemDefaultSkills") : -1;
    Obj tab = dsm && f_it > 0 ? AT(dsm, f_it, Obj) : NULL;
    int32_t f_rows = tab ? foff(I.object_get_class(tab), "tableDatas") : -1;
    Obj rows = tab && f_rows > 0 ? AT(tab, f_rows, Obj) : NULL;
    if (!rows || f_skills <= 0) { static int n; if (logn(&n, 3)) H->log(ME, "item table not ready (manager %p table %p)", dsm, tab); return; }
    nwsk = 0;
    char list[600] = ""; size_t o = 0;
    for (int i = 0; i < list_n(rows) && nwsk < MAXW; i++) {
        Obj row = list_at(rows, i), sk = row ? AT(row, f_skills, Obj) : NULL;
        Obj w = alen(sk) >= 2 ? aget(sk, 1) : NULL;
        if (!slen(w) || slen(w) >= 64 || str_is(w, "NULL")) continue;
        char c[64]; str_c(w, c, sizeof c);
        int dup = 0;
        for (int j = 0; j < nwsk; j++) if (!strcmp(wsk[j], c)) dup = 1;
        if (dup) continue;
        strcpy(wsk[nwsk++], c);
        if (o < sizeof list) o += (size_t)snprintf(list + o, sizeof list - o, "%s%s", nwsk > 1 ? " " : "", c);
    }
    H->log(ME, "weapon skills (%d items): %s", list_n(rows), list);
}
static int is_weapon_skill(Obj s) {                     /* skill keys are ASCII */
    if (!slen(s)) return 0;
    weapon_skills_load();
    for (int j = 0; j < nwsk; j++) if (str_is(s, wsk[j])) return 1;
    return 0;
}

/* ------------------------------------------------------------------------------------------------ load: grow, migrate */
static uintptr_t skc_h;                                 /* the UnitSkillContainer just deserialized (same load) */
static void (*o_skc_deser)(Obj, Obj, const void *);
static void my_skc_deser(Obj self, Obj data, const void *mi) {
    o_skc_deser(self, data, mi);
    if (skc_h) I.gch_free(skc_h);
    skc_h = self ? I.gch_weak(self, 0) : 0;
}
static Obj unit_weapon_skill(Obj skc, const void *g) {  /* the unit's extra skill that is a weapon's skill */
    Obj d = skc ? AT(skc, f_skc_dict, Obj) : NULL;
    const DL *L = dict_l(d);
    DICT_FOR(d, L, e) {
        if (memcmp(e + L->key, g, 16)) continue;
        Obj std = AT(e, L->val, Obj), ex = std ? AT(std, f_std_extra, Obj) : NULL;
        for (int i = 0; i < list_n(ex); i++) if (is_weapon_skill(list_at(ex, i))) return list_at(ex, i);
        return NULL;
    }
    return NULL;
}
static Obj saved_slots(Obj data, const void *g) {       /* QuickSlotSaveData.QuickSlots of a unit in the save */
    const DL *L = dict_l(data);
    DICT_FOR(data, L, e) if (!memcmp(e + L->key, g, 16)) return AT(e, L->val + f_qssd_slots, Obj);
    return NULL;
}
static int n_mig;
static void (*o_qsc_deser)(Obj, Obj, const void *);
static void my_qsc_deser(Obj self, Obj data, const void *mi) {
    o_qsc_deser(self, data, mi);
    Obj skc = skc_h ? I.gch_target(skc_h) : NULL;
    if (skc_h) { I.gch_free(skc_h); skc_h = 0; }
    Obj d = self ? AT(self, f_qsc_dict, Obj) : NULL;
    const DL *L = dict_l(d);
    int units = 0, from4 = 0, moved = 0, old = 0;
    DICT_FOR(d, L, e) {
        Obj q = AT(e, L->val, Obj), a = qsd_grow(q), sv = saved_slots(data, e + L->key);
        if (alen(a) < NSLOT) continue;
        units++;
        uintptr_t sl = alen(sv);
        if (sl >= NSLOT) aset(a, WS, aget(sv, WS));     /* the stock copy stopped at its own 4-long array */
        Obj w = (sl < NSLOT || (is_weapon_skill(aget(a, 0)) && !str_eq(aget(a, 0), aget(a, WS)))) ? unit_weapon_skill(skc, e + L->key) : NULL;
        char gs[9], before[256], after[256]; guid_c(e + L->key, gs); slots_c(a, before, sizeof before);
        if (sl < NSLOT) {                               /* stock 4-slot save: the weapon skill leaves its free slot */
            from4++;
            int had = 0;
            for (int i = 0; i < WS; i++) if (w && str_eq(aget(a, i), w)) { aset(a, i, NULL); had = 1; }
            if (had) { aset(a, WS, w); moved++; }
        } else if (w && str_eq(aget(a, 0), w) && !str_eq(aget(a, WS), w)) {   /* old binary mod: weapon skill at 0 */
            Obj s4 = aget(a, WS);
            for (int i = 0; i < WS - 1; i++) aset(a, (uintptr_t)i, aget(a, (uintptr_t)i + 1));
            aset(a, WS - 1, s4); aset(a, WS, w); old++;
        } else continue;
        slots_c(a, after, sizeof after);
        if (logn(&n_mig, 40)) H->log(ME, "load %s: %s %s -> %s", gs, sl < NSLOT ? "4-slot save" : "old layout", before, after);
    }
    H->log(ME, "load: %d units, %d from 4-slot saves (%d weapon skills moved to the weapon slot), %d from the old layout",
           units, from4, moved, old);
}

/* ------------------------------------------------------------------------------------------------ save: 5 slots */
/* The stock save copy (QuickSlotData.Serialize, inlined in the container's Serialize on Windows) writes into a new
   string[4] and indexes it with the live array's length: a 5-long array throws. It runs on 4-long copies here, then
   every saved entry gets the 5 slots. The live arrays are kept in a managed holder while the copies are in place
   (the GC does not see C memory). */
static int n_ser;
static Obj (*o_qsc_ser)(Obj, const void *);
static Obj my_qsc_ser(Obj self, const void *mi) {
    Obj d = self ? AT(self, f_qsc_dict, Obj) : NULL;
    const DL *L = dict_l(d);
    int n = d && L ? AT(d, L->cnt, int32_t) : 0;
    Obj hold = n > 0 ? I.array_new(k_object, (uintptr_t)n * 2) : NULL;   /* [QuickSlotData, its 5-long array] */
    int k = 0;
    DICT_FOR(d, L, e) {
        Obj q = AT(e, L->val, Obj), a = q ? AT(q, f_qsd_slots, Obj) : NULL;
        if (!hold || k >= n || alen(a) != NSLOT) continue;
        Obj v = I.array_new(k_string, WS);
        if (!v) continue;
        for (uintptr_t i = 0; i < WS; i++) aset(v, i, aget(a, i));
        aset(hold, (uintptr_t)k * 2, q); aset(hold, (uintptr_t)k * 2 + 1, a); k++;
        setref(q, &AT(q, f_qsd_slots, Obj), v);
    }
    Obj r = o_qsc_ser(self, mi);
    for (int i = 0; i < k; i++) { Obj q = aget(hold, (uintptr_t)i * 2); setref(q, &AT(q, f_qsd_slots, Obj), aget(hold, (uintptr_t)i * 2 + 1)); }
    const DL *R = dict_l(r);
    int put = 0;
    DICT_FOR(r, R, e) {
        Obj a = qsd_grow(qsd_of(self, (const WGuid *)(e + R->key)));
        if (alen(a) != NSLOT) continue;
        Obj s = I.array_new(k_string, NSLOT);
        if (!s) continue;
        for (uintptr_t i = 0; i < NSLOT; i++) aset(s, i, aget(a, i));
        setref(_ea, e + R->val + f_qssd_slots, s);
        put++;
    }
    if (logn(&n_ser, 5)) H->log(ME, "save: %d units with 5 slots", put);
    return r;
}

/* ------------------------------------------------------------------------------------------------ slot writes */
/* New unit (recruit, starting unit): its QuickSlotData is made in OnSpawned (inlined into OnPlayerUnitSpawned on
   Windows) with 4 slots. */
static void (*o_spawned)(Obj, WGuid, Obj, int32_t, const void *);
static void my_spawned(Obj self, WGuid g, Obj std, int32_t cost, const void *mi) {
    o_spawned(self, g, std, cost, mi);
    if (self) qsd_grow(qsd_of(AT(self, f_pudc_qsc, Obj), &g));
}

/* TryRegisterQuickSlot(guid, skill) = OnAddExtraSkill's slot step (equip of an item with a default skill, affecter
   extra skills): a weapon's skill goes to WS (and leaves the free slots), as the old mod's equip cave did; any other
   skill gets the stock search over the 4 free slots only. */
static int n_eq;
static uint8_t (*o_tryreg)(Obj, WGuid, Obj, const void *);
static uint8_t my_tryreg(Obj self, WGuid g, Obj skill, const void *mi) {
    Obj q = self ? qsd_of(self, &g) : NULL, a = qsd_grow(q);
    if (alen(a) != NSLOT) return o_tryreg(self, g, skill, mi);
    if (is_weapon_skill(skill)) {
        for (int i = 0; i < WS; i++) if (str_eq(aget(a, i), skill)) aset(a, i, NULL);
        aset(a, WS, skill);
        if (logn(&n_eq, 20)) { char gs[9], s[48]; guid_c(&g, gs); str_c(skill, s, sizeof s); H->log(ME, "equip %s: %s -> weapon slot", gs, s); }
        return 1;                                       /* registered: ProcessEquip then adds the skill to the unit */
    }
    if (aget(a, WS)) return o_tryreg(self, g, skill, mi);   /* WS taken: the stock search cannot land there */
    /* WS empty: the stock search would take it. Search a 4-long view; WS is null, so nothing is hidden from the GC. */
    AT(a, 0x18, uintptr_t) = WS;
    uint8_t r = o_tryreg(self, g, skill, mi);
    AT(a, 0x18, uintptr_t) = NSLOT;
    return r;
}

/* Game events never write WS (Set handler duplicate removal, a drop or move onto it, an unset). */
static int n_ref;
static void (*o_addat)(Obj, WGuid, int32_t, Obj, const void *);
static void my_addat(Obj self, WGuid g, int32_t idx, Obj skill, const void *mi) {
    if (idx == WS) { if (logn(&n_ref, 20)) H->log(ME, "write to the weapon slot refused (add)"); return; }
    o_addat(self, g, idx, skill, mi);
}
static void (*o_remat)(Obj, WGuid, int32_t, const void *);
static void my_remat(Obj self, WGuid g, int32_t idx, const void *mi) {
    if (idx == WS) { if (logn(&n_ref, 20)) H->log(ME, "write to the weapon slot refused (remove)"); return; }
    o_remat(self, g, idx, mi);
}

/* Skill reset: the stock clears every slot (Array.Clear inlined), then re-adds the equipped weapon's default skills to
   the unit: the weapon skill stays active, so WS keeps it. */
static void (*o_resetskill)(Obj, WGuid, const void *);
static void my_resetskill(Obj self, WGuid g, const void *mi) {
    Obj a = self ? qsd_grow(qsd_of(AT(self, f_pudc_qsc, Obj), &g)) : NULL;
    Obj w = alen(a) == NSLOT ? aget(a, WS) : NULL;
    o_resetskill(self, g, mi);
    if (w && alen(a) == NSLOT) aset(a, WS, w);
}

/* Unequip: ProcessUnequip calls OnRemoveExtraSkill(guid, skill), then removes the skill from the unit only if a slot
   still holds it (IQuickSlotViewer.IsRegistered). WS still holds it then; it is emptied after ProcessUnequip. */
#define MAXU 8
static struct { WGuid g; Obj skill; Obj pudc; } unq[MAXU];
static int nunq, n_uneq;
static void (*o_remextra)(Obj, WGuid, Obj, const void *);
static void my_remextra(Obj self, WGuid g, Obj skill, const void *mi) {
    o_remextra(self, g, skill, mi);
    if (nunq < MAXU && is_weapon_skill(skill)) { unq[nunq].g = g; unq[nunq].skill = skill; unq[nunq].pudc = self; nunq++; }
}
static void (*o_unequip)(Obj, int32_t, Obj, int32_t, Obj, Obj, const void *);
static void my_unequip(Obj self, int32_t map, Obj ent, int32_t slot, Obj key, Obj tags, const void *mi) {
    nunq = 0;
    o_unequip(self, map, ent, slot, key, tags, mi);
    for (int i = 0; i < nunq; i++) {                    /* item data strings and the container: alive for this call */
        Obj a = unq[i].pudc ? qsd_grow(qsd_of(AT(unq[i].pudc, f_pudc_qsc, Obj), &unq[i].g)) : NULL;
        if (alen(a) != NSLOT || !str_eq(aget(a, WS), unq[i].skill)) continue;
        aset(a, WS, NULL);
        if (logn(&n_uneq, 20)) { char gs[9], s[48]; guid_c(&unq[i].g, gs); str_c(unq[i].skill, s, sizeof s); H->log(ME, "unequip %s: %s left the weapon slot", gs, s); }
    }
    nunq = 0;
}

/* Every reader of a unit's slots (skill bar refresh, AI) asks GetAll: grow there too, in case a path made a 4-long
   array that the hooks above did not see. Inside DecideSkill (AI auto-use: first usable skill in slot order) the
   weapon skill comes first, as with the old mod's weapon slot at index 0. */
static uintptr_t decide_lo; static int decide_on;
static Obj (*o_getall)(Obj, WGuid, const void *);
static Obj my_getall(Obj self, WGuid g, const void *mi) {
    Obj r = o_getall(self, g, mi);
    if (r && alen(r) < NSLOT) { Obj a = qsd_grow(qsd_of(self, &g)); if (alen(a) == NSLOT) r = a; }
    uintptr_t ra = (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
    if (decide_on && ra - decide_lo < 0x2000 && alen(r) == NSLOT && aget(r, WS)) {
        Obj v = I.array_new(k_string, NSLOT);
        if (v) { aset(v, 0, aget(r, WS)); for (uintptr_t i = 0; i < WS; i++) aset(v, i + 1, aget(r, i)); r = v; }
    }
    return r;
}
static Obj (*o_decide)(Obj, const void *);
static Obj my_decide(Obj self, const void *mi) {
    decide_on = 1;
    Obj r = o_decide(self, mi);
    decide_on = 0;
    return r;
}

/* ------------------------------------------------------------------------------------------------ UI: the 6th slot */
static void *k_transform, *k_go;
static void *m_inst, *m_set_name, *m_get_tr, *m_get_go, *m_go_active, *m_tr_parent, *m_tr_find, *m_sib_set, *m_implicit,
    *m_pos_get, *m_pos_set, *m_size_get, *m_size_set, *m_amin_set, *m_amax_set, *m_pivot_set;
static int32_t f_qsui_slots = -1, f_ssui_index = -1, f_ssui_interact = -1;

static void *msig(void *k, const char *name, int argc, const char *sig) {   /* method by name + parameter types */
    for (void *c = k; c; c = I.class_get_parent(c)) {
        void *it = NULL, *m;
        while ((m = I.class_get_methods(c, &it))) {
            if (strcmp(I.method_get_name(m), name) || (int)I.method_get_param_count(m) != argc) continue;
            if (!sig) return m;
            char buf[512] = ""; size_t o = 0;
            for (int i = 0; i < argc; i++) {
                char *tn = I.type_get_name(I.method_get_param(m, (uint32_t)i));
                o += (size_t)snprintf(buf + o, o < sizeof buf ? sizeof buf - o : 0, "%s%s", i ? "," : "", tn ? tn : "?");
                if (tn && I.il_free) I.il_free(tn);
            }
            if (!strcmp(buf, sig)) return m;
        }
    }
    return NULL;
}
static int alive(Obj o) { return o && CALL(m_implicit, uint8_t, Obj)(o, m_implicit); }
static Obj tr(Obj c) {
    if (!alive(c)) return NULL;
    if (I.class_is_assignable_from(k_transform, I.object_get_class(c))) return c;
    return CALL(m_get_tr, Obj, Obj)(c, m_get_tr);
}
static Obj tparent(Obj t) { return alive(t) ? CALL(m_tr_parent, Obj, Obj)(t, m_tr_parent) : NULL; }
static Obj tfind(Obj t, const char *path) { return alive(t) ? CALL(m_tr_find, Obj, Obj, Obj)(t, I.string_new(path), m_tr_find) : NULL; }
static void set_active(Obj c, int on) {
    Obj g = alive(c) ? CALL(m_get_go, Obj, Obj)(c, m_get_go) : NULL;
    if (alive(g)) CALL(m_go_active, void, Obj, uint8_t)(g, (uint8_t)on, m_go_active);
}
static V2 pos(Obj t) { return CALL(m_pos_get, V2, Obj)(t, m_pos_get); }
static void set_pos(Obj t, V2 v) { CALL(m_pos_set, void, Obj, V2)(t, v, m_pos_set); }
static V2 size(Obj t) { return CALL(m_size_get, V2, Obj)(t, m_size_get); }
static void set_size(Obj t, V2 v) { CALL(m_size_set, void, Obj, V2)(t, v, m_size_set); }
static void shift_x(Obj t, float dx) { if (alive(t)) { V2 p = pos(t); p.x += dx; set_pos(t, p); } }

/* Owner's layout (2026-10-04, old mod's clone_slot.py --layout): the slot pitch is 64 + 4 = 68 px. The panel grows by
   68 (362 -> 430, centred, so its left part moves 34 px left): the basic slot moves -34 to stay at panel x 10..74, the
   weapon slot goes to 78..142, the divider to 146, the free-slot row (left-anchored in the panel) moves its 4 slots
   to 152..420; Layout_Item and GetDownButton (children of the root, glued to the panel's right edge) move +34. */
#define PITCH 68.0f
static int ui_add_slot(Obj view, Obj list) {
    Obj basic = list_at(list, 0), first = list_at(list, 1);
    Obj tb = tr(basic), panel = tparent(tb), row = tparent(tr(first)), root = tr(view);
    if (!alive(panel) || !alive(row) || !alive(root) || row == panel) { H->log(ME, "skill bar: panel not found"); return -1; }
    Obj c = CALL(m_inst, Obj, Obj, Obj)(first, panel, m_inst);          /* a free slot, in prefab state (before Init) */
    Obj tc = tr(c);
    if (!alive(tc)) { H->log(ME, "skill bar: copy failed"); return -1; }
    CALL(m_set_name, void, Obj, Obj)(c, I.string_new("SubUI_SkillSlot_Weapon"), m_set_name);
    V2 half = { 0.5f, 0.5f }, bp = pos(tb);
    CALL(m_amin_set, void, Obj, V2)(tc, half, m_amin_set); CALL(m_amax_set, void, Obj, V2)(tc, half, m_amax_set);
    CALL(m_pivot_set, void, Obj, V2)(tc, half, m_pivot_set);
    set_size(tc, size(tb));
    V2 ps = size(panel); ps.x += PITCH; set_size(panel, ps);
    shift_x(tb, -PITCH / 2);
    set_pos(tc, (V2){ bp.x - PITCH / 2 + PITCH, bp.y });
    CALL(m_sib_set, void, Obj, int32_t)(tc, 1, m_sib_set);
    shift_x(tfind(panel, "Img_Divider"), PITCH);
    shift_x(row, PITCH / 2); V2 rs = size(row); rs.x -= PITCH; set_size(row, rs);
    shift_x(tfind(root, "Layout_Item"), PITCH / 2);
    shift_x(tfind(root, "GetDownButton"), PITCH / 2);
    set_active(tfind(tc, "Img_KeyBG"), 0); set_active(tfind(tc, "Txt_KeyBG"), 0);   /* no key reaches this slot */
    void *m_add = I.class_get_method_from_name(I.object_get_class(list), "Add", 1);
    if (!m_add) { H->log(ME, "skill bar: List.Add not found"); return -1; }
    CALL(m_add, void, Obj, Obj)(list, c, m_add);
    H->log(ME, "skill bar: weapon slot added (panel %.0f px wide, basic slot x %.0f, weapon slot x %.0f)", ps.x, bp.x - PITCH / 2,
           bp.x + PITCH / 2);
    return 0;
}

/* QuickSlotUI.Init wires every slot of _skillSlots (loop bound = the list's Count): click (UseSkill Index = UI index),
   hover, right click (_onInteract) and the key text skillKeys[i-1] (indexer: a missing key throws, and the ammo slots
   are wired after this loop). The presenter fills keys 0..3. */
static void (*o_ui_init)(Obj, Obj, Obj, Obj, const void *);
static void my_ui_init(Obj self, Obj ammo, Obj keys, Obj itemkey, const void *mi) {
    Obj list = self ? AT(self, f_qsui_slots, Obj) : NULL;
    if (list && list_n(list) == UI_W) ui_add_slot(self, list);
    if (list && list_n(list) > UI_W && keys) {          /* a bar copy made from a 6-slot bar has the slot already */
        void *m_set = I.class_get_method_from_name(I.object_get_class(keys), "set_Item", 2);
        if (m_set) CALL(m_set, void, Obj, int32_t, Obj)(keys, UI_W - 1, I.string_new(""), m_set);
    }
    o_ui_init(self, ammo, keys, itemkey, mi);
    Obj w = list && list_n(list) > UI_W ? list_at(list, UI_W) : NULL;
    if (w) setref(w, &AT(w, f_ssui_interact, Obj), NULL);   /* no right-click menu (the stock basic slot has none) */
}
static Obj (*o_payload)(Obj, const void *);
static Obj my_payload(Obj self, const void *mi) {       /* drag source */
    if (self && AT(self, f_ssui_index, int32_t) == UI_W) return NULL;
    return o_payload(self, mi);
}
static uint8_t (*o_candrop)(Obj, Obj, const void *);
static uint8_t my_candrop(Obj self, Obj payload, const void *mi) {   /* drop target (the stock OnDrop runs only after it) */
    if (self && AT(self, f_ssui_index, int32_t) == UI_W) return 0;
    return o_candrop(self, payload, mi);
}

/* ------------------------------------------------------------------------------------------------ init */
static int hk(const char *ns, const char *cls, const char *m, int argc, void *repl, void **orig) {
    int r = H->hook_method(ME, ns, cls, m, argc, repl, orig);
    if (r) H->log(ME, "hook %s.%s.%s (%d args) failed (%d)", ns, cls, m, argc, r);
    return r;
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
#define FN(f, n) *(void **)&I.f = H->il2cpp_fn(n)
    FN(object_get_class, "il2cpp_object_get_class"); FN(class_get_parent, "il2cpp_class_get_parent");
    FN(class_get_field_from_name, "il2cpp_class_get_field_from_name"); FN(field_get_offset, "il2cpp_field_get_offset");
    FN(field_get_type, "il2cpp_field_get_type"); FN(class_from_type, "il2cpp_class_from_type");
    FN(class_get_element_class, "il2cpp_class_get_element_class"); FN(class_value_size, "il2cpp_class_value_size");
    FN(field_static_get_value, "il2cpp_field_static_get_value"); FN(class_get_method_from_name, "il2cpp_class_get_method_from_name");
    FN(class_get_methods, "il2cpp_class_get_methods"); FN(method_get_name, "il2cpp_method_get_name");
    FN(method_get_param_count, "il2cpp_method_get_param_count"); FN(method_get_param, "il2cpp_method_get_param");
    FN(type_get_name, "il2cpp_type_get_name"); FN(il_free, "il2cpp_free");
    FN(class_is_assignable_from, "il2cpp_class_is_assignable_from"); FN(array_new, "il2cpp_array_new");
    FN(string_new, "il2cpp_string_new"); FN(wbarrier, "il2cpp_gc_wbarrier_set_field");
    FN(gch_weak, "il2cpp_gchandle_new_weakref"); FN(gch_target, "il2cpp_gchandle_get_target"); FN(gch_free, "il2cpp_gchandle_free");
#undef FN
    for (void **p = (void **)&I; p < (void **)(&I + 1); p++) if (!*p) { H->log(ME, "il2cpp export missing (#%d)", (int)(p - (void **)&I)); return 1; }

    const char *M = "Refactor.Main", *UI = "Refactor.UI";
    void *k_qsd = H->find_class(M, "QuickSlotData"), *k_qsc = H->find_class(M, "UnitQuickSlotContainer");
    void *k_pudc = H->find_class(M, "PlayerUnitDataContainer"), *k_skc = H->find_class(M, "UnitSkillContainer");
    void *k_std = H->find_class(M, "SkillTreeData"), *k_qssd = H->find_class(M, "QuickSlotSaveData");
    void *k_qsui = H->find_class(UI, "QuickSlotUI"), *k_ssui = H->find_class(UI, "SkillSlotUI");
    void *k_rect = H->find_class("UnityEngine", "RectTransform");
    k_string = H->find_class("System", "String"); k_object = H->find_class("System", "Object");
    k_transform = H->find_class("UnityEngine", "Transform"); k_go = H->find_class("UnityEngine", "GameObject");
    void *k_uobj = H->find_class("UnityEngine", "Object"), *k_comp = H->find_class("UnityEngine", "Component");
    if (!k_qsd || !k_qsc || !k_pudc || !k_skc || !k_std || !k_qssd || !k_qsui || !k_ssui || !k_rect || !k_string
        || !k_object || !k_transform || !k_go || !k_uobj || !k_comp) { H->log(ME, "a game class was not found"); return 2; }
    f_qsd_slots = foff(k_qsd, "_quickSlots"); f_qsc_dict = foff(k_qsc, "_unitQuickSlot");
    f_pudc_qsc = foff(k_pudc, "_unitQuickSlot"); f_pudc_skc = foff(k_pudc, "_unitSkillTrees");
    f_skc_dict = foff(k_skc, "_skillTreeDatas"); f_std_extra = foff(k_std, "_extraSkills");
    f_qssd_slots = foff(k_qssd, "QuickSlots") - OHDR;
    void *k_strlist = ftype_class(k_std, "_extraSkills");                    /* List<string>: every List<T> has this layout */
    f_list_items = k_strlist ? foff(k_strlist, "_items") : -1; f_list_size = k_strlist ? foff(k_strlist, "_size") : -1;
    f_qsui_slots = foff(k_qsui, "_skillSlots"); f_ssui_index = foff(k_ssui, "_index"); f_ssui_interact = foff(k_ssui, "_onInteract");
    H->log(ME, "fields: slots %d dict %d pudc %d/%d skills %d extra %d save %d list %d/%d bar %d index %d interact %d",
           f_qsd_slots, f_qsc_dict, f_pudc_qsc, f_pudc_skc, f_skc_dict, f_std_extra, f_qssd_slots, f_list_items, f_list_size,
           f_qsui_slots, f_ssui_index, f_ssui_interact);
    if (f_qsd_slots <= 0 || f_qsc_dict <= 0 || f_pudc_qsc <= 0 || f_skc_dict <= 0 || f_std_extra <= 0 || f_qssd_slots < 0
        || f_list_items <= 0 || f_list_size <= 0 || f_qsui_slots <= 0 || f_ssui_index <= 0 || f_ssui_interact <= 0) {
        H->log(ME, "a game field was not found"); return 3;
    }
    /* the three dictionaries read in place: Dictionary<Guid, QuickSlotData>, <Guid, SkillTreeData>, <Guid, QuickSlotSaveData> */
    void *k_pusd = H->find_class(M, "PlayerUnitsSaveData");
    if (!dict_lk(ftype_class(k_qsc, "_unitQuickSlot")) || !dict_lk(ftype_class(k_skc, "_skillTreeDatas"))
        || !k_pusd || !dict_lk(ftype_class(k_pusd, "QuickSlots"))) { H->log(ME, "a dictionary layout was not found"); return 3; }
    m_implicit = msig(k_uobj, "op_Implicit", 1, NULL);
    m_inst = msig(k_uobj, "Instantiate", 2, "UnityEngine.Object,UnityEngine.Transform");
    m_set_name = msig(k_uobj, "set_name", 1, NULL);
    m_get_tr = msig(k_comp, "get_transform", 0, NULL); m_get_go = msig(k_comp, "get_gameObject", 0, NULL);
    m_go_active = msig(k_go, "SetActive", 1, NULL);
    m_tr_parent = msig(k_transform, "get_parent", 0, NULL); m_tr_find = msig(k_transform, "Find", 1, "System.String");
    m_sib_set = msig(k_transform, "SetSiblingIndex", 1, NULL);
    m_pos_get = msig(k_rect, "get_anchoredPosition", 0, NULL); m_pos_set = msig(k_rect, "set_anchoredPosition", 1, NULL);
    m_size_get = msig(k_rect, "get_sizeDelta", 0, NULL); m_size_set = msig(k_rect, "set_sizeDelta", 1, NULL);
    m_amin_set = msig(k_rect, "set_anchorMin", 1, NULL); m_amax_set = msig(k_rect, "set_anchorMax", 1, NULL);
    m_pivot_set = msig(k_rect, "set_pivot", 1, NULL);
    if (!m_implicit || !m_inst || !m_set_name || !m_get_tr || !m_get_go || !m_go_active || !m_tr_parent || !m_tr_find || !m_sib_set
        || !m_pos_get || !m_pos_set || !m_size_get || !m_size_set || !m_amin_set || !m_amax_set || !m_pivot_set) {
        H->log(ME, "a Unity UI method was not found"); return 4;
    }
    void *dmi = H->find_method(H->find_class("Refactor.Task", "DecidePlayerCombatBehaviour"), "DecideSkill", 0);
    decide_lo = dmi ? (uintptr_t)H->method_address(dmi) : 0;
    if (!decide_lo) { H->log(ME, "DecidePlayerCombatBehaviour.DecideSkill not found"); return 5; }

    /* the data array and the 6th UI slot must come together (6 UI slots + 4 slots or 5 UI slots + 5 slots throws
       every frame): any failed hook = the module returns non-zero and the loader removes every hook of it */
    if (hk(M, "UnitSkillContainer", "Deserialize", 1, (void *)my_skc_deser, (void **)&o_skc_deser)
        || hk(M, "UnitQuickSlotContainer", "Deserialize", 1, (void *)my_qsc_deser, (void **)&o_qsc_deser)
        || hk(M, "UnitQuickSlotContainer", "Serialize", 0, (void *)my_qsc_ser, (void **)&o_qsc_ser)
        || hk(M, "UnitQuickSlotContainer", "TryRegisterQuickSlot", 2, (void *)my_tryreg, (void **)&o_tryreg)
        || hk(M, "UnitQuickSlotContainer", "AddQuickSlotAt", 3, (void *)my_addat, (void **)&o_addat)
        || hk(M, "UnitQuickSlotContainer", "RemoveQuickSlotAt", 2, (void *)my_remat, (void **)&o_remat)
        || hk(M, "UnitQuickSlotContainer", "GetAll", 1, (void *)my_getall, (void **)&o_getall)
        || hk(M, "PlayerUnitDataContainer", "OnPlayerUnitSpawned", 3, (void *)my_spawned, (void **)&o_spawned)
        || hk(M, "PlayerUnitDataContainer", "ResetSkill", 1, (void *)my_resetskill, (void **)&o_resetskill)
        || hk(M, "PlayerUnitDataContainer", "OnRemoveExtraSkill", 2, (void *)my_remextra, (void **)&o_remextra)
        || hk("Refactor.Main.Processor", "ItemControlProcessor", "ProcessUnequip", 5, (void *)my_unequip, (void **)&o_unequip)
        || hk(UI, "QuickSlotUI", "Init", 3, (void *)my_ui_init, (void **)&o_ui_init)
        || hk(UI, "SkillSlotUI", "GetPayload", 0, (void *)my_payload, (void **)&o_payload)
        || hk(UI, "SkillSlotUI", "CanDrop", 1, (void *)my_candrop, (void **)&o_candrop))
        return 6;
    /* AI order only (weapon skill first). Optional: the multiplayer module hooks this method in diagnostic test runs. */
    int order = !hk("Refactor.Task", "DecidePlayerCombatBehaviour", "DecideSkill", 0, (void *)my_decide, (void **)&o_decide);
    H->log(ME, "weapon skill slot on (%d hooks; AI auto-use order %s)", 14 + order, order ? "weapon skill first" : "stock (hook taken)");
    return 0;
}
