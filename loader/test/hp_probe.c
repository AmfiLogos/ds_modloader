// hp-probe: TEST ONLY module for live_hp_test.py (never shipped). Selects a building the game's way, changes its
// durability and reads back what the bottom panel and the Inspect window show. Requests: <DSRIG_DIR>/hp_probe_req.txt,
// one line, polled at StandaloneInputModule.UpdateModule (EventSystem.Update ticks it every frame, main thread):
//   pick <KEY>      the first BuildingEntity with that table key (EntityComponent._components): its guid is kept
//   select          SelectDataContainer (PlayerControlEventHandler._selectReader): ClearAll, group Building, Select(it);
//                   SelectProcessor.Update then sends the selection to the views (as after a click)
//   add <v>         StatusComponent.AddValue(Durability, v) on it; then each frame, for 3 s, the probe compares the panel's
//                   number and bar (and the Inspect window's Durability row while it shows) with the real durability and
//                   logs the time each first matched ("watch: panel matched after N ms" / "NOT matched")
//   inspect         EntitySummaryUIView.OnToggleClicked(true, index of EntityInspect): the Inspect toggle's click
//   state           one line: what the panel and the window show, and the real durability
// Each answer is one or more dsload.log lines "hp-probe: <cmd>: ...". Every game call by il2cpp name.
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

DSL_PLUGIN();
static const DSLHost *H;
static DSLPlugin *ME;
typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define LOG(...) H->log(ME, "hp-probe: " __VA_ARGS__)

static Obj (*rt_invoke)(const void *, Obj, void **, Obj *);
static void *(*ocls)(Obj), *(*parent)(void *), *(*field)(void *, const char *), *(*ctype)(void *);
static Obj (*tobj)(void *);
static size_t (*foffset)(void *);
static void (*fs_get)(void *, void *);
static const void *(*vmethod)(Obj, const void *);
static void *(*unbox)(Obj);
static void *(*mparam)(const void *, uint32_t);
static char *(*tname)(const void *);
static char req_path[1024];

static void *K(const char *ns, const char *n) { return H->find_class(ns, n); }
static int32_t fo(void *k, const char *name) {
    for (void *c = k; c; c = parent(c)) { void *f = field(c, name); if (f) return (int32_t)foffset(f); }
    LOG("field %s not found", name);
    return -1;
}
static Obj call(const void *mi, Obj self, void **args, int *threw) {
    if (!mi) { if (threw) *threw = 1; LOG("a method was not found"); return NULL; }
    Obj exc = NULL, r = rt_invoke(mi, self, args, &exc);
    if (threw) *threw = exc != NULL;
    return exc ? NULL : r;
}
static int32_t enum_v(const char *ns, const char *cls, const char *name) {
    void *k = K(ns, cls), *f = k ? field(k, name) : NULL; int32_t v = -1;
    if (f) fs_get(f, &v);
    return v;
}
static int alive(Obj o) { static int32_t oc = -1; if (oc < 0) oc = fo(K("UnityEngine", "Object"), "m_CachedPtr"); return o && AT(o, oc, void *); }
static int active(Obj comp) {                          /* gameObject.activeInHierarchy of a Component */
    static void *mg, *ma; if (!mg) { mg = H->find_method(K("UnityEngine", "Component"), "get_gameObject", 0); ma = H->find_method(K("UnityEngine", "GameObject"), "get_activeInHierarchy", 0); }
    if (!alive(comp)) return 0;
    Obj g = call(mg, comp, NULL, NULL), r = alive(g) ? call(ma, g, NULL, NULL) : NULL;
    return r && (*(uint8_t *)unbox(r) & 1);
}
static int go_self(Obj go) {                           /* GameObject.activeSelf */
    static void *ma; if (!ma) ma = H->find_method(K("UnityEngine", "GameObject"), "get_activeSelf", 0);
    Obj r = alive(go) ? call(ma, go, NULL, NULL) : NULL;
    return r && (*(uint8_t *)unbox(r) & 1);
}
static Obj find_obj(void *k) {                         /* Object.FindObjectOfType(Type, includeInactive: true) */
    static void *mi; void *ko = K("UnityEngine", "Object");
    if (!mi) { void *it = NULL, *m; void *(*methods)(void *, void **) = H->il2cpp_fn("il2cpp_class_get_methods");
               const char *(*mname)(void *) = H->il2cpp_fn("il2cpp_method_get_name"); uint32_t (*pc)(void *) = H->il2cpp_fn("il2cpp_method_get_param_count");
               while ((m = methods(ko, &it))) if (!strcmp(mname(m), "FindObjectOfType") && pc(m) == 2) { mi = m; break; } }
    uint8_t inactive = 1; void *args[2] = { tobj(ctype(k)), &inactive };
    return mi ? call(mi, NULL, args, NULL) : NULL;
}
static void utf8(Obj s, char *out, size_t n) {
    size_t o = 0; int32_t len = s ? AT(s, 0x10, int32_t) : 0;
    for (int32_t i = 0; i < len && o + 4 < n; i++) {
        uint32_t u = ((uint16_t *)((char *)s + 0x14))[i];
        if (u < 0x80) out[o++] = (char)u;
        else if (u < 0x800) { out[o++] = (char)(0xC0 | u >> 6); out[o++] = (char)(0x80 | (u & 0x3F)); }
        else { out[o++] = (char)(0xE0 | u >> 12); out[o++] = (char)(0x80 | ((u >> 6) & 0x3F)); out[o++] = (char)(0x80 | (u & 0x3F)); }
    }
    out[o] = 0;
}
static void tmp_text(Obj t, char *out, size_t n) {      /* TMP_Text.text (virtual) */
    static void *mi; if (!mi) mi = H->find_method(K("TMPro", "TMP_Text"), "get_text", 0);
    out[0] = 0;
    if (!alive(t) || !mi) return;
    const void *vm = vmethod(t, mi);
    utf8(call(vm ? vm : mi, t, NULL, NULL), out, n);
}
static long num(const char *s) {                        /* the number before any '/', separators skipped; -1 = none */
    long v = 0; int any = 0;
    for (; *s && *s != '/'; s++) if (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); any = 1; }
    return any ? v : -1;
}
static double now_ms(void) { struct timespec ts; timespec_get(&ts, TIME_UTC); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }

/* ---------------------------------------------------------------- the picked building */
static uint8_t picked[16]; static int has_pick;
static int32_t f_guid = -1, f_key = -1;
static Obj comps_dict(void) {
    void *kec = K("Refactor.Main", "EntityComponent"), *fi = field(kec, "_instance"); Obj ec = NULL;
    if (fi) fs_get(fi, &ec);
    static int32_t oc = -1; if (oc < 0) oc = fo(kec, "_components");
    return ec ? AT(ec, oc, Obj) : NULL;
}
/* Dictionary<IEntity, ComponentList>: _entries[i] = {hashCode, next, key, value}, 24 bytes; array data at +0x20 */
static Obj entity_cl(Obj *cl_out) {
    Obj d = comps_dict(); if (!d || !has_pick) return NULL;
    void *kd = ocls(d), *kb = K("Refactor", "BuildingEntity"); int32_t oe = fo(kd, "_entries"), on = fo(kd, "_count");
    Obj e = AT(d, oe, Obj); int n = AT(d, on, int32_t);
    for (int i = 0; e && i < n; i++) {
        char *x = (char *)e + 0x20 + 24 * i; Obj ent = AT(x, 8, Obj);
        if (AT(x, 0, int32_t) < 0 || !ent || ocls(ent) != kb || memcmp((char *)ent + f_guid, picked, 16)) continue;
        if (cl_out) *cl_out = AT(x, 16, Obj);
        return ent;
    }
    return NULL;
}
static Obj reader(void) {
    Obj cl = NULL, rd = NULL; if (!entity_cl(&cl) || !cl) return NULL;
    int32_t t = enum_v("Refactor.Component", "ComponentType", "Status"); void *a[2] = { &t, &rd };
    call(H->find_method(K("Refactor.Main", "ComponentList"), "TryGetReader", 2), cl, a, NULL);
    return rd;
}
static float stat(Obj rd, const char *name) {
    int32_t t = enum_v("Refactor", "StatType", name); void *a[1] = { &t };
    Obj r = rd ? call(H->find_method(K("Refactor", "StatusReader"), "GetValue", 1), rd, a, NULL) : NULL;
    return r ? *(float *)unbox(r) : -1;
}
static void do_pick(const char *key) {
    Obj d = comps_dict(); if (!d) { LOG("pick: no EntityComponent"); return; }
    void *kd = ocls(d), *kb = K("Refactor", "BuildingEntity"); int32_t oe = fo(kd, "_entries"), on = fo(kd, "_count");
    Obj e = AT(d, oe, Obj); int n = AT(d, on, int32_t);
    for (int i = 0; e && i < n; i++) {
        char *x = (char *)e + 0x20 + 24 * i; Obj ent = AT(x, 8, Obj); char k[96];
        if (AT(x, 0, int32_t) < 0 || !ent || ocls(ent) != kb) continue;
        utf8(AT(ent, f_key, Obj), k, sizeof k);
        if (strcmp(k, key)) continue;
        memcpy(picked, (char *)ent + f_guid, 16); has_pick = 1;
        Obj rd = reader();
        LOG("pick: %s %02x%02x%02x%02x dur=%.1f max=%.1f", k, picked[3], picked[2], picked[1], picked[0], stat(rd, "Durability"), stat(rd, "MaxDurability"));
        return;
    }
    LOG("pick: none %s", key);
}
static Obj select_container(void) {                     /* MainFlow._eventHandler.<PlayerControlEventHandler>k__BackingField._selectReader */
    void *kmf = K("Refactor.Main", "MainFlow"), *keh = K("Refactor.Main.Event", "EventHandler"), *kpc = K("Refactor.Main.Event", "PlayerControlEventHandler");
    Obj mf = find_obj(kmf), eh = mf ? AT(mf, fo(kmf, "_eventHandler"), Obj) : NULL, pc = eh ? AT(eh, fo(keh, "<PlayerControlEventHandler>k__BackingField"), Obj) : NULL;
    return pc ? AT(pc, fo(kpc, "_selectReader"), Obj) : NULL;
}
static void do_select(void) {
    Obj ent = entity_cl(NULL), sc = select_container();
    void *ks = K("Refactor.Main.Processor", "SelectDataContainer"), *sel1 = NULL;
    void *(*methods)(void *, void **) = H->il2cpp_fn("il2cpp_class_get_methods"); const char *(*mname)(void *) = H->il2cpp_fn("il2cpp_method_get_name");
    uint32_t (*pc)(void *) = H->il2cpp_fn("il2cpp_method_get_param_count");
    void *it = NULL, *m;
    while ((m = methods(ks, &it))) if (!strcmp(mname(m), "Select") && pc(m) == 1 && strstr(tname(mparam(m, 0)), "IEntity") && !strstr(tname(mparam(m, 0)), "List")) sel1 = m;
    if (!ent || !sc || ocls(sc) != ks || !sel1) { LOG("select: not possible (entity %p container %p Select %p)", ent, sc, sel1); return; }
    int threw = 0; call(H->find_method(ks, "ClearAll", 0), sc, NULL, &threw);
    AT(sc, fo(ks, "_selectGroup"), int32_t) = enum_v("Refactor.Main.Processor", "SelectGroup", "Building");
    void *a[1] = { ent }; int t2 = 0; call(sel1, sc, a, &t2);
    LOG("select: %s", threw || t2 ? "THREW" : "done");
}

/* ---------------------------------------------------------------- what the UI shows */
typedef struct { int panel, insp, type, sel; char cur[64], max[64], icur[64], imax[64]; float fill, c, m; } Shown;
static void shown(Shown *s) {
    memset(s, 0, sizeof *s); s->fill = -1; s->type = -1;
    void *kv = K("Refactor.UI", "EntitySummaryUIView"), *kbs = K("Refactor.UI", "EntitySummaryBuildingStatusUI");
    Obj v = find_obj(kv);
    if (active(v)) {
        Obj bp = AT(v, fo(kv, "_buildingPanel"), Obj), bs = AT(v, fo(kv, "_buildingStatusUI"), Obj);
        s->panel = go_self(bp) && active(bs);
        if (s->panel) {
            tmp_text(AT(bs, fo(kbs, "_txtCurrentDurability"), Obj), s->cur, sizeof s->cur);
            tmp_text(AT(bs, fo(kbs, "_txtMaxDurability"), Obj), s->max, sizeof s->max);
            static void *mf; if (!mf) mf = H->find_method(K("UnityEngine.UI", "Image"), "get_fillAmount", 0);
            Obj img = AT(bs, fo(kbs, "_imgTotalDurability"), Obj), r = alive(img) ? call(mf, img, NULL, NULL) : NULL;
            if (r) s->fill = *(float *)unbox(r);
        }
    }
    void *kiv = K("Refactor.UI", "EntityInspectionView"), *kst = K("Refactor.UI", "SubUI_EntityInspectionStatus"), *kel = K("Refactor.UI", "FinalStatElementUI");
    Obj iv = find_obj(kiv), st = active(iv) ? AT(iv, fo(kiv, "_statusUI"), Obj) : NULL;
    if (active(st)) {
        Obj d = AT(st, fo(kst, "_buildingElements"), Obj);
        int32_t dur = enum_v("Refactor", "StatType", "Durability"), mx = enum_v("Refactor", "StatType", "MaxDurability");
        if (d) {
            void *kd = ocls(d); Obj e = AT(d, fo(kd, "_entries"), Obj); int n = AT(d, fo(kd, "_count"), int32_t);
            for (int i = 0; e && i < n; i++) {
                char *x = (char *)e + 0x20 + 24 * i; int k = AT(x, 8, int32_t); Obj el = AT(x, 16, Obj);
                if (AT(x, 0, int32_t) < 0 || !active(el) || (k != dur && k != mx)) continue;
                tmp_text(AT(el, fo(kel, "_txtAmount"), Obj), k == dur ? s->icur : s->imax, 64);
                s->insp = 1;
            }
        }
    }
    Obj rd = reader(); s->c = stat(rd, "Durability"); s->m = stat(rd, "MaxDurability");
}
static void do_state(void) {
    Shown s; shown(&s);
    LOG("state: panel=%d cur='%s' max='%s' fill=%.4f real=%.1f/%.1f insp=%d icur='%s' imax='%s'", s.panel, s.cur, s.max, s.fill, s.c, s.m, s.insp, s.icur, s.imax);
}
static void do_add(float v) {
    Obj cl = NULL; if (!entity_cl(&cl) || !cl) { LOG("add: no picked building"); return; }
    void *kcl = K("Refactor.Main", "ComponentList");
    int32_t t = enum_v("Refactor.Component", "ComponentType", "Status"); Obj comp = NULL; void *a[2] = { &t, &comp };
    call(H->find_method(kcl, "TryGetComponent", 2), cl, a, NULL);
    int32_t dur = enum_v("Refactor", "StatType", "Durability"); void *b[2] = { &dur, &v }; int threw = 0;
    if (comp) call(H->find_method(ocls(comp), "AddValue", 2), comp, b, &threw);
    LOG("add: %.1f %s -> dur=%.1f", v, !comp ? "NO COMPONENT" : threw ? "THREW" : "done", stat(reader(), "Durability"));
}
static void do_inspect(void) {
    void *kv = K("Refactor.UI", "EntitySummaryUIView"); Obj v = find_obj(kv);
    Obj arr = v ? AT(v, fo(kv, "_currentToggleTypes"), Obj) : NULL; int32_t want = enum_v("Refactor.UI", "StaticUIType", "EntityInspect"), idx = -1;
    for (int i = 0; arr && i < (int)AT(arr, 0x18, uintptr_t); i++) if (((int32_t *)((char *)arr + 0x20))[i] == want) idx = i;
    if (idx < 0) { LOG("inspect: no Inspect toggle (view %p)", v); return; }
    uint8_t on = 1; void *a[2] = { &on, &idx }; int threw = 0;
    call(H->find_method(kv, "OnToggleClicked", 2), v, a, &threw);
    LOG("inspect: toggle %d %s", idx, threw ? "THREW" : "clicked");
}

/* ---------------------------------------------------------------- watch after add: per frame, first match time */
static int watching; static double w0; static int w_panel, w_insp;
static int same(const char *txt, float real) { return num(txt) == (long)real; }
static void watch(void) {
    double t = now_ms() - w0;
    Shown s; shown(&s);
    if (!w_panel && s.panel && same(s.cur, s.c) && (s.m <= 0 || (s.fill >= 0 && s.fill - s.c / s.m < 0.002f && s.c / s.m - s.fill < 0.002f))) {
        w_panel = 1; LOG("watch: panel matched after %.0f ms (cur '%s' fill %.4f, real %.1f/%.1f)", t, s.cur, s.fill, s.c, s.m); }
    if (!w_insp && s.insp && same(s.icur, s.c)) { w_insp = 1; LOG("watch: window matched after %.0f ms (icur '%s', real %.1f)", t, s.icur, s.c); }
    if (t > 3000) {
        if (!w_panel) LOG("watch: panel NOT matched after 3000 ms (panel %d cur '%s' fill %.4f, real %.1f/%.1f)", s.panel, s.cur, s.fill, s.c, s.m);
        if (!w_insp) LOG("watch: window NOT matched after 3000 ms (insp %d icur '%s', real %.1f)", s.insp, s.icur, s.c);
        LOG("watch: done"); watching = 0;
    }
}

static void (*o_tick)(Obj, const void *);
static unsigned frames;
static void my_tick(Obj self, const void *mi) {
    o_tick(self, mi);
    if (watching) watch();
    if (++frames % 2) return;
    FILE *f = fopen(req_path, "r");
    if (!f) return;
    char line[256] = ""; if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f);
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0]) return;
    if ((f = fopen(req_path, "w"))) fclose(f);        /* emptied = done */
    char cmd[32] = "", arg[128] = ""; sscanf(line, "%31s %127s", cmd, arg);
    if (!strcmp(cmd, "pick")) do_pick(arg);
    else if (!strcmp(cmd, "select")) do_select();
    else if (!strcmp(cmd, "state")) do_state();
    else if (!strcmp(cmd, "inspect")) do_inspect();
    else if (!strcmp(cmd, "add")) { do_add((float)atof(arg)); watching = 1; w0 = now_ms(); w_panel = w_insp = 0; }
    else LOG("unknown request '%s'", line);
}
DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    const char *d = getenv("DSRIG_DIR");
    if (!d) { LOG("no DSRIG_DIR (test copies only)"); return 1; }
    snprintf(req_path, sizeof req_path, "%s/hp_probe_req.txt", d);
#define FN(v, n) *(void **)&v = H->il2cpp_fn(n)
    FN(rt_invoke, "il2cpp_runtime_invoke"); FN(ocls, "il2cpp_object_get_class"); FN(parent, "il2cpp_class_get_parent");
    FN(field, "il2cpp_class_get_field_from_name"); FN(ctype, "il2cpp_class_get_type"); FN(tobj, "il2cpp_type_get_object");
    FN(foffset, "il2cpp_field_get_offset"); FN(fs_get, "il2cpp_field_static_get_value"); FN(vmethod, "il2cpp_object_get_virtual_method");
    FN(unbox, "il2cpp_object_unbox"); FN(mparam, "il2cpp_method_get_param"); FN(tname, "il2cpp_type_get_name");
#undef FN
    if (!rt_invoke || !ocls || !parent || !field || !ctype || !tobj || !foffset || !fs_get || !vmethod || !unbox || !mparam || !tname) return 2;
    void *kb = K("Refactor", "BuildingEntity"); f_guid = fo(kb, "<Guid>k__BackingField"); f_key = fo(kb, "<Key>k__BackingField");
    if (H->hook_method(ME, "UnityEngine.EventSystems", "StandaloneInputModule", "UpdateModule", 0, (void *)my_tick, (void **)&o_tick)) return 3;
    LOG("ready (%s)", req_path);
    return 0;
}
