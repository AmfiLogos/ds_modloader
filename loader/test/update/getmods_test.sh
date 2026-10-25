#!/bin/zsh
# "Get the host's mods" safety test (loader side, no game): the real dsl_hostmods.c + dsl_update.c + Mac install code
# against a local release server (relserver.py). The breaks it catches: a mod that is not one of ours is downloaded; a
# download with a wrong sha256 is installed; a failed fix (no release for the host's older version, Apply refused)
# leaves the guest's mods changed; the plan names the wrong action for a missing / off / older / newer / extra mod.
# usage: test/update/getmods_test.sh   (output in a new temp folder)
set -u; cd "${0:A:h}"; H="$PWD"; L="${H:h:h}"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/getmods_test.XXXXXX"); R=$OUT/root; REL=$OUT/rel; M=$R/loader/mods
mkdir -p $M $REL $OUT/zips
fails=0; pass=0
check() { if eval "$2"; then print "PASS $1"; pass=$((pass+1)); else print "FAIL $1"; fails=$((fails+1)); fi }
nice -n 19 clang -arch arm64 -O1 -Wall -Wno-unused-function -DDSL_SIGN_DEV_KEY -I$L/src -o $OUT/t getmods_test.c $L/src/dsl_hostmods.c $L/src/dsl_update.c $L/src/dsl_sig.c $L/src/dsl_sig_mc.c \
  $L/src/dsl_json.c $L/start/mac/dsl_update_mac.c || exit 1

modjson() { print -r -- "{\"id\":\"$1\",\"name\":\"$1 name\",\"version\":\"$2\",\"scope\":\"${3:-gameplay}\"}"; }
inst() { mkdir -p $M/$1; modjson $1 $2 ${3:-gameplay} > $M/$1/mod.json; }
# release <repo> <id> <ver> [BAD_SHA]: tag <id>-v<ver>, asset <id>-<ver>.zip holding <id>/mod.json
release() {
  local s=$OUT/zips/$2-$3; mkdir -p $s/$2; modjson $2 $3 > $s/$2/mod.json
  local d=$REL/$1/$2-v$3; mkdir -p $d; (cd $s && zip -qr $d/$2-$3.zip $2); [[ -n ${4:-} ]] && touch $d/$4
}
inst hell-difficulty 0.1.4; inst custom-modes 0.2.0; inst foo 1.0.0; inst multiplayer 0.1.13; inst comfy 1.0.0 client
mkdir -p $OUT/zips/gcd-cdr-0.1.4/gcd-cdr $REL/AmfiLogos/ds_modloader/gcd-cdr-v0.1.4     # with names per language
print -r -- '{"id":"gcd-cdr","name":"Global cooldown fix","version":"0.1.4","scope":"gameplay","i18n":{"fr":{"name":"Correctif du temps de recharge global"}}}' > $OUT/zips/gcd-cdr-0.1.4/gcd-cdr/mod.json
(cd $OUT/zips/gcd-cdr-0.1.4 && zip -qr $REL/AmfiLogos/ds_modloader/gcd-cdr-v0.1.4/gcd-cdr-0.1.4.zip gcd-cdr)
release AmfiLogos/ds_modloader gcd-cdr 0.1.5 BAD_SHA
release AmfiLogos/ds_modloader gcd-cdr 0.1.6
release AmfiLogos/ds_custommodes custom-modes 0.2.0
mkdir -p $REL/evil/evil/evil-v1.0.0; print x > $REL/evil/evil/evil-v1.0.0/evil-1.0.0.zip

PORT=$((20000 + RANDOM % 20000))
nice -n 19 clang -O2 -o $OUT/dsl_sign ${L:h}/tools/release_sign/dsl_sign.c $L/src/dsl_sig.c $L/src/dsl_sig_mc.c || exit 1   # releases signed with the dev key
DSL_SIGN_TOOL=$OUT/dsl_sign python3 $H/relserver.py $REL $PORT & SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
for i in {1..50}; do curl -s -o /dev/null http://127.0.0.1:$PORT/repos/x/y/releases && break; sleep 0.1; done
[[ -f $REL/requests.log ]] && mv $REL/requests.log $OUT/probe.log    # the readiness probe is not a loader request
export DSML_ROOT=$R DSL_UPDATE=0 DSL_UPDATE_API=http://127.0.0.1:$PORT HM_ON=",custom-modes,foo,multiplayer,comfy," HM_EN=",custom-modes,foo,multiplayer,comfy,hell-difficulty,"
host() { print -r -- "$1" > $OUT/host.txt; }
ver() { sed -n 's/.*"version":"\([^"]*\)".*/\1/p' $M/$1/mod.json 2>/dev/null; }
reqs() { cat $REL/requests.log 2>/dev/null; }

# 1 plan: every kind of row; a mod that is not ours = not_official, button off
host $'gcd-cdr@0.1.4:aa\nhell-difficulty@0.1.4\ncustom-modes@0.1.9\nevil@1.0.0\nmultiplayer@0.1.13'
p=$($OUT/t plan $OUT/host.txt 0 2>/dev/null)
act() { print -r -- "$p" | python3 -c "import json,sys; d=json.loads(sys.stdin.read().split('\n',1)[1]); print({r['id']:r['action'] for r in d['rows']}.get('$1'), d['can_fix'])"; }
check "plan: missing official mod = download" '[[ $(act gcd-cdr) == "download 0" ]]'
check "plan: installed but off = turn_on" '[[ $(act hell-difficulty) == "turn_on 0" ]]'
check "plan: mine newer than the host = host_older" '[[ $(act custom-modes) == "host_older 0" ]]'
check "plan: not one of ours = not_official, can_fix 0" '[[ $(act evil) == "not_official 0" ]]'
check "plan: my extra gameplay mod = turn_off; multiplayer and client mods never rows" '[[ $(act foo) == "turn_off 0" && $(act multiplayer) == "None 0" && $(act comfy) == "None 0" ]]'
host $'hell-difficulty@0.1.5\ncustom-modes@0.2.0\nfoo@1.0.0'
p=$($OUT/t plan $OUT/host.txt 0 2>/dev/null); check "plan: mine older = update, equal and on = same, button on" '[[ $(act hell-difficulty) == "update 1" && $(act custom-modes) == "same 1" ]]'

# 1b a mod I do not have: its name from the official release's mod.json, in the player's language (never the raw id)
host $'gcd-cdr@0.1.4\nevil@1.0.0'
p=$(HM_LANG=fr $OUT/t plan $OUT/host.txt 0 wait 2>/dev/null)
nm() { print -r -- "$p" | python3 -c "import json,sys; d=json.loads(sys.stdin.read().split('\n',1)[1]); print({r['id']:r['name'] for r in d['rows']}.get('$1'), d.get('names_pending'))"; }
check "plan: missing official mod named from its release (fr); not-official id: no request" '[[ "$(nm gcd-cdr)" == "Correctif du temps de recharge global 0" && "$(nm evil)" == "evil 0" && -z "$(reqs | grep -i evil)" ]]'
[[ -f $REL/requests.log ]] && mv $REL/requests.log $OUT/names.log

# 2 get with a mod that is not ours: refused before any request
host $'gcd-cdr@0.1.4\nevil@1.0.0'
r=$($OUT/t get $OUT/host.txt 'DSMP_REJOIN=ip|10.0.0.2|7777' 2>/dev/null)
check "not official: refused, nothing requested, nothing installed" '[[ "$r" == "GET -1 "* ]] && [[ -z "$(reqs)" ]] && [[ ! -e $M/gcd-cdr && ! -e $M/evil ]]'

# 3 the host's older version has no release: fails, nothing installed (gcd-cdr was downloaded first but not installed)
host $'gcd-cdr@0.1.4\nhell-difficulty@0.1.4\ncustom-modes@0.1.9'
r=$($OUT/t get $OUT/host.txt 'DSMP_REJOIN=ip|10.0.0.2|7777' 2>/dev/null)
check "host older, no release: failed, says the host should update, mods unchanged" '[[ "$r" == *"STATE -1"* && "$r" == *"The host should update it"* && ! -e $M/gcd-cdr && $(ver custom-modes) == 0.2.0 && "$r" != *APPLY* ]]'

# 4 wrong sha256: never installed, never kept under the asset name
host $'gcd-cdr@0.1.5'
r=$($OUT/t get $OUT/host.txt 'DSMP_REJOIN=ip|10.0.0.2|7777' 2>/dev/null)
check "wrong sha256: failed, not installed, no .zip kept" '[[ "$r" == *"STATE -1"* && ! -e $M/gcd-cdr && ! -e $R/update/hostmods/dl/gcd-cdr-0.1.5.zip && "$r" != *APPLY* ]]'

# 5 the fix: download + install + Apply with the host's ids + my client mods + multiplayer + the env line
host $'gcd-cdr@0.1.4\nhell-difficulty@0.1.4\ncustom-modes@0.2.0'
r=$($OUT/t get $OUT/host.txt 'DSMP_REJOIN=ip|10.0.0.2|7777' 2>/dev/null)
check "fix: installed at the host's version, Apply = host ids + client + multiplayer, foo off, env passed" \
  '[[ "$r" == *"STATE 3"* && $(ver gcd-cdr) == 0.1.4 && "$r" == *"APPLY gcd-cdr,hell-difficulty,custom-modes,"* && "$r" == *multiplayer* && "$r" == *comfy* && "$r" != *foo* && "$r" == *"ENV DSMP_REJOIN=ip|10.0.0.2|7777"* ]]'
check "only official release URLs were requested" '! reqs | grep -v -E "^GET /(repos/AmfiLogos/(ds_modloader|ds_custommodes)/releases|dl/AmfiLogos/(ds_modloader|ds_custommodes)/[a-z-]+-v[0-9.]+/([a-z-]+-[0-9.]+\.zip|release-manifest\.json(\.sig)?))[? ]" | grep -q .'

# 6 Apply refused after the install: rolled back (gcd-cdr back to 0.1.4)
export HM_ON=",custom-modes,gcd-cdr,hell-difficulty,multiplayer,comfy,"
host $'gcd-cdr@0.1.6\nhell-difficulty@0.1.4\ncustom-modes@0.2.0'
r=$(HM_APPLY_REFUSE=1 $OUT/t get $OUT/host.txt 'DSMP_REJOIN=ip|10.0.0.2|7777' 2>/dev/null)
check "Apply refused: install rolled back, the old version is back" '[[ "$r" == *"STATE -1"* && $(ver gcd-cdr) == 0.1.4 ]]'

# 7 mod settings lines in the host's text (lane modsettings): parsed, a settings row, the host's values taken before Apply
host $'gcd-cdr@0.1.4\ngcd-cdr/cdr@off\nhell-difficulty@0.1.4\ncustom-modes@0.2.0'
p=$(HM_MYSET='gcd-cdr/cdr@on' $OUT/t plan $OUT/host.txt 0 2>/dev/null)
check "settings: only the setting differs = settings row, button on" '[[ $(act gcd-cdr) == "settings 1" && $(act hell-difficulty) == "same 1" ]]'
r=$(HM_MYSET='gcd-cdr/cdr@on' $OUT/t get $OUT/host.txt 'DSMP_REJOIN=ip|10.0.0.2|7777' 2>/dev/null)
check "settings: the host's lines taken, then Apply" '[[ "$r" == *"TAKE gcd-cdr@0.1.4|gcd-cdr/cdr@off|"*"APPLY gcd-cdr,"* && "$r" == *"STATE 3"* ]]'
r=$(HM_MYSET='gcd-cdr/cdr@on' HM_APPLY_REFUSE=1 $OUT/t get $OUT/host.txt 'DSMP_REJOIN=ip|10.0.0.2|7777' 2>/dev/null)
check "settings: Apply refused = the taken values dropped" '[[ "$r" == *"TAKE "*"UNDO "* && "$r" == *"STATE -1"* ]]'

# 8 a mod with the host's version, on in the profile, that does not run here (the crash guard turned it off): the
# break = "Get the host's mods" soft-restarts for it (it is still off after the restart), the guest rejoins, is refused
# again, and again. It must be not_running with its reason, no button, and get must refuse before any Apply.
host $'gcd-cdr@0.1.4\nhell-difficulty@0.1.4\ncustom-modes@0.2.0'
export HM_ON=",custom-modes,hell-difficulty,multiplayer,comfy," HM_EN=",custom-modes,gcd-cdr,hell-difficulty,multiplayer,comfy," HM_FAIL=",gcd-cdr,"
p=$($OUT/t plan $OUT/host.txt 0 2>/dev/null)
check "on but not running (crash guard): not_running with its reason, button off" '[[ $(act gcd-cdr) == "not_running 0" ]] && print -r -- "$p" | grep -q "\"why\":\"it stopped the game while starting last time"'
r=$($OUT/t get $OUT/host.txt 'DSMP_REJOIN=ip|10.0.0.2|7777' 2>/dev/null)
check "on but not running: get refused with the name and the reason, no Apply (no soft restart, no rejoin)" '[[ "$r" == "GET -1 Global cooldown fix is on but did not start on this computer: it stopped the game"* && "$r" != *APPLY* ]]'
p=$(HM_DEP=",gcd-cdr," $OUT/t plan $OUT/host.txt 0 2>/dev/null)
check "on, not running for a missing requirement: still turn_on (the host's mods may bring it)" '[[ $(act gcd-cdr) == "turn_on 1" ]]'

print "== $pass passed, $fails failed (output $OUT)"
exit $fails
