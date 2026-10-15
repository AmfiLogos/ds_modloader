#!/bin/zsh
# Launcher lane night run: wait for a free pair (rig rules: max 2 leased), then weapon-slot 10 min (full-state check off)
# and the wrong-modset cases on the same pair, then give the pair back.
R=~/ds_multiplayer/rig/rig.py; L=~/ds_launcher; P=""
until P=$(python3 $R lease launcher 2>/dev/null) && [[ $P == [A-J][A-J] ]]; do
  [ -e ~/ds_multiplayer/rig/release.lock ] && echo "release.lock: waiting"; sleep 120; done
echo "leased $P at $(date +%H:%M)"
for x in ${(s::)P}; do [ -d $L/test_env/pair/$x/DungeonSettlers.app ] || { mkdir -p $L/test_env/pair/$x; src=E; [[ $x == ${P[2]} ]] && src=F; cp -c -R $L/test_env/pair/$src/DungeonSettlers.app $L/test_env/pair/$x/; }; done
python3 $L/tests/weaponslot_pair.py $P --minutes 10 --no-full-check > $L/test_env/pair/run.log 2>&1
tail -1 $L/test_env/pair/run.log | cut -c1-400
python3 $L/tests/modset_pair.py $P $L/modcheck/work/t/build/dsrig.dylib > $L/test_env/pair/modset_run.log 2>&1
grep "CASE\|RESULT" $L/test_env/pair/modset_run.log | cut -c1-600
python3 $R release $P
