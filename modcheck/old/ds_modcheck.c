/* See ds_modcheck.h. */
#include "ds_modcheck.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct { char id[64], ver[32], rest[200]; } mod;

static int parse(const char *s, mod *m, int max) {
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

static const mod *find(const mod *m, int n, const char *id) {
    for (int i = 0; i < n; i++) if (!strcmp(m[i].id, id)) return &m[i];
    return NULL;
}

/* display name of an id from the "id\tName\n" table, else the id */
static void name_of(const char *names, const char *id, char *out, size_t n) {
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

typedef struct { char *p; size_t n, len; } buf;
static void put(buf *b, const char *fmt, ...) {
    if (b->len >= b->n) return;
    va_list a; va_start(a, fmt);
    int w = vsnprintf(b->p + b->len, b->n - b->len, fmt, a);
    va_end(a);
    if (w > 0) b->len += (size_t)w;
    if (b->len >= b->n) b->len = b->n - 1;
}

/* one kind of difference as "A 1.0, B 2.0" (sep between entries); returns how many */
enum { K_MISSING, K_EXTRA, K_VERSION, K_FILES };
static int list(buf *b, int kind, const mod *h, int nh, const mod *g, int ng, const char *names, int with_versions_pair) {
    int c = 0; char nm[96];
    const mod *src = kind == K_EXTRA ? g : h; int ns = kind == K_EXTRA ? ng : nh;
    for (int i = 0; i < ns; i++) {
        const mod *o = find(kind == K_EXTRA ? h : g, kind == K_EXTRA ? nh : ng, src[i].id);
        int hit = kind == K_MISSING ? !o : kind == K_EXTRA ? !o
                : kind == K_VERSION ? (o && strcmp(o->ver, src[i].ver))
                : (o && !strcmp(o->ver, src[i].ver) && strcmp(o->rest, src[i].rest));
        if (!hit) continue;
        name_of(names, src[i].id, nm, sizeof nm);
        if (c++) put(b, ", ");
        if (kind == K_VERSION && with_versions_pair) put(b, "%s (host %s, you %s)", nm, src[i].ver, o->ver);
        else if (kind == K_VERSION) put(b, "%s (host %s, player %s)", nm, src[i].ver, o->ver);
        else put(b, "%s%s%s", nm, *src[i].ver ? " " : "", src[i].ver);
    }
    return c;
}

int dsmc_guest_text(const char *host_lines, const char *guest_lines, int vanilla, const char *names,
                    char *out, size_t n, dsmc_counts *counts) {
    static mod h[DSMC_MAX_MODS], g[DSMC_MAX_MODS];
    int nh = vanilla ? 0 : parse(host_lines, h, DSMC_MAX_MODS), ng = parse(guest_lines, g, DSMC_MAX_MODS);
    buf b = {out, n, 0}; if (n) out[0] = 0;
    char scratch[4]; buf z = {scratch, sizeof scratch, 0};
    dsmc_counts c = {
        list(&z, K_MISSING, h, nh, g, ng, names, 1), list(&z, K_EXTRA, h, nh, g, ng, names, 1),
        list(&z, K_VERSION, h, nh, g, ng, names, 1), list(&z, K_FILES, h, nh, g, ng, names, 1) };
    if (counts) *counts = c;
    int total = c.missing + c.extra + c.version + c.files;
    if (!total || !n) return total;
    if (vanilla) {
        put(&b, "This host uses the Vanilla check: no gameplay mods. Turn off: ");
        list(&b, K_EXTRA, h, nh, g, ng, names, 1);
        put(&b, ". Client mods (comfort mods) are fine.");
    } else {
        put(&b, "Your gameplay mods are not the same as the host's.");
        if (c.missing) { put(&b, "\nMissing (the host uses them): "); list(&b, K_MISSING, h, nh, g, ng, names, 1); }
        if (c.extra) { put(&b, "\nExtra (the host does not use them): "); list(&b, K_EXTRA, h, nh, g, ng, names, 1); }
        if (c.version) { put(&b, "\nOther version: "); list(&b, K_VERSION, h, nh, g, ng, names, 1); }
        if (c.files) { put(&b, "\nSame version, different files (reinstall): "); list(&b, K_FILES, h, nh, g, ng, names, 1); }
        put(&b, "\nJoin from the launcher: it gets the host's mods for this game.");
    }
    return total;
}

int dsmc_host_text(const char *player, const char *host_lines, const char *guest_lines, int vanilla, const char *names,
                   char *out, size_t n) {
    static mod h[DSMC_MAX_MODS], g[DSMC_MAX_MODS];
    int nh = vanilla ? 0 : parse(host_lines, h, DSMC_MAX_MODS), ng = parse(guest_lines, g, DSMC_MAX_MODS);
    buf b = {out, n, 0}; if (n) out[0] = 0;
    char scratch[4]; buf z = {scratch, sizeof scratch, 0};
    int k[4], total = 0;
    for (int i = 0; i < 4; i++) total += k[i] = list(&z, i, h, nh, g, ng, names, 0);
    if (!total || !n) return total;
    put(&b, "%s cannot join: ", player && *player ? player : "A player");
    static const char *lab[4] = {"missing ", "extra ", "other version ", "different files "};
    int first = 1;
    for (int i = 0; i < 4; i++) {
        if (!k[i]) continue;
        if (!first) put(&b, "; ");
        first = 0; put(&b, "%s", vanilla && i == K_EXTRA ? "gameplay mods on in a vanilla game: " : lab[i]);
        list(&b, i, h, nh, g, ng, names, 0);
    }
    return total;
}
