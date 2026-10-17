// UTF-8 paths and environment over the wide Windows calls (see dsl_u8_win.h).
#include "dsl_u8_win.h"
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#define WP 2048                                           /* wide path buffer (characters) */

int dsl_u8_to_w(const char *s, wchar_t *out, size_t n) {
    if (!s || !n) return -1;
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)n)) { out[0] = 0; return -1; }
    return 0;
}
int dsl_w_to_u8(const wchar_t *s, char *out, size_t n) {
    if (!s || !n) return -1;
    if (!WideCharToMultiByte(CP_UTF8, 0, s, -1, out, (int)n, NULL, NULL)) { out[0] = 0; return -1; }
    return 0;
}

/* getenv: one copy per name and value; a changed value gets a new copy (old pointers stay valid, as the CRT's) */
typedef struct { char *name, *val; } EnvC;
static EnvC envc[128];
static int nenv;
static SRWLOCK envl = SRWLOCK_INIT;
const char *dsl_u8_getenv(const char *name) {
    wchar_t wn[256], wv[32768];
    if (dsl_u8_to_w(name, wn, 256)) return NULL;
    DWORD r = GetEnvironmentVariableW(wn, wv, 32768);
    if (!r || r >= 32768) return NULL;
    char *v = malloc((size_t)r * 4 + 4);
    if (!v || dsl_w_to_u8(wv, v, (size_t)r * 4 + 4)) { free(v); return NULL; }
    AcquireSRWLockExclusive(&envl);
    const char *res = v;
    int i = 0;
    for (; i < nenv; i++) if (!strcmp(envc[i].name, name)) break;
    if (i < nenv && !strcmp(envc[i].val, v)) { res = envc[i].val; free(v); }
    else if (i < nenv) envc[i].val = v;                   /* the old copy is kept (callers may hold it) */
    else if (nenv < 128) { envc[nenv].name = _strdup(name); envc[nenv].val = v; nenv++; }
    ReleaseSRWLockExclusive(&envl);
    return res;
}
int dsl_u8_setenv(const char *name, const char *value) {
    wchar_t wn[256], *wv = NULL;
    if (dsl_u8_to_w(name, wn, 256)) return -1;
    if (value) {
        size_t n = strlen(value) + 1;
        wv = malloc(n * sizeof *wv);
        if (!wv || dsl_u8_to_w(value, wv, n)) { free(wv); return -1; }
    }
    BOOL ok = SetEnvironmentVariableW(wn, wv);
    _wputenv_s(wn, wv ? wv : L"");                        /* the CRT's copy too (code that still reads _wgetenv) */
    free(wv);
    return ok ? 0 : -1;
}

FILE *dsl_u8_fopen(const char *path, const char *mode) {
    wchar_t wp[WP], wm[16];
    if (dsl_u8_to_w(path, wp, WP) || dsl_u8_to_w(mode, wm, 16)) { errno = EINVAL; return NULL; }
    return _wfopen(wp, wm);
}
int dsl_u8_remove(const char *path) { wchar_t wp[WP]; return dsl_u8_to_w(path, wp, WP) ? -1 : _wremove(wp); }
int dsl_u8_rename(const char *from, const char *to) {
    wchar_t a[WP], b[WP];
    if (dsl_u8_to_w(from, a, WP) || dsl_u8_to_w(to, b, WP)) return -1;
    return MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) ? 0 : -1;
}
int dsl_u8_mkdir(const char *path) { wchar_t wp[WP]; return dsl_u8_to_w(path, wp, WP) ? -1 : _wmkdir(wp); }
int dsl_u8_rmdir(const char *path) { wchar_t wp[WP]; return dsl_u8_to_w(path, wp, WP) ? -1 : _wrmdir(wp); }
int dsl_u8_access(const char *path, int mode) { wchar_t wp[WP]; return dsl_u8_to_w(path, wp, WP) ? -1 : _waccess(wp, mode); }
unsigned long dsl_u8_attr(const char *path) { wchar_t wp[WP]; return dsl_u8_to_w(path, wp, WP) ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(wp); }

static time_t ft_unix(FILETIME f) { ULARGE_INTEGER u = { .LowPart = f.dwLowDateTime, .HighPart = f.dwHighDateTime }; return (time_t)(u.QuadPart / 10000000ULL - 11644473600ULL); }
static FILETIME unix_ft(time_t t) { ULARGE_INTEGER u; u.QuadPart = ((ULONGLONG)t + 11644473600ULL) * 10000000ULL; return (FILETIME){ u.LowPart, u.HighPart }; }
int dsl_u8_stat(const char *path, struct stat *st) {
    wchar_t wp[WP]; WIN32_FILE_ATTRIBUTE_DATA a;
    if (dsl_u8_to_w(path, wp, WP) || !GetFileAttributesExW(wp, GetFileExInfoStandard, &a)) { errno = ENOENT; return -1; }
    memset(st, 0, sizeof *st);
    st->st_mode = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? (_S_IFDIR | 0755) : (_S_IFREG | 0644);
    st->st_size = (a.nFileSizeHigh ? 0x7FFFFFFF : (long)(a.nFileSizeLow > 0x7FFFFFFF ? 0x7FFFFFFF : a.nFileSizeLow));
    st->st_mtime = ft_unix(a.ftLastWriteTime); st->st_atime = ft_unix(a.ftLastAccessTime); st->st_ctime = ft_unix(a.ftCreationTime);
    return 0;
}
int dsl_u8_utime(const char *path, const struct utimbuf *t) {
    wchar_t wp[WP];
    if (dsl_u8_to_w(path, wp, WP)) return -1;
    HANDLE h = CreateFileW(wp, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    time_t now = time(NULL);
    FILETIME a = unix_ft(t ? t->actime : now), m = unix_ft(t ? t->modtime : now);
    BOOL ok = SetFileTime(h, NULL, &a, &m);
    CloseHandle(h);
    return ok ? 0 : -1;
}
void *dsl_u8_loadlib(const char *path, unsigned long flags) { wchar_t wp[WP]; return dsl_u8_to_w(path, wp, WP) ? NULL : (void *)LoadLibraryExW(wp, NULL, flags); }
int dsl_u8_module_path(void *module, char *out, size_t n) {
    wchar_t wp[WP]; DWORD r = GetModuleFileNameW((HMODULE)module, wp, WP);
    if (!r || r >= WP) { if (n) out[0] = 0; return -1; }
    return dsl_w_to_u8(wp, out, n);
}

struct dsl_u8_dir { HANDLE h; WIN32_FIND_DATAW fd; int first; struct dsl_u8_dirent e; };
dsl_u8_DIR *dsl_u8_opendir(const char *path) {
    wchar_t wp[WP];
    if (dsl_u8_to_w(path, wp, WP - 4)) { errno = ENOENT; return NULL; }
    size_t l = wcslen(wp);
    if (l && wp[l - 1] != L'\\' && wp[l - 1] != L'/') wp[l++] = L'\\';
    wp[l++] = L'*'; wp[l] = 0;
    dsl_u8_DIR *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->h = FindFirstFileW(wp, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE) { free(d); errno = ENOENT; return NULL; }
    d->first = 1;
    return d;
}
struct dsl_u8_dirent *dsl_u8_readdir(dsl_u8_DIR *d) {
    if (!d) return NULL;
    if (!d->first && !FindNextFileW(d->h, &d->fd)) return NULL;
    d->first = 0;
    dsl_w_to_u8(d->fd.cFileName, d->e.d_name, sizeof d->e.d_name);
    return &d->e;
}
int dsl_u8_closedir(dsl_u8_DIR *d) { if (!d) return -1; FindClose(d->h); free(d); return 0; }

static void find_fill(const WIN32_FIND_DATAW *w, dsl_u8_find *fd) { fd->dwFileAttributes = w->dwFileAttributes; dsl_w_to_u8(w->cFileName, fd->cFileName, sizeof fd->cFileName); }
void *dsl_u8_findfirst(const char *pattern, dsl_u8_find *fd) {
    wchar_t wp[WP]; WIN32_FIND_DATAW w;
    if (dsl_u8_to_w(pattern, wp, WP)) return INVALID_HANDLE_VALUE;
    HANDLE h = FindFirstFileW(wp, &w);
    if (h != INVALID_HANDLE_VALUE) find_fill(&w, fd);
    return h;
}
int dsl_u8_findnext(void *h, dsl_u8_find *fd) {
    WIN32_FIND_DATAW w;
    if (!FindNextFileW((HANDLE)h, &w)) return 0;
    find_fill(&w, fd); return 1;
}
int dsl_u8_copyfile(const char *from, const char *to, int fail) {
    wchar_t a[WP], b[WP];
    return !dsl_u8_to_w(from, a, WP) && !dsl_u8_to_w(to, b, WP) && CopyFileW(a, b, fail);
}
int dsl_u8_deletefile(const char *path) { wchar_t wp[WP]; return !dsl_u8_to_w(path, wp, WP) && DeleteFileW(wp); }
int dsl_u8_createdir(const char *path) { wchar_t wp[WP]; return !dsl_u8_to_w(path, wp, WP) && CreateDirectoryW(wp, NULL); }
int dsl_u8_removedir(const char *path) { wchar_t wp[WP]; return !dsl_u8_to_w(path, wp, WP) && RemoveDirectoryW(wp); }
int dsl_u8_movefile(const char *from, const char *to, unsigned long flags) {
    wchar_t a[WP], b[WP];
    return !dsl_u8_to_w(from, a, WP) && !dsl_u8_to_w(to, b, WP) && MoveFileExW(a, b, flags);
}
