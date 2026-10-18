// Mod settings for zetu-suite. With a loader SDK that has mod settings (API 5, dsl_get_bool / dsl_get_int on
// DSLHost), the real wrappers of dsl_sdk.h are used. With an older SDK header this stub gives the same calls:
// the default value (an older loader has no settings), or DSL_ZETU_<ID> from the environment (test rigs only).
#pragma once
#include "dsl_sdk.h"
#include <stdlib.h>

#if DSL_API_VERSION < 5
static inline int zetu_env_int(const char *id, int dflt) {
    char k[96] = "DSL_ZETU_"; size_t n = 9;
    for (const char *p = id; *p && n + 1 < sizeof k; p++) k[n++] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
    k[n] = 0;
    const char *v = getenv(k);
    return v && *v ? atoi(v) : dflt;
}
static inline int dsl_get_bool(const DSLHost *h, DSLPlugin *self, const char *id, int dflt) {
    (void)h; (void)self; return zetu_env_int(id, dflt) != 0; }
static inline int dsl_get_int(const DSLHost *h, DSLPlugin *self, const char *id, int dflt) {
    (void)h; (void)self; return zetu_env_int(id, dflt); }
#endif
