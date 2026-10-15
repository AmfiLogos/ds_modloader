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
#define TM_MAX_MARK    3
#define TM_FREE        (-1)
#define TM_RESERVED    (-2)        /* = DSNET_OWNER_RESERVED: owned by a player who is away (kept for him) */
#define TM_READY_TYPE  11          /* DSNET_EXPEDITION_READY: payload u8 ready, u8 n, n x guid[16] */
#define TM_READY_MAX_PAYLOAD (2 + 16 * TM_MAX_MARK)

typedef struct tm_state {
    int me;                                    /* local player id */
    int in_dungeon;                            /* 1 = screen is read-only */
    int admin;                                 /* host lobby "Admin mode": only then may the host move other players' units */
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
int tm_layout(const tm_state *s, tm_column *cols, int max_cols);   /* returns the column count */

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
