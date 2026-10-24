#!/bin/zsh
# app/test/run.sh - tests of "Dungeon Settlers Modded.app" without starting the game (no window).
#   (a) dsml-helper prepare: base + play copy from Steam's real install (APFS clone, read only), second call
#       instant, codesign -v of the play copy, Steam's app unchanged (CodeResources sha + mtimes)
#   (b) stub dry run from a copy (never quarantined: macOS moved quarantined test copies to the Trash): env/argv, defaults, loader copy,
#       pinned copy.json, loader update retires the old folder
#   (c) an existing active.json is never changed by the stub
#   (f) player mods folder ROOT/mods: package zip installed, bad zip set aside, removal restores the app's own copy
# Needs app/build/Dungeon Settlers Modded.app (app/build_app.sh). Tmp ROOT under test_env/ (gitignored).
set -u
HERE="${0:A:h}"; REPO="${HERE:h:h}"
APP="$REPO/app/build/Dungeon Settlers Modded.app"
[[ -d $APP ]] || { echo "build the app first: app/build_app.sh"; exit 2; }
T="$REPO/test_env/run-$(date +%Y%m%d-%H%M%S)"; ROOT="$T/root"; mkdir -p "$ROOT"
STEAM="$HOME/Library/Application Support/Steam/steamapps/common/Dungeon Settlers/DungeonSettlers.app"
fails=0
ok()  { echo "ok   $*"; }
bad() { echo "FAIL $*"; fails=$((fails + 1)); }
check() { local what=$1; shift; if "$@" >/dev/null 2>&1; then ok "$what"; else bad "$what"; fi; }
steam_state() { local cr="$STEAM/Contents/_CodeSignature/CodeResources"
  echo "$(shasum -a 256 "$cr" | cut -c1-64) $(stat -f %m "$cr") $(stat -f %m "$STEAM") $(stat -f %m "$STEAM/Contents/Frameworks/GameAssembly.dylib")"; }
now() { python3 -c 'import time; print(f"{time.time():.2f}")'; }

echo "== (a) helper prepare (ROOT $ROOT)"
S0=$(steam_state)
t0=$(now); OUT1=$("$APP/Contents/MacOS/dsml-helper" prepare --root "$ROOT" 2>"$T/helper1.err"); rc1=$?; t1=$(now)
OUT2=$("$APP/Contents/MacOS/dsml-helper" prepare --root "$ROOT" 2>"$T/helper2.err"); rc2=$?; t2=$(now)
FIRST=$(python3 -c "print(f'{$t1-$t0:.1f}')"); SECOND=$(python3 -c "print(f'{$t2-$t1:.2f}')")
echo "first prepare ${FIRST} s, second ${SECOND} s"
check "helper exit 0 twice" test $rc1 -eq 0 -a $rc2 -eq 0
PLAY=$(print -r -- "$OUT1" | sed -n 's/^PLAY //p'); BUILD=$(print -r -- "$OUT1" | sed -n 's/^BUILD //p')
check "helper prints BUILD and PLAY" test -n "$BUILD" -a -d "$PLAY"
check "second call same answer" test "$OUT1" = "$OUT2"
check "second call under 1 s" python3 -c "import sys; sys.exit(0 if $SECOND < 1 else 1)"
check "play copy codesign -v" codesign -v "$PLAY"
check "play copy bundle id" test "$(plutil -extract CFBundleIdentifier raw "$PLAY/Contents/Info.plist")" = com.CanOpener.Dungeon-Settlers.Modded
check "play copy icon" test -f "$PLAY/Contents/Resources/AppIcon.icns" -a "$(plutil -extract CFBundleIconFile raw "$PLAY/Contents/Info.plist")" = AppIcon
check "Steam's app unchanged" test "$S0" = "$(steam_state)"

echo "== (b) stub dry run from a copy"
Q="$T/q/Dungeon Settlers Modded.app"; mkdir -p "${Q:h}"; ditto "$APP" "$Q"
# No quarantine flag is ever written on a test copy (lead rule 2026-10-15: macOS 27 moved such copies to the Trash).
# The stub still runs `xattr -cr` on its loader folder, for a player's downloaded app.
stub() { env DSMODDED_DRY_RUN=1 DSMODDED_ROOT="$ROOT" DSMP_NO_DIALOG=1 "$Q/Contents/MacOS/DS Modded" "$@"; }
OUT=$(stub -psn_0_123 -foo 2>&1); rc=$?
print -r -- "$OUT" > "$T/dry1.txt"
check "stub dry run exit 0" test $rc -eq 0
P="$ROOT/profiles/modded"; D="$P/data"
QR=$(cd "$Q" && pwd -P)
typeset -A WANT
WANT=(SteamAppId 2798330 SteamGameId 2798330 CFFIXED_USER_HOME "$P/home" DSRIG_DIR "$P" DSRIG_LIVE 1 DSRIG_FPS 60
      DYLD_INSERT_LIBRARIES "$ROOT/loader/dsload.dylib" DSL_MODS_DIR "$ROOT/loader/mods" DSL_PROFILE "$D/Mods/active.json"
      DSL_LOG_DIR "$P" DSML_SESSION "$D/Mods/worlds.json" DSML_BUILD "$BUILD" DSML_APP "$QR" DSML_DATA "$D")
for k v in ${(kv)WANT}; do
  print -r -- "$OUT" | grep -qxF "ENV $k=$v" && ok "env $k" || bad "env $k (want $v; got $(print -r -- "$OUT" | grep "^ENV $k="))"
done
check "cwd = PROFILE" sh -c "grep -qxF 'CWD $P' '$T/dry1.txt'"
ARGS=$(print -r -- "$OUT" | sed -n 's/^ARGV //p' | tr '\n' '|')
check "argv (game, -logFile, args minus -psn_)" test "$ARGS" = "$PLAY/Contents/MacOS/Dungeon Settlers|-logFile|$P/player.log|-foo|"
check "steam_appid.txt" test "$(cat "$P/steam_appid.txt")" = 2798330
# default = every shipped mod (name order) except "default_on": false (Hell, ZET-U, Multi-unit view), as the Windows starter writes it
# (Sound settings fix is on by default since the kit ships it, v0.1.23)
check "active.json default" test "$(cat "$D/Mods/active.json")" = '{"enabled":["custom-modes","gcd-cdr","multiplayer","sound-fix"],"order":["custom-modes","gcd-cdr","multiplayer","sound-fix"]}'
check "worlds.json default" test "$(cat "$D/Mods/worlds.json")" = '{"worlds":{}}'
check "copy.json pinned" python3 -c "import json,sys; j=json.load(open('$P/copy.json')); sys.exit(0 if j=={'build':'$BUILD','app':'''$PLAY''','ident':'1'} else 1)"
# Local Network: the copy's own UUID (Steam's UUID = macOS blocks the copy and never asks); a copy pinned before
# that (copy.json without ident) gets it in place on the next start
uuids() { dwarfdump --uuid "$1/Contents/MacOS/Dungeon Settlers" | awk '{print $2}' | sort | tr '\n' ' '; }
check "play copy has its own UUID" test "$(uuids "$PLAY")" != "$(uuids "$STEAM")"
OLD="$T/oldcopy/DungeonSettlers.app"; mkdir -p "${OLD:h}"; cp -c -R "$ROOT/games/$BUILD/base/DungeonSettlers.app" "$OLD"   # as v0.1.12 made it
plutil -replace CFBundleIdentifier -string com.CanOpener.Dungeon-Settlers.Modded "$OLD/Contents/Info.plist"
codesign -f -s - "$OLD/Contents/MacOS/Dungeon Settlers" 2>/dev/null; codesign -f -s - "$OLD" 2>/dev/null
cp "$P/copy.json" "$T/copy.json.keep"
print -r -- "{\"build\": \"$BUILD\", \"app\": \"$OLD\"}" > "$P/copy.json"
stub >/dev/null 2>&1
check "old pinned copy: own UUID given" test "$(uuids "$OLD")" = "$(uuids "$PLAY")"
check "old pinned copy: alert text" test -f "$OLD/Contents/Resources/de.lproj/InfoPlist.strings"
check "old pinned copy: still signed" codesign -v "$OLD"
check "old pinned copy: copy.json ident" grep -q '"ident": "1"' "$P/copy.json"
mv "$T/copy.json.keep" "$P/copy.json"
check "loader VERSION copied" cmp "$ROOT/loader/VERSION" "$APP/Contents/Resources/dsml/VERSION"
check "loader dylib copied" cmp "$ROOT/loader/dsload.dylib" "$APP/Contents/Resources/dsml/dsload.dylib"
check "no quarantine in ROOT/loader" sh -c "! xattr -lr '$ROOT/loader' | grep -q quarantine"
check "first-run flag" test -f "$D/.copied_from_normal_game"
# second run: nothing re-copied; a changed VERSION retires the old loader folder
stub >/dev/null 2>&1
check "same VERSION: no retired loader" sh -c "! ls -d '$ROOT'/retired/loader-* 2>/dev/null"
echo old > "$ROOT/loader/VERSION"
stub >/dev/null 2>&1
check "new VERSION: old loader moved to retired/" sh -c "ls -d '$ROOT'/retired/loader-* && grep -qx old '$ROOT'/retired/loader-*/VERSION"
check "new VERSION: loader updated" cmp "$ROOT/loader/VERSION" "$APP/Contents/Resources/dsml/VERSION"

echo "== (c) an existing active.json stays as it is"
print -r -- '{"enabled":["multiplayer","hell-difficulty"],"order":["multiplayer","hell-difficulty"]}' > "$D/Mods/active.json"
stub >/dev/null 2>&1
check "player's active.json unchanged" test "$(cat "$D/Mods/active.json")" = '{"enabled":["multiplayer","hell-difficulty"],"order":["multiplayer","hell-difficulty"]}'
print -r -- '{"enabled": [ ], "order": []}' > "$D/Mods/active.json"
stub >/dev/null 2>&1
check "empty list unchanged (vanilla)" test "$(cat "$D/Mods/active.json")" = '{"enabled": [ ], "order": []}'

echo "== (f) player mods (ROOT/mods): a package zip is installed, a bad zip set aside, a removed one gives the app's own back"
G="$APP/Contents/Resources/dsml/mods/gcd-cdr"
python3 - "$G" "$ROOT/mods/gcd-cdr-9.9.9.zip" <<'PY'
import json, os, sys, zipfile
src, z = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(z, 'w') as zf:
    for d, _, fs in os.walk(src):
        for f in fs:
            p = os.path.join(d, f); a = 'gcd-cdr/' + os.path.relpath(p, src)
            if f == 'mod.json': m = json.load(open(p)); m['version'] = '9.9.9'; m = {'credits': [{'id': 'not-the-mod'}], **m}; zf.writestr(a, json.dumps(m))   # a nested "id" first: the stub must read the top-level one
            else: zf.write(p, a)
PY
print -r -- "not a zip" > "$ROOT/mods/broken-1.0.zip"
OUTU=$(stub 2>&1); print -r -- "$OUTU" > "$T/dry_user.txt"
check "env DSML_USER_MODS_DIR" grep -qxF "ENV DSML_USER_MODS_DIR=$ROOT/mods" "$T/dry_user.txt"
check "zip unpacked to mods/gcd-cdr" test -f "$ROOT/mods/gcd-cdr/mod.json" -a -f "$ROOT/mods/.installed/gcd-cdr-9.9.9.zip"
check "bad zip set aside" test -f "$ROOT/mods/.failed/broken-1.0.zip"
check "player mod in the loader" sh -c "grep -q 9.9.9 '$ROOT/loader/mods/gcd-cdr/mod.json' && test -f '$ROOT/loader/mods/gcd-cdr/.player_mod'"
mv "$ROOT/mods/gcd-cdr" "$T/gcd-cdr-player"
stub >/dev/null 2>&1
check "player mod taken out: the app's own back" sh -c "cmp '$ROOT/loader/mods/gcd-cdr/mod.json' '$G/mod.json' && test ! -e '$ROOT/loader/mods/gcd-cdr/.player_mod'"

echo "== (e) a BepInEx mod enabled: BepInEx installed at first use (real download), Doorstop env"
mkdir -p "$ROOT/loader/mods/fake-bie"; print -r -- '{"id":"fake-bie","version":"1.0.0","kind":"bepinex","scope":"client","platforms":{"any":{"files":{}}}}' > "$ROOT/loader/mods/fake-bie/mod.json"
print -r -- '{"enabled":["multiplayer","fake-bie"],"order":["multiplayer","fake-bie"]}' > "$D/Mods/active.json"
ML="$HOME/Library/Logs/DungeonSettlersModded.log"; mo=$(( $(stat -f %z "$ML" 2>/dev/null || echo 0) + 1 ))   # the log is append-mode: read only this run's bytes
t0=$(now); OUTB=$(stub 2>&1); t1=$(now); print -r -- "$OUTB" > "$T/dry_bie.txt"
B="$ROOT/bepinex/mac-arm64/0.1.0"
check "BepInEx installed" test -f "$B/libdoorstop.dylib" -a -f "$B/dotnet/libcoreclr.dylib"
check "no quarantine on BepInEx" sh -c "! xattr -lr '$B' | grep -q quarantine"
check "DYLD: dsload then Doorstop" grep -qxF "ENV DYLD_INSERT_LIBRARIES=$ROOT/loader/dsload.dylib:$B/libdoorstop.dylib" "$T/dry_bie.txt"
check "plugin folder" grep -qxF "ENV BEPINEX_PLUGIN_DIRS=$ROOT/loader/mods/fake-bie" "$T/dry_bie.txt"
# a BepInEx mod that needs a newer fake-bie (loader/src/dsl_deps.h) stays enabled but is not run
mkdir -p "$ROOT/loader/mods/fake-bie2"; print -r -- '{"id":"fake-bie2","version":"1.0.0","kind":"bepinex","requires":["fake-bie>=2.0.0"],"platforms":{"any":{"files":{}}}}' > "$ROOT/loader/mods/fake-bie2/mod.json"
print -r -- '{"enabled":["multiplayer","fake-bie","fake-bie2"],"order":["multiplayer","fake-bie","fake-bie2"]}' > "$D/Mods/active.json"
OUTR=$(stub 2>&1); print -r -- "$OUTR" > "$T/dry_bie_req.txt"
check "unmet requirement: that BepInEx mod is not run" grep -qxF "ENV BEPINEX_PLUGIN_DIRS=$ROOT/loader/mods/fake-bie" "$T/dry_bie_req.txt"
mv "$ROOT/loader/mods/fake-bie2" "$T/fake-bie2-out"
check "Doorstop target" grep -qxF "ENV DOORSTOP_TARGET_ASSEMBLY=$B/BepInEx/core/BepInEx.Unity.IL2CPP.dll" "$T/dry_bie.txt"
check "first-start notice" sh -c "tail -c +$mo '$ML' | grep -q 'Preparing mods (first start)'"
echo "bepinex first launch with download: $(python3 -c "print(f'{$t1-$t0:.1f}')") s"
print -r -- '{"enabled":["multiplayer"],"order":["multiplayer"]}' > "$D/Mods/active.json"
OUTN=$(stub 2>&1); print -r -- "$OUTN" > "$T/dry_nobie.txt"
check "no BepInEx mod: no Doorstop" sh -c "! grep -q DOORSTOP '$T/dry_nobie.txt' && grep -qxF 'ENV DYLD_INSERT_LIBRARIES=$ROOT/loader/dsload.dylib' '$T/dry_nobie.txt'"

echo "== (d) helper error goes to the notice (no Steam install in a fake HOME)"
FH="$T/fakehome"; mkdir -p "$FH/Library/Logs"
OUTE=$(env HOME="$FH" DSMODDED_DRY_RUN=1 DSMODDED_ROOT="$T/root_err" DSMP_NO_DIALOG=1 "$Q/Contents/MacOS/DS Modded" 2>&1); rce=$?
check "no Steam install: exit 1" test $rce -eq 1
check "no Steam install: notice logged" grep -q "notice (not shown): Dungeon Settlers is not installed by Steam" "$FH/Library/Logs/DungeonSettlersModded.log"

echo "first_prepare_seconds=$FIRST second_prepare_seconds=$SECOND" > "$T/times.txt"
echo "== $fails failure(s); files in $T"
exit $(( fails > 0 ))
