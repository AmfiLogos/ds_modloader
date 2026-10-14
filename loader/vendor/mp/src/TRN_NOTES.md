# Transitions lane notes (2026-10-12)

Design: `research/transitions_design.md`. Files: `trn.h`, `trn_logic.c` (no game code), `trn_game.c` (hooks + `trn_*` socket commands),
`trn_mp.c` (mp glue), `trn_test/` (`make -C trn_test test`). Test build: `build_trn/build_trn.sh` -> `build_trn/dsrig_trn.dylib`,
launcher `build_trn/trn_rig.py`, game test `build_trn/trn_floor_test.py`.

## 1. Deterministic floor generation (done, tested in the game)
- Seed: `DSDungeonPreDataGenerator.GetRandomNumbersGenerator` 0xC5ABDC. When the game asks for a random seed (it uses the
  clock-seeded `_runtimeSeedGenerator`), the hook gives `trn_floor_seed(TimeData.Tick, region, floor, current dungeon seed, attempt)`.
  These values are saved and the same on every peer. Forced seeds and the Tutorial region are left to the game.
  The game's own retries use requested+1, +2 and then the default seeds 0, 1, 2, so they stay deterministic.
- Time limit: `GraphBasedGeneratorGrid2D<object>..ctor` 0x1E71D84 sets `EarlyStopIfTimeExceeded` to none and `EarlyStopIfIterationsExceeded`
  to the cap (default 20000; `trn_cfg {cap}`). The iteration count is read in the early-stop closure 0x1D0F1F0.
- Result log: `SetGenerationResult` 0xC5B1E4 hashes the generated map (seed, RawGrids, rooms, corridors). See `trn_status`.
- **Game test, pair EF, 2026-10-12:** both copies loaded the same save with `det:1` (tick held at 8938508) and went down 3 floors
  (`trn_godown` + `step 1`). Each time the seed, map hash, room count and dungeon floor and seed were identical:
  1566237515 / f91df92d6550632a / 14 rooms; 577951367 / 3f8075463e0b3fd5 / 14; 1997880150 / 632b28035864ea25 / 15.
  Files: `build_trn/floor_test_EF_det.json`.
  Control run with the stock clock seed (`trn_cfg {seed:0}`): different seeds (1048380185 vs 1962984239) and different maps.
  Cap check (`cap 3`): every attempt was stopped after 4 iterations, then the game tried requested+1, +2 and seeds 0, 1, 2,
  and then aborted the transition cleanly. Iterations seen on a real floor: 4 to 15. The cap of 20000 is not tuned further.
- Stock bug seen: going down from floor 4 (rig only, no UI check) throws IndexOutOfRange in `Generate d__24` (floor table); the game aborts the transition.

## 2. Flow barrier (code ready, needs other lanes)
- `trn_flow_busy()` = rig `busy()` || `EventQueue._isProcessiningAsync` (mf+0x48 -> +0x40).
- Sim scope around the 9 flow coroutines (`ProcessAsyncEvent`, ChangeMap `d__100`, `Run`, `RunGuarded`, `d__30/31/51/52/53`). It uses
  `rig_det_sim_scope` / `rig_det_in_sim` through dlsym, so it does nothing until the determinism lane exports them (status says "absent").
- `trn_flow_frame()` (trn_mp.c): busy edges -> `dsnet_set_busy` (weak, netcore). When a map change ends: `mp_vote_speed(1)`
  (owner: speed back to 1x; it is a command, so all peers apply it at the same step).

## 3. Host-only autosave (code ready)
- `trn_set_role(is_host)`. Client: `RequestAutoSave` 0xFCA3C0, `<StartAutoSave>.MoveNext` 0xFCD068 and `<ProcessPendingAutoSave>.MoveNext` 0xFCCE18
  do nothing. MP host and client: the game-hour autosave (`AutoSavePresenter.CallAutoSave` 0xE84070, `AutoSaveHandlerView.CallAutoSave` 0xE83E98)
  never enters the EventQueue. The host calls the save helper directly (mf+0x50 -> +0x20 -> +0xA8). Not tested in the game yet.

## Registration lines for the lead
- `rig_main.c`: `#include "trn.h"`, and `trn_install();` right after `rig_det_install();`.
- `rig_cmd.c` `rig_exec`: after the det line: `if (!strncmp(cmd, "trn_", 4)) { char *tr = trn_exec(cmd, line); if (tr) return tr; }`.
- `build.sh`: add `trn_logic.c trn_game.c`. Once mp is linked, also add `trn_mp.c`.
- mp glue (`mp_game.c`): call `trn_set_role(dsnet_is_host)` at session start, and `trn_flow_frame()` once per frame while a session runs.
- **mp_core (mp lane):** check `api.ready()` after EVERY `sim_tick`. If it drops, keep the step's remaining ticks, do not call
  `mp_step_done`, and finish the step once `ready()` is back (design 3.3). `rig_det_mp_ready` must use `trn_flow_busy()` (or add
  EventQueue+0x40 to `busy()`).
- **determinism lane:** export `rig_det_sim_scope(int)` and `rig_det_in_sim(void)`. Make counter Random/Guid hooks ignore non-main
  threads (Edgar runs on a worker thread).
- **orders lane:** in multiplayer, do not send captured `ChangeMapRequested` (ord id 42) or `GoDownDungeonRequested` (43) from the UI.
  Give the ord payload to `ui_map_propose(kind, payload, len)` instead. On accept, the ui lane calls `trn_submit_map_change(payload, len)`,
  which sends it as a host ord order (zero guid).
- **netcore:** `dsnet_set_busy(c, busy)`. The server does not advance the timer while a playing peer is busy, and exposes `dsnet_busy_mask` for the UI.

## UI needs (ui lane)
- Map-change vote: already in `ui_model` (`ui_mapvote_*`, order types 4/5). This lane provides `trn_submit_map_change`.
- "Waiting for <names> (loading)" overlay while `dsnet_busy_mask` has other players, after our own transition is done.
- Optional: show "speed set to 1x after map change".
