# Dungeon Settlers Modded

A mod loader for Dungeon Settlers (Steam), for Mac (Apple chip) and Windows. It plays a copy of the game with mods.
Steam's game, your normal saves and your settings do not change.

It is made for Steam build 25846389 of Dungeon Settlers. Other game builds are not supported.

## The three repositories

- ds_modloader (this repo): the mod loader, the Mac app, the Windows installer, and two small mods
  (Global cooldown fix, Hell difficulty). Its releases page has the installers (`Dungeon-Settlers-Modded-Mac.dmg`,
  `Dungeon-Settlers-Modded-Setup.exe`).
- [ds_multiplayer](https://github.com/AmfiLogos/ds_multiplayer): the Multiplayer mod.
- [ds_custommodes](https://github.com/AmfiLogos/ds_custommodes): the Custom Modes mod (My Units, Unit Builder,
  Gallery, Test Fights, Foreign Expedition, PvP).

The installers hold the loader and all four mods. You only need the separate mod zips to add or update one mod.

## What you need

- Dungeon Settlers on Steam, build 25846389, and Steam open and signed in.
- Mac: a Mac with an Apple chip (M1 or later).
- Windows: a 64-bit Windows PC.

## Download and install

Get the installers from the latest release on this repo's [releases page](https://github.com/AmfiLogos/ds_modloader/releases).
A release named `v0.1.N` is the installer release. Its number is the mod loader's version.

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

The default mods are inside: Multiplayer, Custom Modes, Global cooldown fix, Hell difficulty (off until you turn it on).
On the title screen, click Mods to turn mods on or off.

## Auto-update

At the title screen, the switch "Auto-update" is in the top-right corner. It is on by default.

- On: while you play, the game downloads a new version and checks it. The next time you start the game, a small
  window "Updating Dungeon Settlers Modded to 0.1.N" shows for a few seconds, then the game starts as usual. No
  question, no restart.
- Off: nothing is downloaded. A button "Update to 0.1.N" shows instead. Click it: the update installs the next time
  you start the game.

Nothing installs while the game runs. If the new version does not reach the title screen twice in a row, the old
version comes back and that version is not offered again. Your saves are not changed. Without internet the game starts
as usual and checks again next time.

## Crash reports

At the first start, the game asks: "Send crash reports to help fix bugs?". Nothing is sent before you click Yes.

- A report is sent only after a crash, a freeze, a multiplayer desync report, a failed join or a multiplayer
  view that showed nothing. It is sent at the next
  start or at the title screen, never while you play. At most 3 reports per day.
- It holds the logs (`dsload.log`, `player.log`, `rig.log`, the game's `ErrorLog.txt`), desync report summaries, the
  versions of the loader, mods and game, your OS and CPU type, and a random install id (not a hardware id).
- Before sending, your computer removes user names in folder paths, your user, full and computer names, Steam names and
  Steam ids, player names, IP and MAC addresses and e-mail addresses.
- Reports are kept 90 days, then deleted automatically. The server does not keep your IP address with a report.
- To change your answer: title screen, Mods, the switch "Send crash reports".

Privacy note: [crashrep/PRIVACY.md](crashrep/PRIVACY.md), also at https://ds-crashrep.vercel.app/privacy.

## Add a mod

Each mod has its own download: `<mod>-<version>.zip` (for example `gcd-cdr-0.1.2.zip`). One file works on Mac and Windows.

1. Download the zip. Do not unzip it.
2. Start the game, click Mods, then click "Open mods folder". Put the zip into that folder.
   (Or open the folder yourself. Mac: in Finder, click Go, Go to Folder, paste
   `~/Library/Application Support/DSModLauncher/mods`. Windows: press Windows+R, paste
   `%LOCALAPPDATA%\Dungeon Settlers Modded\mods`, press Enter.)
3. In the Mods window, click Refresh. The mod shows up in the list. Turn it on and click Apply.
   The game restarts behind a loading screen, with the mod on.

A zip of a mod you already have replaces that mod. A file that is not a mod moves to the folder `.failed`.

## Mod settings

Some mods have settings. In the Mods window, click Settings next to the mod.

- Gameplay settings change when you click Apply (at the title screen). In multiplayer, every player needs the same
  values: a player with other values cannot join, and the message names the setting and both values.
- Display settings change at once and are only for your screen.
- Default values puts every setting of that mod back to its default. Undo changes drops what you did not apply.

For mod makers: declare the settings in `mod.json` ("settings") and read them with `dsl_get_bool` / `dsl_get_int` /
`dsl_get_choice` (`loader/sdk/dsl_sdk.h`, API 5; the format is described there, above `settings_get_bool`).

## Hook list

For other mod makers (BepInEx, MelonLoader, patch audits): [docs/hooks.json](docs/hooks.json) lists every native
hook and code patch that the loader and the official mods (multiplayer, custom-modes, gcd-cdr, hell-difficulty,
zetu-suite) put into GameAssembly, for game build 25846389 on Mac (arm64) and Windows (x64).
[docs/hooks.tsv](docs/hooks.tsv) is the same list as a table. Each loader release has both files attached.

Each entry gives the owner, the Il2Cpp name of the hooked method (`Namespace.Class$$Method`, as Il2CppDumper names
it), the offset in `GameAssembly.dylib` / `GameAssembly.dll`, the kind (detour or in-place patch), when it is
installed (always, a mod setting, or a test-only switch), and the name the loader log prints
(`<label>: hooked <rva> for <owner>` in `dsload.log`). The top of the JSON file explains every field.

- The list is made from the sources by `tools/hooks/hooklist.py gen` (it needs the Il2CppDumper dumps of the game
  build). Sites that the script cannot follow by itself are in `tools/hooks/sites.tsv`.
- Every loader build runs `tools/hooks/hooklist.py check`: it fails when the sources hook something that
  `docs/hooks.json` does not list. Then run `gen` and commit both files.
- Not in the list: hooks outside GameAssembly (the Mac window and focus handling in `loader/start/mac`, the Windows
  window hooks of hidden test copies in `loader/start/win/dsl_soft_win.c`).

## Where your saves are

Your normal saves are played in place. The modded game lists the worlds of your normal game's save folder and
loads them from there. When you save, it writes back to that same folder. So the same world goes on in both games.
There is no copy and no import step.

- Mac: `~/Library/Application Support/CanOpener/Dungeon Settlers/Saves`
- Windows: `%USERPROFILE%\AppData\LocalLow\CanOpener\Dungeon Settlers\Saves`

Worlds that exist only in the modded game (worlds you start or host there, and multiplayer worlds) stay in the
modded game's own folder, `data/Saves`. The normal game never lists them. Both kinds of worlds show in one Load list
in the modded game.

The modded game's own folder is `data` in:

- Mac: `~/Library/Application Support/DSModLauncher/profiles/modded`
- Windows: `%LOCALAPPDATA%\Dungeon Settlers Modded\profiles\modded` (or the folder you chose in the installer)

Uninstalling keeps this folder.

Some mods change a world in a way that the game without mods cannot read. When such a mod is on (mod.json
`"save_dependent": true`) and you load a normal save, the game asks first. It names the mods, and you choose Load or
Cancel. It asks once per world and mod set. Some of these mods also make a dated copy of that world's save files
first, in `data/Mods/save_backups/`. The warning then names that folder.

When you delete a normal save in the modded game, a second question asks if you want to delete it in the game
without mods too. Cancel deletes nothing.

Older versions (before v0.1.13) copied your normal saves into `data/Saves` on the first start. If such a copy has
the same name as a normal save, it is moved to `data/Mods/save_copies/` and never deleted. If you played the copy, a
notice on the title screen says which file is newer and where the copy is kept.

Mod data: each mod has its own folder, `data/ModData/<mod id>/`. Mods keep their settings and progress files there.
A mod update or turning a mod off or on does not change it. In multiplayer, a mod can mark some of these files as
gameplay files (mod.json `"sync"`). A guest then plays with the host's copy of those files. The guest's own files
are put aside during the session and come back after it. If the game closes during a session, they come back at the
next start. For mod makers: `loader/NOTES.md`, "Mod data folder + multiplayer sync".

Steam Cloud backup: the mod loader copies the modded-only saves (`data/Saves`) to your own Steam Cloud, as files
named `dsmodded_<save file>`. It uploads only while plenty of space stays free for the game's own cloud saves (at
least 1 GB, or 20 times what the game uses, whichever is bigger). If space is short, it pauses. On a new computer,
the modded game downloads the saves it does not have. If one save changed on two computers, the newest one wins and
the other copy goes to `data/Mods/cloud_backups/`. When you delete a world in the modded game, its cloud copy goes
too. The game's own cloud saves never change. Multiplayer temporary saves are never uploaded.

## Known limits

- One game build only (25846389).
- Saves made with Hell difficulty may not work without the mod loader. The Mods window and the world's hover card say so.
- Windows was tested in CrossOver on a Mac, not yet on a real Windows PC with Steam.
- Multiplayer was tested on one local network, not yet between two homes over the internet.
- The Mac app and the Windows installer are not signed, so the system warns on the first start.

## Report a bug

Open an issue on this repo's Issues page. Say your system (Mac or Windows), the installer release (for example v0.1.22) and
what you did. Attach the `.log` files from this folder (not the `data` folder, which holds your saves):
Mac: `~/Library/Application Support/DSModLauncher/profiles/modded` (and `~/Library/Logs/DungeonSettlersModded.log`).
Windows: `%LOCALAPPDATA%\Dungeon Settlers Modded\profiles\modded`.

## Make a mod

See [docs/MODDING.md](docs/MODDING.md): how to make a native (C), BepInEx or MelonLoader mod for this loader, and how
the loader compares with BepInEx, MelonLoader and changing the game files (startup times measured).

## Build it yourself

See [BUILDING.md](BUILDING.md). The license files that ship with each release are in [licenses/](licenses/).

Releases for auto-update: tag `v0.1.N` in ds_modloader with the assets `Dungeon-Settlers-Modded-Mac.dmg` (or the
`.zip`) and `Dungeon-Settlers-Modded-Windows.zip` (the kit zip; the Setup.exe alone is not enough), built with the same
number in `RELEASE` (or `DSM_RELEASE`): a kit whose RELEASE is not the tag's number is refused. Mod zips
`<id>-0.1.N.zip` (tag `<id>-v0.1.N`) in their own repos, the same number as their mod.json. GitHub's asset digest is
the checksum (a `<asset>.sha256` file also works). Tests: `loader/test/update/` (Mac, local release server) and
`loader/start/win/test/update_test.sh` (wine).

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
