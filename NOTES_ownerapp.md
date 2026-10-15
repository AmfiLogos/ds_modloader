# Owner test app (branch ownerapp, 2026-10-15)

Goal: one app the owner opens from ~/Applications and tests on his own Mac, with the multiplayer mod in it and ON.

## What changed
- `core/src/seed.rs`: a signed mod list + its files inside the app (`Contents/Resources/seed`). At start the launcher
  installs it into its own folder when it is newer than the list it has (same signature check and replay rule as a
  download; files go to the normal cache, so Play never downloads). First run only: multiplayer ON for new worlds.
- `src-tauri/src/main.rs`: installs the seed at start; reads `<root>/stock_dirs.txt` (the owner's verified stock assets,
  because his live Steam install has mod-changed files); `DSML_SMOKE=status|prepare` runs the same code with no window.
- `tools/make_seed.sh`: makes `src-tauri/seed` (git-ignored: it holds the dylib) from `~/ds_multiplayer/rig/lan_latest`.
  Mod version 0.N.0 and index seq N for release vN, so a later release (v11 -> seq 11) replaces it at the next start.
- `src-tauri/tauri.owner.conf.json`: adds the seed to the bundle. The normal build (no overlay) has no seed.
- `core/tests/e2e_mac.rs` `owner_app_smoke`: the installed app, no window, fresh launcher folder -> listed ON ->
  copy -> hidden boot -> socket answers -> owner's prefs / settings / real GameAssembly unchanged.

## Still open
- The index URL is still a placeholder (`fetch.rs`): "Check for updates" falls back to the bundled list.
- The seed also carries the DEV-ONLY no-Steam patch (`dev_only`, hidden from players) for the hidden tests.
- Signing key: owner decision 2026-10-15, the mod list is open for anyone to change; the shared key in `dev_keys/`
  stays. No real key pair is planned.

## Rebuild with a new multiplayer release
`tools/make_seed.sh && cd src-tauri && CARGO_BUILD_JOBS=4 nice -n 19 cargo tauri build --config tauri.owner.conf.json`,
then install the app (old one to ~/Applications/old_versions/ with a date-time suffix) and run `owner_app_smoke`.

## Test results 2026-10-15
- 18:00 `owner_app_smoke` PASS on the installed app with lan v11 (multiplayer 0.11.0, dsrig.dylib e58c6dec...): listed ON,
  copy from the real Steam install (read only), hidden boot to the title screen, socket answered, owner's prefs /
  UserSetting.json / real GameAssembly unchanged. v10 app moved to ~/Applications/old_versions/DS Mod Launcher-2026-10-15-1800.app.
- Socket path limit: a control socket path over 104 bytes was cut (`ctl.soc`) and nothing answered. Fix on
  ds_multiplayer branch `sockpath` (rig_sock.c: long path binds at /tmp/dsrig-<uid>-<hash>.sock, link at the asked
  path). `long_socket_path_answers` PASS with that dylib (148-byte path). Not in v11: it ships with the next merge.
