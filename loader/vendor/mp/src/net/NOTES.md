# dsnet notes (2026-10-12)

Lockstep net core in C, no game dependencies. Design copied from rwmt (RimWorld Multiplayer):
- Server timer, stamping, tickUntil + sentCmds: `Common/MultiplayerServer.cs` Run/TickNet, `CommandHandler.Send`.
- Client pacing (3-7 step buffer, 1.2x / 0.8x): `Client/Patches/TickPatch.cs:73-94` -> `dsnet_steps_this_frame`.
  Changed 2026-10-12: several steps per frame (accumulate real time, keep the remainder, max 4 steps carried,
  frame time counted up to 1 s, never more than tickUntil allows), so 10 fps and 2 fps peers still run
  60 steps/s. The server's frame-time slowdown is now opt-in (cfg.slow_to_frame_time), and the ticksBehind
  slowdown only counts the backlog above ok_behind (a 2 fps peer is ~30 steps behind between frames).
- Accept tickUntil only when receivedCmds >= sentCmds: `MultiplayerSession.ProcessTimeControl` -> `process_timectl`.
- Hold when a Playing peer is > max_behind (90) ticks behind, need one < ok_behind (40); slow up to 3x by max ticksBehind/60;
  time per tick = max reported frame time, clamped 1x..4x tick_ms.
- Host-only fingerprint every 30 steps, relayed to the others; mismatch -> desynced, ticking stops.

## Files
- `dsnet.h` public API. `dsnet_internal.h` packets, transport vtable, link.
- `dsnet_link.c` framed link (u32 length + body), fd transport, blob fragments (1 KB), log.
- `dsnet_server.c` sequencer thread. `dsnet_client.c` client net thread + game-thread API.
- `test/test_dsnet.c` toy sim + 20 tests. `make test` (nice 19), about 52 s (freeze_8s and low_fps run as child processes alongside).

## Game hook usage (per frame, game thread)
```
dsnet_report_frame_time(c, dt_ms);
dsnet_can_run_tick(c, timer);                 // also runs a pending save load
for (n = dsnet_steps_this_frame(c, dt_ms); n--; ) {
    if (!dsnet_can_run_tick(c, timer)) break;
    dsnet_take_cmds_for_tick(c, timer, on_cmd, ctx);   // run orders in server order
    for (k = dsnet_ticks_per_step(c); k--; ) game_tick();  // 0 while paused
    dsnet_report_fingerprint(c, timer, state_hash());
    timer++;
}
```
After a load the game must take its timer from the load callback (`tick`). The save is the state at the
moment the JOIN_POINT command runs: commands of that step that come after it, and the step's game ticks,
still run after the load.

## Semantics decided here (check with the owner)
- Pause: paused while ANY present player's vote is pause; a player's vote resets on leave.
- Speed: lowest speed vote among present players (RimWorld LowestWins). Default 1.
- Ownership (dsnet keeps the table, the game blocks foreign orders with `dsnet_owner_of`):
  a player may claim an unowned unit or release its own; no cap (bench; the 3-limit is per expedition); the host (player 0)
  may assign/release any unit. Claims run in server order, so the first claim wins everywhere.
- Host is always player 0 and is the desync reference. Late joiners and resyncing players get
  status loading -> catchup -> playing and never hold the timer while loading/catching up.
- Auto pause (owner, 2026-10-12): on a late join and on a desync flag the server issues PAUSE_ALL
  (every present player's vote becomes pause). Play resumes by the normal vote (all vote run).
  The server drops pause/speed votes from a desynced or loading player, so the pause holds until
  that player is back in sync. Off with cfg.no_auto_pause = 1.
- Host lobby settings (owner, 2026-10-12), fixed at server start, sent in the WELCOME, read with
  dsnet_allow_speedup / dsnet_allow_pause. Start from dsnet_cfg_defaults() (both on; a zeroed cfg means off).
  allow_speedup 0: the server drops SPEED_VOTE above 1x. allow_pause 0: the server drops PAUSE_VOTE and
  never auto pauses; late joiners and desynced players load the save and fast-forward (up to 200 steps
  per frame) while the others keep playing. The game hook should also stop local popups from pausing.
- Pause mode (owner, 2026-10-12) replaces allow_pause: cfg.pause_mode NONE / ANYONE (default) / ALL_VOTE,
  sent in the WELCOME (dsnet_pause_mode; dsnet_allow_pause = mode != NONE). cfg.allow_pause = 0 still forces
  NONE (mp_core sets it). ALL_VOTE: paused only when every present player votes pause, any run vote
  resumes; an auto pause (join/desync) sets all votes, so in ALL_VOTE any in-sync player can end it.
  Story pause pauses in every mode. dsnet_pause_by(): first pause voter (command order) among the votes
  holding the pause; for an auto pause, the joining/desynced player (PAUSE_ALL payload u8 cause);
  -1 when not vote-paused. Save overhead now 2926 bytes.
- Bench / expedition / recruits / gear (owner, PLAN.md "Live version + base"):
  ownership has no per-player cap (table of 1024 units); a leaving player's units become reserved (see stable ids).
  EXPEDITION_READY payload {u8 ready, u8 n 0-3, n guids} (Teams lane format): marks without ready allowed;
  dropped if n > 3, foreign/duplicate unit, or ready with n = 0; a marked unit changing owner leaves the marks
  and clears that player's ready. Getters dsnet_marked_of / dsnet_owned_list / dsnet_ready_mask.
  When every present player is ready (also checked after a leave), dsnet delivers a synthetic EXPEDITION_START
  to the game callback right after that command (same tick, every peer), then resets the ready flags (marks kept).
  dsnet_party_ok: no player > 3 units in a list (unowned count for the host). Recruit rule:
  dsnet_set_recruit_owner(guid) inside the recruiting order's callback -> issuer owns it.
  cfg.gear_mode SHARED_OWN (default) / SHARED_ANY / OWN_STORAGE in the WELCOME; dsnet_may_equip,
  dsnet_item_owner / dsnet_item_set_owner (inside a callback: local + deterministic, outside: ITEM_OWNER cmd,
  same claim/release/host rule) / dsnet_may_use_item (OWN_STORAGE: tagged items only by their owner).
  Save: only used table entries are stored (about 1.4 KB + 17 bytes per owned unit/item; max 89936 =
  DSNET_SAVE_OVERHEAD); dsnet_save_overhead(c) gives the current size.
- Player list: server sends P_PLAYERS at 5 Hz (state, ping, ticks behind) and pings once a second;
  dsnet_player_net(c, player, &ping, &behind, &state), state IN_SYNC / RESYNCING / LOADING / ABSENT.
  dsnet_is_disconnected(c) for a guest to see the host is gone.
- Stable player ids (owner, 2026-10-13): HELLO carries uid[16] (dsnet_set_player_uid; random per connection
  if unset; duplicate -> DSNET_REJ_DUP_ID). PLAYER_JOIN payload = uid + name. Sim keeps known[] (uid, name,
  slot; max 64); unit/item tags store the known[] index. Leave -> slot absent -> units RESERVED
  (dsnet_owner_of = DSNET_OWNER_RESERVED -2, dsnet_owner_info gives the name); rejoin with the same uid in any
  slot -> his again. Reserved units: only the host in admin mode may release / reassign; not equippable.
  World persistence: dsnet_export_owners (sections: 0 players uid+name, 1 units guid+uid, 2 items guid+uid)
  stored by the glue with the save; host dsnet_import_owners -> DSNET_OWNERS_LOAD (15) commands.
  (Replaces the earlier "leave -> units unclaimed".)
- Disconnect hold + temporary control (lead, 2026-10-13): DSNET_HOLD (16, host only, every pause mode incl.
  NONE): {on, reason, uid}; 0 game ticks while any hold; a PLAYER_LEFT hold ends when that uid rejoins.
  dsnet_hold / dsnet_hold_count / dsnet_hold_get (reason, uid, name). DSNET_TEMP_CONTROL (17, host only):
  {uid, on}, only while that player is away; the host may ORDER his reserved units (not reassign/equip);
  ends on rejoin. dsnet_may_order(c, player, unit) = the full order rule for mp_core. "No pause" + disconnect
  follows HOLD until the owner decides.
- Owner-table hash (windows lane proposal, 2026-10-13): P_FP carries the game hash + an order-independent hash
  of the unit/item owner tables (owner by uid). A split is a desync; dsnet_desync_reason() = GAME / OWNERS bits.
  dsnet_owner_hashes() for status / cross-play checks (compare paused at the same step).
- Barrier (transitions_design.md 3.7): dsnet_set_busy(c, 1/0) sends P_BUSY; the server does not advance
  its timer while any playing peer is busy, so the others stop within a few steps. The game hook holds
  the remaining ticks of the step itself. dsnet_busy_mask() (in TIMECTL, ~30 Hz) = busy, loading,
  catching up or desynced players, for "Waiting for <name>". No timeout/kick yet.
- Join checks (HELLO): protocol, game build id, mod version, float rounding mode (fegetround on the
  connecting thread vs the thread that started the server). Mismatch -> P_REJECT with a reason,
  dsnet_wait_welcome returns -2, dsnet_reject_reason tells which. Set dsnet_set_identity(build, mod)
  before connecting. Not checked yet: arm64 flush-to-zero bit (FPCR.FZ), which also changes float results.
- dsnet puts its own state (players present, votes, ownership) in front of every save
  (DSNET_SAVE_OVERHEAD = 1706 bytes), so the game's part may be up to 64 MB minus that.

## Known limits / later work
- All packets go over one reliable stream (TCP or socketpair). Steam P2P: implement `dsnet_transport`
  (write/read/poll_fd/close); time control and keep-alive could then go unreliable.
- A blob is queued whole into the link's output buffer (64 MB save = 64 MB per receiver in memory).
  Fine for now; stream it lazily if memory matters.
- The command log is never pruned. Prune at each join point (keep from save_seq_after).
- Fingerprint is one u64 per 30 steps. rwmt also sends per-RNG-call trace hashes so the first
  diverging call can be found; add once the game hook can hash call sites.
- Disconnect of the host ends the game (no host migration).
- Logging: `dsnet_set_log(fn)` (route to ds_log in the dylib) or env DSNET_LOG=1 for stderr.

## Connect without blocking (ux review #1, 2026-10-13)
- `dsnet_client_connect_tcp` never waits longer than `DSNET_CONNECT_TIMEOUT_MS` (8 s): numeric address first (no DNS),
  then a non-blocking connect polled in 50 ms slices (was a blocking connect: ~75 s on an address that does not answer).
- Menus use `dsnet_connect_start(host, port, timeout_ms)` / `dsnet_connect_poll` (0 trying, 1 connected, enum
  dsnet_connect_error) / `dsnet_connect_adopt` / `dsnet_connect_free` (cancel; returns at once). Lookup + connect run on a
  detached helper thread; poll never blocks and reports TIMEOUT itself if a name lookup hangs past the limit. An adopted
  socket is taken by the next `dsnet_client_connect_tcp` to the same host + port, so the HELLO (and its fegetround) still
  goes out from the game thread.
- `make test` adds `connect`: unreachable 10.255.255.1 -> TIMEOUT at the limit with every poll < 20 ms, bad name ->
  BAD_ADDRESS, closed port -> REFUSED, cancel < 20 ms, live server -> adopted socket welcomed.
- `DSNET_HOLD_LOBBY` (2): the host's lobby hold (0 game ticks until Start). `dsnet_reject_text(why)`: the one wording of a refusal.

## Host rules (DSNET_RULES = 18, lead 2026-10-13)
- Payload u8 pause_mode, u8 allow_speedup, u8 gear_mode, u8 units_per_player (1, 2, 3, 4, 6; default 3). Host only.
  The server checks it and applies its own part at once (cfg: pause / speed vote filters, auto pause, WELCOME values for
  later joiners), then stamps it; each peer applies it in sim_apply at that step. Rules live in the sim state
  (`simstate.rules`, 4 bytes in every save; DSNET_SAVE_OVERHEAD 94815). The WELCOME carries units as a 20th byte.
- Votes and holds stay. Speed-up off caps the speed at 1x (votes kept). Pause mode NONE: pause votes stay but do not pause.
- Units per player: an OWNERSHIP that gives a unit to a player who already owns >= the limit is refused (counted in
  `dsnet_claim_refusals(c, issuer)`, local); recruits over the limit stay free; owners restored on rejoin / OWNERS_LOAD are
  not capped; a lowered limit leaves owned units owned. The expedition party is limited by the same rule
  (DSNET_PARTY_MAX is 6; `dsnet_marked_of` still copies at most 3; `dsnet_marked_list` for up to 6).
- API: dsnet_rules_get / dsnet_set_rules / dsnet_rules_valid / dsnet_units_per_player / dsnet_owned_count /
  dsnet_claim_refusals. Test `rules` (make test). basic / expedition tests adapted: the bench was "no cap".
