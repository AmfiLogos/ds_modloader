/* ui_model - pure logic of the multiplayer interface. See ui_model.h. */
#include "ui_model.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------- players */
const char *ui_player_label(const ui_players *p, int player, char out[40]) {
    if (p && player >= 0 && player < UI_MAX_PLAYERS && p->name[player][0]) snprintf(out, 40, "%s", p->name[player]);
    else if (player == 0) snprintf(out, 40, "Host");
    else snprintf(out, 40, "Player %d", player + 1);
    return out;
}

/* One colour per player (owner 2026-10-14), readable on the game's dark panels (#222025). lan_v10 (owner 01:40): a
   colour-blind-safe palette that avoids every game status colour: research/ux_flow_v10.md section 4 (Machado 2009
   simulation + CIEDE2000; slots 1-4 stay 13.3+ apart in every vision; no red / orange / green / teal / purple hue,
   L* 60+, 10+ from the cream body text #ccbeb8 and the away grey #8a8088). Slot order = join order. */
static const char *const colors[UI_PALETTE_N] = { "#FFEE88", "#44AAFF", "#DD66BB", "#EEF6FF", "#AA9988", "#DDAA00", "#BBBBCC", "#AACCFF" };
static const char *const color_names[UI_PALETTE_N] = { "Yellow", "Blue", "Pink", "White", "Taupe", "Gold", "Silver", "Light blue" };
const char *ui_palette_color(int i) { return colors[(unsigned)i % UI_PALETTE_N]; }
const char *ui_palette_name(int i) { return color_names[(unsigned)i % UI_PALETTE_N]; }
/* slot -> stable player uid -> colour (owner 2026-10-14: a rejoin into another slot keeps the colour). The glue sets
   ui_color_resolver to the dsnet table (dsnet_player_color); unset or unknown slot: by slot. lan_v10: a viewer's own
   choice for that player (ui_color_override) comes first; it is local to this screen. */
int (*ui_color_resolver)(int player);
int (*ui_color_override)(int player);
const char *ui_player_color(int player) {
    int c = ui_color_override && player >= 0 ? ui_color_override(player) : -1;
    if (c < 0) c = ui_color_resolver && player >= 0 ? ui_color_resolver(player) : -1;
    return colors[(unsigned)(c >= 0 ? c : player) % UI_PALETTE_N];
}

void ui_owner_tag(const ui_players *p, int owner, char *out, size_t cap) {
    if (!cap) return;
    out[0] = 0;
    if (owner < 0 || owner >= UI_MAX_PLAYERS) return;
    char nm[40];
    snprintf(out, cap, " <color=%s>[%s]</color>", ui_player_color(owner), ui_player_label(p, owner, nm));
}

/* ---------------------------------------------------------------- unit claims */
int ui_max_owned = UI_MAX_OWNED;
int ui_ownership_valid(int issuer, int cur, int nw, const int cnt[UI_MAX_PLAYERS]) {
    if (issuer < 0 || issuer >= UI_MAX_PLAYERS) return 0;
    if (issuer != 0 && !((cur == UI_UNCLAIMED || cur == issuer) && (nw == issuer || nw == UI_UNCLAIMED))) return 0;
    if (nw == UI_UNCLAIMED) return 1;
    if (nw < 0 || nw >= UI_MAX_PLAYERS) return 0;
    int c = cnt[nw] - (cur == nw ? 1 : 0);
    return c < ui_max_owned;
}

ui_act ui_portrait_action(int me, int cur, const int cnt[UI_MAX_PLAYERS], uint32_t present) {
    ui_act a = { UI_ACT_NONE, UI_UNCLAIMED };
    if (me < 0 || me >= UI_MAX_PLAYERS) return a;
    if (me != 0) {
        if (cur == me) { a.kind = UI_ACT_RELEASE; return a; }
        if (cur != UI_UNCLAIMED) { a.kind = UI_ACT_DENY_OWNED; a.target = cur; return a; }
        if (cnt[me] >= ui_max_owned) { a.kind = UI_ACT_DENY_FULL; a.target = me; return a; }
        a.kind = UI_ACT_CLAIM; a.target = me; return a;
    }
    /* host: next owner in the cycle unclaimed, 0, 1, ... (present players with room), unclaimed */
    int start = cur == UI_UNCLAIMED ? 0 : cur + 1;
    for (int p = start; p < UI_MAX_PLAYERS; p++) {
        if (!(present >> p & 1u)) continue;
        if (cnt[p] >= ui_max_owned) continue;
        a.kind = UI_ACT_ASSIGN; a.target = p; return a;
    }
    if (cur == UI_UNCLAIMED) { a.kind = UI_ACT_DENY_FULL; a.target = 0; return a; }   /* everyone full */
    a.kind = UI_ACT_ASSIGN; a.target = UI_UNCLAIMED;
    return a;
}

ui_act ui_portrait_action_admin(int me, int cur, const int cnt[UI_MAX_PLAYERS], uint32_t present, int admin) {
    if (me == 0 && !admin && (cur >= 1 || cur == UI_RESERVED)) { ui_act a = { UI_ACT_DENY_OWNED, cur }; return a; }
    if (cur == UI_RESERVED) { ui_act a = { UI_ACT_DENY_OWNED, cur }; return a; }    /* players: reserved = another's */
    return ui_portrait_action(me, cur, cnt, present);
}
int ui_may_order(int me, int owner) { return owner >= 0 ? owner == me : me == 0; }

/* ---------------------------------------------------------------- votes */
int ui_effective_speed(const ui_votes *v) {
    if (!v->allow_speedup) return 1;
    int s = 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++)
        if (v->present >> i & 1u) { int x = v->speed[i] ? v->speed[i] : 1; if (!s || x < s) s = x; }
    return s ? s : 1;
}
int ui_is_paused(const ui_votes *v) {
    if (v->story_pause & v->present) return 1;               /* owner 2026-10-12: story pauses all, even with pause off */
    if (!v->allow_pause) return 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) if ((v->present >> i & 1u) && v->pause[i]) return 1;
    return 0;
}
int ui_pause_voters(const ui_votes *v, const ui_players *p, char *out, size_t cap) {
    int n = 0; size_t len = 0;
    if (cap) out[0] = 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) {
        if (!(v->present >> i & 1u) || !v->pause[i]) continue;
        char nm[40]; ui_player_label(p, i, nm);
        if (len < cap) len += (size_t)snprintf(out + len, cap - len, "%s%s", n ? ", " : "", nm);
        n++;
    }
    return n;
}

void ui_pause_text(const ui_votes *v, const ui_players *p, char *out, size_t cap) {
    char who[256], nm[40];
    if (!cap) return;
    int n = v->allow_pause ? ui_pause_voters(v, p, who, sizeof who) : 0;
    if (v->story_pause & v->present) snprintf(out, cap, n ? "Paused for a story event (and by %s)" : "Paused for a story event", who);
    else if (n) snprintf(out, cap, "Paused by %s", who);
    else if (v->pause_by1 > 0) snprintf(out, cap, "Paused by %s", ui_player_label(p, v->pause_by1 - 1, nm));
    else snprintf(out, cap, "Paused");
}
const char *ui_speed_text(int s) { return s >= 2 ? "Fast" : "Normal"; }

/* ---------------------------------------------------------------- status line */
/* comma list of present players (except `skip`) whose speed vote is (above ? > : ==) eff */
static int speed_names(const ui_votes *v, const ui_players *p, int eff, int above, int skip, char *out, size_t cap) {
    size_t len = 0; int n = 0;
    if (cap) out[0] = 0;
    for (int i = 0; i < UI_MAX_PLAYERS && len < cap; i++) {
        int s = v->speed[i] ? v->speed[i] : 1;
        if (!(v->present >> i & 1u) || i == skip || (above ? s <= eff : s != eff)) continue;
        char nm[40];
        len += (size_t)snprintf(out + len, cap - len, "%s%s", n++ ? ", " : "", ui_player_label(p, i, nm));
    }
    return n;
}

void ui_status_line(ui_net_state st, const ui_votes *v, const ui_players *p, int me, char *out, size_t cap) {
    if (!cap) return;
    out[0] = 0;
    switch (st) {
    case UI_NET_OFF: return;
    case UI_NET_CONNECTING: snprintf(out, cap, "Connecting to host..."); return;
    case UI_NET_WAITING: snprintf(out, cap, "Waiting for the host's game..."); return;
    case UI_NET_DESYNC: snprintf(out, cap, "Resyncing with host..."); return;
    case UI_NET_LOADING: snprintf(out, cap, "Resyncing with host... (loading)"); return;
    case UI_NET_LOST: snprintf(out, cap, "Connection to host lost"); return;
    case UI_NET_PLAYING: break;
    }
    if (v->busy & v->present & ~(1u << ((unsigned)me & 31))) {   /* transitions barrier: someone still loads */
        size_t len = (size_t)snprintf(out, cap, "Waiting for ");
        int n = 0;
        for (int i = 0; i < UI_MAX_PLAYERS && len < cap; i++) {
            if (i == me || !(v->busy & v->present & 1u << i)) continue;
            char nm[40];
            len += (size_t)snprintf(out + len, cap - len, "%s%s", n++ ? ", " : "", ui_player_label(p, i, nm));
        }
        if (len < cap) snprintf(out + len, cap - len, " (loading)");
        return;
    }
    if (ui_is_paused(v) || v->paused_ext) { ui_pause_text(v, p, out, cap); return; }
    if (!v->allow_speedup) return;
    /* speed request: someone voted higher than the effective speed (review #19) */
    int eff = ui_effective_speed(v), want = 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) if ((v->present >> i & 1u) && v->speed[i] > want) want = v->speed[i];
    if (want <= eff) return;
    char names[256];
    int mine = me >= 0 && me < UI_MAX_PLAYERS && (v->present >> me & 1u) && v->speed[me] > eff;
    if (mine) {
        speed_names(v, p, eff, 0, me, names, sizeof names);
        snprintf(out, cap, "You want %s \u00B7 waiting for %s", ui_speed_text(want), names);
    } else {
        int n = speed_names(v, p, eff, 1, -1, names, sizeof names);
        snprintf(out, cap, "%s %s %s \u00B7 press >> to agree", names, n > 1 ? "want" : "wants", ui_speed_text(want));
    }
}

/* ---------------------------------------------------------------- popups */
ui_pop_kind ui_pop_kind_of(int t) {
    switch (t) { case 3: return UI_POP_RESULT; case 11: return UI_POP_TRADE; case 21: return UI_POP_LEVELUP; case 22: return UI_POP_STORY; default: return UI_POP_OTHER; }
}
int ui_result_chooser(int unit_owner, int forced) { return !forced && unit_owner >= 0 ? unit_owner : 0; }
int ui_story_chooser(int unit_owner) { return unit_owner == UI_NO_UNIT ? -1 : unit_owner == UI_UNCLAIMED ? 0 : unit_owner; }
int ui_story_chooser_rule(int rule, int unit_owner) {
    switch (rule) {
    case UI_EVC_HOST: return 0;
    case UI_EVC_FIRST: return UI_STORY_FIRST;
    case UI_EVC_MAJORITY: return UI_STORY_VOTE;
    default: return unit_owner >= 0 ? unit_owner : 0;      /* owner; unclaimed / no unit / away: the host */
    }
}

ui_route ui_popup_route(ui_pop_kind k, int me, int owner) {
    switch (k) {
    case UI_POP_LEVELUP: case UI_POP_TRADE: {
        int who = owner >= 0 ? owner : 0;                    /* unclaimed or unknown unit: the host */
        return who == me ? UI_ROUTE_OPEN_NO_PAUSE : UI_ROUTE_SKIP;
    }
    case UI_POP_RESULT:
        return ui_result_chooser(owner, 0) == me ? UI_ROUTE_RESULT_CHOOSER : UI_ROUTE_RESULT_READONLY;
    case UI_POP_STORY: {
        int c = ui_story_chooser(owner);
        if (c < 0) return UI_ROUTE_STORY_VOTE;
        return c == me ? UI_ROUTE_STORY_CHOOSER : UI_ROUTE_STORY_READONLY;
    }
    default: return UI_ROUTE_STOCK;
    }
}

void ui_story_reset(ui_story *s, uint32_t node, int chooser) {
    s->node = node; s->chooser = chooser; s->decided = -1; s->handover = 0;
    memset(s->vote, -1, sizeof s->vote);
}

static int tally(ui_story *s, uint32_t present) {
    if (s->decided >= 0 || s->chooser >= 0 || !present) return s->decided;
    int cnt[UI_MAX_CHOICES] = {0};
    for (int i = 0; i < UI_MAX_PLAYERS; i++) {
        if (!(present >> i & 1u)) continue;
        if (s->vote[i] < 0) return -1;                       /* someone still has to vote */
        cnt[(int)s->vote[i]]++;
    }
    int best = 0;
    for (int c = 0; c < UI_MAX_CHOICES; c++) if (cnt[c] > best) best = cnt[c];
    int hv = (present & 1u) ? s->vote[0] : -1;
    if (hv >= 0 && cnt[hv] == best) return s->decided = hv;    /* host breaks a tie */
    for (int c = 0; c < UI_MAX_CHOICES; c++) if (cnt[c] == best) return s->decided = c;
    return -1;
}

int ui_story_vote(ui_story *s, int player, uint32_t node, int choice, uint32_t present, int chooser) {
    if (player < 0 || player >= UI_MAX_PLAYERS || choice < 0 || choice >= UI_MAX_CHOICES || !node) return s->decided;
    if (node != s->node) ui_story_reset(s, node, chooser);
    if (s->decided >= 0) return s->decided;
    if (s->chooser >= 0) {
        if (player == s->chooser) { s->vote[player] = (int8_t)choice; s->decided = choice; }
        return s->decided;
    }
    if (s->chooser == UI_STORY_FIRST) { s->vote[player] = (int8_t)choice; return s->decided = choice; }
    s->vote[player] = (int8_t)choice;
    return tally(s, present);
}
int ui_story_recheck(ui_story *s, uint32_t present) { return tally(s, present); }
int ui_story_close(ui_story *s, uint32_t present) {
    if (s->decided >= 0 || !s->node) return s->decided;
    uint32_t voted = 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) if ((present >> i & 1u) && s->vote[i] >= 0) voted |= 1u << i;
    if (!voted) return -1;
    int ch = s->chooser; s->chooser = UI_STORY_VOTE;
    int d = tally(s, voted);                                  /* the votes in so far, host breaks a tie */
    if (d < 0) s->chooser = ch;
    return d;
}
int ui_story_timeout(ui_story *s, uint32_t node, int chooser, uint32_t present) {
    if (!node) return s->decided;
    if (node != s->node) ui_story_reset(s, node, chooser);
    if (s->decided >= 0 || s->handover) return s->decided;
    if (s->chooser == UI_STORY_VOTE) { int d = ui_story_close(s, present); if (d >= 0) return d; }
    s->chooser = 0; s->handover = 1;                          /* the host chooses now */
    if (s->vote[0] >= 0) s->decided = s->vote[0];             /* (a vote the host already gave) */
    return s->decided;
}
void ui_story_line(const ui_story *s, const ui_players *p, int me, int chooser, int votes_in, int voters, int left_s,
                   char *out, size_t cap) {
    char nm[40], hn[40], tail[96] = "";
    ui_player_label(p, 0, hn);
    if (s && s->decided >= 0) { snprintf(out, cap, "Chosen"); return; }
    if (s && s->handover) {
        if (me == 0) snprintf(out, cap, "You decide (no answer in 60 s)");
        else snprintf(out, cap, "%s decides (no answer in 60 s)", hn);
        return;
    }
    if (left_s >= 0) {
        if (chooser == UI_STORY_VOTE) snprintf(tail, sizeof tail, " \u00B7 %d s, then the votes decide", left_s);
        else snprintf(tail, sizeof tail, " \u00B7 %d s, then %s decide%s", left_s, me == 0 ? "you" : hn, me == 0 ? "" : "s");
    }
    if (chooser >= 0) {
        if (chooser == me) snprintf(out, cap, "You choose%s", tail);
        else snprintf(out, cap, "%s chooses%s", ui_player_label(p, chooser, nm), tail);
    } else if (chooser == UI_STORY_FIRST) snprintf(out, cap, "The first answer decides%s", tail);
    else snprintf(out, cap, "Vote \u00B7 %d of %d \u00B7 host breaks ties%s", votes_in, voters, tail);
}
int ui_story_votes_in(const ui_story *s, uint32_t present) {
    int n = 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) if ((present >> i & 1u) && s->vote[i] >= 0) n++;
    return n;
}
uint32_t ui_story_node_hash(const char *const *keys, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) {
        for (const char *c = keys[i] ? keys[i] : ""; *c; c++) { h ^= (uint8_t)*c; h *= 16777619u; }
        h ^= 0x1F; h *= 16777619u;                           /* separator */
    }
    return h ? h : 1;
}
void ui_story_args_write(uint8_t o[UI_STORY_ARGS], uint32_t node, int choice) {
    o[0] = (uint8_t)node; o[1] = (uint8_t)(node >> 8); o[2] = (uint8_t)(node >> 16); o[3] = (uint8_t)(node >> 24);
    o[4] = (uint8_t)choice;
}
int ui_story_args_read(const uint8_t *a, size_t n, uint32_t *node, int *choice) {
    if (n < UI_STORY_ARGS) return -1;
    *node = (uint32_t)a[0] | (uint32_t)a[1] << 8 | (uint32_t)a[2] << 16 | (uint32_t)a[3] << 24;
    *choice = a[4];
    return *choice < UI_MAX_CHOICES && *node ? 0 : -1;
}

/* ---------------------------------------------------------------- map-change vote */
void ui_mapvote_clear(ui_mapvote *m) { memset(m, 0, sizeof *m); m->decided = -1; memset(m->vote, -1, sizeof m->vote); }
static int map_tally(ui_mapvote *m, uint32_t present) {
    if (!m->id || m->decided >= 0) return m->decided;
    int yes = 0, no = 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) {
        if (!(present >> i & 1u)) continue;
        if (m->vote[i] < 0) return -1;
        if (m->vote[i]) yes++; else no++;
    }
    if (!yes && !no) return -1;
    if (yes != no) return m->decided = yes > no;
    return m->decided = (present & 1u) ? m->vote[0] : 0;     /* tie: the host's vote */
}
int ui_mapvote_open(const ui_mapvote *m) { return m->id && m->decided < 0; }
int ui_mapvote_propose(ui_mapvote *m, uint32_t id, int proposer, int kind, const uint8_t *order, size_t len, uint32_t present) {
    if (!id || proposer < 0 || proposer >= UI_MAX_PLAYERS || len > UI_MAP_MAX_ORDER || ui_mapvote_open(m)) return 0;
    ui_mapvote_clear(m);
    m->id = id; m->proposer = proposer; m->kind = kind;
    m->order_len = (uint16_t)len;
    if (len) memcpy(m->order, order, len);
    m->vote[proposer] = 1;
    map_tally(m, present);
    return 1;
}
int ui_mapvote_vote(ui_mapvote *m, int player, uint32_t id, int accept, uint32_t present) {
    if (!m->id || id != m->id || player < 0 || player >= UI_MAX_PLAYERS || m->decided >= 0) return m->decided;
    if (accept == UI_MAP_CANCEL || accept == UI_MAP_CANCEL_USER) {   /* withdrawn: only the proposer or the host */
        if (player != m->proposer && player != 0) return -1;
        m->cancelled = accept;
        return m->decided = 0;
    }
    if (player == m->proposer) return -1;                    /* the proposer always accepts */
    m->vote[player] = accept ? 1 : 0;
    return map_tally(m, present);
}
int ui_mapvote_recheck(ui_mapvote *m, uint32_t present) { return map_tally(m, present); }
const char *ui_map_kind_text(int kind) {
    switch (kind) {
    case UI_MAP_ENTER_DUNGEON: return "enter the dungeon";
    case UI_MAP_RETURN_BASE: return "return to the settlement";
    case UI_MAP_NEXT_FLOOR: return "go down to the next floor";
    default: return "change the map";
    }
}
const char *ui_map_kind_text_floor(int kind, int next_floor, char *out, size_t cap) {
    if (kind == UI_MAP_NEXT_FLOOR && next_floor >= 1) snprintf(out, cap, "go down to floor %d", next_floor);
    else snprintf(out, cap, "%s", ui_map_kind_text(kind));
    return out;
}
int ui_mapvote_wait_line(const ui_mapvote *m, const ui_players *p, int me, uint32_t present, char *out, size_t cap) {
    if (cap) out[0] = 0;
    if (!ui_mapvote_open(m) || m->proposer != me) return 0;
    size_t len = (size_t)snprintf(out, cap, "Waiting for ");
    int n = 0, yes = 0, all = 0;
    for (int i = 0; i < UI_MAX_PLAYERS; i++) {
        if (!(present >> i & 1u)) continue;
        all++;
        if (m->vote[i] == 1) yes++;
        if (m->vote[i] >= 0 || len >= cap) continue;
        char nm[40];
        len += (size_t)snprintf(out + len, cap - len, "%s%s", n++ ? ", " : "", ui_player_label(p, i, nm));
    }
    if (len < cap) snprintf(out + len, cap - len, " (%d of %d agree)", yes, all);
    return 1;
}
const char *ui_map_question(int kind, int next_floor, char *out, size_t cap) {
    if (kind == UI_MAP_NEXT_FLOOR && next_floor >= 1) snprintf(out, cap, "Go down to floor %d?", next_floor);
    else if (kind == UI_MAP_NEXT_FLOOR) snprintf(out, cap, "Go down to the next floor?");
    else if (kind == UI_MAP_RETURN_BASE) snprintf(out, cap, "Return to the settlement?");
    else if (kind == UI_MAP_ENTER_DUNGEON) snprintf(out, cap, "Enter the dungeon?");
    else snprintf(out, cap, "Change the map?");
    return out;
}
const char *ui_map_short(int kind) {
    return kind == UI_MAP_NEXT_FLOOR ? "go down" : kind == UI_MAP_RETURN_BASE ? "return to the settlement" : kind == UI_MAP_ENTER_DUNGEON ? "enter the dungeon" : "change the map";
}
void ui_mapvote_marks(const ui_mapvote *m, const ui_players *p, uint32_t present, const char *ok, char *out, size_t cap) {
    size_t n = 0; int k = 0;
    if (cap) out[0] = 0;
    for (int i = 0; i < UI_MAX_PLAYERS && n < cap; i++) {
        if (!(present >> i & 1u)) continue;
        char nm[40];
        const char *mk = m->vote[i] == 1 ? ok : m->vote[i] == 0 ? "<color=#f16071>no</color>" : "\u2026";
        n += (size_t)snprintf(out + n, cap - n, "%s<color=%s>%s</color> %s", k++ ? "   " : "", ui_player_color(i), ui_player_label(p, i, nm),
                              m->vote[i] == 1 ? "" : mk);
        if (m->vote[i] == 1 && n < cap) n += (size_t)snprintf(out + n, cap - n, "<color=#71ab40>%s</color>", ok);
    }
}
void ui_mapvote_result(const ui_mapvote *m, const ui_players *p, uint32_t present, int cur_floor_no, char *out, size_t cap) {
    char nm[40];
    if (m->cancelled == UI_MAP_CANCEL) { snprintf(out, cap, "No answer in %d s: the vote was cancelled", UI_MAP_TIMEOUT_S); return; }
    if (m->cancelled == UI_MAP_CANCEL_USER) { snprintf(out, cap, "%s cancelled the vote", ui_player_label(p, m->proposer, nm)); return; }
    if (m->decided == 1) {
        int yes = 0, all = 0;
        for (int i = 0; i < UI_MAX_PLAYERS; i++) if (present >> i & 1u) { all++; yes += m->vote[i] == 1; }
        const char *what = m->kind == UI_MAP_NEXT_FLOOR ? "going down" : m->kind == UI_MAP_RETURN_BASE ? "returning to the settlement"
                         : m->kind == UI_MAP_ENTER_DUNGEON ? "entering the dungeon" : "changing the map";
        if (yes >= all) snprintf(out, cap, "Everyone agreed: %s", what);
        else snprintf(out, cap, "Agreed (%d of %d): %s", yes, all, what);
        return;
    }
    size_t n = 0; int k = 0;
    for (int i = 0; i < UI_MAX_PLAYERS && n < cap; i++)
        if ((present >> i & 1u) && m->vote[i] == 0) n += (size_t)snprintf(out + n, cap - n, "%s%s", k++ ? ", " : "", ui_player_label(p, i, nm));
    if (!k) n = (size_t)snprintf(out, cap, "The vote");
    if (n < cap) {
        if (m->kind == UI_MAP_NEXT_FLOOR && cur_floor_no >= 1) snprintf(out + n, cap - n, " said no: staying on floor %d", cur_floor_no);
        else snprintf(out + n, cap - n, " said no: staying here");
    }
}
size_t ui_map_propose_write(uint8_t *o, size_t cap, uint32_t id, int kind, const uint8_t *order, size_t len) {
    if (len > UI_MAP_MAX_ORDER || cap < 7 + len) return 0;
    o[0] = (uint8_t)id; o[1] = (uint8_t)(id >> 8); o[2] = (uint8_t)(id >> 16); o[3] = (uint8_t)(id >> 24);
    o[4] = (uint8_t)kind; o[5] = (uint8_t)len; o[6] = (uint8_t)(len >> 8);
    if (len) memcpy(o + 7, order, len);
    return 7 + len;
}
int ui_map_propose_read(const uint8_t *a, size_t n, uint32_t *id, int *kind, const uint8_t **order, size_t *len) {
    if (n < 7) return -1;
    *id = (uint32_t)a[0] | (uint32_t)a[1] << 8 | (uint32_t)a[2] << 16 | (uint32_t)a[3] << 24;
    *kind = a[4];
    *len = (size_t)a[5] | (size_t)a[6] << 8;
    if (!*id || *len > UI_MAP_MAX_ORDER || 7 + *len > n) return -1;
    *order = a + 7;
    return 0;
}
void ui_map_vote_write(uint8_t o[5], uint32_t id, int accept) {
    o[0] = (uint8_t)id; o[1] = (uint8_t)(id >> 8); o[2] = (uint8_t)(id >> 16); o[3] = (uint8_t)(id >> 24);
    o[4] = accept == UI_MAP_CANCEL || accept == UI_MAP_CANCEL_USER ? (uint8_t)accept : accept ? 1 : 0;
}
int ui_map_vote_read(const uint8_t *a, size_t n, uint32_t *id, int *accept) {
    if (n < 5) return -1;
    *id = (uint32_t)a[0] | (uint32_t)a[1] << 8 | (uint32_t)a[2] << 16 | (uint32_t)a[3] << 24;
    *accept = a[4] == UI_MAP_CANCEL || a[4] == UI_MAP_CANCEL_USER ? a[4] : a[4] != 0;
    return *id ? 0 : -1;
}

/* ---------------------------------------------------------------- demo-v2 view helpers */
int ui_local_order(const int *owner, int n, int me, int *out) {
    int k = 0;
    for (int i = 0; i < n; i++) if (owner[i] == me) out[k++] = i;
    for (int i = 0; i < n; i++) if (owner[i] != me) out[k++] = i;
    return k;
}
void ui_resync_text(const ui_players *p, int who, int me, char *out, size_t cap) {
    char nm[40], hn[40];
    ui_player_label(p, 0, hn);
    if (who == me) snprintf(out, cap, "Your game went out of sync: reloading everyone from %s's save", hn);
    else snprintf(out, cap, "%s's game went out of sync: reloading everyone from %s's save", ui_player_label(p, who, nm), hn);
}
void ui_owner_tag_world(const ui_players *p, int owner, const char *dot, char *out, size_t cap) {
    if (!cap) return;
    out[0] = 0;
    if (owner < 0 || owner >= UI_MAX_PLAYERS) return;
    char nm[40];
    snprintf(out, cap, " <color=%s>%s%s%s</color>", ui_player_color(owner), dot ? dot : "", dot && dot[0] ? " " : "", ui_player_label(p, owner, nm));
}
void ui_pause_line(const ui_players *p, int by, int story, char *out, size_t cap) {
    char nm[40];
    if (by >= 0) snprintf(out, cap, "Paused by %s", ui_player_label(p, by, nm));
    else if (story) snprintf(out, cap, "Paused for a story event");
    else snprintf(out, cap, "Paused");
}
void ui_player_row(const ui_players *p, int player, int me, int ping_ms, int state, char *out, size_t cap) {
    char nm[40], ping[16];
    if (ping_ms >= 0) snprintf(ping, sizeof ping, "%d ms", ping_ms); else snprintf(ping, sizeof ping, "-");
    const char *st = state == 0 ? "in sync" : state == 1 ? "loading" : state == 2 ? "<color=#f16071>out of sync</color>" : "";
    snprintf(out, cap, "<color=%s>\u25CF</color> %s%s  %s  %s", ui_player_color(player), ui_player_label(p, player, nm),
             player == me ? " (you)" : "", player == me ? "" : ping, st);
}

/* ---------------------------------------------------------------- live-test settings */
void ui_settings_default(ui_settings *s) {
    memset(s, 0, sizeof *s);
    s->see_through = UI_SEE_THROUGH_MIN; s->names = UI_NAMES_OTHERS; s->tooltips = 1;
    s->pings = 1; s->ping_sound = 1; s->ping_chat = 1; s->views = 1; s->offscreen = 0; s->outline = UI_OUTLINE_OFF;
}
static int jint(const char *j, const char *key, int *out) {
    char k[48]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = strstr(j, k);
    if (!p) return 0;
    p = strchr(p + strlen(k), ':');
    if (!p) return 0;
    char *e; long v = strtol(p + 1, &e, 10);
    if (e == p + 1) return 0;
    *out = (int)v; return 1;
}
static int hex32(const char *h) { for (int i = 0; i < 32; i++) if (!isxdigit((unsigned char)h[i])) return 0; return 1; }
void ui_settings_parse(const char *j, ui_settings *s) {
    int v;
    if (!j) return;
    /* lan_v10: the see-through never goes under the minimum (an older file's 0 / 25 becomes 30) */
    if (jint(j, "see_through", &v) && v >= 0 && v <= 100) s->see_through = v < UI_SEE_THROUGH_MIN ? UI_SEE_THROUGH_MIN : v > 90 ? 90 : v;
    if (jint(j, "names", &v) && v >= UI_NAMES_OFF && v <= UI_NAMES_ALL) s->names = v;
    if (jint(j, "list_open", &v)) s->list_open = v != 0;
    if (jint(j, "pos_set", &v)) s->pos_set = v != 0;
    if (jint(j, "tooltips", &v)) s->tooltips = v != 0;
    if (jint(j, "compact", &v)) s->compact = v != 0;
    if (jint(j, "pos_x", &v) && v > -5000 && v < 5000) s->pos_x = v;
    if (jint(j, "pos_y", &v) && v > -5000 && v <= 0) s->pos_y = v;
    if (jint(j, "pings", &v)) s->pings = v != 0;
    if (jint(j, "ping_sound", &v)) s->ping_sound = v != 0;
    if (jint(j, "ping_chat", &v)) s->ping_chat = v != 0;
    if (jint(j, "views", &v)) s->views = v != 0;
    if (jint(j, "offscreen", &v)) s->offscreen = v != 0;
    if (jint(j, "outline", &v) && v >= UI_OUTLINE_OFF && v <= UI_OUTLINE_BOTH) s->outline = v;
    /* "colors":"<uid 32 hex>:<idx>,..." */
    const char *c = strstr(j, "\"colors\"");
    if (c && (c = strchr(c + 8, ':')) && (c = strchr(c, '"'))) {
        c++;
        while (hex32(c) && c[32] == ':') {
            char u[33]; memcpy(u, c, 32); u[32] = 0;
            int idx = atoi(c + 33);
            if (idx >= 0 && idx < UI_PALETTE_N) ui_settings_color_set(s, u, idx);
            c = strchr(c + 33, ',');
            if (!c) break;
            c++;
        }
    }
}
int ui_settings_format(const ui_settings *s, char *out, size_t cap) {
    int n = snprintf(out, cap, "{\"see_through\":%d,\"names\":%d,\"list_open\":%d,\"pos_set\":%d,\"pos_x\":%d,\"pos_y\":%d,\"tooltips\":%d,\"compact\":%d,"
                     "\"pings\":%d,\"ping_sound\":%d,\"ping_chat\":%d,\"views\":%d,\"offscreen\":%d,\"outline\":%d,\"colors\":\"",
                     s->see_through, s->names, s->list_open, s->pos_set, s->pos_x, s->pos_y, s->tooltips, s->compact,
                     s->pings, s->ping_sound, s->ping_chat, s->views, s->offscreen, s->outline);
    for (int i = 0; i < s->ncol && n > 0 && (size_t)n < cap; i++) n += snprintf(out + n, cap - (size_t)n, "%s%s:%d", i ? "," : "", s->col_uid[i], s->col_idx[i]);
    if (n > 0 && (size_t)n < cap) n += snprintf(out + n, cap - (size_t)n, "\"}\n");
    return n;
}
int ui_settings_color_get(const ui_settings *s, const char *u) {
    if (!u) return -1;
    for (int i = 0; i < s->ncol; i++) if (!strcmp(s->col_uid[i], u)) return s->col_idx[i];
    return -1;
}
void ui_settings_color_set(ui_settings *s, const char *u, int idx) {
    if (!u || !hex32(u)) return;
    int k = -1;
    for (int i = 0; i < s->ncol && k < 0; i++) if (!strcmp(s->col_uid[i], u)) k = i;
    if (idx < 0) {                                            /* back to the automatic colour */
        if (k >= 0) { for (int i = k; i + 1 < s->ncol; i++) { memcpy(s->col_uid[i], s->col_uid[i + 1], 33); s->col_idx[i] = s->col_idx[i + 1]; } s->ncol--; }
        return;
    }
    if (k < 0) {
        if (s->ncol == UI_MAX_COLOR_OVR) { for (int i = 0; i + 1 < s->ncol; i++) { memcpy(s->col_uid[i], s->col_uid[i + 1], 33); s->col_idx[i] = s->col_idx[i + 1]; } s->ncol--; }   /* oldest out */
        k = s->ncol++; snprintf(s->col_uid[k], 33, "%s", u);
    }
    s->col_idx[k] = idx % UI_PALETTE_N;
}
const char *ui_outline_text(int m) { return m == UI_OUTLINE_OWN ? "Your units" : m == UI_OUTLINE_FRIENDS ? "Friends' units" : m == UI_OUTLINE_BOTH ? "Both" : "Off"; }
int ui_see_through_next(int v) { return v < 50 ? 50 : v < 70 ? 70 : v < 90 ? 90 : UI_SEE_THROUGH_MIN; }
int ui_see_through_prev(int v) { return v > 70 ? 70 : v > 50 ? 50 : v > UI_SEE_THROUGH_MIN ? UI_SEE_THROUGH_MIN : 90; }
const char *ui_names_text(int m) { return m == UI_NAMES_ALL ? "All" : m == UI_NAMES_OFF ? "Off" : "Others"; }
int ui_name_tag_visible(int mode, int owner, int me) {
    if (owner < 0 || mode == UI_NAMES_OFF) return 0;
    return mode == UI_NAMES_ALL || owner != me;
}
int ui_may_order_admin(int me, int owner, int admin) { return ui_may_order(me, owner) || (admin && me == 0); }
int ui_filter_selection(const int *is_player, const int *owner, int n, int me, int admin, int *keep) {
    int mine = 0, k = 0;
    for (int i = 0; i < n; i++) if (is_player[i] && ui_may_order_admin(me, owner[i], admin)) mine++;
    for (int i = 0; i < n; i++) {
        keep[i] = n < 2 || !mine || !is_player[i] || ui_may_order_admin(me, owner[i], admin);
        k += keep[i];
    }
    return k;
}
void ui_player_row2(const ui_players *p, int player, int me, int ping_ms, int state, char *out, size_t cap) {
    char nm[40];
    ui_player_label(p, player, nm);
    if (player == me) { snprintf(out, cap, "<color=%s>%s</color>", ui_player_color(player), nm); return; }
    if (state == 2) snprintf(out, cap, "<color=%s>%s</color>  <color=#f16071>out of sync</color>", ui_player_color(player), nm);
    else if (state == 1) snprintf(out, cap, "<color=%s>%s</color>  loading", ui_player_color(player), nm);
    else if (ping_ms >= 0) snprintf(out, cap, "<color=%s>%s</color>  %d ms", ui_player_color(player), nm, ping_ms);
    else snprintf(out, cap, "<color=%s>%s</color>", ui_player_color(player), nm);
}
int ui_signal_bars(int ping, int state) {
    if (state == 2 || (state < 0 && ping < 0)) return 0;
    if (ping < 0) return state == 1 ? 1 : 4;               /* own row / loading without a ping */
    return ping < 60 ? 4 : ping < 120 ? 3 : ping < 200 ? 2 : 1;
}
void ui_player_row3(const ui_players *p, int player, int me, int ping_ms, int state, char *out, size_t cap) {
    char nm[40], bars[256]; size_t n = 0;
    ui_player_label(p, player, nm);
    int b = player == me ? -1 : ui_signal_bars(ping_ms, state);
    if (b == 0) snprintf(bars, sizeof bars, "<color=#f16071><b>X</b></color>");
    else if (b < 0) bars[0] = 0;
    else {
        static const int h[4] = { 45, 65, 85, 105 };          /* bar heights, % of the line */
        for (int i = 0; i < 4; i++)
            n += (size_t)snprintf(bars + n, sizeof bars - n, "<size=%d%%><color=%s>|</color></size>", h[i], i < b ? "#58D68D" : "#5D6D7E");
    }
    snprintf(out, cap, "%d  <color=%s>%s</color>  %s", player + 1, ui_player_color(player), nm, bars);
}
void ui_player_cols(const ui_players *p, int player, int me, int ping_ms, int state, const char *dot,
                    char *left, size_t lcap, char *right, size_t rcap) {
    char nm[40];
    snprintf(left, lcap, "<color=%s>%s</color> %s", ui_player_color(player), dot && dot[0] ? dot : "\u2022", ui_player_label(p, player, nm));
    if (!rcap) return;
    right[0] = 0;
    if (state == 2) { snprintf(right, rcap, "<color=#f16071>out of sync</color>"); return; }
    if (state == 1) { snprintf(right, rcap, "loading"); return; }
    if (player == 0) { snprintf(right, rcap, "host"); return; }
    (void)me; (void)ping_ms;
}
int ui_conn_level(int ping, int behind, int state) {
    if (state == 1 || state == 2) return 1;
    if (state < 0 && ping < 0) return 0;
    if (ping > 250) return 1;
    if ((ping >= 120 && ping <= 250) || behind >= UI_BEHIND_CATCHUP) return 2;
    return 3;
}
const char *ui_conn_color(int level) { return level >= 3 ? "#71ab40" : level == 2 ? "#ea9f3d" : level == 1 ? "#c82e48" : "#4a4550"; }
void ui_list_header(int n, int open, char *out, size_t cap) { (void)open; snprintf(out, cap, "Players (%d)", n); }   /* open / shut: the stock arrow image (design 2026-10-13) */

/* ---------------------------------------------------------------- lobby */
int ui_parse_address(const char *in, char *host, size_t hcap, uint16_t *port, uint16_t def_port) {
    if (!in || !hcap) return -1;
    while (isspace((unsigned char)*in)) in++;
    size_t n = strlen(in);
    while (n && isspace((unsigned char)in[n - 1])) n--;
    if (!n || n > 255) return -1;
    char buf[256]; memcpy(buf, in, n); buf[n] = 0;
    char *h = buf, *ps = NULL;
    if (*h == '[') {                                         /* [v6]:port */
        char *e = strchr(h, ']');
        if (!e) return -1;
        *e = 0; h++;
        if (e[1] == ':') ps = e + 2; else if (e[1]) return -1;
    } else {
        char *c = strchr(h, ':');
        if (c && strchr(c + 1, ':')) c = NULL;               /* bare v6 without port */
        if (c) { *c = 0; ps = c + 1; }
    }
    if (!*h) return -1;
    for (char *c = h; *c; c++) if (isspace((unsigned char)*c)) return -1;
    long p = def_port;
    if (ps) {
        char *end; p = strtol(ps, &end, 10);
        if (!*ps || *end || p < 1 || p > 65535) return -1;
    }
    if (strlen(h) >= hcap) return -1;
    strcpy(host, h);
    *port = (uint16_t)p;
    return 0;
}

/* ---------------------------------------------------------------- co-op pings (lan_v10) */
uint32_t ui_map_key(int map, int floor) { return (uint32_t)(map < 0 ? 0xFFFF : map) << 16 | (uint32_t)(floor < 0 ? 0xFFFF : floor & 0xFFFF); }
int ui_ping_in_area(const ui_ping *p, int x, int y) {
    int r = p->big ? 2 : 1, dx = x - p->x, dy = y - p->y;
    return dx >= -r && dx <= r && dy >= -r && dy <= r;
}
int ui_ping_count(const ui_pings *s, int owner) {
    int n = 0;
    for (int i = 0; i < UI_PING_MAX; i++) n += s->p[i].used && (owner < 0 || s->p[i].owner == owner);
    return n;
}
int ui_ping_add(ui_pings *s, int owner, int id, uint32_t map_key, int x, int y, double now, const uint32_t *inside, int ninside, int big) {
    int slot = -1, oldest = -1, n = 0, freei = -1;
    for (int i = 0; i < UI_PING_MAX; i++) {
        ui_ping *p = &s->p[i];
        if (!p->used) { if (freei < 0) freei = i; continue; }
        if (p->owner != owner) continue;
        if (p->id == id) slot = i;
        n++;
        if (oldest < 0 || p->t0 < s->p[oldest].t0) oldest = i;
    }
    if (slot < 0) slot = n >= UI_PING_MAX_PER ? oldest : freei;
    if (slot < 0) {                                          /* table full: the oldest ping of anyone */
        for (int i = 0; i < UI_PING_MAX; i++) if (slot < 0 || s->p[i].t0 < s->p[slot].t0) slot = i;
    }
    ui_ping *p = &s->p[slot];
    memset(p, 0, sizeof *p);
    p->used = 1; p->owner = owner; p->id = id; p->big = big != 0; p->map_key = map_key; p->x = x; p->y = y; p->t0 = now;
    for (int k = 0; k < ninside && p->ninside < UI_PING_INSIDE; k++) p->inside[p->ninside++] = inside[k];
    return slot;
}
int ui_ping_del(ui_pings *s, int owner, int id) {
    for (int i = 0; i < UI_PING_MAX; i++) if (s->p[i].used && s->p[i].owner == owner && s->p[i].id == id) { s->p[i].used = 0; return 1; }
    return 0;
}
int ui_ping_clear_other_maps(ui_pings *s, uint32_t key) {
    int n = 0;
    for (int i = 0; i < UI_PING_MAX; i++) if (s->p[i].used && s->p[i].map_key != key) { s->p[i].used = 0; n++; }
    return n;
}
int ui_ping_clear_owner(ui_pings *s, int owner) {
    int n = 0;
    for (int i = 0; i < UI_PING_MAX; i++) if (s->p[i].used && s->p[i].owner == owner) { s->p[i].used = 0; n++; }
    return n;
}
int ui_ping_arrive(ui_pings *s, uint32_t key, uint32_t h, int x, int y, int *cleared, int cap) {
    int n = 0;
    for (int i = 0; i < UI_PING_MAX; i++) {
        ui_ping *p = &s->p[i];
        if (!p->used || p->map_key != key) continue;
        int k = -1;
        for (int j = 0; j < p->ninside && k < 0; j++) if (p->inside[j] == h) k = j;
        int in = ui_ping_in_area(p, x, y);
        if (k >= 0) {                                        /* was there at placement: counts once it has left */
            if (!in) { p->inside[k] = p->inside[--p->ninside]; }
            continue;
        }
        if (!in) continue;
        p->used = 0;
        if (n < cap && cleared) cleared[n] = i;
        n++;
    }
    return n;
}
int ui_ping_near(const ui_pings *s, uint32_t key, int x, int y, int r) {
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < UI_PING_MAX; i++) {
        const ui_ping *p = &s->p[i];
        if (!p->used || p->map_key != key) continue;
        int dx = abs(x - p->x), dy = abs(y - p->y), d = dx > dy ? dx : dy;
        if (d <= r && (d < bd || (d == bd && p->t0 > s->p[best].t0))) { best = i; bd = d; }
    }
    return best;
}
int ui_spam_try(ui_spam *s, double now) {
    if (now < s->lock_until) return 0;
    int in = 0, old = 0;
    for (int i = 0; i < 3; i++) { if (s->t[i] > 0 && now - s->t[i] < 5.0) in++; if (s->t[i] < s->t[old]) old = i; }
    if (in >= 3) { s->lock_until = now + 4.0; return 0; }
    s->t[old] = now;
    return 1;
}
int ui_edge_arrow(float w, float h, float m, float tx, float ty, float *ax, float *ay, float *deg) {
    if (tx >= 0 && tx <= w && ty >= 0 && ty <= h) return 0;
    float cx = w / 2, cy = h / 2, dx = tx - cx, dy = ty - cy;
    float hx = cx - m, hy = cy - m;
    if (hx < 1) hx = 1; if (hy < 1) hy = 1;
    float sx = dx != 0 ? hx / (dx < 0 ? -dx : dx) : 1e30f, sy = dy != 0 ? hy / (dy < 0 ? -dy : dy) : 1e30f;
    float k = sx < sy ? sx : sy;
    *ax = cx + dx * k; *ay = cy + dy * k;
    float a = atan2f(dy, dx) * 57.29578f;
    *deg = a;
    return 1;
}
