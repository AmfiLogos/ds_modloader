// Included LAST by the Windows start files (after <windows.h>): their narrow CRT and "A" calls go to the UTF-8 helpers
// of dsl_u8_win.h, so every path and environment value they share with the core is UTF-8 (the core gets the same
// names through dsl_compat_win.h).
#pragma once
#include <string.h>
#include "dsl_u8_win.h"
#define getenv(k) dsl_u8_getenv(k)
#define fopen(p, m) dsl_u8_fopen(p, m)
#define _putenv_s(k, v) dsl_u8_setenv(k, v)
#define SetEnvironmentVariableA(k, v) (dsl_u8_setenv(k, v) == 0)
#define GetModuleFileNameA(m, b, n) (dsl_u8_module_path(m, b, n) ? 0u : (DWORD)strlen(b))
#define GetFileAttributesA(p) ((DWORD)dsl_u8_attr(p))
#define LoadLibraryExA(p, h, f) ((HMODULE)dsl_u8_loadlib(p, f))
#define WIN32_FIND_DATAA dsl_u8_find
#define FindFirstFileA(p, fd) ((HANDLE)dsl_u8_findfirst(p, fd))
#define FindNextFileA(h, fd) dsl_u8_findnext(h, fd)
#define CopyFileA(a, b, f) dsl_u8_copyfile(a, b, f)
#define DeleteFileA(p) dsl_u8_deletefile(p)
#define CreateDirectoryA(p, sa) dsl_u8_createdir(p)
#define RemoveDirectoryA(p) dsl_u8_removedir(p)
#define MoveFileExA(a, b, f) dsl_u8_movefile(a, b, f)
