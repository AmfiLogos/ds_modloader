# Windows launcher (branch winlauncher, 2026-10-15)

Based on branch `ownerapp` (bundled signed mod list, multiplayer ON at the first run). Same UI and core as the Mac app.

## What it does on Windows
- Finds the game: `HKCU\Software\Valve\Steam\SteamPath`, then HKLM `InstallPath` (64 and 32 bit), then
  `Program Files (x86)\Steam`, `Program Files\Steam`, `Steam`, `SteamLibrary` on drives C to Z (the friend kit's
  `find_game.bat` order), then every library in `libraryfolders.vdf`; build id from `appmanifest_2798330.acf`.
  `DSML_STEAM_ROOTS` (path list) replaces this for tests.
- Copy per build in `%LOCALAPPDATA%\DSModLauncher\games\<build>\` (`core/src/windows.rs`):
  - `base\` = full copy of Steam's folder (about 700 MB; NTFS has no clone). Every file is checked against the stock
    list in the signed index (`games.<build>.win-x64.files`, made by `dsml-tool winstock`). A friend-kit install in
    Steam's folder is handled: the stock `baselib_orig.dll` is used, the kit's files are skipped. Changed files:
    refused with the Steam "Verify integrity of game files" steps.
  - `play-<key>\` = base hard-linked + the mods. A file a mod changes is always a new file renamed into place, so base
    stays stock. Code mod rule: a file named in `inject` is a proxy DLL; the game's file of that name becomes
    `<name>_orig.dll` (multiplayer: `baselib.dll` -> `baselib_orig.dll`). Other files go next to the exe
    (`det_ranges_win.tsv`). `steam_appid.txt` next to the exe.
  - Steam's folder is only read. The normal game from Steam stays stock.
- Play (`core/src/launch.rs`): saves copied once from `%USERPROFILE%\AppData\LocalLow\CanOpener\Dungeon Settlers` into
  `profiles\default\data` (the DLL redirects the save folder there through `DSRIG_DIR`); Steam closed -> `steam.exe
  -silent`, wait up to 90 s for a signed-in user (registry `ActiveProcess`), result in `profiles\default\launcher.log`.
  The game starts in every case: the multiplayer DLL turns the game's Steam check off. Game started detached.
- Remove an old copy (the player said yes): deleted on Windows (a full copy is ~700 MB), retired on the Mac as before.
- Installer: NSIS, current user (no admin), WebView2 bootstrapper if the runtime is missing. Uninstall
  (`src-tauri/windows/hooks.nsh`): removes the app, `games\`, `cache\`, `retired\`; keeps `profiles\` (saves),
  `builder\` and `state.json`. "Delete the application data" ticked: the whole `DSModLauncher` folder goes.

## Build (on this Mac, no Windows machine needed)
```
tools/make_seed.sh   with WIN_DLL=<released baselib.dll> WIN_RANGES=<det_ranges_win.tsv>   (one index, Mac + Windows)
source tools/win_env.sh; export PATH=~/tools/nsis/bin:$PATH
cd src-tauri && nice -n 19 cargo tauri build --target x86_64-pc-windows-gnu
-> target/x86_64-pc-windows-gnu/release/bundle/nsis/DS Mod Launcher_0.1.0_x64-setup.exe
```
- Toolchain: `~/tools/llvm-mingw` (same as ds_multiplayer win/build_win.sh), Rust target `x86_64-pc-windows-gnu`.
  `tools/mingw_link.sh` maps Rust's libgcc / libgcc_eh to llvm-mingw's libunwind (stub folder `test_env/mingw_stubs`).
- makensis 3.11 for macOS: built from the NSIS source with scons (`~/tools/nsis_build`), Windows stubs and plugins
  from the official nsis-3.11.zip, installed in `~/tools/nsis` (data folders next to `bin/`).
- `src-tauri/tauri.windows.conf.json` is merged for the Windows target (nsis, seed resources, currentUser).

## Tests (CrossOver bottle DSMP only)
- `cargo test -p dsml-core`: windows.rs copy rules run on the Mac too (stock base, kit folder, changed file,
  data + patch mods, path escape), Steam start rules (`steam_start_rules`).
- `tools/win_smoke.py`: the real exe under wine, fake Steam library `C:\dsml_winlauncher\steam` (clone of the bottle's
  stock game), fresh launcher folder: status -> Play (copy + game `-batchmode -nographics`) -> DLL answers on
  127.0.0.1:47899 -> quit. 2026-10-15 17:54: PASS (all 11 checks; copy + start 9 s).
- `tools/win_install_test.py <setup.exe>`: silent install -> prepare -> silent uninstall -> what stays / goes.
  Use an installer built with `--config '{"bundle":{"windows":{"webviewInstallMode":{"type":"skip","silent":null}}}}'`
  so the test bottle does not get the WebView2 runtime.
- Not tested here: the window (WebView2 UI) on Windows, a real Steam start (no Steam in DSMP), a real Windows PC.

## Open points
- Cross-play mod-set hash: `join.rs modset_lines` hashes the files of THIS platform, so a Mac launcher host and a
  Windows launcher guest send different hashes and the host refuses (DSNET_REJ_MODSET). Sent to the lead; not changed here.
- `UnityCrashHandler64.exe` is not in the stock list (not needed to play; the bottle's stock copy has none).
- Unsigned: SmartScreen shows "Windows protected your PC" -> "More info" -> "Run anyway" (plan section 8).
- The dev key signs the seed (NOTES_ownerapp.md). Same rule before a public release.

## Cross-platform mod-set hash (finding for the in-game loader, 2026-10-15)
- `join.rs modset_lines` puts the sha256 of the mod's files FOR THIS PLATFORM in each line, so Mac and Windows get different hashes for the same mod.
- `dsnet_server.c modset_refused` compares the hashes with strcmp, so a Mac host refuses a Windows guest (DSNET_REJ_MODSET) even when both run the same mod versions.
- Lead-approved fix (not built, owner stopped the lane): hash the files of EVERY platform listed in the signed index (platform keys sorted), the same for patches, and use the same rule in the loader's dsload_modset.json check.
- Also: leave the multiplayer mod itself out of the gameplay mod set (HELLO already checks build + protocol), so a guest with no session (friend kit) can join a host that runs only multiplayer.
- Test to add: for one index, the Mac line equals the Windows line.
