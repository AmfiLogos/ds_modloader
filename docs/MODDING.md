# Making mods for Dungeon Settlers Modded

This guide is for people who want to make a mod for Dungeon Settlers that runs in this mod loader. It also compares
the three ways to mod this game (our loader, BepInEx, MelonLoader) and the old way (change the game files), so you
can choose.

You need to know some C (native mods) or C# (BepInEx / MelonLoader mods). You do not need to know the loader's code.

The game is Dungeon Settlers on Steam, build 25846389, on Mac (Apple chip) and Windows (64-bit).

## Startup time: the short answer

Measured on a Mac (Apple M4 Max), 3 starts per case, hidden test copy, from process start to "title ready" (the game's
own flag `MainFlow._bootReady` is set on the title scene):

| How the game starts | Title ready (median of 3) |
|---|---|
| Stock game, no mods | 10.5 s |
| "Replace files": a patched `GameAssembly.dylib`, no loader | 11.6 s |
| Our loader, no mods | 2.5 s |
| Our loader, the default mods (Multiplayer, Custom Modes, Global cooldown fix) | 3.1 s |
| Our loader + a BepInEx mod: first start / later starts | 59.6 s / 4.0 s |
| Our loader + a MelonLoader mod: first start / later starts | 55.8 s / 4.6 s |

**Changing the game files is not faster than the loader. It is about as slow as the stock game (10 to 12 s),
because the patched game still starts like the stock one.** The loader reaches the title about 7 to 8 seconds sooner.
It does not wait for the Unity splash screen (it draws the same two logos while the game loads), and it runs frames
without a cap and loading at high priority until the title. With that speed-up turned off, the loader takes 12.0 s,
so the loader itself costs at most about 1.4 s. A native mod costs well under one second at startup.

Windows (from the logs of our Windows test machine, not measured again for this guide): our loader with the default
mods reaches the title in 7.0 to 10.3 s; BepInEx's first start took 286 s, its next start 16 s.

The full table, the method and the files it comes from are in [Startup time, measured](#startup-time-measured).

## Why make a mod for this loader, and does it work

Each claim has a verdict: **proven** (we measured or tested it), **plausible** (the design says so, not measured),
**unproven** (not tested), **no** (it is not true).

| Claim | Verdict | Evidence |
|---|---|---|
| Players install a mod with one zip, the same file on Mac and Windows | proven | `loader/modules/package_mods.sh` makes one zip per mod; the official mods ship this way; README "Add a mod" |
| The Steam game files are not changed | proven | the loader plays a copy of the game (README "Dungeon Settlers Modded"); our test copies read the Steam install only |
| The game starts faster with the loader than without it | proven (Mac) | see the startup table: the loader cuts the Unity splash wait and loads at full speed until the title |
| A native mod costs almost nothing at startup | proven (Mac) | loader with no mods vs the default 3 mods, see the table |
| A mod that finds game code by name keeps working after a game update | plausible | name lookups (`find_method_address`, `hook_method`, `field_offset`) do not use fixed addresses, and game fields moved in 5 of the last 8 game updates (our game-update notes). Not yet proven on a real update with this loader |
| Two mods cannot break each other by hooking the same function | proven | one hook engine; a second hook on the same function is refused and logged (host test `loader/test/run_tests.sh`) |
| A crashing mod does not stop the game for good | proven | a crash inside a mod's start phase turns that mod off at the next start, until its file changes (`dsload_state/<id>.disabled`) |
| Multiplayer refuses players with a different mod set or settings | proven | HELLO mod-set hash: id, version, sha256 of each file and every gameplay setting value; rig pair tests |
| BepInEx and MelonLoader mods also run under the loader | proven (Mac and Windows, BepInEx); proven (Mac, MelonLoader) | `loader/test/l1_bie_test.py`, `l1_ml_test.py`, Windows VM runs (`loader/test/winharmony/`) |
| A mod made for this loader also runs in the stock game | no | the loader gives the SDK; the stock game has none |
| The loader works on Linux or on Intel Macs | no | Mac arm64 and Windows x64 only |

## Risks

| Risk | What happens | What to do |
|---|---|---|
| Game update | Only build 25846389 is supported. Another build = mods with `game_builds` for this build are off; the game itself still runs | find code by name; keep per-build tables small; ship a new version after an update |
| Wrong address or offset | crash or a hang in the game | names first; Windows addresses and field offsets are NOT the Mac ones (see [Traps](#traps-that-cost-us-days)) |
| Your mod changes what a save holds | the stock game may not load that save | set `save_dependent` (and `save_backup`) in mod.json: the loader warns the player and can back up the save first |
| Multiplayer desync | players' games drift apart | follow the [multiplayer rules](#multiplayer-rules) |
| A BepInEx mod's first start | 2 to 5 minutes before the title, once per game build (see the table) | tell your players; the loader shows "Preparing mods (first start)" |
| Unsigned code | Mac: an ad-hoc signed dylib is enough; nobody signs third-party mods | players must trust the source of your zip |

## Terms

| Term | Meaning |
|---|---|
| IL2CPP | Unity's way to turn the game's C# code into native machine code. The game's code is in `GameAssembly.dylib` (Mac) or `GameAssembly.dll` (Windows); there is no C# DLL to edit |
| il2cpp API | C functions that `GameAssembly` exports (`il2cpp_class_from_name`, ...). The loader uses them to find classes, methods and fields by name |
| RVA | relative virtual address: the place of a function inside `GameAssembly`, counted from the start of the file's image. Changes with every game build and differs between Mac and Windows |
| Hook (detour) | the loader redirects a game function to your function; you call the original through a pointer it gives you |
| Patch | a few bytes of game code changed in memory (never in the file) |
| SDK | software development kit: here the one header `loader/sdk/dsl_sdk.h` |
| API version | the number of the SDK layout (now 6). Fields are only added at the end, so an old mod loads in a new loader |
| ABI | application binary interface: how C functions pass arguments and return values. Mac arm64 and Windows x64 differ (see the bool trap) |
| dylib / DLL | a native library for Mac / for Windows |
| mod.json | the file that describes your mod to the loader |
| Gameplay / client scope | gameplay = every multiplayer player needs the same mod and settings; client = only on your screen |
| Lockstep | how multiplayer works here: every player's game runs the same simulation steps with the same inputs |
| Desync | two players' games no longer have the same state |
| BepInEx | a mod loader for Unity games; mods are C# plugins. Version 6 is needed for IL2CPP games |
| MelonLoader | another mod loader for Unity games; mods are C# "Melons" |
| Doorstop | the small library that starts BepInEx inside the game process |
| Harmony (HarmonyX) | the C# library that BepInEx and MelonLoader mods use to patch game methods |
| Il2CppInterop | the tool that makes C# stand-in assemblies of an IL2CPP game, so C# mods can call it. It runs at the first start of BepInEx and MelonLoader |
| Cpp2IL | the tool that reads the IL2CPP game files for Il2CppInterop |
| .NET / CoreCLR | the C# runtime that BepInEx and MelonLoader bring for IL2CPP games |
| sha256 | a checksum of a file. The loader compares each file with the value in mod.json |
| Il2CppDumper | a tool that lists every class, method, field and address of an IL2CPP game (`dump.cs`, `script.json`) |

## 1. What a mod is

A mod is one folder `<mods>/<id>/` with a `mod.json` and the mod's files. Players get it as one zip
`<id>-<version>.zip` that holds that folder. The same zip works on Mac and Windows: it holds the Mac `.dylib` and the
Windows `.dll`, and one `mod.json` that lists both.

The mods folder: Mac `~/Library/Application Support/DSModLauncher/mods`, Windows
`%LOCALAPPDATA%\Dungeon Settlers Modded\mods` (Mods window, "Open mods folder").

### mod.json fields

| Field | Meaning |
|---|---|
| `id` | the folder name. Letters, digits, `-`, `_` |
| `name`, `short_name`, `description` | the texts of the Mods window. `i18n` gives them per language: `{"fr": {"name": ..., "description": ...}}` |
| `version` | your mod's version, for example `"1.0.0"`. Other mods' `requires` compare with it |
| `author` | your name or handle |
| `kind` | `code` = a native mod (the loader loads it); `bepinex` = a BepInEx plugin; `melon` = a MelonLoader mod; `data`, `patch`, `builtin` = only counted as installed |
| `api` | the SDK API your native mod needs, 1 to 6 |
| `scope` | `gameplay` (default) or `client`. See [Multiplayer rules](#multiplayer-rules) |
| `sim` | `true` = your mod changes the simulation (fights, stats, map). For information |
| `save_dependent` | `true` = your mod writes save data that the stock game cannot read. The loader asks the player before it loads a normal save with your mod on |
| `save_backup` | `true` (with `save_dependent`) = the loader copies the save files to `data/Mods/save_backups/` first |
| `default_on` | `false` = installed but off until the player turns it on |
| `game_builds` | list of Steam build ids, for example `["25846389"]`; `"*"` = any build. On another build your mod is off |
| `platforms` | `{"mac-arm64": {"entry": "<id>.dylib", "files": {"<id>.dylib": "<sha256>"}}, "win-x64": {"entry": "<id>.dll", "files": {...}}}`. The loader checks each listed file's sha256 and does not load the mod if one differs. A .NET mod uses `"any"` |
| `tables` | per-build address tables: `{"25846389": {"mac-arm64": "rva/25846389_mac.tsv", "win-x64": "rva/25846389_win.tsv"}}`. Each line: `name<TAB>0xRVA`. A mod with tables but none for the running build is not loaded |
| `requires`, `optional`, `conflicts` | lists of `"id"` or `"id>=1.2"` (`>=`, `>`, `<=`, `<`, `=`, `!=`). `"loader>=3.2"` = the loader's internal API. A mod whose requirement is missing stays on in the list, does not run, and the Mods window says why |
| `settings` | the settings the player can change in the Mods window. See [Settings](#4-settings) |
| `sync` | files of your data folder that a multiplayer guest gets from the host. See [Data folder](#5-data-folder) |

Full field list as the loader reads it: [loader/NOTES.md](../loader/NOTES.md), sections "mod.json", "Dependencies",
"Mod settings", "Mod data folder".

Never put saves or player files in a mod: `loader/modules/scan_saves.py` fails our build when it finds one.

## 2. Your first native mod (C)

A native mod is a small C library. The loader opens it inside the game, gives it a `DSLHost` table of functions, and
calls up to three entry points:

| Entry point | When | What works |
|---|---|---|
| `dsl_module_load(host, self)` | right after the loader opened your library, before `GameAssembly` is in memory | read env and settings, open files |
| `dsl_module_early(host, self)` | `GameAssembly` is in memory, none of its code ran | hooks by address (`hook_rva`, `table`); NOT name lookups |
| `dsl_plugin_init(host, self)` | after the game's il2cpp start | name lookups, hooks by name, everything else |

Return 0 from each. Any other value removes every hook of your mod for this start. Export only the ones you need.

### Step 1: the code

`hello.c` (one file; the same source builds on Mac and Windows):

```c
#include "dsl_sdk.h"
DSL_PLUGIN();                                   /* exports the API version this file was built for */

static const DSLHost *H; static DSLPlugin *ME;
static void (*orig_set_gcd)(void *self, const void *mi);

/* runs instead of Refactor.Component.SkillComponent.SetGlobalCooldown() */
static void my_set_gcd(void *self, const void *mi) {
    static int n; if (++n <= 5) H->log(ME, "global cooldown starts (#%d)", n);
    orig_set_gcd(self, mi);                     /* always call the game's own code unless you replace it on purpose */
}
static void on_click(void *ud) { (void)ud; H->log(ME, "my title button was clicked"); }

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    if (H->hook_method(ME, "Refactor.Component", "SkillComponent", "SetGlobalCooldown", 0,
                       (void *)my_set_gcd, (void **)&orig_set_gcd)) return 1;   /* not found or already hooked: off */
    dsl_title_add_button(H, ME, "Hello", 200, "A button of my mod", on_click, NULL);
    return 0;
}
```

The hooked method above is the one that the official Global cooldown fix (`gcd-cdr`) also hooks. Only one mod can hook
a function: turn `gcd-cdr` off to try this example, or pick another method. The list of every function the official
mods hook is [docs/hooks.tsv](hooks.tsv).

A full small example: [loader/modules/gcd-cdr/gcd_cdr.c](../loader/modules/gcd-cdr/gcd_cdr.c) (62 lines: finds a
class, two fields and two methods by name, uses a per-build table for one generic method, hooks one method).

### Step 2: build

Mac (Apple chip, Xcode command line tools):

```sh
clang -arch arm64 -O2 -Wall -mmacosx-version-min=12.0 -I loader/sdk -dynamiclib \
  -install_name @rpath/hello.dylib -o hello/hello.dylib hello.c
codesign -f -s - hello/hello.dylib        # ad-hoc signature: Apple chips run no unsigned code
```

Windows x64 (cross-built on the Mac or on Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw); on Windows
use the same llvm-mingw `clang`):

```sh
x86_64-w64-mingw32-clang -O2 -Wall -D__USE_MINGW_ANSI_STDIO=1 -I loader/sdk -shared -Wl,--no-insert-timestamp \
  -o hello/hello.dll hello.c -Wl,--export-all-symbols -static -static-libgcc
```

These are the flags of `loader/modules/build_mods.sh` (function `simple`). Add `-ffile-prefix-map=$HOME=/src` so your
home folder path does not end up in the binary, and strip debug info before you ship (`strip -S` / `llvm-strip
--strip-debug`).

### Step 3: mod.json

```json
{
  "id": "hello", "name": "Hello", "version": "0.1.0", "author": "you",
  "kind": "code", "api": 3, "scope": "client",
  "game_builds": ["25846389"],
  "platforms": {
    "mac-arm64": { "entry": "hello.dylib", "files": { "hello.dylib": "<sha256 of hello.dylib>" } },
    "win-x64":   { "entry": "hello.dll",   "files": { "hello.dll":   "<sha256 of hello.dll>" } }
  },
  "requires": ["loader>=2.0.0"],
  "description": "Logs the global cooldown and adds a title button."
}
```

`shasum -a 256 hello/hello.dylib` gives the sha256. `api` 3 is the lowest API with title buttons. The same mod.json goes
next to both binaries.

### Step 4: try it

Put the folder `hello/` (with mod.json, hello.dylib, hello.dll) into the mods folder, start the game, click Mods, turn
Hello on, click Apply. Your log lines are in `dsload.log`: Mac
`~/Library/Application Support/DSModLauncher/profiles/modded`, Windows `%LOCALAPPDATA%\Dungeon Settlers Modded\profiles\modded`
(the previous start: `dsload.prev.log`). The loader also logs each hook: `SetGlobalCooldown: hooked 0x... for hello`.

## 3. Finding game code: names first, tables second

- **By name** (after `dsl_plugin_init`): `find_class(ns, name)`, `find_method(klass, name, argc)`,
  `find_method_address(ns, klass, method, argc)`, `field_offset(klass, name)`, `hook_method(...)`. This works on every
  game build as long as the name stays, and on Mac and Windows alike. Use it whenever you can.
- **By address** (only where names cannot work: hooks before il2cpp starts, a place inside a function, a generic
  method): a per-build table in mod.json `tables`, read with `host->table(self, "name")`. You need one table per game
  build and per platform. Get the addresses from an Il2CppDumper dump of that build (`script.json`, `dump.cs`).
- **Code patches**: `patch_rva(self, rva, old, new, n, name)` checks that the current bytes are `old`, writes at most
  16 bytes (a multiple of 4), and undoes it if your mod fails.
- **Function signatures**: an IL2CPP method `R M(A a)` of an object is the C function `R M(void *self, A a, const void
  *method_info)`; a static method has no `self`. `dump.cs` from Il2CppDumper shows the C# signatures.
- **One owner per function**: a function that another mod already hooks is refused (log line). A function that starts
  with a jump (another loader's patch) is refused on Mac; on Windows the loader chains with BepInEx's Harmony jumps.
- Call il2cpp functions only on the game's main thread (inside a hook of game code, or a UI callback).

## 4. Settings

Declare them in mod.json; the player changes them in the Mods window (Settings next to your mod):

```json
"settings": [
  { "id": "max_units", "type": "int", "default": 4, "min": 4, "max": 8, "step": 1, "scope": "gameplay",
    "label": "Max units in the dungeon", "help": "How many units can enter.",
    "i18n": { "fr": { "label": "Unités max dans le donjon", "help": "Combien d'unités peuvent entrer." } } },
  { "id": "show_hint", "type": "toggle", "default": true, "scope": "local", "label": "Show the hint" },
  { "id": "mode", "type": "choice", "default": "fast",
    "choices": [ { "id": "fast", "label": "Fast" }, { "id": "safe", "label": "Safe" } ], "label": "Mode" }
]
```

Read them with `dsl_get_bool(host, self, "show_hint", 1)`, `dsl_get_int(...)`, `dsl_get_choice(...)`. These work in every
phase. A `gameplay` setting never changes in a running game (Apply restarts behind a loading screen) and is part of the
multiplayer check. A `local` setting changes at once: register `dsl_on_setting_change(host, self, cb, ud)`. With an
older loader the functions return your default. Example: [loader/modules/zetu-suite/mod.json](../loader/modules/zetu-suite/mod.json).

## 5. Data folder

Keep your files (settings of your own, progress, caches) in your mod's data folder, never in your mod folder: an
update of your mod replaces the mod folder.

- C: `const char *d = dsl_mod_data_dir(host, self);` = `<game data>/ModData/<id>/` (NULL from a loader older than
  API 6; add `"requires": ["loader>=3.2"]`).
- C#: `Path.Combine(Environment.GetEnvironmentVariable("DSML_MOD_DATA") ?? Path.Combine(Application.persistentDataPath, "ModData"), "<id>")`.
- `"sync": ["campaign/*.json", "markers/**"]` in mod.json: these files are gameplay data. A multiplayer guest plays with
  the host's copy (at most 1 MB and 256 files per mod); the guest's own files come back after the session. Read them
  again when a world loads.

## 6. Game-style UI

`dsl_ui(host)` returns a widget kit that clones the game's own widgets: `panel`, `text`, `button`, `toggle`,
`value_row`, `slider`, `field` (text input), `scroll`, `image_png`, `tooltip`, `notice` (the game's message box).
Other places: `dsl_title_add_button` (a title screen button), `dsl_menu_add_tab` (a tab in the Mods window),
`dsl_loadrow_add` / `dsl_loadlist_filter` (lines and filters in the Load list). All callbacks run on the game's main
thread between frames. Signatures and rules: [loader/sdk/dsl_sdk.h](../loader/sdk/dsl_sdk.h). Tests that use every
widget: `loader/test/mods_src/uikit-demo/`.

## 7. Multiplayer rules

- `scope: gameplay` (the default): every player in a session needs your mod, the same version, the same file sha256s
  and the same gameplay setting values. A player with a difference cannot join, and the message names the mod or the
  setting. Use `scope: client` only for mods that change nothing in the simulation (UI, sound, camera).
- Anything that changes game state must run inside the lockstep step: a hook on code that the world tick runs, or a
  callback of the multiplayer API (`register_tick`, `register_sim_cmd`). Never change state from a UI click, a key,
  a timer or another thread directly; send a sim command instead.
- Random numbers: use the multiplayer API's `rand_*` with your mod id and one stream number per feature. A draw
  inside the step is the same on every player. `UnityEngine.Random` inside the step is already routed.
- The multiplayer API: [ds_multiplayer src/MP_API.md](https://github.com/AmfiLogos/ds_multiplayer/blob/main/src/MP_API.md)
  (C: `mp_api_get(2)` found with `dlsym` on the multiplayer library after `module_loaded("multiplayer")`; C#:
  `DsmpBridge.dll`, mod id `dsmp-bridge`). With no multiplayer mod, every call still works locally.
- While your mod has state that must not be restarted under: `dsl_set_session_busy(host, self, "reason")`.

## 8. BepInEx and MelonLoader mods under this loader

You can ship a C# mod in the same zip format. The loader installs and starts the .NET loader for the player.

- mod.json: `"kind": "bepinex"` (or `"melon"`), `"platforms": {"any": {"files": {"MyMod.dll": "<sha256>"}}}`,
  `scope`, `requires` / `optional` / `conflicts` as above. Example: `loader/modules/dsmp-bridge/mod.json`.
- BepInEx: version 6 IL2CPP. Windows: the official build be.788 (sha256 pinned), downloaded into the modded game copy
  at first use. Mac: our arm64 build of BepInEx (release `bepinex-mac-0.1.3` of ds_multiplayer), because the official
  macOS build is x64 only. Each enabled bepinex mod becomes one plugin folder. The BepInEx console window is always off.
- MelonLoader: Windows: the official 0.7.3 x64 (version.dll in the game copy). Mac: our arm64 build (release
  `melonloader-mac-0.1.1` of ds_multiplayer), because the official macOS build is x64 only.
- One .NET loader per start: if a player turns on BepInEx mods and MelonLoader mods together, the BepInEx ones run and
  the Mods window says why the others do not.
- A C# mod can use the multiplayer API (`DsmpBridge.dll`) and the data folder (`DSML_MOD_DATA`) the same way.
- Harmony on a function that a native mod also hooks: Windows chains both (ours runs first); see
  [loader/NOTES.md](../loader/NOTES.md) "Harmony + our MinHook on one function, Windows".
- Settings: a C# mod uses its loader's own config (BepInEx `Config.Bind`, MelonLoader `MelonPreferences`). The
  `dsl_get_*` functions of section 4 are C only; the official C# mods (ZET-U for BepInEx) use `Config.Bind`.

## 9. Packaging and release

- Zip layout: one folder `<id>/` with `mod.json`, the Mac and Windows binaries, `rva/` tables if any, and your license.
  Name: `<id>-<version>.zip`. `mod.json` `version` = the version in the zip name.
- sha256: every file listed in `platforms.*.files` must match, or the loader does not load the mod.
- Players install it: Mods, "Open mods folder", drop the zip, Refresh, turn it on, Apply. A zip that is not a mod
  moves to `.failed`.
- Signing: the loader's auto-update covers only the official mods (multiplayer, custom-modes, gcd-cdr,
  hell-difficulty, zetu-suite, sound-fix, multi-bar, dsmp-bridge), signed with the project's release key. Your mod is installed by hand and
  is not signed by us. On Mac, the ad-hoc `codesign -s -` signature is enough.
- Before you publish: search your files for your user name and home path (build paths end up in binaries).

## 10. Testing

- Test on a copy of the game, never on your own saves first. The modded game uses its own data folder
  (`profiles/modded/data`), but it plays your normal saves in place (README "Where your saves are").
- Read `dsload.log` after each start: it lists each mod, why one did not load (file sha256, API, build, table,
  requirement), each hook, and each crash guard.
- A crash inside one of your entry points turns your mod off at the next start until its file changes. Delete
  nothing: rebuild, and the new file is tried again.
- Test on both systems. Mac and Windows differ in addresses, field offsets and the `bool` return (see below); a Mac run
  proves nothing about Windows.
- Our own tests, if you build this repo: host test `loader/test/run_tests.sh` (no game), hidden game copies
  `loader/test/lrig.py` (see [loader/NOTES.md](../loader/NOTES.md) "Tests").

### Traps that cost us days

- Never keep a raw game object pointer past the current frame. Use a weak GC handle (`il2cpp_gchandle_new_weakref`,
  pointer-sized) and fetch it again each time.
- A C# `bool` return is `bool` / `uint8_t` in C, never `int` (Windows leaves the upper bytes as garbage).
- Mac addresses and field offsets are not Windows ones. Small getters can be inlined on Windows only: a hook that
  never fires on Windows usually means that.
- An exception thrown inside your hook stops the game's whole Update of that object: check every NULL.

## 11. Comparison: this loader, BepInEx, MelonLoader, changing the game files

Facts about BepInEx and MelonLoader come from their official pages (see [Sources](#sources)) or from our own runs.
"Not measured" means that we do not have a number.

| | Our loader, native mod (C) | BepInEx 6 (IL2CPP) | MelonLoader | Change the game files |
|---|---|---|---|---|
| Mod language | C (any language that makes a C library) | C# (.NET 6), Harmony | C# (.NET 6), Harmony | bytes: instruction patches, edited asset bundles |
| Setup for players | installer once; then drop a mod zip in the mods folder | standalone: unzip BepInEx into the game folder (Windows: `winhttp.dll` proxy), run once; under our loader: automatic | standalone: installer, or `version.dll` + the zip by hand; under our loader: automatic | run a patch script on the Steam files; Mac: ad-hoc re-sign `GameAssembly.dylib` |
| Startup, Mac (measured) | 2.5 s no mods, 3.1 s default mods | under our loader: first start 30 to 66 s (median 59.6), then 4.0 s | under our loader: first start 50 to 61 s (median 55.8), then 4.6 s | 11.3 to 12.2 s (median 11.6), the same as stock (10.5 s) |
| Startup, Windows (our logs) | 7.0 to 10.3 s, default mods | under our loader: first start 286 s, next start 16 s | no real Windows number (an old Wine run: 260 s / 55 s) | not measured |
| First start | nothing to prepare | Cpp2IL + Il2CppInterop make C# stand-ins of the whole game, once per game build | the same (Il2CppAssemblyGenerator), once per game build | nothing to prepare |
| Runtime cost | a jump to your C function; no .NET in the process | the .NET runtime in the process; each Harmony patch goes from native to managed code. Not measured | the same as BepInEx. Not measured | none |
| Game update | mods that use names keep working if the names stay; address tables need a new row; the loader turns off mods made for another build | the stand-ins are made again automatically when `GameAssembly` changes (hash check); plugins that use names keep working if the names stay | the same, SHA512 check | Steam's update puts the stock file back; every patch address must be found again for the new build |
| Multiplayer | mod-set check (id, version, file sha256, gameplay settings), data sync, the multiplayer API | under our loader: in the same mod-set check, and `DsmpBridge.dll` for the multiplayer API; standalone: nothing | under our loader: in the mod-set check; standalone: nothing | not in the mod-set check (it lists mods, not changed game files) |
| Mac support | native Apple chip | official: the IL2CPP support chart says no macOS; the builds have a `macos-x64` archive with an x64-only .NET (Rosetta). Under our loader: our arm64 build | official: macOS x64 since 0.7.1, no arm64. Under our loader: our arm64 build | yes, with ad-hoc re-signing |
| Two mods on one function | refused and logged (one owner per function) | Harmony allows several patches on one method ([Harmony docs](https://harmony.pardeike.net/articles/priorities.html)) | the same (HarmonyX) | the second patch overwrites the first |
| Debugging | `dsload.log` (each mod, hook, refusal), crash guard turns a crashing mod off, `lldb` | `BepInEx/LogOutput.log`; console window (off under our loader); decompile the stand-in DLLs to read the game's types | `MelonLoader/Latest.log`; console | `lldb` only; a crash names no mod |
| What the modder needs | C, an Il2CppDumper dump for addresses, two builds (Mac + Windows), care with the ABI (bool, offsets, inlining) | C#, the .NET 6 SDK, the stand-in DLLs from one BepInEx start, one DLL for both systems | the same as BepInEx, with MelonLoader's stand-ins | a disassembler, the exact bytes per build and per platform |
| Stability of the tool | ours, pinned to one game build | BepInEx's own README: "only Unity Mono has stable releases"; IL2CPP = bleeding-edge builds (we pin be.788) | release 0.7.3 | n/a |

When to use what:
- Native C mod: best startup and runtime cost, and the full SDK (settings window, UI kit, Load list, multiplayer).
  Costs more care on two platforms.
- BepInEx or MelonLoader mod: easiest if you know Unity C# modding; one DLL for both systems. Players wait once per
  game build (about 1 minute on our Mac, about 5 minutes on our Windows test machine).
- Changing the game files: no startup gain over the loader (measured), lost at each Steam update, and invisible to the
  multiplayer check. We moved our own byte patches (Global cooldown fix, Hell) into loader mods for these reasons.

## Startup time, measured

### Mac (this guide, 2026-10-20)

Machine: Apple M4 Max, macOS 27.2, other test copies running (load average about 7). Game build 25846389. Loader
v0.1.19 candidate (`dsload.dylib` of the release build folder). One hidden test copy (APFS clone of the Steam app,
own bundle id, window invisible, sound muted, 960x540 windowed). Cases run round-robin, 3 rounds, so the Mac's load
hits every case alike.

Marker: a small test library (`loader/test/boottime/boot_probe.c`) reads the game's `MainFlow` object between frames
and writes the time when `_currentScene` = Title and `_bootReady` = 1. Time 0 = the process start time from the
kernel. In every loader run this marker and the loader's own log line "boot done (title ready)" agree within 0.01 s.
After that moment the game fades the title in (about 1.7 s); that part is the same game code in every case and is
not counted.

| Case | Run 1 | Run 2 | Run 3 | Median |
|---|---|---|---|---|
| a. Stock game | 10.52 | 11.27 | 9.96 | 10.52 s |
| b. Patched file (Hell's 20 instruction patches written into `GameAssembly.dylib`, re-signed), no loader | 11.64 | 12.16 | 11.30 | 11.64 s |
| c0. Loader, no mods, boot speed-up off (`DSL_FASTBOOT=0`, `DSL_PREWARM=0`) | 11.96 | 12.13 | 11.84 | 11.96 s |
| c. Loader, no mods | 2.04 | 2.49 | 2.86 | 2.49 s |
| d. Loader, default mods (multiplayer, custom-modes, gcd-cdr) | 3.50 | 2.49 | 3.08 | 3.08 s |
| e. d + BepInEx mod (dsmp-bridge), fresh BepInEx folder (first start) | 29.72 | 59.57 | 66.27 | 59.57 s |
| e. same folder, next start | 3.22 | 4.80 | 4.04 | 4.04 s |
| f. d + MelonLoader mod (hello-melon), fresh MelonLoader folder (first start) | 49.57 | 55.81 | 60.67 | 55.81 s |
| f. same folder, next start | 4.63 | 4.18 | 5.97 | 4.63 s |

Notes:
- Every test copy has one instruction changed in `GameAssembly.dylib` (the Steam check off), so "stock" here is the
  stock game minus Steam. Without it a copy cannot start outside Steam.
- b is about 1 s slower than a in each round. b swaps in another `GameAssembly.dylib` file each time, which is read
  cold from the disk; the 20 patched instructions do not run at startup. So b = stock, within that noise.
- The BepInEx and MelonLoader installs came from the same zips the app downloads (0.3 to 0.8 s to unpack); the
  download (31 MB / 52 MB) is not counted. Their logs confirm the mod ran (`dsload.log`: "1 BepInEx plugin(s)
  running" / "1 MelonLoader mod(s) running").
- Why the loader is fast: the stock game holds its first scene until the Unity splash ends (about 4 s here), then
  loads with a capped frame rate. The loader cancels that wait and draws the same logos over the load, runs frames
  uncapped and sets loading priority High until the title (`loader/src/dsl_soft.c`, `dsl_fastboot.c`).
- Script: `loader/test/boottime/boot_compare.py` (results, logs and probe lines per run in its output folder).

### Windows (from earlier logs, not measured for this guide)

| What | Result | File |
|---|---|---|
| Loader v0.1.16.1 / v0.1.17, default mods, real starter, Windows VM, starter start to title | 7.0, 7.1, 7.1, 7.1, 7.0, 7.2, 7.1, 7.9 s (first start after install: 25.2 s) | `fps_run/r161a.log`, `r161b.log`, `r17a.log`, `r17b.log` in the Windows test tool's output folder (not in this repo) |
| Loader, other checklist runs of 2026-10-20 (crash-report question, settings) | 7.8 to 10.3 s (first start 20.2 s) | `run12/run.log`, `run13/run.log`, `run14/run.log` (same folder) |
| Loader v0.1.16.1 + BepInEx be.788 (custom-modes, gcd-cdr, a BepInEx test plugin), first start / next start | 285.6 s / 16.1 s | `bie0161/run.log` (same folder) |
| MelonLoader 0.7.3 on Windows | no real Windows run; an older Wine run gave 260 s first start (with downloads), 55 s next start | ds_multiplayer `ext/melon/NOTES.md` |
| Stock game on Windows | not measured | |

## Sources

- BepInEx README (support chart, "only Unity Mono has stable releases"): https://github.com/BepInEx/BepInEx
- BepInEx bleeding-edge builds (be.789, archives per platform): https://builds.bepinex.dev/projects/bepinex_be
- BepInEx v6.0.0-pre.2 release notes: https://github.com/BepInEx/BepInEx/releases/tag/v6.0.0-pre.2
- BepInEx IL2CPP install guide ("First run in Il2Cpp games may take some time"): https://docs.bepinex.dev/master/articles/user_guide/installation/unity_il2cpp.html
- BepInEx interop generation and its hash check: https://github.com/BepInEx/BepInEx/blob/master/Runtimes/Unity/BepInEx.Unity.IL2CPP/Il2CppInteropManager.cs
- BepInEx plugin, config, logging, patching docs: https://docs.bepinex.dev/master/articles/dev_guide/plugin_tutorial/2_plugin_start.html,
  https://docs.bepinex.dev/master/articles/dev_guide/plugin_tutorial/4_configuration.html,
  https://docs.bepinex.dev/master/articles/dev_guide/plugin_tutorial/3_logging.html,
  https://docs.bepinex.dev/master/articles/dev_guide/runtime_patching.html
- Il2CppInterop: https://github.com/BepInEx/Il2CppInterop
- MelonLoader README (platforms, proxy DLL): https://github.com/LavaGang/MelonLoader
- MelonLoader releases (0.7.3, macOS x64 since 0.7.1): https://github.com/LavaGang/MelonLoader/releases
- MelonLoader assembly generation and its hash check: https://github.com/LavaGang/MelonLoader/blob/master/Dependencies/Il2CppAssemblyGenerator/Core.cs
- MelonLoader wiki (install, quick start, patching, preferences, attributes): https://melonwiki.xyz/gettingstarted.md,
  https://melonwiki.xyz/modders/quickstart.md, https://melonwiki.xyz/modders/patching.md,
  https://melonwiki.xyz/modders/preferences.md, https://melonwiki.xyz/modders/attributes.md
- Our arm64 BepInEx and MelonLoader for Mac: releases `bepinex-mac-0.1.3` and `melonloader-mac-0.1.1` of
  [ds_multiplayer](https://github.com/AmfiLogos/ds_multiplayer/releases); pinned in `app/bepinex/install_bepinex.sh`
  and `app/melon/install_melon.sh` (sha256).
- Not found in the official pages: a startup or per-start cost figure for either tool, and native arm64 support on Mac.
