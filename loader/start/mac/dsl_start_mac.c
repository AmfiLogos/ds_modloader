// Mac start part of the loader (dsload.dylib, injected first in DYLD_INSERT_LIBRARIES). Moved here unchanged from
// src/dsl_main.c when the core became OS-neutral (src/dsl_os.h).
// Constructor (before GameAssembly is mapped):
//   1. log; DYLD_INSERT_LIBRARIES: remove only our own entry, keep every other loader's entry;
//   2. read the GameAssembly file's arm64 LC_UUID -> game build (dsl_builds.h); unknown build -> nothing loads;
//   3. dsl_core_start (mods scan, dlopen, dsl_module_load).
// GameAssembly add-image callback (none of its code has run): same LC_UUID as the file -> dsl_core_ga_mapped.
#include "dsl_internal.h"
#include "dsl_os.h"
#include "dsl_builds.h"
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <mach-o/nlist.h>
#include <CommonCrypto/CommonDigest.h>
#include <libkern/OSByteOrder.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crt_externs.h>
#include <CoreFoundation/CoreFoundation.h>

static const struct symtab_command *symtab;
static uintptr_t linkedit_base;
static char self_path[1024];

/* ------------------------------------------------------------------ OS calls of the core */
const char *dsl_os_build_list(void) {
    static char known[512];
    if (!known[0]) for (size_t i = 0; i < sizeof DSL_BUILDS / sizeof DSL_BUILDS[0]; i++)
        snprintf(known + strlen(known), sizeof known - strlen(known), "%s\"%s\"", i ? "," : "", DSL_BUILDS[i].build);
    return known;
}
int dsl_os_sha256_file(const char *path, char out[65]) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    CC_SHA256_CTX c; CC_SHA256_Init(&c);
    unsigned char buf[1 << 16]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) CC_SHA256_Update(&c, buf, (CC_LONG)n);
    fclose(f);
    unsigned char d[32]; CC_SHA256_Final(d, &c);
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
    return 0;
}
int dsl_os_sha256_buf(const void *data, size_t n, char out[65]) {
    unsigned char d[32]; CC_SHA256(data, (CC_LONG)n, d);
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
    return 0;
}
void *dsl_os_dlopen(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
void *dsl_os_dlsym(void *h, const char *name) { return dlsym(h, name); }
const char *dsl_os_dlerror(void) { return dlerror(); }
int dsl_os_batchmode(void) {
    char **av = *_NSGetArgv(); int ac = *_NSGetArgc();
    for (int i = 1; i < ac; i++) if (av[i] && !strcmp(av[i], "-batchmode")) return 1;
    return 0;
}
void dsl_os_notice(const char *title, const char *msg) {
    CFStringRef t = CFStringCreateWithCString(NULL, title, kCFStringEncodingUTF8);
    CFStringRef m = CFStringCreateWithCString(NULL, msg, kCFStringEncodingUTF8);
    if (t && m) CFUserNotificationDisplayNotice(0, kCFUserNotificationCautionAlertLevel, NULL, NULL, NULL, t, m, CFSTR("OK"));
    if (t) CFRelease(t);
    if (m) CFRelease(m);
}

/* ------------------------------------------------------------------ GameAssembly: file (before mapping) and image */
static void uuid_str(const uint8_t *u, char *out, size_t n) {
    snprintf(out, n, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7],u[8],u[9],u[10],u[11],u[12],u[13],u[14],u[15]);
}
static const char *build_of_uuid(const char *uuid) {
    for (size_t i = 0; i < sizeof DSL_BUILDS / sizeof DSL_BUILDS[0]; i++) if (!strcmp(DSL_BUILDS[i].uuid, uuid)) return DSL_BUILDS[i].build;
    return NULL;
}

/* arm64 LC_UUID of a (fat or thin) Mach-O file on disk */
static int file_uuid(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    uint8_t hdr[4096]; size_t got = fread(hdr, 1, sizeof hdr, f);
    long off = 0; int r = -1;
    if (got >= 8 && OSReadBigInt32(hdr, 0) == FAT_MAGIC) {
        uint32_t nf = OSReadBigInt32(hdr, 4);
        for (uint32_t i = 0; i < nf && 8 + 20 * (i + 1) <= got; i++)
            if (OSReadBigInt32(hdr, 8 + 20 * i) == (uint32_t)CPU_TYPE_ARM64) off = (long)OSReadBigInt32(hdr, 8 + 20 * i + 8);
        if (!off) { fclose(f); return -1; }
    }
    uint8_t *lc = malloc(1 << 20);
    if (fseek(f, off, SEEK_SET) == 0 && fread(lc, 1, 1 << 20, f) > sizeof(struct mach_header_64)) {
        const struct mach_header_64 *h = (const void*)lc;
        if (h->magic == MH_MAGIC_64 && h->cputype == CPU_TYPE_ARM64 && h->sizeofcmds + sizeof *h <= (1 << 20)) {
            const uint8_t *p = lc + sizeof *h;
            for (uint32_t i = 0; i < h->ncmds; i++) {
                const struct load_command *c = (const void*)p;
                if (c->cmd == LC_UUID) { uuid_str(((const struct uuid_command*)c)->uuid, out, n); r = 0; break; }
                p += c->cmdsize;
            }
        }
    }
    free(lc); fclose(f);
    return r;
}

static int parse_ga(const struct mach_header_64 *h, uintptr_t base, char *uuid, size_t un) {
    const uint8_t *p = (const uint8_t*)(h + 1);
    uint64_t text_vm = 0, le_vm = 0, le_off = 0; int have_text = 0, have_le = 0;
    uuid[0] = 0; dsl_text_lo = UINT64_MAX; dsl_text_hi = 0; dsl_image_end = 0;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct load_command *lc = (const void*)p;
        if (lc->cmd == LC_UUID) uuid_str(((const struct uuid_command*)lc)->uuid, uuid, un);
        else if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *s = (const void*)lc;
            if (s->vmaddr + s->vmsize > dsl_image_end && strcmp(s->segname, "__PAGEZERO")) dsl_image_end = s->vmaddr + s->vmsize;
            if (!strcmp(s->segname, "__TEXT")) {
                text_vm = s->vmaddr; have_text = 1;
                const struct section_64 *sec = (const void*)(s + 1);
                for (uint32_t k = 0; k < s->nsects; k++)
                    if (sec[k].size && (sec[k].flags & (S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS))) {
                        if (sec[k].addr < dsl_text_lo) dsl_text_lo = sec[k].addr;
                        if (sec[k].addr + sec[k].size > dsl_text_hi) dsl_text_hi = sec[k].addr + sec[k].size;
                    }
            } else if (!strcmp(s->segname, "__LINKEDIT")) { le_vm = s->vmaddr; le_off = s->fileoff; have_le = 1; }
        } else if (lc->cmd == LC_SYMTAB) symtab = (const void*)lc;
        p += lc->cmdsize;
    }
    if (!have_text || !have_le || !symtab || text_vm != 0 || dsl_text_lo >= dsl_text_hi) return -1;
    linkedit_base = base + (uintptr_t)(le_vm - text_vm) - (uintptr_t)le_off;
    return 0;
}

void *dsl_ga_sym(const char *name) {
    if (!symtab || !name || !dsl_ga_base) return NULL;
    const struct nlist_64 *nl = (const void*)(linkedit_base + symtab->symoff);
    const char *str = (const char*)(linkedit_base + symtab->stroff);
    for (uint32_t i = 0; i < symtab->nsyms; i++) {
        if ((nl[i].n_type & N_TYPE) != N_SECT || !(nl[i].n_type & N_EXT) || (nl[i].n_type & N_STAB)) continue;
        uint32_t sx = nl[i].n_un.n_strx; if (sx >= symtab->strsize) continue;
        const char *s = str + sx;
        if (s[0] == '_' && !strcmp(s + 1, name)) return (void*)(dsl_ga_base + nl[i].n_value);
    }
    return NULL;
}

static int mapped;
static void on_image(const struct mach_header *mh, intptr_t slide) {
    (void)slide;
    if (mapped) return;
    Dl_info di;
    if (!dladdr(mh, &di) || !di.dli_fname || !strstr(di.dli_fname, "/GameAssembly.dylib")) return;
    const struct mach_header_64 *h = (const void*)mh;
    if (h->magic != MH_MAGIC_64) return;
    mapped = 1;
    char uuid[40];
    if (parse_ga(h, (uintptr_t)mh, uuid, sizeof uuid)) { dsl_log("GameAssembly header not understood: modules get no game access"); dsl_build_id = NULL; return; }
    const char *b = build_of_uuid(uuid);
    if (!b || !dsl_build_id || strcmp(b, dsl_build_id)) {
        dsl_log("mapped GameAssembly (uuid %s) is not the file read at start (build %s): modules get no game access", uuid, dsl_build_id ? dsl_build_id : "?");
        dsl_core_ga_differs(uuid, b); return;
    }
    dsl_core_ga_mapped((uintptr_t)mh, di.dli_fname);
}

/* DYLD_INSERT_LIBRARIES: drop our own entry so children (crash reporter, helpers) do not load us; keep every
   other entry (BepInEx / MelonLoader doorstop, a profiler...) untouched and in its order. */
static void pass_through_env(void) {
    const char *e = getenv("DYLD_INSERT_LIBRARIES");
    if (!e) return;
    char rself[1024]; if (!realpath(self_path, rself)) snprintf(rself, sizeof rself, "%s", self_path);
    char *copy = strdup(e), *out = calloc(1, strlen(e) + 1), *save = NULL;
    int kept = 0, dropped = 0;
    for (char *t = strtok_r(copy, ":", &save); t; t = strtok_r(NULL, ":", &save)) {
        char rt[1024]; if (!realpath(t, rt)) snprintf(rt, sizeof rt, "%s", t);
        if (!strcmp(rt, rself)) { dropped++; continue; }
        if (kept++) strcat(out, ":");
        strcat(out, t);
    }
    if (kept) setenv("DYLD_INSERT_LIBRARIES", out, 1); else unsetenv("DYLD_INSERT_LIBRARIES");
    dsl_log("DYLD_INSERT_LIBRARIES: removed our entry (%d), kept %d other entr%s%s%s", dropped, kept, kept == 1 ? "y" : "ies", kept ? ": " : "", kept ? out : "");
    free(copy); free(out);
}

static void note_other_loaders(void) {
    static const char *known[] = { "dsrig.dylib", "dsmods.dylib", "libdoorstop", "MelonLoader", "Bootstrap.dylib", "libdobby" };
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char *nm = _dyld_get_image_name(i); if (!nm) continue;
        for (size_t k = 0; k < sizeof known / sizeof *known; k++) if (strstr(nm, known[k]))
            dsl_log("other loader in the process: %s%s", nm, (k < 2) ? " (has its own hook engine: shared functions are refused, not chained)" : "");
    }
}

/* test copies never take the owner's screen (owner v13.3: a test copy that opened full screen switched his Space).
   A test copy = an app whose Info.plist has LSBackgroundOnly (the rule for every test copy; a player's copy never has
   it). Before Unity reads its window settings: the copy's own preferences domain is set windowed 960x540; a copy whose
   domain still says full screen after that is stopped here. DSL_NOTCH=0 (the notch fix makes the game cover the whole
   screen) unless the caller set it (notch_test). The owner's own domains are never written. */
static void test_copy_window(void) {
    CFBundleRef mb = CFBundleGetMainBundle(); if (!mb) return;
    CFTypeRef bo = CFBundleGetValueForInfoDictionaryKey(mb, CFSTR("LSBackgroundOnly"));
    int bg = bo && ((CFGetTypeID(bo) == CFBooleanGetTypeID() && CFBooleanGetValue((CFBooleanRef)bo)) ||
                    (CFGetTypeID(bo) == CFStringGetTypeID() && CFStringCompare((CFStringRef)bo, CFSTR("1"), 0) == kCFCompareEqualTo));
    if (!bg) return;
    CFStringRef dom = CFBundleGetIdentifier(mb); if (!dom) return;
    char db[200] = ""; CFStringGetCString(dom, db, sizeof db, kCFStringEncodingUTF8);
    if (!strcmp(db, "com.CanOpener.Dungeon-Settlers") || !strcmp(db, "com.CanOpener.Dungeon-Settlers.Modded")) { dsl_log("screen: test copy with the owner's bundle id %s: its settings are not touched", db); return; }
    setenv("DSL_NOTCH", "0", 0);
    static const struct { const char *k; int v; } W[] = { { "Screenmanager Fullscreen mode", 3 }, { "Screenmanager Resolution Width", 960 },
        { "Screenmanager Resolution Height", 540 }, { "Screenmanager Resolution Use Native", 0 } };
    for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
        CFStringRef k = CFStringCreateWithCString(NULL, W[i].k, kCFStringEncodingUTF8); int v = W[i].v;
        CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &v);
        CFPreferencesSetAppValue(k, n, dom); CFRelease(n); CFRelease(k);
    }
    CFPreferencesAppSynchronize(dom);
    CFPropertyListRef m = CFPreferencesCopyAppValue(CFSTR("Screenmanager Fullscreen mode"), dom); long mode = -1;
    if (m && CFGetTypeID(m) == CFNumberGetTypeID()) CFNumberGetValue((CFNumberRef)m, kCFNumberLongType, &mode);
    if (m) CFRelease(m);
    if (mode == 0 || mode == 1) { dsl_log("screen: test copy %s: its settings still say full screen (mode %ld): this copy does not start", db, mode); _exit(3); }
    dsl_log("screen: test copy %s: windowed 960x540 (mode %ld), DSL_NOTCH=%s", db, mode, getenv("DSL_NOTCH"));
}

__attribute__((constructor)) static void dsl_init(void) {
#if !defined(__arm64__)
    return;
#endif
    /* soft restart B started by LaunchServices (dsl_soft_mac.c): LaunchServices starts every app in "/", so A passes
       its start folder (the profile folder with steam_appid.txt). Before the soft snapshot: B's own B starts there too. */
    const char *scwd = getenv("DSL_SOFT_CWD");
    int scwd_r = (scwd && *scwd) ? chdir(scwd) : 1;
    Dl_info di;
    if (dladdr((void*)dsl_init, &di) && di.dli_fname) snprintf(self_path, sizeof self_path, "%s", di.dli_fname);
    const char *home = getenv("HOME");
    char defdir[1024] = "";
    if (home) snprintf(defdir, sizeof defdir, "%s/Library/Application Support/CanOpener/Dungeon Settlers", home);
    if (dsl_core_log_open(defdir, self_path)) return;
    if (scwd_r != 1) dsl_log("DSL_SOFT_CWD %s: chdir %s", scwd, scwd_r == 0 ? "ok" : "failed");
    dsl_soft_snapshot(dsl_core_logdir());
    test_copy_window();
    pass_through_env();

    /* main executable -> app; GameAssembly file -> build */
    char exe[1024]; uint32_t sz = sizeof exe;
    char appc[1100] = "";
    if (_NSGetExecutablePath(exe, &sz) == 0) {
        char r[1024]; if (realpath(exe, r)) snprintf(exe, sizeof exe, "%s", r);
        char *s = strstr(exe, "/Contents/MacOS/");
        if (s) { snprintf(appc, sizeof appc, "%.*s/Contents", (int)(s - exe), exe); }
    }
    char mods_default[1200] = "";
    if (appc[0]) snprintf(mods_default, sizeof mods_default, "%s/Resources/mods", appc);
    const char *gp = getenv("DSL_GA_PATH");
    char gafile[1200];
    if (gp && *gp) snprintf(gafile, sizeof gafile, "%s", gp);
    else snprintf(gafile, sizeof gafile, "%s/Frameworks/GameAssembly.dylib", appc);
    char uuid[40];
    if (file_uuid(gafile, uuid, sizeof uuid)) { dsl_log("cannot read %s: loader inactive", gafile); return; }
    dsl_log("GameAssembly file %s: arm64 uuid %s", gafile, uuid);
    int r = dsl_core_start(build_of_uuid(uuid), uuid, mods_default);
    note_other_loaders();
    if (r) return;
    _dyld_register_func_for_add_image(on_image);
}
