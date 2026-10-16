#!/bin/zsh
# TEST ONLY: wait until no wine process and no wine.lock, then run bie_fail_test.py (log in build/run.log)
cd ${0:A:h}
for i in $(seq 1 120); do
  python3 bie_fail_test.py "$@" > build/run.log 2>&1
  grep -qE "^wine busy|FileExistsError" build/run.log || exit 0
  sleep 30
done
