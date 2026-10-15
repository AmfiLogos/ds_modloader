# Dungeon Settlers Mod Launcher (step 1, Mac)

## How to test the launcher (owner's Mac)
1. Open Steam and stay signed in (the launcher starts the game through your Steam copy).
2. In Finder, open your Applications folder (Go > Home > Applications) and double-click "DS Mod Launcher".
3. If macOS says it cannot check the app: System Settings > Privacy & Security > "Open Anyway", then open it again.
4. The "Multiplayer" mod is already in the list and switched ON. Nothing to download.
5. Click Play. The first Play makes a private copy of the game (about one minute), then the game opens.
6. In the game: Multiplayer > Host a game, as with the LAN app. Your normal Steam game and saves are not changed.
7. To play without the mod: switch "Multiplayer" off in the launcher, then click Play.

Plan: `~/ds_multiplayer/research/plan_launcher.md`. Owner decisions: `~/ds_multiplayer/PLAN.md`, section "Launcher".

- `core/` (Rust crate `dsml-core`): Steam detection + build id (`steam.rs`), signed index + checksums
  (`crypto.rs`, `fetch.rs`), pin / update rules (`decide.rs`), Mac side copy per build (`copy_mac.rs`, port of
  `rig/make_mp_app.sh`), launch with the code mods (`launch.rs`, same env as `rig/mp_launcher.c`),
  Windows stubs (`windows.rs`), mods per world + groups (`worlds.rs`), gameplay/client scope + mod-set hash +
  join plan (`join.rs`), difficulties (`difficulty.rs`: the game's Devastation unlock read from
  OutGame.json `DifficultyUnlockSaveData.IsDownfallUnlocked`, custom difficulties such as Hell as mods). `dsml-tool`: keygen / sign / verify / file entry / patches (stock vs modded -> byte patches) for the publisher.
- `src-tauri/`: Tauri 2 shell (commands: status, refresh_index, set_enabled, play, remove_copy).
- `ui/`: plain HTML/CSS/JS, game palette, Cinzel + Lora (OFL, `ui/fonts/OFL-*.txt`).
- `dev_keys/`: the shared signing key pair (public on purpose, see its README).

Tests:
- `cargo test -p dsml-core` : unit tests + local-server download tests.
- `cargo test -p dsml-core --test e2e_mac -- --ignored --nocapture` : fake Steam library (APFS clone of the
  real app, read only) -> signed index -> copies -> hidden launch (-batchmode, background-only, guard kills on
  any visible window) -> the multiplayer dylib answers on its socket. Checks the owner's prefs, settings and
  real GameAssembly are unchanged. Test files go to `test_env/` (git-ignored).
  `worlds_hell_and_client_mod_compose` (same command): two worlds, Hell (gameplay) + weapon slot (client) as
  byte-patch mods on one copy, plain world on another; the Hell copy boots to the title screen hidden.
- `cargo test -p dsml-core --test e2e_mac owner_app_smoke -- --ignored --nocapture`: the installed owner app
  (`OWNER_APP`, default `~/Applications/DS Mod Launcher.app`) with no window (`DSML_SMOKE=status|prepare`) on a fresh
  launcher folder: bundled mod list installed, multiplayer listed ON, Play's copy made, game boots hidden, socket answers.
- `tests/weaponslot_pair.py <PAIR>`: host WITH the weapon-slot mod, guest WITHOUT, one lockstep game on a
  leased rig pair (copies from `make_pair_copies`). Result in `test_env/pair/result.json`.

Owner test build (bundled mod list, no published index yet): `tools/make_seed.sh` (released multiplayer build from
`~/ds_multiplayer/rig/lan_latest`, signed with the shared key in `dev_keys/`) then `cd src-tauri && cargo tauri build --config
tauri.owner.conf.json`. See `NOTES_ownerapp.md`.

Env: `DSML_ROOT` (launcher folder, default `~/Library/Application Support/DSModLauncher`), `DSML_INDEX_URL`
(default: placeholder GitHub URL), `DSML_STOCK_DIRS` (owner's Mac: verified stock asset copies; also read from
`<root>/stock_dirs.txt`, one folder per line), `DSML_SEED_DIR` (bundled mod list, default `Contents/Resources/seed`),
`DSML_SMOKE` (tests, no window).
