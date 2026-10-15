// uikit-demo: TEST-ONLY module for the dsl_ui kit (loader/src/dsl_ui.c). Never shipped.
// Adds the title button "UI Kit Demo"; its click opens one panel with every DSLUi widget.
// With DSL_UIKIT_TEST=<dir> (loader/test/uikit_test.py) it also runs a small test driver on the main run loop:
//   <dir>/req.json  {"seq":n,"cmd":...}  ->  <dir>/resp.json  {"seq":n,...}
//   cmd state | click {target} | hover {target} | key {code} | type {text} | wheel {target, dy} | capture {name}
//       | api {op, ...}   (toggle_set, value_set, field_set, set_text, place, set_active, set_enabled, destroy,
//                          clipboard_set, clipboard_get, tooltip_off, notice1, open, hover_off)
// Mouse: a fake mouse fed into Unity's Input (see fk_*), keys: real NSEvents given to the game window. The window stays
// on the far-off test stage (DSL_SOFT_STAGE) and the app is never activated.
// Without the multiplayer module, GUIUtility.systemCopyBuffer get/set are hooked to <dir>/clipboard.txt (as MP does
// in its test mode), so a test never touches the owner's clipboard.
#include "dsl_sdk.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>

DSL_PLUGIN();
typedef void *Obj;
static const DSLHost *H; static DSLPlugin *ME; static const DSLUi *U;
static char tdir[1024], png_path[1200];

/* test exports of dsl_ui.c (dlsym) */
static int (*t_rect)(DSLObj, float *); static size_t (*t_label)(DSLObj, char *, size_t);
static DSLObj (*t_title)(const char *, char *, size_t); static DSLObj (*t_notice_btn)(int);
static int (*t_type)(DSLObj, const char *); static void (*t_hover)(DSLObj); static int (*t_tip)(char *, size_t);

/* ------------------------------------------------------------------ the demo panel */
static DSLObj panel, t_head, t_status, b_press, b_disabled, tg, vr, fd, sc, rows[20], img, b_notice, b_notice1, b_close;
static int n_click, n_disabled, n_toggle, n_value, n_submit, n_ok, n_cancel, n_title, built;
static const char *const speeds[] = { "Slow", "Normal", "Fast" };
static void status(void) {
    char s[256]; snprintf(s, sizeof s, "clicks %d \xC2\xB7 toggle %d \xC2\xB7 value %d \xC2\xB7 submit %d \xC2\xB7 notice ok %d cancel %d",
                          n_click, n_toggle, n_value, n_submit, n_ok, n_cancel);
    if (t_status) U->set_text(t_status, s);
}
static void on_press(void *ud, DSLObj w) { (void)ud; (void)w; n_click++; status(); }
static void on_disabled(void *ud, DSLObj w) { (void)ud; (void)w; n_disabled++; }
static void on_toggle(void *ud, DSLObj w) { (void)ud; n_toggle++; H->log(ME, "toggle -> %d", U->toggle_get(w)); status(); }
static void on_value(void *ud, DSLObj w) { (void)ud; n_value++; H->log(ME, "value -> %d", U->value_get(w)); status(); }
static void on_submit(void *ud, DSLObj w) { (void)ud; char b[128]; U->field_get(w, b, sizeof b); n_submit++; H->log(ME, "submit \"%s\"", b); status(); }
static void on_ok(void *ud, DSLObj w) { (void)ud; (void)w; n_ok++; H->log(ME, "notice OK"); status(); }
static void on_cancel(void *ud, DSLObj w) { (void)ud; (void)w; n_cancel++; H->log(ME, "notice Cancel"); status(); }
static void on_notice(void *ud, DSLObj w) { (void)ud; (void)w; U->notice("<size=130%>UI Kit Demo</size>\nThis is the game's own notice box.", "Yes", "No", on_ok, on_cancel, NULL); }
static void on_notice1(void *ud, DSLObj w) { (void)ud; (void)w; U->notice("One button only.", "Got it", NULL, on_ok, NULL, NULL); }
static void close_panel(void) { if (panel) U->destroy(panel); panel = NULL; built = 0; }
static void on_close(void *ud, DSLObj w) { (void)ud; (void)w; close_panel(); }
static void build(void) {
    if (built) return;
    panel = U->panel(NULL, 40, 30, 560, 640, 0);
    if (!panel) { H->log(ME, "panel: NULL (kit not ready?)"); return; }
    t_head = U->text(panel, 20, 12, 520, 40, "UI Kit Demo", 26, DSL_TXT_CENTER | DSL_TXT_TITLE, 0);
    t_status = U->text(panel, 20, 54, 520, 40, "", 13, DSL_TXT_CENTER | DSL_TXT_WRAP, 0xA69085FF);
    b_press = U->button(panel, 20, 100, 250, 40, "Press me", on_press, NULL);
    b_disabled = U->button(panel, 290, 100, 250, 40, "Disabled", on_disabled, NULL);
    U->set_enabled(b_disabled, 0);
    U->tooltip(b_press, "Press me", "A dsl_ui button. The text you read is the game's own tooltip window.");
    tg = U->toggle(panel, 20, 152, 520, "Sound effects", 0, on_toggle, NULL);
    vr = U->value_row(panel, 20, 198, 520, "Speed", speeds, 3, 1, on_value, NULL);
    fd = U->field(panel, 20, 252, 520, 44, "Type a name", "", on_submit, NULL);
    sc = U->scroll(panel, 20, 308, 330, 180);
    for (int i = 0; i < 20; i++) { char t[32]; snprintf(t, sizeof t, "Row %d", i + 1); rows[i] = U->text(sc, 10, 4 + 28 * i, 300, 24, t, 16, DSL_TXT_LEFT, 0); }
    U->scroll_height(sc, 4 + 28 * 20);
    img = U->image_png(panel, 380, 320, 128, 144, png_path, 1);
    b_notice = U->button(panel, 20, 506, 250, 40, "Notice", on_notice, NULL);
    b_notice1 = U->button(panel, 290, 506, 250, 40, "Notice (one button)", on_notice1, NULL);
    b_close = U->button(panel, 20, 570, 520, 44, "Close", on_close, NULL);
    built = 1; status();
    H->log(ME, "panel built: panel %p text %p button %p toggle %p value %p field %p scroll %p image %p", panel, t_head, b_press, tg, vr, fd, sc, img);
}
static void on_title(void *ud) { (void)ud; n_title++; H->log(ME, "title button clicked"); build(); }

/* ------------------------------------------------------------------ test driver */
static id game_window(void) {                                      /* the biggest window of the app */
    id app = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    id wins = ((id (*)(id, SEL))objc_msgSend)(app, sel_registerName("windows"));
    unsigned long n = ((unsigned long (*)(id, SEL))objc_msgSend)(wins, sel_registerName("count")); id best = nil; double ba = 0;
    for (unsigned long i = 0; i < n; i++) {
        id w = ((id (*)(id, SEL, unsigned long))objc_msgSend)(wins, sel_registerName("objectAtIndex:"), i);
        CGRect f = ((CGRect (*)(id, SEL))objc_msgSend)(w, sel_registerName("frame"));
        if (f.size.width * f.size.height > ba) { ba = f.size.width * f.size.height; best = w; }
    }
    return best;
}
static double uptime(void) { id pi = ((id (*)(Class, SEL))objc_msgSend)(objc_getClass("NSProcessInfo"), sel_registerName("processInfo")); return ((double (*)(id, SEL))objc_msgSend)(pi, sel_registerName("systemUptime")); }
static int via_view = 1;                                          /* NSWindow swallows a first click on a non-key window */
static void send_ev(id w, id ev) {
    if (!w || !ev) return;
    if (!via_view) { ((void (*)(id, SEL, id))objc_msgSend)(w, sel_registerName("sendEvent:"), ev); return; }
    id v = ((id (*)(id, SEL))objc_msgSend)(w, sel_registerName("contentView"));
    unsigned long t = ((unsigned long (*)(id, SEL))objc_msgSend)(ev, sel_registerName("type"));
    const char *sel = t == 1 ? "mouseDown:" : t == 2 ? "mouseUp:" : t == 5 ? "mouseMoved:" : t == 22 ? "scrollWheel:" : t == 10 ? "keyDown:" : t == 11 ? "keyUp:" : NULL;
    if (v && sel && ((BOOL (*)(id, SEL, SEL))objc_msgSend)(v, sel_registerName("respondsToSelector:"), sel_registerName(sel)))
        ((void (*)(id, SEL, id))objc_msgSend)(v, sel_registerName(sel), ev);
    else ((void (*)(id, SEL, id))objc_msgSend)(w, sel_registerName("sendEvent:"), ev);
}
/* fake mouse (test only, as MP's TEAMS_MOUSE_TEST build): Unity reads the pointer from the global cursor, which a test
   must never move, so UnityEngine.Input.GetMouseButton / Down / Up and mousePosition and BaseInput.mouseScrollDelta return
   scripted values while a fake action runs. The stock StandaloneInputModule -> raycast -> OnPointerClick chain decides. */
typedef struct { float x, y, z; } V3; typedef struct { float x, y; } V2;
static int (*frame_now)(const void *);
static int fk_on, fk_down = -1, fk_until, fk_wheel_at = -1; static float fk_x, fk_y, fk_wy;
static bool (*o_mb)(int, const void *), (*o_mbd)(int, const void *), (*o_mbu)(int, const void *);
static V3 (*o_mpos)(const void *); static V2 (*o_wheel)(Obj, const void *);
static int fnow(void) { return frame_now ? frame_now(NULL) : 0; }
static int fk_live(void) { return fk_on && fnow() <= fk_until; }
static bool hk_mb(int b, const void *mi) { return fk_live() ? (fk_down >= 0 && b == 0 && fnow() == fk_down) : o_mb(b, mi); }
static bool hk_mbd(int b, const void *mi) { return fk_live() ? (fk_down >= 0 && b == 0 && fnow() == fk_down) : o_mbd(b, mi); }
static bool hk_mbu(int b, const void *mi) { return fk_live() ? (fk_down >= 0 && b == 0 && fnow() == fk_down + 1) : o_mbu(b, mi); }
static V3 hk_mpos(const void *mi) { return fk_live() ? (V3){ fk_x, fk_y, 0 } : o_mpos(mi); }
static V2 hk_wheel(Obj self, const void *mi) { return fk_live() && fnow() == fk_wheel_at ? (V2){ 0, fk_wy } : o_wheel(self, mi); }
static void fk_at(const float r[4]) { fk_on = 1; fk_x = (r[0] + r[2]) / 2; fk_y = (r[1] + r[3]) / 2; fk_down = -1; fk_wheel_at = -1; }
static int click_rect(const float r[4]) { fk_at(r); fk_down = fnow() + 5; fk_until = fk_down + 30; return 0; }   /* rest 5 frames, press, release */
static int move_rect(const float r[4]) { fk_at(r); fk_until = 0x7fffffff; return 0; }                           /* hover until "mouse_off" */
static int wheel_rect(const float r[4], int dy) { fk_at(r); fk_wheel_at = fnow() + 3; fk_wy = (float)dy; fk_until = fk_wheel_at + 20; return 0; }
static id ns_str(const char *s) { return ((id (*)(Class, SEL, const char *))objc_msgSend)(objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), s); }
static int key_code(int kc) {                                     /* one special key (36 Return, 53 Escape) down + up */
    id w = game_window(); if (!w) return -1;
    long wn = ((long (*)(id, SEL))objc_msgSend)(w, sel_registerName("windowNumber"));
    char one[2] = { kc == 36 ? '\r' : kc == 53 ? 0x1b : 0, 0 };
    for (int up = 0; up < 2; up++) {
        id ev = ((id (*)(Class, SEL, unsigned long, CGPoint, unsigned long, double, long, id, id, id, BOOL, unsigned short))objc_msgSend)(
            objc_getClass("NSEvent"), sel_registerName("keyEventWithType:location:modifierFlags:timestamp:windowNumber:context:characters:charactersIgnoringModifiers:isARepeat:keyCode:"),
            up ? 11 : 10, CGPointZero, 0, uptime(), wn, nil, ns_str(one), ns_str(one), NO, (unsigned short)kc);
        send_ev(w, ev);
    }
    return 0;
}
/* the game's own frame (ScreenCapture.CaptureScreenshotAsTexture, between frames like dsl_soft.c capture()) */
static int capture(const char *name) {
    static Obj (*cap)(const void *); static Obj (*enc)(Obj, const void *); static void (*des)(Obj, const void *);
    if (!cap) { cap = H->find_method_address("UnityEngine", "ScreenCapture", "CaptureScreenshotAsTexture", 0);
                enc = H->find_method_address("UnityEngine", "ImageConversion", "EncodeToPNG", 1); des = H->find_method_address("UnityEngine", "Object", "Destroy", 1); }
    if (!cap || !enc) return -1;
    Obj tex = cap(NULL); if (!tex) return -2;
    Obj png = enc(tex, NULL); if (des) des(tex, NULL);
    if (!png) return -3;
    size_t n = (size_t)*(uintptr_t *)((char *)png + 0x18);
    char p[1400]; snprintf(p, sizeof p, "%s/cap/%s.png", tdir, name);
    FILE *f = fopen(p, "wb"); if (!f) return -4;
    fwrite((char *)png + 0x20, 1, n, f); fclose(f);
    return (int)n;
}
static DSLObj target(const char *t) {
    static const struct { const char *n; DSLObj *o; } map[] = { { "panel", &panel }, { "head", &t_head }, { "status", &t_status },
        { "press", &b_press }, { "disabled", &b_disabled }, { "toggle", &tg }, { "value", &vr }, { "field", &fd }, { "scroll", &sc },
        { "row1", &rows[0] }, { "row20", &rows[19] }, { "image", &img }, { "notice", &b_notice }, { "notice1", &b_notice1 }, { "close", &b_close } };
    for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (!strcmp(t, map[i].n)) return *map[i].o;
    if (!strcmp(t, "notice_ok")) return t_notice_btn(0);
    if (!strcmp(t, "notice_cancel")) return t_notice_btn(1);
    if (!strncmp(t, "title:", 6)) return t_title(t + 6, NULL, 0);
    return NULL;
}
/* sub-parts of composite widgets: value row arrows / field are at fixed places inside the root rect */
static int target_rect(const char *t, float r[4]) {
    char base[64]; snprintf(base, sizeof base, "%s", t); char *dot = strchr(base, '.'); if (dot) *dot = 0;
    DSLObj o = target(base); if (!o) return -1;
    int on = t_rect(o, r); if (on < 0) return -1;
    if (dot && !strcmp(dot + 1, "prev")) { float x0 = r[0] + (r[2] - r[0]) * 0.36f; r[0] = x0; r[2] = x0 + (r[3] - r[1]) * 28 / 44.f; }
    if (dot && !strcmp(dot + 1, "next")) { float s = (r[3] - r[1]) / 44.f; r[2] -= 12 * s; r[0] = r[2] - 28 * s; }
    return on;
}
static char *jstr(const char *j, const char *k, char *out, size_t cap) {   /* "k":"v" (no escapes needed by the test) */
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", k); const char *p = strstr(j, pat); out[0] = 0; if (!p) return NULL;
    p += strlen(pat); while (*p == ' ') p++; if (*p != '"') return NULL; p++;
    size_t n = 0; while (*p && *p != '"' && n + 1 < cap) { if (*p == '\\' && p[1]) p++; out[n++] = *p++; } out[n] = 0; return out;
}
static double jnum(const char *j, const char *k, double d) { char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", k); const char *p = strstr(j, pat); return p ? atof(p + strlen(pat)) : d; }
static void jesc(char *o, size_t cap, const char *s) { size_t n = 0; for (; *s && n + 3 < cap; s++) { if (*s == '"' || *s == '\\') o[n++] = '\\'; if ((unsigned char)*s < 0x20) continue; o[n++] = *s; } o[n] = 0; }
static void state(char *out, size_t cap) {
    size_t o = 0; char col[1200] = "", e[1400];
    t_title(NULL, col, sizeof col); jesc(e, sizeof e, col);
    o += (size_t)snprintf(out + o, cap - o, "\"kit\":%d,\"built\":%d,\"title_column\":\"%s\",\"n\":{\"title\":%d,\"click\":%d,\"disabled\":%d,\"toggle\":%d,\"value\":%d,\"submit\":%d,\"ok\":%d,\"cancel\":%d}",
                          U != NULL, built, e, n_title, n_click, n_disabled, n_toggle, n_value, n_submit, n_ok, n_cancel);
    if (built) {
        char fb[256] = "", fe[300]; U->field_get(fd, fb, sizeof fb); jesc(fe, sizeof fe, fb);
        o += (size_t)snprintf(out + o, cap - o, ",\"toggle\":%d,\"value\":%d,\"field\":\"%s\"", U->toggle_get(tg), U->value_get(vr), fe);
    }
    static const char *const names[] = { "panel", "head", "status", "press", "disabled", "toggle", "value", "field", "scroll", "row1", "row20", "image", "notice", "notice1", "close", "notice_ok", "notice_cancel" };
    o += (size_t)snprintf(out + o, cap - o, ",\"w\":{");
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        DSLObj w = target(names[i]); float r[4] = { 0 }; int on = w ? t_rect(w, r) : -1; char l[200] = "", le[260];
        if (w && on >= 0) t_label(w, l, sizeof l); jesc(le, sizeof le, l);
        o += (size_t)snprintf(out + o, cap - o, "%s\"%s\":{\"on\":%d,\"r\":[%.0f,%.0f,%.0f,%.0f],\"text\":\"%s\"}", i ? "," : "", names[i], on, r[0], r[1], r[2], r[3], le);
    }
    { id w = game_window(); CGRect f = w ? ((CGRect (*)(id, SEL))objc_msgSend)(w, sel_registerName("frame")) : CGRectZero;
      double sc = w ? ((double (*)(id, SEL))objc_msgSend)(w, sel_registerName("backingScaleFactor")) : 0;
      o += (size_t)snprintf(out + o, cap - o, ",\"win\":[%.0f,%.0f,%.0f,%.0f,%.1f]", f.origin.x, f.origin.y, f.size.width, f.size.height, sc); }
    { typedef struct { float x, y, z; } V3; static V3 (*mp)(const void *); if (!mp) mp = H->find_method_address("UnityEngine", "Input", "get_mousePosition", 0);
      V3 m = mp ? mp(NULL) : (V3){ 0, 0, 0 }; o += (size_t)snprintf(out + o, cap - o, ",\"mouse\":[%.0f,%.0f]", m.x, m.y); }
    {   /* what the stock input module has under the pointer (PointerInputModule.GetLastPointerEventData(-1).pointerEnter) */
        static Obj (*cur)(const void *), (*mod)(Obj, const void *), (*last)(Obj, int, const void *), (*enter)(Obj, const void *), (*nm)(Obj, const void *), (*press)(Obj, const void *);
        if (!cur) { cur = H->find_method_address("UnityEngine.EventSystems", "EventSystem", "get_current", 0);
                    mod = H->find_method_address("UnityEngine.EventSystems", "EventSystem", "get_currentInputModule", 0);
                    last = H->find_method_address("UnityEngine.EventSystems", "PointerInputModule", "GetLastPointerEventData", 1);
                    enter = H->find_method_address("UnityEngine.EventSystems", "PointerEventData", "get_pointerEnter", 0);
                    press = H->find_method_address("UnityEngine.EventSystems", "PointerEventData", "get_pointerPress", 0);
                    nm = H->find_method_address("UnityEngine", "Object", "get_name", 0); }
        Obj es = cur ? cur(NULL) : NULL, m = es && mod ? mod(es, NULL) : NULL, pd = m && last ? last(m, -1, NULL) : NULL;
        Obj g = pd && enter ? enter(pd, NULL) : NULL, gp = pd && press ? press(pd, NULL) : NULL; char n1[96] = "", n2[96] = "";
        Obj s1 = g && nm ? nm(g, NULL) : NULL, s2 = gp && nm ? nm(gp, NULL) : NULL;
        for (int k = 0; k < 2; k++) { Obj sx = k ? s2 : s1; char *d = k ? n2 : n1; if (!sx) continue; int32_t len = *(int32_t *)((char *)sx + 0x10); const uint16_t *c = (const uint16_t *)((char *)sx + 0x14);
            int i = 0; for (; i < len && i < 90; i++) d[i] = c[i] < 0x80 && c[i] != '"' && c[i] != '\\' ? (char)c[i] : '?'; d[i] = 0; }
        o += (size_t)snprintf(out + o, cap - o, ",\"ptr_under\":\"%s\",\"ptr_press\":\"%s\"", n1, n2);
    }
    char tt[200] = "", te[260]; int ts = t_tip(tt, sizeof tt); jesc(te, sizeof te, tt);
    o += (size_t)snprintf(out + o, cap - o, "},\"tip\":{\"shown\":%d,\"title\":\"%s\"}", ts, te);
}
static void handle(const char *j) {
    char cmd[32], t[64], s[512], out[16384]; int seq = (int)jnum(j, "seq", 0), rc = 0; size_t o = 0;
    jstr(j, "cmd", cmd, sizeof cmd); jstr(j, "target", t, sizeof t);
    out[0] = 0;
    if (!U) rc = -100;
    else if (!strcmp(cmd, "state")) { /* below */ }
    else if (!strcmp(cmd, "click") || !strcmp(cmd, "hover") || !strcmp(cmd, "wheel")) {
        float r[4]; int on = target_rect(t, r);
        if (on <= 0) rc = -1;
        else rc = !strcmp(cmd, "click") ? click_rect(r) : !strcmp(cmd, "hover") ? move_rect(r) : wheel_rect(r, (int)jnum(j, "dy", -3));
        o += (size_t)snprintf(out + o, sizeof out - o, "\"at\":[%.0f,%.0f,%.0f,%.0f],", r[0], r[1], r[2], r[3]);
    }
    else if (!strcmp(cmd, "via")) via_view = (int)jnum(j, "view", 1);
    else if (!strcmp(cmd, "key")) rc = key_code((int)jnum(j, "code", 36));
    else if (!strcmp(cmd, "type")) { jstr(j, "text", s, sizeof s); rc = t_type(fd, s); }
    else if (!strcmp(cmd, "capture")) { jstr(j, "name", s, sizeof s); rc = capture(s); }
    else if (!strcmp(cmd, "api")) {
        char op[32]; jstr(j, "op", op, sizeof op); jstr(j, "s", s, sizeof s); DSLObj w = target(t);
        double a = jnum(j, "a", 0), x = jnum(j, "x", 0), y = jnum(j, "y", 0), ww = jnum(j, "w", 0), hh = jnum(j, "h", 0);
        if (!strcmp(op, "open")) build();
        else if (!strcmp(op, "toggle_set")) U->toggle_set(tg, (int)a);
        else if (!strcmp(op, "value_set")) U->value_set(vr, (int)a);
        else if (!strcmp(op, "field_set")) U->field_set(fd, s);
        else if (!strcmp(op, "set_text")) U->set_text(w, s);
        else if (!strcmp(op, "place")) U->place(w, (float)x, (float)y, (float)ww, (float)hh);
        else if (!strcmp(op, "set_active")) U->set_active(w, (int)a);
        else if (!strcmp(op, "set_enabled")) U->set_enabled(w, (int)a);
        else if (!strcmp(op, "destroy")) { U->destroy(w); if (w == panel) { panel = NULL; built = 0; } }
        else if (!strcmp(op, "tooltip_off")) U->tooltip(w, NULL, NULL);
        else if (!strcmp(op, "hover")) t_hover(w);
        else if (!strcmp(op, "hover_off")) t_hover(NULL);
        else if (!strcmp(op, "mouse_off")) fk_on = 0;
        else if (!strcmp(op, "notice1")) on_notice1(NULL, NULL);
        else if (!strcmp(op, "clipboard_set")) U->clipboard_set(s);
        else if (!strcmp(op, "clipboard_get")) { char c[512], ce[600]; size_t n = U->clipboard_get(c, sizeof c); jesc(ce, sizeof ce, c);
                                                 o += (size_t)snprintf(out + o, sizeof out - o, "\"clip\":\"%s\",\"clip_len\":%zu,", ce, n); }
        else rc = -2;
    } else rc = -3;
    char st[14000]; state(st, sizeof st);
    char p[1200], q[1200]; snprintf(p, sizeof p, "%s/resp.json.tmp", tdir); snprintf(q, sizeof q, "%s/resp.json", tdir);
    FILE *f = fopen(p, "w"); if (!f) return;
    fprintf(f, "{\"seq\":%d,\"cmd\":\"%s\",\"rc\":%d,%s%s}\n", seq, cmd, rc, out, st); fclose(f); rename(p, q);
}
/* UGUI's StandaloneInputModule drops every event while the app has no focus (ShouldIgnoreEventsOnNoFocus) and a hidden
   test copy never gets focus: tell the EventSystem it has focus (test only; the input path itself stays stock) */
static void focus_on(void) {
    static Obj (*cur)(const void *); static void (*foc)(Obj, bool, const void *);
    if (!cur) { cur = H->find_method_address("UnityEngine.EventSystems", "EventSystem", "get_current", 0);
                foc = H->find_method_address("UnityEngine.EventSystems", "EventSystem", "OnApplicationFocus", 1); }
    Obj es = cur ? cur(NULL) : NULL; if (es && foc) foc(es, true, NULL);
}
static void tick(CFRunLoopTimerRef tm, void *info) {
    (void)tm; (void)info;
    focus_on();
    char p[1200]; snprintf(p, sizeof p, "%s/req.json", tdir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char j[4096]; size_t n = fread(j, 1, sizeof j - 1, f); fclose(f); j[n] = 0;
    char d[1300]; snprintf(d, sizeof d, "%s/req.done.json", tdir); rename(p, d);
    handle(j);
}

/* test clipboard: a file (only when the multiplayer module, which does the same in its test mode, is not loaded) */
static Obj (*o_cg)(const void *); static void (*o_cs)(Obj, const void *);
static Obj (*str_new)(const char *);
static Obj h_cg(const void *mi) {
    (void)mi; char p[1200], b[1024] = ""; snprintf(p, sizeof p, "%s/clipboard.txt", tdir);
    FILE *f = fopen(p, "r"); if (f) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); }
    return str_new(b);
}
static void h_cs(Obj v, const void *mi) {
    (void)mi; char p[1200]; snprintf(p, sizeof p, "%s/clipboard.txt", tdir);
    FILE *f = fopen(p, "w"); if (!f) return;
    if (v) { int32_t len = *(int32_t *)((char *)v + 0x10); const uint16_t *c = (const uint16_t *)((char *)v + 0x14);   /* UTF-16 -> UTF-8 (BMP) */
             for (int32_t i = 0; i < len; i++) { uint32_t u = c[i];
                 if (u < 0x80) fputc((int)u, f); else if (u < 0x800) { fputc(0xC0 | (int)(u >> 6), f); fputc(0x80 | (int)(u & 63), f); }
                 else { fputc(0xE0 | (int)(u >> 12), f); fputc(0x80 | (int)(u >> 6 & 63), f); fputc(0x80 | (int)(u & 63), f); } } }
    fclose(f);
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me;
    snprintf(png_path, sizeof png_path, "%s/slogel.png", h->module_dir(me));
    const char *td = getenv("DSL_UIKIT_TEST");
    if (td && *td) {
        snprintf(tdir, sizeof tdir, "%s", td);
        if (!h->module_loaded("multiplayer")) {
            str_new = h->il2cpp_fn("il2cpp_string_new");
            int a = h->hook_method(me, "UnityEngine", "GUIUtility", "get_systemCopyBuffer", 0, (void *)h_cg, (void **)&o_cg);
            int b = h->hook_method(me, "UnityEngine", "GUIUtility", "set_systemCopyBuffer", 1, (void *)h_cs, (void **)&o_cs);
            h->log(me, "test clipboard -> %s/clipboard.txt (hooks %d %d)", tdir, a, b);
        } else h->log(me, "test clipboard: the multiplayer module's test mode (DSMP_TEST) owns it");
    }
    /* the kit: host->ui once the lead wires API 3 into DSLHost; until then straight from dsload (dlsym) */
    U = dsl_ui(h);
    if (!U) {
        void (*inst)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "dsl_ui_install");
        const DSLUi *(*kit)(void) = (const DSLUi *(*)(void))dlsym(RTLD_DEFAULT, "dsl_ui_kit");
        if (inst) inst();
        U = kit ? kit() : NULL;
        h->log(me, "kit through dlsym: install %p kit %p -> %p", (void *)inst, (void *)kit, (void *)U);
    }
    if (!U) { h->log(me, "no dsl_ui kit: demo off"); return 0; }
    int r = dsl_title_add_button(h, me, "UI Kit Demo", 900, "Opens a panel with every dsl_ui widget (test module).", on_title, NULL);
    if (r < 0) {
        int (*ta)(const char *, const char *, int, const char *, void (*)(void *), void *) = dlsym(RTLD_DEFAULT, "dsl_ui_title_add");
        r = ta ? ta(h->plugin_id(me), "UI Kit Demo", 900, "Opens a panel with every dsl_ui widget (test module).", on_title, NULL) : -9;
    }
    h->log(me, "title button: %d", r);
    if (tdir[0]) {
        *(void **)&t_rect = dlsym(RTLD_DEFAULT, "dsl_ui_test_rect"); *(void **)&t_label = dlsym(RTLD_DEFAULT, "dsl_ui_test_label");
        *(void **)&t_title = dlsym(RTLD_DEFAULT, "dsl_ui_test_title"); *(void **)&t_notice_btn = dlsym(RTLD_DEFAULT, "dsl_ui_test_notice_button");
        *(void **)&t_type = dlsym(RTLD_DEFAULT, "dsl_ui_test_type"); *(void **)&t_hover = dlsym(RTLD_DEFAULT, "dsl_ui_test_hover");
        *(void **)&t_tip = dlsym(RTLD_DEFAULT, "dsl_ui_test_tip");
        if (!t_rect || !t_label || !t_title || !t_notice_btn || !t_type || !t_hover || !t_tip) { h->log(me, "test exports missing: driver off"); return 0; }
        frame_now = h->find_method_address("UnityEngine", "Time", "get_frameCount", 0);
        int m1 = h->hook_method(me, "UnityEngine", "Input", "GetMouseButton", 1, (void *)hk_mb, (void **)&o_mb);
        int m2 = h->hook_method(me, "UnityEngine", "Input", "GetMouseButtonDown", 1, (void *)hk_mbd, (void **)&o_mbd);
        int m3 = h->hook_method(me, "UnityEngine", "Input", "GetMouseButtonUp", 1, (void *)hk_mbu, (void **)&o_mbu);
        int m4 = h->hook_method(me, "UnityEngine", "Input", "get_mousePosition", 0, (void *)hk_mpos, (void **)&o_mpos);
        int m5 = h->hook_method(me, "UnityEngine.EventSystems", "BaseInput", "get_mouseScrollDelta", 0, (void *)hk_wheel, (void **)&o_wheel);
        h->log(me, "test fake mouse hooks: %d %d %d %d %d", m1, m2, m3, m4, m5);
        char cd[1200]; snprintf(cd, sizeof cd, "%s/cap", tdir); mkdir(cd, 0755);
        CFRunLoopTimerRef tm = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.5, 0.05, 0, 0, tick, NULL);
        CFRunLoopAddTimer(CFRunLoopGetMain(), tm, kCFRunLoopCommonModes);
        h->log(me, "test driver on (%s)", tdir);
    }
    return 0;
}
