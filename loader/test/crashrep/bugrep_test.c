// TEST ONLY: the bug report's wire format (loader/src/dsl_bugrep_fmt.c), no game. It fails when the receiver would
// refuse a report (no "DSCRASH 1", no bug.txt) or show a broken screenshot (base64 changed by the name scrubber or cut),
// when personal data leaves the PC in the player's text, or when a report too big for the receiver is not made smaller
// (screenshot dropped, text kept).
//   bugrep_test <scratch dir>
#define DSI_IMPLEMENTATION
#include "dsl_internal.h"
#include "dsl_scrub.h"
#include "dsl_bugrep.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

void dsl_log(const char *fmt, ...) { (void)fmt; }
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *gunzip(const unsigned char *gz, size_t gn, size_t *n) {
    size_t cap = 64u << 20; char *o = malloc(cap); z_stream z = { 0 };
    if (!o || inflateInit2(&z, 31) != Z_OK) { free(o); return NULL; }
    z.next_in = (unsigned char *)gz; z.avail_in = (uInt)gn; z.next_out = (unsigned char *)o; z.avail_out = (uInt)cap - 1;
    int r = inflate(&z, Z_FINISH); *n = z.total_out; inflateEnd(&z);
    if (r != Z_STREAM_END) { free(o); return NULL; }
    o[*n] = 0; return o;
}
static int b64v(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1; }
/* the receiver's way: the section after its "===== <name> (" header up to the next "=====" header line, base64 decoded */
static unsigned char *section_b64(const char *t, const char *name, size_t *n) {
    char h[100]; snprintf(h, sizeof h, "===== %s (", name);
    const char *s = strstr(t, h); if (!s) return NULL;
    s = strchr(s, '\n'); if (!s) return NULL; s++;
    const char *e = strstr(s, "\n====="); if (!e) e = s + strlen(s);
    unsigned char *o = malloc((size_t)(e - s)); size_t k = 0; unsigned v = 0; int bits = 0;
    for (const char *p = s; p < e; p++) { int d = b64v(*p); if (d < 0) continue; v = v << 6 | (unsigned)d; bits += 6; if (bits >= 8) { bits -= 8; o[k++] = (unsigned char)(v >> bits); } }
    *n = k; return o;
}
static void write_log(const char *path, size_t n, const char *line) {
    FILE *f = fopen(path, "wb"); size_t l = strlen(line);
    for (size_t w = 0; w < n; w += l) fwrite(line, 1, l, f);
    fclose(f);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "/tmp";
    DslScrub sc; dsl_scrub_init(&sc); dsl_scrub_name(&sc, "Qorvex");
    /* an image whose base64 holds the scrubbed name "Qorvex" (decode "AQorvexA" -> bytes) and random-looking data */
    unsigned char img[3000]; size_t imn = 0;
    { const char *src = "AQorvexAAAAA"; unsigned v = 0; int bits = 0;
      for (const char *p = src; *p; p++) { v = v << 6 | (unsigned)b64v(*p); bits += 6; if (bits >= 8) { bits -= 8; img[imn++] = (unsigned char)(v >> bits); } }
      unsigned x = 12345; while (imn < sizeof img) { x = x * 1103515245u + 12345u; img[imn++] = (unsigned char)(x >> 16); } }
    char b64[5000]; dsl_b64(img, imn, b64);
    CHECK(strstr(b64, "Qorvex") != NULL, "test setup: the image's base64 should hold the scrubbed name");
    char lp[1200]; snprintf(lp, sizeof lp, "%s/bugrep_dsload.log", dir);
    write_log(lp, 200000, "12:00:01 loaded /Users/qorvex/Library/x.dylib for Qorvex\n");
    DslBugLog logs[2] = { { "dsload.log", lp, 768u << 10 }, { "missing.log", "/nonexistent/x.log", 1000 } };
    const char *hdr = "DSCRASH 1\ninstall: 0123456789abcdef0123456789abcdef\nloader: 0.1.21\ngame_build: 25846389\nplatform: mac-arm64\nos: macOS\ncpu: x\nsent: now\nmods: multiplayer 0.1.21 (on)\n";
    DslBugIn in = { hdr, "Qorvex#1234\nsecond line", "The shop froze.\nMy save is in /Users/qorvex/Library/Saves, ask Qorvex.", img, imn, "screenshot.jpg", logs, 2 };

    unsigned char *gz = NULL; size_t gn = 0; int dropped = -1;
    int r = dsl_bugrep_build(&sc, &in, DSL_BUG_GZ_MAX, &gz, &gn, &dropped);
    CHECK(!r && gz && gn > 0 && gn <= DSL_BUG_GZ_MAX && dropped == 0, "build: r %d, %zu bytes, dropped %d", r, gn, dropped);
    size_t tn = 0; char *t = gz ? gunzip(gz, gn, &tn) : NULL;
    CHECK(t != NULL, "the report does not unpack (gzip)");
    if (t) {
        CHECK(!strncmp(t, "DSCRASH 1\n", 10), "first line is not DSCRASH 1");
        CHECK(strstr(t, "\nkind: bug\n") && strstr(t, "\nreason: player bug report\n"), "kind / reason lines missing");
        CHECK(strstr(t, "\ncontact: Qorvex#1234 second line\n") != NULL, "contact: one line, given as typed (not scrubbed)");
        const char *bt = strstr(t, "===== bug.txt (");
        CHECK(bt && strstr(bt, "The shop froze."), "bug.txt section with the player's text missing");
        const char *sh = strstr(t, "===== screenshot.jpg.b64 (");
        CHECK(sh != NULL, "screenshot section missing");
        size_t dn = 0; unsigned char *dec = section_b64(t, "screenshot.jpg.b64", &dn);
        CHECK(dec && dn == imn && !memcmp(dec, img, imn), "screenshot: decoded %zu bytes, %s the image's %zu bytes", dn, dec && dn == imn && !memcmp(dec, img, imn) ? "same as" : "NOT", imn);
        free(dec);
        /* no personal data outside the screenshot (the base64 is not text) */
        char *cut = strdup(t); char *s0 = strstr(cut, "===== screenshot.jpg.b64 ("), *s1 = s0 ? strstr(s0 + 1, "\n=====") : NULL;
        if (s0 && s1) memmove(s0, s1, strlen(s1) + 1);
        char *c2 = strdup(cut); char *cl = strstr(c2, "\ncontact:"); if (cl) { char *e = strchr(cl + 1, '\n'); if (e) memmove(cl, e, strlen(e) + 1); }
        CHECK(!strstr(c2, "qorvex") && !strstr(c2, "Qorvex"), "a scrubbed name is still in the text or the logs");
        CHECK(strstr(c2, "===== dsload.log (") != NULL && !strstr(c2, "missing.log"), "log sections: dsload.log missing or a missing file listed");
        free(cut); free(c2);
        CHECK(sh && bt && bt < sh && strstr(sh, "===== dsload.log (") , "section order bug.txt, screenshot, logs");
        free(t);
    }
    free(gz);

    /* too big for the receiver: logs cut first, then no screenshot, the text stays */
    in.img = img; in.img_n = imn;
    r = dsl_bugrep_build(&sc, &in, 2600, &gz, &gn, &dropped);
    CHECK(!r && gn <= 2600 && dropped == 1, "small limit: r %d, %zu bytes, dropped %d", r, gn, dropped);
    t = !r ? gunzip(gz, gn, &tn) : NULL;
    CHECK(t && !strstr(t, "screenshot.jpg.b64") && strstr(t, "The shop froze."), "small limit: screenshot not dropped or text lost");
    free(t); free(gz);
    /* the receiver refuses a report without text */
    in.text = "";
    CHECK(dsl_bugrep_build(&sc, &in, DSL_BUG_GZ_MAX, &gz, &gn, &dropped) == -1, "an empty text must not make a report");
    remove(lp);
    printf("bugrep format: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
