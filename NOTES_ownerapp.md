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

## MUST before any friend gets the launcher
- The bundled list is signed with `dev_keys/dev.key`, which is PUBLIC in this repo: anyone can sign a list this app
  trusts. The owner makes the real key pair offline (`dsml-tool keygen`, secret never in a repo) and `TRUSTED_KEYS`
  in `src-tauri/src/main.rs` lists only the real keys (dev_keys/README.md). Then re-sign the seed with the real key.
- The index URL is still a placeholder (`fetch.rs`): "Check for updates" falls back to the bundled list.
- The seed also carries the DEV-ONLY no-Steam patch (`dev_only`, hidden from players) for the hidden tests. Drop it
  from a public seed.

## Rebuild with a new multiplayer release
`tools/make_seed.sh && cd src-tauri && CARGO_BUILD_JOBS=4 nice -n 19 cargo tauri build --config tauri.owner.conf.json`,
then install the app (old one to ~/Applications/old_versions/ with a date-time suffix) and run `owner_app_smoke`.
