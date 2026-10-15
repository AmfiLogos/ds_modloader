/* In-game wrong-modset message (launcher lane, 2026-10-15).
 * The host and the guest each have a list of GAMEPLAY mod lines, the same lines the launcher hashes
 * (core/src/join.rs modset_lines, loader dsload_modset.json): `<id>@<version>[:<file sha256s>[:<patch hash>]]`,
 * one per line ('\n'), any order. Client mods are never in the list (never required from others).
 * Pure C, no game code, header only (static functions: the Mac build and the Windows port both just include it;
 * no build-script change). The multiplayer mod calls it after a DSNET_REJ_MODSET refusal. */
#ifndef DS_MODCHECK_H
#define DS_MODCHECK_H
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum { DSMC_MAX_MODS = 64 };

typedef struct {
    int missing, extra, version, files;   /* counts per kind */
} dsmc_counts;

typedef struct { char id[64], ver[32], rest[200]; } dsmc_mod;

static int dsmc_parse(const char *s, dsmc_mod *m, int max) {
    int k = 0;
    while (s && *s && k < max) {
        const char *e = strchr(s, '\n'); size_t l = e ? (size_t)(e - s) : strlen(s);
        char line[300]; if (l >= sizeof line) l = sizeof line - 1;
        memcpy(line, s, l); line[l] = 0;
        while (l && (line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
        s = e ? e + 1 : NULL;
        if (!*line) continue;
        char *at = strchr(line, '@'), *co = at ? strchr(at, ':') : strchr(line, ':');
        if (co) { snprintf(m[k].rest, sizeof m[k].rest, "%s", co + 1); *co = 0; } else m[k].rest[0] = 0;
        if (at) { snprintf(m[k].ver, sizeof m[k].ver, "%s", at + 1); *at = 0; } else m[k].ver[0] = 0;
        snprintf(m[k].id, sizeof m[k].id, "%s", line);
        k++;
    }
    return k;
}

static const dsmc_mod *dsmc_find(const dsmc_mod *m, int n, const char *id) {
    for (int i = 0; i < n; i++) if (!strcmp(m[i].id, id)) return &m[i];
    return NULL;
}

/* display name of an id from the "id\tName\n" table, else the id */
static void dsmc_name_of(const char *names, const char *id, char *out, size_t n) {
    size_t il = strlen(id);
    for (const char *p = names; p && *p;) {
        const char *e = strchr(p, '\n'), *t = strchr(p, '\t');
        if (t && (!e || t < e) && (size_t)(t - p) == il && !strncmp(p, id, il)) {
            size_t l = e ? (size_t)(e - t - 1) : strlen(t + 1); if (l >= n) l = n - 1;
            memcpy(out, t + 1, l); out[l] = 0; return;
        }
        p = e ? e + 1 : NULL;
    }
    snprintf(out, n, "%s", id);
}

typedef struct { char *p; size_t n, len; } dsmc_buf;
static void dsmc_put(dsmc_buf *b, const char *fmt, ...) {
    if (b->len >= b->n) return;
    va_list a; va_start(a, fmt);
    int w = vsnprintf(b->p + b->len, b->n - b->len, fmt, a);
    va_end(a);
    if (w > 0) b->len += (size_t)w;
    if (b->len >= b->n) b->len = b->n - 1;
}

/* one kind of difference as "A 1.0, B 2.0" (sep between entries); returns how many */
enum { DSMC_K_MISSING, DSMC_K_EXTRA, DSMC_K_VERSION, DSMC_K_FILES };
static int dsmc_list(dsmc_buf *b, int kind, const dsmc_mod *h, int nh, const dsmc_mod *g, int ng, const char *names, int with_versions_pair) {
    int c = 0; char nm[96];
    const dsmc_mod *src = kind == DSMC_K_EXTRA ? g : h; int ns = kind == DSMC_K_EXTRA ? ng : nh;
    for (int i = 0; i < ns; i++) {
        const dsmc_mod *o = dsmc_find(kind == DSMC_K_EXTRA ? h : g, kind == DSMC_K_EXTRA ? nh : ng, src[i].id);
        int hit = kind == DSMC_K_MISSING ? !o : kind == DSMC_K_EXTRA ? !o
                : kind == DSMC_K_VERSION ? (o && strcmp(o->ver, src[i].ver))
                : (o && !strcmp(o->ver, src[i].ver) && strcmp(o->rest, src[i].rest));
        if (!hit) continue;
        dsmc_name_of(names, src[i].id, nm, sizeof nm);
        if (c++) dsmc_put(b, ", ");
        if (kind == DSMC_K_VERSION && with_versions_pair) dsmc_put(b, "%s (host %s, you %s)", nm, src[i].ver, o->ver);
        else if (kind == DSMC_K_VERSION) dsmc_put(b, "%s (host %s, player %s)", nm, src[i].ver, o->ver);
        else dsmc_put(b, "%s%s%s", nm, *src[i].ver ? " " : "", src[i].ver);
    }
    return c;
}

/* Builds the player text for the GUEST (the one who cannot join) into out (always 0-terminated).
 * vanilla = 1 when the host's rule is "Vanilla check" (no gameplay mod allowed; host_lines is then empty).
 * names (optional, may be NULL): "id\tDisplay name\n..." to show names instead of ids.
 * Returns the number of differences (0 = same set: out is ""). */
__attribute__((unused)) static int dsmc_guest_text(const char *host_lines, const char *guest_lines, int vanilla, const char *names,
                    char *out, size_t n, dsmc_counts *counts) {
    static dsmc_mod h[DSMC_MAX_MODS], g[DSMC_MAX_MODS];
    int nh = vanilla ? 0 : dsmc_parse(host_lines, h, DSMC_MAX_MODS), ng = dsmc_parse(guest_lines, g, DSMC_MAX_MODS);
    dsmc_buf b = {out, n, 0}; if (n) out[0] = 0;
    char scratch[4]; dsmc_buf z = {scratch, sizeof scratch, 0};
    dsmc_counts c = {
        dsmc_list(&z, DSMC_K_MISSING, h, nh, g, ng, names, 1), dsmc_list(&z, DSMC_K_EXTRA, h, nh, g, ng, names, 1),
        dsmc_list(&z, DSMC_K_VERSION, h, nh, g, ng, names, 1), dsmc_list(&z, DSMC_K_FILES, h, nh, g, ng, names, 1) };
    if (counts) *counts = c;
    int total = c.missing + c.extra + c.version + c.files;
    if (!total || !n) return total;
    if (vanilla) {
        dsmc_put(&b, "This host uses the Vanilla check: no gameplay mods. Turn off: ");
        dsmc_list(&b, DSMC_K_EXTRA, h, nh, g, ng, names, 1);
        dsmc_put(&b, ". Client mods (comfort mods) are fine.");
    } else {
        dsmc_put(&b, "Your gameplay mods are not the same as the host's.");
        if (c.missing) { dsmc_put(&b, "\nMissing (the host uses them): "); dsmc_list(&b, DSMC_K_MISSING, h, nh, g, ng, names, 1); }
        if (c.extra) { dsmc_put(&b, "\nExtra (the host does not use them): "); dsmc_list(&b, DSMC_K_EXTRA, h, nh, g, ng, names, 1); }
        if (c.version) { dsmc_put(&b, "\nOther version: "); dsmc_list(&b, DSMC_K_VERSION, h, nh, g, ng, names, 1); }
        if (c.files) { dsmc_put(&b, "\nSame version, different files (reinstall): "); dsmc_list(&b, DSMC_K_FILES, h, nh, g, ng, names, 1); }
        dsmc_put(&b, "\nJoin from the launcher: it gets the host's mods for this game.");
    }
    return total;
}

/* The same differences seen from the host (one short line for the host's chat / log), e.g.
 * "The owner cannot join: missing Hell difficulty 1.0.0; extra Cheaper recruits 1.0.0". */
__attribute__((unused)) static int dsmc_host_text(const char *player, const char *host_lines, const char *guest_lines, int vanilla, const char *names,
                   char *out, size_t n) {
    static dsmc_mod h[DSMC_MAX_MODS], g[DSMC_MAX_MODS];
    int nh = vanilla ? 0 : dsmc_parse(host_lines, h, DSMC_MAX_MODS), ng = dsmc_parse(guest_lines, g, DSMC_MAX_MODS);
    dsmc_buf b = {out, n, 0}; if (n) out[0] = 0;
    char scratch[4]; dsmc_buf z = {scratch, sizeof scratch, 0};
    int k[4], total = 0;
    for (int i = 0; i < 4; i++) total += k[i] = dsmc_list(&z, i, h, nh, g, ng, names, 0);
    if (!total || !n) return total;
    dsmc_put(&b, "%s cannot join: ", player && *player ? player : "A player");
    static const char *lab[4] = {"missing ", "extra ", "other version ", "different files "};
    int first = 1;
    for (int i = 0; i < 4; i++) {
        if (!k[i]) continue;
        if (!first) dsmc_put(&b, "; ");
        first = 0; dsmc_put(&b, "%s", vanilla && i == DSMC_K_EXTRA ? "gameplay mods on in a vanilla game: " : lab[i]);
        dsmc_list(&b, i, h, nh, g, ng, names, 0);
    }
    return total;
}
#endif
