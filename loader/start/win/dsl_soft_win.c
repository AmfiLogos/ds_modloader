// Windows OS part of the soft restart (src/dsl_soft_os.h). Same protocol as the Mac (src/dsl_soft.c): A starts B with
// CreateProcess (same exe, command line, folder, environment; DSL_PROFILE / DSL_SOFT_HANDOFF replaced).
// B hidden: user32 ShowWindow / SetWindowPos / SetForegroundWindow / CreateWindowExW are hooked (MinHook, outside the
// game code, so not through the engine) and keep every window of the process invisible and in the background until
// the swap. Swap: A allows B into the foreground (AllowSetForegroundWindow), B moves its window onto A's rectangle and
// shows it, A hides its windows and quits.
// Main-thread tick: a thread timer (SetTimer without a window) of the thread that loaded us = Unity's main thread;
// its WM_TIMER runs in Unity's own message loop, between frames.
#include "dsl_internal.h"
#include "dsl_soft_os.h"
#include "MinHook.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static wchar_t *env0; static size_t env0_len;           /* the environment block as started (double-NUL terminated) */
static wchar_t exe0[MAX_PATH], cwd0[MAX_PATH], *cmd0;
static int hidden, stage_on; static double stage_x, stage_y;
static PROCESS_INFORMATION child;
static DWORD tid0;

double dsl_sos_proc_start(void) {
    FILETIME c, e, k, u;
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
        ULARGE_INTEGER t; t.LowPart = c.dwLowDateTime; t.HighPart = c.dwHighDateTime;
        return (double)(t.QuadPart - 116444736000000000ULL) / 1e7;
    }
    FILETIME n; GetSystemTimeAsFileTime(&n); ULARGE_INTEGER t; t.LowPart = n.dwLowDateTime; t.HighPart = n.dwHighDateTime;
    return (double)(t.QuadPart - 116444736000000000ULL) / 1e7;
}

void dsl_sos_snapshot(void) {
    wchar_t *e = GetEnvironmentStringsW();
    if (e) {
        const wchar_t *p = e; while (*p) p += wcslen(p) + 1;
        env0_len = (size_t)(p - e) + 1;
        env0 = malloc(env0_len * sizeof *env0); memcpy(env0, e, env0_len * sizeof *env0);
        FreeEnvironmentStringsW(e);
    }
    GetModuleFileNameW(NULL, exe0, MAX_PATH);
    GetCurrentDirectoryW(MAX_PATH, cwd0);
    cmd0 = _wcsdup(GetCommandLineW());
    tid0 = GetCurrentThreadId();
}
void dsl_sos_unsetenv(const char *name) { SetEnvironmentVariableA(name, NULL); char k[128]; snprintf(k, sizeof k, "%s=", name); _putenv(k); }

/* ------------------------------------------------------------------ the process's windows */
typedef struct { HWND w[32]; int n; } Wins;
static BOOL CALLBACK enum_cb(HWND h, LPARAM lp) {
    Wins *ws = (Wins *)lp; DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && ws->n < 32) ws->w[ws->n++] = h;
    return TRUE;
}
static void own_windows(Wins *ws) { ws->n = 0; EnumWindows(enum_cb, (LPARAM)ws); }
static int is_game_class(HWND h) { wchar_t c[64]; return GetClassNameW(h, c, 64) && !wcscmp(c, L"UnityWndClass"); }
static HWND game_window(void) {                          /* UnityWndClass, else the largest top-level window */
    Wins ws; own_windows(&ws); HWND best = NULL; long long ba = -1;
    for (int i = 0; i < ws.n; i++) {
        RECT r; GetWindowRect(ws.w[i], &r); long long a = (long long)(r.right - r.left) * (r.bottom - r.top) + (is_game_class(ws.w[i]) ? 1LL << 40 : 0);
        if (a > ba) { ba = a; best = ws.w[i]; }
    }
    return best;
}
static int ours(HWND h) { DWORD pid = 0; GetWindowThreadProcessId(h, &pid); return pid == GetCurrentProcessId() && !GetParent(h); }

static BOOL (WINAPI *o_show)(HWND, int);
static BOOL (WINAPI *o_setpos)(HWND, HWND, int, int, int, int, UINT);
static BOOL (WINAPI *o_fg)(HWND);
static HWND (WINAPI *o_create)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static BOOL WINAPI my_show(HWND h, int cmd) { if (hidden && ours(h)) return o_show(h, SW_HIDE); return o_show(h, cmd); }
static BOOL WINAPI my_setpos(HWND h, HWND after, int x, int y, int cx, int cy, UINT f) {
    if (hidden && ours(h)) { f &= ~(UINT)SWP_SHOWWINDOW; f |= SWP_NOACTIVATE; if (after == HWND_TOP || after == HWND_TOPMOST) f |= SWP_NOZORDER; }
    if (stage_on && ours(h)) { x = (int)stage_x; y = (int)stage_y; f &= ~(UINT)SWP_NOMOVE; }
    return o_setpos(h, after, x, y, cx, cy, f);
}
static BOOL WINAPI my_fg(HWND h) { if (hidden && ours(h)) return TRUE; return o_fg(h); }
static HWND WINAPI my_create(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD st, int x, int y, int w, int h, HWND parent, HMENU m, HINSTANCE i, LPVOID p) {
    if (hidden && !parent) { st &= ~(DWORD)WS_VISIBLE; ex |= WS_EX_NOACTIVATE; }
    if (stage_on && !parent) { x = (int)stage_x; y = (int)stage_y; }
    return o_create(ex, cls, name, st, x, y, w, h, parent, m, i, p);
}
static int hook_api(const wchar_t *mod, const char *fn, void *repl, void **orig) {
    MH_STATUS s = MH_CreateHookApi(mod, fn, repl, orig);
    if (s == MH_OK) s = MH_EnableHook(MH_ALL_HOOKS);
    if (s != MH_OK) { dsl_log("soft: hook %s failed (MinHook %d)", fn, s); return -1; }
    return 0;
}
void dsl_sos_windows_install(int hide, int stage, double sx, double sy) {
    hidden = hide; stage_on = stage; stage_x = sx; stage_y = sy;
    if (!hide && !stage) return;
    MH_STATUS s = MH_Initialize(); if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { dsl_log("soft: MH_Initialize %d", s); return; }
    hook_api(L"user32", "ShowWindow", (void *)my_show, (void **)&o_show);
    hook_api(L"user32", "SetWindowPos", (void *)my_setpos, (void **)&o_setpos);
    hook_api(L"user32", "SetForegroundWindow", (void *)my_fg, (void **)&o_fg);
    hook_api(L"user32", "CreateWindowExW", (void *)my_create, (void **)&o_create);
    dsl_log("soft: window hooks installed (hidden %d, stage %d)", hide, stage);
}
void dsl_sos_set_hidden(int on) { hidden = on; }

void dsl_sos_frame_log(FILE *f) {
    Wins ws; own_windows(&ws); HWND fg = GetForegroundWindow();
    for (int i = 0; i < ws.n; i++) {
        RECT r; GetWindowRect(ws.w[i], &r); wchar_t c[64] = L""; GetClassNameW(ws.w[i], c, 64);
        fprintf(f, " | win %p %ls vis %d fg %d %ld,%ld %ldx%ld", (void *)ws.w[i], c, IsWindowVisible(ws.w[i]) ? 1 : 0, ws.w[i] == fg,
                r.left, r.top, r.right - r.left, r.bottom - r.top);
    }
}
void dsl_sos_stage_check(void) {
    if (!stage_on) return;
    Wins ws; own_windows(&ws);
    for (int i = 0; i < ws.n; i++) {
        RECT r; GetWindowRect(ws.w[i], &r);
        if ((r.left != (long)stage_x || r.top != (long)stage_y) && IsWindowVisible(ws.w[i])) {
            (o_setpos ? o_setpos : SetWindowPos)(ws.w[i], NULL, (int)stage_x, (int)stage_y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            dsl_log("soft test: window off the stage (%ld,%ld): moved back", r.left, r.top);
        }
    }
}
int dsl_sos_game_frame(double fr[4]) {
    HWND h = game_window(); if (!h) return 0;
    RECT r; if (!GetWindowRect(h, &r)) return 0;
    fr[0] = r.left; fr[1] = r.top; fr[2] = r.right - r.left; fr[3] = r.bottom - r.top;
    return 1;
}

/* ------------------------------------------------------------------ B process */
/* The game is built with Unity's "force single instance": a second process finds the named mutex
   "...-SingleInstanceMutex-..." of the first, looks for its window and never starts (it hung under wine with A in
   -batchmode). A closes its own handle of that mutex right before it starts B; B then creates it as the only one.
   (A failed restart leaves A without it: only a second double-click could then open a second game.) */
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } UStr;
typedef LONG (NTAPI *NtQueryObject_t)(HANDLE, int, PVOID, ULONG, PULONG);
static int release_single_instance(void) {
    NtQueryObject_t q = (NtQueryObject_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
    if (!q) return 0;
    static union { UStr s; BYTE b[4096]; } buf;
    int closed = 0;
    for (ULONG_PTR h = 4; h < 0x10000; h += 4) {
        ULONG n = 0;
        if (q((HANDLE)h, 2 /* ObjectTypeInformation */, buf.b, sizeof buf.b, &n) < 0) continue;
        if (!buf.s.Buffer || buf.s.Length != 12 || wcsncmp(buf.s.Buffer, L"Mutant", 6)) continue;
        if (q((HANDLE)h, 1 /* ObjectNameInformation */, buf.b, sizeof buf.b, &n) < 0 || !buf.s.Buffer) continue;
        wchar_t name[600]; size_t L = buf.s.Length / 2; if (L > 599) L = 599; wcsncpy(name, buf.s.Buffer, L); name[L] = 0;
        if (!wcsstr(name, L"SingleInstanceMutex")) continue;
        CloseHandle((HANDLE)h); closed++;
        dsl_log("soft: A: single-instance mutex %ls released for B", name);
    }
    return closed;
}

static int env_key_is(const wchar_t *line, const char *key) {
    size_t k = strlen(key);
    for (size_t i = 0; i < k; i++) if (towupper(line[i]) != (wchar_t)toupper((unsigned char)key[i])) return 0;
    return line[k] == L'=';
}
int dsl_sos_spawn(const char *profile, const char *handoff, const char *env_lines) {
    /* environment = as started, without DSL_PROFILE / DSL_SOFT_HANDOFF / the caller's keys, + those values */
    char keys[66][128]; int nk = 0;
    snprintf(keys[nk++], 128, "DSL_PROFILE"); snprintf(keys[nk++], 128, "DSL_SOFT_HANDOFF");
    char *ov = _strdup(env_lines ? env_lines : ""), *sv = NULL;
    for (char *l = strtok_s(ov, "\n", &sv); l && nk < 66; l = strtok_s(NULL, "\n", &sv)) {
        char *eq = strchr(l, '='); if (!eq || eq == l) continue;
        snprintf(keys[nk++], 128, "%.*s", (int)(eq - l), l);
    }
    free(ov);
    size_t cap = env0_len + 8192, n = 0;
    wchar_t *blk = malloc(cap * sizeof *blk);
    for (const wchar_t *p = env0; p && *p; p += wcslen(p) + 1) {
        int drop = 0; for (int i = 0; i < nk; i++) if (env_key_is(p, keys[i])) drop = 1;
        if (drop) continue;
        size_t L = wcslen(p) + 1; memcpy(blk + n, p, L * sizeof *blk); n += L;
    }
    char line[2600];
    #define ADD(s) do { int w = MultiByteToWideChar(CP_UTF8, 0, s, -1, blk + n, (int)(cap - n - 2)); if (w > 0) n += (size_t)w; } while (0)
    snprintf(line, sizeof line, "DSL_PROFILE=%s", profile); ADD(line);
    snprintf(line, sizeof line, "DSL_SOFT_HANDOFF=%s", handoff); ADD(line);
    ov = _strdup(env_lines ? env_lines : ""); sv = NULL;
    for (char *l = strtok_s(ov, "\n", &sv); l; l = strtok_s(NULL, "\n", &sv)) if (strchr(l, '=') && l[0] != '=') ADD(l);
    free(ov);
    blk[n++] = 0;
    /* Unity keeps its -logFile open without sharing: B gets the other of player.log / player.soft.log (B would hang
       at start otherwise; seen under wine) */
    wchar_t *cmd = malloc((wcslen(cmd0) + 64) * sizeof *cmd); wcscpy(cmd, cmd0);
    wchar_t *lf = wcsstr(cmd, L"-logFile ");
    if (lf) {
        wchar_t *v = lf + 9; while (*v == L' ') v++;
        int q = *v == L'"'; wchar_t *b0 = v + q, *e = b0;
        while (*e && (q ? *e != L'"' : *e != L' ')) e++;
        wchar_t path[MAX_PATH * 2]; size_t L = (size_t)(e - b0); if (L >= MAX_PATH * 2 - 16) L = MAX_PATH * 2 - 16;
        wcsncpy(path, b0, L); path[L] = 0;
        size_t pl = wcslen(path);
        if (pl > 9 && !_wcsicmp(path + pl - 9, L".soft.log")) wcscpy(path + pl - 9, L".log");
        else if (pl > 4 && !_wcsicmp(path + pl - 4, L".log")) wcscpy(path + pl - 4, L".soft.log");
        wchar_t *rest = _wcsdup(e + (q && *e ? 1 : 0));
        swprintf(v, wcslen(cmd0) + 64 - (size_t)(v - cmd), L"\"%ls\"%ls", path, rest);
        free(rest);
    }
    if (!release_single_instance()) dsl_log("soft: A: no single-instance mutex found");
    STARTUPINFOW si = { sizeof si };
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;           /* the first ShowWindow of B: hidden */
    BOOL ok = CreateProcessW(exe0, cmd, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT, blk, cwd0[0] ? cwd0 : NULL, &si, &child);
    dsl_log("soft: A: B command line: %ls", cmd);
    free(blk); free(cmd);
    if (!ok) { dsl_log("soft: A: CreateProcess %ls failed (%lu)", exe0, GetLastError()); memset(&child, 0, sizeof child); return -1; }
    CloseHandle(child.hThread); child.hThread = NULL;
    dsl_log("soft: A: B started, pid %lu (%ls, profile %s)", child.dwProcessId, exe0, profile);
    return 0;
}
long dsl_sos_child_pid(void) { return (long)child.dwProcessId; }
int dsl_sos_child_exited(void) {
    if (!child.hProcess) return 0;
    if (WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0) { CloseHandle(child.hProcess); memset(&child, 0, sizeof child); return 1; }
    return 0;
}
void dsl_sos_child_kill(void) {
    if (!child.hProcess) return;
    TerminateProcess(child.hProcess, 1);
    dsl_log("soft: A: B pid %lu killed", child.dwProcessId);
    CloseHandle(child.hProcess); memset(&child, 0, sizeof child);
}
int dsl_sos_pid_alive(long pid) {
    if (pid <= 0) return 0;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid); if (!h) return 0;
    int alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h); return alive;
}
void dsl_sos_yield(void) { if (child.dwProcessId) AllowSetForegroundWindow(child.dwProcessId); }
void dsl_sos_show(const double *fr, int activate) {
    hidden = 0;
    HWND h = game_window(); if (!h) { dsl_log("soft: B: no game window to show"); return; }
    BOOL (WINAPI *sp)(HWND, HWND, int, int, int, int, UINT) = o_setpos ? o_setpos : SetWindowPos;
    if (fr && fr[2] > 0 && !stage_on) sp(h, NULL, (int)fr[0], (int)fr[1], (int)fr[2], (int)fr[3], SWP_NOZORDER | SWP_NOACTIVATE);
    (o_show ? o_show : ShowWindow)(h, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (activate) { (o_fg ? o_fg : SetForegroundWindow)(h); SetActiveWindow(h); }
}
void dsl_sos_order_out(void) {
    Wins ws; own_windows(&ws);
    for (int i = 0; i < ws.n; i++) (o_show ? o_show : ShowWindow)(ws.w[i], SW_HIDE);
}

/* ------------------------------------------------------------------ main-thread tick */
static void (*tick_fn)(void);
static int ticks_seen;
static void CALLBACK timer_proc(HWND h, UINT m, UINT_PTR id, DWORD t) {
    (void)h; (void)m; (void)id; (void)t;
    if (!ticks_seen++) dsl_log("soft: first tick on thread %lu (timer set on thread %lu)", GetCurrentThreadId(), tid0);
    if (tick_fn) tick_fn();
}
void dsl_sos_timer(void (*fn)(void)) {
    tick_fn = fn;
    if (!SetTimer(NULL, 0, 16, timer_proc)) dsl_log("soft: SetTimer failed (%lu): soft restart has no tick", GetLastError());
}
