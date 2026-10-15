/* dsnet - lockstep network core for the Dungeon Settlers multiplayer mod.
 * Design copied from RimWorld Multiplayer (rwmt): the host-side sequencer stamps every
 * command with its own timer, clients may only run timer steps < tickUntil, and tickUntil
 * is only accepted once every command sent before it has arrived (sentCmds rule).
 * No game dependencies. Thread model: one sequencer thread in the host process, one network
 * thread per client; the game thread calls the client API below. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define DSNET_PROTO        2   /* 2: HELLO carries the gameplay mod set + match rule (core next, 2026-10-15) */
#define DSNET_MAX_PLAYERS  32
#define DSNET_MAX_OWNED    6                    /* largest "units per player" rule (1, 2, 3, 4, 6; default 3) */
#define DSNET_PARTY_MAX    DSNET_MAX_OWNED      /* a player's expedition party: at most the units-per-player rule */
#define DSNET_UNITS_DEFAULT 3
#define DSNET_MAX_UNITS    1024                 /* owned units tracked (bench: no per-player limit) */
#define DSNET_MAX_ITEMS    4096                 /* owner-tagged items (gear mode OWN_STORAGE) */
#define DSNET_MAX_PAYLOAD  60000
#define DSNET_MAX_BLOB     (64u * 1024 * 1024)
#define DSNET_FRAG         1024
#define DSNET_NO_PLAYER    0xFF
#define DSNET_MAX_KNOWN    64                   /* players remembered by a world (stable ids) */
#define DSNET_COLORS        8                   /* player colours (= ui_player_color palette size) */
#define DSNET_OWNER_RESERVED (-2)               /* owner_of: owned by a player who is not here now */
#define DSNET_MAX_HOLDS    8
#define DSNET_SAVE_OVERHEAD 112292 /* MAX; only used table entries are stored, see dsnet_save_overhead */                /* dsnet state put in front of every save */

enum dsnet_cmd_type {
    DSNET_GAME_ORDER   = 1,   /* opaque payload, executed by the game */
    DSNET_SPEED_VOTE   = 2,   /* payload u8 speed (1..); effective speed = lowest vote */
    DSNET_PAUSE_VOTE   = 3,   /* payload u8 1=pause 0=run; paused while ANY player votes pause */
    DSNET_JOIN_POINT   = 4,   /* server-issued; host must call dsnet_upload_save from the callback */
    DSNET_FINGERPRINT  = 5,   /* not delivered to the game; handled inside dsnet */
    DSNET_OWNERSHIP    = 6,   /* payload guid[16] + u8 owner slot (DSNET_NO_PLAYER = release). Claim a free unit or
                                 release your own; the host may assign free and his own units to anyone; another
                                 player's unit or a RESERVED one only in admin mode. No limit (bench).
                                 Owners are stored by player uid, so they survive leave / rejoin / slot change. */
    DSNET_PLAYER_JOIN  = 7,   /* server-issued, player = slot that joined, payload = uid[16] + name (max 31 bytes).
                                 A known uid gets his units back (they were reserved while he was away). */
    DSNET_PLAYER_LEAVE = 8,   /* server-issued, player = who left; their votes reset; his units become RESERVED */
    DSNET_PAUSE_ALL    = 9,   /* server-issued on a late join or a desync: every present player's vote
                                 becomes pause; play resumes by the normal vote (all vote run) */
    DSNET_STORY_PAUSE  = 10,  /* payload u8 1=story popup open, 0=closed; 0 game ticks while any present
                                 player has one open. Allowed in every pause mode. */
    DSNET_EXPEDITION_READY = 11, /* payload u8 ready (0/1), u8 n (0..units per player), n unit guids: the issuer's marked
                                 units. Dropped if n is over the rule, a guid is not the issuer's, or a duplicate. Ready
                                 with n = 0 is allowed (a viewer approves); the start needs at least 1 unit in total.
                                 A marked unit that changes owner leaves the marks and clears ready.
                                 When every present player is ready, dsnet delivers EXPEDITION_START. */
    DSNET_ITEM_OWNER   = 12,  /* payload guid[16] + u8 owner: same rules as DSNET_OWNERSHIP */
    DSNET_EXPEDITION_START = 13, /* made by dsnet on every peer right after the command that made everyone
                                 ready (same tick, same place in the order); player = DSNET_NO_PLAYER;
                                 payload u8 count, then per player: u8 player, u8 n, n guids. Ready flags reset
                                 (marks kept). A leaving player's units become unclaimed. */
    DSNET_ADMIN_MODE   = 14,  /* payload u8 0/1, only from the host (others dropped by the server). On: the host
                                 may order, equip and modify any unit (dsnet_may_equip, re-own other
                                 players' units and items). Default off. */
    DSNET_OWNERS_LOAD  = 15,  /* host only: restore owners saved with the world (see dsnet_import_owners) */
    DSNET_HOLD         = 16,  /* host only, every pause mode: payload u8 on, u8 reason (enum dsnet_hold_reason),
                                 uid[16] (who it is about, zeros if nobody). 0 game ticks while any hold is on.
                                 A PLAYER_LEFT hold ends by itself when that uid rejoins. Max 8 holds. */
    DSNET_TEMP_CONTROL = 17,  /* host only: payload uid[16], u8 on. While that player is away the host may ORDER
                                 his reserved units (no reassigning, no equipping). Ends when he rejoins. */
    DSNET_RULES        = 18,  /* host only (lobby and in play): payload u8 pause_mode, u8 allow_speedup, u8 gear_mode,
                                 u8 units_per_player (1, 2, 3, 4 or 6), [u8 event_choice (enum dsnet_event_choice)],
                                 [u8 population (enum dsnet_population)], [u8 shared_fog (0/1)]; a shorter payload keeps the current ones. The server checks it, applies its own part at once
                                 (pause / speed vote filters, auto pause) and stamps it; every peer applies it at that
                                 step. Votes, holds and owners stay; owners above a lowered unit limit keep their units
                                 but cannot claim more. Part of the session state (saves, late joiners). */
    DSNET_LEND         = 19,  /* payload guid[16] + u8 slot. Lend control of a unit, ownership stays: the owner (the host
                                 for unclaimed units) lends it to another present player (slot), or takes it back
                                 (DSNET_NO_PLAYER); the borrower gives it back (DSNET_NO_PLAYER). While lent only the
                                 borrower (and the host in admin mode) may order it. Ends when the borrower leaves or the
                                 unit changes owner. In the session state, the saves and the owner hash. */
    DSNET_PATCH_POINT  = 20,  /* server-issued (lan_v10 partial resync): payload u32 mask of the player slots that asked.
                                 Host: dsnet_upload_patch from inside the callback (its state at this exact place in the
                                 command order). A guest whose bit is set stops right after this command (the rest of
                                 the step waits) until the patch arrives through dsnet_patch_cb. Others: no effect. */
};

typedef struct dsnet_cmd {      /* envelope {type u8, tick u32, player u8, scope u8, len u16, payload} */
    uint8_t  type;
    uint32_t tick;              /* server timer step the command runs at */
    uint8_t  player;            /* issuer, stamped by the server */
    uint8_t  scope;             /* map / world id, opaque to dsnet */
    uint16_t len;
    const uint8_t *payload;
    uint32_t seq;               /* global order number, stamped by the server */
} dsnet_cmd;

typedef struct dsnet_cfg {
    uint16_t port;              /* 0 = any free port, read back with dsnet_server_port */
    const char *bind_addr;      /* NULL = 0.0.0.0 */
    int min_players;            /* timer starts once this many players are in (default 1) */
    int max_behind;             /* hold the timer if a playing peer is more ticks behind (default 90) */
    int ok_behind;              /* need one peer under this many ticks behind (default 40) */
    float tick_ms;              /* standard time per timer step (default 1000/60) */
    float net_ms;               /* time-control packet interval (default 1000/30) */
    int fp_interval;            /* fingerprint every N timer steps (default 30) */
    int slow_to_frame_time;     /* 1 = RimWorld rule: time per step = slowest peer's frame time (max 4x).
                                   Off by default: peers run several steps per frame instead. */
    int no_auto_pause;          /* 1 = do not pause everyone on a late join / desync (default: pause) */
    /* host lobby settings, fixed before the start and sent to every player in the WELCOME */
    int allow_speedup;          /* 0 = the server drops SPEED_VOTE above 1x */
    int gear_mode;              /* DSNET_GEAR_SHARED_OWN (default) / _SHARED_ANY / _OWN_STORAGE */
    int pause_mode;             /* DSNET_PAUSE_NONE / _ANYONE (default) / _ALL, see below */
    int units_per_player;       /* 1, 2, 3, 4 or 6 (0 = DSNET_UNITS_DEFAULT): owned units per player and party size */
    int event_choice;           /* story event choices: enum dsnet_event_choice (default OWNER) */
    int population;             /* enum dsnet_population (default SHARED) */
    int shared_fog;             /* 1 = vanilla fog (what any unit revealed is shown to all); 0 (default) = each player sees
                                   only what his own units reveal. Display only (mp_fog.c): the sim's fog state stays shared */
    int allow_pause;            /* legacy: 0 forces pause_mode NONE (the server drops PAUSE_VOTE, never auto pauses; joiners and
                                   desynced players catch up while the others keep playing */
    /* join checks: build id + mod version default to dsnet_set_identity; the float rounding mode
     * is read with fegetround() on the thread that calls dsnet_server_start */
    const char *build_id;
    uint32_t mod_version;
} dsnet_cfg;

typedef struct dsnet_server dsnet_server;
typedef struct dsnet_client dsnet_client;

/* Optional log sink (defaults to stderr when DSNET_LOG=1 is set). */
void dsnet_set_log(void (*fn)(const char *line));

/* Game build id (max 31 chars) and mod version sent in every HELLO. Set once before connecting.
 * The client's float rounding mode is read with fegetround() on the thread that connects:
 * call connect from the game thread. */
void dsnet_set_identity(const char *build_id, uint32_t mod_version);
/* Gameplay mod set + match rule sent in every HELLO (from the launcher's session file, DSML_SESSION; mp_config.c
 * mp_session_mods). modset = the launcher's gameplay mod-set hash (64 hex, core/src/join.rs modset_hash), "" = not
 * started by the launcher = no gameplay mods (counts as the hash of no mods). rule: DSNET_MATCH_MODS / _VANILLA.
 * lines = the launcher's modset_lines joined by '\n' (`<id>@<version>:<sorted file sha256s>[:<patch hash>]`, sorted;
 * "" = none; max DSNET_MODLINES_MAX - 1 bytes). The server takes the host's own HELLO as the reference:
 * MODS match = a joiner's set must equal the host's; VANILLA = nobody (host included) may run a gameplay mod.
 * A refusal is DSNET_REJ_MODSET. It carries the words (dsnet_reject_text(DSNET_REJ_MODSET)) AND the host's rule + lines
 * (dsnet_reject_host_mods), so the guest can name missing / extra mods against its own lines (dsnet_my_mod_lines). */
enum { DSNET_MATCH_MODS = 0, DSNET_MATCH_VANILLA = 1 };
#define DSNET_MODLINES_MAX 4096
void dsnet_set_modset(int rule, const char *modset, const char *lines);
const char *dsnet_my_mod_lines(void);
/* after a DSNET_REJ_MODSET refusal (process-wide, the newest one): host rule, host lines ('\n'-joined). 0 = filled, -1 none */
int dsnet_reject_host_mods(int *host_rule, char *lines, size_t n);
#define DSNET_MODSET_NONE "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"   /* sha256("") = no gameplay mod */
/* Stable player id (16 bytes): created once and kept in the app data folder, or derived from the Steam id.
 * Sent in every HELLO from then on; NULL = a random id per connection (tests). Two connections with the
 * same id are refused (DSNET_REJ_DUP_ID). */
void dsnet_set_player_uid(const uint8_t uid[16]);
enum dsnet_reject { DSNET_REJ_PROTO = 1, DSNET_REJ_BUILD, DSNET_REJ_MOD, DSNET_REJ_ROUNDING, DSNET_REJ_FULL, DSNET_REJ_DUP_ID,
                   DSNET_REJ_OTHER_WORLD,  /* local (lan_v10 F17): the host now hosts another world than the one asked for */
                   DSNET_REJ_MODSET        /* the gameplay mods differ from the host's, or the match is vanilla only */ };
/* The world the host runs (campaign id, 16 bytes, + its name), sent by the host's client whenever it changes and given to
 * every joiner in the WELCOME. dsnet_host_world: 0 = known (filled), -1 = the host had no world loaded yet. */
int dsnet_set_world(dsnet_client *c, const uint8_t id[16], const char *name);   /* host only */
int dsnet_host_world(dsnet_client *c, uint8_t id[16], char name[32]);
/* The one wording of a refusal for players (mp_game and the menu both use it), without a "Could not join: " prefix.
 * Unknown reasons give "". */
const char *dsnet_reject_text(int why);

/* ---- host side: sequencer ---- */
/* Pause modes (host lobby). Story pauses (DSNET_STORY_PAUSE) pause in every mode.
 * NONE:   pause votes dropped, no auto pause; joiners / desynced players catch up while others play.
 * ANYONE: paused while any present player votes pause (auto pause on late join / desync).
 * ALL:    paused only while every present player votes pause; any run vote resumes. */
/* Gear modes (host lobby). SHARED_OWN: shared storage, each player equips only his own units (host also
 * the unclaimed ones). SHARED_ANY: shared storage, anyone equips any unit. OWN_STORAGE: like SHARED_OWN,
 * and items tagged with an owner may only be used by that owner (untagged items: anyone). */
enum dsnet_hold_reason { DSNET_HOLD_OTHER = 0, DSNET_HOLD_PLAYER_LEFT = 1, DSNET_HOLD_LOBBY = 2 };   /* LOBBY: host in the lobby, before Start */
enum dsnet_gear_mode { DSNET_GEAR_SHARED_OWN = 0, DSNET_GEAR_SHARED_ANY = 1, DSNET_GEAR_OWN_STORAGE = 2 };
enum dsnet_pause_mode { DSNET_PAUSE_NONE = 0, DSNET_PAUSE_ANYONE = 1, DSNET_PAUSE_ALL_VOTE = 2 };
/* Start from this: allow_speedup = allow_pause = 1, pause_mode = ANYONE. */
dsnet_cfg     dsnet_cfg_defaults(void);
dsnet_server *dsnet_server_start(const dsnet_cfg *cfg);   /* NULL = defaults */
uint16_t      dsnet_server_port(dsnet_server *s);
void          dsnet_server_stop(dsnet_server *s);
typedef struct dsnet_server_stats {
    uint32_t timer, sent_cmds, players, held_steps;
    float time_per_tick_ms;
    int max_behind_seen;
    uint32_t log_base;          /* first command still kept in the server's log (older ones trimmed, see log_trim) */
} dsnet_server_stats;
extern uint32_t dsnet_log_trim_min;   /* trim the server log in chunks of at least this many entries (default 4096; tests) */
void dsnet_server_get_stats(dsnet_server *s, dsnet_server_stats *out);

/* ---- client side ---- */
typedef void (*dsnet_cmd_cb)(const dsnet_cmd *cmd, void *user);
/* Called on the game thread (from dsnet_can_run_tick) when a save arrives after a resync or a
 * late join. The game loads `data` and continues from timer step `tick` (cmds of that step that
 * came after the join point are still to run, then that step's game ticks). */
typedef void (*dsnet_load_cb)(uint32_t tick, const uint8_t *data, size_t len, void *user);

/* Host's own client: same packet path through a local socket pair. */
dsnet_client *dsnet_client_connect_local(dsnet_server *s, const char *name);
/* TCP: never longer than DSNET_CONNECT_TIMEOUT_MS (an address that does not answer used to block ~75 s). If a
 * connector for the same host + port was adopted (below), its socket is used and nothing blocks. */
#define DSNET_CONNECT_TIMEOUT_MS 8000
dsnet_client *dsnet_client_connect_tcp(const char *host, uint16_t port, const char *name);
/* Non-blocking connect for a menu: the name lookup and the TCP connect run on a helper thread, limited to
 * timeout_ms. The game thread calls dsnet_connect_poll each frame; it never waits.
 *   0 still trying, 1 connected, then dsnet_connect_adopt + dsnet_client_connect_tcp(same host, port) uses that socket
 *   (the HELLO is still sent from the game thread, for the rounding check). Errors: enum dsnet_connect_error.
 * dsnet_connect_free cancels (if still trying) and frees; it returns at once (a stuck name lookup ends on its own). */
typedef struct dsnet_connector dsnet_connector;
enum dsnet_connect_error { DSNET_CONN_BAD_ADDRESS = -1, DSNET_CONN_REFUSED = -2, DSNET_CONN_TIMEOUT = -3, DSNET_CONN_UNREACHABLE = -4 };
dsnet_connector *dsnet_connect_start(const char *host, uint16_t port, int timeout_ms);
int              dsnet_connect_poll(dsnet_connector *k);
int              dsnet_connect_adopt(dsnet_connector *k);   /* 0 ok (k still to be freed), -1 not connected */
void             dsnet_connect_free(dsnet_connector *k);
void          dsnet_client_close(dsnet_client *c);
void          dsnet_set_load_cb(dsnet_client *c, dsnet_load_cb cb, void *user);
int           dsnet_wait_welcome(dsnet_client *c, int timeout_ms);   /* 0 ok, -1 timeout/closed, -2 rejected */
int           dsnet_reject_reason(dsnet_client *c);                  /* enum dsnet_reject, 0 = none */

/* UI thread: send a command; the server decides its tick. */
int  dsnet_submit(dsnet_client *c, uint8_t type, uint8_t scope, const void *payload, uint16_t len);
/* May timer step `timer` run now? (timer < tickUntil, not desynced, not loading) */
int  dsnet_can_run_tick(dsnet_client *c, uint32_t timer);
/* Run step `timer`: delivers its commands in server order. Call exactly once per step,
 * before the step's game ticks. Returns the number of commands delivered to cb. */
int  dsnet_take_cmds_for_tick(dsnet_client *c, uint32_t timer, dsnet_cmd_cb cb, void *user);
/* Game ticks to run in this timer step: 0 while paused, else the lowest speed vote. */
int  dsnet_ticks_per_step(dsnet_client *c);
/* Pacing helper (RimWorld TickPatch): timer steps to try this frame. */
int  dsnet_steps_this_frame(dsnet_client *c, float frame_ms);
void dsnet_report_frame_time(dsnet_client *c, float ms);
/* Call after each step with the state hash; every fp_interval steps it is compared with the host's,
 * together with dsnet's own owner-table hash (a split in unit/item owners is a desync too). */
void dsnet_report_fingerprint(dsnet_client *c, uint32_t tick, uint64_t hash);

int      dsnet_player_id(dsnet_client *c);          /* -1 before welcome */
int      dsnet_is_host(dsnet_client *c);
int      dsnet_is_desynced(dsnet_client *c, uint32_t *bad_tick, uint32_t *last_good_tick);
/* What differed at the bad tick: bits DSNET_DESYNC_GAME (the game hash) and DSNET_DESYNC_OWNERS. */
enum { DSNET_DESYNC_GAME = 1, DSNET_DESYNC_OWNERS = 2 };
int      dsnet_desync_reason(dsnet_client *c);
/* Order-independent hashes of the unit / item owner tables (owner by stable uid), and their sizes. */
void     dsnet_owner_hashes(dsnet_client *c, uint64_t *units, uint64_t *items, int *nunits, int *nitems);
uint32_t dsnet_tick_until(dsnet_client *c);
uint32_t dsnet_timer(dsnet_client *c);               /* next step this client will run */
int      dsnet_owner_of(dsnet_client *c, const uint8_t guid[16]);   /* slot, -1 free, DSNET_OWNER_RESERVED */
/* Owner details for "Reserved for <name>": returns like dsnet_owner_of; uid / name (32 bytes) filled when owned. */
int      dsnet_owner_info(dsnet_client *c, const uint8_t guid[16], uint8_t uid[16], char name[32]);
/* palette index of the player in this slot (picked at his first join in the world, kept with the world), -1 if none */
int      dsnet_player_color(dsnet_client *c, int player);
int      dsnet_player_uid(dsnet_client *c, int player, uint8_t uid[16]);   /* 0 ok, -1 absent */
/* Holds: dsnet_hold sends DSNET_HOLD (host only). dsnet_hold_get(i) for i < count: reason, uid, name of
 * that player (for "Waiting for <name>"); returns -1 past the end. */
int      dsnet_hold(dsnet_client *c, int on, int reason, const uint8_t uid[16]);
int      dsnet_hold_count(dsnet_client *c);
int      dsnet_hold_get(dsnet_client *c, int i, int *reason, uint8_t uid[16], char name[32]);
/* Temporary control ("Keep playing" after a disconnect): host only, sends DSNET_TEMP_CONTROL. */
int      dsnet_set_temp_control(dsnet_client *c, const uint8_t uid[16], int on);
int      dsnet_temp_control(dsnet_client *c, const uint8_t uid[16]);   /* 1 if the host has it now */
/* May `player` order `unit`: own unit; unclaimed -> host; reserved -> host in admin mode or with temporary
 * control of that owner; anything -> host in admin mode. */
int      dsnet_may_order(dsnet_client *c, int player, const uint8_t unit[16]);
/* Persistence: export the owner tables (players by uid + name, units, items) to store with the world save;
 * the host imports them after loading the world (sent as DSNET_OWNERS_LOAD commands, so every peer applies
 * them in order). Returns the size written (or needed when buf is NULL / too small). */
size_t   dsnet_export_owners(dsnet_client *c, uint8_t *buf, size_t cap);
int      dsnet_import_owners(dsnet_client *c, const uint8_t *data, size_t len);   /* host only; 0 ok */
/* Recruit rule: call from inside the take_cmds callback of the order that created the unit; the
 * unit becomes the order issuer's. Same call on every peer -> same table. -1 outside a callback. */
int      dsnet_set_recruit_owner(dsnet_client *c, const uint8_t guid[16]);
/* Expedition: mark up to 3 of your units, ready or not (rules at DSNET_EXPEDITION_READY). */
int      dsnet_expedition_ready(dsnet_client *c, int ready, const uint8_t (*guids)[16], int n);
uint32_t dsnet_ready_mask(dsnet_client *c);            /* players marked ready */
int      dsnet_marked_of(dsnet_client *c, int player, uint8_t out[3][16]);       /* n, -1 not present */
int      dsnet_owned_list(dsnet_client *c, int player, uint8_t (*out)[16], int max);   /* n owned (copies <= max) */
/* 0 if no player has more than 3 units among guids (unowned units count for the host), else -1. */
int      dsnet_party_ok(dsnet_client *c, const uint8_t (*guids)[16], int n);
/* Gear */
int      dsnet_gear_mode(dsnet_client *c);
/* Host rules (DSNET_RULES): the session's current values, from the WELCOME and then from executed RULES commands. */
/* Story event choices (owner 2026-10-13; the ui lane runs the popup): who picks when a story event opens. */
enum dsnet_event_choice { DSNET_EVENT_OWNER = 0, DSNET_EVENT_HOST = 1, DSNET_EVENT_FIRST = 2, DSNET_EVENT_MAJORITY = 3 };
/* Population (owner 2026-10-13, PLAN.md "POPULATION rule"): how the clan's unit cap (from reputation, the game's number)
 * is shared. SHARED: one cap for the clan, first come (stock). PER_PLAYER: every player may have the full cap of his
 * own units. SPLIT: the cap divided between connected players, the remainder +1 to the first ones (lowest slots).
 * MULTIPLIED: cap x connected players, one pool for all. Overflow is allowed: nobody is removed after a rule change or a
 * leave; only no new hire above the cap. Check inside the replayed recruit order, so every peer decides the same. */
enum dsnet_population { DSNET_POP_SHARED = 0, DSNET_POP_PER_PLAYER = 1, DSNET_POP_SPLIT = 2, DSNET_POP_MULTIPLIED = 3 };
typedef struct dsnet_rules { int pause_mode, allow_speedup, gear_mode, units_per_player, event_choice, population, shared_fog; } dsnet_rules;
/* The cap for one player: mode, the clan's base cap, connected players, this player's rank among them (0 = lowest slot).
 * *pool = 1: compare with the clan's total; 0: with the player's own units. Pure, deterministic. */
int dsnet_population_cap(int mode, int base_cap, int nplayers, int rank, int *pool);
/* Same for a session (the current rule, present players): may `player` hire one more unit? clan_count = the clan's units
 * now (the game's number), own_count = his own (dsnet_owned_count). 1 yes, 0 no. *cap / *pool: what it compared. */
int dsnet_may_recruit(dsnet_client *c, int player, int base_cap, int clan_count, int own_count, int *cap, int *pool);
/* Both rules together (lead 2026-10-13: AND): 0 may hire; DSNET_RECRUIT_POP_FULL no room under the Population cap;
 * DSNET_RECRUIT_UNITS_FULL the player already has units_per_player units. dsnet_may_recruit = (this == 0). */
enum { DSNET_RECRUIT_OK = 0, DSNET_RECRUIT_POP_FULL = 1, DSNET_RECRUIT_UNITS_FULL = 2 };
int dsnet_recruit_check(dsnet_client *c, int player, int base_cap, int clan_count, int own_count, int *cap, int *pool);
void     dsnet_rules_get(dsnet_client *c, dsnet_rules *out);
int      dsnet_set_rules(dsnet_client *c, const dsnet_rules *r);       /* host only; -1 bad values (nothing sent) */
int      dsnet_rules_valid(const dsnet_rules *r);
int      dsnet_units_per_player(dsnet_client *c);
int      dsnet_owned_count(dsnet_client *c, int player);              /* units owned by that player now */
/* Claims refused because of the unit limit, per issuer (a counter: the UI compares it to say why nothing happened) */
uint32_t dsnet_claim_refusals(dsnet_client *c, int player);
/* Lend control (DSNET_LEND). Ownership (dsnet_owner_of) stays the owner while lent.
 * dsnet_lend: owner (host for free units) lends to a present player; to = the owner or DSNET_NO_PLAYER takes it back.
 * dsnet_give_back: the borrower returns it. dsnet_controller_of: who orders it now = the borrower while lent, else the
 * same value as dsnet_owner_of (slot, -1 free, DSNET_OWNER_RESERVED). dsnet_lent_to: the borrower, or -1 when not lent. */
int      dsnet_lend(dsnet_client *c, const uint8_t unit[16], int to_slot);
int      dsnet_give_back(dsnet_client *c, const uint8_t unit[16]);
int      dsnet_controller_of(dsnet_client *c, const uint8_t unit[16]);
int      dsnet_lent_to(dsnet_client *c, const uint8_t unit[16]);
int      dsnet_lent_count(dsnet_client *c);
/* Lend events for messages on every peer (same order everywhere): seq counts them; last = the newest one.
 * why: DSNET_LEND_LENT 0, _TAKEN_BACK 1, _GIVEN_BACK 2, _RETURNED 3 (the borrower left). */
enum { DSNET_LEND_LENT = 0, DSNET_LEND_TAKEN_BACK = 1, DSNET_LEND_GIVEN_BACK = 2, DSNET_LEND_RETURNED = 3 };
uint32_t dsnet_lend_seq(dsnet_client *c);
int      dsnet_lend_last(dsnet_client *c, uint8_t unit[16], int *owner, int *from, int *to, int *why);   /* 0 ok, -1 none yet */
/* Chat (owner 2026-10-13): messages travel outside the simulation (never stamped, never in saves or hashes, cannot
 * desync), relayed by the server to every connected player; system lines (joined / left / reconnected) are made locally
 * from the session's join / leave commands. dsnet_chat_poll returns the next one (1) or 0; a ring of 128 per client. */
#define DSNET_CHAT_MAX 200
enum dsnet_chat_kind { DSNET_CHAT_TEXT = 0, DSNET_CHAT_JOINED = 1, DSNET_CHAT_LEFT = 2, DSNET_CHAT_RECONNECTED = 3 };
typedef struct dsnet_chat_msg { int player, kind; char name[32]; char text[DSNET_CHAT_MAX + 1]; } dsnet_chat_msg;
int dsnet_chat_send(dsnet_client *c, const char *text);           /* 0 sent, -1 empty / not connected (text cut at 200 bytes) */
int dsnet_chat_poll(dsnet_client *c, dsnet_chat_msg *out);
/* "<Name> is typing..." (owner 2026-10-14): call dsnet_chat_typing(c, 1) on each key press while typing (sent at most once a
 * second) and dsnet_chat_typing(c, 0) when the line is sent or cancelled. dsnet_chat_typing_of: 1 while that player typed in
 * the last DSNET_TYPING_MS (cleared by their stop or their chat line). Outside the sim, like the chat. */
#define DSNET_TYPING_MS 4000
int dsnet_chat_typing(dsnet_client *c, int on);
int dsnet_chat_typing_of(dsnet_client *c, int player);
/* Co-op markers (lan_v10): friends' views and pings, outside the sim like the chat. */
#define DSNET_HAS_MARKS 1
int dsnet_view_send(dsnet_client *c, uint32_t map_key, float cx, float cy, float half_w, float half_h); /* 0 sent */
/* last view received from that player: 0 = known (age_ms = ms since it arrived), -1 none. Cleared when he leaves. */
int dsnet_view_of(dsnet_client *c, int player, uint32_t *map_key, float out[4], double *age_ms);
enum { DSNET_MARK_ADD = 1, DSNET_MARK_DEL = 2 };
typedef struct dsnet_mark_ev { int from, op, owner, id; uint32_t map_key; int x, y; } dsnet_mark_ev;
int dsnet_mark_send(dsnet_client *c, int op, int owner, int id, uint32_t map_key, int x, int y); /* 0 sent */
int dsnet_mark_poll(dsnet_client *c, dsnet_mark_ev *out);   /* 1 = one event (ring of 64), 0 none; own sends NOT echoed */
#define DSNET_MARK_SPAM_N  4                    /* server: at most 4 adds per sender in any 5 s (deletes never dropped) */
#define DSNET_MARK_SPAM_MS 5000
/* marks of a player, up to max (dsnet_marked_of copies at most 3, for older callers) */
int      dsnet_marked_list(dsnet_client *c, int player, uint8_t (*out)[16], int max);
int      dsnet_admin_mode(dsnet_client *c);              /* host admin mode, from executed commands */
int      dsnet_may_equip(dsnet_client *c, int player, const uint8_t unit[16]);
int      dsnet_item_owner(dsnet_client *c, const uint8_t item[16]);   /* player or -1 */
/* Inside a take_cmds callback: applies at once (deterministic, e.g. crafted/looted item -> issuer).
 * Outside: sends an ITEM_OWNER command (rules above). owner DSNET_NO_PLAYER = untag. */
int      dsnet_item_set_owner(dsnet_client *c, const uint8_t item[16], int owner);
int      dsnet_may_use_item(dsnet_client *c, int player, const uint8_t item[16]);
int      dsnet_needs_state(dsnet_client *c);         /* waiting for a save (late join / resync) */
int      dsnet_is_disconnected(dsnet_client *c);     /* the connection to the server is gone */
int      dsnet_host_left(dsnet_client *c);           /* 1 if the server said BYE (the host ended the session) */
void     dsnet_test_cut(dsnet_client *c);            /* test: drop our own link with no BYE (a lost connection) */
/* Silence limit for both sides (default 5000 ms, env DSNET_RX_TIMEOUT_MS, 0 = off). Heartbeats come from the
 * network threads, so a game thread frozen by a map load or a big save does not count as silence. */
void     dsnet_set_rx_timeout_ms(int ms);
size_t   dsnet_save_overhead(dsnet_client *c);       /* bytes dsnet would put in front of a save now */
/* Player list (from the server, ~5 Hz). state: enum dsnet_net_state. 0 ok, -1 absent. */
enum dsnet_net_state { DSNET_NET_ABSENT = 0, DSNET_NET_IN_SYNC, DSNET_NET_RESYNCING, DSNET_NET_LOADING };
int      dsnet_player_net(dsnet_client *c, int player, int *ping_ms, int *ticks_behind, int *state);
/* Host lobby settings from the WELCOME. With allow_pause 0 the game hook should also stop local
 * popups (letters, dialogs) from pausing the game. */
int      dsnet_allow_speedup(dsnet_client *c);
/* Lobby / vote UI. All follow executed commands, so they match on every peer. */
uint32_t    dsnet_present_mask(dsnet_client *c);                 /* bit per player in the game */
/* Players others are waiting for: loading a save, catching up, desynced, or busy in a barrier
 * (dsnet_set_busy). From the server, about 30 times a second; for "Waiting for <name>" only. */
uint32_t    dsnet_busy_mask(dsnet_client *c);
/* Barrier (transitions_design.md 3.7): set while a local flow holds this peer mid-step. The server
 * stops its timer while any playing peer is busy, so everyone stops within a few steps; it
 * restarts when all are done. Holding the remaining ticks of the step is the game hook's job. */
void        dsnet_set_busy(dsnet_client *c, int busy);
uint32_t    dsnet_story_pause_mask(dsnet_client *c);             /* bit per player with a story popup open */
int         dsnet_vote_of(dsnet_client *c, int player, int *speed, int *pause);   /* 0 ok, -1 not present */
const char *dsnet_player_name(dsnet_client *c, int player);      /* NULL if not present; copy it, the
                                                                    buffer changes on the next join/leave */
int      dsnet_allow_pause(dsnet_client *c);                  /* pause mode != NONE */
int      dsnet_pause_mode(dsnet_client *c);
/* Who caused the current vote pause: the first pause voter (in command order) among the votes that
 * hold it; for an auto pause, the joining / desynced player. -1 if not paused by votes (story only). */
int      dsnet_pause_by(dsnet_client *c);

/* ---- lan_v10: section hashes, partial resync, desync report ----
 * Section hashes ride with the host's fingerprint: dsnet_report_fingerprint_ex sends (game hash, sec[3]); a guest keeps
 * both sides' values, so at a desync it knows which sections differ (sec: units, positions, rng+tick; owners = dsnet). */
void     dsnet_report_fingerprint_ex(dsnet_client *c, uint32_t tick, uint64_t hash, const uint64_t sec[3]);
/* + a deeper hash (0 = not computed at this step). Compared like the fingerprint but NOT a desync by itself: counted in
 * dsnet_deep_status (checks, fails, newest equal / differing tick); the caller decides (dsnet_force_desync). */
void     dsnet_report_fingerprint_deep(dsnet_client *c, uint32_t tick, uint64_t hash, const uint64_t sec[3], uint64_t deep);
void     dsnet_deep_status(dsnet_client *c, uint32_t *checks, uint32_t *fails, uint32_t *last_ok, uint32_t *last_bad);
/* Guest: flag a desync found by the game itself (a deep hash differs while the fingerprint is equal): like a fingerprint
 * mismatch; the report info gets both sides' hashes at that tick. -1 if already desynced / host / loading. */
int      dsnet_force_desync(dsnet_client *c, uint32_t tick, int partial);   /* partial 1: a partial resync may follow */
uint32_t dsnet_fp_interval(dsnet_client *c);
typedef struct dsnet_desync_info {
    uint32_t bad_tick, last_good;
    uint64_t mine[5], host[5];      /* [0] game hash, [1..3] sections (0 if the host sent none), [4] owner tables */
    uint64_t deep_mine, deep_host;  /* the deep hashes at bad_tick when the game flagged it (dsnet_force_desync), else 0 */
} dsnet_desync_info;
int      dsnet_desync_get(dsnet_client *c, dsnet_desync_info *out);   /* 0 = desynced (filled), -1 = in sync */
/* Partial resync: called on the game thread (from dsnet_can_run_tick) when the host's patch for our PATCH_POINT arrived.
 * dsnet has already put the host's own state (owners, votes, rules) in place. data = the host's game snapshot. Return 0
 * when the game applied it AND its state now equals the host's (fingerprint proof), else -1: dsnet stays desynced and the
 * caller asks for a full resync (dsnet_request_resync). */
typedef int (*dsnet_patch_cb)(uint32_t tick, const uint8_t *data, size_t len, void *user);
void     dsnet_set_patch_cb(dsnet_client *c, dsnet_patch_cb cb, void *user);
/* Guest, desynced: ask for a partial resync instead of the full save (sends P_PATCH_REQ). The guest keeps running its
 * steps while desynced until its PATCH_POINT. -1 if not desynced or already asked. */
int      dsnet_request_patch(dsnet_client *c);
/* On: this client's P_DESYNC tells the server a partial resync comes first (no auto pause until a full resync is asked). */
void     dsnet_set_partial_resync(dsnet_client *c, int on);
int      dsnet_patch_state(dsnet_client *c);   /* 0 none, 1 asked (running to the patch point), 2 stopped there, waiting */
void     dsnet_patch_abort(dsnet_client *c);   /* give up (timeout): stays desynced, a full resync is next */
uint32_t dsnet_patch_count(dsnet_client *c);   /* partial resyncs applied with fingerprint proof (this session) */
/* Host only, from inside the DSNET_PATCH_POINT callback: the game snapshot (dsnet adds its own state in front). */
int      dsnet_upload_patch(dsnet_client *c, const uint8_t *data, size_t len);
/* Desync report: a guest sends its part to the host outside the sim (<= 8 MB); the host takes each arrived part on its
 * game thread (1 = got one, *data malloc'd, *from = the sender's slot). */
int      dsnet_send_report(dsnet_client *c, const uint8_t *data, size_t len);
/* Full-state check (lan_v10): the host sends its section list for a step outside the sim; guests keep the newest one.
 * dsnet_full_take: 1 = a new one arrived since the last take (*data malloc'd, *tick its step). */
int      dsnet_send_full(dsnet_client *c, uint32_t tick, const uint8_t *data, size_t len);
int      dsnet_full_take(dsnet_client *c, uint32_t *tick, uint8_t **data, size_t *len);
int      dsnet_report_take(dsnet_client *c, uint8_t **data, size_t *len, int *from);

/* Resync by save transfer: ask the server for a join point; the host uploads its save; this
 * client gets it through the load callback, then replays the logged commands. */
int dsnet_request_resync(dsnet_client *c);
/* Host only, from inside the DSNET_JOIN_POINT callback. len <= 64 MB - DSNET_SAVE_OVERHEAD, sent in 1 KB fragments. */
int dsnet_upload_save(dsnet_client *c, const uint8_t *data, size_t len);
