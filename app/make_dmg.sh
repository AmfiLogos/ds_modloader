#!/bin/zsh
# make_dmg.sh [app] -> app/build/Dungeon-Settlers-Modded-Mac.dmg: the app + an Applications shortcut (drag to install).
# Default app = app/build/Dungeon Settlers Modded.app (build_app.sh). Not signed with a Developer ID: the README says how
# to open it the first time. The image holds no "._" files (a disk image keeps the attributes itself; they are cleared).
set -e
HERE="${0:A:h}"; NAME="Dungeon Settlers Modded"
APP=${1:-$HERE/build/$NAME.app}; APP=${APP:A}; OUT=$HERE/build/Dungeon-Settlers-Modded-Mac.dmg
[[ -d $APP/Contents/Resources/dsml/mods ]] || { echo "no app with mods at $APP (run app/build_app.sh)" >&2; exit 1; }
codesign -v --deep --strict "$APP" || { echo "the app's signature is broken: $APP" >&2; exit 1; }
mkdir -p $HERE/old; ST=$HERE/build/dmgstage
[[ -e $ST ]] && mv $ST $HERE/old/dmgstage-$(date +%Y%m%d-%H%M%S)
[[ -e $OUT ]] && mv $OUT $HERE/old/$(date +%Y%m%d-%H%M%S)-${OUT:t}
mkdir -p $ST
ditto --norsrc --noextattr --noacl "$APP" "$ST/$NAME.app"
xattr -cr "$ST/$NAME.app"
codesign -v --deep --strict "$ST/$NAME.app"
ln -s /Applications "$ST/Applications"
nice -n 10 hdiutil create -quiet -volname "$NAME" -srcfolder $ST -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov $OUT.tmp.dmg
mv -f $OUT.tmp.dmg $OUT
mv $ST $HERE/old/dmgstage-$(date +%Y%m%d-%H%M%S)
hdiutil verify -quiet $OUT
echo "dmg: $OUT ($(du -h $OUT | cut -f1))"
