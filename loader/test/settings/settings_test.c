// Host test of mod settings (src/dsl_settings.c): no game. Each case runs in a fresh child process (the file keeps its
// state in statics, like one game process). Breaks it catches: a module reads another value than the player set, the
// multiplayer lines do not change with a gameplay value, an Apply's values are lost or leak into settings.json before
// the new game runs, a local change is not saved / not told to the module, the host's values are not taken.
#define DSI_IMPLEMENTATION
#include "dsl_internal.h"
#include "dsl_menu.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct DSLPlugin { char id[64]; };
static char D[1024], MODS[1100], PROF[1200];
void dsl_log(const char *fmt, ...) { va_list a; va_start(a, fmt); if (getenv("V")) { vfprintf(stderr, fmt, a); fputc('\n', stderr); } va_end(a); }
const char *dsl_mods_dir(void) { return MODS; }
const char *dsl_profile_file(void) { return PROF; }
const char *dsl_reg_data_dir(void) { return D; }
const char *dsl_plugin_name(struct DSLPlugin *self) { return self ? self->id : "loader"; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void put(const char *path, const char *t) { FILE *f = fopen(path, "w"); fputs(t, f); fclose(f); }
static char *get(const char *path) { FILE *f = fopen(path, "r"); if (!f) return strdup(""); static char b[65536]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0; return strdup(b); }
static int run(void (*fn)(void)) { fflush(stdout); pid_t p = fork(); if (!p) { fn(); fflush(stdout); _exit(fails ? 1 : 0); } int st; waitpid(p, &st, 0); return WIFEXITED(st) ? WEXITSTATUS(st) : 2; }

static DSLHost H; static struct DSLPlugin ZT = { "zt" }, OTHER = { "plain" };
static void init(void) { dsl_settings_host(&H); dsl_settings_init(); }

static void t_values(void) {               /* what a module reads: saved values, clamped; profile wins for gameplay only */
    init();
    CHECK(H.settings_get_int(&ZT, "gold", 1) == 4, "gold %d (saved 5 -> step 2 from 0 = 4)", H.settings_get_int(&ZT, "gold", 1));
    CHECK(H.settings_get_bool(&ZT, "rule", 0) == 1, "rule from the profile");
    CHECK(!strcmp(H.settings_get_choice(&ZT, "mode", "x"), "normal"), "mode: an unknown saved choice gives the default, got %s", H.settings_get_choice(&ZT, "mode", "x"));
    CHECK(H.settings_get_bool(&ZT, "bars", 0) == 1, "bars (local) from settings.json, not the profile's false");
    CHECK(H.settings_get_int(&ZT, "nope", 77) == 77 && H.settings_get_int(&ZT, "rule", 77) == 77, "undeclared id / wrong type -> dflt");
    CHECK(H.settings_get_bool(&OTHER, "rule", 9) == 9, "another mod's id");
    CHECK(dsl_settings_count("zt") == 4, "4 valid settings (the bad ids skipped), got %d", dsl_settings_count("zt"));
}
static void t_lines(void) {                /* multiplayer lines = the running gameplay values, local ones left out */
    init();
    char b[1000]; int n = dsl_settings_lines("zt", b, sizeof b);
    CHECK(n == 3 && !strcmp(b, "zt/gold@4\nzt/rule@on\nzt/mode@normal"), "lines (%d): %s", n, b);
}
static void t_apply_a(void) {              /* A: a gameplay draft goes into the profile, not into settings.json, not live */
    init();
    char before[65536]; snprintf(before, sizeof before, "%s", get(getenv("DSL_SETTINGS_FILE")));
    int gi = -1, mi = -1; for (int i = 0; i < dsl_settings_count("zt"); i++) { if (!strcmp(dsl_settings_at("zt", i)->id, "gold")) gi = i; if (!strcmp(dsl_settings_at("zt", i)->id, "mode")) mi = i; }
    dsl_settings_set("zt", gi, 9); dsl_settings_set("zt", mi, 2);
    CHECK(dsl_settings_pending(NULL) == 2, "2 drafts, got %d", dsl_settings_pending(NULL));
    CHECK(H.settings_get_int(&ZT, "gold", 1) == 4, "the running value stays until Apply");
    CHECK(!strcmp(get(getenv("DSL_SETTINGS_FILE")), before), "settings.json unchanged before the new game runs");
    char m[4000]; dsl_settings_profile_member(m, sizeof m);
    char p[5000]; snprintf(p, sizeof p, "{\"enabled\":[\"zt\"],\"order\":[\"zt\"],%s}", m);
    put(getenv("NEXT_PROFILE"), p);
}
static void t_apply_b(void) {              /* B: started with that profile: runs with the values, commits them */
    init();
    CHECK(H.settings_get_int(&ZT, "gold", 1) == 8, "gold 9 -> 8 (step 2), got %d", H.settings_get_int(&ZT, "gold", 1));
    CHECK(!strcmp(H.settings_get_choice(&ZT, "mode", "x"), "hard"), "mode hard, got %s", H.settings_get_choice(&ZT, "mode", "x"));
    char b[1000]; dsl_settings_lines("zt", b, sizeof b);
    CHECK(strstr(b, "zt/gold@8") && strstr(b, "zt/mode@hard"), "lines follow the Apply: %s", b);
    dsl_settings_commit();
    char *s = get(getenv("DSL_SETTINGS_FILE"));
    CHECK(strstr(s, "\"gold\":8") && strstr(s, "\"mode\":\"hard\""), "committed: %s", s);
    CHECK(strstr(s, "\"gone\":{\"x\":3}"), "a mod that is not installed keeps its values: %s", s);
    CHECK(strstr(s, "\"bars\":true"), "local value kept: %s", s);
}
static int seen; static char seen_id[64];
static void cb(void *ud, const char *id) { (void)ud; seen++; snprintf(seen_id, sizeof seen_id, "%s", id); }
static void t_local(void) {                /* a local change: live, saved at once, the module told */
    init();
    H.settings_on_change(&ZT, cb, NULL);
    int bi = -1; for (int i = 0; i < dsl_settings_count("zt"); i++) if (!strcmp(dsl_settings_at("zt", i)->id, "bars")) bi = i;
    dsl_settings_set("zt", bi, 0);
    CHECK(H.settings_get_bool(&ZT, "bars", 1) == 0, "live");
    CHECK(seen == 1 && !strcmp(seen_id, "bars"), "callback %d %s", seen, seen_id);
    CHECK(strstr(get(getenv("DSL_SETTINGS_FILE")), "\"bars\":false") != NULL, "saved at once");
    CHECK(dsl_settings_pending(NULL) == 0, "a local change needs no Apply");
}
static void t_host(void) {                 /* the host's lines become the drafts of the next Apply */
    init();
    int n = dsl_settings_take_lines("multiplayer@1.0:aa\nzt@1.0.0:bb\nzt/gold@6\nzt/mode@easy\nzt/rule@off\nzt/bars@off\nzt/unknown@3\nzt/mode@nosuch\n");
    CHECK(n == 3, "3 gameplay values taken (bars is local, unknown / bad skipped), got %d", n);
    char m[4000]; dsl_settings_profile_member(m, sizeof m);
    CHECK(strstr(m, "\"gold\":6") && strstr(m, "\"mode\":\"easy\"") && strstr(m, "\"rule\":false"), "profile member: %s", m);
}

int main(int argc, char **argv) {
    const char *T = argc > 1 ? argv[1] : "/tmp"; snprintf(D, sizeof D, "%s", T);
    snprintf(MODS, sizeof MODS, "%s/mods", D); snprintf(PROF, sizeof PROF, "%s/active.json", D);
    char p[1300], sf[1300], np[1300]; snprintf(sf, sizeof sf, "%s/settings.json", D); snprintf(np, sizeof np, "%s/pending.json", D);
    setenv("DSL_SETTINGS_FILE", sf, 1); setenv("NEXT_PROFILE", np, 1);
    snprintf(p, sizeof p, "mkdir -p '%s/zt' '%s/plain'", MODS, MODS); if (system(p)) return 2;
    snprintf(p, sizeof p, "%s/zt/mod.json", MODS);
    put(p, "{\"id\":\"zt\",\"version\":\"1.0.0\",\"settings\":["
           "{\"id\":\"gold\",\"type\":\"int\",\"min\":0,\"max\":10,\"step\":2,\"default\":2,\"label\":{\"en\":\"Gold\",\"ja\":\"ゴールド\"}},"
           "{\"id\":\"Bad Id\",\"type\":\"toggle\"},{\"id\":\"rule\",\"type\":\"toggle\",\"default\":false},"
           "{\"id\":\"bars\",\"type\":\"toggle\",\"default\":true,\"scope\":\"local\"},"
           "{\"id\":\"mode\",\"type\":\"choice\",\"default\":\"normal\",\"choices\":[{\"id\":\"easy\",\"label\":\"Easy\"},{\"id\":\"normal\"},\"hard\"]},"
           "{\"id\":\"x\",\"type\":\"slider\"}]}");
    snprintf(p, sizeof p, "%s/plain/mod.json", MODS); put(p, "{\"id\":\"plain\",\"version\":\"1.0.0\"}");
    put(sf, "{\"zt\":{\"gold\":5,\"mode\":\"nosuch\",\"bars\":true},\"gone\":{\"x\":3}}");
    put(PROF, "{\"enabled\":[\"zt\"],\"settings\":{\"zt\":{\"rule\":true,\"bars\":false}}}");
    int bad = 0;
    bad |= run(t_values); bad |= run(t_lines); bad |= run(t_apply_a);
    snprintf(PROF, sizeof PROF, "%s", np); bad |= run(t_apply_b);
    snprintf(PROF, sizeof PROF, "%s/active.json", D);
    bad |= run(t_local); bad |= run(t_host);
    printf(bad ? "== settings test FAILED\n" : "== settings test: all ok\n");
    return bad;
}
