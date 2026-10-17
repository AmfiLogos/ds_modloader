// Windows OS part of the loader core (src/dsl_os.h): module DLLs, sha256 (BCrypt), GameAssembly exports, notices.
// The soft restart's OS part is dsl_soft_win.c.
#include "dsl_internal.h"
#include "dsl_os.h"
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include "dsl_u8_names_win.h"                     /* last: paths and environment are UTF-8 (wide Windows calls) */

/* GameAssembly.dll builds this loader knows (dsl_start_win.c reads the mapped image's PE header) */
const char *dsl_os_build_list(void) { return "\"25846389\""; }

int dsl_os_sha256_file(const char *path, char out[65]) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    BCRYPT_ALG_HANDLE alg = NULL; BCRYPT_HASH_HANDLE h = NULL; int r = -1;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 && BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0) {
        static unsigned char buf[1 << 16]; size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) BCryptHashData(h, buf, (ULONG)n, 0);
        unsigned char d[32];
        if (BCryptFinishHash(h, d, sizeof d, 0) == 0) { for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]); r = 0; }
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    fclose(f);
    return r;
}

int dsl_os_sha256_buf(const void *data, size_t n, char out[65]) {
    BCRYPT_ALG_HANDLE alg = NULL; BCRYPT_HASH_HANDLE h = NULL; unsigned char d[32]; int r = -1;
    if (!BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) && !BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) &&
        !BCryptHashData(h, (PUCHAR)data, (ULONG)n, 0) && !BCryptFinishHash(h, d, sizeof d, 0)) {
        for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]); r = 0;
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return r;
}
static char dlerr[512];
void *dsl_os_dlopen(const char *path) {
    char p[1200]; snprintf(p, sizeof p, "%s", path);
    for (char *s = p; *s; s++) if (*s == '/') *s = '\\';
    /* the module's own folder first for its dependencies */
    HMODULE m = LoadLibraryExA(p, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) {
        DWORD e = GetLastError();
        int k = snprintf(dlerr, sizeof dlerr, "LoadLibrary %s: error %lu ", p, e);
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, dlerr + k, (DWORD)(sizeof dlerr - k), NULL);
        for (char *s = dlerr; *s; s++) if (*s == '\r' || *s == '\n') *s = ' ';
    }
    return m;
}
void *dsl_os_dlsym(void *h, const char *name) { return h ? (void *)GetProcAddress((HMODULE)h, name) : NULL; }
const char *dsl_os_dlerror(void) { return dlerr; }

int dsl_os_batchmode(void) { return strstr(GetCommandLineA(), "-batchmode") != NULL; }

/* shown from its own thread: the core may call this inside DllMain (loader lock), where a window must not open */
typedef struct { wchar_t t[256], m[1024]; } Notice;
static DWORD WINAPI notice_thread(LPVOID a) {
    Notice *n = a;
    MessageBoxW(NULL, n->m, n->t, MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
    HeapFree(GetProcessHeap(), 0, n);
    return 0;
}
void dsl_os_notice(const char *title, const char *msg) {
    Notice *n = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *n);
    if (!n) return;
    MultiByteToWideChar(CP_UTF8, 0, title, -1, n->t, 256);
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, n->m, 1024);
    HANDLE h = CreateThread(NULL, 0, notice_thread, n, 0, NULL);
    if (h) CloseHandle(h); else HeapFree(GetProcessHeap(), 0, n);
}

const char *dsl_os_native_path(const char *utf8, char *out, size_t n) {
    wchar_t w[2048], s[2048]; BOOL lossy = FALSE;
    snprintf(out, n, "%s", utf8 ? utf8 : "");
    if (!utf8 || dsl_u8_to_w(utf8, w, 2048)) return out;
    if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, out, (int)n, NULL, &lossy) && !lossy) return out;
    DWORD k = GetShortPathNameW(w, s, 2048);
    lossy = FALSE;
    if (k && k < 2048 && WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, s, -1, out, (int)n, NULL, &lossy) && !lossy) return out;
    snprintf(out, n, "%s", utf8);                             /* nothing better: as is */
    return out;
}

void *dsl_ga_sym(const char *name) {
    HMODULE ga = dsl_ga_base ? (HMODULE)dsl_ga_base : GetModuleHandleA("GameAssembly.dll");
    return ga && name ? (void *)GetProcAddress(ga, name) : NULL;
}
