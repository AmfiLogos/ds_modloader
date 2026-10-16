# Dungeon Settlers Modded

A mod loader for Dungeon Settlers (Steam), for Mac (Apple chip) and Windows. It plays a copy of the game with mods.
Steam's game, your normal saves and your settings do not change.

## Download and install

Get the files from the latest release on the Releases page of this repository.

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
3. Quit the game and start it again. The mod is installed. Click Mods and turn it on.

A zip of a mod you already have replaces that mod. A file that is not a mod moves to the folder `.failed`.

## For developers

Build: `loader/modules/build_mods.sh` (all mods, both systems), `app/build_app.sh` then `app/make_dmg.sh` (Mac),
`loader/start/win/build_win.sh` then `loader/start/win/installer/make_installer.sh` (Windows, NSIS 3),
`loader/modules/package_mods.sh` (one zip per mod in `loader/build/packages/`).

## Mod Launcher (step 1, Mac; older design notes)

Plan: `~/ds_multiplayer/research/plan_launcher.md`. Owner decisions: `~/ds_multiplayer/PLAN.md`, section "Launcher".

- `core/` (Rust crate `dsml-core`): Steam detection + build id (`steam.rs`), signed index + checksums
  (`crypto.rs`, `fetch.rs`), pin / update rules (`decide.rs`), Mac side copy per build (`copy_mac.rs`, port of
  `rig/make_mp_app.sh`), launch with the code mods (`launch.rs`, same env as `rig/mp_launcher.c`),
  Windows stubs (`windows.rs`), mods per world + groups (`worlds.rs`), gameplay/client scope + mod-set hash +
  join plan (`join.rs`), difficulties (`difficulty.rs`: the game's Devastation unlock read from
  OutGame.json `DifficultyUnlockSaveData.IsDownfallUnlocked`, custom difficulties such as Hell as mods). `dsml-tool`: keygen / sign / verify / file entry / patches (stock vs modded -> byte patches) for the publisher.
- `src-tauri/`: Tauri 2 shell (commands: status, refresh_index, set_enabled, play, remove_copy).
- `ui/`: plain HTML/CSS/JS, game palette, Cinzel + Lora (OFL, `ui/fonts/OFL-*.txt`).
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
(default: placeholder GitHub URL), `DSML_STOCK_DIRS` (owner's Mac: verified stock asset copies).
