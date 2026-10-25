#!/bin/zsh
# app/test/player_mods_test.sh - a player's copy of a mod (ROOT/mods/<id>) never replaces a NEWER copy that the app or
# auto-update brings; an equal or newer player copy still wins. Host only: no game, no Steam, a fake HOME.
# Break caught (owner's Mac 2026-10-25): ROOT/mods/weapon-slot 0.1.1 from an old release zip, the app shipped 0.1.2. The
# stub kept 0.1.2, then the Mods window's refresh (loader/ui/dsl_pkg.c) copied 0.1.1 back into ROOT/loader/mods with a
# new .player_mod: the player stays on the old mod forever (wrong world kinds, old save format, refused by players on
# the new version).
# Mac start sequence per case: stub (app/stub/ds_modded.c, DSMODDED_DRY_RUN=1) -> the Mods window opens (dsl_pkg_refresh,
# host driver player_mods/pkg_refresh.c) -> stub again. Windows layout (one folder: the player's = the loader's): an
# older zip dropped there must not replace the newer installed mod; a newer zip still installs.
# usage: app/test/player_mods_test.sh   (files in test_env/, gitignored)
set -u
HERE="${0:A:h}"; REPO="${HERE:h:h}"; L="$REPO/loader"
T="$REPO/test_env/pmods-$(date +%Y%m%d-%H%M%S)-$$"; mkdir -p "$T"
CC=(nice -n 19 clang -arch arm64 -O1 -Wall -mmacosx-version-min=11.0)
fails=0; pass=0
check() { if eval "$2"; then print "PASS $1"; pass=$((pass+1)); else print "FAIL $1"; fails=$((fails+1)); fi }

APP="$T/app/Dungeon Settlers Modded.app"; AM="$APP/Contents/Resources/dsml/mods"
mkdir -p "$APP/Contents/MacOS" "$AM/weapon-slot"
$CC -framework CoreFoundation -I"$L/src" -o "$APP/Contents/MacOS/DS Modded" "$REPO/app/stub/ds_modded.c" || exit 2
$CC -I"$L/src" -o "$T/pkg_refresh" "$HERE/player_mods/pkg_refresh.c" "$L/ui/dsl_pkg.c" "$L/src/dsl_json.c" || exit 2
print kit-test > "$APP/Contents/Resources/dsml/VERSION"
mod() {  # mod <dir> <version> <whose> [extra mod.json fields]: a weapon-slot folder; its binary says whose copy it is
  mkdir -p "$1"; print -r -- "{\"id\": \"weapon-slot\", \"version\": \"$2\", \"kind\": \"code\"${4:-}}" > "$1/mod.json"
  print -r -- "$3" > "$1/weapon-slot.dylib"
}
mod "$AM/weapon-slot" 0.1.2 app
PLAY="$T/play/DungeonSettlers.app"; mkdir -p "$PLAY/Contents/MacOS"; : > "$PLAY/Contents/MacOS/Dungeon Settlers"
FH="$T/home"; mkdir -p "$FH/Library/Logs"; SLOG="$FH/Library/Logs/DungeonSettlersModded.log"
ver() { /usr/bin/python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$1" 2>/dev/null }

# run <case> <player version> [extra fields]: ROOT/mods/weapon-slot = the player's copy; stub, Mods window, stub
run() {
  R="$T/$1/root"; LM="$R/loader/mods/weapon-slot"; mkdir -p "$R/profiles/modded"
  print -r -- "{\"build\": \"1\", \"app\": \"$PLAY\", \"ident\": \"1\"}" > "$R/profiles/modded/copy.json"
  mod "$R/mods/weapon-slot" "$2" player "${3:-}"
  local i; for i in 1 2; do
    env -i HOME="$FH" PATH=/usr/bin:/bin DSMODDED_DRY_RUN=1 DSMODDED_ROOT="$R" DSMP_NO_DIALOG=1 DSMODDED_NO_STEAM=1 \
      "$APP/Contents/MacOS/DS Modded" >> "$T/$1/stub.txt" 2>&1 || print "stub exit $? ($1)"
    (( i == 1 )) && env DSL_MODS_DIR="$R/loader/mods" DSML_USER_MODS_DIR="$R/mods" "$T/pkg_refresh" >> "$T/$1/refresh.txt" 2>&1
  done
}

print "== Mac: player copy older than the app's (owner's case)"
run older 0.1.1
check "the app's newer copy runs (0.1.2, not the player's, no .player_mod)" '[[ $(ver "$LM/mod.json") == 0.1.2 && $(cat "$LM/weapon-slot.dylib") == app && ! -e "$LM/.player_mod" ]]'
check "the Mods window says why (one line), the stub too" 'grep -q "player mod weapon-slot 0.1.1 is older than the loader.s 0.1.2" "$T/older/refresh.txt" && grep -q "player mod weapon-slot 0.1.1 is older than the loader.s 0.1.2" "$SLOG"'
check "the player's folder is left as it is" '[[ $(ver "$R/mods/weapon-slot/mod.json") == 0.1.1 ]]'

print "== Mac: player copy of the same version (other files) and newer: the player's copy runs"
run equal 0.1.2 ', "name": "player build"'
check "same version: the player's copy runs" '[[ $(cat "$LM/weapon-slot.dylib") == player && -f "$LM/.player_mod" ]]'
run newer 0.1.3
check "newer: the player's copy runs" '[[ $(ver "$LM/mod.json") == 0.1.3 && $(cat "$LM/weapon-slot.dylib") == player && -f "$LM/.player_mod" ]]'

print "== Windows layout (one mods folder): an older zip never replaces the installed newer mod"
W="$T/win/mods"; mod "$W/weapon-slot" 0.1.2 app
zipmod() { local s="$T/win/src-$1"; mod "$s/weapon-slot" "$1" player; (cd "$s" && /usr/bin/ditto -c -k --keepParent weapon-slot "$W/weapon-slot-$1.zip") }
zipmod 0.1.1
env -u DSML_USER_MODS_DIR DSL_MODS_DIR="$W" "$T/pkg_refresh" > "$T/win/refresh1.txt" 2>&1
check "older zip: the installed 0.1.2 stays, zip set aside, no 'not a mod' note" '[[ $(ver "$W/weapon-slot/mod.json") == 0.1.2 && $(cat "$W/weapon-slot/weapon-slot.dylib") == app && -f "$W/.installed/weapon-slot-0.1.1.zip" ]] && grep -q "is older than the installed 0.1.2" "$T/win/refresh1.txt" && grep -qx "REFRESH 0 " "$T/win/refresh1.txt"'
zipmod 0.1.3
env -u DSML_USER_MODS_DIR DSL_MODS_DIR="$W" "$T/pkg_refresh" > "$T/win/refresh2.txt" 2>&1
check "newer zip: installed" '[[ $(ver "$W/weapon-slot/mod.json") == 0.1.3 && $(cat "$W/weapon-slot/weapon-slot.dylib") == player ]]'

print "== $pass passed, $fails failed (files in $T)"
exit $(( fails > 0 ))
