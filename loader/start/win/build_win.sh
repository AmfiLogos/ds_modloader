#!/bin/zsh
# Windows start part of the mod loader, cross-built on this Mac with ~/tools/llvm-mingw. Output (loader/build/win/):
#   baselib.dll                      the loader: proxy of the game's baselib.dll + loader core (src/) + MinHook engine
#   Dungeon Settlers Modded.exe      the starter (update install, Steam, first-start save copy, environment, game start)
#   kit/                             the friend kit: install.bat, uninstall.bat, README.txt, files/ (+ mods/ when given)
# Mods (built in their own repos) are put into kit/files/mods/<id>/ by MODS="<dir> <dir> ..." (each a module folder
# with mod.json and its .dll), e.g. MODS=~/ds_multiplayer/win/build/mods/multiplayer.
# DSMP_ROOT: a ds_multiplayer checkout (MinHook in win/third_party, the game's baselib.dll in win/25846389).
# DSL_TEST_NOSTEAM=1: a TEST build into loader/build/win_test (never shipped): the starter's DSMODDED_NO_STEAM=1 and the
# proxy's DSL_STEAM=0 turn the game's Steam check off (Wine, the VM's no-Steam runs). A release build (loader/build/win)
# fails when any binary of its kit has such a path (steam_gate.py; owner 2026-10-18: "require Steam, like Mac").
set -e; cd "${0:A:h}"; W="$PWD"; L="${W:h:h}"
B=~/tools/llvm-mingw/bin; TC=$B/x86_64-w64-mingw32-clang
D=${DSMP_ROOT:-~/ds_multiplayer}; MH=$D/win/third_party/minhook
GAME=${DSL_WIN_GAME:-~/ds_multiplayer/win/25846389}             # read-only copies of the Windows game files
O=$L/build/win; NS=(); [[ ${DSL_TEST_NOSTEAM:-} == 1 ]] && { O=$L/build/win_test; NS=(-DDSL_TEST_NOSTEAM); echo "TEST build (DSL_TEST_NOSTEAM): $O, never ship it"; }
OBJ=$O/obj; mkdir -p $OBJ $O/old
olds=($OBJ/*.o(N)); (( ${#olds} )) && mv -f $olds $O/old/

python3 gen_def.py $GAME/baselib.dll $O/baselib_exports.def
PM=(-ffile-prefix-map=$HOME=/src)                               # no build path (user name) in what ships (gate G12)
PM+=(-ffile-prefix-map=$L=/loader -ffile-prefix-map=${D:A}=/mp)  # any build folder: the same debug info, so the same .buildid hash
# links: -Wl,--no-insert-timestamp = no build time in the PE headers (the same bytes from every build)
CF=($PM $NS -O2 -g -Wall -Wno-unused-function -D__USE_MINGW_ANSI_STDIO=1 -DDSL_RELEASE=\"$(head -1 $L/../RELEASE)\" -I$L/src -I$L/sdk -I$MH/include)
# the kit entry of this release from CHANGELOG.md, built into the loader (the What's new window, src/dsl_whatsnew.c)
mkdir -p $O/gen && python3 $L/../tools/changelog/changelog.py cheader --file $L/../CHANGELOG.md --version "$(head -1 $L/../RELEASE)" --out $O/gen/dsl_whatsnew_data.h
CF+=(-I$O/gen)
# MelonLoader guard plugin (kind "melon" mods): its DLL is embedded into bie_win.c (starter + baselib.dll)
MLG=${ML_GUARD_DLL:-$D/ext/melon/plugin/DSMelonGuard/bin/Release/net6.0/DSMelonGuard.dll}; MLGF=()
if [[ -f $MLG ]]; then
  # its debug record names the .pdb by its full build path (user name): keep only the file name, same length (gate G12)
  python3 -c 'import re,sys; d=open(sys.argv[1],"rb").read(); d=re.sub(rb"[^\x00]*[/\\\\]([^/\\\\\x00]+\.pdb)\x00", lambda m: m.group(1)+b"\x00"*(len(m.group(0))-len(m.group(1))), d); open(sys.argv[2],"wb").write(d)' $MLG $OBJ/ml_guard.dll
  xxd -i -n ml_guard_dll $OBJ/ml_guard.dll > $OBJ/ml_guard.h; MLGF=(-DDSL_ML_GUARD -I$OBJ); else echo "WARNING: no $MLG: MelonLoader guard plugin not embedded"; fi
# the core = every src/*.c except the arm64 engine (dsl_engine_win.c replaces it) + the Mods window (loader/ui, lane L2)
CORE=($L/src/*.c(N) $L/ui/*.c(N)); CORE=(${CORE:#*/dsl_engine.c})
for f in $CORE; do nice -n 19 $TC $CF -include compat/dsl_compat_win.h -c $f -o $OBJ/${f:t:r}.o; done
for f in dsl_start_win.c dsl_engine_win.c dsl_os_win.c dsl_soft_win.c bie_win.c dsl_bie_win.c dsl_ml_win.c compat/dsl_u8_win.c dsl_update_win.c; do nice -n 19 $TC $CF $MLGF -Icompat -I$D/win/third_party/zlib -c $f -o $OBJ/${f:t:r}.o; done
for f in $D/win/third_party/zlib/{adler32,crc32,inffast,inflate,inftrees,zutil}.c; do nice -n 19 $TC $PM -O2 -w -c $f -o $OBJ/zl_${f:t:r}.o; done
for f in $MH/src/hook.c $MH/src/buffer.c $MH/src/trampoline.c $MH/src/hde/hde64.c; do nice -n 19 $TC $PM -O2 -w -I$MH/include -c $f -o $OBJ/mh_${f:t:r}.o; done
nice -n 19 $TC -shared -Wl,--no-insert-timestamp -o $O/baselib.dll.tmp $OBJ/*.o $O/baselib_exports.def -static -static-libgcc -lbcrypt -lurlmon -Wl,-Bstatic -lpthread
mv -f $O/baselib.dll.tmp $O/baselib.dll
echo "built $O/baselib.dll"

# the kit's files\lang, staged once: the loader's texts (lang/<code>.json) + install.bat / uninstall.bat texts in the game's
# languages (setup_<code>.bat, Windows line ends). The kit gets this folder; the starter carries the same bytes and puts back
# what ROOT\game\lang lacks at its start (ds_modded_win.c lang_heal: auto-updates up to 0.1.28 never copied them)
LS=$O/gen/lang; [[ -d $LS ]] && mv $LS $O/old/lang-$(date +%Y%m%d-%H%M%S); mkdir -p $LS
LJ=($L/lang/*.json(N)); (( ${#LJ} )) && cp $LJ $LS/
python3 setup_lang/gen_setup_lang.py bat $LS
for f in $LS/*.bat(N); do perl -pi -e 's/\r?\n/\r\n/' "$f"; done
python3 gen_lang_embed.py $LS $O/gen/dsl_lang_embed.h
$B/x86_64-w64-mingw32-windres ds_modded_win.rc -O coff -o $OBJ/ds_modded_res.res.o
ZL=$D/win/third_party/zlib; ZO=(); mkdir -p $OBJ/zl
for f in $ZL/{adler32,crc32,inffast,inflate,inftrees,zutil}.c; do nice -n 19 $TC $PM -O2 -w -c $f -o $OBJ/zl/${f:t:r}.o; ZO+=($OBJ/zl/${f:t:r}.o); done
nice -n 19 $TC $PM $NS -Wl,--no-insert-timestamp -O2 -Wall -municode -mwindows -I$L/src -Icompat -I$ZL -I$O/gen $MLGF ds_modded_win.c bie_win.c compat/dsl_u8_win.c ../dsl_update_start.c dsl_update_win.c $L/src/dsl_json.c $ZO $OBJ/ds_modded_res.res.o -o "$O/Dungeon Settlers Modded.exe.tmp" -lshell32 -lole32 -ladvapi32 -luuid -lurlmon -lbcrypt
mv -f "$O/Dungeon Settlers Modded.exe.tmp" "$O/Dungeon Settlers Modded.exe"
echo "built $O/Dungeon Settlers Modded.exe"

# kit (bat / txt files get Windows line ends)
K=$O/kit; [[ -d $K ]] && mv $K $O/old/kit-$(date +%Y%m%d-%H%M%S)
mkdir -p $K/files/mods
cp kit/install.bat kit/uninstall.bat kit/README.txt $K/
cp kit/find_game.bat kit/copy_exclude.txt $K/files/
mkdir -p $K/licenses; cp $L/../licenses/LICENSE.txt $L/../licenses/THIRD_PARTY_NOTICES.txt $K/licenses/   # kit\licenses (in the kit zip too), copied to the install folder
cp $O/baselib.dll "$O/Dungeon Settlers Modded.exe" $K/files/
# the texts staged above (files\lang): install.bat puts them next to the loader, ROOT\game\lang (the starter reads them too)
mkdir -p $K/files/lang; cp $LS/* $K/files/lang/
print 25846389 > $K/files/BUILD.txt      # first line only: install.bat and the 0.1.13 starters read it
print 25846389 > $K/files/BUILDS.txt     # every game build this kit runs on, one a line (the update install, dsl_update_win.c)
print ${DSM_RELEASE:-$(head -1 $L/../RELEASE)} > $K/files/RELEASE     # the kit release (auto-update compares it)
stat -f %z $GAME/GameAssembly.dll > $K/files/GASIZE.txt
for m in ${=MODS}; do cp -R $m $K/files/mods/; done
~/tools/llvm-mingw/bin/llvm-strip --strip-debug $K/files/baselib.dll "$K/files/Dungeon Settlers Modded.exe"   # no build paths
for f in $K/*.bat $K/*.txt $K/files/*.bat $K/files/*.txt $K/files/RELEASE $K/files/lang/*.bat; do perl -pi -e 's/\r?\n/\r\n/' "$f"; done
for f in $LS/*; do cmp -s $f $K/files/lang/${f:t} || { echo "build_win: kit files/lang/${f:t} is not the starter's built-in copy (lang_heal would undo it)" >&2; exit 1; }; done
echo "kit $K: $(ls $K/files/mods | tr '\n' ' ')"
python3 $L/modules/scan_saves.py $K || { echo "build_win: player data inside the kit (a save or a player file in MODS?)" >&2; exit 1; }   # owner 2026-10-16: no saves in what ships
if (( ! ${#NS} )); then python3 $W/steam_gate.py $K || { echo "build_win: a release binary can turn the game's Steam check off" >&2; exit 1; }; fi

# the friend zip: one folder "Dungeon Settlers Modded" inside
Z="$O/Dungeon Settlers Modded - Windows.zip"; (( ${#NS} )) && Z="$O/Dungeon Settlers Modded - Windows TEST.zip"; [[ -f "$Z" ]] && mv "$Z" "$O/old/$(date +%Y%m%d-%H%M%S) ${Z:t}"
ST=$O/zipstage; [[ -d $ST ]] && mv $ST $O/old/zipstage-$(date +%Y%m%d-%H%M%S)
mkdir -p $ST && cp -R $K "$ST/Dungeon Settlers Modded"
(cd $ST && zip -qrX "$Z" "Dungeon Settlers Modded")
echo "zip $Z ($(du -h "$Z" | cut -f1))"
