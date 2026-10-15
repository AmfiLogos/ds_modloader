// Multiplayer as a dsload module. Replaces the two files of ~/ds_multiplayer/src that owned process-level
// things: rig_main.c (constructor, add-image callback, DYLD_INSERT_LIBRARIES) and rig_hook.c (its own hook
// engine). Every other multiplayer source is compiled unchanged (vendor/mp/src snapshot).
//   dsl_module_load  = rig_init()  without unsetenv("DYLD_INSERT_LIBRARIES") and without the add-image callback
//   dsl_module_early = on_image()  (same hooks, same order), hooks go through the loader's one engine
//   rig_hook(rva,..) = host->hook_rva
// Inactive (returns 1) without DSRIG_DIR, like the old dylib.
#include "dsl_sdk.h"
#include "mpmenu.h"
#include "rig.h"
#include "mp_game.h"
#include "ord_api.h"
#include "ui_mp.h"
#include "trn.h"
void ui_teams_install(void);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

DSL_PLUGIN();

uintptr_t ds_base;
char rig_dir[1024];
int rig_hooks_late;
static FILE *lf;
static pthread_mutex_t loglock = PTHREAD_MUTEX_INITIALIZER;
static const DSLHost *H;
static DSLPlugin *ME;

void rig_log(const char *fmt, ...) {
    if (!lf) return;
    pthread_mutex_lock(&loglock);
    struct timeval tv; gettimeofday(&tv, NULL);
    char ts[32]; strftime(ts, sizeof ts, "%H:%M:%S", localtime(&tv.tv_sec));
    fprintf(lf, "%s.%03d ", ts, (int)(tv.tv_usec / 1000));
    va_list ap; va_start(ap, fmt); vfprintf(lf, fmt, ap); va_end(ap);
    fputc('\n', lf); fflush(lf);
    pthread_mutex_unlock(&loglock);
}

int rig_hook(uintptr_t rva, void *repl, void **orig, const char *who) {
    int r = H->hook_rva(ME, rva, repl, orig, who);
    if (r) rig_log("%s: hook %#lx refused by the loader engine (see dsload.log)", who, (unsigned long)rva);
    else rig_log("%s: hooked %#lx", who, (unsigned long)rva);
    return r;
}

/* ---- hooks (rig_main.c) */
static void (*o_update)(void *, void *);
static void my_update(void *self, void *mi) {
    rig_hooks_late = 1;
    rig_frame(self);
    if (!getenv("DSRIG_MINIMAL")) rig_det_frame(self);
    o_update(self, mi);
}

typedef void *Obj;
static Obj (*o_pdp)(void *);
static Obj my_pdp(void *mi) {
    (void)mi; (void)o_pdp;
    static char p[1100];
    if (!p[0]) snprintf(p, sizeof p, "%s/data", rig_dir);
    return ((Obj (*)(const char *))(ds_base + 0x6F9738))(p);     /* il2cpp_string_new */
}

DSL_EXPORT int dsl_module_load(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    const char *d = getenv("DSRIG_DIR");
    if (!d || !*d) { host->log(self, "DSRIG_DIR not set: multiplayer inactive"); return 1; }
    if (!host->game_build || strcmp(host->game_build, RIG_BUILD)) { host->log(self, "game build %s is not %s", host->game_build ? host->game_build : "?", RIG_BUILD); return 2; }
    snprintf(rig_dir, sizeof rig_dir, "%s", d);
    char lp[1100]; snprintf(lp, sizeof lp, "%s/rig.log", rig_dir);
    lf = fopen(lp, "a");
    rig_log("---- rig start pid %d as dsload module %s (batchmode arg: %s, hide windows: %s)", getpid(), host->plugin_id(self),
            getenv("DSRIG_BATCH") ? "yes" : "no", getenv("DSRIG_HIDEWIN") ? "yes" : "no");
    if (getenv("DSRIG_LIVE")) rig_log("DSRIG_LIVE: normal game (sound, frame rate, windows untouched)");
    else if (getenv("DSRIG_HIDEWIN")) rig_win_install();
    else if (getenv("DSRIG_SPACE")) rig_win_space_install();
    host->log(self, "rig log %s", lp);
    return 0;
}

DSL_EXPORT int dsl_module_early(const DSLHost *host, DSLPlugin *self) {
    ds_base = host->ga_base;
    rig_log("GameAssembly %s mapped at %p (build %s)", host->ga_path(), (void*)ds_base, RIG_BUILD);
    int bad = 0;
    bad |= rig_hook(0xD1B648, (void*)my_update, (void**)&o_update, "MainFlow.Update");
    bad |= rig_hook(0x507B74C, (void*)my_pdp, (void**)&o_pdp, "Application.get_persistentDataPath");
    if (bad) { rig_log("hook install failed: the rig will not answer game commands"); return 3; }
    if (getenv("DSRIG_MINIMAL")) {
        rig_log("DSRIG_MINIMAL: det / trn / mp / ord / ui hooks NOT installed");
        char sp2[1100]; snprintf(sp2, sizeof sp2, "%s/ctl.sock", rig_dir); rig_sock_start(sp2); return 0;
    }
    rig_det_install();
    trn_install();
    mp_game_install();
    ord_install();
    ui_install();
    ui_teams_install();
    mpmenu_install();
    char sp[1100]; snprintf(sp, sizeof sp, "%s/ctl.sock", rig_dir);
    rig_sock_start(sp);
    host->log(self, "multiplayer started (control socket %s)", sp);
    return 0;
}

/* World check (dsload, mods per world): a guest loading the host's world is checked by the join (HELLO) instead. */
DSL_EXPORT int dsl_world_check_skip(void) {
    mp_session *s = mp_game_session();
    return s && mp_server_port(s) == 0;           /* the host listens (and also has its own client); a guest does not */
}
