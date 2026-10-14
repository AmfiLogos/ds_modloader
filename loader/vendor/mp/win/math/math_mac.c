// math_mac.c - Mac side of the deterministic math swap (cross-play with Windows, see win/NOTES.md).
// GameAssembly.dylib calls libm through lazy pointers (__la_symbol_ptr). For the 27 libm symbols that game
// code uses, this file writes the address of the shared dsm_ function (musl, no FMA; win/math/dsmath.h) into the
// pointer slot, before any GameAssembly code runs. The Windows side (dsmath_win.c) redirects the UCRT entry
// points to the same functions. hypot is left alone on both sides (no Windows twin found yet).
// Self-contained: registers its own add-image callback, no change to rig_main.c needed.
// On when DSMP_MATH=1, or <DSRIG_DIR>/dsmp_math.on exists, or built with -DDSMATH_DEFAULT_ON=1 (src/build.sh since v9).
// Off when DSMP_MATH=0 or <DSRIG_DIR>/dsmp_math.off exists (wins over the rest).
#include "dsmath.h"
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void rig_log(const char *fmt, ...);

#define GA_UUID "3C5DD8BD-E371-3C0E-A674-A81042B0AC9F"   /* GameAssembly.dylib 25846389 arm64 (= RIG_GA_UUID) */

typedef struct { float s, c; } SCf;
typedef struct { double s, c; } SCd;
/* Windows calls sin and cos separately for these, so the Mac does the same (not dsm_sincos*) */
static SCf w_sincosf(float x) { SCf r = { dsm_sinf(x), dsm_cosf(x) }; return r; }
static SCd w_sincos(double x) { SCd r = { dsm_sin(x), dsm_cos(x) }; return r; }
/* the Windows game code reaches powf for exp2f (UCRT 0x4DC810) */
static float w_exp2f(float x) { return dsm_powf(2.0f, x); }

#define hypot_SKIP 1
static const struct { uint64_t rva; const char *sym; void *fn; } SLOTS[] = {
#define SLOT(rva, sym) { rva, #sym, (void *)FN_##sym },
#define FN___sincos_stret w_sincos
#define FN___sincosf_stret w_sincosf
#define FN_exp2f w_exp2f
#define FN_hypot NULL
#define FN_acos dsm_acos
#define FN_acosf dsm_acosf
#define FN_asin dsm_asin
#define FN_atan dsm_atan
#define FN_atan2 dsm_atan2
#define FN_atan2f dsm_atan2f
#define FN_atanf dsm_atanf
#define FN_cos dsm_cos
#define FN_cosf dsm_cosf
#define FN_cosh dsm_cosh
#define FN_exp dsm_exp
#define FN_expf dsm_expf
#define FN_log dsm_log
#define FN_log10 dsm_log10
#define FN_log10f dsm_log10f
#define FN_logf dsm_logf
#define FN_pow dsm_pow
#define FN_powf dsm_powf
#define FN_sin dsm_sin
#define FN_sinf dsm_sinf
#define FN_sinh dsm_sinh
#define FN_tan dsm_tan
#define FN_tanf dsm_tanf
#define FN_tanh dsm_tanh
#include "mac_libm_slots.h"
#undef SLOT
};

static int enabled(void) {
    const char *e = getenv("DSMP_MATH"), *d = getenv("DSRIG_DIR");
    char p[1100];
    if (e && *e == '0') return 0;                                           /* off switches (tests, measurements) */
    if (d && *d) { snprintf(p, sizeof p, "%s/dsmp_math.off", d); if (access(p, F_OK) == 0) return 0; }
#ifdef DSMATH_DEFAULT_ON
    return 1;
#endif
    if (e && *e == '1') return 1;
    if (d && *d) { snprintf(p, sizeof p, "%s/dsmp_math.on", d); if (access(p, F_OK) == 0) return 1; }
    return 0;
}

static void uuid_of(const struct mach_header_64 *h, char *out, size_t n) {
    out[0] = 0;
    const uint8_t *p = (const uint8_t *)(h + 1);
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct load_command *lc = (const void *)p;
        if (lc->cmd == LC_UUID) {
            const uint8_t *u = ((const struct uuid_command *)lc)->uuid;
            snprintf(out, n, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                     u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
        }
        p += lc->cmdsize;
    }
}

static int done;
static void on_image(const struct mach_header *mh, intptr_t slide) {
    (void)slide;
    if (done) return;
    Dl_info di;
    if (!dladdr(mh, &di) || !di.dli_fname || !strstr(di.dli_fname, "/GameAssembly.dylib")) return;
    done = 1;
    char uuid[40]; uuid_of((const struct mach_header_64 *)mh, uuid, sizeof uuid);
    if (strcmp(uuid, GA_UUID)) { rig_log("dsmath: GameAssembly uuid %s is not 25846389: libm left alone", uuid); return; }
    uintptr_t base = (uintptr_t)mh;
    int n = 0, bad = 0;
    for (size_t i = 0; i < sizeof SLOTS / sizeof *SLOTS; i++) {
        if (!SLOTS[i].fn) continue;
        void **slot = (void **)(base + SLOTS[i].rva);
        mach_vm_address_t pg = (uintptr_t)slot & ~(uintptr_t)(vm_page_size - 1);
        if (mach_vm_protect(mach_task_self(), pg, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS) { bad++; continue; }
        *slot = SLOTS[i].fn;   /* __la_symbol_ptr is in __DATA (stays writable) */
        n++;
    }
    rig_log("dsmath %s: %d libm pointers of GameAssembly redirected%s", DSMATH_VERSION, n, bad ? " (SOME FAILED)" : "");
}

/* the probe for tests: returns the function a GameAssembly libm slot now points to */
void *dsmath_mac_slot(uintptr_t base, const char *sym) {
    for (size_t i = 0; i < sizeof SLOTS / sizeof *SLOTS; i++)
        if (!strcmp(SLOTS[i].sym, sym)) return *(void **)(base + SLOTS[i].rva);
    return NULL;
}

__attribute__((constructor)) static void dsmath_mac_init(void) {
    if (!enabled()) return;
    _dyld_register_func_for_add_image(on_image);
}
