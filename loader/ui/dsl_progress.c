// dsl_progress.c - the progress bar of a soft restart (owner v13.3; designs "Reload" A and B on the design canvas).
//   Apply at the title: design B, a strip at the bottom of the game's loading screen ("Applying mods", the step, %, a thin
//   bar), or design C, the ornament bar in the middle ("Step 4 of 7 · Loading fonts"); B or C at random each Apply
//   (owner), the same in the old and the new game (both take it from the apply time in the handoff).
//   Flow K (a world with other mods, restart straight into it): a framed panel in the middle ("Applying mods", a bar,
//   the step, %, "Your world opens when this is done.").
// The numbers are real steps from src/dsl_soft.c (dsl_soft_progress_view): the old game draws B's last step while it
// waits; the new game draws 100 % from its own loading screen until it fades in. Drawn with the kit (the game's fonts),
// above the loading screen (own canvas, sorting 32100). Shared C: Mac and Windows.
#include "../src/dsl_internal.h"
#include "../src/dsl_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dsl_soft_progress_view(int *pct, char *text, size_t cap, char *arg, size_t acap, int *panel, long *seed);   /* src/dsl_soft.c */
void dsl_ui_flat(DSLObj o, uint32_t rgba);                                  /* src/dsl_ui.c */
void dsl_ui_turned(DSLObj o, float cx, float cy, float size, float deg);
void dsl_ui_raise(DSLObj o, int order);
void dsl_ui_fit_screen(DSLObj w, float wd, float h, float ay);

#define TITLE_C 0xE3CFB0FFu
#define BODY_C 0xCBBFAEFFu
#define NOTE_C 0xA99C8AFFu
#define COPPER 0x9A6A4BFFu
#define FILL 0xC98A52FFu
#define TRACK 0x2A2530FFu
#define WELL 0x0E0C10FFu
#define PANEL 0x1F1C22FFu
#define DARK 0x15131AFFu

static const DSLUi *U;
enum { D_STRIP, D_PANEL, D_ORNAMENT };
static DSLObj root, fill, step_t, pct_t; static int built_panel = -1; static float fill_x, fill_y, fill_h; static float fill_w; static int last_pct = -1; static char last_text[160], last_arg[160];
static int last_fit = -1;                                                   /* design last fitted to the screen */
static unsigned last_gen;                                                   /* ds_i18n.h generation: a language change redraws */

static DSLObj flat_at(DSLObj parent, float x, float y, float w, float h, uint32_t c) {
    DSLObj p = U->panel(parent, x, y, w, h, c); if (p) dsl_ui_flat(p, c); return p;
}
static void destroy_ui(void) { if (root) U->destroy(root); root = fill = step_t = pct_t = NULL; built_panel = -1; last_fit = -1; last_pct = -1; last_text[0] = 0; last_arg[0] = 0; }

/* design B: bottom strip (reference 1920x1080, the game's NOW LOADING sits at the bottom right below it) */
static void build_strip(void) {
    root = U->panel(NULL, 0, 0, 1920, 1080, 0x00000001); if (!root) return;  /* the game's loading screen shows through */
    U->esc_push(root, NULL, NULL, DSL_ESC_HUD);                       /* a loading overlay, not a window: never takes Escape */
    dsl_ui_flat(root, 0x00000001);
    U->text(root, 128, 886, 700, 44, TR("Applying mods"), 30, DSL_TXT_LEFT | DSL_TXT_TITLE, TITLE_C);
    step_t = U->text(root, 820, 888, 972, 44, "", 26, DSL_TXT_RIGHT, BODY_C);
    pct_t = NULL;                                                          /* the % is part of the step line */
    flat_at(root, 128, 944, 1664, 20, COPPER);                             /* copper lines above and below the track */
    flat_at(root, 128, 946, 1664, 16, TRACK);
    fill_x = 128; fill_y = 946; fill_h = 16; fill_w = 1664; fill = flat_at(root, 128, 946, 0, 16, FILL);
}
/* design C: ornament bar (title, a thin bar between two copper diamonds, "Step n of m · step") */
static void build_ornament(void) {
    root = U->panel(NULL, 0, 0, 1920, 1080, 0x00000001); if (!root) return;
    U->esc_push(root, NULL, NULL, DSL_ESC_HUD);                       /* a loading overlay, not a window: never takes Escape */
    dsl_ui_flat(root, 0x00000001);
    float w = 960, x = (1920 - w) / 2, y = 540;
    flat_at(root, 0, y - 170, 1920, 280, 0x222025FFu);                    /* the loading screen's colour over its tip text */
    U->text(root, 0, y - 110, 1920, 60, TR("Applying mods"), 50, DSL_TXT_CENTER | DSL_TXT_TITLE, 0xEFE6D6FFu);
    DSLObj d1 = flat_at(root, 0, 0, 15, 15, COPPER), d2 = flat_at(root, 0, 0, 15, 15, COPPER);
    if (d1) dsl_ui_turned(d1, x + 8, y, 15, 45); if (d2) dsl_ui_turned(d2, x + w - 8, y, 15, 45);
    flat_at(root, x + 30, y - 4, w - 60, 9, 0x3A3036FFu);
    fill_x = x + 30; fill_y = y - 4; fill_h = 9; fill_w = w - 60; fill = flat_at(root, fill_x, fill_y, 0, 9, FILL);
    step_t = U->text(root, 0, y + 30, 1920, 36, "", 27, DSL_TXT_CENTER, 0xB8AC9BFFu);
    pct_t = NULL;
}
/* the step's text in the current language: its key (English, from dsl_soft.c) translated, with its argument */
static void step_text(const char *key, const char *arg, char *out, size_t cap, int english) {
    const char *f = english ? key : TR(key);
    if (strstr(key, "%s")) snprintf(out, cap, f, arg); else snprintf(out, cap, "%s", f);
    dsi_utf8_fix_end(out);
}
/* "Step n of m" of the title Apply: the steps dsl_soft.c reports (by their keys), in their order */
static void step_of(const char *key, const char *text, char *out, size_t cap) {
    static const char *const order[] = { "Preparing mods", "Loading %s", "Loading game data", "Loading fonts", "Loading menus", "Almost done", "Done" };
    int n = (int)(sizeof order / sizeof order[0]), k = 0;
    for (int i = 0; i < n; i++) if (!strcmp(key, order[i])) k = i + 1;
    if (!k && !strcmp(key, "Loading mods")) k = 2;                        /* no mod names */
    if (k) snprintf(out, cap, TR("Step %d of %d \xC2\xB7 %s"), k, n, text); else snprintf(out, cap, "%s", text);
    dsi_utf8_fix_end(out);
}
/* design A: centered panel */
static void build_panel(void) {
    root = U->panel(NULL, 0, 0, 1920, 1080, 0x00000001); if (!root) return;  /* the game's loading screen shows through */
    U->esc_push(root, NULL, NULL, DSL_ESC_HUD);                       /* a loading overlay, not a window: never takes Escape */
    dsl_ui_flat(root, 0x00000001);
    float w = 840, h = 330, x = (1920 - w) / 2, y = (1080 - h) / 2;
    flat_at(root, x - 8, y - 8, w + 16, h + 16, 0x5E4231FFu);               /* outer ring */
    flat_at(root, x - 6, y - 6, w + 12, h + 12, DARK);
    flat_at(root, x - 3, y - 3, w + 6, h + 6, COPPER);                     /* copper border */
    DSLObj in = flat_at(root, x, y, w, h, PANEL);
    if (!in) return;
    U->text(in, 0, 40, w, 56, TR("Applying mods"), 44, DSL_TXT_CENTER | DSL_TXT_TITLE, 0xEFE6D6FFu);
    flat_at(in, 66, 116, w / 2 - 66 - 16, 2, 0x6B4C39FFu);                   /* divider with a small diamond gap */
    flat_at(in, w / 2 + 16, 116, w / 2 - 66 - 16, 2, 0x6B4C39FFu);
    { DSLObj d = flat_at(in, 0, 0, 12, 12, COPPER); if (d) dsl_ui_turned(d, w / 2, 117, 12, 45); }
    flat_at(in, 66, 148, w - 132, 34, COPPER);                             /* the bar: copper frame, dark well, fill */
    flat_at(in, 68, 150, w - 136, 30, WELL);
    fill_x = 72; fill_y = 154; fill_h = 22; fill_w = w - 144; fill = flat_at(in, 72, 154, 0, 22, FILL);
    step_t = U->text(in, 66, 200, w - 260, 36, "", 27, DSL_TXT_LEFT, BODY_C);
    pct_t = U->text(in, w - 200, 200, 134, 36, "", 27, DSL_TXT_RIGHT, BODY_C);
    U->text(in, 0, 262, w, 30, TR("Your world opens when this is done."), 22, DSL_TXT_CENTER, NOTE_C);
}
void dsl_progress_frame(void) {
    static int tick; if (++tick % 3) return;
    int pct = 0, panel = 0; long seed = 0; char key[160] = "", arg[160] = "", text[400];
    if (!dsl_soft_progress_view(&pct, key, sizeof key, arg, sizeof arg, &panel, &seed)) { if (root) destroy_ui(); return; }
    int design = panel ? D_PANEL : (seed & 1) ? D_ORNAMENT : D_STRIP;     /* B or C at random (the apply second) */
    const char *force = getenv("DSL_PROGRESS_DESIGN");                     /* test only: strip | ornament */
    if (!panel && force && !strcmp(force, "strip")) design = D_STRIP;
    if (!panel && force && !strcmp(force, "ornament")) design = D_ORNAMENT;
    if (!U) U = dsl_ui_kit();
    if (!U) return;
    if (root && (built_panel != design || last_gen != dsi_generation())) destroy_ui();   /* a language change: built again */
    last_gen = dsi_generation();
    if (!root) {
        if (design == D_PANEL) build_panel(); else if (design == D_ORNAMENT) build_ornament(); else build_strip();
        if (!root) return;                                                 /* the kit has no templates yet */
        built_panel = design; dsl_ui_raise(root, 32100);
        dsl_log("progress: %s shown", design == D_PANEL ? "centered panel" : design == D_ORNAMENT ? "ornament bar" : "bottom strip");
    }
    /* the designs are 1920 x 1080: centred on the screen (the strip at its bottom), smaller on a 4:3 or tall window,
       where the layer is narrower than 1920 and the right side was cut off */
    if (tick % 30 == 0 || built_panel != last_fit) { dsl_ui_fit_screen(root, 1920, 1080, design == D_STRIP ? 0.f : 0.5f); last_fit = built_panel; }
    if (pct == last_pct && !strcmp(key, last_text) && !strcmp(arg, last_arg)) return;
    last_pct = pct; snprintf(last_text, sizeof last_text, "%s", key); snprintf(last_arg, sizeof last_arg, "%s", arg);
    step_text(key, arg, text, sizeof text, 0);
    char b[600];
    if (design == D_PANEL) { U->set_text(step_t, text); snprintf(b, sizeof b, "%d%%", pct); U->set_text(pct_t, b); }
    else if (design == D_ORNAMENT) { step_of(key, text, b, sizeof b); U->set_text(step_t, b); }
    else { snprintf(b, sizeof b, "%s  \xC2\xB7  %d%%", text, pct); dsi_utf8_fix_end(b); U->set_text(step_t, b); }
    if (fill) { float fw = fill_w * pct / 100.f; U->set_active(fill, fw > 0.5f); U->place(fill, fill_x, fill_y, fw, fill_h); }
    { char en[400]; step_text(key, arg, en, sizeof en, 1); dsl_log("progress: %d%% %s", pct, en); }   /* the log stays English */
}
