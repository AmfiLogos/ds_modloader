// Stands in for libsteam_api / GameAssembly in quiet_test: an image dlopened after launch whose binds (system, dlsym)
// must go through dsl_quiet_mac.c. The command only names a missing file: harmless if it ever ran.
#include <dlfcn.h>
#include <stdbool.h>
#include <stdlib.h>
bool SteamAPI_RestartAppIfNecessary(unsigned app) { (void)app; return system("/usr/bin/open /nonexistent/dslquiet-steam") == 0 ? true : true; }
int fs_open(void) { return system("/usr/bin/open /nonexistent/dslquiet-late"); }
void *fs_resolve(void *h, const char *n) { return dlsym(h, n); }
void *fs_restart_addr(void) { return (void *)SteamAPI_RestartAppIfNecessary; }   /* the real one, not via dlsym */
