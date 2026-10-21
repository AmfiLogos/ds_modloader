#!/bin/zsh
# mac_gate.sh <work dir> <hidden instance app>: the Mac auto-update gate in one command. Builds the three test apps
# (0.1.3 installed, 0.1.4 new, 0.1.5 = 0.1.4 whose loader aborts at start), then runs mac_update_test.py on them with a
# rig lease. <hidden instance app>: a LSBackgroundOnly copy of the game (loader/test/make_instance.sh), never a window.
# Last line: "RESULT n/n pass".
# Real-release mode (after a publish): mac_gate.sh <work> <instance app> real 0.1.7 -> only a test copy of release
# 0.1.6.99 is built, and the REAL GitHub release v0.1.7 must be offered: Off = corner button, On = background download
# (sha256 ok) and the next-launch "Updating ... to 0.1.7" install, on the test copy only. REAL_APP=<app> + DSL_REAL_OLD=<its
# release>: the test copy is that real released app instead of a 0.1.6.99 build.
set -e; H="${0:A:h}"; REPO="${H:h:h:h}"; W=${1:A}; INST=${2:A}
A=$W/apps; mkdir -p $A $W/run
if [[ ${3:-} == real ]]; then
  export DSL_REAL_RELEASE=${4:?version}; export DSL_REAL_OLD=${DSL_REAL_OLD:-0.1.6.99}
  if [[ -n ${REAL_APP:-} ]]; then   # start from a real released app (REAL_APP=<.app from its .dmg>, DSL_REAL_OLD = its release)
    mkdir -p $A/$DSL_REAL_OLD; ditto "$REAL_APP" "$A/$DSL_REAL_OLD/Dungeon Settlers Modded.app"
  else
    DSM_RELEASE=$DSL_REAL_OLD zsh $REPO/app/build_app.sh > $W/build_old.log 2>&1 || { tail -3 $W/build_old.log; exit 1; }
    mkdir -p $A/$DSL_REAL_OLD; ditto "$REPO/app/build/Dungeon Settlers Modded.app" "$A/$DSL_REAL_OLD/Dungeon Settlers Modded.app"
  fi
  R=~/ds_multiplayer/rig/rig.py; P=""
  for i in {1..60}; do P=$(python3 $R lease mac_gate --pid $$ 2>/dev/null | tail -1); [[ $P == [A-J][A-J] ]] && break; sleep 10; done
  [[ $P == [A-J][A-J] ]] || { print "no rig pair free"; exit 2; }
  trap "python3 $R release $P mac_gate >/dev/null" EXIT
  python3 $H/mac_update_test.py $W/run $A $INST
  exit $?
fi
# signed updates: the test apps' loader trusts the public dev key that relserver.py signs with (-DDSL_SIGN_DEV_KEY);
# loader/build/dsload.dylib is a release build again right after (sign_release.sh also refuses a kit with the dev key)
EXTRA_CFLAGS=-DDSL_SIGN_DEV_KEY LOADER_ONLY=1 zsh $REPO/loader/build.sh > $W/build_loader_dev.log 2>&1 || { tail -3 $W/build_loader_dev.log; exit 1; }
for v in 0.1.3 0.1.4; do
  DSM_RELEASE=$v zsh $REPO/app/build_app.sh > $W/build_$v.log 2>&1 || { tail -3 $W/build_$v.log; LOADER_ONLY=1 zsh $REPO/loader/build.sh > /dev/null 2>&1; exit 1; }
  mkdir -p $A/$v; ditto "$REPO/app/build/Dungeon Settlers Modded.app" "$A/$v/Dungeon Settlers Modded.app"
  # test apps only: hidden apps (the restart test starts the stub through LaunchServices: never a Dock icon, never focus)
  plutil -replace LSBackgroundOnly -bool YES "$A/$v/Dungeon Settlers Modded.app/Contents/Info.plist"
  plutil -replace LSUIElement -bool YES "$A/$v/Dungeon Settlers Modded.app/Contents/Info.plist"
  codesign -f -s - "$A/$v/Dungeon Settlers Modded.app" 2>/dev/null
done
LOADER_ONLY=1 zsh $REPO/loader/build.sh > $W/build_loader_release.log 2>&1 || { print "loader/build/dsload.dylib is still the TEST build (dev key): rebuild it"; exit 1; }
B="$A/0.1.5/Dungeon Settlers Modded.app"; mkdir -p $A/0.1.5; ditto "$A/0.1.4/Dungeon Settlers Modded.app" "$B"
print '#include <stdlib.h>\n__attribute__((constructor)) static void die(void) { abort(); }' > $W/die.c
clang -arch arm64 -arch x86_64 -dynamiclib -o "$B/Contents/Resources/dsml/dsload.dylib" $W/die.c
print 0.1.5 > "$B/Contents/Resources/dsml/RELEASE"; print broken > "$B/Contents/Resources/dsml/VERSION"
codesign -f -s - "$B/Contents/Resources/dsml/dsload.dylib" 2>/dev/null; codesign -f -s - "$B" 2>/dev/null
R=~/ds_multiplayer/rig/rig.py; P=""
for i in {1..60}; do P=$(python3 $R lease mac_gate --pid $$ 2>/dev/null | tail -1); [[ $P == [A-J][A-J] ]] && break; sleep 10; done
[[ $P == [A-J][A-J] ]] || { print "no rig pair free"; exit 2; }
trap "python3 $R release $P mac_gate >/dev/null" EXIT
python3 $H/mac_update_test.py $W/run $A $INST ${UPD_TESTS:+$UPD_TESTS}     # UPD_TESTS=restart,off: only those
