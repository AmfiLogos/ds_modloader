// OS part of the soft restart (src/dsl_soft.c keeps the state machines and every game call, by il2cpp names).
//   Mac:     start/mac/dsl_soft_mac.c  (Cocoa windows through the objc runtime, posix_spawn, CFRunLoop timer)
//   Windows: start/win/dsl_soft_win.c  (the Unity window by HWND, CreateProcess, thread timer)
// "Windows" below = the game's own top-level windows. Every call runs on the game's main thread.
#pragma once
#include <stdio.h>

double dsl_sos_proc_start(void);            /* process start time (seconds since 1970) */
void dsl_sos_snapshot(void);                /* exe, argv, environment and folder as started (B gets the same) */
/* B (hidden = 1): every game window stays invisible from its creation, never takes focus, no Dock / taskbar entry.
   stage = test only: every window kept at (sx, sy), far off every display. */
void dsl_sos_windows_install(int hidden, int stage, double sx, double sy);
void dsl_sos_set_hidden(int on);
int dsl_sos_spawn(const char *profile, const char *handoff, const char *env_lines);  /* start B; 0 = ok */
long dsl_sos_child_pid(void);
int dsl_sos_child_exited(void);             /* 1 = B ended */
void dsl_sos_child_kill(void);             /* B killed, reaped later without blocking; child_pid() is 0 after */
int dsl_sos_pid_alive(long pid);
int dsl_sos_game_frame(double f[4]);        /* game window x, y, w, h (OS units); 0 = none */
void dsl_sos_yield(void);                   /* A: let B take the foreground (B asks for it at the swap) */
void dsl_sos_show(const double *frame, int activate);   /* B: at A's frame (NULL = keep), visible, in front */
void dsl_sos_order_out(void);               /* A: every window out, before it quits */
void dsl_sos_frame_log(FILE *f);            /* one line part: the windows' state */
void dsl_sos_stage_check(void);             /* test stage: a window that left the stage -> invisible, moved back */
void dsl_sos_timer(void (*tick)(void));     /* tick() ~60 times a second on the main thread, between frames */
void dsl_sos_unsetenv(const char *name);
