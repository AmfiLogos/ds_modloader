// Contract between the loader core (loader/src, lane L1) and the Mods window (loader/ui, lane L2).
// Not part of the module SDK. L1 owns this file; ask L1 for changes.
//
// L1 provides: the tab / title / load-list registries, the frame tick, the dsl_ui kit, the mod list, profiles,
// groups and Apply (soft restart). L2 provides the window: dsl_win_* below (weak symbols: without loader/ui the
// loader still builds and the "Mods" title button only logs).
#pragma once
#include "../sdk/dsl_sdk.h"

/* ---- provided by L1 (loader/src) */
int dsl_tab_count(void);                        /* module tabs sorted by order; the loader's Mods tab is not in it */
const DSLTab *dsl_tab_at(int i);
const char *dsl_tab_owner(int i);               /* module id that added it */
const DSLUi *dsl_ui_kit(void);                  /* NULL before il2cpp_init returned */

/* installed mods (every <mods>/<id>/mod.json seen at start, code or not) */
typedef struct DSLModInfo {
    const char *id, *name, *version, *description, *scope;   /* scope "gameplay" | "client" */
    int enabled;                                 /* in the running profile */
    int active;                                  /* 1 loaded and running, 0 not, -1 failed (reason) */
    const char *reason;                          /* why not active ("" if active) */
    int hidden;                                  /* loader-internal (not shown as a switch) */
    const char *kind;                            /* mod.json "kind": "code" | "bepinex" | "data" ...; never NULL */
} DSLModInfo;
int dsl_mods_count(void);
const DSLModInfo *dsl_mods_at(int i);

/* groups (<data>/Mods/groups.json {"groups":[{"name","mods":[ids]}]}) */
int dsl_groups_count(void);
const char *dsl_group_name(int i);
int dsl_group_mods(int i, const char **ids, int max);         /* returns count */
int dsl_group_save(const char *name, const char *const *ids, int n);   /* add or replace by name; 0 = ok */
int dsl_group_delete(const char *name);

/* Apply: write <data>/Mods/pending.json with these ids enabled and soft restart into it. The new process makes it
   active.json once it runs (commit only on success). Returns 0 = started, -1 = refused (reason copied to why). */
int dsl_apply_mods(const char *const *ids, int n, char *why, size_t cap);
const char *dsl_busy_reason(void);              /* session busy / soft restart running / NULL */

/* mods of one world (Load list "Mods: ... Change"): ids of its gameplay set; set = remember another set for it
   (takes effect the next time it is loaded: the "Load it with them?" question then offers that set). */
int dsl_world_mods(const char *guid, const char **ids, int max);    /* count; an unknown world = the running set; 0 = vanilla */
int dsl_world_set_mods(const char *guid, const char *const *ids, int n);

/* mod settings (src/dsl_settings.c; mod.json "settings", SDK API 5). i = the setting's index in its mod's list. */
enum { DSL_SET_TOGGLE = 0, DSL_SET_INT = 1, DSL_SET_CHOICE = 2 };
typedef struct DSLSettingInfo {
    const char *id, *label, *help;               /* texts in the current language (help "" = none) */
    int type, gameplay;                          /* gameplay 1: changes apply with Apply; 0 (local): at once */
    int min, max, step, nchoices, dflt;          /* toggle 0..1, choice 0..nchoices-1 (dflt = index) */
} DSLSettingInfo;
int dsl_settings_count(const char *mod);                         /* declared settings of that mod (0 = no Settings button) */
const DSLSettingInfo *dsl_settings_at(const char *mod, int i);
const char *dsl_settings_choice_label(const char *mod, int i, int c);
void dsl_settings_value_label(const char *mod, int i, int v, char *out, size_t cap);   /* On / Off, the number, the choice */
int dsl_settings_value(const char *mod, int i);                  /* what the page shows (a gameplay draft or the value) */
int dsl_settings_running(const char *mod, int i);                /* the value this game runs with */
int dsl_settings_set(const char *mod, int i, int v);             /* local: saved + live; gameplay: draft for Apply */
int dsl_settings_pending(const char *mod);                       /* gameplay drafts not applied (mod NULL = all mods) */
void dsl_settings_undo(void);                                    /* drop the gameplay drafts */
int dsl_settings_changes(char *out, size_t cap);                 /* "Label: value, ..." of the drafts; returns how many */
/* multiplayer "Get the host's mods": the host's "<mod>/<setting>@<value>" lines become the drafts (the next Apply
   starts with them). Returns how many values were taken. */
int dsl_settings_take_lines(const char *lines);

/* ---- provided by L2 (loader/ui); weak in L1 */
void dsl_win_init(void) __attribute__((weak));            /* after il2cpp_init, once */
void dsl_win_open(const char *tab_id) __attribute__((weak));  /* NULL = Mods tab; from the title button / menu_open */
void dsl_win_close(void) __attribute__((weak));
int dsl_win_is_open(void) __attribute__((weak));
void dsl_win_frame(void) __attribute__((weak));           /* every frame from L1's tick (main thread) */
/* Mods tab bound to one world (from its "Change"): switches show that world's set; the Apply button reads "Save for
   this world" and calls dsl_world_set_mods. Missing: L1 opens the normal Mods tab. */
void dsl_win_open_world(const char *guid, const char *world_name) __attribute__((weak));
void dsl_win_relang(void) __attribute__((weak));               /* the game's language changed: an open window is built again */
