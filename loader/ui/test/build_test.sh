#!/bin/zsh
# TEST ONLY: a test dsload = this branch's loader/src + lane L1's working dsl_ui.c (snapshot) + ../dsl_win.c + win_glue.c,
# and the starter module dsltest-win. Output in build/. Windows compile check of ../dsl_win.c (llvm-mingw).
set -e; cd "${0:A:h}"; L=../..
mkdir -p build/l1snap build/mods/dsltest-win
[[ -n "$KEEP_SNAP" ]] || cp ~/ds_launcher_l1core/loader/src/dsl_ui.c build/l1snap/dsl_ui.c
CF=(-O2 -Wall -Wno-unused-function -mmacosx-version-min=12.0)
nice -n 19 clang -arch arm64 $CF -I$L/src -I$L/sdk -dynamiclib -framework CoreFoundation -framework CoreGraphics -lobjc -install_name @rpath/dsload.dylib \
  -o build/dsload.dylib.tmp $L/src/dsl_main.c $L/src/dsl_engine.c $L/src/dsl_il2cpp.c $L/src/dsl_json.c $L/src/dsl_world.c $L/src/dsl_soft.c \
  build/l1snap/dsl_ui.c ../dsl_win.c win_glue.c
codesign -f -s - build/dsload.dylib.tmp 2>/dev/null; mv -f build/dsload.dylib.tmp build/dsload.dylib
nice -n 19 clang -arch arm64 $CF -I$L/sdk -dynamiclib -install_name @rpath/dsltest-win.dylib -o build/mods/dsltest-win/dsltest-win.dylib.tmp dsltest-win/dsltest_win.c
codesign -f -s - build/mods/dsltest-win/dsltest-win.dylib.tmp 2>/dev/null; mv -f build/mods/dsltest-win/dsltest-win.dylib.tmp build/mods/dsltest-win/dsltest-win.dylib
cp dsltest-win/mod.json build/mods/dsltest-win/
echo "built build/dsload.dylib + build/mods/dsltest-win"
TC=~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
[[ -x $TC ]] && nice -n 19 $TC -O2 -Wall -Wextra -Wno-unused-parameter -I$L/src -c ../dsl_win.c -o build/dsl_win.win.o && echo "windows compile check of dsl_win.c: ok"
