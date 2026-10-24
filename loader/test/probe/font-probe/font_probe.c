// font-probe: TEST-ONLY module (never shipped) for loader/test/look_test.py (and ds_custommodes home/test/look_test.py). The units test driver of ds_multiplayer
// (ext/units13/driver/units_driver.c: fake mouse in UnityEngine.Input, real key NSEvents, the game's own frame capture,
// targets from Units Laboratory's test registry cm_test_target) plus one command that lists every TMP text's font.
//   "title:<label>" a title column button, "home:<label>" a home page button, "<prefix>:<name>" any registered part.
// DSL_UNITS_TEST=<dir>: <dir>/req.json {"seq":n,"cmd":...} -> <dir>/resp.json {"seq":n,...}
//   cmd state | click {target} | hover {target} | key {code} | type {target,text} | wheel {target,dy} | capture {name}
//       | call {fn, s}  (a Units Laboratory test function "int fn(const char *s, char *out, size_t cap)" by name)
//       | fonts {name, all}  (every TMP text -> <dir>/fonts_<name>.jsonl, see fonts_dump)
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
#include <stdint.h>

DSL_PLUGIN();
typedef void *Obj;
static const DSLHost *H; static DSLPlugin *ME; static const DSLUi *U;
static char tdir[1024];
static int (*t_rect)(DSLObj, float *); static size_t (*t_label)(DSLObj, char *, size_t);
static DSLObj (*t_title)(const char *, char *, size_t); static DSLObj (*t_notice_btn)(int);
static int (*t_type)(DSLObj, const char *); static int (*t_tip)(char *, size_t);
static DSLObj (*c_target)(const char *); static size_t (*c_state)(char *, size_t); static void *cm_img;

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
static char *jstr(const char *j, const char *k, char *out, size_t cap) {   /* "k":"v" (no escapes needed by the test) */
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", k); const char *p = strstr(j, pat); out[0] = 0; if (!p) return NULL;
    p += strlen(pat); while (*p == ' ') p++; if (*p != '"') return NULL; p++;
    size_t n = 0; while (*p && *p != '"' && n + 1 < cap) { if (*p == '\\' && p[1]) p++; out[n++] = *p++; } out[n] = 0; return out;
}
static double jnum(const char *j, const char *k, double d) { char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", k); const char *p = strstr(j, pat); return p ? atof(p + strlen(pat)) : d; }
static void jesc(char *o, size_t cap, const char *s) { size_t n = 0; for (; *s && n + 3 < cap; s++) { if (*s == '"' || *s == '\\') o[n++] = '\\'; if ((unsigned char)*s < 0x20) continue; o[n++] = *s; } o[n] = 0; }

/* "bring:<name>": the Bring Units card window of the multiplayer module (src/mu_bring.c mu_bring_test_find; since the Units
   Laboratory split it is there, not in custom-modes). Looked up on use: the module image is mapped by then. */
static DSLObj mp_bring_target(const char *n) {
    static DSLObj (*f)(const char *); static int looked;
    if (!looked) {
        looked = 1; char p[1200]; snprintf(p, sizeof p, "%s/../multiplayer/multiplayer.dylib", H->module_dir(ME));
        void *h = dlopen(p, RTLD_NOLOAD | RTLD_LOCAL); if (h) *(void **)&f = dlsym(h, "mu_bring_test_find");
        H->log(ME, "multiplayer image %p (%s): bring target %p", h, p, (void *)f);
    }
    return f ? f(n) : NULL;
}
static DSLObj target(const char *t) {
    if (!strncmp(t, "bring:", 6)) { DSLObj o = mp_bring_target(t + 6); if (o) return o; }
    if (!strcmp(t, "notice_ok")) return t_notice_btn(0);
    if (!strcmp(t, "notice_cancel")) return t_notice_btn(1);
    if (!strncmp(t, "title:", 6)) return t_title(t + 6, NULL, 0);
    return c_target ? c_target(t) : NULL;
}
static void state(char *out, size_t cap) {
    size_t o = 0; char col[1200] = "", e[1400];
    t_title(NULL, col, sizeof col); jesc(e, sizeof e, col);
    o += (size_t)snprintf(out + o, cap - o, "\"kit\":%d,\"title_column\":\"%s\"", U != NULL, e);
    if (c_state && o + 2 < cap) { o += (size_t)snprintf(out + o, cap - o, ",\"cm\":{"); o += c_state(out + o, cap - o - 2); o += (size_t)snprintf(out + o, cap - o, "}"); }
    { id w = game_window(); CGRect f = w ? ((CGRect (*)(id, SEL))objc_msgSend)(w, sel_registerName("frame")) : CGRectZero;
      o += (size_t)snprintf(out + o, cap - o, ",\"win\":[%.0f,%.0f,%.0f,%.0f]", f.origin.x, f.origin.y, f.size.width, f.size.height); }
    char tt[200] = "", te[260]; int ts = t_tip(tt, sizeof tt); jesc(te, sizeof te, tt);
    o += (size_t)snprintf(out + o, cap - o, ",\"tip\":{\"shown\":%d,\"title\":\"%s\"}", ts, te);
}

/* ------------------------------------------------------------------ font probe (test only, never shipped)
   "fonts" {name, all}: every TMP_Text (Resources.FindObjectsOfTypeAll; all:0 = only active in hierarchy) ->
   <tdir>/fonts_<name>.jsonl: path, active, text, font asset, shared material, size, style, DASText style/material,
   material floats (outline, dilate, underlay). */
static void *(*i_cls_type)(void *), *(*i_type_obj)(void *), *(*i_obj_cls)(void *), *(*i_str_new)(const char *);
static const char *(*i_cls_name)(void *); static void *(*i_cls_methods)(void *, void **); static const char *(*i_m_name)(void *);
static uint32_t (*i_m_pcount)(void *); static void *(*i_m_param)(void *, uint32_t); static int (*i_type_type)(void *);
static void *(*i_cls_parent)(void *);
static void *find_m(const char *ns, const char *cn, const char *name, int argc, int ptype) {   /* ptype: il2cpp type enum of param 0, -1 any */
    void *k = H->find_class(ns, cn); if (!k) return NULL;
    void *it = NULL, *m;
    while ((m = i_cls_methods(k, &it))) {
        if (strcmp(i_m_name(m), name) || (int)i_m_pcount(m) != argc) continue;
        if (ptype >= 0 && argc > 0 && i_type_type(i_m_param(m, 0)) != ptype) continue;
        return H->method_address(m);
    }
    return NULL;
}
static void u8of(Obj s, char *out, size_t cap) {                    /* System.String -> UTF-8 */
    size_t o = 0; out[0] = 0; if (!s) return;
    int32_t n = *(int32_t *)((char *)s + 0x10); uint16_t *c = (uint16_t *)((char *)s + 0x14);
    for (int i = 0; i < n && o + 5 < cap; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < n) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
        if (u < 0x80) out[o++] = (char)u;
        else if (u < 0x800) { out[o++] = (char)(0xC0 | u >> 6); out[o++] = (char)(0x80 | (u & 63)); }
        else if (u < 0x10000) { out[o++] = (char)(0xE0 | u >> 12); out[o++] = (char)(0x80 | ((u >> 6) & 63)); out[o++] = (char)(0x80 | (u & 63)); }
        else { out[o++] = (char)(0xF0 | u >> 18); out[o++] = (char)(0x80 | ((u >> 12) & 63)); out[o++] = (char)(0x80 | ((u >> 6) & 63)); out[o++] = (char)(0x80 | (u & 63)); }
    }
    out[o] = 0;
}
typedef struct { float r, g, b, a; } Col4;
static int fonts_dump(const char *name, int all) {
    static Obj (*fall)(Obj, const void *), (*oname)(Obj, const void *), (*cgo)(Obj, const void *), (*ctr)(Obj, const void *), (*tpar)(Obj, const void *);
    static bool (*act)(Obj, const void *); static Obj (*gfont)(Obj, const void *), (*gmat)(Obj, const void *), (*gtext)(Obj, const void *);
    static float (*gsize)(Obj, const void *); static int (*gstyle)(Obj, const void *); static int (*pid)(Obj, const void *);
    static float (*mfloat)(Obj, int, const void *); static bool (*mhas)(Obj, int, const void *); static Col4 (*gcol)(Obj, const void *);
    static Obj (*mshader)(Obj, const void *); static void *k_tmp, *k_das; static int32_t f_st = -1, f_fm = -1;
    static bool (*govf)(Obj, const void *), (*gtrunc)(Obj, const void *); static float (*gpw)(Obj, const void *); static int (*gomode)(Obj, const void *);
    typedef struct { float x, y, w, h; } RF; static RF (*grect)(Obj, const void *);
    if (!fall) {
        i_cls_type = H->il2cpp_fn("il2cpp_class_get_type"); i_type_obj = H->il2cpp_fn("il2cpp_type_get_object"); i_obj_cls = H->il2cpp_fn("il2cpp_object_get_class");
        i_str_new = H->il2cpp_fn("il2cpp_string_new"); i_cls_name = H->il2cpp_fn("il2cpp_class_get_name"); i_cls_methods = H->il2cpp_fn("il2cpp_class_get_methods");
        i_m_name = H->il2cpp_fn("il2cpp_method_get_name"); i_m_pcount = H->il2cpp_fn("il2cpp_method_get_param_count"); i_m_param = H->il2cpp_fn("il2cpp_method_get_param");
        i_type_type = H->il2cpp_fn("il2cpp_type_get_type"); i_cls_parent = H->il2cpp_fn("il2cpp_class_get_parent");
        fall = H->find_method_address("UnityEngine", "Resources", "FindObjectsOfTypeAll", 1);
        oname = H->find_method_address("UnityEngine", "Object", "get_name", 0);
        cgo = H->find_method_address("UnityEngine", "Component", "get_gameObject", 0);
        ctr = H->find_method_address("UnityEngine", "Component", "get_transform", 0);
        tpar = H->find_method_address("UnityEngine", "Transform", "get_parent", 0);
        act = H->find_method_address("UnityEngine", "GameObject", "get_activeInHierarchy", 0);
        gfont = H->find_method_address("TMPro", "TMP_Text", "get_font", 0);
        gmat = H->find_method_address("TMPro", "TMP_Text", "get_fontSharedMaterial", 0);
        gtext = H->find_method_address("TMPro", "TMP_Text", "get_text", 0);
        gsize = H->find_method_address("TMPro", "TMP_Text", "get_fontSize", 0);
        gstyle = H->find_method_address("TMPro", "TMP_Text", "get_fontStyle", 0);
        gcol = H->find_method_address("UnityEngine.UI", "Graphic", "get_color", 0);
        pid = H->find_method_address("UnityEngine", "Shader", "PropertyToID", 1);
        mfloat = find_m("UnityEngine", "Material", "GetFloat", 1, 0x08);
        mhas = find_m("UnityEngine", "Material", "HasProperty", 1, 0x08);
        mshader = H->find_method_address("UnityEngine", "Material", "get_shader", 0);
        govf = H->find_method_address("TMPro", "TMP_Text", "get_isTextOverflowing", 0);
        gtrunc = H->find_method_address("TMPro", "TMP_Text", "get_isTextTruncated", 0);
        gpw = H->find_method_address("TMPro", "TMP_Text", "get_preferredWidth", 0);
        gomode = H->find_method_address("TMPro", "TMP_Text", "get_overflowMode", 0);
        grect = H->find_method_address("UnityEngine", "RectTransform", "get_rect", 0);
        k_tmp = H->find_class("TMPro", "TMP_Text"); k_das = H->find_class("Refactor.UI", "DASText");
        if (k_das) { f_st = H->field_offset(k_das, "_fontStyle"); f_fm = H->field_offset(k_das, "_fontMaterial"); }
        H->log(ME, "font probe: fall %p name %p font %p mat %p getfloat %p das %p (%d %d)", (void *)fall, (void *)oname, (void *)gfont, (void *)gmat, (void *)mfloat, k_das, f_st, f_fm);
    }
    if (!fall || !k_tmp || !oname || !gfont) return -1;
    Obj arr = fall(i_type_obj(i_cls_type(k_tmp)), NULL); if (!arr) return -2;
    size_t n = (size_t)*(uintptr_t *)((char *)arr + 0x18); Obj *el = (Obj *)((char *)arr + 0x20);
    char p[1400]; snprintf(p, sizeof p, "%s/fonts_%s.jsonl", tdir, name);
    FILE *f = fopen(p, "w"); if (!f) return -3;
    const char *props[] = { "_OutlineWidth", "_FaceDilate", "_OutlineSoftness", "_UnderlayOffsetX", "_UnderlayOffsetY", "_UnderlayDilate", "_UnderlaySoftness", "_WeightNormal", "_WeightBold", "_ScaleRatioA" };
    int pids_[10]; for (int i = 0; i < 10; i++) pids_[i] = pid ? pid(i_str_new(props[i]), NULL) : 0;
    int k = 0;
    for (size_t i = 0; i < n; i++) {
        Obj t = el[i]; if (!t) continue;
        Obj go = cgo(t, NULL); if (!go) continue;
        int a = act(go, NULL) ? 1 : 0;
        if (!all && !a) continue;
        char path[600] = "", nm[120], tmp[600];
        Obj tr = ctr(t, NULL), tr0 = tr;
        for (int d = 0; tr && d < 8; d++, tr = tpar(tr, NULL)) { u8of(oname(tr, NULL), nm, sizeof nm); snprintf(tmp, sizeof tmp, "%s/%s", nm, path); snprintf(path, sizeof path, "%s", tmp); }
        char tx[300], fn[120] = "", mn[120] = "", sh[120] = "", cls[80], e1[700], e2[700], e3[200], e4[200];
        u8of(gtext(t, NULL), tx, sizeof tx);
        Obj fa = gfont(t, NULL); if (fa) u8of(oname(fa, NULL), fn, sizeof fn);
        Obj m = gmat ? gmat(t, NULL) : NULL; if (m) { u8of(oname(m, NULL), mn, sizeof mn); Obj s = mshader ? mshader(m, NULL) : NULL; if (s) u8of(oname(s, NULL), sh, sizeof sh); }
        snprintf(cls, sizeof cls, "%s", i_cls_name(i_obj_cls(t)));
        int dst = -1, dfm = -1;
        if (!strcmp(cls, "DASText") && f_st >= 0) { dst = *(int32_t *)((char *)t + f_st); dfm = *(int32_t *)((char *)t + f_fm); }
        Col4 c = gcol ? gcol(t, NULL) : (Col4){ 0 };
        jesc(e1, sizeof e1, path); jesc(e2, sizeof e2, tx); jesc(e3, sizeof e3, fn); jesc(e4, sizeof e4, mn);
        fprintf(f, "{\"path\":\"%s\",\"active\":%d,\"cls\":\"%s\",\"text\":\"%s\",\"font\":\"%s\",\"mat\":\"%s\",\"shader\":\"%s\",\"size\":%.1f,\"style\":%d,\"das_style\":%d,\"das_mat\":%d,\"color\":[%.2f,%.2f,%.2f,%.2f]",
                e1, a, cls, e2, e3, e4, sh, gsize ? gsize(t, NULL) : 0.f, gstyle ? gstyle(t, NULL) : -1, dst, dfm, c.r, c.g, c.b, c.a);
        if (m && mfloat && mhas) { fprintf(f, ",\"mf\":{"); int fi = 0; for (int q = 0; q < 10; q++) if (mhas(m, pids_[q], NULL)) fprintf(f, "%s\"%s\":%.3f", fi++ ? "," : "", props[q] + 1, mfloat(m, pids_[q], NULL)); fprintf(f, "}"); }
        RF rr = grect && tr0 ? grect(tr0, NULL) : (RF){ 0 };
        fprintf(f, ",\"ovf\":%d,\"trunc\":%d,\"omode\":%d,\"pw\":%.1f,\"rw\":%.1f,\"rh\":%.1f", govf ? govf(t, NULL) : -1, gtrunc ? gtrunc(t, NULL) : -1,
                gomode ? gomode(t, NULL) : -1, gpw ? gpw(t, NULL) : -1.f, rr.w, rr.h);
        fprintf(f, "}\n"); k++;
    }
    fclose(f);
    return k;
}
static void handle(const char *j) {
    static char out[65536], st[200000];
    char cmd[32], t[256], s[4096]; int seq = (int)jnum(j, "seq", 0), rc = 0; size_t o = 0;
    jstr(j, "cmd", cmd, sizeof cmd); jstr(j, "target", t, sizeof t);
    out[0] = 0;
    if (!U) rc = -100;
    else if (!strcmp(cmd, "state")) { }
    else if (!strcmp(cmd, "click") || !strcmp(cmd, "hover") || !strcmp(cmd, "wheel") || !strcmp(cmd, "rect")) {
        DSLObj w = target(t); float r[4] = { 0 }; int on = w ? t_rect(w, r) : -1;
        char l[400] = "", le[500]; if (w && on >= 0) t_label(w, l, sizeof l); jesc(le, sizeof le, l);
        if (!strcmp(cmd, "rect")) rc = on;
        else if (on <= 0) rc = -1;
        else rc = !strcmp(cmd, "click") ? click_rect(r) : !strcmp(cmd, "hover") ? move_rect(r) : wheel_rect(r, (int)jnum(j, "dy", -3));
        o += (size_t)snprintf(out + o, sizeof out - o, "\"on\":%d,\"at\":[%.0f,%.0f,%.0f,%.0f],\"text\":\"%s\",", on, r[0], r[1], r[2], r[3], le);
    }
    else if (!strcmp(cmd, "mouse_off")) fk_on = 0;
    else if (!strcmp(cmd, "via")) via_view = (int)jnum(j, "view", 1);
    else if (!strcmp(cmd, "key")) rc = key_code((int)jnum(j, "code", 36));
    else if (!strcmp(cmd, "type")) { jstr(j, "text", s, sizeof s); DSLObj w = target(t); rc = w ? t_type(w, s) : -1; }
    else if (!strcmp(cmd, "field_set")) { jstr(j, "text", s, sizeof s); DSLObj w = target(t); if (w) U->field_set(w, s); else rc = -1; }
    else if (!strcmp(cmd, "capture")) { jstr(j, "name", s, sizeof s); rc = capture(s); }
    else if (!strcmp(cmd, "fonts")) { jstr(j, "name", s, sizeof s); rc = fonts_dump(s, (int)jnum(j, "all", 0)); }
    else if (!strcmp(cmd, "call")) {
        char fn[96]; jstr(j, "fn", fn, sizeof fn); jstr(j, "s", s, sizeof s);
        int (*f)(const char *, char *, size_t) = cm_img ? (int (*)(const char *, char *, size_t))dlsym(cm_img, fn) : NULL;
        static char r2[60000], re[65000]; r2[0] = 0;
        rc = f ? f(s, r2, sizeof r2) : -404; jesc(re, sizeof re, r2);
        o += (size_t)snprintf(out + o, sizeof out - o, "\"out\":\"%s\",", re);
    } else rc = -3;
    state(st, sizeof st);
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


DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me;
    const char *td = getenv("DSL_UNITS_TEST");
    if (!td || !*td) { h->log(me, "DSL_UNITS_TEST not set: driver off"); return 0; }
    snprintf(tdir, sizeof tdir, "%s", td);
    U = dsl_ui(h);
    if (!U) { h->log(me, "no dsl_ui kit: driver off"); return 0; }
    char p[1200]; snprintf(p, sizeof p, "%s/../custom-modes/custom-modes.dylib", h->module_dir(me));
    cm_img = dlopen(p, RTLD_NOLOAD | RTLD_LOCAL);
    if (cm_img) { *(void **)&c_target = dlsym(cm_img, "cm_test_target"); *(void **)&c_state = dlsym(cm_img, "cm_test_state"); }
    h->log(me, "custom-modes image %p (%s): target %p state %p", cm_img, p, (void *)c_target, (void *)c_state);
    *(void **)&t_rect = dlsym(RTLD_DEFAULT, "dsl_ui_test_rect"); *(void **)&t_label = dlsym(RTLD_DEFAULT, "dsl_ui_test_label");
    *(void **)&t_title = dlsym(RTLD_DEFAULT, "dsl_ui_test_title"); *(void **)&t_notice_btn = dlsym(RTLD_DEFAULT, "dsl_ui_test_notice_button");
    *(void **)&t_type = dlsym(RTLD_DEFAULT, "dsl_ui_test_type"); *(void **)&t_tip = dlsym(RTLD_DEFAULT, "dsl_ui_test_tip");
    if (!t_rect || !t_label || !t_title || !t_notice_btn || !t_type || !t_tip) { h->log(me, "loader test exports missing: driver off"); return 0; }
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
    h->log(me, "units test driver on (%s)", tdir);
    return 0;
}
