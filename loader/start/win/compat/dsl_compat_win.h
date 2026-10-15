// Force-included (-include) into the loader core when it is built for Windows (llvm-mingw): the few POSIX / BSD
// calls of the core that mingw lacks or spells differently.
#pragma once
#include <io.h>
#include <direct.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#define fsync(fd) _commit(fd)
#define mkdir(p, mode) _mkdir(p)
static inline size_t dsl_strlcat(char *d, const char *s, size_t n) {
    size_t l = strlen(d), k = strlen(s);
    if (l + 1 < n) { size_t c = k < n - l - 1 ? k : n - l - 1; memcpy(d + l, s, c); d[l + c] = 0; }
    return l + k;
}
#define strlcat dsl_strlcat
static inline char *dsl_strcasestr(const char *h, const char *n) {
    size_t k = strlen(n);
    for (; *h; h++) if (!_strnicmp(h, n, k)) return (char *)h;
    return k ? NULL : (char *)h;
}
#define strcasestr dsl_strcasestr
/* POSIX rename replaces an existing target; the Windows CRT's fails (status / handoff / modset files went stale) */
__declspec(dllimport) int __stdcall MoveFileExA(const char *from, const char *to, unsigned long flags);
static inline int dsl_rename(const char *from, const char *to) { return MoveFileExA(from, to, 0x1 | 0x2) ? 0 : -1; }  /* REPLACE_EXISTING | COPY_ALLOWED */
#define rename dsl_rename
/* setenv / unsetenv: the CRT copy (getenv) and the process block (children, the soft restart's B) */
#include <stdlib.h>
__declspec(dllimport) int __stdcall SetEnvironmentVariableA(const char *name, const char *value);
static inline int dsl_setenv(const char *k, const char *v, int ow) {
    if (!ow && getenv(k)) return 0;
    SetEnvironmentVariableA(k, v); return _putenv_s(k, v) ? -1 : 0;
}
static inline int dsl_unsetenv(const char *k) { SetEnvironmentVariableA(k, 0); return _putenv_s(k, "") ? -1 : 0; }
#define setenv dsl_setenv
#define unsetenv dsl_unsetenv
