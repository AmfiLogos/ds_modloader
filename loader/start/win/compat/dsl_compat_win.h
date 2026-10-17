// Force-included (-include) into the loader core when it is built for Windows (llvm-mingw): the few POSIX / BSD
// calls of the core that mingw lacks or spells differently.
#pragma once
#include <io.h>
#include <direct.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#define fsync(fd) _commit(fd)

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
/* Paths and environment values are UTF-8 inside the loader; these names go to the wide Windows calls (dsl_u8_win.h).
   The system headers come first, so their own declarations are not renamed. */
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/utime.h>
#include "dsl_u8_win.h"
#define getenv(k) dsl_u8_getenv(k)
#define setenv(k, v, ow) ((!(ow) && dsl_u8_getenv(k)) ? 0 : dsl_u8_setenv(k, v))
#define unsetenv(k) dsl_u8_setenv(k, NULL)
#define fopen(p, m) dsl_u8_fopen(p, m)
#define remove(p) dsl_u8_remove(p)
#define unlink(p) dsl_u8_remove(p)
#define rename(a, b) dsl_u8_rename(a, b)
#undef mkdir
#define mkdir(p, mode) dsl_u8_mkdir(p)
#define _mkdir(p) dsl_u8_mkdir(p)
#define rmdir(p) dsl_u8_rmdir(p)
#define access(p, m) dsl_u8_access(p, m)
#define stat(p, b) dsl_u8_stat(p, b)
#define utime(p, t) dsl_u8_utime(p, t)
#define DIR dsl_u8_DIR
#define dirent dsl_u8_dirent
#define opendir(p) dsl_u8_opendir(p)
#define readdir(d) dsl_u8_readdir(d)
#define closedir(d) dsl_u8_closedir(d)
