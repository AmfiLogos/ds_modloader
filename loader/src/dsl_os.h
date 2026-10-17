// Loader core <-> OS start part. The core (dsl_main.c, dsl_world.c, dsl_il2cpp.c, dsl_json.c) is the same C on every
// OS; everything that differs lives in the start part, which also owns the process entry:
//   Mac:     start/mac/dsl_start_mac.c  (dsload.dylib: constructor, dyld add-image callback, LC_UUID build check)
//   Windows: start/win/dsl_start_win.c  (baselib.dll proxy: DllMain, PE TimeDateStamp build check) + dsl_engine_win.c
// Start part calls, in this order:
//   dsl_core_log_open(logdir, self)        once, first
//   dsl_core_start(build, ga_id, modsdir)  build check + pin, mods scan, modules opened (dsl_module_load); 0 = go on
//   dsl_core_ga_mapped(base, ga_path)      GameAssembly mapped, none of its code ran (dsl_text_lo/hi, dsl_image_end
//                                          and dsl_ga_sym ready): engine, il2cpp_init hook, dsl_module_early
// The core calls dsl_core_il2cpp_ready itself (its own il2cpp_init hook).
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#define DSL_PLATFORM "win-x64"
#define DSL_MODULE_EXT ".dll"
#else
#define DSL_PLATFORM "mac-arm64"
#define DSL_MODULE_EXT ".dylib"
#endif

/* ---- core, called by the start part */
int dsl_core_log_open(const char *logdir, const char *self_path);
int dsl_core_start(const char *build, const char *ga_id, const char *modsdir_default);
int dsl_core_ga_mapped(uintptr_t base, const char *ga_path);
void dsl_core_ga_differs(const char *ga_id, const char *found_build);   /* mapped image != the file read at start */
const char *dsl_core_logdir(void);
/* Optional, set by the start part before dsl_core_log_open: this binary's path in UTF-8 when self_path is in another
   encoding (Windows: GetModuleFileNameA gives the ANSI code page; ds_i18n.h opens the lang folder as UTF-8). */
extern char dsl_core_self_utf8[1024];
/* Optional start-part condition, run by dsl_core_ga_mapped after the engine is up and before any module hook
   (Windows: the save-folder redirect). Non-zero = every module off this session; why = the player's message. */
extern int (*dsl_core_prereq)(char *why, size_t n);

/* ---- start part, called by the core */
const char *dsl_os_build_list(void);                     /* JSON list of the builds this loader knows: "\"a\",\"b\"" */
int dsl_os_sha256_file(const char *path, char out[65]);  /* 0 = ok */
int dsl_os_sha256_buf(const void *data, size_t n, char out[65]);
int dsl_os_file_stamp(const char *utf8, long long *size, long long *mtime);   /* 0 = ok; mtime in the OS's finest unit */
int dsl_os_replace(const char *from_utf8, const char *to_utf8);            /* rename over an existing file; 0 = ok */
void *dsl_os_dlopen(const char *path);                   /* NULL = failed, see dsl_os_dlerror */
void *dsl_os_dlsym(void *h, const char *name);
const char *dsl_os_dlerror(void);
int dsl_os_batchmode(void);                              /* the game was started with -batchmode */
void dsl_os_notice(const char *title, const char *msg);  /* one system notice (not in -batchmode) */
void *dsl_ga_sym(const char *name);
/* a path for a module's plain fopen: Mac as is; Windows the ANSI code page (the core's paths are UTF-8), or the
   8.3 short form when a letter is outside it */
const char *dsl_os_native_path(const char *utf8, char *out, size_t n);                      /* exported GameAssembly symbol (il2cpp API), NULL if none */
