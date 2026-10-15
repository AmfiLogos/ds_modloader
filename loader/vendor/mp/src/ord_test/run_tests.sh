#!/bin/zsh
# Orders lane tests (no game): offsets vs il2cpp.h, then ord_core round trips with fake objects.
set -e; cd "${0:A:h}"
B=${TMPDIR:-/tmp}/ord_test_build; mkdir -p $B
python3 gen_events.py
nice -n 19 clang++ -std=c++17 -fsyntax-only -w -I. offsets_check.cpp
nice -n 19 clang++ -std=c++17 -fsyntax-only -w -I. offsets_manual.cpp
echo "offsets: all static_asserts hold against il2cpp.h"
python3 gen_offsets_md.py
nice -n 19 clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $B/test_ord test_ord.c ../ord_core.c
nice -n 19 $B/test_ord
