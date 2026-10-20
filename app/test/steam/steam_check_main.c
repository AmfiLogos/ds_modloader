// Runs the stub's real ensure_steam() on a fake app (app/test/steam/steam_check.sh). argv: app log
#define main stub_main
#include "../../stub/ds_modded.c"
#undef main
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    snprintf(logpath, sizeof logpath, "%s", argv[2]);
    return ensure_steam(argv[1]);
}
