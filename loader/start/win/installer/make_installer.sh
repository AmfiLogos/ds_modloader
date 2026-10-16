#!/bin/zsh
# make_installer.sh [kit dir] -> loader/build/win/Dungeon-Settlers-Modded-Setup.exe (NSIS 3, built on this Mac).
# The kit = build_win.sh's loader/build/win/kit (install.bat, uninstall.bat, README.txt, files/ with the mods).
# Env: MAKENSIS (default ~/tools/nsis/bin/makensis, native NSIS 3.11), VER (default 13.2.0: the installer version).
set -e; cd "${0:A:h}"; I=$PWD; L=${I:h:h:h}
KIT=${1:-$L/build/win/kit}; KIT=${KIT:A}; O=$L/build/win; OUT=$O/Dungeon-Settlers-Modded-Setup.exe
MK=${MAKENSIS:-~/tools/nsis/bin/makensis}; VER=${VER:-13.2.0}
[[ -x $MK ]] || { echo "makensis not found: $MK" >&2; exit 1; }
for f in install.bat files/find_game.bat files/baselib.dll "files/Dungeon Settlers Modded.exe" files/BUILD.txt files/GASIZE.txt; do
  [[ -f $KIT/$f ]] || { echo "kit $KIT: no $f (run build_win.sh first)" >&2; exit 1; }
done
[[ -n $(ls $KIT/files/mods 2>/dev/null) ]] || { echo "kit $KIT: no mods in files/mods (build_win.sh with MODS=...)" >&2; exit 1; }
mkdir -p $O/old; [[ -f $OUT ]] && mv $OUT $O/old/$(date +%Y%m%d-%H%M%S)-${OUT:t}
nice -n 19 $MK -V2 -DKIT=$KIT -DOUT=$OUT.tmp -DVER=$VER -DICON=${I:h}/icon/dsmodded.ico $I/dsmodded.nsi
mv -f $OUT.tmp $OUT
echo "installer $OUT ($(du -h $OUT | cut -f1)), kit mods: $(ls $KIT/files/mods | tr '\n' ' ')"
