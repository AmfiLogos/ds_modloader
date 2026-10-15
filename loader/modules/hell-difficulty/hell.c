// Hell difficulty (sixth difficulty, CombatDifficulty 5) as a dsload code module: the code half of the owner's
// ~/DungeonSettlers_HellDifficulty/b25846389/apply_hell.py, applied in memory through the loader's engine instead
// of bytes in GameAssembly. The TABLE half (difficulty rows, texts, catalog.bin) is data: the launcher applies it
// to the play copy (apply_hell.py --tables-only --out-dir), declared as "hell-difficulty-data" in "requires".
//   1. 20 in-place instruction patches (difficulty max 3->4, iron mode, unlock gate, tooltips, mood rows), each
//      checked against the expected stock bytes (rva/<build>_mac_patches.tsv);
//   2. the staff strip: on Hell, RecruitHelper.DetermineInitialEquipments drops "Staff" items from a recruit's
//      default kit (Mage, Druid, Prodigy, Cursed Child arrive without their staff). The byte patch did it with a
//      cave around the ParseItems call; here DetermineInitialEquipments keeps its `this` in a thread-local and ParseItems
//      filters only when called from that site (return address +0xA4) on a Hell campaign.
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DSL_PLUGIN();

static const DSLHost *H;
static DSLPlugin *ME;
static void *(*orig_parse)(void *self, void *items, const void *mi);
static void *(*orig_determine)(void *self, void *key, const void *mi);
static _Thread_local void *cur_helper;           /* RecruitHelper `this` of the running DetermineInitialEquipments
                                                    (the ParseItems call there does not pass it: x0 is garbage) */
static uintptr_t strip_site;                     /* return address of the ParseItems call in DetermineInitialEquipments */
static int stripped;

static int hexbytes(const char *s, uint8_t *out, int max) {
    int n = 0;
    while (s[0] && s[1] && n < max) { unsigned v; if (sscanf(s, "%2x", &v) != 1) return -1; out[n++] = (uint8_t)v; s += 2; }
    return s[0] ? -1 : n;
}

static int is_hell(void *recruit_helper) {           /* this+0x30 _campaignSetting, +0x20 CampaignStartSetting, +0x1C _combatDifficulty */
    void *cs = recruit_helper ? *(void **)((char *)recruit_helper + 0x30) : NULL;
    void *st = cs ? *(void **)((char *)cs + 0x20) : NULL;
    return st && *(int32_t *)((char *)st + 0x1C) == 5;
}
static int has_staff(void *s) {                      /* il2cpp string: length @0x10, UTF-16 chars @0x14 */
    if (!s) return 0;
    int32_t n = *(int32_t *)((char *)s + 0x10); const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
    static const uint16_t w[5] = { 'S', 't', 'a', 'f', 'f' };
    for (int32_t i = 0; i + 5 <= n; i++) if (!memcmp(c + i, w, sizeof w)) return 1;
    return 0;
}

static void *my_determine(void *self, void *key, const void *mi) {
    void *prev = cur_helper; cur_helper = self;
    void *r = orig_determine(self, key, mi);
    cur_helper = prev;
    return r;
}

/* List<ValueTuple<string,int>>: _items @0x10, _size @0x18, _version @0x1C; array data @0x20, 16 bytes per entry */
static void *my_parse(void *self, void *items, const void *mi) {
    void *list = orig_parse(self, items, mi);
    (void)self;                                      /* not RecruitHelper here: ParseItems ignores `this` */
    if (!list || (uintptr_t)__builtin_return_address(0) != strip_site || !is_hell(cur_helper)) return list;
    void *arr = *(void **)((char *)list + 0x10);
    int32_t n = *(int32_t *)((char *)list + 0x18), k = 0;
    if (!arr || n <= 0) return list;
    char *data = (char *)arr + 0x20;
    for (int32_t i = 0; i < n; i++) {
        if (has_staff(*(void **)(data + 16 * i))) continue;
        if (k != i) memcpy(data + 16 * k, data + 16 * i, 16);
        k++;
    }
    if (k != n) {
        for (int32_t i = k; i < n; i++) memset(data + 16 * i, 0, 16);   /* like List.RemoveAt: no stale string refs */
        *(int32_t *)((char *)list + 0x18) = k;
        (*(int32_t *)((char *)list + 0x1C))++;
        if (++stripped <= 20) H->log(ME, "Hell recruit: %d staff item(s) removed from the starting kit", n - k);
    }
    return list;
}

DSL_EXPORT int dsl_module_early(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    if (host->size < sizeof(DSLHost)) { host->log(self, "loader too old (needs patch_rva)"); return 1; }
    char path[1200]; snprintf(path, sizeof path, "%s/rva/%s_mac_patches.tsv", host->module_dir(self), host->game_build);
    FILE *f = fopen(path, "r");
    if (!f) { host->log(self, "no patch table %s", path); return 2; }
    char line[512]; int ok = 0, bad = 0;
    while (fgets(line, sizeof line, f)) {
        char kind[16], rv[32], old[64], neu[64], note[200] = "";
        if (line[0] == '#' || sscanf(line, "%15s %31s %63s %63s %199[^\n]", kind, rv, old, neu, note) < 4 || strcmp(kind, "patch")) continue;
        uint8_t o[16], n[16];
        int a = hexbytes(old, o, 16), b = hexbytes(neu, n, 16);
        if (a <= 0 || a != b || host->patch_rva(self, (uintptr_t)strtoull(rv, NULL, 0), o, n, (size_t)a, note)) { bad++; continue; }
        ok++;
    }
    fclose(f);
    if (bad) { host->log(self, "%d of %d patches failed: Hell OFF (the loader puts every byte back)", bad, ok + bad); return 3; }
    void *ret = host->table(self, "DetermineInitialEquipments.ParseItems_return");
    void *parse = host->table(self, "RecruitHelper.ParseItems");
    void *det = host->table(self, "RecruitHelper.DetermineInitialEquipments");
    if (!ret || !parse || !det) { host->log(self, "table entries missing"); return 4; }
    strip_site = (uintptr_t)ret;
    if (host->hook(self, det, (void *)my_determine, (void **)&orig_determine)) return 5;
    if (host->hook(self, parse, (void *)my_parse, (void **)&orig_parse)) return 6;
    host->log(self, "Hell difficulty ready: %d code patches + staff strip on CombatDifficulty 5", ok);
    return 0;
}
