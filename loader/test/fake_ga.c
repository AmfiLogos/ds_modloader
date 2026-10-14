// Fake GameAssembly.dylib for the host test (never the game): the il2cpp exports the loader uses, a fake
// DungeonBehaviorSimulator (floor-logger's class) and a few plain functions. -O0: plain prologues.
#include <stdint.h>
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

static FakeMethod dbs_methods[] = {
    { (void*)DBS_OnEnterSafeFloor, "OnEnterSafeFloor", 0 }, { (void*)DBS_OnEnterNonSafeFloor, "OnEnterNonSafeFloor", 0 },
    { (void*)DBS_Tick, "Tick", 0 }, { (void*)Foreign_Target, "ForeignTarget", 0 } };
static FakeField dbs_fields[] = { { "_floorEnteredMin", 0x80 }, { "_isInSafeFloor", 0x85 } };
static FakeClass classes[] = { { "Refactor.Runtime", "DungeonBehaviorSimulator", dbs_methods, 4, dbs_fields, 2 } };
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
