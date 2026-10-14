/* ord_smoke.c - in-game loopback smoke test of the orders lane (rig variant build only, build_ord/).
 * Socket commands (rig_exec in the build_ord copy forwards "ord_*" here):
 *   ord_loop   {"on":1}          capture on: every captured order is queued and applied by ord_apply at
 *                                the start of the next frame (loopback, no network). on:0 = pass-through.
 *   ord_inject {"ev":"Name", "ns":"Refactor.Main.Event", "<kind>@<dec offset>":value, ...}
 *              builds the event object and sends it through InputDispatcher.<Update>b__8_0 (the hooked site),
 *              like the input pipeline does. kinds: i32 u8 v2 ("x,y") guid nguid ni32 str ent (guid of an entity
 *              on the current map)
 *   ord_select {"guids":"g1,g2", "group":3002}   set the selection directly (no view update)
 *   ord_stats  {}                counters + selection + last apply results */
#include "../rig.h"
#include "../ord_api.h"
#include "../ord_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

void *ord_debug_find_entity(int map, const uint8_t g[16]);
void *ord_debug_lguid_new(const uint8_t (*g)[16], int n);
void *ord_debug_class(const char *ns, const char *name);
const char *ord_debug_class_name(void *obj);
int ord_debug_select(int map, const uint8_t (*g)[16], int n, int group);
int ord_debug_selection(uint8_t (*out)[16], int max, int *group);

#define SETREF(o, slot, v) F(0x6F95C8, void (*)(Obj, void **, Obj))((o), (void **)(slot), (v))

static struct { uint8_t *p; size_t n; } q[256];
static int qn, loop_on;
static int last_rc[32], nlast, applied_total, captured_total;

static void loop_emit(void *u, const uint8_t owner[16], const uint8_t *p, size_t n) {
    (void)u; (void)owner;
    captured_total++;
    if (qn == 256) { rig_log("ord_smoke: queue full"); return; }
    q[qn].p = malloc(n); memcpy(q[qn].p, p, n); q[qn].n = n; qn++;
    rig_log("ord_smoke: captured kind %d (%zu bytes)", p[2] | p[3] << 8, n);
}

void ord_smoke_frame(void) {
    if (!qn) return;
    int n = qn; qn = 0;
    for (int i = 0; i < n; i++) {
        int rc = ord_apply(0, q[i].p, q[i].n);
        rig_log("ord_smoke: applied kind %d -> rc %d", q[i].p[2] | q[i].p[3] << 8, rc);
        last_rc[nlast++ & 31] = rc; applied_total++;
        free(q[i].p);
    }
}

static char *ok_json(const char *extra) {
    Buf b = {0}; bput(&b, "{\"ok\":true%s%s}", extra ? "," : "", extra ? extra : ""); return b.p;
}
static char *err_json(const char *m) { Buf b = {0}; bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, m); bput(&b, "}"); return b.p; }

/* "kind@off":value pairs of a flat JSON object */
static char *c_inject(const char *line) {
    char ev[128], ns[128] = "Refactor.Main.Event";
    if (!jstr(line, "ev", ev, sizeof ev)) return err_json("need ev");
    jstr(line, "ns", ns, sizeof ns);
    void *k = ord_debug_class(ns, ev);
    if (!k) return err_json("class not found");
    if (!mf) return err_json("no MainFlow");
    Obj o = F(0x6F96CC, Obj (*)(void *))(k);
    int map = cur_map();
    const char *p = line;
    while ((p = strchr(p, '"'))) {
        const char *ks = p + 1, *ke = strchr(ks, '"');
        if (!ke) break;
        const char *at = memchr(ks, '@', (size_t)(ke - ks));
        p = ke + 1;
        if (!at) continue;
        char kind[16] = ""; size_t kl = (size_t)(at - ks); if (kl >= sizeof kind) continue;
        memcpy(kind, ks, kl);
        int off = atoi(at + 1);
        const char *v = strchr(ke, ':'); if (!v) break; v++;
        while (*v == ' ') v++;
        char sv[256] = "";
        if (*v == '"') { const char *e = strchr(v + 1, '"'); size_t l = e ? (size_t)(e - v - 1) : 0; if (l >= sizeof sv) l = sizeof sv - 1; memcpy(sv, v + 1, l); sv[l] = 0; p = e ? e + 1 : v + 1; }
        char *f = (char *)o + off;
        if (!strcmp(kind, "i32")) *(int32_t *)f = atoi(v);
        else if (!strcmp(kind, "u8")) *(uint8_t *)f = (uint8_t)atoi(v);
        else if (!strcmp(kind, "v2")) { int x = 0, y = 0; sscanf(sv, "%d,%d", &x, &y); ((int32_t *)f)[0] = x; ((int32_t *)f)[1] = y; }
        else if (!strcmp(kind, "f2")) { float x = 0, y = 0; sscanf(sv, "%f,%f", &x, &y); ((float *)f)[0] = x; ((float *)f)[1] = y; }
        else if (!strcmp(kind, "guid")) { Guid g; if (!guid_parse(sv, &g)) return err_json("bad guid"); memcpy(f, &g, 16); }
        else if (!strcmp(kind, "nguid")) { Guid g; if (!guid_parse(sv, &g)) return err_json("bad guid"); f[0] = 1; memcpy(f + 4, &g, 16); }
        else if (!strcmp(kind, "ni32")) { f[0] = 1; *(int32_t *)(f + 4) = atoi(v); }
        else if (!strcmp(kind, "str")) SETREF(o, f, F(0x6F9738, Obj (*)(const char *))(sv));
        else if (!strcmp(kind, "lguid")) {                 /* "g1,g2" -> List<Guid> */
            uint8_t gl[32][16]; int gn = 0; char tmp[256]; snprintf(tmp, sizeof tmp, "%s", sv);
            for (char *t = strtok(tmp, ","); t && gn < 32; t = strtok(NULL, ",")) if (guid_parse(t, (Guid *)gl[gn])) gn++;
            SETREF(o, f, ord_debug_lguid_new((const uint8_t (*)[16])gl, gn));
        }
        else if (!strcmp(kind, "ent")) {
            Guid g; if (!guid_parse(sv, &g)) return err_json("bad guid");
            Obj e = ord_debug_find_entity(map, (const uint8_t *)&g);
            if (!e) return err_json("entity not found");
            SETREF(o, f, e);
        } else return err_json("bad field kind");
    }
    Obj disp = AT(mf, 0x68, Obj);
    F(0xD3BAA4, void (*)(Obj, Obj, void *))(disp, o, NULL);     /* InputDispatcher.<Update>b__8_0 (hooked) */
    char x[160]; snprintf(x, sizeof x, "\"class\":\"%s\",\"queued_orders\":%d,\"tick\":%lld", ord_debug_class_name(o), qn, (long long)tick());
    return ok_json(x);
}

static char *c_select(const char *line) {
    char gs[1024] = ""; double grp = 0;
    jstr(line, "guids", gs, sizeof gs); jnum(line, "group", &grp);
    uint8_t g[32][16]; int n = 0;
    for (char *t = strtok(gs, ","); t && n < 32; t = strtok(NULL, ",")) if (guid_parse(t, (Guid *)g[n])) n++;
    int k = ord_debug_select(cur_map(), (const uint8_t (*)[16])g, n, (int)grp);
    char x[64]; snprintf(x, sizeof x, "\"selected\":%d", k);
    return k < 0 ? err_json("no selection container") : ok_json(x);
}

static char *c_stats(void) {
    ord_stats s; ord_get_stats(&s);
    uint8_t g[32][16]; int grp = 0; int n = ord_debug_selection(g, 32, &grp);
    Buf b = {0};
    bput(&b, "{\"ok\":true,\"loop\":%d,\"captured\":%u,\"applied\":%u,\"apply_failed\":%u,\"blocked\":%u,\"passed_local\":%u,"
             "\"unknown_types\":%u,\"queue\":%d,\"sel_group\":%d,\"sel\":[", loop_on, s.captured, s.applied, s.apply_failed,
         s.blocked, s.passed_local, s.unknown_types, qn, grp);
    for (int i = 0; i < n; i++) { char t[40]; Guid gg; memcpy(&gg, g[i], 16); guid_str(gg, t); bput(&b, "%s\"%s\"", i ? "," : "", t); }
    bput(&b, "],\"last_rc\":[");
    int m = nlast < 32 ? nlast : 32;
    for (int i = 0; i < m; i++) bput(&b, "%s%d", i ? "," : "", last_rc[(nlast - m + i) & 31]);
    bput(&b, "],\"tick\":%lld}", (long long)tick());
    return b.p;
}

/* simulate the hover UI: register an InteractionOption (what the cursor view does) with n interactions
 * {"n":1,"type0":2,"user0":g,"target0":g,"item0":g,"input0":0,"task0":0,"possible0":1} ; n 0 = empty option */
static char *c_hover(const char *line) {
    double dn = 0; jnum(line, "n", &dn); int n = (int)dn;
    Obj pc = AT(AT(mf, 0x50, Obj), 0x38, Obj); Obj ioc = AT(pc, 0x40, Obj);
    void *icls = ord_debug_class("Refactor.Control", "Interaction");
    if (!ioc || !icls) return err_json("no InteractionOptionControl / Interaction class");
    Obj list = obj_new(0x6645A50);                                   /* List<Interaction> */
    Obj arr = F(0x6F8BA8, Obj (*)(void *, size_t))(icls, (size_t)(n > 4 ? n : 4));
    int map = cur_map();
    for (int i = 0; i < n; i++) {
        char *e = (char *)arr + 0x20 + i * 0xD8, key[32], gs[64]; double v;
        snprintf(key, sizeof key, "type%d", i); if (jnum(line, key, &v)) AT(e, 0x0, int32_t) = (int32_t)v;
        snprintf(key, sizeof key, "input%d", i); if (jnum(line, key, &v)) AT(e, 0x44, int32_t) = (int32_t)v;
        snprintf(key, sizeof key, "task%d", i); if (jnum(line, key, &v)) AT(e, 0xD0, int32_t) = (int32_t)v;
        snprintf(key, sizeof key, "index%d", i); if (jnum(line, key, &v)) AT(e, 0x40, int32_t) = (int32_t)v;
        AT(e, 0x20, uint8_t) = 1;
        snprintf(key, sizeof key, "possible%d", i); if (jnum(line, key, &v)) AT(e, 0x20, uint8_t) = (uint8_t)v;
        const char *ek[3] = { "user", "target", "item" }; int eo[3] = { 0x28, 0x30, 0x38 };
        for (int k = 0; k < 3; k++) {
            snprintf(key, sizeof key, "%s%d", ek[k], i);
            Guid g;
            if (jstr(line, key, gs, sizeof gs) && guid_parse(gs, &g)) {
                Obj ent = ord_debug_find_entity(map, (const uint8_t *)&g);
                if (!ent) return err_json("hover entity not found");
                SETREF(arr, e + eo[k], ent);
            }
        }
    }
    SETREF(list, (char *)list + 0x10, arr); AT(list, 0x18, int32_t) = n; AT(list, 0x1C, int32_t) = 1;
    Obj opt = obj_new(0x66503C8);                                    /* Refactor.UI.InteractionOption */
    F(0xE72AC8, void (*)(Obj, Obj, void *))(opt, list, NULL);
    F(0xC2562C, void (*)(Obj, Obj, void *))(ioc, opt, NULL);
    return ok_json("\"hover\":1");
}

/* read-only: {"guid":unit} -> behaviour option, quick slots, auto-use skills (to show Tier A effects) */
void *ord_debug_skill_component(void *unit);
static void put_str(Buf *b, Obj s) {
    if (!s) { bput(b, "null"); return; }
    char t[256]; int32_t n = AT(s, 0x10, int32_t); int k = 0;
    for (int i = 0; i < n && k < 250; i++) { uint16_t ch = ((uint16_t *)((char *)s + 0x14))[i]; t[k++] = ch < 128 ? (char)ch : '?'; }
    t[k] = 0; bstr(b, t);
}
static char *c_read(const char *line) {
    char gs[64]; Guid g;
    if (!jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g)) return err_json("need guid");
    Obj u = ord_debug_find_entity(cur_map(), (const uint8_t *)&g);
    if (!u) return err_json("unit not found");
    Buf b = {0}; bput(&b, "{\"ok\":true");
    Obj r = AT(u, 0x40, Obj); Obj info = r ? AT(r, 0x10, Obj) : NULL;          /* EntityInfoComponent */
    if (info) bput(&b, ",\"behaviour\":%d", AT(info, 0x20, int32_t));
    Obj pudc = AT(cdc(), 0x48, Obj); Obj qsc = pudc ? AT(pudc, 0x20, Obj) : NULL; Obj d = qsc ? AT(qsc, 0x10, Obj) : NULL;
    bput(&b, ",\"quick\":[");
    if (d) {
        Obj ent = AT(d, 0x18, Obj); int n = AT(d, 0x20, int32_t);
        for (int i = 0; ent && i < n; i++) {
            char *e = (char *)ent + 0x20 + i * 0x20;
            if (*(int32_t *)e < 0 || memcmp(e + 8, &g, 16)) continue;
            Obj q = *(Obj *)(e + 0x18); Obj arr = q ? AT(q, 0x10, Obj) : NULL;
            int m = arr ? (int)AT(arr, 0x18, uint64_t) : 0;
            for (int k = 0; k < m; k++) { if (k) bput(&b, ","); put_str(&b, *(Obj *)((char *)arr + 0x20 + k * 8)); }
        }
    }
    bput(&b, "],\"auto\":{");
    Obj sc = ord_debug_skill_component(u); Obj au = sc ? AT(sc, 0x48, Obj) : NULL;
    if (au) {
        Obj ent = AT(au, 0x18, Obj); int n = AT(au, 0x20, int32_t), k = 0;
        for (int i = 0; ent && i < n; i++) {
            char *e = (char *)ent + 0x20 + i * 0x18;
            if (*(int32_t *)e < 0) continue;
            if (k++) bput(&b, ",");
            put_str(&b, *(Obj *)(e + 8)); bput(&b, ":%s", *(bool *)(e + 0x10) ? "true" : "false");
        }
    }
    bput(&b, "},\"cooldowns\":{");
    Obj cd = sc ? AT(sc, 0x38, Obj) : NULL;
    if (cd) {
        Obj ent = AT(cd, 0x18, Obj); int n = AT(cd, 0x20, int32_t), k = 0;
        for (int i = 0; ent && i < n; i++) {
            char *e = (char *)ent + 0x20 + i * 0x18;
            if (*(int32_t *)e < 0) continue;
            if (k++) bput(&b, ",");
            put_str(&b, *(Obj *)(e + 8)); bput(&b, ":%.2f", *(float *)(e + 0x10));
        }
    }
    bput(&b, "}}");
    return b.p;
}

/* replay test of the Run path without the cursor: a RUN order with one attack candidate, through ord_apply
 * {"unit":g,"target":g,"x":..,"y":..} */
static char *c_run_attack(const char *line) {
    char a[64], t[64]; double x = 0, y = 0;
    if (!jstr(line, "unit", a, sizeof a) || !jstr(line, "target", t, sizeof t)) return err_json("need unit, target");
    double ty = 2; jnum(line, "x", &x); jnum(line, "y", &y); jnum(line, "type", &ty);   /* InteractionType (2 attack, 29 portal) */
    ord_msg *m = calloc(1, sizeof *m);
    m->kind = ORD_K_RUN; m->ctx.map = cur_map(); m->ctx.sel_group = 3003; m->ctx.nsel = 1;
    guid_parse(a, (Guid *)&m->ctx.sel[0]);
    m->x = (int32_t)x; m->y = (int32_t)y; m->nact = 1;
    m->act[0].unit = m->ctx.sel[0];
    ord_ia *ia = &m->act[0].ia;
    ia->type = (int)ty; ia->is_possible = 1; ia->has_user = 1; ia->user = m->ctx.sel[0];
    ia->has_target = 1; guid_parse(t, (Guid *)&ia->target);
    ia->target_text_null = ia->invalid_msg_null = ia->ps1_null = ia->ps2_null = 1;
    m->nnear = 1; m->near_units[0] = m->ctx.sel[0];
    ord_wbuf w = {0};
    int rc = ord_msg_write(NULL, &w, m);
    if (!rc) rc = ord_apply(0, w.p, w.n);
    free(w.p); free(m);
    char r[64]; snprintf(r, sizeof r, "\"rc\":%d", rc);
    return ok_json(r);
}

int rig_save_campaign(const char *name);
char *ord_smoke_exec(const char *cmd, const char *line) {
    if (!strcmp(cmd, "ord_owner")) {            /* {"guid":g} -> dsnet owner of the unit (-1 unclaimed) */
        char gs[64]; Guid g;
        if (!jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g)) return err_json("need guid");
        void *(*sess)(void) = dlsym(RTLD_DEFAULT, "mp_game_session");
        void *(*cli)(void *) = dlsym(RTLD_DEFAULT, "mp_client");
        int (*own)(void *, const uint8_t *) = dlsym(RTLD_DEFAULT, "dsnet_owner_of");
        void *S = sess ? sess() : NULL;
        if (!S || !cli || !own) return err_json("no session");
        char x[48]; snprintf(x, sizeof x, "\"owner\":%d", own(cli(S), (const uint8_t *)&g)); return ok_json(x);
    }
    if (!strcmp(cmd, "ord_item")) {             /* {"guid":item, "owner":N?} read (and with owner: set via dsnet) the item owner */
        char gs[64]; Guid g; double o;
        if (!jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g)) return err_json("need guid");
        void *(*sess)(void) = dlsym(RTLD_DEFAULT, "mp_game_session");
        void *(*cli)(void *) = dlsym(RTLD_DEFAULT, "mp_client");
        int (*get)(void *, const uint8_t *) = dlsym(RTLD_DEFAULT, "dsnet_item_owner");
        int (*set)(void *, const uint8_t *, int) = dlsym(RTLD_DEFAULT, "dsnet_item_set_owner");
        void *S = sess ? sess() : NULL;
        if (!S || !cli || !get || !set) return err_json("no session");
        int rc = jnum(line, "owner", &o) ? set(cli(S), (const uint8_t *)&g, (int)o) : 0;
        char x[64]; snprintf(x, sizeof x, "\"owner\":%d,\"rc\":%d", get(cli(S), (const uint8_t *)&g), rc); return ok_json(x);
    }
    if (!strcmp(cmd, "ord_party")) {            /* {"guids":"g1,g2"} activate this peer's expedition party (UI state); none = deactivate */
        char gs[1024] = ""; jstr(line, "guids", gs, sizeof gs);
        Obj ctx = AT(mf, 0x90, Obj);
        if (!ctx) return err_json("no ExpeditionPlanPartyContext");
        Obj l = obj_new(0x66457D8); Obj a = F(0x6F8BA8, Obj (*)(void *, size_t))(md(0x664E200), 32); int n = 0;
        for (char *t = strtok(gs, ","); t && n < 32; t = strtok(NULL, ",")) { Guid g; if (guid_parse(t, &g)) memcpy((char *)a + 0x20 + 16 * n++, &g, 16); }
        SETREF(l, (char *)l + 0x10, a); AT(l, 0x18, int32_t) = n; AT(l, 0x1C, int32_t) = 1;
        if (n) F(0xEDFB68, void (*)(Obj, Obj, void *))(ctx, l, NULL);      /* Activate(IReadOnlyList<Guid>) */
        else F(0xEDFD6C, void (*)(Obj, void *))(ctx, NULL);               /* Deactivate() */
        char x[64]; snprintf(x, sizeof x, "\"active\":%d,\"n\":%d", AT(ctx, 0x18, uint8_t), n); return ok_json(x);
    }
    if (!strcmp(cmd, "ord_save")) {             /* {"name":X}: CampaignSaveLoader.Save -> data/Saves/X.json */
        char nm[128] = "ord_check"; jstr(line, "name", nm, sizeof nm);
        return rig_save_campaign(nm) ? ok_json("\"saved\":1") : err_json("save failed");
    }
    if (!strcmp(cmd, "ord_read")) return c_read(line);
    if (!strcmp(cmd, "ord_run_attack")) return c_run_attack(line);
    if (!strcmp(cmd, "ord_hover")) return c_hover(line);
    if (!strcmp(cmd, "ord_loop")) {
        double on = 0; jnum(line, "on", &on);
        loop_on = on != 0;
        ord_capture(loop_on ? loop_emit : NULL, NULL);
        return ok_json(loop_on ? "\"loop\":1" : "\"loop\":0");
    }
    if (!strcmp(cmd, "ord_inject")) return c_inject(line);
    if (!strcmp(cmd, "ord_select")) return c_select(line);
    if (!strcmp(cmd, "ord_stats")) return c_stats();
    return NULL;
}
