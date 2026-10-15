// baselib.dll proxy = the Windows start part of the mod loader (Mac: dsload.dylib injected by the app stub).
// Every export of the game's own baselib.dll forwards to baselib_orig.dll (build/win/baselib_exports.def).
// GameAssembly.dll imports baselib, so DllMain runs while GameAssembly is mapped and before any of its code: the
// same moment as the Mac add-image callback. Order:
//   DllMain   configure (environment from the starter, or defaults from the copy's own folder) -> build of the
//             mapped GameAssembly (PE TimeDateStamp + SizeOfImage) -> dsl_core_start: modules opened, dsl_module_load
//             -> dsl_core_ga_mapped: engine, il2cpp_init hook, dsl_module_early (RVA hooks) -> own hooks below.
//   il2cpp_init returned (the core's hook): dsl_plugin_init.
// Own hooks (the start part's jobs, not the mods'): the save folder (Application.persistentDataPath -> <DSRIG_DIR>/data,
// the same folder the Mac copy uses) and, when the starter found no signed-in Steam (DSL_STEAM=0), the game's Steam
// check off (StoreRuntimeHandler.Start -> Disabled), so the game still starts.
// Not our copy (no DSL_MODS_DIR and no dsmodded_build.txt next to the exe): does nothing, the game runs stock.
#include "dsl_internal.h"
#include "dsl_os.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GameAssembly.dll builds this loader knows: PE TimeDateStamp + SizeOfImage -> Steam build */
static const struct { uint32_t ts, size; const char *build; } WIN_BUILDS[] = {
    { 0x6AC9E3E7u, 0x5951000u, "25846389" },
};
/* Windows RVAs of build 25846389 (win/win_map.tsv in ds_multiplayer) */
#define W_PERSISTENT_PATH 0x38E9DD0       /* UnityEngine.Application$$get_persistentDataPath */
#define W_STRING_NEW      0x49E320        /* il2cpp_string_new (export) */
#define W_STORE_START     0xA3F240        /* Refactor.Main.StoreRuntimeHandler$$Start (Steam gate) */

static char data_dir[1100];
static HMODULE self_mod;

static void env_default(const char *k, const char *v) { if (!getenv(k) || !*getenv(k)) { _putenv_s(k, v); SetEnvironmentVariableA(k, v); } }

/* the starter sets everything; started another way (a soft-restart child inherits it), the copy's folder gives it */
static int configure(void) {
    if (getenv("DSL_MODS_DIR") && *getenv("DSL_MODS_DIR")) return 1;
    char exe[MAX_PATH], root[MAX_PATH], v[MAX_PATH + 64];
    if (!GetModuleFileNameA(NULL, exe, sizeof exe)) return 0;
    char *sl = strrchr(exe, '\\'); if (!sl) return 0; *sl = 0;                       /* exe dir = ROOT\game */
    snprintf(v, sizeof v, "%s\\dsmodded_build.txt", exe);
    if (GetFileAttributesA(v) == INVALID_FILE_ATTRIBUTES) return 0;
    snprintf(root, sizeof root, "%s", exe); sl = strrchr(root, '\\'); if (!sl) return 0; *sl = 0;
    FILE *f = fopen(v, "r"); char build[64] = "";
    if (f) { if (fgets(build, sizeof build, f)) build[strcspn(build, "\r\n ")] = 0; fclose(f); }
    char prof[MAX_PATH + 32]; snprintf(prof, sizeof prof, "%s\\profiles\\modded", root);
    env_default("DSRIG_DIR", prof);
    env_default("DSRIG_LIVE", "1");
    env_default("DSRIG_FPS", "60");
    snprintf(v, sizeof v, "%s\\mods", root); env_default("DSL_MODS_DIR", v);
    snprintf(v, sizeof v, "%s\\data\\Mods\\active.json", prof); env_default("DSL_PROFILE", v);
    env_default("DSL_LOG_DIR", prof);
    snprintf(v, sizeof v, "%s\\data\\Mods\\worlds.json", prof); env_default("DSML_SESSION", v);
    if (*build) env_default("DSML_BUILD", build);
    env_default("DSML_APP", exe);
    snprintf(v, sizeof v, "%s\\data", prof); env_default("DSML_DATA", v);
    env_default("SteamAppId", "2798330"); env_default("SteamGameId", "2798330");
    return 1;
}

static const char *ga_build(HMODULE ga, uint64_t *text_lo, uint64_t *text_hi, uint64_t *image_end, char *id, size_t idn) {
    IMAGE_DOS_HEADER *d = (IMAGE_DOS_HEADER *)ga;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((char *)ga + d->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    *text_lo = UINT64_MAX; *text_hi = 0;                 /* every code section (.text and il2cpp in this build) */
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) if (s[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
        if (s[i].VirtualAddress < *text_lo) *text_lo = s[i].VirtualAddress;
        if (s[i].VirtualAddress + s[i].Misc.VirtualSize > *text_hi) *text_hi = s[i].VirtualAddress + s[i].Misc.VirtualSize;
    }
    *image_end = nt->OptionalHeader.SizeOfImage;
    snprintf(id, idn, "PE stamp %08X size %08X", (unsigned)nt->FileHeader.TimeDateStamp, (unsigned)nt->OptionalHeader.SizeOfImage);
    for (size_t i = 0; i < sizeof WIN_BUILDS / sizeof WIN_BUILDS[0]; i++)
        if (WIN_BUILDS[i].ts == nt->FileHeader.TimeDateStamp && WIN_BUILDS[i].size == nt->OptionalHeader.SizeOfImage) return WIN_BUILDS[i].build;
    return NULL;
}

/* ---- own hooks */
typedef void *Obj;
static Obj (*o_pdp)(void *);
static Obj my_pdp(void *mi) {
    (void)mi; (void)o_pdp;
    return ((Obj (*)(const char *))(dsl_ga_base + W_STRING_NEW))(data_dir);
}
static int (*o_store_start)(Obj, Obj, void *);
static int my_store_start(Obj self, Obj on_signal, void *mi) {
    (void)self; (void)on_signal; (void)mi; (void)o_store_start;
    dsl_log("StoreRuntimeHandler.Start -> Disabled (no signed-in Steam at start)");
    return 0;
}

static void own_hooks(void) {
    const char *rd = getenv("DSRIG_DIR");
    if (rd && *rd) {
        snprintf(data_dir, sizeof data_dir, "%s/data", rd);
        for (char *p = data_dir; *p; p++) if (*p == '\\') *p = '/';
        CreateDirectoryA(data_dir, NULL);
        if (dsl_hook(DSL_OWNER_LOADER, (void *)(dsl_ga_base + W_PERSISTENT_PATH), (void *)my_pdp, (void **)&o_pdp, "Application.get_persistentDataPath"))
            dsl_log("save folder NOT redirected: the game would use its normal saves; mods stay off");
        else dsl_log("save folder: %s", data_dir);
    }
    const char *st = getenv("DSL_STEAM");
    if (!st || *st != '1')
        dsl_hook(DSL_OWNER_LOADER, (void *)(dsl_ga_base + W_STORE_START), (void *)my_store_start, (void **)&o_store_start, "StoreRuntimeHandler.Start (Steam check off)");
}

static void start(HMODULE ga) {
    uint64_t lo, hi, end;
    char self[MAX_PATH] = "", path[MAX_PATH] = "", id[64];
    GetModuleFileNameA(self_mod, self, sizeof self);
    GetModuleFileNameA(ga, path, sizeof path);
    for (char *p = self; *p; p++) if (*p == '\\') *p = '/';
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    const char *rd = getenv("DSRIG_DIR");
    if (dsl_core_log_open(rd, self)) return;
    dsl_soft_snapshot(dsl_core_logdir());                                        /* soft restart: role A or B, tick */
    const char *build = ga_build(ga, &lo, &hi, &end, id, sizeof id);
    dsl_log("GameAssembly %s: %s", path, id);
    char mods[MAX_PATH + 16] = "";
    if (dsl_core_start(build, id, mods)) return;                                /* modules opened, dsl_module_load */
    dsl_text_lo = lo; dsl_text_hi = hi; dsl_image_end = end;
    if (dsl_core_ga_mapped((uintptr_t)ga, path)) return;                        /* engine, il2cpp_init hook, dsl_module_early */
    own_hooks();
}

static DWORD WINAPI wait_ga(LPVOID arg) {
    (void)arg;
    for (int i = 0; i < 60000; i++) {
        HMODULE m = GetModuleHandleA("GameAssembly.dll");
        if (m) { start(m); return 0; }
        Sleep(1);
    }
    dsl_log("GameAssembly.dll never loaded");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    (void)res;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    self_mod = h;
    if (!configure()) return TRUE;                       /* not our copy: stock game */
    HMODULE m = GetModuleHandleA("GameAssembly.dll");
    if (m) start(m);
    else CloseHandle(CreateThread(NULL, 0, wait_ga, NULL, 0, NULL));
    return TRUE;
}
