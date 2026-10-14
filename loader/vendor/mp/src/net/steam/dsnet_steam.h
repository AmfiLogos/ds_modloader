/* dsnet Steam transport: ISteamNetworkingSockets P2P (NAT traversal + Valve relay) and a
 * friends-only lobby, loaded at runtime from the game's own libsteam_api (Steamworks SDK 1.62,
 * flat C API). Nothing is linked against Steam; every entry point is resolved with dlsym.
 *
 * Two ways to use it:
 *  1. Bridge (works with the current dsnet core, no core change):
 *     host:   s = dsnet_server_start(cfg with bind_addr "127.0.0.1");
 *             dsnet_steam_host_start(dsnet_server_port(s), build, mod, 8);
 *             each incoming Steam peer is bridged to 127.0.0.1:port, so the server sees a TCP peer.
 *     joiner: j = dsnet_steam_join_lobby(lobby) or dsnet_steam_join_host(steamid);
 *             poll dsnet_steam_join_poll(j, &port) until 1, then
 *             dsnet_client_connect_tcp("127.0.0.1", port, name).
 *  2. Native transport: dsnet_steam_transport(hconn) returns a dsnet_transport for one
 *     connection. Using it without the bridge needs two small core entry points that take a
 *     dsnet_transport instead of an fd (server: add a remote peer; client: connect over a
 *     transport); see the end of dsnet_steam.c.
 *
 * Channels: the transport cuts the link's byte stream at frame borders. P_TIMECTL, P_KEEPALIVE
 * and P_FRAMETIME frames go unreliable (rwmt: time control is Unreliable); every other frame goes
 * reliable ordered. Both kinds carry whole frames, so the link's framing stays intact.
 * DSNET_STEAM_ALL_RELIABLE=1 in the environment sends everything reliable.
 *
 * Steam must already be initialised by the game (SteamAPI_Init). The game uses Steamworks.NET
 * manual dispatch, which takes every callback on the game's pipe, so async lobby calls here run on
 * a private Steam pipe and connection events use the SteamNetworkingSockets function-pointer
 * callback, pumped by this module's own thread. */
#pragma once
#include "../dsnet_internal.h"

#define DSNET_STEAM_VPORT 0          /* P2P virtual port of the dsnet listen socket */

/* Find libsteam_api in the process (already loaded by the game); `path` = NULL looks through the
 * loaded images, else dlopen(path). 0 ok, -1 not found / symbol missing (dsnet_steam_error()). */
int         dsnet_steam_load(const char *path);
const char *dsnet_steam_error(void);
int         dsnet_steam_is_running(void);        /* Steam client process up (no init needed) */
/* Standalone programs only (the game does this itself): SteamAPI_InitFlat with this app id +
 * manual dispatch. 0 ok, -1 failed (dsnet_steam_error()). */
int         dsnet_steam_init_standalone(uint32_t appid);
void        dsnet_steam_shutdown_standalone(void);
int         dsnet_steam_logged_on(void);
uint64_t    dsnet_steam_my_id(void);
const char *dsnet_steam_my_name(void);              /* Steam persona name, "" if unknown */
int         dsnet_steam_initialized(void);          /* SteamAPI_Init done in this process (by the game or us) */
/* Relay network status: 100 = current, 1..99 = in progress, <= 0 failed/unknown. */
int         dsnet_steam_relay_status(void);

/* ---- native transport over one SNS connection (takes ownership: close() closes it) ---- */
dsnet_transport dsnet_steam_transport(uint32_t hconn);

/* ---- host ---- */
typedef struct dsnet_steam_host dsnet_steam_host;
/* Opens the P2P listen socket, creates the friends-only lobby (data: dsmp_proto, dsmp_build,
 * dsmp_mod, dsmp_host) and bridges every accepted peer to 127.0.0.1:server_port. */
dsnet_steam_host *dsnet_steam_host_start(uint16_t server_port, const char *build_id,
                                         uint32_t mod_version, int max_members);
uint64_t dsnet_steam_host_lobby(dsnet_steam_host *h);   /* 0 while pending, ~0 if creation failed */
uint32_t dsnet_steam_host_listen_socket(dsnet_steam_host *h);
int      dsnet_steam_host_peers(dsnet_steam_host *h);   /* bridged peers alive */
int      dsnet_steam_host_invite(dsnet_steam_host *h);  /* Steam overlay invite dialog (needs the overlay) */
/* Invite without the overlay: Steam sends the friend a lobby invite in Steam chat. 0 ok. */
int      dsnet_steam_host_invite_friend(dsnet_steam_host *h, uint64_t friend_id);
void     dsnet_steam_host_stop(dsnet_steam_host *h);    /* leaves the lobby, closes peers + socket */

/* ---- joiner ---- */
typedef struct dsnet_steam_join dsnet_steam_join;
dsnet_steam_join *dsnet_steam_join_lobby(uint64_t lobby, const char *build_id, uint32_t mod_version);
dsnet_steam_join *dsnet_steam_join_host(uint64_t host_steamid);   /* no lobby, connect directly */
/* 1 ready (connect dsnet to 127.0.0.1:*port), 0 pending, -1 failed (dsnet_steam_join_error). */
int         dsnet_steam_join_poll(dsnet_steam_join *j, uint16_t *port);
const char *dsnet_steam_join_error(dsnet_steam_join *j);
void        dsnet_steam_join_close(dsnet_steam_join *j);

/* Lobby to join from "+connect_lobby <id>" in argv or the Steam launch command line, or from an
 * accepted invite seen on this module's pipe while running (GameLobbyJoinRequested_t). 0 = none.
 * Each request is returned once. */
uint64_t dsnet_steam_take_join_request(void);

/* Friends now in this game with a lobby (friend list "Join game"). Returns count written. */
typedef struct dsnet_steam_friend { uint64_t steamid, lobby; char name[64]; } dsnet_steam_friend;
int dsnet_steam_friends_in_game(dsnet_steam_friend *out, int max);
/* All friends (for an invite list without the overlay); lobby != 0 if they are in one. */
int dsnet_steam_friends(dsnet_steam_friend *out, int max);
/* Feed a callback the game's own dispatcher pulled from its pipe (the mod hooks Steamworks.NET's
 * ManualDispatch_GetNextCallback): GameLobbyJoinRequested_t / GameRichPresenceJoinRequested_t
 * become join requests for dsnet_steam_take_join_request. Safe before dsnet_steam_load. */
void dsnet_steam_feed_callback(int id, const void *param, int cub);
const char *dsnet_steam_lobby_data(uint64_t lobby, const char *key);   /* "" if unknown */
uint64_t    dsnet_steam_lobby_owner(uint64_t lobby);                   /* the host's SteamID64, 0 if unknown */

/* Test hook: bridge a ready-made SNS connection (e.g. from CreateSocketPair) as a host peer /
 * as a joiner. Used by steam_check. */
void              dsnet_steam_host_close_peers(dsnet_steam_host *h);   /* drop every guest (host leaving) */
int               dsnet_steam_host_adopt(dsnet_steam_host *h, uint32_t hconn);
dsnet_steam_join *dsnet_steam_join_adopt(uint32_t hconn);
/* Test hook: ISteamNetworkingSockets::CreateSocketPair (loopback through SNS). 0 ok. */
int dsnet_steam_socket_pair(uint32_t *a, uint32_t *b);
