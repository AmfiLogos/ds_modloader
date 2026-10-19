// src/dsl_bie_cfg.h on a real be.788 BepInEx.cfg (argv[1]) and on the cases a player's file can be in. The breaks it
// catches: the console stays on, disk logging ([Logging.Disk] Enabled) turned off, a player's other setting lost.
// run_tests.sh builds and runs it on BepInEx.be788.cfg (the file a be.788 start writes, unchanged).
#include "dsl_bie_cfg.h"
static int fails;
static void check(const char *what, int ok) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); fails += !ok; }
static int count(const char *s, const char *w) { int n = 0; for (; (s = strstr(s, w)); s++) n++; return n; }
int main(int argc, char **argv) {
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL; static char real[1 << 16]; size_t n = f ? fread(real, 1, sizeof real - 1, f) : 0;
    if (f) fclose(f); real[n] = 0;
    check("real file read", n > 1000 && strstr(real, "[Logging.Disk]"));
    char *t = dsl_bie_cfg_console_off_text(real);
    const char *con = t ? strstr(t, "[Logging.Console]") : NULL, *disk = t ? strstr(t, "[Logging.Disk]") : NULL;
    check("real: console off, disk logging still on, one line changed",
          con && disk && strstr(con, "Enabled = false") < disk && strstr(disk, "Enabled = true") && strlen(t) == strlen(real) + 1);
    check("real: second pass changes nothing", t && !dsl_bie_cfg_console_off_text(t));
    free(t);
    t = dsl_bie_cfg_console_off_text(NULL);
    check("no file: the section and key only", t && !strcmp(t, "[Logging.Console]\n\nEnabled = false\n")); free(t);
    t = dsl_bie_cfg_console_off_text("[Logging.Disk]\r\nEnabled = true\r\n\r\n[Logging.Console]\r\nLogLevels = All\r\n\r\n[Preloader]\r\nX = 1\r\n");
    check("section without the key, CRLF file: key added in it, disk untouched",
          t && !strcmp(t, "[Logging.Disk]\r\nEnabled = true\r\n\r\n[Logging.Console]\r\nLogLevels = All\r\n\r\nEnabled = false\r\n[Preloader]\r\nX = 1\r\n")); free(t);
    t = dsl_bie_cfg_console_off_text("[Logging.Disk]\nEnabled = true\nWriteUnityLog = false");
    check("no section, last line without newline: appended, player line kept",
          t && count(t, "Enabled = ") == 2 && strstr(t, "WriteUnityLog = false\n\n[Logging.Console]\n\nEnabled = false\n")); free(t);
    return fails != 0;
}
