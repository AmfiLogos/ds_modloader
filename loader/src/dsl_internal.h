// Loader internals (not part of the module SDK).
#pragma once
#include <stdint.h>
#include <stddef.h>

#define DSL_LOADER_VERSION "3.2.0"         /* internal loader API number: mods' "requires": ["loader>=x"]. Never shown */
#ifndef DSL_RELEASE                          /* the release number players see (repo file RELEASE, set by the builds) */
#define DSL_RELEASE "dev"
#endif
#define DSL_OWNER_LOADER (-1)

/* player text in the game's language: TR("English"), TRN(one, other, n) (ds_i18n.h; its implementation is in
   dsl_main.c). Files: <folder of the loader binary>/lang/<code>.json (Mac ROOT/loader/lang, Windows ROOT\game\lang). */
#include "ds_i18n.h"
void dsl_lang_early(void);               /* dsl_main.c: lang folder + UserSetting.json language, before il2cpp */
int dsl_lang_poll(void);                 /* dsl_main.c, main thread once per frame: the game's live language; 1 = changed */
void dsl_reg_lang_changed(void);         /* dsl_reg.c: after a change (the kit has dropped its fonts): relabel, rebuild */

extern uintptr_t dsl_ga_base;            /* GameAssembly arm64 load address, 0 before it is mapped */
extern uint64_t dsl_text_lo, dsl_text_hi;/* __TEXT code range (rva) */
extern uint64_t dsl_image_end;           /* end of the last segment (rva) */
extern const char *dsl_build_id;         /* NULL = unknown build: loader inactive */
extern int dsl_ready;                    /* il2cpp_init returned */

void dsl_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *dsl_owner_name(int owner);
void dsl_mark_codepatch(void);

/* hook engine (dsl_engine.c): the only code that writes GameAssembly code or hook stubs in this process */
int dsl_engine_init(void);               /* after GameAssembly is mapped; 0 = ok */
void dsl_engine_batch(int on);           /* 1: hooks are queued (no game code can run yet), 0: all enabled at once */
void dsl_engine_set_late(void);          /* game code may run on other threads: new stubs go to the late page */
int dsl_hook(int owner, void *target, void *repl, void **orig, const char *who);
int dsl_unhook(int owner, void *target, const char *who);
int dsl_unhook_owner(int owner);
int dsl_hook_count(int owner);           /* active hooks + code patches of an owner (-2 = all) */
int dsl_patch(int owner, void *target, const void *old, const void *neu, size_t n, const char *who);
extern int dsl_codepatch_ok;

/* il2cpp name lookups (dsl_il2cpp.c) */
int dsl_il2cpp_bind(void);
void *dsl_ga_sym(const char *name);
void *dsl_il2cpp_fn(const char *name);
void *dsl_find_class(const char *ns, const char *name);
void *dsl_find_method(void *klass, const char *name, int argc);
void *dsl_method_address(void *mi);
void *dsl_find_method_address(const char *ns, const char *klass, const char *method, int argc);
int32_t dsl_field_offset(void *klass, const char *name);

/* tiny JSON (dsl_json.c): parse into a tree; NULL on error */
typedef enum { JN_NULL, JN_BOOL, JN_NUM, JN_STR, JN_ARR, JN_OBJ } JType;
typedef struct JNode {
    JType t; double num; int b; char *str;       /* str: JN_STR value */
    char *key;                                   /* member name inside an object */
    struct JNode *child, *next;
} JNode;
JNode *json_parse(const char *text);
void json_free(JNode *n);
JNode *json_get(const JNode *obj, const char *key);
const char *json_str(const JNode *obj, const char *key);  /* NULL if missing / not a string */

/* auto-update (dsl_update.c) */
void dsl_update_start(void);                 /* after the modules' init: check thread */
void dsl_update_start_kit_only(void);       /* mods off (unknown / not pinned build): kit check + download only */
int dsl_update_will_install(void);          /* 1 = a newer kit is fetched and installed at the next start */
void dsl_update_frame(void);                 /* every frame (title corner, confirm, the starter's notice) */
/* crash reports (dsl_crash.c) */
void dsl_crash_boot(void);                   /* start part, after dsl_soft_snapshot: the previous session's end */
void dsl_crash_start(void);                  /* after the modules' init: the quit mark */
void dsl_crash_frame(void);                  /* every frame (consent notice, desync / join scan, sending) */
/* signed updates (dsl_sig.c): sig_b64 = a .sig file's text; the trusted key's index, -1 = not a valid signature */
int dsl_sig_check(const void *msg, size_t n, const char *sig_b64);
int dsl_sig_dev(void);                             /* 1 = a test build that trusts the public dev key */
/* dsl_update.c helpers for "Get the host's mods" (any thread) */
const char *dsl_update_mod_repo(const char *id);   /* the official repo of a mod id (MOD_REPOS); NULL = not an official mod */
int dsl_update_ready(void);                        /* 1 = ROOT/update known (downloads possible in this start) */
const char *dsl_update_dir(void);                  /* ROOT/update ("" = unknown) */
int dsl_update_vcmp(const char *a, const char *b); /* "0.1.10" > "0.1.9": -1 / 0 / 1 */
/* exact official release: tag "<id>-v<ver>", asset "<id>-<ver>.zip" + its sha256. 0 found, 1 none, -1 no answer / not official */
int dsl_update_find_mod_release(const char *id, const char *ver, char *url, size_t ucap, char *asset, size_t acap, char sha[65]);
int dsl_update_fetch(const char *url, const char *asset, const char *sha, const char *sub, char *out, size_t n);   /* ROOT/update/<sub>/<asset>, sha256 checked; 0 = ok */
/* "Get the host's mods" (dsl_hostmods.c; its exports dsl_hostmods_plan / _get / _state are for the multiplayer mod) */
void dsl_hostmods_frame(void);               /* main thread, every frame (dsl_reg.c) */
/* dsl_reg.c */
int dsl_apply_mods_env(const char *const *ids, int n, const char *env, char *why, size_t cap);   /* Apply + env lines for the new process */
void dsl_mods_rescan(void);                  /* read the mods folder again (a mod installed while the game runs) */
/* soft restart (dsl_soft.c) */
void dsl_soft_snapshot(const char *logdir);  /* constructor, before the env is edited */
void dsl_soft_install(void);                 /* after il2cpp_init */
/* fast boot (dsl_fastboot.c): bundle CRC skip for files that passed the game's check before */
void dsl_fastboot_install(const char *logdir); /* after il2cpp_init (from dsl_soft_install) */
void dsl_fastboot_tick(void);                /* main thread, every soft tick */
void dsl_fastboot_yield(void *target, const char *who);   /* before a module hooks <target>: fast boot gives it up if it is its method */
int dsl_fastboot_part(const char *env);      /* one boot speed-up part on? <env> "1"/"0", else DSL_FASTBOOT "1"/"0", else DSL_FB_DEFAULT */
int dsl_soft_title_idle(void);               /* title on screen, boot done, nothing loading, no loading screen */
int dsl_soft_busy(void);                     /* a restart runs (A) or is not finished (B) */
int dsl_soft_input_blocked(void);            /* A: the title under the loading screen takes no input now */
void dsl_soft_reopen_request(const char *what); /* A: the next Apply's new game opens this ("mods"), NULL = none */
const char *dsl_soft_take_reopen(void);      /* B: what to open now that it is on screen (once), else NULL */
int dsl_soft_wanted(void);                   /* 1 = this process is B of a soft restart */
int dsl_soft_apply(const char *profile, const char *env_lines);   /* host->soft_restart */

/* API 3 pieces (dsl_sdk.h API 3; MASTER_PLAN 2.2 A) */
struct DSLUi; struct DSLLoadRow; struct DSLLoadFilter;
/* dsl_ui.c: widget kit + the frame tick (EventSystem.Update -> dsl_frame_hook) */
void dsl_ui_install(void);
int dsl_ui_title_add(const char *owner_id, const char *label, int order, const char *tip, void (*click)(void *), void *ud);
int dsl_ui_title_icon(const char *owner_id, void (*click)(void *), void *ud, float size, void (*fill)(void *, void *, int));   /* fill(ud, box (DSLObj), fresh) */
void dsl_frame_hook(void);                               /* dsl_reg.c: every frame, main thread */
/* dsl_rows.c: load list service (stock Load list rows, its filter toggles, the New Campaign line). No hook on the
   row sites (multiplayer owns them until it moves to this API): polling the open SaveLoadView each frame. */
struct DSLHost; void dsl_cloud_install(struct DSLHost *host);   /* dsl_cloud.c: after dsl_reg_ready */
/* "mp_*" = a multiplayer temporary save (mp_sync join point, mp_fullcheck full-state check, test saves): the mod writes
   it through the game's Save and reads it back from <DSML_DATA>/Saves by path. Never a game save (dsl_saves.c), never
   in the Steam Cloud backup (dsl_cloud.c). One rule for both. */
static inline int dsl_save_is_mp_temp(const char *name) { return (name[0] | 32) == 'm' && (name[1] | 32) == 'p' && name[2] == '_'; }
/* dsl_saves.c: the game's own save folder listed and played in place next to the modded saves */
void dsl_saves_tutorial_import(void);                    /* before GameAssembly: the normal game's tutorial keys -> modded OutGame.json */
void dsl_saves_install(void);                            /* after il2cpp_init, before dsl_cloud_install */
int dsl_saves_is_game(const char *save_name);            /* 1 = a save of the game's own folder */
void dsl_saves_loading(const char *save_name, const char *guid);   /* dsl_world.c: a load with its world guid */
int dsl_saves_load_gate(const char *save_name);          /* dsl_world.c: 1 = hold this load (save warning first) */
void dsl_saves_hidden(int *off, int *unread);            /* dsl_rows.c: game worlds not on the Load list (off / unreadable) */
void dsl_saves_off_note(int win, char *out, size_t cap); /* dsl_main.c: mods off this session: the worlds not listed, "" = none */
int dsl_saves_hidden_worlds(const char *dir, const char *not_in);   /* worlds of a save folder that are not also in not_in */
int dsl_module_syms(const char *name, void **out, int max);         /* dsl_main.c: an optional export of each running module */
void dsl_rows_install(void);                             /* after dsl_ui_install */
void dsl_rows_frame(void);                               /* from dsl_frame_hook */
int dsl_rows_add(const char *owner_id, const struct DSLLoadRow *r);
int dsl_rows_filter(const char *owner_id, const struct DSLLoadFilter *f);
void dsl_rows_refresh(void);
/* dsl_reg.c: mods per world (the loader's own row line and the New Campaign line) */
int dsl_world_label(const char *guid, char *out, size_t cap);        /* "Mods: Hell difficulty, Multiplayer" (or "Mods: none") */
void dsl_world_change(const char *guid, const char *save_name);       /* its [Change] was clicked */
void dsl_newworld_label(char *out, size_t cap);                      /* "Mods for this world: ..." */
void dsl_newworld_change(void);
int dsl_worlds_bad(const char **kept);                     /* dsl_reg.c: worlds.json unreadable at start (kept path) */
int dsl_key_same(const char *a, const char *b);            /* gameplay keys equal (old keys with "multiplayer" too) */
void dsl_world_key_eff(const char *guid, const char *stored, char *out, size_t cap);   /* dsl_reg.c: the set the world plays
                                                              with (its stored key + fixes on in all worlds - its offs) */
void dsl_world_mismatch(const char *save_name, const char *guid, const char *key);   /* dsl_world.c refused: ask (flow K) */
/* dsl_reg.c, custom difficulty (mod.json "difficulty": Hell): a world uses it only on its level (combat or management) */
void dsl_world_note_level(const char *guid, int combat, int management);   /* a world's levels (save header, open world) */
int dsl_world_key_match(const char *guid, const char *eff, const char *mine);   /* the world's set = this game's set, as for it */
void dsl_world_hello(const char *guid);                  /* the multiplayer set is about this world now ("" = the title) */
int dsl_mod_is_difficulty(const char *id);               /* mod.json "difficulty": runs whenever installed */
const char *dsl_mod_difficulty_name(const char *id);     /* its level's name ("Hell") */
int dsl_mod_in_hello(const char *id);                    /* in this game's multiplayer set now */
int dsl_world_save_dependent(const char *guid, const char **ids, int max, char *key, size_t kcap);   /* the running mods whose
                                                            saves need the loader, as that world uses them; count */
const char *dsl_reg_data_dir(void);
struct DSLHost; struct DSLPlugin;
void dsl_reg_host(struct DSLHost *h);                    /* constructor: API 3 host fields */
void dsl_reg_ready(struct DSLHost *h);                   /* il2cpp_init returned, before the modules' init */
void dsl_reg_after_init(void);                           /* after the modules' init */
/* dsl_main.c, for dsl_reg.c */
const char *dsl_mods_dir(void);
const char *dsl_profile_file(void);
const char *dsl_log_dir(void);
int dsl_plugin_index(struct DSLPlugin *self);
const char *dsl_plugin_name(struct DSLPlugin *self);
int dsl_mod_state(const char *id, const char **why);     /* 1 running, 2 failed, 3 skipped, 0 listed / unknown */
int dsl_mod_dep_text(const char *id, char *out, size_t cap);   /* a dependency reason (dsl_deps.h), 0 = none */
void dsl_deps_for_set(const char *const *ids, int n, int *ok);   /* the dependency rule for a set to start */
int dsl_soft_shot(const char *png_path);                 /* test: full frame incl. UI */
void *dsl_soft_mainflow(void);                           /* dsl_soft.c: the live MainFlow or NULL */
/* dsl_settings.c: mod settings (SDK API 5) */
struct DSLHost; void dsl_settings_host(struct DSLHost *h);   /* fills the API 5 host fields */
void dsl_settings_init(void);                            /* mod.json "settings" + settings.json + the profile's values */
void dsl_settings_rescan(void);                          /* mods folder changed (package installed) */
int dsl_settings_lines(const char *mod, char *out, size_t cap);   /* "<mod>/<id>@<value>" lines of its gameplay settings */
int dsl_settings_profile_member(char *out, size_t cap);  /* "settings":{...} for pending.json (0 = no gameplay setting) */
void dsl_settings_commit(void);                          /* B on screen: the Apply's gameplay values -> settings.json */
/* dsl_moddata.c: a data folder per mod (SDK API 6) + its multiplayer sync (exports dsl_moddata_pack / _copy / _apply /
   _session_end / _fingerprint are for the multiplayer mod) */
void dsl_moddata_host(struct DSLHost *h);                /* fills the API 6 host field */
void dsl_moddata_start(void);                            /* dsl_core_start: an unfinished guest session undone, env */
void dsl_moddata_after_init(void);                       /* mods known: folders + moddata.json */
void dsl_moddata_rescan(void);                           /* mods folder changed */
const char *dsl_moddata_root(void);                      /* <data>/ModData (UTF-8) */
int dsl_moddata_fingerprint(char out[65]);               /* data fingerprint of this copy; returns the sync mod count */
const char *dsl_moddata_fp_start(void);                 /* that fingerprint after the modules' init ("" = not computed) */
#define MAXP_REG 32
