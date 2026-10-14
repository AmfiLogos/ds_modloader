/* export_vs_c harness: the PvP mod's own C (pvp_wiki.c + pvp_build.c + fe/) on links read from stdin, one per line,
 * as cmd_wiki (pvp_units.c) runs them with max = 8. argv: SEED PLAYER_UID. Per link:
 *   L <n>            then either  ERR <cmd_wiki error>  or, per unit:
 *   E <jdump(entry, 0)>   N <notes, newline as \n>   M <the six pb_stored_majors as hex bit patterns>
 * fe_guid_seed(SEED) before each link. Built by tests/export_vs_c.rs. */
#include "pvp_build.h"
#include "fe/fe_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void esc(const char *s) { for (; *s; s++) { if (*s == '\n') fputs("\\n", stdout); else if (*s == '\\') fputs("\\\\", stdout); else putchar(*s); } }
int main(int argc, char **argv) {
    unsigned long long seed = argc > 1 ? strtoull(argv[1], NULL, 10) : 1;
    const char *uid = argc > 2 ? argv[2] : "00112233445566778899aabbccddeeff";
    static char line[1 << 16]; static wk_build wb[WK_MAX_UNITS]; int ln = 0;
    while (fgets(line, sizeof line, stdin)) {
        size_t L = strlen(line); while (L && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = 0;
        printf("L %d\n", ++ln);
        fe_guid_seed(seed);
        char err[400] = ""; int fpm = 0;
        int n = wk_decode(line, wb, WK_MAX_UNITS, &fpm, err, sizeof err);
        if (n < 1) { printf("ERR "); esc(err); printf("\n"); continue; }
        if (fpm) { printf("ERR this link was made with other wiki data (another game version): make it again on the wiki\n"); continue; }
        char *outs[WK_MAX_UNITS]; char nts[WK_MAX_UNITS][600]; double maj[WK_MAX_UNITS][6]; int ok = 1;
        for (int i = 0; i < n; i++) {
            nts[i][0] = 0;
            jv *e = pb_entry(&wb[i], uid, nts[i], sizeof nts[i], err, sizeof err);
            if (!e) { char m2[480]; snprintf(m2, sizeof m2, "unit %d: %s", i + 1, err); printf("ERR "); esc(m2); printf("\n"); ok = 0;
                      for (int j = 0; j < i; j++) free(outs[j]); break; }
            outs[i] = jdump(e, 0, NULL); jfree(e);
            pb_stored_majors(&wb[i], maj[i]);
        }
        if (!ok) continue;
        for (int i = 0; i < n; i++) {
            printf("E %s\nN ", outs[i]); esc(nts[i]); printf("\nM");
            for (int m = 0; m < 6; m++) { unsigned long long u; memcpy(&u, &maj[i][m], 8); printf(" %016llx", u); }
            printf("\n"); free(outs[i]);
        }
    }
    return 0;
}
