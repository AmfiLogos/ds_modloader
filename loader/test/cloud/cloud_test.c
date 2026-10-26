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
#include <pthread.h>
#define DSI_IMPLEMENTATION                   /* dsl_cloud.c's TR() (no language file here: English) */
#include "ds_i18n.h"
void dsl_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); printf("    log: "); vprintf(fmt, ap); va_end(ap); putchar('\n'); }
const void *dsl_ui_kit(void) { return NULL; }
void *dsl_os_dlopen(const char *p) { return dlopen(p, RTLD_NOW); }
void *dsl_os_dlsym(void *h, const char *n) { return dlsym(h, n); }
struct DSLHost; void dsl_cloud_install(struct DSLHost *h); int dsl_cloud_pass(void); int dsl_cloud_test_bind(void);
void dsl_cloud_status(char *o, size_t n); void dsl_cloud_frame(void);
extern int dsl_cloud_up, dsl_cloud_down, dsl_cloud_del, dsl_cloud_conflicts, dsl_cloud_skipped_space, dsl_cloud_mod_here, dsl_cloud_mod_there;
/* the game's frames: a thread that runs dsl_cloud_frame like dsl_frame_hook (the main thread; the passes run on another) */
int dsl_reg_scene(void) { return 1; }                      /* at the title */
static pthread_t frame_th; static volatile int frames_run = 1, filter_off_thread;
static void *frames(void *a) { (void)a; while (frames_run) { dsl_cloud_frame(); usleep(1000); } return NULL; }
/* the Load list's filters with no label (dsl_rows_mod_world), as the two mods register them: multiplayer hides its hub /
   PvP temp worlds by name (mp_worldsave.h), Units Laboratory every save whose name has a test world's GUID */
#define TG1 "1f2e3d4c5b6a79881f2e3d4c5b6a7988"
#define TG2 "a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5"
int dsl_rows_mod_world(const char *guid, const char *save, int list) {
    (void)guid; (void)list;
    if (!pthread_equal(pthread_self(), frame_th)) filter_off_thread++;   /* module code: the main thread only */
    return !strncmp(save, "MPHub_", 6) || !strncmp(save, "MPPvP_", 6) || strstr(save, TG1) || strstr(save, TG2);
}
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
    setenv("DSL_CLOUD_ASK_WAIT", "1", 1); machine("A");
    check("fake steam_api bound", dsl_cloud_test_bind() == 0);
    puts("== 0 no game frame yet (the mods have not said which saves are theirs): nothing synced");
    pc("dsmodded_Moon.json", q); check("A: pass skipped, nothing uploaded", dsl_cloud_pass() == 1 && dsl_cloud_up == 0 && !has(q));
    pthread_create(&frame_th, NULL, frames, NULL);
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
    puts("== 14 multiplayer temporary saves (mp_*) never go to the cloud; earlier loaders' dsmodded_mp_* copies are deleted");
    char s2[900]; sprintf(p, "%s/M", T); mkdir(p, 0755); sprintf(p, "%s/M/Saves", T); mkdir(p, 0755);
    pc("dsmodded_mp_fullcheck.json", q); put(q, "old loader's full check", now - 400);   /* uploaded by an earlier loader */
    pc("dsmodded_MP_Test.json", q); put(q, "old loader's test world", now - 400);
    pc("mp_other.json", q); put(q, "not ours (no prefix)", now - 400);
    pc("dsmodded_Mpire.json", q); put(q, "a player's world", now - 400);
    machine("M"); loc("M", "mp_sync.json", p); put(p, "join point", now - 60); loc("M", "Mpire.json", q); put(q, "a player's world", now - 400);
    extern int dsl_cloud_mp_removed; int mr0 = dsl_cloud_mp_removed; dsl_cloud_pass();
    pc("dsmodded_mp_sync.json", q); check("M: local mp_sync.json not uploaded", !has(q) && has(p));
    pc("dsmodded_mp_fullcheck.json", q); pc("dsmodded_MP_Test.json", p);
    check("M: the cloud's dsmodded_mp_* files deleted, not downloaded", !has(q) && !has(p) && dsl_cloud_mp_removed == mr0 + 2
          && (loc("M", "mp_fullcheck.json", s2), !has(s2)) && (loc("M", "MP_Test.json", s2), !has(s2)));
    pc("mp_other.json", q); pc("dsmodded_Mpire.json", p); check("M: other cloud files kept (no prefix; Mpire is not mp_)", has(q) && has(p));
    pc("Game__AutoSave_00000000000000000000000000000000_0.json", q); check("M: the game's own cloud file untouched", has(q));
    puts("== 15 mod worlds (a Load-list filter with no label hides them): never uploaded; their cloud copies never downloaded or deleted");
    sprintf(p, "%s/N", T); mkdir(p, 0755); sprintf(p, "%s/N/Saves", T); mkdir(p, 0755);
    pc("dsmodded_Arena-Slime_Hard__AutoSave_" TG2 "_0.json", q); put(q, "a test world an earlier loader uploaded", now - 400);
    pc("dsmodded_MPPvP_Old.json", q); put(q, "a PvP temp world an earlier loader uploaded", now - 400);
    machine("N");
    loc("N", "Normal.json", p); put(p, "a player's world", now - 300);
    loc("N", "Elven-Expedition_DPS Check_Very-Hard__AutoSave_" TG1 "_0.json", p); put(p, "Test Fights world", now - 300);
    loc("N", "MPHub_x.json", p); put(p, "hub temp world", now - 300);
    int u15 = dsl_cloud_up; dsl_cloud_pass();
    pc("dsmodded_Normal.json", q); check("N: the player's world uploaded", has(q));
    pc("dsmodded_Elven-Expedition_DPS Check_Very-Hard__AutoSave_" TG1 "_0.json", p); pc("dsmodded_MPHub_x.json", q);
    check("N: the Test Fights world and the hub world not uploaded", !has(p) && !has(q) && dsl_cloud_up == u15 + 1);
    loc("N", "Arena-Slime_Hard__AutoSave_" TG2 "_0.json", p); loc("N", "MPPvP_Old.json", q);
    check("N: mod worlds in the cloud not downloaded", !has(p) && !has(q));
    pc("dsmodded_Arena-Slime_Hard__AutoSave_" TG2 "_0.json", p); pc("dsmodded_MPPvP_Old.json", q);
    check("N: their cloud copies kept, counted (log line)", has(p) && has(q) && dsl_cloud_mod_there == 2 && dsl_cloud_mod_here == 2);
    check("the mods' filters only asked on the frame thread", !filter_off_thread);
    frames_run = 0; pthread_join(frame_th, NULL);
    printf("== %d ok, %d failed\n", oks, fails);
    return fails != 0;
}
