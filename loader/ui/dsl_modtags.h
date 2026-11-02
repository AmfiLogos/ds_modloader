/* dsl_modtags.h - THE template for a mod's tags in a mod list of the loader (owner 2026-11-02, PLAN_v013 item 33).
 * The rule (which tag a mod gets): ../src/dsl_modtag_rule.h. The drawing (badges with the game's own sprites, hover
 * cards, legend): dsl_modtags.c. Any list of mods draws its rows' tags with dsl_modtags_row and explains them once with
 * dsl_modtags_legend (the Mods window: dsl_win.c mod_row / mods_open). */
#ifndef DSL_MODTAGS_H
#define DSL_MODTAGS_H
#include "../sdk/dsl_sdk.h"
#include "../src/dsl_menu.h"
#include "../src/dsl_modtag_rule.h"

enum { DSL_TAG_SLOGEL, DSL_TAG_ABYSS, DSL_TAG_ONE, DSL_TAG_ALL, DSL_TAG_N };   /* the four icons */
#define DSL_MODTAG_BADGE 32.0f                     /* one badge (frame + icon), square */
#define DSL_MODTAG_GAP 4.0f

/* the space two badges take on a row (badges + gaps): the row's switch is that much narrower */
#define DSL_MODTAGS_W (2 * DSL_MODTAG_BADGE + 2 * DSL_MODTAG_GAP)
/* the badges of mod m: [kind][side] from x (left edge), top at y. *kind_out / *side_out = the two badges (NULL when the
   tag does not resolve). Returns the number of badges drawn (2, or 0 for a mod whose scope is not client / gameplay). */
int dsl_modtags_row(const DSLUi *U, DSLObj parent, float x, float y, const DSLModInfo *m, DSLObj *kind_out, DSLObj *side_out);
/* the legend: the four icons and what they mean, in two lines of two (kind, then side), at (x, y), w wide.
   out[DSL_TAG_N] = its four parts (their hover cards are the badges' cards). Returns its height. */
float dsl_modtags_legend(const DSLUi *U, DSLObj parent, float x, float y, float w, DSLObj out[DSL_TAG_N]);
/* the tag ids for tests and logs: "vanilla" / "gameplay", "client" / "all" ("" = does not resolve) */
const char *dsl_modtags_kind_id(int kind);
const char *dsl_modtags_side_id(int side);
/* the icons made from the game's sprites (0..DSL_TAG_N); built once per game start on the first call */
int dsl_modtags_icons_ok(void);
#endif
