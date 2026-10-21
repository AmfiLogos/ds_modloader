// bugrep-probe: TEST-ONLY module (never shipped), driven by loader/test/bugrep_test.py.
// Clicks the game's own Bug Report buttons (title: TitleMenuView._reportBugButton; Esc menu in a world:
// SystemMenuView._reportBugButton) with a fake mouse in UnityEngine.Input (never the real cursor), and counts what the
// stock buttons do: Application.OpenURL (the developers' web form) and TitleMenuView.OpenSavesFolder (Finder). Both are
// counted and NEVER run, so an old loader (no bug report window) cannot open a browser or Finder on the owner's Mac.
// DSL_BUGPROBE=<dir>: <dir>/req.json {"seq":n,"cmd":"state"|"click_title"|"click_esc"} -> <dir>/resp.json
// {"seq":n,"rc":..,"openurl":n,"folder":n,"url":"..."}.
// The EventSystem is told it has focus (a hidden copy never has the app focus; UGUI drops events without it).
#include "dsl_sdk.h"
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DSL_PLUGIN();
typedef void *Obj;
typedef struct { float x, y, z; } V3;
static const DSLHost *H; static DSLPlugin *ME;
static char tdir[1024], last_url[300];
static int n_url, n_folder;
static int (*t_rect)(DSLObj, float *); static int (*frame_now)(const void *);
static int fnow(void) { return frame_now ? frame_now(NULL) : 0; }

static void (*o_url)(Obj, const void *), (*o_folder)(const void *);
static void utf8_of(Obj s, char *out, size_t cap) {                   /* il2cpp string -> ASCII-ish (test log only) */
    out[0] = 0; if (!s) return;
    int n = *(int *)((char *)s + 0x10); const uint16_t *c = (const uint16_t *)((char *)s + 0x14); size_t k = 0;
    for (int i = 0; i < n && k + 1 < cap; i++) out[k++] = c[i] < 128 ? (char)c[i] : '?';
    out[k] = 0;
}
static void h_url(Obj url, const void *mi) { (void)mi; n_url++; utf8_of(url, last_url, sizeof last_url); H->log(ME, "probe: Application.OpenURL(%s) counted, NOT opened", last_url); }
static void h_folder(const void *mi) { (void)mi; n_folder++; H->log(ME, "probe: TitleMenuView.OpenSavesFolder counted, NOT opened"); }

static int fk_on, fk_down = -1, fk_until; static float fk_x, fk_y;
static bool (*o_mb)(int, const void *), (*o_mbd)(int, const void *), (*o_mbu)(int, const void *); static V3 (*o_mpos)(const void *);
static int fk_live(void) { return fk_on && fnow() <= fk_until; }
static bool hk_mb(int b, const void *mi) { return fk_live() ? (b == 0 && fnow() == fk_down) : o_mb(b, mi); }
static bool hk_mbd(int b, const void *mi) { return fk_live() ? (b == 0 && fnow() == fk_down) : o_mbd(b, mi); }
static bool hk_mbu(int b, const void *mi) { return fk_live() ? (b == 0 && fnow() == fk_down + 1) : o_mbu(b, mi); }
static V3 hk_mpos(const void *mi) { return fk_live() ? (V3){ fk_x, fk_y, 0 } : o_mpos(mi); }

static Obj (*es_cur)(const void *); static void (*es_focus)(Obj, bool, const void *);
static Obj (*find_obj)(Obj, const void *); static void *find_obj_mi;
static void *(*class_type)(void *); static Obj (*type_obj)(void *);
static Obj find_view(const char *k) {
    void *kl = H->find_class("Refactor.UI", k);
    return kl && find_obj && class_type && type_obj ? find_obj(type_obj(class_type(kl)), find_obj_mi) : NULL;
}
static int click_field(const char *view, const char *field) {         /* fake click in the middle of view.field's rect */
    Obj v = find_view(view); if (!v) return -2;
    int32_t off = H->field_offset(H->find_class("Refactor.UI", view), field); if (off < 0) return -3;
    Obj b = *(Obj *)((char *)v + off); if (!b) return -4;
    float r[4] = { 0 }; int rc = t_rect ? t_rect((DSLObj)b, r) : -5;
    if (rc < 1) return rc < 0 ? rc : -6;                              /* not shown */
    fk_on = 1; fk_x = (r[0] + r[2]) / 2; fk_y = (r[1] + r[3]) / 2; fk_down = fnow() + 12; fk_until = fk_down + 30;
    H->log(ME, "probe: click %s.%s at %.0f,%.0f (frame %d)", view, field, fk_x, fk_y, fk_down);
    return 0;
}
static int jint(const char *j, const char *k, int d) { char p[64]; snprintf(p, sizeof p, "\"%s\":", k); const char *s = strstr(j, p); return s ? atoi(s + strlen(p)) : d; }
static void jstr(const char *j, const char *k, char *out, size_t cap) {
    char p[64]; snprintf(p, sizeof p, "\"%s\": \"", k); const char *s = strstr(j, p); out[0] = 0;
    if (!s) { snprintf(p, sizeof p, "\"%s\":\"", k); s = strstr(j, p); } if (!s) return;
    s += strlen(p); size_t n = 0; while (*s && *s != '"' && n + 1 < cap) out[n++] = *s++; out[n] = 0;
}
static void tick(CFRunLoopTimerRef tm, void *info) {
    (void)tm; (void)info;
    Obj es = es_cur ? es_cur(NULL) : NULL; if (es && es_focus) es_focus(es, true, NULL);
    char p[1200], d[1300]; snprintf(p, sizeof p, "%s/req.json", tdir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char j[2048]; size_t n = fread(j, 1, sizeof j - 1, f); fclose(f); j[n] = 0;
    snprintf(d, sizeof d, "%s/req.done.json", tdir); rename(p, d);
    char cmd[32]; jstr(j, "cmd", cmd, sizeof cmd); int rc = 0;
    if (!strcmp(cmd, "click_title")) rc = click_field("TitleMenuView", "_reportBugButton");
    else if (!strcmp(cmd, "click_esc")) rc = click_field("SystemMenuView", "_reportBugButton");
    char q[1200]; snprintf(p, sizeof p, "%s/resp.json.tmp", tdir); snprintf(q, sizeof q, "%s/resp.json", tdir);
    f = fopen(p, "w"); if (!f) return;
    fprintf(f, "{\"seq\":%d,\"rc\":%d,\"frame\":%d,\"openurl\":%d,\"folder\":%d,\"url\":\"%s\"}\n", jint(j, "seq", 0), rc, fnow(), n_url, n_folder, last_url);
    fclose(f); rename(p, q);
}
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me;
    const char *td = getenv("DSL_BUGPROBE"); if (!td || !*td) { h->log(me, "probe: DSL_BUGPROBE not set: off"); return 0; }
    snprintf(tdir, sizeof tdir, "%s", td);
    frame_now = h->find_method_address("UnityEngine", "Time", "get_frameCount", 0);
    es_cur = h->find_method_address("UnityEngine.EventSystems", "EventSystem", "get_current", 0);
    es_focus = h->find_method_address("UnityEngine.EventSystems", "EventSystem", "OnApplicationFocus", 1);
    void *ko = h->find_class("UnityEngine", "Object");                 /* FindObjectOfType(System.Type): not the generic <T>(bool) */
    { void *(*gm)(void *, void **) = h->il2cpp_fn("il2cpp_class_get_methods"); const char *(*nm)(void *) = h->il2cpp_fn("il2cpp_method_get_name");
      uint32_t (*pc)(void *) = h->il2cpp_fn("il2cpp_method_get_param_count"); void *(*pa)(void *, uint32_t) = h->il2cpp_fn("il2cpp_method_get_param");
      char *(*tn)(void *) = h->il2cpp_fn("il2cpp_type_get_name"); void *it = NULL, *mi;
      while (ko && gm && nm && pc && pa && tn && (mi = gm(ko, &it))) {
          if (strcmp(nm(mi), "FindObjectOfType") || pc(mi) != 1) continue;
          char *t = tn(pa(mi, 0)); if (t && !strcmp(t, "System.Type")) { find_obj_mi = mi; break; }
      } }
    find_obj = find_obj_mi ? *(void **)find_obj_mi : NULL;
    *(void **)&class_type = h->il2cpp_fn("il2cpp_class_get_type"); *(void **)&type_obj = h->il2cpp_fn("il2cpp_type_get_object");
    *(void **)&t_rect = dlsym(RTLD_DEFAULT, "dsl_ui_test_rect");
    int bad = 0;
    bad |= h->hook_method(me, "UnityEngine", "Application", "OpenURL", 1, (void *)h_url, (void **)&o_url);
    bad |= h->hook_method(me, "Refactor.UI", "TitleMenuView", "OpenSavesFolder", 0, (void *)h_folder, (void **)&o_folder);
    bad |= h->hook_method(me, "UnityEngine", "Input", "GetMouseButton", 1, (void *)hk_mb, (void **)&o_mb);
    bad |= h->hook_method(me, "UnityEngine", "Input", "GetMouseButtonDown", 1, (void *)hk_mbd, (void **)&o_mbd);
    bad |= h->hook_method(me, "UnityEngine", "Input", "GetMouseButtonUp", 1, (void *)hk_mbu, (void **)&o_mbu);
    bad |= h->hook_method(me, "UnityEngine", "Input", "get_mousePosition", 0, (void *)hk_mpos, (void **)&o_mpos);
    h->log(me, "probe: hooks %s, FindObjectOfType %s, rect export %s, il2cpp %s", bad ? "FAILED" : "ok", find_obj ? "ok" : "MISSING",
           t_rect ? "ok" : "MISSING", class_type && type_obj ? "ok" : "MISSING");
    CFRunLoopTimerRef tm = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.5, 0.05, 0, 0, tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetMain(), tm, kCFRunLoopCommonModes);
    return 0;
}
