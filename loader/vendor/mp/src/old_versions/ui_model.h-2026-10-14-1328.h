/* ui_model - pure logic of the multiplayer interface (no game, no dsnet calls).
 * Used by ui_game.c in the dylib and tested by ui_test/. Notes: ~/ds_multiplayer/src/UI_NOTES.md.
 *
 * Players are dsnet ids 0..31; the host is always player 0. Unit owner -1 = unclaimed. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define UI_MAX_PLAYERS 32          /* = DSNET_MAX_PLAYERS */
#define UI_MAX_OWNED   3           /* default units per player */
extern int ui_max_owned;           /* the host rule "Units per player" (1, 2, 3, 4 or 6; dsnet_units_per_player), default 3 */
#define UI_UNCLAIMED   (-1)
#define UI_NO_UNIT     (-2)        /* popup with no unit involved */
#define UI_MAX_CHOICES 16

/* ---------------------------------------------------------------- players */
typedef struct ui_players {
    uint32_t present;                          /* bit per player */
    char name[UI_MAX_PLAYERS][32];             /* "" = unknown -> "Player N" */
} ui_players;
const char *ui_player_label(const ui_players *p, int player, char out[40]);   /* name or "Player N" / "Host" */
const char *ui_player_color(int player);       /* "#RRGGBB" */
extern int (*ui_color_resolver)(int player);   /* slot -> palette index (the world's per-uid colour), -1 unknown */
/* " <color=#RRGGBB>[Name]</color>" for a unit tag (shortcut bar, world name); "" if unclaimed */
void ui_owner_tag(const ui_players *p, int owner, char *out, size_t cap);

/* ---------------------------------------------------------------- unit claims */
/* Same rule as dsnet's OWNERSHIP command: issuer 0 (host) may set any owner; others may only
 * claim an unclaimed unit for themselves or release their own; max UI_MAX_OWNED per player. */
int ui_ownership_valid(int issuer, int cur_owner, int new_owner, const int owned_count[UI_MAX_PLAYERS]);

typedef enum {
    UI_ACT_NONE = 0,
    UI_ACT_CLAIM,          /* mp_claim */
    UI_ACT_RELEASE,        /* mp_release */
    UI_ACT_ASSIGN,         /* host: mp_assign(target); target -1 = release */
    UI_ACT_DENY_OWNED,     /* belongs to another player (target = owner) */
    UI_ACT_DENY_FULL,      /* this player already has 3 units */
} ui_act_kind;
typedef struct { ui_act_kind kind; int target; } ui_act;
/* Right-click on a unit portrait. Player: unclaimed -> claim, own -> release, other's -> deny.
 * Host: cycles the owner: unclaimed -> host -> next present player -> ... -> unclaimed,
 * skipping players that already own 3. */
ui_act ui_portrait_action(int me, int cur_owner, const int owned_count[UI_MAX_PLAYERS], uint32_t present);
/* Same with the host admin rule (owner 2026-10-13): admin off = the host may give FREE or its own units to any
 * player but may not take another player's unit (cur >= 1) or a reserved one (-2, owner away). */
#define UI_RESERVED (-2)   /* dsnet_owner_of: owned by a player who is away (DSNET_OWNER_RESERVED) */
ui_act ui_portrait_action_admin(int me, int cur_owner, const int owned_count[UI_MAX_PLAYERS], uint32_t present, int admin);

/* May this player order a unit with this owner? (mp_core rule: owner only; unclaimed = host only) */
int ui_may_order(int me, int owner);

/* ---------------------------------------------------------------- speed / pause votes */
typedef struct ui_votes {
    uint32_t present;
    uint8_t speed[UI_MAX_PLAYERS];             /* GameSpeed 1..3 */
    uint8_t pause[UI_MAX_PLAYERS];
    int allow_speedup, allow_pause;
    uint32_t story_pause;                      /* players holding a story popup open (pauses even with pause off) */
    uint32_t busy;                             /* players still loading a map change (transitions barrier) */
    int pause_by1;                             /* dsnet_pause_by + 1 (0 = nobody known) */
    int paused_ext;                            /* the session is paused (mp stats) even if no vote shows it (auto pause) */
} ui_votes;
int ui_effective_speed(const ui_votes *v);     /* lowest present vote, 1 if none; 1 if speed-up off */
int ui_is_paused(const ui_votes *v);           /* a present story popup, or any present pause vote (votes ignored if pause off) */
/* Comma list of players voting pause ("The owner, B"); returns their count. */
int ui_pause_voters(const ui_votes *v, const ui_players *p, char *out, size_t cap);
/* The one pause text (review #20: the panel and the big Pause banner agree): story event (and voters), else
 * "Paused by <voters>", else "Paused by <pause_by>", else "Paused". */
void ui_pause_text(const ui_votes *v, const ui_players *p, char *out, size_t cap);
/* Speed is Normal or Fast (3.5x; owner 2026-10-13: never write 2x / 3x). */
const char *ui_speed_text(int game_speed);       /* "Normal" / "Fast" */

/* ---------------------------------------------------------------- status line */
typedef enum {
    UI_NET_OFF = 0,        /* no session */
    UI_NET_CONNECTING,     /* waiting for the host's welcome */
    UI_NET_WAITING,        /* connected, waiting for the host's save */
    UI_NET_PLAYING,
    UI_NET_DESYNC,         /* flagged, frozen, asking for the save */
    UI_NET_LOADING,        /* loading the host's save */
    UI_NET_LOST,           /* connection closed */
} ui_net_state;
/* One line under the clock: resync state first, then who paused (ui_pause_text), then a speed request:
 * others see "Ben wants Fast: press >> to agree", the asker sees "You want Fast: waiting for the owner".
 * me = this player. "" when nothing to say. */
void ui_status_line(ui_net_state st, const ui_votes *v, const ui_players *p, int me, char *out, size_t cap);

/* ---------------------------------------------------------------- popups (owner rules 2026-10-12) */
typedef enum { UI_POP_OTHER = 0, UI_POP_LEVELUP, UI_POP_TRADE, UI_POP_STORY, UI_POP_RESULT } ui_pop_kind;
typedef enum {
    UI_ROUTE_STOCK = 0,          /* not ours: stock behaviour */
    UI_ROUTE_SKIP,               /* do not open on this peer */
    UI_ROUTE_OPEN_NO_PAUSE,      /* open here, never pause (time lock off) */
    UI_ROUTE_STORY_CHOOSER,      /* story: this player chooses */
    UI_ROUTE_STORY_READONLY,     /* story: someone else chooses ("X is choosing") */
    UI_ROUTE_STORY_VOTE,         /* story, no unit: every player votes, host breaks a tie */
    UI_ROUTE_RESULT_CHOOSER,     /* return-to-base screen: this player picks (pauses everyone) */
    UI_ROUTE_RESULT_READONLY,    /* return-to-base screen: someone else picks ("X is choosing") */
} ui_route;
/* PopUpType -> kind (DungeonResult 3 = return-to-base screen, TradeUI 11, LevelUp 21, EventNodeUI 22) */
ui_pop_kind ui_pop_kind_of(int popup_type);
/* unit_owner: owner of the unit involved, UI_UNCLAIMED, or UI_NO_UNIT. */
ui_route ui_popup_route(ui_pop_kind kind, int me, int unit_owner);
/* Return-to-base screen (owner 2026-10-12): the owner of the unit that used the portal picks; unclaimed
 * unit, no unit, or the forced-return button: the host. */
int ui_result_chooser(int unit_owner, int forced);
/* Who chooses a story node: owner, host for an unclaimed unit, -1 = vote (no unit). */
int ui_story_chooser(int unit_owner);
/* Host rule "Event choices" (owner 2026-10-13, lan_v8): who decides a story choice.
 * OWNER: the unit's owner (unclaimed unit or no unit: the host); HOST: the host; FIRST: the first answer counts;
 * MAJORITY: every present player votes, most votes wins, the host breaks a tie; after the timeout the votes in so
 * far decide (never a vote waiting on someone who cannot answer). Returns a player, UI_STORY_VOTE (-1) or UI_STORY_FIRST. */
enum { UI_EVC_OWNER = 0, UI_EVC_HOST = 1, UI_EVC_FIRST = 2, UI_EVC_MAJORITY = 3 };
#define UI_STORY_VOTE  (-1)
#define UI_STORY_FIRST (-3)
#define UI_STORY_TIMEOUT_S 60
int ui_story_chooser_rule(int rule, int unit_owner);


/* Story vote tally. Fed only by commands (same order on every peer), so it is deterministic.
 * chooser: a player, UI_STORY_VOTE (majority) or UI_STORY_FIRST (the first answer decides). */
typedef struct ui_story {
    uint32_t node;                             /* hash of the node's choice keys; 0 = none */
    int chooser;                               /* player, or -1 = vote */
    int8_t vote[UI_MAX_PLAYERS];               /* -1 = not voted */
    int decided;                               /* chosen index, -1 = open */
} ui_story;
void ui_story_reset(ui_story *s, uint32_t node, int chooser);
/* A STORY_VOTE command from `player`. A vote for another node starts a new tally with `chooser`
 * (ui_story_chooser of the node's unit; the node may not be on screen yet on this peer).
 * Chooser mode: only the chooser's vote counts and decides. Vote mode: a player may change
 * their vote until every present player voted; then most votes wins, a tie goes to the host's
 * choice if it is tied, else to the lowest index. Returns the decided index or -1. */
int ui_story_vote(ui_story *s, int player, uint32_t node, int choice, uint32_t present, int chooser);
/* Re-check after a player left (present changed). Returns the decided index or -1. */
int ui_story_recheck(ui_story *s, uint32_t present);
/* Majority timeout: decide with the votes in (host breaks a tie). -1 if nobody voted yet. */
int ui_story_close(ui_story *s, uint32_t present);
int ui_story_votes_in(const ui_story *s, uint32_t present);   /* how many present players voted */
uint32_t ui_story_node_hash(const char *const *keys, int n);  /* FNV-1a over the choice keys; never 0 */

/* STORY_VOTE wire (mp order type UI_ORDER_STORY_VOTE, guid zero): u32 node LE, u8 choice */
#define UI_ORDER_STORY_VOTE 3
#define UI_STORY_ARGS 5
void ui_story_args_write(uint8_t out[UI_STORY_ARGS], uint32_t node, int choice);
int  ui_story_args_read(const uint8_t *a, size_t n, uint32_t *node, int *choice);   /* 0 ok */

/* ---------------------------------------------------------------- map-change vote */
/* A player proposes a map change; the others accept or decline; the proposer counts as accept.
 * Decided once every present player voted: more accepts = go, more declines = stay, a tie goes to
 * the host's vote. One proposal at a time: a new one while another is open is ignored.
 * Fed only by commands (same order on every peer). */
enum { UI_MAP_ENTER_DUNGEON = 1, UI_MAP_RETURN_BASE = 2, UI_MAP_NEXT_FLOOR = 3 };
#define UI_MAP_MAX_ORDER 1024
typedef struct ui_mapvote {
    uint32_t id;                               /* 0 = none */
    int proposer, kind;
    int8_t vote[UI_MAX_PLAYERS];               /* 1 accept, 0 decline, -1 not yet */
    int decided;                               /* -1 open, 0 declined, 1 accepted */
    int cancelled;                             /* decided 0 by a cancel (60 s with no answer) */
    uint16_t order_len;
    uint8_t order[UI_MAP_MAX_ORDER];           /* the transitions lane's order bytes, sent by the host on accept */
} ui_mapvote;
void ui_mapvote_clear(ui_mapvote *m);
/* Returns 1 if this proposal is now the open one (it may already be decided: alone in the game). */
int  ui_mapvote_propose(ui_mapvote *m, uint32_t id, int proposer, int kind, const uint8_t *order, size_t len, uint32_t present);
/* accept: 1 yes, 0 no, UI_MAP_CANCEL = the proposer (or the host) withdraws it (timeout). Decided or -1. */
#define UI_MAP_CANCEL 2          /* timeout (60 s with no decision) */
#define UI_MAP_CANCEL_USER 3     /* the proposer pressed Cancel */
#define UI_MAP_TIMEOUT_S 60
int  ui_mapvote_vote(ui_mapvote *m, int player, uint32_t id, int accept, uint32_t present);
int  ui_mapvote_recheck(ui_mapvote *m, uint32_t present);
int  ui_mapvote_open(const ui_mapvote *m);     /* a proposal waits for votes */
const char *ui_map_kind_text(int kind);        /* "go down to the next floor", "return to the settlement" ... */
/* Same with the floor number when known (floor >= 1): "go down to floor 3". */
const char *ui_map_kind_text_floor(int kind, int next_floor, char *out, size_t cap);
/* Proposer's line while its vote is open: "Waiting for Ben, Cy (1 of 3 agree)". 0 = nothing (not open / not mine). */
int  ui_mapvote_wait_line(const ui_mapvote *m, const ui_players *p, int me, uint32_t present, char *out, size_t cap);
/* layout v2 §5b texts. Question: "Go down to floor 3?" / "Return to the settlement?" / "Enter the dungeon?".
 * Short verb for "The owner wants to go down." Marks: "The owner <ok>   Ben ..." (ok = a check glyph the font has, or "yes").
 * Result toast on every peer: "Everyone agreed: going down", "Ben said no: staying on floor 2",
 * "No answer in 60 s: the vote was cancelled", "The owner cancelled the vote". */
const char *ui_map_question(int kind, int next_floor, char *out, size_t cap);
const char *ui_map_short(int kind);
void ui_mapvote_marks(const ui_mapvote *m, const ui_players *p, uint32_t present, const char *ok, char *out, size_t cap);
void ui_mapvote_result(const ui_mapvote *m, const ui_players *p, uint32_t present, int cur_floor_no, char *out, size_t cap);
/* wire: MAP_PROPOSE (mp order type UI_ORDER_MAP_PROPOSE, guid zero): u32 id, u8 kind, u16 len, order[len]
 *       MAP_VOTE    (UI_ORDER_MAP_VOTE): u32 id, u8 accept */
#define UI_ORDER_MAP_PROPOSE 4
#define UI_ORDER_MAP_VOTE    5
size_t ui_map_propose_write(uint8_t *out, size_t cap, uint32_t id, int kind, const uint8_t *order, size_t len);  /* 0 = too big */
int    ui_map_propose_read(const uint8_t *a, size_t n, uint32_t *id, int *kind, const uint8_t **order, size_t *len);
void   ui_map_vote_write(uint8_t out[5], uint32_t id, int accept);
int    ui_map_vote_read(const uint8_t *a, size_t n, uint32_t *id, int *accept);

/* ---------------------------------------------------------------- demo-v2 view helpers (owner 2026-10-12) */
/* Local unit order: this player's units first (in the game's order), then the others (game order).
 * owner[i] = owner of the game's i-th player unit. out[k] = game index shown at place k. Returns n. */
int  ui_local_order(const int *owner, int n, int me, int *out);
/* Resync toast (review #10), every peer: "Ben's game went out of sync: reloading everyone from the owner's save"
 * ("Your game ..." on the player who went out of sync). */
void ui_resync_text(const ui_players *p, int who, int me, char *out, size_t cap);
/* World name tag (review #26): owner colour dot + name, no brackets, no size change: " <color=#..>\u25CF Ben</color>".
 * dot = the glyph the world font has ("\u25CF", "\u2022" ...), NULL / "" = none. */
void ui_owner_tag_world(const ui_players *p, int owner, const char *dot, char *out, size_t cap);
/* Line under the big Pause panel: "Paused by the owner" / "Paused for a story event" / "Paused". by = dsnet_pause_by. */
void ui_pause_line(const ui_players *p, int by, int story, char *out, size_t cap);
/* One player-list row: coloured dot + name (+ " (you)"), ping, sync state.
 * ping_ms < 0 = unknown; state: 0 in sync, 1 catching up / loading, 2 desynced, -1 unknown. */
void ui_player_row(const ui_players *p, int player, int me, int ping_ms, int state, char *out, size_t cap);

/* ---------------------------------------------------------------- live-test settings (owner 2026-10-13) */
enum { UI_NAMES_OFF = 0, UI_NAMES_OTHERS = 1, UI_NAMES_ALL = 2 };
typedef struct ui_settings {
    int see_through;      /* % for other players' units in the world: 0 (default), 25, 50, 75, 100 */
    int names;            /* player names above units: UI_NAMES_* (default OTHERS) */
    int list_open;        /* player list expanded (default 0) */
    int pos_set;          /* the player dragged the panel (lan_v8): pos_x / pos_y are used, else the default place */
    int pos_x, pos_y;     /* panel top-left, canvas units from the HUD canvas's top centre */
    int tooltips;         /* "Show tooltips on hover" (default on) */
    int compact;          /* lan_v9 "Panel size": 0 Normal (default), 1 Compact (75%) */
} ui_settings;
void ui_settings_default(ui_settings *s);
void ui_settings_parse(const char *json, ui_settings *s);       /* missing / bad keys keep their value */
int  ui_settings_format(const ui_settings *s, char *out, size_t cap);
int  ui_see_through_next(int v);                                /* 0 -> 25 -> 50 -> 75 -> 100 -> 0 */
const char *ui_names_text(int mode);                            /* "Others" / "All" / "Off" */
int  ui_name_tag_visible(int mode, int owner, int me);          /* owner tag above a unit? */
/* Multi-unit select (select all, box): keep[i] = 0 for player units this player may not order, when at least
 * one item is a unit this player controls. is_player[i]: item i is a player unit; owner[i]: its owner.
 * Single items and lists with none of my units are left alone (selecting to look is allowed). Returns kept count. */
int  ui_filter_selection(const int *is_player, const int *owner, int n, int me, int admin, int *keep);
/* host admin mode (owner 2026-10-13): the host (player 0) may order any unit while it is on */
int  ui_may_order_admin(int me, int owner, int admin);
/* player list: one compact row ("The owner" for me; "B  42 ms" / "B  out of sync" for others) and the header */
void ui_player_row2(const ui_players *p, int player, int me, int ping_ms, int state, char *out, size_t cap);
void ui_list_header(int nplayers, int open, char *out, size_t cap);
/* Connection indicator (layout v2 §1): 3 = in sync and ping < 120 ms; 2 = ping 120-250 or catching up (40+ ticks
 * behind); 1 = ping > 250, resyncing (state 2) or loading (state 1); 0 = disconnected / unknown (shown as an X). */
#define UI_BEHIND_CATCHUP 40
int  ui_conn_level(int ping_ms, int ticks_behind, int state);
const char *ui_conn_color(int level);            /* "#71ab40" / "#ea9f3d" / "#c82e48"; unlit bars "#4a4550" */
/* player row in two columns (layout v2 §3): left "<owner-colour dot> Name"; status (muted): "host", "loading",
 * "out of sync" (#f16071), "catching up", or "". The connection bars are drawn as images next to it. */
void ui_player_cols(const ui_players *p, int player, int me, int ping_ms, int state, const char *dot,
                    char *left, size_t lcap, char *right, size_t rcap);   /* "Players (2)"; the open state is the stock arrow */
/* signal strength from ping (owner 2026-10-13): <60 ms 4, <120 3, <200 2, else 1; 0 = red X (desynced / absent / unknown) */
int  ui_signal_bars(int ping_ms, int state);
/* row: "<n>  <name>  <bars>": player number (1-based), name in the player's colour, 4 bar glyphs (lit green / dim grey) or a red X */
void ui_player_row3(const ui_players *p, int player, int me, int ping_ms, int state, char *out, size_t cap);

/* ---------------------------------------------------------------- lobby */
/* "1.2.3.4:7777", "[::1]:7777", "host.name" (port = def_port). Trims spaces. 0 ok, -1 bad. */
int ui_parse_address(const char *in, char *host, size_t host_cap, uint16_t *port, uint16_t def_port);
