#!/bin/sh
# Linker for x86_64-pc-windows-gnu with llvm-mingw: Rust asks for libgcc / libgcc_eh (GNU mingw names);
# llvm-mingw has libunwind + compiler-rt instead. test_env/mingw_stubs maps the names (made by tools/win_env.sh).
exec "$LLVM_MINGW/bin/x86_64-w64-mingw32-clang" -L "$DSML_MINGW_STUBS" "$@"
