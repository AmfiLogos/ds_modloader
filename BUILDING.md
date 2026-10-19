# Building ds_modloader

This repo builds the mod loader, the Mac app, the Windows starter and installer, and one zip per mod.
All builds run on a Mac. The Windows files are cross-built on the Mac.

## 1. Requirements

You need a Mac with Apple silicon (arm64). `app/build_app.sh` stops if the helper is not arm64.
The versions below are the ones used for the current builds.

| Tool | Version used | Used by |
|---|---|---|
| macOS | 27.2 | all |
| Xcode (clang, codesign, vtool, lipo) | Xcode 27.1, Apple clang 21.0.0 | all Mac parts |
| Rust (rustup stable) | rustc 1.99.0, cargo 1.99.0 | `app/build_app.sh` (dsml-helper), tests |
| llvm-mingw | clang 23.1.3, target x86_64-w64-windows-gnu | all Windows parts |
| NSIS (native makensis) | 3.11 | `loader/start/win/installer/make_installer.sh` |
| Python 3 | 3.10.4 | build helpers |
| git, zip, ditto, hdiutil | git 2.54.0, Zip 3.0 | pins, zips, disk image |

Python packages: `pefile` (2024.8.26, for `loader/start/win/gen_def.py`), `lz4` (4.4.5) and `UnityPy` (1.25.0, for the
Hell table delta), `Pillow` (9.1.1, only for `app/icon/make_icon.py`).

Downloads:
- llvm-mingw: the macOS release of https://github.com/mstorsjo/llvm-mingw.
- NSIS 3.11: source and Windows zip from https://nsis.sourceforge.io. Build a native `makensis` with SCons
  (SCons 4.11.1 was used). The Windows zip gives the stubs and plugins.

Fixed tool places (no env var):
- llvm-mingw: the scripts expect it at `tools/llvm-mingw` in your home folder
  (`loader/modules/build_mods.sh:53` and `:115`, `loader/start/win/build_win.sh:10` and `:53`).
  Put it there, use a link, or edit those lines.
- `loader/modules/build_mods.sh:86` runs `/usr/local/bin/python3` for the Hell delta. That Python needs `lz4` and `UnityPy`.
- NSIS: set `MAKENSIS` to your `makensis`.

## 2. External inputs (not in this repo)

Never commit these. They are game files or are made from game files.

| Input | What it is | Env var |
|---|---|---|
| ds_multiplayer clone | https://github.com/AmfiLogos/ds_multiplayer. Must hold the commit in `loader/mp_pin.txt`. | `MP_REPO` (default `ds_multiplayer` in your home folder), `MP_PIN` |
| ds_custommodes clone | https://github.com/AmfiLogos/ds_custommodes. Must hold the commit in `loader/cm_pin.txt`. | `CM_REPO` (default `ds_custommodes` in your home folder), `CM_PIN` |
| `det_ranges.tsv` | Mac method ranges for the multiplayer determinism layer. Made by `ext/core_next/tools/gen_ranges.py` in ds_multiplayer, from an Il2CppDumper dump (`script.json`, `dump.cs`) of the Mac GameAssembly, build 25846389. See ds_multiplayer `BUILDING.md`. | `MP_DET_RANGES` (default `$MP_REPO/build/det_ranges.tsv`) |
| Windows game folder `win/25846389` | Copies of the Windows game files of Steam build 25846389 (`baselib.dll`, `GameAssembly.dll`, `global-metadata.dat`) and an Il2CppDumper dump in `dump/`. Lives inside your ds_multiplayer clone. | `DSL_WIN_GAME` (loader start), else `$MP_REPO/win/25846389` |
| `HELL_STOCK_WIN` | The stock Windows `table_assets_all_ea6555cd907771d454adb127292fd425.bundle` (StreamingAssets/aa/StandaloneWindows64). Read only. | `HELL_STOCK_WIN` |
| `HELL_STOCK_MAC` | The stock Mac `table_assets_all_ee685f7db1494c1e6bf42e948951b8dc.bundle`. Read only. | `HELL_STOCK_MAC` |
| MelonLoader guard DLL | `DSMelonGuard.dll`, built from ds_multiplayer `ext/melon/plugin/DSMelonGuard` (.NET, net6.0, against MelonLoader 0.7.3). Without it the build warns and does not embed it. | `ML_GUARD_DLL` (default under `DSMP_ROOT`) |
| floor-logger example | The owner's API 1 example mod (`floor-logger.dylib`, `mod.json`). | none: `loader/build.sh:80-81` and `loader/test/run_tests.sh:36-37` expect `DungeonSettlersMods/mods/floor-logger/` in your home folder. Edit the lines. |

Other env vars: `DSMP_ROOT` (ds_multiplayer checkout for MinHook and zlib, default `ds_multiplayer` in your home folder),
`MP_WIN_TREE` (build Windows multiplayer from a checkout, not the pin), `SKIP_MAC=1`, `LOADER_ONLY=1`, `VER` (installer version, default: the file `RELEASE`, the release number players see).

The supported game is Dungeon Settlers, Steam build 25846389.

## 3. Build order

Run each command from the repo root. Set the env vars of section 2 first.

1. Loader and Mac modules (also called by step 2):
   `zsh loader/build.sh`
   Output: `loader/build/dsload.dylib`, `loader/build/mods/<id>/`.
   `LOADER_ONLY=1 zsh loader/build.sh` builds only `dsload.dylib`.
2. Every mod for Mac and Windows:
   `HELL_STOCK_WIN=... HELL_STOCK_MAC=... zsh loader/modules/build_mods.sh`
   Output: `loader/build/mods_all/mac/<id>/` and `loader/build/mods_all/win/<id>/`. Check a set with
   `zsh loader/modules/build_mods.sh --check`.
3. One zip per mod:
   `zsh loader/modules/package_mods.sh`
   Output: `loader/build/packages/<id>-<version>.zip`.
4. Windows loader, starter and kit:
   `MODS="$(echo $PWD/loader/build/mods_all/win/*)" zsh loader/start/win/build_win.sh`
   (full paths: the script changes folder first). Output in `loader/build/win/`: `baselib.dll`,
   `Dungeon Settlers Modded.exe`, `kit/`, `Dungeon Settlers Modded - Windows.zip`.
5. Windows installer:
   `MAKENSIS=<your makensis> zsh loader/start/win/installer/make_installer.sh`
   Output: `loader/build/win/Dungeon-Settlers-Modded-Setup.exe`.
6. Mac app (needs steps 1 and 2, and `app/icon/AppIcon.icns`, which is in git; `python3 app/icon/make_icon.py` remakes it):
   `zsh app/build_app.sh`
   Output: `app/build/Dungeon Settlers Modded.app` and `.zip`.
7. Mac disk image: `zsh app/make_dmg.sh`. Output: `app/build/Dungeon-Settlers-Modded-Mac.dmg`.
8. Sign every release before the upload (the loader's signed auto-update, v0.1.16 and later; `loader/src/dsl_sig.c`):
   `zsh tools/release_sign/sign_release.sh <repo> <tag> <files...>`, one call per release (the kit `v0.1.N`, and each
   mod release `<id>-v<ver>` of ds_modloader, ds_multiplayer and ds_custommodes). It writes `release-manifest.json` and
   `release-manifest.json.sig` next to the first file: upload both with the release's files. A loader refuses a release
   without them. The private key is only in the owner's macOS Keychain (service `ds-modloader-release-signing`); the
   script reads it there. For a kit it first runs `python3 tools/release_sign/check_keys.py <kit files>`: every loader in
   the .dmg, the zips and the unpacked Setup.exe must hold both release keys and never the public dev key (a test build,
   `-DDSL_SIGN_DEV_KEY`, as `mac_gate.sh` makes). Any mismatch stops the signing; run it in the release gate too.

BepInEx and MelonLoader are not built here. `app/bepinex/install_bepinex.sh` and `app/melon/install_melon.sh` go into
the app. At first use they download a release zip from ds_multiplayer GitHub Releases and check its sha256.

Old outputs are moved to `old/` folders, never deleted. Each build scans its output for your user name and fails if found.

## 4. Tests

- Loader host test (fake GameAssembly, no game): `zsh loader/test/run_tests.sh`. Needs the floor-logger files (section 2).
- Update host tests (local release server, no game): `zsh loader/test/update/signed_test.sh`, `getmods_test.sh`,
  `kitonly_test.sh`. The test release server signs with the public dev key (`dev_keys/dev.key`); only loaders built with
  `EXTRA_CFLAGS=-DDSL_SIGN_DEV_KEY` trust it (`mac_gate.sh` does that for its test apps, then rebuilds the release loader).
- Rust: `cargo test -p dsml-core` and `cargo test -p ds-unit-builder`.
  The game test `cargo test -p dsml-core --test e2e_mac -- --ignored --nocapture` needs the game from Steam.
- App: `zsh app/test/run.sh`. Needs the built app and the game installed by Steam.
- `loader/test/*_test.py` and `tests/*.py` start real game copies through the ds_multiplayer test rig.

## 5. Unit Builder icons

The Unit Builder screen of `src-tauri/` shows game icons from `ui/bicons/`.
These icons are game art. They are not in the repo, and `ui/bicons/` is in `.gitignore`.
You extract them from your own installed game:

```
python3 builder/gen/extract_icons.py
```

- Needs Python 3 with `UnityPy` and `Pillow` (`python3 -m pip install UnityPy Pillow`).
- Needs Dungeon Settlers from Steam, build 25846389 (Mac or Windows). The script reads the game files and never changes them.
- The script finds the game in your Steam libraries. If it does not, add `--game <folder>` or set `DS_GAME_DIR`.
  The folder can be the Steam folder `Dungeon Settlers`, the `.app`, or the game's Data folder.
- It writes 876 icons, then `ui/bicons/manifest.txt`. One icon (`stats/Talent_DontKnow.png`) is not in this game build.
- Without `ui/bicons/manifest.txt`, `cargo build -p ds-mod-launcher` warns and `src-tauri/tests/ui_smoke.py` stops. Both tell you to run the script.
