// Minimal JSON reader for mod.json / profile.json (objects, arrays, strings with \" \\ \/ \n \t \r \b \f \uXXXX (written
// as UTF-8, surrogate pairs too), numbers, bools, null).
#include "dsl_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct { const char *p; int depth; } P;

static void ws(P *s) { while (*s->p && isspace((unsigned char)*s->p)) s->p++; }
static JNode *node(JType t) { JNode *n = calloc(1, sizeof *n); if (n) n->t = t; return n; }

static int hex4(const char *p, unsigned *v) {
    *v = 0;
    for (int i = 0; i < 4; i++) {
        if (!isxdigit((unsigned char)p[i])) return 0;
        *v = *v * 16 + (unsigned)(isdigit((unsigned char)p[i]) ? p[i] - '0' : (tolower((unsigned char)p[i]) - 'a' + 10));
    }
    return 1;
}
static char *pstr(P *s) {
    if (*s->p != '"') return NULL;
    s->p++;
    size_t cap = 32, n = 0; char *o = malloc(cap);
    while (*s->p && *s->p != '"') {
        char c = *s->p++;
        if (c == '\\') {
            c = *s->p++;
            if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; else if (c == 'r') c = '\r'; else if (c == 'b') c = '\b'; else if (c == 'f') c = '\f';
            else if (c == 'u') {                                         /* \uXXXX -> UTF-8 (Python's json.dumps writes these) */
                unsigned v, w;
                if (!hex4(s->p, &v)) { free(o); return NULL; }
                s->p += 4;
                if (v >= 0xD800 && v < 0xDC00 && s->p[0] == '\\' && s->p[1] == 'u' && hex4(s->p + 2, &w) && w >= 0xDC00 && w < 0xE000) {
                    v = 0x10000 + ((v - 0xD800) << 10) + (w - 0xDC00); s->p += 6;
                }
                if ((v >= 0xD800 && v < 0xE000) || v == 0) v = 0xFFFD;   /* lone surrogate, NUL: the replacement character */
                char u[4]; int k = 0;
                if (v < 0x80) u[k++] = (char)v;
                else if (v < 0x800) { u[k++] = (char)(0xC0 | v >> 6); u[k++] = (char)(0x80 | (v & 63)); }
                else if (v < 0x10000) { u[k++] = (char)(0xE0 | v >> 12); u[k++] = (char)(0x80 | (v >> 6 & 63)); u[k++] = (char)(0x80 | (v & 63)); }
                else { u[k++] = (char)(0xF0 | v >> 18); u[k++] = (char)(0x80 | (v >> 12 & 63)); u[k++] = (char)(0x80 | (v >> 6 & 63)); u[k++] = (char)(0x80 | (v & 63)); }
                if (n + (size_t)k + 1 > cap) o = realloc(o, cap = (cap + (size_t)k) * 2);
                memcpy(o + n, u, (size_t)k); n += (size_t)k;
                continue;
            }
            else if (c != '"' && c != '\\' && c != '/') { free(o); return NULL; }
        }
        if (n + 2 > cap) o = realloc(o, cap *= 2);
        o[n++] = c;
    }
    if (*s->p != '"') { free(o); return NULL; }
    s->p++; o[n] = 0; return o;
}

static JNode *val(P *s) {
    ws(s);
    if (++s->depth > 32) return NULL;
    JNode *n = NULL;
    char c = *s->p;
    if (c == '{' || c == '[') {
        n = node(c == '{' ? JN_OBJ : JN_ARR); s->p++;
        JNode **tail = &n->child;
        ws(s);
        if (*s->p == (c == '{' ? '}' : ']')) { s->p++; s->depth--; return n; }
        for (;;) {
            char *key = NULL;
            if (c == '{') { ws(s); key = pstr(s); if (!key) goto bad; ws(s); if (*s->p++ != ':') { free(key); goto bad; } }
            JNode *v = val(s);
            if (!v) { free(key); goto bad; }
            v->key = key; *tail = v; tail = &v->next;
            ws(s);
            if (*s->p == ',') { s->p++; continue; }
            if (*s->p == (c == '{' ? '}' : ']')) { s->p++; break; }
            goto bad;
        }
    } else if (c == '"') {
        char *str = pstr(s); if (!str) return NULL;
        n = node(JN_STR); n->str = str;
    } else if (!strncmp(s->p, "true", 4)) { n = node(JN_BOOL); n->b = 1; s->p += 4; }
    else if (!strncmp(s->p, "false", 5)) { n = node(JN_BOOL); s->p += 5; }
    else if (!strncmp(s->p, "null", 4)) { n = node(JN_NULL); s->p += 4; }
    else if (c == '-' || isdigit((unsigned char)c)) { char *e; double d = strtod(s->p, &e); if (e == s->p) return NULL; n = node(JN_NUM); n->num = d; s->p = e; }
    s->depth--;
    return n;
bad:
    json_free(n); return NULL;
}

JNode *json_parse(const char *text) {
    if (!text) return NULL;
    P s = { text, 0 };
    JNode *n = val(&s);
    if (!n) return NULL;
    ws(&s);
    if (*s.p) { json_free(n); return NULL; }
    return n;
}

void json_free(JNode *n) {
    while (n) { JNode *nx = n->next; json_free(n->child); free(n->str); free(n->key); free(n); n = nx; }
}

JNode *json_get(const JNode *o, const char *key) {
    if (!o || o->t != JN_OBJ) return NULL;
    for (JNode *c = o->child; c; c = c->next) if (c->key && !strcmp(c->key, key)) return c;
    return NULL;
}
const char *json_str(const JNode *o, const char *key) { JNode *n = json_get(o, key); return n && n->t == JN_STR ? n->str : NULL; }
