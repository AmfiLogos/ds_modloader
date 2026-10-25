// dsl_whatsnew.c - the "What's new" window (owner 2026-10-25, "All three": a CHANGELOG.md per repo, a release check, and
// an in-game window shown ONCE after the kit version changed, by auto-update or a manual install).
//
// Text: the kit entry of this release in CHANGELOG.md (section "## Dungeon Settlers Modded (kit)"), built INTO the
// loader by tools/changelog/changelog.py cheader -> build/gen/dsl_whatsnew_data.h (build.sh, start/win/build_win.sh).
// In the binary, not a file next to it: every kit install replaces the loader binary on both OS, but the Windows
// auto-update copied no text files up to 0.1.27 (dsl_update_win.c: baselib.dll, the starter, RELEASE, mods). The release check
// (changelog.py check --binary) fails a kit whose loader does not hold its entry. The title is in the game's language;
// the entry is English (owner's pick: entries are written once, in English).
// When: at the idle title (menu column shown, scene 1, nothing loading, no soft restart running), with no window of the
// game and none of ours (the Mods window, a notice box, the crash-report question of a first start ...) open, for 30
// frames in a row. Remembered: <data>/Mods/whatsnew.txt = the kit version last shown, written when the window shows
// (a quit or a crash while it shows never shows it again). A missing file (a first install, or a kit from before this
// window) shows it too (pick logged in PLAN_v013.md: an update from 0.1.27 has no file either).
// Window: the kit template dsl_window_std (dim backdrop, title in the game's title font, the game's red X, Escape, UI
// Scale), the entry in a scroll list, one OK button. Every close (X, Escape, OK) is the same close.
// Never in a test copy (background-only or DSRIG_CTL=1, dsl_soft_test_copy): other tests must not meet it. Test only:
// DSL_WHATSNEW_TEST=1 shows it in a test copy too (loader/test/whatsnew_test.py).
#include "dsl_internal.h"
#include "dsl_menu.h"
#include "../sdk/dsl_sdk.h"
#if __has_include("dsl_whatsnew_data.h")
#include "dsl_whatsnew_data.h"
#else                                                      /* a build without the generated header (host tests): no window */
#define DSL_WHATSNEW_VER DSL_RELEASE
static const char DSL_WHATSNEW_TEXT[] = "";
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dsl_reg_scene(void);                                   /* dsl_reg.c: 1 title, 2 world */
int dsl_ui_title_shown(void);                              /* dsl_ui.c: title menu column on screen */
int dsl_ui_game_window_shown(void);                        /* dsl_ui.c: a stock window of the game shows */
int dsl_ui_notice_shown(void);                             /* dsl_ui.c: the kit's notice box shows */
int dsl_esc_taken(void);                                   /* dsl_ui.c: a window of ours on the Escape stack shows */
void dsl_ui_flat(DSLObj o, uint32_t rgba);

#define WIN_W 900.0f
#define WIN_H 640.0f
#define GOLD 0xD9BE8FFFu
#define BODY 0xE8DCC0FFu

static const DSLUi *U;
static DSLObj w_win;
static int state;                                          /* 0 not decided, 1 waits for the title, 2 shown, 3 nothing to do */
static int calm, tries;                                    /* frames in a row the title was free; builds that failed */

static void path(char *out, size_t n) { snprintf(out, n, "%s/whatsnew.txt", dsl_reg_data_dir()); }
static void last_shown(char *out, size_t n) {
    char p[1200]; path(p, sizeof p); out[0] = 0;
    FILE *f = fopen(p, "r"); if (!f) return;
    if (!fgets(out, (int)n, f)) out[0] = 0;
    fclose(f); out[strcspn(out, "\r\n ")] = 0;
}
static void remember(void) {
    char p[1200], t[1300]; path(p, sizeof p); snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "w"); if (!f) { dsl_log("whatsnew: cannot write %s", t); return; }
    fprintf(f, "%s\n", DSL_WHATSNEW_VER); fclose(f);
    remove(p); if (rename(t, p)) dsl_log("whatsnew: cannot write %s", p);
}

static void close_window(void) {
    if (w_win) U->window_close(w_win);                       /* the window, its backdrop and its Escape entry */
    w_win = NULL;
}
static void esc_close(void *ud) { (void)ud; if (w_win) dsl_log("whatsnew: window closed"); close_window(); }
static void ok_click(void *ud, DSLObj w) { (void)w; esc_close(ud); }

static int build(void) {
    DSLWinStd o = { .size = sizeof o, .kind = DSL_WIN_MODAL, .w = WIN_W, .h = WIN_H, .title = TR("What's new"), .close = esc_close };
    DSLWinParts pt; w_win = U->window_std(&o, &pt);
    if (!w_win) return -1;
    U->tooltip(pt.close_x, NULL, TR("Close (Esc)"));
    float y = pt.body_y + 16;
    char v[96]; snprintf(v, sizeof v, "Dungeon Settlers Modded v%s", DSL_WHATSNEW_VER);   /* the product name is not translated */
    U->text(w_win, 40, y, WIN_W - 80, 30, v, 20, DSL_TXT_LEFT | DSL_TXT_TITLE, GOLD);
    y += 44;
    float lh = WIN_H - y - 84;
    U->panel(w_win, 32, y, WIN_W - 64, lh, 0);
    dsl_ui_flat(U->panel(w_win, 36, y + 4, WIN_W - 72, lh - 8, 0), 0x2D2A31FFu);
    DSLObj list = U->scroll(w_win, 40, y + 8, WIN_W - 80, lh - 16);
    const float tw = WIN_W - 112;
    DSLObj t = list ? U->text(list, 12, 8, tw, 40, DSL_WHATSNEW_TEXT, 17, DSL_TXT_LEFT | DSL_TXT_WRAP, BODY) : NULL;
    float th = t && U->text_height ? U->text_height(t, tw) : 0;
    if (t && th > 40) U->place(t, 12, 8, tw, th);
    if (list) U->scroll_height(list, (th > 40 ? th : 40) + 16);
    U->button(w_win, (WIN_W - 200) / 2, WIN_H - 66, 200, 46, TR("OK"), ok_click, NULL);
    return 0;
}

void dsl_whatsnew_frame(void) {
    if (state == 3) return;
    if (!state) {
        if (dsl_reg_scene() != 1) return;                    /* decided at the title: the test-copy signal and the data folder are set */
        int lines = 0; for (const char *s = DSL_WHATSNEW_TEXT; *s; s++) lines += *s == '\n'; if (DSL_WHATSNEW_TEXT[0]) lines++;
        char last[64]; last_shown(last, sizeof last);
        const char *tc = dsl_soft_test_copy(), *te = getenv("DSL_WHATSNEW_TEST");
        state = 3;
        if (!lines) dsl_log("whatsnew: this loader has no CHANGELOG entry for %s: no window", DSL_WHATSNEW_VER);
        else if (!strcmp(last, DSL_WHATSNEW_VER)) dsl_log("whatsnew: %s was shown before: no window", DSL_WHATSNEW_VER);
        else if (tc && !(te && !strcmp(te, "1"))) dsl_log("whatsnew: test copy (%s): no window (DSL_WHATSNEW_TEST=1 shows it)", tc);
        else { state = 1; dsl_log("whatsnew: kit %s (last shown: %s): the window shows at the idle title (%d lines)", DSL_WHATSNEW_VER, last[0] ? last : "none", lines); }
        return;
    }
    if (state == 2) {
        if (w_win && dsl_reg_scene() != 1) { dsl_log("whatsnew: the title went away: window closed"); close_window(); }
        if (!w_win) state = 3;
        return;
    }
    if (!U) U = dsl_ui_kit();
    int free_ = U && dsl_reg_scene() == 1 && dsl_ui_title_shown() && dsl_soft_title_idle() && !dsl_soft_busy()
                && !dsl_ui_game_window_shown() && !dsl_ui_notice_shown() && !dsl_esc_taken()
                && !(dsl_win_is_open && dsl_win_is_open());
    calm = free_ ? calm + 1 : 0;
    if (calm < 30) return;
    if (build()) {
        close_window(); calm = 0;
        if (++tries >= 5) { state = 3; dsl_log("whatsnew: the window could not be built (5 tries): no window this start"); }
        else dsl_log("whatsnew: the window could not be built: next try later");
        return;
    }
    remember(); state = 2;
    dsl_log("whatsnew: window shown for %s (the CHANGELOG entry, %zu bytes)", DSL_WHATSNEW_VER, strlen(DSL_WHATSNEW_TEXT));
}
