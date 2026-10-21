// getmods_test.c: driver for getmods_test.sh. Links the real src/dsl_hostmods.c + src/dsl_update.c + the Mac OS part
// (download, sha256, install) with small stubs for the game side (mods list = ROOT/loader/mods/*/mod.json, Apply = a line
// printed). The game is never involved.
//   getmods_test plan <host lines file> <vanilla>      prints the plan JSON
//   getmods_test get <host lines file> <env line>      runs the fix (frames until done) and prints STATE / WHY / APPLY
//   getmods_test check                                 the auto-update check (DSL_UPDATE unset; signed_test.sh): prints
//                                                      ROOT/update/ready.txt once written, "(none)" after 5 s
// Env: DSML_ROOT (ROOT), DSL_UPDATE_API (relserver.py), HM_ON / HM_EN (",a,b," running / enabled ids), HM_APPLY_REFUSE=1,
// HM_MYSET (mod settings lines).
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
int dsl_settings_take_lines(const char *lines) {
    printf("TAKE "); for (const char *c = lines; *c; c++) putchar(*c == '\n' ? '|' : *c); printf(" ");
    return 1;
}
void dsl_settings_undo(void) { printf("UNDO "); }
static DSLModInfo MI[32]; static int nmi, scanned;
static int listed(const char *var, const char *id) {
    const char *l = getenv(var); char k[80]; snprintf(k, sizeof k, ",%s,", id); return l && strstr(l, k) != NULL;
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
        m->kind = "code"; m->reason = ""; m->enabled = listed("HM_EN", m->id); m->active = listed("HM_ON", m->id);
        json_free(j);
    }
    if (dd) closedir(dd);
}
int dsl_mods_count(void) { scan(); return nmi; }
const DSLModInfo *dsl_mods_at(int i) { scan(); return i >= 0 && i < nmi ? &MI[i] : NULL; }
void dsl_mods_rescan(void) { scanned = 0; scan(); }

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
