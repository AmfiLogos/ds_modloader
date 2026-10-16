// Hell difficulty, table half (Mac + Windows): the Hell table bundle is rebuilt from the player's OWN stock bundle and a
// small delta shipped with the module (rva/<build>_<os>_tables.dsdelta, made by tools/hell_delta.py from the owner's
// apply_hell.py table edits; no game file ships). The game then loads that bundle instead of the stock one: a hook on
// AssetBundle.LoadFromFileAsync_Internal swaps the path (crc 0) when the stock table bundle is asked for. Nothing in the
// game folder changes, so Mods-window changes (soft restart) and other mod sets need no file step.
// Cache: <DSRIG_DIR or DSL_LOG_DIR or module folder>/hell-difficulty/<target sha256>.bundle, checked by sha256 each start.
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define DELTA_OS "win"
#define AA_SUB "StandaloneWindows64"
#else
#define MKDIR(p) mkdir(p, 0755)
#define DELTA_OS "mac"
#define AA_SUB "StandaloneOSX"
#endif

/* ---- sha256 (FIPS 180-4) */
typedef struct { uint32_t h[8]; uint64_t n; uint8_t b[64]; size_t k; } Sha;
static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_blk(Sha *s, const uint8_t *p) {
    uint32_t w[64], a, b, c, d, e, f, g, h;
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha256(const uint8_t *p, size_t n, uint8_t out[32]) {
    Sha s = { { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 }, 0, { 0 }, 0 };
    size_t i = 0;
    for (; i + 64 <= n; i += 64) sha_blk(&s, p + i);
    uint8_t t[128] = { 0 }; size_t r = n - i; memcpy(t, p + i, r); t[r] = 0x80;
    size_t L = r + 9 <= 64 ? 64 : 128; uint64_t bits = (uint64_t)n * 8;
    for (int j = 0; j < 8; j++) t[L - 1 - j] = (uint8_t)(bits >> (8 * j));
    sha_blk(&s, t); if (L == 128) sha_blk(&s, t + 64);
    for (int j = 0; j < 8; j++) { out[4 * j] = (uint8_t)(s.h[j] >> 24); out[4 * j + 1] = (uint8_t)(s.h[j] >> 16); out[4 * j + 2] = (uint8_t)(s.h[j] >> 8); out[4 * j + 3] = (uint8_t)s.h[j]; }
}

/* ---- LZ4 block (Unity bundles: LZ4 / LZ4HC = the same block format); 0 = ok */
static int lz4_block(const uint8_t *src, size_t sn, uint8_t *dst, size_t dn) {
    size_t i = 0, o = 0;
    while (i < sn) {
        unsigned tok = src[i++]; size_t lit = tok >> 4;
        if (lit == 15) { unsigned b; do { if (i >= sn) return -1; b = src[i++]; lit += b; } while (b == 255); }
        if (i + lit > sn || o + lit > dn) return -1;
        memcpy(dst + o, src + i, lit); i += lit; o += lit;
        if (i >= sn) break;                                   /* last sequence: literals only */
        if (i + 2 > sn) return -1;
        size_t off = src[i] | (size_t)src[i + 1] << 8; i += 2;
        size_t ml = (tok & 15) + 4;
        if ((tok & 15) == 15) { unsigned b; do { if (i >= sn) return -1; b = src[i++]; ml += b; } while (b == 255); }
        if (!off || off > o || o + ml > dn) return -1;
        for (size_t k = 0; k < ml; k++) dst[o + k] = dst[o - off + k];   /* overlapping copy, byte by byte */
        o += ml;
    }
    return o == dn ? 0 : -1;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t le64(const uint8_t *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* decompressed block data of a UnityFS bundle (tools/hell_delta.py unityfs_data); malloc'd, NULL on error */
static uint8_t *ufs_data(const uint8_t *raw, size_t rn, size_t *outn, const char **why) {
    size_t i = 0;
    if (rn < 64 || memcmp(raw, "UnityFS", 8)) { *why = "not a UnityFS bundle"; return NULL; }
    i = 8; uint32_t ver = be32(raw + i); i += 4;
    while (i < rn && raw[i]) i++; i++;
    while (i < rn && raw[i]) i++; i++;
    if (i + 20 > rn) { *why = "short header"; return NULL; }
    uint32_t csz = be32(raw + i + 8), usz = be32(raw + i + 12), flags = be32(raw + i + 16); i += 20;
    if (ver >= 7) i = (i + 15) & ~(size_t)15;
    const uint8_t *bic; if (flags & 0x80) bic = raw + rn - csz; else { bic = raw + i; i += csz; }
    if (csz > rn || i > rn) { *why = "bad blocks info"; return NULL; }
    uint8_t *bi = malloc(usz ? usz : 1); if (!bi) { *why = "out of memory"; return NULL; }
    unsigned comp = flags & 0x3F;
    if (comp == 0 && csz == usz) memcpy(bi, bic, usz);
    else if ((comp == 2 || comp == 3) && !lz4_block(bic, csz, bi, usz)) {}
    else { free(bi); *why = "blocks info compression not supported"; return NULL; }
    if (flags & 0x200) i = (i + 15) & ~(size_t)15;
    if (usz < 20) { free(bi); *why = "bad blocks info"; return NULL; }
    uint32_t nb = be32(bi + 16); size_t total = 0;
    if (20 + (size_t)nb * 10 > usz) { free(bi); *why = "bad block list"; return NULL; }
    for (uint32_t b = 0; b < nb; b++) total += be32(bi + 20 + 10 * b);
    uint8_t *out = malloc(total ? total : 1); size_t o = 0;
    if (!out) { free(bi); *why = "out of memory"; return NULL; }
    for (uint32_t b = 0; b < nb; b++) {
        const uint8_t *e = bi + 20 + 10 * b; uint32_t u = be32(e), c = be32(e + 4); unsigned f = (unsigned)(e[8] << 8 | e[9]) & 0x3F;
        if (i + c > rn) { *why = "block past the end"; goto bad; }
        if (f == 0 && c == u) memcpy(out + o, raw + i, u);
        else if ((f == 2 || f == 3) && !lz4_block(raw + i, c, out + o, u)) {}
        else { *why = "block compression not supported"; goto bad; }
        i += c; o += u;
    }
    free(bi); *outn = total; return out;
bad:
    free(bi); free(out); return NULL;
}

static uint8_t *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = L >= 0 ? malloc((size_t)L + 1) : NULL;
    if (b && fread(b, 1, (size_t)L, f) != (size_t)L) { free(b); b = NULL; }
    fclose(f); if (b) *n = (size_t)L; return b;
}
static void hex(const uint8_t *d, int n, char *out) { for (int i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", d[i]); }

static const DSLHost *DH;                 /* own names: hell.c includes this file */
static DSLPlugin *DME;
static char bundle_name[200], hell_path[1200];
static void *(*o_load)(void *path, uint32_t crc, uint64_t off, const void *mi);
static void *(*str_new)(const char *);
static int swaps;

static int ends_with_name(void *s) {                   /* il2cpp string ends with bundle_name (ASCII), after a / or \ */
    if (!s) return 0;
    int32_t n = *(int32_t *)((char *)s + 0x10); const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    int32_t m = (int32_t)strlen(bundle_name);
    if (n <= m) return 0;
    for (int32_t i = 0; i < m; i++) if (c[n - m + i] != (uint8_t)bundle_name[i]) return 0;
    return c[n - m - 1] == '/' || c[n - m - 1] == '\\';
}
static void *my_load(void *path, uint32_t crc, uint64_t off, const void *mi) {
    if (ends_with_name(path)) {
        void *np = str_new(hell_path);
        if (np) {
            if (++swaps <= 5) DH->log(DME, "Hell tables: the game loads %s instead of the stock bundle (crc %#x -> 0)", hell_path, crc);
            return o_load(np, 0, 0, mi);
        }
    }
    return o_load(path, crc, off, mi);
}

/* dsl_plugin_init part: build / check the Hell bundle, then the load hook. 0 = ok (or no delta for this OS: nothing to do) */
int hell_tables_init(const DSLHost *host, DSLPlugin *self) {
    DH = host; DME = self;
    char dp[1200]; snprintf(dp, sizeof dp, "%s/rva/%s_" DELTA_OS "_tables.dsdelta", host->module_dir(self), host->game_build);
    size_t dn = 0; uint8_t *d = slurp(dp, &dn);
    if (!d) { host->log(self, "Hell tables: no delta %s: table half not applied by this module", dp); return 0; }
    int rc = 10; uint8_t *raw = NULL, *src = NULL, *out = NULL;
    if (dn < 8 + 4 + 8 + 32 + 8 + 32 + 2 || memcmp(d, "DSDLT1\0\0", 8) || d[8] != 1) { host->log(self, "Hell tables: %s is not a delta file", dp); goto done; }
    const uint8_t *p = d + 12; uint64_t src_size = le64(p); const uint8_t *src_sha = p + 8; p += 40;
    uint64_t dst_size = le64(p); const uint8_t *dst_sha = p + 8; p += 40;
    unsigned nl = (unsigned)(p[0] | p[1] << 8); p += 2;
    if (nl == 0 || nl >= sizeof bundle_name || p + nl > d + dn) { host->log(self, "Hell tables: bad delta header"); goto done; }
    memcpy(bundle_name, p, nl); bundle_name[nl] = 0; p += nl;
    char dsha[65]; hex(dst_sha, 32, dsha);
    /* cache */
    const char *base = getenv("DSRIG_DIR"); if (!base || !*base) base = getenv("DSL_LOG_DIR"); if (!base || !*base) base = host->module_dir(self);
    char cd[1100]; snprintf(cd, sizeof cd, "%s/hell-difficulty", base); MKDIR(cd);
    snprintf(hell_path, sizeof hell_path, "%s/%.16s.bundle", cd, dsha);
    size_t on = 0; out = slurp(hell_path, &on);
    uint8_t h[32];
    if (out && on == dst_size) { sha256(out, on, h); if (!memcmp(h, dst_sha, 32)) { rc = 0; host->log(self, "Hell tables: %s (cached, sha256 ok)", hell_path); goto hook; } }
    free(out); out = NULL;
    /* the player's stock bundle: <GameAssembly folder>/<Exe>_Data/StreamingAssets/aa/<platform>/ (Windows),
       Contents/Frameworks/GameAssembly.dylib -> Contents/Resources/Data/StreamingAssets/aa/StandaloneOSX/ (Mac) */
    char sp[1400]; const char *ga = host->ga_path ? host->ga_path() : NULL;
    if (!ga) { host->log(self, "Hell tables: GameAssembly path unknown"); goto done; }
    snprintf(sp, sizeof sp, "%s", ga);
    char *sl = strrchr(sp, '/'), *bs = strrchr(sp, '\\'); if (bs > sl) sl = bs;
    if (!sl) { host->log(self, "Hell tables: bad GameAssembly path %s", ga); goto done; }
#ifdef _WIN32
    *sl = 0;
    { size_t L = strlen(sp); snprintf(sp + L, sizeof sp - L, "\\DungeonSettlers_Data\\StreamingAssets\\aa\\" AA_SUB "\\%s", bundle_name); }
#else
    *sl = 0; sl = strrchr(sp, '/'); if (!sl) { host->log(self, "Hell tables: bad GameAssembly path %s", ga); goto done; }
    *sl = 0;
    { size_t L = strlen(sp); snprintf(sp + L, sizeof sp - L, "/Resources/Data/StreamingAssets/aa/" AA_SUB "/%s", bundle_name); }
#endif
    size_t rn = 0; raw = slurp(sp, &rn);
    if (!raw) { host->log(self, "Hell tables: stock bundle %s not found", sp); goto done; }
    sha256(raw, rn, h);
    if (rn != src_size || memcmp(h, src_sha, 32)) { char hs[65]; hex(h, 32, hs); host->log(self, "Hell tables: %s is not the stock bundle this delta is for (size %zu, sha256 %.16s)", sp, rn, hs); goto done; }
    const char *why = NULL; size_t sn = 0; src = ufs_data(raw, rn, &sn, &why);
    if (!src) { host->log(self, "Hell tables: %s: %s", sp, why); goto done; }
    out = malloc(dst_size ? dst_size : 1); size_t o = 0;
    if (!out) goto done;
    const uint8_t *end = d + dn;
    for (;;) {
        if (p >= end) { host->log(self, "Hell tables: delta ends early"); goto done; }
        uint8_t op = *p++;
        if (op == 0) break;
        if (op == 1 && p + 12 <= end) { uint64_t so = le64(p); uint32_t L = le32(p + 8); p += 12;
            if (so + L > sn || o + L > dst_size) { host->log(self, "Hell tables: delta copy out of range"); goto done; }
            memcpy(out + o, src + so, L); o += L; }
        else if (op == 2 && p + 4 <= end) { uint32_t L = le32(p); p += 4;
            if (p + L > end || o + L > dst_size) { host->log(self, "Hell tables: delta add out of range"); goto done; }
            memcpy(out + o, p, L); p += L; o += L; }
        else { host->log(self, "Hell tables: bad delta op %u", op); goto done; }
    }
    sha256(out, o, h);
    if (o != dst_size || memcmp(h, dst_sha, 32)) { host->log(self, "Hell tables: rebuilt bundle does not match (size %zu of %llu)", o, (unsigned long long)dst_size); goto done; }
    { char tp[1300]; snprintf(tp, sizeof tp, "%s.tmp", hell_path);
      FILE *f = fopen(tp, "wb"); int ok = f && fwrite(out, 1, o, f) == o; if (f) fclose(f);
      remove(hell_path);                                  /* Windows rename does not replace */
      if (!ok || rename(tp, hell_path)) { host->log(self, "Hell tables: cannot write %s", hell_path); goto done; } }
    host->log(self, "Hell tables: built %s from the stock bundle (%zu bytes)", hell_path, o);
    rc = 0;
hook:
    str_new = (void *(*)(const char *))host->il2cpp_fn("il2cpp_string_new");
    if (!str_new) { host->log(self, "Hell tables: il2cpp_string_new not found"); rc = 11; goto done; }
    if (host->hook_method(self, "UnityEngine", "AssetBundle", "LoadFromFileAsync_Internal", 3, (void *)my_load, (void **)&o_load)) {
        host->log(self, "Hell tables: hook AssetBundle.LoadFromFileAsync_Internal refused"); rc = 12; goto done;
    }
    host->log(self, "Hell tables ready: %s -> %s", bundle_name, hell_path);
done:
    free(d); free(raw); free(src); free(out);
    return rc;
}
