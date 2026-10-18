/* In-game wrong-modset message (launcher lane, 2026-10-15).
 * The host and the guest each have a list of GAMEPLAY mod lines, the same lines the launcher hashes
 * (core/src/join.rs modset_lines, loader dsload_modset.json): `<id>@<version>[:<file sha256s>[:<patch hash>]]`,
 * one per line ('\n'), any order. Client mods are never in the list (never required from others).
 * Mod settings (loader API 5): one more line per gameplay setting of a gameplay mod, `<mod id>/<setting id>@<value>`
 * (value: on / off, an integer or a choice id). Those lines are their own kind ("Other setting values"); a setting of a
 * mod that is missing, extra or of another version is not listed again (the mod line says it all).
 * Pure C, no game code, header only (static functions: the Mac build and the Windows port both just include it;
 * no build-script change). The multiplayer mod calls it after a DSNET_REJ_MODSET refusal. */
#ifndef DS_MODCHECK_H
#define DS_MODCHECK_H
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* i18n: the multiplayer mod defines DSMC_TR as TR (ds_i18n.h) before the include, so the guest text is in the player's
 * language; any other user of this header gets English. TR_NOOP marks the keys for the extractor. */
#ifndef DSMC_TR
#define DSMC_TR(s) (s)
#endif
#ifndef TR_NOOP
#define TR_NOOP(s) (s)
#endif

enum { DSMC_MAX_MODS = 256 };

typedef struct {
    int missing, extra, version, files;   /* counts per kind (mod lines) */
    int settings;                         /* setting lines with another value (or on one side only) */
} dsmc_counts;

typedef struct { char id[112], ver[64], rest[200]; int set; } dsmc_mod;   /* set: a setting line (id "<mod>/<setting>") */

/* optional words of a setting line in the player's language (the loader's dsl_setting_text, found with dlsym by the
 * multiplayer mod): label = the setting's label, vtext = the value as the player reads it. Not set: the raw ids. */
typedef int (*dsmc_setting_fn)(const char *mod, const char *setting, const char *value, char *label, size_t lcap,
                               char *vtext, size_t vcap);
static dsmc_setting_fn dsmc_setting_text;
__attribute__((unused)) static void dsmc_set_setting_text(dsmc_setting_fn fn) { dsmc_setting_text = fn; }

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
        m[k].set = strchr(m[k].id, '/') != NULL;
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
        if (src[i].set) continue;                                     /* setting lines: dsmc_slist */
        const dsmc_mod *o = dsmc_find(kind == DSMC_K_EXTRA ? h : g, kind == DSMC_K_EXTRA ? nh : ng, src[i].id);
        int hit = kind == DSMC_K_MISSING ? !o : kind == DSMC_K_EXTRA ? !o
                : kind == DSMC_K_VERSION ? (o && strcmp(o->ver, src[i].ver))
                : (o && !strcmp(o->ver, src[i].ver) && strcmp(o->rest, src[i].rest));
        if (!hit) continue;
        dsmc_name_of(names, src[i].id, nm, sizeof nm);
        if (c++) dsmc_put(b, "%s", DSMC_TR(TR_NOOP(", ")));
        if (kind == DSMC_K_VERSION && with_versions_pair) dsmc_put(b, DSMC_TR(TR_NOOP("%s (host %s, you %s)")), nm, src[i].ver, o->ver);
        else if (kind == DSMC_K_VERSION) dsmc_put(b, "%s (host %s, player %s)", nm, src[i].ver, o->ver);
        else dsmc_put(b, "%s%s%s", nm, *src[i].ver ? " " : "", src[i].ver);
    }
    return c;
}

/* setting lines whose value differs (or that one side lacks) while the mod itself is the same version on both sides;
 * guest = 1: "<Mod name>: <Label> (host X, you Y)" in the player's words, 0: "<mod>/<setting> (host X, player Y)" */
static int dsmc_mod_same(const dsmc_mod *h, int nh, const dsmc_mod *g, int ng, const char *mod, size_t ml) {
    char id[112]; if (ml >= sizeof id) return 0;
    memcpy(id, mod, ml); id[ml] = 0;
    const dsmc_mod *a = dsmc_find(h, nh, id), *b = dsmc_find(g, ng, id);
    return a && b && !strcmp(a->ver, b->ver);
}
static int dsmc_slist(dsmc_buf *b, const dsmc_mod *h, int nh, const dsmc_mod *g, int ng, const char *names, int guest) {
    int c = 0;
    for (int pass = 0; pass < 2; pass++) {                               /* host's setting lines, then the guest's own ones */
        const dsmc_mod *src = pass ? g : h, *oth = pass ? h : g; int ns = pass ? ng : nh, no = pass ? nh : ng;
        for (int i = 0; i < ns; i++) {
            if (!src[i].set) continue;
            const dsmc_mod *o = dsmc_find(oth, no, src[i].id);
            if (pass ? o != NULL : (o && !strcmp(o->ver, src[i].ver))) continue;   /* same value; pass 1: only guest-only lines */
            const char *sl = strchr(src[i].id, '/');
            if (!dsmc_mod_same(h, nh, g, ng, src[i].id, (size_t)(sl - src[i].id))) continue;
            const char *hv = pass ? "-" : src[i].ver, *gv = pass ? src[i].ver : o ? o->ver : "-";
            if (c++) dsmc_put(b, "%s", guest ? DSMC_TR(TR_NOOP(", ")) : ", ");
            if (!guest) { dsmc_put(b, "%s (host %s, player %s)", src[i].id, hv, gv); continue; }
            char mod[112], nm[96], lab[160], hx[160], gx[160];
            snprintf(mod, sizeof mod, "%.*s", (int)(sl - src[i].id), src[i].id);
            dsmc_name_of(names, mod, nm, sizeof nm);
            snprintf(lab, sizeof lab, "%s", sl + 1); snprintf(hx, sizeof hx, "%s", hv); snprintf(gx, sizeof gx, "%s", gv);
            if (dsmc_setting_text) {
                if (strcmp(hv, "-")) dsmc_setting_text(mod, sl + 1, hv, lab, sizeof lab, hx, sizeof hx);
                if (strcmp(gv, "-")) dsmc_setting_text(mod, sl + 1, gv, lab, sizeof lab, gx, sizeof gx);
            }
            dsmc_put(b, DSMC_TR(TR_NOOP("%s: %s (host %s, you %s)")), nm, lab, hx, gx);
        }
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
        dsmc_list(&z, DSMC_K_VERSION, h, nh, g, ng, names, 1), dsmc_list(&z, DSMC_K_FILES, h, nh, g, ng, names, 1),
        vanilla ? 0 : dsmc_slist(&z, h, nh, g, ng, names, 1) };
    if (counts) *counts = c;
    int mods = c.missing + c.extra + c.version + c.files, total = mods + c.settings;
    if (!total || !n) return total;
    if (vanilla) {
        char l[1000]; dsmc_buf lb = {l, sizeof l, 0}; l[0] = 0;               /* the list goes inside one sentence (word order) */
        dsmc_list(&lb, DSMC_K_EXTRA, h, nh, g, ng, names, 1);
        dsmc_put(&b, DSMC_TR(TR_NOOP("This host allows no gameplay mods. Turn off: %s. Client mods (comfort mods) are fine.")), l);
    } else {
        dsmc_put(&b, "%s", mods ? DSMC_TR(TR_NOOP("Your gameplay mods are not the same as the host's."))
                                : DSMC_TR(TR_NOOP("Your mod settings are not the same as the host's.")));
        if (c.missing) { dsmc_put(&b, "\n%s ", DSMC_TR(TR_NOOP("Missing (the host uses them):"))); dsmc_list(&b, DSMC_K_MISSING, h, nh, g, ng, names, 1); }
        if (c.extra) { dsmc_put(&b, "\n%s ", DSMC_TR(TR_NOOP("Extra (the host does not use them):"))); dsmc_list(&b, DSMC_K_EXTRA, h, nh, g, ng, names, 1); }
        if (c.version) { dsmc_put(&b, "\n%s ", DSMC_TR(TR_NOOP("Other version:"))); dsmc_list(&b, DSMC_K_VERSION, h, nh, g, ng, names, 1); }
        if (c.files) { dsmc_put(&b, "\n%s ", DSMC_TR(TR_NOOP("Same version, different files (reinstall):"))); dsmc_list(&b, DSMC_K_FILES, h, nh, g, ng, names, 1); }
        if (c.settings) { dsmc_put(&b, "\n%s ", DSMC_TR(TR_NOOP("Other setting values:"))); dsmc_slist(&b, h, nh, g, ng, names, 1); }
        if (mods) dsmc_put(&b, "\n%s", DSMC_TR(TR_NOOP("Join from the launcher: it gets the host's mods for this game.")));
        else dsmc_put(&b, "\n%s", DSMC_TR(TR_NOOP("Set the same values in the Mods window (Settings of that mod), then join again.")));
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
    int k[5], total = 0;
    for (int i = 0; i < 4; i++) total += k[i] = dsmc_list(&z, i, h, nh, g, ng, names, 0);
    total += k[4] = vanilla ? 0 : dsmc_slist(&z, h, nh, g, ng, names, 0);
    if (!total || !n) return total;
    dsmc_put(&b, "%s cannot join: ", player && *player ? player : "A player");
    static const char *lab[5] = {"missing ", "extra ", "other version ", "different files ", "other setting "};
    int first = 1;
    for (int i = 0; i < 5; i++) {
        if (!k[i]) continue;
        if (!first) dsmc_put(&b, "; ");
        first = 0; dsmc_put(&b, "%s", vanilla && i == DSMC_K_EXTRA ? "gameplay mods on in a vanilla game: " : lab[i]);
        if (i == 4) dsmc_slist(&b, h, nh, g, ng, names, 0); else dsmc_list(&b, i, h, nh, g, ng, names, 0);
    }
    return total;
}
#endif
