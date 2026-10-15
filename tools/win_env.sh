# Cross-compile environment for the Windows launcher (x86_64-pc-windows-gnu, llvm-mingw). Source it:
#   source tools/win_env.sh
# Needs ~/tools/llvm-mingw (same toolchain as ds_multiplayer/win/build_win.sh) and `rustup target add x86_64-pc-windows-gnu`.
export LLVM_MINGW=${LLVM_MINGW:-$HOME/tools/llvm-mingw}
export PATH="$LLVM_MINGW/bin:$PATH"
_here=${${(%):-%x}:A:h}
export DSML_MINGW_STUBS="$_here/../test_env/mingw_stubs"
if [[ ! -f $DSML_MINGW_STUBS/libgcc_eh.a ]]; then
  mkdir -p "$DSML_MINGW_STUBS"
  cp "$LLVM_MINGW/x86_64-w64-mingw32/lib/libunwind.a" "$DSML_MINGW_STUBS/libgcc_eh.a"
  "$LLVM_MINGW/bin/x86_64-w64-mingw32-ar" rcs "$DSML_MINGW_STUBS/libgcc.a"
fi
export CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER="$_here/mingw_link.sh"
export CC_x86_64_pc_windows_gnu="$LLVM_MINGW/bin/x86_64-w64-mingw32-clang"
export CXX_x86_64_pc_windows_gnu="$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++"
export AR_x86_64_pc_windows_gnu="$LLVM_MINGW/bin/x86_64-w64-mingw32-ar"
export RC_x86_64_pc_windows_gnu="$LLVM_MINGW/bin/x86_64-w64-mingw32-windres"
export CARGO_BUILD_JOBS=4
