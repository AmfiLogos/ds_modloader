#!/bin/zsh
# app/test/steam/steam_check.sh - the start's Steam check (app/stub/ds_modded.c ensure_steam) on a fake libsteam_api.
# Break it catches: a Steam client that runs but does not answer games (its ipcserver restarted) was called
# "not running", Steam was "started" again and the player waited 90 s for a wrong message (2026-10-20).
# Never starts the real Steam: DSMP_STEAM_APP names an app that does not exist; the fake client is a tiny program.
# The check is built with STEAM_CLIENT_PROC=dsl_steam_osx, so it looks for the fake client only and runs while the
# owner's real Steam runs (before v0.1.22 it printed "skip" then and failed every gate run on this Mac).
set -u
HERE="${0:A:h}"; REPO="${HERE:h:h:h}"
T="$REPO/test_env/steam-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$T/Fake.app/Contents/PlugIns"
clang -arch arm64 -O2 -dynamiclib -o "$T/Fake.app/Contents/PlugIns/libsteam_api.dylib" "$HERE/fake_steam_api.c" || exit 2
clang -arch arm64 -O2 -w -mmacosx-version-min=11.0 -framework CoreFoundation -I"$REPO/loader/src" \
  -DSTEAM_CLIENT_PROC='"dsl_steam_osx"' -o "$T/check" "$HERE/steam_check_main.c" || exit 2
fails=0
ok()  { echo "ok   $*"; }
bad() { echo "FAIL $*"; fails=$((fails + 1)); }
run() { local name=$1; shift; env DSMP_NO_DIALOG=1 DSMP_STEAM_APP=dsl-no-such-app-steamcheck DSMP_STEAM_WAIT=3 "$@" \
          "$T/check" "$T/Fake.app" "$T/$name.log"; echo $? > "$T/$name.rc"; }
has() { grep -qF -- "$2" "$T/$1.log"; }

run healthy FAKE_RUN=1 FAKE_INIT=1
[[ $(cat $T/healthy.rc) == 0 && ! -s $T/healthy.log ]] && ok "Steam answers: game starts, nothing logged" || bad "healthy"

run absent FAKE_RUN=0
has absent "Steam is not running: starting" && has absent "could not be started" && ok "no Steam client: starts Steam" \
  || bad "absent: $(cat $T/absent.log)"

# a running client that games cannot reach
echo '#include <unistd.h>\nint main(void) { sleep(60); return 0; }' > "$T/fake_client.c"
clang -o "$T/dsl_steam_osx" "$T/fake_client.c" || exit 2
"$T/dsl_steam_osx" & SP=$!; sleep 0.3
run silent FAKE_RUN=0
has silent "Steam runs (steam_osx pid $SP)" && ! has silent "starting" && has silent "it does not answer" \
  && ok "running but silent Steam: not called 'not running', not started again, true message" || bad "silent: $(cat $T/silent.log)"
kill $SP 2>/dev/null; wait $SP 2>/dev/null
echo "$fails failure(s); logs in $T"
[[ $fails == 0 ]]
