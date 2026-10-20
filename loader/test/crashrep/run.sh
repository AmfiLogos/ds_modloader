#!/bin/zsh
# Unit test of the crash report scrubber and gzip (no game). The real samples are this Mac's own logs: read in place,
# never copied into the repo. CRASHREP_SAMPLES="file1 file2 ..." replaces the default list.
H=${0:A:h}; L=${H:h:h}; T=$(mktemp -d "${TMPDIR:-/tmp}/dsl_crashrep.XXXXXX")
clang -O1 -g -Wall -I$L/src -o $T/scrub_test $H/scrub_test.c $L/src/dsl_scrub.c $L/src/dsl_scrub_names.c $L/src/dsl_json.c \
  $L/start/mac/dsl_update_mac.c -lz || exit 1
P=~/Library/"Application Support"/DSModLauncher/profiles
S=(${=CRASHREP_SAMPLES})
if (( ! ${#S} )); then
  S=($P/*/dsload.log(N) $P/*/dsload.prev.log(N) $P/*/rig.log(N) $P/*/player.log(N) $P/*/stdout.log(N) $P/*/data/ErrorLog.txt(N)
     ~/Library/Logs/DungeonSettlersModded.log(N) ~/Library/"Application Support"/CrossOver/Bottles/*/drive_c/**/dsload.log(N[1,3]))
fi
$T/scrub_test "$P/modded" $S || exit 1
clang -O1 -g -Wall -I$L/src -o $T/bugrep_test $H/bugrep_test.c $L/src/dsl_bugrep_fmt.c $L/src/dsl_scrub.c $L/src/dsl_scrub_names.c \
  $L/src/dsl_json.c $L/start/mac/dsl_update_mac.c -lz || exit 1
$T/bugrep_test $T || exit 1
echo "== crash report scrub tests all ok"
