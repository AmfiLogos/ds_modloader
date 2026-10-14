/* mp_steam: Steam multiplayer for the menu lane (implemented in mp_game.c, dylib only).
 * Host: friends-only Steam lobby + dsnet over ISteamNetworkingSockets P2P (src/net/steam/).
 * Guest: joins on an accepted Steam invite (GameLobbyJoinRequested, seen by hooking the game's
 * Steamworks.NET dispatcher) or on "+connect_lobby <id>" in the launch arguments; then the host's
 * save arrives through the normal join point. All calls: main thread. */
#pragma once
#include <stdint.h>
#include "net/steam/dsnet_steam.h"

enum { MP_STEAM_IDLE = 0, MP_STEAM_HOSTING, MP_STEAM_JOINING, MP_STEAM_IN_GAME, MP_STEAM_ENDED };
/* finer phase for the menu panels */
enum { MP_PH_IDLE = 0, MP_PH_CREATING_LOBBY, MP_PH_LOBBY_READY, MP_PH_CONNECTING, MP_PH_WAITING_SAVE,
       MP_PH_LOADING, MP_PH_IN_GAME, MP_PH_FAILED = -1 };

typedef struct mp_steam_info {
    int state;                  /* MP_STEAM_* */
    int phase;                  /* MP_PH_* (FAILED = state ENDED) */
    int steam;                  /* 1 = Steam is up and logged on in this process */
    int is_host, player;        /* player = own dsnet id (-1 before the WELCOME) */
    uint64_t lobby;             /* Steam lobby (host: once created; guest: the one joined) */
    int nplayers;               /* players present in the game (dsnet), names below */
    struct { int id; char name[32]; } players[8];
    char host_name[64];         /* player 0's name once known */
    char msg[160];              /* MP_STEAM_ENDED: why ("The host left the game", join refused...) */
} mp_steam_info;

/* Host. save: NULL or "" = the campaign already loaded (or the new campaign the menu starts next);
 * else a save name in the MP save folder ("MyColony" -> data/Saves/MyColony.json) or a full path,
 * loaded now. 0 ok, -1 a session already runs, -2 dsnet did not start, -3 no Steam, -4 load failed. */
int  mp_steam_host(const char *save, const mp_config *cfg);   /* cfg: lobby flags + name; NULL = defaults */
int  mp_steam_invite(void);                         /* Steam overlay invite dialog; -1 no lobby */
int  mp_steam_invite_friend(uint64_t steamid);      /* invite in Steam chat (works without the overlay) */
int  mp_steam_friends(dsnet_steam_friend *out, int max);   /* for an in-game invite list */
int  mp_steam_join(uint64_t lobby);                 /* join a lobby (friend list); 0 ok, -3 no Steam */
/* Leave: host = closes the lobby, every guest gets "The host left"; guest = the host continues and
 * this player's units become unclaimed. The local game keeps the campaign (the menu decides). */
void mp_steam_leave(void);
void mp_steam_state(mp_steam_info *out);
void mp_steam_ack(void);                            /* menu showed the ENDED message: back to IDLE */
