// fakegame.c - stands in for the game binary "Dungeon Settlers" of a test play copy (app/test/run.sh (g)): writes
// <play-modded>/fakegame_<pid>.txt with its path, the start libraries it runs with and the stub's variables, then ends.
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void) {
    char exe[PATH_MAX], real[PATH_MAX], out[PATH_MAX]; uint32_t sz = sizeof exe;
    if (_NSGetExecutablePath(exe, &sz) || !realpath(exe, real)) return 1;
    snprintf(out, sizeof out, "%s", real);
    for (int i = 0; i < 4; i++) *strrchr(out, '/') = 0;                 /* .../play-modded */
    snprintf(out + strlen(out), sizeof out - strlen(out), "/fakegame_%d.txt", getpid());
    FILE *f = fopen(out, "w"); if (!f) return 1;
    fprintf(f, "EXE %s\n", real);
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char *n = _dyld_get_image_name(i);
        if (strstr(n, "dsload") || strstr(n, "probe")) fprintf(f, "IMAGE %s\n", n);
    }
    const char *k[] = { "DSML_APP", "DSMODDED_ROOT", "DSML_KEEP_DYLD_INSERT_LIBRARIES" };
    for (int i = 0; i < 3; i++) fprintf(f, "ENV %s=%s\n", k[i], getenv(k[i]) ? getenv(k[i]) : "");
    fclose(f);
    return 0;
}
