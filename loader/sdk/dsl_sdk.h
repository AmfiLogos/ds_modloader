// dsl_sdk.h - module SDK for the Dungeon Settlers mod loader (dsload.dylib), API version 6.
// API 6 = API 5 + mod_data_dir (appended to DSLHost): <game data>/ModData/<mod id>/, a folder of this mod that a mod
// update or a disable / enable never touches (the mod's own folder is replaced by an update). mod.json "sync" lists
// the files there that are gameplay: a multiplayer guest gets the host's copy of them for the session (../NOTES.md
// "Mod data folder"). Wrapper dsl_mod_data_dir (NULL from an older loader).
// API 5 = API 4 + mod settings (appended to DSLHost): a module declares its settings in mod.json "settings" and reads
// them with dsl_get_bool / dsl_get_int / dsl_get_choice (wrappers below: an older loader gives the default back).
// API 4 = API 3, same DSLHost layout, plus one behaviour: title_add_button called again with the same self, click
// and ud relabels that button (new label / tip) instead of adding a second one. A module that relabels after a
// language change checks host->api_version >= 4 first (an API 3 loader would add a second button).
// The loader loads every module whose dsl_plugin_api_version (and mod.json "api") is 1..DSL_API_VERSION, so API 1,
// 2 and 3 modules load exactly as before. DSL_PLUGIN() still exports 3 (DSL_PLUGIN_API): API 4 adds no field, a module
// checks host->api_version at run time, so a module built with this header also loads in an API 3 loader.
// API 3 = API 2 + soft restart, session busy, Mods window tabs, title buttons, load-list rows / filter and the
// dsl_ui widget kit (all appended to DSLHost; check host->size before use, or use the dsl_* wrappers below).
// API 2 = API 1 (~/DungeonSettlersMods/loader/sdk/dsl_sdk.h) + fields appended to DSLHost + two optional
// earlier entry points. An API 1 plugin (only dsl_plugin_init) loads unchanged.
//
// A module is an arm64 dylib in <mods>/<id>/ with a mod.json (see ../NOTES.md). The loader is injected with
// DYLD_INSERT_LIBRARIES. It dlopens each enabled module in dependency order and calls, when exported:
//
//   dsl_module_load(host, self)   right after dlopen, before GameAssembly is mapped (host->ga_base == 0).
//                                 Read env/config, open logs. No hooks yet.
//   dsl_module_early(host, self)  GameAssembly is mapped, none of its code has run yet (dyld add-image time).
//                                 Hooks by RVA / per-build table work; il2cpp name lookups do NOT work yet.
//   dsl_plugin_init(host, self)   after il2cpp_init returned (API 1 entry point). Name lookups work.
//
// Return 0 for success. Any other value: every hook of this module is removed, the module is off this session.
// Crash guard: if the game dies inside one of these calls, the next start skips the module (until its file changes).
//
// Hooks: one engine for every module (one stub allocator, one registry). A function already hooked by any
// module, or starting with a branch / PC-relative instruction (another loader's or a static patch), is refused.
// Hooks may be installed in any phase; after il2cpp_init they go to a separate stub page.
//
// mod.json "scope": "gameplay" (default: every multiplayer player needs it) or "client" (only this player).
// mod.json "category": absent / "game" = a game mod (per world), "fix" = a fix for a bug of the game (per world, own
// tab, "On in all worlds" box, never makes a world "Modded" in the Load list filters), "feature" = a launcher mod (on for
// every world, never in a world's mod set). See ../NOTES.md "Fixes" and "Launcher features vs game mods".
// A module may export `int dsl_world_check_skip(void)`: 1 = the world check (mods per world) lets the current load
// through (multiplayer guest loading the host's world).
// A module may export `int dsl_world_temporary(const char *guid)`: 1 = a temporary world of the mod (multiplayer: the
// Foreign Expedition hub, made new for each session), never registered in worlds.json.
// The loader exports `int dsl_world_copy_mods(const char *from, const char *to)` (kit 0.1.27): world `to` (a copy the mod
// wrote, e.g. multiplayer's single-player copy) plays with the mods of `from`. 0 ok.
// A module that hides world rows of the stock Load list itself may export `int dsl_loadlist_hidden(int *kind)`: the
// number of world rows it hides in the open list now; *kind 1 = multiplayer worlds, 2 = single-player worlds, else 0.
// The loader writes one line under the list ("5 multiplayer worlds are hidden ...") so a player knows they are not lost.
// The loader's filter panel on the right of the Load list (kit v0.1.23: Vanilla & Vanilla+, Modded, Multiplayer, Latest
// save only) asks two more optional exports: `int dsl_loadlist_world_mp(const char *guid)` 1 = a multiplayer world (the
// panel's Multiplayer kind; no module with it = no Multiplayer box), and `int dsl_loadlist_own(void)` 1 = this module
// filters the open list itself now, so the panel stays away. The loader exports `int dsl_loadlist_panel(void)` (1).
//
// Log: <log dir>/dsload.log (DSL_LOG_DIR, else ~/Library/Application Support/CanOpener/Dungeon Settlers).
#pragma once
#include <stdint.h>
#include <stddef.h>

#define DSL_API_VERSION 6

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DSLPlugin DSLPlugin;          /* opaque handle, one per module */
typedef void *DSLObj;                        /* a game UI object (RectTransform of a widget); NULL = none */

/* ---- API 3 structs (pass a pointer; the loader copies them, strings included) */
typedef struct DSLTab {
    const char *id;                          /* unique, e.g. "my-units" */
    const char *title;                       /* tab strip label, e.g. "My Units" */
    int order;
    void (*open)(void *ud, DSLObj root, float w, float h);
    void (*update)(void *ud);                /* may be NULL */
    void (*close)(void *ud);                 /* may be NULL */
    int (*can_close)(void *ud);              /* may be NULL = 1 */
    void *ud;
} DSLTab;

typedef struct DSLLoadRow {
    void (*text)(void *ud, const char *guid, const char *save_name, char *out, size_t cap);
    void (*click)(void *ud, const char *guid, const char *save_name);   /* may be NULL */
    void *ud;
} DSLLoadRow;

enum { DSL_LIST_LOAD = 0, DSL_LIST_OFF = -1 };  /* mode(): which list opened; a module may return its own >0 ids */
typedef struct DSLLoadFilter {
    int (*show)(void *ud, const char *guid, const char *save_name, int toggle_on);
    const char *(*label)(void *ud);          /* toggle label for this opening; NULL / "" = no toggle */
    int (*mode)(void *ud);                   /* may be NULL = DSL_LIST_LOAD */
    void *ud;
} DSLLoadFilter;

/* ---- dsl_ui: game-style widgets (copied once from mpmenu.c's helpers; same look as the multiplayer pages).
   Coordinates: pixels inside `parent`, origin top-left, y down. Every create call returns the widget's
   RectTransform (DSLObj) or NULL. Strings are copied. Callbacks run on the main thread between frames. */
typedef void (*dsl_ui_cb)(void *ud, DSLObj w);
enum { DSL_TXT_LEFT = 0, DSL_TXT_CENTER = 1, DSL_TXT_RIGHT = 2, DSL_TXT_WRAP = 4, DSL_TXT_TITLE = 8 /* Cinzel */ };
typedef struct DSLUi {
    uint32_t size;                           /* sizeof(DSLUi) of the loader; functions only get appended */
    DSLObj (*panel)(DSLObj parent, float x, float y, float w, float h, uint32_t rgba);       /* game panel sprite, tinted */
    DSLObj (*text)(DSLObj parent, float x, float y, float w, float h, const char *s, float size, int flags, uint32_t rgba);
    void (*set_text)(DSLObj text_or_button, const char *s);
    DSLObj (*button)(DSLObj parent, float x, float y, float w, float h, const char *label, dsl_ui_cb click, void *ud);
    DSLObj (*toggle)(DSLObj parent, float x, float y, float w, const char *label, int on, dsl_ui_cb change, void *ud);
    int (*toggle_get)(DSLObj t);
    void (*toggle_set)(DSLObj t, int on);    /* no change callback */
    /* value row: label  < value >  ; change gets the new index through value_get */
    DSLObj (*value_row)(DSLObj parent, float x, float y, float w, const char *label, const char *const *values, int n,
                        int index, dsl_ui_cb change, void *ud);
    int (*value_get)(DSLObj v);
    void (*value_set)(DSLObj v, int index);
    DSLObj (*field)(DSLObj parent, float x, float y, float w, float h, const char *placeholder, const char *text,
                    dsl_ui_cb submit, void *ud);                                            /* stock TMP_InputField clone */
    size_t (*field_get)(DSLObj f, char *out, size_t cap);
    void (*field_set)(DSLObj f, const char *s);
    /* scroll list: returns the content RectTransform; put rows in it, then set its height */
    DSLObj (*scroll)(DSLObj parent, float x, float y, float w, float h);
    void (*scroll_height)(DSLObj content, float h);
    DSLObj (*image_png)(DSLObj parent, float x, float y, float w, float h, const char *png_path, int nearest);
    void (*tooltip)(DSLObj target, const char *title, const char *body);   /* game tooltip on hover; NULL body = off */
    /* the game's notice box (CommonNoticeView). cancel_label NULL = one button. */
    void (*notice)(const char *text, const char *ok_label, const char *cancel_label, dsl_ui_cb ok, dsl_ui_cb cancel, void *ud);
    void (*place)(DSLObj w, float x, float y, float w_, float h);
    void (*set_active)(DSLObj w, int on);
    void (*set_enabled)(DSLObj w, int on);   /* buttons / toggles / fields: grey + no click */
    void (*destroy)(DSLObj w);
    size_t (*clipboard_get)(char *out, size_t cap);
    void (*clipboard_set)(const char *s);
    /* appended (check size): close the notice box this kit showed last, no callback (e.g. a "please wait" box) */
    void (*notice_close)(void);
    /* appended (check size): preferred height of a text widget's text at width w (0 = its own width), at its design
       size, so a caller can grow the box to its text (the game's way for long text; CJK lines are taller) */
    float (*text_height)(DSLObj text, float w);
    /* appended (check size; API 5): the game's own slider (clone of the Options window's): label  ===o===  value.
       Whole numbers min..max by step; change(ud, w) after the player moved it to another step (read slider_get).
       NULL when the game's slider is not available (use value_row then). */
    DSLObj (*slider)(DSLObj parent, float x, float y, float w, const char *label, int min, int max, int step, int value,
                     dsl_ui_cb change, void *ud);
    int (*slider_get)(DSLObj s);
    void (*slider_set)(DSLObj s, int value);  /* no change callback */
    /* appended (check size, or use the wrappers below; kit 0.1.28): the Escape key. The kit owns it: one press closes
       the top-most open window of the ones pushed here (the last pushed that is still shown), and the game does not see
       that press (no pause menu, a stock window under it stays). With none of them shown, Escape goes to the game as
       before. close(ud) runs on the main thread between frames; it does exactly what the window's own Close / Cancel /
       X does, cleanup included, and calls esc_drop (or destroys `win`). A notice box of this kit is pushed by the kit
       itself (Escape = its Cancel; OK when it has one button). A text field with the keyboard: that Escape only leaves
       the field (the game's field drops the typed text); flags DSL_ESC_EDIT = the window is an edit row or box whose
       Cancel drops the edit, so that same Escape also closes it. Push a window again when it shows again (it goes on
       top). 0 = ok.
       ESCAPE RULE (owner 2026-10-25, every window of every mod, now and later; ../NOTES.md "Escape"): one press acts on
       the top window only. Register, right where it is built or shown:
         - a window or panel with a Close / X / Cancel: esc_push(win, its_close, ud, 0); esc_drop when it hides;
         - a box inside a window (confirm, paste, rename): push the box when it shows (it goes on top), close = its Cancel;
           an edit row or box with a text field: flags DSL_ESC_EDIT;
         - a sub-page with a Back button (a Settings page, a Details page): Escape goes Back one level first, then the
           window closes. Push the sub-page's own body when it shows (close = its Back), or let the window's close run
           the Back while a sub-page is open (it returns without closing);
         - this kit's notice box: nothing to do (the kit pushes it);
         - a HUD panel with no Close (a meter, a log, a loading overlay): esc_push(panel, NULL, NULL, DSL_ESC_HUD): it
           never takes Escape, and it says "not a window" to the check below.
       Test copies (background-only or DSRIG_CTL=1): a root panel of this kit (parent NULL) that shows 30 frames after it
       was made and is neither pushed (itself or a part of it, or a panel made in the same frame: its dim backdrop) nor
       DSL_ESC_HUD logs "ESC-MISSING <its first text>" in dsload.log; the esc tests fail on that line. */
    int (*esc_push)(DSLObj win, void (*close)(void *ud), void *ud, int flags);
    void (*esc_drop)(DSLObj win);            /* the window closed (or hid) another way */
} DSLUi;
enum { DSL_ESC_EDIT = 1, DSL_ESC_HUD = 2 };   /* esc_push flags (DSL_ESC_HUD: close may be NULL) */

typedef struct DSLHost {
    /* ---- API 1 (layout frozen) */
    uint32_t api_version;
    uint32_t size;                           /* sizeof(DSLHost) of the loader; fields only get appended */
    const char *game_build;                  /* Steam build, e.g. "25846389" (from the GameAssembly file, known in every phase) */
    uintptr_t ga_base;                       /* 0 in dsl_module_load */
    void (*log)(DSLPlugin *self, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    const char *(*plugin_id)(DSLPlugin *self);
    void *(*rva)(uintptr_t rva);
    int (*il2cpp_ready)(void);
    void *(*il2cpp_fn)(const char *export_name);
    void *(*find_class)(const char *namespaze, const char *name);
    void *(*find_method)(void *klass, const char *name, int argc);
    void *(*method_address)(void *method_info);
    void *(*find_method_address)(const char *namespaze, const char *klass, const char *method, int argc);
    int32_t (*field_offset)(void *klass, const char *name);
    int (*hook)(DSLPlugin *self, void *target, void *replacement, void **orig);
    int (*hook_method)(DSLPlugin *self, const char *namespaze, const char *klass, const char *method,
                       int argc, void *replacement, void **orig);
    int (*unhook)(DSLPlugin *self, void *target);

    /* ---- API 2 (appended) */
    int (*hook_rva)(DSLPlugin *self, uintptr_t rva, void *replacement, void **orig, const char *name);
    /* per-build table of this module (mod.json "hooks"/"tables": <build> -> <platform> -> tsv "name<TAB>0xRVA"):
       ga_base + rva, or NULL if the name is not in the table */
    void *(*table)(DSLPlugin *self, const char *name);
    const char *(*module_dir)(DSLPlugin *self);    /* folder of the module (its mod.json) */
    const char *(*ga_path)(void);                   /* GameAssembly.dylib path, NULL before it is mapped */
    int (*module_loaded)(const char *id);           /* 1 = that module is active in this process */
    /* in-place code patch: `old` must match the current bytes; n <= 16, multiple of 4. Never overlaps a hook or
       another module's patch. Undone when the module fails. 0 = ok. */
    int (*patch_rva)(DSLPlugin *self, uintptr_t rva, const void *old, const void *neu, size_t n, const char *name);
    /* JSON of the running mod set (dsload_modset.json): {"gameplay":[{"id","version","files":{name:sha256}}],"client":[..],
       "session_modset", "check", ...}. Copies into buf (if any), returns the full length. Valid after dsl_plugin_init. */
    size_t (*gameplay_mods)(char *buf, size_t n);

    /* ---- API 3 (appended; ~/ds_multiplayer/research/MASTER_PLAN.md 2.2 A). Every callback below runs on the game's
       main thread between frames, never inside a game function. `ud` = the caller's pointer, passed back as is. */

    /* soft restart (flow J): the game's own loading screen covers the screen, a hidden new game process starts with
       `profile` (a profile JSON path, {"enabled":[ids],"order":[ids]}), returns to the same place (title page, or the
       same world after the game's own autosave, same camera / zoom / speed), then the windows swap. env_lines =
       "KEY=value\n" lines that replace those keys in the new process, or NULL. DSL_SOFT_LOAD=<save name> (from the
       title) = the new process opens that save (flow K). DSL_SOFT_EXE=<game executable> (+ its DYLD_INSERT_LIBRARIES)
       = start another play copy (a set with data mods). Main thread, title or single-player world only; refused
       (-1, reason in dsload.log) while session_busy() is set. 0 = started; failure or 90 s = old mods stay. */
    int (*soft_restart)(const char *profile, const char *env_lines);
    /* a running multiplayer session (or any mod state that must not be restarted under): reason text, NULL = free.
       While set: soft restart refused, the Mods tab's Apply is disabled and shows the reason. */
    void (*set_session_busy)(DSLPlugin *self, const char *reason);
    const char *(*session_busy)(void);        /* NULL = free, else the reason of the first module that set it */

    /* Mods window tabs (the loader draws the window, the Mud Slogel title and the tab strip; "Mods" = the loader's
       own tab, always first). The module fills `root` (a RectTransform, w x h pixels, origin top-left) with
       dsl_ui widgets in open(); update() every frame while the tab shows; close() when the tab or window closes
       (widgets under root are destroyed by the loader after close). can_close() == 0 keeps the tab open (unsaved
       edits); NULL = always. order: lower = further left (Mods = 0; Custom Modes uses 10..50). 0 = ok. */
    int (*menu_add_tab)(DSLPlugin *self, const struct DSLTab *tab);
    int (*menu_open)(const char *tab_id);     /* open the Mods window at that tab (NULL = Mods); 0 = ok */

    /* Title screen button in the stock column style (clone of SubUI_TitleMenuButton), below the stock column and the
       multiplayer clones. order: lower = higher up (the loader's "Mods" = 100). tip = game tooltip body or NULL.
       Calling it again with the same self, click and ud gives that button a new label / tip (e.g. after the game's
       language changed) instead of adding a second button: API 4 (host->api_version >= 4); an API 3 loader adds
       a second button, so do not call it again there. */
    int (*title_add_button)(DSLPlugin *self, const char *label, int order, const char *tip,
                            void (*click)(void *ud), void *ud);

    /* Load list (stock Load screen rows and the New Campaign screen). The loader owns the row hooks
       (SubUI_SaveFileButton.SetData, SubUI_SettlerNameCase.Init); modules add text and filters through it.
       loadrow_add: text(ud, guid, save_name, out, cap) writes one line for that save (empty = none); the loader
       stacks the lines of every module under the save name. click (optional) = the line was clicked. */
    int (*loadrow_add)(DSLPlugin *self, const struct DSLLoadRow *row);
    /* loadlist_filter: show(ud, guid, save_name, toggle_on) -> 1 show / 0 hide. The loader adds ONE toggle row per
       filter above the list with label(ud) (e.g. "Show multiplayer worlds"); toggle_on = its state (starts 0).
       mode(ud) is asked each time the list opens; DSL_LIST_OFF = this filter does nothing for this opening. */
    int (*loadlist_filter)(DSLPlugin *self, const struct DSLLoadFilter *f);
    void (*loadlist_refresh)(void);           /* re-run texts + filters on the open list (after a tag removal, ...) */

    const struct DSLUi *ui;                   /* widget kit, NULL until il2cpp_init returned */
    const char *(*data_dir)(void);            /* <game data>/Mods (profiles, groups.json, worlds.json, units/) */

    /* ---- appended after API 4 (no version change: check DSL_HAS(host, title_button_icon), or use the wrapper) */
    /* A picture at the left of this module's title button (the one of title_add_button with the same self, click and
       ud). The loader keeps a size x size box (units) at the button's left edge, centred in height, and the label in
       the rest of the button (its text centred there), from the first frame the button is drawn: the button never
       changes size or moves. fill(ud, box, fresh) runs right before a frame is drawn: fresh = 1 with a new, empty box
       (a new title view, a language change), 0 each time the button shows again after it was hidden (a sub-page's
       Back); the module puts its picture under box (a RectTransform; a picture there must not take clicks). Call it
       after title_add_button. 0 = ok, -1 = no such button. */
    int (*title_button_icon)(DSLPlugin *self, void (*click)(void *ud), void *ud, float size,
                             void (*fill)(void *ud, DSLObj box, int fresh));

    /* ---- API 5 (appended): mod settings. mod.json "settings": [{"id", "type": "toggle" | "int" | "choice", "default",
       "min" / "max" / "step" (int), "input": "number" (int, optional: a typed number field instead of the slider, for
       exact values in a wide range such as a port; digits only, values outside min..max are refused), "choices": [{"id", "label", "i18n": {"<code>": "..."}}] (choice), "scope":
       "gameplay" (default: the simulation uses it; every multiplayer player needs the same value, part of the mod-set
       check) | "local" (display only), "label", "help", "i18n": {"<code>": {"label", "help"}}}]. Ids: a-z 0-9 _ . -
       The player changes them in the Mods window (Settings of that mod). Values come from files: these work in every
       phase, dsl_module_load included. A gameplay value never changes in a running process (a change applies with the
       next Apply, a soft restart); a local value changes live. `dflt` is returned for an id mod.json does not declare
       (or a value of the wrong type). get_choice returns the choice id (valid until the next change of that setting). */
    int (*settings_get_bool)(DSLPlugin *self, const char *id, int dflt);
    int (*settings_get_int)(DSLPlugin *self, const char *id, int dflt);
    const char *(*settings_get_choice)(DSLPlugin *self, const char *id, const char *dflt);
    /* changed(ud, id) on the game's main thread after a local setting of this module changed (Settings page). Several
       calls add several callbacks. 0 = ok. */
    int (*settings_on_change)(DSLPlugin *self, void (*changed)(void *ud, const char *id), void *ud);

    /* ---- API 6 (appended): this mod's data folder, <game data>/ModData/<mod id> (made if missing), in the form the
       module's own fopen opens. Kept when the mod is updated, turned off or on (put settings, progress, campaign files
       there, never in module_dir). mod.json "sync": a list of patterns (examples in ../NOTES.md) = files of this folder
       that every multiplayer player needs the same (relative, '/' only; * and ? inside a name, ** = any folders; names
       A-Z a-z 0-9 . _ - and inner spaces; no program files). A guest gets the host's copy of them when it joins
       (at most 1 MB and 256 files per mod) and its own come back after the session; a module reads them again when a
       world loads. The same folder for .NET mods: env DSML_MOD_DATA (= <game data>/ModData), else
       Application.persistentDataPath + "/ModData"; <game data>/ModData/moddata.json lists the folders and the
       session ("guest" = the host's files are in place). NULL = no folder (bad id). */
    const char *(*mod_data_dir)(DSLPlugin *self);
} DSLHost;
#define DSL_HAS_TITLE_BUTTON_ICON 1               /* this header has title_button_icon (older copies of it do not) */

#ifndef DSL_PLUGIN_API
#define DSL_PLUGIN_API 3                      /* the API a module needs from its loader (API 4 = runtime check only) */
#endif
#define DSL_PLUGIN() __attribute__((visibility("default"))) const uint32_t dsl_plugin_api_version = DSL_PLUGIN_API

typedef int (*dsl_plugin_init_fn)(const DSLHost *host, DSLPlugin *self);
typedef int (*dsl_module_load_fn)(const DSLHost *host, DSLPlugin *self);
typedef int (*dsl_module_early_fn)(const DSLHost *host, DSLPlugin *self);

#define DSL_EXPORT __attribute__((visibility("default")))

/* API 3 wrappers: -1 / NULL when the loader is older than API 3 */
#define DSL_HAS(host, field) ((host) && (host)->size >= offsetof(DSLHost, field) + sizeof((host)->field) && (host)->field)
static inline int dsl_soft_restart(const DSLHost *h, const char *profile, const char *env_lines) {
    return DSL_HAS(h, soft_restart) ? h->soft_restart(profile, env_lines) : -1; }
static inline void dsl_set_session_busy(const DSLHost *h, DSLPlugin *self, const char *reason) {
    if (DSL_HAS(h, set_session_busy)) h->set_session_busy(self, reason); }
static inline int dsl_menu_add_tab(const DSLHost *h, DSLPlugin *self, const DSLTab *t) {
    return DSL_HAS(h, menu_add_tab) ? h->menu_add_tab(self, t) : -1; }
static inline int dsl_title_add_button(const DSLHost *h, DSLPlugin *self, const char *label, int order, const char *tip,
                                       void (*click)(void *), void *ud) {
    return DSL_HAS(h, title_add_button) ? h->title_add_button(self, label, order, tip, click, ud) : -1; }
static inline int dsl_loadrow_add(const DSLHost *h, DSLPlugin *self, const DSLLoadRow *r) {
    return DSL_HAS(h, loadrow_add) ? h->loadrow_add(self, r) : -1; }
static inline int dsl_loadlist_filter(const DSLHost *h, DSLPlugin *self, const DSLLoadFilter *f) {
    return DSL_HAS(h, loadlist_filter) ? h->loadlist_filter(self, f) : -1; }
static inline const DSLUi *dsl_ui(const DSLHost *h) { return DSL_HAS(h, ui) ? h->ui : NULL; }
static inline int dsl_title_button_icon(const DSLHost *h, DSLPlugin *self, void (*click)(void *), void *ud, float size,
                                        void (*fill)(void *ud, DSLObj box, int fresh)) {
    return DSL_HAS(h, title_button_icon) ? h->title_button_icon(self, click, ud, size, fill) : -1; }
/* API 5 wrappers (mod settings): dflt when the loader is older than API 5 or the id is not in mod.json */
static inline int dsl_get_bool(const DSLHost *h, DSLPlugin *self, const char *id, int dflt) {
    return DSL_HAS(h, settings_get_bool) ? h->settings_get_bool(self, id, dflt) : dflt; }
static inline int dsl_get_int(const DSLHost *h, DSLPlugin *self, const char *id, int dflt) {
    return DSL_HAS(h, settings_get_int) ? h->settings_get_int(self, id, dflt) : dflt; }
static inline const char *dsl_get_choice(const DSLHost *h, DSLPlugin *self, const char *id, const char *dflt) {
    return DSL_HAS(h, settings_get_choice) ? h->settings_get_choice(self, id, dflt) : dflt; }
/* API 6 wrapper: NULL when the loader is older than API 6 */
static inline const char *dsl_mod_data_dir(const DSLHost *h, DSLPlugin *self) {
    return DSL_HAS(h, mod_data_dir) ? h->mod_data_dir(self) : NULL; }
static inline int dsl_on_setting_change(const DSLHost *h, DSLPlugin *self, void (*changed)(void *ud, const char *id), void *ud) {
    return DSL_HAS(h, settings_on_change) ? h->settings_on_change(self, changed, ud) : -1; }
/* Escape wrappers (kit 0.1.28): -1 / nothing with an older kit, whose Escape does nothing for these windows */
#define DSL_UI_HAS(ui, field) ((ui) && (ui)->size >= offsetof(DSLUi, field) + sizeof((ui)->field) && (ui)->field)
static inline int dsl_esc_push(const DSLUi *ui, DSLObj win, void (*close)(void *ud), void *ud, int flags) {
    return DSL_UI_HAS(ui, esc_push) ? ui->esc_push(win, close, ud, flags) : -1; }
static inline void dsl_esc_drop(const DSLUi *ui, DSLObj win) { if (DSL_UI_HAS(ui, esc_drop)) ui->esc_drop(win); }

#ifdef __cplusplus
}
#endif
