// Sound settings fix (mod id sound-fix): the Options window's volume sliders and mute buttons change the sound at once,
// as a dsload code module (runtime hooks, GameAssembly file unchanged). Client only: sound is local, nothing of the
// game state changes. Every game call by il2cpp name (Mac and Windows); build 25846389 (DS_B.0.4.29).
//
// The stock bug: the Options window (Refactor.UI.SubUI_SoundSetting, a prefab of the Addressables bundle uiprefab_assets_all)
// writes the slider value to ITS reference of the audio mixer DSAudioMixer. Addressables put a copy of that mixer into
// the bundle, so the window writes a second mixer that no sound plays through. The game's sounds go through the
// mixer of Refactor.Sound.SoundManager (sharedassets0). That mixer gets the saved volumes only from
// SoundManager.RefreshSoundSetting (when the title screen loads: CampaignLoadingEventHandler.OnEvent(EnterTitleSceneRequested))
// and from the window's Reset buttons. So a slider or mute button seems to do nothing until the player goes back to
// the title screen (or restarts the game).
// The fix: after the game stores a volume or a mute (SoundSetting.SetSoundVolume / SetSoundMute: the
// UserSettingEventHandler events of the window's sliders and mute buttons), the module calls the game's own
// RefreshSoundSetting with that SoundSetting: the same call the title screen makes (all 5 groups, muted groups at
// -80 dB, the master volume the loading fades go back to).
#include "dsl_sdk.h"
#include <stdint.h>

DSL_PLUGIN();

static const DSLHost *H;
static DSLPlugin *ME;
typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))

static void *f_instance;                               /* Util.MonoSingleton<SoundManager>._instance (static) */
static void *mi_refresh;                               /* SoundManager.RefreshSoundSetting(SoundSetting) */
static int32_t off_cached = -1, off_mixer = -1;
static void (*fs_get)(void *field, void *out);
static Obj (*rt_invoke)(const void *mi, Obj obj, void **params, Obj *exc);
static int applied_n, skipped_n;

/* the live SoundManager, read each time (never kept): NULL before it exists, after it was destroyed, or without mixer */
static Obj sound_manager(void) {
    Obj sm = NULL;
    fs_get(f_instance, &sm);
    if (!sm || !AT(sm, off_cached, void *) || !AT(sm, off_mixer, Obj)) return NULL;
    return sm;
}

static void apply(Obj ss, const char *why) {
    Obj sm = ss ? sound_manager() : NULL;
    if (!sm) { if (++skipped_n <= 5) H->log(ME, "%s: no SoundManager yet, nothing applied", why); return; }
    Obj exc = NULL; void *args[1] = { ss };
    rt_invoke(mi_refresh, sm, args, &exc);             /* catches a game exception (none expected) */
    if (exc) { if (++skipped_n <= 5) H->log(ME, "%s: RefreshSoundSetting threw an exception", why); return; }
    int n = ++applied_n;
    if (n <= 5 || n % 200 == 0) H->log(ME, "%s: sound settings applied to the mixer (#%d)", why, n);
}

static void (*orig_set_volume)(Obj self, int32_t group, float percent, const void *mi);
static void my_set_volume(Obj self, int32_t group, float percent, const void *mi) {
    orig_set_volume(self, group, percent, mi);
    apply(self, "volume changed");
}
static void (*orig_set_mute)(Obj self, int32_t group, uint8_t mute, const void *mi);
static void my_set_mute(Obj self, int32_t group, uint8_t mute, const void *mi) {
    orig_set_mute(self, group, mute, mi);
    apply(self, "mute changed");
}

static int hook_named(const char *ns, const char *cls, const char *m, int argc, void *repl, void **orig) {
    int r = H->hook_method(ME, ns, cls, m, argc, repl, orig);
    if (r) H->log(ME, "hook %s.%s.%s(%d args) failed (%d)", ns, cls, m, argc, r);
    return r;
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;                               /* API 1 functions only */
    void *(*parent)(void *) = H->il2cpp_fn("il2cpp_class_get_parent");
    void *(*field)(void *, const char *) = H->il2cpp_fn("il2cpp_class_get_field_from_name");
    void (*cinit)(void *) = H->il2cpp_fn("il2cpp_runtime_class_init");
    *(void **)&fs_get = H->il2cpp_fn("il2cpp_field_static_get_value");
    *(void **)&rt_invoke = H->il2cpp_fn("il2cpp_runtime_invoke");
    void *k_sm = H->find_class("Refactor.Sound", "SoundManager");
    void *k_single = k_sm && parent ? parent(k_sm) : NULL;          /* Util.MonoSingleton<SoundManager> */
    void *k_obj = H->find_class("UnityEngine", "Object");
    f_instance = k_single && field ? field(k_single, "_instance") : NULL;
    mi_refresh = k_sm ? H->find_method(k_sm, "RefreshSoundSetting", 1) : NULL;
    off_cached = k_obj ? H->field_offset(k_obj, "m_CachedPtr") : -1;
    off_mixer = k_sm ? H->field_offset(k_sm, "_audioMixer") : -1;
    if (!f_instance || !mi_refresh || !fs_get || !rt_invoke || !cinit || off_cached <= 0 || off_mixer <= 0) {
        H->log(ME, "missing: SoundManager %p instance %p RefreshSoundSetting %p, offsets m_CachedPtr %d _audioMixer %d",
               k_sm, f_instance, mi_refresh, off_cached, off_mixer);
        return 2;
    }
    cinit(k_single);
    if (hook_named("Refactor.Setting", "SoundSetting", "SetSoundVolume", 2, (void *)my_set_volume, (void **)&orig_set_volume)) return 3;
    if (hook_named("Refactor.Setting", "SoundSetting", "SetSoundMute", 2, (void *)my_set_mute, (void **)&orig_set_mute)) return 4;
    H->log(ME, "sound sliders and mute buttons apply at once (SoundManager._audioMixer +%#x)", off_mixer);
    return 0;
}
