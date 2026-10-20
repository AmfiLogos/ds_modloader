// dsl_moddata.c - a data folder per mod, and its multiplayer sync (owner 2026-10-18, lane moddata, v0.1.16).
//
// Folder: <data>/ModData/<mod id>/ (<data> = the game's save folder = Application.persistentDataPath of the modded
// game; the parent of dsl_reg_data_dir()). It is outside the mods folder, so a mod update (the old mod folder is moved
// to a backup, dsl_hostmods.c / the installer) and a disable / enable never touch it. Modules: DSLHost.mod_data_dir
// (SDK API 6). .NET / BepInEx mods: env DSML_MOD_DATA (= <data>/ModData), else persistentDataPath + "/ModData";
// <data>/ModData/moddata.json says the folder, each running mod's folder and sync patterns, and the session state.
//
// mod.json "sync": ["campaign/*.json", "markers/**"]: files of that folder that are gameplay (every multiplayer player
// needs the same). Patterns are relative, '/' separated: * and ? inside one name, ** = any number of folders. Only
// running gameplay mods count. Names: A-Z a-z 0-9 . _ - and inner spaces, never starting with '.', no Windows device
// names, no program files (.dll .exe .dylib ...); links are never followed. Anything else is skipped with a log line.
//
// Multiplayer (the multiplayer mod's join point block, ds_multiplayer src/mp_game.c "MDAT"):
//   host   dsl_moddata_pack() builds the block (every sync file of every such mod; > 1 MB or > 256 files for one mod =
//          its list only, marked "over", and a log line), dsl_moddata_copy() hands it out.
//   guest  dsl_moddata_apply() checks ALL of it first (ids, patterns of THIS copy's mod.json, safe names, sha256 of each
//          file), then puts its own matching files aside (<data>/ModData/.mp_stash/<id>/), writes the host's, reads its
//          folder again and compares the data fingerprint (sha256 of the sorted "<id>/<path>@<sha256>" lines) with the
//          host's. Any failure = the join is refused (a code and the reason) and the own files come back.
//          dsl_moddata_session_end(): back on the title: the session files go, the own files come back. A game that
//          ended inside a session gets them back at its next start (dsl_moddata_start). Nothing received is ever run.
// The stash marker <data>/ModData/.mp_stash/session.json {"phase", "mods": {id: [patterns]}} makes every step safe to
// redo after a crash: "stashing" (own files moving out) -> "playing" (host files in) -> "back" (session files in
// .mp_stash/.trash, own files moving back) -> marker gone.
#include "dsl_internal.h"
#include "dsl_menu.h"
#include "dsl_os.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#define MD_EXPORT __declspec(dllexport)
#else
#define MD_EXPORT __attribute__((visibility("default")))
#endif

#define MD_MAX_MOD_BYTES (1u << 20)                   /* per mod, sum of its sync files */
#define MD_MAX_FILES 256                              /* per mod */
#define MD_MAX_MODS 64
#define MD_MAX_PATH 200                               /* a relative path */
#define MD_MAX_DEPTH 8
#define MD_MAX_PAT 16                                 /* patterns per mod */
enum { MD_OK = 0, MD_TOO_BIG = 1, MD_BAD = 2, MD_WRITE = 3, MD_DIFFERS = 4 };

/* ------------------------------------------------------------------ paths */
static char root_s[1100];                             /* <data>/ModData (UTF-8) */
const char *dsl_moddata_root(void) {
    if (root_s[0]) return root_s;
    const char *m = dsl_reg_data_dir(), *sl = strrchr(m, '/');
#ifdef _WIN32
    const char *bs = strrchr(m, '\\'); if (bs && (!sl || bs > sl)) sl = bs;
#endif
    if (sl) snprintf(root_s, sizeof root_s, "%.*s/ModData", (int)(sl - m), m);
    else snprintf(root_s, sizeof root_s, "ModData");
    mkdir(root_s, 0755);
    return root_s;
}
static int is_link(const char *p) {
#ifdef _WIN32
    unsigned long a = dsl_u8_attr(p); return a != 0xFFFFFFFFul && (a & 0x400);   /* FILE_ATTRIBUTE_REPARSE_POINT */
#else
    struct stat st; return !lstat(p, &st) && S_ISLNK(st.st_mode);
#endif
}
static int is_dir(const char *p) { struct stat st; return !stat(p, &st) && S_ISDIR(st.st_mode); }
int dsl_moddata_id_ok(const char *s) {               /* a mod id as a folder name */
    if (!s || !*s || strlen(s) >= 64 || *s == '.') return 0;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '_' || *s == '.')) return 0;
    return 1;
}
static int name_ok(const char *s, size_t n) {          /* one path component */
    if (!n || n > 96 || s[0] == '.' || s[0] == ' ' || s[n - 1] == ' ' || s[n - 1] == '.') return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ' ')) return 0;
    }
    static const char *dev[] = { "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
                                 "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };
    size_t b = 0; while (b < n && s[b] != '.') b++;
    for (size_t k = 0; k < sizeof dev / sizeof *dev; k++) if (strlen(dev[k]) == b && !strncasecmp(s, dev[k], b)) return 0;
    return 1;
}
static int exec_ext(const char *p) {                    /* program files are never synced */
    static const char *x[] = { ".dll", ".exe", ".dylib", ".so", ".bat", ".cmd", ".com", ".sh", ".ps1", ".scr", ".msi", ".jar",
                               ".app", ".command", ".vbs", ".js", ".py", ".lnk" };
    const char *d = strrchr(p, '.'); if (!d) return 0;
    for (size_t k = 0; k < sizeof x / sizeof *x; k++) if (!strcasecmp(d, x[k])) return 1;
    return 0;
}
/* a relative path from a mod.json or the network: 1 = safe ('/' only, no "..", no absolute, safe names, depth) */
int dsl_moddata_path_ok(const char *p) {
    size_t L = p ? strlen(p) : 0; if (!L || L > MD_MAX_PATH || exec_ext(p)) return 0;
    int depth = 0;
    for (const char *s = p; ; ) {
        const char *e = strchr(s, '/'); size_t n = e ? (size_t)(e - s) : strlen(s);
        if (!name_ok(s, n) || ++depth > MD_MAX_DEPTH) return 0;
        if (!e) break; s = e + 1;
    }
    return 1;
}

/* ------------------------------------------------------------------ patterns */
static int seg_match(const char *p, size_t pn, const char *s, size_t sn) {   /* * and ? inside one name */
    if (!pn) return !sn;
    if (*p == '*') { for (size_t k = 0; k <= sn; k++) if (seg_match(p + 1, pn - 1, s + k, sn - k)) return 1; return 0; }
    return sn && (*p == '?' || *p == *s) && seg_match(p + 1, pn - 1, s + 1, sn - 1);
}
static int path_match(const char *p, const char *s) {  /* '/'-separated; "**" = zero or more names */
    if (!*p) return !*s;
    const char *pe = strchr(p, '/'); size_t pn = pe ? (size_t)(pe - p) : strlen(p);
    const char *pnext = pe ? pe + 1 : p + pn;
    if (pn == 2 && p[0] == '*' && p[1] == '*') {
        if (path_match(pnext, s)) return 1;
        if (!*s) return 0;
        const char *se = strchr(s, '/');
        return se ? path_match(p, se + 1) : path_match(pnext, "") ;
    }
    if (!*s) return 0;
    const char *se = strchr(s, '/'); size_t sn = se ? (size_t)(se - s) : strlen(s);
    if (!seg_match(p, pn, s, sn)) return 0;
    if (!pe) return !se;
    return se ? path_match(pnext, se + 1) : 0;
}
static int pattern_ok(const char *p) {                  /* like a path, with * ? ** allowed in names */
    size_t L = p ? strlen(p) : 0; if (!L || L > MD_MAX_PATH || *p == '/' || strstr(p, "..") || strchr(p, '\\') || strchr(p, ':')) return 0;
    for (const char *s = p; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
                                          strchr("-_. */?", *s))) return 0;
    for (const char *s = p; ; ) { const char *e = strchr(s, '/'); if ((e ? (size_t)(e - s) : strlen(s)) == 0 || *s == '.') return 0; if (!e) break; s = e + 1; }
    return 1;
}

/* ------------------------------------------------------------------ the running mods with "sync" */
typedef struct { char id[64]; int np; char pat[MD_MAX_PAT][MD_MAX_PATH + 1]; } SyncMod;
static SyncMod SM[MD_MAX_MODS]; static int nsm = -1;
static char *slurp(const char *p, size_t cap, size_t *got) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    char *b = malloc(cap + 1); size_t n = b ? fread(b, 1, cap, f) : 0; fclose(f);
    if (!b) return NULL; b[n] = 0; if (got) *got = n; return b;
}
static int read_patterns(const char *id, SyncMod *m) {   /* <mods>/<id>/mod.json "sync": count of valid patterns */
    char p[1400]; snprintf(p, sizeof p, "%s/%s/mod.json", dsl_mods_dir(), id);
    char *t = slurp(p, 1 << 20, NULL); JNode *j = t ? json_parse(t) : NULL; free(t);
    JNode *s = json_get(j, "sync"); m->np = 0; snprintf(m->id, sizeof m->id, "%s", id);
    if (s && s->t == JN_ARR) for (JNode *c = s->child; c; c = c->next) {
        if (c->t != JN_STR || !pattern_ok(c->str) || exec_ext(c->str)) { dsl_log("moddata: %s: sync pattern \"%s\" skipped (relative, '/' only, names A-Z a-z 0-9 . _ - space * ?, no ..)", id, c->t == JN_STR ? c->str : "?"); continue; }
        if (m->np < MD_MAX_PAT) snprintf(m->pat[m->np++], sizeof m->pat[0], "%s", c->str);
    }
    json_free(j);
    return m->np;
}
static void sync_scan(void) {
    if (nsm >= 0) return; nsm = 0;
    for (int i = 0; i < dsl_mods_count() && nsm < MD_MAX_MODS; i++) {
        const DSLModInfo *x = dsl_mods_at(i);
        if (!x || x->active != 1 || strcmp(x->scope, "gameplay") || x->feature || !strcmp(x->id, "multiplayer") || !dsl_moddata_id_ok(x->id)) continue;
        if (read_patterns(x->id, &SM[nsm])) nsm++;
    }
}
void dsl_moddata_rescan(void) { nsm = -1; }
static const SyncMod *sync_of(const char *id) { sync_scan(); for (int i = 0; i < nsm; i++) if (!strcmp(SM[i].id, id)) return &SM[i]; return NULL; }
static int matches(const char *const *pat, int np, const char *rel) {
    if (!dsl_moddata_path_ok(rel)) return 0;
    for (int k = 0; k < np; k++) if (path_match(pat[k], rel)) return 1;
    return 0;
}

/* ------------------------------------------------------------------ folder walk: matching files, sorted */
typedef struct { char rel[MD_MAX_PATH + 1]; long long size; } FileE;
typedef struct { FileE *f; int n, cap, over_files; long long bytes; } FileList;
static int cmp_file(const void *a, const void *b) { return strcmp(((const FileE *)a)->rel, ((const FileE *)b)->rel); }
static void walk(const char *base, const char *rel, int depth, const char *const *pat, int np, FileList *L) {
    char dir[1500]; snprintf(dir, sizeof dir, "%s%s%s", base, *rel ? "/" : "", rel);
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char r[600], full[2200]; snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", e->d_name);
        snprintf(full, sizeof full, "%s/%s", base, r);
        if (strlen(r) > MD_MAX_PATH || is_link(full)) continue;
        struct stat st; if (stat(full, &st)) continue;
        if (S_ISDIR(st.st_mode)) { if (depth + 1 < MD_MAX_DEPTH) walk(base, r, depth + 1, pat, np, L); continue; }
        if (!S_ISREG(st.st_mode) || !matches(pat, np, r)) continue;
        if (L->n >= MD_MAX_FILES) { L->over_files = 1; continue; }
        if (L->n == L->cap) { L->cap = L->cap ? L->cap * 2 : 16; L->f = realloc(L->f, (size_t)L->cap * sizeof *L->f); }
        snprintf(L->f[L->n].rel, sizeof L->f[0].rel, "%s", r); L->f[L->n].size = (long long)st.st_size; L->bytes += st.st_size; L->n++;
    }
    closedir(d);
}
static FileList list_files(const char *id, const char *const *pat, int np) {
    FileList L = {0}; char base[1200]; snprintf(base, sizeof base, "%s/%s", dsl_moddata_root(), id);
    if (np && is_dir(base) && !is_link(base)) walk(base, "", 0, pat, np, &L);
    if (L.n) qsort(L.f, (size_t)L.n, sizeof *L.f, cmp_file);
    return L;
}
static const char *const *pats(const SyncMod *m) { static const char *p[MD_MAX_PAT]; for (int k = 0; k < m->np; k++) p[k] = m->pat[k]; return p; }

/* ------------------------------------------------------------------ small helpers */
typedef struct { char *p; size_t n, cap; } Buf;
static void bput(Buf *b, const void *d, size_t n) {
    if (b->n + n > b->cap) { while (b->n + n > b->cap) b->cap = b->cap ? b->cap * 2 : 4096; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, d, n); b->n += n;
}
static void bput32(Buf *b, uint32_t v) { bput(b, &v, 4); }
static void bputs(Buf *b, const char *s) { bput(b, s, strlen(s)); }
static void jesc(Buf *b, const char *s) {
    bputs(b, "\"");
    for (; *s; s++) { char e[8]; if (*s == '"' || *s == '\\') { e[0] = '\\'; e[1] = *s; bput(b, e, 2); }
                      else if ((unsigned char)*s < 0x20) { snprintf(e, sizeof e, "\\u%04x", (unsigned char)*s); bputs(b, e); } else bput(b, s, 1); }
    bputs(b, "\"");
}
static int put_file(const char *path, const void *d, size_t n) {   /* temp + rename: a failed write keeps the old file */
    char tmp[1500]; snprintf(tmp, sizeof tmp, "%s.tmp-%d", path, (int)getpid());
    FILE *f = fopen(tmp, "wb"); if (!f) return -1;
    int bad = (n && fwrite(d, 1, n, f) != n); bad |= fflush(f) != 0; bad |= fclose(f) != 0;
    if (bad || rename(tmp, path)) { remove(tmp); return -1; }
    return 0;
}
static int mkdirs(const char *base, const char *rel_dir) {          /* base/<rel_dir>, every part a real folder */
    char p[1500]; snprintf(p, sizeof p, "%s", base); size_t bl = strlen(p);
    if (!is_dir(p) || is_link(p)) return -1;
    for (const char *s = rel_dir; *s; ) {
        const char *e = strchr(s, '/'); size_t n = e ? (size_t)(e - s) : strlen(s);
        snprintf(p + bl, sizeof p - bl, "/%.*s", (int)n, s); bl = strlen(p);
        if (is_link(p)) return -1;
        if (!is_dir(p) && mkdir(p, 0755) && !is_dir(p)) return -1;
        if (!e) break; s = e + 1;
    }
    return 0;
}
static int move_file(const char *from_base, const char *to_base, const char *rel) {   /* same volume: rename */
    char a[1500], b[1500], dir[MD_MAX_PATH + 1]; snprintf(a, sizeof a, "%s/%s", from_base, rel); snprintf(b, sizeof b, "%s/%s", to_base, rel);
    snprintf(dir, sizeof dir, "%s", rel); char *sl = strrchr(dir, '/'); if (sl) *sl = 0; else dir[0] = 0;
    if (mkdirs(to_base, dir)) return -1;
    return rename(a, b);
}
static void prune_dirs(const char *dir, int depth) {               /* empty folders under dir (dir itself too) */
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *e; char sub[64][100]; int ns = 0;
    while ((e = readdir(d))) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && ns < 64 && strlen(e->d_name) < 100) snprintf(sub[ns++], 100, "%s", e->d_name);
    closedir(d);
    for (int i = 0; i < ns && depth < MD_MAX_DEPTH + 2; i++) { char p[1500]; snprintf(p, sizeof p, "%s/%s", dir, sub[i]); if (is_dir(p) && !is_link(p)) prune_dirs(p, depth + 1); }
    rmdir(dir);                                                    /* fails while not empty: fine */
}
static void sha_lines(Buf *lines, const char *id, const FileList *L, char (*shas)[65]) {
    for (int i = 0; i < L->n; i++) { if (lines->n) bputs(lines, "\n"); bputs(lines, id); bputs(lines, "/"); bputs(lines, L->f[i].rel); bputs(lines, "@"); bputs(lines, shas[i]); }
}
static int sha_file(const char *base, const char *rel, char out[65]) {
    char p[1500]; snprintf(p, sizeof p, "%s/%s", base, rel); size_t n = 0;
    char *b = slurp(p, MD_MAX_MOD_BYTES + 1, &n); if (!b) return -1;
    dsl_os_sha256_buf(b, n, out); free(b); return 0;
}
static int cmp_cstr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static void fp_of(Buf *lines, char out[65]) {                      /* lines '\n'-joined in any order -> sorted -> sha256 */
    if (!lines->n) { memset(out, '0', 64); out[64] = 0; return; }
    bput(lines, "", 1);
    size_t cap = 1; for (size_t i = 0; i < lines->n; i++) cap += lines->p[i] == '\n';
    char **v = malloc(cap * sizeof *v); int n = 0;
    for (char *s = lines->p; s && n < (int)cap; ) { v[n++] = s; char *e = strchr(s, '\n'); if (e) { *e = 0; s = e + 1; } else s = NULL; }
    qsort(v, (size_t)n, sizeof *v, cmp_cstr);
    Buf j = {0}; for (int i = 0; i < n; i++) { if (i) bputs(&j, "\n"); bputs(&j, v[i]); }
    dsl_os_sha256_buf(j.p ? j.p : "", j.n, out); free(j.p); free(v);
}
/* the data fingerprint of this copy now: sha256 of the sorted "<id>/<path>@<sha256>" lines of every running sync mod
   ('0' x 64 = no file). Also in dsload_modset.json "data". */
int dsl_moddata_fingerprint(char out[65]) {
    sync_scan(); Buf lines = {0};
    for (int i = 0; i < nsm; i++) {
        FileList L = list_files(SM[i].id, pats(&SM[i]), SM[i].np); char (*sh)[65] = calloc((size_t)L.n + 1, 65);
        char base[1200]; snprintf(base, sizeof base, "%s/%s", dsl_moddata_root(), SM[i].id);
        for (int k = 0; k < L.n; k++) sha_file(base, L.f[k].rel, sh[k]);
        sha_lines(&lines, SM[i].id, &L, sh); free(sh); free(L.f);
    }
    fp_of(&lines, out); free(lines.p);
    return nsm;
}

/* ------------------------------------------------------------------ moddata.json (for .NET mods and people) */
static int guest_now;
static void write_info(void) {
    sync_scan(); Buf b = {0}; char nat[1100];
    bputs(&b, "{\n  \"format\": 1,\n  \"dir\": "); jesc(&b, dsl_os_native_path(dsl_moddata_root(), nat, sizeof nat));
    bputs(&b, ",\n  \"session\": "); bputs(&b, guest_now ? "\"guest\"" : "\"none\"");
    bputs(&b, ",\n  \"note\": \"One folder per mod: <dir>/<mod id>. Kept when a mod is updated or turned off. Files matching a mod's mod.json sync patterns are the host's copy while you are a multiplayer guest; yours come back after the session.\",\n  \"mods\": {");
    int k = 0;
    for (int i = 0; i < dsl_mods_count(); i++) {
        const DSLModInfo *x = dsl_mods_at(i); if (!x || x->active != 1 || !dsl_moddata_id_ok(x->id)) continue;
        char d[1300]; snprintf(d, sizeof d, "%s/%s", dsl_moddata_root(), x->id);
        bputs(&b, k++ ? ",\n    " : "\n    "); jesc(&b, x->id); bputs(&b, ": {\"dir\": "); jesc(&b, dsl_os_native_path(d, nat, sizeof nat));
        bputs(&b, ", \"sync\": [");
        const SyncMod *m = sync_of(x->id);
        for (int p = 0; m && p < m->np; p++) { if (p) bputs(&b, ", "); jesc(&b, m->pat[p]); }
        bputs(&b, "]}");
    }
    bputs(&b, k ? "\n  }\n}\n" : "}\n}\n"); bput(&b, "", 1);
    char p[1200]; snprintf(p, sizeof p, "%s/moddata.json", dsl_moddata_root());
    if (put_file(p, b.p, b.n - 1)) dsl_log("moddata: cannot write %s", p);
    free(b.p);
}

/* ------------------------------------------------------------------ the guest's stash (crash-safe) */
static void stash_dir(char *out, size_t n, const char *sub) { snprintf(out, n, "%s/.mp_stash%s%s", dsl_moddata_root(), sub ? "/" : "", sub ? sub : ""); }
static void marker_path(char *out, size_t n) { stash_dir(out, n, "session.json"); }
typedef struct { int n; SyncMod m[MD_MAX_MODS]; char phase[16]; } Marker;
static int marker_write(const Marker *mk) {
    char d[1200]; stash_dir(d, sizeof d, NULL); mkdir(d, 0755);
    Buf b = {0}; bputs(&b, "{\"phase\":\""); bputs(&b, mk->phase); bputs(&b, "\",\"mods\":{");
    for (int i = 0; i < mk->n; i++) {
        if (i) bputs(&b, ","); jesc(&b, mk->m[i].id); bputs(&b, ":[");
        for (int k = 0; k < mk->m[i].np; k++) { if (k) bputs(&b, ","); jesc(&b, mk->m[i].pat[k]); }
        bputs(&b, "]");
    }
    bputs(&b, "}}\n");
    char p[1200]; marker_path(p, sizeof p); int r = put_file(p, b.p, b.n); free(b.p); return r;
}
static int marker_read(Marker *mk) {                   /* 1 = a session marker is there */
    char p[1200]; marker_path(p, sizeof p); char *t = slurp(p, 1 << 20, NULL); if (!t) return 0;
    JNode *j = json_parse(t); free(t); memset(mk, 0, sizeof *mk);
    const char *ph = json_str(j, "phase"); snprintf(mk->phase, sizeof mk->phase, "%s", ph ? ph : "playing");   /* unreadable: the careful way */
    JNode *m = json_get(j, "mods");
    if (m && m->t == JN_OBJ) for (JNode *c = m->child; c && mk->n < MD_MAX_MODS; c = c->next) {
        if (!c->key || !dsl_moddata_id_ok(c->key) || c->t != JN_ARR) continue;
        SyncMod *s = &mk->m[mk->n++]; snprintf(s->id, sizeof s->id, "%s", c->key);
        for (JNode *q = c->child; q && s->np < MD_MAX_PAT; q = q->next) if (q->t == JN_STR && pattern_ok(q->str)) snprintf(s->pat[s->np++], sizeof s->pat[0], "%s", q->str);
    }
    json_free(j);
    return 1;
}
/* every matching file of the mods in mk: from <root>/<id>/ to <base>/<id>/ (base = stash or trash). -1 = one failed */
static int move_all(const Marker *mk, const char *to_base, int from_stash) {
    int bad = 0;
    for (int i = 0; i < mk->n; i++) {
        const SyncMod *m = &mk->m[i]; char from[1300], to[1300], sbase[1200];
        if (from_stash) { stash_dir(sbase, sizeof sbase, NULL); snprintf(from, sizeof from, "%s/%s", sbase, m->id); snprintf(to, sizeof to, "%s/%s", dsl_moddata_root(), m->id); }
        else { snprintf(from, sizeof from, "%s/%s", dsl_moddata_root(), m->id); snprintf(to, sizeof to, "%s/%s", to_base, m->id); }
        FileList L = {0};
        if (from_stash) { if (is_dir(from)) walk(from, "", 0, pats(m), m->np, &L); }
        else L = list_files(m->id, pats(m), m->np);
        if (L.n && !is_dir(to) && mkdir(to, 0755) && !is_dir(to)) bad = 1;
        for (int k = 0; k < L.n; k++) if (move_file(from, to, L.f[k].rel)) { bad = 1; dsl_log("moddata: %s: cannot move %s", m->id, L.f[k].rel); }
        free(L.f);
    }
    return bad ? -1 : 0;
}
static void trash_clear(void) {
    char t[1200]; stash_dir(t, sizeof t, ".trash");
    if (!is_dir(t)) return;
    DIR *d = opendir(t); struct dirent *e;                /* everything under .trash goes: walk each mod folder there */
    while (d && (e = readdir(d))) if (dsl_moddata_id_ok(e->d_name)) {
        char md[1400]; snprintf(md, sizeof md, "%s/%s", t, e->d_name);
        static const char *any[] = { "**" }; FileList L = {0}; walk(md, "", 0, any, 1, &L);
        for (int k = 0; k < L.n; k++) { char p[2000]; snprintf(p, sizeof p, "%s/%s", md, L.f[k].rel); remove(p); }
        free(L.f);
    }
    if (d) closedir(d);
    prune_dirs(t, 0);
}
/* own files back: the session's files into .trash, the stash back, .trash emptied, marker gone. Safe to redo. */
static int restore(const char *why, int info) {
    static Marker mk; if (!marker_read(&mk)) return 0;
    char t[1200]; stash_dir(t, sizeof t, ".trash");
    if (!strcmp(mk.phase, "playing")) {
        mkdir(t, 0755);
        if (move_all(&mk, t, 0)) { dsl_log("moddata: restore (%s): session files could not all be moved aside; tried again at the next start", why); return -1; }
        snprintf(mk.phase, sizeof mk.phase, "back"); if (marker_write(&mk)) return -1;
    }
    if (move_all(&mk, NULL, 1)) { dsl_log("moddata: restore (%s): own files could not all be put back; tried again at the next start", why); return -1; }
    trash_clear();
    char p[1200]; marker_path(p, sizeof p); remove(p);
    char s[1200]; stash_dir(s, sizeof s, NULL); prune_dirs(s, 0);
    for (int i = 0; i < mk.n; i++) dsl_log("moddata: %s: own sync files are back (%s)", mk.m[i].id, why);
    guest_now = 0; if (info) write_info();               /* not at the very start: the mods are not scanned yet */
    return 0;
}

/* ------------------------------------------------------------------ host: the block */
/* "MD1\0", u32 nmods, per mod: u8 idlen, id, u8 flags (1 = over the limit: no bytes), u32 nfiles,
   per file: u16 pathlen, path, sha256 (64 hex), u32 size, bytes (not when over); then the fingerprint (64 hex). */
static Buf packed; static int packed_ok;
MD_EXPORT long dsl_moddata_pack(void) {
    free(packed.p); memset(&packed, 0, sizeof packed); packed_ok = 0;
    sync_scan(); if (!nsm) return 0;
    Buf *b = &packed; Buf lines = {0}; bputs(b, "MD1"); bput(b, "", 1); bput32(b, (uint32_t)nsm);
    for (int i = 0; i < nsm; i++) {
        const SyncMod *m = &SM[i]; char base[1200]; snprintf(base, sizeof base, "%s/%s", dsl_moddata_root(), m->id);
        FileList L = list_files(m->id, pats(m), m->np);
        int over = L.over_files || L.bytes > MD_MAX_MOD_BYTES;
        if (over) dsl_log("moddata: %s: sync files are %lld bytes in %d%s files, over the limit (%u bytes, %d files): not sent; guests are refused until they are smaller",
                          m->id, L.bytes, L.n, L.over_files ? "+" : "", MD_MAX_MOD_BYTES, MD_MAX_FILES);
        uint8_t il = (uint8_t)strlen(m->id), fl = (uint8_t)over; bput(b, &il, 1); bputs(b, m->id); bput(b, &fl, 1); bput32(b, (uint32_t)L.n);
        char (*sh)[65] = calloc((size_t)L.n + 1, 65); int fail = 0;
        for (int k = 0; k < L.n && !fail; k++) {
            char p[1500]; snprintf(p, sizeof p, "%s/%s", base, L.f[k].rel); size_t n = 0;
            char *d = slurp(p, MD_MAX_MOD_BYTES + 1, &n);
            if (!d) { fail = 1; dsl_log("moddata: %s: cannot read %s", m->id, L.f[k].rel); break; }
            dsl_os_sha256_buf(d, n, sh[k]);
            uint16_t pl = (uint16_t)strlen(L.f[k].rel); bput(b, &pl, 2); bputs(b, L.f[k].rel); bput(b, sh[k], 64);
            bput32(b, (uint32_t)n); if (!over) bput(b, d, n);
            free(d);
        }
        if (!fail) sha_lines(&lines, m->id, &L, sh);
        free(sh); free(L.f);
        if (fail) { free(lines.p); free(packed.p); memset(&packed, 0, sizeof packed); return -1; }
        dsl_log("moddata: %s: %d sync file(s), %lld bytes%s", m->id, L.n, L.bytes, over ? " (list only)" : " packed for the join");
    }
    char fp[65]; fp_of(&lines, fp); free(lines.p); bput(b, fp, 64);
    packed_ok = 1;
    return (long)b->n;
}
MD_EXPORT size_t dsl_moddata_copy(uint8_t *out, size_t cap) {
    if (!packed_ok || cap < packed.n) return 0;
    memcpy(out, packed.p, packed.n); return packed.n;
}

/* ------------------------------------------------------------------ guest: apply */
typedef struct { const char *rel; size_t rl; const char *sha; const uint8_t *data; uint32_t size; } InF;
typedef struct { char id[64]; int over; uint32_t nf; InF *f; } InM;
static int rd(const uint8_t **p, const uint8_t *end, void *out, size_t n) { if ((size_t)(end - *p) < n) return -1; if (out) memcpy(out, *p, n); *p += n; return 0; }
#define WHY(...) snprintf(why, whycap, __VA_ARGS__)
/* 0 = the host's files are in place (and equal), else MD_* with the reason (English, for the log; mod id first) */
MD_EXPORT int dsl_moddata_apply(const uint8_t *data, size_t len, char *why, size_t whycap) {
    char dummy[8]; if (!why || !whycap) { why = dummy; whycap = sizeof dummy; } why[0] = 0;
    const uint8_t *p = data, *end = data + len; uint32_t nm = 0; char mg[4];
    if (rd(&p, end, mg, 4) || memcmp(mg, "MD1", 4) || rd(&p, end, &nm, 4) || nm > MD_MAX_MODS) { WHY("the host's mod data block is not readable"); return MD_BAD; }
    InM *in = calloc(nm + 1, sizeof *in); int rc = MD_OK; Buf host_lines = {0};
    sync_scan();
    for (uint32_t i = 0; i < nm && !rc; i++) {
        uint8_t il = 0, fl = 0; InM *m = &in[i];
        if (rd(&p, end, &il, 1) || il == 0 || il >= sizeof m->id || rd(&p, end, m->id, il) || rd(&p, end, &fl, 1) || rd(&p, end, &m->nf, 4) || m->nf > MD_MAX_FILES)
            { WHY("the host's mod data block is not readable"); rc = MD_BAD; break; }
        m->id[il] = 0; m->over = fl & 1;
        const SyncMod *mine = dsl_moddata_id_ok(m->id) ? sync_of(m->id) : NULL;
        if (!mine) { WHY("%s: the host syncs files of this mod, but it does not run here with \"sync\" in its mod.json", m->id); rc = MD_BAD; break; }
        m->f = calloc(m->nf + 1, sizeof *m->f); uint64_t total = 0;
        for (uint32_t k = 0; k < m->nf && !rc; k++) {
            uint16_t pl = 0; InF *f = &m->f[k];
            if (rd(&p, end, &pl, 2) || !pl || pl > MD_MAX_PATH) { WHY("%s: the host's mod data block is not readable", m->id); rc = MD_BAD; break; }
            f->rel = (const char *)p; f->rl = pl; if (rd(&p, end, NULL, pl)) { WHY("%s: block cut short", m->id); rc = MD_BAD; break; }
            char rel[MD_MAX_PATH + 1]; memcpy(rel, f->rel, pl); rel[pl] = 0;
            if (memchr(rel, 0, pl) || !matches(pats(mine), mine->np, rel)) { WHY("%s: the host sent \"%s\", which is not a safe name or not in this mod's sync patterns", m->id, rel); rc = MD_BAD; break; }
            for (uint32_t q = 0; q < k; q++) if (m->f[q].rl == pl && !memcmp(m->f[q].rel, rel, pl)) { WHY("%s: \"%s\" twice", m->id, rel); rc = MD_BAD; }
            if (rc) break;
            f->sha = (const char *)p; if (rd(&p, end, NULL, 64) || rd(&p, end, &f->size, 4)) { WHY("%s: block cut short", m->id); rc = MD_BAD; break; }
            total += f->size;
            if (m->over) continue;
            if (total > MD_MAX_MOD_BYTES) { WHY("%s: the host sent more than %u bytes", m->id, MD_MAX_MOD_BYTES); rc = MD_TOO_BIG; break; }
            f->data = p; if (rd(&p, end, NULL, f->size)) { WHY("%s: block cut short", m->id); rc = MD_BAD; break; }
            char sh[65]; dsl_os_sha256_buf(f->data, f->size, sh);
            if (memcmp(sh, f->sha, 64)) { WHY("%s: \"%s\" arrived damaged (sha256)", m->id, rel); rc = MD_BAD; break; }
            if (host_lines.n) bputs(&host_lines, "\n"); bputs(&host_lines, m->id); bputs(&host_lines, "/"); bputs(&host_lines, rel); bputs(&host_lines, "@"); bput(&host_lines, sh, 64);
        }
        if (!rc && m->over) { WHY("%s: the host's synced files are over the limit (%llu bytes, %u files; at most %u bytes and %d files)", m->id, (unsigned long long)total, m->nf, MD_MAX_MOD_BYTES, MD_MAX_FILES); rc = MD_TOO_BIG; }
    }
    char host_fp[65] = "";
    if (!rc && (rd(&p, end, host_fp, 64) || p != end)) { WHY("the host's mod data block is not readable"); rc = MD_BAD; }
    host_fp[64] = 0;
    char lines_fp[65]; if (!rc) { fp_of(&host_lines, lines_fp); if (memcmp(lines_fp, host_fp, 64)) { WHY("the host's file list does not match its fingerprint"); rc = MD_BAD; } }
    free(host_lines.p);
    /* every sync mod of this copy is in the session (one the host did not list = the host has none of its files) */
    static Marker mk, cur;                             /* ~200 KB each: never on the game thread's stack */
    memset(&mk, 0, sizeof mk); snprintf(mk.phase, sizeof mk.phase, "stashing");
    for (int i = 0; i < nsm; i++) mk.m[mk.n++] = SM[i];
    if (!rc) {
        int active = marker_read(&cur);
        if (active && strcmp(cur.phase, "playing")) { restore("an unfinished session first", 1); active = marker_read(&cur); }
        if (!active) {                                 /* first join of this session: own files aside */
            if (marker_write(&mk)) { WHY("cannot write the session marker in %s", dsl_moddata_root()); rc = MD_WRITE; }
            else {
                char s[1200]; stash_dir(s, sizeof s, NULL);
                if (move_all(&mk, s, 0)) { WHY("cannot put this copy's own files aside"); rc = MD_WRITE; }
                snprintf(mk.phase, sizeof mk.phase, "playing"); if (!rc && marker_write(&mk)) { WHY("cannot write the session marker"); rc = MD_WRITE; }
                if (rc) { snprintf(mk.phase, sizeof mk.phase, "stashing"); marker_write(&mk); restore("join refused", 1); }
            }
        } else {                                       /* again in the same session (another join point): last files out */
            char t[1200]; stash_dir(t, sizeof t, ".trash"); mkdir(t, 0755);
            if (move_all(&cur, t, 0)) { WHY("cannot replace the files of the last join"); rc = MD_WRITE; }
            trash_clear();
        }
    }
    for (uint32_t i = 0; i < nm && !rc; i++) {           /* the host's files */
        char base[1200]; snprintf(base, sizeof base, "%s/%s", dsl_moddata_root(), in[i].id);
        if (mkdirs(dsl_moddata_root(), in[i].id)) { WHY("%s: cannot make its data folder", in[i].id); rc = MD_WRITE; break; }
        for (uint32_t k = 0; k < in[i].nf && !rc; k++) {
            char rel[MD_MAX_PATH + 1], dir[MD_MAX_PATH + 1], fp[1500]; memcpy(rel, in[i].f[k].rel, in[i].f[k].rl); rel[in[i].f[k].rl] = 0;
            snprintf(dir, sizeof dir, "%s", rel); char *sl = strrchr(dir, '/'); if (sl) *sl = 0; else dir[0] = 0;
            snprintf(fp, sizeof fp, "%s/%s", base, rel);
            if (mkdirs(base, dir) || is_link(fp) || put_file(fp, in[i].f[k].data, in[i].f[k].size)) { WHY("%s: cannot write \"%s\"", in[i].id, rel); rc = MD_WRITE; }
        }
    }
    if (!rc) {                                         /* what is there now = what the host has? */
        Buf now = {0};
        for (int i = 0; i < nsm; i++) {
            FileList L = list_files(SM[i].id, pats(&SM[i]), SM[i].np); char (*sh)[65] = calloc((size_t)L.n + 1, 65);
            char base[1200]; snprintf(base, sizeof base, "%s/%s", dsl_moddata_root(), SM[i].id);
            for (int k = 0; k < L.n; k++) sha_file(base, L.f[k].rel, sh[k]);
            sha_lines(&now, SM[i].id, &L, sh); free(sh); free(L.f);
        }
        char mine[65]; fp_of(&now, mine); free(now.p);
        if (memcmp(mine, host_fp, 64)) { WHY("the mod data files here differ from the host's after the copy (data fingerprint %.12s, host %.12s)", mine, host_fp); rc = MD_DIFFERS; }
    }
    if (rc) restore("join refused", 1);                /* also a refused re-join inside a running session: own files back */
    for (uint32_t i = 0; i < nm; i++) free(in[i].f);
    if (!rc) {
        int nf = 0; for (uint32_t i = 0; i < nm; i++) nf += (int)in[i].nf;
        dsl_log("moddata: guest session: %d file(s) of %u mod(s) from the host in place (data fingerprint %.12s)", nf, nm, host_fp);
        guest_now = 1; write_info();
    } else dsl_log("moddata: join refused (%d): %s", rc, why);
    free(in);
    return rc;
}
#undef WHY
/* the guest is back on the title (or the session ended): own files back. No session = nothing. */
MD_EXPORT void dsl_moddata_session_end(void) { restore("session ended", 1); }

/* ------------------------------------------------------------------ SDK + start */
static const char *h_mod_data_dir(DSLPlugin *self) {
    static struct { char id[64], nat[1300]; } C[MD_MAX_MODS]; static int nc;
    const char *id = dsl_plugin_name(self);
    if (!self || !dsl_moddata_id_ok(id)) return NULL;
    for (int i = 0; i < nc; i++) if (!strcmp(C[i].id, id)) return C[i].nat;
    if (mkdirs(dsl_moddata_root(), id)) return NULL;
    if (nc >= MD_MAX_MODS) return NULL;
    char p[1300]; snprintf(p, sizeof p, "%s/%s", dsl_moddata_root(), id);
    snprintf(C[nc].id, sizeof C[nc].id, "%s", id); dsl_os_native_path(p, C[nc].nat, sizeof C[nc].nat);
    return C[nc++].nat;
}
void dsl_moddata_host(DSLHost *h) { h->mod_data_dir = h_mod_data_dir; }
/* dsl_core_start, before any module or runtime: a session the last game did not end is undone, env DSML_MOD_DATA */
void dsl_moddata_start(void) {
    char nat[1100]; const char *r = dsl_moddata_root();
    restore("the last game ended inside a multiplayer session", 0);
    setenv("DSML_MOD_DATA", dsl_os_native_path(r, nat, sizeof nat), 1);
}
static char fp_start[65];
const char *dsl_moddata_fp_start(void) { return fp_start; }
/* after the modules' init (mods known): a folder for every running mod + moddata.json */
void dsl_moddata_after_init(void) {
    dsl_moddata_rescan();
    for (int i = 0; i < dsl_mods_count(); i++) { const DSLModInfo *x = dsl_mods_at(i); if (x && x->active == 1 && dsl_moddata_id_ok(x->id)) mkdirs(dsl_moddata_root(), x->id); }
    write_info();
    dsl_moddata_fingerprint(fp_start);
    for (int i = 0; i < nsm; i++) dsl_log("moddata: %s syncs %d pattern(s) in multiplayer (first: %s)", SM[i].id, SM[i].np, SM[i].pat[0]);
}
