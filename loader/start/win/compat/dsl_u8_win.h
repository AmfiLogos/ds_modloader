// Windows: every path and environment value inside the loader (core, start part, starter) is UTF-8, and only these
// helpers talk to Windows, through its wide (W) calls. The narrow CRT / "A" calls use the ANSI code page: a user name
// like "José Søren" (or one outside that code page) gave broken paths (saves audit item 2, install audit P1-1, P2-10).
// The core gets them by name through compat/dsl_compat_win.h (getenv, fopen, stat ...).
#pragma once
#include <stdio.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/utime.h>
#include <wchar.h>

int dsl_u8_to_w(const char *s, wchar_t *out, size_t n);          /* 0 = ok */
int dsl_w_to_u8(const wchar_t *s, char *out, size_t n);          /* 0 = ok */
const char *dsl_u8_getenv(const char *name);                     /* the process environment, UTF-8; stays valid */
int dsl_u8_setenv(const char *name, const char *value);          /* NULL value = remove; also the CRT copy */
FILE *dsl_u8_fopen(const char *path, const char *mode);
int dsl_u8_remove(const char *path);
int dsl_u8_rename(const char *from, const char *to);             /* replaces an existing target (POSIX rename) */
int dsl_u8_mkdir(const char *path);
int dsl_u8_rmdir(const char *path);
int dsl_u8_access(const char *path, int mode);
int dsl_u8_stat(const char *path, struct stat *st);              /* st_mode (dir / file), st_size, st_mtime */
int dsl_u8_utime(const char *path, const struct utimbuf *t);
unsigned long dsl_u8_attr(const char *path);                     /* GetFileAttributesW; 0xFFFFFFFF = missing */
void *dsl_u8_loadlib(const char *path, unsigned long flags);     /* LoadLibraryExW */
int dsl_u8_module_path(void *module, char *out, size_t n);       /* GetModuleFileNameW; 0 = ok */

/* opendir / readdir / closedir with UTF-8 names (only d_name) */
struct dsl_u8_dirent { char d_name[1040]; };
typedef struct dsl_u8_dir dsl_u8_DIR;
dsl_u8_DIR *dsl_u8_opendir(const char *path);
struct dsl_u8_dirent *dsl_u8_readdir(dsl_u8_DIR *d);
int dsl_u8_closedir(dsl_u8_DIR *d);

/* FindFirstFile / FindNextFile with UTF-8 names (cFileName, dwFileAttributes); FindClose as usual */
typedef struct { unsigned long dwFileAttributes; char cFileName[1040]; } dsl_u8_find;
void *dsl_u8_findfirst(const char *pattern, dsl_u8_find *fd);    /* INVALID_HANDLE_VALUE when nothing */
int dsl_u8_findnext(void *h, dsl_u8_find *fd);
int dsl_u8_copyfile(const char *from, const char *to, int fail_if_exists);   /* nonzero = ok, as CopyFileA */
int dsl_u8_deletefile(const char *path);                         /* nonzero = ok */
int dsl_u8_createdir(const char *path);                          /* nonzero = ok */
int dsl_u8_removedir(const char *path);                          /* nonzero = ok */
int dsl_u8_movefile(const char *from, const char *to, unsigned long flags);  /* nonzero = ok */
