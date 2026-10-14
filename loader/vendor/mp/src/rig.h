// rig.h - test-rig module for Dungeon Settlers build 25846389 (arm64): shared declarations.
// The rig dylib is injected into an isolated copy of the game (DYLD_INSERT_LIBRARIES) and gives an
// agent a control socket. Game access recipes: ~/ds_multiplayer/research/rig_access_spec.md.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RIG_BUILD "25846389"
#define RIG_GA_UUID "3C5DD8BD-E371-3C0E-A674-A81042B0AC9F"
/* runtime hook stubs / slots in GameAssembly's zero tails (same areas as the dsmods loader; the
   rig copies run the STOCK GameAssembly, so nothing else uses them there) */
#define RIG_CAVE_LO 0x604B000ULL
#define RIG_CAVE_HI 0x604B780ULL
#define RIG_SLOT_LO 0x6B9B800ULL
#define RIG_SLOT_HI 0x6B9C000ULL

extern uintptr_t ds_base;
extern char rig_dir[1024];             /* instance folder (env DSRIG_DIR) */
void rig_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* rig_hook.c */
int rig_hook(uintptr_t rva, void *repl, void **orig, const char *who);

/* rig_win.c: keep every window invisible and the app inactive */
void rig_win_install(void);
void rig_win_frame(void);
void rig_win_place(void);
void rig_win_space_install(void);  /* demo: DSRIG_SPACE=<space id> */
void rig_win_space_frame(void);   /* demo: DSRIG_SHOWWIN=left|right */

/* rig_sock.c: control socket; requests are run on the main thread by rig_drain() */
typedef struct RigReq {
    char *line;                        /* request JSON (one line) */
    char *resp;                        /* response JSON, malloc'd by the handler */
    int done;
    struct RigReq *next;
} RigReq;
void rig_sock_start(const char *path);
RigReq *rig_take_requests(void);       /* main thread: take every queued request (FIFO) */
void rig_finish(RigReq *r, char *resp);/* main thread: hand the response back */

/* tiny JSON helpers (flat objects only) */
int jstr(const char *j, const char *key, char *out, size_t n);   /* 1 = found */
int jnum(const char *j, const char *key, double *out);           /* 1 = found */

/* growable text buffer */
typedef struct { char *p; size_t n, cap; } Buf;
void bput(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void bstr(Buf *b, const char *s);      /* JSON string with escapes */

/* rig_cmd.c */
char *rig_exec(const char *line);      /* main thread */
void rig_frame(void *mainflow);        /* main thread, once per MainFlow.Update before the original */
extern volatile int64_t rig_frames;

/* rig_cmd.c helpers shared with rig_det.c */
typedef void *Obj;
typedef struct { uint64_t lo, hi; } Guid;
typedef struct { uint32_t s[4]; } RState;
#define AT(o, off, T) (*(T *)((char *)(o) + (off)))
#define F(va, T) ((T)(void *)(ds_base + (va)))
extern Obj mf;                         /* MainFlow instance (from the hook) */
void *md(uint64_t slot);
Obj obj_new(uint64_t ti_slot);
void guid_str(Guid g, char *out);
int guid_parse(const char *s, Guid *g);
Obj dict_int_get(Obj d, int key);
Obj cdc(void);
int scene(void);
int cur_map(void);
int64_t tick(void);
Obj units_dict(int map);
Obj pos_dict(int map);
int busy(void);
bool unit_dead(Obj u);
float unit_stat(Obj u, int stat);
int unit_task(Obj u);
Obj find_unit(int map, Guid g);
uint64_t fnv(uint64_t h, const void *p, size_t n);
RState rstate(void);
Guid ent_guid(Obj e);
void unit_combat(Obj u, int *state, Guid *cur, Guid *force, Guid *task_target);
/* world fingerprint: h[0] all, h[1] units, h[2] positions, h[3] rng+tick */
void rig_world_hash(RState rs, uint64_t h[4], int *nunits, int *npos);

/* rig_det.c: lockstep determinism layer */
void rig_det_install(void);            /* hooks; called from the GameAssembly add-image callback */
int rig_det_frame(void *mainflow);     /* main thread, after rig_frame: runs locked sim cycles; 1 = locked */
char *rig_det_exec(const char *cmd, const char *line); /* NULL = not a det command */
void rig_det_status(Buf *b);           /* extra status fields */
int rig_det_locked(void);
RState rig_det_sim_state(void);
void rig_det_on_load(void);
void rig_det_arm(void);               /* mp session start: det on, auto-lock on a runnable campaign */
int rig_order_move(Guid g, int x, int y);
int rig_save_campaign(const char *name);
int rig_load_save_file(const char *path);
void rig_det_write_detail(const char *why);
uint64_t rig_det_extra_hash(uint64_t h);            /* load_save: reset deterministic counters / seed */
