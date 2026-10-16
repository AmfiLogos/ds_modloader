#!/bin/zsh
# Windows start part (review F1 + F2) in the CrossOver bottle DSMP (wine, not real Windows): graphics and audio off,
# one wine copy at a time (ds_multiplayer win/wloader.py). Needs the kit of build_win.sh (MODS=<multiplayer module>)
# installed with `wloader.py install`. Cases:
#   normal    DllMain only arms (loader lock held), start() runs once on the first il2cpp_init call with the lock free,
#             save folder redirected, modules started
#   redirfail DSL_TEST_REDIRECT_FAIL=1, data folder of its own: every module off, status file inactive, no module hook
#   samedir   DSL_TEST_REDIRECT_FAIL=1, data folder = the game's own (DSMODDED_GAME_DATA): mods stay on
#   late      late_ga_test.exe: baselib.dll loaded before GameAssembly.dll: armed from the DLL notification
set -u; T="${0:A:h}"; L="${T:h:h:h}"; D=${DSMP_ROOT:-~/ds_multiplayer}; W=$D/win
C=~/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
P=~/"Library/Application Support/CrossOver/Bottles/DSMP/drive_c/users/crossover/AppData/Local/Dungeon Settlers Modded/profiles/modded"
WP='C:\users\crossover\AppData\Local\Dungeon Settlers Modded'
fails=0
check() { if eval "$2"; then print "PASS $1"; else print "FAIL $1"; fails=$((fails+1)); fi }
run() {  # run <case> <env...>: one start, until the title (control port) or 80 s after mods-off
  local n=$1; shift
  env "$@" python3 $W/wloader.py start >/dev/null 2>&1
  for i in {1..30}; do sleep 6
    r=$(cd $W && python3 -c 'import wloader as w; print(w.cmd({"cmd":"status"},timeout=5).get("boot_ready"))' 2>/dev/null)
    [[ $r == True ]] && break
    (( i > 12 )) && grep -q "START CONDITION FAILED" "$P/dsload.log" 2>/dev/null && break
  done
  TITLE=$r; LOG=$(cat "$P/dsload.log"); STATUS=$(cat "$P/dsload_status.json")
  python3 $W/wloader.py stop >/dev/null 2>&1
}
has() { print -r -- "$LOG" | grep -q -- "$1" }

run normal DSL_X=0
check "normal: DllMain armed with the loader lock held" 'has "armed thread .* loader lock held: yes (GameAssembly already mapped at DllMain)"'
check "normal: start on the first il2cpp_init, loader lock free" 'has "start thread .* via il2cpp_init.*loader lock held: no"'
check "normal: start ran once" '[[ $(print -r -- "$LOG" | grep -c "start order: start thread") == 1 ]]'
check "normal: save folder redirected" 'has "save folder: .*profiles/modded/data"'
check "normal: module started, game at the title" 'has "modules: 1 loaded of 1 opened" && [[ $TITLE == True ]]'

run redirfail DSL_TEST_REDIRECT_FAIL=1
check "redirfail: mods off before any module hook" 'has "START CONDITION FAILED" && ! has "hooked .* for multiplayer" && ! has "early start done"'
check "redirfail: status file inactive with the message" 'print -r -- "$STATUS" | grep -q "\"active\":false,\"reason\":\"start_condition_failed\".*normal saves"'

run samedir DSL_TEST_REDIRECT_FAIL=1 "DSMODDED_GAME_DATA=$WP\\profiles\\modded\\data"
check "samedir: same folder, mods stay on" 'has "is the game.s own save folder" && has "modules: 1 loaded of 1 opened"'

~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang -O2 -Wall $T/late_ga_test.c -o $L/build/win/late_ga_test.exe || exit 1
out=$(cd $L/build/win && nice -n 19 $C/wine --bottle DSMP "Z:${PWD//\//\\}\\late_ga_test.exe" "$WP\\game" 2>/dev/null)
print -r -- "$out"
check "late: armed from the DLL notification" '[[ $out == *PASS* ]]'
print "== $fails failed"; exit $(( fails > 0 ))
