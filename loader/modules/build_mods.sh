#!/bin/zsh
# Every mod we make, for mac-arm64 AND win-x64 (owner, v12 item 11: "every mod we make must work on Windows too").
# Output: build/mods_all/mac/<id>/ (the Mac app's mods) and build/mods_all/win/<id>/ (the Windows kit: MODS=build/mods_all/win/*).
# Both copies of a module get the SAME mod.json, listing the files (sha256) of BOTH platforms: the loader's mod-set line
# hashes every platform's listed files, so a Mac player and a Windows player with the same mod give the same line.
#
#   build_mods.sh                 build all (Mac part through ../build.sh; Windows with ~/tools/llvm-mingw)
#   build_mods.sh --check [dir]   check a built set (default build/mods_all): every module lists mac-arm64 + win-x64,
#                                 entry files exist, listed sha256s match, both copies have the same mod.json. Exit 1 = bad.
# Env: MP_PIN (default ../mp_pin.txt) = the ds_multiplayer commit for multiplayer (and the headers Custom Modes uses);
#      CM_PIN (default ../cm_pin.txt) = the ds_custommodes commit for custom-modes on both platforms, CM_REPO its clone;
#      MP_WIN_TREE = a ds_multiplayer checkout to build the Windows multiplayer from instead of the pin
#      (development); SKIP_MAC=1 = reuse ../build/mods (no ../build.sh run).
#      Game files for the Hell delta: HELL_STOCK_WIN / HELL_STOCK_MAC (stock table bundles; read only).
set -e; cd "${0:A:h}"; MD=$PWD; L=${MD:h}
OUT=$L/build/mods_all

check() {
  python3 - "$1" <<'PY'
import hashlib, json, os, sys
root = sys.argv[1]; bad = []
mac, win = os.path.join(root, "mac"), os.path.join(root, "win")
ids = sorted(set(os.listdir(mac) if os.path.isdir(mac) else []) | set(os.listdir(win) if os.path.isdir(win) else []))
if not ids: bad.append(f"no modules under {root}")
for i in ids:
    mj = {}
    for side, d in (("mac", mac), ("win", win)):
        p = os.path.join(d, i, "mod.json")
        if not os.path.exists(p): bad.append(f"{i}: no {side} copy"); continue
        mj[side] = open(p, "rb").read()
        m = json.loads(mj[side]); pl = m.get("platforms", {})
        if m.get("kind") == "bepinex":           # BepInEx plugin: platforms.any.files = the same DLLs on both systems
            fs = (pl.get("any") or {}).get("files") or {}
            if not fs: bad.append(f"{i}: bepinex mod.json lists no platforms.any files")
            for f, h in fs.items():
                fp = os.path.join(d, i, f)
                if not os.path.exists(fp): bad.append(f"{i}: {side} file {f} missing")
                elif hashlib.sha256(open(fp, "rb").read()).hexdigest() != h: bad.append(f"{i}: {side} file {f} sha256 differs from mod.json")
            continue
        key = "mac-arm64" if side == "mac" else "win-x64"
        for k in ("mac-arm64", "win-x64"):
            if k not in pl: bad.append(f"{i}: mod.json lacks {k}")
        e = pl.get(key, {})
        if not e.get("entry") or not os.path.exists(os.path.join(d, i, e["entry"])): bad.append(f"{i}: {side} entry {e.get('entry')} missing")
        files = e.get("files") or {}
        if e.get("entry") and e["entry"] not in files: bad.append(f"{i}: {key} files do not list the entry")
        for f, h in files.items():
            fp = os.path.join(d, i, f)
            if not os.path.exists(fp): bad.append(f"{i}: {side} file {f} missing")
            elif hashlib.sha256(open(fp, "rb").read()).hexdigest() != h: bad.append(f"{i}: {side} file {f} sha256 differs from mod.json")
        for b, t in (m.get("tables") or {}).items():
            if t.get(key) and not os.path.exists(os.path.join(d, i, t[key])): bad.append(f"{i}: {side} table {t[key]} missing")
    if len(mj) == 2 and mj["mac"] != mj["win"]: bad.append(f"{i}: mac and win mod.json differ")
for b in bad: print("CHECK FAIL:", b)
print(f"check {root}: {len(ids)} modules ({', '.join(ids)}): {'OK' if not bad else str(len(bad)) + ' problem(s)'}")
sys.exit(1 if bad else 0)
PY
}
if [[ $1 == --check ]]; then check ${2:-$OUT}; exit $?; fi

TC=~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
[[ -x $TC ]] || { echo "llvm-mingw not found: $TC" >&2; exit 1; }
PM=(-ffile-prefix-map=$HOME=/src)                               # no build path (user name) in what ships (gate G12)
CFM=($PM -arch arm64 -O2 -Wall -Wno-unused-function -mmacosx-version-min=12.0 -I$L/sdk)
CFW=($PM -O2 -Wall -Wno-unused-function -D__USE_MINGW_ANSI_STDIO=1 -I$L/sdk)
mkdir -p $OUT/old; for s in mac win; do [[ -d $OUT/$s ]] && mv $OUT/$s $OUT/old/$s-$(date +%Y%m%d-%H%M%S); done
mkdir -p $OUT/mac $OUT/win

# ---- Mac: ../build.sh = loader + multiplayer + custom-modes from the pinned ds_multiplayer tree (its own rules)
if [[ -z $SKIP_MAC ]]; then MP_PIN=${MP_PIN:-} zsh $L/build.sh; fi
for i in multiplayer custom-modes; do [[ -d $L/build/mods/$i ]] && cp -R $L/build/mods/$i $OUT/mac/; done
[[ -d $OUT/mac/multiplayer ]] || { echo "no Mac multiplayer module in $L/build/mods" >&2; exit 1; }

# ---- simple modules (one C file each, same source for both platforms)
simple() {   # id src
  local id=$1 src=$2
  mkdir -p $OUT/mac/$id $OUT/win/$id
  nice -n 19 clang $CFM -dynamiclib -install_name @rpath/$id.dylib -o $OUT/mac/$id/$id.dylib.tmp $MD/$id/$src
  codesign -f -s - $OUT/mac/$id/$id.dylib.tmp 2>/dev/null; mv -f $OUT/mac/$id/$id.dylib.tmp $OUT/mac/$id/$id.dylib
  nice -n 19 $TC $CFW -shared -Wl,--no-insert-timestamp -o $OUT/win/$id/$id.dll.tmp $MD/$id/$src -Wl,--export-all-symbols -static -static-libgcc
  mv -f $OUT/win/$id/$id.dll.tmp $OUT/win/$id/$id.dll
  if [[ -d $MD/$id/rva ]]; then                                  # none for an all-by-name module (zetu-suite)
    mkdir -p $OUT/mac/$id/rva $OUT/win/$id/rva; cp $MD/$id/rva/*_mac*.tsv $OUT/mac/$id/rva/; cp $MD/$id/rva/*_win*.tsv $OUT/win/$id/rva/
  fi
  cp $MD/$id/mod.json $OUT/mac/$id/; cp $MD/$id/mod.json $OUT/win/$id/
  echo "built $id (mac-arm64 + win-x64)"
}
simple gcd-cdr gcd_cdr.c
simple hell-difficulty hell.c
simple zetu-suite zetu.c
simple sound-fix sound_fix.c
simple multi-bar multi_bar.c
simple weapon-slot weapon_slot.c
simple locked-slots locked_slots.c
# Hell table half: deltas from the stock table bundles (game files, read only; only the delta ships)
# owner 2026-10-20: the CrossOver DSMP bottle is gone; its stock Windows bundle was copied next to the Mac one
HW=${HELL_STOCK_WIN:-$HOME/DungeonSettlers_HellDifficulty/b25846389/stock/table_assets_all_ea6555cd907771d454adb127292fd425.bundle}
HM=${HELL_STOCK_MAC:-$HOME/DungeonSettlers_HellDifficulty/b25846389/stock/table_assets_all_ee685f7db1494c1e6bf42e948951b8dc.bundle}
B=25846389; T=$L/build/hell_delta; mkdir -p $T
for os f in win "$HW" mac "$HM"; do
  [[ -f $f ]] || { echo "Hell: stock table bundle for $os not found: $f" >&2; exit 1; }
  nice -n 19 /usr/local/bin/python3 $MD/hell-difficulty/tools/hell_delta.py "$f" $T/${B}_${os}_tables.dsdelta | tail -1
  cp $T/${B}_${os}_tables.dsdelta $OUT/$os/hell-difficulty/rva/
done

# ---- Windows multiplayer + custom-modes (ds_multiplayer win/build_win.sh, module target)
if [[ -n $MP_WIN_TREE ]]; then WT=${MP_WIN_TREE:A}
else
  MP_REPO=${MP_REPO:-$HOME/ds_multiplayer}; PIN=${MP_PIN:-$(head -1 $L/mp_pin.txt)}
  SHA=$(git -C $MP_REPO rev-parse --verify "$PIN^{commit}") || { echo "mp pin $PIN not found" >&2; exit 1; }
  WT=$L/build/pinwin/$SHA
  if [[ ! -f $WT/win/build_win.sh ]]; then
    mkdir -p $WT.tmp; git -C $MP_REPO archive $SHA | tar -x -C $WT.tmp; mv $WT.tmp $WT
  fi
  [[ -e $WT/win/25846389 ]] || ln -s $MP_REPO/win/25846389 $WT/win/25846389    # game files + dump: local only, never in git
fi
# Custom Modes from its own pin (cm_pin.txt), not the multiplayer tree's custom/: the ds_custommodes tree replaces
# $WT/custom (same folder as before: the Windows DLLs keep quickjs' __FILE__ paths, so the bytes do not change)
CM_REPO=${CM_REPO:-$HOME/ds_custommodes}; CM_PIN=${CM_PIN:-$(head -1 $L/cm_pin.txt)}
CSHA=$(git -C $CM_REPO rev-parse --verify "$CM_PIN^{commit}") || { echo "cm pin $CM_PIN not found in $CM_REPO" >&2; exit 1; }
if [[ -z $MP_WIN_TREE && $(cat $WT/CM_PINNED 2>/dev/null) != $CSHA ]]; then
  mkdir -p $L/build/old; [[ -d $WT/custom ]] && mv $WT/custom $L/build/old/custom-$(date +%Y%m%d-%H%M%S)
  mkdir -p $WT/custom.tmp; git -C $CM_REPO archive $CSHA | tar -x -C $WT/custom.tmp; mv $WT/custom.tmp $WT/custom
  print -r -- $CSHA > $WT/CM_PINNED
fi
# the loader SDK goes inside the tree build_win.sh maps to /mp: with $L/sdk its folder path was in the debug info and so
# in the DLL's build id (8 bytes differed between build folders, v0.1.24/25); now the same bytes from any folder
mkdir -p $WT/win/build/dsl_sdk; cp -p $L/sdk/*.h $WT/win/build/dsl_sdk/
DSMP_TARGET=module DSL_SDK=$WT/win/build/dsl_sdk DSL_MODJSON=$MD/multiplayer/mod.json CM_MODJSON=${CM_MODJSON:-} nice -n 19 zsh $WT/win/build_win.sh | grep -v "^ABI \|^warning" | tail -6
cp -R $WT/win/build/mods/multiplayer $OUT/win/
[[ -d $WT/win/build/mods/custom-modes ]] && cp -R $WT/win/build/mods/custom-modes $OUT/win/
[[ -d $OUT/mac/custom-modes/art ]] && cp -R $OUT/mac/custom-modes/art $OUT/win/custom-modes/   # card pictures (DPS check): same files on Windows
# translations of the pinned trees (repo lang/*.json; a subfolder, so not in mod.json "files" nor the mod-set hash)
for s in mac win; do
  if [[ -n $MP_WIN_TREE ]]; then zsh $MD/copy_lang.sh $WT - $OUT/$s/multiplayer; else zsh $MD/copy_lang.sh $MP_REPO $SHA $OUT/$s/multiplayer; fi
  if [[ -d $OUT/$s/custom-modes ]]; then zsh $MD/copy_lang.sh $CM_REPO $CSHA $OUT/$s/custom-modes; fi
done

# ---- map files of the Foreign Expedition hub and the PvP v1 world (ds_multiplayer maps/README.md: game saves, so not in
# git): hub_<build>.json + base_<build>.json for every game build of the module, top level next to the module in both
# copies (listed with sha256 in mod.json files below). scan_saves.py lets exactly these names through. A missing map = no build.
MAPS=${MP_MAPS:-${MP_REPO:-$HOME/ds_multiplayer}/maps}
for b in $(python3 -c 'import json,sys; print(" ".join(json.load(open(sys.argv[1]))["game_builds"]))' $MD/multiplayer/mod.json); do
  for k in hub base; do
    [[ -f $MAPS/${k}_$b.json ]] || { echo "multiplayer: map file $MAPS/${k}_$b.json missing (ds_multiplayer maps/README.md)" >&2; exit 1; }
    for s in mac win; do cp $MAPS/${k}_$b.json $OUT/$s/multiplayer/; done
  done
done
echo "multiplayer: map files from $MAPS"

# ---- what ships carries no debug info (build paths) and no user name: strip, then scan every file (fails the build)
for f in $OUT/win/**/*.dll(N); do ~/tools/llvm-mingw/bin/llvm-strip --strip-debug $f; done
for f in $OUT/mac/**/*.dylib(N); do strip -S $f 2>/dev/null; codesign -f -s - $f 2>/dev/null; done
# ---- dsmp-bridge (BepInEx library for C# mods, one AnyCPU DLL for both): after the strip (a .NET DLL is not stripped)
zsh $MD/build_bridge.sh $OUT
python3 $MD/scan_names.py "$USER" $OUT/mac $OUT/win > $OUT/name_scan.txt || { cat $OUT/name_scan.txt; echo "build_mods: the user name is inside the built mods" >&2; exit 1; }
tail -1 $OUT/name_scan.txt

# ---- one mod.json per module, both platforms' files listed (and identical in both copies)
python3 - $OUT <<'PY'
import hashlib, json, os, sys
out = sys.argv[1]
def sha(p): return hashlib.sha256(open(p, "rb").read()).hexdigest()
for i in sorted(set(os.listdir(f"{out}/mac")) | set(os.listdir(f"{out}/win"))):
    srcs = [f"{out}/{s}/{i}/mod.json" for s in ("mac", "win") if os.path.exists(f"{out}/{s}/{i}/mod.json")]
    m = json.load(open(srcs[0], encoding="utf-8")); pl = m.setdefault("platforms", {})
    if m.get("kind") == "bepinex": print(f"{i}: any {len(pl.get('any', {}).get('files', {}))} file(s) (bepinex)"); continue   # build_bridge.sh wrote it
    for s in srcs[1:]:                                   # the Windows build may add its own win-x64 part (entry)
        for k, v in json.load(open(s)).get("platforms", {}).items(): pl.setdefault(k, {}).update({x: y for x, y in v.items() if x != "files"})
    for side, key, ext in (("mac", "mac-arm64", ".dylib"), ("win", "win-x64", ".dll")):
        d = f"{out}/{side}/{i}"
        if not os.path.isdir(d): continue
        e = pl.setdefault(key, {}); e.setdefault("entry", i + ext)
        files = {}
        for r, _, fs in os.walk(d):
            for f in sorted(fs):
                rel = os.path.relpath(os.path.join(r, f), d)
                if rel == "mod.json" or rel.startswith(".") or "/" in rel or rel == "PINNED_COMMIT": continue   # top-level files (the loader checks those)
                files[rel] = sha(os.path.join(r, f))
        e["files"] = files
    t = json.dumps(m, indent=1, ensure_ascii=False) + "\n"
    for s in ("mac", "win"):
        if os.path.isdir(f"{out}/{s}/{i}"): open(f"{out}/{s}/{i}/mod.json", "w", encoding="utf-8").write(t)
    print(f"{i}: " + ", ".join(f"{k} {len(v.get('files', {}))} file(s)" for k, v in sorted(pl.items())))
PY
check $OUT
