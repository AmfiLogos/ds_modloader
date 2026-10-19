// BepInEx's own setting file (BepInEx/config/BepInEx.cfg): the BepInEx console window never shows (owner rule
// 2026-10-19); its log file (BepInEx/LogOutput.log, [Logging.Disk]) stays on. Every start that runs BepInEx sets
// [Logging.Console] Enabled = false and keeps every other line of the player's file. No file yet (first start): one with
// only that key; BepInEx adds its other settings at its start.
// Used by the Windows starter (start/win/bie_win.c, after dsl_u8_names_win.h: fopen takes UTF-8 paths there), the Mac
// app stub (app/stub/ds_modded.c) and dsload's Mac soft restart (start/mac/dsl_bie_mac.c).
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the file's text with Enabled = false in [Logging.Console]; NULL = already so (nothing to write) */
static inline char *dsl_bie_cfg_console_off_text(const char *old) {
    if (!old) old = "";
    const char *nl = strstr(old, "\r\n") ? "\r\n" : "\n";
    size_t n = strlen(old), cap = n + 128;
    char *out = malloc(cap); if (!out) return NULL;
    size_t o = 0; int in_sec = 0, sec_seen = 0, done = 0;
    for (const char *l = old; *l; ) {
        const char *e = strchr(l, '\n'); size_t len = e ? (size_t)(e - l + 1) : strlen(l);
        const char *s = l; while (s < l + len && (*s == ' ' || *s == '\t')) s++;
        const char *c = s; size_t cl = (size_t)(l + len - s);
        while (cl && (c[cl - 1] == '\n' || c[cl - 1] == '\r' || c[cl - 1] == ' ' || c[cl - 1] == '\t')) cl--;
        if (cl && *c == '[') {
            if (in_sec && !done) { o += (size_t)snprintf(out + o, cap - o, "Enabled = false%s", nl); done = 1; }   /* section without the key */
            in_sec = cl == 17 && !memcmp(c, "[Logging.Console]", 17); sec_seen |= in_sec;
        } else if (in_sec && !done && cl > 7 && !memcmp(c, "Enabled", 7)) {
            const char *q = c + 7; while (*q == ' ' || *q == '\t') q++;
            if (*q == '=') {
                q++; while (*q == ' ' || *q == '\t') q++;
                size_t vl = (size_t)(c + cl - q);
                if (vl == 5 && !strncmp(q, "false", 5)) { free(out); return NULL; }
                o += (size_t)snprintf(out + o, cap - o, "Enabled = false%s", nl); done = 1;
                l += len; continue;
            }
        }
        memcpy(out + o, l, len); o += len; out[o] = 0;
        l += len;
    }
    if (in_sec && !done) {                                    /* [Logging.Console] is the last section, no key */
        if (o && out[o - 1] != '\n') o += (size_t)snprintf(out + o, cap - o, "%s", nl);
        o += (size_t)snprintf(out + o, cap - o, "Enabled = false%s", nl); done = 1;
    }
    if (!sec_seen) {
        if (o && out[o - 1] != '\n') o += (size_t)snprintf(out + o, cap - o, "%s", nl);
        snprintf(out + o, cap - o, "%s[Logging.Console]%s%sEnabled = false%s", o ? nl : "", nl, nl, nl);
    }
    return out;
}
/* 1 = written, 0 = already off, -1 = cannot write (the folder of cfg exists: the caller makes it) */
static inline int dsl_bie_console_off(const char *cfg) {
    char *old = NULL; FILE *f = fopen(cfg, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
        if (L >= 0 && L < (8 << 20) && (old = malloc((size_t)L + 1))) { size_t r = fread(old, 1, (size_t)L, f); old[r] = 0; }
        fclose(f);
        if (!old) return -1;                                  /* unreadable: never replace the player's file blind */
    }
    char *t = dsl_bie_cfg_console_off_text(old); free(old);
    if (!t) return 0;
    int r = -1;
    if ((f = fopen(cfg, "wb"))) { size_t n = strlen(t); r = fwrite(t, 1, n, f) == n ? 1 : -1; if (fclose(f)) r = -1; }
    free(t);
    return r;
}
