// dsload.dylib: the one native mod loader of the Mac launcher. Injected first in DYLD_INSERT_LIBRARIES.
//
// Constructor (before GameAssembly is mapped):
//   1. log; DYLD_INSERT_LIBRARIES: remove only our own entry, keep every other loader's entry;
//   2. read the GameAssembly file's arm64 LC_UUID -> game build (dsl_builds.h); unknown build -> nothing loads;
//   3. scan <mods>/<id>/mod.json, keep enabled code modules for this build + platform, check sha256, requires,
//      conflicts, per-build tables; order = dependencies first, then profile order;
//   4. dlopen each module, call dsl_module_load.
// GameAssembly add-image callback (none of its code has run): engine init, il2cpp_init hook, dsl_module_early.
// il2cpp_init returned: dsl_plugin_init of every module, then new hooks go to the late stub page.
// Crash guard per module and phase: <state dir>/<id>.pending left over at the next start = it crashed there.
#include "dsl_internal.h"
#include "dsl_builds.h"
#include "../sdk/dsl_sdk.h"
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <mach-o/nlist.h>
#include <CommonCrypto/CommonDigest.h>
#include <libkern/OSByteOrder.h>
#include <dlfcn.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

uintptr_t dsl_ga_base;
uint64_t dsl_text_lo, dsl_text_hi, dsl_image_end;
const char *dsl_build_id;
static const struct symtab_command *symtab;
static uintptr_t linkedit_base;
static char ga_path_s[1024];

static FILE *lf;
static pthread_mutex_t loglock = PTHREAD_MUTEX_INITIALIZER;
static char logdir[1024], statedir[1100], modsdir[1024], profile_path[1100], self_path[1024];

#define MAXP 32
#define MAXT 512
typedef struct { char name[64]; uintptr_t rva; } TabEnt;
struct DSLPlugin {
    char id[64], version[32], dir[1100], file[1200], sig[64];
    int idx, state;                       /* 0 listed, 1 active, 2 failed, 3 skipped */
    int api, order_key, ordered, gameplay;
    void *h;
    JNode *mf;                            /* parsed mod.json */
    TabEnt *tab; int ntab;
    dsl_module_load_fn load; dsl_module_early_fn early; dsl_plugin_init_fn init;
};
static struct DSLPlugin P[MAXP];
static int np;
static struct DSLPlugin *ORDER[MAXP];
static int norder;
static DSLHost host;
static char provided[64][64]; static int nprovided;   /* enabled non-code mods (data / patch / builtin) */

/* ------------------------------------------------------------------ log */
void dsl_log(const char *fmt, ...) {
    if (!lf) return;
    pthread_mutex_lock(&loglock);
    struct timeval tv; gettimeofday(&tv, NULL);
    char ts[32]; strftime(ts, sizeof ts, "%H:%M:%S", localtime(&tv.tv_sec));
    fprintf(lf, "%s.%03d ", ts, (int)(tv.tv_usec / 1000));
    va_list ap; va_start(ap, fmt); vfprintf(lf, fmt, ap); va_end(ap);
    fputc('\n', lf); fflush(lf);
    pthread_mutex_unlock(&loglock);
}
const char *dsl_owner_name(int owner) {
    if (owner == DSL_OWNER_LOADER) return "the loader";
    return (owner >= 0 && owner < np) ? P[owner].id : "?";
}

/* ------------------------------------------------------------------ files */
static char *read_file(const char *path, size_t max) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    char *b = malloc(max + 1); size_t n = fread(b, 1, max, f); fclose(f);
    b[n] = 0; return b;
}
static int write_file(const char *path, const char *text) {
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w"); if (!f) return -1;
    fputs(text, f); fflush(f); fsync(fileno(f)); fclose(f);
    return rename(tmp, path);
}
static void mkdirs(const char *p) {
    char b[1100]; snprintf(b, sizeof b, "%s", p);
    for (char *s = b + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(b, 0755); *s = '/'; }
    mkdir(b, 0755);
}
static void file_sig(const char *path, char *out, size_t n) {
    struct stat st;
    if (stat(path, &st)) { snprintf(out, n, "missing"); return; }
    snprintf(out, n, "%lld:%ld", (long long)st.st_size, (long)st.st_mtimespec.tv_sec);
}
static int sha256_file(const char *path, char out[65]) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    CC_SHA256_CTX c; CC_SHA256_Init(&c);
    unsigned char buf[1 << 16]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) CC_SHA256_Update(&c, buf, (CC_LONG)n);
    fclose(f);
    unsigned char d[32]; CC_SHA256_Final(d, &c);
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
    return 0;
}

/* ------------------------------------------------------------------ crash guard (same rules as dsmods loader 1.0) */
static void guard_path(char *out, size_t n, const char *id, const char *ext) { snprintf(out, n, "%s/%s.%s", statedir, id, ext); }
static int guard_check(const char *id, const char *sig, const char *what) {
    char pend[1200], dis[1200]; guard_path(pend, sizeof pend, id, "pending"); guard_path(dis, sizeof dis, id, "disabled");
    char *p = read_file(pend, 4096);
    if (p) {
        char txt[700]; snprintf(txt, sizeof txt, "%s\nthe game stopped during: %s\n", sig, p[0] ? p : "?");
        write_file(dis, txt); unlink(pend);
        dsl_log("CRASH GUARD: the game stopped last time while %s ran (%s). %s is now OFF. It is tried again when its file changes, "
                "or when you delete %s", what, p, id, dis);
        free(p);
    }
    char *d = read_file(dis, 4096);
    if (!d) return 0;
    char *nl = strchr(d, '\n'); if (nl) *nl = 0;
    int same = strcmp(d, sig) == 0; free(d);
    if (same) { dsl_log("%s is OFF by the crash guard (file %s)", what, dis); return 1; }
    unlink(dis);
    dsl_log("%s: file changed since the crash; crash guard reset, trying again", what);
    return 0;
}
static void guard_enter(const char *id, const char *phase) { char p[1200]; guard_path(p, sizeof p, id, "pending"); write_file(p, phase); }
static void guard_leave(const char *id) { char p[1200]; guard_path(p, sizeof p, id, "pending"); unlink(p); }
static int codepatch_marked;
static char loader_sig[64];
void dsl_mark_codepatch(void) {
    if (codepatch_marked) return;
    codepatch_marked = 1; guard_enter("_codepatch", "runtime code patching (hook stubs)");
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

static int parse_ga(const struct mach_header_64 *h, char *uuid, size_t un) {
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
    linkedit_base = dsl_ga_base + (uintptr_t)(le_vm - text_vm) - (uintptr_t)le_off;
    return 0;
}

void *dsl_ga_sym(const char *name) {
    if (!symtab || !name) return NULL;
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

/* ------------------------------------------------------------------ host API */
static void h_log(DSLPlugin *self, const char *fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    dsl_log("[%s] %s", self ? self->id : "?", buf);
}
static const char *h_plugin_id(DSLPlugin *self) { return self ? self->id : ""; }
static void *h_rva(uintptr_t r) { return dsl_ga_base ? (void*)(dsl_ga_base + r) : NULL; }
static int h_ready(void) { return dsl_ready; }
static int h_hook(DSLPlugin *self, void *t, void *r, void **o) {
    if (!self || self->state != 1) return -1;
    return dsl_hook(self->idx, t, r, o, self->id);
}
static int h_hook_method(DSLPlugin *self, const char *ns, const char *k, const char *m, int argc, void *r, void **o) {
    if (!self || self->state != 1) return -1;
    void *a = dsl_find_method_address(ns, k, m, argc);
    if (!a) { dsl_log("[%s] hook %s.%s refused: method not found", self->id, k, m); return -1; }
    char who[160]; snprintf(who, sizeof who, "%s %s.%s", self->id, k, m);
    return dsl_hook(self->idx, a, r, o, who);
}
static int h_unhook(DSLPlugin *self, void *t) { return self ? dsl_unhook(self->idx, t, self->id) : -1; }
static int h_hook_rva(DSLPlugin *self, uintptr_t rva, void *r, void **o, const char *name) {
    if (!self || self->state != 1 || !dsl_ga_base) return -1;
    char who[160]; snprintf(who, sizeof who, "%s %s", self->id, name ? name : "");
    return dsl_hook(self->idx, (void*)(dsl_ga_base + rva), r, o, who);
}
static void *h_table(DSLPlugin *self, const char *name) {
    if (!self || !name || !dsl_ga_base) return NULL;
    for (int i = 0; i < self->ntab; i++) if (!strcmp(self->tab[i].name, name)) return (void*)(dsl_ga_base + self->tab[i].rva);
    return NULL;
}
static const char *h_module_dir(DSLPlugin *self) { return self ? self->dir : ""; }
static const char *h_ga_path(void) { return dsl_ga_base ? ga_path_s : NULL; }
static int h_patch_rva(DSLPlugin *self, uintptr_t rva, const void *old, const void *neu, size_t n, const char *name) {
    if (!self || self->state != 1 || !dsl_ga_base) return -1;
    char who[160]; snprintf(who, sizeof who, "%s %s", self->id, name ? name : "");
    return dsl_patch(self->idx, (void*)(dsl_ga_base + rva), old, neu, n, who);
}
static char *modset_json;                       /* built after the modules started (write_modset) */
static size_t h_gameplay_mods(char *buf, size_t n) {
    size_t need = modset_json ? strlen(modset_json) : 2;
    if (buf && n) snprintf(buf, n, "%s", modset_json ? modset_json : "{}");
    return need;
}
static int h_module_loaded(const char *id) {
    for (int i = 0; i < np; i++) if (!strcmp(P[i].id, id)) return P[i].state == 1;
    return 0;
}

/* ------------------------------------------------------------------ manifests */
static int vcmp(const char *a, const char *b) {          /* dotted numeric compare */
    while (*a || *b) {
        long x = strtol(a, (char**)&a, 10), y = strtol(b, (char**)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if (!isdigit((unsigned char)*a) && !isdigit((unsigned char)*b)) break;
    }
    return 0;
}
/* "id", "id>=1.2", "id<2", "id=1.0.0" -> id + op + version */
static void split_req(const char *s, char *id, size_t n, char *op, char *ver) {
    size_t i = 0;
    while (s[i] && !strchr("<>=!", s[i]) && i + 1 < n) { id[i] = s[i]; i++; }
    id[i] = 0;
    while (i > 0 && id[i - 1] == ' ') id[--i] = 0;
    op[0] = 0; ver[0] = 0;
    const char *p = s + strlen(id);
    while (*p == ' ') p++;
    int k = 0; while (*p && strchr("<>=!", *p) && k < 2) op[k++] = *p++;
    op[k] = 0;
    while (*p == ' ') p++;
    snprintf(ver, 32, "%s", p);
}
static int ver_ok(const char *have, const char *op, const char *want) {
    if (!op[0]) return 1;
    int c = vcmp(have, want);
    if (!strcmp(op, ">=")) return c >= 0;
    if (!strcmp(op, ">")) return c > 0;
    if (!strcmp(op, "<=")) return c <= 0;
    if (!strcmp(op, "<")) return c < 0;
    if (!strcmp(op, "=") || !strcmp(op, "==")) return c == 0;
    if (!strcmp(op, "!=")) return c != 0;
    return 0;
}
static struct DSLPlugin *find_mod(const char *id) {
    for (int i = 0; i < np; i++) if (!strcmp(P[i].id, id)) return &P[i];
    return NULL;
}
static int is_provided(const char *id) { for (int i = 0; i < nprovided; i++) if (!strcmp(provided[i], id)) return 1; return 0; }

static int str_in(const JNode *arr, const char *s) {
    if (!arr) return -1;
    if (arr->t == JN_STR) return !strcmp(arr->str, s) || !strcmp(arr->str, "*");
    if (arr->t != JN_ARR) return -1;
    int i = 0;
    for (JNode *c = arr->child; c; c = c->next, i++) if (c->t == JN_STR && (!strcmp(c->str, s) || !strcmp(c->str, "*"))) return 1;
    return 0;
}
static int list_pos(const JNode *arr, const char *s) {
    int i = 0;
    if (arr && arr->t == JN_ARR) for (JNode *c = arr->child; c; c = c->next, i++) if (c->t == JN_STR && !strcmp(c->str, s)) return i;
    return -1;
}

static int load_table(struct DSLPlugin *p, const char *rel) {
    char path[1400]; snprintf(path, sizeof path, "%s/%s", p->dir, rel);
    char *t = read_file(path, 1 << 20);
    if (!t) { dsl_log("%s: per-build table %s not found", p->id, rel); return -1; }
    p->tab = calloc(MAXT, sizeof *p->tab);
    for (char *line = strtok(t, "\n"); line && p->ntab < MAXT; line = strtok(NULL, "\n")) {
        if (line[0] == '#' || !line[0]) continue;
        char name[64]; char rv[40];
        if (sscanf(line, "%63s %39s", name, rv) != 2) continue;
        p->tab[p->ntab].rva = (uintptr_t)strtoull(rv, NULL, 0);
        snprintf(p->tab[p->ntab].name, sizeof p->tab[p->ntab].name, "%s", name);
        p->ntab++;
    }
    free(t);
    dsl_log("%s: per-build table %s: %d entries", p->id, rel, p->ntab);
    return 0;
}

/* read every <mods>/<id>/mod.json; keep enabled code modules that fit this build */
static void scan_mods(const char *build) {
    char *prof_txt = read_file(profile_path, 1 << 20);
    JNode *prof = prof_txt ? json_parse(prof_txt) : NULL;
    free(prof_txt);
    if (prof_txt && !prof) dsl_log("profile %s: not valid JSON: every installed mod is enabled", profile_path);
    if (!prof) dsl_log("no profile (%s): every installed mod is enabled", profile_path);
    JNode *enabled = json_get(prof, "enabled"), *order = json_get(prof, "order"), *prov = json_get(prof, "provided");
    if (prov && prov->t == JN_ARR) for (JNode *c = prov->child; c && nprovided < 64; c = c->next)
        if (c->t == JN_STR) snprintf(provided[nprovided++], 64, "%s", c->str);

    DIR *d = opendir(modsdir);
    if (!d) { dsl_log("mods folder %s not found: no modules", modsdir); json_free(prof); return; }
    struct dirent *e;
    char names[MAXP * 2][256]; int nn = 0;
    while ((e = readdir(d)) && nn < MAXP * 2) if (e->d_name[0] != '.') snprintf(names[nn++], 256, "%s", e->d_name);
    closedir(d);
    qsort(names, (size_t)nn, 256, (int (*)(const void*, const void*))strcmp);
    for (int k = 0; k < nn && np < MAXP; k++) {
        char dir[1100], mp[1200]; snprintf(dir, sizeof dir, "%s/%s", modsdir, names[k]); snprintf(mp, sizeof mp, "%s/mod.json", dir);
        char *txt = read_file(mp, 1 << 20);
        if (!txt) continue;
        JNode *m = json_parse(txt); free(txt);
        if (!m || m->t != JN_OBJ) { dsl_log("%s: mod.json is not valid JSON: skipped", names[k]); json_free(m); continue; }
        const char *id = json_str(m, "id"), *kind = json_str(m, "kind");
        if (!kind) kind = json_str(m, "type");
        if (!id || strcmp(id, names[k])) { dsl_log("%s: mod.json id '%s' does not match the folder: skipped", names[k], id ? id : ""); json_free(m); continue; }
        if (enabled && str_in(enabled, id) == 0) { dsl_log("%s: not enabled in the profile", id); json_free(m); continue; }
        if (!kind || strcmp(kind, "code")) {                       /* data / patch / builtin: applied to the copy by the launcher */
            if (nprovided < 64 && !is_provided(id)) snprintf(provided[nprovided++], 64, "%s", id);
            json_free(m); continue;
        }
        struct DSLPlugin *p = &P[np]; memset(p, 0, sizeof *p);
        snprintf(p->id, sizeof p->id, "%s", id); snprintf(p->dir, sizeof p->dir, "%s", dir);
        snprintf(p->version, sizeof p->version, "%s", json_str(m, "version") ? json_str(m, "version") : "?");
        p->mf = m; p->idx = np; p->state = 0;
        const char *scope = json_str(m, "scope");
        p->gameplay = !(scope && !strcmp(scope, "client"));          /* default: gameplay (launcher model.rs) */
        JNode *api = json_get(m, "api"); p->api = api && api->t == JN_NUM ? (int)api->num : 1;
        int pos = list_pos(order, id); if (pos < 0) pos = list_pos(enabled, id);
        p->order_key = pos < 0 ? 1000 + np : pos;
        np++;
        /* build */
        JNode *gb = json_get(m, "game_builds"); if (!gb) gb = json_get(m, "game_build");
        if (str_in(gb, build) == 0) { p->state = 3; dsl_log("%s: made for game build(s) other than %s: not loaded", id, build); continue; }
        if (p->api < 1 || p->api > DSL_API_VERSION) { p->state = 3; dsl_log("%s: module API %d, loader API %d: not loaded", id, p->api, DSL_API_VERSION); continue; }
        /* platform files */
        JNode *plat = json_get(json_get(m, "platforms"), "mac-arm64");
        const char *entry = plat ? json_str(plat, "entry") : NULL;
        char defname[80]; snprintf(defname, sizeof defname, "%s.dylib", id);
        if (json_get(m, "platforms") && !plat) { p->state = 3; dsl_log("%s: no mac-arm64 build: not loaded", id); continue; }
        snprintf(p->file, sizeof p->file, "%s/%s", dir, entry ? entry : defname);
        JNode *files = plat ? json_get(plat, "files") : NULL;
        int bad = 0;
        if (files && files->t == JN_OBJ) for (JNode *f = files->child; f; f = f->next) {
            if (f->t != JN_STR || strchr(f->key, '/') ) continue;
            char fp[1400], h[65]; snprintf(fp, sizeof fp, "%s/%s", dir, f->key);
            if (sha256_file(fp, h)) { dsl_log("%s: file %s missing", id, f->key); bad = 1; }
            else if (strcasecmp(h, f->str)) { dsl_log("%s: file %s sha256 %s, mod.json says %s", id, f->key, h, f->str); bad = 1; }
        }
        if (bad) { p->state = 3; dsl_log("%s: files do not match mod.json: not loaded", id); continue; }
        /* per-build table: "tables" (or the plan's "hooks") -> build -> platform -> file */
        JNode *tb = json_get(m, "tables"); if (!tb) tb = json_get(m, "hooks");
        if (tb && tb->t == JN_OBJ) {
            const char *rel = json_str(json_get(tb, build), "mac-arm64");
            if (!rel) { p->state = 3; dsl_log("%s: no mac-arm64 table for game build %s: not loaded", id, build); continue; }
            if (load_table(p, rel)) { p->state = 3; continue; }
        }
    }
    json_free(prof);

    /* conflicts (both directions; the one later in the profile order is dropped) */
    for (int i = 0; i < np; i++) {
        if (P[i].state != 0) continue;
        JNode *cf = json_get(P[i].mf, "conflicts");
        if (!cf || cf->t != JN_ARR) continue;
        for (JNode *c = cf->child; c; c = c->next) {
            if (c->t != JN_STR) continue;
            char cid[64], op[3], ver[32]; split_req(c->str, cid, sizeof cid, op, ver);
            struct DSLPlugin *o = find_mod(cid);
            int hit = (o && o->state == 0 && ver_ok(o->version, op, ver)) || (!o && is_provided(cid));
            if (!hit) continue;
            struct DSLPlugin *drop = (o && o->order_key < P[i].order_key) ? &P[i] : (o ? o : &P[i]);
            drop->state = 3;
            dsl_log("CONFLICT: %s and %s cannot run together; %s is not loaded", P[i].id, cid, drop->id);
            if (drop == &P[i]) break;
        }
    }
    /* requires: repeat until stable (a dropped module can break another's requirement) */
    for (int changed = 1; changed; ) {
        changed = 0;
        for (int i = 0; i < np; i++) {
            if (P[i].state != 0) continue;
            JNode *rq = json_get(P[i].mf, "requires");
            if (!rq || rq->t != JN_ARR) continue;
            for (JNode *c = rq->child; c; c = c->next) {
                if (c->t != JN_STR) continue;
                char rid[64], op[3], ver[32]; split_req(c->str, rid, sizeof rid, op, ver);
                const char *why = NULL;
                if (!strcmp(rid, "loader")) { if (!ver_ok(DSL_LOADER_VERSION, op, ver)) why = "loader version"; }
                else {
                    struct DSLPlugin *o = find_mod(rid);
                    if (o) { if (o->state != 0) why = "is not loaded"; else if (!ver_ok(o->version, op, ver)) why = "wrong version"; }
                    else if (!is_provided(rid)) why = "is not installed / enabled";
                }
                if (why) { P[i].state = 3; changed = 1; dsl_log("%s: needs %s, which %s: not loaded", P[i].id, c->str, why); break; }
            }
        }
    }
    /* order: dependencies first, ties by profile order (Kahn) */
    for (;;) {
        struct DSLPlugin *best = NULL;
        for (int i = 0; i < np; i++) {
            if (P[i].state != 0 || P[i].ordered) continue;
            int ready = 1;
            JNode *rq = json_get(P[i].mf, "requires");
            if (rq && rq->t == JN_ARR) for (JNode *c = rq->child; c; c = c->next) {
                char rid[64], op[3], ver[32]; if (c->t != JN_STR) continue; split_req(c->str, rid, sizeof rid, op, ver);
                struct DSLPlugin *o = find_mod(rid);
                if (o && o->state == 0 && !o->ordered) ready = 0;
            }
            if (ready && (!best || P[i].order_key < best->order_key)) best = &P[i];
        }
        if (!best) break;
        best->ordered = 1; ORDER[norder++] = best;
    }
    for (int i = 0; i < np; i++) if (P[i].state == 0 && !P[i].ordered) { P[i].state = 3; dsl_log("%s: dependency cycle: not loaded", P[i].id); }
    char ol[1024] = ""; for (int i = 0; i < norder; i++) { strlcat(ol, i ? ", " : "", sizeof ol); strlcat(ol, ORDER[i]->id, sizeof ol); }
    dsl_log("load order: %s", norder ? ol : "(none)");
}

/* ------------------------------------------------------------------ phases */
static int fail(struct DSLPlugin *p, const char *phase, int r) {
    int n = dsl_unhook_owner(p->idx); p->state = 2;
    dsl_log("%s: FAILED: %s returned %d; %d hook(s) removed; off for this session", p->id, phase, r, n);
    return -1;
}

static void open_modules(void) {
    for (int i = 0; i < norder; i++) {
        struct DSLPlugin *p = ORDER[i];
        file_sig(p->file, p->sig, sizeof p->sig);
        char what[128]; snprintf(what, sizeof what, "module %s", p->id);
        if (guard_check(p->id, p->sig, what)) { p->state = 3; continue; }
        guard_enter(p->id, "loading the dylib");
        p->h = dlopen(p->file, RTLD_NOW | RTLD_LOCAL);
        if (!p->h) { guard_leave(p->id); p->state = 2; dsl_log("%s: FAILED to load: %s", p->id, dlerror()); continue; }
        const uint32_t *api = dlsym(p->h, "dsl_plugin_api_version");
        p->load = (dsl_module_load_fn)dlsym(p->h, "dsl_module_load");
        p->early = (dsl_module_early_fn)dlsym(p->h, "dsl_module_early");
        p->init = (dsl_plugin_init_fn)dlsym(p->h, "dsl_plugin_init");
        if (!api || *api < 1 || *api > DSL_API_VERSION || !(p->load || p->early || p->init)) {
            guard_leave(p->id); p->state = 2;
            dsl_log("%s: FAILED: %s (loader API %d)", p->id, !api ? "no dsl_plugin_api_version (DSL_PLUGIN() missing)" :
                    (*api < 1 || *api > DSL_API_VERSION) ? "API version not supported" : "no entry point", DSL_API_VERSION);
            continue;
        }
        p->state = 1;
        if (p->load) {
            guard_enter(p->id, "dsl_module_load");
            int r = p->load(&host, p);
            guard_leave(p->id);
            if (r) { fail(p, "dsl_module_load", r); continue; }
        } else guard_leave(p->id);
        dsl_log("%s %s: opened (API %u; entry points:%s%s%s)", p->id, p->version, *api, p->load ? " load" : "", p->early ? " early" : "", p->init ? " init" : "");
    }
}

static void run_init(void) {
    int ok = 0, n = 0;
    for (int i = 0; i < norder; i++) {
        struct DSLPlugin *p = ORDER[i];
        if (p->state != 1) continue;
        n++;
        if (p->init) {
            guard_enter(p->id, "dsl_plugin_init");
            int r = p->init(&host, p);
            guard_leave(p->id);
            if (r) { fail(p, "dsl_plugin_init", r); continue; }
        }
        ok++; dsl_log("%s %s: loaded (%d hook(s))", p->id, p->version, dsl_hook_count(p->idx));
    }
    dsl_log("modules: %d loaded of %d opened; %d hook(s) active in the process", ok, n, dsl_hook_count(-2));
}

/* dsload_modset.json: what really runs, for the multiplayer join check (scope gameplay = every player needs it).
   Each code module: id, version, sha256 of its folder's files named in mod.json (else its dylib). Compared with the
   launcher's session lines (<id>@<version>:<sha>,...): a difference is logged, never fatal. */
typedef struct { char *p; size_t n, cap; } SB;
static void sb(SB *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb(SB *b, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int k = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    if (b->n + (size_t)k + 1 > b->cap) { b->cap = (b->n + (size_t)k + 1) * 2; b->p = realloc(b->p, b->cap); }
    va_start(ap, fmt); vsnprintf(b->p + b->n, (size_t)k + 1, fmt, ap); va_end(ap); b->n += (size_t)k;
}
static void write_modset(void) {
    char *stxt = NULL; const char *sp = getenv("DSML_SESSION");
    if (sp) stxt = read_file(sp, 8 << 20);
    JNode *sess = stxt ? json_parse(stxt) : NULL; free(stxt);
    JNode *lines = json_get(sess, "modset_lines");
    SB b = {0};
    sb(&b, "{\"build\":\"%s\",\"loader\":\"%s\",\"session_modset\":\"%s\",\"gameplay\":[", dsl_build_id ? dsl_build_id : "",
       DSL_LOADER_VERSION, getenv("DSML_MODSET") ? getenv("DSML_MODSET") : (json_str(sess, "modset") ? json_str(sess, "modset") : ""));
    int first = 1, diffs = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) { sb(&b, "],\"client\":["); first = 1; }
        for (int i = 0; i < norder; i++) {
            struct DSLPlugin *p = ORDER[i];
            if (p->state != 1 || p->gameplay != (pass == 0)) continue;
            sb(&b, "%s{\"id\":\"%s\",\"version\":\"%s\",\"files\":{", first ? "" : ",", p->id, p->version); first = 0;
            JNode *files = json_get(json_get(json_get(p->mf, "platforms"), "mac-arm64"), "files");
            char shas[16][65]; int ns = 0;
            if (files && files->t == JN_OBJ && files->child) {
                for (JNode *f = files->child; f && ns < 16; f = f->next) {
                    char fp[1400]; snprintf(fp, sizeof fp, "%s/%s", p->dir, f->key);
                    if (sha256_file(fp, shas[ns])) continue;
                    sb(&b, "%s\"%s\":\"%s\"", ns ? "," : "", f->key, shas[ns]); ns++;
                }
            } else if (!sha256_file(p->file, shas[0])) { const char *bn = strrchr(p->file, '/'); sb(&b, "\"%s\":\"%s\"", bn ? bn + 1 : p->file, shas[0]); ns = 1; }
            sb(&b, "}}");
            if (pass == 0 && lines && lines->t == JN_ARR) {           /* compare with the launcher's line for this mod */
                char key[128]; snprintf(key, sizeof key, "%s@%s", p->id, p->version);
                JNode *hit = NULL;
                for (JNode *l = lines->child; l; l = l->next) if (l->t == JN_STR && !strncmp(l->str, key, strlen(key)) && (l->str[strlen(key)] == ':' || !l->str[strlen(key)])) hit = l;
                if (!hit) { diffs++; dsl_log("mod set: %s runs here but is not in the launcher's gameplay list", key); }
                else for (int k = 0; k < ns; k++) if (!strcasestr(hit->str, shas[k])) { diffs++; dsl_log("mod set: %s file sha256 %s is not in the launcher's line '%s'", key, shas[k], hit->str); }
            }
        }
    }
    if (lines && lines->t == JN_ARR) for (JNode *l = lines->child; l; l = l->next) {
        if (l->t != JN_STR) continue;
        char id[64]; size_t k = 0; while (l->str[k] && l->str[k] != '@' && k < 63) { id[k] = l->str[k]; k++; } id[k] = 0;
        struct DSLPlugin *o = find_mod(id);
        if (o && o->state != 1) { diffs++; dsl_log("mod set: the launcher lists %s but it is not running (%s)", l->str, o->state == 2 ? "failed" : "skipped"); }
    }
    sb(&b, "],\"provided\":[");
    for (int i = 0; i < nprovided; i++) sb(&b, "%s\"%s\"", i ? "," : "", provided[i]);
    sb(&b, "],\"check\":\"%s\",\"differences\":%d,\"world_refusals\":0}", !sess ? "no session" : (lines ? (diffs ? "differs" : "ok") : "no modset_lines"), diffs);
    json_free(sess);
    modset_json = b.p;
    char path[1200]; snprintf(path, sizeof path, "%s/dsload_modset.json", logdir);
    write_file(path, modset_json);
    dsl_log("mod set written to %s (check: %s, %d difference(s))", path, !sp ? "no session" : (diffs ? "differs" : "ok"), diffs);
}

void dsl_world_install(void **modules, int n);
static void world_install(void) {
    void *hs[MAXP]; int k = 0;
    for (int i = 0; i < norder; i++) if (ORDER[i]->state == 1) hs[k++] = ORDER[i]->h;
    dsl_world_install(hs, k);
}

static int (*orig_init)(const char *);
static int (*orig_init_utf16)(const void *);
static void on_il2cpp_ready(const char *via) {
    if (dsl_ready) return;
    dsl_ready = 1;
    guard_leave("_codepatch");
    dsl_log("il2cpp_init%s returned: runtime code patching works", via);
    if (dsl_il2cpp_bind()) dsl_log("il2cpp exports missing: name lookups will fail");
    run_init();
    write_modset();
    world_install();
    dsl_engine_set_late();
}
static int my_il2cpp_init(const char *domain) { int r = orig_init(domain); on_il2cpp_ready(""); return r; }
static int my_il2cpp_init_utf16(const void *domain) { int r = orig_init_utf16(domain); on_il2cpp_ready("_utf16"); return r; }

static void on_image(const struct mach_header *mh, intptr_t slide) {
    (void)slide;
    if (dsl_ga_base) return;
    Dl_info di;
    if (!dladdr(mh, &di) || !di.dli_fname || !strstr(di.dli_fname, "/GameAssembly.dylib")) return;
    const struct mach_header_64 *h = (const void*)mh;
    if (h->magic != MH_MAGIC_64) return;
    dsl_ga_base = (uintptr_t)mh;
    snprintf(ga_path_s, sizeof ga_path_s, "%s", di.dli_fname);
    char uuid[40];
    if (parse_ga(h, uuid, sizeof uuid)) { dsl_log("GameAssembly header not understood: modules get no game access"); dsl_build_id = NULL; return; }
    const char *b = build_of_uuid(uuid);
    if (!b || !dsl_build_id || strcmp(b, dsl_build_id)) {
        dsl_log("mapped GameAssembly (uuid %s) is not the file read at start (build %s): modules get no game access", uuid, dsl_build_id ? dsl_build_id : "?");
        dsl_build_id = NULL; return;
    }
    host.ga_base = dsl_ga_base; host.game_build = dsl_build_id;
    dsl_log("GameAssembly build %s mapped at %p (code %#llx-%#llx, image end %#llx)", dsl_build_id, (void*)dsl_ga_base,
            (unsigned long long)dsl_text_lo, (unsigned long long)dsl_text_hi, (unsigned long long)dsl_image_end);
    int any = 0, want_init = 0;
    for (int i = 0; i < norder; i++) if (ORDER[i]->state == 1) { any = 1; if (ORDER[i]->init) want_init = 1; }
    if (getenv("DSML_SESSION")) want_init = any = 1;                 /* the world check runs after il2cpp_init */
    if (!any) return;
    if (guard_check("_codepatch", loader_sig, "runtime code patching")) { dsl_codepatch_ok = 0; dsl_log("code patching off this session: modules not started"); return; }
    if (dsl_engine_init()) return;
    if (want_init) {
        void *ii = dsl_ga_sym("il2cpp_init"), *iu = dsl_ga_sym("il2cpp_init_utf16");
        int a = ii ? dsl_hook(DSL_OWNER_LOADER, ii, (void*)my_il2cpp_init, (void**)&orig_init, "loader il2cpp_init") : -1;
        int c = iu ? dsl_hook(DSL_OWNER_LOADER, iu, (void*)my_il2cpp_init_utf16, (void**)&orig_init_utf16, "loader il2cpp_init_utf16") : -1;
        if (a && c) dsl_log("could not hook il2cpp_init: dsl_plugin_init will not be called");
    }
    for (int i = 0; i < norder; i++) {
        struct DSLPlugin *p = ORDER[i];
        if (p->state != 1 || !p->early) continue;
        guard_enter(p->id, "dsl_module_early");
        int r = p->early(&host, p);
        guard_leave(p->id);
        if (r) { fail(p, "dsl_module_early", r); continue; }
        dsl_log("%s: early start done (%d hook(s))", p->id, dsl_hook_count(p->idx));
    }
    if (!want_init) { guard_leave("_codepatch"); run_init(); write_modset(); dsl_engine_set_late(); }
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

__attribute__((constructor)) static void dsl_init(void) {
#if !defined(__arm64__)
    return;
#endif
    Dl_info di;
    if (dladdr((void*)dsl_init, &di) && di.dli_fname) snprintf(self_path, sizeof self_path, "%s", di.dli_fname);
    const char *ld = getenv("DSL_LOG_DIR"), *rd = getenv("DSRIG_DIR"), *home = getenv("HOME");
    if (ld && *ld) snprintf(logdir, sizeof logdir, "%s", ld);
    else if (rd && *rd) snprintf(logdir, sizeof logdir, "%s", rd);
    else if (home) snprintf(logdir, sizeof logdir, "%s/Library/Application Support/CanOpener/Dungeon Settlers", home);
    else return;
    snprintf(statedir, sizeof statedir, "%s/dsload_state", logdir);
    mkdirs(statedir);
    char lp[1200], pp[1200];
    snprintf(lp, sizeof lp, "%s/dsload.log", logdir); snprintf(pp, sizeof pp, "%s/dsload.prev.log", logdir);
    rename(lp, pp);
    lf = fopen(lp, "w");
    file_sig(self_path, loader_sig, sizeof loader_sig);
    dsl_log("dsload %s (module API %d) pid %d: %s", DSL_LOADER_VERSION, DSL_API_VERSION, getpid(), self_path);
    pass_through_env();

    /* main executable -> app; GameAssembly file -> build */
    char exe[1024]; uint32_t sz = sizeof exe;
    char appc[1100] = "";
    if (_NSGetExecutablePath(exe, &sz) == 0) {
        char r[1024]; if (realpath(exe, r)) snprintf(exe, sizeof exe, "%s", r);
        char *s = strstr(exe, "/Contents/MacOS/");
        if (s) { snprintf(appc, sizeof appc, "%.*s/Contents", (int)(s - exe), exe); }
    }
    const char *md = getenv("DSL_MODS_DIR"), *pf = getenv("DSL_PROFILE"), *gp = getenv("DSL_GA_PATH");
    if (md && *md) snprintf(modsdir, sizeof modsdir, "%s", md);
    else if (appc[0]) snprintf(modsdir, sizeof modsdir, "%s/Resources/mods", appc);
    snprintf(profile_path, sizeof profile_path, "%s", (pf && *pf) ? pf : "");
    if (!profile_path[0]) snprintf(profile_path, sizeof profile_path, "%s/profile.json", modsdir);
    char gafile[1200];
    if (gp && *gp) snprintf(gafile, sizeof gafile, "%s", gp);
    else snprintf(gafile, sizeof gafile, "%s/Frameworks/GameAssembly.dylib", appc);
    char uuid[40];
    if (file_uuid(gafile, uuid, sizeof uuid)) { dsl_log("cannot read %s: loader inactive", gafile); return; }
    dsl_build_id = build_of_uuid(uuid);
    if (!dsl_build_id) { dsl_log("unknown GameAssembly build (arm64 uuid %s): loader inactive, the game runs without code mods", uuid); return; }
    dsl_log("game build %s (%s); mods %s; profile %s", dsl_build_id, gafile, modsdir, profile_path);

    host = (DSLHost){ .api_version = DSL_API_VERSION, .size = sizeof(DSLHost), .game_build = dsl_build_id, .ga_base = 0,
        .log = h_log, .plugin_id = h_plugin_id, .rva = h_rva, .il2cpp_ready = h_ready,
        .il2cpp_fn = dsl_il2cpp_fn, .find_class = dsl_find_class, .find_method = dsl_find_method,
        .method_address = dsl_method_address, .find_method_address = dsl_find_method_address, .field_offset = dsl_field_offset,
        .hook = h_hook, .hook_method = h_hook_method, .unhook = h_unhook,
        .hook_rva = h_hook_rva, .table = h_table, .module_dir = h_module_dir, .ga_path = h_ga_path, .module_loaded = h_module_loaded,
        .patch_rva = h_patch_rva, .gameplay_mods = h_gameplay_mods };
    scan_mods(dsl_build_id);
    note_other_loaders();
    if (!norder && !getenv("DSML_SESSION")) { dsl_log("no module to load"); return; }
    open_modules();
    _dyld_register_func_for_add_image(on_image);
}
