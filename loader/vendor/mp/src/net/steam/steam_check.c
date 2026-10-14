/* Standalone check of the Steam transport. Loads libsteam_api from the game bundle (read only).
 * Steam not running -> says so, exit 0. Otherwise: SteamAPI_InitFlat as the game's app id (no
 * window, no overlay), P2P listen socket + friends-only lobby (left again at the end), then a
 * real dsnet session (server + host client + one joiner) over an SNS loopback socket pair through
 * the same transport and bridge code a remote friend would use. */
#include "dsnet_steam.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define GAME "~/Library/Application Support/Steam/steamapps/common/Dungeon Settlers/DungeonSettlers.app/Contents"

static int got_order;
static void on_cmd(const dsnet_cmd *c, void *u) {
    if (c->type == DSNET_GAME_ORDER && c->len == 5 && !memcmp(c->payload, "hello", 5)) got_order |= *(int *)u;
}

static void run_ticks(dsnet_client *c, uint32_t *timer, int bit) {
    while (dsnet_can_run_tick(c, *timer)) { dsnet_take_cmds_for_tick(c, *timer, on_cmd, &bit); (*timer)++; }
}

int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : GAME "/PlugIns/steam_api.bundle/Contents/MacOS/libsteam_api.dylib";
    if (dsnet_steam_load(lib)) { printf("load: %s\n", dsnet_steam_error()); return 1; }
    printf("loaded %s\n", lib);
    if (!dsnet_steam_is_running()) { printf("Steam is not running: nothing to test, exit 0\n"); return 0; }

    uint32_t appid = 2798330;
    FILE *f = fopen(GAME "/MacOS/steam_appid.txt", "r");
    if (f) { if (fscanf(f, "%u", &appid) != 1) appid = 2798330; fclose(f); }
    if (dsnet_steam_init_standalone(appid)) { printf("Steam running but init failed: %s\n", dsnet_steam_error()); return 0; }
    int online = dsnet_steam_logged_on();
    if (!online) printf("Steam running, NOT logged on: lobby and P2P skipped, loopback tests only\n");
    else printf("Steam up, app %u, my SteamID %llu\n", appid, (unsigned long long)dsnet_steam_my_id());

    int fails = 0;
    dsnet_set_identity("steam_check", 1);
    dsnet_cfg cfg = dsnet_cfg_defaults();
    cfg.bind_addr = "127.0.0.1";
    dsnet_server *srv = dsnet_server_start(&cfg);
    if (!srv) { printf("dsnet server failed\n"); dsnet_steam_shutdown_standalone(); return 1; }

    dsnet_steam_host *h = dsnet_steam_host_start(dsnet_server_port(srv), NULL, 0, 4);
    if (!h) { printf("host start failed: %s\n", dsnet_steam_error()); fails++; }
    else {
        printf("P2P listen socket %u (virtual port %d)\n", dsnet_steam_host_listen_socket(h), DSNET_STEAM_VPORT);
        uint64_t lobby = 0;
        for (int i = 0; online && i < 1500 && !(lobby = dsnet_steam_host_lobby(h)); i++) usleep(10000);
        if (!online) printf("lobby: skipped (offline)\n");
        else if (!lobby || lobby == ~0ull) { printf("lobby: FAILED (%s)\n", lobby ? "CreateLobby error" : "no result in 15 s"); fails++; }
        else printf("lobby %llu: dsmp_proto=%s dsmp_build=%s dsmp_mod=%s dsmp_host=%s\n", (unsigned long long)lobby,
                    dsnet_steam_lobby_data(lobby, "dsmp_proto"), dsnet_steam_lobby_data(lobby, "dsmp_build"),
                    dsnet_steam_lobby_data(lobby, "dsmp_mod"), dsnet_steam_lobby_data(lobby, "dsmp_host"));

        /* dsnet over an SNS loopback pair: host client local, joiner through transport + bridges */
        dsnet_client *hc = dsnet_client_connect_local(srv, "host");
        uint32_t a, b; uint16_t port = 0;
        dsnet_steam_join *j = NULL;
        if (!hc || dsnet_wait_welcome(hc, 3000)) { printf("host client: FAILED\n"); fails++; }
        else if (dsnet_steam_socket_pair(&a, &b)) { printf("CreateSocketPair: FAILED\n"); fails++; }
        else if (dsnet_steam_host_adopt(h, a) || !(j = dsnet_steam_join_adopt(b)) || dsnet_steam_join_poll(j, &port) != 1) {
            printf("bridge: FAILED\n"); fails++;
        } else {
            dsnet_client *jc = dsnet_client_connect_tcp("127.0.0.1", port, "joiner");
            int w = jc ? dsnet_wait_welcome(jc, 5000) : -1;
            printf("joiner over SNS pair: welcome %s, player %d\n", w ? "FAILED" : "ok", jc ? dsnet_player_id(jc) : -1);
            if (w) fails++;
            else {
                dsnet_submit(jc, DSNET_GAME_ORDER, 0, "hello", 5);
                uint32_t th = 0, tj = 0;
                double t0 = dsnet_now_ms();
                while (got_order != 3 && dsnet_now_ms() - t0 < 5000) {
                    run_ticks(hc, &th, 1); run_ticks(jc, &tj, 2); usleep(5000);
                }
                printf("order from joiner delivered: host %s, joiner %s; joiner tick_until %u (time control rides the unreliable lane)\n",
                       got_order & 1 ? "yes" : "NO", got_order & 2 ? "yes" : "NO", dsnet_tick_until(jc));
                if (got_order != 3 || !dsnet_tick_until(jc)) fails++;
                printf("host peers bridged: %d\n", dsnet_steam_host_peers(h));
                /* guest leaves: the host's session goes on with the host alone */
                dsnet_client_close(jc); dsnet_steam_join_close(j); j = NULL;
                t0 = dsnet_now_ms();
                while (dsnet_present_mask(hc) != 1 && dsnet_now_ms() - t0 < 5000) { run_ticks(hc, &th, 1); usleep(5000); }
                uint32_t t_before = th;
                for (int i = 0; i < 40; i++) { run_ticks(hc, &th, 1); usleep(5000); }
                printf("guest left: host present mask %u, host still ticking %s, bridged peers %d\n",
                       dsnet_present_mask(hc), th > t_before ? "yes" : "NO", dsnet_steam_host_peers(h));
                if (dsnet_present_mask(hc) != 1 || th <= t_before) fails++;
            }
        }
        dsnet_steam_join_close(j);

        /* second guest, then the host closes its Steam side: the guest's join must report the loss */
        dsnet_steam_join *j2 = NULL;
        if (!dsnet_steam_socket_pair(&a, &b) && !dsnet_steam_host_adopt(h, a) && (j2 = dsnet_steam_join_adopt(b))
            && dsnet_steam_join_poll(j2, &port) == 1) {
            dsnet_client *jc2 = dsnet_client_connect_tcp("127.0.0.1", port, "guest2");
            int w = jc2 ? dsnet_wait_welcome(jc2, 5000) : -1;
                        dsnet_steam_host_close_peers(h);
            double t1 = dsnet_now_ms(); int r = 0;
            while ((r = dsnet_steam_join_poll(j2, NULL)) != -1 && dsnet_now_ms() - t1 < 5000) usleep(5000);
            printf("host left: guest2 welcome %s, guest sees the loss: %s (%s, %.0f ms)\n", w ? "FAILED" : "ok",
                   r == -1 ? "yes" : "NO", dsnet_steam_join_error(j2), dsnet_now_ms() - t1);
            if (w || r != -1) fails++;
            dsnet_client_close(jc2);
        } else { printf("second pair: FAILED\n"); fails++; }
        dsnet_steam_join_close(j2);

        /* invite accepted while running: the game's dispatcher hands GameLobbyJoinRequested_t to us */
        uint8_t cb[16]; uint64_t fake = 109775244900000001ull, fr = 76561198000000000ull;
        memcpy(cb, &fake, 8); memcpy(cb + 8, &fr, 8);
        dsnet_steam_take_join_request();                        /* drop argv / launch line result */
        dsnet_steam_feed_callback(333, cb, 16);
        uint64_t got = dsnet_steam_take_join_request();
        printf("GameLobbyJoinRequested -> join request %s\n", got == fake ? "ok" : "FAILED");
        if (got != fake) fails++;

        /* real ConnectP2P to our own SteamID (one account: Steam may refuse; informational only) */
        if (online && getenv("STEAM_CHECK_SELF")) {
            dsnet_steam_join *js = dsnet_steam_join_host(dsnet_steam_my_id());
            double t2 = dsnet_now_ms(); int r = 0;
            while (js && (r = dsnet_steam_join_poll(js, NULL)) == 0 && dsnet_now_ms() - t2 < 15000) usleep(10000);
            printf("P2P to self: %s %s\n", r == 1 ? "connected" : r < 0 ? "refused:" : "timeout", js && r < 0 ? dsnet_steam_join_error(js) : "");
            dsnet_steam_join_close(js);
        }
        dsnet_client_close(hc);
        printf("relay network status: %d (100 = ready)\n", dsnet_steam_relay_status());
        dsnet_steam_host_stop(h);
    }
    dsnet_server_stop(srv);
    dsnet_steam_shutdown_standalone();
    printf("%s%s\n", fails ? "RESULT: FAIL" : "RESULT: OK", online ? "" : " (offline: lobby / P2P not tested)");
    return fails ? 1 : 0;
}
