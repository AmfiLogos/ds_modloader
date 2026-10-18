#!/bin/zsh
# Host test of mod settings (src/dsl_settings.c), no game. Temp folder per run.
H=${0:A:h}; L=${H:h:h}; T=$(mktemp -d "${TMPDIR:-/tmp}/dsl_settings.XXXXXX")
clang -O0 -g -Wall -fsanitize=address,undefined -I$L/src -I$L/sdk -o $T/settings_test $H/settings_test.c $L/src/dsl_settings.c $L/src/dsl_json.c || exit 1
$T/settings_test $T
