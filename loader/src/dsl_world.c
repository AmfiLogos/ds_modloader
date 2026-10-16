// Mods per world: refuse to load a world that was played with another gameplay mod set (launcher contract,
// ~/ds_launcher core/src/flow.rs Session). Active only when the launcher started the game (DSML_SESSION set).
//   hook:  Refactor.Main.Event.CampaignLoadingEventHandler.LoadCampaign(string)  (title Load list, Continue, rig and
//          MP loads all go through it; it only builds the coroutine, the work runs later in MoveNext)
//   guid:  the game's own TryGetSaveHeader(saveName, out CampaignSaveHeader, out CampaignSaveLoadSet); CampaignGuid @0
//   rule:  worlds[guid] exists and != this copy's modset -> refuse; unknown guid -> allow; a module that exports
//          dsl_world_check_skip() returning 1 (multiplayer guest: the HELLO check covers it) -> allow
//   refuse: the game's CancelIronModeLoad(notice, log) (closes the loading view, shows a popup with that text) and
//          the original coroutine object with its state set to "finished", so the caller's `yield return` ends at once.
// The world check also fills dsload_modset.json (dsl_main.c) with what the session file says.
#include "dsl_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "dsl_os.h"

#define REFUSE_TEXT "This world uses other mods."
/* in a world (Esc menu Load): no restart there (owner 2026-10-16 #10), flow K only from the title */
#define REFUSE_IN_WORLD "This world uses other mods. Go back to the title screen to load it."
int dsl_reg_scene(void);                             /* dsl_reg.c: MainFlow._currentScene, 1 title, 2 world */

static void *(*orig_load)(void *self, void *name, const void *mi);
static int (*try_header)(void *self, void *name, void *hdr, void **set, const void *mi);
static void (*cancel_notice)(void *self, void *notice, void *log, const void *mi);
static void *(*str_new)(const char *);
static int (*skip_fns[32])(void);
static int nskip;
int dsl_world_refusals;

static char *read_all(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (8 << 20)) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1); size_t r = fread(b, 1, (size_t)n, f); fclose(f); b[r] = 0; return b;
}

/* utf-16 il2cpp string -> ascii (enough for save names) */
static void str_ascii(void *s, char *out, size_t n) {
    out[0] = 0; if (!s) return;
    int32_t len = *(int32_t*)((char*)s + 0x10); const uint16_t *c = (const uint16_t*)((char*)s + 0x14);
    size_t k = 0; for (int32_t i = 0; i < len && k + 1 < n; i++) out[k++] = c[i] < 128 ? (char)c[i] : '?';
    out[k] = 0;
}
static void guid_text(const uint8_t *b, char *out) {
    snprintf(out, 40, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             *(const uint32_t*)b, *(const uint16_t*)(b + 4), *(const uint16_t*)(b + 6), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

static void *my_load(void *self, void *name, const void *mi) {
    void *it = orig_load(self, name, mi);
    char nm[256]; str_ascii(name, nm, sizeof nm);
    for (int i = 0; i < nskip; i++) if (skip_fns[i]()) { dsl_log("world check: load %s skipped (multiplayer guest: the join check covers it)", nm); return it; }
    const char *sp = getenv("DSML_SESSION"), *mine = getenv("DSML_MODSET");
    char *txt = sp ? read_all(sp) : NULL;
    JNode *s = txt ? json_parse(txt) : NULL; free(txt);
    if (!s) { dsl_log("world check: session file %s not readable: load %s allowed", sp ? sp : "?", nm); return it; }
    if (!mine || !*mine) mine = json_str(s, "modset");
    uint8_t hdr[0x100] = {0}; void *set = NULL;
    if (!try_header || !try_header(self, name, hdr, &set, NULL)) { dsl_log("world check: no header for %s: the game handles it", nm); json_free(s); return it; }
    char g[40]; guid_text(hdr, g);
    JNode *worlds = json_get(s, "worlds"), *w = NULL;
    if (worlds && worlds->t == JN_OBJ) for (JNode *c = worlds->child; c; c = c->next) if (c->key && !strcasecmp(c->key, g)) w = c;
    if (!w || w->t != JN_STR) { dsl_log("world check: %s (world %s) not registered by the launcher: allowed", nm, g); json_free(s); return it; }
    if (mine && dsl_key_same(w->str, mine)) { dsl_log("world check: %s (world %s) uses this mod set: allowed", nm, g); json_free(s); return it; }
    dsl_log("world check: REFUSED %s (world %s): the world's mod set %s, this copy %s", nm, g, w->str, mine ? mine : "?");
    int in_world = dsl_reg_scene() == 2;
    if (!in_world) dsl_world_mismatch(nm, g, w->str);   /* asks "Load it with them?" (flow K, dsl_reg.c) */
    json_free(s);
    dsl_world_refusals++;
    if (it) *(int32_t*)((char*)it + 0x10) = -2;      /* <>1__state: MoveNext returns false at once */
    if (cancel_notice && str_new) {
        char lg[200]; snprintf(lg, sizeof lg, "dsload: world %s needs another mod set", g);
        cancel_notice(self, str_new(in_world ? REFUSE_IN_WORLD : REFUSE_TEXT), str_new(lg), NULL);
    }
    return it;
}

/* after il2cpp_init; owner = the loader */
void dsl_world_install(void **modules, int n) {
    for (int i = 0; i < n && nskip < 32; i++) {
        int (*f)(void) = modules[i] ? (int (*)(void))dsl_os_dlsym(modules[i], "dsl_world_check_skip") : NULL;
        if (f) skip_fns[nskip++] = f;
    }
    if (!getenv("DSML_SESSION")) { dsl_log("world check: off (game not started by the launcher)"); return; }
    const char *ns = "Refactor.Main.Event", *k = "CampaignLoadingEventHandler";
    void *cls = dsl_find_class(ns, k);
    void *m_load = cls ? dsl_find_method(cls, "LoadCampaign", 1) : NULL;
    void *m_hdr = cls ? dsl_find_method(cls, "TryGetSaveHeader", 3) : NULL;
    void *m_cancel = cls ? dsl_find_method(cls, "CancelIronModeLoad", 2) : NULL;
    try_header = m_hdr ? dsl_method_address(m_hdr) : NULL;
    cancel_notice = m_cancel ? dsl_method_address(m_cancel) : NULL;
    str_new = dsl_ga_sym("il2cpp_string_new");
    void *a = m_load ? dsl_method_address(m_load) : NULL;
    if (!a || !try_header) { dsl_log("world check: CampaignLoadingEventHandler methods not found: check OFF"); return; }
    if (dsl_hook(DSL_OWNER_LOADER, a, (void*)my_load, (void**)&orig_load, "loader world check LoadCampaign"))
        dsl_log("world check: hook refused: check OFF");
    else dsl_log("world check: on (session %s, this copy's mod set %s, %d skip callback(s))", getenv("DSML_SESSION"),
                 getenv("DSML_MODSET") ? getenv("DSML_MODSET") : "(from the session file)", nskip);
}
