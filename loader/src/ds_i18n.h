/* ds_i18n.h - small string tables for the Dungeon Settlers mods (pure C, no game calls).
 *
 * The same file ships in ds_modloader, ds_multiplayer and ds_custommodes; keep the copies identical
 * (tools/i18n_check.py compares them).
 *
 * English text is the key:  TR("Host a world")  returns the text in the current language, or the
 * English text when the language file has no entry. Plural:  TRN("%d unit", "%d units", n).
 * Language files: <dir>/<code>.json, one JSON object per language, UTF-8:
 *     { "Host a world": "Héberger un monde",
 *       "%d unit||%d units": ["%d unité", "%d unités"] }
 * A plural value lists the language's forms in this order: en, de, es, fr, pt-BR [one, other];
 * ru [one, few, many]; ko, ja, zh-Hans, zh-Hant [other] (one form). See dsi_plural_index().
 * An entry whose printf conversions or <rich tags> differ from the English key is dropped at load,
 * so a translated format can never crash a printf sink.
 *
 * Codes follow the game's LanguageType (Refactor.Setting.LanguageType):
 *   0 en, 1 ko, 2 fr, 3 de, 4 ru, 5 zh-Hans, 6 zh-Hant, 7 ja, 8 es, 9 pt-BR.
 *
 * Windows: the folder name is UTF-8 (opened with _wfopen).
 * Use: exactly one .c file of each dylib/exe does  #define DSI_IMPLEMENTATION  before the include.
 * Not thread-safe: set the language and look up on one thread (the game's main thread).
 */
#ifndef DS_I18N_H
#define DS_I18N_H

#include <stddef.h>

#if defined(__GNUC__) && !defined(_WIN32)
#define DSI_HIDDEN __attribute__((visibility("hidden")))
#else
#define DSI_HIDDEN
#endif

#define DSI_LANGS 10

/* -DDSI_PREFIX=xx_ renames the public functions (xx_tr ...): two string tables in one image (Windows rig DLL) */
#ifdef DSI_PREFIX
#define DSI_CAT2(a, b) a##b
#define DSI_CAT(a, b) DSI_CAT2(a, b)
#define dsi_set_dir DSI_CAT(DSI_PREFIX, set_dir)
#define dsi_set_lang DSI_CAT(DSI_PREFIX, set_lang)
#define dsi_lang DSI_CAT(DSI_PREFIX, lang)
#define dsi_code DSI_CAT(DSI_PREFIX, code)
#define dsi_code_of DSI_CAT(DSI_PREFIX, code_of)
#define dsi_generation DSI_CAT(DSI_PREFIX, generation)
#define dsi_tr DSI_CAT(DSI_PREFIX, tr)
#define dsi_trn DSI_CAT(DSI_PREFIX, trn)
#define dsi_lang_from_usersetting DSI_CAT(DSI_PREFIX, lang_from_usersetting)
#define dsi_utf8_clip DSI_CAT(DSI_PREFIX, utf8_clip)
#define dsi_utf8_fix_end DSI_CAT(DSI_PREFIX, utf8_fix_end)
#define dsi_loaded DSI_CAT(DSI_PREFIX, loaded)
#define dsi_dropped DSI_CAT(DSI_PREFIX, dropped)
#endif

/* the compiler checks printf arguments against the English key */
#if defined(__GNUC__) || defined(__clang__)
#define DSI_FMT1 __attribute__((format_arg(1)))
#define DSI_FMT2 __attribute__((format_arg(2)))
#else
#define DSI_FMT1
#define DSI_FMT2
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* folder that holds <code>.json (copied; NULL or "" = no files, English only) */
DSI_HIDDEN void dsi_set_dir(const char *dir);
/* game LanguageType 0..9 (anything else = English). Loads the file on first use.
   Returns 1 when the language changed (then redraw text), else 0. */
DSI_HIDDEN int dsi_set_lang(int game_lang);
DSI_HIDDEN int dsi_lang(void);                    /* current LanguageType */
DSI_HIDDEN const char *dsi_code(void);            /* "en", "fr", "zh-Hans", ... */
DSI_HIDDEN const char *dsi_code_of(int game_lang);
DSI_HIDDEN unsigned dsi_generation(void);         /* +1 on every change; for caches */
DSI_HIDDEN const char *dsi_tr(const char *en) DSI_FMT1;
DSI_HIDDEN const char *dsi_trn(const char *one, const char *other, long n) DSI_FMT1 DSI_FMT2;
/* LanguageType from the text of UserSetting.json (GeneralSettingData.SaveCurrentLanguageType), or 0 */
DSI_HIDDEN int dsi_lang_from_usersetting(const char *json_text);
/* cut s so it fits cap bytes (cap includes the NUL) without splitting a UTF-8 character */
DSI_HIDDEN void dsi_utf8_clip(char *s, size_t cap);
/* drop a broken UTF-8 sequence at the end of s (after a byte-capped snprintf) */
DSI_HIDDEN void dsi_utf8_fix_end(char *s);
/* entries loaded / dropped by validation for the current language (logs, tests) */
DSI_HIDDEN int dsi_loaded(void);
DSI_HIDDEN int dsi_dropped(void);

#ifdef __cplusplus
}
#endif

#define TR(s) dsi_tr(s)
#define TRN(one, other, n) dsi_trn(one, other, (long)(n))
/* marks a string for the extractor without translating it now (tables of English keys) */
#define TR_NOOP(s) (s)
/* plural pair for a table initializer: { TRN_NOOP("%d file", "%d files") } -> "%d file", "%d files" */
#define TRN_NOOP(one, other) one, other

#ifdef DSI_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <wchar.h>
/* declared here so the implementation file does not need <windows.h> */
__declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int, unsigned long, const char *, int, wchar_t *, int);
static FILE *dsi_fopen(const char *utf8_path) {
    wchar_t w[1200];
    if (MultiByteToWideChar(65001u /* CP_UTF8 */, 0, utf8_path, -1, w, 1200) <= 0) return NULL;
    return _wfopen(w, L"rb");
}
#else
#define dsi_fopen(p) fopen((p), "rb")
#endif

static const char *const dsi_codes[DSI_LANGS] = {
    "en", "ko", "fr", "de", "ru", "zh-Hans", "zh-Hant", "ja", "es", "pt-BR"};

typedef struct {
    const char *key;  /* English (plural: "one||other") */
    const char **val; /* nval forms */
    int nval;
} DsiEnt;

typedef struct {
    int tried;
    DsiEnt *tab; /* open addressing, cap is a power of 2 */
    size_t cap, n;
    int dropped;
} DsiLang;

static DsiLang dsi_l[DSI_LANGS];
static char dsi_dir[1024];
static int dsi_cur;
static unsigned dsi_gen;

static unsigned long dsi_hash(const char *s) {
    unsigned long h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

static const DsiEnt *dsi_find(const DsiLang *L, const char *key) {
    size_t i;
    if (!L->tab) return NULL;
    for (i = dsi_hash(key) & (L->cap - 1); L->tab[i].key; i = (i + 1) & (L->cap - 1))
        if (!strcmp(L->tab[i].key, key)) return &L->tab[i];
    return NULL;
}

static void dsi_put(DsiLang *L, const char *key, const char **val, int nval) {
    size_t i;
    if ((L->n + 1) * 2 > L->cap) {
        size_t oc = L->cap, j;
        DsiEnt *old = L->tab;
        L->cap = oc ? oc * 2 : 256;
        L->tab = (DsiEnt *)calloc(L->cap, sizeof(DsiEnt));
        if (!L->tab) { L->tab = old; L->cap = oc; return; }
        L->n = 0;
        for (j = 0; j < oc; j++)
            if (old[j].key) dsi_put(L, old[j].key, old[j].val, old[j].nval);
        free(old);
    }
    for (i = dsi_hash(key) & (L->cap - 1); L->tab[i].key; i = (i + 1) & (L->cap - 1))
        if (!strcmp(L->tab[i].key, key)) { L->tab[i].val = val; L->tab[i].nval = nval; return; }
    L->tab[i].key = key;
    L->tab[i].val = val;
    L->tab[i].nval = nval;
    L->n++;
}

/* ---- validation: printf conversions and <tags> must match the English key ---- */

/* writes the signature of s (conversions like "%d", "%.40s", tags like "<color=#ff0000>") into out */
static void dsi_sig(const char *s, char *out, size_t cap) {
    size_t o = 0;
    out[0] = 0;
    while (*s) {
        const char *b = s;
        if (*s == '%') {
            s++;
            if (*s == '%') { s++; continue; }
            while (*s && strchr("-+ #0123456789.*$hljztLqI", *s)) s++;
            if (*s) s++;
        } else if (*s == '<') {
            const char *e = strchr(s, '>');
            if (!e || e - s > 64) { s++; continue; }
            s = e + 1;
        } else {
            s++;
            continue;
        }
        if (o + (size_t)(s - b) + 2 >= cap) { out[0] = '!'; out[1] = 0; return; }
        memcpy(out + o, b, (size_t)(s - b));
        o += (size_t)(s - b);
        out[o++] = '\x1f';
        out[o] = 0;
    }
}

static int dsi_same_sig(const char *en, const char *tr) {
    char a[512], b[512];
    dsi_sig(en, a, sizeof a);
    dsi_sig(tr, b, sizeof b);
    return a[0] != '!' && !strcmp(a, b);
}

/* ---- tiny JSON reader: one object of string -> string | [string, ...] ---- */

typedef struct { const char *p, *e; } DsiJ;

static void dsi_ws(DsiJ *j) {
    while (j->p < j->e && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static int dsi_hex4(const char *p, unsigned *v) {
    int i;
    *v = 0;
    for (i = 0; i < 4; i++) {
        char c = p[i];
        *v <<= 4;
        if (c >= '0' && c <= '9') *v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') *v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') *v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    return 1;
}

static size_t dsi_put_utf8(char *o, unsigned c) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) {
        o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        o[2] = (char)(0x80 | (c & 0x3F)); return 3;
    }
    o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((c >> 6) & 0x3F)); o[3] = (char)(0x80 | (c & 0x3F)); return 4;
}

/* parses a JSON string at j->p (on the opening quote); returns a malloc'd UTF-8 copy or NULL */
static char *dsi_jstr(DsiJ *j) {
    const char *s;
    char *out, *o;
    if (j->p >= j->e || *j->p != '"') return NULL;
    s = ++j->p;
    while (j->p < j->e && *j->p != '"') j->p += (*j->p == '\\' && j->p + 1 < j->e) ? 2 : 1;
    if (j->p >= j->e) return NULL;
    out = o = (char *)malloc((size_t)(j->p - s) + 1);
    if (!out) return NULL;
    while (s < j->p) {
        if (*s != '\\') { *o++ = *s++; continue; }
        s++;
        switch (*s) {
        case 'n': *o++ = '\n'; s++; break;
        case 't': *o++ = '\t'; s++; break;
        case 'r': *o++ = '\r'; s++; break;
        case 'b': *o++ = '\b'; s++; break;
        case 'f': *o++ = '\f'; s++; break;
        case 'u': {
            unsigned c, d;
            if (j->p - s < 5 || !dsi_hex4(s + 1, &c)) { free(out); return NULL; }
            s += 5;
            if (c >= 0xD800 && c < 0xDC00 && j->p - s >= 6 && s[0] == '\\' && s[1] == 'u' &&
                dsi_hex4(s + 2, &d) && d >= 0xDC00 && d < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                s += 6;
            }
            if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
            o += dsi_put_utf8(o, c);
            break;
        }
        default: *o++ = *s++; break; /* \" \\ \/ */
        }
    }
    *o = 0;
    j->p++;
    return out;
}

static void dsi_load(DsiLang *L, int lang) {
    char path[1200], *buf;
    long len;
    FILE *f;
    DsiJ j;
    L->tried = 1;
    if (!dsi_dir[0] || lang <= 0) return;
    snprintf(path, sizeof path, "%s/%s.json", dsi_dir, dsi_codes[lang]);
    f = dsi_fopen(path);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > (64L << 20) || !(buf = (char *)malloc((size_t)len))) { fclose(f); return; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); return; }
    fclose(f);
    j.p = buf;
    j.e = buf + len;
    if (j.e - j.p >= 3 && (unsigned char)j.p[0] == 0xEF && (unsigned char)j.p[1] == 0xBB && (unsigned char)j.p[2] == 0xBF)
        j.p += 3;
    dsi_ws(&j);
    if (j.p >= j.e || *j.p != '{') { free(buf); return; }
    j.p++;
    for (;;) {
        char *key, *forms[8];
        int nf = 0, ok = 1, i;
        dsi_ws(&j);
        if (j.p < j.e && *j.p == '}') break;
        if (!(key = dsi_jstr(&j))) break;
        dsi_ws(&j);
        if (j.p >= j.e || *j.p != ':') { free(key); break; }
        j.p++;
        dsi_ws(&j);
        if (j.p < j.e && *j.p == '[') {
            j.p++;
            for (;;) {
                char *v;
                dsi_ws(&j);
                if (j.p < j.e && *j.p == ']') { j.p++; break; }
                if (!(v = dsi_jstr(&j))) { ok = 0; break; }
                if (nf < 8) forms[nf++] = v; else free(v);
                dsi_ws(&j);
                if (j.p < j.e && *j.p == ',') j.p++;
            }
        } else {
            char *v = dsi_jstr(&j);
            if (v) forms[nf++] = v; else ok = 0;
        }
        if (!ok) { free(key); for (i = 0; i < nf; i++) free(forms[i]); break; }
        if (key[0] != '_' && nf > 0) {
            /* plural key "one||other": every form must match the "other" English form */
            const char *bar = strstr(key, "||");
            const char *en = bar ? bar + 2 : key;
            int good = 1;
            for (i = 0; i < nf; i++) {
                if (bar) {
                    /* the one-form may drop the number ("une unité"), so match it to the English one-form too */
                    char one[512];
                    size_t ol = (size_t)(bar - key);
                    if (ol >= sizeof one) ol = sizeof one - 1;
                    memcpy(one, key, ol);
                    one[ol] = 0;
                    if (!dsi_same_sig(en, forms[i]) && !dsi_same_sig(one, forms[i])) good = 0;
                } else if (!dsi_same_sig(en, forms[i]) || !forms[i][0]) {
                    good = 0;
                }
            }
            if (good) {
                const char **v = (const char **)malloc(sizeof(char *) * (size_t)nf);
                if (v) {
                    for (i = 0; i < nf; i++) v[i] = forms[i];
                    dsi_put(L, key, v, nf);
                    key = NULL;
                    nf = 0;
                }
            } else {
                L->dropped++;
            }
        }
        free(key);
        for (i = 0; i < nf; i++) free(forms[i]);
        dsi_ws(&j);
        if (j.p < j.e && *j.p == ',') j.p++;
    }
    free(buf);
}

void dsi_set_dir(const char *dir) {
    int i;
    snprintf(dsi_dir, sizeof dsi_dir, "%s", dir ? dir : "");
    for (i = 0; i < DSI_LANGS; i++) if (!dsi_l[i].tab) dsi_l[i].tried = 0;
}

int dsi_set_lang(int game_lang) {
    if (game_lang < 0 || game_lang >= DSI_LANGS) game_lang = 0;
    if (!dsi_l[game_lang].tried) dsi_load(&dsi_l[game_lang], game_lang);
    if (game_lang == dsi_cur) return 0;
    dsi_cur = game_lang;
    dsi_gen++;
    return 1;
}

int dsi_lang(void) { return dsi_cur; }
const char *dsi_code_of(int game_lang) {
    return (game_lang >= 0 && game_lang < DSI_LANGS) ? dsi_codes[game_lang] : "en";
}
const char *dsi_code(void) { return dsi_codes[dsi_cur]; }
unsigned dsi_generation(void) { return dsi_gen; }
int dsi_loaded(void) { return (int)dsi_l[dsi_cur].n; }
int dsi_dropped(void) { return dsi_l[dsi_cur].dropped; }

const char *dsi_tr(const char *en) {
    const DsiEnt *e;
    if (!en || !dsi_cur) return en;
    e = dsi_find(&dsi_l[dsi_cur], en);
    return e ? e->val[0] : en;
}

/* CLDR cardinal plural form index for the language's form list (integers only) */
static int dsi_plural_index(int lang, long n) {
    long a = n < 0 ? -n : n, m10 = a % 10, m100 = a % 100;
    switch (lang) {
    case 1: case 5: case 6: case 7: return 0;              /* ko, zh, ja: one form */
    case 4:                                                /* ru: one, few, many */
        if (m10 == 1 && m100 != 11) return 0;
        if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return 1;
        return 2;
    case 2: case 9: return a <= 1 ? 0 : 1;                 /* fr, pt-BR: 0 and 1 are "one" */
    default: return a == 1 ? 0 : 1;                        /* en, de, es */
    }
}

const char *dsi_trn(const char *one, const char *other, long n) {
    char key[1024];
    const DsiEnt *e;
    int k;
    if (dsi_cur) {
        snprintf(key, sizeof key, "%s||%s", one, other);
        e = dsi_find(&dsi_l[dsi_cur], key);
        if (e) {
            k = dsi_plural_index(dsi_cur, n);
            return e->val[k < e->nval ? k : e->nval - 1];
        }
    }
    return (n == 1 || n == -1) ? one : other;
}

int dsi_lang_from_usersetting(const char *t) {
    const char *p = t ? strstr(t, "\"SaveCurrentLanguageType\"") : NULL;
    long v;
    if (!p) return 0;
    p += strlen("\"SaveCurrentLanguageType\"");
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':') p++;
    v = strtol(p, NULL, 10);
    return (v >= 0 && v < DSI_LANGS) ? (int)v : 0;
}

void dsi_utf8_fix_end(char *s) {
    size_t n = strlen(s), i = n, need;
    unsigned char c;
    while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80 && n - i < 3) i--;
    if (i == 0) { if (n && ((unsigned char)s[0] & 0xC0) == 0x80) s[0] = 0; return; }
    c = (unsigned char)s[i - 1];
    need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (c >= 0x80 && n - (i - 1) < need) s[i - 1] = 0;
}

void dsi_utf8_clip(char *s, size_t cap) {
    if (!s || !cap) return;
    if (strlen(s) < cap) return;
    s[cap - 1] = 0;
    dsi_utf8_fix_end(s);
}

#endif /* DSI_IMPLEMENTATION */
#endif /* DS_I18N_H */
