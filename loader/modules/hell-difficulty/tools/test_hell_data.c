// Host test of hell_data.c (no game): rebuild the Hell bundle from a stock bundle + delta with a fake host.
//   cc -I../../../sdk test_hell_data.c -o /tmp/t && /tmp/t <module dir with rva/<build>_<os>_tables.dsdelta> <fake GameAssembly path>
#include "../hell_data.c"
#include <stdarg.h>
static const char *MD, *GA;
static void lg(DSLPlugin *s, const char *fmt, ...) { (void)s; va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); putchar('\n'); }
static const char *mdir(DSLPlugin *s) { (void)s; return MD; }
static const char *gap(void) { return GA; }
static void *ifn(const char *n) { (void)n; return NULL; }
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    MD = argv[1]; GA = argv[2];
    DSLHost h; memset(&h, 0, sizeof h);
    h.size = sizeof h; h.game_build = "25846389"; h.log = lg; h.module_dir = mdir; h.ga_path = gap; h.il2cpp_fn = ifn;
    int r = hell_tables_init(&h, NULL);
    printf("rc %d (11 = built/checked, no il2cpp in this test)\n", r);
    return r == 11 ? 0 : 1;
}
