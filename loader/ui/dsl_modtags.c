// dsl_modtags.c - a mod's tags in the loader's mod lists (owner 2026-11-02, PLAN_v013 item 33: "icons depending on if a
// mod is client only ... an icon for vanilla and vanilla plus versus actual Gameplay change ... a regular slogel for
// vanilla and vanilla plus mods ... an Abyss slogel" for gameplay changes; "I don't know how to make it clear that it's a
// mod and it's not just a random icon"). Like tModLoader's side icons (Config/UI/UIModConfigList.cs: a small icon at the
// row's right edge + a hover line), each mod row gets two badges: what the mod changes and who needs it, each with the
// kit's hover card, and the list explains the four icons once (dsl_modtags_legend).
// The rule (which tag): ../src/dsl_modtag_rule.h. THE template: dsl_modtags.h.
//
// Icons: made from the game's OWN sprites, by name, at run time, once per game start (nothing of the game is shipped):
//   slogel: ResourceLoader.LoadUnitObject("UNIT_MudSlogel") (the game's Mud Slogel): its SpriteRenderer's sprite;
//   abyss:  ResourceLoader.LoadAbyssObject("UNIT_AbyssMudSlogel") (the game's Abyss Mud Slogel): its sprite through the
//           values of its material (AbyssBasicMaterial, shader AllIn1SpriteShader): negative, contrast + brightness, HSV,
//           then overlay (texture x colour x glow, multiplied, blend). That order and the HSV matrix are the shader's
//           (the wiki's pipeline/abyss_shader.py checked them against the compiled Metal fragment); a step whose
//           keyword is off in the material is skipped;
//   all:    the loaded sprite "Icon_Population" (the game's population icon: three people);
//   one:    the loaded sprite "PortraitNone" (one person) in the population icon's two colours, with its outline.
// A game texture cannot be read (not readable): it is drawn into a RenderTexture (Graphics.Blit) and read back
// (Texture2D.ReadPixels), trimmed to its visible pixels and written as a PNG into <profile dir>/.cache/modtag_<n>.png
// (the kit's image_png reads files). An icon that cannot be made: its badge shows the frame only, and the log says
// "MODTAG-MISSING <icon> (<why>)" (loader/test/modtags_test.py fails on it).
#include "../src/dsl_internal.h"
#include "dsl_modtags.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_(p) _mkdir(p)
#else
#define mkdir_(p) mkdir(p, 0755)
#endif

typedef void *Obj;
typedef struct { float x, y, w, h; } Rect4;
typedef struct { float r, g, b, a; } Col4;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define CALL(mi, R, ...) ((R (*)(__VA_ARGS__, const void *))(*(void **)(mi)))
#define CALL0(mi, R) ((R (*)(const void *))(*(void **)(mi)))

static const char *const ICON_NAME[DSL_TAG_N] = { "slogel", "abyss", "one", "all" };
/* the legend's labels = the hover cards' titles; one plain line each (9 languages: lang/<code>.json) */
static const char *label_of(int t) {
    return t == DSL_TAG_SLOGEL ? TR("Vanilla & Vanilla+") : t == DSL_TAG_ABYSS ? TR("Changes the gameplay")
         : t == DSL_TAG_ONE ? TR("Only your game") : TR("Every player needs it");
}
static const char *body_of(int t) {
    return t == DSL_TAG_SLOGEL ? TR("This mod does not change how the game is played.")
         : t == DSL_TAG_ABYSS ? TR("This mod changes how the game is played.")
         : t == DSL_TAG_ONE ? TR("Only your game uses this mod. Other players in a multiplayer game do not need it.")
         : TR("In a multiplayer game, every player needs this mod.");
}
const char *dsl_modtags_kind_id(int kind) { return kind == DSL_KIND_VANILLA ? "vanilla" : kind == DSL_KIND_GAMEPLAY ? "gameplay" : ""; }
const char *dsl_modtags_side_id(int side) { return side == DSL_SIDE_CLIENT ? "client" : side == DSL_SIDE_ALL ? "all" : ""; }

/* ---------------------------------------------------------------- Unity by name */
static struct {
    Obj (*str)(const char *); Obj (*onew)(void *); void *(*parent)(void *);
    void *(*methods)(void *, void **); const char *(*mname)(void *); uint32_t (*pcount)(void *);
    void *(*param)(void *, uint32_t); char *(*tname)(void *); void (*il_free)(void *);
    void *(*ctype)(void *); Obj (*tobj)(void *);
} X;
static void *k_rl, *k_sr, *k_sprite, *k_tex2d;
static void *m_load_unit, *m_load_abyss, *m_children, *m_sr_sprite, *m_r_mat, *m_spr_tex, *m_spr_rect, *m_tex_w, *m_tex_h,
            *m_rt_get, *m_rt_rel, *m_rt_active, *m_rt_setactive, *m_blit, *m_t2d_ctor, *m_read, *m_pix32, *m_destroy, *m_name,
            *m_mat_float, *m_mat_color, *m_mat_tex, *m_mat_has, *m_mat_kw;
static int bound;
static char miss[300];
/* a method of ns.k (or a parent) by name + parameter types ("" for none; NULL = any with that count) */
static void *msig(const char *ns, const char *k, const char *name, int argc, const char *sig) {
    for (void *c = dsl_find_class(ns, k); c && X.methods; c = X.parent ? X.parent(c) : NULL) {
        void *it = NULL, *mi;
        while ((mi = X.methods(c, &it))) {
            if (strcmp(X.mname(mi), name) || (int)X.pcount(mi) != argc) continue;
            if (!sig) return mi;
            char got[256] = ""; size_t o = 0;
            for (int i = 0; i < argc && o < sizeof got; i++) {
                char *t = X.tname(X.param(mi, (uint32_t)i));
                o += (size_t)snprintf(got + o, sizeof got - o, "%s%s", i ? "," : "", t ? t : "?");
                if (t && X.il_free) X.il_free(t);
            }
            if (!strcmp(got, sig)) return *(void **)mi ? mi : NULL;
        }
    }
    size_t o = strlen(miss); snprintf(miss + o, sizeof miss - o, " %s.%s/%d", k, name, argc);
    return NULL;
}
static int bind(void) {
    if (bound) return bound > 0;
    bound = -1;
    *(void **)&X.str = dsl_il2cpp_fn("il2cpp_string_new"); *(void **)&X.onew = dsl_il2cpp_fn("il2cpp_object_new");
    *(void **)&X.parent = dsl_il2cpp_fn("il2cpp_class_get_parent"); *(void **)&X.methods = dsl_il2cpp_fn("il2cpp_class_get_methods");
    *(void **)&X.mname = dsl_il2cpp_fn("il2cpp_method_get_name"); *(void **)&X.pcount = dsl_il2cpp_fn("il2cpp_method_get_param_count");
    *(void **)&X.param = dsl_il2cpp_fn("il2cpp_method_get_param"); *(void **)&X.tname = dsl_il2cpp_fn("il2cpp_type_get_name");
    *(void **)&X.il_free = dsl_il2cpp_fn("il2cpp_free"); *(void **)&X.ctype = dsl_il2cpp_fn("il2cpp_class_get_type");
    *(void **)&X.tobj = dsl_il2cpp_fn("il2cpp_type_get_object");
    if (!X.str || !X.onew || !X.methods || !X.mname || !X.pcount || !X.param || !X.tname || !X.ctype || !X.tobj) { snprintf(miss, sizeof miss, " il2cpp api"); return 0; }
    k_rl = dsl_find_class("Refactor", "ResourceLoader"); k_sr = dsl_find_class("UnityEngine", "SpriteRenderer");
    k_sprite = dsl_find_class("UnityEngine", "Sprite"); k_tex2d = dsl_find_class("UnityEngine", "Texture2D");
    m_load_unit = msig("Refactor", "ResourceLoader", "LoadUnitObject", 1, "System.String");
    m_load_abyss = msig("Refactor", "ResourceLoader", "LoadAbyssObject", 1, "System.String");
    m_children = msig("UnityEngine", "Component", "GetComponentInChildren", 2, "System.Type,System.Boolean");
    m_sr_sprite = msig("UnityEngine", "SpriteRenderer", "get_sprite", 0, "");
    m_r_mat = msig("UnityEngine", "Renderer", "get_sharedMaterial", 0, "");
    m_spr_tex = msig("UnityEngine", "Sprite", "get_texture", 0, ""); m_spr_rect = msig("UnityEngine", "Sprite", "get_rect", 0, "");
    m_tex_w = msig("UnityEngine", "Texture", "get_width", 0, ""); m_tex_h = msig("UnityEngine", "Texture", "get_height", 0, "");
    m_rt_get = msig("UnityEngine", "RenderTexture", "GetTemporary", 3, "System.Int32,System.Int32,System.Int32");
    m_rt_rel = msig("UnityEngine", "RenderTexture", "ReleaseTemporary", 1, "UnityEngine.RenderTexture");
    m_rt_active = msig("UnityEngine", "RenderTexture", "get_active", 0, "");
    m_rt_setactive = msig("UnityEngine", "RenderTexture", "set_active", 1, "UnityEngine.RenderTexture");
    m_blit = msig("UnityEngine", "Graphics", "Blit", 2, "UnityEngine.Texture,UnityEngine.RenderTexture");
    m_t2d_ctor = msig("UnityEngine", "Texture2D", ".ctor", 4, "System.Int32,System.Int32,UnityEngine.TextureFormat,System.Boolean");
    m_read = msig("UnityEngine", "Texture2D", "ReadPixels", 3, "UnityEngine.Rect,System.Int32,System.Int32");
    m_pix32 = msig("UnityEngine", "Texture2D", "GetPixels32", 0, "");
    m_destroy = msig("UnityEngine", "Object", "Destroy", 1, "UnityEngine.Object");
    m_name = msig("UnityEngine", "Object", "get_name", 0, "");
    m_mat_float = msig("UnityEngine", "Material", "GetFloat", 1, "System.String");
    m_mat_color = msig("UnityEngine", "Material", "GetColor", 1, "System.String");
    m_mat_tex = msig("UnityEngine", "Material", "GetTexture", 1, "System.String");
    m_mat_has = msig("UnityEngine", "Material", "HasProperty", 1, "System.String");
    m_mat_kw = msig("UnityEngine", "Material", "IsKeywordEnabled", 1, "System.String");
    if (!k_sr || !k_sprite || !k_tex2d || !m_children || !m_sr_sprite || !m_spr_tex || !m_spr_rect || !m_tex_w || !m_tex_h || !m_rt_get
        || !m_rt_rel || !m_rt_active || !m_rt_setactive || !m_blit || !m_t2d_ctor || !m_read || !m_pix32 || !m_name) return 0;
    bound = 1; return 1;
}
static int alive(Obj o) { return o && AT(o, 0x10, void *) != NULL; }             /* UnityEngine.Object.m_CachedPtr */
static int name_is(Obj o, const char *want) {                                       /* Object.name == want (ASCII) */
    Obj s = alive(o) ? CALL(m_name, Obj, Obj)(o, m_name) : NULL; if (!s) return 0;
    int32_t n = AT(s, 0x10, int32_t); const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    if ((size_t)n != strlen(want)) return 0;
    for (int32_t i = 0; i < n; i++) if (c[i] != (unsigned char)want[i]) return 0;
    return 1;
}

/* ---------------------------------------------------------------- pixels */
typedef struct { int w, h; uint8_t *px; } Img;                                      /* RGBA, row 0 = top */
static void img_free(Img *m) { free(m->px); m->px = NULL; m->w = m->h = 0; }
/* the (x, y, w, h) part of a texture (y from the bottom, Unity's way) into *out, through a RenderTexture */
static int tex_read(Obj tex, int x, int y, int w, int h, Img *out, char *why, size_t wn) {
    int tw = CALL(m_tex_w, int, Obj)(tex, m_tex_w), th = CALL(m_tex_h, int, Obj)(tex, m_tex_h);
    if (tw <= 0 || th <= 0) { snprintf(why, wn, "texture %dx%d", tw, th); return 0; }
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > tw) w = tw - x; if (y + h > th) h = th - y;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) { snprintf(why, wn, "part %d,%d %dx%d of %dx%d", x, y, w, h, tw, th); return 0; }
    Obj rt = CALL(m_rt_get, Obj, int, int, int)(tw, th, 0, m_rt_get);
    if (!alive(rt)) { snprintf(why, wn, "no RenderTexture"); return 0; }
    Obj prev = CALL0(m_rt_active, Obj)(m_rt_active);
    CALL(m_blit, void, Obj, Obj)(tex, rt, m_blit);
    CALL(m_rt_setactive, void, Obj)(rt, m_rt_setactive);
    Obj t2 = X.onew(k_tex2d);
    if (t2) {
        CALL(m_t2d_ctor, void, Obj, int, int, int, bool)(t2, w, h, 4, false, m_t2d_ctor);   /* RGBA32, no mips */
        CALL(m_read, void, Obj, Rect4, int, int)(t2, (Rect4){ (float)x, (float)y, (float)w, (float)h }, 0, 0, m_read);
    }
    CALL(m_rt_setactive, void, Obj)(prev, m_rt_setactive);
    CALL(m_rt_rel, void, Obj)(rt, m_rt_rel);
    Obj arr = t2 ? CALL(m_pix32, Obj, Obj)(t2, m_pix32) : NULL;
    size_t n = arr ? AT(arr, 0x18, uintptr_t) : 0;
    int ok = arr && n == (size_t)w * (size_t)h;
    if (ok) {
        out->w = w; out->h = h; out->px = malloc(n * 4);
        if (!out->px) ok = 0;
        else for (int r = 0; r < h; r++) memcpy(out->px + (size_t)r * w * 4, (char *)arr + 0x20 + (size_t)(h - 1 - r) * w * 4, (size_t)w * 4);
    } else snprintf(why, wn, "read back %zu pixels, want %d", n, w * h);
    if (t2 && m_destroy) CALL(m_destroy, void, Obj)(t2, m_destroy);
    return ok;
}
static void img_trim(Img *m) {                                                       /* to the visible pixels */
    int x0 = m->w, y0 = m->h, x1 = -1, y1 = -1;
    for (int y = 0; y < m->h; y++) for (int x = 0; x < m->w; x++)
        if (m->px[((size_t)y * m->w + x) * 4 + 3]) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    if (x1 < 0) { img_free(m); return; }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    for (int y = 0; y < h; y++) memmove(m->px + (size_t)y * w * 4, m->px + ((size_t)(y + y0) * m->w + x0) * 4, (size_t)w * 4);
    m->w = w; m->h = h;
}
static int sprite_img(Obj sp, Img *out, char *why, size_t wn) {                      /* a sprite's own frame, trimmed */
    if (!alive(sp)) { snprintf(why, wn, "no sprite"); return 0; }
    Obj tex = CALL(m_spr_tex, Obj, Obj)(sp, m_spr_tex);
    if (!alive(tex)) { snprintf(why, wn, "sprite without texture"); return 0; }
    Rect4 r = CALL(m_spr_rect, Rect4, Obj)(sp, m_spr_rect);
    if (!tex_read(tex, (int)lroundf(r.x), (int)lroundf(r.y), (int)lroundf(r.w), (int)lroundf(r.h), out, why, wn)) return 0;
    img_trim(out);
    if (!out->px) { snprintf(why, wn, "sprite has no visible pixel"); return 0; }
    return 1;
}

/* ---------------------------------------------------------------- PNG (stored deflate: tiny icons) */
static uint32_t crc_tab[256];
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; crc_tab[i] = v; }
    c = ~c; while (n--) c = crc_tab[(c ^ *p++) & 255] ^ (c >> 8); return ~c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n) {
    uint8_t h[8]; be32(h, n); memcpy(h + 4, type, 4); fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t c = crc(0, (const uint8_t *)type, 4); c = crc(c, d, n);
    uint8_t t[4]; be32(t, c); fwrite(t, 1, 4, f);
}
static int png_write(const char *path, const Img *m) {
    size_t raw_n = (size_t)m->h * (1 + (size_t)m->w * 4), blocks = raw_n / 65535 + 1;
    size_t z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_n), *z = malloc(z_n); if (!raw || !z) { free(raw); free(z); return -1; }
    for (int y = 0; y < m->h; y++) { raw[(size_t)y * (1 + m->w * 4)] = 0; memcpy(raw + (size_t)y * (1 + m->w * 4) + 1, m->px + (size_t)y * m->w * 4, (size_t)m->w * 4); }
    size_t o = 0; z[o++] = 0x78; z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t p = 0; p < raw_n || p == 0; ) {
        size_t n = raw_n - p > 65535 ? 65535 : raw_n - p;
        z[o++] = p + n >= raw_n; z[o++] = (uint8_t)n; z[o++] = (uint8_t)(n >> 8); z[o++] = (uint8_t)~n; z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, raw + p, n); o += n; p += n; if (!n) break;
    }
    be32(z + o, (b << 16) | a); o += 4;
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (f) {
        static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 }; fwrite(sig, 1, 8, f);
        uint8_t ih[13]; be32(ih, (uint32_t)m->w); be32(ih + 4, (uint32_t)m->h); ih[8] = 8; ih[9] = 6; ih[10] = ih[11] = ih[12] = 0;
        chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, (uint32_t)o); chunk(f, "IEND", NULL, 0);
        fclose(f);
    }
    free(raw); free(z);
    return f && !rename(tmp, path) ? 0 : -1;
}

/* ---------------------------------------------------------------- the four icons */
static struct { char path[1200]; int w, h, ok; } ic[DSL_TAG_N];
static int tries, nok;
static time_t tried_at;
static char cache_dir[1100];
static Obj unit_sprite(void *m_load, const char *key, Obj *mat, char *why, size_t wn) {
    Obj rl = k_rl ? dsl_view_get(k_rl, DSL_VIEW_NOW) : NULL;
    if (!rl || !m_load) { snprintf(why, wn, "no ResourceLoader%s", m_load ? "" : " method"); return NULL; }
    Obj u = CALL(m_load, Obj, Obj, Obj)(rl, X.str(key), m_load);
    if (!alive(u)) { snprintf(why, wn, "%s did not load", key); return NULL; }
    Obj r = CALL(m_children, Obj, Obj, Obj, bool)(u, X.tobj(X.ctype(k_sr)), true, m_children);
    if (!alive(r)) { snprintf(why, wn, "%s has no SpriteRenderer", key); return NULL; }
    if (mat) *mat = m_r_mat ? CALL(m_r_mat, Obj, Obj)(r, m_r_mat) : NULL;
    Obj sp = CALL(m_sr_sprite, Obj, Obj)(r, m_sr_sprite);
    if (!alive(sp)) snprintf(why, wn, "%s has no sprite", key);
    return alive(sp) ? sp : NULL;
}
/* the abyss look: the material's own values, the shader's steps (header) */
static float mf(Obj mat, const char *p, float dflt) {
    Obj s = X.str(p);
    if (m_mat_has && !CALL(m_mat_has, bool, Obj, Obj)(mat, s, m_mat_has)) return dflt;
    return CALL(m_mat_float, float, Obj, Obj)(mat, s, m_mat_float);
}
static int kw(Obj mat, const char *k) { return !m_mat_kw || CALL(m_mat_kw, bool, Obj, Obj)(mat, X.str(k), m_mat_kw); }
static int abyss_img(Img *m, Obj mat, char *why, size_t wn) {
    if (!alive(mat) || !m_mat_float || !m_mat_color) { snprintf(why, wn, "no abyss material"); return 0; }
    int neg = kw(mat, "NEGATIVE_ON"), con = kw(mat, "CONTRAST_ON"), hsv = kw(mat, "HSV_ON"), ovl = kw(mat, "OVERLAY_ON") && kw(mat, "OVERLAYMULT_ON");
    float na = mf(mat, "_NegativeAmount", 1), ct = mf(mat, "_Contrast", 1), br = mf(mat, "_Brightness", 0);
    float hs = mf(mat, "_HsvShift", 0), sat = mf(mat, "_HsvSaturation", 1), val = mf(mat, "_HsvBright", 1);
    float ob = mf(mat, "_OverlayBlend", 0), og = mf(mat, "_OverlayGlow", 1);
    Col4 oc = CALL(m_mat_color, Col4, Obj, Obj)(mat, X.str("_OverlayColor"), m_mat_color);
    float ot[3] = { 1, 1, 1 };                                                       /* the overlay texture's colour (one texel) */
    Obj otex = ovl && m_mat_tex ? CALL(m_mat_tex, Obj, Obj, Obj)(mat, X.str("_OverlayTex"), m_mat_tex) : NULL;
    if (alive(otex)) {
        int tw = CALL(m_tex_w, int, Obj)(otex, m_tex_w), th = CALL(m_tex_h, int, Obj)(otex, m_tex_h); Img t = { 0 }; char w2[120];
        if (tex_read(otex, tw / 2, th / 2, 1, 1, &t, w2, sizeof w2)) { for (int k = 0; k < 3; k++) ot[k] = t.px[k] / 255.f; img_free(&t); }
    }
    float ch = val * sat * cosf(hs * 3.14159265f / 180.f), sh = val * sat * sinf(hs * 3.14159265f / 180.f);
    float M[3][3] = { { .299f * val + .701f * ch + .168f * sh, .587f * val - .587f * ch + .330f * sh, .114f * val - .114f * ch - .497f * sh },
                      { .299f * val - .299f * ch - .328f * sh, .587f * val + .413f * ch + .035f * sh, .114f * val - .114f * ch + .292f * sh },
                      { .299f * val - .300f * ch + 1.25f * sh, .587f * val - .588f * ch - 1.05f * sh, .114f * val + .886f * ch - .203f * sh } };
    float om[3] = { ot[0] * oc.r * og, ot[1] * oc.g * og, ot[2] * oc.b * og };
    for (size_t i = 0; i < (size_t)m->w * m->h; i++) {
        uint8_t *p = m->px + i * 4; if (!p[3]) continue;
        float c[3] = { p[0] / 255.f, p[1] / 255.f, p[2] / 255.f };
        for (int k = 0; k < 3; k++) {
            if (neg) c[k] = c[k] + (1 - 2 * c[k]) * na;
            if (con) c[k] = (c[k] - .5f) * ct + .5f + br;
        }
        if (hsv) { float r = c[0], g = c[1], b = c[2]; for (int k = 0; k < 3; k++) c[k] = M[k][0] * r + M[k][1] * g + M[k][2] * b; }
        if (ovl) for (int k = 0; k < 3; k++) c[k] = c[k] + (c[k] * om[k] - c[k]) * ob;
        for (int k = 0; k < 3; k++) { float v = c[k] < 0 ? 0 : c[k] > 1 ? 1 : c[k]; p[k] = (uint8_t)lroundf(v * 255); }
    }
    if (dsl_soft_test_copy())
        dsl_log("modtags: abyss look: negative %d (%.3f) contrast %d (%.3f, %.3f) hsv %d (%.1f, %.3f, %.3f) overlay %d (%.3f x %.0f, colour %.3f %.3f %.3f, texel %.3f %.3f %.3f)",
                neg, na, con, ct, br, hsv, hs, sat, val, ovl, ob, og, oc.r, oc.g, oc.b, ot[0], ot[1], ot[2]);
    return 1;
}
/* one person: PortraitNone's shape in the population icon's light and dark colours, with a 1 px outline */
static int one_img(Img *m, const Img *pop) {
    uint8_t light[4] = { 215, 214, 194, 255 }, dark[4] = { 36, 33, 39, 255 };
    if (pop && pop->px) {                                                            /* its lightest and darkest opaque pixels */
        int lo = 1 << 30, hi = -1;
        for (size_t i = 0; i < (size_t)pop->w * pop->h; i++) {
            const uint8_t *p = pop->px + i * 4; if (p[3] < 255) continue;
            int l = p[0] * 3 + p[1] * 6 + p[2];
            if (l > hi) { hi = l; memcpy(light, p, 4); } if (l < lo) { lo = l; memcpy(dark, p, 4); }
        }
    }
    int w = m->w + 2, h = m->h + 2; uint8_t *o = calloc((size_t)w * h, 4); if (!o) return 0;
    for (int y = 0; y < m->h; y++) for (int x = 0; x < m->w; x++) if (m->px[((size_t)y * m->w + x) * 4 + 3])
        for (int dy = 0; dy < 3; dy++) for (int dx = 0; dx < 3; dx++) { uint8_t *q = o + ((size_t)(y + dy) * w + x + dx) * 4; if (!q[3]) memcpy(q, dark, 4); }
    for (int y = 0; y < m->h; y++) for (int x = 0; x < m->w; x++) if (m->px[((size_t)y * m->w + x) * 4 + 3]) memcpy(o + ((size_t)(y + 1) * w + x + 1) * 4, light, 4);
    free(m->px); m->px = o; m->w = w; m->h = h; img_trim(m);
    return m->px != NULL;
}
static void keep(int t, Img *m, const char *why) {
    if (m && m->px) {
        snprintf(ic[t].path, sizeof ic[t].path, "%s/modtag_%s.png", cache_dir, ICON_NAME[t]);
        if (!png_write(ic[t].path, m)) { ic[t].ok = 1; ic[t].w = m->w; ic[t].h = m->h; nok++; }
        else why = "cannot write the PNG";
    }
    if (!ic[t].ok) dsl_log("MODTAG-MISSING %s (%s)", ICON_NAME[t], why && why[0] ? why : "?");
    if (m) img_free(m);
}
static void icons_build(void) {
    if (nok == DSL_TAG_N || tries >= 3 || (tries && time(NULL) - tried_at < 5)) return;   /* at most 3 tries, 5 s apart */
    tries++; tried_at = time(NULL); nok = 0; memset(ic, 0, sizeof ic);
    const char *p = getenv("DSL_PROFILE"), *s = p ? strrchr(p, '/') : NULL, *b = p ? strrchr(p, '\\') : NULL;
    if (b > s) s = b;
    if (!s) { for (int t = 0; t < DSL_TAG_N; t++) dsl_log("MODTAG-MISSING %s (no DSL_PROFILE)", ICON_NAME[t]); return; }
    snprintf(cache_dir, sizeof cache_dir, "%.*s/.cache", (int)(s - p), p); mkdir_(cache_dir);
    if (!bind()) { for (int t = 0; t < DSL_TAG_N; t++) dsl_log("MODTAG-MISSING %s (not found:%s)", ICON_NAME[t], miss); return; }
    char why[200] = "", why2[200] = ""; Img m = { 0 }, pop = { 0 };
    /* Mud Slogel, then Abyss Mud Slogel (its own sprite through its own material) */
    Obj sp = unit_sprite(m_load_unit, "UNIT_MudSlogel", NULL, why, sizeof why);
    if (sp) sprite_img(sp, &m, why, sizeof why);
    keep(DSL_TAG_SLOGEL, &m, why);
    why[0] = 0; Obj mat = NULL;
    sp = unit_sprite(m_load_abyss, "UNIT_AbyssMudSlogel", &mat, why, sizeof why);
    if (sp && sprite_img(sp, &m, why, sizeof why) && !abyss_img(&m, mat, why, sizeof why)) img_free(&m);
    keep(DSL_TAG_ABYSS, &m, why);
    /* the population icon and the empty portrait: loaded sprites of the game, by name */
    Obj arr = dsl_view_assets(k_sprite, DSL_VIEW_NOW), pop_sp = NULL, one_sp = NULL;
    size_t n = arr ? AT(arr, 0x18, uintptr_t) : 0;
    for (size_t i = 0; i < n && (!pop_sp || !one_sp); i++) {
        Obj o = AT(arr, 0x20 + i * sizeof(void *), Obj);
        if (!pop_sp && name_is(o, "Icon_Population")) pop_sp = o; else if (!one_sp && name_is(o, "PortraitNone")) one_sp = o;
    }
    snprintf(why, sizeof why, "sprite Icon_Population not loaded (%zu sprites)", n);
    if (pop_sp) sprite_img(pop_sp, &pop, why, sizeof why);
    snprintf(why2, sizeof why2, "sprite PortraitNone not loaded (%zu sprites)", n);
    if (one_sp && sprite_img(one_sp, &m, why2, sizeof why2) && !one_img(&m, &pop)) img_free(&m);
    keep(DSL_TAG_ONE, &m, why2);
    keep(DSL_TAG_ALL, &pop, why);
    dsl_log("modtags: %d of %d icons made from the game's sprites (try %d)", nok, DSL_TAG_N, tries);
}
int dsl_modtags_icons_ok(void) { icons_build(); return nok; }

/* ---------------------------------------------------------------- drawing */
#define ICON_MAX 24.0f
static void icon_at(const DSLUi *U, DSLObj parent, float cx, float cy, float box, int t) {   /* centred on (cx, cy), aspect kept */
    if (!ic[t].ok || !U->image_png) return;
    float k = box / (float)(ic[t].w > ic[t].h ? ic[t].w : ic[t].h), w = ic[t].w * k, h = ic[t].h * k;
    U->image_png(parent, cx - w / 2, cy - h / 2, w, h, ic[t].path, 1);
}
static DSLObj badge(const DSLUi *U, DSLObj parent, float x, float y, int t) {
    DSLObj p = U->panel(parent, x, y, DSL_MODTAG_BADGE, DSL_MODTAG_BADGE, 0);         /* the game's panel frame: a tag, not a stray picture */
    if (!p) return NULL;
    icon_at(U, p, DSL_MODTAG_BADGE / 2, DSL_MODTAG_BADGE / 2, ICON_MAX, t);
    U->tooltip(p, label_of(t), body_of(t));
    return p;
}
int dsl_modtags_row(const DSLUi *U, DSLObj parent, float x, float y, const DSLModInfo *m, DSLObj *kind_out, DSLObj *side_out) {
    if (kind_out) *kind_out = NULL;
    if (side_out) *side_out = NULL;
    if (!U || !m) return 0;
    int k = dsl_modtag_kind(m->fix, m->vanilla_plus, m->scope), s = dsl_modtag_side(m->scope);
    if (!k || !s) { dsl_log("modtags: %s: no tag (scope '%s')", m->id, m->scope ? m->scope : "(none)"); return 0; }
    icons_build();
    DSLObj a = badge(U, parent, x, y, k == DSL_KIND_VANILLA ? DSL_TAG_SLOGEL : DSL_TAG_ABYSS);
    DSLObj b = badge(U, parent, x + DSL_MODTAG_BADGE + DSL_MODTAG_GAP, y, s == DSL_SIDE_CLIENT ? DSL_TAG_ONE : DSL_TAG_ALL);
    if (kind_out) *kind_out = a;
    if (side_out) *side_out = b;
    return (a != NULL) + (b != NULL);
}
float dsl_modtags_legend(const DSLUi *U, DSLObj parent, float x, float y, float w, DSLObj out[DSL_TAG_N]) {
    if (!U) return 0;
    icons_build();
    static const int order[DSL_TAG_N] = { DSL_TAG_SLOGEL, DSL_TAG_ABYSS, DSL_TAG_ONE, DSL_TAG_ALL };   /* line 1: kind, line 2: side */
    float cw = w / 2, lh = 26, top = y;
    for (int line = 0; line < 2; line++) {
        float hmax = lh;
        for (int col = 0; col < 2; col++) {
            int t = order[line * 2 + col];
            float cx = x + col * cw, tw = cw - 30 - 8;
            DSLObj cell = U->panel(parent, cx, y, cw - 8, lh, 0x00000001u);           /* the hover area (no visible fill) */
            icon_at(U, parent, cx + 11, y + lh / 2, 20, t);
            DSLObj tx = U->text(parent, cx + 28, y + 3, tw, 20, label_of(t), 14, DSL_TXT_LEFT | DSL_TXT_WRAP, 0xA69085FFu);   /* the Mods window's SUB */
            float ph = tx && U->text_height ? U->text_height(tx, tw) : 0;
            if (ph > 22) { U->place(tx, cx + 28, y + 3, tw, ph); if (ph + 6 > hmax) hmax = ph + 6; }
            if (cell) { U->place(cell, cx, y, cw - 8, hmax); U->tooltip(cell, label_of(t), body_of(t)); }
            if (out) out[t] = tx;
        }
        y += hmax + 2;
    }
    return y - top;
}
