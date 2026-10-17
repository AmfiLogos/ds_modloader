#!/bin/zsh
# Windows start part (review F1 + F2) in the CrossOver bottle DSMP (wine, not real Windows): graphics and audio off,
# one wine copy at a time (ds_multiplayer win/wloader.py). Needs the kit of build_win.sh (MODS=<multiplayer module>)
# installs it itself (wloader.py install). Holds the shared one-wine-copy lock (rig/wine_lease.py) for the
# whole run; refuses when wine runs or the lock is taken. Cases:
#   normal    DllMain only arms (loader lock held), start() runs once on the first il2cpp_init call with the lock free,
#             save folder redirected, modules started
#   redirfail DSL_TEST_REDIRECT_FAIL=1, data folder of its own: every module off, status file inactive, no module hook
#   nonascii  game exe started directly with DSRIG_DIR under "JoséTest": the game writes into that folder (UTF-8 path)
#   late      late_ga_test.exe: baselib.dll loaded before GameAssembly.dll: armed from the DLL notification
set -u; T="${0:A:h}"; L="${T:h:h:h}"; D=${DSMP_ROOT:-~/ds_multiplayer}; W=$D/win
C=~/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
P=~/"Library/Application Support/CrossOver/Bottles/DSMP/drive_c/users/crossover/AppData/Local/Dungeon Settlers Modded/profiles/modded"
WP='C:\users\crossover\AppData\Local\Dungeon Settlers Modded'
fails=0
python3 $D/rig/wine_lease.py take winstart --pid $$ || { print "wine busy or the wine lock is taken"; exit 2; }
unlock() { python3 $D/rig/wine_lease.py release winstart >/dev/null }
trap unlock EXIT
check() { if eval "$2"; then print "PASS $1"; else print "FAIL $1"; fails=$((fails+1)); fi }
# install.bat installs only into a new, empty or own folder: an earlier test's leftovers (an uninstall keeps profiles\)
# go aside first (never deleted)
R=${P:h:h}
if [[ -d $R && ! -f $R/dsmodded_install.txt && ! -f "$R/Dungeon Settlers Modded.exe" ]]; then
  mkdir -p ${R:h}/old_winstart && mv $R ${R:h}/old_winstart/$(date +%Y%m%d-%H%M%S) && print "moved aside: $R (not an install of ours)"
fi
python3 $W/wloader.py install $L/build/win/kit | tail -1
check "install: starter and game copy in place" '[[ -f "$R/Dungeon Settlers Modded.exe" && -f $R/game/GameAssembly.dll ]]'
(( fails )) && { print "== $fails failed (install)"; exit 1; }
run() {  # run <case> <env...>: one start, until the title (control port) or 80 s after mods-off
  local n=$1; shift
  [[ -f $P/dsload.log ]] && mkdir -p $P/old && mv $P/dsload.log $P/old/dsload.$(date +%s).log   # no stale log is ever read
  env "$@" python3 $W/wloader.py start >/dev/null 2>&1
  for i in {1..30}; do sleep 6
    r=$(cd $W && python3 -c 'import wloader as w; print(w.cmd({"cmd":"status"},timeout=5).get("boot_ready"))' 2>/dev/null)
    [[ $r == True ]] && break
    (( i > 12 )) && grep -q "START CONDITION FAILED" "$P/dsload.log" 2>/dev/null && break
  done
  TITLE=$r; LOG=$(cat "$P/dsload.log" 2>/dev/null); STATUS=$(cat "$P/dsload_status.json" 2>/dev/null)
  python3 $W/wloader.py stop >/dev/null 2>&1
}
has() { print -r -- "$LOG" | grep -q -- "$1" }

run normal DSL_X=0
check "normal: DllMain armed with the loader lock held" 'has "armed thread .* loader lock held: yes (GameAssembly already mapped at DllMain)"'
check "normal: start on the first il2cpp_init, loader lock free" 'has "start thread .* via il2cpp_init.*loader lock held: no"'
check "normal: start ran once" '[[ $(print -r -- "$LOG" | grep -c "start order: start thread") == 1 ]]'
check "normal: save folder redirected" 'has "save folder: .*profiles/modded/data"'
check "normal: every module started, game at the title" 'print -r -- "$LOG" | command grep -qE "modules: ([1-9][0-9]*) loaded of \1 opened" && [[ $TITLE == True ]]'

run redirfail DSL_TEST_REDIRECT_FAIL=1
check "redirfail: mods off before any module hook" 'has "START CONDITION FAILED" && ! has "hooked .* for multiplayer" && ! has "early start done"'
check "redirfail: status file inactive with the message" 'print -r -- "$STATUS" | grep -q "\"active\":false,\"reason\":\"start_condition_failed\".*normal saves"'

BL=${P:h:h:h}; J="$BL/JoséTest"; [[ -d $J ]] && mkdir -p $L/build/win/old && mv "$J" $L/build/win/old/JoséTest.$(date +%s)
mkdir -p "$J/prof"; JW='C:\users\crossover\AppData\Local\JoséTest\prof'
env DSRIG_DIR="$JW" DSL_LOG_DIR="$JW" DSL_MODS_DIR="$WP\\mods" DSRIG_LIVE=1 DSL_NO_ALERT=1 SteamAppId=2798330 SteamGameId=2798330 \
    WINEDLLOVERRIDES="winhttp=n,b" nice -n 19 $C/wine --bottle DSMP --workdir "$WP\\game" --no-wait "$WP\\game\\DungeonSettlers.exe" -batchmode -nographics >/dev/null 2>&1
for i in {1..30}; do sleep 3; [[ -f "$J/prof/data/ErrorLog.txt" ]] && break; done
sleep 5; for p in $(ps -axo pid=,command= | command grep "DungeonSettlers.exe" | command grep -v grep | awk '{print $1}'); do kill -9 $p; done
check "nonascii: save folder logged as UTF-8" 'command grep -aq "save folder: C:/users/crossover/AppData/Local/JoséTest/prof/data" "$J/prof/dsload.log"'
check "nonascii: the game wrote into that folder" '[[ -f "$J/prof/data/ErrorLog.txt" ]]'

~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang -O2 -Wall $T/late_ga_test.c -o $L/build/win/late_ga_test.exe || exit 1
out=$(cd $L/build/win && nice -n 19 $C/wine --bottle DSMP "Z:${PWD//\//\\}\\late_ga_test.exe" "$WP\\game" 2>/dev/null)
print -r -- "$out"
check "late: armed from the DLL notification" '[[ $out == *PASS* ]]'
print "== $fails failed"; exit $(( fails > 0 ))
