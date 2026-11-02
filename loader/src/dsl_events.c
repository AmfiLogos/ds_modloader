// dsl_events.c - the loader's EVENTS (SDK API 7: on_event / off_event / view_shown; owner 2026-11-02, PLAN_v013 items
// 3 + 32, the tModLoader study's idea 1; plan ds_multiplayer ext/nvevents/PLAN.md). The registry is src/dsl_evreg.h.
//
// WHY. Every mod that needed "the title is on screen", "a world is open", "the Esc menu shows" or a frame tick hooked a
// game Update method or polled (the 2026-10-26 FPS audit: polls were the loader's FPS cost). The loader owns ONE hook
// per event and calls every module that listens; two mods share an event, none of them searches the scene.
//
// Sources (game build 25846389, checked on Mac and Windows: ext/nvevents/PLAN.md "Step 1"):
//   views: Refactor.UI.UI_Base.InitOnCreateUI (both containers make a window there: VIEW_CREATED; a static view is also
//          active from then on: VIEW_SHOWN), UI_Base.Activate / Deactivate (only PopUpViewContainer.ActivateView /
//          DeactivateView call them: VIEW_SHOWN / VIEW_HIDDEN), PopUpViewContainer / StaticViewContainer.ClearOnSceneMove
//          (before the call: VIEW_HIDDEN for every shown view of that container). Static views (TitleMenuView,
//          TooltipUI, QuickSlotUI, CommonNoticeView) never go through Activate / Deactivate.
//          Windows: the linker folded UI_Base.Activate (with 5 other methods) and Deactivate (with 3) into shared code.
//          The hook there acts only when its return address is the one call in PopUpViewContainer.ActivateView /
//          DeactivateView (found in the code at install); every other caller tail-jumps on untouched (no object read).
//   frame, scene, world, language: the kit tick (EventSystem.Update, the loader's own hook): dsl_events_frame at the end
//          of dsl_frame_hook.
// Records check (every frame, about 1 us): the game's own window records (PopUpViewContainer.ActiveUIs,
// StaticViewContainer.ActiveUIs: the dictionaries dsl_view_find reads; never a scene search) are compared with the
// events sent. A window the hooks did not report gets its events here, one frame late: this IS the fallback when a hook
// site is refused or a call site is not found (or DSL_EV_NOHOOK=1 in a test copy). In a test copy, a window missed by a
// hook that is on logs "EVENT-RULE missed ..." (loader/test/events_test.py fails on it).
// Objects are kept as weak GC handles between frames (traps T1); the raw pointer is only compared, never read.
#include "dsl_internal.h"
#include "../sdk/dsl_sdk.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void *Obj;

static struct {
    uintptr_t (*gc_new)(Obj, bool); Obj (*gc_get)(uintptr_t); void (*gc_free)(uintptr_t);
    void *(*obj_class)(Obj); bool (*assignable)(void *, void *); const char *(*cname)(void *);
    void *(*vmethod)(Obj, void *);
} I;

/* ------------------------------------------------------------------ the registry (dsl_evreg.h) */
static int evr_running(DSLPlugin *self) { return !self || dsl_plugin_running(self); }
static int evr_class_is(void *vk, void *want) { return vk && want && I.assignable && I.assignable(want, vk); }
static const char *evr_name(DSLPlugin *self) { return dsl_plugin_name(self); }
static uint64_t evr_clock_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
#define evr_log dsl_log
#include "dsl_evreg.h"

static int on;                                   /* il2cpp names bound: events run */
static uint64_t frame;
static int scene_last = -1, lang_last = -1;
static char world[40];                           /* the open world's guid after WORLD_LOADED, "" = none */
static int test_copy;
static void meter_on(void) {                     /* test copies: the per-mod time meter (dsl_evreg.h) + the start times check */
    if (evr_meter) return;
    evr_meter = 1;
    dsl_log("modtime: meter on (test copy): event callbacks timed per module; SLOW-MOD over %.0f ms per s (title, world)", EVR_SLOW_MS_S);
}

static DSLEvent ev_base(int type) {
    DSLEvent e; memset(&e, 0, sizeof e);
    e.size = sizeof e; e.type = type; e.scene = dsl_reg_scene(); e.prev_scene = e.scene; e.loading = dsl_soft_loading(); e.frame = frame;
    e.guid = world; e.lang = dsi_lang(); e.lang_code = dsi_code();
    return e;
}

/* ------------------------------------------------------------------ the view table */
#define VT_MAX 160
typedef struct { Obj o; uintptr_t h; void *k; int kind, id, shown, seen; } VEnt;
static VEnt VT[VT_MAX]; static int nvt;
static void *k_ui, *k_static, *mi_popid, *mi_staid;
static int hk_init, hk_act, hk_deact, hk_clr_pop, hk_clr_sta;   /* 1 = that hook is on (a miss there is an EVENT-RULE) */
static unsigned miss_n, rule_n;

static const char *kname(void *k) { return k && I.cname ? I.cname(k) : "?"; }
static int vcall_id(Obj o, void *mi) {             /* UI_Base.get_PopUpID / get_StaticID (virtual: the class's own) */
    void *m = mi && I.vmethod ? I.vmethod(o, mi) : NULL;
    void *fp = m ? *(void **)m : NULL;
    return fp ? ((int32_t (*)(Obj, const void *))fp)(o, m) : -1;
}
static VEnt *vt_find(Obj o) { for (int i = 0; i < nvt; i++) if (VT[i].o == o) return &VT[i]; return NULL; }
static VEnt *vt_add(Obj o, int kind, int id) {
    if (nvt >= VT_MAX) {                                     /* full (never seen: about 50 windows exist): drop a hidden one */
        int j = 0; while (j < nvt && VT[j].shown) j++;
        if (j == nvt) return NULL;
        I.gc_free(VT[j].h); VT[j] = VT[--nvt];
    }
    VEnt *e = &VT[nvt++]; memset(e, 0, sizeof *e);
    e->o = o; e->h = I.gc_new(o, false); e->k = I.obj_class(o); e->kind = kind;
    e->id = id >= 0 ? id : vcall_id(o, kind == DSL_VIEW_STATIC ? mi_staid : mi_popid);
    return e;
}
static void vt_drop(int kind) {                            /* kind 0 = every entry */
    int k = 0;
    for (int i = 0; i < nvt; i++) { if (!kind || VT[i].kind == kind) I.gc_free(VT[i].h); else VT[k++] = VT[i]; }
    nvt = k;
}
static void send_view(int type, VEnt *e, Obj o) {
    if (o) dsl_view_note(o);                                 /* dsl_view_find's cache: the next lookup is a handle read */
    if (!evr_n) return;
    DSLEvent ev = ev_base(type);
    ev.view = o; ev.klass = e->k; ev.view_kind = e->kind; ev.view_id = e->id;
    evr_send(&ev);
}
static void miss(const char *what, VEnt *e, int hooked) {
    miss_n++;
    if (hooked && test_copy && rule_n++ < 20)
        dsl_log("EVENT-RULE missed %s %s (%s %d): the hook did not report it, the records check did (one frame late)",
                what, kname(e->k), e->kind == DSL_VIEW_STATIC ? "static view" : "popup", e->id);
}

/* hook bodies: after the game's own call */
static void on_created(Obj o) {
    if (!o || vt_find(o)) return;
    int st = k_static && I.assignable(k_static, I.obj_class(o));
    VEnt *e = vt_add(o, st ? DSL_VIEW_STATIC : DSL_VIEW_POPUP, -1); if (!e) return;
    send_view(DSL_EV_VIEW_CREATED, e, o);
    if (st && (e = vt_find(o)) && !e->shown) { e->shown = 1; send_view(DSL_EV_VIEW_SHOWN, e, o); }   /* a static view is active from now on */
}
static void on_shown(Obj o) {
    if (!o) return;
    VEnt *e = vt_find(o);
    if (!e) { if (!(e = vt_add(o, DSL_VIEW_POPUP, -1))) return; send_view(DSL_EV_VIEW_CREATED, e, o); if (!(e = vt_find(o))) return; }
    if (e->shown) return;
    e->shown = 1; send_view(DSL_EV_VIEW_SHOWN, e, o);
}
static void on_hidden(Obj o) {
    VEnt *e = o ? vt_find(o) : NULL;
    if (!e || !e->shown) return;
    e->shown = 0; send_view(DSL_EV_VIEW_HIDDEN, e, o);
}
static void on_clear(int kind) {                           /* before the container forgets its windows (scene change) */
    for (int i = 0; i < nvt; i++) {
        VEnt *e = &VT[i]; if (e->kind != kind || !e->shown) continue;
        e->shown = 0; Obj o = I.gc_get(e->h);
        send_view(DSL_EV_VIEW_HIDDEN, e, o);                 /* a callback may add entries: VT[i] stays valid (no drop here) */
    }
    vt_drop(kind);
}

/* the records check: the game's ActiveUIs against the table (both kinds) */
static void records_check(void) {
    static Obj vals[64]; static int keys[64];
    for (int w = 0; w < 2; w++) {
        int kind = w ? DSL_VIEW_STATIC : DSL_VIEW_POPUP;
        int n = dsl_view_records(w, vals, keys, 64);
        if (n < 0) continue;                                 /* not readable now (no MainFlow, a scene change) */
        for (int i = 0; i < nvt; i++) if (VT[i].kind == kind) VT[i].seen = 0;
        for (int i = 0; i < n; i++) {
            Obj v = vals[i]; VEnt *e = vt_find(v);
            if (e && e->kind != kind) continue;
            if (!e) {
                if (!(e = vt_add(v, kind, keys[i]))) continue;
                miss("created", e, hk_init); send_view(DSL_EV_VIEW_CREATED, e, v);
                if (!(e = vt_find(v))) continue;
            }
            e->seen = 1;
            if (!e->shown) { e->shown = 1; miss("shown", e, kind == DSL_VIEW_STATIC ? hk_init : hk_act); send_view(DSL_EV_VIEW_SHOWN, e, v); }
        }
        for (int i = 0; i < nvt; i++) {
            VEnt *e = &VT[i];
            if (e->kind != kind || !e->shown || e->seen) continue;
            e->shown = 0;
            miss("hidden", e, kind == DSL_VIEW_STATIC ? hk_clr_sta : hk_deact);
            send_view(DSL_EV_VIEW_HIDDEN, e, I.gc_get(e->h));
        }
    }
}

/* ------------------------------------------------------------------ the hooks */
static void (*o_init)(Obj, Obj, const void *);
static void h_init(Obj self, Obj input, const void *mi) { o_init(self, input, mi); on_created(self); }
static void (*o_clr_pop)(Obj, const void *), (*o_clr_sta)(Obj, const void *);
static void h_clr_pop(Obj self, const void *mi) { on_clear(DSL_VIEW_POPUP); o_clr_pop(self, mi); }
static void h_clr_sta(Obj self, const void *mi) { on_clear(DSL_VIEW_STATIC); o_clr_sta(self, mi); }
#ifdef _WIN32
/* shared code (identical-code folding): 4 pointer arguments passed on untouched (rcx rdx r8 r9; none of the sharing
   methods takes more), the result register too; only the one call in the popup container acts */
typedef void *(*Fn4)(void *, void *, void *, void *);
static Fn4 o_activate, o_deactivate;
static uintptr_t ret_act, ret_deact;
#if defined(__has_attribute)
#if __has_attribute(musttail)
#define EV_TAIL __attribute__((musttail))
#endif
#endif
#ifndef EV_TAIL
#define EV_TAIL
#endif
static __attribute__((noinline)) void *act_slow(void *a, void *b, void *c, void *d) { o_activate(a, b, c, d); on_shown(a); return NULL; }
static __attribute__((noinline)) void *deact_slow(void *a, void *b, void *c, void *d) { o_deactivate(a, b, c, d); on_hidden(a); return NULL; }
static void *h_activate(void *a, void *b, void *c, void *d) {        /* no frame: a compare and a jump for the other methods */
    if ((uintptr_t)__builtin_return_address(0) != ret_act) EV_TAIL return o_activate(a, b, c, d);
    EV_TAIL return act_slow(a, b, c, d);
}
static void *h_deactivate(void *a, void *b, void *c, void *d) {
    if ((uintptr_t)__builtin_return_address(0) != ret_deact) EV_TAIL return o_deactivate(a, b, c, d);
    EV_TAIL return deact_slow(a, b, c, d);
}
/* the return address of the one direct call (E8 rel32) of callee inside the first n bytes of fn; 0 = none or several */
static uintptr_t call_ret(const void *fn, size_t n, const void *callee) {
    const uint8_t *p = fn; uintptr_t r = 0; int hits = 0;
    for (size_t i = 0; p && i + 5 <= n; i++) {
        if (p[i] != 0xE8) continue;
        int32_t d; memcpy(&d, p + i + 1, 4);
        if ((uintptr_t)(p + i + 5) + (intptr_t)d == (uintptr_t)callee) { r = (uintptr_t)(p + i + 5); hits++; }
    }
    return hits == 1 ? r : 0;
}
#else
static void (*o_activate)(Obj, const void *), (*o_deactivate)(Obj, const void *);
static void h_activate(Obj self, const void *mi) { o_activate(self, mi); on_shown(self); }
static void h_deactivate(Obj self, const void *mi) { o_deactivate(self, mi); on_hidden(self); }
#endif

/* il2cpp ready, after dsl_reg_ready (the kit and dsl_view are up), before the modules' init (main thread) */
void dsl_events_install(void) {
    static int done; if (done) return; done = 1;
#define X(var, name) *(void **)&I.var = dsl_il2cpp_fn(name)
    X(gc_new, "il2cpp_gchandle_new"); X(gc_get, "il2cpp_gchandle_get_target"); X(gc_free, "il2cpp_gchandle_free");
    X(obj_class, "il2cpp_object_get_class"); X(assignable, "il2cpp_class_is_assignable_from"); X(cname, "il2cpp_class_get_name");
    X(vmethod, "il2cpp_object_get_virtual_method");
#undef X
    if (!I.gc_new || !I.gc_get || !I.gc_free || !I.obj_class || !I.assignable) { dsl_log("events: il2cpp names missing: events off"); return; }
    dsl_view_install();
    on = 1; lang_last = dsi_lang();
    test_copy = dsl_soft_test_copy() != NULL;
    if (test_copy) meter_on();
    k_ui = dsl_find_class("Refactor.UI", "UI_Base"); k_static = dsl_find_class("Refactor.UI", "StaticUI_Base");
    mi_popid = k_ui ? dsl_find_method(k_ui, "get_PopUpID", 0) : NULL; mi_staid = k_ui ? dsl_find_method(k_ui, "get_StaticID", 0) : NULL;
    const char *nh = getenv("DSL_EV_NOHOOK");
    if (nh && *nh == '1' && test_copy) { dsl_log("events: DSL_EV_NOHOOK=1 (test copy): no view hooks, the records check sends the view events"); return; }
    void *a_init = dsl_find_method_address("Refactor.UI", "UI_Base", "InitOnCreateUI", 1);
    void *a_act = dsl_find_method_address("Refactor.UI", "UI_Base", "Activate", 0);
    void *a_deact = dsl_find_method_address("Refactor.UI", "UI_Base", "Deactivate", 0);
    void *a_cpop = dsl_find_method_address("Refactor.UI", "PopUpViewContainer", "ClearOnSceneMove", 0);
    void *a_csta = dsl_find_method_address("Refactor.UI", "StaticViewContainer", "ClearOnSceneMove", 0);
    int ok_act = a_act != NULL, ok_deact = a_deact != NULL;
#ifdef _WIN32
    void *a_pav = dsl_find_method_address("Refactor.UI", "PopUpViewContainer", "ActivateView", 1);
    void *a_pdv = dsl_find_method_address("Refactor.UI", "PopUpViewContainer", "DeactivateView", 1);
    ret_act = a_pav && a_act ? call_ret(a_pav, 0x400, a_act) : 0;
    ret_deact = a_pdv && a_deact ? call_ret(a_pdv, 0x400, a_deact) : 0;
    if (!ret_act) { ok_act = 0; dsl_log("events: the call of UI_Base.Activate in PopUpViewContainer.ActivateView not found: popups shown by the records check"); }
    if (!ret_deact) { ok_deact = 0; dsl_log("events: the call of UI_Base.Deactivate in PopUpViewContainer.DeactivateView not found: popups hidden by the records check"); }
#endif
    hk_init = a_init && !dsl_hook_m(DSL_OWNER_LOADER, a_init, (void *)h_init, (void **)&o_init, "loader events UI_Base.InitOnCreateUI", "Refactor.UI.UI_Base.InitOnCreateUI/1");
    hk_act = ok_act && !dsl_hook_m(DSL_OWNER_LOADER, a_act, (void *)h_activate, (void **)&o_activate, "loader events UI_Base.Activate", "Refactor.UI.UI_Base.Activate/0");
    hk_deact = ok_deact && !dsl_hook_m(DSL_OWNER_LOADER, a_deact, (void *)h_deactivate, (void **)&o_deactivate, "loader events UI_Base.Deactivate", "Refactor.UI.UI_Base.Deactivate/0");
    hk_clr_pop = a_cpop && !dsl_hook_m(DSL_OWNER_LOADER, a_cpop, (void *)h_clr_pop, (void **)&o_clr_pop, "loader events PopUpViewContainer.ClearOnSceneMove", "Refactor.UI.PopUpViewContainer.ClearOnSceneMove/0");
    hk_clr_sta = a_csta && !dsl_hook_m(DSL_OWNER_LOADER, a_csta, (void *)h_clr_sta, (void **)&o_clr_sta, "loader events StaticViewContainer.ClearOnSceneMove", "Refactor.UI.StaticViewContainer.ClearOnSceneMove/0");
    dsl_log("events: view hooks: created %s, shown %s, hidden %s, scene clear %s / %s (off = the records check sends it, one frame late)",
            hk_init ? "on" : "OFF", hk_act ? "on" : "OFF", hk_deact ? "on" : "OFF", hk_clr_pop ? "on" : "OFF", hk_clr_sta ? "on" : "OFF");
}

/* ------------------------------------------------------------------ the kit tick */
void dsl_events_frame(void) {
    if (!on) return;
    frame++;
    if (!test_copy && dsl_soft_test_copy()) { test_copy = 1; meter_on(); }
    if (test_copy) {
        static int judged; if (!judged) { judged = 1; dsl_start_times_check(); }   /* the modules' start times (dsl_main.c) */
        int sn = dsl_reg_scene();
        evr_meter_tick(evr_clock_ns(), dsl_soft_loading() ? 0 : sn == DSL_SCENE_WORLD ? 2 : sn == DSL_SCENE_TITLE ? 1 : 0);
    }
    records_check();
    int s = dsl_reg_scene();
    char g[40] = ""; int have = s == DSL_SCENE_WORLD && dsl_reg_world_guid(g);
    if (world[0] && (s != DSL_SCENE_WORLD || (have && strcmp(g, world)))) {
        if (evr_n) { DSLEvent ev = ev_base(DSL_EV_WORLD_CLOSED); evr_send(&ev); }
        if (test_copy) dsl_log("events: world closed %s", world);
        world[0] = 0;
    }
    if (s >= 0 && s != scene_last) {
        int prev = scene_last; scene_last = s;
        if (!hk_clr_pop || !hk_clr_sta) for (int i = 0; i < nvt; i++) if (!VT[i].shown) { I.gc_free(VT[i].h); VT[i--] = VT[--nvt]; }   /* no clear hook: forget the hidden windows of the last scene */
        if (test_copy) dsl_log("events: scene %d -> %d (frame %llu; %u view(s) known; records check sent %u)", prev, s, (unsigned long long)frame, (unsigned)nvt, miss_n);
        if (evr_n) { DSLEvent ev = ev_base(DSL_EV_SCENE); ev.prev_scene = prev; evr_send(&ev); }
    }
    if (!world[0] && have && !dsl_soft_loading()) {
        snprintf(world, sizeof world, "%s", g);
        if (test_copy) dsl_log("events: world loaded %s", world);
        if (evr_n) { DSLEvent ev = ev_base(DSL_EV_WORLD_LOADED); evr_send(&ev); }
    }
    int L = dsi_lang();
    if (L != lang_last) { lang_last = L; if (evr_n) { DSLEvent ev = ev_base(DSL_EV_LANG); evr_send(&ev); } }
    if (evr_n) { DSLEvent ev = ev_base(DSL_EV_FRAME); evr_send(&ev); }
}

/* ------------------------------------------------------------------ host API (SDK API 7) */
static int h_on_event(DSLPlugin *self, int type, int flags, void *klass, dsl_event_cb cb, void *ud) {
    if (self && !dsl_plugin_running(self)) return -1;
    return evr_add(self, type, flags, klass, cb, ud);
}
static int h_off_event(DSLPlugin *self, int type, dsl_event_cb cb, void *ud) { return evr_remove(self, type, cb, ud); }
static int h_view_shown(void *klass) {
    if (!on || !klass) return 0;
    for (int i = 0; i < nvt; i++) if (VT[i].shown && I.assignable(klass, VT[i].k)) return 1;
    return 0;
}
void dsl_events_host(struct DSLHost *h) { h->on_event = h_on_event; h->off_event = h_off_event; h->view_shown = h_view_shown; }
