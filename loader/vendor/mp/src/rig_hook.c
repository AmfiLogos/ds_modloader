// Runtime entry hooks, copied from ~/DungeonSettlersMods/loader/src/dsl_hook.c (same stub shape):
//   site:  b stub
//   stub:  adrp x16, slot@PAGE ; ldr x16, [x16, slot@PAGEOFF] ; cbz x16, fb ; br x16
//   fb:    <displaced first instruction> ; b site+4          (ORIG = stub+16)
// Written in memory only, before any game code runs (from the GameAssembly add-image callback).
#include "rig.h"
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>
#include <string.h>

static uintptr_t next_stub, next_slot, stub_hi, slot_hi;
static uintptr_t late_stub, late_hi;      /* hooks installed after the game started (e.g. ui_teams on its first frame) */
int rig_hooks_late;                       /* set by rig_main at the first MainFlow.Update */

/* Hook table (2026-10-12: 80 -> 682). Stubs and slots live in one anonymous region mapped right
   after GameAssembly's image (__LINKEDIT ends at RIG_IMAGE_END), so every site in __TEXT is within the
   +-128 MB reach of its `b stub`. Page 1: stubs (RX, re-protected around each write); page 2: slots (RW). */
#define RIG_IMAGE_END 0x6C94000ULL
static int table_init(void) {
    mach_vm_size_t sz = 3 * 0x4000;
    for (uintptr_t a = ds_base + RIG_IMAGE_END; a + sz < ds_base + 0x7F00000ULL; a += 0x100000) {
        mach_vm_address_t addr = a;
        if (mach_vm_allocate(mach_task_self(), &addr, sz, VM_FLAGS_FIXED) != KERN_SUCCESS) continue;
        next_stub = addr; stub_hi = addr + 0x4000;
        next_slot = addr + 0x4000; slot_hi = addr + 0x8000;
        late_stub = addr + 0x8000; late_hi = addr + 0xC000;     /* own page: making it writable never touches */
        mach_vm_protect(mach_task_self(), late_stub, 0x4000, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);   /* stubs other threads run */
        mach_vm_protect(mach_task_self(), addr, 0x4000, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
        rig_log("hook table at GameAssembly+%#lx: up to %d hooks", (unsigned long)(addr - ds_base), (int)(0x4000 / 24));
        return 0;
    }
    /* fallback: the old cave in __TEXT's zero tail (80 hooks) */
    next_stub = ds_base + RIG_CAVE_LO; stub_hi = ds_base + RIG_CAVE_HI;
    next_slot = ds_base + RIG_SLOT_LO; slot_hi = ds_base + RIG_SLOT_HI;
    rig_log("hook table: no free region after the image, using the 80-stub cave");
    return 0;
}
static int in_table(uintptr_t a) { return a >= ds_base + RIG_IMAGE_END; }

static int is_pc_relative(uint32_t i) {
    if ((i & 0x1F000000) == 0x10000000) return 1;   /* adr, adrp */
    if ((i & 0x7C000000) == 0x14000000) return 1;   /* b, bl */
    if ((i & 0xFF000010) == 0x54000000) return 1;   /* b.cond */
    if ((i & 0x7E000000) == 0x34000000) return 1;   /* cbz, cbnz */
    if ((i & 0x7E000000) == 0x36000000) return 1;   /* tbz, tbnz */
    if ((i & 0x3B000000) == 0x18000000) return 1;   /* ldr (literal) */
    return 0;
}

static int write_code(uintptr_t addr, const void *src, size_t n) {
    mach_vm_address_t pg = addr & ~(uintptr_t)(vm_page_size - 1);
    mach_vm_size_t sz = ((addr + n + vm_page_size - 1) & ~(uintptr_t)(vm_page_size - 1)) - pg;
    if (mach_vm_protect(mach_task_self(), pg, sz, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return -1;
    memcpy((void*)addr, src, n);
    kern_return_t kr = mach_vm_protect(mach_task_self(), pg, sz, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    sys_icache_invalidate((void*)addr, n);
    return kr == KERN_SUCCESS ? 0 : -1;
}
static int all_zero(uintptr_t a, size_t n) { for (size_t i = 0; i < n; i++) if (((volatile uint8_t*)a)[i]) return 0; return 1; }
static uint32_t enc_b(uintptr_t from, uintptr_t to) { return 0x14000000u | (uint32_t)(((intptr_t)(to - from) >> 2) & 0x3FFFFFF); }

int rig_hook(uintptr_t rva, void *repl, void **orig, const char *who) {
    if (!next_stub) table_init();
    uintptr_t site = ds_base + rva;
    uint32_t insn = *(volatile uint32_t*)site;
    if (is_pc_relative(insn)) { rig_log("%s: hook refused: %#lx starts with PC-relative %08x", who, (unsigned long)rva, insn); return -1; }
    /* after start, stubs go to their own page: making it writable never touches stubs other threads may run */
    uintptr_t *ns = (rig_hooks_late && late_stub) ? &late_stub : &next_stub;
    uintptr_t hi = (ns == &late_stub) ? late_hi : stub_hi;
    if (*ns + 24 > hi || next_slot + 8 > slot_hi) { rig_log("%s: no stub left", who); return -1; }
    uintptr_t stub = *ns, slot = next_slot;
    if (!all_zero(stub, 24) || !all_zero(slot, 8)) { rig_log("%s: stub/slot area not free", who); return -1; }
    int64_t pages = (int64_t)((slot & ~(uintptr_t)0xFFF) - (stub & ~(uintptr_t)0xFFF)) >> 12;
    uint32_t code[6];
    code[0] = 0x90000000u | ((uint32_t)(pages & 3) << 29) | ((uint32_t)((pages >> 2) & 0x7FFFF) << 5) | 16;
    code[1] = 0xF9400000u | ((uint32_t)((slot & 0xFFF) / 8) << 10) | (16 << 5) | 16;
    code[2] = 0xB4000000u | (2u << 5) | 16;
    code[3] = 0xD61F0200u;
    code[4] = insn;
    code[5] = enc_b(stub + 20, site + 4);
    if (in_table(stub)) {                       /* our own anonymous page: RW, write, back to RX */
        mach_vm_address_t pg = stub & ~(uintptr_t)(vm_page_size - 1);
        if (mach_vm_protect(mach_task_self(), pg, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS) { rig_log("%s: stub page RW failed", who); return -1; }
        memcpy((void *)stub, code, sizeof code);
        if (mach_vm_protect(mach_task_self(), pg, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE) != KERN_SUCCESS) { rig_log("%s: stub page RX failed", who); return -1; }
        sys_icache_invalidate((void *)stub, sizeof code);
    } else if (write_code(stub, code, sizeof code)) { rig_log("%s: stub write failed", who); return -1; }
    *(void* volatile*)slot = repl;
    uint32_t br = enc_b(site, stub);
    if (write_code(site, &br, 4)) { *(void* volatile*)slot = NULL; rig_log("%s: site write failed", who); return -1; }
    *ns += 24; next_slot += 8;
    if (orig) *orig = (void*)(stub + 16);
    rig_log("%s: hooked %#lx", who, (unsigned long)rva);
    return 0;
}
