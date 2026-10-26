#!/bin/zsh
# Build the ZET-U Mod Suite BepInEx plugin (+ the test-only probe) and, with `zip`, the release zip.
#   ./build.sh            build/plugin/ZetuSuite.dll, build/probe/ZetuProbe.dll (test only, never in the zip)
#   ./build.sh zip        + build/release/zetu-suite-bepinex-v<ver>.zip (BepInEx/plugins/ZetuSuite/ZetuSuite.dll + README)
#   ./build.sh guard <dll>  only the guard below, on that DLL
# Refs (not in git): ZETU_BIE_LIB (be.788 core DLLs), ZETU_INTEROP (BepInEx/interop of game build 25846389); see
# Directory.Build.props for the defaults. Nothing outside build/ is written.
set -e; [[ $1 == guard ]] && G=${2:A}; cd "${0:A:h}"
# Guard: the plugin names no game type or method by its compiler number (interop "_<Method>_d__<N>" iterators, also
# "_<Method>_b__<N>" lambdas and "__c__DisplayClass<N>_<M>"). The number changes with almost every game build, and a
# method that names one fails to load on any other build before its own try/catch runs (0.1.1: "Could not load type
# '_LoadCampaign_d__73'", no world loaded). Find them by method name instead: Plugin.IterType / Plugin.IterField.
guard() {
  [[ -f $1 ]] || { print -u2 "guard: FAIL, no file $1"; return 1 }
  local hit=$(strings -a $1 | grep -oE '_[A-Za-z0-9_]+_[bd]__[0-9]+|__c__DisplayClass[0-9]+_[0-9]+' | sort -u | tr '\n' ' ')
  [[ -z $hit ]] && { print "guard: OK, ${1:t} names no game type by its compiler number"; return 0 }
  print -u2 "guard: FAIL, ${1:t} names game type(s) by their compiler number: $hit
  the number changes with almost every game build; a method that names one fails to load on any other build
  (0.1.1: TypeLoadException, no world loaded). Find them by method name: Plugin.IterType / Plugin.IterField."
  return 1
}
[[ $1 == guard ]] && { guard $G; exit }
export DOTNET_ROOT=${DOTNET_ROOT:-$HOME/.dotnet}; DN=${DOTNET:-$DOTNET_ROOT/dotnet}
$DN build ZetuSuite.csproj -c Release -o build/plugin -nologo -v q
guard build/plugin/ZetuSuite.dll
$DN build probe/ZetuProbe.csproj -c Release -o build/probe -nologo -v q
[[ $1 == zip ]] || exit 0
V=$(sed -n 's/.*<Version>\(.*\)<\/Version>.*/\1/p' ZetuSuite.csproj)
R=build/release; S=$R/zetu-suite-bepinex-v$V; mkdir -p $R
[[ -e $S ]] && mv $S $R/old-$(date +%Y%m%d-%H%M%S)-${S:t}
mkdir -p $S/BepInEx/plugins/ZetuSuite
cp build/plugin/ZetuSuite.dll $S/BepInEx/plugins/ZetuSuite/
cp README.md $S/BepInEx/plugins/ZetuSuite/README.md
[[ -e $S.zip ]] && mv $S.zip $R/old-$(date +%Y%m%d-%H%M%S)-${S:t}.zip
(cd $S && TZ=UTC zip -X -r -q ../${S:t}.zip BepInEx)
shasum -a 256 $S.zip | tee $S.zip.sha256
