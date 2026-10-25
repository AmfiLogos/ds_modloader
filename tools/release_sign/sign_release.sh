#!/bin/zsh
# sign_release.sh - sign one GitHub release for the loader's signed auto-update (loader/src/dsl_sig.c + dsl_update.c).
# A loader of v0.1.16 or later installs a kit or mod release ONLY when it carries release-manifest.json and
# release-manifest.json.sig, signed by a key it trusts. Run this for EVERY release (the kit v<ver>, and each mod release
# <id>-v<ver> of ds_modloader, ds_multiplayer, ds_custommodes) before the upload:
#
#   tools/release_sign/sign_release.sh <repo> <tag> <file>...
#       e.g. AmfiLogos/ds_modloader v0.1.16 Dungeon-Settlers-Modded-Mac.dmg Dungeon-Settlers-Modded-Windows.zip Dungeon-Settlers-Modded-Setup.exe
#            AmfiLogos/ds_multiplayer multiplayer-v0.1.12 multiplayer-0.1.12.zip
#       Writes release-manifest.json + release-manifest.json.sig into OUT (default: the first file's folder). Upload both
#       with the release's files: gh release create <tag> <files> <OUT>/release-manifest.json <OUT>/release-manifest.json.sig
#   tools/release_sign/sign_release.sh --existing <repo> <tag>
#       back-sign a release that is already published: downloads its files into OUT (default ./signed-<tag>), checks each
#       against GitHub's sha256 digest, signs. Prints the upload command; uploads nothing.
#   tools/release_sign/sign_release.sh --keygen
#       once: makes the main and spare keys in the Keychain (refuses when one exists) and prints the two PUBLIC keys
#       for loader/src/dsl_sig.c.
#
# The key: macOS Keychain, service ds-modloader-release-signing, account ed25519 (KEY_ACCOUNT=ed25519-spare signs with
# the spare). It only goes from the Keychain through a pipe into dsl_sign: never on disk, in an argument or in a log.
# Checks: file names fit the tag (make_manifest.py); the new signature is checked again by the loader's own code against
# the keys the loader trusts (a key that is not in dsl_sig.c fails here); a kit whose loaders do not hold exactly the two
# release keys, or also trust the public dev key (a test build, -DDSL_SIGN_DEV_KEY), is refused (check_keys.py); a release
# with no final CHANGELOG.md entry, or a kit whose loaders lack it, is refused (tools/changelog/changelog.py check).
set -euo pipefail
H="${0:A:h}"; REPO="${H:h:h}"; SVC=ds-modloader-release-signing; ACCT=${KEY_ACCOUNT:-ed25519}
T=$(mktemp -d "${TMPDIR:-/tmp}/sign_release.XXXXXX")
SIGN=$T/dsl_sign
clang -O2 -Wall -o $SIGN $H/dsl_sign.c $REPO/loader/src/dsl_sig.c $REPO/loader/src/dsl_sig_mc.c

if [[ ${1:-} == --keygen ]]; then
  for a in ed25519 ed25519-spare; do
    if security find-generic-password -s $SVC -a $a >/dev/null 2>&1; then print -u2 "sign_release: Keychain $SVC / $a exists already: nothing made"; exit 1; fi
  done
  for a in ed25519 ed25519-spare; do
    $SIGN keygen | awk -v s=$SVC -v a=$a '{ print "add-generic-password -s " s " -a " a " -l \"DS mod loader release signing (" a ")\" -w " $0 }' | security -i >/dev/null
    print "$a public key: $(security find-generic-password -s $SVC -a $a -w | $SIGN pub)"
  done
  exit 0
fi

existing=0; [[ ${1:-} == --existing ]] && { existing=1; shift; }
(( $# >= 2 )) || { sed -n '2,25p' $0; exit 2; }
R=$1; TAG=$2; shift 2
if (( existing )); then
  OUT=${OUT:-$PWD/signed-$TAG}; mkdir -p $OUT
  gh release view $TAG -R $R --json assets -q '.assets[] | [.name, .digest] | @tsv' > $T/assets.tsv
  files=()
  while IFS=$'\t' read -r n d; do
    if [[ $TAG == v* ]]; then [[ $n == Dungeon-Settlers-Modded-(Mac.dmg|Mac.zip|Windows.zip|Setup.exe) ]] || continue
    else [[ $n == ${TAG%-v*}-${TAG##*-v}.zip ]] || continue; fi
    [[ $d == sha256:* ]] || { print -u2 "sign_release: $n has no GitHub digest: not signed"; exit 1; }
    gh release download $TAG -R $R -p $n -D $OUT --clobber
    [[ $(shasum -a 256 $OUT/$n | cut -d' ' -f1) == ${d#sha256:} ]] || { print -u2 "sign_release: $n differs from GitHub's digest: not signed"; exit 1; }
    files+=($OUT/$n)
  done < $T/assets.tsv
  set -- $files
else
  (( $# >= 1 )) || { print -u2 "sign_release: no files"; exit 2; }
  OUT=${OUT:-${1:A:h}}
fi

M=$OUT/release-manifest.json
python3 -I $H/make_manifest.py $R $TAG $T/release-manifest.json "$@"
# the release's CHANGELOG.md entry (owner 2026-10-25): none, still "Unreleased" or empty = not signed; a kit whose loaders
# do not hold its entry (the What's new window, loader/src/dsl_whatsnew.c) = not signed. CHANGELOG=<file> names the file;
# else ds_modloader = this tree's CHANGELOG.md, ds_multiplayer / ds_custommodes = their public main (clone ~/<repo>, fetched).
# Back-signing (--existing) skips it: those releases are out already.
if (( ! existing )); then
  CL=${CHANGELOG:-}
  if [[ -z $CL && $R == */ds_modloader ]]; then CL=$REPO/CHANGELOG.md
  elif [[ -z $CL ]]; then
    C=$HOME/${R#*/}
    { git -C $C fetch -q origin main && git -C $C show origin/main:CHANGELOG.md > $T/CHANGELOG.md; } || { print -u2 "sign_release: no CHANGELOG.md on the main of $R ($C): not signed"; exit 1; }
    CL=$T/CHANGELOG.md
  fi
  kb=(); [[ $TAG == v* ]] && for f in "$@"; do kb+=(--binary $f); done
  python3 -I $H/../changelog/changelog.py check --file $CL --tag $TAG $kb || { print -u2 "sign_release: the CHANGELOG entry of $TAG is missing or not final: not signed"; exit 1; }
fi
# a kit: every loader in its files must trust exactly the two release keys, never the public dev key (check_keys.py)
if [[ $TAG == v* ]]; then python3 -I $H/check_keys.py "$@" || { print -u2 "sign_release: kit loader keys wrong: not signed"; exit 1; }; fi
security find-generic-password -s $SVC -a $ACCT -w | $SIGN sign $T/release-manifest.json > $T/release-manifest.json.sig
$SIGN verify $T/release-manifest.json $T/release-manifest.json.sig > /dev/null
mv -f $T/release-manifest.json $T/release-manifest.json.sig $OUT/
print "signed $R $TAG ($#) file(s), key $ACCT: $M and $M.sig"
if (( existing )); then print "upload: gh release upload $TAG -R $R ${(q)M} ${(q)M}.sig"; fi
