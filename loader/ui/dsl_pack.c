// Dungeon Settlers mod loader: the "Import a mod pack" window (owner 2026-11-02, PLAN_v013 item 36).
//
// Opened from the Mods window's Groups header (Import). The player pastes a mod pack code a friend sent (the Mods
// window's Share makes one); the window shows the plan of src/dsl_hostmods.c (dsl_pack_plan: what is downloaded,
// updated, on or off in the group, refusals) and one button. That button starts dsl_pack_import: the ONE "get these
// mods" helper (official signed releases only, sha256 checked, every change rolled back on a failure). Its end saves a
// group; nothing is switched: the player picks the group and clicks Apply, as for any group.
// Window rules: kit window_std (dim backdrop, header, red X, Escape = Cancel, UI Scale); closes with the Mods window.
// Clipboard: the player's clipboard. In a test copy (DSL_UI_TEST set) only the file DSL_PACK_CLIP, never the clipboard.
// Test controls (the Mods window's registry, dsl_win_named): pack-code (field), pack-paste, pack-go, pack-cancel,
// pack-status, pack-head, pack-row:<id>.
#include "../src/dsl_internal.h"
#include "../src/dsl_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WIN_W 900.0f
#define WIN_H 680.0f
#define L 32.0f
#define CODE_MAX 24000
#define GOLD 0xD9BE8FFFu
#define SUB 0xA69085FFu
#define WARN 0xEA9F3DFFu

static const DSLUi *U;
DSLObj dsl_win_named(DSLObj w, int kind, const char *name, dsl_ui_cb cb, void *ud, const char *text);
void dsl_win_named_text(DSLObj w, const char *text);
void dsl_win_named_drop(const char *prefix);

static int is_open, started, done, plan_ok, downloads, names_tries;
static DSLObj w_win, w_code, w_head, w_list, w_status, w_go, w_cancel, w_paste;
static DSLObj w_rows[128]; static int nrows;
static char last_code[CODE_MAX + 1], group_s[80], status_s[800];
static double next_plan_at;

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

/* ---------------------------------------------------------------- clipboard (a test copy: a file) */
static const char *clip_file(int *test) {
    const char *f = getenv("DSL_PACK_CLIP"), *t = getenv("DSL_UI_TEST");
    *test = (f && *f) || (t && *t);
    return f && *f ? f : NULL;
}
void dsl_pack_clip_set(const char *s) {
    int test; const char *f = clip_file(&test);
    if (!test) { if (!U) U = dsl_ui_kit(); if (U) U->clipboard_set(s); return; }
    if (!f) { dsl_log("pack: test copy without DSL_PACK_CLIP: the clipboard is not touched"); return; }
    FILE *o = fopen(f, "wb"); if (o) { fputs(s, o); fclose(o); }
}
size_t dsl_pack_clip_get(char *out, size_t cap) {
    if (!out || !cap) return 0;
    out[0] = 0;
    int test; const char *f = clip_file(&test);
    if (!test) { if (!U) U = dsl_ui_kit(); return U ? U->clipboard_get(out, cap) : 0; }
    FILE *i = f ? fopen(f, "rb") : NULL; if (!i) return 0;
    size_t n = fread(out, 1, cap - 1, i); fclose(i); out[n] = 0;
    return n;
}

/* ---------------------------------------------------------------- the plan */
static void set_status(const char *s) {
    snprintf(status_s, sizeof status_s, "%s", s); dsi_utf8_fix_end(status_s);
    if (w_status) { U->set_text(w_status, status_s); dsl_win_named_text(w_status, status_s); }
}
static void go_label(void) {
    const char *t = done ? TR("Close") : downloads ? TR("Download and save as a group") : TR("Save as a group");
    U->set_text(w_go, t); dsl_win_named_text(w_go, t);
}
static void rows_clear(void) {
    for (int i = 0; i < nrows; i++) U->destroy(w_rows[i]);
    nrows = 0; dsl_win_named_drop("pack-row:");
}
/* one row's text: what the import does with this mod */
static void row_text(JNode *r, char *out, size_t cap, uint32_t *col) {
    const char *a = json_str(r, "action"), *nm = json_str(r, "name"), *hv = json_str(r, "host_ver"), *mv = json_str(r, "my_ver"), *why = json_str(r, "why");
    if (!a) a = ""; if (!nm || !*nm) nm = json_str(r, "id") ? json_str(r, "id") : "?"; if (!hv) hv = ""; if (!mv) mv = ""; if (!why) why = "";
    *col = 0;
    if (!strcmp(a, "download")) { snprintf(out, cap, TR("%s %s: will be downloaded"), nm, hv); *col = GOLD; }
    else if (!strcmp(a, "update") || !strcmp(a, "host_older")) { snprintf(out, cap, TR("%s: version %s will be downloaded (you have %s)"), nm, hv, mv); *col = GOLD; }
    else if (!strcmp(a, "reinstall")) { snprintf(out, cap, TR("%s %s: will be downloaded again (your files are not the official ones)"), nm, hv); *col = GOLD; }
    else if (!strcmp(a, "not_official")) { snprintf(out, cap, TR("%s %s: cannot be downloaded (not one of the mod loader's own mods)"), nm, hv); *col = WARN; }
    else if (!strcmp(a, "not_running")) { snprintf(out, cap, TR("%s: on in the group, but it does not start on this computer: %s"), nm, why); *col = WARN; }
    else if (!strcmp(a, "turn_on")) snprintf(out, cap, TR("%s %s: you have it, on in the group"), nm, hv);
    else if (!strcmp(a, "settings")) snprintf(out, cap, TR("%s %s: you have it, with other settings in the group"), nm, hv);
    else if (!strcmp(a, "turn_off")) { snprintf(out, cap, TR("%s: off in the group"), nm); *col = SUB; }
    else snprintf(out, cap, TR("%s %s: you have it"), nm, hv);
    dsi_utf8_fix_end(out);
}
static void plan_now(void) {
    char code[CODE_MAX + 1]; U->field_get(w_code, code, sizeof code);
    snprintf(last_code, sizeof last_code, "%s", code);
    rows_clear(); plan_ok = 0; downloads = 0;
    U->set_text(w_head, ""); dsl_win_named_text(w_head, "");
    if (!strstr(code, "DSPACK") && !code[strspn(code, " \t\r\n")]) {
        set_status(TR("Paste the code here. It starts with DSPACK.")); U->set_enabled(w_go, 0); go_label(); return;
    }
    static char js[65536];
    int r = dsl_pack_plan(code, js, sizeof js);
    JNode *j = json_parse(js);
    const char *err = json_str(j, "error");
    if (r < 0 || !j) { set_status(err && *err ? err : TR("This mod pack code is damaged or cut off. Ask your friend to send it again."));
                       U->set_enabled(w_go, 0); go_label(); json_free(j); return; }
    const char *nm = json_str(j, "name"), *kit = json_str(j, "kit"), *grp = json_str(j, "group");
    snprintf(group_s, sizeof group_s, "%s", grp ? grp : "");
    char hd[400];
    if (nm && *nm) snprintf(hd, sizeof hd, TR("Mod pack \"%s\" (mod loader %s)"), nm, kit && *kit ? kit : "?");
    else snprintf(hd, sizeof hd, TR("Mod pack (mod loader %s)"), kit && *kit ? kit : "?");
    dsi_utf8_fix_end(hd); U->set_text(w_head, hd); dsl_win_named_text(w_head, hd);
    JNode *rows = json_get(j, "rows"); float y = 0;
    for (JNode *x = rows && rows->t == JN_ARR ? rows->child : NULL; x && nrows < 128; x = x->next) {
        char t[600]; uint32_t col; row_text(x, t, sizeof t, &col);
        DSLObj o = U->text(w_list, 8, y, WIN_W - 2 * L - 40, 30, t, 16, DSL_TXT_LEFT | DSL_TXT_WRAP, col);
        float th = o && U->text_height ? U->text_height(o, WIN_W - 2 * L - 40) : 30; if (th < 30) th = 30;
        if (o) U->place(o, 8, y, WIN_W - 2 * L - 40, th);
        char rn[100]; snprintf(rn, sizeof rn, "pack-row:%s", json_str(x, "id") ? json_str(x, "id") : "?");
        w_rows[nrows++] = dsl_win_named(o, 3, rn, NULL, NULL, t);
        y += th + 6;
    }
    U->scroll_height(w_list, y);
    JNode *cf = json_get(j, "can_fix"), *dl = json_get(j, "downloads"), *np = json_get(j, "names_pending");
    plan_ok = cf && cf->t == JN_NUM && cf->num > 0;
    downloads = dl && dl->t == JN_NUM ? (int)dl->num : 0;
    if (np && np->t == JN_NUM && np->num > 0 && names_tries++ < 6) next_plan_at = now_s() + 1.0;   /* mod names still coming */
    char s[700];
    if (err && *err) snprintf(s, sizeof s, "%s", err);
    else if (!plan_ok) snprintf(s, sizeof s, "%s", TR("This mod pack cannot be imported here."));
    else snprintf(s, sizeof s, TR("It will be saved as the group \"%s\". Your mods do not change until you pick that group and click Apply."), group_s);
    set_status(s);
    U->set_enabled(w_go, plan_ok && !dsl_busy_reason());
    go_label();
    json_free(j);
}

/* ---------------------------------------------------------------- buttons */
static void close_window(void);
static void paste_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    static char c[CODE_MAX + 1]; dsl_pack_clip_get(c, sizeof c);
    U->field_set(w_code, c); dsl_win_named_text(w_code, c);
    names_tries = 0; plan_now();
}
static void go_click(void *ud, DSLObj w) {
    (void)ud; (void)w;
    if (done) { close_window(); return; }
    if (started || !plan_ok) return;
    char code[CODE_MAX + 1], why[700] = ""; U->field_get(w_code, code, sizeof code);
    if (dsl_pack_import(code, why, sizeof why)) { set_status(why); plan_now(); if (why[0]) set_status(why); return; }
    started = 1;
    U->set_enabled(w_go, 0); U->set_enabled(w_paste, 0); U->set_enabled(w_code, 0);
    dsl_log("pack: import started from the window (group \"%s\")", group_s);
}
static void cancel_click(void *ud, DSLObj w) { (void)ud; (void)w; close_window(); }
static void esc_close(void *ud) { (void)ud; close_window(); }

static void close_window(void) {
    if (!is_open) return;
    if (w_win) U->window_close(w_win);                                 /* the window and its backdrop, off the Escape stack */
    w_win = w_code = w_head = w_list = w_status = w_go = w_cancel = w_paste = NULL; nrows = 0;
    dsl_win_named_drop("pack-");
    is_open = 0;
    dsl_log("pack: window closed%s", started && !done ? " (the import goes on; the Mods window shows its end)" : "");
}
void dsl_pack_win_close(void) { close_window(); }
int dsl_pack_win_is_open(void) { return is_open; }

void dsl_pack_win_open(void) {
    if (!U) U = dsl_ui_kit();
    if (!U) { dsl_log("pack: no widget kit: the window cannot open"); return; }
    if (is_open) close_window();
    started = done = plan_ok = downloads = names_tries = 0; last_code[0] = group_s[0] = 0; next_plan_at = 0;
    DSLWinStd o = { .size = sizeof o, .kind = DSL_WIN_MODAL, .w = WIN_W, .h = WIN_H, .title = TR("Import a mod pack"), .close = esc_close,
                    .title_x = L, .title_size = 30 };
    DSLWinParts pt; w_win = U->window_std(&o, &pt);
    if (!w_win) { dsl_log("pack: the window could not be built"); return; }
    is_open = 1;
    U->tooltip(pt.close_x, NULL, TR("Close (Esc)"));
    dsl_win_named(pt.close_x, 0, "pack-close", NULL, NULL, "X");
    const float CW = WIN_W - 2 * L;
    U->text(w_win, L, 92, CW, 30, TR("Paste the mod pack code a friend sent you. You see what changes before anything is downloaded."), 16, DSL_TXT_LEFT | DSL_TXT_WRAP, GOLD);
    w_code = dsl_win_named(U->field(w_win, L, 128, CW - 136, 96, "DSPACK1...", "", NULL, NULL), 2, "pack-code", NULL, NULL, "");
    dsl_ui_field_multiline(w_code, CODE_MAX);
    w_paste = dsl_win_named(U->button(w_win, L + CW - 124, 128, 124, 44, TR("Paste"), paste_click, NULL), 0, "pack-paste", paste_click, NULL, TR("Paste"));
    U->tooltip(w_paste, TR("Paste"), TR("Put the code from the clipboard in the box."));
    w_head = dsl_win_named(U->text(w_win, L, 236, CW, 30, "", 18, DSL_TXT_LEFT | DSL_TXT_TITLE, GOLD), 3, "pack-head", NULL, NULL, "");
    U->panel(w_win, L, 270, CW, 230, 0);
    w_list = U->scroll(w_win, L + 4, 274, CW - 8, 222);
    w_status = dsl_win_named(U->text(w_win, L, 510, CW, 66, "", 15, DSL_TXT_LEFT | DSL_TXT_WRAP, 0), 3, "pack-status", NULL, NULL, "");
    w_cancel = dsl_win_named(U->button(w_win, WIN_W - L - 380 - 12 - 150, 596, 150, 48, TR("Cancel"), cancel_click, NULL), 0, "pack-cancel", cancel_click, NULL, TR("Cancel"));
    w_go = dsl_win_named(U->button(w_win, WIN_W - L - 380, 596, 380, 48, TR("Save as a group"), go_click, NULL), 0, "pack-go", go_click, NULL, TR("Save as a group"));
    /* a code on the clipboard: in the box at once (the player copied it from the chat a moment ago) */
    static char c[CODE_MAX + 1]; dsl_pack_clip_get(c, sizeof c);
    if (strstr(c, "DSPACK")) { U->field_set(w_code, c); dsl_win_named_text(w_code, c); }
    plan_now();
    dsl_log("pack: window open%s", strstr(c, "DSPACK") ? " (a code from the clipboard)" : "");
}

/* every frame (main thread, from dsl_win_frame): the typed / pasted code, mod names arriving, the import's progress */
void dsl_pack_win_frame(void) {
    if (!is_open) return;
    if (!started) {
        char code[CODE_MAX + 1]; U->field_get(w_code, code, sizeof code);
        if (strcmp(code, last_code)) { names_tries = 0; next_plan_at = now_s() + 0.3; snprintf(last_code, sizeof last_code, "%s", code); dsl_win_named_text(w_code, code); }
        if (next_plan_at > 0 && now_s() >= next_plan_at) { next_plan_at = 0; plan_now(); }
        return;
    }
    if (done) return;
    int pct; char t[800], g[80];
    int s = dsl_pack_state(&pct, t, sizeof t, g, sizeof g);
    if (strcmp(t, status_s)) set_status(t);
    if (s == 4) { done = 1; U->set_enabled(w_go, 1); go_label(); U->set_enabled(w_cancel, 0); }
    else if (s == -1) {                                                 /* nothing changed: the plan again, the button again */
        started = 0; U->set_enabled(w_paste, 1); U->set_enabled(w_code, 1);
        char why[800]; snprintf(why, sizeof why, "%s", t);
        plan_now(); set_status(why);
    }
}
