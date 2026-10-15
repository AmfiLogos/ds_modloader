// Soft restart (~/ds_multiplayer/research/plan_modloader.md section 2): apply another mod set without a visible restart.
// A = the running game, B = a new game process started with the new mod set (same executable, same environment
// and arguments; DSL_PROFILE = the new profile). The player only sees the game's own loading screen.
//
// A  dsl_soft_restart(profile)   [in a world: pause, the game's own autosave]  the game's loading screen
//    (ScreenFadeEffectView.AnimateFadeOut with LoadingUI)  writes <log dir>/softrestart/handoff.json  starts B.
//    B "ready"  -> A yields activation to B and writes "go".  B "shown" -> A orders its windows out and quits.
//    No "ready" within DSL_SOFT_TIMEOUT s (default 90) or B died: A kills B, fades back in, keeps its mods.
// B  DSL_SOFT_HANDOFF=<handoff.json>: from its first NSWindow on, every window is invisible (alpha 0, lowest level,
//    no mouse, never key/main), the app never activates and has no Dock icon (Accessory policy). At the title:
//    in a world case LoadCampaignRequested(<save>) (the game's own Load path), then pause until the swap.
//    Settled -> the same loading screen (fade out), "ready". "go" -> window on A's frame, normal level, alpha 1,
//    front, Regular policy, activate; "shown"; game speed back to A's; fade in.
// Test only (never in a player's start): DSL_SOFT_TEST=1 (apply by the file <log dir>/softrestart/apply_request.json),
// DSL_SOFT_STAGE=x,y (every window of A and B is kept at that origin, e.g. far off every display), DSL_SOFT_NOACTIVATE=1
// (no activation, no yield), DSL_SOFT_CAPTURE=<dir> (the game's own frame as PNG ~10 per second while a restart runs).
// Every step goes to dsload.log with the time since the process started; window state per frame to softrestart_frames.log.
// OS-neutral since the core/OS split: windows, process start and the main-thread timer are in dsl_soft_os.h
// (start/mac/dsl_soft_mac.c, start/win/dsl_soft_win.c); everything here uses the game by il2cpp names.
#include "dsl_internal.h"
#include "dsl_soft_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

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
static int test_mode, no_activate, stage_on; static double stage_x, stage_y;
static char cap_dir[1024]; static int cap_on; static double cap_last; static int cap_n;
static char handoff_path[1200];
static JNode *hand;                                  /* B: parsed handoff */
static double a_frame[4]; static int a_has_frame;

/* ------------------------------------------------------------------ game access (names, resolved after il2cpp_init) */
static Obj mf;
#define fade live_fade()                                 /* MainFlow, ScreenFadeEffectView (from their Awake) */
static void (*o_mf_awake)(Obj, const void *), (*o_fade_awake)(Obj, const void *);
static int32_t off_scene = -1, off_boot = -1, off_eq = -1, off_set = -1, off_sl = -1, off_ts = -1, off_lc = -1;
static void *(*obj_new)(void *klass); static void *(*str_new)(const char *);
static void (*eq_add)(Obj, Obj, const void *);
static void *k_load, *k_autosave, *k_setspeed, *k_chgspeed;
static void (*ctor_load)(Obj, Obj, const void *), (*ctor_autosave)(Obj, const void *);
static void (*ctor_setspeed)(Obj, int, const void *), (*ctor_chgspeed)(Obj, int, int, const void *);
static int (*lc_loading)(Obj, const void *);
static void (*fade_out)(Obj, Obj, int, const void *), (*fade_in)(Obj, const void *);
static int (*fade_done)(Obj, const void *), (*fade_loading)(Obj, const void *);
static void (*fade_stage)(Obj, Obj, const void *), (*fade_progress)(Obj, float, const void *);
static void (*app_quit)(const void *);
static Obj (*cam_main)(const void *); static Obj (*comp_tf)(Obj, const void *);
typedef struct { float x, y, z; } V3;
static V3 (*tf_pos)(Obj, const void *); static float (*cam_ortho)(Obj, const void *);
static V3 (*cs_getpos)(Obj, const void *); static void (*cs_setpos)(Obj, V3, const void *); static float (*cs_getortho)(Obj, const void *);
static Obj (*cap_tex)(const void *); static Obj (*enc_png)(Obj, const void *); static void (*obj_destroy)(Obj, const void *);

/* B boot speed: the Unity splash is cancelled and frames are uncapped while hidden (the boot is frame bound:
   one loading step per frame), then the game's own frame cap and vsync come back at the swap */
static int (*splash_done)(void); static void (*splash_cancel)(void);
static void (*set_fps)(int); static int (*get_fps)(void);              /* icalls (the managed getters are stripped) */
static void (*set_vsync)(int); static int (*get_vsync)(void);
static void (*set_blp)(int); static int (*get_blp)(void);              /* Application.backgroundLoadingPriority */
static int fast_on, fast_saved, saved_fps, saved_vsync, splash_seen, saved_blp = -1;
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
    if (set_fps && get_fps && set_vsync && get_vsync) {
        int f = get_fps(), v = get_vsync();
        if (f != 10000 || v != 0) {                                       /* the game (re)applies its settings at boot */
            if (!fast_saved) { fast_saved = 1; saved_fps = f; saved_vsync = v; SLOG("B: frame cap %d vsync %d -> uncapped while hidden", f, v); }
            else if (f != 10000) saved_fps = f;
            if (v != 0) saved_vsync = v;
            set_vsync(0); set_fps(10000);
        }
    }
}
static void fast_end(void) {
    if (!fast_on) return;
    fast_on = 0;
    if (saved_blp >= 0 && set_blp) set_blp(saved_blp);
    if (fast_saved && set_fps && set_vsync) { set_vsync(saved_vsync); set_fps(saved_fps); SLOG("B: frame cap %d vsync %d restored", saved_fps, saved_vsync); }
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
static Obj cam_setting(void) { Obj s = (mf && off_set >= 0) ? AT(mf, off_set, Obj) : NULL; return s ? AT(s, 0x18, Obj) : NULL; }  /* CameraSetting */
/* the camera target the game saves (CameraSetting) */
static int cam_target(float *x, float *y, float *z) {
    Obj c = cam_setting(); if (!c || !cs_getpos) return 0;
    V3 p = cs_getpos(c, NULL); *x = p.x; *y = p.y; *z = cs_getortho ? cs_getortho(c, NULL) : 0; return 1;
}
static int camera(float *x, float *y, float *z) {
    Obj c = cam_main ? cam_main(NULL) : NULL; if (!c || !comp_tf || !tf_pos) return 0;
    Obj t = comp_tf(c, NULL); if (!t) return 0;
    V3 p = tf_pos(t, NULL); *x = p.x; *y = p.y; *z = cam_ortho ? cam_ortho(c, NULL) : 0; return 1;
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
static void *klass(const char *ns, const char *k) { void *c = dsl_find_class(ns, k); if (!c) dsl_log("soft: class %s.%s not found", ns, k); return c; }
static int32_t foff(void *k, const char *f) { int32_t o = dsl_field_offset(k, f); if (o < 0) dsl_log("soft: field %s not found", f); return o; }

/* ------------------------------------------------------------------ small files */
static void path_of(char *out, size_t n, const char *what) { snprintf(out, n, "%s.%s", handoff_path, what); }
static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static void touch_file(const char *p, const char *text) { FILE *f = fopen(p, "w"); if (f) { if (text) fputs(text, f); fclose(f); } }
static char *slurp(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (1 << 20)) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1); size_t r = fread(b, 1, (size_t)n, f); fclose(f); b[r] = 0; return b;
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
static int a_state = A_IDLE; static double a_t0, a_tstate;
static char a_profile[1200], a_save[512], a_env[2048]; static int a_speed = -1, a_scene;
static double save_wait_from; static time_t save_newer_than;

static void a_fail(const char *why) {
    SLOG("A: soft restart FAILED: %s; the old mods stay", why);
    dsl_sos_child_kill();
    if (fade && fade_in) fade_in(fade, NULL);
    if (a_speed > 0) set_speed(a_speed);
    char p[1300]; path_of(p, sizeof p, "failed"); touch_file(p, why);
    a_state = A_FAILED; cap_on = 0;
}

/* newest save written after `t` in the game's Saves folder: the autosave of this restart */
static Obj (*pdp)(const void *);                     /* Application.persistentDataPath */
static int newest_save(time_t t, char *name, size_t n) {
    Obj s = pdp ? pdp(NULL) : NULL; if (!s) return 0;
    char d[1200]; int32_t len = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    size_t k = 0; for (int32_t i = 0; i < len && k + 1 < 1100; i++) d[k++] = c[i] < 128 ? (char)c[i] : '?';
    d[k] = 0; strcat(d, "/Saves");
    DIR *dir = opendir(d); if (!dir) return 0;
    struct dirent *e; time_t best = 0; int ok = 0;
    while ((e = readdir(dir))) {
        size_t L = strlen(e->d_name); if (L < 6 || strcmp(e->d_name + L - 5, ".json")) continue;
        char full[1600]; snprintf(full, sizeof full, "%s/%s", d, e->d_name);
        struct stat st; if (stat(full, &st) || st.st_mtime < t || st.st_mtime < best) continue;
        best = st.st_mtime; snprintf(name, n, "%.*s", (int)(L - 5), e->d_name); ok = 1;
    }
    closedir(dir); return ok;
}

static int a_save_done;
static void jesc(char *out, size_t n, const char *s) {    /* JSON string body (Windows paths carry backslashes) */
    size_t k = 0;
    for (; s && *s && k + 3 < n; s++) { if (*s == '"' || *s == '\\') out[k++] = '\\'; out[k++] = *s; }
    out[k] = 0;
}
static void write_handoff(void) {
    char jp[2400], js[1100]; jesc(jp, sizeof jp, a_profile); jesc(js, sizeof js, a_save);
    float cx = 0, cy = 0, cz = 0; int hc = camera(&cx, &cy, &cz);
    float tx = 0, ty = 0, tz = 0; int ht = cam_target(&tx, &ty, &tz);
    double f[4] = { 0, 0, 0, 0 }; dsl_sos_game_frame(f);
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", handoff_path);
    FILE *o = fopen(tmp, "w"); if (!o) return;
    fprintf(o, "{\"v\":1,\"from_pid\":%d,\"profile\":\"%s\",\"scene\":%d,\"save\":\"%s\",\"save_pending\":%d,\"speed\":%d,"
               "\"frame\":[%.1f,%.1f,%.1f,%.1f],\"camera\":[%.3f,%.3f,%.3f],\"has_camera\":%d,\"camera_target\":[%.3f,%.3f,%.3f],\"has_target\":%d,\"apply_at\":%.3f}\n",
            (int)getpid(), jp, a_scene, js, !a_save_done, a_speed, f[0], f[1], f[2], f[3], cx, cy, cz, hc, tx, ty, tz, ht, t_start + a_t0);
    fclose(o); rename(tmp, handoff_path);
    SLOG("A: handoff written: scene %d save '%s' speed %d frame %.0f,%.0f %.0fx%.0f camera %.2f,%.2f size %.2f, target %.2f,%.2f size %.2f",
         a_scene, a_save, a_speed, f[0], f[1], f[2], f[3], cx, cy, cz, tx, ty, tz);
}

int dsl_soft_wanted(void) { return role_b; }
int dsl_soft_busy(void) { return role_b || (a_state != A_IDLE && a_state != A_FAILED); }
void *dsl_soft_mainflow(void) { return mf; }
int dsl_soft_apply(const char *profile, const char *env_lines) {
    if (role_b && hidden) { dsl_log("soft: refused, this process is still being handed over"); return -1; }
    if (a_state != A_IDLE && a_state != A_FAILED) { dsl_log("soft: refused, a soft restart already runs"); return -1; }
    if (!dsl_ready || !mf || !fade || !obj_new) { dsl_log("soft: refused, game not ready (mf %p fade %p)", mf, fade); return -1; }
    int sc = scene();
    if ((sc != 1 && sc != 2) || busy()) { dsl_log("soft: refused, scene %d busy %d", sc, busy()); return -1; }
    snprintf(a_profile, sizeof a_profile, "%s", profile ? profile : "");
    snprintf(a_env, sizeof a_env, "%s", env_lines ? env_lines : "");
    a_scene = sc; a_save[0] = 0; a_t0 = since(); a_tstate = a_t0;
    const char *ld = env_lines ? strstr(env_lines, "DSL_SOFT_LOAD=") : NULL;   /* restart straight into a given save */
    if (ld && (ld == env_lines || ld[-1] == '\n')) {
        ld += 14; size_t n = strcspn(ld, "\n"); if (n >= sizeof a_save) n = sizeof a_save - 1;
        memcpy(a_save, ld, n); a_save[n] = 0;
    }
    char p[1300];
    for (const char *x = "ready\0go\0shown\0failed\0"; *x; x += strlen(x) + 1) { path_of(p, sizeof p, x); unlink(p); }
    if (getenv("DSL_SOFT_CAPTURE")) cap_on = 1;
    SLOG("A: APPLY profile %s (scene %d)", a_profile, sc);
    if (a_save[0]) {                                                      /* given save: B loads it, A saves nothing */
        a_scene = 2; a_speed = -1; a_save_done = 1; SLOG("A: B will load the save %s", a_save);
    } else if (sc == 2) {
        a_speed = game_speed();
        set_speed(0);                                                     /* the save = what the player sees now */
        Obj e = obj_new(k_autosave); ctor_autosave(e, NULL); queue(e);    /* the game's own autosave */
        save_newer_than = time(NULL) - 1; save_wait_from = since();
        a_save_done = 0; SLOG("A: paused (speed was %d), autosave queued", a_speed);
    } else { a_speed = -1; a_save_done = 1; }
    fade_out(fade, NULL, 1, NULL);                                        /* the game's loading screen */
    write_handoff();                                                      /* B starts now; the save name follows */
    if (dsl_sos_spawn(a_profile, handoff_path, a_env)) { a_fail("could not start B"); return -1; }
    SLOG("A: B started, pid %ld", dsl_sos_child_pid());
    a_state = A_WAIT_READY;
    return 0;
}

static void a_tick(void) {
    char p[1300];
    double t = since();
    if (a_state == A_IDLE || a_state == A_FAILED || a_state == A_QUITTING) return;
    capture("A");
    if (dsl_sos_child_exited()) { a_fail("B exited before the swap"); return; }
    double lim = getenv("DSL_SOFT_TIMEOUT") ? atof(getenv("DSL_SOFT_TIMEOUT")) : 90;
    if (t - a_t0 > lim) { a_fail("timeout"); return; }
    if (!a_save_done) {
        if (!busy() && newest_save(save_newer_than, a_save, sizeof a_save) && t - save_wait_from > 0.5) {
            a_save_done = 1; SLOG("A: autosave done: %s", a_save); write_handoff();
        } else if (t - save_wait_from > 30) { a_fail("autosave not written"); return; }
    }
    if (a_state == A_WAIT_READY) {
        path_of(p, sizeof p, "ready");
        static double last_note;
        if (t - last_note > 5) { last_note = t; SLOG("A: waiting: B ready %d, autosave done %d, loading screen up %d", exists(p), a_save_done, fade_done ? fade_done(fade, NULL) : -1); }
        if (!exists(p) || !a_save_done) return;
        if (t - a_t0 < 0.4 || !fade_done || !fade_done(fade, NULL)) return;   /* A's own loading screen must be up */
        SLOG("A: B ready");
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
static int b_state = B_BOOT; static double b_tstate; static int b_frames_ok, b_load_speed = -1;
static void b_tick(void) {
    char p[1300]; double t = since();
    if (b_state == B_DONE) return;
    capture("B");
    if (dsl_ready && b_state < B_FADING) fast_tick();
    double lim = getenv("DSL_SOFT_TIMEOUT") ? atof(getenv("DSL_SOFT_TIMEOUT")) : 90;
    if (b_state < B_SHOWN && (t > lim + 30 || (dsl_ready && !obj_new))) {      /* never stay as an invisible process */
        SLOG("B: cannot finish the handover (%s): B quits", obj_new ? "too slow" : "soft restart off");
        b_state = B_DONE; if (app_quit && mf) app_quit(NULL); else exit(0); return;
    }
    if (!dsl_ready || !mf) return;
    if (!dsl_sos_pid_alive((long)(json_get(hand, "from_pid") ? json_get(hand, "from_pid")->num : 0)) && b_state < B_SHOWN) {
        SLOG("B: A is gone before the swap: B quits"); if (app_quit) app_quit(NULL); else exit(0); b_state = B_DONE; return;
    }
    int sc = scene();
    if (b_state == B_BOOT) {
        if (sc != 1 || !boot_ready() || busy()) return;
        if (json_get(hand, "save_pending") && json_get(hand, "save_pending")->num != 0) {      /* A's autosave still running */
            char *txt = slurp(handoff_path); JNode *j = txt ? json_parse(txt) : NULL; free(txt);
            if (!j) return;
            if (!json_get(j, "save_pending") || json_get(j, "save_pending")->num != 0) { json_free(j); return; }
            json_free(hand); hand = j;
        }
        SLOG("B: title ready");
        const char *save = json_str(hand, "save");
        if (save && *save && json_get(hand, "scene") && json_get(hand, "scene")->num == 2) {
            Obj e = obj_new(k_load); ctor_load(e, str_new(save), NULL); queue(e);
            SLOG("B: LoadCampaignRequested %s", save); b_state = B_LOADING; b_tstate = t;
        } else { b_state = B_SETTLE; b_tstate = t; }
        return;
    }
    if (b_state == B_LOADING) {
        if (sc != 2 || busy() || (fade_loading && fade_loading(fade, NULL))) { b_frames_ok = 0; return; }
        if (++b_frames_ok < 10) return;
        b_load_speed = game_speed(); set_speed(0);
        SLOG("B: world loaded, paused until the swap (speed was %d)", b_load_speed);
        b_state = B_SETTLE; b_tstate = t; return;
    }
    if (b_state == B_SETTLE) {
        fast_end();                                                       /* the loading screen fade runs at the game's own pace */
        if (t - b_tstate < 0.3) return;                                   /* a few frames for the view */
        if (fade) fade_out(fade, NULL, 1, NULL);
        b_state = B_FADING; b_tstate = t; return;
    }
    if (b_state == B_FADING) {
        if (t - b_tstate < 0.4 || (fade && fade_done && !fade_done(fade, NULL))) return;
        float cx, cy, cz, tx, ty, tz;
        if (camera(&cx, &cy, &cz)) SLOG("B: camera %.2f,%.2f size %.2f", cx, cy, cz);
        if (cam_target(&tx, &ty, &tz)) SLOG("B: camera target %.2f,%.2f size %.2f", tx, ty, tz);
        path_of(p, sizeof p, "ready"); touch_file(p, NULL); SLOG("B: ready (loading screen up, window still hidden)");
        b_state = B_WAIT_GO; return;
    }
    if (b_state == B_WAIT_GO) {
        path_of(p, sizeof p, "go"); if (!exists(p)) return;
        hidden = 0;
        fast_end();
        dsl_sos_show(a_has_frame ? a_frame : NULL, !no_activate);
        frame_log("B");
        path_of(p, sizeof p, "shown"); touch_file(p, NULL);
        double at = json_get(hand, "apply_at") ? json_get(hand, "apply_at")->num : 0;
        SLOG("B: SHOWN (%.3f s after apply in A)", at > 0 ? now_s() - at : -1.0);
        b_state = B_SHOWN; b_tstate = t; return;
    }
    if (b_state == B_SHOWN) {
        if (t - b_tstate < 0.05) return;                                  /* one frame on screen with the loading screen */
        if (fade && fade_in) fade_in(fade, NULL);
        int sp = json_get(hand, "speed") ? (int)json_get(hand, "speed")->num : -1;
        if (sc == 2 && sp <= 0) sp = b_load_speed;                        /* given save: the speed the game loaded it with */
        if (sc == 2 && sp > 0) set_speed(sp);
        SLOG("B: fade in, speed %d", sp);
        b_state = B_DONE; b_tstate = t;
        role_b = 0; a_state = A_IDLE;                                     /* from now on a normal game: it may soft restart itself */
    }
}

/* ------------------------------------------------------------------ main-thread tick (between Unity frames) */
static double ff_until;
static void tick(void) {
    if (stage_on) dsl_sos_stage_check();                                 /* test stage: a window off the stage is never seen */
    if (role_b) b_tick(); else a_tick();
    double t = since();
    if (ff && (hidden || a_state != A_IDLE || t < ff_until || (role_b && b_state != B_DONE))) frame_log(role_b ? "B" : "A");
    if (b_state == B_DONE && ff_until == 0) ff_until = t + 3;
    if (b_state == B_DONE && t < ff_until && a_state == A_IDLE) capture("B");   /* test: the first seconds after the swap */
    if (test_mode && !role_b && a_state == A_IDLE) {                    /* test: apply by file; optional camera nudge first */
        static double last, due; static char pend[1200], pend_env[1024];
        if (due > 0 && t >= due) { due = 0; dsl_soft_apply(pend, pend_env[0] ? pend_env : NULL); return; }
        if (due > 0 || t - last < 0.5) return; last = t;
        char p[1300]; snprintf(p, sizeof p, "%s/apply_request.json", sdir);
        char *txt = slurp(p); if (!txt) return;
        unlink(p);
        JNode *j = json_parse(txt); free(txt);
        const char *pr = j ? json_str(j, "profile") : NULL;
        if (!pr) { dsl_log("soft: apply_request.json without profile"); json_free(j); return; }
        snprintf(pend, sizeof pend, "%s", pr); snprintf(pend_env, sizeof pend_env, "%s", json_str(j, "env") ? json_str(j, "env") : "");
        JNode *cm = json_get(j, "camera_move"); Obj cs = cam_setting();
        if (cm && cm->t == JN_ARR && cm->child && cm->child->next && cs && cs_getpos && cs_setpos) {
            V3 c = cs_getpos(cs, NULL); V3 n = { c.x + (float)cm->child->num, c.y + (float)cm->child->next->num, c.z };
            cs_setpos(cs, n, NULL);
            SLOG("test: camera target moved %.2f,%.2f -> %.2f,%.2f", c.x, c.y, n.x, n.y);
            due = t + 1.0;
        } else due = t;
        json_free(j);
    }
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
            role_b = 1; hidden = 1; if (cap_dir[0]) cap_on = 1;
            JNode *f = json_get(hand, "frame"); int k = 0;
            if (f && f->t == JN_ARR) for (JNode *c = f->child; c && k < 4; c = c->next) a_frame[k++] = c->num;
            a_has_frame = (k == 4 && a_frame[2] > 0);
            dsl_sos_unsetenv("DSL_SOFT_HANDOFF");                           /* B's own later restarts start fresh */
        }
    }
    dsl_sos_windows_install(role_b, stage_on, stage_x, stage_y);
    char fp[1300]; snprintf(fp, sizeof fp, "%s/softrestart_frames_%s_%d.log", sdir, role_b ? "B" : "A", (int)getpid());
    if (role_b || test_mode) ff = fopen(fp, "w");
    dsl_log("soft: role %s%s%s%s, process started %.3f s ago", role_b ? "B (handoff " : "A", role_b ? handoff_path : "", role_b ? ")" : "",
            stage_on ? ", test stage" : "", since());
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
    k_autosave = klass("Refactor.Main.Event", "AutoSaveRequested"); ctor_autosave = meth("Refactor.Main.Event", "AutoSaveRequested", ".ctor", 0);
    k_setspeed = klass("Refactor.Main.Event", "SetGameSpeedRequested"); ctor_setspeed = meth("Refactor.Main.Event", "SetGameSpeedRequested", ".ctor", 1);
    k_chgspeed = klass("Refactor.Main.Event", "ChangeGameSpeedRequested"); ctor_chgspeed = meth("Refactor.Main.Event", "ChangeGameSpeedRequested", ".ctor", 2);
    fade_out = meth("Refactor.UI", "ScreenFadeEffectView", "AnimateFadeOut", 2); fade_in = meth("Refactor.UI", "ScreenFadeEffectView", "AnimateFadeIn", 0);
    fade_done = meth("Refactor.UI", "ScreenFadeEffectView", "get_IsFadeOutCompleted", 0);
    fade_loading = meth("Refactor.UI", "ScreenFadeEffectView", "get_IsLoading", 0);
    fade_stage = meth("Refactor.UI", "ScreenFadeEffectView", "SetLoadingStage", 1);
    fade_progress = meth("Refactor.UI", "ScreenFadeEffectView", "SetLoadingProgress", 1);
    app_quit = meth("UnityEngine", "Application", "Quit", 0); pdp = meth("UnityEngine", "Application", "get_persistentDataPath", 0);
    cam_main = meth("UnityEngine", "Camera", "get_main", 0); comp_tf = meth("UnityEngine", "Component", "get_transform", 0);
    tf_pos = meth("UnityEngine", "Transform", "get_position", 0); cam_ortho = meth("UnityEngine", "Camera", "get_orthographicSize", 0);
    cs_getpos = meth("Refactor.Main", "CameraSetting", "GetPosition", 0); cs_setpos = meth("Refactor.Main", "CameraSetting", "SetPosition", 1);
    cs_getortho = meth("Refactor.Main", "CameraSetting", "GetOrthoSize", 0);
    void *(*icall)(const char *) = dsl_ga_sym("il2cpp_resolve_icall");
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
    if (cap_dir[0]) {
        cap_tex = meth("UnityEngine", "ScreenCapture", "CaptureScreenshotAsTexture", 0);
        enc_png = meth("UnityEngine", "ImageConversion", "EncodeToPNG", 1); obj_destroy = meth("UnityEngine", "Object", "Destroy", 1);
    }
    int miss = !gc_new || !gc_get || !obj_new || !str_new || !eq_add || !k_load || !ctor_load || !k_autosave || !ctor_autosave || !fade_out || !fade_in || !fade_done
               || off_scene < 0 || off_eq < 0 || !k_setspeed || !ctor_setspeed || !k_chgspeed || !ctor_chgspeed;
    void *a1 = meth("Refactor.Main", "MainFlow", "Awake", 0), *a2 = meth("Refactor.UI", "ScreenFadeEffectView", "Awake", 0);
    int h1 = a1 ? dsl_hook(DSL_OWNER_LOADER, a1, (void *)my_mf_awake, (void **)&o_mf_awake, "loader soft MainFlow.Awake") : -1;
    int h2 = a2 ? dsl_hook(DSL_OWNER_LOADER, a2, (void *)my_fade_awake, (void **)&o_fade_awake, "loader soft ScreenFadeEffectView.Awake") : -1;
    if (miss || h1 || h2) { dsl_log("soft: game functions missing (%d) or hooks refused (%d %d): soft restart OFF", miss, h1, h2); obj_new = NULL; return; }
    SLOG("soft restart ready (%s)", role_b ? "B: waiting for the title" : "A");
}
