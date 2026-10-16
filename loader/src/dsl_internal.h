// Loader internals (not part of the module SDK).
#pragma once
#include <stdint.h>
#include <stddef.h>

#define DSL_LOADER_VERSION "3.0.0"
#define DSL_OWNER_LOADER (-1)

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

/* soft restart (dsl_soft.c) */
void dsl_soft_snapshot(const char *logdir);  /* constructor, before the env is edited */
void dsl_soft_install(void);                 /* after il2cpp_init */
int dsl_soft_busy(void);                     /* a restart runs (A) or is not finished (B) */
int dsl_soft_wanted(void);                   /* 1 = this process is B of a soft restart */
int dsl_soft_apply(const char *profile, const char *env_lines);   /* host->soft_restart */

/* API 3 pieces (dsl_sdk.h API 3; MASTER_PLAN 2.2 A) */
struct DSLUi; struct DSLLoadRow; struct DSLLoadFilter;
/* dsl_ui.c: widget kit + the frame tick (EventSystem.Update -> dsl_frame_hook) */
void dsl_ui_install(void);
int dsl_ui_title_add(const char *owner_id, const char *label, int order, const char *tip, void (*click)(void *), void *ud);
void dsl_frame_hook(void);                               /* dsl_reg.c: every frame, main thread */
/* dsl_rows.c: load list service (stock Load list rows, its filter toggles, the New Campaign line). No hook on the
   row sites (multiplayer owns them until it moves to this API): polling the open SaveLoadView each frame. */
struct DSLHost; void dsl_cloud_install(struct DSLHost *host);   /* dsl_cloud.c: after dsl_reg_ready */
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
int dsl_key_same(const char *a, const char *b);            /* gameplay keys equal (old keys with "multiplayer" too) */
void dsl_world_mismatch(const char *save_name, const char *guid, const char *key);   /* dsl_world.c refused: ask (flow K) */
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
int dsl_soft_shot(const char *png_path);                 /* test: full frame incl. UI */
void *dsl_soft_mainflow(void);                           /* dsl_soft.c: the live MainFlow or NULL */
#define MAXP_REG 32
