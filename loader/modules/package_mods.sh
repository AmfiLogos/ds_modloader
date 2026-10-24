#!/bin/zsh
# One download per mod: build/packages/<id>-<version>.zip, made from a built set (build_mods.sh output, default
# build/mods_all). Inside: ONE folder <id>/ = the Mac copy and the Windows copy merged (the same mod.json, the
# .dylib AND the .dll, every rva/ table), i.e. exactly the folder the loader reads from its mods folder on both systems.
# A player puts the zip (or the unzipped <id> folder) into the mods folder; the starter unpacks it on the next start.
#
#   package_mods.sh [mods_all dir] [out dir]
# Release assets: multiplayer + dsmp-bridge -> ds_multiplayer, custom-modes -> ds_custommodes, gcd-cdr + hell-difficulty
# (+ zetu-suite, sound-fix, multi-bar) -> ds_modloader, and hooks.json + hooks.tsv (the hook list, docs/) -> ds_modloader.
set -e; cd "${0:A:h}"; L=${PWD:h}
SRC=${1:-$L/build/mods_all}; OUT=${2:-$L/build/packages}
zsh ./build_mods.sh --check $SRC >/dev/null || { zsh ./build_mods.sh --check $SRC; echo "package_mods: $SRC is not a good built set" >&2; exit 1; }
[[ -d $OUT ]] && { mkdir -p ${OUT:h}/old; mv $OUT ${OUT:h}/old/packages-$(date +%Y%m%d-%H%M%S); }
mkdir -p $OUT
for d in $SRC/mac/*(/); do
  id=${d:t}; ver=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' $d/mod.json)
  ST=$OUT/.stage/$id; mkdir -p $ST
  # Mac copy first, then the Windows copy on top: a file in both must be the same bytes (mod.json is, checked above)
  cp -R $d/ $ST/
  ( cd $SRC/win/$id && find . -type f ) | while read -r f; do
    if [[ -f $ST/$f ]] && ! cmp -s $ST/$f $SRC/win/$id/$f; then echo "package_mods: $id: $f differs between mac and win" >&2; exit 1; fi
    mkdir -p $ST/${f:h}; cp $SRC/win/$id/$f $ST/$f
  done
  # licenses/ in every release file: the mod's own repo license (multiplayer: ds_multiplayer, + the rwmt-Multiplayer
  # license its net code comes from; custom-modes: ds_custommodes; the others: this repo) and the third-party notices.
  # The repos' texts come from the pinned commits (mp_pin.txt / cm_pin.txt), the same trees the mods are built from.
  mkdir -p $ST/licenses; cp $L/../licenses/THIRD_PARTY_NOTICES.txt $ST/licenses/
  case $id in
    multiplayer) git -C ${MP_REPO:-$HOME/ds_multiplayer} show "$(head -1 $L/mp_pin.txt):LICENSE" > $ST/licenses/LICENSE.txt
                 git -C ${MP_REPO:-$HOME/ds_multiplayer} show "$(head -1 $L/mp_pin.txt):src/net/LICENSE.rwmt-Multiplayer.txt" > $ST/licenses/LICENSE.rwmt-Multiplayer.txt ;;
    dsmp-bridge) git -C ${MP_REPO:-$HOME/ds_multiplayer} show "$(head -1 $L/mp_pin.txt):LICENSE" > $ST/licenses/LICENSE.txt ;;   # source: ds_multiplayer ext/bepinex_bridge
    custom-modes) git -C ${CM_REPO:-$HOME/ds_custommodes} show "$(head -1 $L/cm_pin.txt):LICENSE" > $ST/licenses/LICENSE.txt ;;
    *) cp $L/../licenses/LICENSE.txt $ST/licenses/ ;;
  esac
  [[ -s $ST/licenses/LICENSE.txt ]] || { echo "package_mods: $id: no license text" >&2; exit 1; }
  xattr -cr $ST
  # fixed order and dates: the same set gives the same zip bytes
  find $ST -exec touch -h -t 202601010000 {} +
  Z=$OUT/$id-$ver.zip
  (cd $OUT/.stage && find $id | LC_ALL=C sort | zip -qX -D $Z -@)
  echo "$Z ($(du -h $Z | cut -f1), $(zipinfo -1 $Z | wc -l | tr -d ' ') files)"
done
mkdir -p ${OUT:h}/old; mv $OUT/.stage ${OUT:h}/old/packages-stage-$(date +%Y%m%d-%H%M%S)
# the hook list (docs/hooks.json + hooks.tsv): ds_modloader release assets, checked against the pinned sources first
python3 $L/../tools/hooks/hooklist.py check >/dev/null || { python3 $L/../tools/hooks/hooklist.py check; echo "package_mods: docs/hooks.json lacks a hook: run tools/hooks/hooklist.py gen" >&2; exit 1; }
cp $L/../docs/hooks.json $L/../docs/hooks.tsv $OUT/ && echo "$OUT/hooks.json, hooks.tsv (hook list)"
# what ships: no user name, no build path (the same scan as the friend zips)
python3 $PWD/scan_names.py "$USER" $OUT/*.zip > $OUT/name_scan.txt || { cat $OUT/name_scan.txt; echo "package_mods: the user name is inside a package" >&2; exit 1; }
tail -1 $OUT/name_scan.txt; mkdir -p ${OUT:h}/old; mv $OUT/name_scan.txt ${OUT:h}/old/packages_name_scan.txt
# and no save or player file (owner 2026-10-16)
python3 $PWD/scan_saves.py $OUT/*.zip || { echo "package_mods: player data inside a package" >&2; exit 1; }
