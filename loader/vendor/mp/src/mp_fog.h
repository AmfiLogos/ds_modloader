/* mp_fog: per-player fog of war, display only (host rule "Shared fog of war", dsnet_rules.shared_fog; default Off).
 * Off: each player's fog shows only what their own units (plus buildings, unowned units, temporary reveals) see; enemies
 * outside that are hidden. The sim's vision / visited state is never changed. Research: ext/core_next/fog_research.md. */
#pragma once
void mp_fog_install(void);            /* hooks; once at start (mpmenu_install) */
void mp_fog_set_shared(int on);       /* 1 = vanilla; redraws when it changes */
void mp_fog_owner_changed(void);      /* unit owners / lends changed: redraw */
void mp_fog_frame(void);              /* main thread, once per frame (mpmenu_frame) */
