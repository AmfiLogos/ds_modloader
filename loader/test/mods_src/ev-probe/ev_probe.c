// ev-probe: TEST-ONLY module (never shipped), read by loader/test/events_test.py (Mac hidden copy) and the Windows VM
// run (ext/nvevents). It listens to every loader event (SDK API 7) and logs one "EVPROBE ..." line per event into
// dsload.log (frames: a count line at each scene change). EVPROBE_LANG=1: at the title, after 120 title frames, it sets
// the game's language static to German (LanguageSetting.currentLanguage = 3) and 120 frames later back (the loader's
// language poll must send DSL_EV_LANG twice). It hooks nothing.
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DSL_PLUGIN();
static const DSLHost *H; static DSLPlugin *ME;
static const char *(*cname)(void *);
static void *(*field_from_name)(void *, const char *); static void (*static_set)(void *, void *); static void (*static_get)(void *, void *);
static void *(*class_from_name)(void *, const char *, const char *); static void **(*domain_assemblies)(void *, size_t *);
static void *(*domain_get)(void); static void *(*assembly_image)(void *);
static unsigned long n_title, n_world, n_any, n_tf, n_load, n_bad;   /* n_load: unfiltered frames while loading; n_bad: a
                                                                          title / world frame sent while loading (wrong) */
static int lang_test, lang_step, lang_old = -1;
static void *lang_field;

static const char *ev_name(int t) {
    static const char *const n[] = { "?", "frame", "scene", "world-loaded", "world-closed", "view-created", "view-shown", "view-hidden", "lang" };
    return t > 0 && t <= DSL_EV_LAST ? n[t] : "?";
}
static void *find_class(const char *ns, const char *name) {
    if (!domain_get || !domain_assemblies || !assembly_image || !class_from_name) return NULL;
    size_t n = 0; void **as = domain_assemblies(domain_get(), &n);
    for (size_t i = 0; i < n; i++) { void *k = class_from_name(assembly_image(as[i]), ns, name); if (k) return k; }
    return NULL;
}
static void on_ev(void *ud, const DSLEvent *e) {
    (void)ud;
    switch (e->type) {
    case DSL_EV_SCENE:
        H->log(ME, "EVPROBE scene %d -> %d frames title=%lu world=%lu any=%lu loading=%lu bad=%lu", e->prev_scene, e->scene, n_title, n_world, n_any, n_load, n_bad); break;
    case DSL_EV_WORLD_LOADED: case DSL_EV_WORLD_CLOSED:
        H->log(ME, "EVPROBE %s %s scene=%d", ev_name(e->type), e->guid ? e->guid : "", e->scene); break;
    case DSL_EV_VIEW_CREATED: case DSL_EV_VIEW_SHOWN: case DSL_EV_VIEW_HIDDEN:
        H->log(ME, "EVPROBE %s %s kind=%s id=%d scene=%d obj=%s shown_now=%d", ev_name(e->type), e->klass && cname ? cname(e->klass) : "?",
               e->view_kind == DSL_VIEW_STATIC ? "static" : e->view_kind == DSL_VIEW_POPUP ? "popup" : "?", e->view_id, e->scene,
               e->view ? "yes" : "no", dsl_view_shown(H, e->klass)); break;
    case DSL_EV_LANG:
        H->log(ME, "EVPROBE lang %d %s", e->lang, e->lang_code ? e->lang_code : "?"); break;
    }
}
static void on_frame(void *ud, const DSLEvent *e) {
    unsigned long *c = ud; (*c)++;
    if (e->loading) { if (c == &n_any) n_load++; else n_bad++; }
    if (c == &n_any && n_any % 300 == 0) H->log(ME, "EVPROBE frames title=%lu world=%lu any=%lu loading=%lu bad=%lu scene=%d", n_title, n_world, n_any, n_load, n_bad, e->scene);
    if (c != &n_title || !lang_test || !lang_field || !static_set) return;
    if (lang_step == 0 && n_title == 120) {
        int32_t v = 0; if (static_get) static_get(lang_field, &v); lang_old = v;
        int32_t de = 3; static_set(lang_field, &de); lang_step = 1;
        H->log(ME, "EVPROBE set-lang 3 (was %d) frame %llu", lang_old, (unsigned long long)e->frame);
    } else if (lang_step == 1 && n_title == 240) {
        int32_t v = lang_old >= 0 ? lang_old : 0; static_set(lang_field, &v); lang_step = 2;
        H->log(ME, "EVPROBE set-lang %d (back) frame %llu", v, (unsigned long long)e->frame);
    }
}
static void on_tf(void *ud, const DSLEvent *e) { (void)ud; (void)e; n_tf++; }

DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me;
    if (!DSL_HAS(h, on_event)) { h->log(me, "EVPROBE no-events (loader API older than 7)"); return 0; }
#define F(v, n) *(void **)&v = h->il2cpp_fn(n)
    F(cname, "il2cpp_class_get_name"); F(field_from_name, "il2cpp_class_get_field_from_name"); F(static_set, "il2cpp_field_static_set_value");
    F(static_get, "il2cpp_field_static_get_value"); F(class_from_name, "il2cpp_class_from_name"); F(domain_assemblies, "il2cpp_domain_get_assemblies");
    F(domain_get, "il2cpp_domain_get"); F(assembly_image, "il2cpp_assembly_get_image");
#undef F
    const char *lt = getenv("EVPROBE_LANG"); lang_test = lt && *lt == '1';
    if (lang_test) { void *k = find_class("Refactor.Setting", "LanguageSetting"); lang_field = k && field_from_name ? field_from_name(k, "currentLanguage") : NULL; }
    int bad = 0;
    for (int t = DSL_EV_SCENE; t <= DSL_EV_LAST; t++) bad |= dsl_on_event(h, me, t, 0, NULL, on_ev, NULL) != 0;
    bad |= dsl_on_event(h, me, DSL_EV_FRAME, DSL_EVF_TITLE, NULL, on_frame, &n_title) != 0;
    bad |= dsl_on_event(h, me, DSL_EV_FRAME, DSL_EVF_WORLD, NULL, on_frame, &n_world) != 0;
    bad |= dsl_on_event(h, me, DSL_EV_FRAME, 0, NULL, on_frame, &n_any) != 0;
    bad |= dsl_on_event(h, me, DSL_EV_FRAME, DSL_EVF_TITLE, NULL, on_tf, NULL) != 0;
    int dup = dsl_on_event(h, me, DSL_EV_FRAME, DSL_EVF_TITLE, NULL, on_tf, NULL);      /* the same twice: refused */
    h->log(me, "EVPROBE subscribed %s; duplicate %s; language test %s", bad ? "FAILED" : "ok", dup == -1 ? "refused" : "ACCEPTED",
           !lang_test ? "off" : lang_field ? "on" : "on, NO FIELD");
    return 0;
}
