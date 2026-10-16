# Dungeon Settlers Mod Launcher (step 1, Mac)

**Roadmap:** what we work on now and next, and where you can help: [ROADMAP.md](ROADMAP.md).

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
