// Soft restart (~/ds_multiplayer/research/plan_modloader.md section 2, PLAN_V13 R5): apply another mod set without a
// visible restart. A = the running game, B = a new game process started with the new mod set (same executable, same
// environment and arguments; DSL_PROFILE = the new profile). The player only sees the game's own loading screen.
// Mods change ONLY at the title (owner, v13): an Apply in a world is refused (log line, -1).
//
// A  dsl_soft_apply(profile)   at the title only.  The game's loading screen (ScreenFadeEffectView.AnimateFadeOut with
//    LoadingUI)  writes <log dir>/softrestart/handoff.json  starts B (or takes the prewarmed B, below).
//    B "ready" -> A yields activation to B and writes "go".  B "shown" -> A orders its windows out and quits.
//    No "ready" within DSL_SOFT_TIMEOUT s (default 90) or B died: A kills B, fades back in, keeps its mods.
//    DSL_SOFT_LOAD=<save> in env_lines (flow K, from the title): B opens that save before it is shown.
// Prewarm: dsl_soft_prewarm(profile, env) starts B early, hidden; it boots to the title, puts the loading screen up,
//    writes "ready" and waits there (muted, 10 fps). dsl_soft_apply with the same profile CONTENT and env lines uses it
//    (swap at once); anything else stops it and starts cold. B never outlives A; a waiting B quits after
//    DSL_SOFT_PREWARM_MAX s (default 1800); A stops it when A enters a world.
// B  DSL_SOFT_HANDOFF=<handoff.json>: from its first window on, every window is invisible, the app never activates and
//    has no Dock / taskbar entry (dsl_soft_os.h). Audio is muted (AudioListener pause + volume 0) while hidden. Boot
//    fluff cut while hidden (Unity splash cancelled, frames uncapped, background loading High). At the title: the
//    loading screen at once (no settle wait), "ready". Flow K: LoadCampaignRequested(<save>) first, world loaded, pause.
//    "go" -> handoff read again (A's frame and apply time now), window on A's frame, visible, front, activate; "shown";
//    sound, frame cap and speed back; fade in.
// Test only (never in a player's start): DSL_SOFT_TEST=1 (apply by <log dir>/softrestart/apply_request.json, prewarm by
// prewarm_request.json), DSL_SOFT_STAGE=x,y (every window kept at that origin, far off every display),
// DSL_SOFT_NOACTIVATE=1 (no activation, no yield), DSL_SOFT_CAPTURE=<dir> (the game's own frame as PNG ~10/s while a
// restart runs). Every step goes to dsload.log with the time since the process started; window state to softrestart_frames_*.log.
// OS-neutral: windows, process start and the main-thread timer are in dsl_soft_os.h (start/mac/dsl_soft_mac.c,
// start/win/dsl_soft_win.c); everything here uses the game by il2cpp names.
#include "dsl_internal.h"
#include "dsl_soft_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))

/* ------------------------------------------------------------------ time, log */
static double t_start;                               /* process start (kernel), seconds */
static double now_s(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec / 1e6; }
static double since(void) { return now_s() - t_start; }
#define SLOG(fmt, ...) dsl_log("soft +%.3fs: " fmt, since(), ##__VA_ARGS__)
static char sdir[1200];                              /* <log dir>/softrestart */
static FILE *ff;                                     /* per-frame window log */

/* ------------------------------------------------------------------ role */
static int role_b;                                   /* this process is B (DSL_SOFT_HANDOFF) */
static int hidden;                                   /* B: windows kept invisible */
static int b_prewarm;                                /* B was started as a prewarm (waits at the title) */
static int test_mode, no_activate, stage_on; static double stage_x, stage_y;
static char cap_dir[1024]; static int cap_on; static double cap_last; static int cap_n;
static char handoff_path[1200];
static JNode *hand;                                  /* B: parsed handoff */
static double a_frame[4]; static int a_has_frame;

static double env_num(const char *k, double def) { const char *v = getenv(k); return v && *v ? atof(v) : def; }
static double pw_max(void) { return env_num("DSL_SOFT_PREWARM_MAX", 1800); }     /* a waiting prewarm B lives at most this long */

/* ------------------------------------------------------------------ game access (names, resolved after il2cpp_init) */
static Obj mf;
#define fade live_fade()                                 /* MainFlow, ScreenFadeEffectView (from their Awake) */
static void (*o_mf_awake)(Obj, const void *), (*o_fade_awake)(Obj, const void *);
static int32_t off_scene = -1, off_boot = -1, off_eq = -1, off_set = -1, off_sl = -1, off_ts = -1, off_lc = -1;
static void *(*obj_new)(void *klass); static void *(*str_new)(const char *);
static void (*eq_add)(Obj, Obj, const void *);
static void *k_load, *k_setspeed, *k_chgspeed;
static void (*ctor_load)(Obj, Obj, const void *);
static void (*ctor_setspeed)(Obj, int, const void *), (*ctor_chgspeed)(Obj, int, int, const void *);
static int (*lc_loading)(Obj, const void *);
static void (*fade_out)(Obj, Obj, int, const void *), (*fade_in)(Obj, const void *);
static int (*fade_done)(Obj, const void *), (*fade_loading)(Obj, const void *);
static void (*app_quit)(const void *);
static Obj (*cap_tex)(const void *); static Obj (*enc_png)(Obj, const void *); static void (*obj_destroy)(Obj, const void *);

/* B boot speed: the Unity splash is cancelled and frames are uncapped while hidden (the boot is frame bound:
   one loading step per frame); a prewarm B waiting at the title runs at DSL_SOFT_IDLE_FPS (10). The game's own frame
   cap and vsync come back at the swap. */
static int (*splash_done)(void); static void (*splash_cancel)(void);
static void (*set_fps)(int); static int (*get_fps)(void);              /* icalls (the managed getters are stripped) */
static void (*set_vsync)(int); static int (*get_vsync)(void);
static void (*set_blp)(int); static int (*get_blp)(void);              /* Application.backgroundLoadingPriority */
static int fast_on, fast_saved, saved_fps, saved_vsync, splash_seen, saved_blp = -1;
static int fps_mode;                                                    /* frame cap forced while hidden: 10000 boot, idle fps waiting */
static void force_fps(int target) {
    if (!set_fps || !get_fps || !set_vsync || !get_vsync) return;
    int f = get_fps(), v = get_vsync();
    if (f == target && v == 0) return;                                  /* the game (re)applies its settings at boot */
    if (!fast_saved) { fast_saved = 1; saved_fps = f; saved_vsync = v; SLOG("B: frame cap %d vsync %d -> %d while hidden", f, v, target); }
    else {
        if (f != target && f != fps_mode) saved_fps = f;                /* a value the game set itself */
        if (v != 0) saved_vsync = v;
        if (target != fps_mode) SLOG("B: frame cap -> %d while hidden", target);
    }
    fps_mode = target;
    set_vsync(0); set_fps(target);
}
static void fast_tick(void) {
    if (!fast_on) return;
    if (splash_done && !splash_done()) {
        if (!splash_seen++) SLOG("B: Unity splash screen running: cancelled");
        if (splash_cancel) splash_cancel();
    }
    if (set_blp && get_blp && !getenv("DSL_SOFT_NOBLP")) {
        int b = get_blp();
        if (b != 4) { if (saved_blp < 0) SLOG("B: background loading priority %d -> High while hidden", b); saved_blp = b; set_blp(4); }
    }
    force_fps(10000);
}
static void fast_end(void) {
    if (!fast_on) return;
    fast_on = 0;
    if (saved_blp >= 0 && set_blp) set_blp(saved_blp);
    if (fast_saved && set_fps && set_vsync) { set_vsync(saved_vsync); set_fps(saved_fps); SLOG("B: frame cap %d vsync %d restored", saved_fps, saved_vsync); }
}

/* B mute while hidden: AudioListener.pause + volume 0 (icalls; the game's own volume / pause wanted values come back
   at the swap). The game fades AudioListener.volume itself (SoundManager.FadeIn/OutAudioListener): a non-zero value
   read here is the game's, kept for the swap. */
static float (*get_vol)(void); static void (*set_vol)(float); static int (*get_apause)(void); static void (*set_apause)(int);
static int mute_on, mute_saved_pause = -1; static float mute_saved_vol = -1;
static void mute_tick(void) {
    if (!mute_on) return;
    if (get_vol && set_vol) {
        float v = get_vol();
        if (v != 0) { if (mute_saved_vol < 0) SLOG("B: audio volume %.2f -> 0 while hidden", v); mute_saved_vol = v; set_vol(0); }
        else if (mute_saved_vol < 0) mute_saved_vol = 1;
    }
    if (get_apause && set_apause && !get_apause()) { if (mute_saved_pause < 0) { mute_saved_pause = 0; SLOG("B: audio paused while hidden"); } set_apause(1); }
}
static void mute_end(void) {
    if (!mute_on) return;
    mute_on = 0;
    if (set_apause && mute_saved_pause >= 0) set_apause(mute_saved_pause);
    if (set_vol && mute_saved_vol >= 0) set_vol(mute_saved_vol);
    SLOG("B: audio back (volume %.2f, pause %d)", mute_saved_vol, mute_saved_pause);
}

static int scene(void) { return (mf && off_scene >= 0) ? AT(mf, off_scene, int32_t) : -1; }
static int boot_ready(void) { return (mf && off_boot >= 0) ? AT(mf, off_boot, uint8_t) : 0; }
static int busy(void) {
    if (!mf) return 1;
    Obj sl = off_sl >= 0 ? AT(mf, off_sl, Obj) : NULL; if (sl && AT(sl, 0x10, int32_t) > 0) return 1;      /* SceneLoader._loadCount */
    Obj ts = off_ts >= 0 ? AT(mf, off_ts, Obj) : NULL; if (ts && AT(ts, 0x14, uint8_t)) return 1;          /* MapTransitionState.IsTransitionRunning */
    Obj lc = off_lc >= 0 ? AT(mf, off_lc, Obj) : NULL; if (lc && lc_loading && lc_loading(lc, NULL)) return 1;
    return 0;
}
static Obj speed_obj(void) { Obj s = (mf && off_set >= 0) ? AT(mf, off_set, Obj) : NULL; return s ? AT(s, 0x10, Obj) : NULL; }   /* CampaignSettingContainer.GameSpeedSetting */
static int game_speed(void) { Obj g = speed_obj(); return g ? AT(g, 0x10, int32_t) : -1; }
static void queue(Obj ev) { Obj eq = (mf && off_eq >= 0) ? AT(mf, off_eq, Obj) : NULL; if (eq && ev) eq_add(eq, ev, NULL); }
static void set_speed(int s) {
    if (!obj_new) return;
    if (s == 0) { Obj e = obj_new(k_chgspeed); ctor_chgspeed(e, 0, 1, NULL); queue(e); }        /* ChangeGameSpeedRequested(Pause, AbsolutePause) */
    else { Obj e = obj_new(k_setspeed); ctor_setspeed(e, s, NULL); queue(e); }
}

static void my_mf_awake(Obj self, const void *mi) { mf = self; SLOG("MainFlow.Awake"); o_mf_awake(self, mi); }
/* the live ScreenFadeEffectView: Awake runs for every copy (a scene may bring a duplicate that the singleton destroys);
   the one whose native object still exists (UnityEngine.Object.m_CachedPtr at 0x10) is used */
static uintptr_t fades[8]; static int nfades;                            /* strong GC handles (pointer size in this il2cpp): a destroyed copy stays readable */
static uintptr_t (*gc_new)(Obj, int); static Obj (*gc_get)(uintptr_t);
static void my_fade_awake(Obj self, const void *mi) {
    if (nfades < 8 && gc_new) fades[nfades++] = gc_new(self, 0);
    SLOG("ScreenFadeEffectView.Awake %p (#%d)", self, nfades);
    o_fade_awake(self, mi);
}
static Obj live_fade(void) {
    for (int i = nfades - 1; i >= 0; i--) { Obj o = gc_get(fades[i]); if (o && AT(o, 0x10, void *)) return o; }
    return NULL;
}

static void *meth(const char *ns, const char *k, const char *m, int argc) { return dsl_find_method_address(ns, k, m, argc); }

/* test only (DSL_SOFT_BOOTLOG=1, B): every step of MainFlow.Start (the game's boot coroutine) with time, Unity frame
   count and what it yields; Unity frame count every 0.5 s until the title */
static int boot_log; static int (*frame_count)(void);
static void *(*obj_class)(Obj); static const char *(*class_name)(void *);
static void *(*nested_types)(void *, void **);
static int (*o_start_mn)(Obj, const void *); static int32_t off_it_state = -1, off_it_cur = -1;
static int my_start_mn(Obj self, const void *mi) {
    int s0 = off_it_state >= 0 ? AT(self, off_it_state, int32_t) : -9; double t0 = since();
    int r = o_start_mn(self, mi);
    Obj cur = off_it_cur >= 0 ? AT(self, off_it_cur, Obj) : NULL;
    SLOG("boot step: MainFlow.Start state %d -> %d in %.1f ms, yields %s, frame %d", s0, off_it_state >= 0 ? AT(self, off_it_state, int32_t) : -9,
         (since() - t0) * 1000, cur && obj_class && class_name ? class_name(obj_class(cur)) : "null", frame_count ? frame_count() : -1);
    return r;
}
/* the same for every iterator nested in Refactor.Addressable.AddressableInitializer (LoadSheets, LoadFont, ...) */
#define NIT 8
static int (*o_it[NIT])(Obj, const void *); static const char *it_name[NIT]; static int32_t it_state[NIT], it_cur[NIT];
static int it_mn(int i, Obj self, const void *mi) {
    int s0 = it_state[i] >= 0 ? AT(self, it_state[i], int32_t) : -9; double t0 = since();
    int r = o_it[i](self, mi);
    Obj cur = it_cur[i] >= 0 ? AT(self, it_cur[i], Obj) : NULL;
    SLOG("boot step: %s state %d -> %d in %.1f ms, yields %s, frame %d", it_name[i], s0, it_state[i] >= 0 ? AT(self, it_state[i], int32_t) : -9,
         (since() - t0) * 1000, cur ? class_name(obj_class(cur)) : "null", frame_count ? frame_count() : -1);
    return r;
}
#define ITF(i) static int it_mn##i(Obj self, const void *mi) { return it_mn(i, self, mi); }
ITF(0) ITF(1) ITF(2) ITF(3) ITF(4) ITF(5) ITF(6) ITF(7)
static int (*const it_fn[NIT])(Obj, const void *) = { it_mn0, it_mn1, it_mn2, it_mn3, it_mn4, it_mn5, it_mn6, it_mn7 };
static void boot_log_install(void *kmf, void *(*icall)(const char *)) {
    frame_count = icall ? icall("UnityEngine.Time::get_frameCount") : NULL;
    obj_class = dsl_ga_sym("il2cpp_object_get_class"); class_name = dsl_ga_sym("il2cpp_class_get_name");
    nested_types = dsl_ga_sym("il2cpp_class_get_nested_types");
    void *it = NULL, *k, *ks = NULL;
    while (kmf && nested_types && class_name && (k = nested_types(kmf, &it))) if (!strncmp(class_name(k), "<Start>d__", 10)) ks = k;
    void *m = ks ? dsl_find_method(ks, "MoveNext", 0) : NULL, *a = m ? dsl_method_address(m) : NULL;
    if (ks) { off_it_state = dsl_field_offset(ks, "<>1__state"); off_it_cur = dsl_field_offset(ks, "<>2__current"); }
    int h = a ? dsl_hook(DSL_OWNER_LOADER, a, (void *)my_start_mn, (void **)&o_start_mn, "loader soft boot log MainFlow.Start") : -1;
    SLOG("boot log: MainFlow.Start iterator %s, hook %d, frame count %s", ks ? class_name(ks) : "NOT FOUND", h, frame_count ? "ok" : "MISSING");
    void *ka = dsl_find_class("Refactor.Addressable", "AddressableInitializer"); int n = 0; it = NULL;
    while (ka && n < NIT && (k = nested_types(ka, &it))) {
        const char *cn = class_name(k); if (cn[0] != '<') continue;
        void *mm = dsl_find_method(k, "MoveNext", 0), *aa = mm ? dsl_method_address(mm) : NULL; if (!aa) continue;
        it_name[n] = cn; it_state[n] = dsl_field_offset(k, "<>1__state"); it_cur[n] = dsl_field_offset(k, "<>2__current");
        if (!dsl_hook(DSL_OWNER_LOADER, aa, (void *)it_fn[n], (void **)&o_it[n], "loader soft boot log AddressableInitializer")) n++;
    }
    SLOG("boot log: %d AddressableInitializer iterator(s) hooked", n);
}
static void *klass(const char *ns, const char *k) { void *c = dsl_find_class(ns, k); if (!c) dsl_log("soft: class %s.%s not found", ns, k); return c; }
static int32_t foff(void *k, const char *f) { int32_t o = dsl_field_offset(k, f); if (o < 0) dsl_log("soft: field %s not found", f); return o; }

/* ------------------------------------------------------------------ small files */
static void path_of(char *out, size_t n, const char *what) { snprintf(out, n, "%s.%s", handoff_path, what); }
static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static void touch_file(const char *p, const char *text) { FILE *f = fopen(p, "w"); if (f) { if (text) fputs(text, f); fclose(f); } }
static char *slurp(const char *p) {
    if (!p) return NULL;
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (1 << 20)) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1); size_t r = fread(b, 1, (size_t)n, f); fclose(f); b[r] = 0; return b;
}
static int put_text(const char *p, const char *text) {          /* tmp + rename */
    char tmp[1400]; snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "wb"); if (!f) return -1;
    size_t n = strlen(text); int ok = fwrite(text, 1, n, f) == n; ok &= fclose(f) == 0;
    if (!ok || rename(tmp, p)) { unlink(tmp); return -1; }
    return 0;
}
static void flags_clear(int keep_ready) {
    char p[1300];
    for (const char *x = "ready\0go\0shown\0failed\0"; *x; x += strlen(x) + 1) {
        if (keep_ready && !strcmp(x, "ready")) continue;
        path_of(p, sizeof p, x); unlink(p);
    }
}

/* ------------------------------------------------------------------ per-frame window log + capture */
static void frame_log(const char *who) {
    if (!ff) return;
    fprintf(ff, "%.3f %s scene %d busy %d hidden %d", since(), who, scene(), mf ? busy() : -1, hidden);
    dsl_sos_frame_log(ff);
    fputc('\n', ff); fflush(ff);
}
static void capture(const char *who) {
    if (!cap_on || !cap_tex || !enc_png || !dsl_ready || !mf) return;
    double t = since(); if (t - cap_last < 0.1) return; cap_last = t;
    Obj tex = cap_tex(NULL); if (!tex) return;
    Obj png = enc_png(tex, NULL); if (obj_destroy) obj_destroy(tex, NULL);
    if (!png) return;
    size_t n = (size_t)AT(png, 0x18, uintptr_t);
    char p[1400]; snprintf(p, sizeof p, "%s/%s_%04d_%09.3f.png", cap_dir, who, cap_n++, t_start + t);
    FILE *f = fopen(p, "wb"); if (f) { fwrite((char *)png + 0x20, 1, n, f); fclose(f); }
}

/* test: the game's own full frame (UI included) to a PNG file; main thread, between frames */
int dsl_soft_shot(const char *path) {
    if (!cap_tex) { cap_tex = meth("UnityEngine", "ScreenCapture", "CaptureScreenshotAsTexture", 0);
                    enc_png = meth("UnityEngine", "ImageConversion", "EncodeToPNG", 1); obj_destroy = meth("UnityEngine", "Object", "Destroy", 1); }
    if (!cap_tex || !enc_png || !dsl_ready) return -1;
    Obj tex = cap_tex(NULL); if (!tex) return -1;
    Obj png = enc_png(tex, NULL); if (obj_destroy) obj_destroy(tex, NULL);
    if (!png) return -1;
    size_t n = (size_t)AT(png, 0x18, uintptr_t);
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    fwrite((char *)png + 0x20, 1, n, f); fclose(f); return 0;
}

/* ------------------------------------------------------------------ A side */
enum { A_IDLE, A_WAIT_READY, A_WAIT_SHOWN, A_QUITTING, A_FAILED };
static int a_state = A_IDLE; static double a_t0;
static char a_save[512]; static int a_scene, a_fade_seen_open; static double a_ready_seen;

static void a_fail(const char *why) {
    SLOG("A: soft restart FAILED: %s; the old mods stay", why);
    dsl_sos_child_kill();
    if (fade && fade_in) fade_in(fade, NULL);
    char p[1300]; path_of(p, sizeof p, "failed"); touch_file(p, why);
    a_state = A_FAILED; cap_on = 0;
}

static void jesc(char *out, size_t n, const char *s) {    /* JSON string body (Windows paths carry backslashes) */
    size_t k = 0;
    for (; s && *s && k + 3 < n; s++) { if (*s == '"' || *s == '\\') out[k++] = '\\'; out[k++] = *s; }
    out[k] = 0;
}
static void write_handoff(const char *profile, int prewarm, double apply_at) {
    char jp[2400], js[1100]; jesc(jp, sizeof jp, profile); jesc(js, sizeof js, a_save);
    double f[4] = { 0, 0, 0, 0 }; dsl_sos_game_frame(f);
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", handoff_path);
    FILE *o = fopen(tmp, "w"); if (!o) return;
    fprintf(o, "{\"v\":2,\"from_pid\":%d,\"profile\":\"%s\",\"scene\":%d,\"save\":\"%s\",\"speed\":-1,\"prewarm\":%d,"
               "\"frame\":[%.1f,%.1f,%.1f,%.1f],\"apply_at\":%.3f}\n",
            (int)getpid(), jp, a_scene, js, prewarm, f[0], f[1], f[2], f[3], apply_at);
    fclose(o); rename(tmp, handoff_path);
    SLOG("A: handoff written: scene %d save '%s' prewarm %d frame %.0f,%.0f %.0fx%.0f", a_scene, a_save, prewarm, f[0], f[1], f[2], f[3]);
}
/* B's "ready" file holds B's pid: a ready from an older (stopped) B never counts */
static int ready_ok(void) {
    char p[1300]; path_of(p, sizeof p, "ready");
    char *t = slurp(p); if (!t) return 0;
    long pid = atol(t); free(t);
    return pid > 0 && pid == dsl_sos_child_pid();
}

/* ------------------------------------------------------------------ prewarm (A) */
enum { PW_NONE, PW_BOOTING, PW_READY };
static int pw_state; static double pw_t0;
static char *pw_content, *pw_env; static char pw_profile[1300];
static void pw_clear(void) { free(pw_content); pw_content = NULL; free(pw_env); pw_env = NULL; pw_state = PW_NONE; }
static void pw_stop(const char *why) {
    if (pw_state == PW_NONE) return;
    SLOG("A: prewarm B pid %ld stopped: %s", dsl_sos_child_pid(), why);
    dsl_sos_child_kill();
    pw_clear(); flags_clear(0);
}
/* the snapshot the prewarm B loads: next to the given profile (same data folder), "prewarm-<name>"; pending.json ->
   prewarm-pending.json, so B still turns it into active.json once it is on screen (dsl_reg.c commit_pending) */
static int pw_snapshot(const char *profile, const char *text, char *out, size_t n) {
    const char *sl = strrchr(profile, '/'); const char *bs = strrchr(profile, '\\'); if (bs > sl) sl = bs;
    if (sl) snprintf(out, n, "%.*sprewarm-%s", (int)(sl - profile + 1), profile, sl + 1);
    else snprintf(out, n, "prewarm-%s", profile);
    return put_text(out, text);
}
static int pw_alive(void) {                           /* 0 = no prewarm B (any that ended by itself is cleared) */
    if (pw_state != PW_NONE && dsl_sos_child_exited()) { SLOG("A: prewarm B ended by itself"); pw_clear(); flags_clear(0); }
    return pw_state != PW_NONE;
}
static int pw_same(const char *profile, const char *env_lines) {
    if (pw_state == PW_NONE || !pw_content) return 0;
    char *t = slurp(profile); int same = t && !strcmp(t, pw_content) && !strcmp(env_lines ? env_lines : "", pw_env ? pw_env : "");
    free(t); return same;
}
static int refuse_common(const char *what) {
    if (role_b) { dsl_log("soft: %s refused, this process is still being handed over", what); return -1; }
    if (a_state != A_IDLE && a_state != A_FAILED) { dsl_log("soft: %s refused, a soft restart already runs", what); return -1; }
    if (!dsl_ready || !mf || !fade || !obj_new) { dsl_log("soft: %s refused, game not ready (mf %p fade %p)", what, mf, fade); return -1; }
    int sc = scene();
    if (sc != 1) { dsl_log("soft: %s refused: mods change only at the title (scene %d)", what, sc); return -1; }
    if (busy()) { dsl_log("soft: %s refused, the title is still loading", what); return -1; }
    return 0;
}
int dsl_soft_prewarm(const char *profile, const char *env_lines) {
    if (refuse_common("prewarm")) return -1;
    if (!profile || !*profile) { dsl_log("soft: prewarm refused, no profile"); return -1; }
    if (env_lines && strstr(env_lines, "DSL_SOFT_LOAD=")) { dsl_log("soft: prewarm refused, a set that opens a save is not prewarmed"); return -1; }
    if (pw_alive() && pw_same(profile, env_lines)) { SLOG("A: prewarm: same set as the waiting B (state %d): kept", pw_state); return 0; }
    pw_stop("replaced by a newer set");
    char *text = slurp(profile);
    if (!text) { dsl_log("soft: prewarm refused, profile %s not readable", profile); return -1; }
    if (pw_snapshot(profile, text, pw_profile, sizeof pw_profile)) { dsl_log("soft: prewarm refused, cannot write %s", pw_profile); free(text); return -1; }
    flags_clear(0);
    a_scene = 1; a_save[0] = 0;
    write_handoff(pw_profile, 1, 0);
    if (dsl_sos_spawn(pw_profile, handoff_path, env_lines ? env_lines : "")) { dsl_log("soft: prewarm: B could not start"); free(text); return -1; }
    pw_content = text; pw_env = strdup(env_lines ? env_lines : "");
    pw_state = PW_BOOTING; pw_t0 = since();
    SLOG("A: PREWARM profile %s (snapshot %s): B pid %ld booting hidden", profile, pw_profile, dsl_sos_child_pid());
    return 0;
}
void dsl_soft_prewarm_stop(void) { pw_stop("stopped by the caller"); }
int dsl_soft_prewarm_state(void) { return pw_state; }
static void pw_tick(void) {
    if (pw_state == PW_NONE) { dsl_sos_child_exited(); return; }       /* also reaps a stopped B (no zombie) */
    if (!pw_alive()) return;
    if (scene() == 2) { pw_stop("A entered a world"); return; }
    double age = since() - pw_t0;
    if (pw_state == PW_BOOTING) {
        if (ready_ok()) { pw_state = PW_READY; SLOG("A: prewarm B ready and waiting at the title, hidden (%.3f s after prewarm)", age); }
        else if (age > env_num("DSL_SOFT_TIMEOUT", 90) + 30) pw_stop("not ready in time");
    }
    if (pw_state != PW_NONE && age > pw_max()) pw_stop("waited the maximum time");
}

/* ------------------------------------------------------------------ apply (A) */
int dsl_soft_wanted(void) { return role_b; }
int dsl_soft_busy(void) { return role_b || (a_state != A_IDLE && a_state != A_FAILED); }    /* a waiting prewarm B is not "busy" for A */
void *dsl_soft_mainflow(void) { return mf; }
int dsl_soft_apply(const char *profile, const char *env_lines) {
    if (refuse_common("apply")) return -1;
    a_save[0] = 0;
    const char *ld = env_lines ? strstr(env_lines, "DSL_SOFT_LOAD=") : NULL;   /* flow K: restart straight into a given save */
    if (ld && (ld == env_lines || ld[-1] == '\n')) {
        ld += 14; size_t n = strcspn(ld, "\n"); if (n >= sizeof a_save) n = sizeof a_save - 1;
        memcpy(a_save, ld, n); a_save[n] = 0;
    }
    a_scene = a_save[0] ? 2 : 1;
    int use_pw = pw_alive() && pw_same(profile, env_lines);
    if (!use_pw && pw_state != PW_NONE) pw_stop("the applied set is not the prewarmed one");
    a_t0 = since(); a_fade_seen_open = 0; a_ready_seen = 0;
    flags_clear(use_pw);                                                  /* a prewarmed B's "ready" stays */
    if (getenv("DSL_SOFT_CAPTURE")) cap_on = 1;
    SLOG("A: APPLY profile %s (title%s%s)", profile ? profile : "", a_save[0] ? ", B opens " : "", a_save);
    fade_out(fade, NULL, 1, NULL);                                        /* the game's loading screen */
    if (fade_done && !fade_done(fade, NULL)) a_fade_seen_open = 1;
    if (use_pw) {
        SLOG("A: the prewarmed B pid %ld is used (%s)", dsl_sos_child_pid(), pw_state == PW_READY ? "ready" : "still booting");
        write_handoff(pw_profile, 1, t_start + a_t0);                     /* frame + apply time of now; B reads it at "go" */
        pw_clear();
    } else {
        write_handoff(profile ? profile : "", 0, t_start + a_t0);        /* B starts now */
        if (dsl_sos_spawn(profile ? profile : "", handoff_path, env_lines ? env_lines : "")) { a_fail("could not start B"); return -1; }
        SLOG("A: B started, pid %ld", dsl_sos_child_pid());
    }
    a_state = A_WAIT_READY;
    return 0;
}

static void a_tick(void) {
    char p[1300];
    double t = since();
    if (a_state == A_IDLE || a_state == A_FAILED) { pw_tick(); return; }
    if (a_state == A_QUITTING) return;
    capture("A");
    if (dsl_sos_child_exited()) { a_fail("B exited before the swap"); return; }
    if (t - a_t0 > env_num("DSL_SOFT_TIMEOUT", 90)) { a_fail("timeout"); return; }
    Obj fv = fade; int up = fv && fade_done && fade_done(fv, NULL);
    if (!up) a_fade_seen_open = 1;
    if (a_state == A_WAIT_READY) {
        static double last_note;
        int rd = ready_ok();
        if (t - last_note > 5) { last_note = t; SLOG("A: waiting: B ready %d, loading screen up %d", rd, up); }
        if (!rd) return;
        if (!a_ready_seen) a_ready_seen = t;
        /* A's own loading screen must be up (its done flag seen low after the call, or 0.4 s). When it never reports
           done (Windows -batchmode test copy), the swap goes on 3 s after B is ready instead of failing at the timeout */
        int open_ok = a_fade_seen_open || t - a_t0 >= 0.4;
        if (!(up && open_ok) && t - a_ready_seen <= 3) return;
        if (!(up && open_ok)) SLOG("A: loading screen not reported up 3 s after B was ready: swap anyway");
        SLOG("A: B ready (%.3f s after apply)", t - a_t0);
        if (!no_activate) dsl_sos_yield();                                 /* activation is given, not taken */
        path_of(p, sizeof p, "go"); touch_file(p, NULL);
        a_state = A_WAIT_SHOWN; return;
    }
    if (a_state == A_WAIT_SHOWN) {
        path_of(p, sizeof p, "shown"); if (!exists(p)) return;
        SLOG("A: B shown: A orders its windows out and quits (%.3f s after apply)", t - a_t0);
        dsl_sos_order_out();
        frame_log("A");
        a_state = A_QUITTING; cap_on = 0;
        if (app_quit) app_quit(NULL); else exit(0);
    }
}

/* ------------------------------------------------------------------ B side */
enum { B_BOOT, B_LOADING, B_SETTLE, B_FADING, B_WAIT_GO, B_SHOWN, B_DONE };
static int b_state = B_BOOT; static double b_tstate; static int b_frames_ok, b_load_speed = -1, b_fade_seen_open;
static void b_read_handoff(void) {                                     /* at "go": A's frame and apply time of now */
    char *txt = slurp(handoff_path); JNode *j = txt ? json_parse(txt) : NULL; free(txt);
    if (!j) { SLOG("B: handoff not readable at go: the first one is kept"); return; }
    json_free(hand); hand = j;
    JNode *f = json_get(hand, "frame"); int k = 0; double fr[4] = { 0, 0, 0, 0 };
    if (f && f->t == JN_ARR) for (JNode *c = f->child; c && k < 4; c = c->next) fr[k++] = c->num;
    if (k == 4 && fr[2] > 0) { memcpy(a_frame, fr, sizeof fr); a_has_frame = 1; }
}
static void b_fade_start(double t) {
    if (fade) fade_out(fade, NULL, 1, NULL);
    b_fade_seen_open = fade && fade_done && !fade_done(fade, NULL);
    b_state = B_FADING; b_tstate = t;
}
static void b_quit(const char *why) {
    SLOG("B: %s: B quits", why);
    b_state = B_DONE; if (app_quit && mf) app_quit(NULL); else exit(0);
}
static void b_tick(void) {
    char p[1300]; double t = since();
    if (b_state == B_DONE) return;
    capture("B");
    if (dsl_ready && b_state < B_FADING) fast_tick();
    if (dsl_ready && hidden) mute_tick();
    double lim = b_prewarm ? pw_max() + 60 : env_num("DSL_SOFT_TIMEOUT", 90) + 30;
    if (b_state < B_SHOWN && (t > lim || (dsl_ready && !obj_new))) { b_quit(obj_new ? "cannot finish the handover in time" : "soft restart off"); return; }
    if (!dsl_ready || !mf) return;
    if (!dsl_sos_pid_alive((long)(json_get(hand, "from_pid") ? json_get(hand, "from_pid")->num : 0)) && b_state < B_SHOWN) { b_quit("A is gone before the swap"); return; }
    int sc = scene();
    if (b_state == B_BOOT) {
        static double last_bl;
        if (boot_log && t - last_bl >= 0.5) { last_bl = t; SLOG("boot: frame %d scene %d bootReady %d busy %d", frame_count ? frame_count() : -1, sc, boot_ready(), busy()); }
        if (sc != 1 || !boot_ready() || busy()) return;
        SLOG("B: title ready (frame %d)", frame_count ? frame_count() : -1);
        const char *save = json_str(hand, "save");
        if (save && *save && json_get(hand, "scene") && json_get(hand, "scene")->num == 2) {
            Obj e = obj_new(k_load); ctor_load(e, str_new(save), NULL); queue(e);
            SLOG("B: LoadCampaignRequested %s", save); b_state = B_LOADING; b_tstate = t;
        } else b_fade_start(t);                                           /* title: nothing to settle, the loading screen at once */
        return;
    }
    if (b_state == B_LOADING) {                                           /* flow K: the world of the given save */
        if (sc != 2 || busy() || (fade_loading && fade_loading(fade, NULL))) { b_frames_ok = 0; return; }
        if (++b_frames_ok < 10) return;
        b_load_speed = game_speed(); set_speed(0);
        SLOG("B: world loaded, paused until the swap (speed was %d)", b_load_speed);
        b_state = B_SETTLE; b_tstate = t; return;
    }
    if (b_state == B_SETTLE) {
        if (t - b_tstate < 0.3) return;                                   /* a few frames for the world view */
        b_fade_start(t); return;
    }
    if (b_state == B_FADING) {
        int up = !fade || !fade_done || fade_done(fade, NULL);
        if (!up) b_fade_seen_open = 1;
        if (!up || (!b_fade_seen_open && t - b_tstate < 0.4)) return;
        char pid[32]; snprintf(pid, sizeof pid, "%d\n", (int)getpid());
        path_of(p, sizeof p, "ready"); touch_file(p, pid);
        SLOG("B: ready (loading screen up, window still hidden, %.3f s after the fade started)%s", t - b_tstate, b_prewarm ? ": prewarmed, waiting for an apply" : "");
        b_state = B_WAIT_GO; b_tstate = t; return;
    }
    if (b_state == B_WAIT_GO) {
        /* the game fades its title in by itself 2-3 s after the title is ready: a waiting B puts its loading screen
           back up, and shows only with it up (at most 1.5 s after "go") */
        static double refade_at, go_seen;
        int up = !fade || !fade_done || fade_done(fade, NULL);
        if (b_prewarm) force_fps((int)env_num("DSL_SOFT_IDLE_FPS", 10));  /* waiting at the title: little CPU / GPU */
        if (!up && t - refade_at > 1.0) { refade_at = t; if (fade) fade_out(fade, NULL, 1, NULL); SLOG("B: the game took the loading screen down: up again"); }
        path_of(p, sizeof p, "go"); if (!exists(p)) return;
        if (!go_seen) go_seen = t;
        if (!up && t - go_seen < 1.5) { force_fps(10000); return; }      /* the fade runs at once, uncapped */
        if (!up) SLOG("B: loading screen not up 1.5 s after go: shown anyway");
        b_read_handoff();
        hidden = 0;
        fast_end();
        mute_end();
        dsl_sos_show(a_has_frame ? a_frame : NULL, !no_activate);
        frame_log("B");
        path_of(p, sizeof p, "shown"); touch_file(p, NULL);
        double at = json_get(hand, "apply_at") ? json_get(hand, "apply_at")->num : 0;
        SLOG("B: SHOWN (%.3f s after apply in A%s)", at > 0 ? now_s() - at : -1.0, b_prewarm ? ", prewarmed" : "");
        b_state = B_SHOWN; b_tstate = t; return;
    }
    if (b_state == B_SHOWN) {
        if (t - b_tstate < 0.05) return;                                  /* one frame on screen with the loading screen */
        if (fade && fade_in) fade_in(fade, NULL);
        int sp = b_load_speed;                                            /* flow K: the speed the game loaded the save with */
        if (sc == 2 && sp > 0) set_speed(sp);
        SLOG("B: fade in, speed %d", sp);
        b_state = B_DONE; b_tstate = t;
        role_b = 0; a_state = A_IDLE;                                     /* from now on a normal game: it may soft restart itself */
    }
}

/* ------------------------------------------------------------------ main-thread tick (between Unity frames) */
static double ff_until, ff_last;
static void test_requests(double t) {                                    /* test: apply / prewarm by file */
    static double last;
    if (t - last < 0.5) return; last = t;
    char p[1300]; snprintf(p, sizeof p, "%s/prewarm_request.json", sdir);
    char *txt = slurp(p);
    if (txt) {
        unlink(p);
        JNode *j = json_parse(txt); free(txt);
        if (j && json_get(j, "stop")) { dsl_soft_prewarm_stop(); dsl_log("soft test: prewarm stop"); }
        else {
            const char *pr = j ? json_str(j, "profile") : NULL;
            int r = pr ? dsl_soft_prewarm(pr, json_str(j, "env")) : -1;
            dsl_log("soft test: prewarm %s -> %d", pr ? pr : "(no profile)", r);
        }
        json_free(j);
    }
    snprintf(p, sizeof p, "%s/apply_request.json", sdir);
    txt = slurp(p); if (!txt) return;
    unlink(p);
    JNode *j = json_parse(txt); free(txt);
    const char *pr = j ? json_str(j, "profile") : NULL;
    if (!pr) dsl_log("soft: apply_request.json without profile");
    else { const char *e = json_str(j, "env"); int r = dsl_soft_apply(pr, e && *e ? e : NULL); dsl_log("soft test: apply %s -> %d", pr, r); }
    json_free(j);
}
static void tick(void) {
    if (stage_on) dsl_sos_stage_check();                                 /* test stage: a window off the stage is never seen */
    if (role_b) b_tick(); else a_tick();
    double t = since();
    int want = hidden || a_state != A_IDLE || t < ff_until || (role_b && b_state != B_DONE);
    if (want && role_b && b_prewarm && b_state == B_WAIT_GO && t - ff_last < 1.0) want = 0;   /* a waiting B: once a second */
    if (ff && want) { frame_log(role_b ? "B" : "A"); ff_last = t; }
    if (b_state == B_DONE && ff_until == 0) ff_until = t + 3;
    if (b_state == B_DONE && t < ff_until && a_state == A_IDLE) capture("B");   /* test: the first seconds after the swap */
    if (test_mode && !role_b && (a_state == A_IDLE || a_state == A_FAILED)) test_requests(t);
}

/* ------------------------------------------------------------------ install */
/* constructor time, BEFORE dsload edits DYLD_INSERT_LIBRARIES */
void dsl_soft_snapshot(const char *logdir) {
    t_start = dsl_sos_proc_start();
    dsl_sos_snapshot();
    snprintf(sdir, sizeof sdir, "%s/softrestart", logdir); mkdir(sdir, 0755);
    snprintf(handoff_path, sizeof handoff_path, "%s/handoff.json", sdir);
    const char *h = getenv("DSL_SOFT_HANDOFF");
    test_mode = getenv("DSL_SOFT_TEST") && *getenv("DSL_SOFT_TEST") == '1';
    no_activate = getenv("DSL_SOFT_NOACTIVATE") && *getenv("DSL_SOFT_NOACTIVATE") == '1';
    const char *stg = getenv("DSL_SOFT_STAGE");
    if (stg && sscanf(stg, "%lf,%lf", &stage_x, &stage_y) == 2) stage_on = 1;
    const char *cd = getenv("DSL_SOFT_CAPTURE");
    if (cd && *cd) { snprintf(cap_dir, sizeof cap_dir, "%s", cd); mkdir(cap_dir, 0755); }
    if (h && *h) {
        snprintf(handoff_path, sizeof handoff_path, "%s", h);
        char *txt = slurp(h); hand = txt ? json_parse(txt) : NULL; free(txt);
        if (!hand) { dsl_log("soft: handoff %s not readable: normal start", h); }
        else {
            role_b = 1; hidden = 1; mute_on = 1; if (cap_dir[0]) cap_on = 1;
            b_prewarm = json_get(hand, "prewarm") && json_get(hand, "prewarm")->num != 0;
            JNode *f = json_get(hand, "frame"); int k = 0;
            if (f && f->t == JN_ARR) for (JNode *c = f->child; c && k < 4; c = c->next) a_frame[k++] = c->num;
            a_has_frame = (k == 4 && a_frame[2] > 0);
            dsl_sos_unsetenv("DSL_SOFT_HANDOFF");                           /* B's own later restarts start fresh */
        }
    }
    dsl_sos_windows_install(role_b, stage_on, stage_x, stage_y);
    char fp[1300]; snprintf(fp, sizeof fp, "%s/softrestart_frames_%s_%d.log", sdir, role_b ? "B" : "A", (int)getpid());
    if (role_b || test_mode) ff = fopen(fp, "w");
    dsl_log("soft: role %s%s%s%s%s, process started %.3f s ago", role_b ? "B (handoff " : "A", role_b ? handoff_path : "", role_b ? ")" : "",
            b_prewarm ? ", prewarm" : "", stage_on ? ", test stage" : "", since());
    dsl_sos_timer(tick);
}

/* after il2cpp_init: names -> addresses; hooks on MainFlow.Awake and ScreenFadeEffectView.Awake (object capture only) */
void dsl_soft_install(void) {
    obj_new = dsl_ga_sym("il2cpp_object_new"); str_new = dsl_ga_sym("il2cpp_string_new");
    gc_new = dsl_ga_sym("il2cpp_gchandle_new"); gc_get = dsl_ga_sym("il2cpp_gchandle_get_target");
    void *kmf = klass("Refactor.Main", "MainFlow");
    if (kmf) {
        off_scene = foff(kmf, "_currentScene"); off_boot = foff(kmf, "_bootReady"); off_eq = foff(kmf, "_eventQueue");
        off_set = foff(kmf, "_campaignSetting"); off_sl = foff(kmf, "_sceneLoader"); off_ts = foff(kmf, "_mapTransitionState");
        off_lc = foff(kmf, "_mapLoadingChecker");
    }
    eq_add = meth("Refactor.Main", "EventQueue", "AddEvent", 1);
    lc_loading = meth("Refactor.Main", "MapLoadingChecker", "get_IsLoading", 0);
    k_load = klass("Refactor.Main.Event", "LoadCampaignRequested"); ctor_load = meth("Refactor.Main.Event", "LoadCampaignRequested", ".ctor", 1);
    k_setspeed = klass("Refactor.Main.Event", "SetGameSpeedRequested"); ctor_setspeed = meth("Refactor.Main.Event", "SetGameSpeedRequested", ".ctor", 1);
    k_chgspeed = klass("Refactor.Main.Event", "ChangeGameSpeedRequested"); ctor_chgspeed = meth("Refactor.Main.Event", "ChangeGameSpeedRequested", ".ctor", 2);
    fade_out = meth("Refactor.UI", "ScreenFadeEffectView", "AnimateFadeOut", 2); fade_in = meth("Refactor.UI", "ScreenFadeEffectView", "AnimateFadeIn", 0);
    fade_done = meth("Refactor.UI", "ScreenFadeEffectView", "get_IsFadeOutCompleted", 0);
    fade_loading = meth("Refactor.UI", "ScreenFadeEffectView", "get_IsLoading", 0);
    app_quit = meth("UnityEngine", "Application", "Quit", 0);
    void *(*icall)(const char *) = dsl_ga_sym("il2cpp_resolve_icall");
    if (role_b && icall) {
        get_vol = icall("UnityEngine.AudioListener::get_volume"); set_vol = icall("UnityEngine.AudioListener::set_volume");
        get_apause = icall("UnityEngine.AudioListener::get_pause"); set_apause = icall("UnityEngine.AudioListener::set_pause");
        SLOG("B: mute while hidden: volume %s, pause %s", get_vol && set_vol ? "ok" : "MISSING", get_apause && set_apause ? "ok" : "MISSING");
    }
    if (role_b && !getenv("DSL_SOFT_NOFAST")) {
        fast_on = 1;
        if (icall) { splash_done = icall("UnityEngine.Rendering.SplashScreen::get_isFinished"); splash_cancel = icall("UnityEngine.Rendering.SplashScreen::CancelSplashScreen"); }
        if (icall) {
            set_fps = icall("UnityEngine.Application::set_targetFrameRate"); get_fps = icall("UnityEngine.Application::get_targetFrameRate");
            set_vsync = icall("UnityEngine.QualitySettings::set_vSyncCount"); get_vsync = icall("UnityEngine.QualitySettings::get_vSyncCount");
            set_blp = icall("UnityEngine.Application::set_backgroundLoadingPriority"); get_blp = icall("UnityEngine.Application::get_backgroundLoadingPriority");
        }
        SLOG("B: boot speed-up: splash %s, frame cap %s", splash_done && splash_cancel ? "ok" : "MISSING", set_fps && set_vsync && get_fps && get_vsync ? "ok" : "MISSING");
    }
    if (role_b && getenv("DSL_SOFT_BOOTLOG") && *getenv("DSL_SOFT_BOOTLOG") == '1') { boot_log = 1; boot_log_install(kmf, icall); }
    if (cap_dir[0]) {
        cap_tex = meth("UnityEngine", "ScreenCapture", "CaptureScreenshotAsTexture", 0);
        enc_png = meth("UnityEngine", "ImageConversion", "EncodeToPNG", 1); obj_destroy = meth("UnityEngine", "Object", "Destroy", 1);
    }
    int miss = !gc_new || !gc_get || !obj_new || !str_new || !eq_add || !k_load || !ctor_load || !fade_out || !fade_in || !fade_done
               || off_scene < 0 || off_eq < 0 || !k_setspeed || !ctor_setspeed || !k_chgspeed || !ctor_chgspeed;
    void *a1 = meth("Refactor.Main", "MainFlow", "Awake", 0), *a2 = meth("Refactor.UI", "ScreenFadeEffectView", "Awake", 0);
    int h1 = a1 ? dsl_hook(DSL_OWNER_LOADER, a1, (void *)my_mf_awake, (void **)&o_mf_awake, "loader soft MainFlow.Awake") : -1;
    int h2 = a2 ? dsl_hook(DSL_OWNER_LOADER, a2, (void *)my_fade_awake, (void **)&o_fade_awake, "loader soft ScreenFadeEffectView.Awake") : -1;
    if (miss || h1 || h2) { dsl_log("soft: game functions missing (%d) or hooks refused (%d %d): soft restart OFF", miss, h1, h2); obj_new = NULL; return; }
    SLOG("soft restart ready (%s)", role_b ? (b_prewarm ? "B prewarm: waiting for the title" : "B: waiting for the title") : "A");
}
