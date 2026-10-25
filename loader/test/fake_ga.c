// Fake GameAssembly.dylib for the host test (never the game): the il2cpp exports the loader uses, a fake
// DungeonBehaviorSimulator (floor-logger's class) and a few plain functions. -O0: plain prologues.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int fake_initialized, fake_orig_init_calls, fake_orig_safe, fake_orig_normal, fake_orig_tick, fake_orig_update, fake_orig_foreign;

typedef struct { void *methodPointer; const char *name; int argc; } FakeMethod;
typedef struct { const char *name; int32_t offset; } FakeField;
typedef struct { const char *ns, *name; FakeMethod *methods; int nm; FakeField *fields; int nf; } FakeClass;

void DBS_OnEnterSafeFloor(uint8_t *self, const void *mi) { fake_orig_safe++; self[0x85] = 1; *(int32_t*)(self + 0x80) += 7; }
void DBS_OnEnterNonSafeFloor(uint8_t *self, const void *mi) { fake_orig_normal++; self[0x85] = 0; *(int32_t*)(self + 0x80) += 11; }
void DBS_Tick(uint8_t *self, const void *mi) { fake_orig_tick++; }
void MainFlow_Update(void *self, const void *mi) { fake_orig_update++; }
void Foreign_Target(void *self, const void *mi) { fake_orig_foreign++; }

/* patch target: mov w0, #1 ; ret ; mov w0, #7 ; ret */
__attribute__((naked)) int Patch_Target(void) { __asm__("mov w0, #1\n ret\n mov w0, #7\n ret"); }

/* mods-per-world: a fake CampaignLoadingEventHandler + il2cpp strings (len @0x10, utf-16 @0x14) */
typedef struct { void *k, *m; int32_t len; uint16_t c[64]; } FStr;
void *il2cpp_string_new(const char *s) { FStr *f = calloc(1, sizeof *f); while (s[f->len] && f->len < 64) { f->c[f->len] = (uint8_t)s[f->len]; f->len++; } return f; }
typedef struct { void *k, *m; int32_t state; } FIter;
int fake_cancel_calls; char fake_cancel_text[128];
void *CLEH_LoadCampaign(void *self, FStr *name, const void *mi) { FIter *it = calloc(1, sizeof *it); return it; }
int CLEH_TryGetSaveHeader(void *self, FStr *name, uint8_t *hdr, void **set, const void *mi) {
    if (!name || name->len < 5) return 0;
    for (int i = 0; i < 16; i++) hdr[i] = (uint8_t)(name->c[4] == '1' ? 0x11 : 0x22);   /* save1 -> 11111111-1111-..., save2 -> 22222222-... */
    /* FAKE_LEVELS="c1,m1;c2,m2": the combat / management levels of save1 / save2 (CampaignSaveHeader +0x64 / +0x68); none = 0 */
    const char *lv = getenv("FAKE_LEVELS"); int c[2] = { 0, 0 }, m[2] = { 0, 0 };
    if (lv) sscanf(lv, "%d,%d;%d,%d", &c[0], &m[0], &c[1], &m[1]);
    int k = name->c[4] == '1' ? 0 : 1; *(int32_t *)(hdr + 0x64) = c[k]; *(int32_t *)(hdr + 0x68) = m[k];
    return 1;
}
void CLEH_CancelIronModeLoad(void *self, FStr *notice, FStr *log, const void *mi) {
    fake_cancel_calls++; int i; for (i = 0; i < notice->len && i < 127; i++) fake_cancel_text[i] = (char)notice->c[i]; fake_cancel_text[i] = 0;
}
static FakeMethod cleh_methods[] = { { (void*)CLEH_LoadCampaign, "LoadCampaign", 1 }, { (void*)CLEH_TryGetSaveHeader, "TryGetSaveHeader", 3 },
                                     { (void*)CLEH_CancelIronModeLoad, "CancelIronModeLoad", 2 } };
int fake_load(const char *name) { FIter *it = CLEH_LoadCampaign(NULL, il2cpp_string_new(name), NULL); return it ? it->state : 99; }

static FakeMethod dbs_methods[] = {
    { (void*)DBS_OnEnterSafeFloor, "OnEnterSafeFloor", 0 }, { (void*)DBS_OnEnterNonSafeFloor, "OnEnterNonSafeFloor", 0 },
    { (void*)DBS_Tick, "Tick", 0 }, { (void*)Foreign_Target, "ForeignTarget", 0 } };
static FakeField dbs_fields[] = { { "_floorEnteredMin", 0x80 }, { "_isInSafeFloor", 0x85 } };
static FakeField hdr_fields[] = { { "CampaignGuid", 0x10 }, { "CombatDifficulty", 0x74 }, { "ManagementDifficulty", 0x78 } };   /* value type: + 0x10 */
static FakeClass classes[] = { { "Refactor.Runtime", "DungeonBehaviorSimulator", dbs_methods, 4, dbs_fields, 2 },
                               { "Refactor.Main.Event", "CampaignLoadingEventHandler", cleh_methods, 3, NULL, 0 },
                               { "Refactor", "CampaignSaveHeader", NULL, 0, hdr_fields, 3 } };
static int fake_domain, fake_image;
static void *fake_assemblies[1] = { &fake_image };

int il2cpp_init(const char *domain) { fake_orig_init_calls++; fake_initialized = 1; return 1; }
int il2cpp_init_utf16(const uint16_t *domain) { fake_orig_init_calls++; fake_initialized = 1; return 1; }
void *il2cpp_domain_get(void) { return fake_initialized ? &fake_domain : NULL; }
void **il2cpp_domain_get_assemblies(void *d, size_t *n) { *n = fake_initialized ? 1 : 0; return fake_assemblies; }
void *il2cpp_assembly_get_image(void *a) { return &fake_image; }
void *il2cpp_class_from_name(void *img, const char *ns, const char *name) {
    if (!fake_initialized) return NULL;
    for (size_t i = 0; i < sizeof classes / sizeof *classes; i++) if (!strcmp(classes[i].ns, ns) && !strcmp(classes[i].name, name)) return &classes[i];
    return NULL;
}
void *il2cpp_class_get_method_from_name(FakeClass *k, const char *name, int argc) {
    for (int i = 0; i < k->nm; i++) if (!strcmp(k->methods[i].name, name) && (argc < 0 || argc == k->methods[i].argc)) return &k->methods[i];
    return NULL;
}
void *il2cpp_class_get_field_from_name(FakeClass *k, const char *name) {
    for (int i = 0; i < k->nf; i++) if (!strcmp(k->fields[i].name, name)) return &k->fields[i];
    return NULL;
}
size_t il2cpp_field_get_offset(FakeField *f) { return (size_t)f->offset; }

void fake_transfer(void *sim, int safe) { if (safe) DBS_OnEnterSafeFloor(sim, NULL); else DBS_OnEnterNonSafeFloor(sim, NULL); }
void fake_tick(void *sim) { DBS_Tick(sim, NULL); }
void fake_frame(void) { MainFlow_Update(NULL, NULL); }
void fake_foreign(void) { Foreign_Target(NULL, NULL); }
