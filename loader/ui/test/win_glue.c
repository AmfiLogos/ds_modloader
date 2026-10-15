// win_glue.c - TEST ONLY (never in a release): stands in for lane L1's side of ../../src/dsl_menu.h until L1 ships it,
// so the Mods window (../dsl_win.c) runs in the game: the frame hook, the mod list (mod.json files + the profile),
// groups.json (L1's format), Apply (pending.json + the loader's soft restart), one module tab built with DSLUi.
#include "../../src/dsl_internal.h"
#include "../../src/dsl_menu.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void dsl_ui_install(void);
void dsl_frame_hook(void) { if (dsl_win_frame) dsl_win_frame(); }

static char *rd(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1); size_t r = fread(b, 1, (size_t)n, f); fclose(f); b[r] = 0; return b; }
static int wr(const char *p, const char *t) { char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", p); FILE *f = fopen(tmp, "wb"); if (!f) return -1;
    fputs(t, f); fclose(f); return rename(tmp, p); }
static void beside(char *o, size_t n, const char *file) { const char *p = getenv("DSL_PROFILE"); const char *s = strrchr(p, '/'); snprintf(o, n, "%.*s/%s", (int)(s - p), p, file); }

/* ---- mods */
static DSLModInfo M[64]; static int nm; static char ids_en[64][64]; static int nen;
static int in_en(const char *id) { for (int i = 0; i < nen; i++) if (!strcmp(ids_en[i], id)) return 1; return 0; }
static void mods_load(void) {
    char *pt = rd(getenv("DSL_PROFILE")); JNode *pj = pt ? json_parse(pt) : NULL; free(pt);
    JNode *en = json_get(pj, "enabled"); nen = 0;
    if (en) for (JNode *c = en->child; c && nen < 64; c = c->next) if (c->t == JN_STR) snprintf(ids_en[nen++], 64, "%s", c->str);
    json_free(pj);
    const char *md = getenv("DSL_MODS_DIR"); DIR *d = opendir(md); struct dirent *e; char names[64][256]; int nn = 0;
    while (d && (e = readdir(d)) && nn < 64) if (e->d_name[0] != '.') snprintf(names[nn++], 256, "%s", e->d_name);
    if (d) closedir(d);
    qsort(names, (size_t)nn, 256, (int (*)(const void *, const void *))strcmp);
    for (int k = 0; k < nn; k++) {
        char p[1400]; snprintf(p, sizeof p, "%s/%s/mod.json", md, names[k]);
        char *t = rd(p); JNode *m = t ? json_parse(t) : NULL; free(t);
        const char *id = json_str(m, "id"); if (!id) { json_free(m); continue; }
        DSLModInfo *x = &M[nm++]; memset(x, 0, sizeof *x);
        x->id = strdup(id); x->name = strdup(json_str(m, "name") ? json_str(m, "name") : id);
        x->version = strdup(json_str(m, "version") ? json_str(m, "version") : ""); x->description = strdup(json_str(m, "description") ? json_str(m, "description") : "");
        x->scope = strdup(json_str(m, "scope") ? json_str(m, "scope") : "gameplay");
        x->enabled = in_en(id); x->active = x->enabled ? 1 : 0; x->reason = "";
        x->hidden = !strncmp(id, "dsltest", 7);
        if (!strcmp(id, "hell-difficulty") && x->enabled) { x->active = -1; x->reason = "needs hell-difficulty-data, which is not installed"; }
        json_free(m);
    }
}
int dsl_mods_count(void) { if (!nm) mods_load(); return nm; }
const DSLModInfo *dsl_mods_at(int i) { return i >= 0 && i < nm ? &M[i] : NULL; }

/* ---- groups ({"groups":[{"name","mods":[ids]}]}) */
static struct { char name[80]; char ids[64][64]; int n; } G[32]; static int ng, g_loaded;
static void g_load(void) {
    g_loaded = 1; ng = 0; char p[1300]; beside(p, sizeof p, "groups.json");
    char *t = rd(p); JNode *j = t ? json_parse(t) : NULL; free(t);
    JNode *a = json_get(j, "groups");
    if (a && a->t == JN_ARR) for (JNode *g = a->child; g && ng < 32; g = g->next) {
        const char *n = json_str(g, "name"); JNode *ms = json_get(g, "mods"); if (!n) continue;
        snprintf(G[ng].name, 80, "%s", n); G[ng].n = 0;
        if (ms) for (JNode *c = ms->child; c && G[ng].n < 64; c = c->next) if (c->t == JN_STR) snprintf(G[ng].ids[G[ng].n++], 64, "%s", c->str);
        ng++;
    }
    json_free(j);
}
static int g_save(void) {
    static char o[32768]; size_t k = (size_t)snprintf(o, sizeof o, "{\"groups\":[");
    for (int i = 0; i < ng; i++) { k += (size_t)snprintf(o + k, sizeof o - k, "%s{\"name\":\"%s\",\"mods\":[", i ? "," : "", G[i].name);
        for (int m = 0; m < G[i].n; m++) k += (size_t)snprintf(o + k, sizeof o - k, "%s\"%s\"", m ? "," : "", G[i].ids[m]);
        k += (size_t)snprintf(o + k, sizeof o - k, "]}"); }
    snprintf(o + k, sizeof o - k, "]}\n");
    char p[1300]; beside(p, sizeof p, "groups.json"); return wr(p, o);
}
int dsl_groups_count(void) { if (!g_loaded) g_load(); return ng; }
const char *dsl_group_name(int i) { return i >= 0 && i < ng ? G[i].name : ""; }
int dsl_group_mods(int i, const char **ids, int max) { if (i < 0 || i >= ng) return 0; int n = 0; for (; n < G[i].n && n < max; n++) ids[n] = G[i].ids[n]; return n; }
int dsl_group_save(const char *name, const char *const *ids, int n) {
    int i; for (i = 0; i < ng && strcmp(G[i].name, name); i++) {}
    if (i == ng) { if (ng >= 32) return -1; ng++; }
    snprintf(G[i].name, 80, "%s", name); G[i].n = 0;
    for (int k = 0; k < n && k < 64; k++) snprintf(G[i].ids[G[i].n++], 64, "%s", ids[k]);
    return g_save();
}
int dsl_group_delete(const char *name) {
    for (int i = 0; i < ng; i++) if (!strcmp(G[i].name, name)) { memmove(&G[i], &G[i + 1], sizeof G[0] * (size_t)(ng - i - 1)); ng--; return g_save(); }
    return -1;
}

/* ---- Apply */
static time_t applied;
int dsl_apply_mods(const char *const *ids, int n, char *why, size_t cap) {
    static char o[8192]; size_t k = (size_t)snprintf(o, sizeof o, "{\"enabled\":[");
    int f = 1;
    for (int i = 0; i < n; i++) { k += (size_t)snprintf(o + k, sizeof o - k, "%s\"%s\"", f ? "" : ",", ids[i]); f = 0; }
    for (int i = 0; i < nm; i++) if (M[i].hidden && M[i].enabled) { k += (size_t)snprintf(o + k, sizeof o - k, "%s\"%s\"", f ? "" : ",", M[i].id); f = 0; }
    snprintf(o + k, sizeof o - k, "]}\n");
    char p[1300]; beside(p, sizeof p, "pending.json");
    if (wr(p, o)) { snprintf(why, cap, "pending.json could not be written"); return -1; }
    if (dsl_soft_apply(p, NULL)) { snprintf(why, cap, "the soft restart refused (see dsload.log)"); return -1; }
    applied = time(NULL);
    return 0;
}
/* A: from Apply on. B (test approximation of L1's B state): until 1 s after it wrote handoff.json.shown (the swap) */
#include <sys/stat.h>
const char *dsl_busy_reason(void) {
    if (applied && time(NULL) - applied < 120) return "soft restart running";
    if (dsl_soft_wanted()) {
        char p[1300]; snprintf(p, sizeof p, "%s/softrestart/handoff.json.shown", getenv("DSL_LOG_DIR") ? getenv("DSL_LOG_DIR") : ".");
        struct stat st; if (stat(p, &st) || time(NULL) - st.st_mtime < 2) return "soft restart running";
    }
    return NULL;
}

/* ---- one module tab, DSLUi only */
static const DSLUi *U; static DSLObj t_out; static int clicks;
static void t_click(void *ud, DSLObj w) { (void)ud; (void)w; char b[64]; snprintf(b, sizeof b, "Last: button %d", ++clicks); U->set_text(t_out, b); }
static void t_open(void *ud, DSLObj root, float w, float h) {
    (void)ud; (void)h; U = dsl_ui_kit();
    U->text(root, 0, 0, w, 32, "A module tab (test)", 20, DSL_TXT_LEFT | DSL_TXT_TITLE, 0xD9BE8FFF);
    U->button(root, 0, 50, 220, 44, "Click me", t_click, NULL);
    static const char *const v[] = { "Off", "Low", "High" };
    U->value_row(root, 0, 110, 540, "A value row", v, 3, 1, NULL, NULL);
    t_out = U->text(root, 0, 170, 540, 40, "Last: nothing", 16, DSL_TXT_LEFT, 0);
}
static DSLTab T1 = { "test", "Test Tab", 50, t_open, NULL, NULL, NULL, NULL };
int dsl_tab_count(void) { return getenv("DSL_UI_TEST_TAB") ? 1 : 0; }
const DSLTab *dsl_tab_at(int i) { return i == 0 && dsl_tab_count() ? &T1 : NULL; }
const char *dsl_tab_owner(int i) { (void)i; return "test"; }

__attribute__((visibility("default"))) int dsltest_start(void) { dsl_ui_install(); dsl_win_init(); return 0; }
