// BepInEx (kind "bepinex" mods) launch part: the core asks, the OS start part answers.
//   Mac: start/mac/dsl_bie_mac.c (Doorstop through DYLD_INSERT_LIBRARIES, our GitHub release zip, lane bepinexmac).
//   Windows: start/win (lane L3) provides the same functions (or stubs that return -1 / 0).
#pragma once
#include <stddef.h>
/* -1 = BepInEx not available in this start (no install folder given), 0 = not installed yet,
    1 = installed, first start for this game build not done yet (interop is generated: ~1.5 min), 2 = ready */
int dsl_bie_state(void);
/* "KEY=value\n" env lines for a process that runs these plugin folders (n = 0: BepInEx off). 0 = ok */
int dsl_bie_env(const char *const *plugin_dirs, int n, char *out, size_t cap);
int dsl_bie_install_start(void);                      /* download + unzip in the background; 0 = started (or done) */
int dsl_bie_install_poll(char *why, size_t cap);      /* 1 = still running, 0 = installed, -1 = failed (why) */
int dsl_bie_running_dir(const char *plugin_dir);      /* 1 = this process runs that plugin folder */
void dsl_bie_mark_ready(void);                        /* this process reached the title with BepInEx on */
