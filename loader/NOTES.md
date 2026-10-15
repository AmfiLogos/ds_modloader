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

  Its table half (difficulty rows, texts, catalog.bin) is data: `apply_hell.py --install --tables-only --out-dir`
  makes it, the launcher puts it into the copy, and the module `requires` `hell-difficulty-data`.
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
