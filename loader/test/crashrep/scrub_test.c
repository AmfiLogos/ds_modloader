// TEST ONLY: crash report scrubber + gzip (loader/src/dsl_scrub.c, dsl_scrub_names.c). No game. It fails when personal
// data would leave the PC, or when a report would not unpack on the receiver.
//   scrub_test <log dir> [sample log files...]
// The samples are this computer's real logs (run.sh lists them); they are only read, never copied into the repo.
#define DSI_IMPLEMENTATION
#include "dsl_internal.h"
#include "dsl_scrub.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

void dsl_log(const char *fmt, ...) { (void)fmt; }
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int ifind(const char *h, const char *n) {          /* case-insensitive (ASCII) substring */
    size_t l = strlen(n); if (!l) return 0;
    for (const char *p = h; *p; p++) { size_t i = 0; while (i < l && p[i] && ((p[i] | 32) == (n[i] | 32) || p[i] == n[i])) i++; if (i == l) return 1; }
    return 0;
}
static char *scrub_s(const DslScrub *sc, const char *in) { size_t n; return dsl_scrub(sc, in, strlen(in), &n); }

static void synthetic(void) {
    DslScrub sc; dsl_scrub_init(&sc); dsl_scrub_name(&sc, "Zorblax"); dsl_scrub_name(&sc, "Ann");
    struct { const char *in, *gone[3], *kept[3]; } T[] = {
        { "log /Users/annlee/Library/Logs/x.log", { "annlee" }, { "/Users/<user>/Library/Logs/x.log" } },
        { "path C:\\Users\\Ann Lee\\AppData\\Local\\x", { "Ann Lee" }, { "C:\\Users\\<user>\\AppData\\Local\\x" } },
        { "save folder: C:/users/marie/AppData/LocalLow", { "marie" }, { "C:/users/<user>/AppData" } },
        { "home /home/bob/.steam and /Users/Shared/DS", { "bob/" }, { "/home/<user>/.steam", "/Users/Shared/DS" } },
        { "Zorblax joined; ann left; max_players Annex", { "Zorblax", "ann left" }, { "Annex", "max_players" } },
        { "steam 76561198012345678 and [U:1:52083] STEAM_0:1:2345", { "76561198012345678", "52083", "2345" }, { "[U:1:<id>]", "STEAM_<id>" } },
        { "write to ann.lee+x@example.co.uk now", { "example", "ann.lee" }, { "<email> now" } },
        { "connect 192.168.1.20:7777 / 10.0.0.5 ok", { "192.168", "10.0.0.5" }, { "<ip>:7777" } },
        { "local 127.0.0.1:7777 0.0.0.0 v0.1.13 Unity 2022.3.62f1 1.2.3.4.5", { NULL }, { "127.0.0.1:7777", "0.0.0.0", "v0.1.13" } },
        { "v6 fe80::1c2d:3e4f%en0 and 2001:db8:0:0:0:0:2:1 end", { "fe80::1c2d", "2001:db8" }, { "%en0" } },
        { "keep ::1 at 12:34:56.789 std::string Refactor.Main::Run", { NULL }, { "::1", "12:34:56.789", "std::string" } },
        { "mac aa:bb:cc:dd:ee:ff end", { "aa:bb:cc" }, { "<mac>" } },
        { "server: player 2 (Zed Q) joined late", { "Zed Q" }, { "player 2 (<player>) joined" } },
        { "mpmenu: join by address 10.1.1.1:7777 rc -4 as \"Qwerty\"", { "Qwerty" }, { "rc -4 as \"<player>\"" } },
        { "hold: waiting for Bobby (rc -1)", { "Bobby" }, { "waiting for <player> (rc -1)" } },
        { "mp: mp.json client 10.0.0.2:7777 name Bobby Tables allow_speedup 1", { "Bobby" }, { "name <player> allow_speedup" } },
        { "server: mail kind 3 (12 bytes) from player 1 -> Carol\nnext", { "Carol" }, { "-> <player>\nnext" } },
        { "mp: joining Dave (10.0.0.3): rc -3", { "Dave" }, { "joining <player> (<ip>): rc -3" } },
        { "mpmenu: invite from Erin: answer 1 -> rc 0", { "Erin" }, { "invite from <player>: answer" } },
        { "Desync report (lan_v10)\nplayer 2 (Frank) flagged a desync", { "Frank" }, { "player 2 (<player>) flagged" } },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        char *o = scrub_s(&sc, T[i].in);
        for (int k = 0; k < 3; k++) if (T[i].gone[k]) CHECK(o && !strstr(o, T[i].gone[k]), "case %zu: \"%s\" still in \"%s\"", i, T[i].gone[k], o ? o : "(null)");
        for (int k = 0; k < 3; k++) if (T[i].kept[k]) CHECK(o && strstr(o, T[i].kept[k]), "case %zu: \"%s\" missing in \"%s\"", i, T[i].kept[k], o ? o : "(null)");
        free(o);
    }
}

static int gunzip_eq(const unsigned char *gz, size_t gn, const char *want, size_t wn) {
    z_stream z = { 0 }; if (inflateInit2(&z, 31) != Z_OK) return 0;
    unsigned char *out = malloc(wn + 64); z.next_in = (unsigned char *)gz; z.avail_in = (uInt)gn; z.next_out = out; z.avail_out = (uInt)(wn + 64);
    int r = inflate(&z, Z_FINISH); size_t got = z.total_out; inflateEnd(&z);
    int ok = r == Z_STREAM_END && got == wn && !memcmp(out, want, wn); free(out); return ok;
}
static void gzip_case(const char *what, const char *d, size_t n) {
    unsigned char *gz = NULL; size_t gn = 0;
    int r = dsl_gzip(d, n, &gz, &gn);
    CHECK(!r && gunzip_eq(gz, gn, d, n), "gzip %s (%zu bytes): does not unpack to the same bytes", what, n);
    free(gz);
}
static void gzip_tests(void) {
    gzip_case("empty", "", 0); gzip_case("one byte", "a", 1);
    size_t n = 1 << 20; char *b = malloc(n);
    for (size_t i = 0; i < n; i++) b[i] = "2026-10-18 12:00:00 [4242] update: kit: installed 0.1.13, newest usable (none newer)\n"[i % 87];
    gzip_case("repeated log text", b, n);
    unsigned x = 12345; for (size_t i = 0; i < n; i++) { x = x * 1103515245u + 12345u; b[i] = (char)(x >> 16); }
    gzip_case("random bytes", b, n);
    for (size_t i = 0; i < n; i++) b[i] = (i / 300) % 2 ? 'a' : (char)(i % 251);   /* long runs + matches near the window end */
    gzip_case("runs", b, n);
    free(b);
}

static char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)l + 1); *n = b ? fread(b, 1, (size_t)l, f) : 0; fclose(f); if (b) b[*n] = 0; return b;
}
static void real(const char *logdir, int argc, char **argv) {
    DslScrub sc; dsl_scrub_collect(&sc, logdir);
    printf("names of this PC to scrub: %d\n", sc.n);
    CHECK(sc.n >= 2, "only %d names found (user name, home folder at least)", sc.n);
    int files = 0;
    for (int a = 0; a < argc; a++) {
        size_t n = 0; char *in = slurp(argv[a], &n); if (!in || !n) { free(in); continue; }
        files++;
        size_t on = 0, on2 = 0; char *o = dsl_scrub(&sc, in, n, &on);
        CHECK(o != NULL, "sample %d: scrub failed", a);
        if (!o) { free(in); continue; }
        int left = 0;
        for (int k = 0; k < sc.n; k++) {
            const char *nm = sc.name[k]; if (strlen(nm) < 6) { char w[200]; snprintf(w, sizeof w, " %s ", nm); if (ifind(o, w)) left++; }
            else if (ifind(o, nm)) left++;
        }
        CHECK(!left, "sample %d: %d name(s) of this PC are still in the scrubbed text", a, left);
        char *o2 = dsl_scrub(&sc, o, on, &on2);                  /* a second pass finds nothing more: no address / id left */
        CHECK(o2 && on2 == on && !memcmp(o, o2, on), "sample %d: a second scrub still changes the text (something was left)", a);
        const char *u = o; int bad_path = 0;
        while ((u = strstr(u, "/Users/"))) { u += 7; if (strncmp(u, "<user>", 6) && strncmp(u, "Shared", 6)) bad_path++; }
        CHECK(!bad_path, "sample %d: %d /Users/<name> path(s) left", a, bad_path);
        CHECK(on * 10 >= n * 6, "sample %d: scrubbing removed too much (%zu of %zu bytes left)", a, on, n);
        unsigned char *gz = NULL; size_t gn = 0;
        CHECK(!dsl_gzip(o, on, &gz, &gn) && gunzip_eq(gz, gn, o, on), "sample %d: gzip does not unpack", a);
        printf("sample %d: %zu -> %zu bytes scrubbed, %zu bytes gzip\n", a, n, on, gn);
        free(gz); free(o2); free(o); free(in);
    }
    printf("real samples read: %d\n", files);
    CHECK(files > 0, "no real log sample found");
}

int main(int argc, char **argv) {
    synthetic();
    gzip_tests();
    if (argc > 1) real(argv[1], argc - 2, argv + 2);
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
