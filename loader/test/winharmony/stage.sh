#!/bin/zsh
# TEST ONLY: builds and stages what winharmony_test.py copies into the Windows VM. Output: build/stage.zip
#   kit/                       the Windows kit (install.bat + files/): KIT=<built kit folder>, else start/win/build_win.sh
#                              of this checkout with MODS=$MP_MOD (a built multiplayer module folder: mod.json + multiplayer.dll)
#   mods/samefn-bie/           SameFnWin.dll (SameFnWin/, built against the pinned BepInEx be.788 core) + mod.json kind bepinex
#   cache/<zip>                the pinned BepInEx zip (sha256 of start/win/bie_win.c), seeded into the test install's cache
# BIE_ZIP=<file>: a local copy of the pinned zip (else it is downloaded once into build/).
set -e; cd "${0:A:h}"; H=$PWD; L=${H:h:h}; W=$L/start/win
B=$H/build; mkdir -p $B/old
SHA=$(sed -n 's/^#define BIE_SHA *"\([0-9a-f]*\)".*/\1/p' $W/bie_win.c)
URL=$(sed -n 's/^#define BIE_URL *L"\([^"]*\)".*/\1/p' $W/bie_win.c)
TAG=$(sed -n 's/^#define BIE_TAG *"\([^"]*\)".*/\1/p' $W/bie_win.c)
ZN="BepInEx-$TAG-win-x64.zip"                     # the name bie_win.c gives it in <ROOT>\cache
Z=$B/$ZN
sum() { shasum -a 256 "$1" | cut -d' ' -f1 }
if [[ ! -f $Z || $(sum $Z) != $SHA ]]; then
  [[ -f $Z ]] && mv $Z $B/old/$ZN.$(date +%s)
  if [[ -n $BIE_ZIP ]]; then cp "$BIE_ZIP" $Z.tmp; else curl -fsSL -o $Z.tmp "$URL"; fi
  [[ $(sum $Z.tmp) == $SHA ]] || { echo "stage: BepInEx zip sha256 $(sum $Z.tmp), expected $SHA" >&2; exit 1; }
  mv $Z.tmp $Z
fi
BD=$B/bie_$TAG; [[ -f $BD/BepInEx/core/BepInEx.Unity.IL2CPP.dll ]] || { mkdir -p $BD; unzip -q -o $Z 'BepInEx/core/*' -d $BD; }

# the plugin
nice -n 19 ~/.dotnet/dotnet build SameFnWin/SameFnWin.csproj -c Release -p:BieDir=$BD/BepInEx -o $B/plugin -nologo -v q
DLL=$B/plugin/SameFnWin.dll

# the kit
if [[ -z $KIT ]]; then
  [[ -f $MP_MOD/multiplayer.dll && -f $MP_MOD/mod.json ]] || { echo "stage: set MP_MOD=<multiplayer module folder> (or KIT=<built kit>)" >&2; exit 1; }
  MODS=$MP_MOD nice -n 19 zsh $W/build_win.sh > $B/build_win.log 2>&1 || { tail -20 $B/build_win.log >&2; exit 1; }
  KIT=$L/build/win/kit
fi
[[ -f $KIT/install.bat && -f $KIT/files/baselib.dll ]] || { echo "stage: $KIT is not a built kit" >&2; exit 1; }
[[ -f $KIT/files/mods/multiplayer/multiplayer.dll ]] || { echo "stage: the kit has no multiplayer module" >&2; exit 1; }

S=$B/stage; [[ -d $S ]] && mv $S $B/old/stage-$(date +%Y%m%d-%H%M%S)
mkdir -p $S/mods/samefn-bie $S/cache
cp -R $KIT $S/kit
cp $DLL $S/mods/samefn-bie/
cat > $S/mods/samefn-bie/mod.json <<EOF
{
 "id": "samefn-bie",
 "name": "SameFn test plugin",
 "version": "0.2.0",
 "kind": "bepinex",
 "scope": "gameplay",
 "platforms": {"any": {"files": {"SameFnWin.dll": "$(sum $DLL)"}}},
 "description": "TEST ONLY: Harmony postfix on MainFlow.Update (loader/test/winharmony)."
}
EOF
cp $Z $S/cache/
[[ -f $B/stage.zip ]] && mv $B/stage.zip $B/old/stage-$(date +%Y%m%d-%H%M%S).zip
(cd $S && zip -qrX $B/stage.zip kit mods cache)
cp $H/wh_guest.ps1 $B/wh_guest.ps1
echo "staged $B/stage.zip ($(du -h $B/stage.zip | cut -f1)): kit $(cat $KIT/files/RELEASE 2>/dev/null), build $(head -1 $KIT/files/BUILD.txt), mods: $(ls $S/kit/files/mods | tr '\n' ' ')+ samefn-bie, plugin sha256 $(sum $DLL | cut -c1-12)"
