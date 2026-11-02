/* SHARED STUB RULE (src/dsl_stub.h): the Windows hook_method refuses a target whose code is a <= 4-byte "ret" stub.
   Bytes below are copied from GameAssembly.dll of build 25846389 (Windows). Breaks caught: a hook on the "ret 0" that
   8696 methods share (Units Laboratory DefaultMode.ActivateMode, 2026-11-02) gets through again; or a normal method
   start (one that has a c3 operand byte, or a 5-byte getter) is refused and a mod stops working on Windows. */
#include "dsl_stub.h"
#include <stdio.h>
static int fails;
static void t(const char *what, const unsigned char *b, int want) {
    int got = dsl_x64_ret_stub(b);
    printf("%s %s (got %d, want %d)\n", got == want ? "PASS" : "FAIL", what, got, want); fails += got != want;
}
int main(void) {
    t("0x50d900 DefaultMode.ActivateMode: ret 0 (8696 methods) refused", (const unsigned char[]){ 0xc2, 0x00, 0x00, 0xcc, 0xcc, 0xcc }, 3);
    t("0x510af0: xor al, al; ret (4869 methods) refused", (const unsigned char[]){ 0x32, 0xc0, 0xc3, 0xcc, 0xcc, 0xcc }, 3);
    t("0x5d3ee0: mov al, 1; ret (1846 methods) refused", (const unsigned char[]){ 0xb0, 0x01, 0xc3, 0xcc, 0xcc, 0xcc }, 3);
    t("0x8b0620: mov rax, rcx; ret (1655 methods) refused", (const unsigned char[]){ 0x48, 0x8b, 0xc1, 0xc3, 0xcc, 0xcc }, 4);
    t("a bare ret refused", (const unsigned char[]){ 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, 1);
    t("0x514360: 5-byte getter mov rax, [rcx+18h]; ret not refused", (const unsigned char[]){ 0x48, 0x8b, 0x41, 0x18, 0xc3, 0xcc }, 0);
    t("0x1104a30: a normal function start not refused", (const unsigned char[]){ 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57 }, 0);
    t("mov rax, rbx (48 8b c3) then code not refused", (const unsigned char[]){ 0x48, 0x8b, 0xc3, 0x48, 0x83, 0xc4 }, 0);
    t("0x5146c0: xor edx, edx; jmp (a tail call) not refused", (const unsigned char[]){ 0x33, 0xd2, 0xe9, 0x39, 0x92, 0xff }, 0);
    return fails != 0;
}
