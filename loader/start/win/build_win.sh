#!/bin/zsh
# Windows start part of the mod loader, cross-built on this Mac with ~/tools/llvm-mingw. Output (loader/build/win/):
#   baselib.dll                      the loader: proxy of the game's baselib.dll + loader core (src/) + MinHook engine
#   Dungeon Settlers Modded.exe      the starter (Steam, first-start save copy, environment, game start)
#   kit/                             the friend kit: install.bat, uninstall.bat, README.txt, files/ (+ mods/ when given)
# Mods (built in their own repos) are put into kit/files/mods/<id>/ by MODS="<dir> <dir> ..." (each a module folder
# with mod.json and its .dll), e.g. MODS=~/ds_multiplayer/win/build/mods/multiplayer.
# DSMP_ROOT: a ds_multiplayer checkout (MinHook in win/third_party, the game's baselib.dll in win/25846389).
set -e; cd "${0:A:h}"; W="$PWD"; L="${W:h:h}"
B=~/tools/llvm-mingw/bin; TC=$B/x86_64-w64-mingw32-clang
D=${DSMP_ROOT:-~/ds_multiplayer}; MH=$D/win/third_party/minhook
GAME=${DSL_WIN_GAME:-~/ds_multiplayer/win/25846389}             # read-only copies of the Windows game files
O=$L/build/win; OBJ=$O/obj; mkdir -p $OBJ $O/old
olds=($OBJ/*.o(N)); (( ${#olds} )) && mv -f $olds $O/old/

python3 gen_def.py $GAME/baselib.dll $O/baselib_exports.def
CF=(-O2 -g -Wall -Wno-unused-function -D__USE_MINGW_ANSI_STDIO=1 -I$L/src -I$L/sdk -I$MH/include)
for f in $L/src/{dsl_main,dsl_il2cpp,dsl_json,dsl_world}.c; do nice -n 19 $TC $CF -include compat/dsl_compat_win.h -c $f -o $OBJ/${f:t:r}.o; done
for f in dsl_start_win.c dsl_engine_win.c dsl_os_win.c; do nice -n 19 $TC $CF -c $f -o $OBJ/${f:t:r}.o; done
for f in $MH/src/hook.c $MH/src/buffer.c $MH/src/trampoline.c $MH/src/hde/hde64.c; do nice -n 19 $TC -O2 -w -I$MH/include -c $f -o $OBJ/mh_${f:t:r}.o; done
nice -n 19 $TC -shared -o $O/baselib.dll.tmp $OBJ/*.o $O/baselib_exports.def -static -static-libgcc -lbcrypt -Wl,-Bstatic -lpthread
mv -f $O/baselib.dll.tmp $O/baselib.dll
echo "built $O/baselib.dll"

$B/x86_64-w64-mingw32-windres ds_modded_win.rc -O coff -o $OBJ/ds_modded_res.res.o
nice -n 19 $TC -O2 -Wall -municode -mwindows ds_modded_win.c $OBJ/ds_modded_res.res.o -o "$O/Dungeon Settlers Modded.exe.tmp" -lshell32 -lole32 -ladvapi32 -luuid
mv -f "$O/Dungeon Settlers Modded.exe.tmp" "$O/Dungeon Settlers Modded.exe"
echo "built $O/Dungeon Settlers Modded.exe"

# kit (bat / txt files get Windows line ends)
K=$O/kit; [[ -d $K ]] && mv $K $O/old/kit-$(date +%Y%m%d-%H%M%S)
mkdir -p $K/files/mods
cp kit/install.bat kit/uninstall.bat kit/README.txt $K/
cp kit/find_game.bat kit/copy_exclude.txt $K/files/
cp $O/baselib.dll "$O/Dungeon Settlers Modded.exe" $K/files/
print 25846389 > $K/files/BUILD.txt
stat -f %z $GAME/GameAssembly.dll > $K/files/GASIZE.txt
for m in ${=MODS}; do cp -R $m $K/files/mods/; done
for f in $K/*.bat $K/*.txt $K/files/*.bat $K/files/*.txt; do perl -pi -e 's/\r?\n/\r\n/' "$f"; done
echo "kit $K: $(ls $K/files/mods | tr '\n' ' ')"
