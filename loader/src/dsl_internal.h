// Loader internals (not part of the module SDK).
#pragma once
#include <stdint.h>
#include <stddef.h>

#define DSL_LOADER_VERSION "2.0.0"
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
int dsl_soft_wanted(void);                   /* 1 = this process is B of a soft restart */
int dsl_soft_apply(const char *profile, const char *env_lines);   /* host->soft_restart */
