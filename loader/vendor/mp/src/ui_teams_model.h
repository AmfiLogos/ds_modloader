/* ui_teams_model - pure logic of the Teams screen (no game, no dsnet calls). Tested by ui_test/teams_test.c.
 * Game side: ui_teams.c. Notes: TEAMS_NOTES.md.
 *
 * Owner rules (2026-10-12): a player owns any number of units in the base (bench); before an expedition he
 * marks 1..3 of them and presses Ready; the expedition may start when every present player who owns a unit
 * is ready (the same rule as dsnet's EXPEDITION_START). Click a free unit = take it, click your own = free it, right-click your own = mark / unmark for
 * the run. The host moves any unit: click it (picked up), then click a column header. Locked in a dungeon.
 * Players are dsnet ids 0..31, host = 0, owner -1 = free. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define TM_MAX_PLAYERS 32
#define TM_MAX_UNITS   128
#define TM_MAX_MARK    6           /* capacity; the limit is the host rule (tm_mark_limit) */
#define TM_FREE        (-1)
#define TM_RESERVED    (-2)        /* = DSNET_OWNER_RESERVED: owned by a player who is away (kept for him) */
#define TM_READY_TYPE  11          /* DSNET_EXPEDITION_READY: payload u8 ready, u8 n, n x guid[16] */
#define TM_READY_MAX_PAYLOAD (2 + 16 * TM_MAX_MARK)

typedef struct tm_state {
    int me;                                    /* local player id */
    int in_dungeon;                            /* 1 = screen is read-only */
    int admin;                                 /* host lobby "Admin mode": only then may the host move other players' units */
    int max_mark;                              /* host rule "units per player" (dsnet_units_per_player: 1..6); 0 = 3 */
    uint32_t present;                          /* bit per player in the game */
    uint32_t ready;                            /* bit per player with ready = 1 */
    int nmarked[TM_MAX_PLAYERS];
    uint8_t marked[TM_MAX_PLAYERS][TM_MAX_MARK][16];
    int nunits;
    uint8_t guid[TM_MAX_UNITS][16];            /* alive player-faction units, stock order */
    int owner[TM_MAX_UNITS];                   /* TM_FREE or player */
} tm_state;

/* ---- layout: one column per present player (id order), then players who left but still own units,
 *      then the free column (player TM_FREE, always last). */
typedef struct tm_column {
    int player;                                /* TM_FREE = free units */
    int n;
    int unit[TM_MAX_UNITS];                    /* indexes into tm_state.guid */
} tm_column;
int tm_layout(const tm_state *s, tm_column *cols, int max_cols);
/* units a player may mark for the run: the host rule, capped by TM_MAX_MARK */
static inline int tm_mark_limit(const tm_state *s) { int m = s->max_mark > 0 ? s->max_mark : 3; return m > TM_MAX_MARK ? TM_MAX_MARK : m; }   /* returns the column count */

int tm_is_marked(const tm_state *s, int player, const uint8_t g[16]);

/* ---- clicks */
typedef enum {
    TM_NONE = 0,
    TM_CLAIM,          /* mp_claim(unit) */
    TM_RELEASE,        /* mp_release(unit) */
    TM_ASSIGN,         /* host: mp_assign(unit, target); target TM_FREE = release */
    TM_PICK,           /* host: unit picked up, waiting for a column click */
    TM_UNPICK,         /* host: clicked the picked unit again */
    TM_TARGET,         /* host: target column set (target = player, TM_NO_TARGET = cleared) */
    TM_MARKS,          /* send the new marks (ready reset to 0): tm_action.marks / nmarks */
    TM_READY,          /* send ready 1 with the current marks */
    TM_UNREADY,        /* send ready 0 with the current marks */
    TM_DENY_LOCKED,    /* in a dungeon */
    TM_DENY_OWNED,     /* another player's unit (target = owner) */
    TM_DENY_FULL,      /* already 3 marked */
    TM_DENY_NO_MARKS,  /* Ready with 0 marked units */
    TM_DENY_NOT_OWN,   /* right-click on a unit that is not yours */
} tm_kind;
typedef struct tm_action {
    tm_kind kind;
    int unit;                                  /* index, -1 none */
    int target;                                /* player for ASSIGN / DENY_OWNED */
    int nmarks;
    uint8_t marks[TM_MAX_MARK][16];
} tm_action;

/* Host's click state (owner test 2026-10-13: a plain left-click on a free unit must TAKE it, for the host too).
 * picked = another player's unit the host picked up (-1 none), target = column the host chose by clicking its
 * header (TM_NO_TARGET none): while set, every unit the host left-clicks moves to that column. */
#define TM_NO_TARGET (-2)
typedef struct tm_sel { int picked, target; } tm_sel;
#define TM_SEL_INIT { -1, TM_NO_TARGET }
/* button 0 = left, 1 = right.
 * Everyone: left on a free unit = take, on your own = free, right on your own = mark / unmark.
 * Host: click a column header to set a target column; then left on a FREE (or his own) unit moves it there.
 * Admin mode on only: also other players' / reserved units (left = pick it up, then a header; or via the target). */
tm_action tm_click_unit(const tm_state *s, int unit, int button, tm_sel *sel);
/* Column header click (host): with a picked unit -> ASSIGN it there; else toggle the target column. */
tm_action tm_click_header(const tm_state *s, int column_player, tm_sel *sel);
tm_action tm_click_ready(const tm_state *s);

/* ---- expedition gate */
/* 1 when every present player is ready (dsnet's EXPEDITION_START rule). */
int tm_all_ready(const tm_state *s);
/* players still to get ready (present, not ready) */
uint32_t tm_waiting_mask(const tm_state *s);
/* union of the marked units of the ready present players, in player order; returns n */
int tm_party(const tm_state *s, uint8_t (*out)[16], int max);

/* ---- wire (DSNET_EXPEDITION_READY payload) */
size_t tm_ready_write(uint8_t *out, size_t cap, int ready, const uint8_t (*marks)[16], int n);
int tm_ready_read(const uint8_t *p, size_t len, int *ready, uint8_t (*marks)[16], int *n);   /* 0 ok */
/* What dsnet must accept (same rule both sides): n <= 3, every mark owned by the issuer, no duplicates. */
int tm_ready_valid(const tm_state *s, int issuer, const uint8_t (*marks)[16], int n);

/* header line for a column: "The owner (host) - ready" etc. owner colour from ui_player_color. */
void tm_header_text(const tm_state *s, int column_player, const char *name, char *out, size_t cap);

/* ---- region vote (owner 2026-10-13): each player picks a region on the Teams screen; the expedition uses
 *      the most-picked region, the host breaks a tie (his pick if it is in the tie, else the lowest tied
 *      region). Picks of absent players do not count. pick < 0 = no pick. Returns -1 if nobody picked. */
#define TM_ORDER_REGION 20         /* mp order type, zero guid (open to everyone): args i32 region */
int tm_region_vote(const int pick[TM_MAX_PLAYERS], uint32_t present);
/* next / previous region in the available list (wraps); cur not in the list -> first / last */
int tm_region_step(const int *avail, int n, int cur, int dir);

/* ---- stock "Go to dungeon" panel party (owner 2026-10-13): each player's party is HIS units only.
 *      From the panel's party list (in order): your units go, free units are taken (claimed) for you,
 *      other players' units are left to them; at most TM_MAX_MARK go, the rest is reported.
 *      If the panel gives none of yours, your existing marks are kept. */
typedef struct tm_panel_res {
    int nmarks; uint8_t marks[TM_MAX_MARK][16];       /* the new marks (ready with these) */
    int nclaim; uint8_t claim[TM_MAX_MARK][16];       /* free units to claim first (subset of marks) */
    int over;                                         /* yours / free units beyond the 3 */
    int foreign;                                      /* other players' units in the panel (not taken) */
    int kept_old;                                     /* 1 = panel had none of yours: old marks kept */
} tm_panel_res;
void tm_panel_party(const tm_state *s, const uint8_t (*panel)[16], int n, tm_panel_res *out);

/* ---- expedition proposal (owner 2026-10-13, replaces the region vote). A player presses Go (Teams or the stock
 *      "Go to dungeon"): mp order TM_ORDER_PROPOSE {i32 region} reaches every peer in command order, and that
 *      player is Ready with his marked units. Everyone sees the proposal (region, each player's marked units,
 *      who stays). Approve = Ready (dsnet EXPEDITION_READY with your marks); when every present player is ready
 *      dsnet starts the expedition (EXPEDITION_START, existing path). Decline (anyone), Cancel (the proposer) and
 *      the host's 60 s timeout go as TM_ORDER_ANSWER {i32 kind, u32 mask}: the proposal ends on every peer and
 *      each peer that was ready sends ready 0. One proposal at a time. */
#define TM_ORDER_PROPOSE 21
#define TM_ORDER_ANSWER  22
#define TM_PARTY_CAP     4         /* the game's PlayerPartyMax: units on one expedition, all players together */
#define TM_PROPOSAL_SECONDS 60
enum { TM_ANS_DECLINE = 0, TM_ANS_CANCEL = 2, TM_ANS_TIMEOUT = 3 };
typedef struct tm_prop { int active, proposer, region; } tm_prop;
typedef enum { TM_PE_NONE = 0, TM_PE_STARTED, TM_PE_UPDATED, TM_PE_DECLINED, TM_PE_CANCELLED, TM_PE_TIMEOUT, TM_PE_PROPOSER_LEFT } tm_pevent;
/* a PROPOSE order from player: starts a proposal (none active), updates the region (same proposer), else ignored */
tm_pevent tm_prop_propose(tm_prop *p, int player, int region, uint32_t present);
/* an ANSWER order: DECLINE from a present player other than the proposer, CANCEL from the proposer,
 *      TIMEOUT from the host (player 0) only; anything else is ignored */
tm_pevent tm_prop_answer(tm_prop *p, int player, int kind, uint32_t present);
/* the proposer left the game: the proposal ends */
tm_pevent tm_prop_check(tm_prop *p, uint32_t present);
/* present players who have not approved yet (not ready); 0 when no proposal */
uint32_t tm_prop_waiting(const tm_prop *p, const tm_state *s);
/* units that would go: every present player's marked units (owned by him), all players together */
int tm_party_members(const tm_state *s);

/* ---- the proposal's party (owner lan_v7 feedback): the proposer picks units in the stock step 1, his and his
 *      allies'. Own units go; free units are taken (claimed) for the proposer; units of present allies go for
 *      their owners (each owner approves and packs his own); reserved / absent / unknown units are skipped.
 *      err: 0 ok, 1 more than TM_PARTY_CAP units, 2 a player over tm_mark_limit (err_player), 3 empty. */
#define TM_PARTY_MAXN 8
typedef struct tm_prop_res {
    int n; uint8_t party[TM_PARTY_MAXN][16];
    int nclaim; uint8_t claim[TM_PARTY_MAXN][16];
    int skipped, err, err_player;
} tm_prop_res;
void tm_proposal_party(const tm_state *s, const uint8_t (*panel)[16], int np, tm_prop_res *r);
/* the units of player in a party, owned by him now (a claimed free unit counts for the proposer) */
int tm_party_of(const tm_state *s, const uint8_t (*party)[16], int n, int player, int proposer, uint8_t (*out)[16]);
