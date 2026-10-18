// OS part of the auto-update (src/dsl_update.c keeps the GitHub check, the state, the title corner and the rollback).
//   Mac:     start/mac/dsl_update_mac.c  (curl, ditto / hdiutil, CommonCrypto; app bundle swap at DSML_APP + ROOT/loader)
//   Windows: start/win/dsl_update_win.c  (URLDownloadToFile, bie_unzip_file, BCrypt; files in place under ROOT)
// Paths are UTF-8 (Windows: ANSI code page, converted inside). The game's loader uses the check / download part (any
// thread); kit / mod install runs only in the starter (Mac stub, Windows exe) before the game opens (start/dsl_update_start.c).
#pragma once
#include <stddef.h>
#include <stdio.h>

#define DSL_UMOVES 160
/* what an install changes, in order: path = the installed file or folder, backup = where the old one goes ("" = there
   was none). Each entry is recorded (and written to the open pending file `log`, flushed) BEFORE its move, so a crash
   in the middle leaves a full record. Rollback (start/dsl_update_start.c) walks it backwards and skips what did not
   happen. progress (may be NULL): 0..100 for the starter's window. */
typedef struct { int n; char path[DSL_UMOVES][1024]; char backup[DSL_UMOVES][1024]; void *log; void (*progress)(int pct); } DslMoves;

const char *dsl_uos_root(void);                          /* ROOT (Mac ~/Library/Application Support/DSModLauncher,
                                                            Windows the installer's folder); "" = unknown */
int dsl_uos_release(char *out, size_t cap);              /* the installed kit release ("0.1.7"); 0 = ok */
const char *dsl_uos_platform(void);                      /* "mac-arm64" / "win-x64" */
int dsl_uos_fetch(const char *url, const char *out_path); /* HTTP(S) GET into a file (GitHub API accept header);
                                                             0 = ok; any failure (network, 403 rate limit) = -1 */
int dsl_uos_sha256(const char *path, char hex[65]);      /* lower-case hex; 0 = ok */
int dsl_uos_mkdirs(const char *path);
int dsl_uos_exists(const char *path);
int dsl_uos_move(const char *from, const char *to);      /* rename (parent folders made); 0 = ok */
int dsl_uos_kit_asset(const char *asset_name);           /* 1 = the release asset is this OS's loader kit */
/* install a downloaded kit / mod zip of version ver. backup = a new empty folder for the old files. Every change goes
   into mv (also on failure: the caller rolls back what mv lists). The kit's RELEASE file / the mod's mod.json version
   must be ver (a release whose files carry another number would be offered again at every start). 0 = ok; else why
   holds one plain sentence. -2 = the kit is not for this install's game build (Windows: needs the installer): no file
   moved, and the starter does not count it as a failed install (it is never put into bad.txt for that). */
int dsl_uos_kit_install(const char *file, const char *ver, const char *backup, DslMoves *mv, char *why, size_t wn);
int dsl_uos_mod_install(const char *zip, const char *id, const char *ver, const char *backup, DslMoves *mv, char *why, size_t wn);
/* shared by the OS parts: record a move in mv (and its pending file) before it is made; -1 = list full */
static inline int dsl_umove_add(DslMoves *mv, const char *path, const char *backup) {
    if (mv->n >= DSL_UMOVES) return -1;
    snprintf(mv->path[mv->n], sizeof mv->path[0], "%s", path); snprintf(mv->backup[mv->n], sizeof mv->backup[0], "%s", backup); mv->n++;
    if (mv->log) { fprintf((FILE *)mv->log, "MOVE\t%s\t%s\n", path, backup); fflush((FILE *)mv->log); }
    return 0;
}

/* the starter's half (start/dsl_update_start.c, built into the Mac stub and the Windows starter): installs a downloaded
   update before the game opens, with the starter's window. Returns 1 when a new kit was installed (start the new
   starter instead of the game); label set = the game gets DSML_UPDATE=<label>. */
typedef struct { void (*open)(const char *title); void (*progress)(int pct); void (*close)(void); } DslUpdWin;
int dsl_update_starter(const char *root, int running, const DslUpdWin *w, char *label, size_t ln);
