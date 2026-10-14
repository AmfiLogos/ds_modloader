// ui_tip.h - our explanatory tooltips in the game's own tooltip window (owner lan_v9, PLAN.md "Owner correction on
// tooltips"). The stock TooltipUI shows a Tooltip_ETC (title + description, the same window the game uses for its
// HUD and panel help); ui_tip only replaces its two texts. Main thread only.
#pragma once

/* Show (or keep showing) our tooltip. Call it every frame while the pointer is over the element; it hides by itself
   0.2 s after the last call (game_tip_frame), or at once with game_tip_hide. Same title + body as the last call =
   no work. Honours the player's "Show tooltips on hover" (panel cog): off = nothing shows.
   game_tip_show:      next to the mouse (stock screen-position placement).
   game_tip_show_rect: next to a RectTransform (stock caller-rect placement, as the game's own buttons do). */
/* 1 = the game functions match this build (checked once) and the scene has its TooltipUI: safe to call the rest */
int game_tip_ready(void);
void game_tip_show(const char *title, const char *body);
void game_tip_show_rect(const char *title, const char *body, void *rect_transform);
void game_tip_hide(void);
/* once per frame from ui_frame (the 0.2 s auto-hide). Cheap when nothing shows. */
void game_tip_frame(void);
/* test socket: {"cmd":"ui_tip","action":"show"|"hide"|"probe","title":..,"body":..,"key":..,"px":..,"py":..}; NULL = not ours */
char *ui_tip_exec(const char *cmd, const char *line);
