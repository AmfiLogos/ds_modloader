#!/bin/zsh
# Signed updates test (loader side, no game): the real src/dsl_update.c + dsl_sig.c + Mac download code (getmods_test.c
# driver) against a local release server (relserver.py), releases signed with the public dev key (the driver is built
# with -DDSL_SIGN_DEV_KEY). The breaks it catches: a kit or mod whose file was changed after signing, whose manifest was
# changed, that is signed by another key or not signed at all is downloaded for install; an old genuine signed manifest
# under a new tag is accepted (replay); the loader goes to a lower version (downgrade, also when a release was deleted);
# a valid signed update, or the bundle-era repair, is refused.
# usage: test/update/signed_test.sh   (output in a new temp folder)
set -u; cd "${0:A:h}"; H="$PWD"; L="${H:h:h}"; TOP="${L:h}"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/signed_test.XXXXXX"); fails=0; pass=0
check() { if eval "$2"; then print "PASS $1"; pass=$((pass+1)); else print "FAIL $1"; fails=$((fails+1)); fi }
nice -n 19 clang -arch arm64 -O1 -Wall -Wno-unused-function -DDSL_SIGN_DEV_KEY -I$L/src -o $OUT/t getmods_test.c $L/src/dsl_hostmods.c \
  $L/src/dsl_update.c $L/src/dsl_sig.c $L/src/dsl_sig_mc.c $L/src/dsl_json.c $L/start/mac/dsl_update_mac.c || exit 1
nice -n 19 clang -O2 -o $OUT/dsl_sign $TOP/tools/release_sign/dsl_sign.c $L/src/dsl_sig.c $L/src/dsl_sig_mc.c || exit 1
$OUT/dsl_sign keygen > $OUT/other.key                     # a key the loader does not trust
KIT=AmfiLogos/ds_modloader; KA=Dungeon-Settlers-Modded-Mac.zip
REL=$OUT/rel; n=0
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
DSL_SIGN_TOOL=$OUT/dsl_sign python3 -I $H/relserver.py $OUT/srv $PORT 2>$OUT/relserver.err & SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT

# a fresh case: empty release dir + ROOT with installed kit release $1 and mods "<id>@<ver>" ...
fresh() {
  n=$((n+1)); [[ -e $OUT/srv ]] && mv $OUT/srv $OUT/srv_old_$n; mkdir -p $OUT/srv
  R=$OUT/root$n; mkdir -p $R/loader/mods; print $1 > $R/loader/RELEASE; shift
  for m in "$@"; do mkdir -p $R/loader/mods/${m%@*}; print -r -- "{\"id\":\"${m%@*}\",\"name\":\"${m%@*}\",\"version\":\"${m#*@}\"}" > $R/loader/mods/${m%@*}/mod.json; done
}
# rel <repo> <tag> <file name>: a release with one random file (relserver signs it with the dev key)
rel() { local d=$OUT/srv/$1/$2; mkdir -p $d; head -c 3000 /dev/urandom > $d/$3; print $d; }
# own <dir> [key]: the test signs <dir>'s release itself now (relserver keeps it as it is)
own() {
  local d=$1 tag=${1:t} repo=${${1:h}#$OUT/srv/}
  python3 -I $TOP/tools/release_sign/make_manifest.py $repo $tag $d/release-manifest.json $d/$(ls $d | grep -v -E '^(release-manifest|KEEP_MANIFEST|NO_SIG)') || return 1
  $OUT/dsl_sign sign $d/release-manifest.json < ${2:-$TOP/dev_keys/dev.key} > $d/release-manifest.json.sig; touch $d/KEEP_MANIFEST
}
run() { env -i HOME=$OUT PATH=/usr/bin:/bin DSML_ROOT=$R DSL_UPDATE_API=http://127.0.0.1:$PORT $OUT/t check 2>$OUT/log$n; }
logn() { cat $OUT/log$n }

fresh 0.1.5; d=$(rel $KIT v0.1.6 $KA)
r=$(run); sha=$(shasum -a 256 $d/$KA | cut -d' ' -f1)
check "valid signed kit 0.1.6: downloaded for install, sha256 from the signed manifest" '[[ "$r" == *"KIT	0.1.6	"*"	$sha"* ]] && logn | grep -q "manifest signed by trusted key 2"'

fresh 0.1.5; d=$(rel $KIT v0.1.6 $KA); own $d; print -n x >> $d/$KA       # changed after signing (GitHub digest matches the new bytes)
r=$(run)
check "kit file changed after signing: not installed" '[[ "$r" == *"(none)"* ]] && logn | grep -q "sha256 .*the release says .*not installed"'

fresh 0.1.5; d=$(rel $KIT v0.1.6 $KA); own $d; head -c 3000 /dev/urandom > $d/$KA
sed -i '' "s/[0-9a-f]\{64\}/$(shasum -a 256 $d/$KA | cut -d' ' -f1)/" $d/release-manifest.json          # manifest edited to the new file
r=$(run)
check "manifest changed after signing: not installed" '[[ "$r" == *"(none)"* ]] && logn | grep -q "manifest signature is NOT valid"'

fresh 0.1.5; d=$(rel $KIT v0.1.6 $KA); own $d $OUT/other.key
r=$(run)
check "kit signed by a key the loader does not trust: not installed" '[[ "$r" == *"(none)"* ]] && logn | grep -q "manifest signature is NOT valid"'

fresh 0.1.5; d=$(rel $KIT v0.1.6 $KA); touch $d/NO_SIG
r=$(run)
check "kit without a signed manifest: not installed" '[[ "$r" == *"(none)"* ]] && logn | grep -q "no signed manifest"'

fresh 0.1.5; d=$(rel $KIT v0.1.4 $KA); own $d; e=$OUT/srv/$KIT/v0.1.7; mkdir -p $e   # the genuine 0.1.4 release, replayed as v0.1.7
cp $d/$KA $d/release-manifest.json $d/release-manifest.json.sig $e/; touch $e/KEEP_MANIFEST
r=$(run)
check "old signed manifest under a new tag (replay): not installed, never the lower 0.1.4" '[[ "$r" == *"(none)"* ]] && logn | grep -q "signed manifest is for .* v0.1.4 kit 0.1.4, not .* v0.1.7 kit 0.1.7"'

fresh 0.1.5; rel $KIT v0.1.4 $KA >/dev/null                                          # a genuine signed release, lower than installed
r=$(run)
check "kit downgrade (only a lower signed 0.1.4): not installed" '[[ "$r" == *"(none)"* ]]'

fresh 0.1.5 gcd-cdr@0.1.6; rel $KIT gcd-cdr-v0.1.5 gcd-cdr-0.1.5.zip >/dev/null        # 0.1.6's own release deleted on GitHub
r=$(run)
check "mod release of the installed version deleted: no downgrade to the older signed 0.1.5" '[[ "$r" == *"(none)"* ]] && ! grep -q "gcd-cdr-0.1.5.zip " <(cat $OUT/srv/requests.log)'

fresh 0.1.5 gcd-cdr@0.10.0; d=$(rel $KIT gcd-cdr-v0.1.5 gcd-cdr-0.1.5.zip)            # bundle-era number: repair still works
r=$(run)
check "bundle-era 0.10.0 mod: repaired to the newest signed release" '[[ "$r" == *"MOD	gcd-cdr	0.1.5	"*"	$(shasum -a 256 $d/gcd-cdr-0.1.5.zip | cut -d" " -f1)"* ]]'

# Get the host's mods: the exact version the host runs, unsigned = refused, never downloaded
fresh 0.1.5 gcd-cdr@0.1.6; d=$(rel $KIT gcd-cdr-v0.1.4 gcd-cdr-0.1.4.zip); touch $d/NO_SIG
print -r -- 'gcd-cdr@0.1.4' > $OUT/host.txt
r=$(env -i HOME=$OUT PATH=/usr/bin:/bin DSML_ROOT=$R DSL_UPDATE=0 DSL_UPDATE_API=http://127.0.0.1:$PORT HM_ON=",gcd-cdr," HM_EN=",gcd-cdr," $OUT/t get $OUT/host.txt x 2>$OUT/log$n)
check "Get the host's mods, release not signed: refused, the mod stays 0.1.6" '[[ "$r" == *"STATE -1"* ]] && grep -q "\"0.1.6\"" $R/loader/mods/gcd-cdr/mod.json && ! grep -q "gcd-cdr-0.1.4.zip " $OUT/srv/requests.log'

print "== $pass passed, $fails failed (output $OUT)"
(( fails == 0 ))
