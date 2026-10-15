// Mac part of BepInEx (src/dsl_bie.h). The app stub gives the paths:
//   DSML_BEPINEX_DIR    <launcher data>/bepinex/mac-arm64/<ver> (writable: BepInEx writes config, interop, logs there)
//   DSML_BEPINEX_SCRIPT install_bepinex.sh (in the app; downloads our release zip, checks sha256, ditto, no quarantine)
//   DSML_DSLOAD         dsload.dylib path (first in DYLD_INSERT_LIBRARIES, Doorstop second)
// Launch env (lane bepinexmac): DOORSTOP_* + BEPINEX_PLUGIN_DIRS (":" list, one folder per enabled bepinex mod).
// First start per game build: <dir>/ds_ready_<build> is written once a BepInEx start reached the title.
#include "../../src/dsl_internal.h"
#include "../../src/dsl_bie.h"
#include <spawn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
extern char **environ;

static const char *dir(void) { const char *d = getenv("DSML_BEPINEX_DIR"); return d && *d ? d : NULL; }
static int has(const char *d, const char *rel) { char p[1400]; struct stat st; snprintf(p, sizeof p, "%s/%s", d, rel); return stat(p, &st) == 0; }
static void ready_path(char *p, size_t n) { snprintf(p, n, "%s/ds_ready_%s", dir(), dsl_build_id ? dsl_build_id : "unknown"); }
int dsl_bie_state(void) {
    const char *d = dir(); if (!d) return -1;
    if (!has(d, "libdoorstop.dylib") || !has(d, "dotnet/libcoreclr.dylib") || !has(d, "BepInEx/core/BepInEx.Unity.IL2CPP.dll")) return 0;
    char p[1400]; struct stat st; ready_path(p, sizeof p);
    return stat(p, &st) == 0 ? 2 : 1;
}
int dsl_bie_env(const char *const *pd, int n, char *out, size_t cap) {
    const char *d = dir(), *dl = getenv("DSML_DSLOAD");
    size_t k = 0; out[0] = 0;
    /* DYLD: dsload first, then the other loaders the start had (dsload removed itself from the live value), minus Doorstop */
    char others[2048] = ""; const char *cur = getenv("DYLD_INSERT_LIBRARIES");
    if (cur) { char *c = strdup(cur), *sv = NULL;
        for (char *t = strtok_r(c, ":", &sv); t; t = strtok_r(NULL, ":", &sv)) {
            if (strstr(t, "libdoorstop") || (dl && !strcmp(t, dl)) || strstr(t, "/dsload.dylib")) continue;
            strlcat(others, ":", sizeof others); strlcat(others, t, sizeof others);
        }
        free(c); }
    if (n > 0 && !d) return -1;
    if (dl) {
        k += (size_t)snprintf(out + k, cap - k, "DYLD_INSERT_LIBRARIES=%s%s", dl, others);
        if (n) k += (size_t)snprintf(out + k, cap - k, ":%s/libdoorstop.dylib", d);
        k += (size_t)snprintf(out + k, cap - k, "\n");
    }
    if (!n) { snprintf(out + k, cap - k, "DOORSTOP_ENABLED=0\nBEPINEX_PLUGIN_DIRS=\n"); return 0; }
    k += (size_t)snprintf(out + k, cap - k, "DOORSTOP_ENABLED=1\nDOORSTOP_TARGET_ASSEMBLY=%s/BepInEx/core/BepInEx.Unity.IL2CPP.dll\n"
                          "DOORSTOP_CLR_RUNTIME_CORECLR_PATH=%s/dotnet/libcoreclr.dylib\nDOORSTOP_CLR_CORLIB_DIR=%s/dotnet\n"
                          "DOORSTOP_IGNORE_DISABLED_ENV=0\nBEPINEX_PLUGIN_DIRS=", d, d, d);
    for (int i = 0; i < n && k < cap; i++) k += (size_t)snprintf(out + k, cap - k, "%s%s", i ? ":" : "", pd[i]);
    if (k < cap) snprintf(out + k, cap - k, "\n");
    return k < cap ? 0 : -1;
}
static pid_t inst_pid; static char inst_out[1300];
int dsl_bie_install_start(void) {
    const char *d = dir(), *sc = getenv("DSML_BEPINEX_SCRIPT");
    if (!d || !sc) return -1;
    if (inst_pid > 0) return 0;
    snprintf(inst_out, sizeof inst_out, "%s.install.log", d);
    char b[1300]; snprintf(b, sizeof b, "%s", d);                    /* parents of the install folder (fresh start) */
    for (char *p = b + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(b, 0755); *p = '/'; }
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, inst_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    char *av[] = { "/bin/zsh", (char *)sc, (char *)d, NULL };
    int r = posix_spawn(&inst_pid, "/bin/zsh", &fa, NULL, av, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (r) { inst_pid = 0; dsl_log("bepinex: cannot start the installer (%d)", r); return -1; }
    dsl_log("bepinex: installing into %s (pid %d)", d, inst_pid);
    return 0;
}
int dsl_bie_install_poll(char *why, size_t cap) {
    if (inst_pid <= 0) return dsl_bie_state() >= 1 ? 0 : -1;
    int st; pid_t w = waitpid(inst_pid, &st, WNOHANG);
    if (w == 0) return 1;
    inst_pid = 0;
    FILE *f = fopen(inst_out, "r"); char line[600] = "", last[600] = "";
    while (f && fgets(line, sizeof line, f)) { line[strcspn(line, "\n")] = 0; if (*line) snprintf(last, sizeof last, "%s", line); }
    if (f) fclose(f);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0 && dsl_bie_state() >= 1) { dsl_log("bepinex: installed (%s)", last); return 0; }
    snprintf(why, cap, "%s", !strncmp(last, "ERROR ", 6) ? last + 6 : "the BepInEx install failed");
    dsl_log("bepinex: install FAILED: %s", why);
    return -1;
}
int dsl_bie_running_dir(const char *pd) {
    const char *e = getenv("BEPINEX_PLUGIN_DIRS"), *on = getenv("DOORSTOP_ENABLED");
    if (!e || !*e || !on || strcmp(on, "1")) return 0;
    size_t L = strlen(pd);
    for (const char *p = e; (p = strstr(p, pd)); p += L) if ((p == e || p[-1] == ':') && (p[L] == 0 || p[L] == ':')) return 1;
    return 0;
}
void dsl_bie_mark_ready(void) {
    static int done; if (done || !dir()) return; done = 1;
    const char *e = getenv("BEPINEX_PLUGIN_DIRS"), *on = getenv("DOORSTOP_ENABLED");
    if (!e || !*e || !on || strcmp(on, "1") || dsl_bie_state() < 1) return;
    char p[1400]; ready_path(p, sizeof p);
    FILE *f = fopen(p, "w"); if (f) { fputs("BepInEx reached the title with this game build\n", f); fclose(f); }
    dsl_log("bepinex: first start for build %s done (%s)", dsl_build_id ? dsl_build_id : "?", p);
}
