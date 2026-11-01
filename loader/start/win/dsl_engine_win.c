// The one hook engine of the process, Windows x64 (same functions as src/dsl_engine.c, see dsl_internal.h).
// MinHook writes the site jump and builds the trampoline (the relocated first instructions). Between the site and
// the module's replacement sits one small thunk per hook that reads a slot, like the Mac stub:
//   +0   mov rax, [rip+slot] ; test rax, rax ; jz +2 ; jmp rax ; jmp [rip+tramp]      slot @+24, tramp @+32
// slot = 0 -> the original runs (unhook). The trampoline is never freed, so a module that still holds its `orig`
// pointer after an unhook keeps calling valid code (same rule as the Mac engine). rax is a scratch register in the
// x64 calling convention (no argument travels in it).
// Code patches (patch_rva): x64 has no instruction alignment, so only 1 <= n <= 16 is checked.
// Several modules on one function: src/dsl_chain.h (shared with the Mac engine). The slot then points at the newest
// module's replacement or at a counting veneer (32 bytes after the thunks):
//   +0 lock inc qword [rip+8] (count @+16) ; +8 jmp [rip+10] (next @+24)
// rax keeps the thunk's value only up to `jmp rax`; the veneer uses no register, only the flags (not kept across calls).
#include "dsl_internal.h"
#include "MinHook.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXH 2048
#define THUNK_SZ 48
#define MAXV 1024
#define VEN_SZ 32
static uint8_t *thunks;
static int nven;
static int late;
static SRWLOCK lock = SRWLOCK_INIT;
int dsl_codepatch_ok = 1;

static int ch_veneer(uintptr_t *code, volatile uint64_t **cnt, void *volatile **next) {
    static const uint8_t c[14] = { 0xF0, 0x48, 0xFF, 0x05, 0x08, 0x00, 0x00, 0x00,     /* lock inc qword [rip+8] (-> +16) */
                                   0xFF, 0x25, 0x0A, 0x00, 0x00, 0x00 };             /* jmp [rip+10] (-> +24) */
    if (!thunks || nven >= MAXV) return -1;
    uint8_t *v = thunks + (size_t)MAXH * THUNK_SZ + (size_t)nven * VEN_SZ;
    memset(v, 0xCC, VEN_SZ);
    memcpy(v, c, sizeof c);
    *(uint64_t *)(v + 16) = 0; *(void **)(v + 24) = NULL;
    FlushInstructionCache(GetCurrentProcess(), v, VEN_SZ);
    nven++;
    *code = (uintptr_t)v; *cnt = (volatile uint64_t *)(v + 16); *next = (void *volatile *)(v + 24);
    return 0;
}
#include "dsl_chain.h"

typedef struct { uintptr_t site; uint8_t *thunk; void *tramp; int owner; int active; char name[48]; ChainSite cs; } Hook;
static Hook H[MAXH];
static int nh;
static void site_sync(Hook *h) {
    int t = ch_top(&h->cs);
    h->active = t >= 0;
    if (t >= 0) { h->owner = CL[h->cs.lk[t]].owner; snprintf(h->name, sizeof h->name, "%s", CL[h->cs.lk[t]].name); }
}

#define SLOT(h) ((void *volatile *)((h)->thunk + 24))

/* The thunks sit next to GameAssembly, like the Mac engine's region after the image. BepInEx (be.788
   INativeDetour.Create -> FollowExportThunks) follows E9 / FF 25 jumps from a method's start before Dobby patches it:
   through our site jump and MinHook's relay it lands on the dsload thunk, so Harmony's detour goes on the thunk.
   Dobby x64 needs an 8-byte RW slot within +-2 GB of what it patches (AllocIndirectStub); at a VirtualAlloc(NULL)
   address its search found none and it wrote through NULL (dobby.dll+0x2b62, VM and Wine 2026-10-18). Near
   GameAssembly it reuses the slot page of BepInEx's own il2cpp_runtime_invoke detour, or finds one as for game code.
   Below the image first (nearest free range), then above its end, within 1 GB; else anywhere (logged). */
#define NEAR_RANGE 0x40000000ull
static void *alloc_near_ga(size_t size, const char **where) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t g = si.dwAllocationGranularity, amin = (uintptr_t)si.lpMinimumApplicationAddress, amax = (uintptr_t)si.lpMaximumApplicationAddress;
    size = (size + g - 1) & ~(g - 1);
    uintptr_t base = dsl_ga_base & ~(g - 1), end = (dsl_ga_base + dsl_image_end + g - 1) & ~(g - 1);
    uintptr_t lo = base > amin + NEAR_RANGE ? base - NEAR_RANGE : amin, hi = end + NEAR_RANGE < amax ? end + NEAR_RANGE : amax;
    MEMORY_BASIC_INFORMATION mi;
    for (uintptr_t a = base - size; dsl_ga_base && base >= lo + size && a >= lo; ) {
        if (!VirtualQuery((void *)a, &mi, sizeof mi)) break;
        if (mi.State == MEM_FREE && (uintptr_t)mi.BaseAddress + mi.RegionSize >= a + size) {
            void *p = VirtualAlloc((void *)a, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (p) { *where = "below GameAssembly"; return p; }
        }
        uintptr_t b = mi.State == MEM_FREE ? a : (uintptr_t)mi.AllocationBase & ~(g - 1);
        if (b < lo + size) break;
        a = b == a ? a - g : b - size;
    }
    for (uintptr_t a = end; dsl_ga_base && a + size <= hi; ) {
        if (!VirtualQuery((void *)a, &mi, sizeof mi)) break;
        uintptr_t re = (uintptr_t)mi.BaseAddress + mi.RegionSize;
        if (mi.State == MEM_FREE && re >= a + size) {
            void *p = VirtualAlloc((void *)a, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (p) { *where = "above GameAssembly"; return p; }
            re = a + g;
        }
        a = (re + g - 1) & ~(g - 1);
    }
    *where = "far from GameAssembly: a BepInEx (Dobby) patch on a hooked function can crash";
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
}

int dsl_engine_init(void) {
    if (thunks) return 0;
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { dsl_log("engine: MH_Initialize failed (%d): hooks off", s); return -1; }
    const char *where = "";
    thunks = alloc_near_ga((size_t)MAXH * THUNK_SZ + (size_t)MAXV * VEN_SZ, &where);
    if (!thunks) { dsl_log("engine: no thunk memory: hooks off"); return -1; }
    dsl_log("engine: MinHook, %d thunks at %p (%s)", MAXH, (void *)thunks, where);
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

/* Where a jump at p goes: E9 rel32 -> its target; FF 25 [rip+disp32] (Dobby's patch, MinHook's relay) -> the address
   of the slot it reads (the slot's value can change later). 0 = another form. */
static uintptr_t jump_ref(const uint8_t *p) {
    if (p[0] == 0xE9) { int32_t r; memcpy(&r, p + 1, 4); return (uintptr_t)p + 5 + (intptr_t)r; }
    if (p[0] == 0xFF && p[1] == 0x25) { int32_t r; memcpy(&r, p + 2, 4); return (uintptr_t)p + 6 + (intptr_t)r; }
    return 0;
}

/* Reverse order (a BepInEx / Dobby patch first, our hook second): MinHook copies the other engine's jump into our
   trampoline (E9 -> FF 25 00000000 <abs>, FF 25 -> FF 25 with a new disp32) and ours runs first, then theirs.
   MinHook does not check that a new disp32 still reaches the slot (Dobby puts it up to 2 GB from the site): the copy
   must go to the same place, else the hook is taken back. */
static int same_jump(const uint8_t *site, const uint8_t *tramp) {
    uintptr_t want = jump_ref(site);
    if (site[0] == 0xE9) {
        uint64_t to; memcpy(&to, tramp + 6, 8);
        return tramp[0] == 0xFF && tramp[1] == 0x25 && !memcmp(tramp + 2, "\0\0\0\0", 4) && to == want;
    }
    return want && jump_ref(tramp) == want;
}

static void chain_watch_start(void);
static int hook_locked(int owner, void *target, void *repl, void **orig, const char *who, const char *method) {
    uintptr_t site = (uintptr_t)target;
    if (!dsl_build_id) { dsl_log("%s: hook refused: the game build is not the pinned one", who); return -1; }
    if (!thunks || !site || !repl) { dsl_log("%s: hook refused (null target/replacement or engine inactive)", who); return -1; }
    if (site < dsl_ga_base + dsl_text_lo || site >= dsl_ga_base + dsl_text_hi) {
        dsl_log("%s: hook refused: %p is not code inside GameAssembly", who, target); return -1;
    }
    uintptr_t rva = site - dsl_ga_base;
    Hook *h = find_site(site);
    if (h && !h->active) {
        const Patch *pq = patch_over(site, 5);
        if (pq) { dsl_log("%s: hook refused: %#llx is inside a code patch of %s (%s)", who, (unsigned long long)rva, dsl_owner_name(pq->owner), pq->name); return -1; }
    }
    if (h) {                                            /* a site hooked before (maybe by another module): chain */
        int had = h->cs.ven != 0;
        int r = ch_add(&h->cs, owner, repl, method, who, orig);
        site_sync(h);
        if (!r && !had && h->cs.ven) chain_watch_start();
        return r;
    }
    const Patch *pp = patch_over(site, 5);
    if (pp) { dsl_log("%s: hook refused: %#llx is inside a code patch of %s (%s)", who, (unsigned long long)rva, dsl_owner_name(pp->owner), pp->name); return -1; }
    int chain = foreign_jump((const uint8_t *)site);
    if (chain && !jump_ref((const uint8_t *)site)) {
        dsl_log("%s: hook refused: %#llx already starts with a jump (a static patch or another loader's hook)", who, (unsigned long long)rva);
        return -1;
    }
    if (nh >= MAXH || ncl >= CH_MAXL) { dsl_log("%s: hook refused: no free thunk left", who); return -1; }
    uint8_t *t = thunks + (size_t)nh * THUNK_SZ;
    void *tramp = NULL;
    MH_STATUS s = MH_CreateHook(target, t, &tramp);
    if (s != MH_OK) { dsl_log("%s: hook %#llx failed (MinHook create %d)", who, (unsigned long long)rva, s); return -1; }
    if (chain && !same_jump((const uint8_t *)site, tramp)) {
        MH_RemoveHook(target);
        dsl_log("%s: hook refused: %#llx starts with another loader's jump that MinHook could not copy", who, (unsigned long long)rva);
        return -1;
    }
    if (chain) dsl_log("%s: %#llx already starts with another loader's jump (BepInEx/Dobby?): chained, ours runs first", who, (unsigned long long)rva);
    make_thunk(t, tramp, NULL);                         /* the slot is set by the first link below, before the enable */
    dsl_mark_codepatch();
    s = batch ? MH_QueueEnableHook(target) : MH_EnableHook(target);
    if (s != MH_OK) { MH_RemoveHook(target); dsl_log("%s: hook %#llx failed (MinHook enable %d)", who, (unsigned long long)rva, s); return -1; }
    if (batch) queued++;
    Hook *n = &H[nh++];
    memset(n, 0, sizeof *n);
    n->site = site; n->thunk = t; n->tramp = tramp;
    n->cs.slot = SLOT(n); n->cs.orig_code = tramp; n->cs.rva = rva;
    ch_add(&n->cs, owner, repl, method, who, orig);   /* first link: cannot be refused (room checked above) */
    site_sync(n);
    return 0;
}

int dsl_hook_m(int owner, void *target, void *repl, void **orig, const char *who, const char *method) {
    AcquireSRWLockExclusive(&lock);
    int r = hook_locked(owner, target, repl, orig, who, method);
    ReleaseSRWLockExclusive(&lock);
    if (!r) dsl_log("%s: hooked %#llx for %s", who, (unsigned long long)((uintptr_t)target - dsl_ga_base), dsl_owner_name(owner));
    return r;
}

int dsl_hook(int owner, void *target, void *repl, void **orig, const char *who) { return dsl_hook_m(owner, target, repl, orig, who, NULL); }

int dsl_unhook(int owner, void *target, const char *who) {
    AcquireSRWLockExclusive(&lock);
    Hook *h = find_site((uintptr_t)target);
    if (h) ch_check(&h->cs);                            /* skip guard before the counts restart */
    int ok = h && ch_remove(&h->cs, owner);
    if (h) site_sync(h);
    ReleaseSRWLockExclusive(&lock);
    if (!ok) { dsl_log("%s: unhook %p: not hooked by this module", who, target); return -1; }
    dsl_log("%s: unhooked %#llx (%s)", who, (unsigned long long)((uintptr_t)target - dsl_ga_base), h->active ? "the other hooks on it still run" : "original code runs again");
    return 0;
}

int dsl_unhook_owner(int owner) {
    int n = 0;
    AcquireSRWLockExclusive(&lock);
    for (int i = 0; i < nh; i++) if (ch_link_of(&H[i].cs, owner)) { ch_check(&H[i].cs); n += ch_remove(&H[i].cs, owner); site_sync(&H[i]); }
    for (int i = 0; i < npt; i++) if (PT[i].active && PT[i].owner == owner) {        /* put the original bytes back */
        if (!memcmp((void *)PT[i].addr, PT[i].neu, PT[i].n) && !write_ga(PT[i].addr, PT[i].old, PT[i].n)) { PT[i].active = 0; n++; }
    }
    ReleaseSRWLockExclusive(&lock);
    return n;
}

/* skip guard runs: every 2 s while a chained site exists, and at exit */
static void chain_check_all(void) { for (int i = 0; i < nh; i++) if (H[i].cs.ven) ch_check(&H[i].cs); }
static DWORD WINAPI chain_watch(LPVOID u) {
    (void)u;
    for (;;) { Sleep(2000); AcquireSRWLockExclusive(&lock); chain_check_all(); ReleaseSRWLockExclusive(&lock); }
    return 0;
}
static void chain_at_exit(void) { if (TryAcquireSRWLockExclusive(&lock)) { chain_check_all(); ReleaseSRWLockExclusive(&lock); } }
static void chain_watch_start(void) {              /* engine lock held */
    static int started;
    if (started) return;
    started = 1;
    HANDLE t = CreateThread(NULL, 0, chain_watch, NULL, 0, NULL);
    if (t) CloseHandle(t);
    atexit(chain_at_exit);
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
    for (int i = 0; i < nh; i++) for (int k = 0; k < H[i].cs.nlk; k++) {
        const ChainLink *l = &CL[H[i].cs.lk[k]];
        if (l->active && (owner == -2 || l->owner == owner)) n++;
    }
    for (int i = 0; i < npt; i++) if (PT[i].active && (owner == -2 || PT[i].owner == owner)) n++;
    ReleaseSRWLockExclusive(&lock);
    return n;
}
