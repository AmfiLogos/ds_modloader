#!/bin/zsh
# TEST ONLY: the loader exactly as lane L1's build.sh makes it (it compiles loader/ui/*.c) + the test module dsltest-win
# (adds a tab through the SDK). Output: ../../build/dsload.dylib and build/mods/dsltest-win. Windows compile check.
set -e; cd "${0:A:h}"; L=../..
LOADER_ONLY=1 $L/build.sh | tail -1
mkdir -p build/mods/dsltest-win
CF=(-O2 -Wall -mmacosx-version-min=12.0)
nice -n 19 clang -arch arm64 $CF -I$L/sdk -dynamiclib -install_name @rpath/dsltest-win.dylib -o build/mods/dsltest-win/dsltest-win.dylib.tmp dsltest-win/dsltest_win.c
codesign -f -s - build/mods/dsltest-win/dsltest-win.dylib.tmp 2>/dev/null; mv -f build/mods/dsltest-win/dsltest-win.dylib.tmp build/mods/dsltest-win/dsltest-win.dylib
cp dsltest-win/mod.json build/mods/dsltest-win/
echo "built build/mods/dsltest-win"
TC=~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
[[ -x $TC ]] && nice -n 19 $TC -O2 -Wall -Wextra -Wno-unused-parameter -I$L/src -c ../dsl_win.c -o build/dsl_win.win.o && echo "windows compile check of dsl_win.c: ok"
