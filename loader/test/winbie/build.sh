#!/bin/zsh
# TEST ONLY: bie_fail_test.exe = start/win/bie_win.c + the test driver (llvm-mingw). Output: build/
set -e; cd "${0:A:h}"; W=../../start/win; ZL=${DSMP_ROOT:-~/ds_multiplayer}/win/third_party/zlib; mkdir -p build
TC=~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang
nice -n 19 $TC -O1 -I$W -I../../src -I$ZL bie_fail_test.c $W/bie_win.c ../../src/dsl_json.c $ZL/{adler32,crc32,inffast,inflate,inftrees,zutil}.c -o build/bie_fail_test.exe -lurlmon -lbcrypt
echo built build/bie_fail_test.exe
