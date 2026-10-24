// sound-probe: TEST ONLY module for sound_fix_test.py (never shipped). Drives the Options window's sound sliders the
// player's way and reads the mixers back. Requests: <DSRIG_DIR>/sound_probe_req.txt, one line, polled at
// StandaloneInputModule.UpdateModule (EventSystem.Update ticks it every frame, main thread):
//   state            the SoundManager's mixer (the one the game's sounds play through): its 5 exposed volumes; the stored
//                    settings (SoundSetting of MainFlow._userSetting): expected dB per group (-80 when muted); when the
//                    Options window was opened: the window's own mixer (pointer, same object or not) and its 5 volumes
//   open             ShowPopUpRequested(GameOption) on the EventQueue (the Settings / Option button)
//   slider <g> <v>   SubUI_Slider.SetValue(v) of group g (0 Master .. 4 UI_SFX) in the open window: UnityEngine.UI.Slider
//                    value -> onValueChanged -> the window's own handler, as a mouse drag does
//   mute <g> <0|1>   the group's Mute toggle (Toggle.isOn), as a click does
//   uisfx            the mixer group the game gives each UI sound (the UI handler's _uiAudioMixerGroup), by name
// Each answer is one or more dsload.log lines "sound-probe: <cmd>: ...". Every game call by il2cpp name.
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DSL_PLUGIN();
static const DSLHost *H;
static DSLPlugin *ME;
typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
static const char *GROUPS[5] = { "Master", "BGM", "Ambience_BGM", "SFX", "UI_SFX" };

static void (*o_tick)(Obj, const void *);
static Obj (*rt_invoke)(const void *, Obj, void **, Obj *);
static Obj (*str_new)(const char *);
static Obj (*obj_new)(void *);
static Obj (*tobj)(void *);
static void *(*ctype)(void *), *(*ocls)(Obj);
static void *(*field)(void *, const char *);
static void (*fs_get)(void *, void *);
static char req_path[1024];

static void *msig(void *k, const char *name, int argc) {
    void *(*methods)(void *, void **) = H->il2cpp_fn("il2cpp_class_get_methods");
    const char *(*mname)(void *) = H->il2cpp_fn("il2cpp_method_get_name");
    uint32_t (*pc)(void *) = H->il2cpp_fn("il2cpp_method_get_param_count");
    uint8_t (*gen)(void *) = H->il2cpp_fn("il2cpp_method_is_generic");
    void *(*parent)(void *) = H->il2cpp_fn("il2cpp_class_get_parent");
    for (void *c = k; c; c = parent(c)) { void *it = NULL, *m; while ((m = methods(c, &it))) if (!strcmp(mname(m), name) && (int)pc(m) == argc && !gen(m)) return m; }
    return NULL;
}
static Obj call(void *mi, Obj self, void **args, int *threw) {
    if (!mi) { if (threw) *threw = 1; H->log(ME, "sound-probe: a method was not found"); return NULL; }
    Obj exc = NULL, r = rt_invoke(mi, self, args, &exc);
    if (threw) *threw = exc != NULL;
    return r;
}
static void *K(const char *ns, const char *n) { return H->find_class(ns, n); }
static int32_t fofs(void *k, const char *f) { return k ? H->field_offset(k, f) : -1; }
static Obj find_obj(void *k) {                       /* Object.FindObjectOfType(Type, includeInactive: true): a window tab not shown is inactive */
    static void *mi; if (!mi) mi = msig(K("UnityEngine", "Object"), "FindObjectOfType", 2);
    uint8_t inactive = 1; void *args[2] = { tobj(ctype(k)), &inactive };
    return mi ? call(mi, NULL, args, NULL) : NULL;
}
static Obj mainflow(void) { return find_obj(K("Refactor.Main", "MainFlow")); }
static Obj sound_manager(void) {
    void *k = K("Refactor.Sound", "SoundManager"), *(*parent)(void *) = H->il2cpp_fn("il2cpp_class_get_parent");
    Obj sm = NULL; void *f = field(parent(k), "_instance"); if (f) fs_get(f, &sm);
    return sm;
}
static int mixer_get(Obj mixer, const char *name, float *out) {
    static void *mi; if (!mi) mi = msig(K("UnityEngine.Audio", "AudioMixer"), "GetFloat", 2);
    void *args[2] = { str_new(name), out }; int threw = 0;
    Obj r = call(mi, mixer, args, &threw);
    return !threw && r && *(uint8_t *)((char *)r + 0x10);        /* boxed bool */
}
static void mixer_line(const char *what, Obj mixer) {
    char line[400]; int o = snprintf(line, sizeof line, "%s mixer %p:", what, mixer);
    for (int g = 0; g < 5 && mixer; g++) { float v = -999; int ok = mixer_get(mixer, GROUPS[g], &v); o += snprintf(line + o, sizeof line - o, " %s=%.2f%s", GROUPS[g], v, ok ? "" : "(?)"); }
    H->log(ME, "sound-probe: state: %s", line);
}
static Obj sound_setting(void) {
    Obj mf = mainflow(); int32_t o = fofs(K("Refactor.Main", "MainFlow"), "_userSetting");
    Obj us = mf && o > 0 ? AT(mf, o, Obj) : NULL; int32_t os = fofs(K("Refactor.Main", "UserSettingContainer"), "SoundSetting");
    return us && os > 0 ? AT(us, os, Obj) : NULL;
}
static Obj sub_sound(void) { return find_obj(K("Refactor.UI", "SubUI_SoundSetting")); }

static void do_state(void) {
    Obj sm = sound_manager(); int32_t om = fofs(K("Refactor.Sound", "SoundManager"), "_audioMixer");
    Obj real = sm && om > 0 ? AT(sm, om, Obj) : NULL;
    mixer_line("game", real);
    Obj ss = sound_setting();
    if (ss) {
        void *k = K("Refactor.Setting", "SoundSetting"), *mdb = msig(k, "GetUnMutedVolumeDecibel", 1), *mmu = msig(k, "GetMutedVolume", 1), *mpc = msig(k, "GetUnMutedVolumePercent", 1);
        char line[400]; int o = 0;
        for (int g = 0; g < 5; g++) {
            int32_t gg = g; void *a[1] = { &gg };
            Obj db = call(mdb, ss, a, NULL), mu = call(mmu, ss, a, NULL), pc = call(mpc, ss, a, NULL);
            float d = db ? *(float *)((char *)db + 0x10) : -999, p = pc ? *(float *)((char *)pc + 0x10) : -999; int m = mu ? *(uint8_t *)((char *)mu + 0x10) : -1;
            o += snprintf(line + o, sizeof line - o, " %s=%.2f(%.0f%%%s)", GROUPS[g], m == 1 ? -80.0f : d, p, m == 1 ? " muted" : "");
        }
        H->log(ME, "sound-probe: state: stored:%s", line);
    } else H->log(ME, "sound-probe: state: stored: no SoundSetting");
    Obj sub = sub_sound(); int32_t ou = fofs(K("Refactor.UI", "SubUI_SoundSetting"), "_audioMixer");
    Obj ui = sub && ou > 0 ? AT(sub, ou, Obj) : NULL;
    if (ui) { H->log(ME, "sound-probe: state: window mixer %s the game mixer", ui == real ? "IS" : "is NOT"); mixer_line("window", ui); }
    else H->log(ME, "sound-probe: state: window: not open");
    H->log(ME, "sound-probe: state done");
}

/* the open window's SubUI_Slider of a group: SubUI_SoundSetting._audioGroupNameToSlider (Dictionary<AudioGroupName,
   SubUI_Slider>: _entries[i] = {hashCode, next, key, value}, 24 bytes; array data at +0x20) */
static Obj slider_of(int g) {
    Obj sub = sub_sound(); int32_t od = fofs(K("Refactor.UI", "SubUI_SoundSetting"), "_audioGroupNameToSlider");
    Obj d = sub && od > 0 ? AT(sub, od, Obj) : NULL; if (!d) return NULL;
    void *kd = ocls(d); int32_t oe = fofs(kd, "_entries"), oc = fofs(kd, "_count");
    if (oe <= 0 || oc <= 0) return NULL;
    Obj e = AT(d, oe, Obj); int n = AT(d, oc, int32_t);
    for (int i = 0; e && i < n; i++) { char *x = (char *)e + 0x20 + 24 * i; if (AT(x, 0, int32_t) >= 0 && AT(x, 8, int32_t) == g) return AT(x, 16, Obj); }
    return NULL;
}
static void do_slider(int g, float v) {
    Obj s = slider_of(g);
    void *mi = msig(K("Refactor.UI", "SubUI_Slider"), "SetValue", 1);
    if (!s || !mi) { H->log(ME, "sound-probe: slider %d: no window slider (%p)", g, s); return; }
    void *a[1] = { &v }; int threw = 0; call(mi, s, a, &threw);
    H->log(ME, "sound-probe: slider %s %.0f: %s", GROUPS[g], v, threw ? "THREW" : "set");
}
static void do_mute(int g, int on) {
    Obj s = slider_of(g); int32_t ot = fofs(K("Refactor.UI", "SubUI_Slider"), "_toggle");
    Obj t = s && ot > 0 ? AT(s, ot, Obj) : NULL; void *mi = msig(K("UnityEngine.UI", "Toggle"), "set_isOn", 1);
    if (!t || !mi) { H->log(ME, "sound-probe: mute %d: no toggle", g); return; }
    uint8_t b = on != 0; void *a[1] = { &b }; int threw = 0; call(mi, t, a, &threw);
    H->log(ME, "sound-probe: mute %s %d: %s", GROUPS[g], on, threw ? "THREW" : "set");
}
static void do_open(void) {
    Obj mf = mainflow(); int32_t oq = fofs(K("Refactor.Main", "MainFlow"), "_eventQueue");
    Obj eq = mf && oq > 0 ? AT(mf, oq, Obj) : NULL;
    void *k = K("Refactor.Main.Event", "ShowPopUpRequested"), *ctor = k ? H->find_method(k, ".ctor", 1) : NULL;
    void *add = msig(K("Refactor.Main", "EventQueue"), "AddEvent", 1);
    if (!eq || !ctor || !add) { H->log(ME, "sound-probe: open: no EventQueue / event (%p %p %p)", eq, ctor, add); return; }
    Obj ev = obj_new(k); int32_t pop = 6; void *a[1] = { &pop }; call(ctor, ev, a, NULL);   /* PopUpType.GameOption */
    void *b[1] = { ev }; int threw = 0; call(add, eq, b, &threw);
    H->log(ME, "sound-probe: open: ShowPopUpRequested(GameOption) %s", threw ? "THREW" : "sent");
}
static void do_uisfx(void) {
    /* AudioSource.outputAudioMixerGroup has no getter in this build (stripped), so read what the game passes on each UI
       sound: PlayUISFX gives the source the UI handler's _uiAudioMixerGroup (AudioClipPoolReturner.SetAudioUISFX) */
    Obj sm = sound_manager(); void *ksm = K("Refactor.Sound", "SoundManager"), *kh = K("Refactor.Sound", "OneTimeSFXSoundHandler");
    int32_t ou = fofs(ksm, "_uiOneTimeSFXSoundHandler"), og = fofs(kh, "_uiAudioMixerGroup");
    Obj hd = sm && ou > 0 ? AT(sm, ou, Obj) : NULL, grp = hd && og > 0 ? AT(hd, og, Obj) : NULL;
    void *m_name = msig(K("UnityEngine", "Object"), "get_name", 0);
    Obj nm = grp ? call(m_name, grp, NULL, NULL) : NULL;
    char s[64] = "(none)"; int len = nm ? AT(nm, 0x10, int32_t) : 0;
    if (nm) { for (int j = 0; j < len && j < 63; j++) s[j] = (char)((uint16_t *)((char *)nm + 0x14))[j]; s[len < 63 ? len : 63] = 0; }
    H->log(ME, "sound-probe: uisfx: UI sounds get output group %s", s);
    H->log(ME, "sound-probe: uisfx done");
}

static unsigned frames;
static void my_tick(Obj self, const void *mi) {
    o_tick(self, mi);
    if (++frames % 10) return;
    FILE *f = fopen(req_path, "r");
    if (!f) return;
    char line[256] = ""; if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f);
    char done[1100]; snprintf(done, sizeof done, "%s.done", req_path); rename(req_path, done);
    line[strcspn(line, "\r\n")] = 0;
    char cmd[32] = ""; int g = 0; float v = 0; sscanf(line, "%31s %d %f", cmd, &g, &v);
    if (!strcmp(cmd, "state")) do_state();
    else if (!strcmp(cmd, "open")) do_open();
    else if (!strcmp(cmd, "slider") && g >= 0 && g < 5) do_slider(g, v);
    else if (!strcmp(cmd, "mute") && g >= 0 && g < 5) do_mute(g, (int)v);
    else if (!strcmp(cmd, "uisfx")) do_uisfx();
    else H->log(ME, "sound-probe: unknown request '%s'", line);
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    const char *d = getenv("DSRIG_DIR");
    if (!d) { H->log(ME, "sound-probe: no DSRIG_DIR (test copies only)"); return 1; }
    snprintf(req_path, sizeof req_path, "%s/sound_probe_req.txt", d);
    *(void **)&rt_invoke = H->il2cpp_fn("il2cpp_runtime_invoke"); *(void **)&str_new = H->il2cpp_fn("il2cpp_string_new");
    *(void **)&obj_new = H->il2cpp_fn("il2cpp_object_new"); *(void **)&tobj = H->il2cpp_fn("il2cpp_type_get_object");
    *(void **)&ctype = H->il2cpp_fn("il2cpp_class_get_type"); *(void **)&ocls = H->il2cpp_fn("il2cpp_object_get_class");
    *(void **)&field = H->il2cpp_fn("il2cpp_class_get_field_from_name"); *(void **)&fs_get = H->il2cpp_fn("il2cpp_field_static_get_value");
    if (!rt_invoke || !str_new || !obj_new || !tobj || !ctype || !ocls || !field || !fs_get) return 2;
    if (H->hook_method(ME, "UnityEngine.EventSystems", "StandaloneInputModule", "UpdateModule", 0, (void *)my_tick, (void **)&o_tick)) return 3;
    H->log(ME, "sound-probe: ready (%s)", req_path);
    return 0;
}
