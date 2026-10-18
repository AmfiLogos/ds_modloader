// The loader core, the same C on every OS (Mac: dsload.dylib, Windows: inside the baselib.dll proxy). The OS start
// part (dsl_os.h: start/mac, start/win) owns the process entry and calls, in order:
//   dsl_core_log_open; dsl_core_start (before GameAssembly is mapped):
//   1. game build from the start part (Mac: the GameAssembly file's arm64 LC_UUID; Windows: PE stamp) + launcher pin;
//      unknown build -> nothing loads;
//   2. scan <mods>/<id>/mod.json, keep enabled code modules for this build + platform, check sha256, requires,
//      conflicts, per-build tables; order = dependencies first, then profile order;
//   3. open each module (dlopen / LoadLibrary), call dsl_module_load.
//   dsl_core_ga_mapped (none of GameAssembly's code has run): engine init, il2cpp_init hook, dsl_module_early.
// il2cpp_init returned: dsl_plugin_init of every module, then new hooks go to the late stub page.
// Crash guard per module and phase: <state dir>/<id>.pending left over at the next start = it crashed there.
#define DSI_IMPLEMENTATION                    /* ds_i18n.h: the string tables live in this file (one per binary) */
#include "dsl_internal.h"
#include "dsl_os.h"
#include "../sdk/dsl_sdk.h"
#include "dsl_bie.h"
#include "dsl_ml.h"
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
static char ga_path_s[1024];

static FILE *lf;
static pthread_mutex_t loglock = PTHREAD_MUTEX_INITIALIZER;
static char logdir[1024], statedir[1100], modsdir[1024], profile_path[1100], self_path[1024];
char dsl_core_self_utf8[1024];

#define MAXP 32
#define MAXT 512
typedef struct { char name[64]; uintptr_t rva; } TabEnt;
struct DSLPlugin {
    char id[64], version[32], dir[1100], file[1200], sig[64], ndir[1100];
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
    time_t sec = tv.tv_sec;                         /* tv_sec is a long on Windows (32 bit), time_t 64 bit */
    char ts[32]; strftime(ts, sizeof ts, "%H:%M:%S", localtime(&sec));
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
    snprintf(out, n, "%lld:%ld", (long long)st.st_size, (long)st.st_mtime);
}
static int sha256_file(const char *path, char out[65]) { return dsl_os_sha256_file(path, out); }

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

/* ------------------------------------------------------------------ build guard */
/* Build guard: the game build must be the pinned one, else no module starts and no hook / patch is written.
   Pinned = the launcher's build for this world (DSML_BUILD, or "build" in the DSML_SESSION file) when given,
   and always one of the builds this loader was made for (dsl_builds.h). The reason goes to dsload.log, stderr,
   <log dir>/dsload_status.json (for the launcher) and, in a normal (not -batchmode) start, one system notice. */
static char status_why[2048];
static void json_esc(char *out, size_t n, const char *s) {
    size_t k = 0;
    for (; s && *s && k + 7 < n; s++) {
        if (*s == '"' || *s == '\\') { out[k++] = '\\'; out[k++] = *s; }
        else if ((unsigned char)*s < 0x20) k += (size_t)snprintf(out + k, n - k, "\\u%04x", *s);
        else out[k++] = *s;
    }
    out[k] = 0;
}
static void write_status(int active, const char *reason, const char *uuid, const char *found, const char *pinned) {
    if (!logdir[0]) return;
    char known[512], w[4200], e[5200];
    snprintf(known, sizeof known, "%s", dsl_os_build_list());
    json_esc(w, sizeof w, status_why);
    snprintf(e, sizeof e, "{\"loader\":\"%s\",\"api\":\"%s\",\"active\":%s,\"reason\":\"%s\",\"uuid\":\"%s\",\"found_build\":\"%s\",\"pinned_build\":\"%s\",\"loader_builds\":[%s],\"message\":\"%s\"}\n",
             DSL_RELEASE, DSL_LOADER_VERSION, active ? "true" : "false", reason, uuid ? uuid : "", found ? found : "", pinned ? pinned : "", known, w);
    char path[1200]; snprintf(path, sizeof path, "%s/dsload_status.json", logdir);
    write_file(path, e);
}
/* mods off this session: the notice also says which worlds the Load list will not show (dsl_saves.c). Windows: no
   save-folder redirect, the game lists its own saves, the modded worlds are hidden; Mac copy: the game saves are. */
static void hidden_note(void) {
    char n[900];
#ifdef _WIN32
    dsl_saves_off_note(1, n, sizeof n);
#else
    dsl_saves_off_note(0, n, sizeof n);
#endif
    size_t k = strlen(status_why);
    if (n[0]) snprintf(status_why + k, sizeof status_why - k, "%s%s", k ? "\n\n" : "", n);
}
static void build_refused(const char *reason, const char *uuid, const char *found, const char *pinned) {
    dsl_build_id = NULL;
    hidden_note();
    dsl_log("GAME BUILD CHECK FAILED (%s): %s", reason, status_why);
    dsl_log("no mod starts, no hook and no code patch is written; the game runs unmodded");
    fprintf(stderr, "dsload: %s\n", status_why);
    write_status(0, reason, uuid, found, pinned);
    if (dsl_os_batchmode() || getenv("DSL_NO_ALERT")) return;
    dsl_os_notice(TR("Dungeon Settlers: mods are off"), status_why);
}
/* the launcher's pinned build for this start, or NULL */
static const char *launcher_pin(void) {
    static char pin[64];
    const char *e = getenv("DSML_BUILD");
    if (e && *e) { snprintf(pin, sizeof pin, "%s", e); return pin; }
    const char *sp = getenv("DSML_SESSION");
    char *txt = sp ? read_file(sp, 8 << 20) : NULL;
    JNode *j = txt ? json_parse(txt) : NULL; free(txt);
    const char *b = json_str(j, "build");
    if (b && *b) snprintf(pin, sizeof pin, "%s", b);
    json_free(j);
    return pin[0] ? pin : NULL;
}

/* ------------------------------------------------------------------ host API */
static void h_log(DSLPlugin *self, const char *fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    dsl_log("[%s] %s", self ? self->id : "?", buf);
}
static const char *h_plugin_id(DSLPlugin *self) { return self ? self->id : ""; }
static void *h_rva(uintptr_t r) { return dsl_ga_base ? (void*)(dsl_ga_base + r) : NULL; }
static int h_ready(void) { return dsl_ready; }
static int mod_hook(DSLPlugin *self, void *t, void *r, void **o, const char *who) {
    dsl_fastboot_yield(t, who);                 /* the loader's fast boot gives its method to a mod that hooks it (Hell tables) */
    return dsl_hook(self->idx, t, r, o, who);
}
static int h_hook(DSLPlugin *self, void *t, void *r, void **o) {
    if (!self || self->state != 1) return -1;
    return mod_hook(self, t, r, o, self->id);
}
static int h_hook_method(DSLPlugin *self, const char *ns, const char *k, const char *m, int argc, void *r, void **o) {
    if (!self || self->state != 1) return -1;
    void *a = dsl_find_method_address(ns, k, m, argc);
    if (!a) { dsl_log("[%s] hook %s.%s refused: method not found", self->id, k, m); return -1; }
    char who[160]; snprintf(who, sizeof who, "%s %s.%s", self->id, k, m);
    return mod_hook(self, a, r, o, who);
}
static int h_unhook(DSLPlugin *self, void *t) { return self ? dsl_unhook(self->idx, t, self->id) : -1; }
static int h_hook_rva(DSLPlugin *self, uintptr_t rva, void *r, void **o, const char *name) {
    if (!self || self->state != 1 || !dsl_ga_base) return -1;
    char who[160]; snprintf(who, sizeof who, "%s %s", self->id, name ? name : "");
    return mod_hook(self, (void*)(dsl_ga_base + rva), r, o, who);
}
static void *h_table(DSLPlugin *self, const char *name) {
    if (!self || !name || !dsl_ga_base) return NULL;
    for (int i = 0; i < self->ntab; i++) if (!strcmp(self->tab[i].name, name)) return (void*)(dsl_ga_base + self->tab[i].rva);
    return NULL;
}
/* paths handed to modules: in the form their own fopen opens (dsl_os_native_path) */
static const char *h_module_dir(DSLPlugin *self) {
    if (!self) return "";
    if (!self->ndir[0]) dsl_os_native_path(self->dir, self->ndir, sizeof self->ndir);
    return self->ndir;
}
static const char *h_ga_path(void) {
    static char n[1024];
    if (!dsl_ga_base) return NULL;
    if (!n[0]) dsl_os_native_path(ga_path_s, n, sizeof n);
    return n;
}
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
static struct DSLPlugin *find_mod_early(const char *id) { for (int i = 0; i < np; i++) if (!strcmp(P[i].id, id)) return &P[i]; return NULL; }
/* for dsl_reg.c (API 3 services) */
const char *dsl_mods_dir(void) { return modsdir; }
const char *dsl_profile_file(void) { return profile_path; }
const char *dsl_log_dir(void) { return logdir; }
int dsl_plugin_index(DSLPlugin *self) { return self ? self->idx : -1; }
const char *dsl_plugin_name(DSLPlugin *self) { return self ? self->id : "loader"; }
int dsl_mod_state(const char *id, const char **why) {
    struct DSLPlugin *p = find_mod_early(id);
    if (why) *why = !p ? TR_NOOP("not loaded: not made for this game version or this computer") : p->state == 2 ? TR_NOOP("it crashed while starting")
                  : p->state == 3 ? TR_NOOP("not loaded: it needs another mod, clashes with one, or crashed last time") : "";   /* keys: the Mods window translates */
    return p ? p->state : 0;
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
        JNode *plat = json_get(json_get(m, "platforms"), DSL_PLATFORM);
        const char *entry = plat ? json_str(plat, "entry") : NULL;
        char defname[80]; snprintf(defname, sizeof defname, "%s" DSL_MODULE_EXT, id);
        if (json_get(m, "platforms") && !plat) { p->state = 3; dsl_log("%s: no " DSL_PLATFORM " build: not loaded", id); continue; }
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
            const char *rel = json_str(json_get(tb, build), DSL_PLATFORM);
            if (!rel) { p->state = 3; dsl_log("%s: no " DSL_PLATFORM " table for game build %s: not loaded", id, build); continue; }
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
        p->h = dsl_os_dlopen(p->file);
        if (!p->h) { guard_leave(p->id); p->state = 2; dsl_log("%s: FAILED to load: %s", p->id, dsl_os_dlerror()); continue; }
        const uint32_t *api = dsl_os_dlsym(p->h, "dsl_plugin_api_version");
        p->load = (dsl_module_load_fn)dsl_os_dlsym(p->h, "dsl_module_load");
        p->early = (dsl_module_early_fn)dsl_os_dlsym(p->h, "dsl_module_early");
        p->init = (dsl_plugin_init_fn)dsl_os_dlsym(p->h, "dsl_plugin_init");
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
/* Cross-platform mod-set hashes: the sha256s listed in mod.json for EVERY platform ("platforms": {key: {"files":
   {name: sha256}}}, keys sorted, "any" = BepInEx plugins), so Mac and Windows give the same line for the same mod.
   Writes "<platform>/<name>":"<sha>" JSON members (json != 0) or the sorted shas joined by ',' into out. 0 = none listed. */
static int cmp_s(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static int listed_hashes(const JNode *mf, char *out, size_t cap, int json) {
    const char *ent[128]; int n = 0; static char buf[128][200];
    JNode *pl = json_get(mf, "platforms");
    if (pl && pl->t == JN_OBJ) for (JNode *k = pl->child; k; k = k->next) {
        JNode *fs = json_get(k, "files");
        if (fs && fs->t == JN_OBJ) for (JNode *f = fs->child; f && n < 128; f = f->next) if (f->t == JN_STR && f->key) {
            if (json) snprintf(buf[n], sizeof buf[n], "\"%s/%s\":\"%s\"", k->key ? k->key : "", f->key, f->str);
            else snprintf(buf[n], sizeof buf[n], "%s", f->str);
            for (char *c = buf[n]; *c && !json; c++) *c = (char)tolower((unsigned char)*c);
            ent[n] = buf[n]; n++;
        }
    }
    qsort(ent, (size_t)n, sizeof *ent, cmp_s);
    out[0] = 0;
    for (int i = 0; i < n; i++) { if (i) strlcat(out, ",", cap); strlcat(out, ent[i], cap); }
    return n;
}
/* .NET mod hosts for dsl_reg.c (src/dsl_ml.h): BepInEx (dsl_bie_*) or MelonLoader (dsl_ml_*), one per mod set */
int dsl_net_kind(const char *kind) { return !kind ? 0 : !strcmp(kind, "bepinex") ? DSL_NET_BIE : !strcmp(kind, "melon") ? DSL_NET_ML : 0; }
const char *dsl_net_name(int k) { return k == DSL_NET_ML ? "MelonLoader" : "BepInEx"; }
const char *dsl_net_size(int k) {
#ifdef _WIN32
    return k == DSL_NET_ML ? "20 MB" : "30 MB";
#else
    return k == DSL_NET_ML ? "52 MB" : "31 MB";
#endif
}
int dsl_net_check(const char *const *kinds, int n, char *why, size_t cap) {
    int b = 0, m = 0;
    for (int i = 0; i < n; i++) { int k = dsl_net_kind(kinds[i]); b |= k == DSL_NET_BIE; m |= k == DSL_NET_ML; }
    if (b && m) {
        if (why && cap) snprintf(why, cap, "%s", TR("BepInEx mods and MelonLoader mods cannot run together. Turn off the mods of one of them."));
        return -1;
    }
    return b ? DSL_NET_BIE : m ? DSL_NET_ML : 0;
}
int dsl_net_state(int k) { return k == DSL_NET_ML ? dsl_ml_state() : dsl_bie_state(); }
int dsl_net_env(int k, const char *const *dirs, int n, char *out, size_t cap) {
    return k == DSL_NET_ML ? dsl_ml_env(dirs, n, out, cap) : dsl_bie_env(dirs, k ? n : 0, out, cap);
}
int dsl_net_install_start(int k) { return k == DSL_NET_ML ? dsl_ml_install_start() : dsl_bie_install_start(); }
int dsl_net_install_poll(int k, char *why, size_t cap) { return k == DSL_NET_ML ? dsl_ml_install_poll(why, cap) : dsl_bie_install_poll(why, cap); }
int dsl_net_running_dir(int k, const char *d) { return k == DSL_NET_ML ? dsl_ml_running_dir(d) : k == DSL_NET_BIE ? dsl_bie_running_dir(d) : 0; }
void dsl_net_mark_ready(void) { dsl_bie_mark_ready(); dsl_ml_mark_ready(); }

/* for dsl_reg.c: "<sorted shas>" of a mod folder's mod.json (every platform); "" when it lists none */
int dsl_mod_hashes(const char *id, char *out, size_t cap) {
    char mp[1300]; snprintf(mp, sizeof mp, "%s/%s/mod.json", modsdir, id);
    char *t = read_file(mp, 1 << 20); JNode *m = t ? json_parse(t) : NULL; free(t);
    int n = m ? listed_hashes(m, out, cap, 0) : 0; if (!m && cap) out[0] = 0;
    json_free(m); return n;
}
static void write_modset(void) {
    char *stxt = NULL; const char *sp = getenv("DSML_SESSION");
    if (sp) stxt = read_file(sp, 8 << 20);
    JNode *sess = stxt ? json_parse(stxt) : NULL; free(stxt);
    JNode *lines = json_get(sess, "modset_lines");
    SB b = {0};
    sb(&b, "{\"build\":\"%s\",\"loader\":\"%s\",\"api\":\"%s\",\"session_modset\":\"%s\",\"gameplay\":[", dsl_build_id ? dsl_build_id : "",
       DSL_RELEASE, DSL_LOADER_VERSION, getenv("DSML_MODSET") ? getenv("DSML_MODSET") : (json_str(sess, "modset") ? json_str(sess, "modset") : ""));
    int first = 1, diffs = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) { sb(&b, "],\"client\":["); first = 1; }
        for (int i = 0; i < norder; i++) {
            struct DSLPlugin *p = ORDER[i];
            if (p->state != 1 || p->gameplay != (pass == 0)) continue;
            if (pass == 0 && !strcmp(p->id, "multiplayer")) continue;      /* not part of the set: its HELLO checks build + protocol */
            sb(&b, "%s{\"id\":\"%s\",\"version\":\"%s\",\"files\":{", first ? "" : ",", p->id, p->version); first = 0;
            JNode *files = json_get(json_get(json_get(p->mf, "platforms"), DSL_PLATFORM), "files");
            char shas[16][65]; int ns = 0;
            static char all[16384];
            if (listed_hashes(p->mf, all, sizeof all, 1)) {                   /* every platform's listed files */
                sb(&b, "%s", all);
                if (files && files->t == JN_OBJ) for (JNode *f = files->child; f && ns < 16; f = f->next) if (f->t == JN_STR) snprintf(shas[ns++], 65, "%s", f->str);
            } else if (files && files->t == JN_OBJ && files->child) {
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
    /* BepInEx plugins and MelonLoader mods (mod.json kind "bepinex" / "melon"; started outside dlopen: start/win,
       the Mac launcher) that the profile enables: in the set like a code module, with the same every-platform hashes */
    for (int pass = 0; pass < 2; pass++) {
        sb(&b, "%s", pass ? "],\"melon\":[" : "],\"bepinex\":[");
        int f1 = 1;
        for (int i = 0; i < nprovided; i++) {
            char mp[1300]; snprintf(mp, sizeof mp, "%s/%s/mod.json", modsdir, provided[i]);
            char *t = read_file(mp, 1 << 20); JNode *m = t ? json_parse(t) : NULL; free(t);
            const char *k = json_str(m, "kind");
            if (k && !strcmp(k, pass ? "melon" : "bepinex")) {
                static char all[16384]; listed_hashes(m, all, sizeof all, 1);
                const char *sc = json_str(m, "scope");
                sb(&b, "%s{\"id\":\"%s\",\"version\":\"%s\",\"scope\":\"%s\",\"files\":{%s}}", f1 ? "" : ",", provided[i],
                   json_str(m, "version") ? json_str(m, "version") : "", sc && !strcmp(sc, "client") ? "client" : "gameplay", all);
                f1 = 0;
            }
            json_free(m);
        }
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

int dsl_module_syms(const char *name, void **out, int max) {
    int k = 0;
    for (int i = 0; i < norder && k < max; i++) if (ORDER[i]->state == 1 && ORDER[i]->h) { void *f = dsl_os_dlsym(ORDER[i]->h, name); if (f) out[k++] = f; }
    return k;
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
    dsl_soft_install();
    dsl_reg_ready(&host);                                            /* kit, rows, title "Mods": before the modules' init */
    dsl_saves_install();                                             /* the game's own saves in the Load list, played in place */
    dsl_cloud_install(&host);                                        /* modded-only saves: Steam Cloud backup */
    run_init();
    dsl_reg_after_init();
    write_modset();
    world_install();
    dsl_engine_set_late();
}
static int my_il2cpp_init(const char *domain) { int r = orig_init(domain); on_il2cpp_ready(""); return r; }
static int my_il2cpp_init_utf16(const void *domain) { int r = orig_init_utf16(domain); on_il2cpp_ready("_utf16"); return r; }

int (*dsl_core_prereq)(char *why, size_t n);
/* the start part's condition failed: modules opened by dsl_core_start go off before any of their hooks (as a failed
   dsl_module_load), no il2cpp_init hook, so no dsl_module_early / dsl_plugin_init; status file + one notice */
static void mods_off(const char *reason, const char *why) {
    int n = 0;
    for (int i = 0; i < norder; i++) if (ORDER[i]->state == 1) { dsl_unhook_owner(ORDER[i]->idx); ORDER[i]->state = 2; n++; }
    snprintf(status_why, sizeof status_why, "%s", why);
    hidden_note();
    dsl_log("START CONDITION FAILED (%s): %s", reason, status_why);
    dsl_log("%d module(s) off this session; no module hook, the game runs unmodded", n);
    write_status(0, reason, NULL, dsl_build_id, NULL);
    if (dsl_os_batchmode() || getenv("DSL_NO_ALERT")) return;
    dsl_os_notice(TR("Dungeon Settlers: mods are off"), status_why);
}

/* GameAssembly mapped (start part: Mac add-image callback, Windows DllMain), none of its code ran yet. The start part
   has checked that the mapped image is the build of dsl_core_start and set dsl_text_lo / hi, dsl_image_end and
   dsl_ga_sym. 0 = modules started (or nothing to do). */
int dsl_core_ga_mapped(uintptr_t base, const char *ga_path) {
    if (dsl_ga_base || !dsl_build_id) return -1;
    dsl_ga_base = base;
    snprintf(ga_path_s, sizeof ga_path_s, "%s", ga_path ? ga_path : "");
    host.ga_base = dsl_ga_base; host.game_build = dsl_build_id;
    dsl_log("GameAssembly build %s mapped at %p (code %#llx-%#llx, image end %#llx)", dsl_build_id, (void*)dsl_ga_base,
            (unsigned long long)dsl_text_lo, (unsigned long long)dsl_text_hi, (unsigned long long)dsl_image_end);
    int any = 0, want_init = 0;
    for (int i = 0; i < norder; i++) if (ORDER[i]->state == 1) { any = 1; if (ORDER[i]->init) want_init = 1; }
    if (getenv("DSML_SESSION") || getenv("DSL_PROFILE")) want_init = any = 1;   /* world check, Mods window: after il2cpp_init */
    if (dsl_soft_wanted()) any = 1;                                  /* B of a soft restart, even with no code mod */
    if (any) want_init = 1;                                          /* soft restart (dsl_soft.c) needs il2cpp names */
    if (!any) return 0;
    if (guard_check("_codepatch", loader_sig, "runtime code patching")) {
        dsl_codepatch_ok = 0; dsl_log("code patching off this session: modules not started");
        snprintf(status_why, sizeof status_why, "%s", TR("The game stopped while the mod loader was starting last time, so mods stay off "
                                                         "until the mod loader is updated."));
        size_t k = strlen(status_why); hidden_note();                 /* a notice only when worlds are hidden (before: silent) */
        if (strlen(status_why) > k && !dsl_os_batchmode() && !getenv("DSL_NO_ALERT")) dsl_os_notice(TR("Dungeon Settlers: mods are off"), status_why);
        if (strlen(status_why) > k) dsl_log("crash guard: %s", status_why);
        return -1;
    }
    if (dsl_engine_init()) return -1;
    dsl_engine_batch(1);                                             /* until the early starts are done: none of GameAssembly runs */
    if (dsl_core_prereq) {
        char why[sizeof status_why] = "";
        if (dsl_core_prereq(why, sizeof why)) { mods_off("start_condition_failed", why); dsl_engine_batch(0); return -1; }
    }
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
    dsl_engine_batch(0);
    if (!want_init) { guard_leave("_codepatch"); run_init(); write_modset(); dsl_engine_set_late(); }
    return 0;
}

/* The mapped GameAssembly is not the file the build came from (start part's check): mods stay off. */
void dsl_core_ga_differs(const char *ga_id, const char *found_build) {
    dsl_log("mapped GameAssembly %s differs from the one checked at start (build %s)", ga_id, dsl_build_id ? dsl_build_id : "?");
    snprintf(status_why, sizeof status_why, "%s", TR("The game files changed after the start check. Mods stay off. Restart the game."));
    build_refused("mapped_image_differs", ga_id, found_build, dsl_build_id);
}

/* log folder: DSL_LOG_DIR, else DSRIG_DIR, else the start part's default (the game's data folder) */
int dsl_core_log_open(const char *logdir_default, const char *self) {
    const char *ld = getenv("DSL_LOG_DIR"), *rd = getenv("DSRIG_DIR");
    if (ld && *ld) snprintf(logdir, sizeof logdir, "%s", ld);
    else if (rd && *rd) snprintf(logdir, sizeof logdir, "%s", rd);
    else if (logdir_default && *logdir_default) snprintf(logdir, sizeof logdir, "%s", logdir_default);
    else return -1;
    snprintf(self_path, sizeof self_path, "%s", self ? self : "");
    snprintf(statedir, sizeof statedir, "%s/dsload_state", logdir);
    mkdirs(statedir);
    char lp[1200], pp[1200];
    snprintf(lp, sizeof lp, "%s/dsload.log", logdir); snprintf(pp, sizeof pp, "%s/dsload.prev.log", logdir);
    remove(pp);
    if (rename(lp, pp) && !access(lp, 0))                /* Windows: a running game (soft restart A) still has it open */
        snprintf(lp, sizeof lp, "%s/dsload.%d.log", logdir, (int)getpid());
    lf = fopen(lp, "w");
    file_sig(self_path, loader_sig, sizeof loader_sig);
    dsl_log("dsload %s (module API %d, %s) pid %d: %s", DSL_RELEASE, DSL_API_VERSION, DSL_PLATFORM, (int)getpid(), self_path);
    return 0;
}
const char *dsl_core_logdir(void) { return logdir; }

/* ------------------------------------------------------------------ language (ds_i18n.h)
   Texts: <folder of this binary>/lang/<code>.json (Mac ROOT/loader/lang, Windows ROOT\game\lang). Before il2cpp: the
   saved choice in <data>/UserSetting.json (GeneralSettingData.SaveCurrentLanguageType), for the start notices; then the
   game's own Refactor.Setting.LanguageSetting.GetCurrentLanguage() once per frame (it can change at the title, Options). */
static int lang_early_done;
void dsl_lang_early(void) {
    if (lang_early_done) return; lang_early_done = 1;
    char d[1100]; snprintf(d, sizeof d, "%s", dsl_core_self_utf8[0] ? dsl_core_self_utf8 : self_path);   /* UTF-8 for dsi_set_dir */
    char *sl = strrchr(d, '/'), *bs = strrchr(d, '\\'); if (bs > sl) sl = bs;
    if (sl) { snprintf(sl, sizeof d - (size_t)(sl - d), "/lang"); dsi_set_dir(d); } else d[0] = 0;
    const char *dd = getenv("DSML_DATA"), *rd = getenv("DSRIG_DIR");
    char c[4][1200]; int nc = 0;
    if (dd && *dd) snprintf(c[nc++], sizeof c[0], "%s/UserSetting.json", dd);
    if (rd && *rd) snprintf(c[nc++], sizeof c[0], "%s/data/UserSetting.json", rd);
    if (logdir[0]) snprintf(c[nc++], sizeof c[0], "%s/UserSetting.json", logdir);
    int lang = 0, k;
    for (k = 0; k < nc; k++) { char *t = read_file(c[k], 1 << 22); if (!t) continue; lang = dsi_lang_from_usersetting(t); free(t); break; }
    dsi_set_lang(lang);
    dsl_log("language %d (%s) from %s; texts %s: %d loaded, %d dropped", lang, dsi_code(), k < nc ? c[k] : "no UserSetting.json",
            d[0] ? d : "(none)", dsi_loaded(), dsi_dropped());
}
int dsl_reg_scene(void);                                         /* dsl_reg.c: MainFlow scene, 1 title, 2 world, -1 none */
int dsl_lang_poll(void) {
    static int32_t (*get)(const void *); static int tried;
    if (!dsl_ready || dsl_reg_scene() < 1) return 0;               /* during the boot the static is not loaded yet (0) */
    if (!tried) {
        tried = 1;
        get = (int32_t (*)(const void *))dsl_find_method_address("Refactor.Setting", "LanguageSetting", "GetCurrentLanguage", 0);
        dsl_log("language: LanguageSetting.GetCurrentLanguage %s", get ? "found" : "NOT found: the start language stays");
    }
    if (!get) return 0;
    int v = get(NULL);
    if (!dsi_set_lang(v)) return 0;
    dsl_log("language: the game's language is now %d (%s): %d texts, %d dropped", dsi_lang(), dsi_code(), dsi_loaded(), dsi_dropped());
    return 1;
}

/* Before GameAssembly is mapped. build = the start part's build for the GameAssembly file (NULL = unknown),
   ga_id = what it read (Mac arm64 LC_UUID, Windows PE stamp) for the logs and the status file, modsdir_default =
   the mods folder when DSL_MODS_DIR is not set. 0 = modules opened, the start part goes on to dsl_core_ga_mapped. */
int dsl_core_start(const char *build, const char *ga_id, const char *modsdir_default) {
    const char *md = getenv("DSL_MODS_DIR"), *pf = getenv("DSL_PROFILE");
    if (md && *md) snprintf(modsdir, sizeof modsdir, "%s", md);
    else if (modsdir_default) snprintf(modsdir, sizeof modsdir, "%s", modsdir_default);
    snprintf(profile_path, sizeof profile_path, "%s", (pf && *pf) ? pf : "");
    if (!profile_path[0]) snprintf(profile_path, sizeof profile_path, "%s/profile.json", modsdir);
    dsl_lang_early();                                                /* the notices below speak the game's language */
    dsl_build_id = build;
    const char *pin = launcher_pin();
    if (!dsl_build_id) {
        snprintf(status_why, sizeof status_why, "%s", TR("The game was updated. Mods stay off until they are updated for this game version."));
        dsl_log("unknown GameAssembly build (%s%s%s): loader inactive, the game runs without code mods", ga_id, pin ? "; the launcher pinned build " : "", pin ? pin : "");
        build_refused("unknown_build", ga_id, NULL, pin); return -1;
    }
    if (pin && strcmp(pin, dsl_build_id)) {
        snprintf(status_why, sizeof status_why, TR("This game is build %s, but the launcher started this world for build %s. "
                 "Mods stay off. Start the world from the launcher again."), dsl_build_id, pin);
        build_refused("not_pinned_build", ga_id, dsl_build_id, pin); return -1;
    }
    snprintf(status_why, sizeof status_why, "build %s%s", dsl_build_id, pin ? " (launcher pin)" : "");
    write_status(1, "ok", ga_id, dsl_build_id, pin ? pin : dsl_build_id);
    dsl_log("game build %s (%s); mods %s; profile %s", dsl_build_id, ga_id, modsdir, profile_path);

    host = (DSLHost){ .api_version = DSL_API_VERSION, .size = sizeof(DSLHost), .game_build = dsl_build_id, .ga_base = 0,
        .log = h_log, .plugin_id = h_plugin_id, .rva = h_rva, .il2cpp_ready = h_ready,
        .il2cpp_fn = dsl_il2cpp_fn, .find_class = dsl_find_class, .find_method = dsl_find_method,
        .method_address = dsl_method_address, .find_method_address = dsl_find_method_address, .field_offset = dsl_field_offset,
        .hook = h_hook, .hook_method = h_hook_method, .unhook = h_unhook,
        .hook_rva = h_hook_rva, .table = h_table, .module_dir = h_module_dir, .ga_path = h_ga_path, .module_loaded = h_module_loaded,
        .patch_rva = h_patch_rva, .gameplay_mods = h_gameplay_mods, .soft_restart = dsl_soft_apply };
    dsl_reg_host(&host);
    scan_mods(dsl_build_id);
    if (!norder && !getenv("DSML_SESSION") && !getenv("DSL_PROFILE") && !dsl_soft_wanted()) { dsl_log("no module to load"); return 1; }
    open_modules();
    return 0;
}
