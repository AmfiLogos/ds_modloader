// IL2CPP name lookups through GameAssembly's exported il2cpp API (resolved from its symbol table, never linked).
#include "dsl_internal.h"
#include <string.h>

int dsl_ready;
/* Every export the loader itself calls. test/check_symbols.sh greps this list from the binary. */
static void *(*p_domain_get)(void);
static void **(*p_domain_get_assemblies)(void *domain, size_t *n);
static void *(*p_assembly_get_image)(void *assembly);
static void *(*p_class_from_name)(void *image, const char *ns, const char *name);
static void *(*p_class_get_method_from_name)(void *klass, const char *name, int argc);
static void *(*p_class_get_field_from_name)(void *klass, const char *name);
static size_t (*p_field_get_offset)(void *field);

int dsl_il2cpp_bind(void) {
    int miss = 0;
#define B(var, name) do { *(void**)&var = dsl_ga_sym(name); if (!var) { dsl_log("il2cpp export %s not found", name); miss++; } } while (0)
    B(p_domain_get, "il2cpp_domain_get");
    B(p_domain_get_assemblies, "il2cpp_domain_get_assemblies");
    B(p_assembly_get_image, "il2cpp_assembly_get_image");
    B(p_class_from_name, "il2cpp_class_from_name");
    B(p_class_get_method_from_name, "il2cpp_class_get_method_from_name");
    B(p_class_get_field_from_name, "il2cpp_class_get_field_from_name");
    B(p_field_get_offset, "il2cpp_field_get_offset");
#undef B
    return miss;
}

void *dsl_il2cpp_fn(const char *name) {
    if (!name || strncmp(name, "il2cpp_", 7) != 0) return NULL;
    return dsl_ga_sym(name);
}

void *dsl_find_class(const char *ns, const char *name) {
    if (!dsl_ready || !p_domain_get || !name) return NULL;
    void *dom = p_domain_get(); if (!dom) return NULL;
    size_t n = 0; void **as = p_domain_get_assemblies(dom, &n);
    for (size_t i = 0; as && i < n; i++) {
        void *img = p_assembly_get_image(as[i]); if (!img) continue;
        void *k = p_class_from_name(img, ns ? ns : "", name);
        if (k) return k;
    }
    return NULL;
}

void *dsl_find_method(void *klass, const char *name, int argc) {
    if (!dsl_ready || !klass || !name) return NULL;
    return p_class_get_method_from_name(klass, name, argc);
}

void *dsl_method_address(void *mi) { return mi ? *(void**)mi : NULL; }   /* MethodInfo.methodPointer is the first field */

void *dsl_find_method_address(const char *ns, const char *klass, const char *method, int argc) {
    void *k = dsl_find_class(ns, klass);
    if (!k) { dsl_log("class %s%s%s not found", ns && *ns ? ns : "", ns && *ns ? "." : "", klass); return NULL; }
    void *m = dsl_find_method(k, method, argc);
    if (!m) { dsl_log("method %s.%s (%d args) not found", klass, method, argc); return NULL; }
    void *a = dsl_method_address(m);
    if (!a) dsl_log("method %s.%s has no native code", klass, method);
    return a;
}

int32_t dsl_field_offset(void *klass, const char *name) {
    if (!dsl_ready || !klass || !name) return -1;
    void *f = p_class_get_field_from_name(klass, name);
    return f ? (int32_t)p_field_get_offset(f) : -1;
}
