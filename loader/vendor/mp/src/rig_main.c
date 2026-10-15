#include "mpmenu.h"
// Test-rig bootstrap. Injected with DYLD_INSERT_LIBRARIES into an isolated game copy (rig/rig.py).
// Does nothing unless DSRIG_DIR is set. When GameAssembly is mapped (before any of its code runs):
// checks the build, hooks MainFlow.Update (main-thread command pump) and
// Application.get_persistentDataPath (saves go to $DSRIG_DIR/data), starts the control socket.
#include "rig.h"
#include "mp_game.h"
#include "ord_api.h"
#include "ui_mp.h"
#include "trn.h"
void ui_teams_install(void);
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <dlfcn.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

uintptr_t ds_base;
char rig_dir[1024];
static FILE *lf;
static pthread_mutex_t loglock = PTHREAD_MUTEX_INITIALIZER;

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

static void uuid_of(const struct mach_header_64 *h, char *out, size_t n) {
    out[0] = 0;
    const uint8_t *p = (const uint8_t*)(h + 1);
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct load_command *lc = (const void*)p;
        if (lc->cmd == LC_UUID) {
            const uint8_t *u = ((const struct uuid_command*)lc)->uuid;
            snprintf(out, n, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                     u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7],u[8],u[9],u[10],u[11],u[12],u[13],u[14],u[15]);
        }
        p += lc->cmdsize;
    }
}

/* ---- hooks */
static void (*o_update)(void *, void *);
static void my_update(void *self, void *mi) {
    { extern int rig_hooks_late; rig_hooks_late = 1; }   /* hooks installed from now on use the late stub page */
    rig_frame(self);
    if (!getenv("DSRIG_MINIMAL")) rig_det_frame(self);                /* locked: runs this frame's sim ticks; the hooks then skip them in Update */
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

static void on_image(const struct mach_header *mh, intptr_t slide) {
    (void)slide;
    if (ds_base) return;
    Dl_info di;
    if (!dladdr(mh, &di) || !di.dli_fname || !strstr(di.dli_fname, "/GameAssembly.dylib")) return;
    const struct mach_header_64 *h = (const void*)mh;
    if (h->magic != MH_MAGIC_64) return;
    char uuid[40]; uuid_of(h, uuid, sizeof uuid);
    if (strcmp(uuid, RIG_GA_UUID)) { rig_log("GameAssembly uuid %s is not build %s: rig inactive", uuid, RIG_BUILD); return; }
    ds_base = (uintptr_t)mh;
    rig_log("GameAssembly %s mapped at %p (build %s)", di.dli_fname, (void*)ds_base, RIG_BUILD);
    int bad = 0;
    bad |= rig_hook(0xD1B648, (void*)my_update, (void**)&o_update, "MainFlow.Update");
    bad |= rig_hook(0x507B74C, (void*)my_pdp, (void**)&o_pdp, "Application.get_persistentDataPath");
    if (bad) { rig_log("hook install failed: the rig will not answer game commands"); }
    if (getenv("DSRIG_MINIMAL")) {      /* perf baseline: only the save-path redirect + command pump, game otherwise stock */
        rig_log("DSRIG_MINIMAL: det / trn / mp / ord / ui hooks NOT installed");
        char sp2[1100]; snprintf(sp2, sizeof sp2, "%s/ctl.sock", rig_dir); rig_sock_start(sp2); return;
    }
    rig_det_install();
    trn_install();                      /* floor seeds, flow barrier, host-only autosave */
    mp_game_install();                  /* speed/pause hooks; mp.json session */
    ord_install();                      /* order capture / replay */
    ui_install();                       /* multiplayer UI (title + campaign) */
    ui_teams_install();                 /* Teams UI hooks at load (ui_teams_frame keeps a late fallback) */
    mpmenu_install();                   /* Multiplayer menu: title, lobby, Esc entries */
    char sp[1100]; snprintf(sp, sizeof sp, "%s/ctl.sock", rig_dir);
    rig_sock_start(sp);
}

__attribute__((constructor)) static void rig_init(void) {
#if !defined(__arm64__)
    return;
#endif
    const char *d = getenv("DSRIG_DIR");
    if (!d || !*d) return;
    snprintf(rig_dir, sizeof rig_dir, "%s", d);
    char lp[1100]; snprintf(lp, sizeof lp, "%s/rig.log", rig_dir);
    struct stat lst;                            /* appended across starts: past 32 MB the old one becomes rig.log.1 */
    if (!stat(lp, &lst) && lst.st_size > (32 << 20)) { char lp1[1110]; snprintf(lp1, sizeof lp1, "%s.1", lp); rename(lp, lp1); }
    lf = fopen(lp, "a");
    rig_log("---- rig start pid %d (batchmode arg: %s, hide windows: %s)", getpid(),
            getenv("DSRIG_BATCH") ? "yes" : "no", getenv("DSRIG_HIDEWIN") ? "yes" : "no");
    unsetenv("DYLD_INSERT_LIBRARIES");          /* children (crash reporter etc.) do not inherit the rig */
    if (getenv("DSRIG_LIVE")) rig_log("DSRIG_LIVE: normal game (sound, frame rate, windows untouched)");
    else if (getenv("DSRIG_HIDEWIN")) rig_win_install();
    else if (getenv("DSRIG_SPACE")) rig_win_space_install();
    _dyld_register_func_for_add_image(on_image);
}
