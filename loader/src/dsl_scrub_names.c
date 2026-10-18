// dsl_scrub_names.c - crash reports: the exact names of this PC for dsl_scrub() (user name, full name, home folder and
// computer name from the OS part, Steam's account and persona names from config/loginusers.vdf, the multiplayer names
// in mp.json / mp_menu.json / mp_history.json of the log folder). Apart from dsl_crash.c so the unit test runs it.
#include "dsl_internal.h"
#include "dsl_scrub.h"
#include "dsl_update_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *p, size_t max) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    char *b = malloc(max + 1); size_t n = b ? fread(b, 1, max, f) : 0; fclose(f);
    if (b) b[n] = 0; return b;
}
static void add_name(void *ud, const char *s) {
    DslScrub *sc = ud; dsl_scrub_name(sc, s);
    const char *a = strrchr(s, '/'), *b = strrchr(s, '\\'); if (b > a) a = b;
    if (a && a[1]) dsl_scrub_name(sc, a + 1);                    /* the last folder of a home path = the user name */
}
static void vdf_names(DslScrub *sc, const char *path) {          /* Steam config/loginusers.vdf: "AccountName" / "PersonaName" */
    char *b = slurp(path, 256 << 10); if (!b) return;
    const char *keys[] = { "\"AccountName\"", "\"PersonaName\"" };
    for (int k = 0; k < 2; k++) for (char *x = b; (x = strstr(x, keys[k])); x++) {
        char *q = strchr(x + strlen(keys[k]), '"'); if (!q) break;
        char *e2 = strchr(q + 1, '"'); if (!e2) break;
        char v[160]; snprintf(v, sizeof v, "%.*s", (int)(e2 - q - 1) < 159 ? (int)(e2 - q - 1) : 159, q + 1); dsl_scrub_name(sc, v);
    }
    free(b);
}
static void json_names(DslScrub *sc, const char *path) {         /* mp.json / mp_menu.json "name", mp_history.json "host" */
    char *b = slurp(path, 256 << 10); if (!b) return;
    JNode *j = json_parse(b); free(b); if (!j) return;
    const char *n = json_str(j, "name"); if (n) dsl_scrub_name(sc, n);
    if (j->t == JN_ARR) for (JNode *c = j->child; c; c = c->next) { const char *h = json_str(c, "host"); if (h) dsl_scrub_name(sc, h); }
    json_free(j);
}
void dsl_scrub_collect(DslScrub *sc, const char *logdir) {
    dsl_scrub_init(sc);
    dsl_uos_identity(add_name, sc);
    char sd[1100], p[1300]; if (!dsl_uos_steam_dir(sd, sizeof sd)) { snprintf(p, sizeof p, "%s/config/loginusers.vdf", sd); vdf_names(sc, p); }
    const char *mp[] = { "mp.json", "mp_menu.json", "mp_history.json", "data/mp.json", "data/mp_menu.json", "data/mp_history.json" };
    for (size_t i = 0; i < sizeof mp / sizeof mp[0]; i++) { snprintf(p, sizeof p, "%s/%s", logdir, mp[i]); json_names(sc, p); }
}
