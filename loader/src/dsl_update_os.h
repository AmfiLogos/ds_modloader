// OS part of the auto-update (src/dsl_update.c keeps the GitHub check, the state, the title corner and the rollback).
//   Mac:     start/mac/dsl_update_mac.c  (curl, ditto / hdiutil, CommonCrypto; app bundle swap at DSML_APP + ROOT/loader)
//   Windows: start/win/dsl_update_win.c  (URLDownloadToFile, bie_unzip_file, BCrypt; files in place under ROOT)
// Paths are UTF-8 (Windows: converted inside). Every function may run on the update thread except kit/mod install
// (main thread, at the title, nothing busy).
#pragma once
#include <stddef.h>

#define DSL_UMOVES 160
/* what an install changed, in order: path = the installed file or folder, backup = where the old one went
   ("" = there was none). Rollback (dsl_update.c) walks it backwards. */
typedef struct { int n; char path[DSL_UMOVES][1024]; char backup[DSL_UMOVES][1024]; } DslMoves;

const char *dsl_uos_root(void);                          /* ROOT (Mac ~/Library/Application Support/DSModLauncher,
                                                            Windows the installer's folder); "" = unknown */
int dsl_uos_release(char *out, size_t cap);              /* the installed kit release ("13.2"); 0 = ok */
const char *dsl_uos_platform(void);                      /* "mac-arm64" / "win-x64" */
int dsl_uos_fetch(const char *url, const char *out_path); /* HTTP(S) GET into a file (GitHub API accept header);
                                                             0 = ok; any failure (network, 403 rate limit) = -1 */
int dsl_uos_sha256(const char *path, char hex[65]);      /* lower-case hex; 0 = ok */
int dsl_uos_mkdirs(const char *path);
int dsl_uos_exists(const char *path);
int dsl_uos_move(const char *from, const char *to);      /* rename (parent folders made); 0 = ok */
int dsl_uos_kit_asset(const char *asset_name);           /* 1 = the release asset is this OS's loader kit */
/* install a downloaded kit / mod zip. backup = a new empty folder for the old files. Every change goes into mv (also
   on failure: the caller rolls back what mv lists). 0 = ok; else why holds one plain sentence. */
int dsl_uos_kit_install(const char *file, const char *backup, DslMoves *mv, char *why, size_t wn);
int dsl_uos_mod_install(const char *zip, const char *id, const char *backup, DslMoves *mv, char *why, size_t wn);
