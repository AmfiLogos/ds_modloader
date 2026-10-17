#!/bin/zsh
# quiet_test (see quiet_test.m): builds start/mac/dsl_quiet_mac.c into an inserted dylib, runs the background-only
# test app with the hooks on, then off (pass-through). Log lines: $OUT/quiet.log. Exit 0 = pass.
set -e; T=${0:A:h}; L=${T:h:h}; OUT=${OUT:-${TMPDIR:-/tmp}/dsl_quiet_test}; rm -rf $OUT; mkdir -p $OUT/QuietTest.app/Contents/MacOS
A=$OUT/QuietTest.app/Contents; CC=(clang -arch arm64 -O2 -Wall -mmacosx-version-min=12.0)
cat > $A/Info.plist <<P
<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict><key>CFBundleIdentifier</key><string>test.dsload.quiet</string>
<key>CFBundleExecutable</key><string>quiet_test</string><key>LSBackgroundOnly</key><true/></dict></plist>
P
printf '#include <stdarg.h>\n#include <stdio.h>\n#include <stdlib.h>\nvoid dsl_log(const char *f, ...) { static FILE *o; if (!o) o = fopen(getenv("QUIET_LOG"), "a"); va_list a; va_start(a, f); vfprintf(o, f, a); va_end(a); fputc(10, o); fflush(o); }\nvoid dsl_quiet_install(void);\n__attribute__((constructor)) static void on(void) { if (!getenv("QUIET_OFF")) dsl_quiet_install(); }\n' > $OUT/stub.c
$CC -dynamiclib -framework AppKit -I$L/src -o $OUT/quiet.dylib $OUT/stub.c $L/start/mac/dsl_quiet_mac.c
$CC -dynamiclib -o $A/MacOS/fakesteam.dylib $T/fakesteam.c
$CC -fobjc-arc -framework AppKit -o $A/MacOS/quiet_test $T/quiet_test.m
codesign -f -s - $OUT/quiet.dylib $A/MacOS/fakesteam.dylib $A/MacOS/quiet_test 2>/dev/null
r=0
QUIET_LOG=$OUT/quiet.log DYLD_INSERT_LIBRARIES=$OUT/quiet.dylib $A/MacOS/quiet_test || r=1
QUIET_LOG=$OUT/quiet_off.log QUIET_OFF=1 DYLD_INSERT_LIBRARIES=$OUT/quiet.dylib $A/MacOS/quiet_test off || r=1
echo "--- $OUT/quiet.log"; cat $OUT/quiet.log
[[ ! -s $OUT/quiet_off.log ]] || { echo "FAIL off: hooks logged while off"; cat $OUT/quiet_off.log; r=1; }
echo $([[ $r == 0 ]] && echo PASS || echo FAIL); exit $r
