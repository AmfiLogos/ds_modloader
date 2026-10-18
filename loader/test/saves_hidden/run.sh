#!/bin/zsh
# Host test of the hidden-worlds count of the mods-off start notice (dsl_saves.c). No game: two temp save folders.
H=${0:A:h}; L=${H:h:h}; T=$(mktemp -d "${TMPDIR:-/tmp}/dsl_hidden.XXXXXX")
clang -O0 -Wall -I$L/src -I$L/sdk -o $T/hidden_test $H/hidden_test.c $L/src/dsl_saves.c $L/src/dsl_json.c -Wl,-undefined,dynamic_lookup 2>&1 | grep -v 'dynamic_lookup' || true
[[ -x $T/hidden_test ]] || exit 1
setopt pipefail
$T/hidden_test $T | grep -v "^    log: "
