# Dungeon Settlers Modded

A mod loader for Dungeon Settlers (Steam), for Mac (Apple chip) and Windows. It plays a copy of the game with mods.
Steam's game, your normal saves and your settings do not change.

It is made for Steam build 25846389 of Dungeon Settlers. Other game builds are not supported.

## The three repositories

- ds_modloader (this repo): the mod loader, the Mac app, the Windows installer, and two small mods
  (Global cooldown, Hell difficulty).
- [ds_multiplayer](https://github.com/AmfiLogos/ds_multiplayer): the Multiplayer mod. Its releases page has the
  installers (`Dungeon-Settlers-Modded-Mac.dmg`, `Dungeon-Settlers-Modded-Setup.exe`).
- [ds_custommodes](https://github.com/AmfiLogos/ds_custommodes): the Custom Modes mod (My Units, Unit Builder,
  Gallery, Test Fights, Foreign Expedition, PvP).

The installers hold the loader and all four mods. You only need the separate mod zips to add or update one mod.

## What you need

- Dungeon Settlers on Steam, build 25846389, and Steam open and signed in.
- Mac: a Mac with an Apple chip (M1 or later).
- Windows: a 64-bit Windows PC.

## Download and install

Get the installers from the latest release on the
[ds_multiplayer releases page](https://github.com/AmfiLogos/ds_multiplayer/releases).

### Mac

1. Download `Dungeon-Settlers-Modded-Mac.dmg` and open it.
2. Drag "Dungeon Settlers Modded" onto the Applications folder.
3. The first time only: the app is not signed by Apple. Right-click the app in Applications and click Open.
   If macOS blocks it, open System Settings, click Privacy & Security, scroll down and click "Open Anyway".
   If that does not work, open Terminal, paste this line and press Return:
   `xattr -dr com.apple.quarantine "/Applications/Dungeon Settlers Modded.app"`
4. Keep Steam open and signed in. The first start makes the modded game copy (about 10 seconds).

### Windows

1. Download `Dungeon-Settlers-Modded-Setup.exe` and double-click it.
2. If Windows says "Windows protected your PC", click "More info", then "Run anyway". The installer is not signed.
3. The installer finds the Steam game folder. If it does not, click Browse and select the folder of
   Dungeon Settlers (in Steam: right-click the game, Manage, Browse local files).
4. Start "Dungeon Settlers Modded" from the desktop or the Start menu.
   To remove it: Settings, Apps, "Dungeon Settlers Modded", Uninstall. Your modded saves stay.

The default mods are inside: Multiplayer, Custom Modes, Global cooldown, Hell difficulty (off until you turn it on).
On the title screen, click Mods to turn mods on or off.

## Add a mod

Each mod has its own download: `<mod>-<version>.zip` (for example `gcd-cdr-2.0.0.zip`). One file works on Mac and Windows.

1. Download the zip. Do not unzip it.
2. Start the game, click Mods, then click "Open mods folder". Put the zip into that folder.
   (Or open the folder yourself. Mac: in Finder, click Go, Go to Folder, paste
   `~/Library/Application Support/DSModLauncher/mods`. Windows: press Windows+R, paste
   `%LOCALAPPDATA%\Dungeon Settlers Modded\mods`, press Enter.)
3. In the Mods window, click Refresh. The mod shows up in the list. Turn it on and click Apply.
   The game restarts behind a loading screen, with the mod on.

A zip of a mod you already have replaces that mod. A file that is not a mod moves to the folder `.failed`.

## Where your saves are

The modded game plays on its own copy of your saves. On its first start it copies your normal saves once (it only
reads them). After that the two sets are separate: progress in the modded game does not change your normal saves.
The modded saves are in the `data/Saves` folder of:

- Mac: `~/Library/Application Support/DSModLauncher/profiles/modded`
- Windows: `%LOCALAPPDATA%\Dungeon Settlers Modded\profiles\modded` (or the folder you chose in the installer)

Uninstalling keeps this folder.

## Known limits

- One game build only (25846389).
- Windows was tested in CrossOver on a Mac, not yet on a real Windows PC with Steam.
- Multiplayer was tested on one local network, not yet between two homes over the internet.
- The Mac app and the Windows installer are not signed, so the system warns on the first start.

## Report a bug

Open an issue on this repo's Issues page. Say your system (Mac or Windows), the release (for example v13.2) and
what you did. Attach the `.log` files from this folder (not the `data` folder, which holds your saves):
Mac: `~/Library/Application Support/DSModLauncher/profiles/modded` (and `~/Library/Logs/DungeonSettlersModded.log`).
Windows: `%LOCALAPPDATA%\Dungeon Settlers Modded\profiles\modded`.

## Build it yourself

See [BUILDING.md](BUILDING.md). The license files that ship with each release are in [licenses/](licenses/).

## Mod Launcher (step 1, Mac; older design notes)

**Roadmap:** what we work on now and next, and where you can help: [ROADMAP.md](ROADMAP.md).

Plan: `research/plan_launcher.md` in ds_multiplayer. Owner decisions: ds_multiplayer `PLAN.md`, section "Launcher".

- `core/` (Rust crate `dsml-core`): Steam detection + build id (`steam.rs`), signed index + checksums
  (`crypto.rs`, `fetch.rs`), pin / update rules (`decide.rs`), Mac side copy per build (`copy_mac.rs`, port of
  `rig/make_mp_app.sh`), launch with the code mods (`launch.rs`, same env as `rig/mp_launcher.c`),
  Windows stubs (`windows.rs`), mods per world + groups (`worlds.rs`), gameplay/client scope + mod-set hash +
  join plan (`join.rs`), difficulties (`difficulty.rs`: the game's Devastation unlock read from
  OutGame.json `DifficultyUnlockSaveData.IsDownfallUnlocked`, custom difficulties such as Hell as mods). `dsml-tool`: keygen / sign / verify / file entry / patches (stock vs modded -> byte patches) for the publisher.
- `src-tauri/`: UNUSED PROTOTYPE, not shipped (see `src-tauri/README.md`). Tauri 2 shell (commands: status,
  refresh_index, set_enabled, play, remove_copy).
- `ui/`: the prototype's pages: plain HTML/CSS/JS, game palette, Cinzel + Lora (OFL, `ui/fonts/OFL-*.txt`).
  `ui/bicons/` holds game art. It is not in git: `builder/gen/extract_icons.py` extracts it from your installed
  game (see BUILDING.md, Unit Builder icons).
- `dev_keys/`: DEV ONLY test key pair (see its README).

Tests:
- `cargo test -p dsml-core` : unit tests + local-server download tests.
- `cargo test -p dsml-core --test e2e_mac -- --ignored --nocapture` : fake Steam library (APFS clone of the
  real app, read only) -> signed index -> copies -> hidden launch (-batchmode, background-only, guard kills on
  any visible window) -> the multiplayer dylib answers on its socket. Checks the owner's prefs, settings and
  real GameAssembly are unchanged. Test files go to `test_env/` (git-ignored).
  `worlds_hell_and_client_mod_compose` (same command): two worlds, Hell (gameplay) + weapon slot (client) as
  byte-patch mods on one copy, plain world on another; the Hell copy boots to the title screen hidden.
- `tests/weaponslot_pair.py <PAIR>`: host WITH the weapon-slot mod, guest WITHOUT, one lockstep game on a
  leased rig pair (copies from `make_pair_copies`). Result in `test_env/pair/result.json`.

Env: `DSML_ROOT` (launcher folder, default `~/Library/Application Support/DSModLauncher`), `DSML_INDEX_URL`
(default: placeholder GitHub URL), `DSML_STOCK_DIRS` (verified stock asset copies, optional).
