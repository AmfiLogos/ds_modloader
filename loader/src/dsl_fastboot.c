// Fast boot (lane fastboot, owner go 2026-10-17): Addressables bundles skip Unity's CRC check when the same file
// already passed it.
//
// The catalog gives every bundle a CRC-32 of its uncompressed data, so AssetBundle.LoadFromFileAsync(path, crc)
// unpacks and checksums the whole file before it loads (fonts_shared: 1168 MB, 3.6-8.9 s on one thread at boot).
// Here a load with a CRC is let through as is the first time; when its request ends with a bundle (the CRC matched),
// the file's size + modification time + that CRC are written to <log dir>/fastboot_crc_<build>.tsv. A later load
// of a file inside the game's own StreamingAssets with the same CRC, size and modification time passes CRC 0.
// Any difference (another file, a mod's replaced bundle, a new game build) = the game's normal CRC check.
// Off: DSL_FASTBOOT=0, an unknown game build, or other first bytes at the hooked method than this build's.
#include "dsl_internal.h"
#include "dsl_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))

static const struct { const char *build, *plat, *bytes; } SITE[] = {          /* AssetBundle.LoadFromFileAsync_Internal */
    { "25846389", "mac", "ff4303d1f6570aa9f44f0ba9fd7b0ca9" },
    { "25846389", "win", "48895c2410488974241848897c242041" },
};
#ifdef _WIN32
#define PLAT "win"
#else
#define PLAT "mac"
#endif

typedef struct { char rel[160]; uint32_t crc; long long size, mtime; } Rec;
static Rec *recs; static int nrec, caprec;
static char tsv[1400], sa[1100]; static size_t sa_n;               /* record file; StreamingAssets prefix ('/' only) */
static int on, skipped, checked;
static Obj (*o_load)(Obj, uint32_t, uint64_t, const void *);
static uintptr_t (*gc_new)(Obj, int); static Obj (*gc_get)(uintptr_t); static void (*gc_free)(uintptr_t);   /* Unity 6: pointer-size handles */
static _Bool (*is_done)(Obj, const void *); static Obj (*get_bundle)(Obj, const void *);
#define NPEND 64
static struct { uintptr_t h; Rec r; } pend[NPEND]; static int npend;

static void utf8_of(Obj s, char *out, size_t n) {                          /* System.String (UTF-16) -> UTF-8, '\\' -> '/' */
    size_t k = 0; int32_t len = s ? AT(s, 0x10, int32_t) : 0; const uint16_t *c = s ? (const uint16_t *)((char *)s + 0x14) : NULL;
    for (int32_t i = 0; i < len && k + 4 < n; i++) {
        uint32_t u = c[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < len) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
        if (u == '\\') u = '/';
        if (u < 0x80) out[k++] = (char)u;
        else if (u < 0x800) { out[k++] = (char)(0xC0 | u >> 6); out[k++] = (char)(0x80 | (u & 63)); }
        else if (u < 0x10000) { out[k++] = (char)(0xE0 | u >> 12); out[k++] = (char)(0x80 | (u >> 6 & 63)); out[k++] = (char)(0x80 | (u & 63)); }
        else { out[k++] = (char)(0xF0 | u >> 18); out[k++] = (char)(0x80 | (u >> 12 & 63)); out[k++] = (char)(0x80 | (u >> 6 & 63)); out[k++] = (char)(0x80 | (u & 63)); }
    }
    out[k] = 0;
}
static Rec *find(const char *rel) { for (int i = 0; i < nrec; i++) if (!strcmp(recs[i].rel, rel)) return &recs[i]; return NULL; }
static void save(void) {
    char tmp[1420]; snprintf(tmp, sizeof tmp, "%s.tmp", tsv);
    FILE *f = fopen(tmp, "w"); if (!f) return;
    fprintf(f, "# bundles that passed the game's own CRC check: path in StreamingAssets, crc, size, modification time\n");
    for (int i = 0; i < nrec; i++) fprintf(f, "%s\t%08x\t%lld\t%lld\n", recs[i].rel, recs[i].crc, recs[i].size, recs[i].mtime);
    fclose(f); dsl_os_replace(tmp, tsv);
}
static void put(const Rec *r) {
    Rec *e = find(r->rel);
    if (!e) { if (nrec == caprec) { caprec = caprec ? caprec * 2 : 32; recs = realloc(recs, caprec * sizeof *recs); } e = &recs[nrec++]; }
    *e = *r; save();
}

static Obj my_load(Obj path, uint32_t crc, uint64_t offset, const void *mi) {
    char p[1100]; Rec r; memset(&r, 0, sizeof r);
    if (!crc || offset || !path) return o_load(path, crc, offset, mi);
    utf8_of(path, p, sizeof p);
    if (strncmp(p, sa, sa_n) || p[sa_n] != '/' || strstr(p + sa_n, "/../") || strlen(p + sa_n + 1) >= sizeof r.rel)
        return o_load(path, crc, offset, mi);                                /* not one of the game's own bundles */
    snprintf(r.rel, sizeof r.rel, "%s", p + sa_n + 1); r.crc = crc;
    if (dsl_os_file_stamp(p, &r.size, &r.mtime)) return o_load(path, crc, offset, mi);
    Rec *e = find(r.rel);
    if (e && e->crc == crc && e->size == r.size && e->mtime == r.mtime) {
        skipped++; dsl_log("fastboot: %s: CRC check skipped (passed before, file unchanged)", r.rel);
        return o_load(path, 0, offset, mi);
    }
    if (e) dsl_log("fastboot: %s changed since its last check (crc %08x/%08x size %lld/%lld): the game checks it", r.rel, e->crc, crc, e->size, r.size);
    Obj req = o_load(path, crc, offset, mi);
    if (req && npend < NPEND) { pend[npend].h = gc_new(req, 0); pend[npend].r = r; npend++; }
    checked++;
    return req;
}

/* every frame (main thread): a checked load that ended with a bundle is recorded */
void dsl_fastboot_tick(void) {
    for (int i = 0; on && i < npend; ) {
        Obj q = gc_get(pend[i].h);
        if (q && !is_done(q, NULL)) { i++; continue; }
        if (q && get_bundle(q, NULL)) { put(&pend[i].r); dsl_log("fastboot: %s passed the game's CRC check: recorded", pend[i].r.rel); }
        else dsl_log("fastboot: %s: no bundle after the CRC check: not recorded", pend[i].r.rel);
        gc_free(pend[i].h); pend[i] = pend[--npend];
    }
}

static int hexeq(const unsigned char *a, const char *hex) {
    for (size_t i = 0; hex[2 * i]; i++) { unsigned v; if (sscanf(hex + 2 * i, "%2x", &v) != 1 || a[i] != v) return 0; }
    return 1;
}
void dsl_fastboot_install(const char *logdir) {
    const char *e = getenv("DSL_FASTBOOT");
    if (e && *e == '0') { dsl_log("fastboot: off (DSL_FASTBOOT=0)"); return; }
    const char *want = NULL;
    for (size_t i = 0; i < sizeof SITE / sizeof SITE[0]; i++)
        if (dsl_build_id && !strcmp(SITE[i].build, dsl_build_id) && !strcmp(SITE[i].plat, PLAT)) want = SITE[i].bytes;
    void *a = dsl_find_method_address("UnityEngine", "AssetBundle", "LoadFromFileAsync_Internal", 3);
    void *done = dsl_find_method_address("UnityEngine", "AsyncOperation", "get_isDone", 0);
    void *bun = dsl_find_method_address("UnityEngine", "AssetBundleCreateRequest", "get_assetBundle", 0);
    Obj (*sap)(const void *) = dsl_find_method_address("UnityEngine", "Application", "get_streamingAssetsPath", 0);
    gc_new = dsl_ga_sym("il2cpp_gchandle_new"); gc_get = dsl_ga_sym("il2cpp_gchandle_get_target"); gc_free = dsl_ga_sym("il2cpp_gchandle_free");
    if (!want) { dsl_log("fastboot: off (no known code for game build %s on %s)", dsl_build_id ? dsl_build_id : "?", PLAT); return; }
    if (!a || !hexeq(a, want)) { dsl_log("fastboot: off (AssetBundle.LoadFromFileAsync_Internal %s: not the expected code of build %s)", a ? "differs" : "not found", dsl_build_id); return; }
    if (!done || !bun || !sap || !gc_new || !gc_get || !gc_free) { dsl_log("fastboot: off (game functions missing)"); return; }
    is_done = done; get_bundle = bun;
    utf8_of(sap(NULL), sa, sizeof sa); sa_n = strlen(sa);
    while (sa_n && sa[sa_n - 1] == '/') sa[--sa_n] = 0;
    if (!sa_n) { dsl_log("fastboot: off (no StreamingAssets path)"); return; }
    snprintf(tsv, sizeof tsv, "%s/fastboot_crc_%s.tsv", logdir, dsl_build_id);
    FILE *f = fopen(tsv, "r"); char l[600];
    while (f && fgets(l, sizeof l, f)) {
        Rec r; memset(&r, 0, sizeof r); char rel[200];
        if (l[0] == '#' || sscanf(l, "%199[^\t]\t%x\t%lld\t%lld", rel, &r.crc, &r.size, &r.mtime) != 4 || strlen(rel) >= sizeof r.rel) continue;
        snprintf(r.rel, sizeof r.rel, "%s", rel);
        if (nrec == caprec) { caprec = caprec ? caprec * 2 : 32; recs = realloc(recs, caprec * sizeof *recs); }
        recs[nrec++] = r;
    }
    if (f) fclose(f);
    if (dsl_hook(DSL_OWNER_LOADER, a, (void *)my_load, (void **)&o_load, "loader fastboot AssetBundle.LoadFromFileAsync_Internal")) {
        dsl_log("fastboot: off (hook refused)"); return;
    }
    on = 1;
    dsl_log("fastboot: bundle CRC skip on (%d bundle(s) checked before, %s)", nrec, tsv);
}
int dsl_fastboot_stats(int *n_skipped, int *n_checked) { if (n_skipped) *n_skipped = skipped; if (n_checked) *n_checked = checked; return on; }
