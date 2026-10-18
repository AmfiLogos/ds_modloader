#!/bin/zsh
# Auto-update install in the Windows starter (start/dsl_update_start.c + dsl_update_win.c, the window in ds_modded_win.c)
# in the CrossOver bottle DSMP (wine, graphics and audio off), window made hidden (DSL_UPDATE_WINDOW=hidden). Holds
# ~/ds_multiplayer/rig/wine.lock for the whole run, like winstart_test.sh. Installs KIT (default: build/win/kit of
# build_win.sh, built with MODS=<multiplayer module>) fresh, then, with --dry starts (update steps run, no game):
#   b  RELEASE mismatch: refused, nothing changed, notice.txt
#   c  install fails after baselib / starter / RELEASE were swapped (a folder where the kit has mods\multiplayer\mod.json):
#      the exact old files back, notice.txt, ready.txt in update\old
#   d  pending.txt DONE + 1 STARTED: kept, 2 STARTED, label passed; then 2 STARTED: rolled back, KEY in bad.txt
#   g  a kit for another game build only (files\BUILDS.txt): refused twice, nothing changed, never in bad.txt (the
#      installer can make it fit later: not a failed install)
#   a  a good kit of a higher RELEASE: installed (RELEASE, baselib.dll + starter swapped, DONE), the new starter starts
#      and appends STARTED, DSML_UPDATE passed. Its BUILD.txt names another build, its BUILDS.txt lists the copy's build
#      second: the update install reads BUILDS.txt
#   e  one real start after a, to the title (control port boot_ready); the loader confirms there (pending.txt -> update\old)
#   x  (with a) a file only the old version of a kit mod had (mods\multiplayer\old_extra.dll) is gone after the update
#      (in the backup); the player's own mod (mods\player-own) stays
#   f  install.bat over the install (Setup.exe runs it too): the old mods\multiplayer goes whole to retired\ (in the
#      uninstall list), the player's own mod stays
# Kits of another RELEASE = the same kit with files\RELEASE changed (all that DSM_RELEASE changes in build_win.sh).
set -u; T="${0:A:h}"; L="${T:h:h:h}"; D=${DSMP_ROOT:-~/ds_multiplayer}; W=$D/win
KIT=${KIT:-$L/build/win/kit}
C=~/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
DRV=~/"Library/Application Support/CrossOver/Bottles/DSMP/drive_c"
R="$DRV/users/crossover/AppData/Local/Dungeon Settlers Modded"; P="$R/profiles/modded"; U="$R/update"
WP='C:\users\crossover\AppData\Local\Dungeon Settlers Modded'
OLD="$DRV/users/crossover/AppData/Local/dsml_update_test_old/$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OLD"
fails=0
LOCK=$D/rig/wine.lock
[[ -z $(ps -axo etime=,comm= | command grep -iE 'wine|\.exe$' | command grep -ve '-[0-9][0-9]:') ]] && mkdir $LOCK 2>/dev/null || { print "wine busy or $LOCK taken"; exit 2; }
print updatetest > $LOCK/owner
unlock() { mkdir -p $D/rig/old; mv $LOCK/owner $D/rig/old/wine.lock.owner.updatetest.$(date +%s) 2>/dev/null; rmdir $LOCK }
trap unlock EXIT
(cd $W && python3 -c 'import wloader as w, sys; sys.exit(0 if w.bottle_silent() else 1)') || exit 2
check() { if eval "$2"; then print "PASS $1"; else print "FAIL $1"; fails=$((fails+1)); fi }

# fresh install of the base kit into an empty folder: what earlier runs left there goes aside (install.bat refuses a
# folder holding files that are not an install of ours)
[[ -d $R ]] && mv "$R" "$OLD/install-before"
python3 $W/wloader.py install $KIT | tail -1
BASE=$(head -1 "$R/RELEASE" | tr -d '\r ')
print "base RELEASE $BASE"

# test kits: <ver> <RELEASE inside>
ST="$OLD/stage"; mkdir -p "$U/test"
mkzip() {  # mkzip <ver> <RELEASE inside> [<BUILD.txt line> <BUILDS.txt lines, space-separated>]
  local s="$ST/$1"; mkdir -p "$s"; cp -R $KIT "$s/Dungeon Settlers Modded"
  printf '%s\r\n' "$2" > "$s/Dungeon Settlers Modded/files/RELEASE"
  if (( $# > 2 )); then printf '%s\r\n' "$3" > "$s/Dungeon Settlers Modded/files/BUILD.txt"; printf '%s\r\n' ${=4} > "$s/Dungeon Settlers Modded/files/BUILDS.txt"; fi
  (cd "$s" && zip -qrX "$U/test/kit-$1.zip" "Dungeon Settlers Modded")
}
ready() {  # ready <ver>: ready.txt for update\test\kit-<ver>.zip
  local sha=$(shasum -a 256 "$U/test/kit-$1.zip" | cut -d' ' -f1)
  printf 'KIND\tkit\nLABEL\tv%s\nKEY\tkit %s\nKIT\t%s\t%s\t%s\n' $1 $1 $1 "$WP\\update\\test\\kit-$1.zip" $sha > "$U/ready.txt"
}
# what an install may change: RELEASE, the starter, baselib.dll, every path under mods (folders as DIR)
manifest() { python3 - "$R" <<'EOF'
import hashlib, os, sys
r = sys.argv[1]; out = []
def h(p): return hashlib.sha256(open(p, "rb").read()).hexdigest()
for f in ("RELEASE", "Dungeon Settlers Modded.exe", "game/baselib.dll"):
    p = os.path.join(r, f); out.append(f + " " + (h(p) if os.path.isfile(p) else "MISSING"))
for d, ds, fs in os.walk(os.path.join(r, "mods")):
    for x in sorted(ds): out.append(os.path.relpath(os.path.join(d, x), r) + " DIR")
    for x in sorted(fs): out.append(os.path.relpath(os.path.join(d, x), r) + " " + h(os.path.join(d, x)))
print("\n".join(sorted(out)))
EOF
}
dry() {  # one --dry start; LOG = its starter.log lines
  local n=$(wc -l < "$P/starter.log" 2>/dev/null || print 0)
  env DSL_UPDATE_WINDOW=hidden nice -n 19 $C/wine --bottle DSMP "$WP\\Dungeon Settlers Modded.exe" --dry >/dev/null 2>&1
  LOG=$(tail -n +$((n + 1)) "$P/starter.log" | tr -d "\r")   # the log is written in text mode (CRLF)
}
has() { print -r -- "$LOG" | grep -q -- "$1" }

COPYB=$(head -1 "$R/game/dsmodded_build.txt" | tr -d '\r ')
mkzip 0.1.91 0.1.90; mkzip 0.1.92 0.1.92; mkzip 0.1.93 0.1.93 1 "1 $COPYB"; mkzip 0.1.94 0.1.94 1 "1 2"

# b: RELEASE in the kit differs from the KIT version
M0=$(manifest); ready 0.1.91; dry
check "b: refused (RELEASE mismatch), nothing changed" 'has "says RELEASE 0.1.90" && [[ "$(manifest)" == "$M0" ]] && [[ ! -e $U/pending.txt ]]'
check "b: notice.txt, ready.txt in update/old" 'grep -q "0.1.91 could not be installed" "$U/notice.txt" && [[ ! -e $U/ready.txt ]] && ls "$U/old" | grep -q "installfailed-.*ready.txt"'

# c: a folder where the kit has mods\multiplayer\mod.json; the swap fails after baselib / starter / RELEASE
mv "$R/mods/multiplayer/mod.json" "$OLD/mod.json"; mkdir -p "$R/mods/multiplayer/mod.json/x"; print keep > "$R/mods/multiplayer/mod.json/x/f.txt"
M0=$(manifest); ready 0.1.92; dry
check "c: failed after the three swaps, rolled back with no failed move" 'has "mod.json is a folder" && print -r -- "$LOG" | grep -qE "rolled back ([3-9]|[1-9][0-9]+) recorded change\(s\), 0 failed"'
check "c: the exact old files" '[[ "$(manifest)" == "$M0" ]] && [[ ! -e $U/pending.txt ]]'
check "c: notice.txt, ready.txt in update/old" 'grep -q "0.1.92 could not be installed" "$U/notice.txt" && [[ ! -e $U/ready.txt ]] && [[ $(ls "$U/old" | grep -c "installfailed-.*ready.txt") == 2 ]]'
mv "$R/mods/multiplayer/mod.json" "$OLD/mod.json-folder"; mv "$OLD/mod.json" "$R/mods/multiplayer/mod.json"

# d: an installed update that crashed once (DONE + 1 STARTED), then a second time
print new > "$R/dtest.txt"; mkdir -p "$U/dbk"; print old > "$U/dbk/dtest.txt"
printf 'LABEL\tv0.1.99\nKEY\tkit 0.1.99\nFROM\t%s\nMOVE\t%s\t%s\nDONE\nSTARTED\n' $BASE "$WP\\dtest.txt" "$WP\\update\\dbk\\dtest.txt" > "$U/pending.txt"
dry
check "d1: one retry: STARTED appended, label passed" '[[ $(grep -c "^STARTED" "$U/pending.txt") == 2 ]] && has "ENV DSML_UPDATE=v0.1.99" && [[ $(cat "$R/dtest.txt") == new ]]'
dry
check "d2: started twice: rolled back, KEY in bad.txt, no label" '[[ $(cat "$R/dtest.txt") == old ]] && [[ ! -e $U/pending.txt ]] && grep -qx "kit 0.1.99" "$U/bad.txt" && has "ENV DSML_UPDATE=$"'
mv "$R/dtest.txt" "$OLD/dtest.txt"

# g: a kit only for other game builds, twice: refused, not counted as a failed install
M0=$(manifest); ready 0.1.94; dry; ready 0.1.94; dry
check "g: kit for other builds refused twice, nothing changed" 'has "is for game build(s) 1,2, the game copy is build $COPYB" && has "kit 0.1.94 is not for this game build: not counted" && [[ "$(manifest)" == "$M0" ]] && [[ ! -e $U/pending.txt ]]'
check "g: never in bad.txt or fails.txt, one line for the player" '! grep -qx "kit 0.1.94" "$U/bad.txt" 2>/dev/null && ! grep -qx "kit 0.1.94" "$U/fails.txt" 2>/dev/null && grep -q "0.1.94 could not be installed" "$U/notice.txt"'

# x: a file only the old multiplayer had, and a player's own mod
own() { mkdir -p "$R/mods/player-own"; print '{"id":"player-own","name":"Mine","version":"1.0.0"}' > "$R/mods/player-own/mod.json"; print mine > "$R/mods/player-own/data.txt" }
own; print old > "$R/mods/multiplayer/old_extra.dll"

# a: a good kit of a higher RELEASE
ready 0.1.93; dry
NP=$(print -r -- "$LOG" | sed -n 's/.*new kit installed: started its starter (pid \([0-9]*\)).*/\1/p')
check "a: installed: RELEASE 0.1.93, baselib.dll + starter swapped, DONE" '[[ $(head -1 "$R/RELEASE" | tr -d "\r ") == 0.1.93 ]] && ls "$U"/backup-$BASE-*/game/baselib.dll "$U"/backup-$BASE-*/"Dungeon Settlers Modded.exe" >/dev/null 2>&1 && grep -q "MOVE.*game.baselib.dll" "$U/pending.txt" && grep -qx DONE "$U/pending.txt" && [[ ! -e $U/ready.txt ]]'
check "a: the new starter ran: STARTED, DSML_UPDATE=v0.1.93" '[[ -n $NP ]] && has "---- start pid $NP," && [[ $(grep -c "^STARTED" "$U/pending.txt") == 1 ]] && has "ENV DSML_UPDATE=v0.1.93"'
check "x: the old multiplayer file is gone (backup), the player's own mod stays" '[[ ! -e $R/mods/multiplayer/old_extra.dll ]] && ls "$U"/backup-$BASE-*/mods/multiplayer/old_extra.dll >/dev/null 2>&1 && [[ $(cat "$R/mods/player-own/data.txt") == mine ]] && [[ -f $R/mods/multiplayer/mod.json ]]'
mv "$R/mods/player-own" "$OLD/player-own-a"   # not part of the real start below

# e: a real start (no --dry) to the title with the swapped files
env DSL_UPDATE_WINDOW=hidden python3 $W/wloader.py start >/dev/null 2>&1
for i in {1..30}; do sleep 6
  r=$(cd $W && python3 -c 'import wloader as w; print(w.cmd({"cmd":"status"},timeout=5).get("boot_ready"))' 2>/dev/null)
  [[ $r == True ]] && break
done
sleep 5; python3 $W/wloader.py stop >/dev/null 2>&1
check "e: the updated install reaches the title" '[[ $r == True ]] && grep -q "v0.1.93 starts (start 2 of 2" "$P/starter.log"'
check "e: confirmed at the title (pending.txt to update/old)" '[[ ! -e $U/pending.txt ]] && ls "$U/old" | grep -q "^ok-.*pending.txt" && grep -q "v0.1.93 confirmed at the title" "$P/dsload.log"'

# f: install.bat over the install: each kit mod folder replaced whole
own; print old > "$R/mods/multiplayer/old_extra2.dll"
python3 $W/wloader.py install $KIT | tail -1
check "f: install.bat: the old multiplayer folder whole in retired, no old file left, the player's own mod stays" '[[ ! -e $R/mods/multiplayer/old_extra2.dll ]] && ls "$R"/retired/mods-multiplayer-*/old_extra2.dll >/dev/null 2>&1 && [[ -f $R/mods/multiplayer/mod.json ]] && [[ $(cat "$R/mods/player-own/data.txt") == mine ]]'
check "f: the retired folder is in the uninstall list" 'iconv -f UTF-16LE -t UTF-8 "$R/install_files.log" | grep -q "retired.mods-multiplayer-.*old_extra2.dll"'
mv "$R/mods/player-own" "$OLD/player-own-f"
print "== $fails failed"; exit $(( fails > 0 ))
