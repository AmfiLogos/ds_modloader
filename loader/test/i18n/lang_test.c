// Unit test: the shipped loader/lang/fr.json and ja.json load through ds_i18n.h with no entry dropped by the
// loader's checks, every en.json key resolves, and a TRC context key gives its own text. No game.
// Also: `lang_test modjson <mod.json> <fr name>` reads a mod.json with the loader's JSON reader and compares
// i18n.fr.name. Run: loader/test/i18n/run.sh
#define DSI_IMPLEMENTATION
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dsl_internal.h"
void dsl_log(const char *fmt, ...) { (void)fmt; }
static int fails;
static void check(const char *what, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); fails += !ok; }

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    if (b) b[n] = 0;
    fclose(f);
    return b;
}

/* every key of en.json gets a translated entry (a stored copy, so never the English pointer itself) */
static void lang_keys(const JNode *en, int lang, const char *code) {
    char what[160];
    int n = 0, miss = 0;
    dsi_set_lang(lang);
    snprintf(what, sizeof what, "%s: loaded, dsi_dropped() == 0 (dropped %d)", code, dsi_dropped());
    check(what, dsi_loaded() > 0 && dsi_dropped() == 0);
    for (const JNode *c = en->child; c; c = c->next) {
        const char *k = c->key, *bar = strstr(k, "||"), *ctx = strchr(k, '\x04');
        const char *r;
        if (k[0] == '_') continue;
        n++;
        if (bar) {
            char one[1024];
            size_t ol = (size_t)(bar - k);
            if (ol >= sizeof one) ol = sizeof one - 1;
            memcpy(one, k, ol); one[ol] = 0;
            r = dsi_trn(one, bar + 2, 2);
            if (r == one || r == bar + 2) { miss++; printf("     missing: %s\n", k); }
        } else if (ctx) {
            char cx[256];
            size_t cl = (size_t)(ctx - k);
            if (cl >= sizeof cx) cl = sizeof cx - 1;
            memcpy(cx, k, cl); cx[cl] = 0;
            r = dsi_trc(cx, ctx + 1);
            if (r == ctx + 1) { miss++; printf("     missing: %s\n", k); }
        } else {
            r = dsi_tr(k);
            if (r == k) { miss++; printf("     missing: %s\n", k); }
        }
    }
    snprintf(what, sizeof what, "%s: all %d en.json keys resolve (missing %d), loaded %d", code, n, miss, dsi_loaded());
    check(what, n > 0 && miss == 0 && dsi_loaded() == n);
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "modjson")) {
        char *t = slurp(argv[2]), what[600];
        JNode *j = t ? json_parse(t) : NULL;
        const char *n = j ? json_str(json_get(json_get(j, "i18n"), "fr"), "name") : NULL;
        snprintf(what, sizeof what, "%s: i18n.fr.name = \"%s\"", argv[2], n ? n : "(none)");
        check(what, n && !strcmp(n, argv[3]));
        json_free(j); free(t);
        return fails != 0;
    }
    if (argc != 3) { fprintf(stderr, "usage: lang_test <loader/lang> <tmp dir>\n"); return 2; }
    char path[1200], *t;
    snprintf(path, sizeof path, "%s/en.json", argv[1]);
    t = slurp(path);
    JNode *en = t ? json_parse(t) : NULL;
    check("en.json parses", en && en->t == JN_OBJ);
    if (!en) return 1;
    dsi_set_dir(argv[1]);
    lang_keys(en, 2, "fr");
    lang_keys(en, 7, "ja");

    // TRC: same English, other meaning; the plain key keeps its own text (de slot, from a test folder)
    snprintf(path, sizeof path, "%s/de.json", argv[2]);
    FILE *f = fopen(path, "wb");
    if (f) { fputs("{\"Host\": \"Gastgeber\", \"verb\\u0004Host\": \"Hosten\", \"bad %d\": \"schlecht %s\"}\n", f); fclose(f); }
    dsi_set_dir(argv[2]);
    dsi_set_lang(3);
    check("TRC(\"verb\", \"Host\") = Hosten", !strcmp(TRC("verb", "Host"), "Hosten"));
    check("TR(\"Host\") = Gastgeber", !strcmp(TR("Host"), "Gastgeber"));
    check("TRC with an unknown context = English", !strcmp(TRC("noun", "Host"), "Host"));
    check("a %d -> %s entry is dropped (dsi_dropped() == 1)", dsi_dropped() == 1);
    dsi_set_lang(0);
    check("English: TRC gives the English", !strcmp(TRC("verb", "Host"), "Host"));
    json_free(en); free(t);
    printf("== %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
