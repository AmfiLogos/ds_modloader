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

## Modules built here (`./build.sh` -> `build/`)
| Module | Source | API | What |
|---|---|---|---|
| multiplayer 0.10.0 | `modules/multiplayer/mp_module.c` + `vendor/mp` (snapshot of `~/ds_multiplayer/src` + `win/math`, 2026-10-14 19:14, `vendor/mp/SNAPSHOT_TAKEN.txt`) | 2 | Every MP source compiled unchanged EXCEPT `rig_main.c` and `rig_hook.c`, which `mp_module.c` replaces: `rig_hook(rva,...)` -> `host->hook_rva`, `rig_init` -> `dsl_module_load` (no `unsetenv`), `on_image` -> `dsl_module_early`. `det_ranges.tsv` ships next to the dylib. |
| gcd-cdr 2.0.0 | `modules/gcd-cdr/` | 2 | The owner's GCD byte patch rewritten as a runtime hook for build 25846389: hooks `SkillComponent.SetGlobalCooldown()` by name, divides `_globalCooldownValue` by (1 + stat 28 / 100), applies it with the game's own `SetGlobalCooldown(float)`. Generic `GetComponent<StatusReader>` + its MethodInfo slot + the runtime-metadata init function come from the per-build table `rva/25846389_mac.tsv` (from dump.cs + disassembly of `CooldownCondition` MoveNext 0x8A4FE4). `sim: true`. |
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
