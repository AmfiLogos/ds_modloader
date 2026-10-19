#!/bin/zsh
# Build the loader and the modules. Output:
#   build/dsload.dylib                    the loader (injected first with DYLD_INSERT_LIBRARIES)
#   build/mods/multiplayer/               multiplayer module (pinned ds_multiplayer commit, mp_pin.txt; minus rig_main.c + rig_hook.c)
#   build/mods/gcd-cdr/                   GCD-uses-cooldown-reduction module
#   build/mods/floor-logger/              the owner's API 1 example plugin, binary copied unchanged from ~/DungeonSettlersMods/mods
# Nothing outside this folder is written. Never overwrites a mapped dylib (mv of a new file).
set -e; cd "${0:A:h}"; L="$PWD"
mkdir -p build/mods
CF=(-O2 -Wall -Wno-unused-function -mmacosx-version-min=12.0 -DDSL_RELEASE=\"$(head -1 ../RELEASE)\")   # release number players see
put() { codesign -f -s - "$1.tmp" 2>/dev/null; mv -f "$1.tmp" "$1"; }

UISRC=(ui/*.c(N))                                   # Mods window (lane L2), when present
WEAKU=(); for w in dsl_win_init dsl_win_open dsl_win_close dsl_win_is_open dsl_win_frame dsl_win_open_world dsl_win_relang; do WEAKU+=(-Wl,-U,_$w); done   # weak, defined by loader/ui
nice -n 19 clang -arch arm64 -arch x86_64 $CF ${=EXTRA_CFLAGS} -Isrc -dynamiclib -fobjc-arc -framework AppKit -framework CoreFoundation -framework CoreGraphics -lobjc $WEAKU -install_name @rpath/dsload.dylib \
  -o build/dsload.dylib.tmp src/dsl_main.c src/dsl_engine.c src/dsl_il2cpp.c src/dsl_json.c src/dsl_world.c src/dsl_saves.c src/dsl_cloud.c src/dsl_soft.c src/dsl_ui.c src/dsl_reg.c src/dsl_rows.c src/dsl_update.c src/dsl_sig.c src/dsl_sig_mc.c src/dsl_hostmods.c src/dsl_fastboot.c src/dsl_settings.c src/dsl_moddata.c src/dsl_crash.c src/dsl_scrub.c src/dsl_scrub_names.c $UISRC start/mac/dsl_start_mac.c start/mac/dsl_update_mac.c start/mac/dsl_soft_mac.c start/mac/dsl_quiet_mac.c start/mac/dsl_bie_mac.c start/mac/dsl_notch_mac.m
put build/dsload.dylib; echo "built build/dsload.dylib"
[[ -n "$LOADER_ONLY" ]] && exit 0

# multiplayer: built from ONE pinned ds_multiplayer commit (mp_pin.txt, or MP_PIN=<commit/tag>), never a hand copy.
# The tree is exported read-only with `git archive` into build/pin/<sha>/; the source list is read from that tree's own
# src/build.sh (its dsrig.dylib list minus rig_main.c + rig_hook.c, which modules/multiplayer/mp_module.c replaces).
# det_ranges.tsv is game-derived (not in git): MP_DET_RANGES, else ~/ds_multiplayer/build/det_ranges.tsv.
MP_REPO=${MP_REPO:-$HOME/ds_multiplayer}; MP_PIN=${MP_PIN:-$(head -1 mp_pin.txt)}
SHA=$(git -C $MP_REPO rev-parse --verify "$MP_PIN^{commit}") || { echo "mp pin $MP_PIN not found in $MP_REPO" >&2; exit 1; }
V=build/pin/$SHA; M=build/mods/multiplayer; mkdir -p $M build/pin
if [[ ! -f $V/src/build.sh ]]; then
  ex=(src win/math); for d in custom sdk rva tools/rv_gate.py win/port; do git -C $MP_REPO cat-file -e $SHA:$d 2>/dev/null && ex+=($d); done   # Custom Modes (M4 split), address tables + their gate when the tree has them
  mkdir -p $V.tmp; git -C $MP_REPO archive $SHA $ex | tar -x -C $V.tmp; mv $V.tmp $V
fi
if [[ ! -f $V/win/math/build/libdsmath_arm64.a ]]; then        # arm64 part of win/math/build_math.sh
  (cd $V/win/math && mkdir -p build/obj_arm64 && for f in musl/*.c; do nice -n 19 clang -arch arm64 -mmacosx-version-min=12.0 -O2 -std=gnu11 \
     -D_USE_MATH_DEFINES -ffp-contract=off -fno-fast-math -U__FP_FAST_FMA -U__FP_FAST_FMAF -Ishim -Imusl -include dsm_rename.h -w -c $f -o build/obj_arm64/${f:t:r}.o; done \
   && ar rcs build/libdsmath_arm64.a build/obj_arm64/*.o)
fi
mpsrc=(${(f)"$(python3 - $V/src/build.sh <<'PY'
import re, sys
t = open(sys.argv[1]).read(); t = t[t.index('clang'):t.index('codesign')].replace('\\\n', ' ')
for w in t.split():
    if w.endswith('.c') and w not in ('rig_main.c', 'rig_hook.c') and not w.startswith('-'):
        print(w)
PY
)"})
(( ${#mpsrc} > 10 )) || { echo "cannot read the source list of $V/src/build.sh" >&2; exit 1; }
srcs=(); for f in $mpsrc; do srcs+=($V/src/$f); done
nice -n 19 clang -arch arm64 $CF -D_DARWIN_C_SOURCE -dynamiclib -framework AppKit -framework CoreGraphics -framework CoreFoundation \
  -install_name @rpath/multiplayer.dylib -Isdk -I$V/src -I$V/win/math -DDSMATH_DEFAULT_ON=1 \
  -o $M/multiplayer.dylib.tmp modules/multiplayer/mp_module.c $srcs $V/win/math/build/libdsmath_arm64.a -lz
put $M/multiplayer.dylib
DR=${MP_DET_RANGES:-$MP_REPO/build/det_ranges.tsv}
cp $DR $M/det_ranges.tsv.tmp; mv -f $M/det_ranges.tsv.tmp $M/det_ranges.tsv
cp modules/multiplayer/mod.json $M/mod.json
if [[ -d $V/rva ]]; then mkdir -p $M/rva; cp $V/rva/*.tsv $M/rva/; fi       # address tables of other game versions (src/rig_rv.c)
print -r -- "$SHA" > $M/PINNED_COMMIT
zsh modules/copy_lang.sh $MP_REPO $SHA $M                          # its translations (repo lang/*.json) when the pin has them
echo "built $M from ds_multiplayer $SHA (${#mpsrc} files)"
# custom-modes: its own repo (github.com/AmfiLogos/ds_custommodes), ONE pinned commit (cm_pin.txt, or CM_PIN=<commit/tag>),
# exported read-only into build/cmpin/<sha>/; headers from the multiplayer pin above (MP_SRC). CM_REPO = a local clone.
CM_REPO=${CM_REPO:-$HOME/ds_custommodes}; CM_PIN=${CM_PIN:-$(head -1 cm_pin.txt)}
CSHA=$(git -C $CM_REPO rev-parse --verify "$CM_PIN^{commit}") || { echo "cm pin $CM_PIN not found in $CM_REPO" >&2; exit 1; }
CV=build/cmpin/$CSHA
if [[ ! -f $CV/build_module.sh ]]; then mkdir -p $CV.tmp; git -C $CM_REPO archive $CSHA | tar -x -C $CV.tmp; mv $CV.tmp $CV; fi
# every game address of multiplayer + Custom Modes' mp part through rv() (the pin's tools/rv_gate.py, when it has one)
if [[ -f $V/tools/rv_gate.py ]]; then python3 $V/tools/rv_gate.py --cm $CV modules/multiplayer/mp_module.c > /dev/null || { python3 $V/tools/rv_gate.py --cm $CV modules/multiplayer/mp_module.c >&2; exit 1; }; fi
OUT=$PWD/build/mods/custom-modes SDK=$PWD/sdk MP_SRC=$PWD/$V/src zsh $CV/build_module.sh
zsh modules/copy_lang.sh $CV - build/mods/custom-modes              # its translations (repo lang/*.json) when the pin has them
echo "built build/mods/custom-modes from ds_custommodes $CSHA"

# gcd-cdr
G=build/mods/gcd-cdr; mkdir -p $G/rva
nice -n 19 clang -arch arm64 $CF -Isdk -dynamiclib -install_name @rpath/gcd-cdr.dylib -o $G/gcd-cdr.dylib.tmp modules/gcd-cdr/gcd_cdr.c
put $G/gcd-cdr.dylib
cp modules/gcd-cdr/mod.json $G/; cp modules/gcd-cdr/rva/*.tsv $G/rva/
echo "built $G"

# zetu-suite (ZET-U Mod Suite): one source, every hook by il2cpp name (no per-build table)
Z=build/mods/zetu-suite; mkdir -p $Z
nice -n 19 clang -arch arm64 $CF -Isdk -dynamiclib -install_name @rpath/zetu-suite.dylib -o $Z/zetu-suite.dylib.tmp modules/zetu-suite/zetu.c
put $Z/zetu-suite.dylib
cp modules/zetu-suite/mod.json $Z/
echo "built $Z"

# hell-difficulty: code half + table half (hell_data.c rebuilds the Hell table bundle from the player's stock one and the
# delta rva/<build>_mac_tables.dsdelta; without the delta the module turns Hell off). The delta is made from the stock
# Mac table bundle (game file, read only; HELL_STOCK_MAC) by modules/hell-difficulty/tools/hell_delta.py, as build_mods.sh does.
E=build/mods/hell-difficulty; mkdir -p $E/rva
nice -n 19 clang -arch arm64 $CF -Isdk -dynamiclib -install_name @rpath/hell-difficulty.dylib -o $E/hell-difficulty.dylib.tmp modules/hell-difficulty/hell.c
put $E/hell-difficulty.dylib
cp modules/hell-difficulty/mod.json $E/; cp modules/hell-difficulty/rva/*.tsv $E/rva/
HM=${HELL_STOCK_MAC:-$HOME/DungeonSettlers_HellDifficulty/b25846389/stock/table_assets_all_ee685f7db1494c1e6bf42e948951b8dc.bundle}
if [[ -f $HM ]] && nice -n 19 /usr/local/bin/python3 modules/hell-difficulty/tools/hell_delta.py "$HM" $E/rva/25846389_mac_tables.dsdelta | tail -1; then :
else echo "WARNING: no Hell table delta (stock Mac table bundle $HM missing): Hell stays off with this build" >&2; fi
echo "built $E"

# floor-logger (API 1, unchanged binary)
F=build/mods/floor-logger; mkdir -p $F
cp ~/DungeonSettlersMods/mods/floor-logger/floor-logger.dylib $F/floor-logger.dylib.tmp; mv -f $F/floor-logger.dylib.tmp $F/floor-logger.dylib
cp ~/DungeonSettlersMods/mods/floor-logger/mod.json $F/
echo "copied $F"
