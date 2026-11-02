/* dsl_modtag_rule.h - THE rule for a mod's tags (owner 2026-11-02, PLAN_v013 item 33: "icons depending on if a mod is
 * client only ... an icon for vanilla and vanilla plus versus actual Gameplay change"). Pure C, no game calls.
 *
 * Kind:  Vanilla & Vanilla+  = a fix for a bug of the game (mod.json "category": "fix"), a small mod marked
 *                              "vanilla_plus": true, or a mod of scope "client" (changes nothing in the gameplay);
 *        Changes the gameplay = every other mod (scope "gameplay", the default).
 * Side:  Only your game      = scope "client" (other players in a multiplayer game do not need it);
 *        Every player        = scope "gameplay" (every player in a multiplayer game needs the same mod).
 *
 * The Load list's world kind (dsl_reg.c dsl_world_is_modded) and the Mods window's icons (ui/dsl_modtags.c) both use
 * these two functions, so they can never disagree. 0 = the tag does not resolve (no scope): the guard test
 * (loader/test/modtags_test.py) fails on it. */
#ifndef DSL_MODTAG_RULE_H
#define DSL_MODTAG_RULE_H
#include <string.h>

enum { DSL_KIND_VANILLA = 1, DSL_KIND_GAMEPLAY = 2 };
enum { DSL_SIDE_CLIENT = 1, DSL_SIDE_ALL = 2 };

static inline int dsl_modtag_kind(int fix, int vanilla_plus, const char *scope) {
    if (!scope || (strcmp(scope, "client") && strcmp(scope, "gameplay"))) return 0;
    return fix || vanilla_plus || !strcmp(scope, "client") ? DSL_KIND_VANILLA : DSL_KIND_GAMEPLAY;
}
static inline int dsl_modtag_side(const char *scope) {
    if (!scope) return 0;
    return !strcmp(scope, "client") ? DSL_SIDE_CLIENT : !strcmp(scope, "gameplay") ? DSL_SIDE_ALL : 0;
}
#endif
