/* SHARED STUB RULE (kit 0.1.30; lead task 2026-11-02 after nvrel found a Units Laboratory hook on Windows at
   Refactor.Control.DefaultMode$$ActivateMode = rva 0x50d900 "ret 0" (c2 00 00 cc), the code of 8696 methods): the
   Windows linker folds identical code, so a body of 4 bytes or less ("ret", "ret 0", "xor al, al; ret", "mov al, 1;
   ret", "mov rax, rcx; ret") is one address shared by thousands of methods. A detour there fires for every one of
   them, so a mod's hook_method refuses it on Windows (src/dsl_main.c h_hook_method) and logs the mod + method. The
   loader's and multiplayer's own hook tables are not refused here: tools/hooks/hooklist.py check fails on every hook
   whose code is shared with other methods unless tools/hooks/shared_ok.tsv names it with the reason it is safe.
   A unique 4-byte method (rare: 55 of 109k on build 25846389) is refused too; the log says so.
   Detection: a "ret" (c3) or "ret imm16" (c2 xx xx) that ends within the first 4 bytes and is followed by int3
   padding (cc): never true for a real function start (an operand byte c3 is never followed by an int3 there).
   Returns the stub's length (1..4), 0 = a normal function. Host test: test/stub_test.c. */
#ifndef DSL_STUB_H
#define DSL_STUB_H
#include <stdint.h>
static inline int dsl_x64_ret_stub(const uint8_t *p) {
    if (!p) return 0;
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0xc3 && p[i + 1] == 0xcc) return i + 1;
        if (p[i] == 0xc2 && i + 3 <= 4 && p[i + 3] == 0xcc) return i + 3;
    }
    return 0;
}
#endif
