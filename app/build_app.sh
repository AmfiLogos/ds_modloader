#!/bin/zsh
# build_app.sh -> app/build/Dungeon Settlers Modded.app (+ .zip). No game file inside: the app makes its own
# game copy from Steam's install on its first start (dsml-helper prepare).
#   Contents/MacOS/DS Modded        stub (app/stub/ds_modded.c)
#   Contents/MacOS/dsml-helper      core crate, release, arm64
#   Contents/Resources/AppIcon.icns app/icon/AppIcon.icns (make_icon.py)
#   Contents/Resources/dsml/        loader/build/dsload.dylib + mods/{multiplayer,custom-modes,gcd-cdr,hell-difficulty} + VERSION
#                                   (mods from loader/build/mods_all/mac: run loader/modules/build_mods.sh first)
set -e
HERE="${0:A:h}"; REPO="${HERE:h}"
OUTDIR="$HERE/build"; NAME="Dungeon Settlers Modded"; APP="$OUTDIR/$NAME.app"
MODS=(multiplayer custom-modes gcd-cdr hell-difficulty)
die() { echo "ERROR: $*" >&2; exit 1; }
[[ -f $REPO/loader/build/dsload.dylib ]] || die "no loader/build/dsload.dylib (build the loader first)"
[[ -f $HERE/icon/AppIcon.icns ]] || die "no app/icon/AppIcon.icns (run app/icon/make_icon.py)"
mkdir -p "$OUTDIR"

# 1. binaries
clang -arch arm64 -O2 -Wall -mmacosx-version-min=11.0 -framework CoreFoundation -I"$REPO/loader/src" \
  -o "$OUTDIR/ds_modded" "$HERE/stub/ds_modded.c"
# no build-machine paths in the shipped binary (panic locations hold the cargo registry and source paths)
(cd "$REPO" && RUSTFLAGS="--remap-path-prefix=$HOME=/src --remap-path-prefix=$HOME/.cargo=/cargo --remap-path-prefix=$HOME/.cargo/registry/src=crates --remap-path-prefix=$HOME/.rustup=rustup --remap-path-prefix=$REPO=dsml" \
  nice -n 19 cargo build -j 4 --release -p dsml-core --bin dsml-helper)
HELPER="$REPO/target/release/dsml-helper"
[[ -f $HELPER ]] || die "dsml-helper was not built"
strip -S -x "$HELPER"                                              # no debug info / local symbols (gate G12)
python3 -c "import sys; d=open(sys.argv[1],'rb').read().lower(); sys.exit(sys.argv[2].lower().encode() in d or sys.argv[3].lower().encode() in d)" "$HELPER" "$HOME" "$USER" || die "dsml-helper holds $HOME paths or the user name"
[[ $(lipo -archs "$HELPER") == arm64 ]] || die "dsml-helper is not arm64 (build on an Apple silicon Mac)"

# 2. bundle in a temp folder, then moved into place (old build -> app/old/)
TMP="$OUTDIR/$NAME.app.building"
[[ -e $TMP ]] && { mkdir -p "$HERE/old"; mv "$TMP" "$HERE/old/$NAME.app.building-$(date +%Y%m%d-%H%M%S)"; }
C="$TMP/Contents"
mkdir -p "$C/MacOS" "$C/Resources/dsml/mods"
cp "$OUTDIR/ds_modded" "$C/MacOS/DS Modded"
cp "$HELPER" "$C/MacOS/dsml-helper"
cp "$HERE/icon/AppIcon.icns" "$C/Resources/AppIcon.icns"
mkdir -p "$C/Resources/licenses"                                  # licenses/: our license + third-party notices, in every release file
cp "$REPO/licenses/LICENSE.txt" "$REPO/licenses/THIRD_PARTY_NOTICES.txt" "$C/Resources/licenses/"
cp "$REPO/loader/build/dsload.dylib" "$C/Resources/dsml/dsload.dylib"
cp "$HERE/bepinex/install_bepinex.sh" "$C/Resources/dsml/install_bepinex.sh"      # BepInEx at first use (kind "bepinex" mods)
cp "$HERE/melon/install_melon.sh" "$C/Resources/dsml/install_melon.sh"            # MelonLoader at first use (kind "melon" mods)
# the loader release (repo file RELEASE, 0.1.N): auto-update compares it with ds_modloader's v0.1.N releases (DSM_RELEASE overrides)
print -r -- "${DSM_RELEASE:-$(head -1 "$REPO/RELEASE")}" > "$C/Resources/dsml/RELEASE"
# the loader's texts (lang/<code>.json; the stub copies dsml/ to ROOT/loader/, so they end in ROOT/loader/lang/)
LJ=("$REPO"/loader/lang/*.json(N)); if (( ${#LJ} )); then mkdir -p "$C/Resources/dsml/lang"; cp "${LJ[@]}" "$C/Resources/dsml/lang/"; fi
for m in $MODS; do   # every shipped mod is required (one mod.json per module, mac-arm64 + win-x64 sha256s)
  [[ -d $REPO/loader/build/mods_all/mac/$m ]] || die "no loader/build/mods_all/mac/$m (run loader/modules/build_mods.sh)"
  cp -R "$REPO/loader/build/mods_all/mac/$m" "$C/Resources/dsml/mods/$m"
done

# every Mach-O: minimum macOS 12.0 or lower (dyld refuses a library newer than the running macOS)
for f in "$C/MacOS/DS Modded" "$C/MacOS/dsml-helper" ${(f)"$(find "$C/Resources/dsml" -type f)"}; do
  file -b "$f" | grep -q Mach-O || continue
  MINOS=$(xcrun vtool -show-build "$f" | awk '/minos/{print $2; exit}')
  if [[ ${MINOS%%.*} -gt 12 ]]; then
    xcrun vtool -set-build-version macos 12.0 ${MINOS} -replace -output "$f.v12" "$f" && mv "$f.v12" "$f"
    echo "${f:t}: minos $MINOS -> 12.0 (code unchanged)"
    [[ $f == "$C/Resources/dsml/mods/"* ]] && die "${f:t}: minos $MINOS needed a rewrite, so its bytes changed and its sha256 in mod.json / the mod-set no longer matches; rebuild it with -mmacosx-version-min=12.0"
  fi
done

# VERSION = sha256 of the dsml folder contents (path + file sha256, sorted)
(cd "$C/Resources/dsml" && find . -type f ! -name VERSION | LC_ALL=C sort | while read -r f; do
   print -r -- "$f $(shasum -a 256 "$f" | cut -c1-64)"; done | shasum -a 256 | cut -c1-64) > "$OUTDIR/VERSION.tmp"
mv "$OUTDIR/VERSION.tmp" "$C/Resources/dsml/VERSION"

cat > "$C/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>DS Modded</string>
  <key>CFBundleIdentifier</key><string>com.amfilogos.ds-modded</string>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleShortVersionString</key><string>$(head -1 "$REPO/RELEASE")</string>
  <key>CFBundleVersion</key><string>$(head -1 "$REPO/RELEASE")</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
plutil -lint "$C/Info.plist" >/dev/null

# 3. ad-hoc signatures inside-out
xattr -cr "$TMP"
find "$C/Resources/dsml" -type f | while read -r f; do
  file -b "$f" | grep -q Mach-O || continue
  # keep the published bytes when already validly signed: the loader and the MP mod-set check compare sha256
  codesign -v "$f" 2>/dev/null || codesign -f -s - "$f"
done
codesign -f -s - "$C/MacOS/dsml-helper"
codesign -f -s - "$C/MacOS/DS Modded"
codesign -f -s - "$TMP"
codesign -v --deep --strict "$TMP" || die "signature check failed"
# the shipped mods must be byte for byte the built ones (the loader and the multiplayer mod-set check their sha256)
for m in $MODS; do
  (cd "$REPO/loader/build/mods_all/mac/$m" && find . -type f | LC_ALL=C sort | while read -r f; do
     cmp -s "$f" "$C/Resources/dsml/mods/$m/$f" || die "mods/$m/${f#./} changed while the app was built (re-sign or minos rewrite): its sha256 no longer matches mod.json"
   done) || exit 1
done

if [[ -e $APP ]]; then mkdir -p "$HERE/old"; mv "$APP" "$HERE/old/$NAME-$(date +%Y%m%d-%H%M%S).app"; fi
mv "$TMP" "$APP"
codesign -v "$APP" && echo "signature ok"
Z="$OUTDIR/$NAME.zip"
[[ -e $Z ]] && { mkdir -p "$HERE/old"; mv "$Z" "$HERE/old/$NAME-$(date +%Y%m%d-%H%M%S).zip"; }
(cd "$OUTDIR" && nice -n 10 ditto -c -k --keepParent "$NAME.app" "$Z")
echo "built: $APP"
echo "zip:   $Z ($(du -h "$Z" | cut -f1))"
echo "dsml VERSION $(cat "$APP/Contents/Resources/dsml/VERSION")"
# gate G12: no user name anywhere in what ships (every file of the zip: bytes, UTF-8, UTF-16LE)
python3 "$REPO/loader/modules/scan_names.py" "$USER" "$Z" > "$OUTDIR/name_scan.txt" || { cat "$OUTDIR/name_scan.txt"; die "the user name is inside $Z"; }
echo "name scan: $(tail -1 "$OUTDIR/name_scan.txt")"
