# dsload: one native mod loader for the Mac launcher (launcher step 2)

Plan: `~/ds_multiplayer/research/plan_launcher.md` (step 2, "One hook engine"). Started 2026-10-14.
Nothing in `~/ds_multiplayer/src`, `rig`, `build`, `win` or `~/DungeonSettlersMods` was changed. The real Steam
install, the owner's saves and prefs were only read (prefs checked byte-identical before/after every game run).

## What it is

`build/dsload.dylib` is injected FIRST in `DYLD_INSERT_LIBRARIES`. It owns the only hook engine in the process and
loads code modules from `<app>/Contents/Resources/mods/<id>/` (the launcher's play-copy layout, `copy_mac.rs`).

| Part | File | Notes |
|---|---|---|
| Hook engine | `src/dsl_engine.c` | Merge of `dsl_hook.c` (dsmods) and `rig_hook.c` (MP). Same 24-byte stub shape. ONE region mapped right after GameAssembly's image (1365 early stubs, 682 late stubs, 2048 slots), ONE registry with owner per site. The `__TEXT` zero tail (static patches + old dsmods cave) is never used. Stubs are written through a second RW mapping, so RX stub pages never become non-executable (both old engines toggled them). |
| Loader | `src/dsl_main.c` | Build from the GameAssembly FILE's arm64 LC_UUID (before it is mapped), mods scan, profile, sha256, requires / conflicts / version ranges, per-build tables, dependency order, crash guard per module and phase, `DYLD_INSERT_LIBRARIES` pass-through. |
| SDK | `sdk/dsl_sdk.h` | API 2 = API 1 + appended `DSLHost` fields (`hook_rva`, `table`, `module_dir`, `ga_path`, `module_loaded`) + two optional entry points. API 1 plugins load unchanged. |
| Name lookups | `src/dsl_il2cpp.c` | Copied from dsmods unchanged. |
| JSON | `src/dsl_json.c` | Small reader for mod.json / profile.json. |

### Module life cycle
1. `dsl_module_load(host, self)`: right after dlopen, before GameAssembly is mapped (env, logs).
2. `dsl_module_early(host, self)`: GameAssembly mapped, none of its code ran yet (dyld add-image time). RVA hooks.
3. `dsl_plugin_init(host, self)`: after `il2cpp_init` returned (loader hooks it). Name lookups. API 1 entry point.
After phase 3 the engine puts new stubs on the late page (game threads may be running).
A non-zero return removes every hook of that module; a crash inside a phase turns the module OFF at the next start
until its file changes (`<log dir>/dsload_state/<id>.disabled`).

### mod.json (fields read by dsload; superset of CONTRACT.md and the plan)
`id` (= folder name), `version`, `kind` or `type` (`code` is loaded; `data` / `patch` / `builtin` only count as
installed for `requires`), `api` (1 or 2), `game_builds` (list) or `game_build` (string), `"*"` = any,
`platforms.mac-arm64.entry` (default `<id>.dylib`), `platforms.mac-arm64.files` {name: sha256} (checked),
`tables` (or `hooks`): {build: {"mac-arm64": "rva/<build>_mac.tsv"}} with lines `name<TAB>0xRVA` (a module with
tables but none for this build is refused), `requires` / `optional` / `conflicts`: see "Dependencies" below.
Profile `<mods>/profile.json` (or `DSL_PROFILE`): `enabled`, `order`, `provided` (ids applied outside the loader,
e.g. `notch-fix`). No profile = every installed mod enabled.
Conflict: the module later in the profile order is dropped and both names are logged.

### Environment
`DSL_MODS_DIR`, `DSL_PROFILE`, `DSL_GA_PATH` (tests), `DSL_LOG_DIR` (else `DSRIG_DIR`, else the game's data folder).
Log: `<log dir>/dsload.log` (previous run: `dsload.prev.log`).

### Coexisting with other loaders
- `DYLD_INSERT_LIBRARIES`: dsload removes ONLY its own entry and keeps every other entry in order (old dsrig did
  `unsetenv`). Proven in host test and in the game (foreign test dylib saw the variable with its own entry).
- A function whose first instruction is a branch or PC-relative (another loader's detour, a static patch) is refused
  with a log line, never overwritten. Proven in the host test with an absolute-jump detour (`ldr x16,#8; br x16`).
- Our engine writes 4 bytes per site (MelonLoader's 16-byte hook broke a 4-byte export, PLAN.md).
- Another hook engine in the same process (old `dsrig.dylib` / `dsmods.dylib`, Doorstop) is logged as a warning.
  Hooks on the SAME function by two engines are refused by ours, not chained. Chaining with Harmony/Dobby is not done.
  (Windows since v0.1.16: chained over an `E9` / `FF 25` jump, see "Harmony + our MinHook on one function, Windows".)

## Owner rules 2026-10-14 ~19:45: mods per world, scope, custom difficulty (PLAN.md "Mods per world + scope + difficulty")
- **Profile per world.** The launcher passes the world's profile path in `DSL_PROFILE` (enabled / order / provided).
  The loader has no global mod list: it loads exactly that profile.
- **Scope.** mod.json `"scope": "gameplay" | "client"`. The default is gameplay, the same as the launcher's `model.rs`.
  After the modules start, the loader writes `<log dir>/dsload_modset.json` with the gameplay and client modules
  that really run (id, version, sha256 of each file in `platforms.mac-arm64.files`, else of the dylib) plus the
  `provided` (data / patch) ids. Modules read the same JSON with `host->gameplay_mods()`, for example for the HELLO
  join check. It is compared with the launcher's `modset_lines` from `DSML_SESSION` (recipe `core/src/join.rs`
  `modset_lines`: `<id>@<version>:<sorted file sha256s>[:<patch hash>]`). A difference is logged. It does not stop the game.
- **World check** (`src/dsl_world.c`, contract with the launcherbuild lane). It is on only when `DSML_SESSION` is set.
  It hooks `CampaignLoadingEventHandler.LoadCampaign(string)` 0xD6C900 by name. Load, Continue (QuickLoadWithNotice),
  rig loads and MP loads all go through it (checked by BL scan). It reads the guid with the game's
  `TryGetSaveHeader`. If `worlds[guid]` differs from `DSML_MODSET`, the load is refused: the coroutine's state is
  set to finished and the game's `CancelIronModeLoad(notice, log)` is called with the notice "This world uses other
  mods. Start it from the launcher." An unknown guid is allowed. A module exporting `dsl_world_check_skip()` that
  returns 1 lets the load through. The multiplayer module returns 1 on a guest (`mp_server_port() == 0`), because
  the HELLO check covers that case.
- **Engine: in-place code patches.** `host->patch_rva(self, rva, old, new, n, name)`. It checks the expected bytes,
  never overlaps a hook or another module's patch (hooks also refuse a patched site), and the original bytes come
  back when the module fails. This is how byte-patch mods go through the one engine.
- **Hell difficulty = module `hell-difficulty` 2.0.0** (`modules/hell-difficulty/`). This is its code half:
  - the 20 instruction patches of `apply_hell.py` (table `rva/25846389_mac_patches.tsv`, generated from the owner's script);
  - the recruit staff strip, ported from the cave to a C hook on `RecruitHelper.ParseItems`. It filters only when
    called from `DetermineInitialEquipments`+0xA4 and when CombatDifficulty == 5.

  Its table half (difficulty rows, texts) is in the module too (`hell_data.c`): at start it rebuilds the Hell table
  bundle from the player's own stock bundle and a shipped delta (`rva/<build>_<mac|win>_tables.dsdelta`, made by
  `tools/hell_delta.py` from the owner's `apply_hell.py` table edits; no game file ships) and loads it instead of the
  stock one. No delta, or a stock bundle that is not the expected one = Hell off. Details: `modules/NOTES_crossmods.md`.
  The mod.json has `"difficulty": {"value": 5}` for the launcher's difficulty list.

## Modules built here (`./build.sh` -> `build/`)
| Module | Source | API | What |
|---|---|---|---|
| multiplayer 0.10.0 | `modules/multiplayer/mp_module.c` + `vendor/mp` (snapshot of `~/ds_multiplayer/src` + `win/math`, 2026-10-14 19:14, `vendor/mp/SNAPSHOT_TAKEN.txt`) | 2 | Every MP source compiled unchanged EXCEPT `rig_main.c` and `rig_hook.c`, which `mp_module.c` replaces: `rig_hook(rva,...)` -> `host->hook_rva`, `rig_init` -> `dsl_module_load` (no `unsetenv`), `on_image` -> `dsl_module_early`. `det_ranges.tsv` ships next to the dylib. |
| gcd-cdr 2.0.0 | `modules/gcd-cdr/` | 2 | The owner's GCD byte patch rewritten as a runtime hook for build 25846389: hooks `SkillComponent.SetGlobalCooldown()` by name, divides `_globalCooldownValue` by (1 + stat 28 / 100), applies it with the game's own `SetGlobalCooldown(float)`. Generic `GetComponent<StatusReader>` + its MethodInfo slot + the runtime-metadata init function come from the per-build table `rva/25846389_mac.tsv` (from dump.cs + disassembly of `CooldownCondition` MoveNext 0x8A4FE4). `sim: true`. |
| hell-difficulty 2.0.0 | `modules/hell-difficulty/` | 2 | Code half of Hell (see above). `sim: true`, scope gameplay. |
| floor-logger 1.0.0 | `~/DungeonSettlersMods/mods/floor-logger` binary, copied unchanged | 1 | Proves API 1 compatibility. |

To refresh the MP snapshot: re-copy `~/ds_multiplayer/src` (+ `win/math/{math_mac.c,*.h,build/libdsmath_arm64.a}`,
`build/det_ranges.tsv`) into `vendor/mp`, then `./build.sh`. If a later MP version adds code to `rig_main.c` or
`rig_hook.c`, port it into `mp_module.c`. Best long-term: MP's own `build.sh` gets a second target that links
`mp_module.c` instead of those two files (owner of src decides; not done here).

## Tests
- `test/run_tests.sh`: host test, fake GameAssembly, never the game. 20 checks (one engine, refusal of a second hook
  on the same function, foreign detour detected, early / init phases, API 1 plugin, order, sha256, conflicts,
  requires, other build, missing table, env pass-through in 3 orders, crash guard, unknown build). Result: 20/20.
- Game copy: `test/make_instance.sh L` (copy of rig's make_instance.sh: APFS clone, stock GameAssembly + Steam gate,
  own bundle id, background-only) + `test/install_mods.sh <App> test/profile_game.json` (launcher layout, re-seal).
  `test/lrig.py` = copy of rig.py with own instance folder (`test_env/instances`), injects
  `dsload.dylib` (+ `LRIG_EXTRA_INSERT`); hidden `-batchmode`, guard process, no lease used.
- `test/game_test.py`: single-player rig smoke through dsload. `test/mp_host_test.py`: single-copy multiplayer host
  session through dsload (see Results).

## Results (2026-10-14)
- Host test: 20/20 PASS (`test/run_tests.sh`).
- Game copy L, hidden `-batchmode`, guard never saw a window, owner prefs identical before/after every run.
  `dsload.log`: build 25846389 from the file, load order multiplayer, gcd-cdr, floor-logger; engine region at
  GameAssembly+0x6c90000, stubs through the RW alias; multiplayer 131 hooks (same count as the release dsrig in
  instance C), gcd-cdr 1, floor-logger 1.0.0 (API 1 binary) 2, loader 2 (`il2cpp_init`, `_utf16`): 136 hooks, 0 refused,
  "modules: 3 loaded of 3 opened". `DYLD_INSERT_LIBRARIES`: our entry removed, the foreign test dylib kept (it saw
  only its own entry).
- `game_test.py` (single-player rig smoke): title, load save, order_move, fingerprints advance, screenshot: PASS.
- `mp_host_test.py` (SOAK_S=600): host session up, lockstep locked; 4 orders through the capture path (ord_inject ->
  ord -> dsnet host) -> mp orders 8, 4/4 units moved; 10 min soak: alive, tick 8940925 -> 8975583, det tick_errors 0,
  16 enemies -> 0 killed, clean exit, PASS. `test_env/smoke_out/*/mp_host_results.json`.
- gcd-cdr: hook installed at 0xDDBA18 and module loaded, but **no global cooldown happened in the 10 min fight**
  (0 calls logged), so its in-game effect is NOT proven yet. Its only caller is `TickEventApplier.Apply(GlobalCooldownApplyData)`
  (tail call at 0xAC71CC), fed by `CooldownCondition` skills. The fight above probably used no such skill [U].
- Found while porting [V: disassembly, not an in-game test]: build 25846389 now HAS
  `Refactor.Combat.SkillCooldownCalculator.GetAdjustedGlobalCooldown` (0x8A54B8 / 0x8ABF6C) = base / (1 + stat 28 / 100),
  the same formula as the owner's patch. `CooldownCondition` MoveNext computes it (0x8A5208) and puts it in
  `GlobalCooldownApplyData`, but `Apply` only checks its sign and then calls `SetGlobalCooldown()`, which uses the
  unadjusted `_globalCooldownValue`. So stock still ignores CDR for GCD in practice and the module is still needed;
  if a later build passes the adjusted value through, the module would divide twice. Owner check in game needed.

## Open points
- A real 2-copy multiplayer pair through dsload (mp2_test-style) is NOT run yet: the pairs were busy with the
  release. To run it: build two copies (`make_instance.sh M`, `N`), set `MP2_PAIR`-style mapping in a copy of
  mp2_test.py onto lrig. Expected to pass because the MP code and its 131 hooks are byte-for-byte the same and go
  through the same stub shape, but that is a conclusion, not a test.
- Windows: the `baselib.dll` proxy still has its own MinHook engine; the same module API is not ported yet.
- The 4-byte site write still makes one GameAssembly page non-executable for a moment (both old engines did the
  same). Safe before game code runs; late hooks (MP ui_teams fallback) carry the same small risk as before.
- `math_mac.c` (MP) still patches GameAssembly's libm lazy pointers from its own constructor. Data pointers, not
  code: it does not touch the engine's region or any hook site.
- The launcher (step 1) still injects `dsrig.dylib` from the multiplayer release entry; switching `launch.rs` /
  the index to `dsload.dylib` + modules is the next step.

## Night 2026-10-15 (loader lane)

### Future-build guard (task 2) - done, host-tested
- Pinned build = the launcher's build for this world when given (`DSML_BUILD`, or `"build"` in the `DSML_SESSION`
  file; the launcher does not send it yet, see MORNING_QUESTIONS) AND one of the builds in `src/dsl_builds.h`.
- Different or unknown build: no module is opened, no hook and no code patch is written, the world check is not
  installed, the game runs unmodded. Three places say why: `dsload.log` ("GAME BUILD CHECK FAILED (<reason>)"),
  stderr ("dsload: ..."), `<log dir>/dsload_status.json` (`active`, `reason` = `unknown_build` / `not_pinned_build` /
  `mapped_image_differs`, `uuid`, `found_build`, `pinned_build`, `loader_builds`, `message`) for the launcher, plus
  one macOS notice "Dungeon Settlers: mods are off" in a normal start (not with `-batchmode` or `DSL_NO_ALERT`).
  On the pinned build the same file says `"active":true,"reason":"ok"`.
- Second layer: the engine itself refuses `hook` / `patch` while `dsl_build_id` is NULL (a module that tries later
  gets a refusal line, nothing is written).
- Host test: 31/31 (T6: env pin differs, session pin differs, pin equal; T5: unknown build says why).

### gcd-cdr proven in game (task 1) - 2026-10-15 05:45, copy L, lease CD, `test/gcd_world_test.py`
Probe = `test/probe/gcd-probe` (test only, never shipped): hooks `SubtractGlobalCooldown` + `HasGlobalCooldown`; a
global cooldown is measured as the sum of the game's own dt from its first count-down tick until it reaches 0.
The save's units used no skill in 150 s, so the probe started 8 global cooldowns per run through the game's public
`SetGlobalCooldown()` (the call `TickEventApplier.Apply` makes after a skill, and the function gcd-cdr hooks) on
units that have cooldown reduction. Base (combat table) 5.000 s. Results (game seconds; last tick adds ~0.01 s):

| Cooldown reduction | Without the mod | With gcd-cdr | Rule 5 / (1 + CDR/100) |
|---|---|---|---|
| 2.4 | 5.01 | 4.893 | 4.883 |
| 2.76 | 5.01 | 4.876 | 4.866 |
| 7.2 | 5.01 | 4.676 | 4.664 |
| 8.7 | 5.01 | 4.609 | 4.600 |
| 9.9 | 5.01 | 4.559 | 4.550 |
| 11.7 | 5.01 | 4.492 | 4.476 |
| 12.9 | 5.01 | 4.442 | 4.429 |

gcd-cdr logged each call ("global cooldown #1: 5.000 s, cooldown reduction 2.8 -> 4.866 s"). Not seen yet: a
global cooldown started by a real skill cast (none happened in these runs). Logs: `test_env/gcd_out/20261015-054549/`.

### World check in game (task 3) - same runs
- Address verified for build 25846389 (Steam buildid 25846389, arm64 UUID 3C5DD8BD-...): dump.cs
  `CampaignLoadingEventHandler.LoadCampaign(string)` RVA 0xD6C900, `TryGetSaveHeader` 0xD6CA44,
  `CancelIronModeLoad` 0xD6CE14; the loader resolves it by name and logged "hooked 0xd6c900". Direct callers
  (BL scan): 0xD75F84, 0xD764BC.
- Session file says the save's world (f816ebc6-...) uses mod set bbb, this copy aaa: "world check: REFUSED ...",
  the game stayed on the title screen (scene 1, no crash). Session changed to aaa: same save "allowed" and loaded.
- [U] The notice text on screen is not verified: batchmode, and the rig screenshot shows only the camera, not the UI.
  `CancelIronModeLoad` takes a TEXT KEY; our plain sentence may show as-is or not resolve. Check in a window run.

### Pair test (task 4) - `test/pair_soak.py` = the MP team's soak_test.py on our copies M (host) + N (guest)
MP snapshot refreshed to lan_v10 src at 06:16 (+ mp_fog.c 06:16 fix; `vendor/mp/SNAPSHOT_TAKEN.txt`), equal to src
at 07:40. The driver waits for "title Y" and the auto-joining guest is past the title before it is polled
(run 1 hung there 22 min), so the shim also accepts scene 2.
| Run | Profile | Result |
|---|---|---|
| 06:29 (1) | multiplayer, gcd-cdr, hell-difficulty, floor-logger | joined, 40 full checks equal, 0 desyncs in 20 min; driver hung (no orders): not counted |
| 07:30 (3), 07:41 (4) | same 4 modules | HOST CRASH at the join-point reload (SIGBUS in mp_fog.c `rebuild` <- `decide` <- `h_eov_set_visible`, LoadCampaign.WorldAndVisionRequested). Reported to corenext. |
| 07:46 (5) | multiplayer only | PASS 3/3: 10.0 min, 0 desyncs, 0 partials, 21 full-state checks equal, orders 37 + 37 (`test_env/build/core_test/soak_20261015-074636`) |
Open: whether gcd-cdr / hell-difficulty / floor-logger change the odds of the fog crash, or it is the MP fog bug alone
(the stack is only MP code + game code; run 1 with the 4 modules got past the same reload).
| 08:54 (7) | multiplayer, gcd-cdr, hell-difficulty, floor-logger (4 loaded, 174 hooks each copy), MP with corenext's 08:05 fog reload fix | PASS 3/3: 10.0 min, 0 desyncs, 0 partials, 21 full-state checks equal, orders 37 + 37 (`test_env/build/core_test/soak_20261015-085425`) |
The 07:30 / 07:41 host crashes were the MP fog bug (stale VisionDataHandler after reload), fixed in src 08:05 by corenext.
No run of this lane used the shared build/dsrig.dylib (our copies load the multiplayer module built here).
Lead decision 10:45: the 08:54 run (all 4 modules = the brief's set plus hell-difficulty, after the fog fix) covers
task 4; no rerun. The MP snapshot was then resynced to src 09:35 (taken while src_merge.lock was held; host tests
31/31, no pair run on it). Next pair run of this lane should resync src first if a merge was in progress.

## Soft restart (plan_modloader.md row 3 / section 2) - 2026-10-15, branch `softrestart`

Owner's words: apply mods without a visible restart; a loading screen like entering a dungeon, the game relaunches
behind it with the fluff cut, the player is put back exactly where they were.

### What is built (`src/dsl_soft.c`, in dsload itself, so it works whatever the new mod set is)
- API: `host->soft_restart(profile, env_lines)` (SDK, appended field) = `dsl_soft_restart()`. The Mods window
  (`modmenu`, other lane) calls it on Apply. `env_lines` replaces env keys for the new process (`DSML_MODSET` of the
  new set; for a set with data mods: `DSL_SOFT_EXE=<that play copy's exe>` + its `DYLD_INSERT_LIBRARIES`).
  It refuses (log line, returns -1) while the game is loading, not at the title / in a world, or already restarting.
  It does not know about multiplayer: the caller must not offer it inside a running multiplayer session (plan).
- A (running game): world case: pause + the game's own `AutoSaveRequested` (the normal autosave of that world; the
  newest file in `<persistentDataPath>/Saves` is the one B loads). The game's own loading screen
  (`ScreenFadeEffectView.AnimateFadeOut(null, showLoadingUI: true)`). Writes `<log dir>/softrestart/handoff.json`
  (scene, save, speed, window frame, camera) and starts B AT ONCE with the same executable, arguments, start folder and
  environment it was started with (taken before dsload edits `DYLD_INSERT_LIBRARIES`), `DSL_PROFILE` = the new profile.
  B "ready" + A's fade done -> A yields activation to B (macOS 14+), writes "go". B "shown" -> A orders its windows out
  and `Application.Quit()`. B dead or no "ready" in `DSL_SOFT_TIMEOUT` s (90): A kills B, fades back in, restores the
  speed, keeps its mods (tested: two runs hit it during development, A stayed usable).
- B (`DSL_SOFT_HANDOFF` set): `TransformProcessType` to UIElement before AppKit, Accessory policy before
  `finishLaunching`, every NSWindow alpha 0 / lowest level / no mouse / never key-main / no activation from creation.
  Fluff cut while hidden: the Unity splash screen is cancelled (icall `SplashScreen::CancelSplashScreen`: it WAS
  running, 3.9 s), frame cap + vsync off (the Entry boot is frame bound: one loading step per frame), background
  loading priority High; the game's own values come back before the swap. At the title: `LoadCampaignRequested(<save>)`
  (the game's Load path; the world check of `dsl_world.c` applies), pause, the same loading screen, "ready". "go":
  window on A's frame, normal level, alpha 1, front, Regular policy, activate, "shown", speed back, the game's fade in.
  B never stays as an invisible process: A gone before the swap, or soft restart unusable, or 120 s -> B quits.
- Game objects by name (il2cpp), hooks only on `MainFlow.Awake` and `ScreenFadeEffectView.Awake` (object capture;
  the live fade view is kept by GC handle and checked through `m_CachedPtr`: a destroyed copy made the fade do nothing
  in one run). GC handles are pointer size in this il2cpp (uint32 crashed).
- Test-only switches (never set by a player's start): `DSL_SOFT_TEST=1` (apply by `softrestart/apply_request.json`,
  optional camera nudge), `DSL_SOFT_STAGE=x,y` (every window kept far off every display), `DSL_SOFT_NOACTIVATE=1`,
  `DSL_SOFT_CAPTURE=<dir>` (the game's own frame, ~10/s, `ScreenCapture.CaptureScreenshotAsTexture`). Every step goes
  to dsload.log with the time since process start; the window state every tick to `softrestart/softrestart_frames_*.log`.

### Proof (copy S = APFS clone, Steam gate off, lease AB; owner prefs identical before/after; nothing on his screen)
`test/soft/soft_test.py <out>`: round 1 = A at the title with multiplayer -> apply multiplayer + gcd-cdr; round 2 = that
B in a loaded world, camera target moved +3,+2, speed Fast -> apply multiplayer only. Final 3 runs (18:54-18:56):

| | apply -> B on screen | B start -> MainFlow.Awake | -> title | -> world loaded |
|---|---|---|---|---|
| title case | 7.23 / 7.42 / 7.56 s | 0.45-0.47 s | 6.15-6.47 s | - |
| world case | 13.68 / 13.74 / 14.02 s | 0.46 s | 6.2-6.5 s | 12.6-12.9 s |
| before the fluff cut (run 4) | 14.8 s / 21.9 s | 4.4 s (splash) | 13.5 s | 21 s |

- (a) B's window: alpha 0.00 in every tick before "go" (1064 ticks logged in one run), and the window server agrees
  (1/s samples: 0.0 before, 1.0 after, every run). Guard (alpha > 0 inside a display, or frontmost): 0 violations.
- (b) What the player sees (frames captured from both games, `soft_out/run12/round{1,2}_player.png`): the world
  fades out (0.3 s) into the game's loading screen (NOW LOADING, Slogel, a tip), it stays ~7 s (title) / ~14 s (world),
  then B's loading screen is shown in the same window place and fades into the same place. No logo, no title, no blank frame.
- (c) B's dsload.log: round 1 "2 loaded" (multiplayer, gcd-cdr), round 2 "1 loaded"; A exited every time (no crash report).
- (d) Same save (A's fresh autosave), camera target 447.50,108.50 zoom 19 in A and in B (exact), speed Fast (2) back.
  In A the nudged target had not moved the visible camera yet (the game moves the view on its next camera input);
  with real player input both agree.
- Dock (`test/soft/dock_test.py`, copy T WITHOUT LSUIElement like a player's copy): LaunchServices lists B as a
  Dock app ("Foreground") for 17 ms (29 ms without `TransformProcessType`), then UIElement until the swap.
  Not visible in any test, but a one-frame Dock icon flicker cannot be ruled out without filming his Dock.
  If he sees it: the copy maker sets LSUIElement in the play copy and dsload sets Regular at a normal start.

### Not proven / open
- Steam: test copies run with the Steam gate off. Two processes with the same app id for ~7-14 s and Steam's
  overlay in B are NOT tested (owner may be in Steam; not tried on his account).
- Real activation at the swap (`yieldActivationToApplication` + `activate`) is not run in tests (it would take focus).
  The Regular switch + Dock icon swap at that moment (B's icon appears, A's goes) is expected, not observed.
- Full-screen modes: the owner plays Windowed (prefs mode 3); a full-screen Space swap is not handled.
- Times are on this Mac under load average 5-9 from other lanes.

### Windows (how; not built: dsload is not ported to the baselib.dll proxy yet, plan row 8)
Same file protocol and the same game calls (il2cpp exports and icalls are the same on Windows). Differences:
start B with `CreateProcessW(GetModuleFileNameW, GetCommandLineW, env block from GetEnvironmentStringsW with
DSL_PROFILE / DSL_SOFT_HANDOFF replaced, same folder)`; in B the proxy hooks `CreateWindowExW` for Unity's window
(class `UnityWndClass`): add `WS_EX_TOOLWINDOW` (no taskbar button) + `WS_EX_LAYERED` alpha 0, never activate
(`ShowWindow` / `SetWindowPos` get `SW_SHOWNOACTIVATE` / `SWP_NOACTIVATE`); swap: A calls
`AllowSetForegroundWindow(B pid)`, B removes the two styles, `SetWindowPos` to A's rect + `HWND_TOP`,
`SetForegroundWindow`. Risk [U]: a layered window may stop DXGI flip-model presents; fallback = window kept visible
but off every monitor (like the Mac test stage). Start once the proxy runs dsload's loader + engine.

## dsl_ui widget kit (lane L1 / agent l1uikit, 2026-10-15) - `src/dsl_ui.c`
Implements every `DSLUi` function of `sdk/dsl_sdk.h` API 3 + `dsl_ui_install()`, `dsl_ui_kit()`, `dsl_ui_title_add()` and the
frame tick (hook `UnityEngine.EventSystems.EventSystem.Update`, once per `Time.frameCount`, then `dsl_frame_hook()`; a weak
empty default is in dsl_ui.c so the loader links before dsl_reg.c defines it). Look ported from mpmenu.c / ui_tip.c.
- Names only: every game function by il2cpp name (+ parameter type names where overloads share name and argc), every field
  by name. No game RVA. Fixed offsets are il2cpp runtime layouts only (object header, array, string, delegate fields).
- Clicks without hooks: buttons / toggles / value arrows are clones of `SubUI_TitleMenuButton` with `Init(crafted Action)`;
  the notice is `CommonNoticeView.Show_Select / Show_Confirm` with crafted Actions. A crafted Action only queues; the
  module callback runs in the frame tick. Field submit = Enter while the field had focus (polled). Tooltip hover polled
  (`Input.mousePosition` vs the screen rect).
- Tooltip = the game's `TooltipUI.ShowTooltipAtScreenPosition` with a stock key, then the shown `Tooltip_ETC`'s texts are
  replaced (no hook: MP owns `Tooltip_ETC.Show`). While ours shows, the pooled window has `CanvasGroup.blocksRaycasts`
  off (else the stock screen clamp pushed it over a low title button and it took the click; found in run 2), restored on hide.
  `TooltipContext` is 0xB8 bytes in this build (MP used a 0xC0 buffer; passed by reference, both work).
- Templates (button, image, text) are cloned from `TitleMenuView._creditButton` the first time the title is seen, kept under
  a hidden DontDestroyOnLoad holder; the text field from the load list prefab (`UIFactory._prefabs.GetUIPrefab(5)._saveNameInputField`,
  no popup flash). Widgets need the title to have been seen once (always true: the game starts there).
- Text look (owner 2026-10-24: "not the same as the rest of the menus"; replaces the plain-material rule of 2026-10-13):
  body text, buttons, toggles, value rows = the language's body font + LanguageSetting.GetMaterial(EasyRead, SoftOutline16)
  ("Lora-Regular SDF OutLine16", as the game's title column, Options and Load Campaign); DSL_TXT_TITLE under 24 = the title
  font + its SoftOutline16 (section heads, as Options' "Language"); titles of 24 and up and text fields = the font's own
  plain material (as the Options title and the game's name fields). Measured with ds_custommodes home/test/font_probe.
- `parent == NULL` = the kit's own screen layer: overlay canvas, sorting 30900 (under MP panels 31000+), scaler copied from
  the game UI (ScaleWithScreenSize 1920x1080). The notice view is raised to 32040, the tooltip canvas to 32050 when shown.
- Title buttons: clone of the stock Credits button, label size 22 (as MP's clones), always the LAST children of the stock
  column, sorted by `order`; shown only while the stock buttons show (MP's pages hide the stock column, ours follow).
  Re-created when the title view is new. Placement: below Quit, below MP's "Multiplayer" (MP inserts after Load).
- Test-only exports (used by the uikit-demo module only): `dsl_ui_test_rect / _label / _title / _notice_button / _type /
  _hover / _tip`.

Test: `python3 test/uikit_test.py [out] [--no-build] [--run=1|2]` on copy L (lease first). Builds with
`EXTRA_CFLAGS=src/dsl_ui.c` until build.sh lists it. Test module `test/mods_src/uikit-demo` (title button "UI Kit Demo" ->
one panel with every widget). Input: a fake mouse in `UnityEngine.Input` (GetMouseButton/Down/Up, mousePosition) and
`BaseInput.mouseScrollDelta` (Unity reads the pointer from the global cursor, which a test must never move), the
EventSystem told it has focus (StandaloneInputModule drops events without focus), Enter as a real key NSEvent, typing via
`TMP_InputField.ProcessEvent`. Clipboard to a file (demo hook without MP, MP's DSMP_TEST hook with MP).
Traps found: (1) without MP the copy's persistentDataPath is `home/Library/Application Support/com.CanOpener.Dungeon-Settlers.mpL`
whose UserSetting.json was borderless full screen: the window covered the display at 18:59 and the guard killed it (about 1 s
visible on the owner's screen); the test now seeds that folder with the windowed 960x540 settings and asserts it.
Result 2026-10-15 19:21 (`~/ds_launcher/loader/test_env/uikit_out/final2/summary.json`): 67/67 pass, 0 guard violations,
0 "refused" lines in both dsload.logs. Run 1 kit alone, run 2 with multiplayer: MP's "Multiplayer" clone opens its page
(our button hides), MP "Back" returns, all widgets work, notice OK goes through MP's OnClick_Confirm hook. Frames in
`run1/drv/cap`, `run2/drv/cap`.
Open: the toggle's empty box is faint when off (owner to judge the look); in-world use not tested (title only);
Windows untested (names only, should port).

## L1 loader core (branch l1-core, 2026-10-15) - MASTER_PLAN 5.2 L1

### What is built
- SDK API 3 (`sdk/dsl_sdk.h`, fixed 18:40, told l2modswin + l3win): soft_restart, set_session_busy / session_busy,
  menu_add_tab / menu_open, title_add_button, loadrow_add / loadlist_filter / loadlist_refresh, `ui` (DSLUi kit), data_dir;
  `dsl_*` inline wrappers check `host->size` (API 1-2 modules load unchanged: host tests 31/31).
- `src/dsl_menu.h` = L1/L2 contract (tabs, kit, installed mods, groups, Apply, world mods; L2's `dsl_win_*` are weak).
- `src/dsl_ui.c` kit (helper agent, 67/67 on copy L), `src/dsl_reg.c` (registries, Apply = `<data>/Mods/pending.json` +
  soft restart, B copies it to active.json once on screen; groups.json; worlds.json; flow K), `src/dsl_rows.c` (Load list
  rows + filters + New Campaign line, by POLLING the open SaveLoadView: multiplayer owns the three row hooks until it uses
  this API; the engine allows one owner per site). The filter only re-shows rows it hid itself.
- Mods per world: key = sorted gameplay ids joined by "+" ("vanilla"). A world gets the running key the first time it is
  open; Apply inside a world gives that world the new key (else the restart's own reload would be refused).
- Flow K: `dsl_world.c` refuses, then "This world uses: <names>. Load it with them?" [Load] = soft restart with the world's
  set + `DSL_SOFT_LOAD=<save>` (now allowed from a world too: the stock Load also discards unsaved play).
- Pinned build: `mp_pin.txt` (or `MP_PIN=`) = one ds_multiplayer commit, exported with `git archive` to build/pin/<sha>;
  source list read from that tree's `src/build.sh`; det_ranges.tsv from `~/ds_multiplayer/build` (game-derived, never in
  git). `custom/build_module.sh` of the pinned tree is run when it exists (M4). vendor/mp untracked, moved to loader/old/.
- App: no forced "modmenu" module (the Mods window is in the loader); default active.json = multiplayer; the app test
  never quarantines a copy. app/test/run.sh 0 failures (first copy 7.4 s).

### Proof (copy S, lease AB, windows on a stage far off every display, guard: 0 violations in every run)
| Test | Result |
|---|---|
| `test/l1_test.py` (run5, MP from pinned main 6e65e66, 150 hooks, none refused) | 15/15: title "Mods"; Load list "Mods: ... Change"; flow K 15.0 s request -> on screen, B runs gcd-cdr+multiplayer, pending -> active.json; flow J in a world 14.6 s, world re-registered, active.json multiplayer |
| `test/l1_rows_test.py` (rows3, test module rows-demo) | 8/8: module API 3 calls, tab, filter hides / toggle shows, row text, Apply refused while busy |
| `test/run_tests.sh` host | 31/31 |
Frames: `~/ds_launcher/loader/test_env/l1_out/{run5,rows3}/*.png`.

### Open
- No Mods window yet (L2): the title button and "Change" log only. Multiplayer does not call set_session_busy yet (M2/M4).
- Steam with two game processes during a restart: still not tested (softrestart notes). Full-screen Space swap not handled.
- loader/build/*.dylib are tracked in this public repo from before; I did not commit new binaries.

## v13 loaderui (branch loaderui, 2026-10-16) - PLAN_V13 WP LUI, owner feedback v12 items 1-3, 6, 7, 9, 10, 14
Helpers (opus): soft restart work in `test/soft/NOTES_v13.md` (branch loaderui-soft, merged here); notice cause in
`../app/stub/NOTES_bg.md` (branch loaderui-bg, merged here; its patch for dsl_soft_mac.c applied by hand).

### What changed
- Title (dsl_reg.c, dsl_ui.c): "Mods" has no hover card. The loader's title buttons sit right below the multiplayer
  module's "Multiplayer" button, by order (Mods 100, then other modules' buttons such as "Custom Modes"); without
  "Multiplayer" right below the stock "Load Campaign" button, the same place (2026-10-20, branch modspos: they were last
  in the column, below Quit; test `test/l1_titlepos_test.py`, 4 cases: all on, multiplayer off, both off, Units
  Laboratory off).
- Hover cards (dsl_ui.c): the card's top-left = the panel's right edge (+8 px), level with the element's top. Panel =
  the widest ancestor narrower than 3/4 of the screen (title column, Mods window, Load list). After layout, a card that
  leaves the screen on the right or was pushed back over the panel by the stock clamp moves outside the panel's left edge.
  2026-10-25 (owner: the filter card covered the filter panel, "barely usable"): that move never happened. The stock card
  places itself 2 frames after the show (`Tooltip_Base.DelayedAlignment`: parked, layout, 2 frames, `SetTooltipAlignment`,
  `_alignmentRoutine` = null; no room on the right -> flipped left of the position = over the panel), and a second
  `ShowTooltipAtScreenPosition` with the same id is ignored (`TooltipUI._tooltipHolder` has it). Now the kit waits for
  `_alignmentRoutine` = null and MOVES the card (Transform.position) right before the UI is drawn (h_cur_perform), so the
  stock place never shows. Sides in order: right, left, below the panel (its left edge), above; a side counts when the
  card is inside the screen there and covers neither the panel nor any ancestor narrower than 3/4 of the screen (the Load
  window around the filter panel); no free side = the one that covers least. `dsl_ui_tip_below(target)` (the filter
  boxes): below first, then the free side nearest the box. Tests: `l1_filter_test.py` runs `tips` (5 sizes / UI scales /
  languages) and `tiplangs` (10 languages), old placement 12 overlap FAILs, new 104/104 + 204/204; `tip_sweep_test.py`
  (every card of the title, Mods window, a mod's settings, a world's mods window, the Load list). At 4:3 with UI 1.4 the
  big cards have no free side (Mods window, Load list rows): they cover the least (measured 20 px of the Mods window's
  top; the Load window's right 111 px, never the filter panel). Load list row cards still sit right of the Load window,
  over the filter panel while the row is hovered (not changed: not asked).
- World rows (dsl_rows.c): right half of the world row: "Mods: <short names> · ... +N" (as many as fit), "Change" at the
  end; the hover card "Mods of this world" lists every mod, one per line. Short name = mod.json "short_name" (new,
  optional), else the name cut at a word before 22 characters. The left half keeps the world name + multiplayer tag.
- Apply only at the title: dsl_soft.c refuses scene 2; dsl_reg.c refuses Apply outside the title ("Go back to the title
  screen to change mods."); the old in-world worlds.json rewrite is gone. Mods window in a world: switches, groups and
  "Save as a group" disabled, Apply / Undo hidden, status "Go back to the title screen to change mods.". Load list in a
  world: no "Change". dsl_world.c: a world with other mods loaded from the in-world Load list is refused with the game's
  own notice "This world uses other mods. Go back to the title screen to load it." (no "Load it with them?"). Flow K at
  the title is unchanged.
- Prewarm (dsl_reg.c + ui/dsl_win.c): every switch / group / Undo change reports the switches (dsl_mods_draft); 1.5 s after
  the last change the set is prepared exactly like an Apply (pending.json + env lines) and dsl_soft_prewarm starts a
  hidden game; switches back to the running set or the window closed without Apply stop it; Apply uses it when the set
  is the same. Not for a set that must download BepInEx or prepare it the first time. DSL_PREWARM=0 turns it off.
  Default ON (MORNING_QUESTIONS v13 loaderui).
- Mods window: only the Mods tab (owner #14: nothing but mods); module tabs stay registered but are not shown (log line).
  "MelonLoader" mark for mod kind "melon", like the "BepInEx" mark.
- Kit (dsl_ui.c): the widget templates are taken at the first title seen, even hidden. Before, a B of flow K (title ->
  straight into a world) could not build any kit widget in that world (Mods window did not open). Found by l1_v13_test.
- "App Running in Background" (macOS 27): cause = B posix_spawned inside A's launchd job; B's AutoFill XPC helper is
  booked on A's app; when A quits, A's Dock tile "runs in the background" and ~30 s later macOS posts the notice (owner's
  log 2026-10-15 23:57:41, 33 s after his first Apply; not at app start). Fix: dsl_soft_mac.c starts B through
  LaunchServices (NSWorkspace openApplicationAtURL, new instance, no activation, same env + args; posix_spawn fallback),
  dsl_start_mac.c chdir to DSL_SOFT_CWD. Proof: toy apps (before: notice; posix_spawn + disclaim: notice; LaunchServices:
  none). A game test copy cannot show it (LSUIElement rule). Check on a player copy: `log show --predicate 'process ==
  "Dock" AND category == "tile"'` must show no "Tile started running in background" at a soft restart.
- Test-only (DSL_SOFT_TEST=1): reg requests "title", "hover" (title:<label> | row:<i> | win:<control>; logs card + panel
  rects), "draft". A hidden B never takes the test request files (A answers them).

### Proof (copy S hidden + muted + staged, leases loaderui, 0 guard violations in every run)
| Test | Result |
|---|---|
| `test/run_tests.sh` host | 31/31 |
| `start/win/build_win.sh` (read only) | baselib.dll + starter + kit built (one old warning) |
| `test/l1_v13_test.py` runs 2, 3, 4 | 17/17 each. Prewarmed Apply 0.83 / 0.83 / 0.82 s, flow K 15.31 / 14.51 / 14.74 s, prewarm start 1.74 / 1.74 / 1.73 s after the switch. Run 2: Multiplayer index 3, Mods 4; no card on Mods; Mods window card x 749-901 vs window x 220-740; row line "Mods: Global cooldown uses..." card x 658-810 vs list panel x 311-649 (960x540 frame); prewarm starts 1.74 s after the switch, B ready 6.3 s later, prewarmed Apply 0.83 s (B shown 0.767 s after Apply in A's log); flow K 15.31 s; in the world: window read-only, Apply refused, no new process, other-mods load refused with no question |
| `test/l1_test.py` | 13/13 (flow J in a world replaced by "Apply refused in a world") |
| `test/l1_rows_test.py` | 8/8 |
| `test/uikit_test.py` | run 1 66/67 (only fail = the old "demo button last" rule); expectation updated (Mods then demo right below Multiplayer): run 2 35/35 |
Shots: `~/ds_launcher/loader/test_env/l1_out/v13_run2/*.png`. Soft restart timings (3 runs each, cold + prewarmed): `test/soft/NOTES_v13.md`.

### Open
- Dock: B is now started by LaunchServices. A player copy has no LSUIElement, so B may show a Dock icon for a moment
  before it turns itself into a UIElement (with posix_spawn this was 17 ms). Not testable here (test copies must have
  LSUIElement). With prewarm, B starts while the player is in the Mods window.
- Multiplayer module ctl.sock: a prewarm B unlinks and re-binds `<DSRIG_DIR>/ctl.sock` (MP src, lane mpfix told via
  orchestrator13).
- Steam with a second process (prewarm, up to 30 min): not tested.

## Mods per world (owner v13 feedback #4, #5; branch worldmods, 2026-10-16)
- Data was already per world (worlds.json key = the save header's CampaignGuid). The break was the display: Load list
  "Change" passed the save FILE name, so the Mods window read "Mods for ElvenExpedition__AutoSave_<hash>_0" and overflowed.
  Now: the world's name as its row shows it (rich text and the multiplayer tag removed), cut at 30 characters.
- worlds.json keys that name a save file (old format) move to their world's guid at start; two entries of one world: a
  guid entry wins, else the newer save file.
- Hover card (#5): it listed only the world's gameplay set; Multiplayer (left out of the key on purpose) was missing.
  Owner's data: Elven Expedition = gcd-cdr + Multiplayer; Moonlight Brigade (no saved set) = the running set, same two;
  Custom Modes off. Now the card lists every mod on when the world plays (its set + multiplayer / client mods that are
  on), one per line; the row shows short names (cut near 16 characters) and "+N".
- Proof `test/l1_world_test.py` (copy S, run world3): 14/14, 0 guard violations. Per-save entry -> guid; row
  "Mods: Global cooldown  +1", card "Global cooldown uses cooldown reduction" / "Multiplayer"; heading "Mods for Elven
  Expedition"; "Save for this world" -> world set; set unchanged after an Apply (soft restart); the autosave loads with
  it (flow K); a second save of the world written in game (CampaignSaveLoader.Save; this seed world is Ironman, so the
  game takes only the autosave kind) has the same world and the same set.

## Test copies never full screen (owner v13.3; branch testwin, 2026-10-16)
- Owner: a test copy that opened full screen switched his screen / Space (the installed app may stay as it is).
  mpZ (a test copy domain, update / crossmods tests) read FullScreenWindow at 3456x2234.
- dsload (start/mac/dsl_start_mac.c): an app with LSBackgroundOnly (every test copy, never a player's copy) gets its own
  preferences domain set windowed 960x540 before Unity reads it, and stops at once if it still says full screen;
  DSL_NOTCH=0 unless set. The owner's domains (com.CanOpener.Dungeon-Settlers, .Modded) are never written.
- lrig.py: require_windowed() refuses a start whose prefs still say full screen; DSL_NOTCH=0 by default (notch_test
  sets 1). l1_test.start_a the same. MP rig/rig.py: the same refusal (ds_multiplayer branch testwin).
- Test stage (dsl_soft_mac.c): staged windows are transparent, ignore the mouse and stay out of Spaces / Mission
  Control at all times, so a move the stage does not catch (2 guard kills today: 20:29 at an apply, 20:48 at a quit;
  cause not found, likely the system placing an off-display window back on a display) shows nothing.
- The soft-restart B takes A's live window mode (handoff "fsmode").
- Proof `test/l1_screen_test.py` (screen3): 6/6, 0 guard violations: full-screen prefs -> refused; with the python check
  skipped dsload sets 960x540 windowed, DSL_NOTCH=0; handoff fsmode; owner's domains byte-identical. l1_v13_test 18/18
  with the transparent stage (shots still render).

## Soft restart progress bar (owner v13.3; branch softprogress, 2026-10-16)
- Designs "Reload" B (bottom strip on the game's NOW LOADING screen, Apply at the title) and A (centered framed panel,
  flow K into a world). Drawn by `ui/dsl_progress.c` with the kit (game fonts), above the loading screen (own canvas 32100).
- Real steps only (src/dsl_soft.c): A: "Starting the game with the new mods" (cold) / "Switching to the new mods"
  (prewarmed, 95 % when ready); B writes <handoff>.progress at each step: "Loading <mods>" (modules loaded), the game's
  boot coroutines (LoadSheets "Loading game data", LoadFont "Loading fonts", LoadCursor/LoadTiles, LoadUIPrefabs "Loading
  menus"), "Opening the title screen", flow K "Opening your world" with the game's own LoadingUI percent (50-95 %),
  "Done". Flow K: the boot is the first half. A reads the file every 0.1 s; never backwards.
- The boot coroutine hooks are now installed in every B (logging still only with DSL_SOFT_BOOTLOG); MainFlow.Start only
  with the log. Kit sprite scan also at most once a second (a hidden B is uncapped: it scanned 30 times in its boot).
- Proof `test/l1_progress_test.py` (copy S, prog3): 8/8, 0 guard violations; cold Apply 6.42 s, flow K 14.57 s (as before);
  shots `~/ds_launcher/loader/test_env/l1_out/prog3/shot_{1_strip_low,2_strip_mid,3_panel_world}.png`.
  Windows (DSMP bottle, w_pkg_test run, -nographics so no picture): dsload.log "progress: bottom strip shown", 0 -> 10
  (mods) -> 20 -> 30 % until the test stops.
- Owner change: Apply at the title shows design B (strip) or C (ornament bar: title, thin bar between two copper
  diamonds, "Step n of 7 · <step>", a plate in the loading screen's colour over its tip) at random: the parity of the
  apply second, read by A and by B from the handoff, so both draw the same. Test-only DSL_PROGRESS_DESIGN=strip|ornament.
  Proof: l1_progress_test DESIGN=strip 8/8, DESIGN=ornament 8/8 (prog_ornament2); shots in
  ~/ds_launcher/loader/test_env/l1_out/prog_strip/ and prog_ornament2/ (shot_*_low, shot_*_mid, shot_3_panel_world).
- Trap: with DSL_SOFT_CAPTURE on, one run (prog1) had A's staged window moved on screen at the apply (guard kill in
  0.5 s). Not seen without the capture (prog2, prog3); the test takes its pictures with the "shot" request instead.

## Game saves in the modded game (owner 2026-10-16 / 10-17; branch savesync, replaces "Import a save")
- Owner: "make sure that the modlauncher connects to my regular saves ... so i can move forward on my saves in the
  modloader"; the import button "doesn't fulfill the function I wanted". Now the Load list of the modded game shows the
  game's own save folder (`DSML_GAME_SAVES`, set by both starters) next to the modded saves, and a game world is played
  IN PLACE: loaded from and saved back to the game's folder. No copy. `src/dsl_saves.c`.
- Hooks by name (Mac + Windows), all in `Refactor.SaveLoadHelper` (decompile 25846389: every reader, writer and delete
  of `CampaignSaveLoader` and the header cache goes through them): `GetAllSavePaths` (modded files + game files),
  `GetCampaignSavePath` (game folder for a game save), `SaveFile` (a new save name of a game world: game folder; the
  world guid comes from the autosave name, the header, or the load seen by `dsl_world.c`), `TryQuarantineCorruptSaveFile`
  (never moves a game file). Hooks: all or none.
- Modded-only worlds (multiplayer, started in the modded game) stay in `<DSML_DATA>/Saves`: hidden from the normal game,
  Steam Cloud backup by `dsl_cloud.c` as before. The starters no longer copy `Saves` at the first start (settings only).
- Old first-start copies (a modded save with the name of a game save): moved to `<DSML_DATA>/Mods/save_copies/`
  (never deleted); different bytes = one notice at the title with the folder. The game's save is the one listed.
- Load list (`dsl_rows.c` groups_apply): two groups with a gold title-font header each, "Game saves: also in Dungeon
  Settlers" first, then "Modded saves: only in Dungeon Settlers Modded" (rows moved with Transform.SetSiblingIndex in the
  list's own layout). No game save listed = the list as before.
- Warning: a game save loaded while a mod with mod.json `"save_dependent": true` runs (data check 2026-10-17: only
  hell-difficulty, combat difficulty 5 + new table rows in the save; gcd-cdr, multiplayer, custom-modes write nothing the
  stock game cannot read) asks first in the game's notice box: Load / Cancel. Hooked at `EventQueue.AddEvent
  (LoadCampaignRequested)` (no loading screen yet); `dsl_world.c` holds loads that did not come that way. Asked once per
  world and mod set: worlds.json `"save_warned": {guid: "a+b"}` (`dsl_reg.c`). A world registered with another set is
  left to the world check (flow K) first.
- Backup (owner 2026-10-18, ZET-U; branch zetu-saves): opt-in per mod, mod.json `"save_backup": true` next to
  `"save_dependent": true` (`dsl_running_save_backup`, `dsl_reg.c`; Hell difficulty has no backup and keeps its text).
  With such a mod running, the warning is a shorter text that ends with the backup folder (the stock box is a fixed
  520 x 109: the old text + a path ran over its buttons in en/de/ru). Its Load first makes byte copies (read back and
  compared, file times kept) of the loaded save + every other game save with the same world guid into
  `<DSML_DATA>/Mods/save_backups/<YYYY-MM-DD_HHMM>_<clan name, file-safe>/` (the folder named in the question; `_2`.. when
  it exists; never inside game_d or mod_d: the game lists Saves recursively). Copies, never a move or delete. A copy
  that fails = NOT loaded, not marked answered (asked again), a one-button notice with the reason 3 frames later.
  Asked once per world and mod set, so copied once. Rig test `test/l1_savebackup_test.py` (copy W; LANGTYPE=3/4/7 for
  the box in de/ru/ja).
- Delete (owner 2026-10-17, branch savesync2): a game save is the normal game's file too. After the game's own delete
  confirm, a game world (`SaveLoadView.CompleteDeleteConfirmation`, the world's delete button) or a game save
  (its `RemoveFileRequested` event at `EventQueue.AddEvent`) asks once more in the game's notice box: "This save is also
  used by Dungeon Settlers without mods. Delete it there too?" Delete / Cancel (Cancel = the view's own "not confirmed",
  nothing deleted).
  Fix 2026-10-20 (branch fixfoc-del; a Windows player: "Remove File" in Host a world did not delete as expected): the
  old hook was on `SaveLoadView.ConfirmRemove`, but build 25846389 INLINES it into the stock box's Yes callback
  (`<>c__DisplayClass57_0.<CheckRemoveSlot>b__0` builds UIInput SaveLoadUI_RemoveCampaign itself; Mac disassembly at
  0x862040), so the question never showed on the player's path and the old rig test passed only because it called
  ConfirmRemove directly. Every remove reaches the game as `RemoveFileRequested(name)` through `IEventQueue.AddEvent`
  (`InputDispatcher.<Update>b__8_0`, an interface call: not inlinable) -> `InputEventHandler.OnEvent(RemoveFileRequested)`
  -> `CampaignSaveLoader.Delete`. A game save's event is held (strong GC handle) and the question shows 3 frames later
  from the frame tick (the kit reuses the game's CommonNoticeView that just closed); Delete puts the held event back in
  the queue, Cancel drops it. Rig test `test/l1_savedel_test.py` clicks the save row + Remove File and the game's own box
  (CommonNoticeView.OnClick_*) in Load Campaign and in Multiplayer > Host a world.
  Modded-only saves: the stock flow. Build 25846389 has the world's delete OFF (`CampaignSaveDeletionService.IsAvailable`
  returns false: no world delete button), so the save's remove is the delete players see; the world hooks wait for a
  build that turns it on (untested in the game until then). Before this, a game world's delete removed nothing: the stock
  `CampaignSaveDeletionService` lists one folder (`GetCampaignSavePath(<name>)`'s); `DeleteCampaignSaves` of a game
  world now routes that one lookup to the game folder. Autosave pruning (`CleanupAutoSaves`, `KeepOnlyRetained...`)
  is not asked (the normal game prunes the same way).
- Set-aside notice: per world, which file is newer (file times) and where the copy is (one copy: its file; more: the
  folder). The kit's box holds 1 KB: lines past that become "(and N more)".
- Test requests (DSL_SOFT_TEST): `<log dir>/softrestart/saves_request.json` `{"cmd":"del_world","guid":..}` /
  `{"cmd":"del_save","name":..}` (the game's own box answered Yes on the open Load list); answers: warn_load / warn_cancel.
- Removed: the title button "Import a save", its code and texts (14 keys pruned from lang/). Kept: the cloud backup.
- Multiplayer temporary saves (bug 2026-10-18, Windows VM): names `mp_*` (mp_sync join point, mp_fullcheck full-state
  check, test saves) are never game saves: not listed from the game folder, never added as a new game save name by
  `SaveFile`, so never routed there. Before, a guest whose game save had the host world's guid wrote mp_fullcheck.json
  (4.4 MB) into the game's Saves folder (Steam Cloud synced); the mod read it from `<DSML_DATA>/Saves`, logged "save
  failed" and never deleted it.
  The Steam Cloud backup (`dsl_cloud.c`) uses the same rule (`dsl_save_is_mp_temp`, dsl_internal.h): mp_* saves are
  never uploaded or downloaded. Before 0.1.15 the backup uploaded them (Windows VM: dsmodded_mp_sync.json queued on a
  join, plus a test world), so a pass now deletes any remote `dsmodded_mp_*` file it lists (FileDelete, only that
  prefix; log line "cloud: removed dsmodded_mp_... from the cloud"). Host test: `loader/test/cloud/run.sh` section 14.
- Packaging: `modules/scan_saves.py` fails a build when a save or player file is inside (folders Saves / profiles /
  instances ..., player file names, any .json with "CampaignSaveHeader"): `package_mods.sh`, `app/build_app.sh`,
  `start/win/build_win.sh` (kit), `start/win/installer/make_installer.sh`. The released packages checked: none has one.

- Tutorial progress (owner 2026-10-20, v0.1.20 test: advanced players got every tutorial notice again). The game keeps it
  in `OutGame.json` of its data folder, `TutorialSaveData` = `OccurredConditions` + `CompletedTutorials`
  (`Refactor.Main.TutorialSaveData`, read once into `TutorialContainer`; `AlertEvaluator.IsTutorialAlreadySeen` takes
  `ALERT_Tutorial_<id>` -> `tutorial.complete.<id>`). The starters copy that file on the FIRST start only. Now
  `dsl_saves_tutorial_import` (dsl_core_start, before GameAssembly, any build) adds on every start each key the normal
  game's file (`dirname(DSML_GAME_SAVES)/OutGame.json`: Mac `~/Library/Application Support/CanOpener/Dungeon Settlers`,
  Windows `%USERPROFILE%\AppData\LocalLow\CanOpener\Dungeon Settlers`) has and `<DSML_DATA>/OutGame.json` lacks. Only
  adds; never writes the normal game's file; an unreadable modded file is left alone; a missing one = a copy. Tests:
  `run_tests.sh` T8 (host), `l1_tutorial_test.py` (hidden copy, a PvP base world made by a test, never an owner save).

## Hidden worlds: a count, never a silent gap (owner 2026-10-18; branch hiddenworlds)
- Owner: a player whose worlds are not in the Load list must see that they are hidden, not lost: a line with how many
  and why. Every case where worlds are on disk but not listed:
  - Load list, mods on (`dsl_rows.c hidden_apply`, one wrapped text line, rebuilt with the list). Top, where the game
    saves would be: the game-saves hooks off this session (`dsl_saves.c`: names missing / a hook refused;
    `DSL_TEST_SAVES_OFF=1` in tests) "N of your game worlds are not listed ..."; a game save the game could not read
    (`TryQuarantineCorruptSaveFile` keeps it in the game folder, so it is not listed) "N game saves could not be read
    ...". Bottom: world rows a filter hides. Kind from the module that hides them: a module may export
    `int dsl_loadlist_hidden(int *kind)` (dsl_sdk.h; multiplayer's list filter, ds_multiplayer branch hiddenworlds):
    "N multiplayer worlds are hidden. Tick the box under the list to show them." (single-player on Your save); the
    loader's own filter toggles, or a module without the export: "N worlds are hidden by a filter under the list."
  - Mods off this session (unknown game build, launcher pin of another build, mapped image differs, the Windows
    save-folder redirect failed, the crash guard): no hook is written, so no line in the list. The start notice
    ("Dungeon Settlers: mods are off", and dsload_status.json "message") gets the count: Windows (no redirect, the game
    lists its own folder) "N modded worlds are not in the Load list this session. They are not lost ..."; Mac copy (the
    modded folder is listed) "N of your game worlds are not in the Load list ...". The crash guard had no notice: it now
    shows one when worlds are hidden.
  - Counted per world (distinct CampaignGuid: autosave name, else the header), `mp_*` temporary saves and files with no
    readable header left out, worlds listed from the other folder left out (`dsl_saves_hidden_worlds`).
  - Not a hidden world: a modded file named like a game save (the game's save of that world is listed); `mp_*` files.
    Not reachable: the normal game (no loader) never lists modded-only worlds; the group header "Modded saves: only in
    Dungeon Settlers Modded" says so in the modded game.
- Filter strip (bug 2026-10-20, v0.1.15-0.1.17): the loader's strip for its own filter toggles (`dsl_rows.c toggles_build`)
  was built for ANY registered filter and drawn with rgba 0 (= the kit's default tint: the game's dark Box panel).
  custom-modes registers a filter with no label (its test worlds), so with Units Laboratory on, every Load list (Load
  Campaign, Host a world / Your save, Esc Load) could show an empty dark bar on top of the multiplayer box "Show
  single-player worlds" / "Show multiplayer worlds": the hidden-worlds line said "tick the box" and no box was seen.
  Now the strip exists only while one of our toggles has a label, and it is see-through (alpha 0).
  `l1_hidden_test.py` M runs with custom-modes and checks the box's pixels in the frame (fails on the old loader).
- Tests: `test/saves_hidden/run.sh` (host; the Windows and Mac branches of the notice count: 3 checks);
  `test/l1_hidden_test.py` (copy Y, hidden): M multiplayer filter + unreadable game save, EN and DE, box ticked = line
  gone; F hooks off; B other build (status message); G crash guard: 19 + 2 checks, screenshots M_en / M_de / F_en / F_de.
## Load list filter panel (owner 2026-10-24, kit v0.1.23, branch feat/load-filters) - `src/dsl_rows.c`
Owner: "add filters for worlds, on the right of the load campaign window ... vanilla & vanilla+ worlds (with changes not
modifying gameplay), modded, & multiplayer worlds. Also make a toggle to show multiple saves or only the latest one."
- Panel (kit widgets, a child of the Load window `Panel_SaveLoadUI`): title "Filters", boxes Vanilla & Vanilla+, Modded,
  Multiplayer, then Latest save only; each with a hover card. Place from the screen each poll: right of the window, top
  level with the list's top (`ScrollView_SettingData`), below the title corner when they would overlap
  (`dsl_update_corner_rect`: Auto-update switch, version, status line, buttons), inside the screen; too narrow = scaled
  down (min 0.5; measured 0.89 at 16:10 UI 1.4). Measured fits: 960x540 UI 0.75 / 1.4, 960x600 UI 1.0 / 1.4.
- Kinds, one per world: Multiplayer = the multiplayer mod's tag (its export `dsl_loadlist_world_mp`), else Modded = a mod
  of the world's set with scope gameplay or not installed (`dsl_world_is_modded`, dsl_reg.c), else Vanilla & Vanilla+.
  Fixes (mod.json `"category": "fix"`, kit v0.1.26) never count: a world with only fixes is Vanilla & Vanilla+.
  A world shows when its kind's box is ticked; the world being played always shows. Defaults = the old list: Vanilla &
  Modded on, Multiplayer off. Choices in `<data>/Mods/loadlist.json` (vanilla, modded, multiplayer, latest_only).
- The multiplayer mod's box "Show multiplayer worlds" under the single-player list is replaced: with a loader that
  exports `dsl_loadlist_panel`, multiplayer 0.1.22 shows every world there and draws no box (its filter stays for Your
  save and the Expedition pick, which export `dsl_loadlist_own` = 1: the panel is hidden then). Its box was off at every
  opening, so there was no saved value to carry over. Old multiplayer + new loader: the panel has no Multiplayer box and
  the old box stays; new multiplayer + old loader: no box, every world shows.
- Latest save only: per open world row, save rows after the first off (the game lists a world's saves newest first:
  `SaveLoadView.<RefreshLoadPanel>b__31_1` orders by save time), and `SubUI_SettlerNameCase._buttonCount` = 1 + its
  `RefreshSize` so the row leaves no empty band (RefreshSize turns the save rows on: it runs first). Checked every frame
  while on (a world the player opens shows its older saves for one frame at most). Off: the stock count back.
- Hidden line: "N worlds are hidden by the filters on the right." (rows the panel hid); the toggles under the list keep
  "by a filter under the list"; the multiplayer mod's own modes keep their lines.
- Data guard (the one real break): Load and Remove File act on `SaveLoadPresenter._selectedSaveName` (set by a click on a
  save row), not on the view's selected row; `SaveLoadView.UnselectAllSlots` + `LockLoadButtons` change only the view. So:
  (1) a selected save that a filter or Latest save only hides is unselected and both buttons locked (each apply + each
  poll); (2) hooks on `SaveLoadPresenter.ClickRemoveButton` / `ClickLoadButton` (delegates of its update table, never
  inlined) refuse the click while `_selectedSaveName` is hidden by the filters (no box, no load; log "rows: Remove File
  refused"). Without (2) the first test run removed the hidden game save after the view was unselected.
- Test requests (DSL_SOFT_TEST, reg_request.json): `{"cmd":"lf", "vanilla"|"modded"|"multiplayer"|"latest_only": 0/1}`,
  `lf_select` (name), `lf_remove`, `lf_state`; each writes `<log dir>/softrestart/lf_state.json`.
- Tests: `test/l1_filter_test.py` (copy V): guard (filter + Latest save only, Remove File then Yes to every box: the
  hidden saves stay), kinds, defaults, MP box gone, Latest save only (newest shown, row count 1), restart keeps the
  choices, place at 4 sizes / UI scales in en/de/ru/ja: 50 + 28 checks. `test/l1_hidden_test.py` M ported to the panel.
- World row "Mods:" line (owner 2026-10-24, kit v0.1.25): it starts after the header's left part = the name text's own
  one-line width (the multiplayer mod writes its "Multiplayer" tag and "with <names>" into that text) and that mod's
  "Remove tag" link when it sits on the line's height (a row child whose text starts "<u>"); at least the row's middle as
  before. It is always one line: the most names that fit at the kit's smallest size (measured at 0.75, CJK widths do not
  scale exactly), then the first name cut with "…", then "Mods: +N"; the other modules' row texts only while they fit.
  No room at all: no line, the [Change] card lists the mods. Before: it ran over "with <names>", and a Russian name took
  3 lines out of the row. Test: l1_filter_test row_checks (text left of the line >= the left part's end, TMP rendered
  height one line), every language (LF_RUNS=langs): 99/99 with main.
- Copy signing trap: codesign refuses a test copy whose Frameworks holds `lang/` (not code); a copy whose Info.plist
  changed after its last signature is then killed at start (Code Signature Invalid, no log). l1_filter_test puts lang
  aside while it signs.

## Mod settings (v0.1.14, lane modsettings, 2026-10-18)
Owner: "add a way to adjust mod settings in the modloader". First user: ZET-U Mod Suite.
- mod.json `"settings"`: list of {id, type toggle | int (min, max, step) | choice (choices [{id, label, i18n}]), default,
  scope gameplay (default) | local, label, help, i18n {code: {label, help}}}. Label / help may also be {"en": .., "ja": ..}.
  Ids a-z 0-9 _ . - (bad entries are skipped with a log line). Schema comment: `sdk/dsl_sdk.h` above `settings_get_bool`.
- `src/dsl_settings.c` (pure C, host test `test/settings/run.sh`): values in `<data>/Mods/settings.json`
  {mod: {id: value}} (only values the player set; members of mods not installed now are kept).
  Gameplay values are frozen per process: the profile's `"settings"` member (written by Apply into pending.json, so a
  prewarm with other values is another set), else settings.json, else the default. B commits them to settings.json
  with pending.json -> active.json (`commit_pending`). Local values: settings.json, changed live, `settings_on_change`
  callbacks run on the main thread.
- SDK API 5 (DSLHost appended): settings_get_bool / _int / _choice (with a default for an older loader or an undeclared
  id), settings_on_change; wrappers dsl_get_bool / dsl_get_int / dsl_get_choice / dsl_on_setting_change. Kit: DSLUi.slider
  (clone of the Options window's SubUI_Slider, polled; NULL = use value_row). DSL_LOADER_VERSION 3.1.0.
- Multiplayer: `set_lines` adds "<mod>/<setting>@<value>" (on/off, number, choice id) for each gameplay setting of each
  running gameplay mod to modset_lines, so the HELLO hash differs when a value differs. ds_modcheck.h names the setting
  (label + values through the loader's exported `dsl_setting_text`). "Get the host's mods": `dsl_settings_take_lines`
  (src/dsl_menu.h) turns the host's lines into the drafts of the next Apply.
- Mods window: a Settings button on rows of mods with settings (not in the world-bound mode); the Settings page shares
  the bottom bar (status lists the gameplay drafts, Undo changes drops them, Apply applies switches + settings).
  Rig proof: `test/settings/ui_settings_test.py <out>` (copy T, lease, cap, hidden).
- Number field (branch numfield, 2026-10-20): an int setting with `"input": "number"` shows a typed field (label left,
  the kit's TMP_InputField clone right; `dsl_ui_field_number` sets TMP ContentType.IntegerNumber + a character limit)
  instead of the slider. A slider cannot pick an exact port (multiplayer `host_port`, 1024..65535). Explicit key, not
  automatic: every setting without it keeps its slider. The typed text is taken on Enter, when the field loses the
  keyboard, and before Apply / Back / close (`num_commit`); not a whole number or outside min..max = refused, the field
  shows the value again and the status line says "Type a whole number from %d to %d." (10 languages). Then it is saved
  like the slider (`dsl_settings_set`: local = settings.json at once; gameplay = a draft for Apply). Parse rule:
  `dsl_settings_parse` (pure C). Test driver: `{"op":"type","target":"set:<id>","text":"..","keys":true}` types through
  `TMP_InputField.ProcessEvent` (the field's own validation); `click` on the field = Enter.
  Rig proof: `test/settings/numfield_test.py <out>` (one copy, multiplayer kit: type, refuse, reopen, host on the port).

## Dependencies (v0.1.16, lane starter, 2026-10-18): `requires`, `optional`, `conflicts`
One rule, `src/dsl_deps.h` (header only), used by the loader core (`dsl_main.c`), the Mods window's Apply
(`dsl_reg.c` prepare), the Windows starter and soft restart (`start/win/bie_win.c` bie_prepare) and the Mac stub
(`app/stub/ds_modded.c` kind_dirs). Checked kinds: `code` and `bepinex`. Other kinds (`data` / `patch` / `builtin`,
`melon`) are never switched off by it, but count as mods others need.
```json
"requires":  ["Core>=0.5.0", "loader>=3.0.0"],
"optional":  ["Extras>=1.2"],
"conflicts": ["OldCore<0.5"]
```
- Entry: `"id"` or `"id<op><version>"`, op `>=`, `>`, `<=`, `<`, `=` (`==`), `!=`; dotted numbers. `loader` = the
  loader's internal API (`DSL_LOADER_VERSION`), in `requires` only.
- `requires`: the mod runs only when each named mod is installed, switched on, can run itself and its version fits.
- `optional` (new): the named mod may be missing or off; when it is on, its version must fit, else this mod does not
  run. A code module's optional mod that loads is loaded first (load order).
- `conflicts`: never runs with the named mod (when its version fits): the one later in the profile order is off; a mod
  the launcher provides (profile `provided`, any version) is never the one switched off.
- Repeated until stable: a mod switched off by the rule can switch off the mods that need it.
- Where the player sees it: the mod stays enabled in the profile (it runs as soon as its requirement is met), is not
  run, and the Mods window says why ("Not running: needs Core >= 0.5.0, but version 0.4.0 is installed"; 7 reasons,
  10 languages). Its BepInEx folder is not copied (Windows) / not in `BEPINEX_PLUGIN_DIRS` (Mac), and it is not in the
  mod set (`dsload_modset.json` "bepinex").
- The starters do not know which code modules fit the game build: they count an enabled code module as running. The
  loader decides again in the game with the build known (a BepInEx mod already started then keeps running).
- Host test: `test/run_tests.sh` (bie-core 0.4.0 too old for a BepInEx mod, a code mod, an optional user).

## Mod data folder + multiplayer sync (v0.1.16, lane moddata, 2026-10-18)
Owner: give each mod its own data folder that survives mod updates, and let multiplayer send it to guests on join.
First user: a BepInEx mod (Delvers) that keeps settings and campaign markers next to the saves.
- Folder: `<data>/ModData/<mod id>/`, `<data>` = the modded game's save folder = `Application.persistentDataPath`
  (Mac `<profile>/data`, Windows `ROOT\profiles\modded\data`). It is outside the mods folder: a mod update (the old
  mod folder is moved to a backup) and turning a mod off / on never touch it. Files a mod writes into its own mod folder
  (`module_dir`) are still lost on an update: use this folder instead.
- Native modules: SDK API 6, `DSLHost.mod_data_dir(self)` / wrapper `dsl_mod_data_dir(host, self)` (NULL from an
  older loader). DSL_LOADER_VERSION 3.2.0 (a mod that needs it: `"requires": ["loader>=3.2"]`).
- .NET / BepInEx / MelonLoader mods (no C SDK): environment variable `DSML_MOD_DATA` = `<data>/ModData` (set by the
  loader before any runtime starts), else `Path.Combine(Application.persistentDataPath, "ModData")`; then `<id>` =
  the id in the mod's mod.json. `<data>/ModData/moddata.json` (rewritten at each start and session change):
  `{"format": 1, "dir", "session": "none" | "guest", "mods": {"<id>": {"dir", "sync": [patterns]}}}`.
  C#: `var dir = Path.Combine(Environment.GetEnvironmentVariable("DSML_MOD_DATA") ?? Path.Combine(Application.persistentDataPath, "ModData"), "<id>");`
- mod.json `"sync": ["campaign/*.json", "markers/**"]`: files of that folder that are gameplay (every multiplayer
  player needs the same). Relative, `/` only; `*` and `?` inside one name, `**` = any number of folders. Names: A-Z a-z
  0-9 `. _ -` and inner spaces, not starting with `.`, no Windows device names (con, nul, com1 ...), no program files
  (.dll .exe .dylib .so .bat .cmd .sh .ps1 .js .py ...). Links are never followed. Only running gameplay-scope mods
  sync. Bad patterns are skipped with a log line.
- Multiplayer (block `MDAT` in the join point save, ds_multiplayer `src/mp_moddata.c`):
  host: every sync file of every such mod, at each join point (so files changed during play reach a later joiner).
  Limit per mod: 1 MB and 256 files; over = only the list is sent, the log says `over the limit`, and guests are
  refused ("the mod X has more data files than can be sent").
  guest: the loader checks the whole block first (mod runs here with "sync", every name safe AND inside this copy's
  own patterns, sha256 of each file, the list against the host's fingerprint). Then it puts its own matching files
  aside (`<data>/ModData/.mp_stash/<id>/`), writes the host's, reads its folder again and compares the data
  fingerprint (sha256 of the sorted `<id>/<path>@<sha256>` lines) with the host's. Any failure: the save does not load,
  the join ends with "Could not join: ..." (translated), the own files are back at once. Non-sync files of the folder
  (local settings) are never touched. Back on the title with no session, the files of the session go and the own
  files come back; a game that ended inside a session gets them back at its next start (before any mod loads). The
  marker `.mp_stash/session.json` (phase stashing / playing / back) makes each step safe to redo after a crash.
  Nothing outside ModData is ever written; nothing received is ever run.
- Fingerprint: the data lines are checked at the join (above), not in the HELLO mod-set hash: a guest's own files are
  expected to differ before the transfer, so a HELLO check would refuse every guest. The data fingerprint at start is
  also in `dsload_modset.json` `"data_fingerprint"` (`000...` = no sync file).
- A mod reads its sync files when a world loads (the guest's files change right before the host's world loads).
- Code: `src/dsl_moddata.c` (pure C). Exports for the multiplayer mod (dlsym; Windows: baselib.dll): `dsl_moddata_pack`
  / `_copy` (host), `dsl_moddata_apply` (guest; 0 ok, 1 too big, 2 bad block, 3 write failed, 4 differs after the copy),
  `dsl_moddata_session_end`.
- Tests: `test/moddata/run.sh` (host test: data kept when the mod folder is replaced, guest gets exactly the host's sync
  files and gets its own back after the session and after a crash, hostile paths / links / damaged / short / client-mod
  / over-limit blocks refused with the guest's files unchanged). `test/moddata/moddata_pair.py` (hidden Mac pair through
  the rig: host + guest with the test mod `test/moddata/demo`, files during and after the session, 30 s of play).
## Harmony + our MinHook on one function, Windows (lane mpapi_win, 2026-10-18) - `test/winharmony/`
Counterpart of `test/l1_bie_test.py`: BepInEx be.788 plugin `SameFnWin` (Harmony postfix on `Refactor.Main.MainFlow.Update`,
the multiplayer module's command-pump hook, rva 0xa3b1d0) + multiplayer, then the plugin off. The plugin also logs the
function's first bytes before / after Harmony, which engine patched first, where our MinHook trampoline returns, and the
`DOORSTOP_DISABLE` value it sees (`BepInEx\samefn_win.log`, written per line so a crash keeps it).
- Run on the real VM (lane that owns it): `./stage.sh` (KIT=<built kit> or MP_MOD=<multiplayer module folder>), then
  `python3 winharmony_test.py [out]` (guest side `wh_guest.ps1`; own test folder C:\dsm\winharmony\root, VMWARE.md rules).
- Order: ours is always first. dsload's hooks go in at GameAssembly mapping; Harmony patches in the plugin's Load, later.
  So our engine never sees Harmony's jump (no "refused" line); the question is whether Harmony's detour (Il2CppInterop,
  Dobby on Windows) survives a site that already starts with our `E9` jump.
  Dobby on Windows) survives a function we already hooked.
- Wine dry run (DSMP bottle, -nographics, the bottle's 0.1.13 loader; same MinHook engine) [V: one run]: site before =
  `e9 ..` (our jump), our trampoline copies 6 bytes and returns to site+6; Harmony's patch then CRASHED the game inside
  dobby.dll (access violation reading 0, dobby.dll+0x2b62, stack through coreclr). No chain. With the plugin off the
  next start was clean (Doorstop off, plugin folder moved out, MainFlow.Update hooked, frame counter moving).
  Not yet seen on real Windows: the VM run decides. Until then: a BepInEx plugin that Harmony-patches a function the
  multiplayer module hooks is expected to crash on Windows.
- Found on the way [V: Doorstop source, master entrypoint.c]: Doorstop sets `DOORSTOP_DISABLE=TRUE` inside the game
  process once its hooks are in. `dsl_reg.c` (Windows branch) reads that variable as "BepInEx is off", so with BepInEx
  running it logs "0 BepInEx plugin(s) running" (Wine run: yes) and the Mods window says the plugin did not start; the
  running mod-set key leaves the BepInEx mod out. Not fixed here; the VM test's "1 BepInEx plugin" check shows it.
  Real Windows VM (lane vmwin2, 2026-10-18 22:26): the same crash, `dobby.DLL+0x2b62` (write to 0), 3.6 min after start.
- Cause (lane harmonyfix, v0.1.16) [V: BepInEx be.788 decompiled, shipped dobby.dll disassembled, Dobby source]:
  BepInEx's `INativeDetour.Create` first runs `FollowExportThunks`: it follows up to 8 jumps `E9 rel32` / `FF 25
  [rip+disp32]` (meant for il2cpp export thunks). From MainFlow.Update it went site `E9` -> MinHook relay `FF 25` -> the
  dsload thunk, so Dobby patched our THUNK, not the site. The thunk block came from `VirtualAlloc(NULL)` (VM
  0x1d29def0000, Wine 0x62540000). Dobby x64 needs an 8-byte RW slot within +-2 GB of what it patches
  (`AllocIndirectStub`, for `jmp [rip+disp32]`); its search found none there, returned NULL, and the caller wrote
  through it (`dobby+0x2b62 = mov [rbx], rsi` after the "[!] Not found near forward stub" log). This is before Dobby
  reads any code: the `E9` at the site was not the cause. Mac: FollowExportThunks only knows x86 bytes, Dobby patches
  the site.
- Fix (`start/win/dsl_engine_win.c`): the thunk block is allocated next to GameAssembly (nearest free range below the
  image, then above its end, within 1 GB; dsload.log "engine: MinHook, 2048 thunks at ... (below GameAssembly)"), like
  the Mac engine's region. Dobby then reuses the slot page of BepInEx's own il2cpp_runtime_invoke detour or finds one as
  for game code. Order at run time: site -> relay -> thunk (Dobby's jump) -> Harmony -> Dobby's copy of the thunk ->
  ours -> the game. Our unhook (slot = 0) still works; Harmony keeps running.
- Reverse order (a BepInEx patch first, our hook second, e.g. a late hook): the engine no longer refuses a site that
  starts with `E9` or `FF 25` (Dobby's patch). MinHook copies that jump into our trampoline; the engine checks that the
  copy reaches the same target / slot (MinHook does not check a new disp32, and Dobby's slot can be 2 GB away) and else
  takes the hook back and refuses as before. Then ours runs first, Harmony second. `EB` and `mov rax; jmp rax` sites
  are still refused. A Harmony unpatch after that puts Dobby's saved bytes back over our jump: our hook stops firing
  there (no crash).
- `chaintest/chaintest.c` (run 0 of winharmony_test.py, no game): the engine + the pinned dobby.dll + BepInEx's
  follow logic on one function, both orders, unhook and re-arm.
- Found on the way [V: Doorstop source, master entrypoint.c]: Doorstop sets `DOORSTOP_DISABLE=TRUE` inside the game
  process once its hooks are in. `dsl_reg.c` (Windows branch) reads that variable as "BepInEx is off", so with BepInEx
  running it logs "0 BepInEx plugin(s) running" (Wine run: yes) and the Mods window says the plugin did not start; the
  running mod-set key leaves the BepInEx mod out. Fixed (harmonyfix, v0.1.16): `dsl_reg.c` reads "TRUE" (Doorstop's
  own mark, `windows/entrypoint.c` after its hooks are in) as running; only the starter's "1" (or any other value) means off.

## Launcher features vs game mods (owner 2026-10-20, branch featsplit)
Owner: Multiplayer and Units Laboratory start from the title (Multiplayer button, Units Laboratory button), never from a
world; they must not mix with the per-world mods.
- mod.json `"category": "feature" | "game"` (default "game"). No "category": the official `multiplayer` and
  `custom-modes` count as features (their released mod.json has none), everything else is a game mod.
- A feature is never in a world's key: `key_of`, `key_norm` (stored keys and `DSML_MODSET` compare without features),
  worlds.json keys are rewritten without them at start (`strip_mp`), flow K keeps the features that are on.
  So a world saved with or without Units Laboratory loads with no "This world uses other mods".
- Multiplayer: the HELLO mod-set lines (`set_lines`) are unchanged: a feature with `scope: gameplay` (Units Laboratory)
  is still in the hash, so every player needs it; `scope: client` features are not. The multiplayer mod stays out (its
  HELLO checks build + protocol). The notch fix is built into the Mac start part (always on, not in the Mods window).
- Mods window (owner 2026-10-20, branch modtabs): two tabs in the window's tab strip, "Game mods" (opens first) and
  "Launcher mods" (category feature), each listing only its mods with one line on how they work. One `mods[]` list
  holds both tabs (rows of the other tab are NULL), so a switch changed on one tab stays when the other opens, the status
  line lists every pending change, and Apply / Undo / groups act on both. A tab change keeps the switches like Settings
  / Refresh (`keep_switches` + rebuild). A tab with no mods says "No launcher mods installed." / "No game mods installed.".
  A world's Mods tab ([Change]) lists only game mods, with the old single "Mods" tab and no tabs of its own. World rows: the line names only game mods; the hover card names
  the features that are on in a block of their own.
- Proof: host test T1c (fails on the old code: world refused); rig `test/featsplit_test.py` 13/13 on copy S.

## Fixes (owner 2026-10-24, kit v0.1.26, branch feat/fixes-tab)
Owner: "some of the mods that we have can be considered not even mods but more game fix ... put them in a different
category like bug fixes"; Modded off in the Load list filters hid every world (each had Global cooldown fix on).
- mod.json `"category": "fix"` (absent = a game mod; `"feature"` = launcher mod, above). Fixes today: Global cooldown
  fix (gcd-cdr 0.1.7, scope gameplay) and Sound settings fix (sound-fix 0.1.1, scope client). No built-in id list: an
  older mod.json without the field is a game mod (pre-launch, no compatibility work).
- A fix is per world like a game mod (in a world's key if scope gameplay; in the HELLO lines as before), but the Load
  list filter counts it as Vanilla & Vanilla+ (`dsl_world_is_modded` skips it). Tooltips say so.
- Mods window, title: a third tab, order Game mods | Fixes | Launcher mods (`tab_of`, one `mods[]` list as before), with
  its own line on top. Each fix of scope gameplay has the box "On in all worlds" under its text (default ticked);
  ticking it also switches the fix on (an Apply change), so new worlds start with it. A fix of scope client has the
  line "The same in all worlds." (its switch is already one value for every world; it is not in any key).
- "On in all worlds": `<data>/Mods/fixes.json` `{"all_worlds": {"<id>": bool}}` (absent = ticked; written at once,
  temp + fsync + rename). Every world plays with a ticked fix, old saves and new ones, unless its own Change window turned
  it off: worlds.json `"fix_off": {guid: "<ids joined by +>"}` (a member of its own, so older loaders keep it and the
  multiplayer mod, which reads the first `"modset"`, never sees it). The set a world plays with = its stored key + the
  ticked fixes - its offs (`key_eff`); `world_key` returns it, and dsl_world.c's check asks `dsl_world_key_eff`, so the
  Load list label, the filter kind, the world check / flow K and the multiplayer set (the running set after flow K)
  agree. A fix that fails in this start (`active == -1`) is not added (else every load would ask "Load it with them?"),
  unless it is in the running key: after flow K that key is the set asked for (`DSML_MODSET`), failed fix included.
- Offs: a world's Change window (`dsl_world_set_mods`) records an off for a fix switched off there when the fix is on in
  all worlds or was on in that world (the world plays as the window showed it, also a world never opened yet, whose
  window shows the running set); an off stays while the fix stays off; switched on = no off. Offs of fixes not installed now are kept. A new world
  (first open, `world_frame`) gets an off for each ticked fix the running set lacks, so it keeps the set it was made with.
  Unticked = each world as before (stored key only; offs then change nothing).
- A world's Change window: no tabs; a "Fixes" title over its fixes of scope gameplay, then a "Mods" title over its game
  mods (a title only over a part with rows). Under the status line the toggle "Show only active mods" (default on,
  `<data>/Mods/modswin.json` `world_active_only`): on = only the mods switched on for that world. Rows are filtered when
  the window opens, when the toggle changes, and after a group pick or Undo (clicks that are not on a row), never on a
  switch. Nothing on = "No mods are on for this world. Switch off "Show only active mods" to see every mod.".
- Log: `reg: fixes: gcd-cdr (on in all worlds), sound-fix (client: the same in all worlds)` at start; `reg: world <guid>
  set to <key>, fixes off: <ids>`; the world check logs the set it compares (with the fixes).

## Startup time: stock vs patched file vs loader vs BepInEx / MelonLoader (lane modguide, 2026-10-20)
Mac M4 Max, one hidden copy (BT), 3 rounds round-robin, process start -> title ready (MainFlow scene Title + `_bootReady`,
read by `test/boottime/boot_probe.c`; equals dsload's "boot done (title ready)" within 0.01 s). Script: `test/boottime/boot_compare.py`.
| Case | Runs (s) | Median |
|---|---|---|
| stock (Steam-gate instruction only) | 10.52 11.27 9.96 | 10.5 |
| patched GameAssembly file (Hell's 20 patches in the file), no loader | 11.64 12.16 11.30 | 11.6 (the +1 s = a cold read of the swapped file) |
| loader, no mods, boot speed-up off | 11.96 12.13 11.84 | 12.0 |
| loader, no mods | 2.04 2.49 2.86 | 2.5 |
| loader, default mods | 3.50 2.49 3.08 | 3.1 |
| + BepInEx mod, first start / next | 29.7 59.6 66.3 / 3.2 4.8 4.0 | 59.6 / 4.0 |
| + MelonLoader mod, first start / next | 49.6 55.8 60.7 / 4.6 4.2 6.0 | 55.8 / 4.6 |
Patching the file gives no speed: it starts like stock. The gain is the boot speed-up (no Unity splash wait, frames
uncapped, loading priority High until the title); the loader itself costs at most ~1.4 s. Windows (older VM logs):
default mods 7.0-10.3 s, BepInEx first start 286 s / next 16 s; stock and MelonLoader not measured there.
Details and the comparison with BepInEx / MelonLoader: `../docs/MODDING.md`.

## Bug report window (owner 2026-10-20, branch bugreport) - `src/dsl_bugrep.c`, `src/dsl_bugrep_fmt.c`
The owner wants "an in-game bug report: text + screenshot + logs", reaching him through the private receiver page and a
Discord ping (receiver side: lane bugrecv; its spec is crashrep/README.md "kind bug").
- The game's own Bug Report buttons are REPLACED in the modded game: `TitleMenuView.OnBugReportPressed` (title) and
  `SystemMenuView.OnReportBugPressed` (Esc menu in a world) are hooked by name in `dsl_reg_ready` (before the modules)
  and never run (stock = `Application.OpenURL` of the developers' form; the title one also opens the saves folder).
  The Esc menu already has the entry: no new button.
- Screenshot = the frame just before the window: title = the next frame; world = the Esc menu scaled to 0 for 3 frames
  (no OnDisable, the game stays paused), shot, scale back, the window opens over the Esc menu (Esc / X / Cancel close it,
  the Esc menu is still there). `CaptureScreenshotAsTexture` -> `GetPixels32` -> box scale to <= 1600 x 900 -> JPEG
  with stb_image_write (`src/third_party/stb`, public domain; this IL2CPP build has no `EncodeToJPG`), quality 85 -> 65
  -> 45 until <= 1 MB -> `<crash>/bugs/shot.jpg`, previewed in the window (`image_png`, LoadImage reads JPEG).
- Window (kit only, Mods window look): multi-line text (kit `dsl_ui_field_multiline`: TMP LineType MultiLineNewline,
  8000 chars, top-left, wrapped, no text fit), contact (optional, one line, 200), "Include a screenshot" (on; off and
  grey when no shot), "Include logs" (on), privacy line + "Privacy note" (opens `ds-crashrep.vercel.app/privacy`), Send /
  Cancel, a status line, "Send saved reports (n)" when some were not sent. No consent question: Send is the consent;
  nothing is sent without it.
- Send (thread): `<crash>/bugs/<time>-<n>/report.txt.gz` is written FIRST (+ screenshot.jpg), then POST
  `/api/report` with `X-DSCR-Kind: bug` (same key / install id as crash reports; a random id when crash reports are not
  in this start). 2xx: "Thanks, your report was sent (id <random part of the receiver's path>)" + sent.txt; 429:
  "Too many reports, try later" (saved); else the error + the folder (saved). 10 newest folders kept. `<crash>` =
  ROOT/crash (DSML_ROOT) else `<log dir>/crash`.
- Wire format (agreed with bugrecv, client side): gzip, `DSCRASH 1`, the crash header lines (install, loader,
  game_build, platform, os, cpu, sent, mods, + scene), `kind: bug`, `contact: <as typed, not scrubbed>`,
  `reason: player bug report`, `===== bug.txt (N of N bytes) =====` (scrubbed), `===== screenshot.jpg.b64 (N of N bytes,
  base64) =====` (76-char lines, NEVER scrubbed or cut), then dsload.log, dsload.prev.log, rig.log, player.log,
  starter.log (Mac `~/Library/Logs/DungeonSettlersModded.log`, Windows `<profile>\starter.log`), ErrorLog.txt (scrubbed
  tails, crash caps). Over 3.5 MB gzip: log tails cut to 1/2 .. 1/16, then the screenshot is dropped (text kept).
- Test switches (no player start sets them): `DSL_BUGREP_DRY=1` (write, no POST), `DSL_BUGREP_ADMIN=<token>`
  (X-DSCR-Admin: receiver rate limits off), `DSL_BUGREP_TEST=<dir>` (cmd.json driver).
- Tests: `test/crashrep/run.sh` runs `bugrep_test.c` (format: base64 survives the scrubber byte for byte, text scrubbed,
  too big -> screenshot dropped; fails when the base64 is scrubbed). Rig: `test/bugrep_test.py [out] [--send]
  [--title-only]` on copy R with test module `bugrep-probe` (fake mouse on the stock buttons; OpenURL and
  OpenSavesFolder counted, never run): title + world, fails on the old loader (the stock button calls OpenURL).
