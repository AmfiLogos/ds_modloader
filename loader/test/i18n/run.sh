#!/bin/zsh
# Unit tests of the loader's JSON reader (UTF-8 from \u escapes) and of the shipped language files. No game.
H=${0:A:h}; L=${H:h:h}; T=$(mktemp -d "${TMPDIR:-/tmp}/dsl_i18n.XXXXXX")
clang -O0 -Wall -I$L/src -o $T/json_test $H/json_test.c $L/src/dsl_json.c || exit 1
$T/json_test || exit 1
clang -O0 -Wall -I$L/src -o $T/lang_test $H/lang_test.c $L/src/dsl_json.c || exit 1
$T/lang_test $L/lang $T || exit 1
# mod.json texts (source, and built ones when present) through the loader's JSON reader: fr name = Python's
for m in $L/modules/*/mod.json(N) $L/build/mods/*/mod.json(N) $L/build/mods_all/*/*/mod.json(N); do
  fr=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8")).get("i18n", {}).get("fr", {}).get("name", ""))' $m)
  [[ -n $fr ]] || continue
  $T/lang_test modjson $m "$fr" || exit 1
done
echo "== i18n tests all ok"
