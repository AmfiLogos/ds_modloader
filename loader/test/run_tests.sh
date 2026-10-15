#!/bin/zsh
# Host test of dsload with a fake GameAssembly (never the game). Output goes to a new temp folder each run.
# usage: test/run_tests.sh
set -u; cd "${0:A:h}"; T="$PWD"; L="${T:h}"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/dsload_test.XXXXXX")
mkdir -p "$OUT"/{fw,gen,mods,log}
FW="$OUT/fw"; M="$OUT/mods"; LOGD="$OUT/log"
fails=0; pass=0
check() { if eval "$2"; then print "PASS $1"; pass=$((pass+1)); else print "FAIL $1"; fails=$((fails+1)); fi }
log() { cat "$LOGD/dsload.log" 2>/dev/null }
CC=(nice -n 19 clang -arch arm64 -mmacosx-version-min=12.0)
$CC -O0 -dynamiclib -install_name @rpath/GameAssembly.dylib -o "$FW/GameAssembly.dylib" fake_ga.c || exit 1
UUID=$(otool -l "$FW/GameAssembly.dylib" | awk '/uuid/{print $2; exit}')
print "    { \"$UUID\", \"test\" }," > "$OUT/gen/test_builds.inc"
$CC -O2 -Wall -DDSL_TEST -I"$OUT/gen" -I"$L/src" -dynamiclib -framework CoreFoundation -framework CoreGraphics -lobjc -install_name @rpath/dsload.dylib -o "$FW/dsload.dylib" \
  "$L"/src/{dsl_main,dsl_engine,dsl_il2cpp,dsl_json,dsl_world,dsl_soft}.c "$L"/start/mac/dsl_start_mac.c "$L"/start/mac/dsl_soft_mac.c || exit 1
$CC -O0 -dynamiclib -o "$FW/foreign.dylib" foreign.c || exit 1
$CC -O0 host.c -o "$OUT/host" "$FW/GameAssembly.dylib" -Wl,-rpath,"$FW" || exit 1
for f in "$FW"/*.dylib "$OUT/host"; do codesign -f -s - "$f" 2>/dev/null; done
mod() {  # mod <id> <mode> <mod.json body>
  mkdir -p "$M/$1"; print -r -- "$3" > "$M/$1/mod.json"
  [[ $2 == none ]] || { $CC -O0 -DMODE=$2 -I"$L/sdk" -dynamiclib -o "$M/$1/$1.dylib" mods_src/testmod.c && codesign -f -s - "$M/$1/$1.dylib" 2>/dev/null; }
}
mod early-mod 1 '{"id":"early-mod","version":"1.2.0","kind":"code","api":2,"game_builds":["test"],"requires":["loader>=2.0"]}'
mod clash 2 '{"id":"clash","version":"1.0.0","kind":"code","api":2,"game_builds":["test"],"requires":["early-mod>=1.0"]}'
mod plain 3 '{"id":"plain","version":"1.0.0","kind":"code","api":2,"game_build":"*","requires":["data-mod"],"tables":{"test":{"mac-arm64":"t.tsv"}}}'
print "x\t0x10" > "$M/plain/t.tsv"
mod bad-sha 3 '{"id":"bad-sha","version":"1.0.0","kind":"code","api":2,"game_builds":["test"],"platforms":{"mac-arm64":{"files":{"bad-sha.dylib":"00"}}}}'
mod conflicter 3 '{"id":"conflicter","version":"1.0.0","kind":"code","api":2,"game_builds":["test"],"conflicts":["early-mod"]}'
mod needs-missing 3 '{"id":"needs-missing","version":"1.0.0","kind":"code","api":2,"game_builds":["test"],"requires":["nope"]}'
mod other-build 3 '{"id":"other-build","version":"1.0.0","kind":"code","api":2,"game_builds":["1"]}'
mod no-table 3 '{"id":"no-table","version":"1.0.0","kind":"code","api":2,"game_builds":["test"],"tables":{"1":{"mac-arm64":"t.tsv"}}}'
mod patcher 5 '{"id":"patcher","version":"1.0.0","kind":"code","api":2,"game_builds":["test"],"scope":"client","requires":["early-mod"]}'
mod failpatch 6 '{"id":"failpatch","version":"1.0.0","kind":"code","api":2,"game_builds":["test"]}'
mod data-mod none '{"id":"data-mod","version":"1.0.0","type":"data","game_build":"test"}'
mkdir -p "$M/floor-logger"; cp ~/DungeonSettlersMods/mods/floor-logger/floor-logger.dylib "$M/floor-logger/"
sed 's/"game_build": "25846389"/"game_build": "test"/' ~/DungeonSettlersMods/mods/floor-logger/mod.json > "$M/floor-logger/mod.json"
print '{"enabled":["floor-logger","plain","early-mod","clash","bad-sha","conflicter","needs-missing","other-build","no-table","data-mod","patcher","failpatch"],"provided":["notch-fix"]}' > "$M/profile.json"

run() { env -i HOME="$OUT" PATH=/usr/bin:/bin DSL_NO_ALERT=1 DSL_LOG_DIR="$LOGD" DSL_MODS_DIR="$M" DSL_GA_PATH="$FW/GameAssembly.dylib" FOREIGN_OUT="$OUT/foreign.txt" "$@" "$OUT/host" 2>&1; }
EXPECT="orig_init=1 orig_safe=2 orig_normal=1 orig_tick=1 orig_update=2"

print "== T1 all modules, our loader first, a foreign loader second (with its own detour)"
r=$(run DYLD_INSERT_LIBRARIES="$FW/dsload.dylib:$FW/foreign.dylib" FOREIGN_HOOK=1)
print -r -- "$r" | sed 's/^/     /'
check "game code runs through every stub" '[[ "$r" == *"$EXPECT"* ]]'
check "floor-logger (API 1 binary, unchanged) loaded and logs" 'log | grep -q "floor-logger 1.0.0: loaded" && log | grep -q "\[floor-logger\] floor started: normal floor (game minute 25, safe flag 0)"'
check "early-mod: load before mapping, early hook by RVA before il2cpp_init, init by name" 'log | grep -q "\[early-mod\] load: ga_base=0 build=test" && log | grep -q "\[early-mod\] early: ready=0 hook_rva=0" && log | grep -q "\[early-mod\] update hook ran 2 times" && log | grep -q "\[early-mod\] tick hook ran"'
check "one engine: second hook on floor-logger's function refused, clash off" 'log | grep -q "is already hooked by floor-logger" && log | grep -q "clash: FAILED: dsl_plugin_init returned 5" && ! log | grep -q "BAD HOOK RAN"'
check "foreign loader's detour detected and not overwritten" 'log | grep -q "starts with a PC-relative instruction 58000050" && [[ "$r" == *"orig_foreign=0"* ]]'
check "dependency order: early-mod before clash, data-mod satisfies plain" 'log | grep "load order:" | grep -q "early-mod.*clash" && log | grep -q "\[plain\] plain module plain up (table x=0x"'
check "sha256 mismatch refused" 'log | grep -q "bad-sha: files do not match mod.json: not loaded"'
check "conflict refused (later one dropped)" 'log | grep -q "CONFLICT: conflicter and early-mod cannot run together; conflicter is not loaded"'
check "missing requirement refused" 'log | grep -q "needs-missing: needs nope, which is not installed / enabled"'
check "other build refused" 'log | grep -q "other-build: made for game build(s) other than test"'
check "missing per-build table refused" 'log | grep -q "no-table: no mac-arm64 table for game build test"'
check "DYLD_INSERT_LIBRARIES: ours removed, foreign kept (in-process and seen by the foreign loader)" '[[ "$r" == *"ENV DYLD_INSERT_LIBRARIES=$FW/foreign.dylib"* ]] && grep -q "saw DYLD_INSERT_LIBRARIES=$FW/foreign.dylib$" "$OUT/foreign.txt"'
check "stubs written through the RW alias" 'log | grep -q "through a RW alias"'
check "late page after init" 'log | grep -q "new stubs go to the late page"'
check "patch_rva: applied, wrong bytes refused, overlap with a hook refused" '[[ "$r" == *"PATCH target=2"* ]] && log | grep -q "\[patcher\] patches: wrong=-1 ok=0 over_hook=-1" && log | grep -q "bytes are 0000000000000000\|bytes are e0008052"'
check "failing module: its patch is undone" 'log | grep -q "failpatch: FAILED: dsl_module_early returned 9; 1 hook(s) removed"'
check "modset file: gameplay vs client scope, no session" 'grep -q "\"client\":\[{\"id\":\"patcher\"" "$LOGD/dsload_modset.json" && grep -q "\"check\":\"no session\"" "$LOGD/dsload_modset.json" && grep -q "\"id\":\"early-mod\",\"version\":\"1.2.0\",\"files\":{\"early-mod.dylib\":\"[0-9a-f]\{64\}\"" "$LOGD/dsload_modset.json"'
check "world check off without a session" '[[ "$r" == *"WORLD save1_state=0 save2_state=0 cancels=0"* ]] && log | grep -q "world check: off"'
check "build status for the launcher: active on the pinned build" 'grep -q "\"active\":true,\"reason\":\"ok\"" "$LOGD/dsload_status.json"'
check "codepatch guard cleared" '[[ ! -e "$LOGD/dsload_state/_codepatch.pending" ]]'

print "== T1b mods per world: session file from the launcher"
EH=$(shasum -a 256 "$M/early-mod/early-mod.dylib" | cut -c1-64)
print -r -- "{\"world\":null,\"modset\":\"aaa\",\"rule\":\"mods\",\"worlds\":{\"11111111-1111-1111-1111-111111111111\":\"aaa\",\"22222222-2222-2222-2222-222222222222\":\"bbb\"},\"modset_lines\":[\"early-mod@1.2.0:$EH\",\"floor-logger@1.0.0:00\",\"ghost@1.0.0:\"]}" > "$OUT/session.json"
r=$(run DYLD_INSERT_LIBRARIES="$FW/dsload.dylib" DSML_SESSION="$OUT/session.json" DSML_MODSET=aaa)
check "same mod set loads, other mod set refused with the launcher message" '[[ "$r" == *"WORLD save1_state=0 save2_state=-2 cancels=1 text=This world uses other mods. Start it from the launcher."* ]] && log | grep -q "world check: REFUSED save2 (world 22222222-2222-2222-2222-222222222222): the world.s mod set bbb, this copy aaa"'
check "modset compared with the launcher lines (floor-logger sha differs, early-mod matches)" 'grep -q "\"check\":\"differs\"" "$LOGD/dsload_modset.json" && log | grep -q "mod set: floor-logger@1.0.0 file sha256" && ! log | grep -q "mod set: early-mod"'
check "game still runs" '[[ "$r" == *"$EXPECT"* ]]'

print "== T2 foreign loader FIRST in the list"
r=$(run DYLD_INSERT_LIBRARIES="$FW/foreign.dylib:$FW/dsload.dylib")
check "game runs, modules load, foreign entry kept" '[[ "$r" == *"$EXPECT"* ]] && log | grep -q "floor-logger 1.0.0: loaded" && [[ "$r" == *"ENV DYLD_INSERT_LIBRARIES=$FW/foreign.dylib"* ]]'

print "== T3 only our loader"
r=$(run DYLD_INSERT_LIBRARIES="$FW/dsload.dylib")
check "DYLD_INSERT_LIBRARIES unset for children" '[[ "$r" == *"ENV DYLD_INSERT_LIBRARIES=(unset)"* ]]'

print "== T4 crash guard (a module that dies in dsl_plugin_init)"
mod crasher 4 '{"id":"crasher","version":"1.0.0","kind":"code","api":2,"game_builds":["test"]}'
cat > "$OUT/crash.c" <<'C'
#include "dsl_sdk.h"
#include <stdlib.h>
DSL_PLUGIN();
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) { if (!getenv("CRASHER_FIXED")) abort(); return 0; }
C
$CC -O0 -I"$L/sdk" -dynamiclib -o "$M/crasher/crasher.dylib" "$OUT/crash.c" && codesign -f -s - "$M/crasher/crasher.dylib" 2>/dev/null
print '{"enabled":["floor-logger","crasher"],"provided":["notch-fix"]}' > "$M/profile.json"
run DYLD_INSERT_LIBRARIES="$FW/dsload.dylib" >/dev/null; rc=$?
check "crash leaves the pending marker" '[[ $rc -ne 0 && -f "$LOGD/dsload_state/crasher.pending" ]]'
r=$(run DYLD_INSERT_LIBRARIES="$FW/dsload.dylib")
check "next start: crasher OFF, floor-logger loads, game runs" '[[ "$r" == *"orig_safe=2"* ]] && log | grep -q "CRASH GUARD: the game stopped last time while module crasher ran (dsl_plugin_init)" && log | grep -q "floor-logger 1.0.0: loaded"'

print "== T6 future build guard: the launcher pinned another build than this copy's"
r=$(run DYLD_INSERT_LIBRARIES="$FW/dsload.dylib" DSML_BUILD=99999999)
check "env pin differs: no module, no hook, game runs, clear reason" '[[ "$r" == *"orig_safe=2"* && "$r" == *"dsload: This copy of the game is build test, but the launcher pinned build 99999999"* ]] && log | grep -q "GAME BUILD CHECK FAILED (not_pinned_build)" && ! log | grep -q "hooked" && ! log | grep -q "loaded (" && grep -q "\"active\":false,\"reason\":\"not_pinned_build\"" "$LOGD/dsload_status.json"'
print -r -- '{"build":"88888888","modset":"aaa","worlds":{}}' > "$OUT/session_pin.json"
r=$(run DYLD_INSERT_LIBRARIES="$FW/dsload.dylib" DSML_SESSION="$OUT/session_pin.json")
check "session pin differs: same refusal (world check not installed either)" '[[ "$r" == *"orig_safe=2"* && "$r" == *"WORLD save1_state=0 save2_state=0 cancels=0"* ]] && log | grep -q "pinned build 88888888" && ! log | grep -q "hooked"'
r=$(run DYLD_INSERT_LIBRARIES="$FW/dsload.dylib" DSML_BUILD=test)
check "pin equal to the copy: modules load" 'log | grep -q "floor-logger 1.0.0: loaded" && grep -q "\"pinned_build\":\"test\"" "$LOGD/dsload_status.json"'

print "== T5 unknown build: loader inactive"
print "" > "$OUT/gen/test_builds.inc"
$CC -O2 -I"$OUT/gen" -I"$L/src" -dynamiclib -framework CoreFoundation -framework CoreGraphics -lobjc -o "$FW/dsload.dylib" "$L"/src/{dsl_main,dsl_engine,dsl_il2cpp,dsl_json,dsl_world,dsl_soft}.c "$L"/start/mac/dsl_start_mac.c "$L"/start/mac/dsl_soft_mac.c && codesign -f -s - "$FW/dsload.dylib" 2>/dev/null
r=$(run DYLD_INSERT_LIBRARIES="$FW/dsload.dylib")
check "nothing hooked, game runs, says why" '[[ "$r" == *"orig_safe=2"* && "$r" == *"dsload: The game was updated"* ]] && log | grep -q "unknown GameAssembly build" && ! log | grep -q hooked && grep -q "\"active\":false,\"reason\":\"unknown_build\"" "$LOGD/dsload_status.json"'

print "== $pass passed, $fails failed (output $OUT)"
exit $fails
