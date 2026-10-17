// baselib.dll proxy = the Windows start part of the mod loader (Mac: dsload.dylib injected by the app stub).
// Every export of the game's own baselib.dll forwards to baselib_orig.dll (build/win/baselib_exports.def).
// GameAssembly.dll imports baselib, so our DllMain runs while GameAssembly is mapped and before any of its code, but
// under the Windows loader lock: there DllMain only ARMS (a 14-byte jump at the entry of il2cpp_init and
// il2cpp_init_utf16, no file, no LoadLibrary, no thread). The first call of either (UnityPlayer, main thread, loader
// lock free, still before any game code) runs start() once (InitOnce), puts the original bytes back and goes on into
// il2cpp_init, now through the core's own hook. GameAssembly not mapped yet at DllMain: a DLL load notification arms
// it when it maps (fallback: a thread that waits for it). Order inside start():
//   configure (environment from the starter, or defaults from the copy's own folder) -> build of the mapped
//   GameAssembly (PE TimeDateStamp + SizeOfImage) -> dsl_core_start: modules opened, dsl_module_load
//   -> dsl_core_ga_mapped: engine, dsl_core_prereq = the save folder below (failed = every module off, before any
//   module hook), il2cpp_init hook, dsl_module_early (RVA hooks) -> the Steam check below.
//   il2cpp_init returned (the core's hook): dsl_plugin_init.
// Own hooks (the start part's jobs, not the mods'): the save folder (Application.persistentDataPath -> <DSRIG_DIR>/data,
// the same folder the Mac copy uses; never the game's own save folder) and, when the
// starter found no signed-in Steam (DSL_STEAM=0), the game's Steam check off (StoreRuntimeHandler.Start -> Disabled).
// Not our copy (no DSL_MODS_DIR and no dsmodded_build.txt next to the exe): does nothing, the game runs stock.
#include "dsl_internal.h"
#include "dsl_os.h"
#include <windows.h>
#include <winternl.h>
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

static char data_dir[3300];                 /* UTF-8 */
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

/* dsl_core_prereq: the save folder, before any module hook. <DSRIG_DIR>/data is always a folder of its own (owner
   2026-10-16: modded saves never live in the game's LocalLow folder), so not redirected = modded play would write the
   normal saves: every module off. DSL_TEST_REDIRECT_FAIL=1 = the hook is not tried (test). */
static int save_folder(char *why, size_t n) {
    /* il2cpp_string_new reads UTF-8; getenv gives the ANSI code page (a user name like José breaks the path): the
       folder comes from the wide environment, UTF-8 for the game, wide for the folder itself */
    wchar_t wd[1100] = L"";
    DWORD wn = GetEnvironmentVariableW(L"DSRIG_DIR", wd, 1090);
    if (wn >= 1090) wn = 0;
    if (wn) { wcscat(wd, L"\\data"); CreateDirectoryW(wd, NULL); }
    else { wd[0] = 0; dsl_log("save folder: no DSRIG_DIR"); }
    if (!WideCharToMultiByte(CP_UTF8, 0, wd, -1, data_dir, sizeof data_dir, NULL, NULL)) { data_dir[0] = 0; wn = 0; }
    for (char *p = data_dir; *p; p++) if (*p == '\\') *p = '/';
    int test = getenv("DSL_TEST_REDIRECT_FAIL") && *getenv("DSL_TEST_REDIRECT_FAIL") == '1';
    if (test) dsl_log("DSL_TEST_REDIRECT_FAIL=1: save folder hook not tried");
    if (!test && wn && !dsl_hook(DSL_OWNER_LOADER, (void *)(dsl_ga_base + W_PERSISTENT_PATH), (void *)my_pdp, (void **)&o_pdp, "Application.get_persistentDataPath")) {
        dsl_log("save folder: %s", data_dir);
        return 0;
    }
    dsl_log("save folder NOT redirected to %s: modded play would write the normal saves", data_dir);
    snprintf(why, n, "%s", TR("The mod loader could not point the game to the Dungeon Settlers Modded save folder, so mods are off. "
             "This session runs the normal game with your normal saves, as if started from Steam. Close the game and "
             "start Dungeon Settlers Modded again to play with mods."));
    return -1;
}

static void steam_check(void) {
    const char *st = getenv("DSL_STEAM");
    if (!st || *st != '1')
        dsl_hook(DSL_OWNER_LOADER, (void *)(dsl_ga_base + W_STORE_START), (void *)my_store_start, (void **)&o_store_start, "StoreRuntimeHandler.Start (Steam check off)");
}

/* ---- the start's timing, written to dsload.log once it is open (proof of the order) */
static LARGE_INTEGER qpf, t_main, t_arm, t_start;
static DWORD tid_main, tid_arm, tid_start;
static int lock_main = -1, lock_arm = -1, lock_start = -1;
static const char *arm_how = "", *start_via = "";
static double ms(LARGE_INTEGER t) { return qpf.QuadPart ? (double)(t.QuadPart - t_main.QuadPart) * 1000.0 / (double)qpf.QuadPart : 0; }
/* 1 = this thread holds the loader lock (PEB->LoaderLock, x64 offset 0x110), -1 = unknown */
static int loader_lock_held(void) {
    static BOOLEAN (NTAPI *held)(void *);
    if (!held) { HMODULE nt = GetModuleHandleA("ntdll.dll"); held = nt ? (void *)GetProcAddress(nt, "RtlIsCriticalSectionLockedByThread") : NULL; }
    void *peb = (void *)__readgsqword(0x60), *ll = peb ? *(void **)((char *)peb + 0x110) : NULL;
    return held && ll ? held(ll) != 0 : -1;
}
static const char *yn(int v) { return v < 0 ? "unknown" : v ? "yes" : "no"; }

static void start(HMODULE ga) {
    uint64_t lo, hi, end;
    char self[MAX_PATH] = "", path[MAX_PATH] = "", id[64];
    if (!configure()) return;
    GetModuleFileNameA(self_mod, self, sizeof self);
    GetModuleFileNameA(ga, path, sizeof path);
    for (char *p = self; *p; p++) if (*p == '\\') *p = '/';
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    const char *rd = getenv("DSRIG_DIR");
    if (dsl_core_log_open(rd, self)) return;
    dsl_log("start order: DllMain thread %lu at 0 ms, loader lock held: %s", tid_main, yn(lock_main));
    dsl_log("start order: armed thread %lu at %.1f ms, loader lock held: %s (GameAssembly %s)", tid_arm, ms(t_arm), yn(lock_arm), arm_how);
    dsl_log("start order: start thread %lu at %.1f ms via %s, loader lock held: %s", tid_start, ms(t_start), start_via, yn(lock_start));
    dsl_soft_snapshot(dsl_core_logdir());                                        /* soft restart: role A or B, tick */
    const char *build = ga_build(ga, &lo, &hi, &end, id, sizeof id);
    dsl_log("GameAssembly %s: %s", path, id);
    char mods[MAX_PATH + 16] = "";
    if (dsl_core_start(build, id, mods)) return;                                /* modules opened, dsl_module_load */
    dsl_text_lo = lo; dsl_text_hi = hi; dsl_image_end = end;
    dsl_core_prereq = save_folder;                                              /* before any module hook */
    int r = dsl_core_ga_mapped((uintptr_t)ga, path);                            /* engine, prereq, il2cpp_init hook, early */
    steam_check();                                                              /* with mods off too: the game must start */
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    dsl_log("start order: start done at %.1f ms (%s); il2cpp_init runs next", ms(t), r ? "modules not started" : "modules started");
}

/* ---- arming: a jump at the entry of il2cpp_init and il2cpp_init_utf16 to gate_*; memory writes only, safe under the
   loader lock. In build 25846389 both functions are longer than the 14 bytes written. */
#define GATE_N 14
static HMODULE ga_mod;
static uint8_t *gate_site[2], gate_old[2][GATE_N];
static INIT_ONCE gate_once = INIT_ONCE_STATIC_INIT;
static volatile LONG armed;
static void *dll_cookie;
static NTSTATUS (NTAPI *ldr_unreg)(void *);

static void *ga_export(HMODULE m, const char *name) {             /* the export table by hand: no loader call */
    IMAGE_DOS_HEADER *d = (IMAGE_DOS_HEADER *)m;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((char *)m + d->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dd->VirtualAddress) return NULL;
    IMAGE_EXPORT_DIRECTORY *e = (IMAGE_EXPORT_DIRECTORY *)((char *)m + dd->VirtualAddress);
    DWORD *names = (DWORD *)((char *)m + e->AddressOfNames), *fns = (DWORD *)((char *)m + e->AddressOfFunctions);
    WORD *ords = (WORD *)((char *)m + e->AddressOfNameOrdinals);
    for (DWORD i = 0; i < e->NumberOfNames; i++)
        if (!strcmp((char *)m + names[i], name)) return (char *)m + fns[ords[i]];
    return NULL;
}
static void write_code(uint8_t *at, const uint8_t *b) {
    DWORD old;
    if (!VirtualProtect(at, GATE_N, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(at, b, GATE_N);
    VirtualProtect(at, GATE_N, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, GATE_N);
}

/* the one init: first caller of il2cpp_init(_utf16), main thread, loader lock free; a second caller waits for it */
static BOOL CALLBACK gate_run(PINIT_ONCE once, PVOID via, PVOID *ctx) {
    (void)once; (void)ctx;
    QueryPerformanceCounter(&t_start); tid_start = GetCurrentThreadId(); lock_start = loader_lock_held(); start_via = via;
    for (int i = 0; i < 2; i++) if (gate_site[i]) write_code(gate_site[i], gate_old[i]);   /* original bytes back first */
    if (dll_cookie && ldr_unreg) ldr_unreg(dll_cookie);
    start(ga_mod);
    return TRUE;
}
static int gate_init(const char *domain) {
    InitOnceExecuteOnce(&gate_once, gate_run, (PVOID)"il2cpp_init", NULL);
    return ((int (*)(const char *))gate_site[0])(domain);                      /* now the core's hook, or the original */
}
static int gate_init_utf16(const void *domain) {
    InitOnceExecuteOnce(&gate_once, gate_run, (PVOID)"il2cpp_init_utf16", NULL);
    return ((int (*)(const void *))gate_site[1])(domain);
}

static void arm(HMODULE ga, const char *how) {
    if (InterlockedCompareExchange(&armed, 1, 0)) return;
    QueryPerformanceCounter(&t_arm); tid_arm = GetCurrentThreadId(); lock_arm = loader_lock_held(); arm_how = how;
    ga_mod = ga;
    const char *names[2] = { "il2cpp_init", "il2cpp_init_utf16" };
    void *to[2] = { (void *)gate_init, (void *)gate_init_utf16 };
    for (int i = 0; i < 2; i++) {
        if (!(gate_site[i] = ga_export(ga, names[i]))) continue;
        memcpy(gate_old[i], gate_site[i], GATE_N);
        uint8_t j[GATE_N] = { 0xFF, 0x25, 0, 0, 0, 0 };                        /* jmp [rip+0], then the address */
        memcpy(j + 6, &to[i], 8);
        write_code(gate_site[i], j);
    }
}

/* GameAssembly maps after our DllMain (not the normal case: it imports us): the loader's DLL notification, under its
   lock, arms it before any of its code runs */
typedef struct { ULONG flags; const UNICODE_STRING *full, *base; void *dll_base; ULONG size; } LdrDllData;
static VOID CALLBACK dll_loaded(ULONG reason, const LdrDllData *d, PVOID ctx) {
    (void)ctx;
    if (reason != 1 || !d || !d->base || !d->base->Buffer) return;                /* 1 = LDR_DLL_NOTIFICATION_REASON_LOADED */
    if (d->base->Length == 16 * sizeof(wchar_t) && !_wcsnicmp(d->base->Buffer, L"GameAssembly.dll", 16))
        arm((HMODULE)d->dll_base, "mapped after DllMain (DLL notification)");
}
static DWORD WINAPI wait_ga(LPVOID arg) {                                       /* no notification API: poll (may be late) */
    (void)arg;
    for (int i = 0; i < 60000; i++) {
        HMODULE m = GetModuleHandleA("GameAssembly.dll");
        if (m) { arm(m, "mapped after DllMain (polling thread)"); return 0; }
        Sleep(1);
    }
    return 0;
}

/* our copy: the starter's environment, or dsmodded_build.txt next to the exe (configure() reads it later) */
static int ours(void) {
    if (GetEnvironmentVariableA("DSL_MODS_DIR", NULL, 0) > 1) return 1;
    wchar_t exe[MAX_PATH + 32];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) return 0;
    wchar_t *sl = wcsrchr(exe, L'\\'); if (!sl) return 0;
    wcscpy(sl + 1, L"dsmodded_build.txt");
    return GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES;
}

/* Under the loader lock: no file read, no LoadLibrary, no module code, no hook engine, no thread unless polling. */
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    (void)res;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    self_mod = h;
    QueryPerformanceFrequency(&qpf); QueryPerformanceCounter(&t_main); tid_main = GetCurrentThreadId(); lock_main = loader_lock_held();
    if (!ours()) return TRUE;                                                    /* not our copy: stock game */
    HMODULE m = GetModuleHandleA("GameAssembly.dll");
    if (m) { arm(m, "already mapped at DllMain"); return TRUE; }
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    NTSTATUS (NTAPI *reg)(ULONG, void *, void *, void **) = nt ? (void *)GetProcAddress(nt, "LdrRegisterDllNotification") : NULL;
    ldr_unreg = nt ? (void *)GetProcAddress(nt, "LdrUnregisterDllNotification") : NULL;
    if (!reg || reg(0, (void *)dll_loaded, NULL, &dll_cookie) != 0) CloseHandle(CreateThread(NULL, 0, wait_ga, NULL, 0, NULL));
    return TRUE;
}
