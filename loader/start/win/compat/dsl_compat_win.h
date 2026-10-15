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
