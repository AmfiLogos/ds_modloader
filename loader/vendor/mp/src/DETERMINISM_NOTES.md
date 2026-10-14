# Determinism lane notes (build 25846389, 2026-10-12)

Code: `src/rig_det.c` (new), small changes in `rig_cmd.c`, `rig_main.c`, `rig_sock.c`, `rig.h`, `build.sh`.
Test: `rig/det_test.py`, `rig/desync_diff.py`, `rig/tools/gen_ranges.py`. Logs of the runs: `rig/det_out/`.

## Result

Two copies (A and B), same save, same scripted orders on the same ticks, different frame caps
(10 / 30 fps) and different tick pacing (A: 6 ticks per frame; B: 0..12 random per frame, then 0..40
for the second half) give the **same fingerprint on every one of 6000 ticks**, with a real fight
(save 1: 2 of 4 player units die, enemies lose 4166 HP; numbers identical on both copies).
See `rig/det_out/full1.log` (save 1) and `rig/det_out/full2.log` (save 2, Golem dungeon, fps swapped, other seed).

Fingerprint per tick (`instances/X/fp.log`): tick, all-hash, units-hash (guid, HP, energy, task type, dead,
combat state, combat target, forced target, task target), positions-hash (every entity position on both
maps, raw float bits), rng-hash (tick + sim Random.State), unit count, position count, sim Guid count,
sim System.Random count.

## How to rerun

```
cd ~/ds_multiplayer/src && ./build.sh                  # copies must be stopped
python3 ~/ds_multiplayer/rig/tools/gen_ranges.py      # once per build: build/det_ranges.tsv + build/methods.tsv
cd ~/ds_multiplayer/rig
python3 det_test.py --ticks 6000                                           # save 1 (ElvenExpedition autosave)
python3 det_test.py --save ~/DungeonSettlers_SaveBackups/ImportReady-B0423-Golem-WikiGolem-2026-09-26-1754.json \
        --ticks 6000 --fps-a 30 --fps-b 10 --seed 777                     # save 2
python3 desync_diff.py                                 # compare instances/A vs instances/B again
python3 rig.py check-prefs
```
Options: `--keep` (leave copies running), `--detail FROM TO` (per-entity dump for lock tick + FROM..TO to
`instances/X/detail.log`, plus position-read tracing in that window).
A run of 6000 ticks takes about 5 minutes wall time on a quiet machine (copies run at background priority).

## Rig commands added (control socket)

| Command | What it does |
|---|---|
| `det_seed {seed}` | session seed, before `load_save` |
| `load_save {file, det:1}` | load with determinism on: game ticks are held, load-time gameplay draws use the sim stream, then the copy **auto-locks** before the first tick |
| `lock {seed?}` | rig owns the clock (manual; auto-lock does this) |
| `unlock` | back to the game's own clock |
| `run {to, per_frame, jitter, jitter_seed}` | run until campaign tick `to`; `per_frame` ticks per frame (jitter 1 = random 0..per_frame) |
| `step {n}` | n more ticks, as fast as possible |
| `schedule {tick, kind:"move", guid, x, y}` | order injected at the start of the cycle that produces campaign tick `tick` (DirectOrderRequested + MoveTask into the map's TickFlowHandler) |
| `detail {from, to}` | per-entity dump + position-read tracing for those ticks |
| `status` | now has a `det` block (locked, target, counters) |

`load_save` without `det` and `smoke` behave as before (all det hooks pass through when det is off).

## What the layer does (rig_det.c)

1. **Owns the loop.** One sim cycle = one world tick: [scheduled orders] -> TickRequested into the global
   EventQueue -> `EventQueue.Update` -> `EventProcessor.Update` -> `TickFlow.Update` Base, Dungeon ->
   `ObserverDriven.Update`. The rig runs N cycles per frame before `MainFlow.Update`; while locked the
   hooks on `TimeHandler.Update` (0xD1BA98), `EventQueue.Update` (0xCCF5E4), `EventProcessor.Update`
   (0xCEC070), `TickFlow.Update` (0xAADCF4), `ObserverDriven.Update` (0xBC0F04) skip their per-frame calls.
   Input dispatch, entity loading and view updates still run once per frame. Every cycle checks that the
   campaign tick moved by exactly 1 (`tick_errors` = 0 in all runs).
2. **A\***: `AstarPath.StartPath` (0xFE19C0) is followed by `BlockUntilCalculated` (0xFE138C) inside a cycle,
   so path callbacks (MoveFirstRunner.OnDone) run at once; `AstarPath.FlushGraphUpdates` (0xFDF880) at the
   start and end of each cycle. Runtime check: A* threadCount = 1, BehaviorManager.updateInterval = 2 (Manual).
3. **UnityEngine.Random**: hooks on Range(float) 0x50E78B4, Range(int) 0x50E7910, RandomRangeInt 0x50E7974,
   value 0x50E79D8. A call goes to the **sim stream** when its caller is not view code and (a cycle is running,
   or the game is loading in det mode); otherwise to the normal Unity stream. View = methods in
   `build/det_ranges.tsv` class 1 (Refactor.View/UI/FX/Sound, UnityEngine.*, classes named *FX*, *Graphic*,
   *HUD*, *Presenter*, *UI*, *Tooltip*...). RandomUtil/WeightedRandomUtil are "helpers": the frame above decides.
   The sim stream is seeded from the session seed at load (InitState, then the global state is restored).
4. **Guid.NewGuid** (0x425803C): sim-routed calls get `{lo = counter, hi = "SIM" tag | seed}`; view calls a
   separate "OUT" counter (FX keys, tooltips), so view Guids never shift the sim sequence.
5. **new System.Random()** (0x4276180): seeded from mix(seed, counter) (sim counter or view counter) instead of
   the clock (SpendTick, SetRandomTarget, WeatherSystem, ItemDropSelector, EnvironmentAffectHelper...).
6. **System.HashCode seed** (found by the trace tool, was the last desync): `EntityTargetSelector.InCloseOrder`
   breaks distance ties with `HashCode.Combine(Guid, Guid)`, and `System.HashCode.s_seed` is random per
   process. Two equidistant enemies were picked in a different order on A and B (about half of the runs
   diverged at tick +593). Fix: `GenerateGlobalSeed` (0x425B62C) hooked to a constant, and the static
   `s_seed` (TypeInfo 0x664E378, static+0x0) forced to 0x9E3779B9 every frame in det mode (the cctor
   inlines the generator, so the hook alone is not enough).
7. **Exception guard**: if a cycle does not return (managed exception), the next frame restores the view
   stream and counts `exc_recovered` (0 in all runs).

## Desync tools

`trace.bin` (32-byte records): every sim-routed RNG draw, Guid, System.Random seed and draw, path start,
injected order, thing spawn/move, combat-target decisions (`ExecutionContext.Convert/SetTarget`,
`CombatTargetApplyData`, `DecidePlayerCombatBehaviour.*`), hostile list inserts; each with 3 GameAssembly
return addresses (frame-pointer walk). `desync_diff.py` prints the first differing tick, which hash part
differs, and the first differing trace record with method names (from `build/methods.tsv`).
Several of these extra hooks were added only to hunt the HashCode bug; they are cheap (trace only when
det is on) and stay as diagnostics.

## The rig lane's question: why did B's Random.state not change while A's did?

Unlocked, sim and view share one Unity stream. Gameplay draws are rare when nobody fights (29 sim draws in
600 idle-ish ticks), and the view draws happen on frame-dependent events: `RuleTile.GetTileAnimationData`
(35,082 draws during load), `Light2D.UpdateMesh` (54), `BGMSoundHandler.ChangeBGM` via RandomUtil,
FX coroutines. In that second B simply made no draw, A made one or more (view or sim). With the layer on,
view draws no longer touch the sim stream.

## Other fixes made on the way

- `rig_sock.c`: accepted sockets get `SO_NOSIGPIPE`. When the machine was overloaded, `rig.py` timed out,
  closed the socket, and the copy's reply write killed the game with SIGPIPE (guard.log "signal 13").
- `det_test.py` tolerates a busy main thread (status retry) and waits up to 25 min for boot.
- Load-time gameplay draw found: `RandomEventContainer.InitializeStartTick` (story-event start tick) used
  the shared stream mixed with tile-animation draws; now on the sim stream.

## Open items (not hit or not fixed)

- **Multi-frame flows** were not hit by the test: map transitions (`ChangeMapRequested` and the
  `MapTransition*` coroutines, `WaitForSecondsRealtime`), autosave, creep clear (1 yield), base field
  discover. While locked, `rig_det_frame` simply does not tick when `busy()` is true; Unity coroutines
  started from a sim cycle still resume per frame. They need a lockstep barrier (all peers report done).
- **EventProcessor is not split**: Select/ControlMode/ItemControl processors run inside the sim cycle with
  the Resolver/Region/EventFlow ones. In batchmode they get no input; with real players local input must
  become scheduled commands (lane 3).
- **Async EventQueue events** (load, autosave, transitions) only start from inside a cycle while locked.
- **TimeHandler absolute time / GetTickAmount** are not advanced while locked; no gameplay reader found
  (only TimeHandler itself references them in the decompiled Refactor code).
- **View classification** is name-based (`gen_ranges.py`). A gameplay method with a view-like name would get
  view routing (would then desync, and the trace shows it with the VIEW flag). Not seen in the runs.
- Unity `Random.insideUnitSphere/onUnitSphere` are not hooked (no sim caller seen).
- Out-of-sim deterministic Guids/System.Random during load assume the load order is the same on all peers
  (true in all runs: fingerprint at lock was equal every time).
- Only `move` orders are schedulable; fights come from moving units into enemy rooms.
- Apple Silicon only, same binary (float results).

## Joined dylib + first 2-player test (2026-10-12 afternoon)

`src/build.sh` now links everything into `build/dsrig.dylib`: rig + det, mp (mp_core/mp_config/mp_game),
dsnet (net/ + net/steam/dsnet_steam.c), orders (ord_core/ord_game/ord_mp + ord_test/ord_smoke.c for the
`ord_*` test commands), UI (ui_game/ui_model). trn_* is NOT linked (ui's `trn_submit_map_change` is weak, `-U`).
Install order: rig hooks, `rig_det_install`, `mp_game_install`, `ord_install`, `ui_install`.
Per frame: `rig_frame` (commands, `mp_game_poll`, `ui_frame`), then `rig_det_frame` (with mp: `mp_game_frame`
at title and in campaign, never during boot), then `MainFlow.Update`.
Hook budget: det diagnostics (21 hooks) only with `DSRIG_DIAG=1`; normal build uses about 62 of the 80 stubs.
`rig.py start` passes every `DSRIG_*` variable of its own environment to the copy.

API for mp (rig_det.c / rig_cmd.c): `rig_det_mp_ready`, `rig_det_sim_tick`, `rig_det_sim_scope`, `rig_det_in_sim`,
`rig_det_state_size/get/set` (set always waits for the next auto-lock), `rig_order_move`, `rig_save_campaign`
(CampaignSaveLoader.Save, isAutoSave = iron flag), `rig_load_save_file`. Test commands: `det_corrupt`.

Fixes needed to make it work (each found in the game):
1. **Local input outside the sim.** EventQueue.Update runs once per frame outside the sim again; every sim cycle and
   every order scope ends with `ProcessSyncEvent` (eq_drain), so at frame time the queue only holds local input.
   Before, a local handler that threw (InteractGridRequested without a hover option) cut a sim cycle short on one
   peer only -> desync after every right-click. Also answers ord notes item 6 (sub-events of a replayed order).
2. **Save -> load is lossy** (MP_NOTES risk, confirmed): base units lose their running tasks (task 130/131/140 -> 0)
   and moving units snap to cell centres. So the **host reloads its own join-point save** too (mp_game.c,
   `host_reload`); every peer starts from the same loaded state. 6010 ticks equal after that.
3. **Iron mode blocks reloading the running campaign** ("Current iron mode campaign load was blocked",
   ApplyIronModeLoadPolicy). `load_file` clears the running campaign's iron flag (loader+0x18 -> +0x20 -> +0x24)
   before a det reload; the loaded save restores it.
4. The auto-lock waits until the old campaign was really left (`load_left`), else a reload locked at once on the
   old state. A client starts its session only at the title screen (a save that arrives during boot cannot load).
6. While a peer is not ready (loading for join / resync / host reload), the game's own load-flow SetSpeed / pause
   calls stay local (`pass_through` in mp_game.c). Before, they became that player's votes and both peers stayed
   paused after a resync.
5. Unity stack traces off for Error/Warning/Log and `AstarPath.logPathResults = None` in rig copies (A* "Path Failed"
   spam cost ~80 % of a frame).

Open (seen in the test):
- A level-up popup (PopupTimeLock, reason 4) still votes pause on both peers: ui's "no time lock for LevelUpView in
  multiplayer" did not take effect. The test releases it with `mp_popup {reason:4, add:0}`.
- Host copy dropped to ~2 fps late in long fights: time is in `VFX_Animator.<Routine>.MoveNext` (view) under
  `ViewUpdateDispatcher.UpdateView`. Not a determinism problem, but it slows the whole lockstep. Not investigated.
- While mp is paused no sim cycle runs, so UI events that only act inside a cycle wait; speed/pause buttons work
  because mp_game hooks SetSpeed/AddPauseReason directly.
- EventProcessor (Select/ControlMode/ItemControl processors) still runs inside the sim cycle; held-input paths
  there are not captured by ord.

Test: `rig/mp2_test.py` (rerun: `cd ~/ds_multiplayer/src && ./build.sh && cd ../rig && python3 mp2_test.py --ticks 6000`).
Run `det_out/mp2_full2.log`, 2026-10-12 18:02-18:37: all 10 checks pass. Smoke passes, prefs identical.

## Transitions + math swap joined (2026-10-12 evening)
- `trn_logic.c trn_game.c trn_mp.c` linked; `trn_install()` after `rig_det_install()`; `trn_*` socket commands;
  `trn_set_role(host)` at session start and `trn_flow_frame()` every mp frame (mp_game.c).
- `rig_det_mp_ready()` also needs `!trn_flow_busy()`. mp_core: `run_ticks` checks `ready()` before EVERY world tick;
  if it drops, the rest of the step is kept (`mid_ticks`/`mid_timer`) and finished first on a later frame; reset on load.
  `make -C src/mp_test test`: all pass.
- Test helpers: `ord_inject` field kind `lguid` ("g1,g2" -> List<Guid>, via `ord_debug_lguid_new`), rig command
  `ui_map_vote {accept}` (`ui_debug_map_vote`: answers the open map-change prompt; batchmode has nobody to click).
- mp2_test floor phase: A injects `GoDownDungeonRequested` -> ord hands it to `ui_map_propose` -> B votes yes ->
  host `trn_submit_map_change` -> both go down. Run `rig/det_out/mp2_trn1.log`: floor 0 -> 1 on both, same seed
  366091328, same map hash c04b49ab45c3b68f, 247 equal ticks after, then the resync check also passed.
- Speed test follows the owner: two speeds only (1 normal, 2 fast 3.5x). A fast + B normal -> normal; both fast -> fast 3.5x.
- Mac math swap: `../win/math/math_mac.c -I../win/math ../win/math/build/libdsmath_arm64.a` in build.sh
  (`win/math/build_math.sh` run once). Off unless `DSMP_MATH=1` or `<DSRIG_DIR>/dsmp_math.on`. Not switched on in my tests.

## Map-change votes, same-tick HP, speed pacing, host slowdown (2026-10-12 night)
- mp2_test now also: pauses both after the fight and compares every unit (guid, cell, HP, dead, task) at the SAME
  tick; floor change by vote (GoDownDungeonRequested) and return to base by vote (ChangeMapRequested ToMap 0),
  each with: both changed map, fingerprints equal for 1000 ticks after, speed back to normal (trn votes 1x after a
  transition; the test sets fast before), autosave written only on A. Run `rig/det_out/mp2_maps1.log`: 17 PASS, 0 FAIL.
- Slow "fast" speed is a dsnet pacing bug, not the speed path: `dsnet_steps_this_frame` runs at most 1 step per
  frame outside catch-up (dsnet_client.c:406-420), so a 10 fps copy runs 10 steps/s (1/6 of normal speed).
  Reported to the lead for netcore (loop while real_time > 0 && n < behind).
- Host 2 fps late in fights was caused by the lock, not by the stock game. `rig/perf_test.py`: plain (game clock)
  stays at 9.9 fps for 6000 ticks; det-locked dropped 9.9 -> 3.3 fps. Cause: TimeHandler.Update is skipped while
  locked, so `_lastTick` never moved and GetTickAmount() (ticks this frame, read by the view) grew without end.
  Fix in rig_det_frame: `_lastTick = _currentTick` at frame start, `_absoluteTime += deltaTime` when ticks ran.
  After the fix det-locked stays at 9.9 fps (`det_out/perf_det2.log`), same end state as before.
- 2026-10-12 20:40: with netcore's pacing fix, `det_out/mp2_maps3.log` 18 PASS / 0 FAIL, including the measured
  rate check: normal 58.6 / 58.8 and fast 205.1 / 205.1 game ticks per real second on A (10 fps host) / B.
  mp_core's Fast = half-tick accumulator 7 per step -> 3,4,3,4 ticks (3.5 average, saved in the mp header).
- Stock baseline: `DSRIG_MINIMAL=1` installs only the save-path redirect and command pump (no det/trn/mp/ord/ui
  hooks, no log tweaks). `perf_test.py --mode plain` with it (`det_out/perf_stock.log`): 9.8-9.9 fps for 4885
  ticks of the fight, then the stock game paused itself (level-up popup), so the run was stopped there.
