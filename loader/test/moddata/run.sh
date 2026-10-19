#!/bin/zsh
# Host test of the mod data folder + its multiplayer sync (src/dsl_moddata.c), no game. Temp folder per run.
H=${0:A:h}; L=${H:h:h}; T=$(mktemp -d "${TMPDIR:-/tmp}/dsl_moddata.XXXXXX")
clang -O0 -g -Wall -fsanitize=address,undefined -I$L/src -I$L/sdk -o $T/moddata_test $H/moddata_test.c $L/src/dsl_moddata.c $L/src/dsl_json.c || exit 1
$T/moddata_test $T
