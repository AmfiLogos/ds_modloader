// title-input-probe: TEST-ONLY module (never shipped), driven by loader/test/soft_input_test.py.
// Counts the stock title actions (TitleMenuView.OnSettingsPressed and friends; counted, not run) and clicks title buttons with a fake mouse
// in UnityEngine.Input (never the real cursor). DSL_TITLEPROBE=<dir>: <dir>/req.json {"seq":n,"cmd":"state"|"click",
// "target":"<title button label>"} -> <dir>/resp.json {"seq":n,"rc":..,"presses":[..],"blocked":0|1}.
// A hidden test copy never has the app focus and UGUI drops every event without it, so the probe tells the EventSystem
// it has focus, except while the loader takes it away on purpose (dsl_soft_input_blocked: a soft restart runs).
#include "dsl_sdk.h"
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

DSL_PLUGIN();
typedef void *Obj;
typedef struct { float x, y, z; } V3;
static const DSLHost *H; static DSLPlugin *ME;
static char tdir[1024];
static int (*t_rect)(DSLObj, float *); static DSLObj (*t_title)(const char *, char *, size_t); static int (*ld_blocked)(void);
static int (*frame_now)(const void *);
static int fnow(void) { return frame_now ? frame_now(NULL) : 0; }

#define NP 4
static const char *pn[NP] = { "OnSettingsPressed", "OnCreditsPressed", "OnBugReportPressed", "OnLoadCampaignPressed" };
static void (*o_p[NP])(Obj, const void *); static int presses[NP];
/* counted, not run: the title stays as it is for the next click */
#define PH(i) static void h_p##i(Obj s, const void *m) { (void)s; (void)m; presses[i]++; H->log(ME, "probe: %s (frame %d)", pn[i], fnow()); }
PH(0) PH(1) PH(2) PH(3)
static void (*hp[NP])(Obj, const void *) = { h_p0, h_p1, h_p2, h_p3 };

static int fk_on, fk_down = -1, fk_until; static float fk_x, fk_y;
static bool (*o_mb)(int, const void *), (*o_mbd)(int, const void *), (*o_mbu)(int, const void *); static V3 (*o_mpos)(const void *);
static int fk_live(void) { return fk_on && fnow() <= fk_until; }
static bool hk_mb(int b, const void *mi) { return fk_live() ? (b == 0 && fnow() == fk_down) : o_mb(b, mi); }
static bool hk_mbd(int b, const void *mi) { return fk_live() ? (b == 0 && fnow() == fk_down) : o_mbd(b, mi); }
static bool hk_mbu(int b, const void *mi) { return fk_live() ? (b == 0 && fnow() == fk_down + 1) : o_mbu(b, mi); }
static V3 hk_mpos(const void *mi) { return fk_live() ? (V3){ fk_x, fk_y, 0 } : o_mpos(mi); }

static Obj (*es_cur)(const void *); static void (*es_focus)(Obj, bool, const void *);
static int blocked(void) { return ld_blocked ? ld_blocked() : -1; }
static void focus_on(void) {
    static int seen_block;
    Obj es = es_cur ? es_cur(NULL) : NULL; if (!es || !es_focus) return;
    if (blocked() == 1) {                                         /* the loader took the focus away on purpose: one */
        if (!seen_block++) es_focus(es, true, NULL);              /* focus-gained event (the OS can send one), then hands off */
        return;
    }
    seen_block = 0; es_focus(es, true, NULL);
}
static int jint(const char *j, const char *k, int d) { char p[64]; snprintf(p, sizeof p, "\"%s\":", k); const char *s = strstr(j, p); return s ? atoi(s + strlen(p)) : d; }
static void jstr(const char *j, const char *k, char *out, size_t cap) {
    char p[64]; snprintf(p, sizeof p, "\"%s\": \"", k); const char *s = strstr(j, p); out[0] = 0;
    if (!s) { snprintf(p, sizeof p, "\"%s\":\"", k); s = strstr(j, p); } if (!s) return;
    s += strlen(p); size_t n = 0; while (*s && *s != '"' && n + 1 < cap) out[n++] = *s++; out[n] = 0;
}
static void tick(CFRunLoopTimerRef tm, void *info) {
    (void)tm; (void)info;
    focus_on();
    char p[1200], d[1300]; snprintf(p, sizeof p, "%s/req.json", tdir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char j[2048]; size_t n = fread(j, 1, sizeof j - 1, f); fclose(f); j[n] = 0;
    snprintf(d, sizeof d, "%s/req.done.json", tdir); rename(p, d);
    int rc = 0; char cmd[32], tg[96]; jstr(j, "cmd", cmd, sizeof cmd);
    if (!strcmp(cmd, "click")) {
        jstr(j, "target", tg, sizeof tg); float r[4] = { 0 };
        DSLObj o = t_title ? t_title(tg, NULL, 0) : NULL;
        rc = o && t_rect ? t_rect(o, r) : -1;
        if (rc >= 0) { fk_on = 1; fk_x = (r[0] + r[2]) / 2; fk_y = (r[1] + r[3]) / 2; fk_down = fnow() + 5; fk_until = fk_down + 30; }
    }
    char q[1200]; snprintf(p, sizeof p, "%s/resp.json.tmp", tdir); snprintf(q, sizeof q, "%s/resp.json", tdir);
    f = fopen(p, "w"); if (!f) return;
    fprintf(f, "{\"seq\":%d,\"rc\":%d,\"frame\":%d,\"presses\":[%d,%d,%d,%d],\"blocked\":%d}\n", jint(j, "seq", 0), rc, fnow(),
            presses[0], presses[1], presses[2], presses[3], blocked());
    fclose(f); rename(p, q);
}
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me;
    const char *td = getenv("DSL_TITLEPROBE"); if (!td || !*td) { h->log(me, "probe: DSL_TITLEPROBE not set: off"); return 0; }
    snprintf(tdir, sizeof tdir, "%s", td);
    frame_now = h->find_method_address("UnityEngine", "Time", "get_frameCount", 0);
    es_cur = h->find_method_address("UnityEngine.EventSystems", "EventSystem", "get_current", 0);
    es_focus = h->find_method_address("UnityEngine.EventSystems", "EventSystem", "OnApplicationFocus", 1);
    *(void **)&t_rect = dlsym(RTLD_DEFAULT, "dsl_ui_test_rect"); *(void **)&t_title = dlsym(RTLD_DEFAULT, "dsl_ui_test_title");
    *(void **)&ld_blocked = dlsym(RTLD_DEFAULT, "dsl_soft_input_blocked");
    int bad = 0;
    for (int i = 0; i < NP; i++) bad |= h->hook_method(me, "Refactor.UI", "TitleMenuView", pn[i], 0, (void *)hp[i], (void **)&o_p[i]);
    bad |= h->hook_method(me, "UnityEngine", "Input", "GetMouseButton", 1, (void *)hk_mb, (void **)&o_mb);
    bad |= h->hook_method(me, "UnityEngine", "Input", "GetMouseButtonDown", 1, (void *)hk_mbd, (void **)&o_mbd);
    bad |= h->hook_method(me, "UnityEngine", "Input", "GetMouseButtonUp", 1, (void *)hk_mbu, (void **)&o_mbu);
    bad |= h->hook_method(me, "UnityEngine", "Input", "get_mousePosition", 0, (void *)hk_mpos, (void **)&o_mpos);
    h->log(me, "probe: hooks %s, exports %p %p, loader input flag %s", bad ? "FAILED" : "ok", (void *)t_rect, (void *)t_title, ld_blocked ? "found" : "missing (old loader)");
    CFRunLoopTimerRef tm = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.5, 0.05, 0, 0, tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetMain(), tm, kCFRunLoopCommonModes);
    return 0;
}
