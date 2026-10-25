// Host driver of the loader's in-game mod refresh (loader/ui/dsl_pkg.c: the Mods window opening, its Refresh button) for
// app/test/player_mods_test.sh. DSL_MODS_DIR = the loader's mods folder, DSML_USER_MODS_DIR = the player's (as the stub
// sets them; unset = one folder, the Windows layout). Prints the loader's log lines and "REFRESH <n> <note>".
#define DSI_IMPLEMENTATION
#include "dsl_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

const char *dsl_mods_dir(void) { return getenv("DSL_MODS_DIR"); }
void dsl_mods_rescan(void) {}
void dsl_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); putchar('\n'); }
int dsl_pkg_refresh(char *fail, size_t fcap);

int main(void) {
    char note[400]; int n = dsl_pkg_refresh(note, sizeof note);
    printf("REFRESH %d %s\n", n, note);
    return 0;
}
