// dsl_bugrep.h - the player's bug report (dsl_bugrep.c = window + send, dsl_bugrep_fmt.c = the report's text).
#pragma once
#include <stddef.h>
struct DslScrub;
#define DSL_BUG_TEXT_MAX 8000        /* the receiver's limit (characters); the field stops there */
#define DSL_BUG_CONTACT_MAX 200
#define DSL_BUG_GZ_MAX (3500u << 10) /* under the receiver's 4 MB */
typedef struct { const char *name, *path; size_t cap; } DslBugLog;   /* a log section: the last cap bytes of path */
typedef struct {
    const char *header;              /* "DSCRASH 1\ninstall: ..\n...mods: ..\n" (scrubbed here) */
    const char *contact;             /* may be NULL / "" */
    const char *text;                /* the player's text, not empty */
    const unsigned char *img; size_t img_n; const char *img_name;   /* "screenshot.jpg"; img NULL = none */
    const DslBugLog *logs; int nlogs;
} DslBugIn;
/* the gzip to send (new buffer); 0 = ok, -1 = empty text / no memory / too big even without the screenshot */
int dsl_bugrep_build(const struct DslScrub *sc, const DslBugIn *in, size_t max_gz, unsigned char **gz, size_t *gn, int *shot_dropped);
size_t dsl_b64(const unsigned char *in, size_t n, char *out);       /* out: n/3*4 + n/57 + 8 bytes */
void dsl_bugrep_one_line(const char *in, char *out, size_t cap);

/* the window (dsl_bugrep.c) */
void dsl_bugrep_install(void);       /* dsl_reg_ready: hooks on the game's two Bug Report buttons */
void dsl_bugrep_frame(void);         /* dsl_frame_hook, main thread */
int dsl_bugrep_is_open(void);
