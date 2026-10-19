#!/bin/zsh
# dsmp-bridge: DsmpBridge.dll (BepInEx 6 IL2CPP plugin "dsmp.bridge", net6, AnyCPU: the SAME file on Mac and Windows) built
# from the pinned ds_multiplayer tree (ext/bepinex_bridge/plugin/DsmpBridge), into <mods_all>/mac/dsmp-bridge and
# <mods_all>/win/dsmp-bridge with one mod.json (kind bepinex, platforms.any.files = the DLL's sha256).
#   build_bridge.sh [mods_all dir]          (build_mods.sh calls it; alone it only writes the two dsmp-bridge folders)
# Env: MP_REPO / MP_PIN as build_mods.sh; DSMP_BIE_LIB = the BepInEx + interop reference DLLs (not in git; default
#      $HOME/ds_multiplayer/ext/bepinex_bridge/lib); DOTNET (default ~/.dotnet/dotnet).
# Scope gameplay: the bridge itself adds no game state, but every peer must run the same version (a peer without it, or
# with an older one, does not run the managed actions and draws a different random), so the join check compares it.
set -e; cd "${0:A:h}"; MD=$PWD; L=${MD:h}
OUT=${1:-$L/build/mods_all}; DN=${DOTNET:-$HOME/.dotnet/dotnet}
MP_REPO=${MP_REPO:-$HOME/ds_multiplayer}; PIN=${MP_PIN:-$(head -1 $L/mp_pin.txt)}
SHA=$(git -C $MP_REPO rev-parse --verify "$PIN^{commit}") || { echo "mp pin $PIN not found" >&2; exit 1; }
P=ext/bepinex_bridge/plugin
git -C $MP_REPO cat-file -e $SHA:$P/DsmpBridge/DsmpBridge.csproj 2>/dev/null || { echo "dsmp-bridge: not in the pinned tree $SHA, skipped"; exit 0; }
[[ -x $DN ]] || { echo "dsmp-bridge: dotnet not found ($DN)" >&2; exit 1; }
export DSMP_BIE_LIB=${DSMP_BIE_LIB:-$HOME/ds_multiplayer/ext/bepinex_bridge/lib}
[[ -f $DSMP_BIE_LIB/BepInEx.Core.dll ]] || { echo "dsmp-bridge: BepInEx reference DLLs not found in $DSMP_BIE_LIB" >&2; exit 1; }
T=$L/build/pinbie/$SHA
if [[ ! -f $T/$P/DsmpBridge/DsmpBridge.csproj ]]; then
  mkdir -p $T.tmp; git -C $MP_REPO archive $SHA $P | tar -x -C $T.tmp; mkdir -p ${T:h}; mv $T.tmp $T
fi
B=$T/out; mkdir -p $B
(cd $T/$P/DsmpBridge && nice -n 19 $DN build -c Release -o $B 2>&1 | grep -E "rror CS|error MSB" | sort -u) || true
[[ -f $B/DsmpBridge.dll ]] || { echo "dsmp-bridge: build failed" >&2; exit 1; }
VER=$(sed -n 's:.*<Version>\(.*\)</Version>.*:\1:p' $T/$P/DsmpBridge/DsmpBridge.csproj | head -1)
for s in mac win; do
  [[ -d $OUT/$s/dsmp-bridge ]] && { mkdir -p $OUT/old; mv $OUT/$s/dsmp-bridge $OUT/old/dsmp-bridge-$s-$(date +%Y%m%d-%H%M%S); }
  mkdir -p $OUT/$s/dsmp-bridge; cp $B/DsmpBridge.dll $OUT/$s/dsmp-bridge/
done
python3 - $MD/dsmp-bridge/mod.json $OUT "$VER" <<'PY'
import hashlib, json, sys
src, out, ver = sys.argv[1:4]
m = json.load(open(src, encoding="utf-8")); m["version"] = ver
m["platforms"] = {"any": {"files": {"DsmpBridge.dll": hashlib.sha256(open(f"{out}/mac/dsmp-bridge/DsmpBridge.dll", "rb").read()).hexdigest()}}}
t = json.dumps(m, indent=1, ensure_ascii=False) + "\n"
for s in ("mac", "win"): open(f"{out}/{s}/dsmp-bridge/mod.json", "w", encoding="utf-8").write(t)
PY
echo "built dsmp-bridge $VER (any: DsmpBridge.dll $(shasum -a 256 $OUT/mac/dsmp-bridge/DsmpBridge.dll | cut -c1-12)) from ${SHA:0:7}"
