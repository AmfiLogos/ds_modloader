#!/bin/zsh
# kitonly_test.sh: a game build the loader does not know (a game update newer than the kit; dsl_main.c dsl_core_start
# refuses it, mods off) still runs the kit check (dsl_update.c, kit only): against a local release server
# (relserver.py) it downloads the newer kit and writes ROOT/update/ready.txt, so the starter installs it at the next
# start. Host test (test/host.c + fake_ga.c, the loader built with no known build, as run_tests.sh T5): never the game.
set -u; H="${0:A:h}"; T="${H:h}"; L="${T:h}"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/dsl_kitonly.XXXXXX"); FW=$OUT/fw; R=$OUT/root; mkdir -p $FW $OUT/gen $OUT/mods $OUT/log $R/loader
fails=0; pass=0
check() { if eval "$2"; then print "PASS $1"; pass=$((pass+1)); else print "FAIL $1"; fails=$((fails+1)); fi }
CC=(nice -n 19 clang -arch arm64 -mmacosx-version-min=12.0)
$CC -O0 -dynamiclib -install_name @rpath/GameAssembly.dylib -o $FW/GameAssembly.dylib $T/fake_ga.c || exit 1
print "" > $OUT/gen/test_builds.inc                                     # no known build: the fake GameAssembly is "new"
$CC -O2 -I$OUT/gen -I$L/src -dynamiclib -framework CoreFoundation -framework CoreGraphics -install_name @rpath/dsload.dylib -o $FW/dsload.dylib \
  $L/src/{dsl_main,dsl_engine,dsl_il2cpp,dsl_json,dsl_world,dsl_saves,dsl_cloud,dsl_soft,dsl_ui,dsl_reg,dsl_rows,dsl_update,dsl_fastboot}.c $L/start/mac/dsl_start_mac.c $L/start/mac/dsl_update_mac.c $L/start/mac/dsl_soft_mac.c $L/start/mac/dsl_quiet_mac.c $L/start/mac/dsl_bie_mac.c $L/start/mac/dsl_notch_mac.m -fobjc-arc -framework AppKit -lobjc -Wl,-U,_dsl_win_init -Wl,-U,_dsl_win_open -Wl,-U,_dsl_win_close -Wl,-U,_dsl_win_is_open -Wl,-U,_dsl_win_frame -Wl,-U,_dsl_win_open_world -Wl,-U,_dsl_win_relang 2>&1 | grep -v "duplicate libraries" ; [[ -f $FW/dsload.dylib ]] || exit 1
$CC -O0 $T/host.c -o $OUT/host $FW/GameAssembly.dylib -Wl,-rpath,$FW || exit 1
for f in $FW/*.dylib $OUT/host; do codesign -f -s - $f 2>/dev/null; done

# installed kit 0.1.3; the release server has v0.1.4 (this OS's kit asset)
print 0.1.3 > $R/loader/RELEASE
REPO=$(sed -n 's/^#define KIT_REPO "\(.*\)"/\1/p' $L/src/dsl_update.c)
mkdir -p $OUT/srv/$REPO/v0.1.4; head -c 4096 /dev/urandom > $OUT/srv/$REPO/v0.1.4/Dungeon-Settlers-Modded-Mac.zip
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
python3 -I $H/relserver.py $OUT/srv $PORT >/dev/null 2>&1 & SRV=$!
trap "kill $SRV 2>/dev/null" EXIT
sleep 0.5

r=$(env -i HOME=$OUT PATH=/usr/bin:/bin DSL_NO_ALERT=1 DSL_LOG_DIR=$OUT/log DSL_MODS_DIR=$OUT/mods DSL_GA_PATH=$FW/GameAssembly.dylib \
    DSML_ROOT=$R DSL_UPDATE_API=http://127.0.0.1:$PORT HOST_WAIT_FILE=$R/update/ready.txt DYLD_INSERT_LIBRARIES=$FW/dsload.dylib $OUT/host 2>&1)
log() { cat $OUT/log/dsload.log 2>/dev/null }
check "unknown build: mods off, the game runs, the notice names the next step" '[[ "$r" == *"orig_safe=2"* && "$r" == *"checks for that update each time you start it"* ]] && log | grep -q "GAME BUILD CHECK FAILED (unknown_build)" && ! log | grep -q hooked'
check "kit check ran with mods off (kit only) and downloaded v0.1.4" 'log | grep -q "kit only (mods are off this start)" && log | grep -q "update: kit: installed 0.1.3, newest usable 0.1.4" && log | grep -q "Dungeon-Settlers-Modded-Mac.zip downloaded, sha256 ok"'
check "ready.txt for the starter: kit 0.1.4 with its file and sha256" 'grep -q "^KEY	kit 0.1.4$" $R/update/ready.txt && grep -q "^KIT	0.1.4	$R/update/dl/Dungeon-Settlers-Modded-Mac.zip	$(shasum -a 256 $OUT/srv/$REPO/v0.1.4/Dungeon-Settlers-Modded-Mac.zip | cut -c1-64)$" $R/update/ready.txt'
print "== $pass passed, $fails failed (output $OUT)"
exit $fails
