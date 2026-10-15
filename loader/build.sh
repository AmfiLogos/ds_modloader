#!/bin/zsh
# Build the loader and the modules. Output:
#   build/dsload.dylib                    the loader (injected first with DYLD_INSERT_LIBRARIES)
#   build/mods/multiplayer/               multiplayer module (pinned ds_multiplayer commit, mp_pin.txt; minus rig_main.c + rig_hook.c)
#   build/mods/gcd-cdr/                   GCD-uses-cooldown-reduction module
#   build/mods/floor-logger/              the owner's API 1 example plugin, binary copied unchanged from ~/DungeonSettlersMods/mods
# Nothing outside this folder is written. Never overwrites a mapped dylib (mv of a new file).
set -e; cd "${0:A:h}"; L="$PWD"
mkdir -p build/mods
CF=(-O2 -Wall -Wno-unused-function -mmacosx-version-min=12.0)
put() { codesign -f -s - "$1.tmp" 2>/dev/null; mv -f "$1.tmp" "$1"; }

UISRC=(ui/*.c(N))                                   # Mods window (lane L2), when present
WEAKU=(); for w in dsl_win_init dsl_win_open dsl_win_close dsl_win_is_open dsl_win_frame dsl_win_open_world; do WEAKU+=(-Wl,-U,_$w); done   # weak, defined by loader/ui
nice -n 19 clang -arch arm64 -arch x86_64 $CF ${=EXTRA_CFLAGS} -Isrc -dynamiclib -framework CoreFoundation -framework CoreGraphics -lobjc $WEAKU -install_name @rpath/dsload.dylib \
  -o build/dsload.dylib.tmp src/dsl_main.c src/dsl_engine.c src/dsl_il2cpp.c src/dsl_json.c src/dsl_world.c src/dsl_soft.c src/dsl_ui.c src/dsl_reg.c src/dsl_rows.c $UISRC
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
  mkdir -p $V.tmp; git -C $MP_REPO archive $SHA src win/math | tar -x -C $V.tmp; mv $V.tmp $V
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
print -r -- "$SHA" > $M/PINNED_COMMIT
echo "built $M from ds_multiplayer $SHA (${#mpsrc} files)"
# custom-modes: built from the same pinned tree once it has one (MASTER_PLAN M4: ds_multiplayer/custom)
if [[ -f $V/custom/build_module.sh ]]; then OUT=$PWD/build/mods/custom-modes SDK=$PWD/sdk zsh $V/custom/build_module.sh && echo "built build/mods/custom-modes"
else echo "custom-modes: not in ds_multiplayer $SHA yet (M4 split): skipped"; fi

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
