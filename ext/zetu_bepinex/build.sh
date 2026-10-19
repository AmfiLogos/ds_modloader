#!/bin/zsh
# Build the ZET-U Mod Suite BepInEx plugin (+ the test-only probe) and, with `zip`, the release zip.
#   ./build.sh            build/plugin/ZetuSuite.dll, build/probe/ZetuProbe.dll (test only, never in the zip)
#   ./build.sh zip        + build/release/zetu-suite-bepinex-v<ver>.zip (BepInEx/plugins/ZetuSuite/ZetuSuite.dll + README)
# Refs (not in git): ZETU_BIE_LIB (be.788 core DLLs), ZETU_INTEROP (BepInEx/interop of game build 25846389); see
# Directory.Build.props for the defaults. Nothing outside build/ is written.
set -e; cd "${0:A:h}"
export DOTNET_ROOT=${DOTNET_ROOT:-$HOME/.dotnet}; DN=${DOTNET:-$DOTNET_ROOT/dotnet}
$DN build ZetuSuite.csproj -c Release -o build/plugin -nologo -v q
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
