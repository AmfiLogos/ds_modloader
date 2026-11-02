// hk-probe: TEST-ONLY module (never shipped), read by loader/test/win/heldkey_vm.py (Windows VM; the Mac proof is
// panelsize_test.py world_cases with uikit-demo, which is Mac only). In a world it builds ONE kit panel from the template
// (dsl_window_std DSL_WIN_PANEL + DSL_WIN_SIZE: "-" / "+", the size keys), big and centred on the screen layer, so the
// middle of the game window is over it and its corners are not. Every 10 frames it writes its state to the file
// HKPROBE_OUT (one JSON object, written whole each time; the reader retries a half-written file):
//   {"scene", "loading", "panel" (built), "f" (dsl_panel_size_get), "cam": [x, y, has] (Refactor.MainCamera
//    .CameraWorldPosition: a held W moves it), "mouse": [x, y] (Input.mousePosition, y up), "screen": [w, h], "frame",
//    "held": the KeyCodes Unity sees held among W S A D Z Up Down Shift Ctrl Alt (Input.GetKeyInt)}
// It hooks nothing.
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DSL_PLUGIN();
typedef void *Obj;
typedef struct { float x, y, z; } V3;
static const DSLHost *H; static DSLPlugin *ME; static const DSLUi *U;
static DSLObj panel; static int tried_at = -1000, world_frames; static const char *out_path;
static Obj (*cam_inst)(const void *); static V3 (*cam_pos)(const void *); static V3 (*mouse_pos)(const void *);
static int (*scr_w)(const void *), (*scr_h)(const void *); static int (*key_int)(int, const void *);
static const int keys[] = { 119, 115, 97, 100, 122, 273, 274, 303, 304, 305, 306, 307, 308 };   /* W S A D Z Up Down Shift Ctrl Alt */

static void closed(void *ud) { (void)ud; dsl_window_close(U, panel); panel = NULL; }
static void build(void) {
    DSLWinStd o = { .size = sizeof o, .kind = DSL_WIN_PANEL, .w = 1100, .h = 600, .title = "Held key probe (test only)", .close = closed,
                    .move_key = "hk-probe.panel", .x = 410, .y = 240, .band_h = 52, .title_size = 22, .flags = DSL_WIN_SIZE };
    DSLWinParts pt; panel = dsl_window_std(U, &o, &pt);
    if (!panel) return;
    U->text(panel, 30, pt.body_y + 20, 1000, 40, "Size keys over this panel size it; the game camera must not move.", 18, DSL_TXT_LEFT, 0);
    H->log(ME, "HKPROBE panel built (size buttons %s)", dsl_panel_size(U, panel, "hk-probe.panel", 0, 0, 0) ? "on" : "MISSING");
}
static void on_frame(void *ud, const DSLEvent *e) {
    (void)ud;
    if (e->scene == DSL_SCENE_WORLD && !e->loading) world_frames++; else world_frames = 0;
    if (!panel && U && world_frames > 120 && world_frames - tried_at >= 60) { tried_at = world_frames; build(); }
    if (!out_path || e->frame % 10) return;
    int has = cam_inst && cam_pos && cam_inst(NULL) != NULL;
    V3 c = has ? cam_pos(NULL) : (V3){ 0, 0, 0 }, m = mouse_pos ? mouse_pos(NULL) : (V3){ 0, 0, 0 };
    FILE *f = fopen(out_path, "wb"); if (!f) return;
    fprintf(f, "{\"scene\":%d,\"loading\":%d,\"panel\":%d,\"f\":%.3f,\"cam\":[%.3f,%.3f,%d],\"mouse\":[%.0f,%.0f],\"screen\":[%d,%d],\"frame\":%llu",
            e->scene, e->loading, panel != NULL, panel ? dsl_panel_size_get(U, panel) : -1.f, c.x, c.y, has, m.x, m.y,
            scr_w ? scr_w(NULL) : -1, scr_h ? scr_h(NULL) : -1, (unsigned long long)e->frame);
    fprintf(f, ",\"held\":["); int n = 0;                          /* the keys Unity sees held now (KeyCode) */
    for (unsigned i = 0; key_int && i < sizeof keys / sizeof *keys; i++) if (key_int(keys[i], NULL)) fprintf(f, "%s%d", n++ ? "," : "", keys[i]);
    fprintf(f, "]}\n");
    fclose(f);
}
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
    H = h; ME = me; U = dsl_ui(h);
    if (!DSL_HAS(h, on_event) || !U) { h->log(me, "HKPROBE needs loader API 7 and the kit"); return 0; }
    out_path = getenv("HKPROBE_OUT");
    *(void **)&cam_inst = h->find_method_address("Refactor", "MainCamera", "get_Instance", 0);
    *(void **)&cam_pos = h->find_method_address("Refactor", "MainCamera", "get_CameraWorldPosition", 0);
    void *k = h->find_class("Refactor", "MainCamera"); void (*cinit)(void *) = (void (*)(void *))h->il2cpp_fn("il2cpp_runtime_class_init");
    if (k && cinit) cinit(k); else cam_inst = NULL;                    /* its statics exist before the first read */
    *(void **)&key_int = h->find_method_address("UnityEngine", "Input", "GetKeyInt", 1);   /* Input.GetKey(KeyCode) */
    *(void **)&mouse_pos = h->find_method_address("UnityEngine", "Input", "get_mousePosition", 0);
    *(void **)&scr_w = h->find_method_address("UnityEngine", "Screen", "get_width", 0);
    *(void **)&scr_h = h->find_method_address("UnityEngine", "Screen", "get_height", 0);
    int bad = dsl_on_event(h, me, DSL_EV_FRAME, 0, NULL, on_frame, NULL);
    h->log(me, "HKPROBE ready: camera %s, mouse %s, keys %s, events %s, state file %s", cam_inst && cam_pos ? "on" : "MISSING", key_int ? "on" : "MISSING",
           mouse_pos ? "on" : "MISSING", bad ? "FAILED" : "ok", out_path ? "on" : "off");
    return 0;
}
