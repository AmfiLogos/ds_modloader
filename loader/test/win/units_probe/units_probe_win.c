// units-probe (Windows): TEST-ONLY module, never shipped. Drives Units Laboratory's windows on the Windows VM for
// loader/test/win/units_lab_vm.py, the way ds_custommodes home/test/font_probe drives them on a Mac test copy, through
// Units Laboratory's own test registry (custom-modes.dll exports cm_test_target / cm_test_state, the same names on Mac).
// A click calls the kit button's own Action (SubUI_TitleMenuButton._onButtonClicked: the loader kit queues the click and
// runs the module's callback on its next frame tick, as a mouse click does); "shown" = the widget is active in the
// hierarchy. Pictures: the loader's DSL_UI_TEST shot (winrig gshot).
// DSL_UNITS_TEST=<dir>: <dir>\req.json {"seq":n,"cmd":...} -> <dir>\resp.json {"seq":n,"rc":..,"cm":{...}}
//   cmd state | open (cm_home_open) | click {target} | rect {target} | call {fn, s} (a custom-modes test function
//   "int fn(const char *s, char *out, size_t cap)" by name)
// Frame tick: StandaloneInputModule.Process (called by EventSystem.Update every frame; the loader owns EventSystem.Update).
#include "dsl_sdk.h"
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DSL_PLUGIN();

typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define CALL(mi, R, ...) ((R (*)(__VA_ARGS__, const void *))(*(void **)(mi)))

static const DSLHost *H; static DSLPlugin *ME;
static char tdir[1024];
static HMODULE cm;
static DSLObj (*c_target)(const char *); static size_t (*c_state)(char *, size_t); static void (*c_home_open)(void);
static void *(*i_class_get_type)(void *), *(*i_type_get_object)(void *), *(*i_object_get_class)(void *);
static void *(*i_class_get_methods)(void *, void **), *(*i_method_get_param)(void *, uint32_t), *(*i_class_get_parent)(void *);
static const char *(*i_method_get_name)(void *); static uint32_t (*i_method_get_param_count)(void *);
static char *(*i_type_get_name)(void *); static void (*i_free)(void *);
static void *(*i_object_get_virtual_method)(void *, void *);
static void *k_tb, *k_tmp, *m_getcomp, *m_inchild, *m_go, *m_active, *m_text;
static int32_t f_act = -1;

static void *mi_sig(void *k, const char *name, int argc, const char *sig) {   /* by name, argc, parameter type names */
    for (void *c = k; c; c = i_class_get_parent(c)) {
        void *it = NULL, *m;
        while ((m = i_class_get_methods(c, &it))) {
            if (strcmp(i_method_get_name(m), name) || (int)i_method_get_param_count(m) != argc) continue;
            if (!sig) return m;
            char buf[256] = ""; size_t o = 0;
            for (int i = 0; i < argc; i++) {
                char *tn = i_type_get_name(i_method_get_param(m, (uint32_t)i));
                o += (size_t)snprintf(buf + o, o < sizeof buf ? sizeof buf - o : 0, "%s%s", i ? "," : "", tn ? tn : "?");
                if (tn && i_free) i_free(tn);
            }
            if (!strcmp(buf, sig)) return m;
        }
    }
    return NULL;
}
static Obj type_of(void *k) { return k ? i_type_get_object(i_class_get_type(k)) : NULL; }
static Obj comp(Obj o, void *k, int child) {
    void *m = child ? m_inchild : m_getcomp;
    return o && k && m ? CALL(m, Obj, Obj, Obj)(o, type_of(k), m) : NULL;
}
static int shown(Obj o) {
    Obj g = o && m_go ? CALL(m_go, Obj, Obj)(o, m_go) : NULL;
    return g && m_active ? CALL(m_active, uint8_t, Obj)(g, m_active) != 0 : 0;
}
static void utf8_of(Obj s, char *out, size_t cap) {
    size_t n = 0; out[0] = 0; if (!s) return;
    int32_t len = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    for (int32_t i = 0; i < len && n + 4 < cap; i++) {
        uint32_t u = c[i];
        if (u < 0x80) out[n++] = (char)u;
        else if (u < 0x800) { out[n++] = (char)(0xC0 | u >> 6); out[n++] = (char)(0x80 | (u & 63)); }
        else { out[n++] = (char)(0xE0 | u >> 12); out[n++] = (char)(0x80 | (u >> 6 & 63)); out[n++] = (char)(0x80 | (u & 63)); }
    }
    out[n] = 0;
}
static void label_of(Obj o, char *out, size_t cap) {
    Obj t = comp(o, k_tmp, 1); out[0] = 0;
    if (!t || !m_text) return;
    void *m = i_object_get_virtual_method ? i_object_get_virtual_method(t, m_text) : NULL; if (!m) m = m_text;
    utf8_of(CALL(m, Obj, Obj)(t, m), out, cap);
}
static int click(Obj o) {                                   /* the kit button's Action: the loader queues the click */
    Obj b = comp(o, k_tb, 0); if (!b) b = comp(o, k_tb, 1);
    Obj a = b && f_act >= 0 ? AT(b, f_act, Obj) : NULL;
    if (!a) return -2;
    void (*fn)(void *, const void *) = AT(a, 0x18, void *);  /* invoke_impl(invoke_impl_this, method), as Action.Invoke */
    if (!fn) return -3;
    fn(AT(a, 0x40, void *), AT(a, 0x28, void *));
    return 0;
}
static char *jstr(const char *j, const char *k, char *out, size_t cap) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", k); const char *p = strstr(j, pat); out[0] = 0; if (!p) return NULL;
    p += strlen(pat); while (*p == ' ') p++; if (*p != '"') return NULL; p++;
    size_t n = 0; while (*p && *p != '"' && n + 1 < cap) { if (*p == '\\' && p[1]) p++; out[n++] = *p++; } out[n] = 0; return out;
}
static double jnum(const char *j, const char *k, double d) { char pat[64]; snprintf(pat, sizeof pat, "\"%s\":", k); const char *p = strstr(j, pat); return p ? atof(p + strlen(pat)) : d; }
static void jesc(char *o, size_t cap, const char *s) { size_t n = 0; for (; *s && n + 3 < cap; s++) { if (*s == '"' || *s == '\\') o[n++] = '\\'; if ((unsigned char)*s < 0x20) continue; o[n++] = *s; } o[n] = 0; }

static void handle(const char *j) {
    static char out[70000], st[200000], l[600], le[800];
    char cmd[32], t[256], s[2048]; int seq = (int)jnum(j, "seq", 0), rc = 0; size_t o = 0;
    jstr(j, "cmd", cmd, sizeof cmd); jstr(j, "target", t, sizeof t);
    out[0] = 0;
    if (!cm && (cm = GetModuleHandleA("custom-modes.dll"))) {            /* loaded after this module: looked up on use */
        *(void **)&c_target = (void *)GetProcAddress(cm, "cm_test_target"); *(void **)&c_state = (void *)GetProcAddress(cm, "cm_test_state");
        *(void **)&c_home_open = (void *)GetProcAddress(cm, "cm_home_open");
    }
    if (!c_target || !c_state) rc = -100;
    else if (!strcmp(cmd, "state")) { }
    else if (!strcmp(cmd, "open")) { if (c_home_open) c_home_open(); else rc = -404; }
    else if (!strcmp(cmd, "click") || !strcmp(cmd, "rect")) {
        Obj w = (Obj)c_target(t); int on = w ? shown(w) : -1;
        if (w) label_of(w, l, sizeof l); else l[0] = 0; jesc(le, sizeof le, l);
        rc = !strcmp(cmd, "rect") ? on : on <= 0 ? -1 : click(w);
        o += (size_t)snprintf(out + o, sizeof out - o, "\"on\":%d,\"text\":\"%s\",", on, le);
    } else if (!strcmp(cmd, "call")) {
        char fn[96]; jstr(j, "fn", fn, sizeof fn); jstr(j, "s", s, sizeof s);
        int (*f)(const char *, char *, size_t) = cm ? (int (*)(const char *, char *, size_t))(void *)GetProcAddress(cm, fn) : NULL;
        static char r2[30000], re[34000]; r2[0] = 0;
        rc = f ? f(s, r2, sizeof r2) : -404; jesc(re, sizeof re, r2);
        o += (size_t)snprintf(out + o, sizeof out - o, "\"out\":\"%s\",", re);
    } else rc = -3;
    size_t n = c_state ? c_state(st, sizeof st) : 0; if (!n) snprintf(st, sizeof st, "\"none\":1");
    char p[1200], q[1200]; snprintf(p, sizeof p, "%s\\resp.json.tmp", tdir); snprintf(q, sizeof q, "%s\\resp.json", tdir);
    FILE *f = fopen(p, "wb"); if (!f) return;
    fprintf(f, "{\"seq\":%d,\"cmd\":\"%s\",\"rc\":%d,%s\"cm\":{%s}}\n", seq, cmd, rc, out, st); fclose(f);
    MoveFileExA(p, q, MOVEFILE_REPLACE_EXISTING);
}
static void tick(void) {
    static unsigned fr;
    if (++fr % 3) return;
    char p[1200]; snprintf(p, sizeof p, "%s\\req.json", tdir);
    FILE *f = fopen(p, "rb"); if (!f) return;
    char j[4096]; size_t n = fread(j, 1, sizeof j - 1, f); fclose(f); j[n] = 0;
    char d[1300]; snprintf(d, sizeof d, "%s\\req.done.json", tdir); MoveFileExA(p, d, MOVEFILE_REPLACE_EXISTING);
    handle(j);
}
static void (*o_process)(Obj, const void *);
static void hk_process(Obj self, const void *mi) { o_process(self, mi); tick(); }

#define GA(n) (*(void **)&i_##n = (void *)GetProcAddress(ga, "il2cpp_" #n))
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me;
    const char *td = getenv("DSL_UNITS_TEST");
    if (!td || !*td) { h->log(me, "DSL_UNITS_TEST not set: driver off"); return 0; }
    snprintf(tdir, sizeof tdir, "%s", td);
    HMODULE ga = GetModuleHandleA("GameAssembly.dll");
    GA(class_get_type); GA(type_get_object); GA(object_get_class); GA(class_get_methods); GA(method_get_param);
    GA(class_get_parent); GA(method_get_name); GA(method_get_param_count); GA(type_get_name); GA(free); GA(object_get_virtual_method);
    if (!i_class_get_type || !i_type_get_object || !i_class_get_methods || !i_method_get_name || !i_type_get_name) { h->log(me, "il2cpp exports missing: driver off"); return 0; }
    cm = GetModuleHandleA("custom-modes.dll");
    if (cm) {
        *(void **)&c_target = (void *)GetProcAddress(cm, "cm_test_target"); *(void **)&c_state = (void *)GetProcAddress(cm, "cm_test_state");
        *(void **)&c_home_open = (void *)GetProcAddress(cm, "cm_home_open");
    }
    void *k_comp = h->find_class("UnityEngine", "Component"), *k_go = h->find_class("UnityEngine", "GameObject");
    k_tb = h->find_class("Refactor.UI", "SubUI_TitleMenuButton"); k_tmp = h->find_class("TMPro", "TMP_Text");
    m_getcomp = k_comp ? mi_sig(k_comp, "GetComponent", 1, "System.Type") : NULL;
    m_inchild = k_comp ? mi_sig(k_comp, "GetComponentInChildren", 1, "System.Type") : NULL;
    m_go = k_comp ? mi_sig(k_comp, "get_gameObject", 0, NULL) : NULL;
    m_active = k_go ? mi_sig(k_go, "get_activeInHierarchy", 0, NULL) : NULL;
    m_text = k_tmp ? mi_sig(k_tmp, "get_text", 0, NULL) : NULL;
    f_act = k_tb ? h->field_offset(k_tb, "_onButtonClicked") : -1;
    int r = h->hook_method(me, "UnityEngine.EventSystems", "StandaloneInputModule", "Process", 0, (void *)hk_process, (void **)&o_process);
    h->log(me, "units test driver (Windows): custom-modes %p target %p state %p open %p; GetComponent %p InChildren %p go %p active %p text %p action +0x%x; frame hook %d (%s)",
           (void *)cm, (void *)c_target, (void *)c_state, (void *)c_home_open, m_getcomp, m_inchild, m_go, m_active, m_text, (unsigned)f_act, r, tdir);
    return 0;
}
