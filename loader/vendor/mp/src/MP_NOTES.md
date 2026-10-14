# mp module notes (2026-10-12)

Glue between the dsnet lockstep core (`src/net/`) and the game (build 25846389). Tests: `make -C src/mp_test test` (about 14 s).

## Files
- `mp.h` public API. `mp_core.c` session, tick gate, command dispatch, fingerprint, resync, speed/pause votes. `mp_config.c` mp.json reader.
  No game code in these three: the game is reached through `mp_game_api` (a function table).
- `mp_game.h` / `mp_game.c` the real game side (dylib only): fills `mp_game_api` with the determinism lane's functions,
  hooks GameSpeedSetting, adds `mp_*` socket commands. Compiles (`clang -fsyntax-only`), NOT linked or run yet:
  it needs the functions listed below.
- `mp_test/` mock game (fake units, fake ticks, fake save) + 6 tests.

## Config: `instances/X/mp.json`
```
{"role":"host","port":7777,"name":"The owner","allow_speedup":true,"allow_pause":true}
{"role":"client","host":"127.0.0.1","port":7777,"name":"B"}
```
Optional: `min_players`, `tick_ms`, `fp_interval` (host), `auto_resync` (1), `resync_delay_ms` (300), `auto_resume` (1), `connect_timeout_ms` (5000).
No mp.json = mp off, no hooks (the rig works as before). Identity sent in HELLO: build `25846389` + `MP_MOD_VERSION` (1).

## One timer step
`mp_frame` (once per MainFlow.Update while locked) = `mp_ticks_to_run_now` (dsnet pacing, 0 while the game is not ready / loading)
then per step: `mp_apply_cmds(timer)` -> `mp_step_ticks` x `api.sim_tick` -> `mp_step_done` (fingerprint, timer+1).
- Commands run between world ticks. A GAME_ORDER runs inside `api.sim_scope(1)` ... `(0)`, so its RNG draws and Guids come
  from the sim streams. The test checks every order ran inside the scope.
- GAME_ORDER payload: `u16 type, guid[16], u16 arg_len, args` (little-endian). Dispatch table `mp_set_order_handler(type, fn)`;
  MOVE (type 1, args `i32 x, i32 y` = cell) is pre-filled and calls `api.order_move`.
- Ownership (owner 2026-10-12): an order on a unit owned by ANOTHER player is dropped, and only the host may order unclaimed units (same on every peer).
- Speed: SPEED_VOTE carries the game's GameSpeed enum; dsnet takes the lowest. Only two speeds are in play (owner 2026-10-12):
  normal (1, 1x) and fast (2, 3.5x). VeryFast (3, 7x) exists in code but is not a player option: votes are clamped to 2, both in
  `mp_vote_speed` and when a step reads the vote. World ticks per step use a half-tick accumulator: 1 or 3.5 (3,4,3,4...).
  60 steps/s -> 60 or 210 ticks/s. Logs/status print `mp_speed_name`: "normal" / "fast 3.5x" / "paused", never "2x"/"3x".
  The accumulator and the shown speed go into the save header, so a loaded peer continues the same pattern.
- `api.show_speed(speed, paused)` is called at the same step on every peer when the effective speed/pause changes; the game side
  writes it into GameSpeedSetting (original SetSpeed) so the UI shows it.

## Speed / pause hooks (mp_game.c)
| Game function | RVA | From the UI | From a command / sim |
|---|---|---|---|
| `GameSpeedSetting.SetSpeed(GameSpeed, SwitchPauseState)` | 0xD24114 | speed 0 + AbsolutePause -> pause vote; speed 0 + RollBack -> toggle own pause; speed 1-3 -> SPEED_VOTE (+ run vote if paused). Local value unchanged | original |
| `GameSpeedSetting.AddPauseReason(GamePauseReason)` | 0xD24848 | User -> pause vote. EssentialPopup / PopupTimeLock / ConsecrationNotice -> `mp_popup_pause` (never a local pause). CheatConsole, Loading -> original | original |
| `GameSpeedSetting.RemovePauseReason` | 0xD249BC | mirror of Add | original |

"From a command / sim" = `mp_is_applying()` or `rig_det_in_sim()` or mp's own show_speed. Fields: `_gameSpeed +0x10`, `_requestedGameSpeed +0x14`,
`_pauseReasons +0x18`, `_gameSpeedLockCount +0x20` (SetSpeed returns false while locked). Found at `mf+0xD8 -> +0x10`.
Popups with `allow_pause` on: the first open popup votes pause, the last closed one votes run (unless the user paused). With it off: ignored.
**Open:** with allow_pause off a popup is open while the game runs; whether every popup UI copes with that is not tested in the game.
**Open:** sim-origin speed changes (`ForceChangeGameSpeedApplyData`) pass through to the local setting on every peer alike but do not
change the mp tick rate. If a story event must pause everyone, it needs a deterministic PAUSE path (later).

## Desync and resync
- After each step `api.fingerprint` (game: `rig_world_hash(rig_det_sim_state())[0]`) goes to `dsnet_report_fingerprint`; dsnet compares every 30 steps.
- On the flag: dsnet stops the steps (freeze); mp logs `DESYNC ... frozen at step N` once, and after `resync_delay_ms` calls `dsnet_request_resync`.
- Host, inside the DSNET_JOIN_POINT command: `api.save` -> 16-byte mp header (magic "DSMP", v1, half-tick remainder, shown speed/paused) -> `dsnet_upload_save`.
- Client: dsnet's load callback (game thread, from `dsnet_can_run_tick`) -> header -> `api.load` -> no steps until `api.ready()`; then timer = `dsnet_timer`.
- Auto pause (dsnet PAUSE_ALL on join/desync): each player re-sends its own run vote once it is in sync (`auto_resume`), unless it paused
  itself or a popup holds it. The loaded player does this after its load (the PAUSE_ALL is inside the save). So play resumes when the last
  player is back.

### Game save path (decomp_25846389)
- There is no save-to-bytes function. `CampaignSaveLoader.Save(string campaignName, bool isAutoSave, string displayName)` **RVA 0x814890**
  (`this` = MainFlow `_campaignSaveLoader` **mf+0x180**) builds `CampaignSaveData` (`CampaignDataContainer.Serialize`) and calls
  `SaveLoadHelper.SaveFile` **0x87573C**: Newtonsoft `JsonConvert.SerializeObject` (TypeNameHandling 4, indented) -> `WriteJsonFileSafely`
  -> `persistentDataPath/Saves/<campaignName>.json` (= `instances/X/data/Saves/` in the rig), synchronous. Returns bool.
- `isAutoSave` trap: if false while the campaign setting byte `+0x24` (iron mode, per `KeepOnlyRetainedCampaignSave`) is set, Save logs a
  warning and returns false. With true in iron mode it then calls `KeepOnlyRetainedCampaignSave` (deletes the campaign's other saves, here
  only inside the instance). So pass isAutoSave = that flag.
- The menu path `InputEventHandler.SaveConfirmed` 0xD9B960 first runs `RunPreSaveStabilization` 0xD9BA8C. Unknown if needed for a mid-tick
  save; the join point runs between ticks.
- mp_game's save = `u32 det_len, det state, the JSON file` (name `mp_sync`, SaveName inside = file stem, so `IsValidLoadData` passes).
  Load = write `data/Saves/mp_sync.json`, `rig_det_state_set` (pending), `rig_load_save_file` (LoadCampaignRequested; 10-25 s).
- **Risk (rwmt lesson 5):** the host does not reload its own save. Anything in memory but not in the JSON (A* state, event queues, BT
  timers, view caches) can differ between the host and the loaded peer. Check "save -> load -> same fingerprint" in the game first.
  If it fails, make every peer (host too) reload at the join point.

## What the determinism lane must expose (declared in `mp_game.h`)
```c
int    rig_det_mp_ready(void);              /* locked && runnable() */
void   rig_det_sim_tick(void);              /* one sim_cycle() */
void   rig_det_sim_scope(int enter);        /* sim RNG + in_sim without a tick (for orders) */
int    rig_det_in_sim(void);
size_t rig_det_state_size(void);
void   rig_det_state_get(void *out);        /* sim_state, seed, guid_sim_n, sr_sim_n */
void   rig_det_state_set(const void *in);   /* not locked: apply at the next auto-lock instead of re-seeding */
int    rig_order_move(Guid g, int x, int y);/* inject_orders body for one MOVE; 0 ok */
int    rig_save_campaign(const char *name); /* CampaignSaveLoader.Save 0x814890 on mf+0x180; 1 ok */
int    rig_load_save_file(const char *path);/* c_load_save {file, det:1} internals; ready() = 0 from here to the auto-lock */
```
And call into mp:
- `mp_game_install()` from the GameAssembly add-image callback, after `rig_det_install()`.
- In `rig_det_frame`, when locked: `if (mp_game_active()) { mp_game_frame(); return 1; }` instead of its target loop.
- In `rig_exec`: `char *mr = mp_game_exec(cmd, line); if (mr) return mr;` and `mp_game_status(&b)` in `c_status`.
- Call `mp_game_poll()` every MainFlow.Update, title screen too (end of `rig_frame`): Steam joiner bridge + accepted Steam invites.
- `build.sh`: add `mp_core.c mp_config.c mp_game.c net/dsnet_link.c net/dsnet_server.c net/dsnet_client.c net/steam/dsnet_steam.c`.

## Title-screen lobby (ui lane)
`mp_game_session()`, `mp_game_host(cfg)` (session now + Steam lobby via the bridge if libsteam_api is loaded and logged on),
`mp_game_join(cfg)` (TCP now; `host_addr` "steam:<lobby id>" = Steam join, connects to the bridge's local port in `mp_game_poll`),
`mp_game_steam_invite()` (overlay invite, -1 without a Steam lobby). Speed hooks are now always installed (pass through with no session).
Order types 3-5 (`MP_ORDER_STORY_VOTE`, `MP_ORDER_MAP_PROPOSE`, `MP_ORDER_MAP_VOTE`, ui lane) skip the owner check; their handlers apply their own rules.
Socket commands then: `mp_status`, `mp_move {guid,x,y}`, `mp_pause {on}`, `mp_speed {speed}`, `mp_popup {reason,add}`,
`mp_claim {guid}`, `mp_release {guid}`, `mp_resync`.

## Test results (mp_test, 3 runs, about 14 s each, all pass)
- orders_ownership: host + 2 TCP clients, 500 steps, about 330 orders (scripted + random from all three): same hashes every step,
  same orders on the same steps, a foreign-unit order dropped on all peers, owner order applied, all orders inside the sim scope.
- speed_pause: all vote Fast -> 3/4 ticks per step averaging 3.5; VeryFast votes (via mp_vote_speed and raw) are clamped to fast; one pause vote pauses all
  (0 ticks per step); two overlapping popups hold one pause until both close; show_speed calls identical on every peer.
- lobby_off (allow_pause 0, allow_speedup 0): pause vote, popup and fast vote ignored, 1 tick every step.
- desync_resync: one client's world corrupted at step 100 -> flagged, frozen, host save (with a 4 KB pad, several fragments) loaded,
  equal again; healthy peers untouched; auto pause then auto resume.
- late_join: third player joins at ~150, waits for the save, loads, equal; auto pause then resume.
- config: host / client / missing role / client without host / "host" as a value / missing file.
Not tested: anything in the real game (mp_game.c is only syntax checked; the game copies belong to the determinism lane right now).

## Steam play (steam lane, 2026-10-12) - `mp_steam.h`, implemented in mp_game.c
- Menu calls: `mp_steam_host(save, cfg)` (save NULL = loaded/new campaign, else save name or path, loaded now; cfg NULL = defaults), `mp_steam_invite()` (overlay),
  `mp_steam_invite_friend(id)` + `mp_steam_friends()` (invite through Steam chat, no overlay needed), `mp_steam_join(lobby)`,
  `mp_steam_leave()`, `mp_steam_state()` (idle / hosting / joining / in_game / ended + players + msg), `mp_steam_ack()`.
  Socket commands for tests: `mp_steam_host {save}`, `mp_steam_join {lobby}`, `mp_steam_invite`, `mp_steam_invite_friend {steamid}`,
  `mp_steam_leave`, `mp_steam_ack`, `mp_steam_state`.
- Steam is initialised by the game itself (MP app = no Steam gate). The rig copies never init Steam, so everything here is off there.
  `steam_ready()` retries every 2 s until libsteam_api is loaded, SteamAPI is up and the user is logged on.
- Host: dsnet server (port 0) + friends-only lobby (dsmp_proto / dsmp_build / dsmp_mod / dsmp_host); each Steam peer is bridged to the
  server's loopback port. Guest: lobby -> owner SteamID -> ConnectP2P; `mp_start` runs only once SNS reports Connected (30 s limit).
- Accepted invites: hook on Steamworks.NET `NativeMethods.SteamAPI_ManualDispatch_GetNextCallback` (RVA 0x1141AAC); the game registers
  no lobby callbacks, so GameLobbyJoinRequested (333) / GameRichPresenceJoinRequested (337) go to `dsnet_steam_feed_callback`.
  Launch argument `+connect_lobby <id>` is read too, BUT Steam launches the normal game (same app id), not the MP app, when the friend
  accepts while the game is closed. So the friend opens Dungeon Settlers MP first, then accepts (or uses the friends list / join).
- Host leaves (or the Steam link drops): guest session ends, state ended "The host left the game", then quit to title
  (`SystemMenuView.QuitToTitleSceneCallback` 0xF2F234 on FindObjectOfType(SystemMenuView), once `rig_det_mp_ready`). Not tested in the game.
- Guest leaves: the host's server sees the TCP side close -> PLAYER_LEAVE -> host continues. Units released by dsnet on PLAYER_LEAVE
  (netcore, done 2026-10-12). A TCP guest also sees the host leave (dsnet_is_disconnected). 2026-10-13: units of a leaver stay RESERVED (owner -2) for the same player uid; on Steam uid = "DSMPSTM\0" + SteamID64, else 16 random bytes kept in <rig_dir>/data/mp_uid; set before every connect (mp_game.c set_uid is the only caller).

## Menu / Steam joiner at step 0 (2026-10-12, from the transitions lane)
- Gap: a joiner with no campaign that connects before the host's timer moved got no join point (dsnet only sends one to late
  joiners), so it sat on the title at step 0 and the host stalled at max_behind.
- Fix (mp only, no dsnet change): `mp_config.need_save` (mp.json `"need_save": true`; `mp_game_join` always sets it). After the
  WELCOME, if dsnet did not already mark the player as needing state, mp calls `dsnet_request_resync` -> the server issues a join
  point even at step 0 -> host uploads -> guest loads. `mp_game_poll` now runs `mp_frame` while not locked (title / loading), so the
  pending save is actually taken on the title screen.
- dsnet limit (netcore lane): a RESYNC sets the player to LOADING, and LOADING players do not count toward `min_players`. With
  `min_players` > 1 and a need_save guest the timer never starts, so the join point never runs (test showed it). Fixed in dsnet by netcore
  (loading/waiting players count; their test prestart_save); mp's forced `min_players = 1` is removed.
- Tests `step0_join(min_players 1)` and `(min_players 2)`: guest with a different world joins at step 0, gets the save, all steps equal.

## Base work + gear (2026-10-12)
- An order whose owner Guid is all zero is global (any player), like types 3-5: base work (craft, build, research, storage, recruit). Tested (`orders_ownership`, type 7).
- `int mp_gear_check(const uint8_t unit[16])` (mp_game.c, exported, ord_mp.c dlsym): 1 if the local player may equip the unit under the host's gear mode (`dsnet_may_equip`); 1 with no session.

## The world's save lives only on the host (owner 2026-10-13)
- The join-point save travels as `data/Saves/mp_sync.json` (the game only loads a file in Saves/ whose stem = its SaveName, so no
  hidden folder is possible). Every peer deletes it as soon as the load that reads it is done (`rig_det_mp_ready()` again): the guest
  after its join/resync load, the host after its own join-point reload. Leftovers from a crash are deleted at start.
- Hook `CampaignSaveLoader.Save` 0x814890: returns false on a guest in a session, and while a received world stays loaded after the
  session ended (until the title). Covers manual save, autosave and save-before-exit (trn_game.c already stops guest autosaves earlier).
- Hook `CampaignSaveLoader.KeepOnlyRetainedCampaignSave` 0x814E88: skipped during the join-point save. Found while doing this: in an
  iron-mode campaign `rig_save_campaign` passes isAutoSave = true, and Save then deletes every other save of the campaign, so each join
  point deleted the HOST's real iron saves (and mp_sync is now deleted too, which would have left the host with none).
- Status (`mp_status`): `guest_world`, `saves_blocked`, `sync_files_deleted`, `sync_file_pending`.
- Proof: `rig/mp_saves_test.py` (leases a pair, runs mp2_test: join from the title, map changes, resync; then compares every file in the
  guest's Saves folder and its SaveHeaderCache.json).
- Save-list cache: after deleting mp_sync.json, mp_game calls `CampaignSaveLoader.TryRefreshSave` 0x812AB8 (-> `SaveLoadHelper.RefreshSaveCache`
  0x8711D4: drops missing files, rewrites `SaveHeaderCache.json`, refreshes the loader's in-memory list). Proven 2026-10-13 on pair CD
  (`rig/mp_saves_test.py`, log `rig/det_out/mp_saves_test2.log`): every mp2 check PASS, guest Saves folder unchanged, guest header cache
  without mp_sync, host mp_sync gone, prefs identical. Status field `cache_refreshes`.

## Host Admin mode (owner 2026-10-13, mpui + netcore)
- `run_order`: a GAME_ORDER from player 0 passes for any unit owner while `dsnet_admin_mode(c)` is on (dsnet simstate, set by the host's
  `DSNET_ADMIN_MODE` command; a client's command is dropped by the server). Read at the command's step, so every peer agrees.
- Test `admin_mode`: a client's admin command ignored; host turns it on at step 50; host orders a unit owned by c1 and an unclaimed one
  (applied); c2 orders both (dropped); same applied orders on all peers.
- `orders_ownership` global-vote check made order-independent (votes from different peers may land in either order; it was flaky after
  the new pacing). Still checks every peer saw the same votes on the same steps.

## Host left -> guests kicked; guest leave / rejoin (lead + owner 2026-10-13, v7)
- Host gone (process killed, Esc "Leave multiplayer", silent link loss): `dsnet_is_disconnected` (netcore's 2 s receive-silence
  timeout covers the silent cut) -> `end_session("The host left the game")` -> back to the title with the game's own
  `QuitToTitleSceneRequested(autoSaveBeforeQuit = false)` (TI 0x6652F28, ctor 0xD61E50) on the global EventQueue. The old
  SystemMenuView path failed when the system menu had never been built. Retried every 2 s until scene 1.
  `rig/host_left_test.py` (pair IJ): title after 1.23 s (kill), 1.23 s (Leave), 3.24 s (silent cut); no save on the guest.
- Any own exit leaves the session at once: hook on `QuitToTitleSceneRequested..ctor` 0xD61E50 (stock Quit to title and
  ReturnTitleAction; the load path does not build one), back on the title while a session ran (not during an mp load),
  `Application.Quit()` / `Quit(int)` hooks (0x507B1A0 / 0x507B150), and an atexit handler. All close the socket -> PLAYER_LEAVE now.
- Stable player id: Steam id (netcore's set_uid), else 16 random bytes kept in `<data>/mp_uid` (per install), so a LAN guest
  who rejoins gets his RESERVED units back.
- Host hold (owner 2026-10-13: a disconnect ALWAYS pauses, every pause mode): on PLAYER_LEAVE the host sends dsnet's HOLD
  (reason PLAYER_LEFT, the leaver's uid; 0 game ticks on every peer, also in "No pause" mode). `mp_left_players` = dsnet's hold
  list. "Keep playing" (`mp_left_choice(1)`, or the host pressing play) = HOLD off + `dsnet_set_temp_control(uid, 1)`: the host
  may ORDER that player's RESERVED units (no reassign / equip) until he rejoins. "Wait" keeps the hold; it ends by itself when
  the same uid rejoins. run_order uses `dsnet_may_order` (own -> owner, unclaimed -> host, RESERVED -> host with Admin or temp
  control, Admin -> any). Tests: mp_test `leave_rejoin`, `leave_nopause`.
- Popup API for mpui: `mp_game_left_players / mp_game_left_choice / mp_game_left_waiting`; socket `mp_left`, `mp_left_choice {keep}`,
  test `mp_quit {mode: stock|title|app}`.
- Owners persist with the world: after each normal host save, `dsnet_export_owners` -> `<data>/mp_owners/<campaign guid>_<tick>.bin`
  (ClanDataContainer.CampaignGuid + the world tick saved). The next host session imports, before its first tick, the table
  with the highest tick <= now and within 1200 ticks (the menu loads without det, so ~60-70 ticks run before the lock).
  Bug 2026-10-13 (pair CD, Start run): keyed by campaign only, a fresh copy of the demo save got the table of a LATER save
  (units already owned, guest claims refused, "owned by the host"). Proven: left_prompt Start variant with that stale table
  planted (all PASS), and `leave_rejoin_test.py --only-e` (table saved at 8939050 restored at 8939112).
- Tests: mp_test `leave_rejoin` (mock); `rig/leave_rejoin_test.py` (pair, TCP, owner menu flow).

## Host reload hung on the title after return-to-base (bug 2026-10-13, fixed)
- Cause (pair IJ ErrorLog 03:02:06 "Save became unavailable before campaign transition ... mp_sync.json does not exist"): the
  game itself deleted mp_sync.json between the host queueing its join-point reload and the load reading it. The map-change
  autosave (iron campaign; SaveAutoSaveFile + its cleanup) deletes the campaign's other saves, mp_sync included. LoadCampaign
  had already closed the campaign, so the host sat on the title. Reproduced with `rig/race_test.py` mode autosave: the game
  tried to delete mp_sync twice right after the reload was queued.
- Fix 1: hook `CampaignSaveLoader.Delete(string)` 0x815748: the game never deletes mp_sync while a session or an mp load is
  pending (`game_deletes_blocked` in mp_status). mp deletes it itself once the load is done.
- Fix 2: stuck-load retry: the last join-point save (JSON + det state) stays in memory until its load is done; if the load sits
  not-busy and not-ready (title, or still the old campaign when the preflight failed first) with the file gone for 3 s, or 20 s
  in any case, it is written again and re-queued (2 tries; `load_retries`).
- `mp_game_load_pending()` for the menu's title teardown (skip it while 1); `mp_game_last_reject()` for the join page.
