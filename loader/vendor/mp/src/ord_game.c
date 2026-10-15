/* ord_game.c - the game side of the orders lane (dylib only): hooks, game access for ord_core, replay.
 * Catalog: research/orders_catalog.md "Set 1". Offsets: ord_test/OFFSETS.md (all static_assert-checked
 * against il2cpp.h by ord_test/run_tests.sh). Notes and open points: ORD_NOTES.md.
 *
 * Flags (RimWorld's Multiplayer.cs idea):
 *   in_interface  1 while the handler of a LOCAL input event runs (set in EventQueue b__2 / b__3)
 *   executing     1 while ord_apply replays an order (every hook passes through)
 *   ticking       1 while the det layer runs a world tick (ord_set_ticking)
 * Tier A (b__8_0): command events are serialized and swallowed; replay runs the event's sync handler
 * right away (same delegate EventQueue.ProcessSyncEvent calls) with the issuer's selection swapped in.
 * Tier B: MoveOrder / WaypointOrder / ActionOrder / UseSkillMode.UseSkill / ChangeAutoSkillData skip the
 * original while in_interface and become orders; replay calls the original under `executing`. */
#include "rig.h"
#include "ord_api.h"
#include "ord_core.h"
#include <stdlib.h>
#include <math.h>
#include <dlfcn.h>
#include <string.h>

/* ---------------------------------------------------------------- RVAs (dump.cs, build 25846389) */
#define VA_B8_0            0xD3BAA4   /* InputDispatcher.<Update>b__8_0(IEventData) */
#define VA_EQ_B2           0xCCFF30   /* EventQueue.<>c__DisplayClass8_0.<.ctor>b__2(IEventData) (sync dispatch) */
#define VA_EQ_B3           0xCCFFEC   /* EventQueue.<>c__DisplayClass8_1.<.ctor>b__3(IEventData) -> IEnumerator */
#define VA_EQ_ADDEVENT     0xCCFAD4   /* EventQueue.AddEvent(IEventData) */
#define VA_MOVE_ALL        0xC07164   /* MoveOrder.All(ISelectReader, Vector2Int) */
#define VA_MOVE_TRY        0xC07440   /* MoveOrder.TryMove(ISelectReader) */
#define VA_MOVE_NEAR       0xC0D30C   /* MoveOrder.Near(UnitEntity, Vector2Int) */
#define VA_WP_RESERVE      0xC072D4   /* WaypointOrder.Reserve(ISelectReader, Vector2Int) */
#define VA_WP_CLEAR        0xC06F5C   /* WaypointOrder.ClearWayPoint(ISelectReader) */
#define VA_RUN             0xC0678C   /* InteractionRunner.Run(Vector2Int, ISelectReader) */
#define VA_EXECUTE         0xC04A38   /* ActionOrder.Execute(Interaction, UnitEntity, RunCheck) -> bool */
#define VA_USE_SKILL       0xC0E2A0   /* UseSkillMode.UseSkill(IEntity, IEntity, ActiveSkill_Base, ICombatTargetSelector, ItemEntity) */
#define VA_SEL_SPECIFIC    0x84927C   /* EntityTargetSelector.SpecificTarget(IEntity) */
#define VA_SEL_USABLE      0x8493CC   /* EntityTargetSelector.SpecificUsableTarget(IEntity) */
#define VA_SEL_FROM_LIST   0x848F4C   /* EntityTargetSelector.SelectFromSpecificTargets(IEnumerable<IEntity>) */
#define VA_SEL_SELF        0x848DC4   /* EntityTargetSelector.Self() */
#define VA_SEL_HOSTILE     0x848394   /* EntityTargetSelector.ClosestHostile() */
#define VA_SEL_GRID        0x7B56C0   /* GridTargetSelector.SpecificGrid(GridData) */
#define VA_SEL_TO_GRID     0x7B54E0   /* GridTargetSelector.ToGrid(EntityTargetSelector) */
#define VA_AUTO_SKILL      0xDDAF2C   /* SkillComponent.ChangeAutoSkillData(string) */
#define VA_RAYCAST         0xFA6CAC   /* EntityUtility.GetRayCastPlayerEntities(IObjectEntityGuidReader, IInMapDataSupplier) */
#define VA_OBS_RECORD      0xBBF998   /* ObserverAggregator.Record(IObserver) */
#define VA_TRADE_COMMIT    0xCB2D14   /* TradeHelper.TryCommit(TradeCommitRequest, out string) -> bool */
#define VA_TRADE_REQ_CTOR  0xCF4694   /* TradeCommitRequest..ctor(MapType, Guid, IEnumerable<TradeCommitDelivery>) */
#define VA_GET_PROCESSOR   0xD45B2C   /* ControlModeProcessor.GetProcessor(ControlMode) */
#define VA_TRY_GET_ENTITY  0xFC16FC   /* InMapDataInterface.TryGetEntity(Guid, out IEntity) */
#define VA_ITEM_GET_ENTITY 0xC88F08   /* ItemDataContainer.GetEntity(Guid) (TryGetValue inside) */
#define VA_COMBAT_INSTANCE 0x7F4404   /* CombatDataManager.get_Instance() */
#define VA_GET_ACTIVE_SKILL 0x7F44A8  /* CombatDataManager.GetActiveSkillData(string) */
#define VA_GET_COMPONENT_OF 0x185F284 /* EntityComponent.GetComponentOf<object>(IEntity) (shared generic body) */
#define VA_OBJECT_GETTYPE  0x42B1E58  /* System.Object.GetType() */
/* Set 2: build modes (ControlMode 1 Gathering, 2 Cancel, 3 PickUp, 4 Demolition, 5 Construction) */
#define VA_GATHER_CLICK    0xC0FE88   /* GatheringMode.LeftClickInteraction(Vector2Int, bool) */
#define VA_GATHER_DRAG     0xC1029C   /* GatheringMode.DragSelection(Vector2Int, Vector2Int) */
#define VA_CANCEL_CLICK    0xBF8254   /* CancelMode.LeftClickInteraction */
#define VA_CANCEL_DRAG     0xBF890C   /* CancelMode.DragSelection */
#define VA_PICKUP_CLICK    0xC10818   /* PickUpMode.LeftClickInteraction */
#define VA_PICKUP_DRAG     0xC10BEC   /* PickUpMode.DragSelection */
#define VA_DEMOLISH_CLICK  0xC0EFC8   /* DemolitionMode.LeftClickInteraction */
#define VA_DEMOLISH_DRAG   0xC0F770   /* DemolitionMode.DragSelection */
#define VA_BUILD_CLICK     0xBFBF88   /* ConstructionMode.LeftClickInteraction */
#define VA_BUILD_DRAG      0xC013F4   /* ConstructionMode.DragSelection */
#define VA_PARTY_CTOR      0xEDFF4C   /* ExpeditionPlanPartyContext..ctor() */
#define VA_PICKUP_EVENT    0xDA0830   /* ItemEventHandler.OnEvent(PickUpItemRequested) (simulation) */
#define VA_TASK_AVAILABLE  0xAE5038   /* TaskAssignableHelper.IsAvailable(TaskGroup, IEntity unit) -> bool (private) */
#define VA_TRY_CRAFT       0xDCA568   /* TaskFinishEventHandler.TryCraft(MapType, CraftingTaskData, RecipeTableData) -> bool */
#define MI_GETCOMP_INVENTORY 0x66919E0 /* Method$EntityComponent.GetComponentOf<InventoryComponent>() */
#define VA_PARTY_REPLACE   0xEDFB74   /* ExpeditionPlanPartyContext.ReplacePartyMembers(IReadOnlyList<Guid>) */
/* il2cpp API exports */
#define VA_IL_DOMAIN_GET   0x6F91F4
#define VA_IL_ASSEMBLIES   0x6F9200
#define VA_IL_ASM_IMAGE    0x6F8BBC
#define VA_IL_CLASS_FROM_NAME 0x6F8BF4
#define VA_IL_CLASS_NAME   0x6F8C20
#define VA_IL_ARRAY_NEW    0x6F8BA8
#define VA_IL_OBJECT_NEW   0x6F96CC
#define VA_IL_STRING_NEW   0x6F9738
#define VA_IL_GCHANDLE_NEW 0x6F9540
#define VA_IL_GCHANDLE_FREE 0x6F95E4
#define VA_IL_WBARRIER     0x6F95C8
#define VA_IL_RUNTIME_INVOKE 0x6F971C  /* il2cpp_runtime_invoke(MethodInfo*, obj, void **params, Il2CppException **exc) */
/* metadata slots (script.json ScriptMetadata / ScriptMetadataMethod) */
#define TI_LIST_IENTITY    0x66458C0  /* List<IEntity>_TypeInfo */
#define TI_IENTITY         0x664EE28  /* Refactor.IEntity_TypeInfo */
#define TI_LIST_GUID       0x66457D8  /* List<Guid>_TypeInfo */
#define TI_GUID            0x664E200  /* System.Guid_TypeInfo */
#define TI_GRIDDATA        0x664E158  /* Refactor.Runtime.Map.GridData_TypeInfo */
#define TI_INPUT_OBSERVER  0x6650188  /* Refactor.ObserverDriven.InputObserver_TypeInfo */
#define TI_TRADE_REQUEST   0x6655DC8  /* Refactor.Main.TradeCommitRequest_TypeInfo */
#define MI_GETCOMP_SKILL   0x66919E8  /* Method$EntityComponent.GetComponentOf<SkillComponent>() */

typedef struct { int32_t x, y; } V2I;
#define SETREF(o, slot, v) F(VA_IL_WBARRIER, void (*)(Obj, void **, Obj))((o), (void **)(slot), (v))

/* ---------------------------------------------------------------- state */
static ord_emit_fn emit_fn;
static void (*recruit_hook)(const uint8_t unit[16], int player);
static int (*gear_check)(const uint8_t unit[16]);
static ord_item_api item_api;
static void (*o_pickup_event)(Obj, Obj, void *);
static bool (*o_try_craft)(Obj, int32_t, Obj, Obj, void *);
static bool (*o_task_available)(Obj, Obj, Obj, void *);
static int hooks_bad;
static void *emit_user;
static int in_interface, executing, ticking;
static int64_t iface_frame, exec_frame;
static ord_stats st;

/* originals */
static void (*o_b8_0)(Obj, Obj, void *);
static void (*o_b2)(Obj, Obj, void *);
static Obj  (*o_b3)(Obj, Obj, void *);
static void (*o_move_all)(Obj, Obj, V2I, void *);
static void (*o_move_try)(Obj, Obj, void *);
static void (*o_move_near)(Obj, Obj, V2I, void *);
static void (*o_wp_reserve)(Obj, Obj, V2I, void *);
static void (*o_wp_clear)(Obj, Obj, void *);
static void (*o_run)(Obj, V2I, Obj, void *);
static bool (*o_execute)(Obj, void *, Obj, Obj, void *);
static void (*o_use_skill)(Obj, Obj, Obj, Obj, Obj, Obj, void *);
static Obj  (*o_sel_specific)(Obj, void *);
static Obj  (*o_sel_usable)(Obj, void *);
static Obj  (*o_sel_from_list)(Obj, void *);
static Obj  (*o_sel_self)(void *);
static Obj  (*o_sel_hostile)(void *);
static Obj  (*o_sel_grid)(Obj, void *);
static Obj  (*o_sel_to_grid)(Obj, void *);
static void (*o_auto_skill)(Obj, Obj, void *);
static Obj  (*o_raycast)(Obj, Obj, void *);
static void (*o_obs_record)(Obj, Obj, void *);
static bool (*o_trade_commit)(Obj, Obj, Obj *, void *);

/* ---------------------------------------------------------------- il2cpp helpers */
static const char *cls_name(Obj o) { return o ? F(VA_IL_CLASS_NAME, const char *(*)(void *))(*(void **)o) : ""; }
static int is_cls(Obj o, const char *name) { return o && !strcmp(cls_name(o), name); }
static Obj str_new(const char *s) { return F(VA_IL_STRING_NEW, Obj (*)(const char *))(s); }
static Obj arr_new(void *elem_cls, size_t n) { return F(VA_IL_ARRAY_NEW, Obj (*)(void *, size_t))(elem_cls, n); }
static Obj obj_alloc(void *cls) { return F(VA_IL_OBJECT_NEW, Obj (*)(void *))(cls); }

static void *class_by_name(const char *ns, const char *name) {
    void *dom = F(VA_IL_DOMAIN_GET, void *(*)(void))();
    size_t n = 0;
    void **as = F(VA_IL_ASSEMBLIES, void **(*)(void *, size_t *))(dom, &n);
    for (size_t i = 0; as && i < n; i++) {
        void *img = F(VA_IL_ASM_IMAGE, void *(*)(void *))(as[i]);
        void *k = img ? F(VA_IL_CLASS_FROM_NAME, void *(*)(void *, const char *, const char *))(img, ns, name) : NULL;
        if (k) return k;
    }
    return NULL;
}

/* objects made during one ord_apply: held by GC handles until it returns (ord_msg lives in malloc memory).
 * This il2cpp (Unity 6) uses pointer-sized handles (Il2CppGCHandle = void*): a uint32 handle crashes gchandle_free. */
static void *held[512];
static int nheld;
static Obj hold(Obj o) {
    if (o && nheld < (int)(sizeof held / sizeof held[0]))
        held[nheld++] = F(VA_IL_GCHANDLE_NEW, void *(*)(Obj, bool))(o, false);
    return o;
}
static void release_all(void) {
    for (int i = 0; i < nheld; i++) F(VA_IL_GCHANDLE_FREE, void (*)(void *))(held[i]);
    nheld = 0;
}

/* Dictionary<ref, ref>: entries +0x18 (stride 0x18: hash@0, next@4, key@8, value@0x10), count +0x20 */
static Obj dict_ref_get(Obj d, Obj key) {
    if (!d) return NULL;
    Obj ent = AT(d, 0x18, Obj); int n = AT(d, 0x20, int32_t);
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x18;
        if (*(int32_t *)e >= 0 && *(Obj *)(e + 8) == key) return *(Obj *)(e + 0x10);
    }
    return NULL;
}

static void utf16_to_utf8(Obj s, char *out, size_t n) {
    size_t o = 0;
    int32_t len = AT(s, 0x10, int32_t);
    const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    for (int32_t i = 0; i < len && o + 4 < n; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < len) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
        if (u < 0x80) out[o++] = (char)u;
        else if (u < 0x800) { out[o++] = (char)(0xC0 | u >> 6); out[o++] = (char)(0x80 | (u & 0x3F)); }
        else if (u < 0x10000) { out[o++] = (char)(0xE0 | u >> 12); out[o++] = (char)(0x80 | ((u >> 6) & 0x3F)); out[o++] = (char)(0x80 | (u & 0x3F)); }
        else { out[o++] = (char)(0xF0 | u >> 18); out[o++] = (char)(0x80 | ((u >> 12) & 0x3F)); out[o++] = (char)(0x80 | ((u >> 6) & 0x3F)); out[o++] = (char)(0x80 | (u & 0x3F)); }
    }
    out[o] = 0;
}

/* ---------------------------------------------------------------- game navigation (all from MainFlow) */
static Obj event_handler(void) { return mf ? AT(mf, 0x50, Obj) : NULL; }
static Obj pc_handler(void) { Obj e = event_handler(); return e ? AT(e, 0x38, Obj) : NULL; }   /* PlayerControlEventHandler */
static Obj item_handler(void) { Obj e = event_handler(); return e ? AT(e, 0x10, Obj) : NULL; } /* ItemEventHandler */
static Obj sel_container(void) {   /* PlayerControlEventHandler._selectReader: the SelectDataContainer */
    Obj p = pc_handler(); Obj s = p ? AT(p, 0x48, Obj) : NULL;
    return is_cls(s, "SelectDataContainer") ? s : NULL;
}
static Obj mode_proc(int mode) {   /* ControlModeProcessor.GetProcessor: cached per current map */
    Obj p = pc_handler(); Obj cmp = p ? AT(p, 0x20, Obj) : NULL;
    return cmp ? F(VA_GET_PROCESSOR, Obj (*)(Obj, int, void *))(cmp, mode, NULL) : NULL;
}
static Obj command_flow(void) {
    Obj dm = mode_proc(0);
    if (!is_cls(dm, "DefaultMode")) return NULL;
    Obj dc = AT(dm, 0x18, Obj); Obj fl = dc ? AT(dc, 0x20, Obj) : NULL;
    return is_cls(fl, "CommandFlow") ? fl : NULL;
}
static Obj runner(void) { Obj f = command_flow(); return f ? AT(f, 0x10, Obj) : NULL; }
static Obj observer_buffer(void) {   /* InputDispatcher._inputBinding._buffer (the ObserverAggregator) */
    Obj d = mf ? AT(mf, 0x68, Obj) : NULL; Obj b = d ? AT(d, 0x28, Obj) : NULL;
    return b ? AT(b, 0x38, Obj) : NULL;
}

/* ---------------------------------------------------------------- entities */
static struct { void *k; int off; } ekl[16];
static int entity_guid_off(Obj e) {
    void *k = *(void **)e;
    for (int i = 0; i < 16 && ekl[i].k; i++) if (ekl[i].k == k) return ekl[i].off;
    const char *n = cls_name(e);
    int off = !strcmp(n, "UnitEntity") ? 0x24 :
              (!strcmp(n, "BuildingEntity") || !strcmp(n, "ItemEntity") || !strcmp(n, "ThingEntity") ||
               !strcmp(n, "FarmEntity") || !strcmp(n, "BlueprintEntity") || !strcmp(n, "NullEntity")) ? 0x10 : -1;
    for (int i = 0; i < 16; i++) if (!ekl[i].k) { ekl[i].k = k; ekl[i].off = off; break; }
    if (off < 0) rig_log("ord: %s is not a known IEntity class", n);
    return off;
}
static int g_ent_guid(ord_obj e, ord_guid *out) {
    if (!e) return 0;
    int off = entity_guid_off(e);
    if (off < 0) return 0;
    memcpy(out, (char *)e + off, 16);
    return 1;
}
static Obj find_on_map(int map, Guid g) {
    Obj sup = mf ? dict_int_get(AT(mf, 0x128, Obj), map) : NULL;
    if (is_cls(sup, "InMapDataInterface")) {
        Obj out = NULL;
        if (F(VA_TRY_GET_ENTITY, bool (*)(Obj, Guid, Obj *, void *))(sup, g, &out, NULL) && out) return out;
    }
    Obj c = cdc(); Obj edc = c ? AT(c, 0x20, Obj) : NULL;               /* inventory items: item container */
    Obj idc = edc ? dict_int_get(AT(edc, 0x28, Obj), map) : NULL;
    return idc ? F(VA_ITEM_GET_ENTITY, Obj (*)(Obj, Guid, void *))(idc, g, NULL) : NULL;
}
static ord_obj g_ent_find(int map, const ord_guid *og) {
    Guid g; memcpy(&g, og, 16);
    if (!g.lo && !g.hi) return NULL;
    Obj e = find_on_map(map, g);
    int cm = cur_map();
    if (!e && cm != map && cm >= 0) e = find_on_map(cm, g);
    if (!e) { char b[40]; guid_str(g, b); rig_log("ord: entity %s not found (map %d)", b, map); }
    return e;
}

/* ---------------------------------------------------------------- ord_env for the real game */
static int g_str_get(ord_obj s, char *out, size_t cap) {
    if (!s) { out[0] = 0; return -1; }
    utf16_to_utf8(s, out, cap);
    return (int)strlen(out);
}
static ord_obj g_str_new(const char *s) { return hold(str_new(s)); }
static void *ev_klass[256];   /* by index in ord_event_table */
static int classes_ok;
static void classes_init(void) {
    if (classes_ok) return;
    classes_ok = 1;
    for (int i = 0; i < ord_event_count && i < 256; i++) {
        ev_klass[i] = class_by_name(ord_event_table[i].ns, ord_event_table[i].name);
        if (!ev_klass[i]) rig_log("ord: class %s.%s not found", ord_event_table[i].ns, ord_event_table[i].name);
    }
}
static ord_obj g_ev_new(const ord_evdef *d) {
    classes_init();
    int i = (int)(d - ord_event_table);
    return (i >= 0 && i < ord_event_count && ev_klass[i]) ? hold(obj_alloc(ev_klass[i])) : NULL;
}
static int g_lguid_get(ord_obj l, ord_guid *out, int max) {
    if (!l) return -1;
    Obj items = AT(l, 0x10, Obj); int n = AT(l, 0x18, int32_t);
    if (n > max) { rig_log("ord: guid list of %d cut to %d", n, max); n = max; }
    for (int i = 0; i < n && items; i++) memcpy(&out[i], (char *)items + 0x20 + i * 16, 16);
    return items ? n : 0;
}
static ord_obj g_lguid_new(const ord_guid *g, int n) {
    Obj l = hold(obj_new(TI_LIST_GUID));
    Obj a = hold(arr_new(md(TI_GUID), (size_t)(n > 4 ? n : 4)));
    if (!l || !a) return NULL;
    for (int i = 0; i < n; i++) memcpy((char *)a + 0x20 + i * 16, &g[i], 16);   /* value elements: no barrier */
    SETREF(l, (char *)l + 0x10, a);
    AT(l, 0x18, int32_t) = n; AT(l, 0x1C, int32_t) = 1;
    return l;
}
/* TradeCommitRequest: Map@0x10, SessionId@0x14, Deliveries@0x28 = ReadOnlyCollection(list@0x10) over a
 * TradeCommitDelivery[] (ctor: Enumerable.ToArray + Array.AsReadOnly); delivery stride 0x28 */
static int g_trade_get(ord_obj req, ord_trade *t) {
    t->map = AT(req, 0x10, int32_t); memcpy(&t->session, (char *)req + 0x14, 16); t->n = 0;
    Obj ro = AT(req, 0x28, Obj); Obj inner = ro ? AT(ro, 0x10, Obj) : NULL;
    if (!inner) return 0;
    const char *in = cls_name(inner);
    char *data; int n;
    if (!strcmp(in, "TradeCommitDelivery[]")) { data = (char *)inner + 0x20; n = (int)AT(inner, 0x18, uint64_t); }
    else if (!strcmp(in, "List`1")) { Obj a = AT(inner, 0x10, Obj); data = a ? (char *)a + 0x20 : NULL; n = AT(inner, 0x18, int32_t); }
    else { rig_log("ord: trade deliveries in a %s", in); return -1; }
    if (n > ORD_MAX_DELIV || (n && !data)) return -1;
    for (int i = 0; i < n; i++) {
        char *e = data + i * 0x28;
        memcpy(&t->d[i].slot, e, 16); t->d[i].dest = AT(e, 0x10, int32_t);
        memcpy(&t->d[i].owner, e + 0x14, 16); t->d[i].amount = AT(e, 0x24, int32_t);
    }
    t->n = n;
    return 0;
}
static void *delivery_cls;
static ord_obj g_trade_new(const ord_trade *t) {
    if (!delivery_cls) delivery_cls = class_by_name("Refactor.Main", "TradeCommitDelivery");
    if (!delivery_cls) return NULL;
    Obj a = hold(arr_new(delivery_cls, (size_t)t->n));
    for (int i = 0; i < t->n; i++) {
        char *e = (char *)a + 0x20 + i * 0x28;
        memcpy(e, &t->d[i].slot, 16); AT(e, 0x10, int32_t) = t->d[i].dest;
        memcpy(e + 0x14, &t->d[i].owner, 16); AT(e, 0x24, int32_t) = t->d[i].amount;
    }
    Obj r = hold(obj_new(TI_TRADE_REQUEST));
    Guid s; memcpy(&s, &t->session, 16);
    F(VA_TRADE_REQ_CTOR, void (*)(Obj, int32_t, Guid, Obj, void *))(r, t->map, s, a, NULL);
    return r;
}
static void g_set_ref(ord_obj o, void *slot, ord_obj v) { SETREF(o, slot, v); }

static const ord_env genv = { g_ent_guid, g_ent_find, g_str_get, g_str_new, g_ev_new, g_lguid_get, g_lguid_new,
                              g_trade_get, g_trade_new, g_set_ref };

/* ---------------------------------------------------------------- flags */
static void recover(void) {   /* an exception may have unwound through a hook: drop stale flags */
    if (executing && exec_frame != rig_frames) { rig_log("ord: stale executing flag reset"); executing = 0; release_all(); }
    if (in_interface && iface_frame != rig_frames) { rig_log("ord: stale in_interface flag reset"); in_interface = 0; }
}
static int capturing(void) { return emit_fn && in_interface && !executing && !ticking; }

/* LOCAL input events passed on by b__8_0: b__2 / b__3 set in_interface while their handler runs */
static struct { Obj ev; int64_t frame; } local_ring[64];
static int local_pos;
static void local_push(Obj ev) { local_ring[local_pos].ev = ev; local_ring[local_pos].frame = rig_frames; local_pos = (local_pos + 1) & 63; }
static int local_take(Obj ev) {
    for (int i = 0; i < 64; i++)
        if (local_ring[i].ev == ev) { local_ring[i].ev = NULL; return rig_frames - local_ring[i].frame <= 2; }
    return 0;
}

/* selector descriptors recorded by the factory hooks during one LOCAL handler */
static struct { Obj sel; ord_seldesc d; } seldesc[16];
static int nseldesc;
static const ord_seldesc *seldesc_of(Obj sel) {
    for (int i = nseldesc - 1; i >= 0; i--) if (seldesc[i].sel == sel) return &seldesc[i].d;
    return NULL;
}
static ord_seldesc *seldesc_add(Obj sel) {
    if (nseldesc == 16) { memmove(seldesc, seldesc + 1, sizeof seldesc[0] * 15); nseldesc = 15; }
    seldesc[nseldesc].sel = sel; memset(&seldesc[nseldesc].d, 0, sizeof(ord_seldesc));
    return &seldesc[nseldesc++].d;
}

/* ---------------------------------------------------------------- capture helpers */
static void ctx_capture(ord_ctx *c, Obj select) {
    c->map = cur_map(); c->sel_group = 0; c->nsel = 0;
    Obj sc = is_cls(select, "SelectDataContainer") ? select : sel_container();
    if (!sc) return;
    c->sel_group = AT(sc, 0x10, int32_t);
    Obj l = AT(sc, 0x18, Obj); Obj items = l ? AT(l, 0x10, Obj) : NULL;
    int n = l ? AT(l, 0x18, int32_t) : 0;
    for (int i = 0; i < n && items && c->nsel < ORD_MAX_SEL; i++) {
        Obj e = *(Obj *)((char *)items + 0x20 + i * 8);
        if (g_ent_guid(e, &c->sel[c->nsel])) c->nsel++;
    }
}
/* ui lane (ui_mp.h): map changes from the UI go to the vote; 1 = taken (do not send), 0 = send as usual */
/* looked up at run time (dlsym), so the dylib links with or without the ui lane */
static int (*ui_map_propose_fn)(int, const uint8_t *, size_t);
static int ui_map_propose_looked;

/* A zero owner means "global" in mp_core (base work, open to everyone). No order about a unit may go out with
 * a zero owner: if the table's owner field is empty, take the first Guid / IEntity field (or, for selection-based
 * events, the first selected entity) that is a unit. Unit-only kinds with no unit at all are refused (0). */
static int is_unit_guid(const ord_guid *g, int map) {
    static const ord_guid z;
    return memcmp(g, &z, 16) && is_cls(g_ent_find(map, g), "UnitEntity");
}
static int owner_or_unit(const ord_msg *m, ord_guid *owner) {
    static const ord_guid z;
    if (memcmp(owner, &z, 16)) return 1;
    int map = m->ctx.map;
    switch (m->kind) {
    case ORD_K_EVENT: {
        const ord_evdef *d = m->ev;
        for (int i = 0; i < d->nf; i++) {
            char *p = (char *)m->evobj + d->f[i].off;
            ord_guid g;
            if (d->f[i].kind == ORD_F_GUID) memcpy(&g, p, 16);
            else if (d->f[i].kind == ORD_F_NGUID && p[0]) memcpy(&g, p + 4, 16);
            else if (d->f[i].kind == ORD_F_ENT && *(Obj *)p && g_ent_guid(*(Obj *)p, &g)) {}
            else continue;
            if (is_unit_guid(&g, map)) { *owner = g; return 1; }
        }
        if (d->flags & ORD_EF_SELOWNER)
            for (int i = 0; i < m->ctx.nsel; i++) if (is_unit_guid(&m->ctx.sel[i], map)) { *owner = m->ctx.sel[i]; return 1; }
        return 1;                       /* no unit involved: global (base work, map travel, story, recruit) */
    }
    case ORD_K_MODE_CLICK: case ORD_K_MODE_DRAG: case ORD_K_OBSERVER: return 1;
    default: return 0;                  /* moves, actions, skills: a unit is required */
    }
}

static int ord_trace(void) {               /* DSRIG_ORD_TRACE=1: one log line per captured / applied order (tests) */
    static int t = -1; if (t < 0) t = getenv("DSRIG_ORD_TRACE") != NULL; return t;
}
static void send(ord_msg *m) {
    ord_wbuf w = { 0 };
    if (ord_trace()) rig_log("ord trace: capture kind %d %s nsel %d", m->kind, m->ev ? m->ev->name : "-", m->ctx.nsel);
    if (ord_msg_write(&genv, &w, m) || w.n > 60000 - 20) {
        rig_log("ord: order kind %d (%s) not serializable (%zu bytes): dropped", m->kind, m->ev ? m->ev->name : "-", w.n);
        free(w.p); return;
    }
    int mk = ord_map_change_kind(m);
    if (mk && !ui_map_propose_looked) { ui_map_propose_looked = 1; ui_map_propose_fn = dlsym(RTLD_DEFAULT, "ui_map_propose"); }
    if (mk && ui_map_propose_fn && w.n <= 1024 && ui_map_propose_fn(mk, w.p, w.n)) {
        rig_log("ord: %s handed to the map-change vote (kind %d)", m->ev->name, mk);
        st.captured++; free(w.p); return;
    }
    ord_guid owner; ord_owner(&genv, m, &owner);
    if (m->kind == ORD_K_EVENT && (m->ev->flags & ORD_EF_EQUIP) && gear_check) {
        /* equip: the host's gear mode (checked in gear_ok above) replaces unit ownership; mode "anyone equips
           any unit" must not be dropped by mp_core's owner check, so the order goes out as global */
        memset(&owner, 0, sizeof owner);
    } else {
        /* an owner field naming a building or storage is not a unit owner: clear it, the unit fields decide */
        static const ord_guid z;
        Obj oe = memcmp(&owner, &z, 16) ? g_ent_find(m->ctx.map, &owner) : NULL;
        if (oe && !is_cls(oe, "UnitEntity")) memset(&owner, 0, sizeof owner);   /* unknown Guids keep their owner */
    }
    if (!(m->kind == ORD_K_EVENT && (m->ev->flags & ORD_EF_EQUIP) && gear_check) && !owner_or_unit(m, &owner)) {
        rig_log("ord: order kind %d (%s) is about a unit but has no owner Guid: dropped", m->kind, m->ev ? m->ev->name : "-");
        st.blocked++; free(w.p); return;
    }
    emit_fn(emit_user, owner.b, w.p, w.n);
    st.captured++;
    free(w.p);
}
static ord_msg *msg_new(uint16_t kind, Obj select) {
    ord_msg *m = calloc(1, sizeof *m);
    if (m) { m->kind = kind; ctx_capture(&m->ctx, select); }
    return m;
}

/* ---------------------------------------------------------------- selection swap (replay) */
static Obj sel_list;          /* our List<IEntity>, kept alive by a GC handle */
static Obj saved_list;
static int32_t saved_group;
static int swapped;
/* Units a player may order (same answer on every peer: dsnet's ownership table is part of the sim state):
 * own units; unclaimed units only for the host (player 0). Non-units (buildings, items) always pass. */
static int may_order(Obj e, int player) {
    ord_guid g;
    if (!is_cls(e, "UnitEntity") || !item_api.unit_owner || player < 0 || !g_ent_guid(e, &g)) return 1;
    int o = item_api.unit_owner(g.b);
    return o >= 0 ? o == player : player == 0;
}
/* Orders in the replay (selection swap, action lists): the same rule as mp_core's check, dsnet_may_order: own units,
 * the borrower of a lent unit, the host for free units, Admin mode (host, any unit) and the host's temporary control
 * of an absent player's units ("Keep playing"). lan_v9: may_order (controller only) skipped Admin and Keep playing. */
static int may_command_q(Obj e, int player, int quiet) {
    ord_guid g;
    if (!item_api.may_order || !is_cls(e, "UnitEntity") || player < 0 || !g_ent_guid(e, &g)) return may_order(e, player);
    int ok = item_api.may_order(g.b, player);
    if (!ok && !quiet) { Guid u; memcpy(&u, &g, 16); char gs[40]; guid_str(u, gs);
               rig_log("ord: replay skips unit %s for player %d (owner %d)", gs, player, item_api.unit_owner ? item_api.unit_owner(g.b) : -9); }
    return ok;
}
static int may_command(Obj e, int player) { return may_command_q(e, player, 0); }
static int filtered_out;

/* Pick-up designations (PickUpMode click / drag) make shared tasks that ANY worker takes. In multiplayer only
 * the designating player's units may take them: the replay records the area (every peer), and
 * TaskAssignableHelper.IsAvailable refuses PickUp tasks (TaskType 101) whose item lies in an area of another
 * player. Areas are kept in memory only (a reload makes old designations free for everyone). */
static struct { int map, x0, y0, x1, y1, player; } pk_area[64];
static int pk_next, pk_n;
static void pickup_area_add(int map, int x0, int y0, int x1, int y1, int player) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    pk_area[pk_next].map = map; pk_area[pk_next].x0 = x0; pk_area[pk_next].y0 = y0;
    pk_area[pk_next].x1 = x1; pk_area[pk_next].y1 = y1; pk_area[pk_next].player = player;
    pk_next = (pk_next + 1) & 63; if (pk_n < 64) pk_n++;
}
static int pickup_owner_at(int map, int x, int y) {      /* newest area wins */
    for (int k = 1; k <= pk_n; k++) {
        int i = (pk_next - k) & 63;
        if (pk_area[i].map == map && x >= pk_area[i].x0 && x <= pk_area[i].x1 && y >= pk_area[i].y0 && y <= pk_area[i].y1)
            return pk_area[i].player;
    }
    return -1;
}
static int entity_cell(int map, const Guid *g, int *x, int *y) {   /* EntityDataContainer positions: Dictionary<Guid, Vector2> */
    Obj d = pos_dict(map);
    if (!d) return 0;
    Obj ent = AT(d, 0x18, Obj); int n = AT(d, 0x20, int32_t);
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x20;
        if (*(int32_t *)e >= 0 && !memcmp(e + 8, g, 16)) { *x = (int)floorf(*(float *)(e + 0x18)); *y = (int)floorf(*(float *)(e + 0x1C)); return 1; }
    }
    return 0;
}
static bool h_task_available(Obj self, Obj group, Obj unit, void *mi) {
    bool r = o_task_available(self, group, unit, mi);
    if (!r || !pk_n || !group || !item_api.unit_owner) return r;
    Obj tasks = AT(group, 0x38, Obj); Obj items = tasks ? AT(tasks, 0x10, Obj) : NULL;
    Obj t0 = (items && AT(tasks, 0x18, int32_t) > 0) ? *(Obj *)((char *)items + 0x20) : NULL;
    if (!t0 || AT(t0, 0x20, int32_t) != 101) return r;                  /* SharedTaskData.TaskType == PickUp */
    Guid g = AT(group, 0x20, Guid); int x, y, map = cur_map();          /* TaskGroup.EntityGuid: the item */
    if (!entity_cell(map, &g, &x, &y)) return r;
    int owner = pickup_owner_at(map, x, y);
    if (owner < 0 || may_command_q(unit, owner, 1)) return r;      /* control (the borrower of a lent unit), not ownership */
    static Obj last_unit, last_group;
    if (unit != last_unit || group != last_group) {
        ord_guid ug; char a[40], b[40]; g_ent_guid(unit, &ug); Guid u; memcpy(&u, &ug, 16); guid_str(u, a); guid_str(g, b);
        rig_log("ord: pick-up of item %s refused for unit %s (designated by player %d)", b, a, owner);
        last_unit = unit; last_group = group;
    }
    return false;
}

static int sel_swap_in(const ord_ctx *c, int player) {
    Obj sc = sel_container();
    if (!sc) return -1;
    if (!sel_list) {
        sel_list = obj_new(TI_LIST_IENTITY);
        F(VA_IL_GCHANDLE_NEW, void *(*)(Obj, bool))(sel_list, false);   /* never freed */
        SETREF(sel_list, (char *)sel_list + 0x10, arr_new(md(TI_IENTITY), ORD_MAX_SEL));
    }
    Obj items = AT(sel_list, 0x10, Obj);
    int n = 0;
    for (int i = 0; i < c->nsel; i++) {
        Obj e = g_ent_find(c->map, &c->sel[i]);
        if (e && !may_command(e, player)) { filtered_out++; continue; }   /* "all units" / select-all: issuer's units only */
        if (e) SETREF(items, (char *)items + 0x20 + (n++) * 8, e);
    }
    for (int i = n; i < ORD_MAX_SEL; i++) SETREF(items, (char *)items + 0x20 + i * 8, NULL);
    AT(sel_list, 0x18, int32_t) = n; AT(sel_list, 0x1C, int32_t)++;
    saved_group = AT(sc, 0x10, int32_t); saved_list = AT(sc, 0x18, Obj);
    AT(sc, 0x10, int32_t) = c->sel_group;
    SETREF(sc, (char *)sc + 0x18, sel_list);
    swapped = 1;
    return 0;
}
static void sel_restore(void) {
    if (!swapped) return;
    Obj sc = sel_container();
    if (sc) { AT(sc, 0x10, int32_t) = saved_group; SETREF(sc, (char *)sc + 0x18, saved_list); }
    swapped = 0; saved_list = NULL;
}

/* ---------------------------------------------------------------- Tier A: b__8_0 */
static void *unknown_k[256];
static int nunknown;
static void note_unknown(Obj ev) {
    void *k = *(void **)ev;
    for (int i = 0; i < nunknown; i++) if (unknown_k[i] == k) return;
    if (nunknown < 256) unknown_k[nunknown++] = k;
    st.unknown_types++;
    rig_log("ord: local event type %s", cls_name(ev));   /* every LOCAL type once: check against the catalog */
}
static int ev_index(Obj ev) {
    classes_init();
    void *k = *(void **)ev;
    for (int i = 0; i < ord_event_count && i < 256; i++) if (ev_klass[i] == k) return i;
    return -1;
}
static int raycast_now(Obj ev, const ord_evdef *d, ord_guid *out) {
    Obj ih = item_handler();
    if (!ih || d->map_off < 0) return 0;
    Obj reader = AT(ih, 0x88, Obj);
    Obj inmap = dict_int_get(AT(ih, 0x18, Obj), AT(ev, d->map_off, int32_t));
    if (!reader || !inmap) return 0;
    Obj u = o_raycast(reader, inmap, NULL);
    return u && g_ent_guid(u, out);
}
/* ExpeditionPlanPartyContext (MainFlow +0x90, UI-only): IsActive @0x18, _partyMembers @0x10 HashSet<Guid>
 * (_slots @0x18 stride 0x18: hashCode @0, value @8; _lastIndex @0x24) */
static void party_capture(ord_msg *m) {
    Obj ctx = mf ? AT(mf, 0x90, Obj) : NULL;
    if (!ctx) return;
    m->party_active = AT(ctx, 0x18, uint8_t) ? 1 : 0;
    Obj hs = AT(ctx, 0x10, Obj); Obj slots = hs ? AT(hs, 0x18, Obj) : NULL;
    int last = hs ? AT(hs, 0x24, int32_t) : 0;
    for (int i = 0; slots && i < last && m->nparty < ORD_MAX_SEL; i++) {
        char *e = (char *)slots + 0x20 + i * 0x18;
        if (*(int32_t *)e >= 0) memcpy(&m->party[m->nparty++], e + 8, 16);
    }
}
/* gear mode: every unit the equip event touches must pass the check (host lobby option). Captured actions only (this
   player's own UI), so the refusal is said on this screen (ux_flow F19): mp_game words it from the unit's owner. */
void ui_notice(const char *text) __attribute__((weak_import));
static int gear_ok(const ord_evdef *d, Obj ev) {
    if (!gear_check) return 1;
    for (int i = 0; i < d->nf; i++) {
        if (d->f[i].kind != ORD_F_GUID) continue;
        ord_guid g; memcpy(&g, (char *)ev + d->f[i].off, 16);
        Obj e = g_ent_find(cur_map(), &g);
        if (is_cls(e, "UnitEntity") && !gear_check(g.b)) {
            rig_log("ord: %s refused by the gear mode", d->name);
            static void (*why)(const uint8_t *, char *, size_t); static int looked;
            if (!looked) { looked = 1; why = (void (*)(const uint8_t *, char *, size_t))dlsym(RTLD_DEFAULT, "mp_gear_refusal_text"); }
            char t[160] = "You cannot equip this unit (gear rule)";
            if (why) why(g.b, t, sizeof t);
            if (ui_notice) ui_notice(t);
            return 0;
        }
    }
    return 1;
}

/* ---------------------------------------------------------------- item ownership (gear mode own storage) */
/* Which inventory slot an item event takes from: holder entity Guid field + slot index field (dump.cs offsets) */
static const struct { const char *ev; int16_t holder, index; } item_src[] = {
    { "EquipItemRequested", 0x28, 0x38 },          /* ItemControlProcessor.EquipItem(target, index): target's inventory */
    { "TransferItemRequested", 0x14, 0x24 },       /* From, Index */
    { "TransferStorageItemRequested", 0x18, 0x38 },/* SourceGuid (must be selected), SlotIndex = storage slot */
    { "TransferEquipRequested", 0x18, 0x14 },      /* From, Index */
    { "TransferAndEquipRequested", 0x18, 0x14 },
    { "ExpeditionTransferItemRequested", 0x14, 0x24 },
    { "ExpeditionEquipFromInventoryRequested", 0x14, 0x24 },
};
/* InventoryComponent._itemSlots (+0x18): ItemHolder[] (struct, stride 0x20, Guid @0) */
static int item_guid_at(Obj holder, int index, ord_guid *out) {
    Obj p = pc_handler(); Obj ec = p ? AT(p, 0x18, Obj) : NULL;
    Obj inv = (ec && holder) ? F(VA_GET_COMPONENT_OF, Obj (*)(Obj, Obj, void *))(ec, holder, md(MI_GETCOMP_INVENTORY)) : NULL;
    Obj arr = is_cls(inv, "InventoryComponent") ? AT(inv, 0x18, Obj) : NULL;
    if (!arr || index < 0 || index >= (int)AT(arr, 0x18, uint64_t)) return 0;
    memcpy(out, (char *)arr + 0x20 + index * 0x20, 16);
    static const ord_guid z;
    return memcmp(out, &z, 16) != 0;
}
/* the item an event would take, 0 = none / not an item-taking event */
static int event_item(const ord_evdef *d, Obj ev, int map, ord_guid *out) {
    for (size_t i = 0; i < sizeof item_src / sizeof item_src[0]; i++) {
        if (strcmp(item_src[i].ev, d->name)) continue;
        ord_guid h; memcpy(&h, (char *)ev + item_src[i].holder, 16);
        return item_guid_at(g_ent_find(map, &h), AT(ev, item_src[i].index, int32_t), out);
    }
    return 0;
}
static int item_allowed(int player, const ord_guid *item) {
    return !item_api.may_use || item_api.may_use(player, item->b);
}
/* item Guids on a map: ItemDataContainer._items (+0x18, Dictionary<Guid,ItemEntity>, stride 0x20) and
 * _onMapItems (+0x20, Dictionary<Guid,ItemHolder>, stride 0x38) */
static Guid *item_guids(int map, int *n) {
    *n = 0;
    Obj c = cdc(); Obj edc = c ? AT(c, 0x20, Obj) : NULL;
    Obj idc = edc ? dict_int_get(AT(edc, 0x28, Obj), map) : NULL;
    Obj d1 = idc ? AT(idc, 0x18, Obj) : NULL, d2 = idc ? AT(idc, 0x20, Obj) : NULL;
    int c1 = d1 ? AT(d1, 0x20, int32_t) : 0, c2 = d2 ? AT(d2, 0x20, int32_t) : 0;
    Guid *g = calloc((size_t)(c1 + c2 + 1), sizeof(Guid));
    Obj e1 = d1 ? AT(d1, 0x18, Obj) : NULL, e2 = d2 ? AT(d2, 0x18, Obj) : NULL;
    for (int i = 0; e1 && i < c1; i++) { char *e = (char *)e1 + 0x20 + i * 0x20; if (*(int32_t *)e >= 0) g[(*n)++] = *(Guid *)(e + 8); }
    for (int i = 0; e2 && i < c2; i++) { char *e = (char *)e2 + 0x20 + i * 0x38; if (*(int32_t *)e >= 0) g[(*n)++] = *(Guid *)(e + 8); }
    return g;
}
static void tag_new_items(int map, const Guid *before, int nb, int player, int in_replay) {
    if (!item_api.tag || player < 0) return;
    int na = 0; Guid *after = item_guids(map, &na);
    for (int i = 0; i < na; i++) {
        int old = 0;
        for (int k = 0; k < nb && !old; k++) old = !memcmp(&after[i], &before[k], 16);
        if (!old) item_api.tag((const uint8_t *)&after[i], player, in_replay);
    }
    free(after);
}
/* crafting station -> player who last queued crafting there (set by the replay on every peer) */
static struct { Guid st; int player; } craft_owner[64];
static int ncraft_owner;
static void craft_owner_set(const Guid *st, int player) {
    for (int i = 0; i < ncraft_owner; i++) if (!memcmp(&craft_owner[i].st, st, 16)) { craft_owner[i].player = player; return; }
    if (ncraft_owner < 64) { craft_owner[ncraft_owner].st = *st; craft_owner[ncraft_owner++].player = player; }
}
static int craft_owner_get(Obj station) {
    ord_guid g;
    if (!g_ent_guid(station, &g)) return -1;
    for (int i = 0; i < ncraft_owner; i++) if (!memcmp(&craft_owner[i].st, &g, 16)) return craft_owner[i].player;
    return -1;
}
/* simulation: an item picked up by a unit goes to the unit's owner */
static void h_pickup_event(Obj self, Obj msg, void *mi) {
    ord_guid item, unit; int ok = 0;
    if (item_api.tag && item_api.unit_owner && msg) {
        Obj it = AT(msg, 0x28, Obj), to = AT(msg, 0x20, Obj);          /* PickUpItemRequested.Item / .To */
        ok = g_ent_guid(it, &item) && is_cls(to, "UnitEntity") && g_ent_guid(to, &unit);
    }
    o_pickup_event(self, msg, mi);
    if (ok) { int o = item_api.unit_owner(unit.b); if (o >= 0) item_api.tag(item.b, o, 0);
              Guid ig; memcpy(&ig, &item, 16); char gs[40]; guid_str(ig, gs); rig_log("ord: pick-up of item %s by a unit of player %d", gs, o); }
    else if (msg && item_api.tag) rig_log("ord: pick-up event not tagged (item %p %s, to %s)", AT(msg, 0x28, Obj), AT(msg, 0x28, Obj) ? cls_name(AT(msg, 0x28, Obj)) : "-", AT(msg, 0x20, Obj) ? cls_name(AT(msg, 0x20, Obj)) : "-");
}
/* simulation: items made by a finished crafting task go to the player who queued crafting at that station */
static bool h_try_craft(Obj self, int32_t map, Obj crafting, Obj recipe, void *mi) {
    int owner = (item_api.tag && crafting) ? craft_owner_get(AT(crafting, 0x28, Obj)) : -1;   /* SharedTaskData.Target */
    int nb = 0; Guid *before = owner >= 0 ? item_guids(map, &nb) : NULL;
    bool r = o_try_craft(self, map, crafting, recipe, mi);
    if (before) { tag_new_items(map, before, nb, owner, 0); free(before); }
    return r;
}

int ui_map_change_blocks(void) __attribute__((weak_import));   /* ui_game.c: map change running -> notice, 1 */
static void h_b8_0(Obj self, Obj data, void *mi) {
    recover();
    if (!emit_fn || executing || !data || !mf) { o_b8_0(self, data, mi); return; }
    int i = ev_index(data);
    if (i < 0) {
        if (ui_map_change_blocks && is_cls(data, "InteractGridRequested") && ui_map_change_blocks()) { st.blocked++; return; }
        note_unknown(data); st.passed_local++;
        if (!is_cls(data, "EscapeActionRequested")) local_push(data);   /* Escape: local only, never in_interface */
        o_b8_0(self, data, mi); return;
    }
    const ord_evdef *d = &ord_event_table[i];
    if (d->flags & ORD_EF_BLOCK) { st.blocked++; rig_log("ord: %s blocked in multiplayer", d->name); return; }
    ord_msg *m = msg_new(ORD_K_EVENT, NULL);
    if (!m) return;
    m->ev = d; m->evobj = data;
    if (d->flags & ORD_EF_RAYCAST) m->has_ray = (uint8_t)raycast_now(data, d, &m->ray);
    if (d->flags & ORD_EF_PARTY) party_capture(m);
    if ((d->flags & ORD_EF_EQUIP) && !gear_ok(d, data)) { st.blocked++; free(m); return; }
    { ord_guid it;
      if (item_api.me && event_item(d, data, cur_map(), &it) && !item_allowed(item_api.me(), &it)) {
          rig_log("ord: %s takes an item owned by another player: blocked", d->name);
          st.blocked++; free(m); return;
      } }
    send(m);
    free(m);
}

/* EventQueue dispatch: in_interface for LOCAL input events */
static void h_b2(Obj closure, Obj msg, void *mi) {
    recover();
    if (executing || !emit_fn || !msg || !local_take(msg)) { o_b2(closure, msg, mi); return; }
    int was = in_interface;
    in_interface = 1; iface_frame = rig_frames; nseldesc = 0;
    o_b2(closure, msg, mi);
    in_interface = was; nseldesc = 0;
}
static Obj h_b3(Obj closure, Obj msg, void *mi) {
    /* async handlers: only the part before the first yield runs here; later parts run in the coroutine */
    recover();
    if (executing || !emit_fn || !msg || !local_take(msg)) return o_b3(closure, msg, mi);
    int was = in_interface;
    in_interface = 1; iface_frame = rig_frames; nseldesc = 0;
    Obj r = o_b3(closure, msg, mi);
    in_interface = was; nseldesc = 0;
    return r;
}

/* ---------------------------------------------------------------- Tier B hooks */
static void h_move_all(Obj self, Obj select, V2I pos, void *mi) {
    if (!capturing()) { o_move_all(self, select, pos, mi); return; }
    ord_msg *m = msg_new(ORD_K_MOVE_ALL, select); if (!m) return;
    m->x = pos.x; m->y = pos.y; send(m); free(m);
}
static void h_move_try(Obj self, Obj select, void *mi) {
    if (!capturing()) { o_move_try(self, select, mi); return; }
    ord_msg *m = msg_new(ORD_K_MOVE_TRY, select); if (!m) return;
    send(m); free(m);
}
static void h_wp_reserve(Obj self, Obj select, V2I pos, void *mi) {
    if (!capturing()) { o_wp_reserve(self, select, pos, mi); return; }
    ord_msg *m = msg_new(ORD_K_WP_RESERVE, select); if (!m) return;
    m->x = pos.x; m->y = pos.y; send(m); free(m);
}
static void h_wp_clear(Obj self, Obj select, void *mi) {
    if (!capturing()) { o_wp_clear(self, select, mi); return; }
    ord_msg *m = msg_new(ORD_K_WP_CLEAR, select); if (!m) return;
    send(m); free(m);
}

/* InteractionRunner.Run: let it run with every ActionOrder.Execute answering false (recorded) and every
 * MoveOrder.Near skipped (recorded); the replay redoes Run's choice: first Execute that succeeds per unit,
 * Near for units with none. */
static ord_msg *run_rec;
static void h_run(Obj self, V2I pos, Obj select, void *mi) {
    if (!capturing() || run_rec) { o_run(self, pos, select, mi); return; }
    ord_msg *m = msg_new(ORD_K_RUN, select); if (!m) return;
    m->x = pos.x; m->y = pos.y;
    run_rec = m;
    o_run(self, pos, select, mi);
    run_rec = NULL;
    send(m); free(m);
}
static bool h_execute(Obj self, void *ia, Obj unit, Obj check, void *mi) {
    if (!capturing()) return o_execute(self, ia, unit, check, mi);
    if (run_rec) {
        if (run_rec->nact < ORD_MAX_ACTS && g_ent_guid(unit, &run_rec->act[run_rec->nact].unit)) {
            ord_ia_from_mem(&genv, ia, &run_rec->act[run_rec->nact].ia);
            run_rec->nact++;
        }
        return false;   /* Run tries the next candidate, then MoveOrder.Near */
    }
    ord_msg *m = msg_new(ORD_K_ACTION, NULL); if (!m) return false;
    m->nact = 1; g_ent_guid(unit, &m->act[0].unit); ord_ia_from_mem(&genv, ia, &m->act[0].ia);
    send(m); free(m);
    return true;
}
static void h_move_near(Obj self, Obj unit, V2I pos, void *mi) {
    if (!capturing()) { o_move_near(self, unit, pos, mi); return; }
    if (run_rec) {
        if (run_rec->nnear < ORD_MAX_LIST && g_ent_guid(unit, &run_rec->near_units[run_rec->nnear])) run_rec->nnear++;
        return;
    }
    ord_msg *m = msg_new(ORD_K_MOVE_NEAR, NULL); if (!m) return;
    g_ent_guid(unit, &m->unit); m->x = pos.x; m->y = pos.y; send(m); free(m);
}

static void h_use_skill(Obj self, Obj user, Obj target, Obj skill, Obj selector, Obj item, void *mi) {
    if (!capturing()) { o_use_skill(self, user, target, skill, selector, item, mi); return; }
    ord_msg *m = msg_new(ORD_K_USE_SKILL, NULL); if (!m) return;
    m->has_user = (uint8_t)g_ent_guid(user, &m->user);
    m->has_target = (uint8_t)g_ent_guid(target, &m->target);
    m->has_item = (uint8_t)g_ent_guid(item, &m->item);
    if (m->has_item && item_api.me && !item_allowed(item_api.me(), &m->item)) {
        rig_log("ord: UseSkill with an item owned by another player: blocked");
        st.blocked++; free(m); return;
    }
    if (skill) g_str_get(AT(skill, 0x10, Obj), m->key, sizeof m->key);   /* Skill_Base.Key */
    const ord_seldesc *d = seldesc_of(selector);
    if (d) m->sel = *d;
    else if (selector) rig_log("ord: UseSkill selector %s has no descriptor: the replay will skip it", cls_name(selector));
    send(m); free(m);
}

/* selector factories: record what each returned selector was built from (only during a LOCAL handler) */
static int recording_sel(void) { return emit_fn && in_interface && !executing; }
static Obj h_sel_specific(Obj e, void *mi) {
    Obj r = o_sel_specific(e, mi);
    if (recording_sel() && r) { ord_seldesc *d = seldesc_add(r); d->kind = ORD_SEL_SPECIFIC; d->has_ent = (uint8_t)g_ent_guid(e, &d->ent); }
    return r;
}
static Obj h_sel_usable(Obj e, void *mi) {
    Obj r = o_sel_usable(e, mi);
    if (recording_sel() && r) { ord_seldesc *d = seldesc_add(r); d->kind = ORD_SEL_SPECIFIC_USABLE; d->has_ent = (uint8_t)g_ent_guid(e, &d->ent); }
    return r;
}
static Obj h_sel_from_list(Obj list, void *mi) {
    Obj r = o_sel_from_list(list, mi);
    if (recording_sel() && r) {
        ord_seldesc *d = seldesc_add(r);
        if (is_cls(list, "List`1")) {
            d->kind = ORD_SEL_FROM_LIST;
            Obj items = AT(list, 0x10, Obj); int n = AT(list, 0x18, int32_t);
            for (int i = 0; i < n && items && d->n < ORD_MAX_LIST; i++)
                if (g_ent_guid(*(Obj *)((char *)items + 0x20 + i * 8), &d->list[d->n])) d->n++;
        } else rig_log("ord: SelectFromSpecificTargets with a %s: not recorded", cls_name(list));
    }
    return r;
}
static Obj h_sel_self(void *mi) {
    Obj r = o_sel_self(mi);
    if (recording_sel() && r) seldesc_add(r)->kind = ORD_SEL_SELF;
    return r;
}
static Obj h_sel_hostile(void *mi) {
    Obj r = o_sel_hostile(mi);
    if (recording_sel() && r) seldesc_add(r)->kind = ORD_SEL_CLOSEST_HOSTILE;
    return r;
}
static Obj h_sel_grid(Obj grid, void *mi) {
    Obj r = o_sel_grid(grid, mi);
    if (recording_sel() && r && grid) {
        ord_seldesc *d = seldesc_add(r);
        d->kind = ORD_SEL_GRID; d->x = AT(grid, 0x10, int32_t); d->y = AT(grid, 0x14, int32_t); d->flag = AT(grid, 0x18, int32_t);
    }
    return r;
}
static Obj h_sel_to_grid(Obj inner, void *mi) {
    Obj r = o_sel_to_grid(inner, mi);
    if (recording_sel() && r) {
        const ord_seldesc *in = seldesc_of(inner);
        if (in && in->kind >= ORD_SEL_SPECIFIC && in->kind <= ORD_SEL_CLOSEST_HOSTILE) {
            ord_seldesc t = *in;
            ord_seldesc *d = seldesc_add(r);
            *d = t; d->inner = t.kind; d->kind = ORD_SEL_TO_GRID;
        }
    }
    return r;
}

static void h_auto_skill(Obj comp, Obj key, void *mi) {
    if (!capturing()) { o_auto_skill(comp, key, mi); return; }
    ord_msg *m = msg_new(ORD_K_AUTO_SKILL, NULL); if (!m) return;
    if (!g_ent_guid(AT(comp, 0x10, Obj), &m->unit)) { free(m); o_auto_skill(comp, key, mi); return; }
    g_str_get(key, m->key, sizeof m->key);
    send(m); free(m);
}

/* ---------------------------------------------------------------- Set 2: build-mode clicks and drags */
typedef void (*mode_click_fn)(Obj, V2I, bool, void *);
typedef void (*mode_drag_fn)(Obj, V2I, V2I, void *);
static mode_click_fn o_mclick[6];
static mode_drag_fn o_mdrag[6];
static const char *mode_cls[6] = { "", "GatheringMode", "CancelMode", "PickUpMode", "DemolitionMode", "ConstructionMode" };
static Obj ctc(void) { Obj p = pc_handler(); return p ? AT(p, 0x38, Obj) : NULL; }   /* ConstructionTargetControl */
static void ctc_capture(ord_msg *m) {
    Obj c = ctc();
    if (!c) return;
    m->has_ckey = g_str_get(AT(c, 0x18, Obj), m->key, sizeof m->key) >= 0;
    m->rotation = AT(c, 0x20, int32_t); m->dsx = AT(c, 0x24, int32_t); m->dsy = AT(c, 0x28, int32_t);
}
static void mode_click(int mode, Obj self, V2I p, bool shift, void *mi) {
    if (!capturing()) { o_mclick[mode](self, p, shift, mi); return; }
    ord_msg *m = msg_new(ORD_K_MODE_CLICK, NULL); if (!m) return;
    m->mode = mode; m->x = p.x; m->y = p.y; m->shift = shift ? 1 : 0; ctc_capture(m);
    send(m); free(m);
}
static void mode_drag(int mode, Obj self, V2I a, V2I b, void *mi) {
    if (!capturing()) { o_mdrag[mode](self, a, b, mi); return; }
    ord_msg *m = msg_new(ORD_K_MODE_DRAG, NULL); if (!m) return;
    m->mode = mode; m->x = a.x; m->y = a.y; m->x2 = b.x; m->y2 = b.y; ctc_capture(m);
    send(m); free(m);
}
#define MODE_HOOKS(n, i) \
    static void h_##n##_click(Obj s, V2I p, bool sh, void *mi) { mode_click(i, s, p, sh, mi); } \
    static void h_##n##_drag(Obj s, V2I a, V2I b, void *mi) { mode_drag(i, s, a, b, mi); }
MODE_HOOKS(gather, 1) MODE_HOOKS(cancel, 2) MODE_HOOKS(pickup, 3) MODE_HOOKS(demolish, 4) MODE_HOOKS(build, 5)

/* DropItemRequested replay: the handler's raycast returns the unit recorded at capture */
static int ray_active;
static Obj ray_unit;
static Obj h_raycast(Obj reader, Obj inmap, void *mi) {
    if (executing && ray_active) return ray_unit;
    return o_raycast(reader, inmap, mi);
}

/* Tutorial / ObserverDriven input observers become orders (one per type per 30 frames) */
static void *input_observer_cls;
static int64_t obs_last[64];
static void h_obs_record(Obj self, Obj obs, void *mi) {
    if (!emit_fn || executing || !obs) { o_obs_record(self, obs, mi); return; }
    if (!input_observer_cls) input_observer_cls = md(TI_INPUT_OBSERVER);
    if (*(void **)obs != input_observer_cls) { o_obs_record(self, obs, mi); return; }
    int32_t t = AT(obs, 0x10, int32_t);
    if (t >= 0 && t < 64) {
        if (obs_last[t] && rig_frames - obs_last[t] < 30) return;
        obs_last[t] = rig_frames;
    }
    ord_msg *m = calloc(1, sizeof *m); if (!m) return;
    m->kind = ORD_K_OBSERVER; m->ctx.map = cur_map(); m->input_type = t;
    send(m); free(m);
}

/* Trade: the request's SessionId is the issuer's; use this peer's session (one session at a time) */
static bool h_trade_commit(Obj helper, Obj req, Obj *failure, void *mi) {
    if (executing && req && helper) memcpy((char *)req + 0x14, (char *)helper + 0x44, 16);
    return o_trade_commit(helper, req, failure, mi);
}

/* ---------------------------------------------------------------- replay */
static Obj sel_build(const ord_seldesc *d, int kind, int map) {
    switch (kind) {
    case ORD_SEL_SPECIFIC: return o_sel_specific(d->has_ent ? g_ent_find(map, &d->ent) : NULL, NULL);
    case ORD_SEL_SPECIFIC_USABLE: return o_sel_usable(d->has_ent ? g_ent_find(map, &d->ent) : NULL, NULL);
    case ORD_SEL_FROM_LIST: {
        Obj l = hold(obj_new(TI_LIST_IENTITY));
        Obj a = hold(arr_new(md(TI_IENTITY), (size_t)(d->n > 4 ? d->n : 4)));
        int n = 0;
        for (int i = 0; i < d->n; i++) { Obj e = g_ent_find(map, &d->list[i]); if (e) SETREF(a, (char *)a + 0x20 + (n++) * 8, e); }
        SETREF(l, (char *)l + 0x10, a); AT(l, 0x18, int32_t) = n; AT(l, 0x1C, int32_t) = 1;
        return n ? o_sel_from_list(l, NULL) : NULL;
    }
    case ORD_SEL_SELF: return o_sel_self(NULL);
    case ORD_SEL_CLOSEST_HOSTILE: return o_sel_hostile(NULL);
    case ORD_SEL_GRID: {
        Obj g = hold(obj_new(TI_GRIDDATA));
        AT(g, 0x10, int32_t) = d->x; AT(g, 0x14, int32_t) = d->y; AT(g, 0x18, int32_t) = d->flag;
        return o_sel_grid(g, NULL);
    }
    case ORD_SEL_TO_GRID: { Obj in = hold(sel_build(d, d->inner, map)); return in ? o_sel_to_grid(in, NULL) : NULL; }
    default: return NULL;
    }
}

static int dispatch_event(Obj ev) {
    Obj eq = AT(mf, 0x48, Obj);
    if (!eq) return -1;
    Obj type = F(VA_OBJECT_GETTYPE, Obj (*)(Obj, void *))(ev, NULL);
    Obj dg = dict_ref_get(AT(eq, 0x28, Obj), type);           /* Action<IEventData>: run now, like ProcessSyncEvent */
    if (dg) {
        /* the delegate's own method (EventQueue closure b__2) on its target, through il2cpp_runtime_invoke so a
         * handler exception is caught here instead of unwinding through ord_apply and the rig's Update hook */
        Obj exc = NULL; void *args[1] = { ev };
        F(VA_IL_RUNTIME_INVOKE, Obj (*)(void *, Obj, void **, Obj *))(AT(dg, 0x28, void *), AT(dg, 0x20, Obj), args, &exc);
        if (exc) { rig_log("ord: %s handler threw %s", cls_name(ev), cls_name(exc)); return -20; }
        return 0;
    }
    if (dict_ref_get(AT(eq, 0x30, Obj), type)) {              /* async handler: queue (runs in EventQueue's coroutine) */
        F(VA_EQ_ADDEVENT, void (*)(Obj, Obj, void *))(eq, ev, NULL);
        return 0;
    }
    rig_log("ord: no handler for %s", cls_name(ev));
    return -1;
}

/* Expedition party for the replay: our own ExpeditionPlanPartyContext filled with the issuer's party */
static Obj party_obj;
static Obj party_ctx(const ord_msg *m) {
    if (!party_obj) {
        void *k = class_by_name("Refactor.UI", "ExpeditionPlanPartyContext");
        if (!k) return NULL;
        party_obj = obj_alloc(k);
        F(VA_IL_GCHANDLE_NEW, void *(*)(Obj, bool))(party_obj, false);   /* never freed */
        F(VA_PARTY_CTOR, void (*)(Obj, void *))(party_obj, NULL);
    }
    F(VA_PARTY_REPLACE, void (*)(Obj, Obj, void *))(party_obj, g_lguid_new(m->party, m->nparty), NULL);
    AT(party_obj, 0x18, uint8_t) = m->party_active;
    return party_obj;
}
/* Recruit: unit Guids on the map before the handler; new ones after it go to the recruit hook */
static Guid *unit_guids(int map, int *n) {
    Obj ud = units_dict(map); *n = 0;
    if (!ud) return calloc(1, sizeof(Guid));
    Obj ent = AT(ud, 0x18, Obj); int cnt = AT(ud, 0x20, int32_t);
    Guid *g = calloc((size_t)cnt + 1, sizeof(Guid));
    for (int i = 0; ent && i < cnt; i++) {
        char *e = (char *)ent + 0x20 + i * 0x20;
        if (*(int32_t *)e >= 0) g[(*n)++] = *(Guid *)(e + 8);
    }
    return g;
}
static void recruit_report(int map, const Guid *before, int nb, int player) {
    int na = 0; Guid *after = unit_guids(map, &na);
    for (int i = 0; i < na; i++) {
        int old = 0;
        for (int k = 0; k < nb && !old; k++) old = !memcmp(&after[i], &before[k], 16);
        if (old) continue;
        char gs[40]; guid_str(after[i], gs);
        rig_log("ord: recruit by player %d -> new unit %s", player, gs);
        if (recruit_hook) recruit_hook((const uint8_t *)&after[i], player);
    }
    free(after);
}

static int apply_msg(ord_msg *m, int player) {
    int map = m->ctx.map, rc = 0;
    if (m->kind != ORD_K_OBSERVER && sel_swap_in(&m->ctx, player)) { rig_log("ord: no selection container"); return -10; }
    switch (m->kind) {
    case ORD_K_EVENT: {
        if (m->ev->flags & ORD_EF_RAYCAST) { ray_active = 1; ray_unit = m->has_ray ? g_ent_find(map, &m->ray) : NULL; }
        Obj ih = item_handler(), saved_party = NULL;
        if ((m->ev->flags & ORD_EF_PARTY) && ih) {           /* ItemEventHandler._expeditionPlanPartyReader (+0x68) */
            Obj pc = party_ctx(m);
            if (pc) { saved_party = AT(ih, 0x68, Obj); SETREF(ih, (char *)ih + 0x68, pc); }
        }
        ord_guid it;
        if (event_item(m->ev, m->evobj, map, &it) && !item_allowed(player, &it)) {   /* same answer on every peer */
            rig_log("ord: %s by player %d takes another player's item: dropped", m->ev->name, player);
            rc = -30;
        } else {
            int nb = 0; Guid *before = (m->ev->flags & ORD_EF_RECRUIT) ? unit_guids(map, &nb) : NULL;
            int ni = 0; Guid *ibefore = (m->ev->flags & ORD_EF_TRADE) ? item_guids(map, &ni) : NULL;
            rc = dispatch_event(m->evobj);
            if (before) { recruit_report(map, before, nb, player); free(before); }
            if (ibefore) { tag_new_items(map, ibefore, ni, player, 1); free(ibefore); }   /* bought items */
            if (!strcmp(m->ev->name, "CraftingTaskGenerateRequested"))
                for (int i = 0; i < m->ctx.nsel; i++)
                    if (is_cls(g_ent_find(map, &m->ctx.sel[i]), "BuildingEntity")) { Guid g; memcpy(&g, &m->ctx.sel[i], 16); craft_owner_set(&g, player); break; }
        }
        if (saved_party) SETREF(ih, (char *)ih + 0x68, saved_party);
        ray_active = 0; ray_unit = NULL;
        break;
    }
    case ORD_K_MODE_CLICK: case ORD_K_MODE_DRAG: {
        Obj proc = mode_proc(m->mode), c = ctc();
        if (!is_cls(proc, mode_cls[m->mode]) || !c) { rc = -11; break; }
        Obj skey = AT(c, 0x18, Obj); int32_t srot = AT(c, 0x20, int32_t), sdx = AT(c, 0x24, int32_t), sdy = AT(c, 0x28, int32_t);
        uint8_t sfree = AT(c, 0x10, uint8_t), screep = AT(c, 0x11, uint8_t);
        SETREF(c, (char *)c + 0x18, m->has_ckey ? g_str_new(m->key) : NULL);
        AT(c, 0x20, int32_t) = m->rotation; AT(c, 0x24, int32_t) = m->dsx; AT(c, 0x28, int32_t) = m->dsy;
        AT(c, 0x10, uint8_t) = 0; AT(c, 0x11, uint8_t) = 0;            /* cheat modes never replay */
        V2I a = { m->x, m->y }, b = { m->x2, m->y2 };
        if (m->mode == 3) {                                  /* pick-up designation: the issuer's units only */
            if (m->kind == ORD_K_MODE_CLICK) pickup_area_add(map, a.x, a.y, a.x, a.y, player);
            else pickup_area_add(map, a.x, a.y, b.x, b.y, player);
        }
        if (m->kind == ORD_K_MODE_CLICK) o_mclick[m->mode](proc, a, m->shift, NULL);
        else o_mdrag[m->mode](proc, a, b, NULL);
        SETREF(c, (char *)c + 0x18, skey);
        AT(c, 0x20, int32_t) = srot; AT(c, 0x24, int32_t) = sdx; AT(c, 0x28, int32_t) = sdy;
        AT(c, 0x10, uint8_t) = sfree; AT(c, 0x11, uint8_t) = screep;
        break;
    }
    case ORD_K_MOVE_ALL: case ORD_K_MOVE_TRY: case ORD_K_WP_RESERVE: case ORD_K_WP_CLEAR: {
        Obj fl = command_flow(); Obj sc = sel_container();
        if (!fl || !sc) { rc = -11; break; }
        V2I p = { m->x, m->y };
        if (m->kind == ORD_K_MOVE_ALL) o_move_all(AT(fl, 0x18, Obj), sc, p, NULL);
        else if (m->kind == ORD_K_MOVE_TRY) o_move_try(AT(fl, 0x18, Obj), sc, NULL);
        else if (m->kind == ORD_K_WP_RESERVE) o_wp_reserve(AT(fl, 0x20, Obj), sc, p, NULL);
        else o_wp_clear(AT(fl, 0x20, Obj), sc, NULL);
        break;
    }
    case ORD_K_RUN: case ORD_K_ACTION: case ORD_K_MOVE_NEAR: {
        Obj rn = runner();
        if (!rn) { rc = -11; break; }
        Obj action = AT(rn, 0x18, Obj), check = AT(rn, 0x10, Obj), move = AT(rn, 0x20, Obj);
        V2I p = { m->x, m->y };
        if (m->kind == ORD_K_MOVE_NEAR) {
            Obj u = g_ent_find(map, &m->unit);
            if (u) o_move_near(move, u, p, NULL); else rc = -12;
            { Guid ug; memcpy(&ug, &m->unit, 16); char gs[40]; guid_str(ug, gs);
              rig_log("ord: MOVE_NEAR by player %d: unit %s %s to (%d,%d) map %d", player, gs, u ? "sent" : "NOT FOUND", m->x, m->y, map); }
            break;
        }
        Obj done[ORD_MAX_ACTS + ORD_MAX_LIST]; int ndone = 0, nnear_done = 0;
        for (int i = 0; i < m->nact; i++) {
            Obj u = g_ent_find(map, &m->act[i].unit);
            int skip = !u || !may_command(u, player);
            for (int k = 0; k < ndone && !skip; k++) if (done[k] == u) skip = 1;
            if (skip) continue;
            uint8_t ia[ORD_IA_SIZE] __attribute__((aligned(8))) = { 0 };
            ord_ia_to_mem(&genv, &m->act[i].ia, map, ia);
            if (o_execute(action, ia, u, check, NULL)) done[ndone++] = u;
        }
        for (int i = 0; i < m->nnear; i++) {
            Obj u = g_ent_find(map, &m->near_units[i]);
            int skip = !u || !may_command(u, player);
            for (int k = 0; k < ndone && !skip; k++) if (done[k] == u) skip = 1;
            if (!skip) { o_move_near(move, u, p, NULL); nnear_done++; }
        }
        /* lan_v9 (lead): an order that moves nobody must be visible in the log: selection empty when captured (no
           candidates), units refused (replay skips ... above) or no action at that cell (unreachable / nothing to do) */
        rig_log("ord: RUN by player %d at (%d,%d) map %d: %d action candidates (%d executed), %d move-near (%d sent), selection %d",
                player, m->x, m->y, map, m->nact, ndone, m->nnear, nnear_done, m->ctx.nsel);
        break;
    }
    case ORD_K_USE_SKILL: {
        Obj mode = mode_proc(6);
        if (!is_cls(mode, "UseSkillMode")) { rc = -11; break; }
        Obj user = m->has_user ? g_ent_find(map, &m->user) : NULL;
        Obj target = m->has_target ? g_ent_find(map, &m->target) : NULL;
        Obj item = m->has_item ? g_ent_find(map, &m->item) : NULL;
        Obj cdm = F(VA_COMBAT_INSTANCE, Obj (*)(void *))(NULL);
        Obj skill = (cdm && m->key[0]) ? F(VA_GET_ACTIVE_SKILL, Obj (*)(Obj, Obj, void *))(cdm, g_str_new(m->key), NULL) : NULL;
        Obj sel = hold(sel_build(&m->sel, m->sel.kind, map));
        if (m->has_item && !item_allowed(player, &m->item)) { rc = -30; break; }
        if (!user || !skill || (m->has_item && !item) || !sel) {
            rig_log("ord: UseSkill %s not replayed (user %p skill %p item %p selector kind %d %p)",
                    m->key, user, skill, item, m->sel.kind, sel);
            rc = -12; break;
        }
        o_use_skill(mode, user, target, skill, sel, item, NULL);
        break;
    }
    case ORD_K_AUTO_SKILL: {
        Obj p = pc_handler(); Obj ec = p ? AT(p, 0x18, Obj) : NULL;
        Obj u = g_ent_find(map, &m->unit);
        Obj comp = (ec && u) ? F(VA_GET_COMPONENT_OF, Obj (*)(Obj, Obj, void *))(ec, u, md(MI_GETCOMP_SKILL)) : NULL;
        if (!is_cls(comp, "SkillComponent")) { rc = -12; break; }
        o_auto_skill(comp, g_str_new(m->key), NULL);
        break;
    }
    case ORD_K_OBSERVER: {
        Obj buf = observer_buffer();
        if (!buf) { rc = -11; break; }
        Obj o = hold(obj_new(TI_INPUT_OBSERVER));
        AT(o, 0x10, int32_t) = m->input_type;
        o_obs_record(buf, o, NULL);
        break;
    }
    default: rc = -3;
    }
    sel_restore();
    return rc;
}

int ord_apply(int player, const uint8_t *payload, size_t len) {
    recover();
    if (!mf || executing || hooks_bad || !o_b8_0) return -1;
    ord_msg *m = calloc(1, sizeof *m);
    if (!m) return -1;
    executing = 1; exec_frame = rig_frames;
    int rc = ord_msg_read(&genv, payload, len, m);
    if (rc) rig_log("ord: bad order payload (%d, %zu bytes)", rc, len);
    else {
        if (ord_trace()) rig_log("ord trace: apply player %d kind %d %s nsel %d", player, m->kind, m->ev ? m->ev->name : "-", m->ctx.nsel);
        rc = apply_msg(m, player);
    }
    executing = 0;
    release_all();
    if (rc) st.apply_failed++; else st.applied++;
    free(m);
    return rc;
}

/* ---------------------------------------------------------------- api */
void ord_capture(ord_emit_fn fn, void *user) { emit_fn = hooks_bad ? NULL : fn; emit_user = user; }
void ord_set_ticking(int on) { ticking = on; }
void ord_set_recruit_hook(void (*fn)(const uint8_t unit[16], int player)) { recruit_hook = fn; }
void ord_set_gear_check(int (*fn)(const uint8_t unit[16])) { gear_check = fn; }
void ord_set_item_api(const ord_item_api *a) { if (a) item_api = *a; else memset(&item_api, 0, sizeof item_api); }
int  ord_executing(void) { return executing; }
void ord_get_stats(ord_stats *out) { *out = st; }

void ord_install(void) {
    struct { uintptr_t va; void *h; void **o; const char *who; } hk[] = {
        { VA_B8_0, h_b8_0, (void **)&o_b8_0, "ord InputDispatcher.b__8_0" },
        { VA_EQ_B2, h_b2, (void **)&o_b2, "ord EventQueue.b__2" },
        { VA_EQ_B3, h_b3, (void **)&o_b3, "ord EventQueue.b__3" },
        { VA_MOVE_ALL, h_move_all, (void **)&o_move_all, "ord MoveOrder.All" },
        { VA_MOVE_TRY, h_move_try, (void **)&o_move_try, "ord MoveOrder.TryMove" },
        { VA_MOVE_NEAR, h_move_near, (void **)&o_move_near, "ord MoveOrder.Near" },
        { VA_WP_RESERVE, h_wp_reserve, (void **)&o_wp_reserve, "ord WaypointOrder.Reserve" },
        { VA_WP_CLEAR, h_wp_clear, (void **)&o_wp_clear, "ord WaypointOrder.ClearWayPoint" },
        { VA_RUN, h_run, (void **)&o_run, "ord InteractionRunner.Run" },
        { VA_EXECUTE, h_execute, (void **)&o_execute, "ord ActionOrder.Execute" },
        { VA_USE_SKILL, h_use_skill, (void **)&o_use_skill, "ord UseSkillMode.UseSkill" },
        { VA_SEL_SPECIFIC, h_sel_specific, (void **)&o_sel_specific, "ord EntityTargetSelector.SpecificTarget" },
        { VA_SEL_USABLE, h_sel_usable, (void **)&o_sel_usable, "ord EntityTargetSelector.SpecificUsableTarget" },
        { VA_SEL_FROM_LIST, h_sel_from_list, (void **)&o_sel_from_list, "ord EntityTargetSelector.SelectFromSpecificTargets" },
        { VA_SEL_SELF, h_sel_self, (void **)&o_sel_self, "ord EntityTargetSelector.Self" },
        { VA_SEL_HOSTILE, h_sel_hostile, (void **)&o_sel_hostile, "ord EntityTargetSelector.ClosestHostile" },
        { VA_SEL_GRID, h_sel_grid, (void **)&o_sel_grid, "ord GridTargetSelector.SpecificGrid" },
        { VA_SEL_TO_GRID, h_sel_to_grid, (void **)&o_sel_to_grid, "ord GridTargetSelector.ToGrid" },
        { VA_AUTO_SKILL, h_auto_skill, (void **)&o_auto_skill, "ord SkillComponent.ChangeAutoSkillData" },
        { VA_RAYCAST, h_raycast, (void **)&o_raycast, "ord EntityUtility.GetRayCastPlayerEntities" },
        { VA_OBS_RECORD, h_obs_record, (void **)&o_obs_record, "ord ObserverAggregator.Record" },
        { VA_TRADE_COMMIT, h_trade_commit, (void **)&o_trade_commit, "ord TradeHelper.TryCommit" },
        { VA_GATHER_CLICK, h_gather_click, (void **)&o_mclick[1], "ord GatheringMode.LeftClickInteraction" },
        { VA_GATHER_DRAG, h_gather_drag, (void **)&o_mdrag[1], "ord GatheringMode.DragSelection" },
        { VA_CANCEL_CLICK, h_cancel_click, (void **)&o_mclick[2], "ord CancelMode.LeftClickInteraction" },
        { VA_CANCEL_DRAG, h_cancel_drag, (void **)&o_mdrag[2], "ord CancelMode.DragSelection" },
        { VA_PICKUP_CLICK, h_pickup_click, (void **)&o_mclick[3], "ord PickUpMode.LeftClickInteraction" },
        { VA_PICKUP_DRAG, h_pickup_drag, (void **)&o_mdrag[3], "ord PickUpMode.DragSelection" },
        { VA_DEMOLISH_CLICK, h_demolish_click, (void **)&o_mclick[4], "ord DemolitionMode.LeftClickInteraction" },
        { VA_DEMOLISH_DRAG, h_demolish_drag, (void **)&o_mdrag[4], "ord DemolitionMode.DragSelection" },
        { VA_BUILD_CLICK, h_build_click, (void **)&o_mclick[5], "ord ConstructionMode.LeftClickInteraction" },
        { VA_BUILD_DRAG, h_build_drag, (void **)&o_mdrag[5], "ord ConstructionMode.DragSelection" },
        { VA_PICKUP_EVENT, h_pickup_event, (void **)&o_pickup_event, "ord ItemEventHandler.OnEvent(PickUpItemRequested)" },
        { VA_TASK_AVAILABLE, h_task_available, (void **)&o_task_available, "ord TaskAssignableHelper.IsAvailable" },
        { VA_TRY_CRAFT, h_try_craft, (void **)&o_try_craft, "ord TaskFinishEventHandler.TryCraft" },
    };
    int bad = 0;
    for (size_t i = 0; i < sizeof hk / sizeof hk[0]; i++) bad |= rig_hook(hk[i].va, hk[i].h, hk[i].o, hk[i].who) != 0;
    if (bad) {   /* a missing hook would desync silently (and replay calls the originals): stay off */
        rig_log("ord: some hooks failed: order capture and replay stay OFF");
        hooks_bad = 1; emit_fn = NULL;
    }
    rig_log("ord: installed (%d event types, wire v%d)", ord_event_count, ORD_VERSION);
}

/* ---------------------------------------------------------------- test hooks (ord_test/ord_smoke.c) */
void *ord_debug_lguid_new(const uint8_t (*g)[16], int n) { return g_lguid_new((const ord_guid *)g, n); }
void *ord_debug_find_entity(int map, const uint8_t g[16]) { return g_ent_find(map, (const ord_guid *)g); }
void *ord_debug_class(const char *ns, const char *name) { return class_by_name(ns, name); }
const char *ord_debug_class_name(void *obj) { return cls_name(obj); }
/* replace the selection with a fresh List<IEntity> of these units (no view update) */
int ord_debug_select(int map, const uint8_t (*g)[16], int n, int group) {
    Obj sc = sel_container();
    if (!sc) return -1;
    Obj l = obj_new(TI_LIST_IENTITY);
    Obj a = arr_new(md(TI_IENTITY), (size_t)(n > 4 ? n : 4));
    int k = 0;
    for (int i = 0; i < n; i++) { Obj e = g_ent_find(map, (const ord_guid *)g[i]); if (e) SETREF(a, (char *)a + 0x20 + (k++) * 8, e); }
    SETREF(l, (char *)l + 0x10, a); AT(l, 0x18, int32_t) = k; AT(l, 0x1C, int32_t) = 1;
    AT(sc, 0x10, int32_t) = group; SETREF(sc, (char *)sc + 0x18, l);
    return k;
}
int ord_debug_selection(uint8_t (*out)[16], int max, int *group) {
    ord_ctx c; ctx_capture(&c, NULL);
    *group = c.sel_group;
    int n = c.nsel < max ? c.nsel : max;
    memcpy(out, c.sel, (size_t)n * 16);
    return n;
}
void *ord_debug_skill_component(void *unit) {
    Obj p = pc_handler(); Obj ec = p ? AT(p, 0x18, Obj) : NULL;
    return (ec && unit) ? F(VA_GET_COMPONENT_OF, Obj (*)(Obj, Obj, void *))(ec, unit, md(MI_GETCOMP_SKILL)) : NULL;
}
