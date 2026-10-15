/* mp.json reader (flat object; strings, numbers, true/false). */
#include "mp.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mp_config_defaults(mp_config *c) {
    memset(c, 0, sizeof *c);
    c->allow_speedup = 1; c->allow_pause = 1;
    c->pause_mode = DSNET_PAUSE_ANYONE; c->gear_mode = 0;
    c->min_players = 1;
    c->auto_resync = 1; c->resync_delay_ms = 300; c->auto_resume = 1; c->partial_resync = 1; c->full_check_steps = 10800;
    c->connect_timeout_ms = 5000;
    c->allow_spectators = 1;
    snprintf(c->name, sizeof c->name, "player");
}

/* value text of "key" (skips whitespace after the colon); NULL if absent */
static const char *find(const char *j, const char *key) {
    size_t kl = strlen(key);
    for (const char *p = j; (p = strchr(p, '"')); p++) {
        const char *e = p + 1;                           /* end of this string */
        while (*e && *e != '"') e += *e == '\\' && e[1] ? 2 : 1;
        if (!*e) break;
        if ((size_t)(e - p - 1) == kl && !strncmp(p + 1, key, kl)) {
            const char *q = e + 1;
            while (isspace((unsigned char)*q)) q++;
            if (*q == ':') {                             /* a key, not a value */
                q++;
                while (isspace((unsigned char)*q)) q++;
                return q;
            }
        }
        p = e;
    }
    return NULL;
}
static int get_str(const char *j, const char *key, char *out, size_t n) {
    const char *v = find(j, key);
    if (!v || *v != '"') return 0;
    size_t o = 0;
    for (v++; *v && *v != '"' && o + 1 < n; v++) {
        if (*v == '\\' && v[1]) v++;
        out[o++] = *v;
    }
    out[o] = 0;
    return 1;
}
static int get_num(const char *j, const char *key, double *out) {
    const char *v = find(j, key);
    if (!v) return 0;
    if (!strncmp(v, "true", 4)) { *out = 1; return 1; }
    if (!strncmp(v, "false", 5)) { *out = 0; return 1; }
    char *e; double d = strtod(v, &e);
    if (e == v) return 0;
    *out = d; return 1;
}

int mp_config_parse(const char *j, mp_config *c) {
    mp_config_defaults(c);
    char role[32] = "";
    if (!get_str(j, "role", role, sizeof role)) return -2;
    if (!strcmp(role, "host")) c->host = 1;
    else if (!strcmp(role, "client")) c->host = 0;
    else return -2;
    get_str(j, "host", c->host_addr, sizeof c->host_addr);
    get_str(j, "name", c->name, sizeof c->name);
    double d;
    if (get_num(j, "port", &d)) c->port = (uint16_t)d;
    if (get_num(j, "allow_speedup", &d)) c->allow_speedup = d != 0;
    if (get_num(j, "allow_pause", &d)) c->allow_pause = d != 0;
    if (get_num(j, "pause_mode", &d)) c->pause_mode = (int)d;
    if (get_num(j, "gear_mode", &d)) c->gear_mode = (int)d;
    if (get_num(j, "min_players", &d)) c->min_players = (int)d;
    if (get_num(j, "tick_ms", &d)) c->tick_ms = (float)d;
    if (get_num(j, "fp_interval", &d)) c->fp_interval = (int)d;
    if (get_num(j, "auto_resync", &d)) c->auto_resync = d != 0;
    if (get_num(j, "resync_delay_ms", &d)) c->resync_delay_ms = (int)d;
    if (get_num(j, "partial_resync", &d)) c->partial_resync = d != 0;
    if (get_num(j, "full_check_steps", &d)) c->full_check_steps = (int)d;
    if (get_num(j, "auto_resume", &d)) c->auto_resume = d != 0;
    if (get_num(j, "connect_timeout_ms", &d)) c->connect_timeout_ms = (int)d;
    if (get_num(j, "need_save", &d)) c->need_save = d != 0;
    if (get_num(j, "spectator", &d)) c->spectator = d != 0;
    if (get_num(j, "allow_spectators", &d)) c->allow_spectators = d != 0;
    if (get_num(j, "shared_fog", &d)) c->shared_fog = d != 0;
    if (!c->host && (!c->host_addr[0] || !c->port)) return -2;   /* a client needs host + port */
    return 0;
}

int mp_config_load(const char *path, mp_config *c) {
    FILE *f = fopen(path, "rb");
    if (!f) { mp_config_defaults(c); return -1; }
    char b[8192]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f);
    b[n] = 0;
    return mp_config_parse(b, c);
}

/* The launcher's session file (env DSML_SESSION; ~/ds_launcher core/src/flow.rs Session): the started world's gameplay
 * mod-set hash ("modset"), the host rule ("rule": "mods" | "vanilla") and "modset_lines" (`<id>@<version>:<shas>...`),
 * copied whole, '\n'-joined, into mods. No variable / no file / unreadable: rule mods, modset "" (= no gameplay mods). 0 = read. */
int mp_session_mods(int *vanilla, char modset[65], char *mods, size_t n) {
    *vanilla = 0; modset[0] = 0; if (n) mods[0] = 0;
    const char *sp = getenv("DSML_SESSION");
    FILE *f = sp && *sp ? fopen(sp, "rb") : NULL;
    if (!f) return -1;
    char *b = malloc(1 << 20); size_t got = fread(b, 1, (1 << 20) - 1, f); fclose(f); b[got] = 0;
    char rule[16] = "";
    get_str(b, "modset", modset, 65);                    /* the first "modset" is the top-level one (field order) */
    get_str(b, "rule", rule, sizeof rule);
    *vanilla = !strcmp(rule, "vanilla");
    const char *v = find(b, "modset_lines");
    size_t o = 0;
    if (v && *v == '[') for (v++; *v && *v != ']'; v++) {
        if (*v != '"') continue;
        const char *e = ++v;
        while (*e && *e != '"') e += *e == '\\' && e[1] ? 2 : 1;      /* the whole line (no escapes in hex / ids) */
        if (n && o + (size_t)(e - v) + 2 < n) o += (size_t)snprintf(mods + o, n - o, "%s%.*s", o ? "\n" : "", (int)(e - v), v);
        v = *e ? e : e - 1;
    }
    free(b);
    return 0;
}
