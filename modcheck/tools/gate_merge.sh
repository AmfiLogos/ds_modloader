#!/bin/zsh
# Launcher lane copy of ext/core_next/tools/gate_merge.sh: builds src to build/merge_launcher.dylib (night rule 9) and gates it.
# rig/gate.sh, but the merged src is built to build/merge_launcher.dylib (night rule 9) and every test uses it.
set -u; cd ~/ds_multiplayer/rig
D=~/ds_multiplayer/build/merge_launcher.dylib; W=~/ds_multiplayer/ext/core_next/tools/run_with_dylib.py   # read-only use (corenext lane)
T=$(date +%Y%m%d-%H%M); L=det_out/gate_launcher_$T
sed 's|-o ../build/dsrig.dylib.tmp|-o ../build/merge_launcher.dylib.tmp|; s|codesign -f -s - ../build/dsrig.dylib.tmp|codesign -f -s - ../build/merge_launcher.dylib.tmp|; s|mv -f ../build/dsrig.dylib.tmp ../build/dsrig.dylib|mv -f ../build/merge_launcher.dylib.tmp ../build/merge_launcher.dylib|; s|echo built ../build/dsrig.dylib|echo built ../build/merge_launcher.dylib|' ../src/build.sh > ../src/.build_merge_launcher.sh
zsh ../src/.build_merge_launcher.sh > ${L}_build.log 2>&1; mv ../src/.build_merge_launcher.sh ~/ds_launcher/modcheck/work/old/build_merge_launcher_$T.sh
grep -q "^built" ${L}_build.log || { echo "BUILD FAILED"; exit 1; }
until PAIR=$(python3 rig.py lease launcher_gate 2>/dev/null | tail -1); [[ $PAIR == [A-Z][A-Z] ]]; do sleep 15; done   # wait for a slot (max 2 pairs)
echo "leased $PAIR $(date +%H:%M)"
export RIG_PAIR=$PAIR MP2_PAIR=$PAIR
trap 'python3 rig.py release $PAIR launcher_gate >/dev/null' EXIT
python3 $W $D rig.py smoke > ${L}_smoke.log 2>&1
s=$(grep -c "fp_tick_advanced': True" ${L}_smoke.log)
python3 $W $D mp2_test.py --ticks 3000 > ${L}_mp2.log 2>&1
p=$(grep -c " PASS " ${L}_mp2.log); f=$(grep -c " FAIL " ${L}_mp2.log); tb=$(grep -c Traceback ${L}_mp2.log)
python3 rig.py release $PAIR launcher_gate >/dev/null
python3 $W $D ../src/ui_test/left_prompt_pair.py --start > ${L}_leftprompt.log 2>&1
lp=$(grep -c "^PASS\| PASS " ${L}_leftprompt.log); lf=$(grep -c "^FAIL\| FAIL " ${L}_leftprompt.log); ltb=$(grep -c Traceback ${L}_leftprompt.log)
PAIR=$(python3 rig.py lease launcher_gate 2>/dev/null | tail -1)
pr=$(python3 rig.py check-prefs)
echo "gate_launcher $T: smoke $s/3; mp2 $p PASS, $f FAIL, tb $tb; left_prompt $lp PASS, $lf FAIL, tb $ltb; prefs $pr"
grep " FAIL " ${L}_mp2.log | cut -c1-200; grep "FAIL" ${L}_leftprompt.log | cut -c1-200
