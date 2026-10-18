// TEST ONLY (Windows VM, no game): the loader's Windows hook engine (start/win/dsl_engine_win.c, MinHook) and BepInEx's
// native detour (be.788 INativeDetour.Create: FollowExportThunks, then dobby.dll DobbyPrepare + DobbyCommit) on the
// same function, in both orders. This exe stands in for GameAssembly (the engine only hooks inside dsl_ga_base's image).
//   forward  ours first (as in the game: hooks at GameAssembly mapping), BepInEx second: both run, result unchanged;
//            our unhook leaves BepInEx's detour running; the re-armed hook runs again.
//   reverse  BepInEx first, ours second (a late hook): the engine chains over Dobby's jump, both run, ours first.
// Usage: chaintest.exe [dobby.dll path]  (default: dobby.dll next to the exe). Prints PASS / FAIL lines, also into
// chaintest.log next to the exe; exit code 0 = every check passes. Built by stage.sh (llvm-mingw).
#include "dsl_internal.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

uintptr_t dsl_ga_base; uint64_t dsl_text_lo, dsl_text_hi, dsl_image_end; const char *dsl_build_id = "chaintest";
static FILE *lf;
void dsl_log(const char *fmt, ...) {
    va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n");
    if (lf) { va_start(a, fmt); fprintf(lf, "  engine: "); vfprintf(lf, fmt, a); fprintf(lf, "\n"); va_end(a); fflush(lf); }
}
const char *dsl_owner_name(int owner) { return owner == 1 ? "testmod" : "?"; }
void dsl_mark_codepatch(void) {}

static int fails, passes;
static void check(const char *name, int ok, const char *fmt, ...) {
    char d[300] = ""; va_list a; va_start(a, fmt); vsnprintf(d, sizeof d, fmt, a); va_end(a);
    printf("%s %s  [%s]\n", ok ? "PASS" : "FAIL", name, d); fflush(stdout);
    if (lf) { fprintf(lf, "%s %s  [%s]\n", ok ? "PASS" : "FAIL", name, d); fflush(lf); }
    if (ok) passes++; else fails++;
}

/* the "game functions": -O0 frame prologues (push rbp; mov rbp, rsp; sub rsp, n), longer than both engines' patch */
static volatile int sink = 1;
__attribute__((noinline)) int game_a(int x) { int r = x; for (int i = 0; i < 3; i++) r = r * 3 + sink; return r + 1; }
__attribute__((noinline)) int game_b(int x) { int r = x; for (int i = 0; i < 4; i++) r = r * 5 - sink; return r - 2; }

/* call order record: 'o' = ours, 'b' = BepInEx */
static char seq[16]; static int nseq;
static void mark(char c) { if (nseq < 15) seq[nseq++] = c; seq[nseq] = 0; }

static int (*o_a)(int), (*o_b)(int), (*d_a)(int), (*d_b)(int);
static int ours_a(int x) { mark('o'); return o_a(x); }
static int ours_b(int x) { mark('o'); return o_b(x); }
static int bie_a(int x) { mark('b'); return d_a(x); }
static int bie_b(int x) { mark('b'); return d_b(x); }

typedef int (*prep_fn)(void *, void *, void **);
typedef int (*commit_fn)(void *);
static prep_fn DobbyPrepare; static commit_fn DobbyCommit;

/* BepInEx be.788 INativeDetour.FollowExportThunks: up to 8 jumps E9 rel32 / FF 25 [rip+disp32] */
static uint8_t *follow(uint8_t *p) {
    for (int i = 0; i < 8; i++) {
        if (p[0] == 0xE9) { int32_t r; memcpy(&r, p + 1, 4); p += 5 + r; continue; }
        if (p[0] == 0xFF && p[1] == 0x25) { int32_t r; memcpy(&r, p + 2, 4); memcpy(&p, p + 6 + r, 8); continue; }
        break;
    }
    return p;
}

/* BepInEx's detour: Create (follow) + GenerateTrampoline (Prepare) + Apply (Commit); where = what it patched */
static int bie_detour(void *fn, void *det, void **orig, const char **where) {
    uint8_t *t = follow(fn);
    uintptr_t g0 = dsl_ga_base, g1 = dsl_ga_base + dsl_image_end;
    *where = t == fn ? "the function itself" : ((uintptr_t)t >= g0 && (uintptr_t)t < g1) ? "inside the image" : "the dsload thunk (BepInEx followed our jump)";
    if (DobbyPrepare(t, det, orig)) return -1;
    return DobbyCommit(t);
}

static void hex(char *out, const void *p, int n) { for (int i = 0; i < n; i++) sprintf(out + 3 * i, "%02x ", ((const uint8_t *)p)[i]); }

int main(int argc, char **argv) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exe, L'\\'); if (sl) sl[1] = 0;
    wchar_t lp[MAX_PATH]; swprintf(lp, MAX_PATH, L"%lschaintest.log", exe); lf = _wfopen(lp, L"w");
    wchar_t dp[MAX_PATH];
    if (argc > 1) MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, dp, MAX_PATH); else swprintf(dp, MAX_PATH, L"%lsdobby.dll", exe);
    HMODULE dob = LoadLibraryW(dp);
    check("dobby.dll loads (DobbyPrepare, DobbyCommit)", dob && (DobbyPrepare = (prep_fn)GetProcAddress(dob, "DobbyPrepare")) && (DobbyCommit = (commit_fn)GetProcAddress(dob, "DobbyCommit")), "%p", (void *)dob);
    if (fails) return 1;

    HMODULE me = GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((uint8_t *)me + ((IMAGE_DOS_HEADER *)me)->e_lfanew);
    dsl_ga_base = (uintptr_t)me; dsl_text_lo = 0x1000; dsl_text_hi = dsl_image_end = nt->OptionalHeader.SizeOfImage;
    int ea = game_a(4), eb = game_b(4);
    check("engine starts", dsl_engine_init() == 0, "image %p", (void *)me);

    /* forward: ours, then BepInEx */
    const char *where = "";
    int h = dsl_hook(1, (void *)game_a, (void *)ours_a, (void **)&o_a, "game_a");
    int d = h ? -1 : bie_detour((void *)game_a, (void *)bie_a, (void **)&d_a, &where);
    check("forward: our hook, then BepInEx's Dobby detour (no crash, no error)", h == 0 && d == 0, "hook %d, dobby %d, dobby patched %s", h, d, where);
    if (!h && !d) {
        nseq = 0; int r = game_a(4);
        check("forward: both run, BepInEx first then ours, result unchanged", !strcmp(seq, "bo") && r == ea, "order %s, result %d (stock %d)", seq, r, ea);
        dsl_unhook(1, (void *)game_a, "game_a");
        nseq = 0; r = game_a(4);
        check("forward: after our unhook BepInEx still runs, ours not", !strcmp(seq, "b") && r == ea, "order %s, result %d", seq, r);
        int rh = dsl_hook(1, (void *)game_a, (void *)ours_a, (void **)&o_a, "game_a");
        nseq = 0; r = game_a(4);
        check("forward: re-armed hook runs again", rh == 0 && !strcmp(seq, "bo") && r == ea, "re-arm %d, order %s, result %d", rh, seq, r);
    }

    /* reverse: BepInEx, then ours */
    char before[64] = "";
    d = bie_detour((void *)game_b, (void *)bie_b, (void **)&d_b, &where);
    hex(before, (void *)game_b, 8);
    h = d ? -1 : dsl_hook(1, (void *)game_b, (void *)ours_b, (void **)&o_b, "game_b");
    check("reverse: BepInEx's Dobby detour, then our hook chained over it", d == 0 && h == 0, "dobby %d (patched %s, site then %s), hook %d", d, where, before, h);
    if (!h && !d) {
        nseq = 0; int r = game_b(4);
        check("reverse: both run, ours first then BepInEx, result unchanged", !strcmp(seq, "ob") && r == eb, "order %s, result %d (stock %d)", seq, r, eb);
        dsl_unhook(1, (void *)game_b, "game_b");
        nseq = 0; r = game_b(4);
        check("reverse: after our unhook BepInEx still runs", !strcmp(seq, "b") && r == eb, "order %s, result %d", seq, r);
    }
    printf("%d/%d checks pass\n", passes, passes + fails);
    if (lf) { fprintf(lf, "%d/%d checks pass\n", passes, passes + fails); fclose(lf); }
    return fails ? 1 : 0;
}
