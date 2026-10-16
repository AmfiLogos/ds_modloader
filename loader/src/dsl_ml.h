// MelonLoader (kind "melon" mods) launch part, same contract as dsl_bie.h (lane melon, v13).
//   Mac: start/mac/dsl_ml_mac.c (our arm64 MelonLoader build, Bootstrap through DYLD_INSERT_LIBRARIES, GitHub release zip).
//   Windows: start/win/dsl_ml_win.c (official MelonLoader 0.7.3 x64, pinned sha256, version.dll in the game copy).
// BepInEx and MelonLoader both host .NET in the game process: one of them per mod set (dsl_net_check).
#pragma once
#include <stddef.h>
/* -1 = MelonLoader not available in this start, 0 = not installed yet,
    1 = installed, first start for this game build not done yet (interop is generated), 2 = ready */
int dsl_ml_state(void);
/* "KEY=value\n" env lines for a process that runs these mod folders (n = 0: MelonLoader off). 0 = ok */
int dsl_ml_env(const char *const *mod_dirs, int n, char *out, size_t cap);
int dsl_ml_install_start(void);                      /* download + unpack in the background; 0 = started (or done) */
int dsl_ml_install_poll(char *why, size_t cap);      /* 1 = still running, 0 = installed, -1 = failed (why) */
int dsl_ml_running_dir(const char *mod_dir);         /* 1 = this process runs that mod folder */
void dsl_ml_mark_ready(void);                        /* this process reached the title with MelonLoader on */

/* .NET mod hosts (dsl_main.c): kind "bepinex" = DSL_NET_BIE, "melon" = DSL_NET_ML, other kinds 0 */
enum { DSL_NET_BIE = 1, DSL_NET_ML = 2 };
int dsl_net_kind(const char *kind);
const char *dsl_net_name(int k);                     /* "BepInEx" / "MelonLoader" */
const char *dsl_net_size(int k);                     /* download size for the first-use notice, "31 MB" */
/* the host a set of kinds needs: 0 none, DSL_NET_BIE / DSL_NET_ML; -1 = both in one set (refused, why) */
int dsl_net_check(const char *const *kinds, int n, char *why, size_t cap);
int dsl_net_state(int k);
int dsl_net_env(int k, const char *const *dirs, int n, char *out, size_t cap);   /* k = 0: no .NET host */
int dsl_net_install_start(int k);
int dsl_net_install_poll(int k, char *why, size_t cap);
int dsl_net_running_dir(int k, const char *dir);
void dsl_net_mark_ready(void);                       /* both hosts */
