// The one hook engine of the process (merge of DungeonSettlersMods dsl_hook.c and ds_multiplayer rig_hook.c).
// Same entry-stub shape as both:
//   site:  b stub
//   stub:  adrp x16, slot@PAGE ; ldr x16, [x16, slot@PAGEOFF] ; cbz x16, fb ; br x16
//   fb:    <displaced first instruction> ; b site+4          (ORIG = stub+16)
// slot = 0 -> the original runs (unhook).
// Stubs and slots live in ONE anonymous region mapped right after GameAssembly's image (rig_hook.c's idea:
// every __TEXT site stays within the +-128 MB reach of `b`). The __TEXT zero tail is never used, so static
// patches there (multi-unit bars, GCD, weapon slot, Hell) and an old dsmods.dylib cave cannot collide with us.
// Stub pages are written through a second RW mapping of the same memory: the RX pages other threads run are
// never made non-executable. Only the 4-byte site write toggles a GameAssembly page (as both old engines did).
#include "dsl_internal.h"
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

#define EARLY_SZ 0x8000          /* 1365 stubs before game code runs */
#define LATE_SZ  0x4000          /* 682 stubs installed while the game runs (own page) */
#define SLOT_SZ  0x4000          /* 2048 slots */
#define REGION_SZ (EARLY_SZ + LATE_SZ + SLOT_SZ)
#define MAXH 2048

typedef struct { uintptr_t site, stub, slot; uint32_t insn; int owner; int active; char name[48]; } Hook;
static Hook H[MAXH];
static int nh;
static uintptr_t region, region_w;                 /* RX view, RW alias (0 = no alias: toggle instead) */
static uintptr_t next_early, next_late, next_slot;
static int late;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int dsl_codepatch_ok = 1;

static int is_pc_relative(uint32_t i) {
    if ((i & 0x1F000000) == 0x10000000) return 1;   /* adr, adrp */
    if ((i & 0x7C000000) == 0x14000000) return 1;   /* b, bl */
    if ((i & 0xFF000010) == 0x54000000) return 1;   /* b.cond */
    if ((i & 0x7E000000) == 0x34000000) return 1;   /* cbz, cbnz */
    if ((i & 0x7E000000) == 0x36000000) return 1;   /* tbz, tbnz */
    if ((i & 0x3B000000) == 0x18000000) return 1;   /* ldr/ldrsw/prfm (literal), incl. SIMD */
    return 0;
}
static uint32_t enc_b(uintptr_t from, uintptr_t to) { return 0x14000000u | (uint32_t)(((intptr_t)(to - from) >> 2) & 0x3FFFFFF); }
static int b_reach(uintptr_t from, uintptr_t to) { intptr_t d = (intptr_t)(to - from); return d >= -(1 << 27) && d < (1 << 27); }

int dsl_engine_init(void) {
    if (region) return 0;
    mach_port_t me = mach_task_self();
    uintptr_t start = (dsl_ga_base + dsl_image_end + 0x3FFF) & ~(uintptr_t)0x3FFF;
    for (uintptr_t a = start; a + REGION_SZ <= dsl_ga_base + dsl_text_lo + (1u << 27) - 0x100000; a += 0x4000) {
        mach_vm_address_t addr = a;
        if (mach_vm_allocate(me, &addr, REGION_SZ, VM_FLAGS_FIXED) != KERN_SUCCESS) continue;
        region = addr;
        break;
    }
    if (!region) { dsl_log("engine: no free region within branch range after GameAssembly: hooks off"); return -1; }
    mach_vm_protect(me, region, EARLY_SZ + LATE_SZ, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    mach_vm_address_t w = 0; vm_prot_t cur, max;
    if (mach_vm_remap(me, &w, EARLY_SZ + LATE_SZ, 0, VM_FLAGS_ANYWHERE, me, region, FALSE, &cur, &max, VM_INHERIT_NONE) == KERN_SUCCESS &&
        mach_vm_protect(me, w, EARLY_SZ + LATE_SZ, FALSE, VM_PROT_READ | VM_PROT_WRITE) == KERN_SUCCESS) region_w = w;
    next_early = region; next_late = region + EARLY_SZ; next_slot = region + EARLY_SZ + LATE_SZ;
    dsl_log("engine: hook region at GameAssembly+%#lx (%d early + %d late stubs, %d slots), stub writes %s",
            (unsigned long)(region - dsl_ga_base), EARLY_SZ / 24, LATE_SZ / 24, SLOT_SZ / 8,
            region_w ? "through a RW alias (RX pages never change)" : "by RW/RX toggling (no alias)");
    return 0;
}

void dsl_engine_batch(int on) { (void)on; }                         /* Windows only (MinHook's thread freeze per hook) */
void dsl_engine_set_late(void) { pthread_mutex_lock(&lock); if (!late) dsl_log("engine: game code is running: new stubs go to the late page"); late = 1; pthread_mutex_unlock(&lock); }

static int write_ga(uintptr_t addr, const void *src, size_t n) {   /* one GameAssembly site (file-backed page) */
    if (!dsl_codepatch_ok) return -1;
    dsl_mark_codepatch();
    mach_vm_address_t pg = addr & ~(uintptr_t)(vm_page_size - 1);
    mach_vm_size_t sz = ((addr + n + vm_page_size - 1) & ~(uintptr_t)(vm_page_size - 1)) - pg;
    if (mach_vm_protect(mach_task_self(), pg, sz, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return -1;
    memcpy((void*)addr, src, n);
    kern_return_t kr = mach_vm_protect(mach_task_self(), pg, sz, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    sys_icache_invalidate((void*)addr, n);
    return kr == KERN_SUCCESS ? 0 : -1;
}
static int write_stub(uintptr_t stub, const void *src, size_t n) {
    if (region_w) { memcpy((void*)(region_w + (stub - region)), src, n); sys_icache_invalidate((void*)stub, n); return 0; }
    mach_vm_address_t pg = stub & ~(uintptr_t)(vm_page_size - 1);
    if (mach_vm_protect(mach_task_self(), pg, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS) return -1;
    memcpy((void*)stub, src, n);
    kern_return_t kr = mach_vm_protect(mach_task_self(), pg, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    sys_icache_invalidate((void*)stub, n);
    return kr == KERN_SUCCESS ? 0 : -1;
}
static int all_zero(uintptr_t a, size_t n) { for (size_t i = 0; i < n; i++) if (((volatile uint8_t*)a)[i]) return 0; return 1; }
static Hook *find_site(uintptr_t site) { for (int i = 0; i < nh; i++) if (H[i].site == site) return &H[i]; return NULL; }

/* In-place code patches (expected bytes -> new bytes), e.g. a custom difficulty's compare changes. Same registry
   rules as hooks: a patch never overlaps an active hook site or another module's patch. */
#define MAXPT 256
typedef struct { uintptr_t addr; uint32_t n; uint8_t old[16], neu[16]; int owner; int active; char name[48]; } Patch;
static Patch PT[MAXPT];
static int npt;
static const Patch *patch_over(uintptr_t a, size_t n) {
    for (int i = 0; i < npt; i++) if (PT[i].active && a < PT[i].addr + PT[i].n && PT[i].addr < a + n) return &PT[i];
    return NULL;
}

static int hook_locked(int owner, void *target, void *repl, void **orig, const char *who) {
    uintptr_t site = (uintptr_t)target;
    if (!dsl_build_id) { dsl_log("%s: hook refused: the game build is not the pinned one", who); return -1; }
    if (!region || !site || !repl) { dsl_log("%s: hook refused (null target/replacement or engine inactive)", who); return -1; }
    if ((site & 3) || site < dsl_ga_base + dsl_text_lo || site >= dsl_ga_base + dsl_text_hi) {
        dsl_log("%s: hook refused: %p is not code inside GameAssembly", who, target); return -1;
    }
    uintptr_t rva = site - dsl_ga_base;
    Hook *h = find_site(site);
    if (h && h->active) { dsl_log("%s: hook refused: %#lx is already hooked by %s (%s)", who, (unsigned long)rva, dsl_owner_name(h->owner), h->name); return -1; }
    if (h) {                                            /* re-hook a site unhooked earlier: reuse its stub */
        *(void* volatile*)h->slot = repl; h->owner = owner; h->active = 1;
        if (orig) *orig = (void*)(h->stub + 16);
        dsl_log("%s: hook %#lx re-armed", who, (unsigned long)rva);
        return 0;
    }
    const Patch *pp = patch_over(site, 4);
    if (pp) { dsl_log("%s: hook refused: %#lx is inside a code patch of %s (%s)", who, (unsigned long)rva, dsl_owner_name(pp->owner), pp->name); return -1; }
    uint32_t insn = *(volatile uint32_t*)site;
    if ((insn & 0xFC000000) == 0x14000000) {
        uintptr_t to = site + (uintptr_t)((intptr_t)((int32_t)(insn << 6) >> 6) * 4);
        dsl_log("%s: hook refused: %#lx already starts with a branch to %s%#lx (a static patch or another loader's hook)", who,
                (unsigned long)rva, (to >= dsl_ga_base && to < dsl_ga_base + dsl_image_end) ? "GameAssembly+" : "",
                (unsigned long)((to >= dsl_ga_base && to < dsl_ga_base + dsl_image_end) ? to - dsl_ga_base : to));
        return -1;
    }
    if (is_pc_relative(insn)) { dsl_log("%s: hook refused: %#lx starts with a PC-relative instruction %08x (maybe another loader's hook)", who, (unsigned long)rva, insn); return -1; }
    uintptr_t *ns = late ? &next_late : &next_early;
    uintptr_t hi = late ? region + EARLY_SZ + LATE_SZ : region + EARLY_SZ;
    if (nh >= MAXH || *ns + 24 > hi || next_slot + 8 > region + REGION_SZ) { dsl_log("%s: hook refused: no free stub/slot left", who); return -1; }
    uintptr_t stub = *ns, slot = next_slot;
    if (!all_zero(stub, 24) || !all_zero(slot, 8)) { dsl_log("%s: hook refused: stub/slot not free", who); return -1; }
    if (!b_reach(site, stub) || !b_reach(stub + 20, site + 4)) { dsl_log("%s: hook refused: stub out of branch range", who); return -1; }
    int64_t pages = (int64_t)((slot & ~(uintptr_t)0xFFF) - (stub & ~(uintptr_t)0xFFF)) >> 12;
    uint32_t code[6];
    code[0] = 0x90000000u | ((uint32_t)(pages & 3) << 29) | ((uint32_t)((pages >> 2) & 0x7FFFF) << 5) | 16;   /* adrp x16 */
    code[1] = 0xF9400000u | ((uint32_t)((slot & 0xFFF) / 8) << 10) | (16 << 5) | 16;                         /* ldr x16,[x16,#off] */
    code[2] = 0xB4000000u | (2u << 5) | 16;                                                                   /* cbz x16, +8 */
    code[3] = 0xD61F0200u;                                                                                    /* br x16 */
    code[4] = insn;                                                                                           /* displaced */
    code[5] = enc_b(stub + 20, site + 4);                                                                     /* b site+4 */
    if (write_stub(stub, code, sizeof code)) { dsl_log("%s: hook %#lx failed: stub write", who, (unsigned long)rva); return -1; }
    *(void* volatile*)slot = repl;
    uint32_t br = enc_b(site, stub);
    if (write_ga(site, &br, 4)) { *(void* volatile*)slot = NULL; dsl_log("%s: hook %#lx failed: site write", who, (unsigned long)rva); return -1; }
    Hook *n = &H[nh++];
    *n = (Hook){ site, stub, slot, insn, owner, 1, "" };
    snprintf(n->name, sizeof n->name, "%s", who);
    *ns += 24; next_slot += 8;
    if (orig) *orig = (void*)(stub + 16);
    return 0;
}

int dsl_hook(int owner, void *target, void *repl, void **orig, const char *who) {
    pthread_mutex_lock(&lock);
    int r = hook_locked(owner, target, repl, orig, who);
    pthread_mutex_unlock(&lock);
    if (!r) dsl_log("%s: hooked %#lx for %s", who, (unsigned long)((uintptr_t)target - dsl_ga_base), dsl_owner_name(owner));
    return r;
}

int dsl_unhook(int owner, void *target, const char *who) {
    pthread_mutex_lock(&lock);
    Hook *h = find_site((uintptr_t)target);
    int ok = h && h->active && h->owner == owner;
    if (ok) { *(void* volatile*)h->slot = NULL; h->active = 0; }
    pthread_mutex_unlock(&lock);
    if (!ok) { dsl_log("%s: unhook %p: not hooked by this module", who, target); return -1; }
    dsl_log("%s: unhooked %#lx (original code runs again)", who, (unsigned long)((uintptr_t)target - dsl_ga_base));
    return 0;
}

int dsl_unhook_owner(int owner) {
    int n = 0;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < nh; i++) if (H[i].active && H[i].owner == owner) { *(void* volatile*)H[i].slot = NULL; H[i].active = 0; n++; }
    for (int i = 0; i < npt; i++) if (PT[i].active && PT[i].owner == owner) {        /* put the original bytes back */
        if (!memcmp((void*)PT[i].addr, PT[i].neu, PT[i].n) && !write_ga(PT[i].addr, PT[i].old, PT[i].n)) { PT[i].active = 0; n++; }
    }
    pthread_mutex_unlock(&lock);
    return n;
}

int dsl_patch(int owner, void *target, const void *old, const void *neu, size_t n, const char *who) {
    uintptr_t a = (uintptr_t)target;
    if (!dsl_build_id) { dsl_log("%s: patch refused: the game build is not the pinned one", who); return -1; }
    if (!region || !a || !old || !neu || n == 0 || n > 16 || (a & 3) || (n & 3)) { dsl_log("%s: patch refused (bad arguments or engine inactive)", who); return -1; }
    if (a < dsl_ga_base + dsl_text_lo || a + n > dsl_ga_base + dsl_text_hi) { dsl_log("%s: patch refused: %p is not code inside GameAssembly", who, target); return -1; }
    uintptr_t rva = a - dsl_ga_base;
    pthread_mutex_lock(&lock);
    int r = -1;
    const Patch *o = patch_over(a, n);
    Hook *h = NULL;
    for (int i = 0; i < nh; i++) if (H[i].active && H[i].site < a + n && a < H[i].site + 4) h = &H[i];
    if (o) dsl_log("%s: patch %#lx refused: overlaps a patch of %s (%s)", who, (unsigned long)rva, dsl_owner_name(o->owner), o->name);
    else if (h) dsl_log("%s: patch %#lx refused: overlaps a hook of %s (%s)", who, (unsigned long)rva, dsl_owner_name(h->owner), h->name);
    else if (npt >= MAXPT) dsl_log("%s: patch refused: table full", who);
    else if (memcmp((void*)a, old, n)) {
        char have[40] = ""; for (size_t i = 0; i < n; i++) snprintf(have + 2 * i, 3, "%02x", ((uint8_t*)a)[i]);
        dsl_log("%s: patch %#lx refused: bytes are %s, not the expected ones (other build, static patch or another loader)", who, (unsigned long)rva, have);
    } else if (write_ga(a, neu, n)) dsl_log("%s: patch %#lx failed: write", who, (unsigned long)rva);
    else {
        Patch *p = &PT[npt++]; memset(p, 0, sizeof *p);
        p->addr = a; p->n = (uint32_t)n; memcpy(p->old, old, n); memcpy(p->neu, neu, n); p->owner = owner; p->active = 1;
        snprintf(p->name, sizeof p->name, "%s", who);
        r = 0;
    }
    pthread_mutex_unlock(&lock);
    if (!r) dsl_log("%s: patched %#lx (%zu bytes) for %s", who, (unsigned long)rva, n, dsl_owner_name(owner));
    return r;
}

int dsl_hook_count(int owner) {
    int n = 0;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < nh; i++) if (H[i].active && (owner == -2 || H[i].owner == owner)) n++;
    for (int i = 0; i < npt; i++) if (PT[i].active && (owner == -2 || PT[i].owner == owner)) n++;
    pthread_mutex_unlock(&lock);
    return n;
}
