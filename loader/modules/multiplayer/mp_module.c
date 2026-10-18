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
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <dlfcn.h>

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
    int r = H->hook_rva(ME, rv(rva), repl, orig, who);
    if (r) rig_log("%s: hook %#lx refused by the loader engine (see dsload.log)", who, (unsigned long)rva);
    else rig_log("%s: hooked %#lx", who, (unsigned long)rva);
    return r;
}

/* ---- session busy (MASTER_PLAN "Open for M4 SPLIT"): while a multiplayer game runs or starts, the loader refuses soft
   restart and the Mods tab's Apply (dsl_set_session_busy). The reason comes from multiplayer's mp_api
   (mp_session_busy_reason, ds_multiplayer src/mp_api.c; found with RTLD_SELF so an older pinned tree still links). */
static void busy_poll(void) {
    static const char *(*why)(void); static int looked; static char last[128]; static int was;
    if (!looked) { looked = 1; why = (const char *(*)(void))dlsym(RTLD_SELF, "mp_session_busy_reason"); }
    if (!why) return;
    const char *r = why();
    if ((r != NULL) == was && (!r || !strcmp(r, last))) return;
    was = r != NULL; snprintf(last, sizeof last, "%s", r ? r : "");
    dsl_set_session_busy(H, ME, r);
    rig_log("loader: session busy %s%s", r ? "SET: " : "cleared", r ? r : "");
}

/* ---- hooks (rig_main.c) */
static void (*o_update)(void *, void *);
static void my_update(void *self, void *mi) {
    rig_hooks_late = 1;
    rig_frame(self);
    busy_poll();
    if (!getenv("DSRIG_MINIMAL")) rig_det_frame(self);
    o_update(self, mi);
}

typedef void *Obj;
static Obj (*o_pdp)(void *);
static Obj my_pdp(void *mi) {
    (void)mi; (void)o_pdp;
    static char p[1100];
    if (!p[0]) snprintf(p, sizeof p, "%s/data", rig_dir);
    return F(0x6F9738, Obj (*)(const char *))(p);     /* il2cpp_string_new */
}

DSL_EXPORT int dsl_module_load(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    const char *d = getenv("DSRIG_DIR");
    if (!d || !*d) { host->log(self, "DSRIG_DIR not set: multiplayer inactive"); return 1; }
    snprintf(rig_dir, sizeof rig_dir, "%s", d);
    char lp[1100]; snprintf(lp, sizeof lp, "%s/rig.log", rig_dir);
    struct stat lst;                            /* appended across starts: past 32 MB the old one becomes rig.log.1 */
    if (!stat(lp, &lst) && lst.st_size > (32 << 20)) { char lp1[1110]; snprintf(lp1, sizeof lp1, "%s.1", lp); rename(lp, lp1); }
    lf = fopen(lp, "a");
    rig_log("---- rig start pid %d as dsload module %s (batchmode arg: %s, hide windows: %s)", getpid(), host->plugin_id(self),
            getenv("DSRIG_BATCH") ? "yes" : "no", getenv("DSRIG_HIDEWIN") ? "yes" : "no");
    /* game addresses: the reference version RIG_BUILD as written, another version through its address table (rig_rv.c) */
    if (!host->game_build || rig_rv_load(host->game_build, host->module_dir(self))) {
        host->log(self, "game build %s: no address table (reference build %s): multiplayer off", host->game_build ? host->game_build : "?", RIG_BUILD);
        return 2;
    }
    if (getenv("DSRIG_LIVE")) rig_log("DSRIG_LIVE: normal game (sound, frame rate, windows untouched)");
    else if (getenv("DSRIG_HIDEWIN") || getenv("DSRIG_BATCH")) rig_win_install();   /* batchmode copies check in with AppKit too */
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
    { const char *on = getenv("DSRIG_CTL"); host->log(self, "multiplayer started (control socket %s)", on && !strcmp(on, "1") ? sp : "off"); }
    return 0;
}

/* World check (dsload, mods per world): a guest loading the host's world is checked by the join (HELLO) instead. */
DSL_EXPORT int dsl_world_check_skip(void) {
    mp_session *s = mp_game_session();
    return s && mp_server_port(s) == 0;           /* the host listens (and also has its own client); a guest does not */
}
