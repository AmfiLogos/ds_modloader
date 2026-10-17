// TEST ONLY (Windows, under wine): the failure paths of start/win/bie_win.c (install audit P2-8, P2-9).
//   bie_fail_test.exe unzip <zip> <dest>          -> prints "unzip <rc>" (must not crash on a damaged zip)
//   bie_fail_test.exe install <root> <game>       -> prints "install <rc> installed <0|1> why <text>"
#include "bie_win.h"
#define DSI_IMPLEMENTATION                   /* bie_win.c's TR() (no language file: English) */
#include "ds_i18n.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static void lg(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); }
int main(int argc, char **argv) {
    bie_set_log(lg);
    if (argc == 4 && !strcmp(argv[1], "unzip")) { printf("unzip %d\n", bie_unzip_file(argv[2], argv[3])); return 0; }
    if (argc == 4 && !strcmp(argv[1], "install")) {
        char why[600] = ""; int r = bie_install_now(argv[2], argv[3], why, sizeof why);
        printf("install %d installed %d why %s\n", r, bie_installed(argv[3]), why); return 0;
    }
    return 2;
}
