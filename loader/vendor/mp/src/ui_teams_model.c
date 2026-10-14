// ui_teams_model: pure logic of the Teams screen. See ui_teams_model.h for the rules.
#include "ui_teams_model.h"
#include "ui_model.h"
#include <stdio.h>
#include <string.h>

static int present(const tm_state *s, int p) { return p >= 0 && p < TM_MAX_PLAYERS && (s->present >> p & 1u); }
static int owns_any(const tm_state *s, int p) {
    for (int i = 0; i < s->nunits; i++) if (s->owner[i] == p) return 1;
    return 0;
}
static int unit_index(const tm_state *s, const uint8_t g[16]) {
    for (int i = 0; i < s->nunits; i++) if (!memcmp(s->guid[i], g, 16)) return i;
    return -1;
}

int tm_is_marked(const tm_state *s, int player, const uint8_t g[16]) {
    if (player < 0 || player >= TM_MAX_PLAYERS) return 0;
    for (int k = 0; k < s->nmarked[player] && k < TM_MAX_MARK; k++) if (!memcmp(s->marked[player][k], g, 16)) return 1;
    return 0;
}

int tm_layout(const tm_state *s, tm_column *cols, int max_cols) {
    int nc = 0;
    int order[TM_MAX_PLAYERS + 1], no = 0;
    for (int p = 0; p < TM_MAX_PLAYERS; p++) if (present(s, p)) order[no++] = p;
    for (int p = 0; p < TM_MAX_PLAYERS; p++) if (!present(s, p) && owns_any(s, p)) order[no++] = p;   /* left the game */
    order[no++] = TM_FREE;
    for (int i = 0; i < s->nunits; i++) if (s->owner[i] == TM_RESERVED) { order[no++] = TM_RESERVED; break; }
    for (int k = 0; k < no && nc < max_cols; k++) {
        tm_column *c = &cols[nc++];
        c->player = order[k]; c->n = 0;
        for (int i = 0; i < s->nunits; i++) {
            int o = s->owner[i];
            if (o < TM_RESERVED || o >= TM_MAX_PLAYERS) o = TM_FREE;
            if (o == c->player) c->unit[c->n++] = i;
        }
    }
    return nc;
}

static tm_action act(tm_kind k, int unit, int target) {
    tm_action a; memset(&a, 0, sizeof a);
    a.kind = k; a.unit = unit; a.target = target;
    return a;
}
static void copy_marks(const tm_state *s, tm_action *a) {
    int n = s->me >= 0 && s->me < TM_MAX_PLAYERS ? s->nmarked[s->me] : 0;
    if (n > tm_mark_limit(s)) n = tm_mark_limit(s);
    for (int k = 0; k < n; k++) memcpy(a->marks[k], s->marked[s->me][k], 16);
    a->nmarks = n;
}

tm_action tm_click_unit(const tm_state *s, int unit, int button, tm_sel *sel) {
    if (unit < 0 || unit >= s->nunits) return act(TM_NONE, -1, 0);
    if (s->in_dungeon) return act(TM_DENY_LOCKED, unit, 0);
    int o = s->owner[unit], me = s->me;
    if (button == 1) {                                         /* right-click: mark / unmark own unit */
        if (o != me) return act(TM_DENY_NOT_OWN, unit, o);
        tm_action a = act(TM_MARKS, unit, 0);
        copy_marks(s, &a);
        for (int k = 0; k < a.nmarks; k++)
            if (!memcmp(a.marks[k], s->guid[unit], 16)) {      /* unmark */
                memmove(a.marks[k], a.marks[k + 1], (size_t)(a.nmarks - k - 1) * 16);
                a.nmarks--;
                return a;
            }
        if (a.nmarks >= tm_mark_limit(s)) return act(TM_DENY_FULL, unit, 0);
        memcpy(a.marks[a.nmarks++], s->guid[unit], 16);
        return a;
    }
    if (me == 0) {
        /* host: with a target column, a FREE unit (or his own) goes there; another player's / a reserved unit only
           in admin mode (lead rule 2026-10-13, same as dsnet's OWNERSHIP rule) */
        if (sel->target != TM_NO_TARGET && (s->admin || o == TM_FREE || o == me)) {
            sel->picked = -1;
            if (o == sel->target) return act(TM_NONE, unit, o);
            return act(TM_ASSIGN, unit, sel->target);
        }
        if (s->admin) {
            if (sel->picked == unit) { sel->picked = -1; return act(TM_UNPICK, unit, 0); }
            if (o != TM_FREE && o != me) { sel->picked = unit; return act(TM_PICK, unit, o); }
        }
        sel->picked = -1;
    }
    if (o == TM_FREE) return act(TM_CLAIM, unit, me);
    if (o == me) return act(TM_RELEASE, unit, TM_FREE);
    return act(TM_DENY_OWNED, unit, o);
}

tm_action tm_click_header(const tm_state *s, int column_player, tm_sel *sel) {
    if (s->in_dungeon) return act(TM_DENY_LOCKED, -1, 0);
    if (s->me != 0) return act(TM_NONE, -1, 0);
    if (column_player == TM_RESERVED) return act(TM_NONE, -1, 0);   /* nobody assigns TO reserved */
    if (column_player != TM_FREE && !present(s, column_player)) { sel->picked = -1; return act(TM_NONE, -1, 0); }   /* left the game */
    if (sel->picked >= 0 && sel->picked < s->nunits) {
        int u = sel->picked;
        sel->picked = -1;
        if (s->owner[u] == column_player) return act(TM_UNPICK, u, 0);
        return act(TM_ASSIGN, u, column_player);
    }
    sel->target = sel->target == column_player ? TM_NO_TARGET : column_player;
    return act(TM_TARGET, -1, sel->target);
}

tm_action tm_click_ready(const tm_state *s) {
    if (s->in_dungeon) return act(TM_DENY_LOCKED, -1, 0);
    int me = s->me;
    if (me < 0 || me >= TM_MAX_PLAYERS) return act(TM_NONE, -1, 0);
    tm_action a = act((s->ready >> me & 1u) ? TM_UNREADY : TM_READY, -1, 0);
    copy_marks(s, &a);
    if (a.kind == TM_READY && a.nmarks == 0) return act(TM_DENY_NO_MARKS, -1, 0);
    return a;
}

/* Same rule as dsnet build_start (2026-10-12): every present player must be ready (a player with no unit
   cannot be ready, so he must take one first). */
uint32_t tm_waiting_mask(const tm_state *s) { return s->present & ~s->ready; }

int tm_all_ready(const tm_state *s) { return s->present && !tm_waiting_mask(s); }

int tm_party(const tm_state *s, uint8_t (*out)[16], int max) {
    int n = 0;
    for (int p = 0; p < TM_MAX_PLAYERS; p++) {
        if (!present(s, p) || !(s->ready >> p & 1u)) continue;
        for (int k = 0; k < s->nmarked[p] && k < TM_MAX_MARK; k++) {
            int i = unit_index(s, s->marked[p][k]);
            if (i < 0 || s->owner[i] != p || n >= max) continue;     /* gone or moved: dsnet drops it too */
            memcpy(out[n++], s->marked[p][k], 16);
        }
    }
    return n;
}

size_t tm_ready_write(uint8_t *out, size_t cap, int ready, const uint8_t (*marks)[16], int n) {
    if (n < 0 || n > TM_MAX_MARK || cap < (size_t)(2 + 16 * n)) return 0;
    out[0] = (uint8_t)(ready ? 1 : 0); out[1] = (uint8_t)n;
    for (int k = 0; k < n; k++) memcpy(out + 2 + 16 * k, marks[k], 16);
    return (size_t)(2 + 16 * n);
}

int tm_ready_read(const uint8_t *p, size_t len, int *ready, uint8_t (*marks)[16], int *n) {
    if (len < 2 || p[0] > 1 || p[1] > TM_MAX_MARK || len != (size_t)(2 + 16 * p[1])) return -1;
    *ready = p[0]; *n = p[1];
    for (int k = 0; k < *n; k++) memcpy(marks[k], p + 2 + 16 * k, 16);
    return 0;
}

int tm_ready_valid(const tm_state *s, int issuer, const uint8_t (*marks)[16], int n) {
    if (n < 0 || n > tm_mark_limit(s)) return 0;
    for (int k = 0; k < n; k++) {
        int i = unit_index(s, marks[k]);
        if (i < 0 || s->owner[i] != issuer) return 0;
        for (int j = 0; j < k; j++) if (!memcmp(marks[j], marks[k], 16)) return 0;
    }
    return 1;
}

void tm_header_text(const tm_state *s, int pl, const char *name, char *out, size_t cap) {
    if (pl == TM_FREE) { snprintf(out, cap, "Free units"); return; }
    char nm[40];
    snprintf(nm, sizeof nm, "%s", name && name[0] ? name : "");
    if (!nm[0]) { if (pl == 0) snprintf(nm, sizeof nm, "Host"); else snprintf(nm, sizeof nm, "Player %d", pl + 1); }
    const char *st;
    if (!present(s, pl)) st = "left the game";
    else if (s->ready >> pl & 1u) st = "<color=#71ab40>READY</color>";
    else st = "choosing";
    int n = pl < TM_MAX_PLAYERS ? s->nmarked[pl] : 0;
    snprintf(out, cap, "<color=%s>%s%s</color>\n<size=70%%>%s - %d/%d for the run</size>",
             ui_player_color(pl), nm, pl == s->me ? " (you)" : "", st, n, tm_mark_limit(s));
}

int tm_region_vote(const int pick[TM_MAX_PLAYERS], uint32_t present) {
    int best = -1, best_n = 0, tie = 0;
    for (int p = 0; p < TM_MAX_PLAYERS; p++) {
        if (!(present >> p & 1u) || pick[p] < 0) continue;
        int r = pick[p], n = 0;
        for (int q = 0; q < TM_MAX_PLAYERS; q++) if ((present >> q & 1u) && pick[q] == r) n++;
        if (n > best_n || (n == best_n && r < best)) { best = r; best_n = n; }
    }
    if (best < 0) return -1;
    for (int p = 0; p < TM_MAX_PLAYERS; p++) {                /* other regions with as many votes? */
        if (!(present >> p & 1u) || pick[p] < 0 || pick[p] == best) continue;
        int n = 0;
        for (int q = 0; q < TM_MAX_PLAYERS; q++) if ((present >> q & 1u) && pick[q] == pick[p]) n++;
        if (n == best_n) tie = 1;
    }
    if (tie && (present & 1u) && pick[0] >= 0) {               /* host breaks the tie if his pick is in it */
        int n = 0;
        for (int q = 0; q < TM_MAX_PLAYERS; q++) if ((present >> q & 1u) && pick[q] == pick[0]) n++;
        if (n == best_n) return pick[0];
    }
    return best;
}

int tm_region_step(const int *avail, int n, int cur, int dir) {
    if (n <= 0) return cur;
    int i = -1;
    for (int k = 0; k < n; k++) if (avail[k] == cur) i = k;
    if (i < 0) return dir >= 0 ? avail[0] : avail[n - 1];
    return avail[((i + (dir >= 0 ? 1 : -1)) % n + n) % n];
}

void tm_panel_party(const tm_state *s, const uint8_t (*panel)[16], int n, tm_panel_res *r) {
    memset(r, 0, sizeof *r);
    for (int i = 0; i < n; i++) {
        int u = unit_index(s, panel[i]);
        if (u < 0) continue;                                   /* not a live player unit */
        int dup = 0;
        for (int k = 0; k < r->nmarks; k++) if (!memcmp(r->marks[k], panel[i], 16)) dup = 1;
        if (dup) continue;
        int o = s->owner[u];
        if (o != s->me && o != TM_FREE) { r->foreign++; continue; }
        if (r->nmarks >= tm_mark_limit(s)) { r->over++; continue; }
        memcpy(r->marks[r->nmarks++], panel[i], 16);
        if (o == TM_FREE) memcpy(r->claim[r->nclaim++], panel[i], 16);
    }
    if (!r->nmarks && s->me >= 0 && s->me < TM_MAX_PLAYERS && s->nmarked[s->me] > 0) {
        r->nmarks = s->nmarked[s->me] > tm_mark_limit(s) ? tm_mark_limit(s) : s->nmarked[s->me];
        memcpy(r->marks, s->marked[s->me], (size_t)r->nmarks * 16);
        r->kept_old = 1;
    }
}

/* ---- expedition proposal */
tm_pevent tm_prop_propose(tm_prop *p, int player, int region, uint32_t present) {
    if (player < 0 || player >= TM_MAX_PLAYERS || !(present >> player & 1u)) return TM_PE_NONE;
    if (p->active && p->proposer != player) return TM_PE_NONE;          /* one proposal at a time */
    int was = p->active;
    p->active = 1; p->proposer = player; p->region = region;
    return was ? TM_PE_UPDATED : TM_PE_STARTED;
}
tm_pevent tm_prop_answer(tm_prop *p, int player, int kind, uint32_t present) {
    if (!p->active || player < 0 || player >= TM_MAX_PLAYERS || !(present >> player & 1u)) return TM_PE_NONE;
    tm_pevent e = TM_PE_NONE;
    if (kind == TM_ANS_DECLINE && player != p->proposer) e = TM_PE_DECLINED;
    else if (kind == TM_ANS_CANCEL && player == p->proposer) e = TM_PE_CANCELLED;
    else if (kind == TM_ANS_TIMEOUT && player == 0) e = TM_PE_TIMEOUT;
    if (e) p->active = 0;
    return e;
}
tm_pevent tm_prop_check(tm_prop *p, uint32_t present) {
    if (p->active && !(present >> p->proposer & 1u)) { p->active = 0; return TM_PE_PROPOSER_LEFT; }
    return TM_PE_NONE;
}
uint32_t tm_prop_waiting(const tm_prop *p, const tm_state *s) { return p->active ? s->present & ~s->ready : 0; }
int tm_party_members(const tm_state *s) {
    int n = 0;
    for (int pl = 0; pl < TM_MAX_PLAYERS; pl++) {
        if (!present(s, pl)) continue;
        for (int k = 0; k < s->nmarked[pl] && k < TM_MAX_MARK; k++) { int i = unit_index(s, s->marked[pl][k]); if (i >= 0 && s->owner[i] == pl) n++; }
    }
    return n;
}

/* ---- the proposal's party */
void tm_proposal_party(const tm_state *s, const uint8_t (*panel)[16], int np, tm_prop_res *r) {
    memset(r, 0, sizeof *r);
    int per[TM_MAX_PLAYERS] = {0};
    for (int i = 0; i < np; i++) {
        int u = unit_index(s, panel[i]);
        if (u < 0) { r->skipped++; continue; }
        int dup = 0;
        for (int k = 0; k < r->n; k++) if (!memcmp(r->party[k], panel[i], 16)) dup = 1;
        if (dup) continue;
        int o = s->owner[u];
        if (o == TM_FREE) o = s->me;                                   /* taken for the proposer */
        else if (!present(s, o)) { r->skipped++; continue; }          /* reserved / away */
        if (o < 0) { r->skipped++; continue; }
        if (r->n >= TM_PARTY_MAXN) { r->err = 1; continue; }
        memcpy(r->party[r->n++], panel[i], 16);
        if (s->owner[u] == TM_FREE) memcpy(r->claim[r->nclaim++], panel[i], 16);
        per[o]++;
    }
    if (!r->n) { r->err = 3; return; }
    if (r->n > TM_PARTY_CAP) { r->err = 1; return; }
    for (int p = 0; p < TM_MAX_PLAYERS; p++) if (per[p] > tm_mark_limit(s)) { r->err = 2; r->err_player = p; return; }
}
int tm_party_of(const tm_state *s, const uint8_t (*party)[16], int n, int player, int proposer, uint8_t (*out)[16]) {
    int k = 0;
    for (int i = 0; i < n && k < TM_MAX_MARK; i++) {
        int u = unit_index(s, party[i]);
        if (u < 0) continue;
        int o = s->owner[u];
        if (o == player || (o == TM_FREE && player == proposer)) memcpy(out[k++], party[i], 16);
    }
    return k;
}
