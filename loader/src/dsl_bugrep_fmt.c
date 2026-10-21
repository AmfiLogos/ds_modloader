// dsl_bugrep_fmt.c - the bug report's text (pure C, no game or OS calls; unit test loader/test/crashrep/bugrep_test.c).
// Used by dsl_bugrep.c (the window). Wire format agreed with the receiver lane (crashrep/README.md "kind bug" is theirs):
//   gzip of UTF-8 text, first line "DSCRASH 1", the crash report's header lines (install .. mods), then
//   "kind: bug", "contact: <one line, max 200, not scrubbed: the player typed it to be found>", "reason: player bug report",
//   "===== bug.txt (N of N bytes) =====" + the player's text (scrubbed like a log),
//   "===== screenshot.jpg.b64 (N of N bytes, base64) =====" + standard base64 in 76-char lines (optional; NEVER scrubbed
//   or cut: a replaced name would break the image), then the log sections as crash reports ("===== dsload.log (n of m
//   bytes, the end of the file) =====", scrubbed). Too big: the logs are cut to 1/2, 1/4 ... of their tails; still too
//   big at 1/16: the screenshot is dropped and the text + logs kept.
#include "dsl_bugrep.h"
#include "dsl_scrub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *p; size_t n, cap; int bad; } Bb;
static void bb_put(Bb *b, const void *s, size_t n) {
    if (b->bad) return;
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 65536; while (c < b->n + n + 1) c *= 2;
        char *q = realloc(b->p, c); if (!q) { b->bad = 1; return; }
        b->p = q; b->cap = c;
    }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void bb_s(Bb *b, const char *s) { bb_put(b, s, strlen(s)); }
static void bb_scrubbed(Bb *b, const DslScrub *sc, const char *s, size_t n) {
    if (!n) return;
    size_t on = 0; char *c = sc ? dsl_scrub(sc, s, n, &on) : NULL;
    if (sc && !c) { b->bad = 1; return; }
    if (c) { bb_put(b, c, on); free(c); } else bb_put(b, s, n);
}
static char *ftail(const char *p, size_t max, size_t *len, long long *size) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); if (sz < 0) sz = 0;
    long from = (size_t)sz > max ? sz - (long)max : 0; fseek(f, from, SEEK_SET);
    size_t want = (size_t)(sz - from); char *b = malloc(want + 1);
    size_t n = b ? fread(b, 1, want, f) : 0; fclose(f);
    if (!b) return NULL;
    b[n] = 0; *len = n; *size = sz; return b;
}
size_t dsl_b64(const unsigned char *in, size_t n, char *out) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0, col = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)in[i] << 16 | (i + 1 < n ? (unsigned)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = T[v >> 18 & 63]; out[o++] = T[v >> 12 & 63];
        out[o++] = i + 1 < n ? T[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
        if ((col += 4) == 76 && i + 3 < n) { out[o++] = '\n'; col = 0; }
    }
    out[o] = 0; return o;
}
void dsl_bugrep_one_line(const char *in, char *out, size_t cap) {      /* contact: one line, no control bytes */
    size_t o = 0;
    for (const unsigned char *s = (const unsigned char *)(in ? in : ""); *s && o + 1 < cap; s++) out[o++] = *s < 32 ? ' ' : (char)*s;
    while (o && (out[o - 1] & 0xC0) == 0x80) o--;                       /* never half a UTF-8 letter at the cut */
    if (o && (unsigned char)out[o - 1] >= 0xC0) o--;
    out[o] = 0;
    while (o && out[o - 1] == ' ') out[--o] = 0;
}

int dsl_bugrep_build(const DslScrub *sc, const DslBugIn *in, size_t max_gz, unsigned char **gz, size_t *gn, int *shot_dropped) {
    *gz = NULL; *gn = 0; if (shot_dropped) *shot_dropped = 0;
    if (!in || !in->text || !in->text[0]) return -1;
    char *b64 = NULL; size_t b64n = 0;
    if (in->img && in->img_n) {
        b64 = malloc(in->img_n / 3 * 4 + in->img_n / 57 + 16); if (!b64) return -1;
        b64n = dsl_b64(in->img, in->img_n, b64);
    }
    char contact[DSL_BUG_CONTACT_MAX + 1]; dsl_bugrep_one_line(in->contact, contact, sizeof contact);
    int with_shot = b64 != NULL;
    for (;;) {
        for (int div = 1; div <= 16; div *= 2) {
            Bb b = { 0 };
            { Bb h = { 0 }; bb_s(&h, in->header ? in->header : "DSCRASH 1\n"); bb_s(&h, "kind: bug\n");
              bb_scrubbed(&b, sc, h.p ? h.p : "", h.n); free(h.p); }
            bb_s(&b, "contact: "); bb_s(&b, contact); bb_s(&b, "\nreason: player bug report\n");
            { size_t tn = strlen(in->text), cn = 0; char *ct = sc ? dsl_scrub(sc, in->text, tn, &cn) : NULL;
              if (sc && !ct) { free(b.p); free(b64); return -1; }
              const char *t = ct ? ct : in->text; if (!ct) cn = tn;
              char l[160]; snprintf(l, sizeof l, "===== bug.txt (%zu of %zu bytes) =====\n", cn, cn); bb_s(&b, l);
              bb_put(&b, t, cn); bb_s(&b, "\n"); free(ct); }
            if (with_shot) {
                char l[200]; snprintf(l, sizeof l, "===== %s.b64 (%zu of %zu bytes, base64) =====\n", in->img_name ? in->img_name : "screenshot.jpg", b64n, b64n);
                bb_s(&b, l); bb_put(&b, b64, b64n); bb_s(&b, "\n");
            }
            for (int i = 0; i < in->nlogs; i++) {
                size_t n = 0; long long sz = 0; char *t = ftail(in->logs[i].path, in->logs[i].cap / (size_t)div, &n, &sz);
                if (!t) continue;
                char l[300]; snprintf(l, sizeof l, "===== %s (%zu of %lld bytes, the end of the file) =====\n", in->logs[i].name, n, sz);
                bb_s(&b, l); bb_scrubbed(&b, sc, t, n); bb_s(&b, "\n"); free(t);
            }
            if (b.bad || !b.p) { free(b.p); free(b64); return -1; }
            unsigned char *z = NULL; size_t zn = 0; int r = dsl_gzip(b.p, b.n, &z, &zn); free(b.p);
            if (r) { free(b64); return -1; }
            if (zn <= max_gz) { *gz = z; *gn = zn; free(b64); if (shot_dropped) *shot_dropped = b64 && !with_shot; return 0; }
            free(z);
        }
        if (!with_shot) break;
        with_shot = 0;                                                  /* too big even with short logs: no screenshot */
    }
    free(b64);
    return -1;
}
