#!/bin/zsh
# Owner test build: makes src-tauri/seed (git-ignored), the signed mod list the app ships in Contents/Resources/seed.
#   tools/make_seed.sh [release folder]     default ~/ds_multiplayer/rig/lan_latest
# Mods: multiplayer (the released dsrig.dylib + det_ranges.tsv, version 0.<release number>.0, ON for new worlds at
# the first run) and the DEV-ONLY no-Steam patch the hidden tests use (never shown to players).
# Signed with dev_keys/dev.key: owner's own test only. Index seq = release number (v10 -> 10).
set -euo pipefail
here=${0:A:h:h}
rel=${1:-$HOME/ds_multiplayer/rig/lan_latest}
app=("$rel"/*.app(N[1]))
[[ -n "$app" ]] || { echo "no .app in $rel" >&2; exit 1; }
fw="$app/Contents/Frameworks"
n=$(sed -nE '1s/.* v([0-9]+) .*/\1/p' "$rel/WHATS_NEW.txt")
[[ -n "$n" ]] || { echo "no release number on line 1 of $rel/WHATS_NEW.txt" >&2; exit 1; }
steam="$HOME/Library/Application Support/Steam/steamapps"
build=$(sed -nE 's/.*"buildid"[[:space:]]+"([0-9]+)".*/\1/p' "$steam/appmanifest_2798330.acf")
stock="$steam/common/Dungeon Settlers/DungeonSettlers.app/Contents/Frameworks/GameAssembly.dylib.stock_$build"
[[ -f "$stock" ]] || { echo "no stock GameAssembly for build $build: $stock" >&2; exit 1; }

out="$here/src-tauri/seed"
[[ -e "$out" ]] && mv "$out" "$here/test_env/seed-old-$(date +%Y%m%d-%H%M%S)"
ver="0.$n.0"
mkdir -p "$out/files/multiplayer/$ver"
cp "$fw/dsrig.dylib" "$fw/det_ranges.tsv" "$out/files/multiplayer/$ver/"

python3 - "$out" "$ver" "$n" "$build" "$(shasum -a 256 "$stock" | cut -d' ' -f1)" <<'EOF'
import hashlib, json, os, sys
out, ver, n, build, ga = sys.argv[1:]
d = f"{out}/files/multiplayer/{ver}"
def entry(name):
    b = open(f"{d}/{name}", "rb").read()
    return {"name": name, "sha256": hashlib.sha256(b).hexdigest(), "size": len(b),
            "url": f"https://bundled.invalid/multiplayer/{ver}/{name}"}
idx = {"schema": 1, "seq": int(n), "issued": "owner test build (bundled, dev key)",
  "games": {build: {"mac-arm64": {"gameassembly_sha256": ga}}},
  "mods": [
    {"id": "multiplayer", "name": "Multiplayer", "version": ver, "kind": "code", "scope": "gameplay",
     "author": "DS Wiki", "game_builds": [build],
     "description": "Play one world together: host a game, friends join over the local network or by address.",
     "platforms": {"mac-arm64": {"files": [entry("dsrig.dylib"), entry("det_ranges.tsv")], "inject": ["dsrig.dylib"],
                                 "env": {"DSRIG_DIR": "{profile_dir}", "DSRIG_FPS": "60", "DSRIG_LIVE": "1"}}}},
    {"id": "test-steam-gate-off", "name": "TEST: no Steam", "version": "1.0.0", "kind": "patch", "dev_only": True,
     "game_builds": [build], "platforms": {"mac-arm64": {"patches": {build: [
       {"file": "Contents/Frameworks/GameAssembly.dylib", "offset": "0xD25730", "expect": "20008052", "new": "00008052"}]}}}}]}
json.dump(idx, open(f"{out}/index.json", "w"), indent=2)
EOF
tool="$here/target/release/dsml-tool"
[[ -x "$tool" ]] || (cd "$here" && CARGO_BUILD_JOBS=4 nice -n 19 cargo build --release -p dsml-core --bin dsml-tool)
"$tool" sign "$out/index.json" "$here/dev_keys/dev.key"
"$tool" verify "$out/index.json" "$here/dev_keys/dev.pub"
echo "seed: multiplayer $ver for build $build (dsrig.dylib $(shasum -a 256 "$fw/dsrig.dylib" | cut -c1-16)...) in $out"
