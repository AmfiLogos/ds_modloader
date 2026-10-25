#!/bin/zsh
# units-probe (Windows, test only, never shipped): out = $1 (default ./build) with units-probe/{mod.json,units-probe.dll}
H=${0:A:h}; L=${H:h:h:h}; O=${1:-$H/build}/units-probe
mkdir -p $O
nice -n 19 ~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang -ffile-prefix-map=$HOME=/src -O2 -Wall -Wno-unused-function \
  -D__USE_MINGW_ANSI_STDIO=1 -I$L/sdk -shared -o $O/units-probe.dll $H/units_probe_win.c -Wl,--export-all-symbols -static -static-libgcc && \
  cp $H/mod.json $O/mod.json && echo "built $O"
