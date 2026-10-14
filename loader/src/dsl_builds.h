// Known GameAssembly builds (arm64 slice LC_UUID; patching and re-signing do not change it).
// The loader stays inactive on any other build. No cave addresses: the engine maps its own region.
#pragma once
typedef struct { const char *uuid, *build; } DSLBuild;
static const DSLBuild DSL_BUILDS[] = {
    { "3C5DD8BD-E371-3C0E-A674-A81042B0AC9F", "25846389" },
#ifdef DSL_TEST
#include "test_builds.inc"      /* host test only: the fake GameAssembly */
#endif
};
