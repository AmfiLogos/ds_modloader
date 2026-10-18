// dsl_win.c - the Mods window (work package L2, ~/ds_multiplayer/research/MASTER_PLAN.md; owner 2026-10-15: "the mods
// is fine and save these mods as group. I would want a tab just to control the mods"; logo: Mud Slogel idle).
//
// Contract: ../src/dsl_menu.h (lane L1 owns it). L1 gives the widget kit (DSLUi, game-style clones), the mod list,
// groups, Apply (soft restart) and the module tabs; this file draws the window with that kit only:
//   title band with the Mud Slogel idle animation, the tab strip (Mods first, then the module tabs by order),
//   the Mods tab (one switch per mod, "Save these mods as a group", the groups, Undo changes, Apply),
//   module tabs (their open / update / close / can_close), Esc and X close the window.
// Built when it opens, destroyed when it closes. Shared C: no game address, Unity calls by name (dsl_internal.h).
//
// Test only: DSL_UI_TEST=<dir> runs one command at a time from <dir>/cmd.json (open / close / click / type / state /
// shot) and answers in <dir>/result.json. "click" calls the named control's own callback (the kit's click path is
// lane L1's test); shots are the game's own frame (ScreenCapture) as PNG.
#include "../src/dsl_internal.h"
#include "../src/dsl_menu.h"
#include "dsl_win_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <shellapi.h>
#define mkdir_(p) _mkdir(p)
#else
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#define mkdir_(p) mkdir(p, 0755)
#endif

typedef void *Obj;
typedef struct { float x, y; } V2;
#define CALL(mi, R, ...) ((R (*)(__VA_ARGS__, const void *))(*(void **)(mi)))
#define CALL0(mi, R) ((R (*)(const void *))(*(void **)(mi)))

#define WIN_W 1040.0f
#define WIN_H 720.0f
#define BAND_H 88.0f
#define TAB_Y 100.0f
#define TAB_W 176.0f
#define TAB_H 40.0f
#define BODY_Y 156.0f
#define GOLD 0xD9BE8FFFu
#define SUB 0xA69085FFu
#define WARN 0xEA9F3DFFu

static const DSLUi *U;
/* dsl_reg.c: the scene (1 title, 2 world) and the switches for the prewarm (n < 0 = drop) */
int dsl_reg_scene(void);
void dsl_mods_draft(const char *const *ids, int n);
static int read_only;                                                  /* in a world: mods change only at the title (owner #10) */
static int is_open, want_open, cur = -1, ntabs_shown;
static char want_tab[48];
static DSLObj w_back, w_win, w_body, w_tab[17], w_mark, w_slogel[SLOGEL_FRAMES];
static int slogel_shown = -1;
static char slogel_dir[1100];

/* ---------------------------------------------------------------- Unity by name (centre a panel, Esc, time, shot) */
static void *m_amin, *m_amax, *m_pivot, *m_pos, *m_size, *m_key, *m_time, *m_shot, *m_png, *m_destroy, *m_cur_es, *m_sel_go,
            *m_go_get, *m_in_focused, *m_img_sprite, *m_gr_color, *k_image, *k_input;
static void *(*x_get_methods)(void *, void **); static const char *(*x_mname)(void *); static uint32_t (*x_pcount)(void *);
static void *(*x_param)(void *, uint32_t); static char *(*x_tname)(void *); static void (*x_free)(void *);
static void *(*x_class_type)(void *); static void *(*x_type_obj)(void *);
/* a method by name + parameter types (overloads with the same argument count), e.g. "System.Type" */
static void *mth_sig(const char *ns, const char *k, const char *m, int argc, const char *sig) {
    void *c = dsl_find_class(ns, k), *it = NULL, *mi;
    while (c && x_get_methods && (mi = x_get_methods(c, &it))) {
        if (strcmp(x_mname(mi), m) || (int)x_pcount(mi) != argc) continue;
        char got[256] = ""; size_t o = 0;
        for (int i = 0; i < argc && o < sizeof got; i++) { char *t = x_tname(x_param(mi, (uint32_t)i)); o += (size_t)snprintf(got + o, sizeof got - o, "%s%s", i ? "," : "", t ? t : "?"); if (t && x_free) x_free(t); }
        if (!strcmp(got, sig)) return mi;
    }
    dsl_log("win: %s.%s.%s(%s) not found", ns, k, m, sig);
    return NULL;
}
static void *mth(const char *ns, const char *k, const char *m, int argc) {
    void *c = dsl_find_class(ns, k), *mi = c ? dsl_find_method(c, m, argc) : NULL;
    if (!mi) dsl_log("win: %s.%s.%s/%d not found", ns, k, m, argc);
    return mi;
}
static void unity_init(void) {
    m_amin = mth("UnityEngine", "RectTransform", "set_anchorMin", 1); m_amax = mth("UnityEngine", "RectTransform", "set_anchorMax", 1);
    m_pivot = mth("UnityEngine", "RectTransform", "set_pivot", 1); m_pos = mth("UnityEngine", "RectTransform", "set_anchoredPosition", 1);
    m_size = mth("UnityEngine", "RectTransform", "set_sizeDelta", 1);
    m_key = mth("UnityEngine", "Input", "GetKeyDownInt", 1);              /* GetKeyDown(KeyCode) has a string twin: the Int one is unique */
    m_time = mth("UnityEngine", "Time", "get_realtimeSinceStartup", 0);
    m_cur_es = mth("UnityEngine.EventSystems", "EventSystem", "get_current", 0);
    m_sel_go = mth("UnityEngine.EventSystems", "EventSystem", "get_currentSelectedGameObject", 0);
    *(void **)&x_get_methods = dsl_il2cpp_fn("il2cpp_class_get_methods"); *(void **)&x_mname = dsl_il2cpp_fn("il2cpp_method_get_name");
    *(void **)&x_pcount = dsl_il2cpp_fn("il2cpp_method_get_param_count"); *(void **)&x_param = dsl_il2cpp_fn("il2cpp_method_get_param");
    *(void **)&x_tname = dsl_il2cpp_fn("il2cpp_type_get_name"); *(void **)&x_free = dsl_il2cpp_fn("il2cpp_free");
    *(void **)&x_class_type = dsl_il2cpp_fn("il2cpp_class_get_type"); *(void **)&x_type_obj = dsl_il2cpp_fn("il2cpp_type_get_object");
    m_go_get = mth_sig("UnityEngine", "Component", "GetComponent", 1, "System.Type");
    m_in_focused = mth("TMPro", "TMP_InputField", "get_isFocused", 0);
    m_img_sprite = mth("UnityEngine.UI", "Image", "set_sprite", 1); m_gr_color = mth("UnityEngine.UI", "Graphic", "set_color", 1);
    k_image = dsl_find_class("UnityEngine.UI", "Image"); k_input = dsl_find_class("TMPro", "TMP_InputField");
}
static Obj comp_of(Obj any_comp, void *k) {                           /* GetComponent(typeof(k)) */
    if (!any_comp || !k || !m_go_get || !x_class_type || !x_type_obj) return NULL;
    return CALL(m_go_get, Obj, Obj, Obj)(any_comp, x_type_obj(x_class_type(k)), m_go_get);
}
typedef struct { float r, g, b, a; } Col;
/* a kit panel as a flat colour (no frame sprite): backdrop, bands, lines, markers */
static DSLObj flat(DSLObj panel, uint32_t c) {
    Obj im = comp_of(panel, k_image);
    if (im && m_img_sprite && m_gr_color) {
        CALL(m_img_sprite, void, Obj, Obj)(im, NULL, m_img_sprite);
        CALL(m_gr_color, void, Obj, Col)(im, (Col){ (c >> 24) / 255.f, (c >> 16 & 255) / 255.f, (c >> 8 & 255) / 255.f, (c & 255) / 255.f }, m_gr_color);
    }
    return panel;
}
static void centre(DSLObj rt) {                                       /* the window in the middle of the screen */
    if (!rt || !m_amin || !m_amax || !m_pivot || !m_pos) return;
    V2 h = { 0.5f, 0.5f }, z = { 0, 0 };
    CALL(m_amin, void, Obj, V2)(rt, h, m_amin); CALL(m_amax, void, Obj, V2)(rt, h, m_amax);
    CALL(m_pivot, void, Obj, V2)(rt, h, m_pivot); CALL(m_pos, void, Obj, V2)(rt, z, m_pos);
}
static void fill_parent(DSLObj rt) {                                 /* the whole screen layer */
    if (!rt || !m_amin || !m_amax || !m_pivot || !m_pos || !m_size) return;
    V2 z = { 0, 0 }, o = { 1, 1 }, h = { 0.5f, 0.5f };
    CALL(m_amin, void, Obj, V2)(rt, z, m_amin); CALL(m_amax, void, Obj, V2)(rt, o, m_amax); CALL(m_pivot, void, Obj, V2)(rt, h, m_pivot);
    CALL(m_size, void, Obj, V2)(rt, z, m_size); CALL(m_pos, void, Obj, V2)(rt, z, m_pos);
}
static int esc_down(void) { return m_key && CALL(m_key, int8_t, int32_t)(27, m_key) != 0; }
static double now_s(void) { return m_time ? CALL0(m_time, float)(m_time) : 0; }
static int typing(void) {                                             /* a text field has the keyboard (Esc is its own) */
    Obj es = m_cur_es ? CALL0(m_cur_es, Obj)(m_cur_es) : NULL;
    Obj go = es && m_sel_go ? CALL(m_sel_go, Obj, Obj)(es, m_sel_go) : NULL;
    static void *m_go_tr; if (!m_go_tr) m_go_tr = mth("UnityEngine", "GameObject", "get_transform", 0);
    Obj tr = go && m_go_tr ? CALL(m_go_tr, Obj, Obj)(go, m_go_tr) : NULL;
    Obj f = tr ? comp_of(tr, k_input) : NULL;
    return f && m_in_focused && CALL(m_in_focused, int8_t, Obj)(f, m_in_focused);
}

/* ---------------------------------------------------------------- test registry: named controls */
typedef struct { char name[80]; DSLObj w; dsl_ui_cb cb; void *ud; int kind; char text[160]; } Reg;   /* kind 0 button, 1 toggle, 2 field, 3 text */
#define NREG 256
static Reg reg[NREG]; static int nreg;
static DSLObj named(DSLObj w, int kind, const char *name, dsl_ui_cb cb, void *ud, const char *text) {
    if (!w || nreg >= NREG) return w;
    Reg *r = &reg[nreg++]; memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", name); r->w = w; r->cb = cb; r->ud = ud; r->kind = kind;
    snprintf(r->text, sizeof r->text, "%s", text ? text : "");
    return w;
}
static void set_text(DSLObj w, const char *s) {
    U->set_text(w, s);
    for (int i = 0; i < nreg; i++) if (reg[i].w == w) snprintf(reg[i].text, sizeof reg[i].text, "%s", s);
}
static void unreg_all(void) { nreg = 0; }

/* ---------------------------------------------------------------- the Mods tab */
#define NM 64
static struct { const DSLModInfo *m; int on, base; DSLObj row, warn; } mods[NM]; static int nmods;   /* base: running set, or the world's; warn: save warning line (owner item 9) */
static int world_mode; static char world_guid[80], world_name[120];   /* Mods tab bound to one world (Load list "Change") */
static DSLObj m_status, m_apply, m_undo, m_save, m_field, m_fsave, m_fcancel, m_glist, m_gmark, m_gnone;
static DSLObj g_row[32], g_del[32]; static int ng_rows, naming, applying, applying_seen_busy;
static double applying_t;
static char carry_note[300];                                           /* shown when the window opens again (failed Apply) */
static int slogel_files(void);
static void reopen_mark(int on);
static void destroy_all(void);
static char m_note[300];                                               /* last refusal / failure, cleared by a change */
static float m_lw, m_rw, m_rx;

static int changed(void) { for (int i = 0; i < nmods; i++) if (mods[i].on != mods[i].base) return 1; return 0; }
static void draft(void) {                                              /* unapplied changes: the loader prewarms that set */
    if (world_mode || read_only || applying) return;
    if (!changed()) { dsl_mods_draft(NULL, -1); return; }
    const char *ids[NM]; int n = 0;
    for (int i = 0; i < nmods; i++) if (mods[i].on) ids[n++] = mods[i].m->id;
    dsl_mods_draft(ids, n);
}
static int group_is_current(int g) {
    const char *ids[NM]; int n = dsl_group_mods(g, ids, NM);
    for (int i = 0; i < nmods; i++) {
        int in = 0; for (int k = 0; k < n; k++) if (!strcmp(ids[k], mods[i].m->id)) in = 1;
        if (in != mods[i].on) return 0;
    }
    return 1;
}
static void mods_sync(void) {
    for (int i = 0; i < nmods; i++) {
        U->toggle_set(mods[i].row, mods[i].on);
        if (!mods[i].warn) continue;
        U->set_active(mods[i].warn, mods[i].on);                       /* test state: the line's text only while it shows */
        for (int k = 0; k < nreg; k++) if (reg[k].w == mods[i].warn) {
            if (!mods[i].on) reg[k].text[0] = 0;
            else { const char *one = mods[i].m->id; int dsl_save_warning(const char *const *, int, char *, size_t); dsl_save_warning(&one, 1, reg[k].text, sizeof reg[k].text); }
        }
    }
    const char *busy = dsl_busy_reason();
    int ch = changed();
    char s[600];
    if (read_only) snprintf(s, sizeof s, "%s", TR("Go back to the title screen to change mods."));
    else if (applying) snprintf(s, sizeof s, "%s", TR("Applying mods..."));
    else if (m_note[0]) snprintf(s, sizeof s, "%s", m_note);
    else if (busy && ch && !world_mode) snprintf(s, sizeof s, TR("Mods cannot change now: %s"), busy);
    else if (!ch && world_mode) snprintf(s, sizeof s, "%s", TR("Switch mods on or off, then Save. The world loads with these mods."));
    else if (!ch) snprintf(s, sizeof s, "%s", TR("Switch mods on or off, then Apply."));
    else {
        char list[500] = "", one[200], t[500]; int k = 0;
        for (int i = 0; i < nmods; i++) if (mods[i].on != mods[i].base) {
            snprintf(one, sizeof one, mods[i].on ? TR("%s on") : TR("%s off"), mods[i].m->name);
            if (k++) { snprintf(t, sizeof t, TR("%s, %s"), list, one); snprintf(list, sizeof list, "%s", t); }
            else snprintf(list, sizeof list, "%s", one);
        }
        snprintf(s, sizeof s, world_mode ? TR("Changes: %s. Save for this world to keep them.") : TR("Changes: %s. Apply to use them."), list);
    }
    dsi_utf8_fix_end(s);
    set_text(m_status, s);
    U->set_enabled(m_apply, ch && (world_mode || !busy) && !applying && !read_only);
    U->set_enabled(m_undo, ch && !applying && !read_only);
    U->set_enabled(m_save, !applying && !read_only);
    for (int i = 0; i < nmods; i++) U->set_enabled(mods[i].row, !applying && !read_only);
    for (int g = 0; g < ng_rows; g++) { U->set_enabled(g_row[g], !read_only); U->set_enabled(g_del[g], !read_only); }
    int cur_g = -1;
    for (int g = 0; g < ng_rows; g++) if (group_is_current(g)) { cur_g = g; break; }
    if (m_gmark) { U->set_active(m_gmark, cur_g >= 0); if (cur_g >= 0) U->place(m_gmark, 0, cur_g * 48.0f + 4, 4, 32); }
}
static void mod_change(void *ud, DSLObj w) {
    int i = (int)(intptr_t)ud;
    mods[i].on = U->toggle_get(w);
    m_note[0] = 0;
    for (int k = 0; k < nreg; k++) if (reg[k].w == w) snprintf(reg[k].text, sizeof reg[k].text, "%s", mods[i].on ? "on" : "off");
    mods_sync(); draft();
}
static void groups_build(void);
static void group_pick(void *ud, DSLObj w) {
    (void)w; int g = (int)(intptr_t)ud;
    const char *ids[NM]; int n = dsl_group_mods(g, ids, NM);
    for (int i = 0; i < nmods; i++) { int in = 0; for (int k = 0; k < n; k++) if (!strcmp(ids[k], mods[i].m->id)) in = 1; mods[i].on = in; }
    m_note[0] = 0;
    dsl_log("win: group %s chosen", dsl_group_name(g));
    mods_sync(); draft();
}
static char del_name[80];
static void del_ok(void *ud, DSLObj w) {
    (void)ud; (void)w;
    if (dsl_group_delete(del_name)) snprintf(m_note, sizeof m_note, "%s", TR("The group could not be deleted."));
    else dsl_log("win: group %s deleted", del_name);
    groups_build(); mods_sync();
}
static void del_click(void *ud, DSLObj w) {
    (void)w; int g = (int)(intptr_t)ud;
    snprintf(del_name, sizeof del_name, "%s", dsl_group_name(g));
    char q[200]; snprintf(q, sizeof q, TR("Delete the group \"%s\"? The mods stay installed."), del_name); dsi_utf8_fix_end(q);
    U->notice(q, TR("Delete"), TR("Cancel"), del_ok, NULL, NULL);
}
static void groups_build(void) {
    for (int g = 0; g < ng_rows; g++) { U->destroy(g_row[g]); U->destroy(g_del[g]); }
    for (int k = 0; k < nreg; k++) if (!strncmp(reg[k].name, "group:", 6) || !strncmp(reg[k].name, "group-del:", 10)) reg[k].w = NULL;
    ng_rows = dsl_groups_count(); if (ng_rows > 32) ng_rows = 32;
    for (int g = 0; g < ng_rows; g++) {
        const char *ids[NM]; int n = dsl_group_mods(g, ids, NM);
        char lb[120], nm[100], tip[600] = ""; size_t o = 0;
        snprintf(lb, sizeof lb, "%s (%d)", dsl_group_name(g), n);
        for (int k = 0; k < n && o < sizeof tip; k++) {
            const char *show = ids[k];
            for (int i = 0; i < nmods; i++) if (!strcmp(mods[i].m->id, ids[k])) show = mods[i].m->name;
            o += (size_t)snprintf(tip + o, sizeof tip - o, "%s%s", k ? ", " : "", show);
        }
        g_row[g] = U->button(m_glist, 10, g * 48.0f, m_rw - 70, 40, lb, group_pick, (void *)(intptr_t)g);
        snprintf(nm, sizeof nm, "group:%s", dsl_group_name(g)); named(g_row[g], 0, nm, group_pick, (void *)(intptr_t)g, lb);
        U->tooltip(g_row[g], dsl_group_name(g), n ? tip : TR("No mods: the game without mods."));
        g_del[g] = U->button(m_glist, m_rw - 54, g * 48.0f, 40, 40, "x", del_click, (void *)(intptr_t)g);
        snprintf(nm, sizeof nm, "group-del:%s", dsl_group_name(g)); named(g_del[g], 0, nm, del_click, (void *)(intptr_t)g, "x");
        U->tooltip(g_del[g], NULL, TR("Delete this group"));
    }
    U->scroll_height(m_glist, ng_rows * 48.0f);
    if (!m_gmark) m_gmark = flat(U->panel(m_glist, 0, 4, 4, 32, 0), GOLD);
    if (m_gnone) U->set_active(m_gnone, ng_rows == 0);
    else U->place(m_gmark, 0, 4, 4, 32);
}
static void naming_show(int on) {
    naming = on;
    U->set_active(m_field, on); U->set_active(m_fsave, on); U->set_active(m_fcancel, on); U->set_active(m_save, !on);
    if (on) { U->field_set(m_field, ""); for (int k = 0; k < nreg; k++) if (reg[k].w == m_field) reg[k].text[0] = 0; }
}
static void save_click(void *ud, DSLObj w) { (void)ud; (void)w; naming_show(1); }
static void fcancel_click(void *ud, DSLObj w) { (void)ud; (void)w; naming_show(0); }
static void fsave_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    char name[80]; U->field_get(m_field, name, sizeof name);
    size_t a = 0, b = strlen(name); while (name[a] == ' ') a++; while (b > a && name[b - 1] == ' ') b--;
    memmove(name, name + a, b - a); name[b - a] = 0;
    if (!name[0]) return;                                              /* no name: the field stays open */
    const char *ids[NM]; int n = 0;
    for (int i = 0; i < nmods; i++) if (mods[i].on) ids[n++] = mods[i].m->id;
    if (dsl_group_save(name, ids, n)) snprintf(m_note, sizeof m_note, "%s", TR("The group could not be saved."));
    else { m_note[0] = 0; dsl_log("win: group %s saved (%d mods)", name, n); }
    naming_show(0);
    groups_build(); mods_sync();
}
/* "Open mods folder": the player's mods folder (Mac: ROOT/mods from the app, Windows: the loader's mods folder) */
static void folder_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    const char *d = getenv("DSML_USER_MODS_DIR"); if (!d || !*d) d = dsl_mods_dir();
    if (!d || !*d) return;
    mkdir_(d); dsl_log("win: open mods folder %s", d);
#ifdef _WIN32
    { wchar_t w[2048]; if (!dsl_u8_to_w(d, w, 2048)) ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL); }
#else
    pid_t pid; int st; char *av[] = { "/usr/bin/open", (char *)d, NULL };
    if (posix_spawn(&pid, av[0], NULL, NULL, av, environ) == 0) waitpid(pid, &st, 0);
#endif
}
/* crash reports (owner 2026-10-18, src/dsl_crash.c): the switch changes the answer of the first-start question */
static void crash_change(void *ud, DSLObj w) { (void)ud; dsl_crash_set_consent(U->toggle_get(w) != 0); }
static void privacy_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    const char *u = dsl_crash_privacy_url(); dsl_log("win: open the privacy note %s", u);
#ifdef _WIN32
    { wchar_t wu[512]; if (!dsl_u8_to_w(u, wu, 512)) ShellExecuteW(NULL, L"open", wu, NULL, NULL, SW_SHOWNORMAL); }
#else
    pid_t pid; int st; char *av[] = { "/usr/bin/open", (char *)u, NULL };
    if (posix_spawn(&pid, av[0], NULL, NULL, av, environ) == 0) waitpid(pid, &st, 0);
#endif
}
static void undo_click(void *ud, DSLObj w) { (void)ud; (void)w; for (int i = 0; i < nmods; i++) mods[i].on = mods[i].base; m_note[0] = 0; mods_sync(); draft(); }
static void apply_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    if (!changed() || applying || read_only) return;
    const char *ids[NM]; int n = 0;
    for (int i = 0; i < nmods; i++) if (mods[i].on) ids[n++] = mods[i].m->id;
    if (world_mode) {                                                  /* no restart: the world remembers its set */
        if (dsl_world_set_mods(world_guid, ids, n)) snprintf(m_note, sizeof m_note, "%s", TR("The mods for this world could not be saved."));
        else { for (int i = 0; i < nmods; i++) mods[i].base = mods[i].on; snprintf(m_note, sizeof m_note, world_name[0] ? TR("Saved. %s loads with these mods.") : TR("Saved. This world loads with these mods."), world_name); dsi_utf8_fix_end(m_note); }
        dsl_log("win: world %s: %d mods saved", world_guid, n);
        mods_sync(); return;
    }
    char why[300] = "";
    if (dsl_apply_mods(ids, n, why, sizeof why)) { snprintf(m_note, sizeof m_note, TR("The mods did not change: %s"), why[0] ? why : TR("the game did not allow the change")); dsi_utf8_fix_end(m_note); mods_sync(); return; }
    applying = 1; applying_seen_busy = 0; applying_t = now_s(); m_note[0] = 0;
    dsl_log("win: Apply: %d mods, soft restart started: the window closes (the loading screen covers the screen)", n);
    reopen_mark(1);
    destroy_all();                                                     /* the game's loading screen must be on top */
}
/* new mod packages (ui/dsl_pkg.c): looked for when the Mods tab opens and with "Refresh"; switches the player already
   changed are kept across the rebuild */
int dsl_pkg_refresh(char *fail, size_t fcap);
static char keep_ids[NM][64]; static int keep_on[NM], nkeep = -1;
static void mods_tab_reload(void);
static void refresh_note(int n, const char *fail) {
    if (fail[0]) snprintf(m_note, sizeof m_note, "%s", fail);
    else if (n > 0) snprintf(m_note, sizeof m_note, TRN("Found %d new mod. Turn it on, then click Apply.", "Found %d new mods. Turn them on, then click Apply.", n), n);
}
static void refresh_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    nkeep = 0; for (int i = 0; i < nmods && nkeep < NM; i++) { snprintf(keep_ids[nkeep], 64, "%s", mods[i].m->id); keep_on[nkeep++] = mods[i].on; }
    char fail[400]; int n = dsl_pkg_refresh(fail, sizeof fail);
    dsl_log("win: Refresh: %d new or changed mod(s)%s%s", n < 0 ? 0 : n, fail[0] ? "; " : "", fail);
    mods_tab_reload();
    if (n == 0 && !fail[0]) snprintf(m_note, sizeof m_note, "%s", TR("The mods folder has no new mods."));
    else refresh_note(n, fail);
    mods_sync();
}
/* byte index where the text passes `cols` columns (a CJK / Hangul / fullwidth character = 2, others 1); strlen if never */
static int wide_at(const unsigned char *p) { return (p[0] >= 0xE3 && p[0] <= 0xED) || (p[0] == 0xEF && (p[1] == 0xBC || p[1] == 0xBD)); }
static size_t cols_at(const char *s, int cols) {
    const unsigned char *p = (const unsigned char *)s; int c = 0;
    while (*p) { int n = *p < 0x80 ? 1 : *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : *p >= 0xC0 ? 2 : 1, wd = wide_at(p) ? 2 : 1;
                 if (c + wd > cols) break; c += wd; for (int k = 0; k < n && *p; k++) p++; }
    return (size_t)((const char *)p - s);
}
static int has_wide(const char *s) { for (const unsigned char *p = (const unsigned char *)s; *p; p++) if (wide_at(p)) return 1; return 0; }
static void mods_open(DSLObj root, float w, float h) {
    nmods = 0; naming = 0; ng_rows = 0; m_gmark = m_gnone = NULL;
    read_only = dsl_reg_scene() == 2;
    snprintf(m_note, sizeof m_note, "%s", carry_note); carry_note[0] = 0;
    if (!world_mode && nkeep < 0) { char fail[400]; int n = dsl_pkg_refresh(fail, sizeof fail); if (n || fail[0]) refresh_note(n, fail); }
    for (int i = 0; i < dsl_mods_count() && nmods < NM; i++) {
        const DSLModInfo *m = dsl_mods_at(i);
        if (!m || m->hidden) continue;
        mods[nmods].m = m; mods[nmods].base = m->enabled != 0; mods[nmods].row = mods[nmods].warn = NULL; nmods++;
    }
    if (world_mode) {                                                  /* gameplay mods: the world's set; client mods stay as they run */
        const char *wid[NM]; int wn = dsl_world_mods(world_guid, wid, NM);
        for (int i = 0; i < nmods; i++) {
            if (mods[i].m->scope && !strcmp(mods[i].m->scope, "client")) continue;
            int in = 0; for (int k = 0; k < wn; k++) if (!strcmp(wid[k], mods[i].m->id)) in = 1;
            mods[i].base = in;
        }
    }
    for (int i = 0; i < nmods; i++) mods[i].on = mods[i].base;
    if (nkeep >= 0) {                                                  /* after Refresh: the switches as the player left them */
        for (int i = 0; i < nmods; i++) for (int k = 0; k < nkeep; k++) if (!strcmp(keep_ids[k], mods[i].m->id)) mods[i].on = keep_on[k];
        nkeep = -1;
    }
    m_lw = 620; m_rx = 650; m_rw = w - m_rx;
    float lh = h - 40 - 76;
    /* the world's name, cut to fit the heading box (620 wide, size 20: about 40 characters with "Mods for ") */
    char wn[160]; snprintf(wn, sizeof wn, "%s", world_name[0] ? world_name : TR("this world"));
    { int c = 0; for (char *p = wn; *p; p++) if ((*p & 0xC0) != 0x80 && ++c > 30) { *p = 0; strcat(wn, "..."); break; } }
    char hd[200]; if (world_mode) snprintf(hd, sizeof hd, TR("Mods for %s"), wn); else snprintf(hd, sizeof hd, "%s", TR("Installed mods")); dsi_utf8_fix_end(hd);
    named(U->text(root, 0, 0, m_lw, 32, hd, 20, DSL_TXT_LEFT | DSL_TXT_TITLE, GOLD), 3, "heading", NULL, NULL, hd);
    if (!world_mode) {
        DSLObj of = named(U->button(root, m_lw - 200, 0, 200, 34, TR("Open mods folder"), folder_click, NULL), 0, "mods-folder", folder_click, NULL, TR("Open mods folder"));
        U->tooltip(of, TR("Open mods folder"), TR("Put the mod's .zip file in this folder. Click Refresh to add it to this list. Then turn it on and click Apply."));
        DSLObj rf = named(U->button(root, m_lw - 316, 0, 108, 34, TR("Refresh"), refresh_click, NULL), 0, "mods-refresh", refresh_click, NULL, TR("Refresh"));
        U->tooltip(rf, TR("Refresh"), TR("Check the mods folder for new mods."));
    }
    U->panel(root, 0, 40, m_lw, lh, 0);
    DSLObj list = U->scroll(root, 8, 48, m_lw - 16, lh - 16);
    float y = 0, rh = 74;
    for (int i = 0; i < nmods; i++, y += rh) {
        rh = 74;
        const DSLModInfo *m = mods[i].m;
        char lb[160], nm[100];
        snprintf(lb, sizeof lb, m->version && m->version[0] ? "%s  (v%s)" : "%s", m->name, m->version);
        mods[i].row = U->toggle(list, 0, y, m_lw - 32, lb, mods[i].on, mod_change, (void *)(intptr_t)i);
        snprintf(nm, sizeof nm, "mod:%s", m->id); named(mods[i].row, 1, nm, mod_change, (void *)(intptr_t)i, mods[i].on ? "on" : "off");
        int bad = m->enabled && m->active != 1 && m->reason && m->reason[0];
        char d[400];
        if (bad) snprintf(d, sizeof d, TR("Not running: %s"), TR(m->reason));   /* reason: an English key (or a starter's text) */
        else snprintf(d, sizeof d, "%s", m->description ? m->description : "");
        /* two lines; all in the tooltip. Counted in columns, not bytes (a Chinese / Japanese / Korean character is two
           columns and three bytes, a Russian letter one column and two bytes): ru and CJK were cut at half the width */
        int cut = 0;
        if (cols_at(d, 150) < strlen(d)) { size_t hi = cols_at(d, 146), lo = cols_at(d, 100), c = hi;
                                           while (c > lo && d[c] != ' ') c--;
                                           if (c == lo && d[c] != ' ' && has_wide(d)) c = hi;   /* no space (CJK): at the column */
                                           while (c > 0 && ((unsigned char)d[c] & 0xC0) == 0x80) c--;   /* never inside a UTF-8 character */
                                           snprintf(d + c, sizeof d - c, " ..."); cut = 1; }
        /* owner: BepInEx plugins with a small mark; MelonLoader mods (kind "melon") the same way */
        const char *mk = !m->kind ? NULL : !strcmp(m->kind, "bepinex") ? "BepInEx" : !strcmp(m->kind, "melon") ? "MelonLoader" : NULL;
        float mw = mk && mk[0] == 'M' ? 100 : 64;
        if (mk) { snprintf(nm, sizeof nm, "mark:%s", m->id); named(U->text(list, 12, y + 38, mw, 18, mk, 13, DSL_TXT_LEFT, GOLD), 3, nm, NULL, NULL, mk); }
        float dx = mk ? 16 + mw : 12, dw = m_lw - (mk ? 60 + mw : 56), dh = rh - 40;
        DSLObj t = U->text(list, dx, y + 38, dw, dh, d, 14, DSL_TXT_LEFT | DSL_TXT_WRAP, bad ? WARN : SUB);
        /* the game's way for long text: the box grows to the text (two CJK lines are taller than two Lora lines) */
        { float ph = t && U->text_height ? U->text_height(t, dw) : 0; if (ph > dh + 2) { dh = ph; U->place(t, dx, y + 38, dw, dh); rh = 40 + dh; } }
        snprintf(nm, sizeof nm, "desc:%s", m->id); named(t, 3, nm, NULL, NULL, d);
        if (cut && m->description) U->tooltip(mods[i].row, m->name, m->description);
        /* owner item 9: a mod whose saves need the loader (mod.json "save_dependent"): one orange line under the
           description while it is switched on */
        int dsl_save_warning(const char *const *ids, int n, char *out, size_t cap);
        char wr[300]; const char *one = m->id;
        if (dsl_save_warning(&one, 1, wr, sizeof wr)) {
            float wh = 20;                                            /* one line; wraps and grows when longer (de / fr / ru) */
            mods[i].warn = U->text(list, 12, y + rh, m_lw - 56, wh, wr, 14, DSL_TXT_LEFT | DSL_TXT_WRAP, WARN);
            { float ph = mods[i].warn && U->text_height ? U->text_height(mods[i].warn, m_lw - 56) : 0; if (ph > wh + 2) { wh = ph; U->place(mods[i].warn, 12, y + rh, m_lw - 56, wh); } }
            snprintf(nm, sizeof nm, "savewarn:%s", m->id); named(mods[i].warn, 3, nm, NULL, NULL, wr);
            rh += wh + 2;
        }
    }
    if (!nmods) U->text(list, 12, 8, m_lw - 56, 40, TR("No mods are installed."), 16, DSL_TXT_LEFT, SUB);
    U->scroll_height(list, y);
    /* groups */
    U->text(root, m_rx, 0, m_rw, 32, TR("Groups"), 20, DSL_TXT_LEFT | DSL_TXT_TITLE, GOLD);
    m_save = named(U->button(root, m_rx, 40, m_rw, 40, TR("Save these mods as a group"), save_click, NULL), 0, "group-new", save_click, NULL, TR("Save these mods as a group"));
    m_field = named(U->field(root, m_rx, 40, m_rw - 184, 40, TR("Group name"), "", fsave_click, NULL), 2, "group-name", fsave_click, NULL, "");
    m_fsave = named(U->button(root, m_rx + m_rw - 176, 40, 84, 40, TR("Save"), fsave_click, NULL), 0, "group-save", fsave_click, NULL, TR("Save"));
    m_fcancel = named(U->button(root, m_rx + m_rw - 84, 40, 84, 40, TR("Cancel"), fcancel_click, NULL), 0, "group-cancel", fcancel_click, NULL, TR("Cancel"));
    naming_show(0);
    m_gnone = U->text(root, m_rx + 10, 100, m_rw - 20, 60, TR("No groups yet. Set the switches, then save them as a group."), 15, DSL_TXT_LEFT | DSL_TXT_WRAP, SUB);
    int crash = !world_mode && dsl_crash_consent() > -2;               /* crash reports: one row above the bottom line */
    m_glist = U->scroll(root, m_rx, 92, m_rw, h - 92 - 76 - (crash ? 54 : 0));
    groups_build();
    if (crash) {
        DSLObj ct = named(U->toggle(root, m_rx, h - 118, m_rw - 116, TR("Send crash reports"), dsl_crash_consent() == 1, crash_change, NULL), 1,
                          "crash-reports", crash_change, NULL, dsl_crash_consent() == 1 ? "on" : "off");
        U->tooltip(ct, TR("Send crash reports"), TR("After a crash, a freeze, a desync or a failed join, the next start sends the logs and versions, without names or IP addresses. At most 3 a day."));
        DSLObj pb = named(U->button(root, m_rx + m_rw - 108, h - 118, 108, 40, TR("Privacy"), privacy_click, NULL), 0, "crash-privacy", privacy_click, NULL, TR("Privacy"));
        U->tooltip(pb, TR("Privacy"), TR("Open the privacy note in your browser."));
    }
    /* bottom */
    flat(U->panel(root, 0, h - 66, w, 1, 0), 0x633D33FF);
    m_status = named(U->text(root, 0, h - 58, m_lw, 58, "", 15, DSL_TXT_LEFT | DSL_TXT_WRAP, SUB), 3, "status", NULL, NULL, "");
    m_undo = named(U->button(root, m_rx, h - 52, 134, 44, TR("Undo changes"), undo_click, NULL), 0, "undo", undo_click, NULL, TR("Undo changes"));
    const char *al = world_mode ? TR("Save for this world") : TR("Apply");
    m_apply = named(U->button(root, w - 200, h - 52, 200, 44, al, apply_click, NULL), 0, "apply", apply_click, NULL, al);
    if (read_only) { U->set_active(m_apply, 0); U->set_active(m_undo, 0); U->set_active(m_save, 0); }
    else if (world_mode) U->tooltip(m_apply, al, TR("This world loads with these mods. The mods running now do not change."));
    else U->tooltip(m_apply, al, TR("Turns these mods on behind a loading screen and brings you back here."));
    mods_sync();
}
static void mods_update(void) {
    static const char *busy_last = (const char *)1;
    const char *b = dsl_busy_reason();
    if (b != busy_last) { busy_last = b; mods_sync(); }
}
/* after Apply the window is closed; this process only lives on if the soft restart failed: say so in the window */
static void apply_watch(void) {
    if (!applying) return;
    const char *b = dsl_busy_reason();
    if (b) applying_seen_busy = 1;
    if (!b && (applying_seen_busy || now_s() - applying_t > 5)) {
        applying = 0;
        char why[240] = "", fp[1200]; const char *ld = getenv("DSL_LOG_DIR");          /* the loader writes the reason there */
        if (ld) { snprintf(fp, sizeof fp, "%s/softrestart/handoff.json.failed", ld);
                  FILE *f = fopen(fp, "r"); if (f) { size_t n = fread(why, 1, sizeof why - 1, f); why[n] = 0; fclose(f);
                                                       while (n && (why[n - 1] == '\n' || why[n - 1] == '\r')) why[--n] = 0; } }
        snprintf(carry_note, sizeof carry_note, TR("The mods did not change: %s"), why[0] ? TR(why) : TR("the mods could not be applied."));
        dsi_utf8_fix_end(carry_note);
        dsl_log("win: the soft restart ended here without a swap: window opens with the reason");
        reopen_mark(0);
        dsl_win_open("mods");
    }
}

/* ---------------------------------------------------------------- window, tabs */
static void tab_close(void); static void tab_select(int t);
static void mods_tab_reload(void) { tab_close(); tab_select(0); }
static const DSLTab *tab_at(int t) { return t > 0 ? dsl_tab_at(t - 1) : NULL; }     /* 0 = Mods, 1.. = module tabs */
static void tab_close(void) {
    if (cur < 0) return;
    if (cur > 0) { const DSLTab *t = tab_at(cur); if (t && t->close) t->close(t->ud); }
    U->destroy(w_body); w_body = NULL;
    for (int k = 0; k < nreg; k++) if (strncmp(reg[k].name, "tab:", 4) && strcmp(reg[k].name, "close")) reg[k].w = NULL;
    cur = -1;
}
static int tab_can_close(void) {
    if (cur == 0) return !applying;
    const DSLTab *t = tab_at(cur);
    return !t || !t->can_close || t->can_close(t->ud);
}
static void tab_select(int t) {
    if (t < 0 || t >= ntabs_shown || t == cur) return;
    if (cur >= 0 && !tab_can_close()) { dsl_log("win: the open tab keeps the focus"); return; }
    tab_close();
    float bw = WIN_W - 48, bh = WIN_H - BODY_Y - 24;
    w_body = U->panel(w_win, 24, BODY_Y, bw, bh, 0xFFFFFF00);         /* invisible holder: everything of the tab */
    cur = t;
    U->place(w_mark, 24 + t * (TAB_W + 8), TAB_Y + TAB_H + 2, TAB_W, 3);
    if (t == 0) mods_open(w_body, bw, bh);
    else { const DSLTab *x = tab_at(t); if (x && x->open) x->open(x->ud, w_body, bw, bh); }
    dsl_log("win: tab %s open", t == 0 ? "mods" : tab_at(t) ? tab_at(t)->id : "?");
}
static void tab_click(void *ud, DSLObj w) { (void)w; tab_select((int)(intptr_t)ud); }
static void close_click(void *ud, DSLObj w) { (void)ud; (void)w; dsl_win_close(); }

static int slogel_files(void) {                                       /* the idle frames as files (image_png reads files) */
    const char *p = getenv("DSL_PROFILE");
    const char *s = p ? strrchr(p, '/') : NULL, *b = p ? strrchr(p, '\\') : NULL;
    if (b > s) s = b;
    if (!s) return -1;
    snprintf(slogel_dir, sizeof slogel_dir, "%.*s/.cache", (int)(s - p), p);
    mkdir_(slogel_dir);
    for (int k = 0; k < SLOGEL_FRAMES; k++) {
        char f[1200], tmp[1300]; struct stat st;
        snprintf(f, sizeof f, "%s/mudslogel_idle_%d.png", slogel_dir, k);
        if (!stat(f, &st) && st.st_size == (off_t)slogel_frames[k].len) continue;
        snprintf(tmp, sizeof tmp, "%s.tmp", f);
        FILE *o = fopen(tmp, "wb"); if (!o) return -1;
        fwrite(slogel_frames[k].png, 1, slogel_frames[k].len, o); fclose(o); rename(tmp, f);
    }
    return 0;
}
static void build(void) {
    unreg_all();
    w_back = flat(U->panel(NULL, 0, 0, 100, 100, 0), 0x0000009E);       /* dims the game and takes its clicks */
    fill_parent(w_back);
    w_win = U->panel(NULL, 0, 0, WIN_W, WIN_H, 0);
    centre(w_win);
    flat(U->panel(w_win, 4, 4, WIN_W - 8, WIN_H - 8, 0), 0x222025FF);      /* one body colour (the frame sprite's inside has a band) */
    flat(U->panel(w_win, 4, 4, WIN_W - 8, BAND_H - 4, 0), 0x2D2A31FF);
    flat(U->panel(w_win, 4, BAND_H, WIN_W - 8, 1, 0), 0xB6835AFF);
    memset(w_slogel, 0, sizeof w_slogel); slogel_shown = -1;
    if (!slogel_files())
        for (int k = 0; k < SLOGEL_FRAMES; k++) {
            char f[1200]; snprintf(f, sizeof f, "%s/mudslogel_idle_%d.png", slogel_dir, k);
            w_slogel[k] = U->image_png(w_win, 24, 10, SLOGEL_W * 2, SLOGEL_H * 2, f, 1);
            if (w_slogel[k]) U->set_active(w_slogel[k], 0);
        }
    named(U->text(w_win, 104, 6, 700, BAND_H - 6, "Dungeon Settlers Modded", 32, DSL_TXT_LEFT | DSL_TXT_TITLE, 0), 3, "title", NULL, NULL, "Dungeon Settlers Modded");
    DSLObj x = named(U->button(w_win, WIN_W - 66, 24, 42, 42, "X", close_click, NULL), 0, "close", close_click, NULL, "X");
    U->tooltip(x, NULL, TR("Close (Esc)"));
    ntabs_shown = 1;                                                   /* nothing but mods in this window (owner 2026-10-16 #14) */
    if (dsl_tab_count()) dsl_log("win: %d module tab(s) not shown: the Mods window shows only mods", dsl_tab_count());
    for (int t = 0; t < ntabs_shown; t++) {
        const char *title = t == 0 ? TR("Mods") : tab_at(t)->title, *id = t == 0 ? "mods" : tab_at(t)->id;
        char nm[80]; snprintf(nm, sizeof nm, "tab:%s", id);
        w_tab[t] = named(U->button(w_win, 24 + t * (TAB_W + 8), TAB_Y, TAB_W, TAB_H, title, tab_click, (void *)(intptr_t)t), 0, nm, tab_click, (void *)(intptr_t)t, title);
    }
    flat(U->panel(w_win, 24, TAB_Y + TAB_H + 7, WIN_W - 48, 1, 0), 0x633D33FF);
    w_mark = flat(U->panel(w_win, 24, TAB_Y + TAB_H + 3, TAB_W, 3, 0), GOLD);   /* under the open tab */
}
static void destroy_all(void) {
    tab_close();
    U->destroy(w_win); U->destroy(w_back);
    w_win = w_back = w_mark = NULL; memset(w_tab, 0, sizeof w_tab);
    unreg_all();
    is_open = 0;
}
static int open_tries, reopen_wait; static double open_retry_at;
static void open_now(void) {
    want_open = 0;
    if (!U) U = dsl_ui_kit();
    if (!U) { dsl_log("win: no widget kit: the Mods window cannot open"); return; }
    if (!is_open) {
        build(); is_open = w_win != NULL;
        if (!is_open) {                                                /* the kit's templates come with the title screen: try again */
            destroy_all();
            if (++open_tries < 120) { want_open = 1; open_retry_at = now_s() + 0.5; }   /* up to a minute */
            else { dsl_log("win: the window could not be built (%d tries)", open_tries); open_tries = 0; }
            return;
        }
        open_tries = 0;
        dsl_log("win: Mods window open");
    }
    int t = 0;
    for (int i = 1; i < ntabs_shown; i++) if (tab_at(i) && !strcmp(tab_at(i)->id, want_tab)) t = i;
    tab_select(t);
}

/* "put people back exactly where they were" (owner): Apply came from this window, so the new game opens it again */
static void reopen_path(char *out, size_t n) { slogel_files(); snprintf(out, n, "%s/reopen_mods_window", slogel_dir); }
static void reopen_mark(int on) {
    char p[1300], used[1400]; reopen_path(p, sizeof p);
    if (on) { FILE *f = fopen(p, "w"); if (f) { fprintf(f, "%ld\n", (long)time(NULL)); fclose(f); } }
    else { snprintf(used, sizeof used, "%s.used", p); rename(p, used); }
}
void dsl_win_init(void) {
    unity_init(); U = dsl_ui_kit();
    char p[1300]; reopen_path(p, sizeof p);
    FILE *f = fopen(p, "r"); long t = 0;
    if (f) { if (fscanf(f, "%ld", &t) != 1) t = 0; fclose(f); }
    int reopen = t && time(NULL) - t < 180;
    if (f) reopen_mark(0);
    reopen_wait = reopen;                                              /* opens once the restart is over (dsl_busy_reason NULL) */
    dsl_log("win: Mods window ready (%s)%s", U ? "kit ok" : "no kit yet", reopen ? "; opens again after the Apply restart" : "");
}
static void rebind(int wm) {                                          /* the Mods tab changes its meaning: build it again */
    if (wm != world_mode && is_open && cur == 0) { world_mode = wm; tab_close(); return; }
    world_mode = wm;
}
void dsl_win_open(const char *tab_id) { rebind(0); snprintf(want_tab, sizeof want_tab, "%s", tab_id ? tab_id : "mods"); want_open = 1; }
void dsl_win_open_world(const char *guid, const char *name) {
    if (!guid || !*guid) { dsl_win_open(NULL); return; }
    int same = world_mode && !strcmp(world_guid, guid);
    if (!same && is_open && cur == 0) tab_close();
    snprintf(world_guid, sizeof world_guid, "%s", guid); snprintf(world_name, sizeof world_name, "%s", name ? name : "");
    world_mode = 1;
    snprintf(want_tab, sizeof want_tab, "mods"); want_open = 1;
    dsl_log("win: Mods for world %s (%s)", guid, world_name);
}
int dsl_win_is_open(void) { return is_open; }
/* the game's language changed (only at the title, from Options): an open window is built again in the new language,
   with the switches as the player left them */
void dsl_win_relang(void) {
    if (!is_open) return;
    if (cur == 0) { nkeep = 0; for (int i = 0; i < nmods && nkeep < NM; i++) { snprintf(keep_ids[nkeep], 64, "%s", mods[i].m->id); keep_on[nkeep++] = mods[i].on; } }
    destroy_all();
    snprintf(want_tab, sizeof want_tab, "mods"); want_open = 1;
    dsl_log("win: language %s: the Mods window is built again", dsi_code());
}
void dsl_win_close(void) {
    if (!is_open) return;
    if (!tab_can_close()) { dsl_log("win: the open tab does not close now"); return; }
    if (!applying) dsl_mods_draft(NULL, -1);                           /* closed without Apply: the prewarmed game quits */
    destroy_all();
    dsl_log("win: Mods window closed");
}

/* test only (dsl_reg.c test requests): a named control of the open window */
DSLObj dsl_win_test_control(const char *name) { for (int i = 0; i < nreg; i++) if (reg[i].w && !strcmp(reg[i].name, name)) return reg[i].w; return NULL; }

/* ---------------------------------------------------------------- test driver */
static const char *test_dir; static int test_checked;
static void test_answer(const char *seq, int ok, const char *msg, int dump) {
    static char out[65536];
    size_t o = (size_t)snprintf(out, sizeof out, "{\"seq\":\"%s\",\"ok\":%d,\"msg\":\"%s\",\"open\":%d,\"tab\":%d,\"busy\":\"%s\"", seq, ok, msg, is_open, cur,
                                dsl_busy_reason() ? "yes" : "");
    if (dump) {
        o += (size_t)snprintf(out + o, sizeof out - o, ",\"controls\":[");
        for (int i = 0, k = 0; i < nreg && o + 400 < sizeof out; i++) {
            if (!reg[i].w) continue;
            char t[200]; size_t j = 0; for (const char *c = reg[i].text; *c && j + 3 < sizeof t; c++) { if (*c == '"' || *c == '\\') t[j++] = '\\'; t[j++] = *c; } t[j] = 0;
            int on = reg[i].kind == 1 ? U->toggle_get(reg[i].w) : -1;
            o += (size_t)snprintf(out + o, sizeof out - o, "%s{\"name\":\"%s\",\"kind\":%d,\"text\":\"%s\",\"on\":%d}", k++ ? "," : "", reg[i].name, reg[i].kind, t, on);
        }
        o += (size_t)snprintf(out + o, sizeof out - o, "]");
    }
    snprintf(out + o, sizeof out - o, "}\n");
    char p[1200], tmp[1300]; snprintf(p, sizeof p, "%s/result.json", test_dir); snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "w"); if (f) { fputs(out, f); fclose(f); rename(tmp, p); }
}
static int shot(const char *path) {
    if (!m_shot) { m_shot = mth("UnityEngine", "ScreenCapture", "CaptureScreenshotAsTexture", 0); m_png = mth("UnityEngine", "ImageConversion", "EncodeToPNG", 1);
                   m_destroy = mth("UnityEngine", "Object", "Destroy", 1); }
    if (!m_shot || !m_png) return -1;
    Obj tex = CALL0(m_shot, Obj)(m_shot); if (!tex) return -1;
    Obj png = CALL(m_png, Obj, Obj)(tex, m_png);
    if (m_destroy) CALL(m_destroy, void, Obj)(tex, m_destroy);
    if (!png) return -1;
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb"); if (!f) return -1;
    fwrite((char *)png + 0x20, 1, (size_t)*(uintptr_t *)((char *)png + 0x18), f); fclose(f);
    return rename(tmp, path);
}
static void test_frame(void) {
    char p[1200]; snprintf(p, sizeof p, "%s/cmd.json", test_dir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char buf[4096]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    char done[1300]; snprintf(done, sizeof done, "%s/cmd.done.json", test_dir); rename(p, done);
    JNode *j = json_parse(buf);
    const char *op = json_str(j, "op"), *seq = json_str(j, "seq") ? json_str(j, "seq") : "", *target = json_str(j, "target");
    char msg[300] = ""; int ok = 1, dump = 0;
    Reg *r = NULL; if (target) for (int i = 0; i < nreg; i++) if (reg[i].w && !strcmp(reg[i].name, target)) r = &reg[i];
    if (!op) { ok = 0; snprintf(msg, sizeof msg, "bad command"); }
    else if (!strcmp(op, "open")) { dsl_win_open(json_str(j, "tab")); open_now(); }
    else if (!strcmp(op, "close")) dsl_win_close();
    else if (!strcmp(op, "open_world")) { dsl_win_open_world(json_str(j, "guid"), json_str(j, "world")); open_now(); }
    else if (!strcmp(op, "state")) dump = 1;
    else if (!strcmp(op, "shot")) { char sp[1300]; snprintf(sp, sizeof sp, "%s/%s.png", test_dir, json_str(j, "name") ? json_str(j, "name") : "shot"); ok = shot(sp) == 0; snprintf(msg, sizeof msg, "%s", sp); }
    else if (!strcmp(op, "click") || !strcmp(op, "type")) {
        if (!r) { ok = 0; snprintf(msg, sizeof msg, "no control %s", target ? target : ""); }
        else if (!strcmp(op, "type")) { U->field_set(r->w, json_str(j, "text") ? json_str(j, "text") : ""); snprintf(r->text, sizeof r->text, "%s", json_str(j, "text") ? json_str(j, "text") : ""); }
        else {
            if (r->kind == 1) U->toggle_set(r->w, !U->toggle_get(r->w));    /* the kit flips a toggle before its callback */
            if (r->cb) r->cb(r->ud, r->w);
        }
    } else if (!strcmp(op, "esc")) { if (is_open && !naming) dsl_win_close(); else if (naming) naming_show(0); }
    else { ok = 0; snprintf(msg, sizeof msg, "unknown op %s", op); }
    test_answer(seq, ok, msg, dump);
    json_free(j);
}

void dsl_win_frame(void) {
    if (!test_checked) { test_checked = 1; test_dir = getenv("DSL_UI_TEST"); if (test_dir && !*test_dir) test_dir = NULL; }
    apply_watch();
    if (reopen_wait && !dsl_busy_reason()) { reopen_wait = 0; dsl_win_open("mods"); }
    if (want_open && now_s() >= open_retry_at) open_now();
    if (is_open) {
        int f = (int)(now_s() * 6) % SLOGEL_FRAMES;                   /* Mud Slogel idle, 6 frames per second */
        if (f != slogel_shown && w_slogel[f]) { if (slogel_shown >= 0 && w_slogel[slogel_shown]) U->set_active(w_slogel[slogel_shown], 0); U->set_active(w_slogel[f], 1); slogel_shown = f; }
        if (cur == 0) mods_update();
        else if (cur > 0) { const DSLTab *t = tab_at(cur); if (t && t->update) t->update(t->ud); }
        if (esc_down() && !typing()) { if (naming) naming_show(0); else dsl_win_close(); }
    }
    if (test_dir && !dsl_soft_wanted()) { static int k; if (++k % 3 == 0) test_frame(); }   /* a hidden B (prewarm) leaves the commands to A */
}
