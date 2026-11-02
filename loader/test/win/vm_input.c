// vm_input: TEST-ONLY helper for the Windows VM tests (never shipped; built by heldkey_vm.py with llvm-mingw, run in the
// guest through the winrig agent). It does what the agent's click / key ops cannot: move the mouse WITHOUT a click, and
// HOLD a key (the agent's key op sends down + up at once, so the game's held-key reader never sees it held).
//   vm_input.exe move <x> <y>      cursor to desktop pixel x, y (physical pixels, as the agent's click)
//   vm_input.exe hold <sc> <ms>    key down, <ms> later key up, as a SCAN code (the physical key: W = 0x11, S = 0x1f).
//                                  Unity reads the physical key, so a virtual-key code is wrong on a non-US layout: on
//                                  the VM (layout f0ce0409, AZERTY-like) VK 'W' arrived as Unity KeyCode.Z (r2 2026-11-02)
//   vm_input.exe fg                the foreground window: its process id, title and keyboard layout, and which of
//                                  Ctrl / Shift / Alt / W / S the system sees held (a stuck modifier changes the key)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int key(WORD sc, int up) {
    INPUT in; memset(&in, 0, sizeof in);
    in.type = INPUT_KEYBOARD; in.ki.wScan = sc; in.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
    return SendInput(1, &in, sizeof in) == 1;
}
int main(int argc, char **argv) {
    SetProcessDPIAware();
    if (argc == 4 && !strcmp(argv[1], "move")) {
        int ok = SetCursorPos(atoi(argv[2]), atoi(argv[3])); POINT p; GetCursorPos(&p);
        printf("move %d -> %ld %ld\n", ok, p.x, p.y); return ok ? 0 : 1;
    }
    if (argc == 4 && !strcmp(argv[1], "hold")) {
        WORD sc = (WORD)strtol(argv[2], NULL, 0); int ms = atoi(argv[3]);
        int a = key(sc, 0); Sleep((DWORD)ms); int b = key(sc, 1);
        printf("hold sc 0x%02x %d ms: down %d up %d\n", sc, ms, a, b); return a && b ? 0 : 1;
    }
    if (argc == 2 && !strcmp(argv[1], "fg")) {
        HWND h = GetForegroundWindow(); DWORD pid = 0; char t[200] = ""; if (h) { GetWindowThreadProcessId(h, &pid); GetWindowTextA(h, t, sizeof t); }
        DWORD tid = h ? GetWindowThreadProcessId(h, NULL) : 0; HKL kl = GetKeyboardLayout(tid);
        printf("fg pid %lu \"%s\" layout %08lx held ctrl %d shift %d alt %d W %d S %d\n", (unsigned long)pid, t,
               (unsigned long)(uintptr_t)kl, GetAsyncKeyState(VK_CONTROL) < 0, GetAsyncKeyState(VK_SHIFT) < 0,
               GetAsyncKeyState(VK_MENU) < 0, GetAsyncKeyState('W') < 0, GetAsyncKeyState('S') < 0);
        return h ? 0 : 1;
    }
    fprintf(stderr, "usage: vm_input move <x> <y> | hold <scancode> <ms> | fg\n");
    return 2;
}
