// Mod settings (SDK API 5, owner 2026-10-18: "add a way to adjust mod settings in the modloader").
//
// A mod declares its settings in mod.json "settings" (schema: sdk/dsl_sdk.h, above settings_get_bool). Each setting is
// "gameplay" (the simulation uses it: every multiplayer player needs the same value) or "local" (display only).
// Files (all in <data>/Mods, next to active.json):
//   settings.json  {"<mod id>": {"<setting id>": true | 3 | "choice-id"}}: the player's values. Only values the player
//                  set are kept; members of mods that are not installed now stay as they are.
//   the profile    pending.json (and active.json after the commit) carry "settings": the gameplay values the new game
//                  of an Apply starts with (same object shape). A prewarmed game is compared by its profile text, so a
//                  settings change gives another prewarm.
// Values of this process: gameplay = the profile's "settings", else settings.json, else the default; frozen for the
// whole process (the multiplayer mod-set lines carry them: "<mod>/<setting>@<value>"). local = settings.json, live:
// a change on the Settings page is saved at once and the module's settings_on_change callbacks run.
// A gameplay change on the Settings page is a draft (like a mod switch): Apply writes it into pending.json, the new game
// (B) runs with it and writes it into settings.json once it is on screen (dsl_settings_commit). A failed Apply changes
// nothing. Pure C, no game calls (host-tested: test/settings_test.c).
#include "dsl_internal.h"
#include "dsl_menu.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SM_MODS 64
#define SM_SET 64
#define SM_CH 32
typedef struct {
    char id[48]; int type, gameplay;                     /* DSL_SET_TOGGLE / INT / CHOICE */
    int dflt, min, max, step;                            /* choice: dflt = index */
    int nch; char ch_id[SM_CH][48]; char *ch_label[SM_CH][DSI_LANGS];
    char *label[DSI_LANGS], *help[DSI_LANGS];
    int run;                                             /* the value this process runs with */
    int saved, has_saved;                                /* settings.json */
    int edit;                                            /* the Settings page (gameplay: applied by Apply) */
    DSLSettingInfo info;                                 /* texts in the current language (dsl_settings_at) */
} Set;
typedef struct { char id[64]; int n; Set *s; } Mod;
static Mod M[SM_MODS]; static int nm, inited;
static struct { char mod[64]; void (*cb)(void *, const char *); void *ud; } CB[64]; static int ncb;

/* ------------------------------------------------------------------ files */
static char *slurp(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    size_t cap = 1 << 20; char *b = malloc(cap + 1); size_t n = fread(b, 1, cap, f); fclose(f); b[n] = 0; return b;
}
static int put_file(const char *path, const char *text) {     /* temp + checks + rename: a failed write keeps the old file */
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp-%d", path, (int)getpid());
    FILE *f = fopen(tmp, "w"); if (!f) return -1;
    int bad = fputs(text, f) < 0; bad |= fflush(f) != 0; bad |= fclose(f) != 0;
    if (bad) { remove(tmp); return -1; }
    return rename(tmp, path);
}
static void settings_path(char *out, size_t n) {
    const char *t = getenv("DSL_SETTINGS_FILE");                 /* tests */
    if (t && *t) snprintf(out, n, "%s", t); else snprintf(out, n, "%s/settings.json", dsl_reg_data_dir());
}
static int id_ok(const char *s) {
    if (!s || !*s || strlen(s) >= 48) return 0;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '.' || *s == '-')) return 0;
    return 1;
}
static char *dup_s(const char *s) { return s && *s ? strdup(s) : NULL; }

/* ------------------------------------------------------------------ mod.json "settings" */
/* "label": "text" or {"en": "...", "fr": "..."}; per language also "i18n": {"<code>": {"label", "help"}} (as name /
   description of mod.json). [0] = English (the fallback). */
static void texts(const JNode *s, const char *key, char *out[DSI_LANGS]) {
    JNode *v = json_get(s, key);
    if (v && v->t == JN_STR) out[0] = dup_s(v->str);
    else if (v && v->t == JN_OBJ) for (int l = 0; l < DSI_LANGS; l++) { const char *t = json_str(v, dsi_code_of(l)); if (t) out[l] = dup_s(t); }
    JNode *tr = json_get(s, "i18n");
    for (int l = 1; l < DSI_LANGS; l++) { const char *t = json_str(json_get(tr, dsi_code_of(l)), key); if (t && *t) { free(out[l]); out[l] = dup_s(t); } }
}
static int num(const JNode *o, const char *k, int d) { JNode *v = json_get(o, k); return v && v->t == JN_NUM ? (int)v->num : d; }
static int fit_int(const Set *s, int v) {                      /* in range and on a step */
    if (v < s->min) v = s->min;
    if (v > s->max) v = s->max;
    return s->min + (v - s->min) / s->step * s->step;
}
static int parse_set(const JNode *j, Set *s, const char *mod) {
    memset(s, 0, sizeof *s);
    const char *id = json_str(j, "id"), *ty = json_str(j, "type"), *sc = json_str(j, "scope");
    if (!id_ok(id)) { dsl_log("settings: %s: a setting without a valid id (a-z 0-9 _ . -) is skipped", mod); return -1; }
    snprintf(s->id, sizeof s->id, "%s", id);
    s->gameplay = !(sc && !strcmp(sc, "local"));
    JNode *d = json_get(j, "default");
    if (ty && (!strcmp(ty, "toggle") || !strcmp(ty, "bool"))) { s->type = DSL_SET_TOGGLE; s->min = 0; s->max = 1; s->step = 1; s->dflt = d && d->t == JN_BOOL && d->b; }
    else if (ty && (!strcmp(ty, "int") || !strcmp(ty, "integer"))) {
        s->type = DSL_SET_INT; s->min = num(j, "min", 0); s->max = num(j, "max", 100); s->step = num(j, "step", 1);
        if (s->step < 1) s->step = 1;
        if (s->max < s->min) { dsl_log("settings: %s/%s: max < min, skipped", mod, id); return -1; }
        s->dflt = fit_int(s, d && d->t == JN_NUM ? (int)d->num : s->min);
    } else if (ty && !strcmp(ty, "choice")) {
        s->type = DSL_SET_CHOICE;
        JNode *cs = json_get(j, "choices");
        if (cs && cs->t == JN_ARR) for (JNode *c = cs->child; c && s->nch < SM_CH; c = c->next) {
            const char *cid = c->t == JN_STR ? c->str : json_str(c, "id");
            if (!id_ok(cid)) { dsl_log("settings: %s/%s: a choice without a valid id is skipped", mod, id); continue; }
            snprintf(s->ch_id[s->nch], 48, "%s", cid);
            if (c->t == JN_OBJ) texts(c, "label", s->ch_label[s->nch]);
            if (!s->ch_label[s->nch][0]) s->ch_label[s->nch][0] = strdup(cid);
            s->nch++;
        }
        if (!s->nch) { dsl_log("settings: %s/%s: no choices, skipped", mod, id); return -1; }
        s->min = 0; s->max = s->nch - 1; s->step = 1;
        for (int k = 0; k < s->nch; k++) if (d && d->t == JN_STR && !strcmp(d->str, s->ch_id[k])) s->dflt = k;
    } else { dsl_log("settings: %s/%s: unknown type \"%s\", skipped", mod, id, ty ? ty : ""); return -1; }
    texts(j, "label", s->label); texts(j, "help", s->help);
    if (!s->label[0]) s->label[0] = strdup(id);
    return 0;
}
/* a JSON value as this setting's value; -1 = not valid for it */
static int value_of(const Set *s, const JNode *v, int *out) {
    if (!v) return -1;
    if (s->type == DSL_SET_TOGGLE) { if (v->t != JN_BOOL) return -1; *out = v->b != 0; return 0; }
    if (s->type == DSL_SET_INT) { if (v->t != JN_NUM) return -1; *out = fit_int(s, (int)v->num); return 0; }
    if (v->t != JN_STR) return -1;
    for (int k = 0; k < s->nch; k++) if (!strcmp(v->str, s->ch_id[k])) { *out = k; return 0; }
    return -1;
}
/* the value as text: settings.json / profile JSON (json = 1) or the multiplayer line ("on", "3", "hard") */
static void value_text(const Set *s, int v, int json, char *out, size_t n) {
    if (s->type == DSL_SET_TOGGLE) snprintf(out, n, "%s", json ? (v ? "true" : "false") : (v ? "on" : "off"));
    else if (s->type == DSL_SET_INT) snprintf(out, n, "%d", v);
    else snprintf(out, n, json ? "\"%s\"" : "%s", s->ch_id[v >= 0 && v < s->nch ? v : 0]);
}

static Mod *mod_of(const char *id) { for (int i = 0; i < nm; i++) if (!strcmp(M[i].id, id)) return &M[i]; return NULL; }
static Set *set_of(Mod *m, const char *id) { if (m && id) for (int i = 0; i < m->n; i++) if (!strcmp(m->s[i].id, id)) return &m->s[i]; return NULL; }

static void load_mod(const char *dir, const char *id) {
    char p[1400]; snprintf(p, sizeof p, "%s/%s/mod.json", dir, id);
    char *t = slurp(p); JNode *j = t ? json_parse(t) : NULL; free(t);
    JNode *ss = json_get(j, "settings");
    const char *jid = json_str(j, "id");
    if (ss && ss->t == JN_ARR && jid && !strcmp(jid, id) && nm < SM_MODS) {
        Mod *m = &M[nm]; memset(m, 0, sizeof *m); snprintf(m->id, sizeof m->id, "%s", id);
        m->s = calloc(SM_SET, sizeof *m->s);
        for (JNode *c = ss->child; c && m->n < SM_SET; c = c->next) {
            if (c->t != JN_OBJ || parse_set(c, &m->s[m->n], id)) continue;
            if (set_of(m, m->s[m->n].id)) { dsl_log("settings: %s/%s declared twice: the first one counts", id, m->s[m->n].id); continue; }
            m->n++;
        }
        if (m->n) { nm++; dsl_log("settings: %s declares %d setting(s)", id, m->n); } else free(m->s);
    }
    json_free(j);
}
static void read_values(void) {
    char p[1200]; settings_path(p, sizeof p);
    char *t = slurp(p); JNode *sv = t ? json_parse(t) : NULL; free(t);
    char *pt = slurp(dsl_profile_file()); JNode *pr = pt ? json_parse(pt) : NULL; free(pt);
    JNode *ps = json_get(pr, "settings");
    for (int i = 0; i < nm; i++) for (int k = 0; k < M[i].n; k++) {
        Set *s = &M[i].s[k]; int v;
        s->has_saved = !value_of(s, json_get(json_get(sv, M[i].id), s->id), &v);
        s->saved = s->has_saved ? v : s->dflt;
        s->run = s->saved;
        if (s->gameplay && ps && !value_of(s, json_get(json_get(ps, M[i].id), s->id), &v)) s->run = v;   /* the Apply's values */
        s->edit = s->run;
    }
    if (ps) dsl_log("settings: gameplay values from the profile %s", dsl_profile_file());
    json_free(sv); json_free(pr);
}
void dsl_settings_init(void) {
    if (inited) return; inited = 1;
    const char *md = dsl_mods_dir(); DIR *d = md && *md ? opendir(md) : NULL;
    if (d) {
        struct dirent *e; char names[SM_MODS * 2][256]; int nn = 0;
        while ((e = readdir(d)) && nn < SM_MODS * 2) if (e->d_name[0] != '.') snprintf(names[nn++], 256, "%s", e->d_name);
        closedir(d);
        qsort(names, (size_t)nn, 256, (int (*)(const void *, const void *))strcmp);
        for (int k = 0; k < nn; k++) load_mod(md, names[k]);
    }
    read_values();
}
/* a mod package installed while the game runs (ui/dsl_pkg.c Refresh): every mod.json again; the values of this process
   (running and drafts) stay for the settings that still exist */
void dsl_settings_rescan(void) {
    static struct { char mod[64], id[48]; int run, edit; } keep[SM_MODS * 4]; int nk = 0;
    for (int i = 0; i < nm; i++) {
        for (int k = 0; k < M[i].n; k++) {
            Set *s = &M[i].s[k];
            if (nk < (int)(sizeof keep / sizeof *keep)) { snprintf(keep[nk].mod, 64, "%s", M[i].id); snprintf(keep[nk].id, 48, "%s", s->id); keep[nk].run = s->run; keep[nk].edit = s->edit; nk++; }
            for (int l = 0; l < DSI_LANGS; l++) { free(s->label[l]); free(s->help[l]); for (int c = 0; c < s->nch; c++) free(s->ch_label[c][l]); }
        }
        free(M[i].s);
    }
    nm = 0; inited = 0;
    dsl_settings_init();
    for (int j = 0; j < nk; j++) {
        Set *s = set_of(mod_of(keep[j].mod), keep[j].id);
        if (s && keep[j].run >= s->min && keep[j].run <= s->max && keep[j].edit >= s->min && keep[j].edit <= s->max) { s->run = keep[j].run; s->edit = keep[j].edit; }
    }
}

/* ------------------------------------------------------------------ settings.json */
static int save_file(void) {
    char p[1200]; settings_path(p, sizeof p);
    char *t = slurp(p); JNode *old = t ? json_parse(t) : NULL; free(t);
    size_t cap = 1 << 16, n = 0; char *b = malloc(cap); int first = 1;
#define PUT(...) do { if (cap - n < 2048) { cap *= 2; b = realloc(b, cap); } int k_ = snprintf(b + n, cap - n, __VA_ARGS__); if (k_ > 0) n += (size_t)k_; } while (0)
    PUT("{");
    if (old && old->t == JN_OBJ) for (JNode *c = old->child; c; c = c->next) {     /* mods not installed now: kept */
        if (!c->key || mod_of(c->key) || c->t != JN_OBJ || !id_ok(c->key)) continue;
        PUT("%s\n \"%s\":{", first ? "" : ",", c->key); first = 0;
        int f2 = 1;
        for (JNode *v = c->child; v; v = v->next) {
            if (!id_ok(v->key)) continue;
            if (v->t == JN_BOOL) PUT("%s\"%s\":%s", f2 ? "" : ",", v->key, v->b ? "true" : "false");
            else if (v->t == JN_NUM) PUT("%s\"%s\":%d", f2 ? "" : ",", v->key, (int)v->num);
            else if (v->t == JN_STR && id_ok(v->str)) PUT("%s\"%s\":\"%s\"", f2 ? "" : ",", v->key, v->str);
            else continue;
            f2 = 0;
        }
        PUT("}");
    }
    for (int i = 0; i < nm; i++) {
        int any = 0; for (int k = 0; k < M[i].n; k++) any |= M[i].s[k].has_saved;
        if (!any) continue;
        PUT("%s\n \"%s\":{", first ? "" : ",", M[i].id); first = 0;
        int f2 = 1;
        for (int k = 0; k < M[i].n; k++) {
            Set *s = &M[i].s[k]; if (!s->has_saved) continue;
            char v[64]; value_text(s, s->saved, 1, v, sizeof v);
            PUT("%s\"%s\":%s", f2 ? "" : ",", s->id, v); f2 = 0;
        }
        PUT("}");
    }
    PUT("\n}\n");
#undef PUT
    json_free(old);
    int r = put_file(p, b); free(b);
    if (r) dsl_log("settings: cannot write %s: the old file stays", p);
    return r;
}
/* B of an Apply, once it is on screen (dsl_reg.c commit_pending): the gameplay values it runs with become the saved ones */
void dsl_settings_commit(void) {
    int ch = 0;
    for (int i = 0; i < nm; i++) for (int k = 0; k < M[i].n; k++) {
        Set *s = &M[i].s[k];
        if (!s->gameplay || (s->has_saved && s->saved == s->run) || (!s->has_saved && s->run == s->dflt)) continue;
        s->saved = s->run; s->has_saved = 1; ch++;
    }
    if (ch && !save_file()) dsl_log("settings: %d gameplay value(s) of the Apply saved", ch);
}

/* ------------------------------------------------------------------ module API (DSLHost, API 5) */
static Set *api_set(DSLPlugin *self, const char *id, int type) {
    if (!inited) dsl_settings_init();
    Set *s = set_of(mod_of(dsl_plugin_name(self)), id);
    return s && s->type == type ? s : NULL;
}
static int h_get_bool(DSLPlugin *self, const char *id, int dflt) { Set *s = api_set(self, id, DSL_SET_TOGGLE); return s ? s->run : dflt; }
static int h_get_int(DSLPlugin *self, const char *id, int dflt) { Set *s = api_set(self, id, DSL_SET_INT); return s ? s->run : dflt; }
static const char *h_get_choice(DSLPlugin *self, const char *id, const char *dflt) { Set *s = api_set(self, id, DSL_SET_CHOICE); return s ? s->ch_id[s->run] : dflt; }
static int h_on_change(DSLPlugin *self, void (*cb)(void *, const char *), void *ud) {
    if (!cb || ncb >= (int)(sizeof CB / sizeof *CB)) return -1;
    snprintf(CB[ncb].mod, sizeof CB[ncb].mod, "%s", dsl_plugin_name(self)); CB[ncb].cb = cb; CB[ncb].ud = ud; ncb++;
    return 0;
}
void dsl_settings_host(DSLHost *h) {
    h->settings_get_bool = h_get_bool; h->settings_get_int = h_get_int; h->settings_get_choice = h_get_choice;
    h->settings_on_change = h_on_change;
}

/* ------------------------------------------------------------------ multiplayer mod-set lines */
/* "<mod>/<setting>@<value>" of every gameplay setting of mod `id` (the running values), '\n' between lines.
   Returns how many. dsl_reg.c set_lines adds them to modset_lines (and so to the HELLO hash). */
int dsl_settings_lines(const char *id, char *out, size_t cap) {
    if (!inited) dsl_settings_init();
    Mod *m = mod_of(id); size_t o = 0; int c = 0; if (cap) out[0] = 0;
    for (int k = 0; m && k < m->n; k++) {
        Set *s = &m->s[k]; if (!s->gameplay) continue;
        char v[64]; value_text(s, s->run, 0, v, sizeof v);
        int w = snprintf(out + o, o < cap ? cap - o : 0, "%s%s/%s@%s", c ? "\n" : "", m->id, s->id, v);
        if (w > 0) o += (size_t)w;
        c++;
    }
    return c;
}
/* the host's lines (multiplayer "Get the host's mods"): its gameplay values become this game's draft, so the next
   Apply starts with them. Lines that are not setting lines are skipped. Returns how many values were taken. */
int dsl_settings_take_lines(const char *lines) {
    if (!inited) dsl_settings_init();
    int took = 0;
    for (const char *l = lines; l && *l; ) {
        const char *e = strchr(l, '\n'); size_t n = e ? (size_t)(e - l) : strlen(l);
        char ln[300]; if (n >= sizeof ln) n = sizeof ln - 1; memcpy(ln, l, n); ln[n] = 0;
        while (n && (ln[n - 1] == '\r' || ln[n - 1] == ' ')) ln[--n] = 0;
        l = e ? e + 1 : NULL;
        char *sl = strchr(ln, '/'), *at = sl ? strchr(sl, '@') : NULL;
        if (!sl || !at || strchr(ln, ':')) continue;
        *sl = 0; *at = 0;
        Set *s = set_of(mod_of(ln), sl + 1); if (!s || !s->gameplay) continue;
        int v = -1; const char *t = at + 1;
        if (s->type == DSL_SET_TOGGLE) v = !strcmp(t, "on") ? 1 : !strcmp(t, "off") ? 0 : -1;
        else if (s->type == DSL_SET_INT) { char *x; long q = strtol(t, &x, 10); if (*t && !*x) v = fit_int(s, (int)q); else continue; }
        else for (int k = 0; k < s->nch; k++) if (!strcmp(t, s->ch_id[k])) v = k;
        if (v < 0 && s->type != DSL_SET_INT) continue;
        s->edit = v; took++;
    }
    if (took) dsl_log("settings: %d gameplay value(s) taken from the host's lines", took);
    return took;
}

/* ------------------------------------------------------------------ the Settings page (ui/dsl_win.c) and Apply */
int dsl_settings_count(const char *mod) { if (!inited) dsl_settings_init(); Mod *m = mod_of(mod); return m ? m->n : 0; }
const DSLSettingInfo *dsl_settings_at(const char *mod, int i) {
    if (!inited) dsl_settings_init();
    Mod *m = mod_of(mod); if (!m || i < 0 || i >= m->n) return NULL;
    Set *s = &m->s[i]; int L = dsi_lang();
    s->info.id = s->id; s->info.type = s->type; s->info.gameplay = s->gameplay;
    s->info.min = s->min; s->info.max = s->max; s->info.step = s->step; s->info.nchoices = s->nch; s->info.dflt = s->dflt;
    s->info.label = s->label[L] ? s->label[L] : s->label[0];
    s->info.help = s->help[L] ? s->help[L] : s->help[0] ? s->help[0] : "";
    return &s->info;
}
const char *dsl_settings_choice_label(const char *mod, int i, int c) {
    Mod *m = mod_of(mod); if (!m || i < 0 || i >= m->n) return "";
    Set *s = &m->s[i]; if (c < 0 || c >= s->nch) return "";
    int L = dsi_lang(); return s->ch_label[c][L] ? s->ch_label[c][L] : s->ch_label[c][0];
}
int dsl_settings_value(const char *mod, int i) { Mod *m = mod_of(mod); return m && i >= 0 && i < m->n ? m->s[i].edit : 0; }
int dsl_settings_running(const char *mod, int i) { Mod *m = mod_of(mod); return m && i >= 0 && i < m->n ? m->s[i].run : 0; }
/* a value from the page: local = saved now + the module's callbacks; gameplay = draft (Apply). 0 = ok */
int dsl_settings_set(const char *mod, int i, int v) {
    Mod *m = mod_of(mod); if (!m || i < 0 || i >= m->n) return -1;
    Set *s = &m->s[i];
    v = s->type == DSL_SET_INT ? fit_int(s, v) : v < s->min ? s->min : v > s->max ? s->max : v;
    s->edit = v;
    if (s->gameplay) return 0;
    if (s->run == v && s->has_saved) return 0;
    s->run = v; s->saved = v; s->has_saved = 1;
    int r = save_file();
    dsl_log("settings: %s/%s = %d (local, live)", mod, s->id, v);
    for (int k = 0; k < ncb; k++) if (!strcmp(CB[k].mod, mod)) CB[k].cb(CB[k].ud, s->id);
    return r;
}
/* gameplay drafts that differ from the running values (Apply needed); mod = NULL: of every mod */
int dsl_settings_pending(const char *mod) {
    int c = 0;
    for (int i = 0; i < nm; i++) if (!mod || !strcmp(M[i].id, mod)) for (int k = 0; k < M[i].n; k++) c += M[i].s[k].gameplay && M[i].s[k].edit != M[i].s[k].run;
    return c;
}
void dsl_settings_undo(void) { for (int i = 0; i < nm; i++) for (int k = 0; k < M[i].n; k++) if (M[i].s[k].gameplay) M[i].s[k].edit = M[i].s[k].run; }
/* the "settings" member of an Apply's profile: every gameplay value of the drafts (members of the mods with settings;
   "" when no mod has a gameplay setting). Same text for the same values (prewarm compare). */
int dsl_settings_profile_member(char *out, size_t cap) {
    if (!inited) dsl_settings_init();
    size_t o = 0; int first = 1, any = 0; if (cap) out[0] = 0;
#define OUT(...) do { int w_ = snprintf(out + o, o < cap ? cap - o : 0, __VA_ARGS__); if (w_ > 0) o += (size_t)w_; } while (0)
    for (int i = 0; i < nm; i++) {
        int g = 0; for (int k = 0; k < M[i].n; k++) g |= M[i].s[k].gameplay;
        if (!g) continue;
        OUT("%s\"%s\":{", first ? "\"settings\":{" : ",", M[i].id); first = 0; any = 1;
        int f2 = 1;
        for (int k = 0; k < M[i].n; k++) {
            Set *s = &M[i].s[k]; if (!s->gameplay) continue;
            char v[64]; value_text(s, s->edit, 1, v, sizeof v);
            OUT("%s\"%s\":%s", f2 ? "" : ",", s->id, v); f2 = 0;
        }
        OUT("}");
    }
    if (any) OUT("}");
#undef OUT
    if (o >= cap && cap) { out[0] = 0; return -1; }
    return any;
}
/* "Gold multiplier 3, Hard mode On" (each gameplay draft of every mod) for the Mods window's status line; how many */
int dsl_settings_changes(char *out, size_t cap) {
    int c = 0; char one[300], t[1200]; if (cap) out[0] = 0;
    for (int i = 0; i < nm; i++) for (int k = 0; k < M[i].n; k++) {
        Set *s = &M[i].s[k]; if (!s->gameplay || s->edit == s->run) continue;
        char v[200]; dsl_settings_value_label(M[i].id, k, s->edit, v, sizeof v);
        snprintf(one, sizeof one, TR("%s: %s"), dsl_settings_at(M[i].id, k)->label, v);
        if (c++) { snprintf(t, sizeof t, TR("%s, %s"), out, one); snprintf(out, cap, "%s", t); } else snprintf(out, cap, "%s", one);
    }
    dsi_utf8_fix_end(out);
    return c;
}
/* a value as the player reads it: On / Off, the number, the choice's label */
void dsl_settings_value_label(const char *mod, int i, int v, char *out, size_t cap) {
    Mod *m = mod_of(mod); if (!m || i < 0 || i >= m->n) { snprintf(out, cap, "?"); return; }
    Set *s = &m->s[i];
    if (s->type == DSL_SET_TOGGLE) snprintf(out, cap, "%s", v ? TR("On") : TR("Off"));
    else if (s->type == DSL_SET_INT) snprintf(out, cap, "%d", v);
    else snprintf(out, cap, "%s", dsl_settings_choice_label(mod, i, v));
}
/* for the multiplayer mod's join refusal text (dlsym, no SDK change): one setting line's words in the current language.
   mod / setting / value as in "<mod>/<setting>@<value>"; label = the setting's label, vtext = the value as the player
   reads it (On / Off, the number, the choice's label). 0 = known setting, -1 = not installed here (out = the raw ids). */
__attribute__((visibility("default"))) int dsl_setting_text(const char *mod, const char *setting, const char *value,
                                                            char *label, size_t lcap, char *vtext, size_t vcap) {
    if (!inited) dsl_settings_init();
    Mod *m = mod_of(mod ? mod : ""); Set *s = set_of(m, setting);
    if (!s) { snprintf(label, lcap, "%s", setting ? setting : ""); snprintf(vtext, vcap, "%s", value ? value : ""); return -1; }
    int i = (int)(s - m->s), v = -1;
    snprintf(label, lcap, "%s", dsl_settings_at(mod, i)->label);
    if (s->type == DSL_SET_TOGGLE && value) v = !strcmp(value, "on") ? 1 : !strcmp(value, "off") ? 0 : -1;
    else if (s->type == DSL_SET_CHOICE && value) for (int k = 0; k < s->nch; k++) if (!strcmp(value, s->ch_id[k])) v = k;
    if (s->type == DSL_SET_INT || v < 0) snprintf(vtext, vcap, "%s", value ? value : "");
    else dsl_settings_value_label(mod, i, v, vtext, vcap);
    return 0;
}
