// vsync-probe: TEST ONLY module for vsync_test.py (never shipped). Reads what the player gets from the game's frame
// settings, every frame on the main thread (MainFlow.Update, after the game's own Update):
//   QualitySettings.vSyncCount, Application.targetFrameRate (icalls), MainFlow._currentScene / _bootReady, and the
//   player's Settings > VSync (MainFlow._userSetting.GraphicSetting.GraphicSettingData.VSyncEnabled, -1 = not made yet).
// One dsload.log line "vsync-probe: frame <n> t <s> scene <s> boot <b> vsync <v> fps <f> setting <x>" each time one of
// them changes. Requests: <DSRIG_DIR>/vsync_probe_req.txt, one line:
//   load <save name>   LoadCampaignRequested(<save name>) on the game's event queue (the game's own Load path)
//   state              the same line now, tagged "state"
//   quit               Application.Quit (the copy has no control socket without the multiplayer module)
// Every game call by il2cpp name.
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

DSL_PLUGIN();
static const DSLHost *H;
static DSLPlugin *ME;
typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))

static void (*o_update)(Obj, const void *);
static int (*get_vsync)(void), (*get_fps)(void);
static Obj (*obj_new)(void *), (*str_new)(const char *);
static void *k_load; static void (*ctor_load)(Obj, Obj, const void *); static void (*eq_add)(Obj, Obj, const void *);
static void (*app_quit)(const void *);
static int32_t off_scene = -1, off_boot = -1, off_eq = -1, off_us = -1, off_gs = -1, off_gsd = -1, off_vse = -1;
static char req_path[1024];
static double t0;

static double now(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec / 1e6; }
static int32_t fofs(const char *ns, const char *k, const char *f) {
    void *c = H->find_class(ns, k); int32_t o = c ? H->field_offset(c, f) : -1;
    if (o < 0) H->log(ME, "vsync-probe: field %s.%s.%s not found", ns, k, f);
    return o;
}
static int setting(Obj mf) {
    Obj us = off_us >= 0 ? AT(mf, off_us, Obj) : NULL, gs = us && off_gs >= 0 ? AT(us, off_gs, Obj) : NULL;
    Obj gd = gs && off_gsd >= 0 ? AT(gs, off_gsd, Obj) : NULL;
    return gd && off_vse >= 0 ? AT(gd, off_vse, uint8_t) != 0 : -1;
}

static unsigned long frames;
static void my_update(Obj self, const void *mi) {
    o_update(self, mi);
    frames++;
    int sc = off_scene >= 0 ? AT(self, off_scene, int32_t) : -9, bt = off_boot >= 0 ? AT(self, off_boot, uint8_t) : -9;
    int vs = get_vsync(), fp = get_fps(), st = setting(self);
    static int l_sc = -99, l_bt = -99, l_vs = -99, l_fp = -99, l_st = -99;
    if (sc != l_sc || bt != l_bt || vs != l_vs || fp != l_fp || st != l_st) {
        H->log(ME, "vsync-probe: frame %lu t %.2f scene %d boot %d vsync %d fps %d setting %d", frames, now() - t0, sc, bt, vs, fp, st);
        l_sc = sc; l_bt = bt; l_vs = vs; l_fp = fp; l_st = st;
    }
    if (frames % 10) return;
    FILE *f = fopen(req_path, "r");
    if (!f) return;
    char line[512] = ""; if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f);
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0]) return;
    if ((f = fopen(req_path, "w"))) fclose(f);                         /* emptied = done */
    if (!strcmp(line, "state")) H->log(ME, "vsync-probe: state frame %lu scene %d boot %d vsync %d fps %d setting %d", frames, sc, bt, vs, fp, st);
    else if (!strcmp(line, "quit") && app_quit) { H->log(ME, "vsync-probe: quit"); app_quit(NULL); }
    else if (!strncmp(line, "load ", 5)) {
        Obj eq = off_eq >= 0 ? AT(self, off_eq, Obj) : NULL;
        if (!eq || !k_load || !ctor_load || !eq_add) { H->log(ME, "vsync-probe: load: game functions missing"); return; }
        Obj e = obj_new(k_load); ctor_load(e, str_new(line + 5), NULL); eq_add(eq, e, NULL);
        H->log(ME, "vsync-probe: load: LoadCampaignRequested %s queued", line + 5);
    } else H->log(ME, "vsync-probe: unknown request '%s'", line);
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self; t0 = now();
    const char *d = getenv("DSRIG_DIR");
    if (!d) { H->log(ME, "vsync-probe: no DSRIG_DIR (test copies only)"); return 1; }
    snprintf(req_path, sizeof req_path, "%s/vsync_probe_req.txt", d);
    void *(*icall)(const char *) = H->il2cpp_fn("il2cpp_resolve_icall");
    *(void **)&obj_new = H->il2cpp_fn("il2cpp_object_new"); *(void **)&str_new = H->il2cpp_fn("il2cpp_string_new");
    if (!icall || !obj_new || !str_new) return 2;
    *(void **)&get_vsync = icall("UnityEngine.QualitySettings::get_vSyncCount");
    *(void **)&get_fps = icall("UnityEngine.Application::get_targetFrameRate");
    if (!get_vsync || !get_fps) return 3;
    off_scene = fofs("Refactor.Main", "MainFlow", "_currentScene"); off_boot = fofs("Refactor.Main", "MainFlow", "_bootReady");
    off_eq = fofs("Refactor.Main", "MainFlow", "_eventQueue"); off_us = fofs("Refactor.Main", "MainFlow", "_userSetting");
    off_gs = fofs("Refactor.Main", "UserSettingContainer", "GraphicSetting");
    off_gsd = fofs("Refactor.Setting", "GraphicSetting", "GraphicSettingData");
    off_vse = fofs("Refactor.Setting", "GraphicSettingData", "VSyncEnabled");
    k_load = H->find_class("Refactor.Main.Event", "LoadCampaignRequested");
    *(void **)&ctor_load = H->find_method_address("Refactor.Main.Event", "LoadCampaignRequested", ".ctor", 1);
    *(void **)&eq_add = H->find_method_address("Refactor.Main", "EventQueue", "AddEvent", 1);
    *(void **)&app_quit = H->find_method_address("UnityEngine", "Application", "Quit", 0);
    if (H->hook_method(ME, "Refactor.Main", "MainFlow", "Update", 0, (void *)my_update, (void **)&o_update)) return 4;
    H->log(ME, "vsync-probe: ready (%s)", req_path);
    return 0;
}
