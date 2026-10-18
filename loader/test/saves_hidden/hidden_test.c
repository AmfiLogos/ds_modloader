// Host test of the hidden-worlds count (dsl_saves.c dsl_saves_off_note): when mods are off for a session the game lists
// one save folder only, and the start notice tells how many worlds of the OTHER folder are not shown. Windows (no save
// folder redirect) hides the modded worlds, the Mac copy hides the game saves; both branches run here on the Mac.
// Real break caught: the notice names the wrong folder or a wrong number (saves counted as worlds, multiplayer
// temporary saves counted, worlds that are listed from the other folder counted). Run: loader/test/saves_hidden/run.sh
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define DSI_IMPLEMENTATION                   /* dsl_saves.c's TRN() (no language file here: English) */
#include "ds_i18n.h"
void dsl_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); printf("    log: "); vprintf(fmt, ap); va_end(ap); putchar('\n'); }
const char *dsl_os_native_path(const char *utf8, char *out, size_t n) { snprintf(out, n, "%s", utf8 ? utf8 : ""); return out; }
void dsl_saves_off_note(int win, char *out, size_t cap);
static int fails, oks;
static void check(const char *what, int ok, const char *got) { printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", what, ok ? "" : ": ", ok ? "" : got); ok ? oks++ : fails++; }
static char T[600];
static void put(const char *dir, const char *name, const char *guid) {
    char p[900]; snprintf(p, sizeof p, "%s/%s/%s.json", T, dir, name); FILE *f = fopen(p, "w");
    if (guid) fprintf(f, "{\"Units\":[1,2,3],\"CampaignSaveHeader\":{\"CampaignGuid\":\"%s\",\"ClanName\":\"Clan\",\"SaveTime\":\"2026-10-18\"}}", guid);
    else fputs("{\"broken\":", f);
    fclose(f);
}
#define A "aaaaaaaa111122223333444455556666"
#define B "bbbbbbbb111122223333444455556666"
#define D "dddddddd111122223333444455556666"
int main(int argc, char **argv) {
    snprintf(T, sizeof T, "%s", argv[1]);
    char p[900], note[900];
    snprintf(p, sizeof p, "%s/data", T); mkdir(p, 0755); snprintf(p, sizeof p, "%s/data/Saves", T); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/game", T); mkdir(p, 0755);
    setenv("DSML_DATA", (snprintf(p, sizeof p, "%s/data", T), p), 1);
    char g[900]; snprintf(g, sizeof g, "%s/game", T); setenv("DSML_GAME_SAVES", g, 1);
    /* modded folder: world A (two autosaves), world C (a manual save: its world from the header), world B, a
       multiplayer temporary save of world X; game folder: world B (so B is listed either way), world D, an unreadable save */
    put("data/Saves", "Alpha__AutoSave_" A "_0", NULL); put("data/Saves", "Alpha__AutoSave_" A "_1", NULL);
    put("data/Saves", "My castle", "cccccccc-1111-2222-3333-444455556666");
    put("data/Saves", "Beta__AutoSave_" B "_0", NULL);
    put("data/Saves", "mp_fullcheck", "eeeeeeee-1111-2222-3333-444455556666");
    put("game", "Beta__AutoSave_" B "_4", NULL); put("game", "Delta__AutoSave_" D "_0", NULL); put("game", "Broken", NULL);
    dsl_saves_off_note(1, note, sizeof note);
    check("Windows, mods off: 2 modded worlds named (A, C; not B, not the mp_ save, not 2 for A's two saves)",
          !strncmp(note, "2 modded worlds are not in the Load list this session.", 54), note);
    dsl_saves_off_note(0, note, sizeof note);
    check("Mac copy, mods off: 1 game world named (D; not B, not the unreadable save)",
          !strncmp(note, "1 of your game worlds is not in the Load list this session.", 59), note);
    snprintf(p, sizeof p, "%s/data", T); snprintf(g, sizeof g, "%s/empty", T); mkdir(g, 0755); setenv("DSML_DATA", g, 1);
    dsl_saves_off_note(1, note, sizeof note);
    check("nothing hidden: no line", note[0] == 0, note);
    printf("%d ok, %d failed\n", oks, fails);
    return fails != 0;
}
