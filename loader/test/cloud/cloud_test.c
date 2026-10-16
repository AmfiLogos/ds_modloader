// Host test of dsl_cloud.c's sync pass against fake_steam_api.dylib (a local folder as the cloud; never real Steam).
// Two "computers" = two DSML_DATA folders. Run: loader/test/cloud/run.sh
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
void dsl_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); printf("    log: "); vprintf(fmt, ap); va_end(ap); putchar('\n'); }
const void *dsl_ui_kit(void) { return NULL; }
void *dsl_os_dlopen(const char *p) { return dlopen(p, RTLD_NOW); }
void *dsl_os_dlsym(void *h, const char *n) { return dlsym(h, n); }
struct DSLHost; void dsl_cloud_install(struct DSLHost *h); int dsl_cloud_pass(void); int dsl_cloud_test_bind(void);
void dsl_cloud_status(char *o, size_t n); int dsl_cloud_import(const char *g, int replace, char *mw, size_t mc);
extern int dsl_cloud_up, dsl_cloud_down, dsl_cloud_del, dsl_cloud_conflicts, dsl_cloud_skipped_space;
static char T[600]; static int fails, oks;
static void check(const char *what, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); ok ? oks++ : fails++; }
static void put(const char *p, const char *s, time_t mt) { FILE *f = fopen(p, "w"); fputs(s, f); fclose(f); if (mt) { struct utimbuf u = { mt, mt }; utime(p, &u); } }
static char *get(const char *p) { static char b[4096]; FILE *f = fopen(p, "r"); if (!f) return NULL; size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); return b; }
static int has(const char *p) { struct stat s; return !stat(p, &s); }
static void pc(const char *f, char *o) { sprintf(o, "%s/cloud/%s", T, f); }
static void machine(const char *m) { char d[700]; sprintf(d, "%s/%s", T, m); setenv("DSML_DATA", d, 1); dsl_cloud_install(NULL); }
static void loc(const char *m, const char *f, char *o) { sprintf(o, "%s/%s/Saves/%s", T, m, f); }
static int count_backups(const char *m) { char c[900]; sprintf(c, "ls '%s/%s/Mods/cloud_backups' 2>/dev/null | wc -l", T, m); FILE *p = popen(c, "r"); int n = 0; fscanf(p, "%d", &n); pclose(p); return n; }
int main(int argc, char **argv) {
    snprintf(T, sizeof T, "%s", argv[1]);
    char p[900], q[900], s[300]; time_t now = time(NULL);
    setenv("DSL_CLOUD", "0", 1);
    sprintf(p, "%s/cloud", T); mkdir(p, 0755); setenv("FAKE_CLOUD_DIR", p, 1);
    pc("Game__AutoSave_00000000000000000000000000000000_0.json", p); put(p, "the game's own auto-cloud file", 0);
    sprintf(p, "%s/A", T); mkdir(p, 0755); sprintf(p, "%s/A/Saves", T); mkdir(p, 0755);
    sprintf(p, "%s/B", T); mkdir(p, 0755); sprintf(p, "%s/B/Saves", T); mkdir(p, 0755);
    loc("A", "Elf__AutoSave_1_0.json", p); put(p, "elf v1", now - 600);
    loc("A", "Moon.json", p); put(p, "moon v1", now - 600);
    loc("A", "Old.json", p); put(p, "old v1", now - 600);
    machine("A");
    check("fake steam_api bound", dsl_cloud_test_bind() == 0);
    puts("== 1 first pass on A: every modded save goes up, with the prefix");
    dsl_cloud_pass();
    pc("dsmodded_Elf__AutoSave_1_0.json", p); pc("dsmodded_Moon.json", q);
    check("A: saves uploaded as dsmodded_<name>", has(p) && has(q) && dsl_cloud_up == 3);
    pc("Game__AutoSave_00000000000000000000000000000000_0.json", p);
    check("the game's own cloud file untouched", !strcmp(get(p), "the game's own auto-cloud file"));
    puts("== 2 a new computer B: downloads what it does not have");
    machine("B"); dsl_cloud_pass();
    loc("B", "Moon.json", p); check("B: Moon downloaded", get(p) && !strcmp(get(p), "moon v1"));
    puts("== 3 A plays on: B gets the new version");
    machine("A"); loc("A", "Elf__AutoSave_1_0.json", p); put(p, "elf v2", now - 300); dsl_cloud_pass();
    pc("dsmodded_Elf__AutoSave_1_0.json", q); { struct utimbuf u2 = { now - 290, now - 290 }; utime(q, &u2); }   /* a later server time */
    machine("B"); dsl_cloud_pass(); loc("B", "Elf__AutoSave_1_0.json", p);
    check("B: Elf v2 downloaded", get(p) && !strcmp(get(p), "elf v2"));
    puts("== 4 both changed Moon: the newest wins, the other one is kept");
    loc("B", "Moon.json", p); put(p, "moon B older", now - 200);
    machine("A"); loc("A", "Moon.json", p); put(p, "moon A newer", now - 100); dsl_cloud_pass();
    pc("dsmodded_Moon.json", q); struct utimbuf u = { now - 50, now - 50 }; utime(q, &u);   /* the cloud's time of A's upload */
    machine("B"); int c0 = dsl_cloud_conflicts; dsl_cloud_pass(); loc("B", "Moon.json", p);
    check("B: conflict seen, the newer cloud copy wins", dsl_cloud_conflicts == c0 + 1 && get(p) && !strcmp(get(p), "moon A newer"));
    check("B: its own older copy kept in Mods/cloud_backups", count_backups("B") == 1);
    puts("== 5 the player deletes a world on A: removed from the cloud (only our file)");
    machine("A"); loc("A", "Old.json", p); unlink(p); dsl_cloud_pass(); pc("dsmodded_Old.json", q);
    check("A: Old.json gone from the cloud", !has(q) && dsl_cloud_del == 1);
    puts("== 6 a lost save folder: nothing deleted in the cloud");
    sprintf(p, "%s/C", T); mkdir(p, 0755); sprintf(p, "%s/C/Saves", T); mkdir(p, 0755); machine("C"); dsl_cloud_pass();   /* downloads Elf + Moon */
    { char x[900]; sprintf(x, "%s/C/Saves/Third.json", T); put(x, "third", now - 30); sprintf(x, "%s/C/Saves/Fourth.json", T); put(x, "fourth", now - 30); dsl_cloud_pass(); }
    { const char *f[] = { "Elf__AutoSave_1_0.json", "Moon.json", "Third.json", "Fourth.json" }; for (int i = 0; i < 4; i++) { loc("C", f[i], p); unlink(p); } }
    int d0 = dsl_cloud_del; dsl_cloud_pass();
    check("C: all 4 synced saves missing at once -> no cloud delete", dsl_cloud_del == d0);
    puts("== 7 space short: no upload, one status line");
    setenv("FAKE_GAME_USE", "100000000", 1); setenv("FAKE_QUOTA_TOTAL", "2000000000", 1);   /* game 100 MB: keep 2 GB free */
    machine("A"); loc("A", "New.json", p); put(p, "new world", now - 20); int k0 = dsl_cloud_skipped_space; dsl_cloud_pass();
    dsl_cloud_status(s, sizeof s); pc("dsmodded_New.json", q);
    check("A: not uploaded when the game's room would shrink", !has(q) && dsl_cloud_skipped_space > k0 && strstr(s, "almost full"));
    printf("    status: %s\n", s);
    unsetenv("FAKE_GAME_USE"); unsetenv("FAKE_QUOTA_TOTAL"); dsl_cloud_pass();
    check("A: uploaded once there is room", has(q));
    puts("== 8 import a world from the game's folder: a copy, the original unchanged");
    sprintf(p, "%s/gamesaves", T); mkdir(p, 0755); setenv("DSML_GAME_SAVES", p, 1);
    sprintf(p, "%s/gamesaves/Elf__AutoSave_abc_0.json", T);
    put(p, "{\"x\":1,\"CampaignSaveHeader\":{\"CampaignGuid\":\"f816ebc6-195c-432c-9fff-754b75ee3738\",\"ClanName\":\"Elven\",\"SaveTime\":\"2026-10-11\"}}", now - 1000);
    struct stat s0; stat(p, &s0); char before[4096]; strcpy(before, get(p));
    machine("A"); int n = dsl_cloud_import("f816ebc6-195c-432c-9fff-754b75ee3738", 0, NULL, 0);
    loc("A", "Elf__AutoSave_abc_0.json", q); struct stat s1; stat(p, &s1);
    check("import copied 1 save", n == 1 && get(q) && !strcmp(get(q), before));
    check("the game's own save unchanged (content + mtime)", !strcmp(get(p), before) && s0.st_mtime == s1.st_mtime);
    check("import again: nothing to copy", dsl_cloud_import("f816ebc6-195c-432c-9fff-754b75ee3738", 0, NULL, 0) == 0);
    puts("== 9 audit #1: the world was played modded -> import never overwrites without asking; replace keeps a copy");
    const char *G = "f816ebc6-195c-432c-9fff-754b75ee3738";
    put(q, "{\"modded progress\":1,\"CampaignSaveHeader\":{\"CampaignGuid\":\"f816ebc6-195c-432c-9fff-754b75ee3738\",\"ClanName\":\"Elven\",\"SaveTime\":\"2026-10-16\"}}", now - 10);
    char mw[24] = ""; int b0 = count_backups("A");
    check("import: modded world differs -> -2 (ask), nothing written", dsl_cloud_import(G, 0, mw, sizeof mw) == -2 && strstr(get(q), "modded progress") && !strcmp(mw, "2026-10-16") && count_backups("A") == b0);
    { char bd[900], c2[1000]; sprintf(bd, "%s/A/Mods/cloud_backups", T); sprintf(c2, "mkdir -p '%s' && mv '%s' '%s.moved' && touch '%s'", bd, bd, bd, bd); system(c2); }   /* the backup folder cannot be written */
    check("replace with a failing backup -> -1, modded save unchanged", dsl_cloud_import(G, 1, NULL, 0) == -1 && strstr(get(q), "modded progress"));
    { char bd[900], c2[2000]; sprintf(bd, "%s/A/Mods/cloud_backups", T); sprintf(c2, "mv '%s' '%s.file' && mv '%s.moved' '%s'", bd, bd, bd, bd); system(c2); }
    check("replace -> copied, the modded one kept in cloud_backups", dsl_cloud_import(G, 1, NULL, 0) == 1 && !strcmp(get(q), before) && count_backups("A") == b0 + 1);
    puts("== 10 audit #4: a conflict that cannot finish keeps ONE copy, not one per pass");
    machine("B"); loc("B", "Moon.json", p); put(p, "moon B again", now - 60);  /* local change, newer */
    pc("dsmodded_Moon.json", q); put(q, "moon cloud again", now - 120);     /* cloud change, older */
    setenv("FAKE_GAME_USE", "100000000", 1); setenv("FAKE_QUOTA_TOTAL", "2000000000", 1);   /* backup cloud, upload fails (space) */
    int cb = count_backups("B"); dsl_cloud_pass(); dsl_cloud_pass(); dsl_cloud_pass();
    (void)cb; { char c4[900]; sprintf(c4, "ls '%s/B/Mods/cloud_backups' | grep -c '^Moon-cloud-'", T); FILE *pp = popen(c4, "r"); int k = 0; fscanf(pp, "%d", &k); pclose(pp);
      check("3 passes, upload blocked by space: 1 backup of that version only", k == 1); }
    unsetenv("FAKE_GAME_USE"); unsetenv("FAKE_QUOTA_TOTAL");
    puts("== 11 open check: Steam returns another case for a name -> one save, no download over it, no delete");
    sprintf(p, "%s/D", T); mkdir(p, 0755); sprintf(p, "%s/D/Saves", T); mkdir(p, 0755); machine("D"); dsl_cloud_pass();
    { char x[900]; sprintf(x, "%s/D/Saves/Case.json", T); put(x, "case local", now - 100); } dsl_cloud_pass();
    { char a1[900], a2[900]; pc("dsmodded_Case.json", a1); pc("dsmodded_case.json", a2); rename(a1, a2); }   /* the cloud lists it in lower case */
    int dd = dsl_cloud_del, dn = dsl_cloud_down; dsl_cloud_pass(); loc("D", "Case.json", p);
    check("case-only difference: nothing downloaded or deleted, local kept", dsl_cloud_del == dd && dsl_cloud_down == dn && get(p) && !strcmp(get(p), "case local"));
    puts("== 12 audit #6: another game process holds the pass lock -> pass skipped");
    { char lp[900]; sprintf(lp, "%s/D/Mods/cloud.lock", T); int fd = open(lp, O_CREAT | O_RDWR, 0644); flock(fd, LOCK_EX);
      extern int dsl_cloud_lock_busy; int lb = dsl_cloud_lock_busy;
      pid_t k = fork(); if (k == 0) { _exit(dsl_cloud_pass() == 1 ? 0 : 1); } int stx; waitpid(k, &stx, 0);
      check("pass skipped while the lock is held (other process)", WIFEXITED(stx) && WEXITSTATUS(stx) == 0); (void)lb; close(fd); }
    puts("== 13 review: deleted on another computer -> not uploaded back; moved aside here (never deleted)");
    sprintf(p, "%s/E", T); mkdir(p, 0755); sprintf(p, "%s/E/Saves", T); mkdir(p, 0755);
    sprintf(p, "%s/F", T); mkdir(p, 0755); sprintf(p, "%s/F/Saves", T); mkdir(p, 0755);
    machine("E"); { char x[900]; sprintf(x, "%s/E/Saves/Rot__AutoSave_9_0.json", T); put(x, "rot", now - 500); sprintf(x, "%s/E/Saves/Keep.json", T); put(x, "keep", now - 500); } dsl_cloud_pass();
    machine("F"); dsl_cloud_pass();                                          /* F gets both */
    loc("F", "Rot__AutoSave_9_0.json", p); unlink(p); dsl_cloud_pass();    /* F's game rotates the autosave away */
    pc("dsmodded_Rot__AutoSave_9_0.json", q); check("F: deleted from the cloud", !has(q));
    machine("E"); extern int dsl_cloud_rdel; int rd0 = dsl_cloud_rdel; dsl_cloud_pass(); loc("E", "Rot__AutoSave_9_0.json", p);
    { char c3[900]; sprintf(c3, "ls '%s/E/Mods/cloud_backups' | grep -c Rot__AutoSave_9_0-deleted-elsewhere", T); FILE *pp = popen(c3, "r"); int k = 0; fscanf(pp, "%d", &k); pclose(pp);
      check("E: not uploaded back, moved to cloud_backups", !has(q) && !has(p) && dsl_cloud_rdel == rd0 + 1 && k == 1); }
    machine("F"); pc("dsmodded_Keep.json", q); unlink(q);                   /* gone from the cloud ... */
    loc("F", "Keep.json", p); put(p, "keep changed on F", now - 100);       /* ... but F changed it since the sync: upload */
    dsl_cloud_pass(); check("F: changed here since the sync -> uploaded, not moved aside", has(q) && !strcmp(get(q), "keep changed on F"));
    puts("== 14 review: import reads only the header, and never opens another world's autosaves");
    { char x[900]; sprintf(x, "%s/gamesaves/Big__AutoSave_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_0.json", T);
      FILE *f = fopen(x, "w"); for (int i = 0; i < 40000; i++) fputs("{\"filler\":\"0123456789012345678901234567890123456789\"},", f);
      fputs("\"CampaignSaveHeader\":{\"CampaignGuid\":\"aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa\",\"ClanName\":\"Big\",\"SaveTime\":\"2026-10-01\"}", f);
      for (int i = 0; i < 40000; i++) fputs(",{\"after\":\"0123456789012345678901234567890123456789\"}", f);
      fclose(f); struct stat bs; stat(x, &bs);
      extern int dsl_cloud_header_bytes; machine("A"); dsl_cloud_header_bytes = 0;
      dsl_cloud_import("f816ebc6-195c-432c-9fff-754b75ee3738", 0, NULL, 0);
      check("import of another world: the big autosave is not opened", dsl_cloud_header_bytes < 4096);
      dsl_cloud_header_bytes = 0; char mw[24]; int ni = dsl_cloud_import("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", 0, mw, sizeof mw);
      printf("    header bytes read %d of a %lld byte file (the copy itself reads it once)\n", dsl_cloud_header_bytes, (long long)bs.st_size);
      check("its own import: header found without reading the whole file", ni == 1 && dsl_cloud_header_bytes < bs.st_size); }
    printf("== %d ok, %d failed\n", oks, fails);
    return fails != 0;
}
