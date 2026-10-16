#!/bin/zsh
# install_bepinex.sh <dest>   (dest = <launcher data>/bepinex/mac-arm64/<VER>)
# BepInEx 6 IL2CPP for Apple chips (lane bepinexmac), first use only: download our GitHub release zip, check its
# sha256, unzip with ditto (never sets quarantine), move into place. Already there = exit 0 at once.
# DSML_BEPINEX_ZIP=<local zip> replaces the download (tests); the sha256 check stays. DSML_LOCK_WAIT=<s> (default 300).
# Prints "BEPINEX <dest>" on success, "ERROR <message>" + exit 1 on failure. Never touches anything outside dest's parent.
VER=0.1.3
URL=https://github.com/AmfiLogos/ds_multiplayer/releases/download/bepinex-mac-$VER/bepinex-mac-arm64-$VER.zip
SHA=64f949bc87d6e1da8e230c356ff8780eacf06b0bc3563b0cf2245ba07c8c8890
D=${1:?usage: install_bepinex.sh <dest>}
[[ -f $D/libdoorstop.dylib && -f $D/BepInEx/core/BepInEx.Unity.IL2CPP.dll && -f $D/dotnet/libcoreclr.dylib ]] && { echo "BEPINEX $D"; exit 0; }
P=${D:h}; mkdir -p $P || { echo "ERROR cannot create $P"; exit 1; }
LOCK=$P/.install.lock
# one installer at a time (stub + game). The lock is ours only if OUR mkdir made it; only then the exit removes it.
OWN=0; trap '(( OWN )) && rmdir $LOCK 2>/dev/null' EXIT; trap 'exit 1' INT TERM HUP
WAIT=${DSML_LOCK_WAIT:-300}                                                   # seconds (tests set it short)
for i in {1..$(( WAIT * 2 ))}; do mkdir $LOCK 2>/dev/null && { OWN=1; break; }; sleep 0.5; done
(( OWN )) || { echo "ERROR another install is still running (waited ${WAIT} s for $LOCK). Try again later; if no install runs, remove that folder."; exit 1; }
[[ -f $D/libdoorstop.dylib && -f $D/dotnet/libcoreclr.dylib ]] && { echo "BEPINEX $D"; exit 0; }
T=$P/.tmp-$$; mkdir -p $T
if [[ -n $DSML_BEPINEX_ZIP ]]; then cp $DSML_BEPINEX_ZIP $T/b.zip || { echo "ERROR cannot read $DSML_BEPINEX_ZIP"; exit 1; }
else curl -fsSL --retry 2 --max-time 600 -o $T/b.zip "$URL" || { echo "ERROR download failed (no internet?): $URL"; mv $T $P/.failed-$$ 2>/dev/null; exit 1; }; fi
GOT=$(shasum -a 256 $T/b.zip | cut -c1-64)
[[ $GOT == $SHA ]] || { echo "ERROR download damaged (sha256 $GOT)"; mv $T $P/.failed-$$ 2>/dev/null; exit 1; }
ditto -x -k $T/b.zip $T/x || { echo "ERROR unzip failed"; exit 1; }
[[ -d $T/x/bepinex-mac-arm64-$VER ]] || { echo "ERROR zip layout not understood"; exit 1; }
[[ -e $D ]] && mv $D $P/.old-$(date +%Y%m%d-%H%M%S)                          # a half install is kept, never deleted
mv $T/x/bepinex-mac-arm64-$VER $D || { echo "ERROR cannot move into $D"; exit 1; }
mv $T $P/.done-$$                                                             # the zip stays aside (no rm)
echo "BEPINEX $D"
