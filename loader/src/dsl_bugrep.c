// dsl_bugrep.c - the player's bug report window (owner 2026-10-20: "an in-game bug report: text + screenshot + logs";
// the game's own "Bug Report" button opens OUR window in the modded game, not the developers' form; reports reach the
// owner through the private receiver page + a Discord ping, receiver lane bugrecv).
//
// Opens from: the title's Bug Report button (TitleMenuView.OnBugReportPressed) and the Esc menu's in a world
// (SystemMenuView.OnReportBugPressed). Both stock methods are hooked by name and never run (they open a web form and the
// saves folder). The screenshot is the game frame just before the window: at the title the next frame; in a world the
// Esc menu is scaled to 0 for 3 frames (no OnDisable, the game stays paused), shot, scaled back, then the window opens
// on top of it. The shot: ScreenCapture.CaptureScreenshotAsTexture -> GetPixels32 -> box scaled to <= 1600 x 900 ->
// JPEG (stb_image_write; the game has no EncodeToJPG) <= 1 MB -> ROOT/crash/bugs/shot.jpg (preview in the window).
// Window: text (many lines, 8000 max), contact (optional), "Include a screenshot" (on), "Include logs" (on), privacy
// line + link, Send / Cancel. Nothing is sent without Send; no consent question (the player sends on purpose).
// Send (thread): the report (dsl_bugrep_fmt.c) is written to ROOT/crash/bugs/<time>/report.txt.gz FIRST, then posted
// (X-DSCR-Kind: bug). 201 = "sent (id)" + sent.txt in that folder; 429 = "Too many reports, try later"; anything else =
// the error + the folder; the window's "Send saved reports" sends folders without sent.txt later. The 10 newest folders
// are kept.
// Test only: DSL_BUGREP_DRY=1 writes the report and does not post; DSL_BUGREP_ADMIN=<token> adds X-DSCR-Admin (receiver
// rate limits off); DSL_BUGREP_TEST=<dir>: <dir>/cmd.json {"seq","op": state|type|contact|tick|send|send_saved|cancel|
// shot, "text", "name", "which": "shot"|"logs"} -> <dir>/result.json.
#include "dsl_internal.h"
#include "dsl_menu.h"
#include "dsl_os.h"
#include "dsl_scrub.h"
#include "dsl_update_os.h"
#include "dsl_bugrep.h"
#include "../sdk/dsl_sdk.h"
#include <dirent.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
int dsl_u8_to_w(const char *s, wchar_t *out, size_t n);
#else
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include "third_party/stb/stb_image_write.h"
#pragma clang diagnostic pop

typedef void *Obj;
typedef struct { float x, y; } V2;
typedef struct { float x, y, z; } V3;
typedef struct { float r, g, b, a; } Col;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define CALL(mi, R, ...) ((R (*)(__VA_ARGS__, const void *))(*(void **)(mi)))
#define CALL0(mi, R) ((R (*)(const void *))(*(void **)(mi)))

const char *dsl_crash_dir(void);
const char *dsl_crash_install(void);
const char *dsl_crash_receiver(void);
const char *dsl_crash_key(void);
int dsl_reg_scene(void);

#define WIN_W 1000.0f
#define WIN_H 700.0f
#define BAND_H 80.0f
#define GOLD 0xD9BE8FFFu
#define SUB 0xA69085FFu
#define WARN 0xEA9F3DFFu
#define SHOT_MAX_W 1600
#define SHOT_MAX_H 900
#define SHOT_MAX_BYTES (1u << 20)
#define KEEP_REPORTS 10

static const DSLUi *U;
static int hooked, is_open, want, want_src, phase, phase_frames;   /* want_src 1 title, 2 world */
static uintptr_t h_menu;                                            /* the Esc menu while it is scaled away (GC handle) */
static V3 menu_scale;
static char bdir[1100], shot_path[1200], status_s[1400];
static int shot_ok, shot_w, shot_h; static long shot_bytes;
static DSLObj w_back, w_win, w_text, w_contact, w_tshot, w_tlogs, w_prev, w_noprev, w_status, w_send, w_cancel, w_saved;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int sending, done_code, unsent_n;                            /* done_code: set by the thread, read by the frame */
static char done_msg[1400], last_id[160];

/* ------------------------------------------------------------------ Unity by name */
static void *m_amin, *m_amax, *m_pivot, *m_pos, *m_size, *m_cur_es, *m_sel_go, *m_go_get, *m_go_tr, *m_in_focused, *m_img_sprite,
            *m_gr_color, *m_shot, *m_tw, *m_th, *m_pix, *m_destroy, *m_comp_tr, *m_tr_getscale, *m_tr_setscale, *m_rect;
static void *k_image, *k_input, *k_tmp;
static void *(*x_class_type)(void *), *(*x_type_obj)(void *);
static void *mth(const char *ns, const char *k, const char *m, int argc) {
    void *kl = dsl_find_class(ns, k); void *mi = kl ? dsl_find_method(kl, m, argc) : NULL;
    return mi && *(void **)mi ? mi : NULL;
}
static void unity_init(void) {
    m_amin = mth("UnityEngine", "RectTransform", "set_anchorMin", 1); m_amax = mth("UnityEngine", "RectTransform", "set_anchorMax", 1);
    m_pivot = mth("UnityEngine", "RectTransform", "set_pivot", 1); m_pos = mth("UnityEngine", "RectTransform", "set_anchoredPosition", 1);
    m_size = mth("UnityEngine", "RectTransform", "set_sizeDelta", 1); m_rect = mth("UnityEngine", "RectTransform", "get_rect", 0);
    m_cur_es = mth("UnityEngine.EventSystems", "EventSystem", "get_current", 0);
    m_sel_go = mth("UnityEngine.EventSystems", "EventSystem", "get_currentSelectedGameObject", 0);
    m_go_tr = mth("UnityEngine", "GameObject", "get_transform", 0);
    m_in_focused = mth("TMPro", "TMP_InputField", "get_isFocused", 0);
    m_img_sprite = mth("UnityEngine.UI", "Image", "set_sprite", 1); m_gr_color = mth("UnityEngine.UI", "Graphic", "set_color", 1);
    m_shot = mth("UnityEngine", "ScreenCapture", "CaptureScreenshotAsTexture", 0);
    m_tw = mth("UnityEngine", "Texture", "get_width", 0); m_th = mth("UnityEngine", "Texture", "get_height", 0);
    m_pix = mth("UnityEngine", "Texture2D", "GetPixels32", 0); m_destroy = mth("UnityEngine", "Object", "Destroy", 1);
    m_comp_tr = mth("UnityEngine", "Component", "get_transform", 0);
    m_tr_getscale = mth("UnityEngine", "Transform", "get_localScale", 0); m_tr_setscale = mth("UnityEngine", "Transform", "set_localScale", 1);
    k_image = dsl_find_class("UnityEngine.UI", "Image"); k_input = dsl_find_class("TMPro", "TMP_InputField");
    k_tmp = dsl_find_class("TMPro", "TMP_Text");
    *(void **)&x_class_type = dsl_il2cpp_fn("il2cpp_class_get_type"); *(void **)&x_type_obj = dsl_il2cpp_fn("il2cpp_type_get_object");
    { void *kc = dsl_find_class("UnityEngine", "Component"); void *mi = NULL;
      void *(*gm)(void *, void **) = dsl_il2cpp_fn("il2cpp_class_get_methods"); const char *(*nm)(void *) = dsl_il2cpp_fn("il2cpp_method_get_name");
      uint32_t (*pc)(void *) = dsl_il2cpp_fn("il2cpp_method_get_param_count"); void *(*pa)(void *, uint32_t) = dsl_il2cpp_fn("il2cpp_method_get_param");
      char *(*tn)(void *) = dsl_il2cpp_fn("il2cpp_type_get_name"); void (*fr)(void *) = dsl_il2cpp_fn("il2cpp_free");
      void *it = NULL;
      while (kc && gm && nm && pc && pa && tn && (mi = gm(kc, &it))) {          /* GetComponent(System.Type) */
          if (strcmp(nm(mi), "GetComponent") || pc(mi) != 1) continue;
          char *t = tn(pa(mi, 0)); int ok = t && !strcmp(t, "System.Type"); if (t && fr) fr(t);
          if (ok) { m_go_get = mi; break; }
      } }
}
static Obj comp_of(Obj any_comp, void *k) {
    if (!any_comp || !k || !m_go_get || !x_class_type || !x_type_obj) return NULL;
    return CALL(m_go_get, Obj, Obj, Obj)(any_comp, x_type_obj(x_class_type(k)), m_go_get);
}
static Col col_of(uint32_t c) { return (Col){ (c >> 24) / 255.f, (c >> 16 & 255) / 255.f, (c >> 8 & 255) / 255.f, (c & 255) / 255.f }; }
static DSLObj flat(DSLObj panel, uint32_t c) {                        /* a kit panel as one flat colour (as the Mods window) */
    Obj im = comp_of(panel, k_image);
    if (im && m_img_sprite && m_gr_color) {
        CALL(m_img_sprite, void, Obj, Obj)(im, NULL, m_img_sprite);
        CALL(m_gr_color, void, Obj, Col)(im, col_of(c), m_gr_color);
    }
    return panel;
}
static void centre(DSLObj rt) {
    if (!rt || !m_amin || !m_amax || !m_pivot || !m_pos) return;
    V2 h = { 0.5f, 0.5f }, z = { 0, 0 };
    CALL(m_amin, void, Obj, V2)(rt, h, m_amin); CALL(m_amax, void, Obj, V2)(rt, h, m_amax);
    CALL(m_pivot, void, Obj, V2)(rt, h, m_pivot); CALL(m_pos, void, Obj, V2)(rt, z, m_pos);
}
typedef struct { float x, y, w, h; } R4;
static void fill_parent(DSLObj rt) {
    if (!rt || !m_amin || !m_amax || !m_pivot || !m_pos || !m_size) return;
    V2 z = { 0, 0 }, o = { 1, 1 }, h = { 0.5f, 0.5f };
    CALL(m_amin, void, Obj, V2)(rt, z, m_amin); CALL(m_amax, void, Obj, V2)(rt, o, m_amax); CALL(m_pivot, void, Obj, V2)(rt, h, m_pivot);
    CALL(m_size, void, Obj, V2)(rt, z, m_size); CALL(m_pos, void, Obj, V2)(rt, z, m_pos);
}
static int typing(void) {                                             /* a text field has the keyboard (Esc is its own) */
    Obj es = m_cur_es ? CALL0(m_cur_es, Obj)(m_cur_es) : NULL;
    Obj go = es && m_sel_go ? CALL(m_sel_go, Obj, Obj)(es, m_sel_go) : NULL;
    Obj tr = go && m_go_tr ? CALL(m_go_tr, Obj, Obj)(go, m_go_tr) : NULL;
    Obj f = tr ? comp_of(tr, k_input) : NULL;
    return f && m_in_focused && CALL(m_in_focused, int8_t, Obj)(f, m_in_focused);
}

/* ------------------------------------------------------------------ files */
static void mkdirs(const char *path) {
    char p[1200]; snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++) if (*s == '/' || *s == '\\') { char c = *s; *s = 0; mkdir(p, 0755); *s = c; }
    mkdir(p, 0755);
}
static int write_file(const char *p, const void *b, size_t n) {
    char t[1300]; snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "wb"); if (!f) return -1;
    size_t w = fwrite(b, 1, n, f); if (fclose(f) || w != n) { remove(t); return -1; }
    return dsl_os_replace(t, p);
}
static unsigned char *read_file(const char *p, size_t max, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || (size_t)sz > max) { fclose(f); return NULL; }
    unsigned char *b = malloc((size_t)sz + 1); size_t got = b ? fread(b, 1, (size_t)sz, f) : 0; fclose(f);
    if (!b || got != (size_t)sz) { free(b); return NULL; }
    b[got] = 0; *n = got; return b;
}
static int exists(const char *p) { struct stat st; return !stat(p, &st); }
static void base_dir(void) {
    if (bdir[0]) return;
    const char *c = dsl_crash_dir(), *ld = dsl_core_logdir();
    if (c && *c) snprintf(bdir, sizeof bdir, "%s/bugs", c); else snprintf(bdir, sizeof bdir, "%s/crash/bugs", ld ? ld : ".");
    mkdirs(bdir);
    snprintf(shot_path, sizeof shot_path, "%s/shot.jpg", bdir);
}
static int list_reports(char names[][64], int max) {                 /* report folders (their names start with the time), oldest first */
    DIR *d = opendir(bdir); struct dirent *e; int k = 0;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9' || k >= max || strlen(e->d_name) >= 64) continue;
        snprintf(names[k++], 64, "%s", e->d_name);
    }
    if (d) closedir(d);
    for (int i = 1; i < k; i++) for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) { char t[64]; memcpy(t, names[j], 64); memcpy(names[j], names[j - 1], 64); memcpy(names[j - 1], t, 64); }
    return k;
}
static int is_unsent(const char *name) {
    char p[1300], s[1300]; snprintf(p, sizeof p, "%s/%s/report.txt.gz", bdir, name); snprintf(s, sizeof s, "%s/%s/sent.txt", bdir, name);
    return exists(p) && !exists(s);
}
static int count_unsent(void) { char n[64][64]; int k = list_reports(n, 64), u = 0; for (int i = 0; i < k; i++) u += is_unsent(n[i]); return u; }
static void prune(void) {                                             /* the newest KEEP_REPORTS folders stay */
    char n[64][64]; int k = list_reports(n, 64);
    for (int i = 0; i + KEEP_REPORTS < k; i++) {
        static const char *const fs[] = { "report.txt.gz", "screenshot.jpg", "sent.txt", "report.txt.gz.resp", "report.txt.gz.head" };
        char p[1400]; for (size_t j = 0; j < sizeof fs / sizeof fs[0]; j++) { snprintf(p, sizeof p, "%s/%s/%s", bdir, n[i], fs[j]); remove(p); }
        snprintf(p, sizeof p, "%s/%s", bdir, n[i]); rmdir(p);
    }
}

/* ------------------------------------------------------------------ the screenshot (main thread) */
typedef struct { unsigned char *p; size_t n, cap; int bad; } Mb;
static void jpg_put(void *ctx, void *data, int size) {                /* stb's writer callback: append to memory */
    Mb *m = ctx;
    if (size <= 0 || m->bad) return;
    if (m->n + (size_t)size > m->cap) {
        size_t c = m->cap ? m->cap : 262144; while (c < m->n + (size_t)size) c *= 2;
        unsigned char *q = realloc(m->p, c); if (!q) { m->bad = 1; return; }
        m->p = q; m->cap = c;
    }
    memcpy(m->p + m->n, data, (size_t)size); m->n += (size_t)size;
}
static int shoot(void) {
    shot_ok = 0; shot_w = shot_h = 0; shot_bytes = 0;
    base_dir(); remove(shot_path);
    if (!m_shot || !m_tw || !m_th || !m_pix) { dsl_log("bugrep: no screenshot (game names missing)"); return -1; }
    Obj tex = CALL0(m_shot, Obj)(m_shot); if (!tex) { dsl_log("bugrep: no screenshot (capture failed)"); return -1; }
    int w = CALL(m_tw, int, Obj)(tex, m_tw), h = CALL(m_th, int, Obj)(tex, m_th);
    Obj arr = w > 0 && h > 0 ? CALL(m_pix, Obj, Obj)(tex, m_pix) : NULL;
    if (m_destroy) CALL(m_destroy, void, Obj)(tex, m_destroy);
    size_t n = arr ? (size_t)AT(arr, 0x18, uintptr_t) : 0;
    if (!arr || n != (size_t)w * (size_t)h) { dsl_log("bugrep: no screenshot (%dx%d, %zu pixels)", w, h, n); return -1; }
    const unsigned char *px = (const unsigned char *)arr + 0x20;      /* Color32 RGBA, bottom row first */
    int dw = w, dh = h;
    if (dw > SHOT_MAX_W) { dh = (int)((long long)dh * SHOT_MAX_W / dw); dw = SHOT_MAX_W; }
    if (dh > SHOT_MAX_H) { dw = (int)((long long)dw * SHOT_MAX_H / dh); dh = SHOT_MAX_H; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    unsigned char *img = malloc((size_t)dw * (size_t)dh * 3); if (!img) return -1;
    for (int y = 0; y < dh; y++) {                                    /* box average of the source pixels under each pixel */
        int y0 = (int)((long long)y * h / dh), y1 = (int)((long long)(y + 1) * h / dh); if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = (int)((long long)x * w / dw), x1 = (int)((long long)(x + 1) * w / dw); if (x1 <= x0) x1 = x0 + 1;
            unsigned r = 0, g = 0, b = 0, c = 0;
            for (int sy = y0; sy < y1; sy++) for (int sx = x0; sx < x1; sx++) { const unsigned char *q = px + ((size_t)sy * w + sx) * 4; r += q[0]; g += q[1]; b += q[2]; c++; }
            unsigned char *o = img + ((size_t)y * dw + x) * 3; o[0] = (unsigned char)(r / c); o[1] = (unsigned char)(g / c); o[2] = (unsigned char)(b / c);
        }
    }
    stbi_flip_vertically_on_write(1);
    int r = -1;
    for (int q = 85; q >= 45 && r; q -= 20) {
        Mb m = { 0 };
        if (!stbi_write_jpg_to_func(jpg_put, &m, dw, dh, 3, img, q) || !m.p || m.bad) { free(m.p); break; }
        if (m.n <= SHOT_MAX_BYTES || q - 20 < 45) {
            if (!write_file(shot_path, m.p, m.n)) { r = 0; shot_bytes = (long)m.n; }
        }
        free(m.p);
    }
    free(img);
    if (r) { dsl_log("bugrep: screenshot could not be written"); return -1; }
    shot_ok = 1; shot_w = dw; shot_h = dh;
    dsl_log("bugrep: screenshot %dx%d -> %dx%d JPEG, %ld bytes", w, h, dw, dh, shot_bytes);
    return 0;
}
/* ------------------------------------------------------------------ send (thread) */
typedef struct { char *text, *contact, *header; int shot, logs; char folder[1200]; char only[64]; } Job;
static void set_done(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void set_done(int code, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); pthread_mutex_lock(&mu); vsnprintf(done_msg, sizeof done_msg, fmt, ap); done_code = code; pthread_mutex_unlock(&mu); va_end(ap);
}
/* one saved report folder to the receiver: HTTP status, -1 = no answer; id = the receiver's id of it */
static int post_folder(const char *folder, char *id, size_t idn) {
    char gz[1300]; snprintf(gz, sizeof gz, "%s/report.txt.gz", folder);
    const char *dry = getenv("DSL_BUGREP_DRY");
    if (dry && !strcmp(dry, "1")) {
        snprintf(id, idn, "dry-run"); dsl_log("bugrep: dry run: %s written, not sent", gz);
        char sp[1300]; snprintf(sp, sizeof sp, "%s/sent.txt", folder); write_file(sp, "dry-run", 7); return 299;
    }
    char h1[100], h2[100], h4[200]; const char *inst = dsl_crash_install();
    char rid[33] = "";
    if (!inst || strlen(inst) != 32) { unsigned char r[16]; dsl_uos_random(r, sizeof r); for (int i = 0; i < 16; i++) snprintf(rid + i * 2, 3, "%02x", r[i]); inst = rid; }
    snprintf(h1, sizeof h1, "X-DSCR-Key: %s", dsl_crash_key()); snprintf(h2, sizeof h2, "X-DSCR-Install: %s", inst);
    const char *adm = getenv("DSL_BUGREP_ADMIN");
    const char *hs[5] = { "Content-Type: application/gzip", h1, h2, "X-DSCR-Kind: bug", NULL }; int nh = 4;
    if (adm && *adm && strlen(adm) < 150) { snprintf(h4, sizeof h4, "X-DSCR-Admin: %s", adm); hs[nh++] = h4; }
    char url[300]; snprintf(url, sizeof url, "%s/api/report", dsl_crash_receiver());
    char resp[1024] = ""; int code = dsl_uos_post_resp(url, gz, hs, nh, resp, sizeof resp);
    id[0] = 0;
    if (code >= 200 && code < 300) {
        JNode *j = json_parse(resp); const char *s = json_str(j, "id");
        const char *b = s ? strrchr(s, '/') : NULL; snprintf(id, idn, "%s", b ? b + 1 : s ? s : "?");
        char *dot = strstr(id, ".txt"); if (dot) *dot = 0;
        char *last = strrchr(id, '-'); if (last && last[1]) memmove(id, last + 1, strlen(last));   /* the random part: unique, short */
        json_free(j);
        char sp[1300]; snprintf(sp, sizeof sp, "%s/sent.txt", folder); write_file(sp, s ? s : "?", strlen(s ? s : "?"));
    }
    dsl_log("bugrep: %s posted: HTTP %d %.200s", folder, code, resp);
    return code;
}
static void result_text(int code, const char *id, const char *folder, int dropped) {
    if (code >= 200 && code < 300) {
        char t[400]; snprintf(t, sizeof t, TR("Thanks, your report was sent (id %s)."), id);
        if (dropped) { strncat(t, " ", sizeof t - strlen(t) - 1); strncat(t, TR("The screenshot was too big and was left out."), sizeof t - strlen(t) - 1); }
        set_done(1, "%s", t);
    } else if (code == 429) set_done(2, "%s %s", TR("Too many reports, try later."), TR("Your report is saved: \"Send saved reports\" sends it later."));
    else {
        char why[120]; if (code < 0) snprintf(why, sizeof why, "%s", TR("no connection")); else snprintf(why, sizeof why, "HTTP %d", code);
        char t[1400]; snprintf(t, sizeof t, TR("The report was not sent (%s). It is saved in %s, you can send it later with \"Send saved reports\"."), why, folder);
        set_done(2, "%s", t);
    }
}
static void *send_thread(void *arg) {
    Job *j = arg;
    if (j->only[0]) {                                                 /* "Send saved reports": the unsent folders, oldest first */
        char n[64][64]; int k = list_reports(n, 64), sent = 0, code = 0; char id[160] = "", folder[1300] = "";
        for (int i = 0; i < k; i++) {
            if (!is_unsent(n[i])) continue;
            snprintf(folder, sizeof folder, "%s/%s", bdir, n[i]);
            code = post_folder(folder, id, sizeof id);
            if (code < 200 || code >= 300) break;
            sent++;
        }
        if (code >= 200 && code < 300) { char t[200]; snprintf(t, sizeof t, TRN("%d saved report was sent.", "%d saved reports were sent.", sent), sent); set_done(1, "%s", t); }
        else result_text(code, id, folder, 0);
        pthread_mutex_lock(&mu); snprintf(last_id, sizeof last_id, "%s", id); unsent_n = count_unsent(); sending = 0; pthread_mutex_unlock(&mu);
        free(j); return NULL;
    }
    DslScrub *sc = malloc(sizeof *sc); if (sc) dsl_scrub_collect(sc, dsl_core_logdir());
    size_t imn = 0; unsigned char *im = j->shot ? read_file(shot_path, 4u << 20, &imn) : NULL;
    const char *ld = dsl_core_logdir(); char p[8][1200]; DslBugLog lg[8]; int nl = 0;
    char data[1100]; { const char *dd = getenv("DSML_DATA"), *rd = getenv("DSRIG_DIR");
                       if (dd && *dd) snprintf(data, sizeof data, "%s", dd); else if (rd && *rd) snprintf(data, sizeof data, "%s/data", rd); else snprintf(data, sizeof data, "%s/data", ld); }
    if (j->logs && ld) {
#define LG(nm, cap, ...) do { snprintf(p[nl], sizeof p[nl], __VA_ARGS__); lg[nl] = (DslBugLog){ nm, p[nl], cap }; nl++; } while (0)
        LG("dsload.log", 768u << 10, "%s/dsload.log", ld);
        LG("dsload.prev.log", 512u << 10, "%s/dsload.prev.log", ld);
        LG("rig.log", 512u << 10, "%s/rig.log", ld);
        LG("player.log", 1536u << 10, "%s/player.log", ld);
#ifdef _WIN32
        LG("starter.log", 128u << 10, "%s/starter.log", ld);
#else
        { const char *home = getenv("HOME"); if (home) LG("starter.log", 128u << 10, "%s/Library/Logs/DungeonSettlersModded.log", home); }
#endif
        LG("ErrorLog.txt", 64u << 10, "%s/ErrorLog.txt", data);
#undef LG
    }
    DslBugIn in = { j->header, j->contact, j->text, im, imn, "screenshot.jpg", lg, nl };
    unsigned char *gz = NULL; size_t gn = 0; int dropped = 0;
    int r = sc ? dsl_bugrep_build(sc, &in, DSL_BUG_GZ_MAX, &gz, &gn, &dropped) : -1;
    free(sc);
    int code = -1; char id[160] = "";
    if (r) set_done(2, "%s", TR("The report could not be made (out of memory?)."));
    else {
        char gp[1300]; mkdirs(j->folder); snprintf(gp, sizeof gp, "%s/report.txt.gz", j->folder);
        if (im && !dropped) { char sp[1300]; snprintf(sp, sizeof sp, "%s/screenshot.jpg", j->folder); write_file(sp, im, imn); }
        if (write_file(gp, gz, gn)) { char t[1400]; snprintf(t, sizeof t, TR("The report could not be saved in %s."), j->folder); set_done(2, "%s", t); }
        else {
            dsl_log("bugrep: report saved: %s (%zu bytes gzip, screenshot %s, logs %s)", gp, gn, im && !dropped ? "yes" : dropped ? "dropped (size)" : "no", j->logs ? "yes" : "no");
            code = post_folder(j->folder, id, sizeof id);
            result_text(code, id, j->folder, dropped);
        }
    }
    free(gz); free(im);
    prune();
    pthread_mutex_lock(&mu); snprintf(last_id, sizeof last_id, "%s", id); unsent_n = count_unsent(); sending = 0; pthread_mutex_unlock(&mu);
    free(j->text); free(j->contact); free(j->header); free(j);
    return NULL;
}
static char *header_text(void) {                                      /* main thread: the crash report's header lines */
    char os[160] = "", cpu[160] = "", plat[80] = ""; dsl_uos_sysinfo(os, sizeof os, cpu, sizeof cpu, plat, sizeof plat);
    time_t now = time(NULL); char ts[40]; strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    size_t cap = 16384, o = 0; char *h = malloc(cap); if (!h) return NULL;
    const char *inst = dsl_crash_install();
    o += (size_t)snprintf(h + o, cap - o, "DSCRASH 1\ninstall: %s\nloader: %s\ngame_build: %s\nplatform: %s\nos: %s\ncpu: %s\nsent: %s\nmods:",
                          inst && *inst ? inst : "none", DSL_RELEASE, dsl_build_id ? dsl_build_id : "unknown", plat, os, cpu, ts);
    for (int i = 0; i < dsl_mods_count() && o + 300 < cap; i++) {
        const DSLModInfo *m = dsl_mods_at(i);
        if (m && !m->hidden) o += (size_t)snprintf(h + o, cap - o, " %s %s (%s)%s", m->id, m->version ? m->version : "?", m->enabled ? "on" : "off", i + 1 < dsl_mods_count() ? "," : "");
    }
    snprintf(h + o, cap - o, "\nscene: %s\n", dsl_reg_scene() == 2 ? "world" : dsl_reg_scene() == 1 ? "title" : "other");
    return h;
}

/* ------------------------------------------------------------------ the window (main thread) */
static void close_window(void);
static void status(const char *s, uint32_t col) {
    snprintf(status_s, sizeof status_s, "%s", s);
    if (!w_status) return;
    U->set_text(w_status, s);
    Obj t = comp_of(w_status, k_tmp);                                 /* gold = sent, orange = a problem, grey = busy */
    if (t && m_gr_color) CALL(m_gr_color, void, Obj, Col)(t, col_of(col), m_gr_color);
}
static void send_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    pthread_mutex_lock(&mu); int busy = sending; pthread_mutex_unlock(&mu);
    if (busy || !w_text) return;
    char *t = malloc(4 * DSL_BUG_TEXT_MAX + 4); if (!t) return;
    U->field_get(w_text, t, 4 * DSL_BUG_TEXT_MAX + 4);
    size_t a = strspn(t, " \t\r\n");
    if (!t[a]) { free(t); status(TR("Please write what happened first."), WARN); return; }
    char c[4 * DSL_BUG_CONTACT_MAX + 4] = ""; if (w_contact) U->field_get(w_contact, c, sizeof c);
    char c1[DSL_BUG_CONTACT_MAX + 1]; dsl_bugrep_one_line(c, c1, sizeof c1);
    Job *j = calloc(1, sizeof *j); if (!j) { free(t); return; }
    j->text = t; j->contact = strdup(c1); j->header = header_text();
    j->shot = shot_ok && w_tshot && U->toggle_get(w_tshot); j->logs = !w_tlogs || U->toggle_get(w_tlogs);
    base_dir();
    char ts[40]; time_t now = time(NULL); strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", localtime(&now));
    static int seq; snprintf(j->folder, sizeof j->folder, "%s/%s-%d", bdir, ts, seq++);
    pthread_mutex_lock(&mu); sending = 1; done_code = 0; done_msg[0] = 0; pthread_mutex_unlock(&mu);
    pthread_t th;
    if (pthread_create(&th, NULL, send_thread, j)) { pthread_mutex_lock(&mu); sending = 0; pthread_mutex_unlock(&mu); free(j->text); free(j->contact); free(j->header); free(j); return; }
    pthread_detach(th);
    U->set_enabled(w_send, 0); if (w_saved) U->set_enabled(w_saved, 0);
    status(TR("Sending..."), SUB);
    dsl_log("bugrep: Send (%zu bytes of text, contact %s, screenshot %s, logs %s)", strlen(t), c1[0] ? "given" : "none", j->shot ? "yes" : "no", j->logs ? "yes" : "no");
}
static void saved_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    pthread_mutex_lock(&mu); int busy = sending; pthread_mutex_unlock(&mu);
    if (busy) return;
    Job *j = calloc(1, sizeof *j); if (!j) return;
    snprintf(j->only, sizeof j->only, "saved");
    pthread_mutex_lock(&mu); sending = 1; done_code = 0; pthread_mutex_unlock(&mu);
    pthread_t th;
    if (pthread_create(&th, NULL, send_thread, j)) { pthread_mutex_lock(&mu); sending = 0; pthread_mutex_unlock(&mu); free(j); return; }
    pthread_detach(th);
    U->set_enabled(w_send, 0); if (w_saved) U->set_enabled(w_saved, 0);
    status(TR("Sending..."), SUB);
    dsl_log("bugrep: Send saved reports");
}
static void cancel_click(void *ud, DSLObj w) { (void)ud; (void)w; close_window(); }
static void privacy_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    const char *u = dsl_crash_privacy_url(); dsl_log("bugrep: open the privacy note %s", u);
#ifdef _WIN32
    { wchar_t wu[512]; if (!dsl_u8_to_w(u, wu, 512)) ShellExecuteW(NULL, L"open", wu, NULL, NULL, SW_SHOWNORMAL); }
#else
    pid_t pid; int st; char *av[] = { "/usr/bin/open", (char *)u, NULL };
    if (posix_spawn(&pid, av[0], NULL, NULL, av, environ) == 0) waitpid(pid, &st, 0);
#endif
}
static void tshot_change(void *ud, DSLObj w) { (void)ud; if (w_prev) U->set_active(w_prev, U->toggle_get(w) != 0); }
static void saved_label(void) {
    if (!w_saved) return;
    char t[120]; snprintf(t, sizeof t, TR("Send saved reports (%d)"), unsent_n);
    U->set_text(w_saved, t); U->set_active(w_saved, unsent_n > 0);
}
static void esc_close(void *ud);
static void build(void) {
    const float L = 32, CW = 600, RX = 664, RW = WIN_W - RX - 32;
    /* the window template (kit window_std): dim backdrop, header, the game's red X and Escape (= Cancel), UI Scale */
    DSLWinStd o = { .size = sizeof o, .kind = DSL_WIN_MODAL, .w = WIN_W, .h = WIN_H, .title = TR("Bug report"), .close = esc_close,
                    .band_h = BAND_H, .title_x = L, .title_size = 30 };
    DSLWinParts pt; w_win = U->window_std(&o, &pt); w_back = pt.backdrop;
    if (!w_win) return;
    U->tooltip(pt.close_x, NULL, TR("Close (Esc)"));
    U->text(w_win, L, 96, CW, 30, TR("What happened? What did you expect?"), 18, DSL_TXT_LEFT, GOLD);
    w_text = U->field(w_win, L, 130, CW, 290, TR("For example: I opened the shop and the game froze. I expected to buy a sword."), "", NULL, NULL);
    dsl_ui_field_multiline(w_text, DSL_BUG_TEXT_MAX);
    U->text(w_win, L, 434, CW, 30, TR("Contact (optional): your Discord name, so we can ask you questions"), 16, DSL_TXT_LEFT | DSL_TXT_WRAP, GOLD);
    w_contact = U->field(w_win, L, 466, CW, 44, TR("Discord name (optional)"), "", NULL, NULL);
    w_tshot = U->toggle(w_win, RX, 130, RW, TR("Include a screenshot"), shot_ok, tshot_change, NULL);
    float pw = RW, ph = RW * 9 / 16; if (shot_ok && shot_w > 0) ph = RW * shot_h / shot_w;
    if (ph > 200) { pw = pw * 200 / ph; ph = 200; }
    w_prev = shot_ok ? U->image_png(w_win, RX, 180, pw, ph, shot_path, 0) : NULL;
    w_noprev = !w_prev ? U->text(w_win, RX, 180, RW, 60, TR("No screenshot could be taken."), 15, DSL_TXT_LEFT | DSL_TXT_WRAP, SUB) : NULL;
    if (!shot_ok && w_tshot) { U->toggle_set(w_tshot, 0); U->set_enabled(w_tshot, 0); }
    w_tlogs = U->toggle(w_win, RX, 396, RW, TR("Include logs"), 1, NULL, NULL);
    U->text(w_win, RX, 440, RW, 70, TR("Game and mod logs, the mod list, versions, your system. Names and folders are taken out."), 14, DSL_TXT_LEFT | DSL_TXT_WRAP, SUB);
    U->text(w_win, L, 526, CW, 50, TR("The report goes to the Dungeon Settlers Modded developer only. No IP address is kept. Reports are deleted after 90 days."), 14, DSL_TXT_LEFT | DSL_TXT_WRAP, SUB);
    U->button(w_win, RX, 530, 200, 40, TR("Privacy note"), privacy_click, NULL);
    w_status = U->text(w_win, L, 586, 560, 92, status_s, 15, DSL_TXT_LEFT | DSL_TXT_WRAP, 0);
    w_saved = U->button(w_win, RX, 584, RW, 40, "", saved_click, NULL);    /* right column, above Cancel / Send: never under the status */
    w_cancel = U->button(w_win, WIN_W - 32 - 260 - 12 + 0, 636, 130, 48, TR("Cancel"), cancel_click, NULL);
    w_send = U->button(w_win, WIN_W - 32 - 130, 636, 130, 48, TR("Send"), send_click, NULL);
    saved_label();
}
static void close_window(void) {
    if (!is_open) return;
    if (w_win) U->window_close(w_win);                                 /* the window and its backdrop */
    w_back = w_win = w_text = w_contact = w_tshot = w_tlogs = w_prev = w_noprev = w_status = w_send = w_cancel = w_saved = NULL;
    is_open = 0;
    dsl_log("bugrep: window closed");
}
static void esc_close(void *ud) { (void)ud; close_window(); }
static void open_window(void) {
    if (!U) U = dsl_ui_kit();
    if (!U) { dsl_log("bugrep: no widget kit: the window cannot open"); return; }
    if (is_open) close_window();
    base_dir();
    pthread_mutex_lock(&mu); unsent_n = count_unsent(); int busy = sending; pthread_mutex_unlock(&mu);
    status_s[0] = 0;
    build();
    is_open = w_win != NULL;
    if (!is_open) { close_window(); dsl_log("bugrep: the window could not be built"); return; }
    if (busy) { U->set_enabled(w_send, 0); status(TR("Sending..."), SUB); }
    dsl_log("bugrep: window open (from the %s, screenshot %s)", want_src == 2 ? "Esc menu" : "title", shot_ok ? "yes" : "no");
}
int dsl_bugrep_is_open(void) { return is_open; }

/* ------------------------------------------------------------------ the two stock buttons */
static void (*o_title)(Obj, const void *), (*o_world)(Obj, const void *);
static void h_title(Obj self, const void *mi) { (void)self; (void)mi; dsl_log("bugrep: title Bug Report pressed: our window (the stock form does not open)"); want = 1; want_src = 1; }
static void h_world(Obj self, const void *mi) {
    (void)mi; dsl_log("bugrep: Esc menu Bug Report pressed: our window (the stock form does not open)");
    if (h_menu) { void (*fr)(uintptr_t) = dsl_il2cpp_fn("il2cpp_gchandle_free"); if (fr) fr(h_menu); h_menu = 0; }
    uintptr_t (*nw)(Obj, uint8_t) = dsl_il2cpp_fn("il2cpp_gchandle_new");
    if (self && nw) h_menu = nw(self, 0);
    want = 1; want_src = 2;
}
static Obj menu_obj(void) {
    Obj (*gt)(uintptr_t) = dsl_il2cpp_fn("il2cpp_gchandle_get_target");
    return h_menu && gt ? gt(h_menu) : NULL;
}
static void menu_hide(int hide) {                                     /* the Esc menu out of the shot: scale 0, then back */
    Obj v = menu_obj(); Obj t = v && m_comp_tr ? CALL(m_comp_tr, Obj, Obj)(v, m_comp_tr) : NULL;
    if (!t || !m_tr_getscale || !m_tr_setscale) return;
    if (hide) { menu_scale = CALL(m_tr_getscale, V3, Obj)(t, m_tr_getscale); CALL(m_tr_setscale, void, Obj, V3)(t, (V3){ 0, 0, 0 }, m_tr_setscale); }
    else CALL(m_tr_setscale, void, Obj, V3)(t, menu_scale.x || menu_scale.y ? menu_scale : (V3){ 1, 1, 1 }, m_tr_setscale);
}
void dsl_bugrep_install(void) {
    if (hooked) return;
    unity_init();
    void *t = dsl_find_method_address("Refactor.UI", "TitleMenuView", "OnBugReportPressed", 0);
    void *w = dsl_find_method_address("Refactor.UI", "SystemMenuView", "OnReportBugPressed", 0);
    int rt = t ? dsl_hook(DSL_OWNER_LOADER, t, (void *)h_title, (void **)&o_title, "loader bug report title button") : -1;
    int rw = w ? dsl_hook(DSL_OWNER_LOADER, w, (void *)h_world, (void **)&o_world, "loader bug report Esc menu button") : -1;
    hooked = 1;
    dsl_log("bugrep: title Bug Report %s, Esc menu Bug Report %s", !rt ? "opens our window" : "NOT replaced", !rw ? "opens our window" : "NOT replaced");
}

/* ------------------------------------------------------------------ test driver (DSL_BUGREP_TEST) */
static const char *test_dir; static int test_checked;
static void jesc(char *o, size_t n, const char *s) { size_t k = 0; for (; s && *s && k + 3 < n; s++) { if (*s == '"' || *s == '\\') o[k++] = '\\'; o[k++] = *s == '\n' ? ' ' : *s; } o[k] = 0; }
static void test_frame(void) {
    char p[1200]; snprintf(p, sizeof p, "%s/cmd.json", test_dir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char buf[16384]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    char dn[1300]; snprintf(dn, sizeof dn, "%s/cmd.done.json", test_dir); rename(p, dn);
    JNode *j = json_parse(buf); const char *op = json_str(j, "op"), *seq = json_str(j, "seq"), *tx = json_str(j, "text");
    int ok = 1;
    int dsl_ui_test_type(DSLObj field, const char *chars);
    if (!op) ok = 0;
    else if (!strcmp(op, "type") && w_text) { U->field_set(w_text, ""); ok = dsl_ui_test_type(w_text, tx ? tx : "") >= 0; }
    else if (!strcmp(op, "set_text") && w_text) U->field_set(w_text, tx ? tx : "");
    else if (!strcmp(op, "contact") && w_contact) U->field_set(w_contact, tx ? tx : "");
    else if (!strcmp(op, "tick")) { const char *wh = json_str(j, "which"); DSLObj t = wh && !strcmp(wh, "shot") ? w_tshot : w_tlogs;
                                    if (t) { U->toggle_set(t, !U->toggle_get(t)); if (t == w_tshot) tshot_change(NULL, t); } else ok = 0; }
    else if (!strcmp(op, "send")) send_click(NULL, NULL);
    else if (!strcmp(op, "send_saved")) saved_click(NULL, NULL);
    else if (!strcmp(op, "cancel")) close_window();
    else if (!strcmp(op, "esc")) { if (is_open && !typing()) close_window(); }
    else if (!strcmp(op, "shot")) { char sp[1300]; snprintf(sp, sizeof sp, "%s/%s.png", test_dir, json_str(j, "name") ? json_str(j, "name") : "shot"); ok = !dsl_soft_shot(sp); }
    else if (strcmp(op, "state")) ok = 0;
    static char text[4 * DSL_BUG_TEXT_MAX + 4]; text[0] = 0; if (w_text) U->field_get(w_text, text, sizeof text);
    char te[600], se[1500], ie[200], cn[300] = "", ce[300]; jesc(te, sizeof te, text); jesc(se, sizeof se, status_s);
    if (w_contact) U->field_get(w_contact, cn, sizeof cn);
    jesc(ce, sizeof ce, cn);
    pthread_mutex_lock(&mu); jesc(ie, sizeof ie, last_id); int sn = sending, un = unsent_n; pthread_mutex_unlock(&mu);
    char out[4096]; int on = snprintf(out, sizeof out, "{\"seq\":\"%s\",\"ok\":%d,\"open\":%d,\"src\":%d,\"shot\":%d,\"shot_w\":%d,\"shot_h\":%d,\"shot_bytes\":%ld,"
                                     "\"shot_path\":\"%s\",\"tick_shot\":%d,\"tick_logs\":%d,\"text_len\":%zu,\"text\":\"%s\",\"contact\":\"%s\",\"sending\":%d,\"unsent\":%d,"
                                     "\"status\":\"%s\",\"id\":\"%s\",\"dir\":\"%s\"}\n",
                                     seq ? seq : "", ok, is_open, want_src, shot_ok, shot_w, shot_h, shot_bytes, shot_path, w_tshot ? U->toggle_get(w_tshot) : -1,
                                     w_tlogs ? U->toggle_get(w_tlogs) : -1, strlen(text), te, ce, sn, un, se, ie, bdir);
    snprintf(p, sizeof p, "%s/result.json", test_dir); write_file(p, out, (size_t)on);
    json_free(j);
}

/* ------------------------------------------------------------------ every frame */
void dsl_bugrep_frame(void) {
    if (!test_checked) { test_checked = 1; test_dir = getenv("DSL_BUGREP_TEST"); if (test_dir && !*test_dir) test_dir = NULL; }
    if (want) {                                                       /* a Bug Report click: shot first, then the window */
        if (!phase) {
            want = 0; phase = 1; phase_frames = 0;
            if (is_open) close_window();
            if (want_src == 2 && menu_obj()) menu_hide(1);
        }
    }
    if (phase) {
        if (++phase_frames >= (want_src == 2 ? 4 : 2)) {
            shoot();
            if (want_src == 2) menu_hide(0);
            phase = 0;
            open_window();
        }
    }
    if (is_open) {
        pthread_mutex_lock(&mu); int dc = done_code; char m[1400]; snprintf(m, sizeof m, "%s", done_msg); if (dc) done_code = 0; int sn = sending; pthread_mutex_unlock(&mu);
        if (dc) {
            status(m, dc == 1 ? GOLD : WARN);
            if (dc == 1 && w_text) U->field_set(w_text, "");
            U->set_enabled(w_send, !sn); saved_label(); if (w_saved) U->set_enabled(w_saved, !sn);
        }
        if (want_src == 2 && dsl_reg_scene() != 2) close_window();      /* the world was left under the window */
        else if (want_src == 1 && dsl_reg_scene() == 2) close_window();
    } else if (want_src == 2 && h_menu && !phase) {
        void (*fr)(uintptr_t) = dsl_il2cpp_fn("il2cpp_gchandle_free"); if (fr) fr(h_menu); h_menu = 0;
    }
    if (test_dir && !dsl_soft_wanted()) { static int k; if (++k % 3 == 0) test_frame(); }
}
