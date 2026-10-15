# Brief: "Dungeon Settlers Modded.app" (stub + helper + icon + build script)

Lane `modsmenu`, sub-task A. Plan: ~/ds_multiplayer/research/plan_modloader.md rows 1 and 9 (read sections 1, 3, 5, 6).
Work ONLY in the worktree `~/ds_launcher_modsmenu` (branch `modsmenu`), only in these paths:
`app/` (new: stub, build script, icon, tests, NOTES.md) and a NEW file `core/src/bin/dsml_helper.rs` (+ its `[[bin]]`
entry in `core/Cargo.toml`). Do NOT change any other file of `core/src` (other lanes use it), nor `loader/` (the lead
of this lane works there), nor `~/ds_multiplayer`. Never touch Steam's install, the owner's saves or prefs (read only).
Never pop a window on the owner's screen: never start the game. Never `rm` (move throwaways to `app/old/`).
Heavy builds `nice -n 19`, max 4 threads (`cargo build -j 4`). No commits of game files or binaries:
add `app/build/` to `.gitignore`. Commit your work on branch `modsmenu` when done (message ends with the attribution
lines below). Do not push.

## What the owner sees
One app "Dungeon Settlers Modded" (download, drag to Applications, double-click). No launcher window, no installer
window. First start: copies the game from Steam's install (APFS clone, seconds), then the game's title screen with
mods. Later starts go straight to the game. Steam closed: it is started in the background first (as the MP app does).
App icon = the Mud Slogel idle sprite, nearest-neighbour scaled (no blur).

## Pieces
1. `app/stub/ds_modded.c` = CFBundleExecutable "DS Modded". Start from `~/ds_mp_steamstart/rig/mp_launcher.c` (branch
   steamstart: `ensure_steam`, `steam_probe`, `notice`, first-run save copy). Flow:
   - ROOT = `$DSMODDED_ROOT` or `~/Library/Application Support/DSModLauncher`. PROFILE = `ROOT/profiles/modded`.
     DATA = `PROFILE/data` (saves), UHOME = `PROFILE/home`. MODS STATE = `DATA/Mods/`.
   - Loader files: the app ships `Contents/Resources/dsml/` (`dsload.dylib`, `mods/<id>/...`). Copy them to
     `ROOT/loader/` when `Contents/Resources/dsml/VERSION` differs from `ROOT/loader/VERSION` (copy to a temp folder,
     clear quarantine `xattr -cr`, then rename into place; never write over a mapped dylib: the old folder is moved to
     `ROOT/retired/` with a date). The app may run translocated / read-only.
   - Game copy: if no complete play copy exists, run `Contents/MacOS/dsml-helper prepare --root ROOT` (no window) and
     read the play app path it prints (last line `PLAY <path>`) plus the build (`BUILD <id>`). If one exists, use it
     (pinned: Steam updating later does not replace it; updates are row 7, not this task). Remember the choice in
     `ROOT/profiles/modded/copy.json` {"build","app"}.
   - Errors (no Steam install, changed game files, no disk space...): one `notice()` with the helper's message, log to
     `~/Library/Logs/DungeonSettlersModded.log`, exit 1.
   - First run: copy the normal saves (READ only) `~/Library/Application Support/CanOpener/Dungeon Settlers/{Saves,
     OutGame.json,UserSetting.json,UserCampaignLog.json}` into DATA, flag file as mp_launcher.c.
   - Default state files if missing (create dirs): `DATA/Mods/active.json` =
     `{"enabled":["multiplayer","modmenu"],"order":["multiplayer","modmenu"]}`; if the file exists but "modmenu" is not
     in "enabled", add it (the Mods menu can never be switched off). `DATA/Mods/worlds.json` = `{"worlds":{}}`.
   - Steam: `steam_appid.txt` in PROFILE (cwd), SteamAppId/SteamGameId 2798330, `ensure_steam(<play app>)`.
   - Env, then `execv` of `<play app>/Contents/MacOS/Dungeon Settlers -logFile PROFILE/player.log` (+ passed args minus
     `-psn_*`), cwd = PROFILE:
     `CFFIXED_USER_HOME=UHOME`, `DSRIG_DIR=PROFILE`, `DSRIG_LIVE=1`, `DSRIG_FPS=60` (unless set),
     `DYLD_INSERT_LIBRARIES=ROOT/loader/dsload.dylib`, `DSL_MODS_DIR=ROOT/loader/mods`,
     `DSL_PROFILE=DATA/Mods/active.json`, `DSL_LOG_DIR=PROFILE`, `DSML_SESSION=DATA/Mods/worlds.json`,
     `DSML_BUILD=<copy build>`, `DSML_APP=<this .app path, real path>`, `DSML_DATA=DATA`.
   - `DSMODDED_DRY_RUN=1`: do everything except `ensure_steam` and `execv`; print the env lines above and the argv to
     stdout instead (tests). `DSMP_NO_DIALOG=1` (already in mp_launcher.c) suppresses dialogs.
   - Build: `clang -arch arm64 -O2 -Wall -mmacosx-version-min=11.0`, `-framework CoreFoundation`.
2. `core/src/bin/dsml_helper.rs` (`dsml-helper`): `prepare --root <ROOT>` = find Steam's install
   (`steam::default_steam_roots`, `find_install`), its build id, then `copy_mac::build_base` + a play copy, reusing
   `copy_mac` as is (`build_play` with no mods is fine; bundle id `com.CanOpener.Dungeon-Settlers.Modded`, name
   "Dungeon Settlers Modded"). `GameStock.gameassembly_sha256`: a built-in table, build 25846389 = the sha256 of the
   stock GameAssembly (compute it from Steam's `GameAssembly.dylib.stock_25846389` or the live file, whichever passes
   `codesign -v` with TeamIdentifier 2DKWCPLJT6 and arm64 UUID 3C5DD8BD-E371-3C0E-A674-A81042B0AC9F; see
   `~/ds_multiplayer/rig/make_mp_app.sh` step 1). Another build: accept the live GameAssembly only if `codesign -v`
   passes with that TeamIdentifier (stock Steam file); the loader then runs it unmodded with its own notice.
   Icon in the copy too (Dock shows the game process): if you can do it without changing `copy_mac.rs`, put the
   `.icns` into the play copy's Resources and set `CFBundleIconFile` BEFORE its signature (e.g. your own small play
   step in the helper: clone base, plist, icon, sign as `build_play` does). Prints `BUILD <id>` and `PLAY <path>`.
   Already present and complete = prints them at once (no work).
3. Icon: `app/icon/make_icon.py` (PIL) from `~/dungeonsettlersdb/site/anim_scenes/MudSlogel_MS_Idle_sheet.png`
   (160 x 36, 5 frames of 32 px; frame 0 = x 0..31; the slogel itself is bbox (2,4,30,34)): centre the frame in a
   square, nearest-neighbour scale to every iconset size (16..1024), `iconutil -c icns` -> `app/icon/AppIcon.icns`
   (commit the .icns and the script; it is a sprite of the game, as the wiki already publishes; fine).
4. `app/build_app.sh` -> `app/build/Dungeon Settlers Modded.app` (+ `app/build/Dungeon Settlers Modded.zip` made with
   `ditto -c -k --keepParent`): Info.plist (CFBundleExecutable "DS Modded", CFBundleIdentifier
   `com.amfilogos.ds-modded`, CFBundleName/DisplayName "Dungeon Settlers Modded", CFBundleIconFile AppIcon,
   LSMinimumSystemVersion 11.0, NSHighResolutionCapable), `Contents/MacOS/DS Modded`, `Contents/MacOS/dsml-helper`
   (cargo release build, arm64), `Contents/Resources/AppIcon.icns`, `Contents/Resources/dsml/` = copy of
   `loader/build/dsload.dylib` + `loader/build/mods/{multiplayer,gcd-cdr,modmenu}` (modmenu may not exist yet when
   you test: copy what exists, warn) + `VERSION` (sha256 of the dsml folder contents). No game file inside. Ad-hoc
   sign inside-out (`codesign -f -s -`), `codesign -v` must pass. Minimum macOS of every Mach-O = 12.0 or lower
   (see the `vtool` step in make_mp_app.sh: a dylib built with minos 27 is refused by dyld on older macOS).

## Tests (no game start, no window)
`app/test/run.sh`: (a) helper `prepare --root <tmp>` makes base + play copy from the real Steam install (APFS
clone, read only) and a second call is instant; `codesign -v` of the play copy passes; Steam's app unchanged
(compare `CodeResources` sha + mtime before/after); (b) stub with `DSMODDED_DRY_RUN=1 DSMODDED_ROOT=<tmp>
DSMP_NO_DIALOG=1` prints the expected env, creates active.json / worlds.json defaults, copies loader files and
clears quarantine (test with a quarantined copy of the app: `xattr -w com.apple.quarantine "0081;00000000;Test;" ...`);
(c) active.json without modmenu gets it added. Use a tmp ROOT under `~/ds_launcher_modsmenu/test_env/` (gitignored).
Record times (first copy seconds) in `app/NOTES.md`.

## Report
Final message under 300 words: what works, test results, the exact env/paths contract if you changed anything,
open points. Long output in `app/NOTES.md`.

Attribution lines for commits:
Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Hwvwu2MYT6Ngd3uXHM4yL2
