#!/bin/zsh
# install_mods.sh <App> <profile.json> : put build/dsload.dylib in Frameworks and build/mods/* in Resources/mods
# (the launcher's play-copy layout), then re-seal the copy ad-hoc. Only for test copies in test_env/.
set -e
APP=${1:A}; PROF=${2:A}; L=${0:A:h:h}
[[ $APP == $L/test_env/* ]] || { echo "refused: $APP is not a test copy" >&2; exit 1; }
FW=$APP/Contents/Frameworks; MD=$APP/Contents/Resources/mods
cp $L/build/dsload.dylib $FW/dsload.dylib.tmp; mv -f $FW/dsload.dylib.tmp $FW/dsload.dylib
mkdir -p $MD
for d in $L/build/mods/*(/); do
  i=${d:t}; mkdir -p $MD/$i
  for f in $d/**/*(.); do r=${f#$d/}; mkdir -p $MD/$i/${r:h}; cp -c $f $MD/$i/$r.tmp 2>/dev/null || cp $f $MD/$i/$r.tmp; mv -f $MD/$i/$r.tmp $MD/$i/$r; done
done
cp $PROF $MD/profile.json
codesign -f -s - "$APP/Contents/MacOS/Dungeon Settlers" 2>&1 | tail -1
echo "installed loader + $(ls $MD | grep -vc profile.json) module folders in $APP"
