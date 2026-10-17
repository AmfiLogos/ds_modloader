#!/bin/zsh
# Unit test of the loader's JSON reader (UTF-8 from \u escapes). No game.
H=${0:A:h}; L=${H:h:h}; T=$(mktemp -d "${TMPDIR:-/tmp}/dsl_i18n.XXXXXX")
clang -O0 -Wall -I$L/src -o $T/json_test $H/json_test.c $L/src/dsl_json.c || exit 1
$T/json_test
