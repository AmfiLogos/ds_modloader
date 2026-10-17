// Unit test: dsl_json.c turns \uXXXX escapes into UTF-8 (Python's json.dumps writes every non-ASCII character that
// way unless ensure_ascii=False). Before the fix they became '?', so a translated mod.json name showed as "????".
// Also: a mod.json "i18n" object is read like any other member. Run: loader/test/i18n/run.sh
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "dsl_internal.h"
void dsl_log(const char *fmt, ...) { (void)fmt; }
static int fails;
static void check(const char *what, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); fails += !ok; }
int main(void) {
    JNode *j = json_parse("{\"name\":\"Difficult\\u00e9 \\u5730\\u7344\",\"e\":\"\\ud83d\\ude00\",\"bad\":\"\\ud83d!\",\"i18n\":{\"fr\":{\"name\":\"L'Enfer\"}}}");
    check("parses", j != NULL);
    const char *n = json_str(j, "name");
    check("\\u00e9 and CJK \\u5730\\u7344 as UTF-8", n && !strcmp(n, "Difficult\xC3\xA9 \xE5\x9C\xB0\xE7\x8D\x84"));
    const char *e = json_str(j, "e");
    check("surrogate pair -> one 4-byte character", e && !strcmp(e, "\xF0\x9F\x98\x80"));
    const char *b = json_str(j, "bad");
    check("lone surrogate -> U+FFFD", b && !strcmp(b, "\xEF\xBF\xBD!"));
    check("i18n.fr.name", !strcmp(json_str(json_get(json_get(j, "i18n"), "fr"), "name") ? json_str(json_get(json_get(j, "i18n"), "fr"), "name") : "", "L'Enfer"));
    json_free(j);
    printf("== %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
