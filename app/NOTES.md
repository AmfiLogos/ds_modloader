# Dungeon Settlers Modded.app - notes (lane modsmenu, sub-task A, 2026-10-15)

## Files
- `app/stub/ds_modded.c` - CFBundleExecutable "DS Modded" (from steamstart's `rig/mp_launcher.c`).
- `core/src/bin/dsml_helper.rs` - `dsml-helper prepare --root <ROOT> [--icon <icns>]` (no `--icon`: uses
  `<helper>/../Resources/AppIcon.icns`). Reuses `copy_mac::build_base`; own small play step (clone base, plist, icon,
  ad-hoc sign) because `copy_mac::build_play` cannot add the icon before signing. `copy_mac.rs` unchanged.
- `app/icon/make_icon.py` -> `app/icon/AppIcon.icns` (Mud Slogel idle frame 0, bbox (2,4,30,34) centred in 32 x 32,
  nearest neighbour to 16..1024).
- `app/build_app.sh` -> `app/build/Dungeon Settlers Modded.app` + `.zip` (2.1 MB). Older builds go to `app/old/`.
- `app/test/run.sh` - tests (a)-(d), no game start.

## Contract (as in the brief, nothing changed)
- ROOT = `$DSMODDED_ROOT` or `~/Library/Application Support/DSModLauncher`; PROFILE = `ROOT/profiles/modded`;
  DATA = `PROFILE/data`; UHOME = `PROFILE/home`; loader = `ROOT/loader/` (from `Contents/Resources/dsml/`).
- Play copy: `ROOT/games/<build>/play-modded/DungeonSettlers.app` (marker `play-modded.json`), base copy
  `ROOT/games/<build>/base/` (`base.json`), files outside Steam's seal -> `ROOT/games/<build>/not_game_files/`.
- Pin: `PROFILE/copy.json` {"build","app"}; used as long as the app's game binary exists.
- Helper stdout: `BUILD <id>`, `PLAY <path>`; error = last line `ERROR <message>`, exit 1 (stderr -> stub log).
- Env: as the brief. Dry run prints `ENV k=v`, `CWD`, `ARGV` lines.
- Stock GameAssembly table: build 25846389 = `e50f729b63df79415b6b3e5753943ad186cba282bc058c739d01e409b3a76876`
  (Steam's `GameAssembly.dylib.stock_25846389`: codesign -v ok, TeamIdentifier 2DKWCPLJT6, arm64 UUID
  3C5DD8BD-...; the LIVE file on the owner's Mac is modded, TeamIdentifier not set). Other builds: the live file
  if it carries TeamIdentifier 2DKWCPLJT6.
- Extra stock dirs for mod-changed resources: `$DSML_STOCK_DIRS` + `~/ds_weaponslot/b25846389/assets/work/stock_aa`
  when it exists (owner's Mac, same as make_mp_app.sh).

## Test results (app/test/run.sh, 2026-10-15): 0 failures, 2 runs
- First prepare (base + play copy, APFS clone, seal check of every resource): 7.1 s and 6.6 s. Second call: 0.02-0.03 s.
- Play copy `codesign -v` ok; Steam's app unchanged (CodeResources sha + mtimes of CodeResources, app, GameAssembly).
- Stub dry run: all 14 env values, cwd, argv (-psn_ dropped), steam_appid.txt, defaults, copy.json, loader copied
  with quarantine cleared, unchanged VERSION = no copy, changed VERSION = old folder to `ROOT/retired/loader-<date>-<pid>`.
- active.json without modmenu: appended (also to an empty list); "order" left as is (the loader falls back to the
  enabled position).
- Helper error (no Steam install, fake HOME): exit 1, the helper's message reaches notice().

## Findings / open points
- **Gatekeeper on macOS 27:** a QUARANTINED copy of the app whose stub is started from a shell was SIGKILLed and
  macOS moved the whole test app to the Trash (`~/.Trash/Dungeon Settlers Modded*.app`, two throwaway test
  copies, left there). `spctl -a -t exec` = rejected (ad-hoc signed). A friend who downloads the zip will get the
  Gatekeeper block; whether macOS 27 still offers "Open Anyway" for it (instead of only "Move to Trash") was NOT
  tested (it needs a Finder double-click = a window). Options: Developer ID + notarization, or a documented
  `xattr -dr com.apple.quarantine` step. The test now quarantines only Resources (the loader files), which is the
  case after "Open Anyway".
- Loader dylibs are NOT re-signed when already validly signed (their sha256 is checked by dsload and by the MP
  mod-set check). A dylib with minos > 12 gets rewritten by vtool and the script warns that its sha256 changes.
- `modmenu` is not built yet (`loader/build/mods/modmenu` missing): build_app.sh warns and ships without it.
- No lock against two stubs preparing at once (the second one's build folder would retire the first's
  `.building`). Rare (double double-click on the very first start).
- First-run save copy never overwrites an existing file in DATA.
- Unity's player log goes to `PROFILE/player.log`; stub log `~/Library/Logs/DungeonSettlersModded.log`.
