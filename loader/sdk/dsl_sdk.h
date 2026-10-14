// dsl_sdk.h - module SDK for the Dungeon Settlers mod loader (dsload.dylib), API version 2.
// API 2 = API 1 (~/DungeonSettlersMods/loader/sdk/dsl_sdk.h) + fields appended to DSLHost + two optional
// earlier entry points. An API 1 plugin (only dsl_plugin_init) loads unchanged.
//
// A module is an arm64 dylib in <mods>/<id>/ with a mod.json (see ../NOTES.md). The loader is injected with
// DYLD_INSERT_LIBRARIES. It dlopens each enabled module in dependency order and calls, when exported:
//
//   dsl_module_load(host, self)   right after dlopen, before GameAssembly is mapped (host->ga_base == 0).
//                                 Read env/config, open logs. No hooks yet.
//   dsl_module_early(host, self)  GameAssembly is mapped, none of its code has run yet (dyld add-image time).
//                                 Hooks by RVA / per-build table work; il2cpp name lookups do NOT work yet.
//   dsl_plugin_init(host, self)   after il2cpp_init returned (API 1 entry point). Name lookups work.
//
// Return 0 for success. Any other value: every hook of this module is removed, the module is off this session.
// Crash guard: if the game dies inside one of these calls, the next start skips the module (until its file changes).
//
// Hooks: one engine for every module (one stub allocator, one registry). A function already hooked by any
// module, or starting with a branch / PC-relative instruction (another loader's or a static patch), is refused.
// Hooks may be installed in any phase; after il2cpp_init they go to a separate stub page.
//
// Log: <log dir>/dsload.log (DSL_LOG_DIR, else ~/Library/Application Support/CanOpener/Dungeon Settlers).
#pragma once
#include <stdint.h>
#include <stddef.h>

#define DSL_API_VERSION 2

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DSLPlugin DSLPlugin;          /* opaque handle, one per module */

typedef struct DSLHost {
    /* ---- API 1 (layout frozen) */
    uint32_t api_version;
    uint32_t size;                           /* sizeof(DSLHost) of the loader; fields only get appended */
    const char *game_build;                  /* Steam build, e.g. "25846389" (from the GameAssembly file, known in every phase) */
    uintptr_t ga_base;                       /* 0 in dsl_module_load */
    void (*log)(DSLPlugin *self, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    const char *(*plugin_id)(DSLPlugin *self);
    void *(*rva)(uintptr_t rva);
    int (*il2cpp_ready)(void);
    void *(*il2cpp_fn)(const char *export_name);
    void *(*find_class)(const char *namespaze, const char *name);
    void *(*find_method)(void *klass, const char *name, int argc);
    void *(*method_address)(void *method_info);
    void *(*find_method_address)(const char *namespaze, const char *klass, const char *method, int argc);
    int32_t (*field_offset)(void *klass, const char *name);
    int (*hook)(DSLPlugin *self, void *target, void *replacement, void **orig);
    int (*hook_method)(DSLPlugin *self, const char *namespaze, const char *klass, const char *method,
                       int argc, void *replacement, void **orig);
    int (*unhook)(DSLPlugin *self, void *target);

    /* ---- API 2 (appended) */
    int (*hook_rva)(DSLPlugin *self, uintptr_t rva, void *replacement, void **orig, const char *name);
    /* per-build table of this module (mod.json "hooks"/"tables": <build> -> <platform> -> tsv "name<TAB>0xRVA"):
       ga_base + rva, or NULL if the name is not in the table */
    void *(*table)(DSLPlugin *self, const char *name);
    const char *(*module_dir)(DSLPlugin *self);    /* folder of the module (its mod.json) */
    const char *(*ga_path)(void);                   /* GameAssembly.dylib path, NULL before it is mapped */
    int (*module_loaded)(const char *id);           /* 1 = that module is active in this process */
} DSLHost;

#define DSL_PLUGIN() __attribute__((visibility("default"))) const uint32_t dsl_plugin_api_version = DSL_API_VERSION

typedef int (*dsl_plugin_init_fn)(const DSLHost *host, DSLPlugin *self);
typedef int (*dsl_module_load_fn)(const DSLHost *host, DSLPlugin *self);
typedef int (*dsl_module_early_fn)(const DSLHost *host, DSLPlugin *self);

#define DSL_EXPORT __attribute__((visibility("default")))

#ifdef __cplusplus
}
#endif
