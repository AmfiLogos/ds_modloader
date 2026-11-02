#!/bin/zsh
# "Get the host's mods" safety test (loader side, no game): the real dsl_hostmods.c + dsl_update.c + Mac install code
# against a local release server (relserver.py). The breaks it catches: a mod that is not one of ours is downloaded; a
# download with a wrong sha256 is installed; a failed fix (no release for the host's older version, Apply refused)
# leaves the guest's mods changed; the plan names the wrong action for a missing / off / older / newer / extra mod.
# Mod packs (section 9, the same helper): a pack code makes a wrong group or downloads a foreign mod; a damaged, newer or
# URL-carrying code is read; a failed import leaves mods changed; a second downloader appears in the loader.
# usage: test/update/getmods_test.sh   (output in a new temp folder)
set -u; cd "${0:A:h}"; H="$PWD"; L="${H:h:h}"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/getmods_test.XXXXXX"); R=$OUT/root; REL=$OUT/rel; M=$R/loader/mods
mkdir -p $M $REL $OUT/zips
fails=0; pass=0
check() { if eval "$2"; then print "PASS $1"; pass=$((pass+1)); else print "FAIL $1"; fails=$((fails+1)); fi }
nice -n 19 clang -arch arm64 -O1 -Wall -Wno-unused-function -DDSL_SIGN_DEV_KEY -DDSL_RELEASE=\"0.1.31\" -I$L/src -o $OUT/t getmods_test.c $L/src/dsl_hostmods.c $L/src/dsl_update.c $L/src/dsl_sig.c $L/src/dsl_sig_mc.c \
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

# 9 mod packs (dsl_pack_*): the same plan + get + rollback; the end saves a group and switches nothing. Each check uses a
# fresh copy of the game (mkroot) so the earlier sections' installs never decide a result.
export HM_ON=",custom-modes,gcd-cdr,multiplayer,comfy," HM_EN=",custom-modes,gcd-cdr,foo,multiplayer,comfy,"; unset HM_FAIL HM_DEP
mkroot() { export DSML_ROOT=$OUT/$1; M=$DSML_ROOT/loader/mods; mkdir -p $M; }
nreq=0; fresh_reqs() { [[ -f $REL/requests.log ]] && mv $REL/requests.log $OUT/reqs_pack.$((++nreq)).log; return 0; }
mkcode() { python3 -c 'import base64,sys,zlib; t=sys.argv[2].encode(); print(sys.argv[1]+"."+base64.b64encode(t).decode()+"."+"%08x"%zlib.crc32(t))' "${2:-DSPACK1}" "$1"; }
dec() { python3 -c 'import base64,sys; print(base64.b64decode(sys.argv[1].split(".")[1]).decode())' "$1"; }
grp() { print -r -- "$r" | python3 -c 'import sys; L=[l[6:] for l in sys.stdin.read().splitlines() if l.startswith("GROUP ")]; print("\n".join("%s|%s|%s"%(n,",".join(sorted(i.split(","))),s) for n,i,s in (x.split("|",2) for x in L)))'; }
nogroup() { ! print -r -- "$r" | grep -q '^GROUP '; }
pj() { print -r -- "$p" | python3 -c "import json,sys; d=json.loads(sys.stdin.read().split('\n',1)[1]); print(d.get('$1'))"; }
offreq() { ! reqs | grep -v -E "^GET /(repos/AmfiLogos/(ds_modloader|ds_custommodes)/releases|dl/AmfiLogos/(ds_modloader|ds_custommodes)/[a-z-]+-v[0-9.]+/([a-z-]+-[0-9.]+\.zip|release-manifest\.json(\.sig)?))[? ]" | grep -q .; }

# 9.1 round trip: a code of my gameplay mods + their settings + a name, imported where one is missing
mkroot pk_share; inst gcd-cdr 0.1.4; inst custom-modes 0.2.0; inst multiplayer 0.1.13; inst comfy 1.0.0 client
c=$(HM_MYSET='gcd-cdr/cdr@off|custom-modes/waves@3|comfy/zoom@2' $OUT/t packcode 'Friday night' gcd-cdr custom-modes multiplayer comfy 2>/dev/null)
print -r -- "${c#CODE }" > $OUT/pack1.txt
mkroot pk_friend; inst custom-modes 0.2.0; inst foo 1.0.0; inst multiplayer 0.1.13; inst comfy 1.0.0 client
fresh_reqs; r=$($OUT/t packget $OUT/pack1.txt 2>/dev/null)
# break: the code carries multiplayer / a client mod / no settings, the import misses the exact version, the group gets
# my extra gameplay mod (foo) or loses the settings or the name, something is switched (APPLY / TAKE), or a foreign URL is asked
check "pack round trip: code = gameplay mods + settings + name; import installs the exact version, saves that group, switches nothing" \
  '[[ "$(dec "${c#CODE }")" == $'"'"'kit 0.1.31\nname Friday night\ncustom-modes/waves@3\ncustom-modes@0.2.0\ngcd-cdr/cdr@off\ngcd-cdr@0.1.4'"'"' && "$r" == *"STATE 4"* && $(ver gcd-cdr) == 0.1.4 && "$(grp)" == "Friday night|comfy,custom-modes,gcd-cdr,multiplayer|custom-modes/waves@3;gcd-cdr/cdr@off" && "$r" != *APPLY* && "$r" != *TAKE* ]] && offreq'
# break: importing a pack named like one of my own groups (other mods) replaces my group
r=$(HM_GROUPS='Friday night|foo,multiplayer,comfy|' $OUT/t packget $OUT/pack1.txt 2>/dev/null)
check "pack named like one of my groups: saved as \"Friday night (2)\", mine not replaced" \
  '[[ "$r" == *"STATE 4"* && "$(grp)" == "Friday night (2)|comfy,custom-modes,gcd-cdr,multiplayer|custom-modes/waves@3;gcd-cdr/cdr@off" ]]'

# 9.2 break: a code naming a mod that is not ours makes a request, installs something or saves a group
mkroot pk_foreign; inst hell-difficulty 0.1.4; inst multiplayer 0.1.13
mkcode $'kit 0.1.31\nevil@1.0.0\nhell-difficulty@0.1.4' > $OUT/pack2.txt
fresh_reqs; p=$($OUT/t packplan $OUT/pack2.txt 2>/dev/null); r=$($OUT/t packget $OUT/pack2.txt 2>/dev/null)
check "pack with a foreign id: plan says not ours, can_fix 0; import refused, no request, nothing installed, no group" \
  '[[ "$(pj error)" == *"not one of the mod loader'"'"'s own mods"* && $(pj can_fix) == 0 && "$r" == "IMPORT -1 "* && -z "$(reqs)" && ! -e $M/evil ]] && nogroup'

# 9.3 break: a pack download with a wrong sha256 is installed, kept or saved as a group
mkroot pk_badsha; inst multiplayer 0.1.13
mkcode $'kit 0.1.31\ngcd-cdr@0.1.5' > $OUT/pack3.txt
r=$($OUT/t packget $OUT/pack3.txt 2>/dev/null)
check "pack, wrong sha256: failed, not installed, no .zip kept, no group" \
  '[[ "$r" == *"STATE -1"* && ! -e $M/gcd-cdr && ! -e $DSML_ROOT/update/hostmods/dl/gcd-cdr-0.1.5.zip ]] && nogroup'

# 9.4 break: a pack version with no release leaves mods changed (custom-modes is downloaded first, never installed)
mkroot pk_norel; inst gcd-cdr 0.1.4; inst multiplayer 0.1.13
mkcode $'kit 0.1.31\ncustom-modes@0.2.0\ngcd-cdr@0.1.9' > $OUT/pack4.txt
r=$($OUT/t packget $OUT/pack4.txt 2>/dev/null)
check "pack, a version with no release: failed, mods unchanged, no group" \
  '[[ "$r" == *"STATE -1"* && "$r" == *"is not available for download"* && $(ver gcd-cdr) == 0.1.4 && ! -e $M/custom-modes ]] && nogroup'

# 9.5 break: the group cannot be saved after a real download + install, and the new version stays installed
mkroot pk_nogroup; inst gcd-cdr 0.1.4; inst multiplayer 0.1.13
mkcode $'kit 0.1.31\ngcd-cdr@0.1.6' > $OUT/pack5.txt
r=$(HM_GROUP_REFUSE=1 $OUT/t packget $OUT/pack5.txt 2>/dev/null)
check "pack, group save refused after the install: rolled back, the old version is back, no group" \
  '[[ "$r" == *"STATE -1"* && "$r" == *"could not be saved"* && $(ver gcd-cdr) == 0.1.4 ]] && nogroup'

# 9.6 break: a damaged, cut, newer-format, newer-kit or URL-carrying code is read (a request, an install), or the player
# gets the wrong reason
mkroot pk_damaged; inst multiplayer 0.1.13
good=$(mkcode $'kit 0.1.31\ngcd-cdr@0.1.4')
# one base64 character changed so that the lines still read as a valid pack (another version): only the crc refuses it
flip=$(python3 -c '
import base64, re, sys
c = sys.argv[1]; pre, b, crc = c.split("."); A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
ok = re.compile(r"(kit [0-9][0-9A-Za-z.+-]*|[A-Za-z0-9._-]+@[0-9][0-9A-Za-z.+-]*)$")
def valid(x):
    try: t = base64.b64decode(x, validate=True).decode("ascii")
    except Exception: return False
    return all(ok.fullmatch(l) for l in t.split("\n"))
print(next(pre + "." + b[:i] + ch + b[i + 1:] + "." + crc for i in range(len(b) - 1, -1, -1) if b[i] != "=" for ch in A
           if ch != b[i] and valid(b[:i] + ch + b[i + 1:])))' "$good")
fresh_reqs; bad6=""
for cs in "damaged|$flip" "damaged|${good[1,$(( ${#good} / 2 ))]}" "newer mod loader. Update|$(mkcode $'kit 0.1.31\ngcd-cdr@0.1.4' DSPACK2)" \
          "newer mod loader (0.1.99). You have 0.1.31|$(mkcode $'kit 0.1.99\ngcd-cdr@0.1.4')" "damaged|$(mkcode $'kit 0.1.31\ngcd-cdr@0.1.4\nhttps://evil.example/x.zip')"; do
  want=${cs%%|*}; print -r -- "${cs#*|}" > $OUT/pack6.txt
  p=$($OUT/t packplan $OUT/pack6.txt 2>/dev/null); r=$($OUT/t packget $OUT/pack6.txt 2>/dev/null)
  [[ "$p" == "RET -1"* && "$(pj error)" == *"$want"* && "$r" == "IMPORT -1 "*"$want"* ]] || bad6="$bad6 [$want: ${cs#*|}]"
done
check "damaged / cut / DSPACK2 / newer kit / URL codes: refused by plan and import with the matching reason, no request${bad6:+:$bad6}" \
  '[[ -z "$bad6" && -z "$(reqs)" && ! -e $M/gcd-cdr ]]'

# 9.7 static guard (no build): only the one helper (src/dsl_hostmods.c), the update code (src/dsl_update.c) and the
# starters (start/**, not scanned) call the download / install functions. break: a second downloader in the loader.
guard=$(python3 -I - $L <<'PY'
import os, re, sys
L = sys.argv[1]
FN = re.compile(r'(?<![A-Za-z0-9_])(dsl_update_fetch|dsl_update_find_mod_release|dsl_uos_mod_install)\s*\(')
DECL = re.compile(r'(?:^|[;{}\n])\s*(?:(?:static|extern|inline)\s+)*(?:int|void|long|unsigned)\s+$')   # "int name(" = a declaration
ALLOW = {'src/dsl_hostmods.c', 'src/dsl_update.c'}
def strip(t):                                     # comments and string / char literals out (a comment naming one is no call)
    o, i, n = [], 0, len(t)
    while i < n:
        if t.startswith('//', i): j = t.find('\n', i); i = n if j < 0 else j
        elif t.startswith('/*', i): j = t.find('*/', i + 2); j = n if j < 0 else j + 2; o.append('\n' * t.count('\n', i, j)); i = j
        elif t[i] in '"\'':
            q, j = t[i], i + 1
            while j < n and t[j] != q and t[j] != '\n': j += 2 if t[j] == '\\' else 1
            o.append(q + q); i = j + 1
        else: o.append(t[i]); i += 1
    return ''.join(o)
bad = []
for d in ('src', 'ui', 'modules'):
    for root, _, fs in os.walk(os.path.join(L, d)):
        for f in fs:
            p = os.path.join(root, f); rel = os.path.relpath(p, L)
            if not f.endswith(('.c', '.m', '.mm', '.cc', '.cpp')) or rel in ALLOW: continue
            t = strip(open(p, encoding='utf-8', errors='replace').read())
            if any(not DECL.search(t[max(0, m.start() - 200):m.start()]) for m in FN.finditer(t)): bad.append('loader/' + rel)
print(' '.join(sorted(bad)))
PY
)
check "no second downloader: only dsl_hostmods.c / dsl_update.c / start call the download and install functions${guard:+ (other: $guard)}" '[[ -z "$guard" ]]'

print "== $pass passed, $fails failed (output $OUT)"
exit $fails
