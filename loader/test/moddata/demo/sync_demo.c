// sync_demo.c - test module of the mod data folder (moddata_pair.py): logs its folder (DSLHost.mod_data_dir, API 6)
// and what campaign/a.json holds when the game starts. mod.json "sync": ["campaign/*.json"].
#include "dsl_sdk.h"
#include <stdio.h>

DSL_PLUGIN();
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *self) {
    const char *d = dsl_mod_data_dir(h, self);
    char p[1200], t[200] = ""; FILE *f = NULL;
    if (d) { snprintf(p, sizeof p, "%s/campaign/a.json", d); f = fopen(p, "r"); }
    if (f) { size_t n = fread(t, 1, sizeof t - 1, f); t[n] = 0; fclose(f); }
    h->log(self, "sync-demo: data folder %s, campaign/a.json \"%s\"", d ? d : "(none: loader older than API 6)", t);
    return 0;
}
