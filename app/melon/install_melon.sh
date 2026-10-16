#!/bin/zsh
# install_melon.sh <dest>   (dest = <launcher data>/melonloader/mac-arm64/<VER>)
# MelonLoader for Apple chips (lane melon: MelonLoader alpha + our arm64 Il2CppInterop), first use only: download our
# GitHub release zip, check its sha256, unzip with ditto (never sets quarantine), move into place. Already there = exit 0.
# DSML_MELON_ZIP=<local zip> replaces the download (tests); the sha256 check stays.
# Prints "MELON <dest>" on success, "ERROR <message>" + exit 1 on failure. Never touches anything outside dest's parent.
VER=0.1.0
URL=https://github.com/AmfiLogos/ds_multiplayer/releases/download/melonloader-mac-$VER/melonloader-mac-arm64-$VER.zip
SHA=f0f2a6e6d13736a96ea66376bdf98badc874ec269b091dd5ca6e9501c1a4643e
D=${1:?usage: install_melon.sh <dest>}
ok() { [[ -f $D/MelonLoader.Bootstrap.dylib && -f $D/MelonLoader/net6/MelonLoader.dll && -d $D/dotnet/host/fxr ]]; }
ok && { echo "MELON $D"; exit 0; }
P=${D:h}; mkdir -p $P || { echo "ERROR cannot create $P"; exit 1; }
LOCK=$P/.install.lock
for i in {1..600}; do mkdir $LOCK 2>/dev/null && break; sleep 0.5; done      # one installer at a time (stub + game)
trap 'rmdir $LOCK 2>/dev/null' EXIT
ok && { echo "MELON $D"; exit 0; }
T=$P/.tmp-$$; mkdir -p $T
if [[ -n $DSML_MELON_ZIP ]]; then cp $DSML_MELON_ZIP $T/m.zip || { echo "ERROR cannot read $DSML_MELON_ZIP"; exit 1; }
else curl -fsSL --retry 2 --max-time 900 -o $T/m.zip "$URL" || { echo "ERROR download failed (no internet?): $URL"; mv $T $P/.failed-$$ 2>/dev/null; exit 1; }; fi
GOT=$(shasum -a 256 $T/m.zip | cut -c1-64)
[[ $GOT == $SHA ]] || { echo "ERROR download damaged (sha256 $GOT)"; mv $T $P/.failed-$$ 2>/dev/null; exit 1; }
ditto -x -k $T/m.zip $T/x || { echo "ERROR unzip failed"; exit 1; }
[[ -d $T/x/melonloader-mac-arm64-$VER ]] || { echo "ERROR zip layout not understood"; exit 1; }
[[ -e $D ]] && mv $D $P/.old-$(date +%Y%m%d-%H%M%S)                          # a half install is kept, never deleted
mv $T/x/melonloader-mac-arm64-$VER $D || { echo "ERROR cannot move into $D"; exit 1; }
mv $T $P/.done-$$                                                             # the zip stays aside (no rm)
echo "MELON $D"
