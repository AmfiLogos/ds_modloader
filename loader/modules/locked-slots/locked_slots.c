// Locked Slots (mod id locked-slots): a unit keeps what is in chosen inventory slots when it comes back to base.
// Right-click a slot of a unit's inventory: "Keep on return" locks the SLOT (a small lock shows on it), "Do not keep on
// return" unlocks it. An empty locked slot keeps its lock. The unit still uses, moves and drops those items as usual;
// the inventory Sort leaves locked slots and their items where they are. Vanilla+: nothing else of the game changes.
// Runtime hooks found by il2cpp name (Mac and Windows), GameAssembly unchanged. Build 25846389 (DS_B.0.4.29).
//
// Return to base (every way: result screen "Go to base", forced return, Return Scroll, multiplayer map change): the
// map change ends in MapTransitionCommitService.DropDungeonInventoryItems -> ItemControlProcessor.DropDungeonInventoryItem,
// which calls the private RouteDungeonReturnInventoryItem(map, unit, index, position) once per filled slot whose item is
// a Resource (the stock game keeps every other item type in the bag). Hooked: a locked index returns at once, so that
// stack stays in its slot; every other slot runs the stock code unchanged. Death / retire / building drops are not
// returns and stay stock. Foreign Expedition home results are written into the home save (Custom Modes), not here.
// Sort: ItemControlProcessor.SortInventory(map, entity) compacts all items into slots 0..n-1 in its order. Hooked: the
// items of locked slots are taken out first, the stock sort runs, the sorted items then move to the unlocked slots in
// the same order, and the locked items go back to their own slots.
// Menu: InteractionOptionControl.RegisterInteraction_Inventory(owner, slot, allow) (right-click of an inventory slot,
// empty slots too) is noted; InteractionOptionUI.Show(option) of that option shows one more row, built like the stock
// rows (InteractionType UIInteraction; InteractionInputType UseSkillTarget = the row shows our text as it is). The row
// is added to the option's list only for the Show call, so the game's registered option (cursor, quick action) stays
// stock. Its click is the stock one: OnSelect sends the row's UIInput, InteractionOption_BanAutoFix with the slot in
// ParamIndex2 marked (LKS_MARK | on | slot), so the game makes a BanAutoFixItemRequested order: in multiplayer the
// multiplayer mod sends it through the lockstep (ord event, owner = the unit) and every player runs it at the same
// step; in single player it goes through the same event queue. ItemEventHandler.OnEvent(BanAutoFixItemRequested) is
// hooked: a marked Index sets / clears the lock and the stock handler does not run; any other Index is stock.
// Lock icon: ItemSlotUI.SetItem (every refresh of a slot: unit window, sort, drag, building storage) shows a copy of
// the slot's own auto-repair marker Image, with a small white pixel padlock with a thin dark edge (made at run time from
// the 7 x 8 picture below: no file, no game asset; owner 2026-10-25: "smaller & white"), in the top-left corner of locked
// slots of a unit's inventory (about a fifth of the slot: never over the item count or the picture's center), and hides
// it everywhere else.
// Data: never in the save. <ModData>/locked-slots/locks_<world guid>.txt, one line per unit: its guid and the locked
// slot numbers. Removing the mod = items drop as in the stock game. Multiplayer: the host's locks of its world go to
// every joiner in a join-point block ("LKSL"); a guest keeps them in memory only; the locks are in the world
// fingerprint (desync check). The module needs every player to have it (scope gameplay, sim true).
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

DSL_PLUGIN();

static const DSLHost *H;
static DSLPlugin *ME;
typedef void *Obj;
typedef struct { uint64_t lo, hi; } WGuid;                /* System.Guid by value (16 bytes) */
typedef struct { uint32_t w[5]; } NGuid;                  /* Nullable<Guid> by value (hasValue + Guid, 20 bytes) */
typedef struct { int32_t x, y; } V2i;                     /* Vector2Int by value */
typedef struct { float x, y; } V2;
typedef struct { float r, g, b, a; } Color;
#define IA_SIZE 0xD8                                      /* Refactor.Control.Interaction (checked at init) */
#define UI_SIZE 0x88                                      /* Refactor.Main.InputModule.UIInput (checked at init) */
typedef struct { uint64_t q[IA_SIZE / 8]; } IA;           /* by value: both ABIs pass it as a pointer to a copy */
typedef struct { uint64_t q[UI_SIZE / 8]; } UIIn;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define MC(mi, R, ...) ((R (*)(__VA_ARGS__))(*(void **)(mi)))   /* call through MethodInfo.methodPointer */
#define AHDR 0x20                                         /* il2cpp array: klass, monitor, bounds, max_length, items */
#define OHDR 0x10                                         /* il2cpp object header: value-type field offsets include it */
#define NSLOT 64                                          /* locks per unit: one bit per inventory slot */
#define LKS_MARK 0x4C4B0000                               /* "LK": a lock order in BanAutoFixItemRequested.Index */
#define LKS_ON 0x8000                                     /* + this bit = lock, without = unlock; low 15 bits = slot */
#define LOCK_NAME "LockedSlotsLock"                       /* the lock Image under an ItemSlotUI */

/* ======================================================================================== il2cpp API (exports) */
static struct {
    void *(*class_get_methods)(void *, void **);
    const char *(*method_get_name)(void *);
    uint32_t (*method_get_param_count)(void *);
    void *(*method_get_param)(void *, uint32_t);
    uint8_t (*method_is_generic)(void *);                 /* C bool: low byte only (T2) */
    void *(*class_get_parent)(void *);
    const char *(*class_get_name)(void *);
    void *(*class_from_type)(void *);
    void *(*class_get_element_class)(void *);
    void *(*class_get_type)(void *);
    Obj (*type_get_object)(void *);
    void *(*class_get_field_from_name)(void *, const char *);
    size_t (*field_get_offset)(void *);
    void *(*field_get_type)(void *);
    void (*field_static_get_value)(void *, void *);
    void *(*class_get_method_from_name)(void *, const char *, int);
    int32_t (*class_value_size)(void *, uint32_t *);
    void *(*object_get_class)(Obj);
    uint8_t (*class_is_assignable_from)(void *, void *);   /* C bool: low byte only (T2) */
    void *(*object_get_virtual_method)(Obj, void *);
    Obj (*object_new)(void *);
    Obj (*array_new)(void *, uintptr_t);
    Obj (*string_new)(const char *);
    uintptr_t (*gch_weak)(Obj, uint8_t);                  /* GC handles are pointer-sized in this il2cpp (T1) */
    Obj (*gch_target)(uintptr_t);
    void (*gch_free)(uintptr_t);
} IL;

static int n_missing;
static void miss(const char *what, const char *a, const char *b) { n_missing++; H->log(ME, "not found: %s %s%s%s", what, a, b ? "." : "", b ? b : ""); }
static void *kls(const char *ns, const char *name) { void *k = H->find_class(ns, name); if (!k) miss("class", ns, name); return k; }
static void *class_of_type(void *t) { return t ? IL.class_from_type(t) : NULL; }
/* method by name, parameter count and (optional) parameter class names "Type,Transform"; walks the parents */
static void *msig_q(void *k, const char *name, int argc, const char *sig) {
    for (void *c = k; c; c = IL.class_get_parent(c)) {
        void *it = NULL, *m;
        while ((m = IL.class_get_methods(c, &it))) {
            if (strcmp(IL.method_get_name(m), name) || (int)IL.method_get_param_count(m) != argc) continue;
            if (IL.method_is_generic && (IL.method_is_generic(m) & 1)) continue;
            if (sig) {
                char buf[256] = ""; size_t o = 0;
                for (int i = 0; i < argc; i++) {
                    void *pk = class_of_type(IL.method_get_param(m, (uint32_t)i));
                    o += (size_t)snprintf(buf + o, o < sizeof buf ? sizeof buf - o : 0, "%s%s", i ? "," : "", pk ? IL.class_get_name(pk) : "?");
                }
                if (strcmp(buf, sig)) continue;
            }
            return m;
        }
    }
    return NULL;
}
static void *msig(void *k, const char *name, int argc, const char *sig) {
    void *m = k ? msig_q(k, name, argc, sig) : NULL;
    if (!m || !*(void **)m) { miss("method", k ? IL.class_get_name(k) : "?", name); return NULL; }
    return m;
}
static void *fld(void *k, const char *name) {
    for (void *c = k; c; c = IL.class_get_parent(c)) { void *f = IL.class_get_field_from_name(c, name); if (f) return f; }
    return NULL;
}
static int32_t fo(void *k, const char *name) {             /* field offset by name (T3: never a literal offset) */
    void *f = k ? fld(k, name) : NULL;
    if (!f) { miss("field", k ? IL.class_get_name(k) : "?", name); return -1; }
    return (int32_t)IL.field_get_offset(f);
}
static int32_t fov(void *k, const char *name) { int32_t o = fo(k, name); return o < 0 ? -1 : o - OHDR; }   /* inside a value type */
static void *ftype_class(void *k, const char *name) { void *f = k ? fld(k, name) : NULL; return f ? class_of_type(IL.field_get_type(f)) : NULL; }
static int enum_v(void *k, const char *name, int dflt) {
    void *f = k ? IL.class_get_field_from_name(k, name) : NULL;
    if (!f) { H->log(ME, "enum %s.%s not found, using %d", k ? IL.class_get_name(k) : "?", name, dflt); return dflt; }
    int32_t v = dflt; IL.field_static_get_value(f, &v);
    return v;
}
static int is_a(Obj o, void *k) { return o && k && (IL.class_is_assignable_from(k, IL.object_get_class(o)) & 1); }
static int logn(int *n, int max) { return ++*n <= max; }
static void guid_s(const void *g, char *out) {             /* .NET text form */
    const uint8_t *b = g;
    snprintf(out, 40, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", (unsigned)AT(b, 0, uint32_t), (unsigned)AT(b, 4, uint16_t),
             (unsigned)AT(b, 6, uint16_t), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static int guid_p(const char *s, WGuid *out) {             /* text form -> bytes; 0 = not a guid */
    uint8_t b[16]; int k = 0, hi = -1;
    for (; *s && k < 16; s++) {
        if (*s == '-') continue;
        int v = hexv(*s); if (v < 0) return 0;
        if (hi < 0) hi = v; else { b[k++] = (uint8_t)(hi << 4 | v); hi = -1; }
    }
    if (k != 16) return 0;
    uint8_t g[16];                                          /* Data1..3 little-endian, Data4 as written */
    g[0] = b[3]; g[1] = b[2]; g[2] = b[1]; g[3] = b[0]; g[4] = b[5]; g[5] = b[4]; g[6] = b[7]; g[7] = b[6];
    memcpy(g + 8, b + 8, 8); memcpy(out, g, 16);
    return 1;
}
static int geq(const WGuid *a, const WGuid *b) { return a->lo == b->lo && a->hi == b->hi; }
static int gzero(const WGuid *a) { return !a->lo && !a->hi; }

/* ======================================================================================== game names */
static struct {
    void *unit, *inv, *ih, *slotui, *mainflow, *ctrl, *optui, *option, *ia, *uiin, *image, *object, *comp, *go, *transform,
         *rect, *graphic, *lang, *ev_ban, *uistream, *ec, *complist, *tex, *texture, *sprite, *color32;
} K;
static struct {
    int32_t unit_guid, inv_slots, ih_guid, ih_key, ih_amount, ih_auto, slot_owner, slot_index, slot_inv, slot_autofix,
            mf_campaign, cd_clan, clan_guid, ctrl_option, option_list, ev_guid, ev_index, ec_components, ui_stream,
            optui_list, ih_size, list_items, cd_time, time_tick,
            ia_type, ia_text, ia_possible, ia_index, ia_input, ia_uiinput,           /* inside Interaction */
            ui_type, ui_p2, ui_g1;                                                  /* inside UIInput */
} O;
static struct {
    void *route, *sort, *ban, *reg, *show, *setitem,                                /* hooked */
         *inv_add, *inv_remove, *ih_empty, *ec_tryget, *cl_tryget, *list_add, *list_removeat, *list_count,
         *obj_find, *obj_inst, *obj_set_name, *op_implicit, *comp_tr, *comp_go, *go_active, *go_activeself, *tr_find,
         *tr_last, *rt_amin, *rt_amax, *rt_pivot, *rt_pos, *rt_size, *img_sprite, *img_preserve, *g_color,
         *g_raycast, *lang_get, *stream_add, *optui_select, *ec_inst, *tex_ctor, *tex_set, *tex_apply, *tex_filter,
         *tex_wrap, *spr_create, *obj_hide;
} M;
static struct { int ia_ui, uit_ban, uit_skilltarget, uit_show_inv, ct_inv, find_incl, rgba32, point, clamp, keep; } E;

/* ---- small Unity helpers (alive = UnityEngine.Object op_Implicit: m_CachedPtr != 0, T1) */
static Obj str(const char *s) { return IL.string_new(s); }
static int alive(Obj o) { return o && (MC(M.op_implicit, uint8_t, Obj, void *)(o, M.op_implicit) & 1); }
static Obj tr(Obj c) { return alive(c) ? MC(M.comp_tr, Obj, Obj, void *)(c, M.comp_tr) : NULL; }
static Obj go(Obj c) { return alive(c) ? MC(M.comp_go, Obj, Obj, void *)(c, M.comp_go) : NULL; }
static void set_active(Obj c, int on) {
    Obj g = go(c);
    if (alive(g) && (MC(M.go_activeself, uint8_t, Obj, void *)(g, M.go_activeself) & 1) != (on != 0))
        MC(M.go_active, void, Obj, uint8_t, void *)(g, (uint8_t)(on != 0), M.go_active);
}
static int is_active(Obj c) { Obj g = go(c); return alive(g) && (MC(M.go_activeself, uint8_t, Obj, void *)(g, M.go_activeself) & 1); }
static void *vm(Obj o, void *mi) { return o && mi ? IL.object_get_virtual_method(o, mi) : NULL; }
static Obj type_of(void *k) { return k ? IL.type_get_object(IL.class_get_type(k)) : NULL; }
static Obj find_any(void *k) { return k ? MC(M.obj_find, Obj, Obj, int32_t, void *)(type_of(k), E.find_incl, M.obj_find) : NULL; }
static int list_count(Obj l) { return l ? MC(M.list_count, int32_t, Obj, void *)(l, M.list_count) : 0; }

/* ======================================================================================== texts (game LanguageType order) */
/* 0 en, 1 ko, 2 fr, 3 de, 4 ru, 5 zh-Hans, 6 zh-Hant, 7 ja, 8 es, 9 pt-BR */
static const char *const T_KEEP[10] = {
    "Keep on return", "복귀 시 유지", "Garder au retour", "Bei Rückkehr behalten", "Оставить при возвращении",
    "返回时保留", "返回時保留", "帰還時に保持", "Conservar al volver", "Manter ao retornar" };
static const char *const T_DROP[10] = {
    "Do not keep on return", "복귀 시 유지 안 함", "Ne pas garder au retour", "Bei Rückkehr nicht behalten", "Не оставлять при возвращении",
    "返回时不保留", "返回時不保留", "帰還時に保持しない", "No conservar al volver", "Não manter ao retornar" };
static int lang(void) {
    int v = M.lang_get ? MC(M.lang_get, int32_t, void *)(M.lang_get) : 0;
    return v >= 0 && v < 10 ? v : 0;
}

/* ======================================================================================== the locks of the world */
#define MAXU 2048
static struct { WGuid u; uint64_t m; } T[MAXU];
static int nT;
static WGuid cur_world; static int have_world, from_host;   /* from_host: the table came from the host (guest) */
static int n_kept, n_sorted, n_toggles, n_rows;              /* counters for the log and the test command */
static int64_t last_tick = -1;                               /* campaign tick of the last lock change (log, test) */
static int lk_find(const WGuid *u) { for (int i = 0; i < nT; i++) if (geq(&T[i].u, u)) return i; return -1; }
static uint64_t lk_mask(const WGuid *u) { int i = lk_find(u); return i < 0 ? 0 : T[i].m; }
static void lk_put(const WGuid *u, uint64_t m) {
    int i = lk_find(u);
    if (i >= 0) { if (m) T[i].m = m; else T[i] = T[--nT]; return; }
    if (!m) return;
    if (nT >= MAXU) { H->log(ME, "more than %d units with locks: this lock is not kept", MAXU); return; }
    T[nT].u = *u; T[nT].m = m; nT++;
}
static uint64_t lk_hash(void) {                             /* same on every player; order-free (sum of entries) */
    uint64_t s = 0;
    for (int i = 0; i < nT; i++) {
        if (!T[i].m) continue;
        uint64_t h = 1469598103934665603ULL; const uint8_t *p = (const uint8_t *)&T[i];
        for (size_t k = 0; k < sizeof T[i].u + sizeof T[i].m; k++) { h ^= p[k]; h *= 1099511628211ULL; }
        s += h;
    }
    return s;
}

/* ---- the world now: MainFlow._campaignData.ClanDataContainer.<CampaignGuid> (the save header's CampaignGuid) */
static uintptr_t h_mainflow;
static int world_now(WGuid *out) {
    Obj mf = h_mainflow ? IL.gch_target(h_mainflow) : NULL;
    if (!alive(mf)) {
        if (h_mainflow) IL.gch_free(h_mainflow);
        h_mainflow = 0; mf = find_any(K.mainflow);
        if (!alive(mf)) return 0;
        h_mainflow = IL.gch_weak(mf, 0);
    }
    Obj cd = AT(mf, O.mf_campaign, Obj), clan = cd ? AT(cd, O.cd_clan, Obj) : NULL;
    if (!clan) return 0;
    memcpy(out, (char *)clan + O.clan_guid, 16);
    return !gzero(out);
}
static int64_t tick_now(void) {                             /* CampaignDataContainer.TimeData.<Tick> (-1 = unknown) */
    Obj mf = h_mainflow ? IL.gch_target(h_mainflow) : NULL, cd = mf ? AT(mf, O.mf_campaign, Obj) : NULL;
    Obj t = cd && O.cd_time > 0 ? AT(cd, O.cd_time, Obj) : NULL;
    return t && O.time_tick > 0 ? AT(t, O.time_tick, int64_t) : -1;
}

/* ---- files: <ModData>/locked-slots/locks_<world guid>.txt ("<unit guid> <slot> <slot> ...") */
static char data_dir[1024];
static void file_of(const WGuid *w, char *out, size_t n) { char g[40]; guid_s(w, g); snprintf(out, n, "%s/locks_%s.txt", data_dir, g); }
static void file_load(const WGuid *w) {
    char p[1100]; file_of(w, p, sizeof p);
    FILE *f = data_dir[0] ? fopen(p, "r") : NULL; if (!f) return;
    char line[600];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char *sp = strchr(line, ' '); if (!sp) continue;
        *sp = 0; WGuid u; if (!guid_p(line, &u)) continue;
        uint64_t m = 0; char *e = sp + 1;
        for (;;) { char *q; long v = strtol(e, &q, 10); if (q == e) break; if (v >= 0 && v < NSLOT) m |= 1ULL << v; e = q; }
        lk_put(&u, lk_mask(&u) | m);
    }
    fclose(f);
}
static void file_save(void) {
    if (!data_dir[0] || !have_world) return;
    char p[1100], tmp[1120]; file_of(&cur_world, p, sizeof p); snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "w");
    if (!f) { H->log(ME, "cannot write %s", tmp); return; }
    fprintf(f, "# Locked Slots: a unit's guid, then its locked inventory slots (0 = the first slot)\n");
    for (int i = 0; i < nT; i++) {
        if (!T[i].m) continue;
        char g[40]; guid_s(&T[i].u, g); fprintf(f, "%s", g);
        for (int s = 0; s < NSLOT; s++) if (T[i].m >> s & 1) fprintf(f, " %d", s);
        fprintf(f, "\n");
    }
    fclose(f);
#ifdef _WIN32
    if (!MoveFileExA(tmp, p, MOVEFILE_REPLACE_EXISTING)) H->log(ME, "cannot replace %s", p);
#else
    if (rename(tmp, p)) H->log(ME, "cannot replace %s", p);
#endif
}

/* ---- multiplayer (mp_api.h of ds_multiplayer, version 1 prefix of the table; fields are only appended) */
typedef struct { char tag[4]; size_t (*size)(void); size_t (*write)(uint8_t *, size_t); void (*read)(const uint8_t *, size_t);
                 int (*override_save)(const uint8_t **, size_t *, const uint8_t **, size_t *); } mp_joinpoint;
typedef struct {
    uint32_t version, size;
    int (*role)(void); void *is_host, *my_uid, *players, *present_mask, *session_busy, *register_host_choice,
        *register_esc_button, *mail_send, *register_mail, *register_sim_cmd, *send_sim_cmd, *register_tick;
    int (*register_fingerprint)(uint64_t (*mix)(void));
    int (*register_joinpoint)(const mp_joinpoint *jp);
    void *register_world_build, *register_order_filter, *ui_set_flag, *register_unit_tag, *register_teams_portal, *modset_lines;
    int (*register_rig_cmd)(const char *prefix, char *(*exec)(const char *cmd, const char *line));
} mp_api;
enum { ROLE_NONE = 0, ROLE_HOST = 1, ROLE_GUEST = 2 };
static const mp_api *mp;
#define MP_HAS(f) (mp && mp->size >= offsetof(mp_api, f) + sizeof(mp->f) && mp->f)
static int role(void) { return mp && mp->role ? mp->role() : ROLE_NONE; }

/* the host's locks at the last join point (guest): used when the world of that guid is loaded */
static WGuid jp_world; static int jp_pending, njp;
static struct { WGuid u; uint64_t m; } *JP;

/* the table follows the world: a new world guid (or a session start / end) loads that world's table */
static void sync_world(void) {
    WGuid w; if (!world_now(&w)) return;
    int guest = role() == ROLE_GUEST;
    if (!guest) jp_pending = 0;
    if (have_world && geq(&w, &cur_world) && from_host == guest) return;
    nT = 0; cur_world = w; have_world = 1; from_host = guest;
    const char *src = "its file";
    if (guest) {
        src = "the host (none for this world)";
        if (jp_pending && geq(&jp_world, &w)) { for (int i = 0; i < njp; i++) lk_put(&JP[i].u, JP[i].m); src = "the host"; }
    } else file_load(&w);
    char g[40]; guid_s(&w, g);
    H->log(ME, "world %s: %d unit(s) with locked slots (from %s)", g, nT, src);
}

#define JP_TAG "LKSL"
static uint8_t *jpb; static size_t jpn;
static size_t jp_size(void) {                               /* host: the block of the world now (0 = no lock) */
    sync_world();
    free(jpb); jpb = NULL; jpn = 0;
    int n = 0; for (int i = 0; i < nT; i++) if (T[i].m) n++;
    if (!have_world || !n) return 0;
    jpn = 1 + 16 + 4 + (size_t)n * 24; jpb = malloc(jpn);
    uint8_t *p = jpb; *p++ = 1; memcpy(p, &cur_world, 16); p += 16; uint32_t c = (uint32_t)n; memcpy(p, &c, 4); p += 4;
    for (int i = 0; i < nT; i++) if (T[i].m) { memcpy(p, &T[i].u, 16); memcpy(p + 16, &T[i].m, 8); p += 24; }
    return jpn;
}
static size_t jp_write(uint8_t *out, size_t cap) {
    if (!jpb || cap < jpn) return 0;
    memcpy(out, jpb, jpn); return jpn;
}
static void jp_read(const uint8_t *p, size_t n) {          /* guest, before the join-point save loads */
    if (role() != ROLE_GUEST) return;
    free(JP); JP = NULL; njp = 0; memset(&jp_world, 0, sizeof jp_world); jp_pending = 1; have_world = 0;
    if (!p || n < 21 || p[0] != 1) { H->log(ME, "join point: the host has no locked slots"); return; }
    uint32_t c; memcpy(&c, p + 17, 4);
    if ((size_t)c > MAXU || 21 + (size_t)c * 24 > n) { H->log(ME, "join point: damaged block (%zu bytes): no locks", n); return; }
    memcpy(&jp_world, p + 1, 16);
    JP = calloc(c ? c : 1, sizeof *JP);
    for (uint32_t i = 0; i < c; i++) { memcpy(&JP[i].u, p + 21 + i * 24, 16); memcpy(&JP[i].m, p + 21 + i * 24 + 16, 8); }
    njp = (int)c;
    char g[40]; guid_s(&jp_world, g); H->log(ME, "join point: the host's locks of world %s for %d unit(s)", g, njp);
}
static uint64_t fp_mix(void) { sync_world(); return lk_hash(); }

/* ======================================================================================== lock icon on the slots */
#define MAXS 256
static struct { uintptr_t h; WGuid u; int idx; } S[MAXS];    /* slots that showed a unit's inventory (weak handles) */
static int nS, sS;
/* the padlock: W white, '.' clear; every clear pixel next to a white one (8 neighbours) is the dark edge */
#define LW 7
#define LH 8
static const char LOCK_ART[LH][LW + 1] = {
    ".......",
    "..WWW..",
    ".W...W.",
    ".WWWWW.",
    ".WW.WW.",
    ".WW.WW.",
    ".WWWWW.",
    "......." };
typedef struct { float x, y, w, h; } Rect;
static uintptr_t h_sprite;
static Obj lock_sprite(void) {
    Obj s = h_sprite ? IL.gch_target(h_sprite) : NULL;
    if (alive(s)) return s;
    if (h_sprite) { IL.gch_free(h_sprite); h_sprite = 0; }
    Obj tex = IL.object_new(K.tex);
    if (!tex) return NULL;
    MC(M.tex_ctor, void, Obj, int32_t, int32_t, int32_t, uint8_t, void *)(tex, LW, LH, E.rgba32, 0, M.tex_ctor);   /* no mipmaps */
    Obj px = IL.array_new(K.color32, LW * LH);               /* Color32[], rows from the bottom */
    if (!px) return NULL;
    for (int y = 0; y < LH; y++) for (int x = 0; x < LW; x++) {
        uint8_t *c = (uint8_t *)px + AHDR + 4 * (size_t)((LH - 1 - y) * LW + x);
        int w = LOCK_ART[y][x] == 'W', edge = 0;
        for (int dy = -1; dy <= 1 && !w; dy++) for (int dx = -1; dx <= 1; dx++) {
            int yy = y + dy, xx = x + dx;
            if (yy >= 0 && yy < LH && xx >= 0 && xx < LW && LOCK_ART[yy][xx] == 'W') edge = 1;
        }
        if (w) { c[0] = c[1] = c[2] = c[3] = 255; }
        else if (edge) { c[0] = 22; c[1] = 18; c[2] = 26; c[3] = 210; }     /* the game's dark panel tone, a little see-through */
        else c[0] = c[1] = c[2] = c[3] = 0;
    }
    MC(M.tex_set, void, Obj, Obj, void *)(tex, px, M.tex_set);
    MC(M.tex_filter, void, Obj, int32_t, void *)(tex, E.point, M.tex_filter);   /* crisp pixels, as the game's art */
    MC(M.tex_wrap, void, Obj, int32_t, void *)(tex, E.clamp, M.tex_wrap);
    MC(M.tex_apply, void, Obj, void *)(tex, M.tex_apply);
    s = MC(M.spr_create, Obj, Obj, Rect, V2, void *)(tex, (Rect){0, 0, LW, LH}, (V2){0.5f, 0.5f}, M.spr_create);
    if (!alive(s)) return NULL;
    MC(M.obj_hide, void, Obj, int32_t, void *)(tex, E.keep, M.obj_hide);       /* not freed by an asset clean-up */
    MC(M.obj_hide, void, Obj, int32_t, void *)(s, E.keep, M.obj_hide);
    h_sprite = IL.gch_weak(s, 0);
    return s;
}
static Obj lock_img(Obj slot, int create) {                 /* the slot's lock Image (a child), NULL = none */
    Obj t = tr(slot); if (!t) return NULL;
    Obj c = MC(M.tr_find, Obj, Obj, Obj, void *)(t, str(LOCK_NAME), M.tr_find);
    if (alive(c)) return c;
    if (!create) return NULL;
    Obj tmpl = AT(slot, O.slot_autofix, Obj), spr = lock_sprite();
    if (!alive(tmpl) || !spr) return NULL;
    Obj img = MC(M.obj_inst, Obj, Obj, Obj, void *)(tmpl, t, M.obj_inst);      /* a copy of the auto-repair marker */
    if (!alive(img)) return NULL;
    MC(M.obj_set_name, void, Obj, Obj, void *)(img, str(LOCK_NAME), M.obj_set_name);
    MC(M.img_sprite, void, Obj, Obj, void *)(img, spr, M.img_sprite);
    MC(M.img_preserve, void, Obj, uint8_t, void *)(img, 1, M.img_preserve);
    void *m = vm(img, M.g_color); if (m) MC(m, void, Obj, Color, void *)(img, (Color){1, 1, 1, 1}, m);
    m = vm(img, M.g_raycast); if (m) MC(m, void, Obj, uint8_t, void *)(img, 0, m);   /* right-click still reaches the slot */
    Obj rt = tr(img);
    /* anchors in the slot's own rect (it scales with the slot and the UI scale): a box 6..27 % from the left and
       7..31 % from the top (24 % high = the 8-pixel picture at about a fifth of the slot); preserveAspect keeps it 7:8 */
    MC(M.rt_amin, void, Obj, V2, void *)(rt, (V2){0.06f, 0.69f}, M.rt_amin); MC(M.rt_amax, void, Obj, V2, void *)(rt, (V2){0.27f, 0.93f}, M.rt_amax);
    MC(M.rt_pivot, void, Obj, V2, void *)(rt, (V2){0, 1}, M.rt_pivot);
    MC(M.rt_pos, void, Obj, V2, void *)(rt, (V2){0, 0}, M.rt_pos);
    MC(M.rt_size, void, Obj, V2, void *)(rt, (V2){0, 0}, M.rt_size);
    MC(M.tr_last, void, Obj, void *)(rt, M.tr_last);        /* drawn over the item icon */
    static int n_made; if (logn(&n_made, 3)) H->log(ME, "lock icon made (white padlock %dx%d, top-left of the slot)", LW, LH);
    return img;
}
static void remember(Obj slot, const WGuid *u, int idx) {
    for (int i = 0; i < nS; i++) if (IL.gch_target(S[i].h) == slot) { S[i].u = *u; S[i].idx = idx; return; }
    int i = nS < MAXS ? nS++ : (sS++ % MAXS);
    if (S[i].h) IL.gch_free(S[i].h);
    S[i].h = IL.gch_weak(slot, 0); S[i].u = *u; S[i].idx = idx;
}
static int known_unit(const WGuid *u) { for (int i = 0; i < nS; i++) if (geq(&S[i].u, u)) return 1; return 0; }
static int n_ovl;
static void overlay(Obj slot) {
    if (!alive(slot)) return;
    Obj owner = AT(slot, O.slot_owner, Obj);
    int idx = AT(slot, O.slot_index, int32_t), inv = AT(slot, O.slot_inv, uint8_t) & 1;
    int unit = inv && is_a(owner, K.unit) && idx >= 0 && idx < NSLOT;
    static int n_seen;                                     /* the first slots seen: which flag the slot kinds carry */
    if (owner && logn(&n_seen, 12)) H->log(ME, "slot ui: owner %s, inventory flag %d, index %d", IL.class_get_name(IL.object_get_class(owner)), inv, idx);
    Obj img = lock_img(slot, 0);
    if (!unit) { if (img) set_active(img, 0); return; }
    WGuid u; memcpy(&u, (char *)owner + O.unit_guid, 16);
    remember(slot, &u, idx);
    sync_world();
    int on = (int)(lk_mask(&u) >> idx & 1);
    if (on && !img) img = lock_img(slot, 1);
    if (img) set_active(img, on);
    if (on && logn(&n_ovl, 5)) { char g[40]; guid_s(&u, g); H->log(ME, "lock icon on: unit %.8s slot %d", g, idx); }
}
static void refresh_unit(const WGuid *u) {                  /* after a lock change: the shown slots of that unit */
    for (int i = 0; i < nS; i++) {
        if (!geq(&S[i].u, u)) continue;
        Obj s = IL.gch_target(S[i].h);
        if (alive(s)) overlay(s);
    }
}

/* ======================================================================================== the lock order (sim) */
static void apply(const WGuid *u, int slot, int on) {
    if (gzero(u) || slot < 0 || slot >= NSLOT) return;
    sync_world();
    if (!have_world) { H->log(ME, "lock order with no world: ignored"); return; }
    uint64_t m = lk_mask(u), b = 1ULL << slot;
    lk_put(u, on ? (m | b) : (m & ~b));
    n_toggles++; last_tick = tick_now();
    if (role() != ROLE_GUEST) file_save();                  /* a guest keeps the host's locks in memory only */
    char g[40]; guid_s(u, g);
    H->log(ME, "unit %s slot %d %s at tick %lld (%d unit(s) with locks)", g, slot, on ? "locked: kept on return" : "unlocked", (long long)last_tick, nT);
    refresh_unit(u);
}
static void (*o_ban)(Obj, Obj, const void *);
static void my_ban(Obj self, Obj ev, const void *mi) {      /* ItemEventHandler.OnEvent(BanAutoFixItemRequested) */
    int32_t idx = ev ? AT(ev, O.ev_index, int32_t) : 0;
    if (((uint32_t)idx & 0xFFFF0000u) != LKS_MARK) { o_ban(self, ev, mi); return; }
    WGuid u; memcpy(&u, (char *)ev + O.ev_guid, 16);
    apply(&u, idx & 0x7FFF, (idx & LKS_ON) != 0);           /* ours: the stock auto-repair code does not run */
}

/* ======================================================================================== return to base (sim) */
static int n_kept_log;
static void (*o_route)(Obj, int32_t, Obj, int32_t, V2i, const void *);
static void my_route(Obj self, int32_t map, Obj ent, int32_t index, V2i pos, const void *mi) {
    if (index >= 0 && index < NSLOT && is_a(ent, K.unit)) {
        sync_world();
        WGuid u; memcpy(&u, (char *)ent + O.unit_guid, 16);
        if (lk_mask(&u) >> index & 1) {                     /* locked: the stack stays in the slot */
            n_kept++;
            if (logn(&n_kept_log, 40)) { char g[40]; guid_s(&u, g); H->log(ME, "return: unit %.8s keeps slot %d", g, index); }
            return;
        }
    }
    o_route(self, map, ent, index, pos, mi);
}

/* ======================================================================================== sort (sim) */
static Obj inv_of(Obj ent) {                                /* EntityComponent._instance._components[ent] -> Inventory */
    Obj ec = NULL; IL.field_static_get_value(M.ec_inst, &ec);
    Obj comps = ec ? AT(ec, O.ec_components, Obj) : NULL, list = NULL, c = NULL;
    if (!comps || !(MC(M.ec_tryget, uint8_t, Obj, Obj, Obj *, void *)(comps, ent, &list, M.ec_tryget) & 1) || !list) return NULL;
    if (!(MC(M.cl_tryget, uint8_t, Obj, int32_t, Obj *, void *)(list, E.ct_inv, &c, M.cl_tryget) & 1)) return NULL;
    return c && IL.object_get_class(c) == K.inv ? c : NULL;  /* exactly InventoryComponent (EquipmentComponent derives from it) */
}
typedef struct { WGuid g; Obj key; int32_t amount; uint8_t autofix; } Held;
static int held_at(Obj inv, int i, Held *h) {               /* 1 = a filled slot, copied into h */
    Obj arr = AT(inv, O.inv_slots, Obj);
    if (!arr || (uintptr_t)i >= AT(arr, 0x18, uintptr_t)) return 0;
    char *e = (char *)arr + AHDR + (size_t)i * (size_t)O.ih_size;
    if (MC(M.ih_empty, uint8_t, void *, void *)(e, M.ih_empty) & 1) return 0;
    memcpy(&h->g, e + O.ih_guid, 16); h->key = AT(e, O.ih_key, Obj); h->amount = AT(e, O.ih_amount, int32_t); h->autofix = AT(e, O.ih_auto, uint8_t) & 1;
    return 1;
}
static void inv_remove(Obj inv, int i) { MC(M.inv_remove, void, Obj, int32_t, void *)(inv, i, M.inv_remove); }
static void inv_add(Obj inv, int i, const Held *h) {
    MC(M.inv_add, void, Obj, int32_t, WGuid, Obj, int32_t, uint8_t, void *)(inv, i, h->g, h->key, h->amount, h->autofix, M.inv_add);
}
static int n_sort_log;
static void (*o_sort)(Obj, int32_t, Obj, const void *);
static void my_sort(Obj self, int32_t map, Obj ent, const void *mi) {
    uint64_t m = 0; WGuid u = {0, 0};
    if (is_a(ent, K.unit)) { sync_world(); memcpy(&u, (char *)ent + O.unit_guid, 16); m = lk_mask(&u); }
    Obj inv = m ? inv_of(ent) : NULL;
    Obj arr = inv ? AT(inv, O.inv_slots, Obj) : NULL;
    int cap = arr ? (int)AT(arr, 0x18, uintptr_t) : 0; if (cap > NSLOT) cap = NSLOT;
    if (!inv || !cap) { o_sort(self, map, ent, mi); return; }
    Held keep[NSLOT]; int kept[NSLOT], nk = 0;
    for (int i = 0; i < cap; i++) if (m >> i & 1 && held_at(inv, i, &keep[nk])) { kept[nk++] = i; inv_remove(inv, i); }
    o_sort(self, map, ent, mi);                             /* the stock order, on the unlocked items only */
    int pos[NSLOT], np = 0, tgt[NSLOT], nt = 0; Held h;
    for (int i = 0; i < cap; i++) { if (held_at(inv, i, &h)) pos[np++] = i; if (!(m >> i & 1)) tgt[nt++] = i; }
    int moved = 0, bad = 0;
    for (int j = np - 1; j >= 0; j--) {                     /* last first: a target is never a slot still to move */
        if (j >= nt) { bad++; continue; }
        if (pos[j] == tgt[j]) continue;
        if (held_at(inv, tgt[j], &h)) { bad++; continue; }
        if (!held_at(inv, pos[j], &h)) { bad++; continue; }
        inv_remove(inv, pos[j]); inv_add(inv, tgt[j], &h); moved++;
    }
    for (int k = 0; k < nk; k++) {                          /* the locked items back to their own slots */
        int at = kept[k];
        if (held_at(inv, at, &h)) {                         /* never over an item: the first empty slot instead */
            bad++; at = -1;
            for (int i = 0; i < cap && at < 0; i++) if (!held_at(inv, i, &h)) at = i;
            if (at < 0) { H->log(ME, "sort: no empty slot for a locked item (should not happen)"); continue; }
        }
        inv_add(inv, at, &keep[k]);
    }
    n_sorted++;
    if (logn(&n_sort_log, 40) || bad) { char g[40]; guid_s(&u, g); H->log(ME, "sort: unit %.8s, %d locked item(s) stayed, %d moved past locked slots%s", g, nk, moved, bad ? " (UNEXPECTED layout, see above)" : ""); }
}

/* ======================================================================================== the right-click menu (UI) */
static uintptr_t h_rec; static WGuid rec_u; static int rec_slot = -1;
static void (*o_reg)(Obj, WGuid, int32_t, uint8_t, const void *);
static void my_reg(Obj self, WGuid g, int32_t slot, uint8_t allow, const void *mi) {   /* RegisterInteraction_Inventory */
    o_reg(self, g, slot, allow, mi);
    if (h_rec) { IL.gch_free(h_rec); h_rec = 0; }
    rec_slot = -1;
    if (slot < 0 || slot >= NSLOT || !known_unit(&g)) return;   /* a unit's inventory window (not a building's storage) */
    Obj opt = AT(self, O.ctrl_option, Obj);
    if (!opt) return;
    h_rec = IL.gch_weak(opt, 0); rec_u = g; rec_slot = slot;
}
static int add_row(Obj list, const WGuid *u, int slot) {
    sync_world();
    int on = !(lk_mask(u) >> slot & 1);                     /* the row does the other state */
    IA ia; memset(&ia, 0, sizeof ia);
    char *p = (char *)&ia, *in = p + O.ia_uiinput;
    AT(p, O.ia_type, int32_t) = E.ia_ui;
    AT(p, O.ia_text, Obj) = str(on ? T_KEEP[lang()] : T_DROP[lang()]);   /* shown as it is (UseSkillTarget row) */
    AT(p, O.ia_possible, uint8_t) = 1;
    AT(p, O.ia_index, int32_t) = slot;
    AT(p, O.ia_input, int32_t) = E.uit_skilltarget;
    AT(in, O.ui_type, int32_t) = E.uit_ban;                 /* the click sends the stock auto-repair order, marked */
    AT(in, O.ui_p2, int32_t) = (int32_t)(LKS_MARK | (on ? LKS_ON : 0) | (uint32_t)slot);
    memcpy(in + O.ui_g1, u, 16);
    MC(M.list_add, void, Obj, IA, void *)(list, ia, M.list_add);
    return 1;
}
static int n_row_log;
static void (*o_show)(Obj, Obj, const void *);
static void my_show(Obj self, Obj option, const void *mi) {   /* InteractionOptionUI.Show(InteractionOption) */
    Obj rec = h_rec ? IL.gch_target(h_rec) : NULL, list = NULL;
    int added = 0;
    if (option && rec == option && rec_slot >= 0) {
        list = AT(option, O.option_list, Obj);
        if (list) added = add_row(list, &rec_u, rec_slot);
    }
    if (h_rec) { IL.gch_free(h_rec); h_rec = 0; }
    rec_slot = -1;
    o_show(self, option, mi);
    if (added) {                                            /* the game's option stays stock (cursor, quick action) */
        int n = list_count(list);
        if (n > 0) MC(M.list_removeat, void, Obj, int32_t, void *)(list, n - 1, M.list_removeat);
        n_rows++;
        if (logn(&n_row_log, 10)) H->log(ME, "menu row shown (%d rows)", n);
    }
}
static void (*o_setitem)(Obj, Obj, NGuid, const void *);
static void my_setitem(Obj self, Obj key, NGuid g, const void *mi) {   /* ItemSlotUI.SetItem(string, Guid?) */
    o_setitem(self, key, g, mi);
    overlay(self);
}

/* ======================================================================================== test commands (rig socket only) */
/* Reachable only through the multiplayer mod's test control channel (DSRIG_CTL=1 copies, T7); a player never has it. */
static char *jdup(const char *s) { size_t n = strlen(s) + 1; char *r = malloc(n); memcpy(r, s, n); return r; }
static int jstr(const char *line, const char *key, char *out, size_t n) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(line, pat); if (!p) return 0;
    p = strchr(p + strlen(pat), '"'); if (!p) return 0; p++;
    size_t k = 0; while (*p && *p != '"' && k + 1 < n) out[k++] = *p++;
    out[k] = 0; return 1;
}
static int jint(const char *line, const char *key, int dflt) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(line, pat); if (!p) return dflt;
    p = strchr(p + strlen(pat), ':'); return p ? atoi(p + 1) : dflt;
}
static Obj shown_ui(void) { return find_any(K.optui); }
static Obj dict_value_of(Obj d, void *want) {               /* the first value of class `want` in a Dictionary (corefx layout) */
    void *k = d ? IL.object_get_class(d) : NULL, *fe = k ? fld(k, "_entries") : NULL;
    void *ec = fe ? IL.class_get_element_class(class_of_type(IL.field_get_type(fe))) : NULL;
    if (!ec) return NULL;
    int32_t oe = fo(k, "_entries"), oc = fo(k, "_count"), oh = fov(ec, "hashCode"), ov = fov(ec, "value"), sz = IL.class_value_size(ec, NULL);
    Obj arr = AT(d, oe, Obj); int n = AT(d, oc, int32_t);
    for (int i = 0; arr && i < n && (uintptr_t)i < AT(arr, 0x18, uintptr_t); i++) {
        char *e = (char *)arr + AHDR + (size_t)i * (size_t)sz;
        Obj v = AT(e, oh, int32_t) >= 0 ? AT(e, ov, Obj) : NULL;
        if (v && IL.object_get_class(v) == want) return v;
    }
    return NULL;
}
static char *t_exec(const char *cmd, const char *line) {
    static char b[16384]; size_t o = 0;
#define PUT(...) (o += (size_t)snprintf(b + o, o < sizeof b ? sizeof b - o : 0, __VA_ARGS__))
    if (!strcmp(cmd, "lks_state")) {
        sync_world();
        char w[40] = ""; if (have_world) guid_s(&cur_world, w);
        PUT("{\"ok\":true,\"world\":\"%s\",\"role\":%d,\"kept\":%d,\"sorted\":%d,\"toggles\":%d,\"rows\":%d,\"last_tick\":%lld,\"units\":[", w, role(), n_kept, n_sorted, n_toggles, n_rows, (long long)last_tick);
        for (int i = 0; i < nT; i++) {
            char g[40]; guid_s(&T[i].u, g); PUT("%s{\"unit\":\"%s\",\"slots\":[", i ? "," : "", g);
            int k = 0; for (int s = 0; s < NSLOT; s++) if (T[i].m >> s & 1) PUT("%s%d", k++ ? "," : "", s);
            PUT("]}");
        }
        PUT("],\"hash\":\"%016llx\"}", (unsigned long long)lk_hash());
        return jdup(b);
    }
    if (!strcmp(cmd, "lks_bag")) {                          /* a unit's bag as this player's sim has it: key x amount per slot */
        char us[64] = ""; WGuid u; jstr(line, "unit", us, sizeof us);
        if (!guid_p(us, &u)) return jdup("{\"ok\":false,\"error\":\"need unit\"}");
        Obj ec = NULL; IL.field_static_get_value(M.ec_inst, &ec);
        Obj d = ec ? AT(ec, O.ec_components, Obj) : NULL, ent = NULL;
        void *k = d ? IL.object_get_class(d) : NULL, *fe = k ? fld(k, "_entries") : NULL;
        void *el = fe ? IL.class_get_element_class(class_of_type(IL.field_get_type(fe))) : NULL;
        if (el) {
            int32_t oe = fo(k, "_entries"), oc = fo(k, "_count"), oh = fov(el, "hashCode"), ok = fov(el, "key"), sz = IL.class_value_size(el, NULL);
            Obj arr = AT(d, oe, Obj); int n = AT(d, oc, int32_t);
            for (int i = 0; arr && !ent && i < n && (uintptr_t)i < AT(arr, 0x18, uintptr_t); i++) {
                char *e = (char *)arr + AHDR + (size_t)i * (size_t)sz; Obj key = AT(e, oh, int32_t) >= 0 ? AT(e, ok, Obj) : NULL;
                if (is_a(key, K.unit) && !memcmp((char *)key + O.unit_guid, &u, 16)) ent = key;
            }
        }
        Obj inv = ent ? inv_of(ent) : NULL;
        if (!inv) return jdup("{\"ok\":false,\"error\":\"unit or bag not found\"}");
        PUT("{\"ok\":true,\"slots\":[");
        Obj arr = AT(inv, O.inv_slots, Obj); int cap = arr ? (int)AT(arr, 0x18, uintptr_t) : 0;
        for (int i = 0; i < cap; i++) {
            Held h; char key[80] = "";
            if (held_at(inv, i, &h)) { int32_t L = h.key ? AT(h.key, 0x10, int32_t) : 0; size_t q = 0;
                for (int32_t c = 0; c < L && q + 1 < sizeof key; c++) { uint16_t ch = ((uint16_t *)((char *)h.key + 0x14))[c]; key[q++] = ch < 0x80 && ch != '"' && ch != '\\' ? (char)ch : '?'; }
                key[q] = 0; PUT("%s[\"%s\",%d]", i ? "," : "", key, h.amount); }
            else PUT("%snull", i ? "," : "");
        }
        PUT("]}");
        return jdup(b);
    }
    if (!strcmp(cmd, "lks_open")) {                         /* the inventory window of the selected unit(s): presenter Activate */
        WGuid w; world_now(&w);
        Obj mf = h_mainflow ? IL.gch_target(h_mainflow) : NULL;
        void *k_vpc = H->find_class("Refactor.View", "ViewPresenterContainer"), *k_ip = H->find_class("Refactor.UI", "EntityInventoryPresenter");
        void *f_vp = fld(K.mainflow, "_viewPresenters"), *f_d = k_vpc ? fld(k_vpc, "_viewPresenters") : NULL;
        void *m_act = k_ip ? msig_q(k_ip, "Activate", 0, NULL) : NULL;
        Obj vpc = mf && f_vp ? AT(mf, (int32_t)IL.field_get_offset(f_vp), Obj) : NULL;
        Obj pr = vpc && f_d ? dict_value_of(AT(vpc, (int32_t)IL.field_get_offset(f_d), Obj), k_ip) : NULL;   /* never a throwing lookup */
        if (!pr || !m_act) return jdup("{\"ok\":false,\"error\":\"no EntityInventoryPresenter\"}");
        MC(m_act, void, Obj, void *)(pr, m_act);
        void *m_ref = msig_q(k_ip, "CommonRefresh", 1, NULL);   /* Activate only shows the view: fill it now */
        if (m_ref) MC(m_ref, void, Obj, uint8_t, void *)(pr, 1, m_ref);
        return jdup(m_ref ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"no CommonRefresh\"}");
    }
    if (!strcmp(cmd, "lks_menu")) {                         /* the stock right-click of a slot: UIInput ShowInteraction_Inventory */
        char us[64] = ""; WGuid u; jstr(line, "unit", us, sizeof us);
        if (!guid_p(us, &u)) return jdup("{\"ok\":false,\"error\":\"need unit\"}");
        Obj ui = shown_ui(), stream = ui ? AT(ui, O.ui_stream, Obj) : NULL;
        if (!stream) return jdup("{\"ok\":false,\"error\":\"no InteractionOptionUI input stream\"}");
        UIIn in; memset(&in, 0, sizeof in); char *p = (char *)&in;
        AT(p, O.ui_type, int32_t) = E.uit_show_inv; AT(p, O.ui_p2, int32_t) = jint(line, "slot", 0); memcpy(p + O.ui_g1, &u, 16);
        MC(M.stream_add, void, Obj, UIIn, void *)(stream, in, M.stream_add);
        return jdup("{\"ok\":true}");
    }
    if (!strcmp(cmd, "lks_rows") || !strcmp(cmd, "lks_pick")) {   /* the rows the menu shows now; pick = click ours */
        Obj ui = shown_ui(), l = ui ? AT(ui, O.optui_list, Obj) : NULL;
        int n = list_count(l), pick = !strcmp(cmd, "lks_pick"), picked = 0;
        Obj items = l ? AT(l, O.list_items, Obj) : NULL;    /* List<T>._items */
        PUT("{\"ok\":true,\"rows\":[");
        for (int i = 0; items && i < n; i++) {
            char *e = (char *)items + AHDR + (size_t)i * IA_SIZE;
            Obj t = AT(e, O.ia_text, Obj); char s[200] = ""; size_t k = 0;
            for (int32_t c = 0; t && c < AT(t, 0x10, int32_t) && k + 8 < sizeof s; c++) {
                uint16_t ch = ((uint16_t *)((char *)t + 0x14))[c];
                if (ch < 0x80 && ch != '"' && ch != '\\') s[k++] = (char)ch;
                else k += (size_t)snprintf(s + k, sizeof s - k, "\\u%04x", ch);
            }
            s[k] = 0;
            int32_t ui_index = AT(e + O.ia_uiinput, O.ui_p2, int32_t);
            int ours = ((uint32_t)ui_index & 0xFFFF0000u) == LKS_MARK;
            PUT("%s{\"type\":%d,\"input\":%d,\"text\":\"%s\",\"ours\":%d,\"lock\":%d}", i ? "," : "", AT(e, O.ia_type, int32_t),
                AT(e, O.ia_input, int32_t), s, ours, ours ? (ui_index & LKS_ON) != 0 : -1);
            if (pick && ours && !picked) {                  /* the element's click: InteractionOptionUI.OnSelect(row) */
                IA ia; memcpy(&ia, e, sizeof ia);
                MC(M.optui_select, void, Obj, IA, void *)(ui, ia, M.optui_select); picked = 1;
            }
        }
        PUT("],\"picked\":%d}", picked);
        return jdup(b);
    }
    if (!strcmp(cmd, "lks_icons")) {                        /* the lock icons on the slots shown now */
        PUT("{\"ok\":true,\"slots\":[");
        int k = 0;
        for (int i = 0; i < nS; i++) {
            Obj s = IL.gch_target(S[i].h); if (!alive(s)) continue;
            Obj img = lock_img(s, 0); char g[40]; guid_s(&S[i].u, g);
            PUT("%s{\"unit\":\"%s\",\"slot\":%d,\"icon\":%d}", k++ ? "," : "", g, S[i].idx, img ? is_active(img) : 0);
        }
        PUT("]}");
        return jdup(b);
    }
#undef PUT
    return NULL;
}

/* ======================================================================================== init */
static int resolve(void) {
    const char *R = "Refactor", *RM = "Refactor.Main", *RUI = "Refactor.UI";
    K.unit = kls(R, "UnitEntity"); K.inv = kls(R, "InventoryComponent"); K.ih = kls(R, "ItemHolder");
    K.slotui = kls(RUI, "ItemSlotUI"); K.mainflow = kls(RM, "MainFlow"); K.ctrl = kls("Refactor.Control", "InteractionOptionControl");
    K.optui = kls(RUI, "InteractionOptionUI"); K.option = kls(RUI, "InteractionOption"); K.ia = kls("Refactor.Control", "Interaction");
    K.uiin = kls("Refactor.Main.InputModule", "UIInput"); K.uistream = kls("Refactor.Main.InputModule", "UIInputStream");
    K.image = kls("UnityEngine.UI", "Image"); K.graphic = kls("UnityEngine.UI", "Graphic");
    K.object = kls("UnityEngine", "Object"); K.comp = kls("UnityEngine", "Component"); K.go = kls("UnityEngine", "GameObject");
    K.transform = kls("UnityEngine", "Transform"); K.rect = kls("UnityEngine", "RectTransform");
    K.lang = kls("Refactor.Setting", "LanguageSetting");
    K.tex = kls("UnityEngine", "Texture2D"); K.texture = kls("UnityEngine", "Texture"); K.sprite = kls("UnityEngine", "Sprite");
    K.color32 = kls("UnityEngine", "Color32");
    K.ev_ban = kls("Refactor.Main.Event", "BanAutoFixItemRequested");
    K.ec = kls(RM, "EntityComponent"); K.complist = kls(RM, "ComponentList");
    void *k_proc = kls("Refactor.Main.Processor", "ItemControlProcessor"), *k_ieh = kls("Refactor.Main.Event", "ItemEventHandler");
    void *k_cd = kls(RM, "CampaignDataContainer"), *k_clan = kls(R, "ClanDataContainer"), *k_static = kls(RUI, "StaticUI_Base");
    if (n_missing) return 1;

    O.unit_guid = fo(K.unit, "<Guid>k__BackingField"); O.inv_slots = fo(K.inv, "_itemSlots");
    O.ih_guid = fov(K.ih, "Guid"); O.ih_key = fov(K.ih, "Key"); O.ih_amount = fov(K.ih, "Amount"); O.ih_auto = fov(K.ih, "AutoFixed");
    O.ih_size = IL.class_value_size(K.ih, NULL);
    O.slot_owner = fo(K.slotui, "_owner"); O.slot_index = fo(K.slotui, "_slotIndex"); O.slot_inv = fo(K.slotui, "_inventory");
    O.slot_autofix = fo(K.slotui, "_imgAutoFix");
    O.mf_campaign = fo(K.mainflow, "_campaignData");
    { void *f = fld(k_cd, "TimeData"), *kt = ftype_class(k_cd, "TimeData"), *g = kt ? fld(kt, "<Tick>k__BackingField") : NULL;   /* log only */
      O.cd_time = f ? (int32_t)IL.field_get_offset(f) : -1; O.time_tick = g ? (int32_t)IL.field_get_offset(g) : -1; } O.cd_clan = fo(k_cd, "ClanDataContainer"); O.clan_guid = fo(k_clan, "<CampaignGuid>k__BackingField");
    O.ctrl_option = fo(K.ctrl, "_registeredOption"); O.option_list = fo(K.option, "_interactions");
    O.ev_guid = fo(K.ev_ban, "TargetGuid"); O.ev_index = fo(K.ev_ban, "Index");
    O.ec_components = fo(K.ec, "_components"); O.ui_stream = fo(k_static, "_inputStream"); O.optui_list = fo(K.optui, "_interactions");
    O.ia_type = fov(K.ia, "InteractionType"); O.ia_text = fov(K.ia, "TargetText"); O.ia_possible = fov(K.ia, "IsPossible");
    O.ia_index = fov(K.ia, "Index"); O.ia_input = fov(K.ia, "InteractionInputType"); O.ia_uiinput = fov(K.ia, "InteractionUIInput");
    O.ui_type = fov(K.uiin, "UIInputType"); O.ui_p2 = fov(K.uiin, "ParamIndex2"); O.ui_g1 = fov(K.uiin, "ParamGuid1");
    int ia_sz = IL.class_value_size(K.ia, NULL), ui_sz = IL.class_value_size(K.uiin, NULL);
    if (ia_sz != IA_SIZE || ui_sz != UI_SIZE || O.ih_size < 0x1D) {
        H->log(ME, "struct sizes changed (Interaction %d, UIInput %d, ItemHolder %d): the module stays off", ia_sz, ui_sz, O.ih_size); return 1;
    }
    E.ia_ui = enum_v(kls("Refactor.Control", "InteractionType"), "UIInteraction", 38);
    void *k_uit = kls("Refactor.Main.InputModule", "UIInputType");
    E.uit_ban = enum_v(k_uit, "InteractionOption_BanAutoFix", 190); E.uit_skilltarget = enum_v(k_uit, "InteractionOption_UseSkillTarget", 213);
    E.uit_show_inv = enum_v(k_uit, "ShowInteraction_Inventory", 178);
    E.ct_inv = enum_v(kls("Refactor.Component", "ComponentType"), "Inventory", 5);
    E.find_incl = enum_v(kls("UnityEngine", "FindObjectsInactive"), "Include", 1);

    M.route = msig(k_proc, "RouteDungeonReturnInventoryItem", 4, NULL); M.sort = msig(k_proc, "SortInventory", 2, NULL);
    M.ban = msig(k_ieh, "OnEvent", 1, "BanAutoFixItemRequested");
    M.reg = msig(K.ctrl, "RegisterInteraction_Inventory", 3, NULL); M.show = msig(K.optui, "Show", 1, "InteractionOption");
    M.setitem = msig(K.slotui, "SetItem", 2, "String,Nullable`1"); M.optui_select = msig(K.optui, "OnSelect", 1, "Interaction");
    M.inv_add = msig(K.inv, "AddItem", 5, NULL); M.inv_remove = msig(K.inv, "RemoveItem", 1, NULL); M.ih_empty = msig(K.ih, "IsEmpty", 0, NULL);
    M.ec_inst = fld(K.ec, "_instance"); if (!M.ec_inst) miss("field", "EntityComponent", "_instance");
    M.ec_tryget = msig(ftype_class(K.ec, "_components"), "TryGetValue", 2, NULL); M.cl_tryget = msig(K.complist, "TryGetComponent", 2, NULL);
    void *k_list = ftype_class(K.option, "_interactions");   /* List<Interaction> */
    O.list_items = fo(k_list, "_items");
    M.list_add = msig(k_list, "Add", 1, NULL); M.list_removeat = msig(k_list, "RemoveAt", 1, NULL); M.list_count = msig(k_list, "get_Count", 0, NULL);
    M.obj_find = msig(K.object, "FindAnyObjectByType", 2, "Type,FindObjectsInactive");
    M.obj_inst = msig(K.object, "Instantiate", 2, "Object,Transform"); M.obj_set_name = msig(K.object, "set_name", 1, NULL);
    M.op_implicit = msig(K.object, "op_Implicit", 1, NULL);
    M.comp_tr = msig(K.comp, "get_transform", 0, NULL); M.comp_go = msig(K.comp, "get_gameObject", 0, NULL);
    M.go_active = msig(K.go, "SetActive", 1, NULL); M.go_activeself = msig(K.go, "get_activeSelf", 0, NULL);
    M.tr_find = msig(K.transform, "Find", 1, "String"); M.tr_last = msig(K.transform, "SetAsLastSibling", 0, NULL);
    M.rt_amin = msig(K.rect, "set_anchorMin", 1, NULL); M.rt_amax = msig(K.rect, "set_anchorMax", 1, NULL);
    M.rt_pivot = msig(K.rect, "set_pivot", 1, NULL); M.rt_pos = msig(K.rect, "set_anchoredPosition", 1, NULL);
    M.rt_size = msig(K.rect, "set_sizeDelta", 1, NULL);
    M.img_sprite = msig(K.image, "set_sprite", 1, NULL); M.img_preserve = msig(K.image, "set_preserveAspect", 1, NULL);
    M.g_color = msig(K.graphic, "set_color", 1, NULL); M.g_raycast = msig(K.graphic, "set_raycastTarget", 1, NULL);
    M.lang_get = msig(K.lang, "GetCurrentLanguage", 0, NULL);
    M.tex_ctor = msig(K.tex, ".ctor", 4, "Int32,Int32,TextureFormat,Boolean"); M.tex_set = msig(K.tex, "SetPixels32", 1, NULL);
    M.tex_apply = msig(K.tex, "Apply", 0, NULL); M.tex_filter = msig(K.texture, "set_filterMode", 1, NULL);
    M.tex_wrap = msig(K.texture, "set_wrapMode", 1, NULL); M.spr_create = msig(K.sprite, "Create", 3, "Texture2D,Rect,Vector2");
    M.obj_hide = msig(K.object, "set_hideFlags", 1, NULL);
    E.rgba32 = enum_v(kls("UnityEngine", "TextureFormat"), "RGBA32", 4); E.point = enum_v(kls("UnityEngine", "FilterMode"), "Point", 0);
    E.clamp = enum_v(kls("UnityEngine", "TextureWrapMode"), "Clamp", 1); E.keep = enum_v(kls("UnityEngine", "HideFlags"), "DontUnloadUnusedAsset", 32);
    M.stream_add = msig(K.uistream, "AddInput", 1, NULL);
    if (n_missing) return 1;
    H->log(ME, "fields: unit guid %d, slots %d, holder %d/%d/%d/%d size %d, slot ui %d/%d/%d/%d, world %d/%d/%d, menu %d/%d, order %d/%d",
           O.unit_guid, O.inv_slots, O.ih_guid, O.ih_key, O.ih_amount, O.ih_auto, O.ih_size, O.slot_owner, O.slot_index, O.slot_inv,
           O.slot_autofix, O.mf_campaign, O.cd_clan, O.clan_guid, O.ctrl_option, O.option_list, O.ev_guid, O.ev_index);
    return 0;
}

static const mp_api *mp_find(void) {
    if (!H->module_loaded || !H->module_loaded("multiplayer")) return NULL;
#ifdef _WIN32
    void *img = (void *)GetModuleHandleA("multiplayer.dll");
    void *get = img ? (void *)GetProcAddress((HMODULE)img, "mp_api_get") : NULL;
#else
    void *img = dlopen("@rpath/multiplayer.dylib", RTLD_NOLOAD | RTLD_LOCAL);
    void *get = img ? dlsym(img, "mp_api_get") : NULL;
#endif
    return get ? ((const mp_api *(*)(uint32_t))get)(1) : NULL;
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
#define FN(f, n) *(void **)&IL.f = H->il2cpp_fn(n)
    FN(class_get_methods, "il2cpp_class_get_methods"); FN(method_get_name, "il2cpp_method_get_name");
    FN(method_get_param_count, "il2cpp_method_get_param_count"); FN(method_get_param, "il2cpp_method_get_param");
    FN(method_is_generic, "il2cpp_method_is_generic"); FN(class_get_parent, "il2cpp_class_get_parent");
    FN(class_get_name, "il2cpp_class_get_name"); FN(class_from_type, "il2cpp_class_from_type");
    FN(class_get_element_class, "il2cpp_class_get_element_class");
    FN(class_get_type, "il2cpp_class_get_type"); FN(type_get_object, "il2cpp_type_get_object");
    FN(class_get_field_from_name, "il2cpp_class_get_field_from_name"); FN(field_get_offset, "il2cpp_field_get_offset");
    FN(field_get_type, "il2cpp_field_get_type"); FN(field_static_get_value, "il2cpp_field_static_get_value");
    FN(class_get_method_from_name, "il2cpp_class_get_method_from_name"); FN(class_value_size, "il2cpp_class_value_size");
    FN(object_get_class, "il2cpp_object_get_class"); FN(class_is_assignable_from, "il2cpp_class_is_assignable_from");
    FN(object_get_virtual_method, "il2cpp_object_get_virtual_method");
    FN(object_new, "il2cpp_object_new"); FN(array_new, "il2cpp_array_new"); FN(string_new, "il2cpp_string_new");
    FN(gch_weak, "il2cpp_gchandle_new_weakref"); FN(gch_target, "il2cpp_gchandle_get_target"); FN(gch_free, "il2cpp_gchandle_free");
#undef FN
    for (void **p = (void **)&IL; p < (void **)(&IL + 1); p++) if (!*p) { H->log(ME, "il2cpp export missing (#%d)", (int)(p - (void **)&IL)); return 1; }
    if (resolve()) { H->log(ME, "%d game names not found on this build: the module stays off (stock game)", n_missing); return 2; }
    const char *dd = dsl_mod_data_dir(H, ME);
    if (!dd) { H->log(ME, "no mod data folder (loader older than API 6): the module stays off"); return 3; }
    snprintf(data_dir, sizeof data_dir, "%s", dd);

    /* every hook or none: a lock that the return honours but the menu cannot change (or the other way) is worse */
    int bad = 0;
    bad |= H->hook(ME, *(void **)M.route, (void *)my_route, (void **)&o_route) ? 1 : 0;
    bad |= H->hook(ME, *(void **)M.sort, (void *)my_sort, (void **)&o_sort) ? 2 : 0;
    bad |= H->hook(ME, *(void **)M.ban, (void *)my_ban, (void **)&o_ban) ? 4 : 0;
    bad |= H->hook(ME, *(void **)M.reg, (void *)my_reg, (void **)&o_reg) ? 8 : 0;
    bad |= H->hook(ME, *(void **)M.show, (void *)my_show, (void **)&o_show) ? 16 : 0;
    bad |= H->hook(ME, *(void **)M.setitem, (void *)my_setitem, (void **)&o_setitem) ? 32 : 0;
    if (bad) { H->log(ME, "a hook was refused (mask %d: return, sort, order, menu, menu row, lock icon): the module stays off", bad); return 4; }

    mp = mp_find();
    if (mp) {
        static const mp_joinpoint jp = { { 'L', 'K', 'S', 'L' }, jp_size, jp_write, jp_read, NULL };
        bad = 0;
        if (MP_HAS(register_joinpoint)) bad |= mp->register_joinpoint(&jp) != 0; else bad = 1;
        if (MP_HAS(register_fingerprint)) bad |= mp->register_fingerprint(fp_mix) != 0; else bad = 1;
        if (MP_HAS(register_rig_cmd)) mp->register_rig_cmd("lks_", t_exec);
        if (bad) { H->log(ME, "multiplayer found but its join point / fingerprint could not be registered: the module stays off"); return 5; }
    }
    H->log(ME, "locked slots on (6 hooks; data %s; multiplayer %s)", data_dir, mp ? "join point + fingerprint" : "not loaded");
    return 0;
}
