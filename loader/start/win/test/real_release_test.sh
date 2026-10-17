#!/bin/zsh
# real_release_test.sh <version>: after a publish, the REAL GitHub release v<version> of ds_modloader is offered to an
# older test install in the CrossOver bottle DSMP (wine, graphics and audio off; update window hidden). No local server.
# KIT (default build/win/kit) is installed fresh with files\RELEASE = 0.1.6.99 (REAL_OLD) into the default folder (what was
# there goes aside). Checks: Off = the corner button "Update to <version>" and nothing downloaded; On = background download
# with sha256 ok, then the next launch installs it (starter.log) and the game reaches the title with it ("confirmed").
set -u; V=${1:?version}; T="${0:A:h}"; L="${T:h:h:h}"; D=${DSMP_ROOT:-~/ds_multiplayer}; W=$D/win
KIT=${KIT:-$L/build/win/kit}; OLDV=${REAL_OLD:-0.1.6.99}
C=~/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
DRV=~/"Library/Application Support/CrossOver/Bottles/DSMP/drive_c"
R="$DRV/users/crossover/AppData/Local/Dungeon Settlers Modded"; P="$R/profiles/modded"; U="$R/update"
WP='C:\users\crossover\AppData\Local\Dungeon Settlers Modded'
OLD="$DRV/users/crossover/AppData/Local/dsml_update_test_old/$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OLD"
fails=0
python3 $D/rig/wine_lease.py take realrelease --pid $$ --wait 540 || { print "wine busy"; exit 2; }
trap "python3 $D/rig/wine_lease.py release realrelease >/dev/null" EXIT
(cd $W && python3 -c 'import wloader as w, sys; sys.exit(0 if w.bottle_silent() else 1)') || exit 2
check() { if eval "$2"; then print "PASS $1"; else print "FAIL $1"; fails=$((fails+1)); fi }
title() { env DSL_UPDATE_WINDOW=hidden python3 $W/wloader.py start >/dev/null 2>&1
  for i in {1..30}; do sleep 6
    r=$(cd $W && python3 -c 'import wloader as w; print(w.cmd({"cmd":"status"},timeout=5).get("boot_ready"))' 2>/dev/null)
    [[ $r == True ]] && break
  done; }
waitlog() { for i in {1..40}; do grep -q -- "$1" "$P/dsload.log" 2>/dev/null && return 0; sleep 3; done; return 1 }

[[ -d $R ]] && mv "$R" "$OLD/install-before"
ST="$OLD/kit"; cp -R $KIT "$ST"; printf '%s\r\n' $OLDV > "$ST/files/RELEASE"
python3 $W/wloader.py install "$ST" | tail -1
check "installed test copy is $OLDV" '[[ $(head -1 "$R/RELEASE" | tr -d "\r ") == $OLDV ]]'

# Off: the button, nothing downloaded
mkdir -p "$U"; print '{"auto":false}' > "$U/settings.json"
title
check "off: corner button \"Update to $V\" (real release)" 'waitlog "corner button \"Update to $V\" shown"'
check "off: nothing downloaded" '[[ ! -e $U/ready.txt ]] && ! grep -q "downloaded, sha256 ok" "$P/dsload.log"'
python3 $W/wloader.py stop >/dev/null 2>&1

# On: background download, then the next launch installs it
print '{"auto":true}' > "$U/settings.json"
title
check "on: real kit downloaded, sha256 ok, waits for the next launch" 'waitlog "downloaded, sha256 ok" && waitlog "ready.txt written ($V"'
python3 $W/wloader.py stop >/dev/null 2>&1
title
check "on: next launch installed $V (starter.log), RELEASE $V" 'grep -q "v$V installed" "$P/starter.log" && [[ $(head -1 "$R/RELEASE" | tr -d "\r ") == $V ]]'
check "on: the game reaches the title with it (confirmed)" '[[ $r == True ]] && waitlog "v$V confirmed at the title"'
python3 $W/wloader.py stop >/dev/null 2>&1
print "== $fails failed"; exit $(( fails > 0 ))
