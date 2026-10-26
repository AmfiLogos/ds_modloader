// scan_drv.c - test only, never shipped: the driver of loader/test/scan_rate_test.py (lane fpsscan, 2026-10-26). A minimal
// copy of the fpsaudit Mac probe (ds_multiplayer ext/fpsaudit/mac/fpsprobe.c). It hooks nothing in GameAssembly, so the
// loader's scan guard sees it only as "game":
//  - Unity calls every script callback through il2cpp_runtime_invoke, a function pointer UnityPlayer.dylib keeps in its own
//    data. Once GameAssembly is loaded, a helper thread swaps that pointer (UnityPlayer's copy only) for a wrapper; the
//    start of MainFlow.Update marks a frame, and a pending command runs there (main thread, between frames).
//  - commands: $SCAN_DRV_DIR/cmd.txt (one line), answer appended to cmd.out:
//      load <SaveName> | speed <0..3> | openload | keys <n> | popup <PopUpType> | unpopup <PopUpType>
//  - state.txt every 0.25 s: swapped, mf_seen, frames, scene, boot, busy, speed, tick, ak_calls, ak_true.
//  - keys <n> (env SCAN_DRV_KEYS=1): "UnityEngine.Input::get_anyKeyDown()" answers "a key went down" on one frame per second
//    for n seconds (batchmode has no input; the name WITH "()" is the form the multiplayer module resolves).
// It never calls a FindObject* method itself: openload finds the title view with FindFirstObjectByType through
// il2cpp_runtime_invoke, so the call comes from GameAssembly's invoker and counts as "game", never as a module.
// Build 25846389 addresses (the same as ds_multiplayer src/rig_cmd.c).
// Build (scan_rate_test.py does it into its out folder): clang -arch arm64 -O2 -fexceptions -dynamiclib -framework AppKit -lobjc
//        -o scan_drv.dylib scan_drv.c   (-fexceptions: the depth reset runs when a managed exception unwinds through w_invoke)
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

typedef void *Obj;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
static uintptr_t ga_base; static void *ga_h;
#define F(rva, T) ((T)(ga_base + (rva)))

static char dir[1024];
static pthread_t main_th;
static volatile int swapped, mf_seen;
static void *(*r_invoke)(void *, void *, void **, void **);
static void *mf_method, *mf;
static int depth;
static volatile int cmd_pending; static char cmd_line[1100]; static char cmd_out[1100];
static volatile int st_scene = -1, st_boot, st_busy, st_speed = -1; static volatile int64_t st_tick = -1; static volatile uint64_t st_frames;

static double wall(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec / 1e6; }

/* ---- game reads (build 25846389, rig_cmd.c) */
static Obj cdc(void) { return mf ? AT(mf, 0xC8, Obj) : NULL; }
static int scene(void) { return mf ? AT(mf, 0x18C, int32_t) : -1; }
static int64_t tick(void) { Obj c = cdc(); Obj t = c ? AT(c, 0xA8, Obj) : NULL; return t ? AT(t, 0x10, int64_t) : -1; }
static int speed(void) { Obj s = mf ? AT(mf, 0xD8, Obj) : NULL; s = s ? AT(s, 0x10, Obj) : NULL; return s ? AT(s, 0x10, int32_t) : -1; }
static int busy(void) {
    if (!mf) return 1;
    Obj sl = AT(mf, 0x38, Obj); if (sl && AT(sl, 0x10, int32_t) > 0) return 1;
    Obj ts = AT(mf, 0xF8, Obj); if (ts && AT(ts, 0x14, bool)) return 1;
    Obj lc = AT(mf, 0xF0, Obj); if (lc && F(0xC98A8C, bool (*)(Obj, void *))(lc, NULL)) return 1;
    return 0;
}
static void *md(uint64_t slot) { F(0x6FB6FC, void (*)(void **))((void **)(ga_base + slot)); return *(void **)(ga_base + slot); }
static Obj obj_new(uint64_t slot) { return F(0x6FB9C4, Obj (*)(void *))(md(slot)); }
static Obj str_new(const char *s) { return F(0x6F9738, Obj (*)(const char *))(s); }
static void enqueue(Obj ev) { F(0xCCFAD4, void (*)(Obj, Obj, void *))(AT(mf, 0x48, Obj), ev, NULL); }

/* FindFirstObjectByType(System.Type) called through il2cpp_runtime_invoke (the caller is GameAssembly's invoker) */
static Obj find_obj(const char *ns, const char *name) {
    void *(*domain_get)(void) = dlsym(ga_h, "il2cpp_domain_get");
    void **(*get_asms)(void *, size_t *) = dlsym(ga_h, "il2cpp_domain_get_assemblies");
    void *(*asm_image)(void *) = dlsym(ga_h, "il2cpp_assembly_get_image");
    void *(*cls_from)(void *, const char *, const char *) = dlsym(ga_h, "il2cpp_class_from_name");
    void *(*cls_type)(void *) = dlsym(ga_h, "il2cpp_class_get_type"); void *(*type_obj)(void *) = dlsym(ga_h, "il2cpp_type_get_object");
    void *(*get_methods)(void *, void **) = dlsym(ga_h, "il2cpp_class_get_methods"); const char *(*mname)(void *) = dlsym(ga_h, "il2cpp_method_get_name");
    uint32_t (*pcount)(void *) = dlsym(ga_h, "il2cpp_method_get_param_count"); void *(*mparam)(void *, uint32_t) = dlsym(ga_h, "il2cpp_method_get_param");
    char *(*tname)(void *) = dlsym(ga_h, "il2cpp_type_get_name"); int (*is_gen)(void *) = dlsym(ga_h, "il2cpp_method_is_generic");
    size_t k = 0; void **as = get_asms(domain_get(), &k); void *c = NULL, *uo = NULL;
    for (size_t i = 0; i < k && !c; i++) c = cls_from(asm_image(as[i]), ns, name);
    for (size_t i = 0; i < k && !uo; i++) uo = cls_from(asm_image(as[i]), "UnityEngine", "Object");
    if (!c || !uo) return NULL;
    static void *find_m; void *it = NULL, *m;
    while (!find_m && (m = get_methods(uo, &it)))
        if (!strcmp(mname(m), "FindFirstObjectByType") && pcount(m) == 1 && !is_gen(m)) { char *tn = tname(mparam(m, 0)); if (tn && !strcmp(tn, "System.Type")) find_m = m; }
    if (!find_m) return NULL;
    void *exc = NULL; void *args[1] = { type_obj(cls_type(c)) };
    Obj r = r_invoke(find_m, NULL, args, &exc);
    return exc ? NULL : r;
}

/* keys: the fake get_anyKeyDown answers true on one frame per second while a `keys <n>` command runs */
static volatile int keys_left; static double key_next; static uint64_t key_frame = (uint64_t)-1;
static volatile long ak_calls, ak_true; static FILE *keys_f;
static bool fake_anykey(void) { ak_calls++; if (st_frames == key_frame) { ak_true++; return 1; } return 0; }
static void keys_frame(void) {
    if (keys_left <= 0 || wall() < key_next) return;
    key_frame = st_frames; keys_left--; key_next += 1.0;
    if (!keys_f) { char q[1300]; snprintf(q, sizeof q, "%s/keys.txt", dir); keys_f = fopen(q, "a"); }
    if (keys_f) { fprintf(keys_f, "%.4f %llu\n", wall(), (unsigned long long)st_frames); fflush(keys_f); }
}
static void run_cmd(void) {
    char *a = cmd_line; while (*a && *a != ' ') a++; if (*a) *a++ = 0;
    const char *c = cmd_line; int ok = 1; const char *why = "";
    if (!strcmp(c, "load")) {
        if (scene() != 1 && scene() != 2) { ok = 0; why = "not at title or in a campaign"; }
        else { Obj ev = obj_new(0x6651118); F(0xDCFD78, void (*)(Obj, Obj, void *))(ev, str_new(a), NULL); enqueue(ev); }   /* LoadCampaignRequested */
    } else if (!strcmp(c, "speed")) {
        int s = atoi(a); Obj ev;
        if (s == 0) { ev = obj_new(0x664B1A8); F(0xDCF088, void (*)(Obj, int, int, void *))(ev, 0, 1, NULL); }  /* ChangeGameSpeedRequested(0, AbsolutePause) */
        else { ev = obj_new(0x66542F0); F(0xDCF018, void (*)(Obj, int, void *))(ev, s, NULL); }                     /* SetGameSpeedRequested */
        enqueue(ev);
    } else if (!strcmp(c, "openload")) {
        Obj tv = find_obj("Refactor.UI", "TitleMenuView");
        if (!tv) { ok = 0; why = "no TitleMenuView"; } else F(0xE710B8, void (*)(Obj, void *))(tv, NULL);              /* OnLoadCampaignPressed */
    } else if (!strcmp(c, "keys")) {
        keys_left = atoi(a); key_next = wall();
    } else if (!strcmp(c, "popup") || !strcmp(c, "unpopup")) {   /* PopUpViewContainer.ActivateView / DeactivateView(type) */
        Obj eh = mf ? AT(mf, 0x50, Obj) : NULL, cl = eh ? AT(eh, 0x28, Obj) : NULL, p = cl ? AT(cl, 0xF0, Obj) : NULL;
        if (!p) { ok = 0; why = "no popup container"; }
        else if (c[0] == 'p') F(0xF62554, void (*)(Obj, int, void *))(p, atoi(a), NULL);
        else if (F(0xF62D64, bool (*)(Obj, int, void *))(p, atoi(a), NULL)) F(0xF62A88, void (*)(Obj, int, void *))(p, atoi(a), NULL);
        else { ok = 0; why = "not open"; }
    } else { ok = 0; why = "unknown command"; }
    snprintf(cmd_out, sizeof cmd_out, "%.3f %s %s %s\n", wall(), ok ? "ok" : "FAIL", c, why);
}

static void frame_mark(Obj self) {
    mf = self;
    st_scene = scene(); st_tick = tick(); st_boot = AT(mf, 0x188, bool); st_frames++;
    if ((st_frames & 15) == 0) { st_busy = busy(); st_speed = speed(); }
    keys_frame();
    if (cmd_pending) { run_cmd(); __atomic_store_n(&cmd_pending, 0, __ATOMIC_RELEASE); }
}

static void undepth(int *u) { (void)u; depth = 0; }          /* also when a managed exception unwinds through here */
static void *w_invoke(void *m, void *obj, void **params, void **exc) {
    if (!pthread_equal(pthread_self(), main_th)) return r_invoke(m, obj, params, exc);
    static int keys_reg;
    if (!keys_reg) { keys_reg = 1; const char *k = getenv("SCAN_DRV_KEYS");
        void (*add)(const char *, void *) = k && *k == '1' ? dlsym(ga_h, "il2cpp_add_internal_call") : NULL;
        if (add) add("UnityEngine.Input::get_anyKeyDown()", (void *)fake_anykey); }
    if (depth) return r_invoke(m, obj, params, exc);
    depth = 1; int guard __attribute__((cleanup(undepth))) = 0; (void)guard;
    if (m == mf_method) frame_mark(obj);
    else if (!mf_method && obj) {
        static const char *(*mname)(void *); static void *(*mcls)(void *); static const char *(*cname)(void *);
        if (!mname) { mname = dlsym(ga_h, "il2cpp_method_get_name"); mcls = dlsym(ga_h, "il2cpp_method_get_class"); cname = dlsym(ga_h, "il2cpp_class_get_name"); }
        const char *n = mname(m);
        if (n && !strcmp(n, "Update")) { const char *cn = cname(mcls(m)); if (cn && !strcmp(cn, "MainFlow")) { mf_method = m; mf_seen = 1; frame_mark(obj); } }
    }
    return r_invoke(m, obj, params, exc);
}

/* ---- swap UnityPlayer's il2cpp_runtime_invoke pointer (its own data sections only) */
static int swap_ptr(void) {
    const struct mach_header_64 *up = NULL; intptr_t slide = 0;
    for (uint32_t i = 0, n = _dyld_image_count(); i < n; i++) {
        const char *nm = _dyld_get_image_name(i); size_t l = nm ? strlen(nm) : 0;
        if (l > 19 && !strcmp(nm + l - 19, "/GameAssembly.dylib") && !ga_h) { ga_h = dlopen(nm, RTLD_NOLOAD | RTLD_LAZY); ga_base = (uintptr_t)_dyld_get_image_header(i); }
        if (l > 18 && !strcmp(nm + l - 18, "/UnityPlayer.dylib")) { up = (const struct mach_header_64 *)_dyld_get_image_header(i); slide = _dyld_get_image_vmaddr_slide(i); }
    }
    if (!ga_h || !up) return 0;
    void *real = dlsym(ga_h, "il2cpp_runtime_invoke"); if (!real) return 0;
    int found = 0;
    const struct load_command *lc = (const void *)(up + 1);
    for (uint32_t c = 0; c < up->ncmds; c++, lc = (const void *)((const char *)lc + lc->cmdsize)) {
        if (lc->cmd != LC_SEGMENT_64) continue;
        const struct segment_command_64 *sg = (const void *)lc;
        if (strcmp(sg->segname, "__DATA") && strcmp(sg->segname, "__DATA_DIRTY")) continue;
        const struct section_64 *s = (const void *)(sg + 1);
        for (uint32_t j = 0; j < sg->nsects; j++, s++) {
            uint64_t *p = (uint64_t *)(s->addr + slide), *e = (uint64_t *)(s->addr + slide + s->size);
            for (; p < e; p++) if (*p == (uint64_t)real) { if (!r_invoke) r_invoke = real; __atomic_store_n(p, (uint64_t)w_invoke, __ATOMIC_RELEASE); found++; }
        }
    }
    return found;
}

static void put_state(void) {
    char p[1100], q[1100]; snprintf(p, sizeof p, "%s/state.txt.tmp", dir); snprintf(q, sizeof q, "%s/state.txt", dir);
    FILE *o = fopen(p, "w"); if (!o) return;
    fprintf(o, "t %.3f\nswapped %d\nmf_seen %d\nframes %llu\nscene %d\nboot %d\nbusy %d\nspeed %d\ntick %lld\nak_calls %ld\nak_true %ld\n",
            wall(), swapped, mf_seen, (unsigned long long)st_frames, st_scene, st_boot, st_busy, st_speed, (long long)st_tick, ak_calls, ak_true);
    fclose(o); rename(p, q);
}

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 24000 && !swapped; i++) { swapped = swap_ptr(); if (!swapped) usleep(5000); }
    put_state();
    for (;;) {
        usleep(250000);
        put_state();
        if (cmd_out[0]) { char q[1100]; snprintf(q, sizeof q, "%s/cmd.out", dir); FILE *c = fopen(q, "a"); if (c) { fputs(cmd_out, c); fclose(c); } cmd_out[0] = 0; }
        if (!cmd_pending) {
            char q[1100], r[1100]; snprintf(q, sizeof q, "%s/cmd.txt", dir); snprintf(r, sizeof r, "%s/cmd.taken", dir);
            if (!rename(q, r)) {
                FILE *c = fopen(r, "r");
                if (c) { if (fgets(cmd_line, sizeof cmd_line, c)) { cmd_line[strcspn(cmd_line, "\r\n")] = 0; __atomic_store_n(&cmd_pending, 1, __ATOMIC_RELEASE); } fclose(c); }
            }
        }
    }
    return NULL;
}

__attribute__((constructor)) static void drv_init(void) {
    const char *d = getenv("SCAN_DRV_DIR"); if (!d || !*d) return;
    snprintf(dir, sizeof dir, "%s", d); unsetenv("SCAN_DRV_DIR");       /* child processes (soft restart prewarm) do nothing */
    main_th = pthread_self();
    pthread_t th; pthread_create(&th, NULL, worker, NULL); pthread_detach(th);
}
