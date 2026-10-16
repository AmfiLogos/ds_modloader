// "GameAssembly maps after baselib.dll" (review F2): loads the proxy baselib.dll of a Windows copy first, then
// GameAssembly.dll, and checks that the proxy armed il2cpp_init and il2cpp_init_utf16 (a jmp [rip] into baselib.dll)
// from the DLL notification, without calling them. Usage: late_ga_test.exe <copy's game folder>. Exit 0 = armed.
// Build: x86_64-w64-mingw32-clang -O2 late_ga_test.c -o late_ga_test.exe. Run under wine in the DSMP bottle.
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int check(HMODULE ga, HMODULE bl, const char *name) {
    unsigned char *f = (unsigned char *)GetProcAddress(ga, name);
    if (!f) { printf("%s: no export\n", name); return 1; }
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((char *)bl + ((IMAGE_DOS_HEADER *)bl)->e_lfanew);
    uintptr_t lo = (uintptr_t)bl, hi = lo + nt->OptionalHeader.SizeOfImage, to;
    memcpy(&to, f + 6, 8);
    int ok = f[0] == 0xFF && f[1] == 0x25 && !f[2] && !f[3] && !f[4] && !f[5] && to >= lo && to < hi;
    printf("%s: %02x %02x ... -> %p (baselib.dll %p-%p): %s\n", name, f[0], f[1], (void *)to, (void *)lo, (void *)hi, ok ? "armed" : "NOT armed");
    return !ok;
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: late_ga_test.exe <game folder>\n"); return 2; }
    char p[MAX_PATH];
    SetEnvironmentVariableA("DSL_MODS_DIR", "unused");                /* the proxy's "our copy" check */
    SetDllDirectoryA(argv[1]);
    snprintf(p, sizeof p, "%s\\baselib.dll", argv[1]);
    HMODULE bl = LoadLibraryExA(p, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!bl) { printf("baselib.dll: load failed %lu\n", GetLastError()); return 2; }
    printf("baselib.dll loaded first; GameAssembly mapped: %s\n", GetModuleHandleA("GameAssembly.dll") ? "yes (test invalid)" : "no");
    snprintf(p, sizeof p, "%s\\GameAssembly.dll", argv[1]);
    HMODULE ga = LoadLibraryExA(p, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!ga) { printf("GameAssembly.dll: load failed %lu\n", GetLastError()); return 2; }
    int bad = check(ga, bl, "il2cpp_init") | check(ga, bl, "il2cpp_init_utf16");
    printf("%s\n", bad ? "FAIL" : "PASS");
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), bad);                      /* never runs GameAssembly's code further */
    return bad;
}
