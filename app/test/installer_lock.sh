#!/bin/zsh
# app/test/installer_lock.sh [installer...] - the install lock of app/bepinex/install_bepinex.sh and app/melon/install_melon.sh
# in a temp destination with a short deadline (DSML_LOCK_WAIT=2):
#   (a) a lock held by another process: the installer fails with a clear message, the lock and a partial install untouched
#   (b) an installer stopped (TERM) while it waits does not remove the other process's lock
#   (c) installed already: success at once, even with the lock held
#   (d) normal install: complete, its own lock removed, the partial install kept aside (.old-*)
#   (e) failure after the lock (damaged zip): ERROR, own lock removed, nothing installed
# Zips: DSML_BEPINEX_ZIP / DSML_MELON_ZIP, else the public release is downloaded once into test_env/zips.
# Prints PASS/FAIL per check; exit 1 on any FAIL. Tmp folders under test_env/ (gitignored, kept).
set -u
HERE="${0:A:h}"; REPO="${HERE:h:h}"
T="$REPO/test_env/lock-$(date +%Y%m%d-%H%M%S)-$$"; mkdir -p "$T" "$REPO/test_env/zips"
fails=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }

run_one() {   # $1 = installer path, $2 = zip env var, $3 = success word, $4 = completeness test (uses $D)
  local INST=$1 ZVAR=$2 WORD=$3 OKT=$4 name=${1:t:r}
  local URL=$(sed -n 's/^URL=//p' $INST) VER=$(sed -n 's/^VER=//p' $INST)
  URL=${URL//\$VER/$VER}
  local Z=${(P)ZVAR:-$REPO/test_env/zips/${URL:t}}
  [[ -f $Z ]] || curl -fsSL --retry 2 -o $Z "$URL" || { echo "FAIL $name: cannot get $URL"; fails=$((fails + 1)); return; }
  local P="$T/$name/mac-arm64" D L out st
  D="$P/$VER"; L="$P/.install.lock"
  mkdir -p "$D"; echo partial > "$D/partial.txt"

  # (a) lock held by "another installer" (this shell)
  mkdir "$L"
  local t0=$SECONDS
  out=$(env $ZVAR=$Z DSML_LOCK_WAIT=2 zsh $INST "$D" 2>&1); st=$?
  check "$name (a) held lock: exit 1" "(( st == 1 ))"
  check "$name (a) held lock: clear message" "[[ \$out == 'ERROR another install is still running'* ]]"
  check "$name (a) held lock: gave up near the deadline ($((SECONDS - t0)) s)" "(( SECONDS - t0 >= 2 && SECONDS - t0 <= 6 ))"
  check "$name (a) held lock: the other lock still there" "[[ -d \$L ]]"
  check "$name (a) held lock: partial install untouched" "[[ \$(cat \$D/partial.txt) == partial && \$(ls -A \$P | wc -l | tr -d ' ') == 2 ]]"

  # (b) stopped while waiting
  env $ZVAR=$Z DSML_LOCK_WAIT=30 zsh $INST "$D" >/dev/null 2>&1 &
  local pid=$!; sleep 1; kill -TERM $pid; wait $pid
  check "$name (b) stopped while waiting: the other lock still there" "[[ -d \$L ]]"

  # (d) normal install after the other installer is done
  rmdir "$L"
  out=$(env $ZVAR=$Z DSML_LOCK_WAIT=2 zsh $INST "$D" 2>&1); st=$?
  check "$name (d) install: exit 0 + '$WORD'" "(( st == 0 )) && [[ \$out == '$WORD '* ]]"
  check "$name (d) install: complete" "$OKT"
  check "$name (d) install: own lock removed" "[[ ! -e \$L ]]"
  check "$name (d) install: partial install kept aside" "[[ -f \$(ls -d \$P/.old-*(N[1]))/partial.txt ]]"

  # (c) installed already: success without the lock
  mkdir "$L"
  out=$(env $ZVAR=$Z DSML_LOCK_WAIT=2 zsh $INST "$D" 2>&1); st=$?
  check "$name (c) installed + lock held: exit 0 at once" "(( st == 0 )) && [[ -d \$L ]]"
  rmdir "$L"

  # (e) damaged zip in a fresh destination
  local D2="$T/$name-bad/mac-arm64/$VER" L2="$T/$name-bad/mac-arm64/.install.lock"
  echo junk > "$T/junk.zip"
  out=$(env $ZVAR="$T/junk.zip" DSML_LOCK_WAIT=2 zsh $INST "$D2" 2>&1); st=$?
  check "$name (e) damaged zip: exit 1 + ERROR" "(( st == 1 )) && [[ \$out == ERROR* ]]"
  check "$name (e) damaged zip: own lock removed, nothing installed" "[[ ! -e \$L2 && ! -e \$D2 ]]"
}

INSTALLERS=($REPO/app/bepinex/install_bepinex.sh $REPO/app/melon/install_melon.sh); (( $# )) && INSTALLERS=($@)
for I in $INSTALLERS; do
  case ${I:t} in
    install_bepinex.sh) run_one $I DSML_BEPINEX_ZIP BEPINEX '[[ -f $D/libdoorstop.dylib && -f $D/BepInEx/core/BepInEx.Unity.IL2CPP.dll && -f $D/dotnet/libcoreclr.dylib ]]' ;;
    install_melon.sh) run_one $I DSML_MELON_ZIP MELON '[[ -f $D/MelonLoader.Bootstrap.dylib && -f $D/MelonLoader/net6/MelonLoader.dll && -d $D/dotnet/host/fxr ]]' ;;
    *) echo "FAIL unknown installer $I"; fails=$((fails + 1)) ;;
  esac
done
echo "installer_lock: $fails failure(s) ($T)"
(( fails == 0 ))
