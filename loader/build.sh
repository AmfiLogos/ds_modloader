#!/bin/zsh
# Build the loader and the modules. Output:
#   build/dsload.dylib                    the loader (injected first with DYLD_INSERT_LIBRARIES)
#   build/mods/multiplayer/               multiplayer module (vendor/mp snapshot of ~/ds_multiplayer/src, minus rig_main.c + rig_hook.c)
#   build/mods/gcd-cdr/                   GCD-uses-cooldown-reduction module
#   build/mods/floor-logger/              the owner's API 1 example plugin, binary copied unchanged from ~/DungeonSettlersMods/mods
# Nothing outside this folder is written. Never overwrites a mapped dylib (mv of a new file).
set -e; cd "${0:A:h}"; L="$PWD"
mkdir -p build/mods
CF=(-O2 -Wall -Wno-unused-function -mmacosx-version-min=12.0)
put() { codesign -f -s - "$1.tmp" 2>/dev/null; mv -f "$1.tmp" "$1"; }

UISRC=(ui/*.c(N))                                   # Mods window (lane L2), when present
nice -n 19 clang -arch arm64 -arch x86_64 $CF ${=EXTRA_CFLAGS} -Isrc -dynamiclib -framework CoreFoundation -framework CoreGraphics -lobjc -install_name @rpath/dsload.dylib \
  -o build/dsload.dylib.tmp src/dsl_main.c src/dsl_engine.c src/dsl_il2cpp.c src/dsl_json.c src/dsl_world.c src/dsl_soft.c src/dsl_ui.c $UISRC
put build/dsload.dylib; echo "built build/dsload.dylib"
[[ -n "$LOADER_ONLY" ]] && exit 0

# multiplayer
V=vendor/mp; M=build/mods/multiplayer; mkdir -p $M
srcs=($V/src/{rig_sock,rig_cmd,rig_win,rig_det}.c $V/src/{mp_core,mp_config,mp_game,mp_fog}.c
      $V/src/net/dsnet_link.c $V/src/net/dsnet_server.c $V/src/net/dsnet_client.c $V/src/net/steam/dsnet_steam.c
      $V/src/{ord_core,ord_game,ord_mp}.c $V/src/ord_test/ord_smoke.c
      $V/src/{ui_game,ui_model,ui_tip,ui_teams,ui_teams_model,mpmenu,mpmenu_model}.c $V/src/{trn_logic,trn_game,trn_mp}.c
      $V/win/math/math_mac.c)
nice -n 19 clang -arch arm64 $CF -D_DARWIN_C_SOURCE -dynamiclib -framework AppKit -framework CoreGraphics -framework CoreFoundation \
  -install_name @rpath/multiplayer.dylib -Isdk -I$V/src -I$V/win/math -DDSMATH_DEFAULT_ON=1 \
  -o $M/multiplayer.dylib.tmp modules/multiplayer/mp_module.c $srcs $V/win/math/build/libdsmath_arm64.a
put $M/multiplayer.dylib
cp -c $V/det_ranges.tsv $M/det_ranges.tsv.tmp 2>/dev/null || cp $V/det_ranges.tsv $M/det_ranges.tsv.tmp; mv -f $M/det_ranges.tsv.tmp $M/det_ranges.tsv
cp modules/multiplayer/mod.json $M/mod.json
echo "built $M"

# gcd-cdr
G=build/mods/gcd-cdr; mkdir -p $G/rva
nice -n 19 clang -arch arm64 $CF -Isdk -dynamiclib -install_name @rpath/gcd-cdr.dylib -o $G/gcd-cdr.dylib.tmp modules/gcd-cdr/gcd_cdr.c
put $G/gcd-cdr.dylib
cp modules/gcd-cdr/mod.json $G/; cp modules/gcd-cdr/rva/*.tsv $G/rva/
echo "built $G"

# hell-difficulty (code half; the table half is applied to the copy, see modules/hell-difficulty/hell.c)
E=build/mods/hell-difficulty; mkdir -p $E/rva
nice -n 19 clang -arch arm64 $CF -Isdk -dynamiclib -install_name @rpath/hell-difficulty.dylib -o $E/hell-difficulty.dylib.tmp modules/hell-difficulty/hell.c
put $E/hell-difficulty.dylib
cp modules/hell-difficulty/mod.json $E/; cp modules/hell-difficulty/rva/*.tsv $E/rva/
echo "built $E"

# floor-logger (API 1, unchanged binary)
F=build/mods/floor-logger; mkdir -p $F
cp ~/DungeonSettlersMods/mods/floor-logger/floor-logger.dylib $F/floor-logger.dylib.tmp; mv -f $F/floor-logger.dylib.tmp $F/floor-logger.dylib
cp ~/DungeonSettlersMods/mods/floor-logger/mod.json $F/
echo "copied $F"
