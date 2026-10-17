// dsl_sdk.h - module SDK for the Dungeon Settlers mod loader (dsload.dylib), API version 4.
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
// A module may export `int dsl_world_check_skip(void)`: 1 = the world check (mods per world) lets the current load
// through (multiplayer guest loading the host's world).
//
// Log: <log dir>/dsload.log (DSL_LOG_DIR, else ~/Library/Application Support/CanOpener/Dungeon Settlers).
#pragma once
#include <stdint.h>
#include <stddef.h>

#define DSL_API_VERSION 4

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
} DSLUi;

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
} DSLHost;

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

#ifdef __cplusplus
}
#endif
