// probe.c - an injected library (as dsload): appends the path of every process it is loaded into to probe_hits.txt
// next to itself (the play copy's starter must never be one of them)
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
__attribute__((constructor)) static void hit(void) {
    Dl_info di; char p[PATH_MAX], exe[PATH_MAX]; uint32_t sz = sizeof exe;
    if (!dladdr((void *)hit, &di) || _NSGetExecutablePath(exe, &sz)) return;
    snprintf(p, sizeof p, "%s", di.dli_fname); *strrchr(p, '/') = 0; strlcat(p, "/probe_hits.txt", sizeof p);
    FILE *f = fopen(p, "a"); if (f) { fprintf(f, "%s\n", exe); fclose(f); }
}
