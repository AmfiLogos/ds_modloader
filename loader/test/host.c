// Test host = stands in for UnityPlayer: calls il2cpp_init, then runs "game code".
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <dlfcn.h>
extern int il2cpp_init(const char *);
extern int fake_orig_init_calls, fake_orig_safe, fake_orig_normal, fake_orig_tick, fake_orig_update, fake_orig_foreign;
extern int Patch_Target(void), fake_load(const char *), fake_cancel_calls; extern char fake_cancel_text[];
extern void fake_transfer(void *sim, int safe), fake_tick(void *sim), fake_frame(void), fake_foreign(void);
int main(void) {
    int r = il2cpp_init("IL2CPP Root Domain");
    uint8_t *sim = calloc(1, 0x100);
    fake_transfer(sim, 1); fake_transfer(sim, 1); fake_transfer(sim, 0); fake_tick(sim); fake_frame(); fake_frame(); fake_foreign();
    const char *e = getenv("DYLD_INSERT_LIBRARIES");
    printf("RESULT init_ret=%d orig_init=%d orig_safe=%d orig_normal=%d orig_tick=%d orig_update=%d orig_foreign=%d floor_min=%d safe=%d\n",
           r, fake_orig_init_calls, fake_orig_safe, fake_orig_normal, fake_orig_tick, fake_orig_update, fake_orig_foreign, *(int32_t*)(sim + 0x80), sim[0x85]);
    int s1 = fake_load("save1"), s2 = fake_load("save2");
    printf("PATCH target=%d\nWORLD save1_state=%d save2_state=%d cancels=%d text=%s\n", Patch_Target(), s1, s2, fake_cancel_calls, fake_cancel_text);
    printf("ENV DYLD_INSERT_LIBRARIES=%s\n", e ? e : "(unset)");
    const char *cm = getenv("HOST_COPY_MODS");          /* "<from> <to>": the loader export multiplayer calls for a world's single-player copy */
    if (cm) { char a[64], b[64]; int (*f)(const char *, const char *) = (int (*)(const char *, const char *))dlsym(RTLD_DEFAULT, "dsl_world_copy_mods");
              if (sscanf(cm, "%63s %63s", a, b) == 2) printf("COPYMODS rc=%d\n", f ? f(a, b) : -9); }
    const char *wf = getenv("HOST_WAIT_FILE");          /* update test: stay up (max 60 s) until the loader's thread wrote it */
    for (int i = 0; wf && *wf && i < 600 && access(wf, 0); i++) usleep(100000);
    return 0;
}
