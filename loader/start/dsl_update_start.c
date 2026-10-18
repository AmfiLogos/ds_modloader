// dsl_update_start.c - the starter's half of the auto-update (owner 2026-10-16): installs happen only here, in the Mac
// stub (#included by app/stub/ds_modded.c) and the Windows starter (start/win/ds_modded_win.c), before the game opens.
// The game's loader (src/dsl_update.c) only checks and downloads; a verified download is announced in
// ROOT/update/ready.txt:
//   KIND kit|mods, LABEL <shown name>, KEY <bad.txt keys, ','-joined>, CONSENT 1 (the player clicked "Update to"),
//   KIT <ver> <file> <sha256>  or  MOD <id> <ver> <file> <sha256> lines (tab-separated).
// At a start, in this order:
//   1. a game of this install still runs (or another starter): nothing here (never move files under a running game).
//   2. ROOT/update/pending.txt (an installed update that has not reached the title yet; the loader confirms it there):
//      no DONE line = the install was cut off: roll back at once. Two STARTED lines = the new version was started twice
//      and never reached the title: roll back, the KEY goes into bad.txt (not offered again), one line for the player.
//      Else one more STARTED line and the game starts with DSML_UPDATE=<label> (one retry after a crash or a quit).
//   3. ready.txt and (Auto-update on, or CONSENT): sha256 checked again, a small window "Updating Dungeon Settlers Modded
//      to 0.1.N" with a progress bar, the install (pending.txt written first, each MOVE line before its move), DONE.
//      Kit: the caller starts the new starter (it goes through step 2). Mods: step 2 here. Install failed: rolled back,
//      the old version starts, one line for the player (ROOT/update/notice.txt, shown by the loader at the title); the
//      second failure of the same update puts it into bad.txt. A kit refused as not for this game build (-2: Windows,
//      it needs the installer) is not counted: the installer can make that kit fit later.
// Rollback walks the MOVE lines backwards and skips what never happened, so it can run again after a crash; bad.txt /
// moving pending.txt away happen only when every move back worked.
#include "../src/dsl_internal.h"
#include "../src/dsl_update_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char us_dir[1100];
static void us_path(char *out, size_t n, const char *f) { snprintf(out, n, "%s/%s", us_dir, f); }
static void us_stamp(char *out, size_t n) { time_t t = time(NULL); strftime(out, n, "%Y%m%d-%H%M%S", localtime(&t)); }
static char *us_slurp(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    char *b = malloc((1 << 20) + 1); size_t n = b ? fread(b, 1, 1 << 20, f) : 0; fclose(f);
    if (b) b[n] = 0; return b;
}
static void us_append(const char *file, const char *line) {
    char p[1200]; us_path(p, sizeof p, file); FILE *f = fopen(p, "ab"); if (f) { fprintf(f, "%s\n", line); fclose(f); }
}
static int us_count(const char *file, const char *line) {               /* lines equal to line */
    char p[1200]; us_path(p, sizeof p, file); char *b = us_slurp(p); if (!b) return 0;
    int n = 0; for (char *l = strtok(b, "\r\n"); l; l = strtok(NULL, "\r\n")) n += !strcmp(l, line);
    free(b); return n;
}
static void us_old(const char *file, const char *why) {                 /* update/<file> -> update/old/<file>-<why>-<time> */
    char p[1200], q[1400], ts[32]; us_path(p, sizeof p, file); us_stamp(ts, sizeof ts);
    snprintf(q, sizeof q, "%s/old/%s-%s-%s", us_dir, why, ts, file);
    if (dsl_uos_exists(p) && dsl_uos_move(p, q)) dsl_log("update: cannot move %s", p);
}
static void us_notice(const char *line) {                                /* one line for the player, at the title */
    char p[1200], t[1300]; us_path(p, sizeof p, "notice.txt"); snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "wb"); if (!f) return;
    fprintf(f, "%s\n", line); fclose(f);
    if (dsl_uos_exists(p)) us_old("notice.txt", "unshown");
    dsl_uos_move(t, p);
}
static int us_vcmp(const char *a, const char *b) {                      /* "0.1.10" > "0.1.9"; a leading v ignored */
    for (;;) {
        while (*a && (*a < '0' || *a > '9')) a++;
        while (*b && (*b < '0' || *b > '9')) b++;
        if (!*a && !*b) return 0;
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
}

/* backwards; a move that never happened is skipped (backup missing = the old file never left / is back already).
   Returns the number of moves that failed. */
static int us_rollback(const DslMoves *mv, const char *root) {
    char ts[32]; us_stamp(ts, sizeof ts); int bad = 0;
    for (int i = mv->n - 1; i >= 0; i--) {
        const char *p = mv->path[i], *b = mv->backup[i];
        char away[1300]; snprintf(away, sizeof away, "%s/retired/update-failed-%s/%d", root, ts, i);
        if (b[0]) {
            if (!dsl_uos_exists(b)) continue;
            if (dsl_uos_exists(p) && dsl_uos_move(p, away)) { bad++; continue; }
            if (dsl_uos_move(b, p)) bad++;
        } else if (dsl_uos_exists(p) && dsl_uos_move(p, away)) bad++;
    }
    dsl_log("update: rolled back %d recorded change(s), %d failed", mv->n, bad);
    return bad;
}

typedef struct { char label[200], key[400], from[64]; int done, started; DslMoves *mv; } UsPending;
static int us_pending_read(UsPending *pd) {
    char p[1200]; us_path(p, sizeof p, "pending.txt"); char *b = us_slurp(p); if (!b) return -1;
    memset(pd, 0, sizeof *pd); pd->mv = calloc(1, sizeof *pd->mv);
    if (!pd->mv) { free(b); return -1; }
    for (char *l = strtok(b, "\r\n"); l; l = strtok(NULL, "\r\n")) {
        if (!strcmp(l, "DONE")) { pd->done = 1; continue; }
        if (!strcmp(l, "STARTED")) { pd->started++; continue; }
        char *t1 = strchr(l, '\t'); if (!t1) continue; *t1++ = 0;
        if (!strcmp(l, "LABEL")) snprintf(pd->label, sizeof pd->label, "%s", t1);
        else if (!strcmp(l, "KEY")) snprintf(pd->key, sizeof pd->key, "%s", t1);
        else if (!strcmp(l, "FROM")) snprintf(pd->from, sizeof pd->from, "%s", t1);
        else if (!strcmp(l, "MOVE")) { char *t2 = strchr(t1, '\t'); if (t2) *t2++ = 0; dsl_umove_add(pd->mv, t1, t2 ? t2 : ""); }
    }
    free(b); return 0;
}
static void us_bad_keys(const char *key) {
    char k[400]; snprintf(k, sizeof k, "%s", key);
    for (char *t = strtok(k, ","); t; t = strtok(NULL, ",")) if (!us_count("bad.txt", t)) us_append("bad.txt", t);
}
/* "v0.1.5" -> "0.1.5" / "mods:Multiplayer 0.1.5" -> "Multiplayer 0.1.5" (the player's lines are whole sentences per
   kind, so a translation never splices "the update to ..." into another sentence) */
static int us_is_mods(const char *label) { return !strncmp(label, "mods:", 5); }
static const char *us_shown(const char *label) { return us_is_mods(label) ? label + 5 : label[0] == 'v' ? label + 1 : label; }

/* step 2; label set when this start runs the installed update */
static void us_pending(const char *root, char *label, size_t ln) {
    UsPending pd; if (us_pending_read(&pd)) return;
    char line[700]; const char *what = us_shown(pd.label); int mods = us_is_mods(pd.label);
    if (!pd.done || pd.started >= 2) {
        dsl_log("update: %s %s: rolling back", pd.label, !pd.done ? "was cut off while installing" : "was started twice and never reached the title");
        if (us_rollback(pd.mv, root)) dsl_log("update: rollback incomplete: pending.txt stays (the next start tries again)");
        else {
            if (pd.done) us_bad_keys(pd.key);
            us_old("pending.txt", pd.done ? "failed" : "cutoff");
            if (!pd.done) snprintf(line, sizeof line, mods ? TR("The mod update (%s) was cut off. Nothing changed.") : TR("The update to %s was cut off. Nothing changed."), what);
            else if (mods) snprintf(line, sizeof line, TR("The mod update (%s) did not start. The old mods are back."), what);
            else if (pd.from[0]) snprintf(line, sizeof line, TR("The update to %s did not start. You have %s again."), what, pd.from);
            else snprintf(line, sizeof line, TR("The update to %s did not start. You have the old version again."), what);
            dsi_utf8_fix_end(line);
            us_notice(line);
        }
    } else {
        us_append("pending.txt", "STARTED");
        snprintf(label, ln, "%s", pd.label);
        dsl_log("update: %s starts (start %d of 2 before it rolls back)", pd.label, pd.started + 1);
    }
    free(pd.mv);
}

typedef struct { char id[64], ver[32], file[1100], sha[65]; } UsItem;
/* step 3; 1 = a kit was installed */
static int us_ready(const char *root, int auto_on, const DslUpdWin *w) {
    char p[1200]; us_path(p, sizeof p, "ready.txt"); char *b = us_slurp(p); if (!b) return 0;
    static UsItem it[24]; int n = 0, kit = 0, consent = 0; char label[200] = "", key[400] = "";
    for (char *l = strtok(b, "\r\n"); l; l = strtok(NULL, "\r\n")) {
        char *f[5] = { l }; int nf = 1;
        for (char *c = l; *c && nf < 5; c++) if (*c == '\t') { *c = 0; f[nf++] = c + 1; }
        if (nf >= 2 && !strcmp(f[0], "LABEL")) snprintf(label, sizeof label, "%s", f[1]);
        else if (nf >= 2 && !strcmp(f[0], "KEY")) snprintf(key, sizeof key, "%s", f[1]);
        else if (nf >= 2 && !strcmp(f[0], "CONSENT")) consent = f[1][0] == '1';
        else if (nf == 4 && !strcmp(f[0], "KIT") && n < 24) { kit = 1; snprintf(it[n].id, sizeof it[n].id, "kit");
            snprintf(it[n].ver, sizeof it[n].ver, "%s", f[1]); snprintf(it[n].file, sizeof it[n].file, "%s", f[2]); snprintf(it[n].sha, sizeof it[n].sha, "%s", f[3]); n++; }
        else if (nf == 5 && !strcmp(f[0], "MOD") && n < 24) { snprintf(it[n].id, sizeof it[n].id, "%s", f[1]);
            snprintf(it[n].ver, sizeof it[n].ver, "%s", f[2]); snprintf(it[n].file, sizeof it[n].file, "%s", f[3]); snprintf(it[n].sha, sizeof it[n].sha, "%s", f[4]); n++; }
    }
    free(b);
    if (!n || (kit && n != 1)) { dsl_log("update: ready.txt unreadable"); us_old("ready.txt", "unreadable"); return 0; }
    if (!auto_on && !consent) { dsl_log("update: %s is downloaded; Auto-update is off and Update was not clicked: not installed", label); return 0; }
    char have[64] = ""; if (dsl_uos_release(have, sizeof have)) have[0] = 0;
    if (kit && have[0] && us_vcmp(it[0].ver, have) <= 0) { dsl_log("update: ready %s is not newer than %s", it[0].ver, have); us_old("ready.txt", "stale"); return 0; }
    for (int i = 0; i < n; i++) {
        char k[128], h[65] = ""; snprintf(k, sizeof k, "%s %s", it[i].id, it[i].ver);
        if (us_count("bad.txt", k)) { dsl_log("update: %s was rolled back before: not installed", k); us_old("ready.txt", "bad"); return 0; }
        if (dsl_uos_sha256(it[i].file, h) || strcmp(h, it[i].sha)) {
            dsl_log("update: %s: sha256 %s, expected %s: not installed", it[i].file, h[0] ? h : "(no file)", it[i].sha);
            us_old("ready.txt", "badsha"); return 0;
        }
    }
    char title[300], ts[32], bdir[1300], why[300] = ""; const char *what = us_shown(label);
    if (kit) snprintf(title, sizeof title, TR("Updating Dungeon Settlers Modded to %s"), it[0].ver);
    else snprintf(title, sizeof title, TR("Updating mods: %s"), what);
    dsi_utf8_fix_end(title);
    us_stamp(ts, sizeof ts); snprintf(bdir, sizeof bdir, "%s/backup-%s-%s", us_dir, have[0] ? have : "old", ts);
    DslMoves *mv = calloc(1, sizeof *mv); if (!mv) return 0;
    us_path(p, sizeof p, "pending.txt");
    FILE *pf = fopen(p, "wb");
    if (!pf) { dsl_log("update: cannot write %s: not installed", p); free(mv); return 0; }
    fprintf(pf, "LABEL\t%s\nKEY\t%s\nFROM\t%s\n", label, key, have); fflush(pf);
    mv->log = pf;
    dsl_log("update: installing %s (Auto-update %s%s)", label, auto_on ? "on" : "off", consent ? ", Update clicked" : "");
    if (w && w->open) w->open(title);
    int r = 0;
    if (kit) { mv->progress = w ? w->progress : NULL; r = dsl_uos_kit_install(it[0].file, it[0].ver, bdir, mv, why, sizeof why); }
    else for (int i = 0; i < n && !r; i++) {
        char mb[1500]; snprintf(mb, sizeof mb, "%s/%s", bdir, it[i].id);
        if (w && w->progress) w->progress(10 + 85 * i / n);
        r = dsl_uos_mod_install(it[i].file, it[i].id, it[i].ver, mb, mv, why, sizeof why);
    }
    if (!r) { fprintf(pf, "DONE\n"); if (fflush(pf)) r = -1, snprintf(why, sizeof why, "%s", TR("the disk is full")); }
    fclose(pf);
    if (w && w->progress) w->progress(100);
    if (w && w->close) w->close();
    if (r) {
        dsl_log("update: install of %s failed: %s", label, why);
        char line[900];
        if (us_rollback(mv, root)) dsl_log("update: rollback incomplete: pending.txt stays (the next start tries again)");
        else {
            us_old("pending.txt", "installfailed");
            if (r == -2) dsl_log("update: %s is not for this game build: not counted as a failure", key);
            else {
                us_append("fails.txt", key);
                if (us_count("fails.txt", key) >= 2) { us_bad_keys(key); dsl_log("update: %s failed twice: not offered again", key); }
            }
        }
        us_old("ready.txt", "installfailed");
        dsi_utf8_fix_end(why);
        if (!kit) snprintf(line, sizeof line, TR("The mod update (%s) could not be installed (%s). The old mods stay."), what, why);
        else if (have[0]) snprintf(line, sizeof line, TR("The update to %s could not be installed (%s). You still have %s."), what, why, have);
        else snprintf(line, sizeof line, TR("The update to %s could not be installed (%s). You still have the old version."), what, why);
        dsi_utf8_fix_end(line);
        us_notice(line);
        free(mv); return 0;
    }
    us_old("ready.txt", "installed");
    dsl_log("update: %s installed (%d change(s), old files in %s)", label, mv->n, bdir);
    free(mv);
    return kit;
}

/* the starter's entry. root = ROOT (dsl_uos_root() must give the same). running = a game of this install (or another
   starter) runs. label: set when this start runs a just-installed update (the caller exports DSML_UPDATE=<label>).
   Returns 1 when a new kit was installed: the caller starts the new starter instead of the game. */
int dsl_update_starter(const char *root, int running, const DslUpdWin *w, char *label, size_t ln) {
    label[0] = 0;
    snprintf(us_dir, sizeof us_dir, "%s/update", root);
    char p[1200]; us_path(p, sizeof p, "old"); dsl_uos_mkdirs(p);
    if (running) {
        us_path(p, sizeof p, "pending.txt");
        if (dsl_uos_exists(p) || (us_path(p, sizeof p, "ready.txt"), dsl_uos_exists(p)))
            dsl_log("update: the game of this install runs: no install and no rollback now");
        return 0;
    }
    us_path(p, sizeof p, "pending.txt");
    if (!dsl_uos_exists(p)) {
        int auto_on = 1; us_path(p, sizeof p, "settings.json");
        char *s = us_slurp(p);
        if (s) { JNode *j = json_parse(s); JNode *a = json_get(j, "auto"); if (a && a->t == JN_BOOL) auto_on = a->b; json_free(j); free(s); }
        if (us_ready(root, auto_on, w)) return 1;
    }
    us_pending(root, label, ln);
    return 0;
}
