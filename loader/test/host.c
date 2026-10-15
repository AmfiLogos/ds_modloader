// Test host = stands in for UnityPlayer: calls il2cpp_init, then runs "game code".
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
    return 0;
}
