// mpmenu.h - the in-game Multiplayer menu (title button, host flow, lobby panel, guest "Joining" panel,
// Esc-menu entries). Pure state machine in mpmenu_model.c (tested in mpmenu_test/), game side in mpmenu.c.
#pragma once
#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------- model (no game code) */
enum { MPM_PAUSE_NONE = 0, MPM_PAUSE_ANYONE = 1, MPM_PAUSE_ALL_VOTE = 2 };          /* = enum dsnet_pause_mode */
enum { MPM_GEAR_SHARED_OWN = 0, MPM_GEAR_SHARED_ANY = 1, MPM_GEAR_OWN_STORAGE = 2 };
typedef struct mpm_settings { int pause_mode, speedup, gear, units, admin, events, population, fog;
                              int spectators, watching; } mpm_settings;   /* fog: shared fog of war 0/1 (default 0); spectators: host allows them (1 default); watching: how many watch now (game side, display only) */
#define MPM_DEFAULT_PORT 47777   /* direct TCP port the host listens on (beside Steam) */

enum mpm_state {
    MPM_IDLE = 0,          /* title, no multiplayer */
    MPM_TITLE_MENU,        /* title: Multiplayer sub-menu (Host / Back) */
    MPM_TITLE_HOST,        /* title: host settings (pause, speed-up, gear) + Load a save / New campaign / Back */
    MPM_PICK_SAVE,         /* stock load list open with host intent */
    MPM_PICK_NEW,          /* stock new-campaign screen open with host intent */
    MPM_HOST_LOADING,      /* campaign loading; session starts when it is in */
    MPM_LOBBY,             /* in game, paused: player list, Invite, Start */
    MPM_PLAYING,           /* in game, multiplayer running */
    MPM_GUEST_JOINING,     /* invite accepted: connecting / waiting for the host's save */
    MPM_GUEST_LOADING,     /* the host's save is loading */
    MPM_FAILED,            /* last attempt failed: message shown on the title */
    MPM_TITLE_JOIN,        /* title: Join by address (text field ip:port) + Connect / Back; Cancel while connecting */
    MPM_GUEST_LOBBY,       /* guest in game while the host is still in the lobby (game held): waiting panel */
    MPM_TITLE_FRIENDS,     /* title: Steam friends hosting a world, one Join button each (Steam version only) */
};
/* buttons (title clones, lobby clones, Esc-menu clones) */
enum mpm_button {
    MPB_NONE = 0,
    MPB_MULTIPLAYER, MPB_HOST, MPB_BACK,                                   /* title */
    MPB_PAUSE_MODE, MPB_SPEEDUP, MPB_GEAR, MPB_LOAD_SAVE, MPB_NEW_CAMPAIGN, /* title host settings */
    MPB_INVITE, MPB_START,                                                  /* lobby (+ MPB_LEAVE) */
    MPB_ESC_INVITE, MPB_LEAVE,                                              /* Esc menu */
    MPB_CANCEL_JOIN,                                                        /* guest joining panel */
    MPB_JOIN_ADDR, MPB_CONNECT,                                             /* title: direct address join */
    MPB_COPY_ADDR,                                                          /* lobby: copy the direct address */
    MPB_LABEL_ADDR, MPB_LABEL_NAME, MPB_INFO,                               /* title: text-only lines (no button frame) */
    /* layout v2 (research/ux_layout_v2.md): title page header, settings row + summary or 3 selector rows, divider */
    MPB_HEADER, MPB_SETTINGS, MPB_SUMMARY, MPB_SEL_PAUSE, MPB_SEL_SPEED, MPB_SEL_GEAR, MPB_DIVIDER,
    MPB_PAUSE_PREV, MPB_PAUSE_NEXT, MPB_SPEED_PREV, MPB_SPEED_NEXT, MPB_GEAR_PREV, MPB_GEAR_NEXT,   /* selector arrows */
    MPB_LOBBY_SETTINGS, MPB_ADMIN,                                          /* lobby window: gear / toggle, admin mode */
    MPB_SEL_UNITS, MPB_UNITS_PREV, MPB_UNITS_NEXT,                          /* rule "Units per player" (host page + lobby) */
    MPB_ADMIN_PREV, MPB_ADMIN_NEXT, MPB_RULES_DONE,                         /* Rules panel (approved mockups): admin row, Done */
    MPB_EVENTS_PREV, MPB_EVENTS_NEXT,                                       /* rule "Event choices" (lan_v8) */
    MPB_JOIN_FRIEND, MPB_FRIEND0, MPB_FRIEND1, MPB_FRIEND2, MPB_FRIEND3, MPB_FRIEND4, MPB_FRIEND5,   /* Steam friends hosting */
    MPB_HIST0, MPB_HIST1, MPB_HIST2, MPB_HIST3, MPB_HIST4, MPB_HIST5,       /* Recent worlds rows (Multiplayer page) */
    MPB_HISTX0, MPB_HISTX1, MPB_HISTX2, MPB_HISTX3, MPB_HISTX4, MPB_HISTX5, /* their remove buttons */
    MPB_HIST_PREV, MPB_HIST_NEXT,                                           /* newer / older page */
    MPB_POP_PREV, MPB_POP_NEXT,                                             /* rule "Population" (lan_v8) */
    MPB_LEAVE_CONFIRM, MPB_LEAVE_CANCEL,                                    /* host Leave confirm box: End for everyone / Cancel (lan_v10) */
    MPB_FOG_PREV, MPB_FOG_NEXT,                                             /* rule "Shared fog of war" (core next 2026-10-15) */
    /* spectators (owner 2026-10-14). Ids >= 64 have no bit in mpm_visible: ask mpm_is_visible */
    MPB_WATCH,                                                              /* Join page: connect as a spectator */
    MPB_HISTW0, MPB_HISTW1, MPB_HISTW2, MPB_HISTW3, MPB_HISTW4, MPB_HISTW5, /* Recent worlds rows: Watch */
    MPB_SPECT_PREV, MPB_SPECT_NEXT,                                         /* rule "Spectators" */
    MPB_COUNT
};
/* what the game side sees each frame */
typedef struct mpm_obs {
    int scene;              /* 1 title, 2 campaign, else other/loading */
    int campaign_ready;     /* scene 2, not busy, campaign ticks > 0 */
    int title_visible;      /* title menu view active (the load / new-campaign screens closed) */
    int picker_open;        /* stock load list (popup 5) or new-campaign screen open */
    int loading;            /* a campaign / scene load is running (rig busy()) */
    int role;               /* session: 0 none, 1 host, 2 guest (also while a Steam join connects) */
    int guest_loading;      /* guest: waiting for or loading the host's save */
    int failed;             /* session start / join failed */
    int esc_open;           /* Esc menu (SystemMenuView) open */
    int host_lobby;         /* guest: the host is still in the lobby (dsnet LOBBY hold on) */
    int steam;              /* Steam is running (the MP app); the LAN app has none */
    int escape;             /* title: Escape pressed this frame (= Back) */
    int edited;             /* title: the address or name field text changed since the last frame */
    int spectator;          /* this copy's session (or the join that runs) watches */
    char host_name[64];     /* guest: the host's name ("" unknown) */
    char msg[160];          /* why the session ended / failed (mp_steam_state), "" none */
} mpm_obs;
/* actions for the game side (bit mask) */
enum {
    MPA_OPEN_LOAD_LIST = 1 << 0,   /* TitleMenuView.OnLoadCampaignPressed */
    MPA_OPEN_NEW       = 1 << 1,   /* TitleMenuView.OnNewGamePressed */
    MPA_HOST_START     = 1 << 2,   /* start the host session with the settings (Steam lobby) */
    MPA_INVITE         = 1 << 3,   /* Steam overlay invite */
    MPA_LEAVE          = 1 << 4,   /* close the session (host: everyone leaves) */
    MPA_LOBBY_SHOW     = 1 << 5,   /* open the lobby panel (pause vote on) */
    MPA_LOBBY_HIDE     = 1 << 6,   /* close it (pause vote off) */
    MPA_REFRESH        = 1 << 7,   /* labels changed */
    MPA_JOIN_ADDR      = 1 << 8,   /* read the address field, connect over TCP (need_save) */
    MPA_COPY_ADDR      = 1 << 9,   /* direct address -> clipboard */
    MPA_JOIN_CANCEL    = 1 << 10,  /* stop the address connect that runs (m->connecting) */
    MPA_START_GAME     = 1 << 11,  /* host pressed Start: the lobby hold ends, the world runs */
    MPA_ADMIN          = 1 << 12,  /* host: flip admin mode (DSNET_ADMIN_MODE) */
    MPA_RULES          = 1 << 13,  /* host, lobby: send m->set as the session rules (DSNET_RULES) and remember them */
    MPA_FRIENDS        = 1 << 14,  /* refresh the list of Steam friends hosting (m->friends) */
    MPA_JOIN_FRIEND    = 1 << 15,  /* join m->friend_pick's Steam lobby */
    MPA_HIST_JOIN      = 1 << 16,  /* reconnect to recent world m->hist_pick (index in the whole list) */
    MPA_HIST_DEL       = 1 << 17,  /* remove recent world m->hist_pick */
    MPA_CONFIRM_LEAVE  = 1 << 18,  /* host pressed Leave multiplayer: show the "End the game for everyone?" box (lan_v10, ux_flow F7) */
    MPA_LEAVE_BOX_CLOSE = 1 << 19, /* close that box (Escape, or the session ended under it) */
    MPA_SPECTATORS     = 1 << 20,  /* host, lobby: flip "Spectators" (off with spectators watching: confirm box first) */
};
typedef struct mpm_model {
    int state;
    mpm_settings set;
    char msg[128];          /* last error / status for the title */
    char connecting[96];    /* join page: "ip:port" being connected (set by the game side), "" none */
    char notice[320];       /* session ended (host left...): text for the stock notice box on the title, "" none */
    char host[64];          /* guest: host name for the Joining line */
    char direct[96];        /* host: "192.168.1.20:47777" (LAN) for the lobby, set by the game side */
    char internet[96];      /* host: public ip:port as Steam sees it, "" unknown */
    int no_field;           /* no text field could be cloned: Join by address reads the clipboard */
    int picker_seen;        /* the load list / new-campaign screen did open (to tell a cancel) */
    int settings_open;      /* host page: the three selector rows show (else the one-line summary) */
    int lobby_settings_open;/* lobby window (host): settings rows in place of the invite rows */
    int steam;              /* last obs.steam (labels) */
    int nfriends, friend_pick;                       /* Steam friends hosting (set by the game side) */
    int nhist, hist_page, hist_pick;                 /* Recent worlds: count (game side), page of 6, picked index */
    char friends[6][48];
    int closed_frames;      /* frames since it closed with no load started */
    int confirm_leave;      /* host: the "End the game for everyone?" box is up */
    int watch;              /* the join asked from the title (Connect / Watch, a Recent worlds row) is a spectator join */
    int spect;              /* last obs.spectator (labels) */
} mpm_model;
void mpm_init(mpm_model *m);
/* one frame: `click` = enum mpm_button or MPB_NONE. Returns MPA_* actions to run now. */
int  mpm_step(mpm_model *m, const mpm_obs *o, int click);
/* which buttons are shown now (bit per enum mpm_button) */
uint64_t mpm_visible(const mpm_model *m, const mpm_obs *o);   /* bit per enum mpm_button below 64 */
int      mpm_is_visible(const mpm_model *m, const mpm_obs *o, int button);   /* any button (ids >= 64 too) */
/* label of a button (settings show their value) */
void mpm_label(const mpm_model *m, int button, char *out, size_t cap);
/* lobby body: next step, "\xC2\xB7 the owner (host)" per player, settings line, addresses (host) */
void mpm_lobby_text(const mpm_model *m, const char *const *names, int n, char *out, size_t cap);
void mpm_lobby_title(const mpm_model *m, char *out, size_t cap);   /* "Multiplayer lobby" / "In the owner's lobby" */
/* "Pause: anyone · Speed-up: on · Gear: own units · Units: 3 · Admin: off" (host page Rules box) */
void mpm_settings_summary(const mpm_settings *s, char *out, size_t cap);
/* Rules panel rows (0 Pause, 1 Speed-up, 2 Gear, 3 Units per player, 4 Admin mode): label and long value */
#define MPM_RULE_ROWS 9                                  /* row 7 "Shared fog", row 8 "Spectators" */
#define MPM_RULE_POP 4                                   /* the Population row: its description line sits under it */
const char *mpm_population_desc(int mode);
const char *mpm_rule_tip(int row);                         /* hover tooltip of a Rules row (owner lan_v8) */
const char *mpm_rule_tip_title(int row);                   /* its title (game tooltip format) */
const char *mpm_rule_label(int row);
void mpm_rule_value(const mpm_settings *s, int row, char *out, size_t cap);
/* value shown between a selector's arrows (MPB_SEL_PAUSE / _SPEED / _GEAR) */
const char *mpm_sel_value(const mpm_model *m, int sel);
const char *mpm_pause_text(int mode);   /* short: "anyone" / "by vote" / "off" */
const char *mpm_gear_text(int gear);    /* short: "own units" / "any unit" / "own storage" (storage shared in the first two) */

/* ---------------------------------------------------------------- game side (dylib only) */
#ifndef MPM_NO_GAME
void  mpmenu_install(void);                        /* add-image callback, after ui_install() */
void  mpmenu_frame(void);                          /* once per MainFlow.Update, title screen too */
char *mpmenu_exec(const char *cmd, const char *line);   /* "mpm_*" socket commands (tests); NULL = not ours */
void  mpmenu_title_init(void *title_menu_view);   /* optional: rebuild the title clones on the next frame */
int   mpmenu_title_click(void *button);            /* 1 = our clone (the click hook calls it) */
int   mpmenu_gear_mode(void);                      /* lobby gear option for the ord / ui lanes; -1 no session */
int   mpmenu_lobby_open(void);                     /* ui lane: 1 while the lobby (host) or waiting panel (guest) is up */
/* ui lane (dlsym): host rule "Spectators" in play. off_ask: spectators watching -> the confirm box (1), none -> off now (0).
 * set: on at once; off = off_ask. Both save the rule (mp_menu.json, the world's line). */
int   mpmenu_spectators_off_ask(void);
int   mpmenu_spectators_set(int allow);
#endif
