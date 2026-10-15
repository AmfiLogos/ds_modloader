// TEST ONLY: starts the kit + the Mods window in a test dsload (win_glue.c) from a module's plugin_init.
#include "dsl_sdk.h"
#include <dlfcn.h>
DSL_PLUGIN();
DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    int (*start)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "dsltest_start");
    if (!start) { host->log(self, "dsltest_start not found: not the test loader"); return -1; }
    return start();
}
