// Host test of the mod data folder + its multiplayer sync (src/dsl_moddata.c): no game. Each case runs in fresh child
// processes (the file keeps its state in statics, like one game process; host and guest are two processes). Breaks it
// catches: a mod's files lost on a mod update; the guest does not get the host's sync files (or gets non-sync files);
// the guest's own files are not back after the session or after a game that died inside it; a path escape (../,
// absolute, backslash, a link) is written; a block over the limit or damaged is used anyway.
#include "dsl_internal.h"
#include "dsl_menu.h"
#include "dsl_os.h"
#include "../../sdk/dsl_sdk.h"
#include <CommonCrypto/CommonDigest.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

long dsl_moddata_pack(void);
size_t dsl_moddata_copy(uint8_t *out, size_t cap);
int dsl_moddata_apply(const uint8_t *p, size_t n, char *why, size_t cap);
void dsl_moddata_session_end(void);

struct DSLPlugin { char id[64]; };
static char T[900], MODS[1000], HOSTD[1000], GUESTD[1000];
void dsl_log(const char *fmt, ...) { va_list a; va_start(a, fmt); if (getenv("V")) { vfprintf(stderr, fmt, a); fputc('\n', stderr); } va_end(a); }
const char *dsl_mods_dir(void) { return MODS; }
static char DATA[1000];                                        /* this process's game data folder */
const char *dsl_reg_data_dir(void) { static char d[1100]; snprintf(d, sizeof d, "%s/Mods", DATA); return d; }
const char *dsl_plugin_name(struct DSLPlugin *self) { return self ? self->id : "loader"; }
static DSLModInfo MI[2] = { { .id = "delvers", .scope = "gameplay", .active = 1, .kind = "bepinex", .enabled = 1 },
                            { .id = "looks", .scope = "client", .active = 1, .kind = "code", .enabled = 1 } };
int dsl_mods_count(void) { return 2; }
const DSLModInfo *dsl_mods_at(int i) { return i >= 0 && i < 2 ? &MI[i] : NULL; }
int dsl_os_sha256_buf(const void *data, size_t n, char out[65]) {
    unsigned char d[32]; CC_SHA256(data, (CC_LONG)n, d); for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]); return 0; }
const char *dsl_os_native_path(const char *u, char *out, size_t n) { snprintf(out, n, "%s", u); return out; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void mkp(const char *path) { char p[1500]; snprintf(p, sizeof p, "%s", path); for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; } }
static void put(const char *path, const char *t) { mkp(path); FILE *f = fopen(path, "w"); fputs(t, f); fclose(f); }
static char *get(const char *path) { FILE *f = fopen(path, "r"); if (!f) return NULL; static char b[1 << 16]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0; return strdup(b); }
static int same(const char *path, const char *want) { char *g = get(path); int ok = want ? g && !strcmp(g, want) : !g; free(g); return ok; }
static int run(void (*fn)(void)) { fflush(stdout); pid_t p = fork(); if (!p) { fn(); fflush(stdout); _exit(fails ? 1 : 0); } int st; waitpid(p, &st, 0); return WIFEXITED(st) ? WEXITSTATUS(st) : 2; }
static void as(const char *data) { snprintf(DATA, sizeof DATA, "%s", data); }   /* this process = the game whose data folder is data */
static char G[1100], H[1100];                                  /* <data>/ModData/delvers of guest / host */

static uint8_t *blk; static size_t blen;                       /* the host's block, passed through a file */
static void save_block(void) { long n = dsl_moddata_pack(); uint8_t *b = malloc(n > 0 ? (size_t)n : 1); size_t k = n > 0 ? dsl_moddata_copy(b, (size_t)n) : 0;
    char p[1000]; snprintf(p, sizeof p, "%s/block.bin", T); FILE *f = fopen(p, "wb"); fwrite(b, 1, k, f); fclose(f); free(b); }
static void load_block(void) { char p[1000]; snprintf(p, sizeof p, "%s/block.bin", T); FILE *f = fopen(p, "rb"); fseek(f, 0, SEEK_END); blen = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET); blk = malloc(blen + 1); blen = fread(blk, 1, blen, f); fclose(f); }
static void host_pack(void) { as(HOSTD); save_block(); }

static void setup(void) {
    char p[1300];
    snprintf(p, sizeof p, "%s/delvers/mod.json", MODS); put(p, "{\"id\":\"delvers\",\"version\":\"1.0.0\",\"sync\":[\"campaign/*.json\",\"markers/**\",\"../x\",\"/abs\",\"bin/*.dll\"]}");
    snprintf(p, sizeof p, "%s/looks/mod.json", MODS); put(p, "{\"id\":\"looks\",\"version\":\"1.0.0\",\"scope\":\"client\",\"sync\":[\"*.json\"]}");
    /* host: two campaign files, a marker deep down, a non-sync settings file, a file in a non-matched sub folder, a link out */
    snprintf(p, sizeof p, "%s/campaign/a.json", H); put(p, "host-a");
    snprintf(p, sizeof p, "%s/campaign/new.json", H); put(p, "host-new");
    snprintf(p, sizeof p, "%s/campaign/deep/no.json", H); put(p, "host-deep");
    snprintf(p, sizeof p, "%s/markers/w1/m.txt", H); put(p, "host-marker");
    snprintf(p, sizeof p, "%s/settings.cfg", H); put(p, "host-settings");
    char secret[1000]; snprintf(secret, sizeof secret, "%s/secret.json", T); put(secret, "SECRET");
    snprintf(p, sizeof p, "%s/campaign/link.json", H); symlink(secret, p);
    snprintf(p, sizeof p, "%s/looks/x.json", HOSTD); put(p, "client mod file");
    /* guest: its own campaign file (other text), one the host does not have, own settings, own marker */
    snprintf(p, sizeof p, "%s/campaign/a.json", G); put(p, "guest-a");
    snprintf(p, sizeof p, "%s/campaign/mine.json", G); put(p, "guest-mine");
    snprintf(p, sizeof p, "%s/markers/w9/m.txt", G); put(p, "guest-marker");
    snprintf(p, sizeof p, "%s/settings.cfg", G); put(p, "guest-settings");
}
#define GP(rel) (snprintf(gp, sizeof gp, "%s/%s", G, rel), gp)
static char gp[1400];
static void guest_own_files(const char *when) {
    CHECK(same(GP("campaign/a.json"), "guest-a"), "%s: guest's own a.json", when);
    CHECK(same(GP("campaign/mine.json"), "guest-mine"), "%s: guest's own mine.json", when);
    CHECK(same(GP("markers/w9/m.txt"), "guest-marker"), "%s: guest's own marker", when);
    CHECK(same(GP("settings.cfg"), "guest-settings"), "%s: guest's settings", when);
    CHECK(same(GP("campaign/new.json"), NULL) && same(GP("markers/w1/m.txt"), NULL), "%s: host's files gone", when);
}

/* ---- the folder: outside the mod's folder, kept when the mod folder is replaced by an update */
static void t_update_keeps(void) {
    as(HOSTD); DSLHost h = {0}; dsl_moddata_host(&h); struct DSLPlugin me = { "delvers" };
    const char *d = h.mod_data_dir(&me);
    CHECK(d && !strncmp(d, HOSTD, strlen(HOSTD)) && strstr(d, "/ModData/delvers") && strncmp(d, MODS, strlen(MODS)), "folder %s", d ? d : "(null)");
    char f[1300], m[1300], bak[1300]; snprintf(f, sizeof f, "%s/progress.json", d); put(f, "kept");
    snprintf(m, sizeof m, "%s/delvers", MODS); snprintf(bak, sizeof bak, "%s/backup-delvers", T);   /* an update: old folder out, new in */
    rename(m, bak); snprintf(f, sizeof f, "%s/delvers/mod.json", MODS); put(f, "{\"id\":\"delvers\",\"version\":\"1.1.0\",\"sync\":[\"campaign/*.json\",\"markers/**\"]}");
    snprintf(f, sizeof f, "%s/progress.json", h.mod_data_dir(&me)); CHECK(same(f, "kept"), "data file lost on update");
    struct DSLPlugin bad = { "../evil" }; CHECK(h.mod_data_dir(&bad) == NULL, "bad id gets a folder");
}

/* ---- guest gets exactly the host's sync files; own files back after the session */
static void t_guest(void) {
    as(GUESTD); load_block(); char why[400];
    int rc = dsl_moddata_apply(blk, blen, why, sizeof why);
    CHECK(rc == 0, "apply rc %d: %s", rc, why);
    CHECK(same(GP("campaign/a.json"), "host-a") && same(GP("campaign/new.json"), "host-new") && same(GP("markers/w1/m.txt"), "host-marker"), "host's sync files not in place");
    CHECK(same(GP("campaign/mine.json"), NULL) && same(GP("markers/w9/m.txt"), NULL), "guest's own sync files still visible during the session");
    CHECK(same(GP("settings.cfg"), "guest-settings"), "non-sync file replaced");
    CHECK(same(GP("campaign/deep/no.json"), NULL) && same(GP("campaign/link.json"), NULL), "non-matching / link file sent");
    char p[1300]; snprintf(p, sizeof p, "%s/looks/x.json", GUESTD); CHECK(same(p, NULL), "client mod's file sent");
    rc = dsl_moddata_apply(blk, blen, why, sizeof why);           /* a second join point in the same session */
    CHECK(rc == 0 && same(GP("campaign/a.json"), "host-a"), "second apply rc %d: %s", rc, why);
    put(GP("campaign/made_in_session.json"), "session");          /* the mod writes during the session */
    put(GP("settings.cfg"), "guest-settings2");                    /* a local file changed during the session: kept */
    dsl_moddata_session_end();
    CHECK(same(GP("campaign/made_in_session.json"), NULL), "session file left after the session");
    CHECK(same(GP("settings.cfg"), "guest-settings2"), "local change lost");
    put(GP("settings.cfg"), "guest-settings");
    guest_own_files("after the session");
}
static void t_guest_dies(void) { as(GUESTD); load_block(); char why[400]; int rc = dsl_moddata_apply(blk, blen, why, sizeof why); CHECK(rc == 0, "apply %d %s", rc, why); _exit(fails ? 1 : 0); }
static void t_next_start(void) { as(GUESTD); dsl_moddata_start(); guest_own_files("next start after a crash"); }

/* ---- hostile / broken blocks: refused, nothing written outside, guest's files unchanged */
static void patch_path(uint8_t *b, size_t n, const char *from, const char *to) {   /* same length */
    for (size_t i = 0; i + strlen(from) <= n; i++) if (!memcmp(b + i, from, strlen(from))) { memcpy(b + i, to, strlen(to)); return; }
}
static size_t one_file_block(uint8_t *b, const char *id, const char *path, const char *data) {   /* valid sha + fingerprint */
    uint8_t *p = b; memcpy(p, "MD1", 4); p += 4; uint32_t one = 1, sz = (uint32_t)strlen(data); memcpy(p, &one, 4); p += 4;
    *p++ = (uint8_t)strlen(id); memcpy(p, id, strlen(id)); p += strlen(id); *p++ = 0; memcpy(p, &one, 4); p += 4;
    uint16_t pl = (uint16_t)strlen(path); memcpy(p, &pl, 2); p += 2; memcpy(p, path, pl); p += pl;
    char sh[65], line[600], fp[65]; dsl_os_sha256_buf(data, sz, sh); memcpy(p, sh, 64); p += 64; memcpy(p, &sz, 4); p += 4; memcpy(p, data, sz); p += sz;
    snprintf(line, sizeof line, "%s/%s@%s", id, path, sh); dsl_os_sha256_buf(line, strlen(line), fp); memcpy(p, fp, 64); p += 64;
    return (size_t)(p - b);
}
static void t_hostile(void) {
    as(GUESTD); load_block(); char why[400];
    char esc[1000]; snprintf(esc, sizeof esc, "%s/escaped.json", T);
    const char *evil[] = { "campaign/../../../escaped.json", "../escaped.json", esc, "campaign\\..\\x.json", "campaign/aa.dll",
                           "campaign/.hidden.json", "settings.cfg", "markers/con.txt", "campaign//a.json" };
    for (size_t k = 0; k < sizeof evil / sizeof *evil; k++) {
        uint8_t c[4096]; size_t n = one_file_block(c, "delvers", evil[k], "EVIL");
        int rc = dsl_moddata_apply(c, n, why, sizeof why);
        CHECK(rc != 0, "evil path %s accepted", evil[k]);
        CHECK(same(esc, NULL), "escaped file written (%s)", evil[k]);
        guest_own_files("after an evil path");
    }
    { uint8_t c[4096]; size_t n = one_file_block(c, "delvers", "campaign/ok.json", "fine");      /* the builder itself is right */
      int rc = dsl_moddata_apply(c, n, why, sizeof why); CHECK(rc == 0 && same(GP("campaign/ok.json"), "fine"), "valid one-file block refused: %s", why);
      dsl_moddata_session_end(); guest_own_files("after the one-file block"); }
    { uint8_t c[4096]; size_t n = one_file_block(c, "looks", "x.json", "client");                 /* a client mod: never synced */
      CHECK(dsl_moddata_apply(c, n, why, sizeof why) != 0, "client mod block accepted"); }
    { uint8_t *c = malloc(blen); memcpy(c, blk, blen); patch_path(c, blen, "host-a", "HOST-A");   /* damaged content */
      int rc = dsl_moddata_apply(c, blen, why, sizeof why); free(c); CHECK(rc != 0, "damaged file accepted"); guest_own_files("after a damaged block"); }
    { int rc = dsl_moddata_apply(blk, blen - 70, why, sizeof why); CHECK(rc != 0, "short block accepted"); guest_own_files("after a short block"); }
    /* the guest's campaign folder is a link to elsewhere: nothing is written through it */
    char out[1000], camp[1400], moved[1400]; snprintf(out, sizeof out, "%s/outside", T); mkdir(out, 0755);
    snprintf(camp, sizeof camp, "%s/campaign", G); snprintf(moved, sizeof moved, "%s/campaign_real", T); rename(camp, moved); symlink(out, camp);
    int rc = dsl_moddata_apply(blk, blen, why, sizeof why);
    char o[1300]; snprintf(o, sizeof o, "%s/a.json", out);
    CHECK(rc != 0 && same(o, NULL), "wrote through a linked folder (rc %d)", rc);
    unlink(camp); rename(moved, camp);
    guest_own_files("after the link case");
}
/* ---- over the limit: the host sends the list only; the guest refuses and keeps its own files */
static void t_big_host(void) { as(HOSTD); char p[1300]; snprintf(p, sizeof p, "%s/campaign/big.json", H); mkp(p); FILE *f = fopen(p, "w"); for (int i = 0; i < (3 << 19); i++) fputc('x', f); fclose(f); save_block(); }
static void t_big_guest(void) {
    as(GUESTD); load_block(); CHECK(blen < 100000, "over-limit block carries the bytes (%zu)", blen);
    char why[400]; int rc = dsl_moddata_apply(blk, blen, why, sizeof why);
    CHECK(rc == 1 && strstr(why, "delvers") && strstr(why, "over the limit"), "rc %d why %s", rc, why);
    guest_own_files("after an over-limit block");
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    snprintf(T, sizeof T, "%s", argv[1]); snprintf(MODS, sizeof MODS, "%s/mods", T);
    snprintf(HOSTD, sizeof HOSTD, "%s/host", T); snprintf(GUESTD, sizeof GUESTD, "%s/guest", T);
    snprintf(H, sizeof H, "%s/ModData/delvers", HOSTD); snprintf(G, sizeof G, "%s/ModData/delvers", GUESTD);
    setup();
    int bad = 0;
    bad |= run(t_update_keeps);
    bad |= run(host_pack);
    bad |= run(t_guest);
    bad |= run(t_guest_dies); bad |= run(t_next_start);
    bad |= run(t_hostile);
    bad |= run(t_big_host); bad |= run(t_big_guest);
    printf(bad ? "moddata: FAILED\n" : "moddata: all cases pass\n");
    return bad ? 1 : 0;
}
