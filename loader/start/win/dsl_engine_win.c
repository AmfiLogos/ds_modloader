// The one hook engine of the process, Windows x64 (same functions as src/dsl_engine.c, see dsl_internal.h).
// MinHook writes the site jump and builds the trampoline (the relocated first instructions). Between the site and
// the module's replacement sits one small thunk per hook that reads a slot, like the Mac stub:
//   +0   mov rax, [rip+slot] ; test rax, rax ; jz +2 ; jmp rax ; jmp [rip+tramp]      slot @+24, tramp @+32
// slot = 0 -> the original runs (unhook). The trampoline is never freed, so a module that still holds its `orig`
// pointer after an unhook keeps calling valid code (same rule as the Mac engine). rax is a scratch register in the
// x64 calling convention (no argument travels in it).
// Code patches (patch_rva): x64 has no instruction alignment, so only 1 <= n <= 16 is checked.
#include "dsl_internal.h"
#include "MinHook.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define MAXH 2048
#define THUNK_SZ 48
typedef struct { uintptr_t site; uint8_t *thunk; void *tramp; int owner; int active; char name[48]; } Hook;
static Hook H[MAXH];
static int nh;
static uint8_t *thunks;
static int late;
static SRWLOCK lock = SRWLOCK_INIT;
int dsl_codepatch_ok = 1;

#define SLOT(h) ((void *volatile *)((h)->thunk + 24))

int dsl_engine_init(void) {
    if (thunks) return 0;
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { dsl_log("engine: MH_Initialize failed (%d): hooks off", s); return -1; }
    thunks = VirtualAlloc(NULL, MAXH * THUNK_SZ, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!thunks) { dsl_log("engine: no thunk memory: hooks off"); return -1; }
    dsl_log("engine: MinHook, %d thunks at %p", MAXH, (void *)thunks);
    return 0;
}

void dsl_engine_set_late(void) {
    AcquireSRWLockExclusive(&lock);
    if (!late) dsl_log("engine: game code is running (MinHook suspends the other threads while it writes a site)");
    late = 1;
    ReleaseSRWLockExclusive(&lock);
}

/* Batch (GameAssembly mapped, DllMain: none of its code can run yet): MH_EnableHook freezes every other thread for
   each hook, and MinHook finds them with a system-wide thread snapshot (CreateToolhelp32Snapshot): about 33 ms per
   hook on a Windows VM, 203 multiplayer hooks = 6.7 s of a black window before the first frame (VM 2026-10-17).
   In the batch the hooks are queued and enabled together by one MH_ApplyQueued (one freeze). */
static int batch, queued;
void dsl_engine_batch(int on) {
    AcquireSRWLockExclusive(&lock);
    if (!on && batch && queued) {
        LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
        MH_STATUS s = MH_ApplyQueued();
        QueryPerformanceCounter(&b);
        dsl_log("engine: %d hook(s) enabled at once (one thread freeze, %.1f ms)%s", queued, (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart,
                s == MH_OK ? "" : ": MinHook apply FAILED");
        if (s != MH_OK) for (int i = 0; i < nh; i++) if (H[i].active) { MH_STATUS e = MH_EnableHook((void *)H[i].site); if (e != MH_OK && e != MH_ERROR_ENABLED) dsl_log("engine: hook %s not enabled (%d)", H[i].name, e); }
        queued = 0;
    }
    batch = on && !late;
    ReleaseSRWLockExclusive(&lock);
}

static Hook *find_site(uintptr_t site) { for (int i = 0; i < nh; i++) if (H[i].site == site) return &H[i]; return NULL; }

#define MAXPT 256
typedef struct { uintptr_t addr; uint32_t n; uint8_t old[16], neu[16]; int owner; int active; char name[48]; } Patch;
static Patch PT[MAXPT];
static int npt;
static const Patch *patch_over(uintptr_t a, size_t n) {
    for (int i = 0; i < npt; i++) if (PT[i].active && a < PT[i].addr + PT[i].n && PT[i].addr < a + n) return &PT[i];
    return NULL;
}

static int write_ga(uintptr_t addr, const void *src, size_t n) {
    if (!dsl_codepatch_ok) return -1;
    dsl_mark_codepatch();
    DWORD old;
    if (!VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &old)) return -1;
    memcpy((void *)addr, src, n);
    VirtualProtect((void *)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, n);
    return 0;
}

static void make_thunk(uint8_t *t, void *tramp, void *repl) {
    static const uint8_t code[20] = {
        0x48, 0x8B, 0x05, 0x11, 0x00, 0x00, 0x00,       /* +0  mov rax, [rip+0x11]  (-> +24 slot) */
        0x48, 0x85, 0xC0,                               /* +7  test rax, rax */
        0x74, 0x02,                                     /* +10 jz +2 (-> +14) */
        0xFF, 0xE0,                                     /* +12 jmp rax */
        0xFF, 0x25, 0x0C, 0x00, 0x00, 0x00,             /* +14 jmp [rip+0x0C]  (-> +32 tramp) */
    };
    memset(t, 0xCC, THUNK_SZ);
    memcpy(t, code, sizeof code);
    *(void **)(t + 32) = tramp;
    *(void *volatile *)(t + 24) = repl;
    FlushInstructionCache(GetCurrentProcess(), t, THUNK_SZ);
}

/* a site that already starts with a jump: another loader's detour or a static patch */
static int foreign_jump(const uint8_t *p) {
    return p[0] == 0xE9 || p[0] == 0xEB || (p[0] == 0xFF && p[1] == 0x25) || (p[0] == 0x48 && p[1] == 0xB8 && p[10] == 0xFF && p[11] == 0xE0);
}

static int hook_locked(int owner, void *target, void *repl, void **orig, const char *who) {
    uintptr_t site = (uintptr_t)target;
    if (!dsl_build_id) { dsl_log("%s: hook refused: the game build is not the pinned one", who); return -1; }
    if (!thunks || !site || !repl) { dsl_log("%s: hook refused (null target/replacement or engine inactive)", who); return -1; }
    if (site < dsl_ga_base + dsl_text_lo || site >= dsl_ga_base + dsl_text_hi) {
        dsl_log("%s: hook refused: %p is not code inside GameAssembly", who, target); return -1;
    }
    uintptr_t rva = site - dsl_ga_base;
    Hook *h = find_site(site);
    if (h && h->active) {
        dsl_log("%s: hook refused: %#llx is already hooked by %s (%s)%s", who, (unsigned long long)rva, dsl_owner_name(h->owner), h->name,
                h->owner != owner ? " (on Windows two methods can share one body: MSVC identical-code folding)" : "");
        return -1;
    }
    if (h) {                                            /* re-hook a site unhooked earlier: reuse its thunk */
        *SLOT(h) = repl; h->owner = owner; h->active = 1;
        if (orig) *orig = h->tramp;
        snprintf(h->name, sizeof h->name, "%s", who);
        dsl_log("%s: hook %#llx re-armed", who, (unsigned long long)rva);
        return 0;
    }
    const Patch *pp = patch_over(site, 5);
    if (pp) { dsl_log("%s: hook refused: %#llx is inside a code patch of %s (%s)", who, (unsigned long long)rva, dsl_owner_name(pp->owner), pp->name); return -1; }
    if (foreign_jump((const uint8_t *)site)) {
        dsl_log("%s: hook refused: %#llx already starts with a jump (a static patch or another loader's hook)", who, (unsigned long long)rva);
        return -1;
    }
    if (nh >= MAXH) { dsl_log("%s: hook refused: no free thunk left", who); return -1; }
    uint8_t *t = thunks + (size_t)nh * THUNK_SZ;
    void *tramp = NULL;
    MH_STATUS s = MH_CreateHook(target, t, &tramp);
    if (s != MH_OK) { dsl_log("%s: hook %#llx failed (MinHook create %d)", who, (unsigned long long)rva, s); return -1; }
    make_thunk(t, tramp, repl);
    dsl_mark_codepatch();
    s = batch ? MH_QueueEnableHook(target) : MH_EnableHook(target);
    if (s != MH_OK) { MH_RemoveHook(target); dsl_log("%s: hook %#llx failed (MinHook enable %d)", who, (unsigned long long)rva, s); return -1; }
    if (batch) queued++;
    Hook *n = &H[nh++];
    *n = (Hook){ site, t, tramp, owner, 1, "" };
    snprintf(n->name, sizeof n->name, "%s", who);
    if (orig) *orig = tramp;
    return 0;
}

int dsl_hook(int owner, void *target, void *repl, void **orig, const char *who) {
    AcquireSRWLockExclusive(&lock);
    int r = hook_locked(owner, target, repl, orig, who);
    ReleaseSRWLockExclusive(&lock);
    if (!r) dsl_log("%s: hooked %#llx for %s", who, (unsigned long long)((uintptr_t)target - dsl_ga_base), dsl_owner_name(owner));
    return r;
}

int dsl_unhook(int owner, void *target, const char *who) {
    AcquireSRWLockExclusive(&lock);
    Hook *h = find_site((uintptr_t)target);
    int ok = h && h->active && h->owner == owner;
    if (ok) { *SLOT(h) = NULL; h->active = 0; }
    ReleaseSRWLockExclusive(&lock);
    if (!ok) { dsl_log("%s: unhook %p: not hooked by this module", who, target); return -1; }
    dsl_log("%s: unhooked %#llx (original code runs again)", who, (unsigned long long)((uintptr_t)target - dsl_ga_base));
    return 0;
}

int dsl_unhook_owner(int owner) {
    int n = 0;
    AcquireSRWLockExclusive(&lock);
    for (int i = 0; i < nh; i++) if (H[i].active && H[i].owner == owner) { *SLOT(&H[i]) = NULL; H[i].active = 0; n++; }
    for (int i = 0; i < npt; i++) if (PT[i].active && PT[i].owner == owner) {        /* put the original bytes back */
        if (!memcmp((void *)PT[i].addr, PT[i].neu, PT[i].n) && !write_ga(PT[i].addr, PT[i].old, PT[i].n)) { PT[i].active = 0; n++; }
    }
    ReleaseSRWLockExclusive(&lock);
    return n;
}

int dsl_patch(int owner, void *target, const void *old, const void *neu, size_t n, const char *who) {
    uintptr_t a = (uintptr_t)target;
    if (!dsl_build_id) { dsl_log("%s: patch refused: the game build is not the pinned one", who); return -1; }
    if (!thunks || !a || !old || !neu || n == 0 || n > 16) { dsl_log("%s: patch refused (bad arguments or engine inactive)", who); return -1; }
    if (a < dsl_ga_base + dsl_text_lo || a + n > dsl_ga_base + dsl_text_hi) { dsl_log("%s: patch refused: %p is not code inside GameAssembly", who, target); return -1; }
    uintptr_t rva = a - dsl_ga_base;
    AcquireSRWLockExclusive(&lock);
    int r = -1;
    const Patch *o = patch_over(a, n);
    Hook *h = NULL;
    for (int i = 0; i < nh; i++) if (H[i].active && H[i].site < a + n && a < H[i].site + 5) h = &H[i];
    if (o) dsl_log("%s: patch %#llx refused: overlaps a patch of %s (%s)", who, (unsigned long long)rva, dsl_owner_name(o->owner), o->name);
    else if (h) dsl_log("%s: patch %#llx refused: overlaps a hook of %s (%s)", who, (unsigned long long)rva, dsl_owner_name(h->owner), h->name);
    else if (npt >= MAXPT) dsl_log("%s: patch refused: table full", who);
    else if (memcmp((void *)a, old, n)) {
        char have[40] = ""; for (size_t i = 0; i < n; i++) snprintf(have + 2 * i, 3, "%02x", ((uint8_t *)a)[i]);
        dsl_log("%s: patch %#llx refused: bytes are %s, not the expected ones (other build, static patch or another loader)", who, (unsigned long long)rva, have);
    } else if (write_ga(a, neu, n)) dsl_log("%s: patch %#llx failed: write", who, (unsigned long long)rva);
    else {
        Patch *p = &PT[npt++]; memset(p, 0, sizeof *p);
        p->addr = a; p->n = (uint32_t)n; memcpy(p->old, old, n); memcpy(p->neu, neu, n); p->owner = owner; p->active = 1;
        snprintf(p->name, sizeof p->name, "%s", who);
        r = 0;
    }
    ReleaseSRWLockExclusive(&lock);
    if (!r) dsl_log("%s: patched %#llx (%zu bytes) for %s", who, (unsigned long long)rva, n, dsl_owner_name(owner));
    return r;
}

int dsl_hook_count(int owner) {
    int n = 0;
    AcquireSRWLockExclusive(&lock);
    for (int i = 0; i < nh; i++) if (H[i].active && (owner == -2 || H[i].owner == owner)) n++;
    for (int i = 0; i < npt; i++) if (PT[i].active && (owner == -2 || PT[i].owner == owner)) n++;
    ReleaseSRWLockExclusive(&lock);
    return n;
}
