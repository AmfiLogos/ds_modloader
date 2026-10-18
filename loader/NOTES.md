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
tables but none for this build is refused), `requires` / `conflicts`: `"id"`, `"id>=1.2"`, `"loader>=2.0.0"`.
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
  "Multiplayer" they stay last in the column as before.
- Hover cards (dsl_ui.c): the card's top-left = the panel's right edge (+8 px), level with the element's top. Panel =
  the widest ancestor narrower than 3/4 of the screen (title column, Mods window, Load list). After layout, a card that
  leaves the screen on the right or was pushed back over the panel by the stock clamp moves outside the panel's left edge.
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
  (`SaveLoadView.ConfirmRemove`) asks once more in the game's notice box: "This save is also used by Dungeon Settlers
  without mods. Delete it there too?" Delete / Cancel (Cancel = the view's own "not confirmed", nothing deleted).
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
- Packaging: `modules/scan_saves.py` fails a build when a save or player file is inside (folders Saves / profiles /
  instances ..., player file names, any .json with "CampaignSaveHeader"): `package_mods.sh`, `app/build_app.sh`,
  `start/win/build_win.sh` (kit), `start/win/installer/make_installer.sh`. The released packages checked: none has one.
