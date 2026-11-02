// getmods_test.c: driver for getmods_test.sh. Links the real src/dsl_hostmods.c + src/dsl_update.c + the Mac OS part
// (download, sha256, install) with small stubs for the game side (mods list = ROOT/loader/mods/*/mod.json, Apply = a line
// printed). The game is never involved.
//   getmods_test plan <host lines file> <vanilla>      prints the plan JSON
//   getmods_test get <host lines file> <env line>      runs the fix (frames until done) and prints STATE / WHY / APPLY
//   getmods_test check                                 the auto-update check (DSL_UPDATE unset; signed_test.sh): prints
//                                                      ROOT/update/ready.txt once written, "(none)" after 5 s
//   getmods_test packcode <name> <id> [<id>...]        mod pack code of these mods: CODE <code> | CODE -1 <why>
//   getmods_test packplan <code file>                  RET <n> + the pack plan JSON (waits for the names, as the window does)
//   getmods_test packget <code file>                   IMPORT <r> <why>, frames until saved / failed: STATE / WHY; the
//                                                      group stub prints GROUP <name>|<ids>|<settings, ';' between>
// Env: DSML_ROOT (ROOT), DSL_UPDATE_API (relserver.py), HM_ON / HM_EN (",a,b," running / enabled ids), HM_APPLY_REFUSE=1,
// HM_MYSET (mod settings lines), HM_FAIL (",a," enabled but not running: off by the crash guard), HM_DEP (",a," a missing
// requirement is the reason), HM_GROUP_REFUSE=1 (group save fails), HM_GROUPS ("<name>|<id,id>|<set;set>": one group the
// player already has).
#include "dsl_internal.h"
#include "dsl_menu.h"
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int dsl_hostmods_plan(const char *host_lines, int vanilla, char *out, size_t cap);
int dsl_hostmods_get(const char *host_lines, int vanilla, const char *env_lines, char *why, size_t cap);
int dsl_hostmods_state(int *pct, char *why, size_t cap);

/* ---- stubs */
void dsl_log(const char *fmt, ...) { va_list a; va_start(a, fmt); fprintf(stderr, "log: "); vfprintf(stderr, fmt, a); fputc('\n', stderr); va_end(a); }
const char *dsi_tr(const char *en) { return en; }
unsigned dsi_generation(void) { return 0; }
const char *dsi_code(void) { return getenv("HM_LANG") ? getenv("HM_LANG") : "en"; }
void dsi_utf8_fix_end(char *s) { (void)s; }
const DSLUi *dsl_ui_kit(void) { return NULL; }
void dsl_ui_place_tr(DSLObj w, float r, float t, float wd, float h) { (void)w; (void)r; (void)t; (void)wd; (void)h; }
int dsl_ui_title_shown(void) { return 0; }
int dsl_ui_widget_rect(DSLObj o, float r[4]) { (void)o; (void)r; return 0; }   /* dsl_update_corner_rect (the Load list's filter panel) */
DSLObj dsl_ui_group(DSLObj parent, float x, float y, float w, float h) { (void)parent; (void)x; (void)y; (void)w; (void)h; return NULL; }   /* dsl_update_frame: the title corner group (f011ec9) */
int dsl_ui_game_window_shown(void) { return 0; }       /* dsl_update_frame: the title corner hides while a game window shows (f011ec9) */
int dsl_reg_scene(void) { return 1; }
int dsl_soft_title_idle(void) { return 0; }
int dsl_soft_wanted(void) { return 0; }
static int applied;
int dsl_soft_busy(void) { return applied; }
const char *dsl_busy_reason(void) { return NULL; }
/* v0.1.21 "Restart and update now" (dsl_update.c): never reached in these tests (no game, no restart) */
int dsl_soft_prewarm_state(void) { return 0; }
void dsl_soft_prewarm_stop(void) {}
void *dsl_find_method_address(const char *ns, const char *klass, const char *method, int argc) { (void)ns; (void)klass; (void)method; (void)argc; return NULL; }
int dsl_sos_starter_known(void) { return 0; }
long dsl_sos_start_starter(void) { return -1; }
int dsl_mod_hashes(const char *id, char *out, size_t cap) { (void)id; if (cap) out[0] = 0; return 0; }
int dsl_apply_mods_env(const char *const *ids, int n, const char *env, char *why, size_t cap) {
    if (getenv("HM_APPLY_REFUSE")) { snprintf(why, cap, "refused by the test"); return -1; }
    printf("APPLY");
    for (int i = 0; i < n; i++) printf("%s%s", i ? "," : " ", ids[i]);
    printf(" ENV %s", env ? env : "");
    applied = 1; return 0;
}
/* mod settings (lane modsettings), found by name like in the loader: HM_MYSET = this game's "<mod>/<setting>@<value>"
   lines ('|' between); take prints what it was given */
int dsl_settings_lines(const char *mod, char *out, size_t cap) {
    const char *e = getenv("HM_MYSET"); char b[2048]; snprintf(b, sizeof b, "%s", e ? e : ""); size_t k = 0, ml = strlen(mod); int c = 0;
    out[0] = 0;
    for (char *sv = NULL, *t = strtok_r(b, "|", &sv); t; t = strtok_r(NULL, "|", &sv))
        if (!strncmp(t, mod, ml) && t[ml] == '/') { k += (size_t)snprintf(out + k, cap - k, "%s%s", c++ ? "\n" : "", t); }
    return c;
}
int dsl_settings_lines_edit(const char *mod, char *out, size_t cap) { return dsl_settings_lines(mod, out, cap); }   /* the next Apply's values */
int dsl_mod_is_difficulty(const char *id) { (void)id; return 0; }
int dsl_win_is_open(void) { return 0; }                    /* no Mods window: dsl_hostmods_frame ends a pack import */
/* groups: an in-memory list (HM_GROUPS = one the player already has); save_ex prints the saved group */
static struct { char name[80], ids[64][64], set[2048]; int n; } G[8]; static int ng, gloaded;
static void groups_load(void) {
    if (gloaded) return; gloaded = 1;
    const char *e = getenv("HM_GROUPS"); if (!e || !*e) return;
    char b[2400]; snprintf(b, sizeof b, "%s", e); char *a = strchr(b, '|'), *c = a ? strchr(a + 1, '|') : NULL; if (!c) return;
    *a = 0; *c = 0; snprintf(G[0].name, sizeof G[0].name, "%s", b); G[0].n = 0;
    for (char *sv = NULL, *t = strtok_r(a + 1, ",", &sv); t && G[0].n < 64; t = strtok_r(NULL, ",", &sv)) snprintf(G[0].ids[G[0].n++], 64, "%s", t);
    snprintf(G[0].set, sizeof G[0].set, "%s", c + 1); for (char *x = G[0].set; *x; x++) if (*x == ';') *x = '\n';
    ng = 1;
}
int dsl_groups_count(void) { groups_load(); return ng; }
const char *dsl_group_name(int i) { groups_load(); return i >= 0 && i < ng ? G[i].name : ""; }
const char *dsl_group_settings(int i) { groups_load(); return i >= 0 && i < ng ? G[i].set : ""; }
int dsl_group_mods(int i, const char **ids, int max) {
    groups_load(); int n = 0; if (i < 0 || i >= ng) return 0;
    for (; n < G[i].n && n < max; n++) ids[n] = G[i].ids[n];
    return n;
}
int dsl_group_save_ex(const char *name, const char *const *ids, int n, const char *settings) {
    groups_load();
    if (getenv("HM_GROUP_REFUSE") && !strcmp(getenv("HM_GROUP_REFUSE"), "1")) return -1;
    int g = 0; while (g < ng && strcmp(G[g].name, name)) g++;
    if (g == ng) { if (ng >= 8) return -1; ng++; }
    snprintf(G[g].name, sizeof G[g].name, "%s", name); G[g].n = 0;
    for (int i = 0; i < n && i < 64; i++) snprintf(G[g].ids[G[g].n++], 64, "%s", ids[i]);
    snprintf(G[g].set, sizeof G[g].set, "%s", settings ? settings : "");
    printf("\nGROUP %s|", name);
    for (int i = 0; i < n; i++) printf("%s%s", i ? "," : "", ids[i]);
    printf("|"); for (const char *x = settings ? settings : ""; *x; x++) putchar(*x == '\n' ? ';' : *x);
    printf("\n"); return 0;
}
int dsl_settings_take_lines(const char *lines) {
    printf("TAKE "); for (const char *c = lines; *c; c++) putchar(*c == '\n' ? '|' : *c); printf(" ");
    return 1;
}
void dsl_settings_undo(void) { printf("UNDO "); }
static DSLModInfo MI[32]; static int nmi, scanned;
static int listed(const char *var, const char *id) {
    const char *l = getenv(var); char k[80]; snprintf(k, sizeof k, ",%s,", id); return l && strstr(l, k) != NULL;
}
int dsl_mod_dep_text(const char *id, char *out, size_t cap) {
    if (!listed("HM_DEP", id)) return 0;
    snprintf(out, cap, "needs a newer mod"); return 1;
}
static void scan(void) {
    if (scanned) return; scanned = 1; nmi = 0;
    char d[1200]; snprintf(d, sizeof d, "%s/loader/mods", getenv("DSML_ROOT")); DIR *dd = opendir(d); struct dirent *e;
    while (dd && (e = readdir(dd)) && nmi < 32) {
        if (e->d_name[0] == '.') continue;
        char p[1500]; snprintf(p, sizeof p, "%s/%s/mod.json", d, e->d_name);
        FILE *f = fopen(p, "rb"); if (!f) continue;
        char b[4096]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;
        JNode *j = json_parse(b); if (!j) continue;
        DSLModInfo *m = &MI[nmi++]; memset(m, 0, sizeof *m);
        m->id = strdup(json_str(j, "id") ? json_str(j, "id") : e->d_name); m->name = strdup(json_str(j, "name") ? json_str(j, "name") : m->id);
        m->version = strdup(json_str(j, "version") ? json_str(j, "version") : ""); m->scope = strdup(json_str(j, "scope") ? json_str(j, "scope") : "gameplay");
        m->kind = "code"; m->enabled = listed("HM_EN", m->id); m->active = listed("HM_ON", m->id) ? 1 : listed("HM_FAIL", m->id) ? -1 : 0;
        m->reason = m->active == -1 ? "it stopped the game while starting last time, so it stays off until it is updated" : "";
        json_free(j);
    }
    if (dd) closedir(dd);
}
int dsl_mods_count(void) { scan(); return nmi; }
const DSLModInfo *dsl_mods_at(int i) { scan(); return i >= 0 && i < nmi ? &MI[i] : NULL; }
void dsl_mods_rescan(void) { scanned = 0; scan(); }
/* dsl_reg.c: in this game's multiplayer set now (no custom-difficulty level rule here: running = in the set) */
int dsl_mod_in_hello(const char *id) { scan(); for (int i = 0; i < nmi; i++) if (!strcmp(MI[i].id, id)) return MI[i].active == 1; return 0; }

static char *slurp(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    char *b = calloc(1, 1 << 16); size_t n = fread(b, 1, (1 << 16) - 1, f); fclose(f); b[n] = 0; return b;
}
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    dsl_update_start();                                           /* ROOT/update (DSL_UPDATE=0: no update check) */
    if (argc == 2 && !strcmp(argv[1], "check")) {
        char p[1200], *b = NULL; snprintf(p, sizeof p, "%s/update/ready.txt", getenv("DSML_ROOT"));
        for (int i = 0; i < 100 && !(b = slurp(p)); i++) usleep(50000);
        printf("READY\n%s", b ? b : "(none)\n"); return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "packcode")) {
        static char code[32768]; char why[600] = ""; const char *ids[64]; int n = 0;
        for (int i = 3; i < argc && n < 64; i++) ids[n++] = argv[i];
        int r = dsl_pack_code(ids, n, argv[2], code, sizeof code, why, sizeof why);
        if (r < 0) printf("CODE -1 %s\n", why); else printf("CODE %s\n", code);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "packplan")) {
        char *code = slurp(argv[2]); if (!code) return 2;
        static char out[65536]; int r = dsl_pack_plan(code, out, sizeof out);
        for (int i = 0; i < 100 && strstr(out, "\"names_pending\":1"); i++) { usleep(100000); r = dsl_pack_plan(code, out, sizeof out); }
        printf("RET %d\n%s\n", r, out); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "packget")) {
        char *code = slurp(argv[2]); if (!code) return 2;
        char why[700] = "", grp[100] = ""; int r = dsl_pack_import(code, why, sizeof why);
        printf("IMPORT %d %s\n", r, why);
        if (r) return 0;
        int s = 0, pct = 0;
        for (int i = 0; i < 3000; i++) {                          /* <= 30 s */
            dsl_hostmods_frame();
            s = dsl_pack_state(&pct, why, sizeof why, grp, sizeof grp);
            if (s == -1 || s == 4) break;
            usleep(10000);
        }
        printf("\nSTATE %d\nWHY %s\n", s, why);
        return 0;
    }
    char *host = argc >= 4 ? slurp(argv[2]) : NULL; if (!host) return 2;
    if (!strcmp(argv[1], "plan")) {
        static char out[65536]; int r = dsl_hostmods_plan(host, atoi(argv[3]), out, sizeof out);
        for (int i = 0; i < 100 && !strcmp(argv[1], "plan") && argc > 4 && strstr(out, "\"names_pending\":1"); i++) {   /* the box asks again */
            usleep(100000); r = dsl_hostmods_plan(host, atoi(argv[3]), out, sizeof out);
        }
        printf("RET %d\n%s\n", r, out); return 0;
    }
    char why[600] = "", env[600]; snprintf(env, sizeof env, "%s\n", argv[3]);
    int r = dsl_hostmods_get(host, 0, env, why, sizeof why);
    printf("GET %d %s\n", r, why);
    if (r) return 0;
    int s = 0, pct = 0;
    for (int i = 0; i < 3000; i++) {                              /* <= 30 s */
        dsl_hostmods_frame();
        s = dsl_hostmods_state(&pct, why, sizeof why);
        if (s == -1 || s == 3) break;
        usleep(10000);
    }
    printf("\nSTATE %d\nWHY %s\n", s, why);
    return 0;
}
