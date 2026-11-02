// Weapon skill slot (mod id weapon-slot), 0.2.0: CLIENT ONLY (owner 2026-10-17 "Weapon slot mod is a local mod only",
// 2026-11-01 "it's client only"). The skill bar gets a sixth button, between the basic attack and the 4 free slots, that
// shows the equipped weapon's skill (Equipment_ItemDefaultSkills[1] of the weapon) and uses it on a click. It follows the
// weapon. No key reaches it, it cannot be dragged, dropped on or right-clicked.
//
// It changes ONLY the local screen. The game's data is never written: the 4 quick slots, the saves, the unit's skills,
// the AI's auto-use order are the stock game's. So a player without the mod can play with a player who has it.
//  - view: the bar's views get a NEW 5-long array [the unit's 4 quick slots, its weapon skill] (QuickSlotUI.InitSkillUI /
//    RefreshSkillUI, event-driven: a fresh look at the unit each time), and UI slot 5 is refreshed each frame next to the
//    free slots (QuickSlotUI.RefreshSkillSlotUI);
//  - click: the stock click path (QuickSlotUI.OnSelect_SkillSlot -> UseQuickSlotSkillRequested Index 5 ->
//    PlayerControlEventHandler: GetSkillAt(unit, Index - 1)). Index 4 does not exist in the game's 4-slot data (stock
//    throws), so UnitQuickSlotContainer.GetSkillAt(unit, 4) answers the unit's weapon skill. The handler then runs the
//    game's own checks and aiming; the cast is the game's own order UseSkillTargetRequested, by skill key, which every
//    multiplayer player applies the same way. Every other index is stock.
//  - "the unit's weapon skill" = the skill of the weapon in its equipment. The button USES it only when the unit HAS that
//    skill (SkillComponent.HasSkill). In the stock game a unit has its weapon skill only while it sits in one of the 4 free
//    slots: a weapon equipped while they are full does not give it (ProcessEquip adds it only when TryRegisterQuickSlot
//    found a slot), and the stock "Remove from Quick Slot" (UnsetQuickSlotRequested) also removes the skill
//    (SkillComponent.RemoveSkill). Then the button shows the weapon's skill GREYED (the stock "cannot use now" look,
//    SkillSlotUI.SetActivate(false)) with the game's simple hover card "free a quick slot, then equip the weapon again"
//    (its own TooltipFeedback.SetSimpleText, the locked-slots pattern); a click does nothing (GetSkillAt answers null).
//  - setting "hide_copy" (local, default off; ext/nvwslot3 plan, the owner picks the default): the weapon skill's own free
//    slot is drawn EMPTY on this screen (the view array gets null there) and its per-frame refresh goes to the weapon button.
//    The game's data still has it there: that slot's key still uses it, auto-use is unchanged. View only.
// Hooks by il2cpp name (Mac and Windows); GameAssembly and the game's UI bundle unchanged. Build 25846389.
// 0.1.x (gameplay: a 5-slot data array, its own save handling, weapon skill first in the AI order) is gone; see
// ~/ds_multiplayer/ext/nvwslot/PLAN.md and ext/nvwslot3/PLAN.md.
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
#define WS 4                                            /* GetSkillAt index of the weapon slot (UI index - 1) */
#define NSLOT 5                                         /* the view's array: 4 quick slots + the weapon skill */
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
static void guid_c(const void *g, char *out) { const uint8_t *b = g; for (int i = 0; i < 4; i++) sprintf(out + 2 * i, "%02x", b[3 - i]); }

/* Dictionary<K, V> (corefx layout: _entries, _count; entry {hashCode, next, key, value}, free entry hashCode < 0).
   Layout per dictionary class, from its field types; the classes this module reads are checked at init. READ ONLY. */
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

/* The skills that are a weapon's own skill: Equipment_ItemDefaultSkills[1] of every item (8 in this build, one per
   weapon type; none of them is in a skill tree). Read once from DataSheetManager._itemTable.tableDatas. */
#define MAXW 64
#define MAXI 512
static char wsk[MAXW][64]; static int nwsk = -1;
static struct { char item[64]; int w; } wit[MAXI]; static int nwit;   /* item key -> its weapon skill (index in wsk) */
static void weapon_skills_load(void) {
    if (nwsk >= 0) return;
    void *k_dsm = H->find_class("Refactor.Util", "DataSheetManager"), *k_itd = H->find_class("Util.Sheet", "ItemTableData");
    void *single = k_dsm ? I.class_get_parent(k_dsm) : NULL;
    void *f_inst = single ? I.class_get_field_from_name(single, "_instance") : NULL;
    Obj dsm = NULL;
    if (f_inst) I.field_static_get_value(f_inst, &dsm);
    int32_t f_it = k_dsm ? foff(k_dsm, "_itemTable") : -1, f_skills = k_itd ? foff(k_itd, "Equipment_ItemDefaultSkills") : -1;
    int32_t f_itd_key = k_itd ? foff(k_itd, "Key") : -1;
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
        int j = 0;
        while (j < nwsk && strcmp(wsk[j], c)) j++;
        if (j == nwsk) {
            strcpy(wsk[nwsk++], c);
            if (o < sizeof list) o += (size_t)snprintf(list + o, sizeof list - o, "%s%s", nwsk > 1 ? " " : "", c);
        }
        Obj key = f_itd_key > 0 ? AT(row, f_itd_key, Obj) : NULL;
        if (nwit < MAXI && slen(key) && slen(key) < 64) { str_c(key, wit[nwit].item, 64); wit[nwit++].w = j; }
    }
    H->log(ME, "weapon skills (%d items, %d with a weapon skill): %s", list_n(rows), nwit, list);
}
static const char *item_weapon_skill(Obj key) {         /* the weapon skill of an item key, or NULL */
    if (!slen(key)) return NULL;
    weapon_skills_load();
    for (int i = 0; i < nwit; i++) if (str_is(key, wit[i].item)) return wsk[wit[i].w];
    return NULL;
}

/* ------------------------------------------------------------------------------------------------ the unit's weapon skill */
/* EntityComponent._instance._components: Dictionary<IEntity, ComponentList>; ComponentList._components holds the
   unit's EquipmentComponent (InventoryComponent._itemSlots: ItemHolder structs, Key) and SkillComponent. Read only. */
static void *k_unit, *k_skillcomp, *k_equip, *f_ec_inst, *m_has;
static int32_t f_ec_comps = -1, f_cl_comps = -1, f_unit_guid = -1, f_inv_slots = -1, f_ih_key = -1, ih_size = 0;
static int is_a(Obj o, void *k) { return o && k && (I.class_is_assignable_from(k, I.object_get_class(o)) & 1); }
static Obj comps_dict(void) {
    Obj ec = NULL;
    I.field_static_get_value(f_ec_inst, &ec);
    return ec ? AT(ec, f_ec_comps, Obj) : NULL;
}
/* key: the equipped weapon's skill ("" = none); owned: the unit HAS it (only then the button uses it) */
typedef struct { char key[64]; uint8_t owned; } WInfo;
static void weapon_of_list(Obj cl, WInfo *w) {
    w->key[0] = 0; w->owned = 0;
    Obj cd = cl ? AT(cl, f_cl_comps, Obj) : NULL, eq = NULL, sc = NULL;
    const DL *C = dict_l(cd);
    DICT_FOR(cd, C, ce) { Obj o = AT(ce, C->val, Obj); if (is_a(o, k_equip)) eq = o; else if (is_a(o, k_skillcomp)) sc = o; }
    Obj slots = eq ? AT(eq, f_inv_slots, Obj) : NULL;
    const char *wn = NULL;
    for (uintptr_t i = 0; i < alen(slots) && !wn; i++) wn = item_weapon_skill(AT((char *)slots + AHDR + i * (uintptr_t)ih_size, f_ih_key, Obj));
    if (!wn) return;
    snprintf(w->key, sizeof w->key, "%s", wn);
    w->owned = sc && (CALL(m_has, uint8_t, Obj, Obj)(sc, I.string_new(wn), m_has) & 1);   /* C# bool: low byte (Windows) */
}
static void weapon_of_entity(Obj ent, WInfo *w) {
    w->key[0] = 0; w->owned = 0;
    if (!is_a(ent, k_unit)) return;
    Obj comps = comps_dict();
    const DL *L = dict_l(comps);
    DICT_FOR(comps, L, e) if (AT(e, L->key, Obj) == ent) { weapon_of_list(AT(e, L->val, Obj), w); return; }
}
static void weapon_of_guid(const WGuid *g, WInfo *w) {
    w->key[0] = 0; w->owned = 0;
    Obj comps = comps_dict();
    const DL *L = dict_l(comps);
    DICT_FOR(comps, L, e) {
        Obj ent = AT(e, L->key, Obj);
        if (is_a(ent, k_unit) && !memcmp((char *)ent + f_unit_guid, g, 16)) { weapon_of_list(AT(e, L->val, Obj), w); return; }
    }
}

/* Per-frame cache by unit Guid (the bar asks every frame; the lookup walks the entity table): re-read every 30 frames,
   and at once (at most once a frame) when the bar is rebuilt (InitSkillUI / RefreshSkillUI run on the game's events:
   select, skill or equipment change, "Remove from Quick Slot"). */
static void *m_frame;
static int32_t frame_now(void) { return ((int32_t (*)(const void *))*(void **)m_frame)(m_frame); }
#define NC 8
static struct { WGuid g; int32_t frame; WInfo w; int used; } wc[NC];
static const WInfo *weapon_cached(Obj user, int fresh) {
    static const WInfo none;
    if (!is_a(user, k_unit)) return &none;
    WGuid g; memcpy(&g, (char *)user + f_unit_guid, sizeof g);
    int32_t f = frame_now(); int lru = 0;
    for (int i = 0; i < NC; i++) {
        if (wc[i].used && !memcmp(&wc[i].g, &g, sizeof g)) {
            if ((fresh && wc[i].frame != f) || (uint32_t)(f - wc[i].frame) >= 30u) { weapon_of_entity(user, &wc[i].w); wc[i].frame = f; }
            return &wc[i].w;
        }
        if (wc[lru].used && (!wc[i].used || wc[i].frame < wc[lru].frame)) lru = i;   /* a free entry first, else the oldest */
    }
    wc[lru].used = 1; wc[lru].g = g; wc[lru].frame = f;
    weapon_of_entity(user, &wc[lru].w);
    return &wc[lru].w;
}

/* ------------------------------------------------------------------------------------------------ click: GetSkillAt(unit, 4) */
static int32_t f_qsc_dict = -1, f_qsd_slots = -1;
static Obj qsd_slots(Obj qsc, const WGuid *g) {         /* the unit's quick-slot array (read only), or NULL */
    Obj d = qsc ? AT(qsc, f_qsc_dict, Obj) : NULL;
    const DL *L = dict_l(d);
    DICT_FOR(d, L, e) if (!memcmp(e + L->key, g, 16)) { Obj q = AT(e, L->val, Obj); return q ? AT(q, f_qsd_slots, Obj) : NULL; }
    return NULL;
}
static int n_click;
static Obj (*o_skillat)(Obj, WGuid, int32_t, const void *);
static Obj my_skillat(Obj self, WGuid g, int32_t idx, const void *mi) {
    if (idx != WS || alen(qsd_slots(self, &g)) > WS) return o_skillat(self, g, idx, mi);
    WInfo w; weapon_of_guid(&g, &w);
    if (logn(&n_click, 20)) {
        char gs[9]; guid_c(&g, gs);
        char why[100]; snprintf(why, sizeof why, w.key[0] ? "(none: the unit does not have %s)" : "(none: no weapon skill)", w.key);
        H->log(ME, "weapon slot used: unit %s -> %s", gs, w.owned ? w.key : why);
    }
    return w.owned ? I.string_new(w.key) : NULL;        /* NULL: the stock handler does nothing (empty slot) */
}

/* ------------------------------------------------------------------------------------------------ texts */
/* the greyed button's hover card (game LanguageType order: 0 en, 1 ko, 2 fr, 3 de, 4 ru, 5 zh-Hans, 6 zh-Hant, 7 ja, 8 es,
   9 pt-BR). "Quick slot" and "equip" are the game's own words in each language ("Remove from Quick Slot", "Equip"). */
static const char *const T_NOSKILL[10] = {
    "This unit does not have its weapon's skill. Free a quick slot, then equip the weapon again.",
    "이 유닛은 무기 스킬을 가지고 있지 않습니다. 퀵 슬롯을 하나 비운 뒤 무기를 다시 장착하세요.",
    "Cette unité n'a pas la compétence de son arme. Libérez un emplacement rapide, puis équipez de nouveau l'arme.",
    "Diese Einheit hat den Skill ihrer Waffe nicht. Mach einen Schnellzugriff-Platz frei und rüste die Waffe erneut aus.",
    "У этого юнита нет навыка его оружия. Освободите быстрый слот и снова экипируйте оружие.",
    "该单位没有其武器的技能。请在快捷栏中空出一格，然后重新装备该武器。",
    "該單位沒有其武器的技能。請在快捷欄中空出一格，然後重新裝備該武器。",
    "このユニットは武器のスキルを持っていません。クイックスロットを1つ空けてから、武器を装備し直してください。",
    "Esta unidad no tiene la habilidad de su arma. Libera una ranura rápida y vuelve a equipar el arma.",
    "Esta unidade não tem a habilidade da arma. Libere um espaço rápido e equipe a arma de novo." };
static void *m_lang;
static int lang(void) {
    int v = m_lang ? ((int32_t (*)(const void *))*(void **)m_lang)(m_lang) : 0;
    return v >= 0 && v < 10 ? v : 0;
}

/* ------------------------------------------------------------------------------------------------ view: 5-long arrays */
static int32_t f_qsui_slots = -1, f_ssui_index = -1, f_ssui_interact = -1, f_ssui_auto = -1, f_ssui_iui = -1;
static int bar6(Obj view) { Obj l = view ? AT(view, f_qsui_slots, Obj) : NULL; return list_n(l) > UI_W; }
static void *k_string;
static int hide_copy;                                   /* setting "hide_copy": the weapon skill's free slot drawn empty */
/* a NEW array for the view: the 4 slots + the weapon skill (when the unit has it). hide_copy: the free slot(s) holding the
   weapon skill are null in it (drawn empty); *hidden = those UI indexes as bits. The game's own array is never changed. */
static Obj view_slots(Obj qs, const WInfo *w, uint8_t *hidden) {
    *hidden = 0;
    Obj v = I.array_new(k_string, NSLOT);
    if (!v) return qs;
    for (uintptr_t i = 0; i < WS && i < alen(qs); i++) {
        Obj s = aget(qs, i);
        if (hide_copy && w->owned && str_is(s, w->key)) { *hidden |= (uint8_t)(1u << (i + 1)); s = NULL; }
        aset(v, i, s);
    }
    if (w->owned) aset(v, WS, I.string_new(w->key));
    return v;
}
/* per view (bar or multi-bar card; only compared, never read through): what the weapon button shows now */
enum { M_NONE = 0, M_OWNED = 1, M_GREY = 2 };
#define NV 8
static struct { Obj view; int32_t frame, wframe; char shown[64]; uint8_t mode, lang, hidden; char lkey[64]; int8_t lmode; } vs[NV];
static int vslot(Obj view) {
    int lru = 0;
    for (int i = 0; i < NV; i++) { if (vs[i].view == view) return i; if (vs[i].frame < vs[lru].frame) lru = i; }
    memset(&vs[lru], 0, sizeof vs[lru]);
    vs[lru].view = view; vs[lru].frame = vs[lru].wframe = -1000; vs[lru].lmode = -1;
    return lru;
}
static void *m_setui, *m_activate, *m_getfb, *m_simple, *k_tipfb;
static int fb_tooltip;                                  /* InteractFeedbackType.Tooltip */
static int alive(Obj o);
static void set_active(Obj c, int on);
static void slot_setui(Obj slot, const char *key) {
    uint8_t nul[32] = { 0 };                            /* Nullable<Guid> parentTooltip = null (by reference: > 16 bytes) */
    if (slot) CALL(m_setui, void, Obj, Obj, void *)(slot, key && key[0] ? I.string_new(key) : NULL, nul, m_setui);
}
/* GREY: the unit does not have its weapon's skill. The stock "cannot use now" look (SetActivate(false): the cooldown cover
   full), no auto-use mark, and the game's simple hover card with our text (locked-slots pattern). SetUI(key) before it
   gave the icon and turned the slot's InteractableUI on (hover); SetUI again (another state) sets the card back. */
static void grey(Obj slot, int l) {
    CALL(m_activate, void, Obj, uint8_t)(slot, 0, m_activate);
    Obj am = f_ssui_auto > 0 ? AT(slot, f_ssui_auto, Obj) : NULL;
    if (alive(am)) set_active(am, 0);
    Obj iu = f_ssui_iui > 0 ? AT(slot, f_ssui_iui, Obj) : NULL;
    Obj fb = alive(iu) ? CALL(m_getfb, Obj, Obj, int32_t)(iu, fb_tooltip, m_getfb) : NULL;
    if (alive(fb) && is_a(fb, k_tipfb)) CALL(m_simple, void, Obj, Obj, uint8_t)(fb, I.string_new(T_NOSKILL[l]), 0, m_simple);
}
static int n_show;
static void note(int i, Obj user, const WInfo *w, int mode) {
    if (vs[i].lmode == mode && !strcmp(vs[i].lkey, w->key)) return;
    vs[i].lmode = (int8_t)mode; snprintf(vs[i].lkey, sizeof vs[i].lkey, "%s", w->key);
    if (!logn(&n_show, 40)) return;
    char gs[9] = "-"; if (is_a(user, k_unit)) guid_c((char *)user + f_unit_guid, gs);
    if (mode == M_GREY) H->log(ME, "weapon slot shows %s greyed: the unit does not have it (unit %s)", w->key, gs);
    else H->log(ME, "weapon slot shows %s (unit %s)", mode ? w->key : "nothing", gs);
}
/* the weapon button (UI slot 5) of view i to the unit's state; a change only (or a language change while greyed) */
static void button_sync(Obj view, int i, Obj user, const WInfo *w) {
    int mode = !w->key[0] ? M_NONE : w->owned ? M_OWNED : M_GREY, l = lang();
    if (mode != vs[i].mode || strcmp(vs[i].shown, w->key) || (mode == M_GREY && vs[i].lang != l)) {
        Obj slot = list_at(AT(view, f_qsui_slots, Obj), UI_W);
        slot_setui(slot, mode ? w->key : NULL);
        if (slot && mode == M_GREY) grey(slot, l);
        vs[i].mode = (uint8_t)mode; vs[i].lang = (uint8_t)l;
        snprintf(vs[i].shown, sizeof vs[i].shown, "%s", mode ? w->key : "");
    }
    note(i, user, w, mode);
}
/* after the stock InitSkillUI / RefreshSkillUI drew the 6 slots from our array: slot 5 has the key when owned, else nothing */
static void after_build(Obj view, Obj user, const WInfo *w, uint8_t hidden) {
    int i = vslot(view);
    vs[i].hidden = hidden;
    vs[i].mode = w->owned ? M_OWNED : M_NONE;
    snprintf(vs[i].shown, sizeof vs[i].shown, "%s", w->owned ? w->key : "");
    button_sync(view, i, user, w);
}
static void (*o_initskill)(Obj, Obj, Obj, Obj, const void *);
static void my_initskill(Obj self, Obj user, Obj qs, Obj inmap, const void *mi) {
    if (!bar6(self) || !qs) { o_initskill(self, user, qs, inmap, mi); return; }
    const WInfo *w = weapon_cached(user, 1); WInfo c = *w; uint8_t hid;
    o_initskill(self, user, view_slots(qs, &c, &hid), inmap, mi);
    after_build(self, user, &c, hid);
}
static void (*o_refreshskill)(Obj, Obj, Obj, Obj, const void *);
static void my_refreshskill(Obj self, Obj user, Obj qs, Obj inmap, const void *mi) {
    if (!bar6(self) || !qs) { o_refreshskill(self, user, qs, inmap, mi); return; }
    const WInfo *w = weapon_cached(user, 1); WInfo c = *w; uint8_t hid;
    o_refreshskill(self, user, view_slots(qs, &c, &hid), inmap, mi);
    after_build(self, user, &c, hid);
}
/* The presenter refreshes every FILLED free slot each frame (cooldown, usable): once a frame per view the weapon button
   follows the unit (a weapon that left empties it, a skill the unit lost greys it) and, when the unit has the skill, gets
   the same refresh. hide_copy: the free slot holding the weapon skill stays drawn empty and its refresh goes to the button. */
static int n_hide;
static void (*o_slotui)(Obj, int32_t, Obj, Obj, Obj, const void *);
static void my_slotui(Obj self, int32_t idx, Obj key, Obj user, Obj inmap, const void *mi) {
    if (idx < 1 || idx > WS || !bar6(self)) { o_slotui(self, idx, key, user, inmap, mi); return; }
    int i = vslot(self); int32_t f = frame_now();
    const WInfo *w = weapon_cached(user, 0);
    if (vs[i].frame != f) { vs[i].frame = f; button_sync(self, i, user, w); }
    if (w->owned && str_is(key, w->key)) {             /* this free slot holds the weapon skill: the copy */
        uint8_t bit = (uint8_t)(1u << idx);
        if (!(vs[i].hidden & bit) != !hide_copy) {     /* the setting changed since this view was built */
            slot_setui(list_at(AT(self, f_qsui_slots, Obj), idx), hide_copy ? NULL : w->key);
            vs[i].hidden ^= bit;
            if (logn(&n_hide, 20)) H->log(ME, "quick slot %d (%s): %s", idx, w->key, hide_copy ? "drawn empty (hide the copy)" : "drawn again");
        }
        if (hide_copy) {
            if (vs[i].wframe != f) { vs[i].wframe = f; o_slotui(self, UI_W, key, user, inmap, mi); }
            return;
        }
    }
    o_slotui(self, idx, key, user, inmap, mi);
    if (w->owned && vs[i].wframe != f) { vs[i].wframe = f; o_slotui(self, UI_W, I.string_new(w->key), user, inmap, mi); }
}
static void on_setting(void *ud, const char *id) {
    (void)ud;
    if (id && strcmp(id, "hide_copy")) return;
    int v = dsl_get_bool(H, ME, "hide_copy", 0);
    if (v != hide_copy) H->log(ME, "setting hide_copy: %s", v ? "on" : "off");
    hide_copy = v;
}

/* ------------------------------------------------------------------------------------------------ UI: the 6th slot */
static void *k_transform, *k_go;
static void *m_inst, *m_set_name, *m_get_tr, *m_get_go, *m_go_active, *m_tr_parent, *m_tr_find, *m_sib_set, *m_implicit,
    *m_pos_get, *m_pos_set, *m_size_get, *m_size_set, *m_amin_set, *m_amax_set, *m_pivot_set;

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
    if (list && list_n(list) > UI_W && keys) {          /* the weapon slot's key text: "" (no key) */
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
#undef FN
    for (void **p = (void **)&I; p < (void **)(&I + 1); p++) if (!*p) { H->log(ME, "il2cpp export missing (#%d)", (int)(p - (void **)&I)); return 1; }

    const char *M = "Refactor.Main", *UI = "Refactor.UI";
    void *k_qsd = H->find_class(M, "QuickSlotData"), *k_qsc = H->find_class(M, "UnitQuickSlotContainer"), *k_std = H->find_class(M, "SkillTreeData");
    void *k_qsui = H->find_class(UI, "QuickSlotUI"), *k_ssui = H->find_class(UI, "SkillSlotUI");
    void *k_rect = H->find_class("UnityEngine", "RectTransform"), *k_time = H->find_class("UnityEngine", "Time");
    k_string = H->find_class("System", "String");
    k_transform = H->find_class("UnityEngine", "Transform"); k_go = H->find_class("UnityEngine", "GameObject");
    void *k_uobj = H->find_class("UnityEngine", "Object"), *k_comp = H->find_class("UnityEngine", "Component");
    if (!k_qsd || !k_qsc || !k_std || !k_qsui || !k_ssui || !k_rect || !k_time || !k_string || !k_transform || !k_go || !k_uobj || !k_comp) {
        H->log(ME, "a game class was not found"); return 2;
    }
    f_qsd_slots = foff(k_qsd, "_quickSlots"); f_qsc_dict = foff(k_qsc, "_unitQuickSlot");
    void *k_strlist = ftype_class(k_std, "_extraSkills");                    /* List<string>: every List<T> has this layout */
    f_list_items = k_strlist ? foff(k_strlist, "_items") : -1; f_list_size = k_strlist ? foff(k_strlist, "_size") : -1;
    f_qsui_slots = foff(k_qsui, "_skillSlots"); f_ssui_index = foff(k_ssui, "_index"); f_ssui_interact = foff(k_ssui, "_onInteract");
    H->log(ME, "fields: slots %d dict %d list %d/%d bar %d index %d interact %d", f_qsd_slots, f_qsc_dict, f_list_items, f_list_size,
           f_qsui_slots, f_ssui_index, f_ssui_interact);
    if (f_qsd_slots <= 0 || f_qsc_dict <= 0 || f_list_items <= 0 || f_list_size <= 0 || f_qsui_slots <= 0 || f_ssui_index <= 0
        || f_ssui_interact <= 0 || !dict_lk(ftype_class(k_qsc, "_unitQuickSlot"))) {
        H->log(ME, "a game field was not found"); return 3;
    }
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
    m_frame = msig(k_time, "get_frameCount", 0, NULL);
    m_setui = msig(k_ssui, "SetUI", 2, NULL);
    /* the greyed button: SetActivate(false), its auto-use mark, its hover card (InteractableUI -> TooltipFeedback) */
    m_activate = msig(k_ssui, "SetActivate", 1, NULL);
    f_ssui_auto = foff(k_ssui, "_autoSkill"); f_ssui_iui = foff(k_ssui, "_interactableUI");
    void *k_iui = H->find_class("Refactor.UI.Util", "InteractableUI"), *k_fbt = H->find_class("Refactor.UI.Util", "InteractFeedbackType");
    k_tipfb = H->find_class("Refactor.UI.Util", "TooltipFeedback");
    m_getfb = k_iui ? msig(k_iui, "GetFeedback", 1, NULL) : NULL;
    m_simple = k_tipfb ? msig(k_tipfb, "SetSimpleText", 2, "System.String,System.Boolean") : NULL;
    void *f_tt = k_fbt ? I.class_get_field_from_name(k_fbt, "Tooltip") : NULL;
    if (f_tt) { int32_t v = 0; I.field_static_get_value(f_tt, &v); fb_tooltip = v; }
    void *k_lang = H->find_class("Refactor.Setting", "LanguageSetting");
    m_lang = k_lang ? msig(k_lang, "GetCurrentLanguage", 0, NULL) : NULL;
    H->log(ME, "grey button: activate %d auto %d iui %d feedback %d simple %d tooltip %d (%s) lang %d", m_activate != NULL, f_ssui_auto,
           f_ssui_iui, m_getfb != NULL, m_simple != NULL, fb_tooltip, f_tt ? "game" : "default", m_lang != NULL);
    if (!m_implicit || !m_inst || !m_set_name || !m_get_tr || !m_get_go || !m_go_active || !m_tr_parent || !m_tr_find || !m_sib_set
        || !m_pos_get || !m_pos_set || !m_size_get || !m_size_set || !m_amin_set || !m_amax_set || !m_pivot_set || !m_frame || !m_setui
        || !m_activate || f_ssui_auto <= 0 || f_ssui_iui <= 0 || !m_getfb || !m_simple || !k_tipfb) {
        H->log(ME, "a Unity UI method was not found"); return 4;
    }
    /* the unit's weapon skill: entities, their components, the equipped items, SkillComponent.HasSkill */
    void *k_ec = H->find_class(M, "EntityComponent"), *k_cl = H->find_class(M, "ComponentList"), *k_ih = H->find_class("Refactor", "ItemHolder");
    void *k_inv = H->find_class("Refactor", "InventoryComponent");
    k_unit = H->find_class("Refactor", "UnitEntity"); k_skillcomp = H->find_class("Refactor.Component", "SkillComponent");
    k_equip = H->find_class("Refactor.Component", "EquipmentComponent");
    f_ec_inst = k_ec ? I.class_get_field_from_name(k_ec, "_instance") : NULL;
    f_ec_comps = k_ec ? foff(k_ec, "_components") : -1; f_cl_comps = k_cl ? foff(k_cl, "_components") : -1;
    f_unit_guid = k_unit ? foff(k_unit, "<Guid>k__BackingField") : -1; f_inv_slots = k_inv ? foff(k_inv, "_itemSlots") : -1;
    f_ih_key = k_ih ? foff(k_ih, "Key") - OHDR : -1; ih_size = k_ih ? I.class_value_size(k_ih, NULL) : 0;
    m_has = k_skillcomp ? H->find_method(k_skillcomp, "HasSkill", 1) : NULL;
    H->log(ME, "unit fields: components %d/%d guid %d slots %d key %d item %d", f_ec_comps, f_cl_comps, f_unit_guid, f_inv_slots, f_ih_key, ih_size);
    if (!k_unit || !k_skillcomp || !k_equip || !f_ec_inst || f_ec_comps <= 0 || f_cl_comps <= 0 || f_unit_guid <= 0 || f_inv_slots <= 0
        || f_ih_key < 0 || ih_size < f_ih_key + 8 || !m_has
        || !dict_lk(ftype_class(k_ec, "_components")) || !dict_lk(ftype_class(k_cl, "_components"))) { H->log(ME, "a unit name was not found"); return 3; }

    /* the 6th UI slot and the views' 5-long arrays must come together (6 UI slots + a 4-long array throws in
       InitSkillUI): any failed hook = the module returns non-zero and the loader removes every hook of it */
    if (hk(UI, "QuickSlotUI", "Init", 3, (void *)my_ui_init, (void **)&o_ui_init)
        || hk(UI, "QuickSlotUI", "InitSkillUI", 3, (void *)my_initskill, (void **)&o_initskill)
        || hk(UI, "QuickSlotUI", "RefreshSkillUI", 3, (void *)my_refreshskill, (void **)&o_refreshskill)
        || hk(UI, "QuickSlotUI", "RefreshSkillSlotUI", 4, (void *)my_slotui, (void **)&o_slotui)
        || hk(UI, "SkillSlotUI", "GetPayload", 0, (void *)my_payload, (void **)&o_payload)
        || hk(UI, "SkillSlotUI", "CanDrop", 1, (void *)my_candrop, (void **)&o_candrop)
        || hk(M, "UnitQuickSlotContainer", "GetSkillAt", 2, (void *)my_skillat, (void **)&o_skillat))
        return 6;
    hide_copy = dsl_get_bool(H, ME, "hide_copy", 0);
    if (dsl_on_setting_change(H, ME, on_setting, NULL) < 0) H->log(ME, "settings: this loader has no live change (hide_copy read at start only)");
    H->log(ME, "weapon skill slot on (client only: 7 hooks, no game data written; hide the copy: %s)", hide_copy ? "on" : "off");
    return 0;
}
