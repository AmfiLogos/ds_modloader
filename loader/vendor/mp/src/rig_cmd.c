#include "mpmenu.h"
// Rig commands, run on Unity's main thread from the MainFlow.Update hook.
// Every address and offset: ~/ds_multiplayer/research/rig_access_spec.md (build 25846389, arm64).
#include "rig.h"
#include "mp_game.h"
#include "ui_mp.h"
#include "trn.h"
int ui_debug_map_vote(int accept);
char *ord_smoke_exec(const char *cmd, const char *line);
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <math.h>

typedef struct { float x, y, w, h; } RectF;
typedef struct { void *begin; int32_t length; } SpanW;


volatile int64_t rig_frames;
Obj mf;                           /* MainFlow instance (from the hook) */

/* ---------------------------------------------------------------- il2cpp / Unity helpers */
static inline void md_init(uint64_t slot) { F(0x6FB6FC, void (*)(void **))((void **)(ds_base + slot)); }
void *md(uint64_t slot) { md_init(slot); return *(void **)(ds_base + slot); }
static inline Obj str_new(const char *s) { return F(0x6F9738, Obj (*)(const char *))(s); }
Obj obj_new(uint64_t ti_slot) { return F(0x6FB9C4, Obj (*)(void *))(md(ti_slot)); }
static void *icall(const char *name) {
    static void *(*resolve)(const char *);
    if (!resolve) resolve = F(0x6F8B90, void *(*)(const char *));
    return resolve(name);
}

static void utf16_to_utf8(Obj s, char *out, size_t n) {
    size_t o = 0;
    if (s) {
        int32_t len = AT(s, 0x10, int32_t);
        const uint16_t *c = (const uint16_t *)((char *)s + 0x14);
        for (int32_t i = 0; i < len && o + 4 < n; i++) {
            uint32_t u = c[i];
            if (u >= 0xD800 && u < 0xDC00 && i + 1 < len) { u = 0x10000 + ((u - 0xD800) << 10) + (c[i + 1] - 0xDC00); i++; }
            if (u < 0x80) out[o++] = (char)u;
            else if (u < 0x800) { out[o++] = (char)(0xC0 | u >> 6); out[o++] = (char)(0x80 | (u & 0x3F)); }
            else if (u < 0x10000) { out[o++] = (char)(0xE0 | u >> 12); out[o++] = (char)(0x80 | ((u >> 6) & 0x3F)); out[o++] = (char)(0x80 | (u & 0x3F)); }
            else { out[o++] = (char)(0xF0 | u >> 18); out[o++] = (char)(0x80 | ((u >> 12) & 0x3F)); out[o++] = (char)(0x80 | ((u >> 6) & 0x3F)); out[o++] = (char)(0x80 | (u & 0x3F)); }
        }
    }
    out[o] = 0;
}

/* .NET Guid text form: int32 a, int16 b, int16 c, 8 bytes */
void guid_str(Guid g, char *out) {
    const uint8_t *b = (const uint8_t *)&g;
    snprintf(out, 40, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             *(const uint32_t *)b, *(const uint16_t *)(b + 4), *(const uint16_t *)(b + 6),
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}
int guid_parse(const char *s, Guid *g) {
    unsigned a, bb, c, d[8];
    if (sscanf(s, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &a, &bb, &c, &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6], &d[7]) != 11) return 0;
    uint8_t *b = (uint8_t *)g;
    *(uint32_t *)b = a; *(uint16_t *)(b + 4) = (uint16_t)bb; *(uint16_t *)(b + 6) = (uint16_t)c;
    for (int i = 0; i < 8; i++) b[8 + i] = (uint8_t)d[i];
    return 1;
}

/* Dictionary<int enum, ref>: entries stride 0x18, key +0x8, value +0x10 */
Obj dict_int_get(Obj d, int key) {
    if (!d) return NULL;
    Obj ent = AT(d, 0x18, Obj); int n = AT(d, 0x20, int32_t);
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x18;
        if (*(int32_t *)e >= 0 && *(int32_t *)(e + 8) == key) return *(Obj *)(e + 0x10);
    }
    return NULL;
}
/* Dictionary<Guid, Vector2>: stride 0x20, key +0x8, x +0x18, y +0x1C */
static int pos_get(Obj d, Guid g, float *x, float *y) {
    if (!d) return 0;
    Obj ent = AT(d, 0x18, Obj); int n = AT(d, 0x20, int32_t);
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x20;
        if (*(int32_t *)e >= 0 && !memcmp(e + 8, &g, 16)) { *x = *(float *)(e + 0x18); *y = *(float *)(e + 0x1C); return 1; }
    }
    return 0;
}

/* ---------------------------------------------------------------- game state */
Obj cdc(void) { return mf ? AT(mf, 0xC8, Obj) : NULL; }
int scene(void) { return mf ? AT(mf, 0x18C, int32_t) : -1; }
int cur_map(void) { Obj c = cdc(); return (scene() == 2 && c) ? AT(c, 0x10, int32_t) : -1; }
int64_t tick(void) { Obj c = cdc(); Obj t = c ? AT(c, 0xA8, Obj) : NULL; return t ? AT(t, 0x10, int64_t) : -1; }
Obj units_dict(int map) {
    Obj c = cdc(); if (!c) return NULL;
    Obj edc = AT(c, 0x20, Obj); if (!edc) return NULL;
    Obj ucd = dict_int_get(AT(edc, 0x18, Obj), map);
    return ucd ? AT(ucd, 0x10, Obj) : NULL;
}
Obj pos_dict(int map) {
    Obj c = cdc(); if (!c) return NULL;
    Obj edc = AT(c, 0x20, Obj); if (!edc) return NULL;
    return dict_int_get(AT(edc, 0x50, Obj), map);
}
static Obj speed_setting(void) { Obj s = mf ? AT(mf, 0xD8, Obj) : NULL; return s ? AT(s, 0x10, Obj) : NULL; }
int busy(void) {
    if (!mf) return 1;
    Obj sl = AT(mf, 0x38, Obj); if (sl && AT(sl, 0x10, int32_t) > 0) return 1;
    Obj ts = AT(mf, 0xF8, Obj); if (ts && AT(ts, 0x14, bool)) return 1;
    Obj lc = AT(mf, 0xF0, Obj); if (lc && F(0xC98A8C, bool (*)(Obj, void *))(lc, NULL)) return 1;
    return 0;
}

static int unit_faction(Obj u) { Obj r = AT(u, 0x40, Obj); Obj c = r ? AT(r, 0x10, Obj) : NULL; return c ? AT(c, 0x18, int32_t) : -1; }
bool unit_dead(Obj u) { Obj r = AT(u, 0x40, Obj); Obj c = r ? AT(r, 0x10, Obj) : NULL; return c ? AT(c, 0x1C, bool) : false; }
float unit_stat(Obj u, int stat) {
    Obj sr = F(0x185FEDC, Obj (*)(Obj, void *))(u, md(0x6691B40));     /* GetComponent<StatusReader> */
    return sr ? F(0xFA0EE4, float (*)(Obj, int, void *))(sr, stat, NULL) : -1.0f;
}
int unit_task(Obj u) {
    Obj tr = F(0x185FEDC, Obj (*)(Obj, void *))(u, md(0x6691B48));     /* GetComponent<TaskReader> */
    if (!tr) return -2;
    Obj tc = AT(tr, 0x10, Obj);
    return tc ? AT(tc, 0x28, int32_t) : -2;
}

RState rstate(void) { RState st; F(0x50E77B0, void (*)(RState *, void *))(&st, NULL); return st; }

/* ---------------------------------------------------------------- per-frame */
static int fps = 30;
void rig_frame(void *mainflow) {
    mf = mainflow;
    static int live = -1;               /* DSRIG_LIVE=1: the owner's MP app, a normal game: no mute, no fps/vSync cap, */
    if (live < 0) live = getenv("DSRIG_LIVE") != NULL;   /* no window hiding / placement */
    int64_t f = rig_frames++;
    if (f == 0) {
        const char *e = getenv("DSRIG_FPS"); if (e && atoi(e) > 0) fps = atoi(e);
        rig_log("first MainFlow.Update; frame cap %d", fps);
    }
    if (f % 60 == 0) {                                  /* the game may re-apply its own settings */
        if (!live) {
            F(0x5097CD8, void (*)(int, void *))(0, NULL);      /* QualitySettings.vSyncCount = 0 */
            F(0x507C0BC, void (*)(int, void *))(fps, NULL);    /* Application.targetFrameRate */
            F(0x507B3FC, void (*)(bool, void *))(true, NULL);  /* Application.runInBackground */
            static void (*set_pause)(bool);
            if (!set_pause) set_pause = icall("UnityEngine.AudioListener::set_pause");
            if (set_pause) set_pause(true);
        }
        if (!getenv("DSRIG_MINIMAL"))
        for (int lt = 0; lt <= 3; lt++)                    /* no stack traces on Error/Assert/Warning/Log: A* "Path Failed" */
            F(0x507C1AC, void (*)(int, int, void *))(lt, 0, NULL);   /* spam cost a copy ~80% of its frame time */
        Obj ak = md(0x664A0A8); Obj asf = ak ? AT(ak, 0xB8, Obj) : NULL; Obj act = asf ? AT(asf, 0x18, Obj) : NULL;
        if (act && !getenv("DSRIG_MINIMAL")) AT(act, 0x48, int32_t) = 0;               /* AstarPath.logPathResults = None */
    }
    if (!live) {
        F(0x5067948, void (*)(float, void *))(0.0f, NULL);   /* AudioListener.volume = 0, every frame (focus handler resets it) */
        if (getenv("DSRIG_HIDEWIN") && f % 30 == 0) rig_win_frame();
        if (getenv("DSRIG_SPACE") && f % 30 == 0) rig_win_space_frame();
        if (getenv("DSRIG_SHOWWIN") && f % 30 == 0) rig_win_place();
    }
    for (RigReq *r = rig_take_requests(); r; ) {
        RigReq *nx = r->next;
        rig_finish(r, rig_exec(r->line));
        r = nx;
    }
    if (getenv("DSRIG_MINIMAL")) return;
    mp_game_poll();                     /* Steam joiner bridge / invites (title screen too) */
    ui_frame();                         /* multiplayer UI: status line, owner tags (title screen too) */
    mpmenu_frame();                     /* Multiplayer menu (title screen too) */
}

/* ---------------------------------------------------------------- commands */
static char *err(const char *msg) { Buf b = {0}; bput(&b, "{\"ok\":false,\"error\":"); bstr(&b, msg); bput(&b, "}"); return b.p; }

static char *c_status(void) {
    Buf b = {0};
    int sc = scene(), map = cur_map();
    bput(&b, "{\"ok\":true,\"boot_ready\":%s,\"frames\":%lld,\"scene\":%d,\"scene_name\":\"%s\",\"busy\":%s,\"map\":%d,\"tick\":%lld",
         mf && AT(mf, 0x188, bool) ? "true" : "false", (long long)rig_frames, sc, sc == 1 ? "title" : sc == 2 ? "campaign" : sc == 9 ? "loading" : sc == 0 ? "entry" : "other",
         busy() ? "true" : "false", map, (long long)tick());
    if (mf) {
        Obj sl = AT(mf, 0x38, Obj), ts = AT(mf, 0xF8, Obj), lc = AT(mf, 0xF0, Obj);
        bput(&b, ",\"scene_loads\":%d,\"map_transition\":%s", sl ? AT(sl, 0x10, int32_t) : -1, ts && AT(ts, 0x14, bool) ? "true" : "false");
        if (lc) bput(&b, ",\"map_loading\":{\"is_loading\":%s,\"stage\":%d,\"fow_loaded\":%d,\"mob_loaded\":%d,\"fow_req\":%d,\"mob_req\":%d}",
                     F(0xC98A8C, bool (*)(Obj, void *))(lc, NULL) ? "true" : "false", AT(lc, 0x14, int32_t),
                     AT(lc, 0x10, bool), AT(lc, 0x11, bool), AT(lc, 0x12, bool), AT(lc, 0x13, bool));
    }
    Obj gs = speed_setting();
    if (gs) bput(&b, ",\"speed\":%d,\"requested_speed\":%d,\"speed_lock\":%d", AT(gs, 0x10, int32_t), AT(gs, 0x14, int32_t), AT(gs, 0x20, int32_t));
    Obj c = cdc();
    if (c && sc == 2) {
        Obj dd = AT(c, 0x40, Obj);
        if (dd) bput(&b, ",\"region\":%d,\"floor\":%d,\"phase\":%d", AT(dd, 0x10, int32_t), AT(dd, 0x14, int32_t), AT(dd, 0x18, int32_t));
        Obj ud = units_dict(map);
        if (ud) bput(&b, ",\"units\":%d", AT(ud, 0x20, int32_t) - AT(ud, 0x28, int32_t));
    }
    char pdp[1024] = "";
    utf16_to_utf8(F(0x507B74C, Obj (*)(void *))(NULL), pdp, sizeof pdp);
    rig_det_status(&b);
    mp_game_status(&b);
    bput(&b, ",\"persistentDataPath\":"); bstr(&b, pdp);
    bput(&b, "}");
    return b.p;
}

static char *c_list_units(const char *line) {
    double dm; int map = jnum(line, "map", &dm) ? (int)dm : cur_map();
    Obj ud = units_dict(map); if (!ud) return err("no unit container for this map (not in a campaign?)");
    Obj pd = pos_dict(map);
    Buf b = {0}; bput(&b, "{\"ok\":true,\"map\":%d,\"tick\":%lld,\"units\":[", map, (long long)tick());
    Obj ent = AT(ud, 0x18, Obj); int n = AT(ud, 0x20, int32_t), k = 0;
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x20;
        if (*(int32_t *)e < 0) continue;
        Guid g = *(Guid *)(e + 8); Obj u = *(Obj *)(e + 0x18);
        if (!u) continue;
        char gs[40], name[256], key[256];
        guid_str(g, gs);
        utf16_to_utf8(F(0xFA47A0, Obj (*)(Obj, void *))(u, NULL), name, sizeof name);
        utf16_to_utf8(AT(u, 0x38, Obj), key, sizeof key);
        float x = 0, y = 0; int hp = pos_get(pd, g, &x, &y);
        bput(&b, "%s{\"guid\":\"%s\",\"name\":", k++ ? "," : "", gs); bstr(&b, name);
        bput(&b, ",\"key\":"); bstr(&b, key);
        bput(&b, ",\"faction\":%d,\"dead\":%s", unit_faction(u), unit_dead(u) ? "true" : "false");
        if (hp) bput(&b, ",\"x\":%.3f,\"y\":%.3f,\"cell\":[%d,%d]", x, y, (int)floorf(x), (int)floorf(y));
        int tk = unit_task(u);                     /* TaskType Deadly 1000 = downed (on the floor, dying), Exhausted 1001 */
        bput(&b, ",\"hp\":%.2f,\"max_hp\":%.2f,\"task\":%d,\"downed\":%s}", unit_stat(u, 9), unit_stat(u, 22), tk, tk == 1000 ? "true" : "false");
    }
    bput(&b, "]}");
    return b.p;
}

Obj find_unit(int map, Guid g) {
    Obj ud = units_dict(map); if (!ud) return NULL;
    Obj ent = AT(ud, 0x18, Obj); int n = AT(ud, 0x20, int32_t);
    for (int i = 0; ent && i < n; i++) {
        char *e = (char *)ent + 0x20 + i * 0x20;
        if (*(int32_t *)e >= 0 && !memcmp(e + 8, &g, 16)) return *(Obj *)(e + 0x18);
    }
    return NULL;
}

static char *c_order_move(const char *line) {
    char gs[64]; double x, y; Guid g;
    if (!jstr(line, "guid", gs, sizeof gs) || !guid_parse(gs, &g) || !jnum(line, "x", &x) || !jnum(line, "y", &y))
        return err("need guid, x, y (cell)");
    int map = cur_map(); if (map < 0) return err("not in a campaign");
    Obj u = find_unit(map, g); if (!u) return err("unit not found on the current map");
    Obj tfh = dict_int_get(AT(mf, 0x140, Obj), map); if (!tfh) return err("no TickFlowHandler for this map");
    uint64_t cell = (uint32_t)(int)x | ((uint64_t)(uint32_t)(int)y << 32);
    Obj task = obj_new(0x6651C78);                                                    /* MoveTask */
    F(0xB1BF60, void (*)(Obj, uint64_t, int, int, void *))(task, cell, 0, 0, NULL);
    Obj ord = obj_new(0x664C9A0);                                                     /* DirectOrderRequested */
    F(0xD64F1C, void (*)(Obj, Obj, Obj, void *))(ord, u, task, NULL);
    F(0xD28034, void (*)(Obj, Obj, bool, void *))(tfh, ord, false, NULL);             /* TickFlowHandler.EnqueueEvent */
    Buf b = {0}; bput(&b, "{\"ok\":true,\"queued_at_tick\":%lld,\"cell\":[%d,%d]}", (long long)tick(), (int)x, (int)y);
    return b.p;
}

static char *c_set_speed(const char *line) {
    double s; if (!jnum(line, "speed", &s) || s < 0 || s > 3) return err("need speed 0..3 (0 = pause)");
    Obj eq = mf ? AT(mf, 0x48, Obj) : NULL; if (!eq) return err("no event queue");
    Obj e;
    if ((int)s == 0) { e = obj_new(0x664B1A8); F(0xDCF088, void (*)(Obj, int, int, void *))(e, 0, 1, NULL); }    /* ChangeGameSpeedRequested(0, AbsolutePause) */
    else { e = obj_new(0x66542F0); F(0xDCF018, void (*)(Obj, int, void *))(e, (int)s, NULL); }                     /* SetGameSpeedRequested */
    F(0xCCFAD4, void (*)(Obj, Obj, void *))(eq, e, NULL);
    return strdup("{\"ok\":true,\"queued\":true}");
}

/* Copy a save file into data/Saves/<SaveName>.json and queue LoadCampaignRequested. NULL ok, else an error text. */
static const char *load_file(const char *file, int det, char *name, size_t name_n) {
    FILE *f = fopen(file, "rb"); if (!f) return "cannot open file";
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *d = malloc((size_t)n + 1); fread(d, 1, (size_t)n, f); fclose(f); d[n] = 0;
    char *p = strstr(d, "\"SaveName\"");
    if (!p || !jstr(p, "SaveName", name, name_n)) { free(d); return "no SaveName in the file"; }
    char dst[1400]; snprintf(dst, sizeof dst, "%s/data/Saves", rig_dir); mkdir(dst, 0755);
    snprintf(dst, sizeof dst, "%s/data/Saves/%s.json", rig_dir, name);
    if (strcmp(dst, file)) {
        char tmp[1500]; snprintf(tmp, sizeof tmp, "%s.tmp", dst);
        FILE *o = fopen(tmp, "wb"); if (!o) { free(d); return "cannot write into data/Saves"; }
        fwrite(d, 1, (size_t)n, o); fclose(o); rename(tmp, dst);
    }
    free(d);
    if (scene() != 1 && scene() != 2) return "game not at title or in a campaign yet";
    if (det && scene() == 2) {
        /* Iron mode blocks loading the running campaign's own save (CampaignLoadingEventHandler.
           ApplyIronModeLoadPolicy: same campaign Guid -> "Current iron mode campaign load was blocked").
           A lockstep resync / host reload needs exactly that, so the running campaign's iron flag is cleared
           first; the loaded save brings its own flag back. */
        Obj ld = AT(mf, 0x180, Obj), a = ld ? AT(ld, 0x18, Obj) : NULL, cs = a ? AT(a, 0x20, Obj) : NULL;
        if (cs && AT(cs, 0x24, bool)) { AT(cs, 0x24, bool) = false; rig_log("load_file: iron flag cleared for the reload"); }
    }
    if (det) rig_det_on_load();                    /* lockstep: hold ticks, auto-lock when loaded */
    Obj ev = obj_new(0x6651118);                                                      /* LoadCampaignRequested */
    F(0xDCFD78, void (*)(Obj, Obj, void *))(ev, str_new(name), NULL);
    F(0xCCFAD4, void (*)(Obj, Obj, void *))(AT(mf, 0x48, Obj), ev, NULL);
    rig_log("load_save %s queued (det %d)", name, det);
    return NULL;
}
int rig_load_save_file(const char *path) { char name[256]; const char *e = load_file(path, 1, name, sizeof name); if (e) rig_log("rig_load_save_file: %s", e); return e == NULL; }

/* CampaignSaveLoader.Save(name, isAutoSave = iron-mode flag, displayName = name) -> data/Saves/<name>.json */
int rig_save_campaign(const char *name) {
    Obj ld = mf ? AT(mf, 0x180, Obj) : NULL; if (!ld) return 0;
    Obj a = AT(ld, 0x18, Obj), b = a ? AT(a, 0x20, Obj) : NULL;
    bool iron = b ? AT(b, 0x24, bool) : false;
    Obj s = str_new(name);
    if (getenv("DSRIG_DETAIL_SYNC")) rig_det_write_detail("save");
    return F(0x814890, bool (*)(Obj, Obj, bool, Obj, void *))(ld, s, iron, s, NULL);
}

/* load_save {"file": "/abs/path.json", "det": 0|1} or {"name": "<SaveName>"} */
static char *c_load_save(const char *line) {
    char file[1024] = "", name[256] = "";
    double detv = 0; jnum(line, "det", &detv);
    jstr(line, "name", name, sizeof name);
    if (jstr(line, "file", file, sizeof file)) {
        const char *e = load_file(file, detv != 0, name, sizeof name);
        if (e) return err(e);
    } else {
        if (!name[0]) return err("need file or name");
        if (scene() != 1 && scene() != 2) return err("game not at title or in a campaign yet");
        if (detv != 0) rig_det_on_load();
        Obj ev = obj_new(0x6651118);
        F(0xDCFD78, void (*)(Obj, Obj, void *))(ev, str_new(name), NULL);
        F(0xCCFAD4, void (*)(Obj, Obj, void *))(AT(mf, 0x48, Obj), ev, NULL);
    }
    Buf b = {0}; bput(&b, "{\"ok\":true,\"queued\":true,\"save\":"); bstr(&b, name); bput(&b, "}");
    return b.p;
}

/* FNV-1a 64 */
uint64_t fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001B3ULL; } return h; }

/* any entity's Guid: UnitEntity keeps it at +0x24, the other entity classes at +0x10 */
Guid ent_guid(Obj e) {
    Guid g = { 0, 0 };
    if (!e) return g;
    memcpy(&g, (char *)e + (*(void **)e == md(0x6656458) ? 0x24 : 0x10), 16);
    return g;
}
/* combat info: state, current target, forced target; task target */
void unit_combat(Obj u, int *state, Guid *cur, Guid *force, Guid *task_target) {
    Obj cr = F(0x185FEDC, Obj (*)(Obj, void *))(u, md(0x6691B00));    /* GetComponent<CombatInfoReader> */
    Obj cc = cr ? AT(cr, 0x10, Obj) : NULL;
    *state = cc ? AT(cc, 0x18, int32_t) : -9;
    *cur = ent_guid(cc ? AT(cc, 0x28, Obj) : NULL);
    *force = ent_guid(cc ? AT(cc, 0x30, Obj) : NULL);
    Obj tr = F(0x185FEDC, Obj (*)(Obj, void *))(u, md(0x6691B48));    /* GetComponent<TaskReader> */
    Obj tc = tr ? AT(tr, 0x10, Obj) : NULL;
    *task_target = ent_guid(tc ? AT(tc, 0x20, Obj) : NULL);
}

void rig_world_hash(RState rs, uint64_t h[4], int *nunits_out, int *npos_out) {
    const uint64_t O = 0xCBF29CE484222325ULL;
    uint64_t hu = O, hp = O, hr = O;
    int64_t t = tick(); hr = fnv(hr, &t, 8); hr = fnv(hr, &rs, 16);
    hr = rig_det_extra_hash(hr);                    /* weather timers + long-lived System.Random states */
    int nunits = 0, npos = 0;
    for (int map = 0; map < 2; map++) {
        Obj ud = units_dict(map), pd = pos_dict(map);
        hu = fnv(hu, &map, 4); hp = fnv(hp, &map, 4);
        if (ud) {
            Obj ent = AT(ud, 0x18, Obj); int n = AT(ud, 0x20, int32_t);
            for (int i = 0; ent && i < n; i++) {
                char *e = (char *)ent + 0x20 + i * 0x20;
                if (*(int32_t *)e < 0) continue;
                Obj u = *(Obj *)(e + 0x18); if (!u) continue;
                hu = fnv(hu, e + 8, 16);
                float hpv = unit_stat(u, 9), en = unit_stat(u, 15); int task = unit_task(u); bool dead = unit_dead(u);
                hu = fnv(hu, &hpv, 4); hu = fnv(hu, &en, 4); hu = fnv(hu, &task, 4); hu = fnv(hu, &dead, 1);
                int cst; Guid ct, cf, tt; unit_combat(u, &cst, &ct, &cf, &tt);
                hu = fnv(hu, &cst, 4); hu = fnv(hu, &ct, 16); hu = fnv(hu, &cf, 16); hu = fnv(hu, &tt, 16);
                nunits++;
            }
        }
        if (pd) {                                   /* every entity position on the map (units, items, buildings...) */
            Obj ent = AT(pd, 0x18, Obj); int n = AT(pd, 0x20, int32_t);
            for (int i = 0; ent && i < n; i++) {
                char *e = (char *)ent + 0x20 + i * 0x20;
                if (*(int32_t *)e < 0) continue;
                hp = fnv(hp, e + 8, 16 + 8);        /* guid + x,y raw bits */
                npos++;
            }
        }
    }
    uint64_t all = fnv(O, &hu, 8); all = fnv(all, &hp, 8); all = fnv(all, &hr, 8);
    h[0] = all; h[1] = hu; h[2] = hp; h[3] = hr;
    if (nunits_out) *nunits_out = nunits;
    if (npos_out) *npos_out = npos;
}

static char *c_fingerprint(void) {
    if (scene() != 2 || !cdc()) return err("not in a campaign");
    RState st = rig_det_locked() ? rig_det_sim_state() : rstate();
    uint64_t h[4]; int nunits, npos;
    rig_world_hash(st, h, &nunits, &npos);
    Buf b = {0};
    bput(&b, "{\"ok\":true,\"hash\":\"%016llx\",\"units_hash\":\"%016llx\",\"pos_hash\":\"%016llx\",\"tick\":%lld,\"units\":%d,\"positions\":%d,\"random\":[%u,%u,%u,%u],\"sim_random\":%s,\"frames\":%lld}",
         (unsigned long long)h[0], (unsigned long long)h[1], (unsigned long long)h[2], (long long)tick(), nunits, npos,
         st.s[0], st.s[1], st.s[2], st.s[3], rig_det_locked() ? "true" : "false", (long long)rig_frames);
    return b.p;
}

/* screenshot {"path": "/abs.png"}: render Camera.main into a RenderTexture, read back, PNG.
   Camera only (no screen-space overlay UI). */
static char *c_screenshot(const char *line) {
    char path[1024]; if (!jstr(line, "path", path, sizeof path)) return err("need path");
    Obj cam = F(0x5083208, Obj (*)(void *))(NULL);                                    /* Camera.main */
    if (!cam) return err("no Camera.main");
    double dw = 960, dh = 540; jnum(line, "w", &dw); jnum(line, "h", &dh);
    int w = (int)dw, h = (int)dh;
    static void (*render)(void *);
    if (!render) render = icall("UnityEngine.Camera::Render_Injected");
    if (!render) return err("Camera::Render_Injected icall not found");
    Obj rt = F(0x50D2E38, Obj (*)(int, int, int, void *))(w, h, 24, NULL);           /* RenderTexture.GetTemporary */
    F(0x5081CB8, void (*)(Obj, Obj, void *))(cam, rt, NULL);                          /* camera.targetTexture = rt */
    render(AT(cam, 0x10, void *));
    F(0x50CE6A4, void (*)(Obj, void *))(rt, NULL);                                    /* RenderTexture.active = rt */
    Obj tex = obj_new(0x6655970);                                                     /* Texture2D */
    F(0x50C4E5C, void (*)(Obj, int, int, int, bool, void *))(tex, w, h, 3, false, NULL);
    RectF r = { 0, 0, (float)w, (float)h };
    F(0x50C63B0, void (*)(Obj, RectF, int, int, void *))(tex, r, 0, 0, NULL);        /* ReadPixels */
    F(0x50C5F34, void (*)(Obj, void *))(tex, NULL);                                   /* Apply */
    F(0x50CE6A4, void (*)(Obj, void *))(NULL, NULL);
    F(0x5081CB8, void (*)(Obj, Obj, void *))(cam, NULL, NULL);
    F(0x50CFE6C, void (*)(Obj, void *))(rt, NULL);                                    /* ReleaseTemporary */
    Obj png = F(0x51657D8, Obj (*)(Obj, void *))(tex, NULL);                          /* EncodeToPNG */
    F(0x50FC8F8, void (*)(Obj, void *))(tex, NULL);                                   /* Object.Destroy */
    if (!png) return err("EncodeToPNG returned null");
    size_t n = (size_t)AT(png, 0x18, uintptr_t);
    FILE *f = fopen(path, "wb"); if (!f) return err("cannot write path");
    fwrite((char *)png + 0x20, 1, n, f); fclose(f);
    Buf b = {0}; bput(&b, "{\"ok\":true,\"path\":"); bstr(&b, path); bput(&b, ",\"bytes\":%zu,\"w\":%d,\"h\":%d}", n, w, h);
    return b.p;
}

char *rig_exec(const char *line) {
    char cmd[64] = "";
    if (!jstr(line, "cmd", cmd, sizeof cmd)) return err("no cmd");
    char *dr = rig_det_exec(cmd, line); if (dr) return dr;
    if (!strncmp(cmd, "mpm_", 4)) { char *mm = mpmenu_exec(cmd, line); if (mm) return mm; }
    char *mr = mp_game_exec(cmd, line); if (mr) return mr;
    if (!strncmp(cmd, "trn_", 4)) { char *tr = trn_exec(cmd, line); if (tr) return tr; }
    if (!strncmp(cmd, "ui_", 3)) { char *ur = ui_exec(cmd, line); if (ur) return ur; }
    if (!strcmp(cmd, "ui_map_vote")) { double a = 1; jnum(line, "accept", &a); Buf b = {0}; bput(&b, "{\"ok\":true,\"rc\":%d}", ui_debug_map_vote(a != 0)); return b.p; }
    if (!strncmp(cmd, "ord_", 4)) { char *orr = ord_smoke_exec(cmd, line); if (orr) return orr; }
    if (!strcmp(cmd, "status")) return c_status();
    if (!strcmp(cmd, "list_units")) return c_list_units(line);
    if (!strcmp(cmd, "order_move")) return c_order_move(line);
    if (!strcmp(cmd, "set_speed")) return c_set_speed(line);
    if (!strcmp(cmd, "load_save")) return c_load_save(line);
    if (!strcmp(cmd, "fingerprint")) return c_fingerprint();
    if (!strcmp(cmd, "screenshot")) return c_screenshot(line);
    return err("unknown cmd");
}
