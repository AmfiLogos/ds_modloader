// A foreign loader (stands in for MelonLoader / Doorstop): own hook engine, absolute-jump detour
// (ldr x16, #8 ; br x16 ; .quad target) written over the first 16 bytes of Foreign_Target. Records the env it saw.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>
int foreign_hits;
static void my_foreign(void *a, void *b) { foreign_hits++; }
__attribute__((constructor)) static void foreign_init(void) {
    const char *e = getenv("DYLD_INSERT_LIBRARIES");
    FILE *f = fopen(getenv("FOREIGN_OUT"), "w");
    if (f) { fprintf(f, "foreign loader saw DYLD_INSERT_LIBRARIES=%s\n", e ? e : "(unset)"); fclose(f); }
    void *t = dlsym(RTLD_DEFAULT, "Foreign_Target");
    if (!t || !getenv("FOREIGN_HOOK")) return;
    uint32_t code[4] = { 0x58000050, 0xD61F0200, 0, 0 };            /* ldr x16, #8 ; br x16 */
    void *tgt = (void *)my_foreign; memcpy(&code[2], &tgt, 8);
    mach_vm_address_t pg = (uintptr_t)t & ~(uintptr_t)(vm_page_size - 1);
    mach_vm_protect(mach_task_self(), pg, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    memcpy(t, code, 16);
    mach_vm_protect(mach_task_self(), pg, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    sys_icache_invalidate(t, 16);
}
