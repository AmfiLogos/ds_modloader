#!/bin/zsh
# Host test of the modded saves' cloud backup (dsl_cloud.c) against a fake Steam Cloud folder. No game, no Steam.
H=${0:A:h}; L=${H:h:h}; T=$(mktemp -d "${TMPDIR:-/tmp}/dsl_cloud.XXXXXX")
clang -O0 -Wall -dynamiclib -o $T/fake_steam_api.dylib $H/fake_steam_api.c || exit 1
clang -O0 -Wall -I$L/src -I$L/sdk -o $T/cloud_test $H/cloud_test.c $L/src/dsl_cloud.c -Wl,-U,_dsl_ui_kit || exit 1
setopt pipefail   # cloud_test FAIL exit or crash is the script result, not grep's
DSL_CLOUD_API=$T/fake_steam_api.dylib $T/cloud_test $T | grep -v "^    log: "
