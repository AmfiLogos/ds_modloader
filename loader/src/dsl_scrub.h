// dsl_scrub.h - crash reports: remove personal data from log text, and gzip (pure C, no game or OS calls;
// unit test loader/test/crashrep). Used by dsl_crash.c.
#pragma once
#include <stddef.h>

#define DSL_SCRUB_NAMES 96
typedef struct DslScrub { int n; char name[DSL_SCRUB_NAMES][160]; } DslScrub;

void dsl_scrub_init(DslScrub *s);
/* an exact text to replace everywhere (user name, full name, computer name, Steam names, player names; ASCII letters
   compared without case). Shorter than 3 bytes, or a common word the logs use, is ignored. */
void dsl_scrub_name(DslScrub *s, const char *name);
/* in[0..n) -> a new NUL-terminated text without: user names in folder paths (/Users/x, /home/x, C:\Users\x), the
   names given above, Steam ids, player names in the multiplayer log lines, e-mail addresses, IPv4 / IPv6 / MAC
   addresses (127.x, 0.0.0.0 and ::1 stay). out_n = its length. NULL = no memory. */
char *dsl_scrub(const DslScrub *s, const char *in, size_t n, size_t *out_n);
/* the names of this PC (dsl_scrub_names.c); logdir = the loader's log folder (the multiplayer files) */
void dsl_scrub_collect(DslScrub *s, const char *logdir);
/* gzip (deflate, fixed Huffman codes + LZ77) of in[0..n) into a new buffer; 0 = ok */
int dsl_gzip(const void *in, size_t n, unsigned char **out, size_t *out_n);
