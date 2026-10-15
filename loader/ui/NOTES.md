# loader/ui - Mods window (lane L2, branch l2-modswin)

Plan: ~/ds_multiplayer/research/MASTER_PLAN.md (L2 row). Contract with the loader core: ../src/dsl_menu.h (lane L1 owns it).

## What is here
- `dsl_win.c`: the window, drawn only with L1's DSLUi kit (game-style clones). Implements dsl_win_init / open / close /
  is_open / frame. Title band with the Mud Slogel idle animation (5 frames, 6 per second, pixel filter), tab strip (Mods
  first, then module tabs from dsl_tab_count/at, at most 4), gold mark under the open tab, X and Esc close (Esc in a text
  field stays with the field). Mods tab: one switch per mod (dsl_mods_*, hidden ones skipped; "Not running: <reason>" in
  orange), "Save these mods as a group" (name field + Save / Cancel), group rows (click = switches to that set, gold mark
  on the group that matches the switches, x = delete after the game's notice), Undo changes, Apply.
- Apply: dsl_apply_mods(); the window closes at once so the game's loading screen covers the screen; the new game opens
  the Mods window again by itself once dsl_busy_reason() is NULL ("put people back exactly where they were"); marker file
  `<dir of DSL_PROFILE>/.cache/reopen_mods_window` (renamed .used after reading; 180 s limit). If the restart ends with
  no swap, this game reopens the window with "The mods did not change: ...".
- Cache: `<dir of DSL_PROFILE>/.cache/mudslogel_idle_<k>.png` (image_png reads files). Asset source:
  `assets/mudslogel_idle.png` -> `dsl_win_assets.h` (assets/gen_assets.py).
- No game address. Unity calls by name (dsl_find_class / dsl_find_method, parameter types for GetComponent(Type)).
  Compiles clean with llvm-mingw (Windows) and clang arm64.

- World mode (dsl_win_open_world, from the Load list "Mods: ... [Change]"): heading "Mods for <world>", switches = the
  world's gameplay set (dsl_world_mods; client mods as they run), button "Save for this world" -> dsl_world_set_mods,
  no restart. A normal open goes back to the running set.

## Needs from L1 (sent 2026-10-15; all done by l1-core 5988e29)
1. build.sh: compile loader/ui/dsl_win.c into dsload; call dsl_win_init() after dsl_ui_install(); frame tick ->
   dsl_win_frame(); title "Mods" button and menu_open -> dsl_win_open().
2. dsl_busy_reason(): non-NULL in the NEW game (B) until its swap and fade-in are done (B_DONE), so the window opens
   after the loading screen, not over it.
3. dsl_apply_mods(): keep hidden / loader-internal mods enabled in pending.json.

## Test (game copy Y, never the owner's screen)
`test/build_test.sh` = the loader exactly as L1's build.sh makes it (it compiles ui/*.c) + test module dsltest-win
(adds a tab through host->menu_add_tab with --tab). Modules from ~/ds_launcher_l1core/loader/build/mods. `test/ui_test.py <out> [--tab] [--apply]` (lease a pair first).
Clicks call the named control's callback (L1's uikit_test covers the kit's mouse path). Frames: <out>/ui/*.png,
soft restart frames <out>/softcap/.
- 2026-10-15 19:18 run9 (--tab): 25/25 PASS, no guard violation. 19:24 run16 (--apply): all PASS; A gone 6.3 s after
  Apply, B runs with gcd-cdr, B opens the Mods window by itself after the swap; loading screen frames clean.
- 19:40 run17 (--tab, L1 loader c7e510f + world mode): all PASS. 19:42 run19 (--apply, real dsl_apply_mods): all PASS;
  A gone 6.4 s, B runs gcd-cdr and reopens the window after the title shows; worlds.json gets the world's set.
- 19:48 run20 / 19:49 run21 on l1-core 5988e29: all PASS. BepInEx mark (DSLModInfo.kind "bepinex", small gold "BepInEx"
  before the description; test mod.json test/fake-bepinex). Unknown world = running set (lead). A failed restart shows the
  loader's reason from <DSL_LOG_DIR>/softrestart/handoff.json.failed. A gone 5.8 s after Apply.
