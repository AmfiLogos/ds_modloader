# i18n lane (loader, Mods window, starters): notes

Branch `i18n` from main e41fa06. The loader, the Mods window, the soft-restart progress, the cloud backup / import and
both starters show their text in the game's language (the 10 `LanguageType` languages) and follow a change made in
Options at the title. No translation is in the branch: `loader/lang/en.json` holds the English keys (159, 7 plural);
the translation passes add `loader/lang/<code>.json` later. No language file = English, so every test still runs in
English.

## Shared core
- `loader/src/ds_i18n.h` and `tools/i18n_tool.py`: copied unchanged from the i18n core (second version, 2026-10-17:
  `TRN_NOOP(one, other)`, `\u` escapes in the extractor, `format_arg` on `dsi_tr` / `dsi_trn`, Windows `_wfopen`,
  `-DDSI_PREFIX`, not used here). With `format_arg` the compiler checks printf arguments against the English key:
  the Mac build, `build_mods.sh`, the Windows build and the test builds give no `-Wformat` warning.
- Windows lang folder in UTF-8 (the core opens it with `_wfopen`): the starter converts with `CP_UTF8` (was `CP_ACP`);
  baselib.dll fills `dsl_core_self_utf8` (`dsl_os.h`) from `GetModuleFileNameW` before `dsl_core_log_open`, and
  `dsl_lang_early` takes the lang folder from it. `self_path` stays ANSI for the core's own `fopen` calls.
- The implementation (`DSI_IMPLEMENTATION`) is in `loader/src/dsl_main.c` (dsload.dylib / baselib.dll), in
  `app/stub/ds_modded.c` (Mac starter) and `loader/start/win/ds_modded_win.c` (Windows starter). Test builds that link a
  loader file on its own define it too (`loader/test/cloud/cloud_test.c`, `loader/test/winbie/bie_fail_test.c`).
- `loader/src/dsl_internal.h` includes `ds_i18n.h`, so every loader file has `TR()`.

## Language files and the language
- Files: `<folder of the loader binary>/lang/<code>.json`: Mac `ROOT/loader/lang` (the app ships them in
  `Contents/Resources/dsml/lang`, the stub copies dsml/ to ROOT/loader), Windows `ROOT\game\lang` (next to
  `game\baselib.dll`; the kit has `files\lang`, `install.bat` copies it). The starters read the same files.
- Before il2cpp (start notices): `dsl_lang_early()` reads `GeneralSettingData.SaveCurrentLanguageType` from
  `$DSML_DATA/UserSetting.json` (else `$DSRIG_DIR/data`, else the log folder).
- Live: `dsl_lang_poll()` once per frame in the kit's EventSystem.Update tick, from the title on (MainFlow scene >= 1:
  during the boot the static is still 0): `Refactor.Setting.LanguageSetting.GetCurrentLanguage` resolved by name.
- On a change (`dsl_ui.c kit_lang_changed` + `dsl_reg.c dsl_reg_lang_changed`): the kit drops its fonts and makes its
  title-button clones again (stock font + material of the new language), mod.json texts switch, "Mods" and
  "Import a save" are relabelled, an open Mods window is built again (switches kept: `dsl_win_relang`), the progress
  view is built again (`dsi_generation`).
- Starters: language from `profiles/modded/data/UserSetting.json`; before the first start (not copied yet) the normal
  game's file. Mac lang folder: `ROOT/loader/lang`, else the app's `Resources/dsml/lang`.

## Fonts (DSLUi kit, `loader/src/dsl_ui.c`; API unchanged)
- Titles: `LanguageSetting.GetFont(VeryDecorative = 2)` instead of "Cinzel-Bold SDF" by name; body:
  `GetFont(EasyRead = 0)` with the font asset's own material (no outline, as before) instead of the notice box font
  captured once. Both are dropped and fetched again after a language change. The old lookups stay as the fallback
  when `GetFont` is missing. Fonts are asked for only once the title was seen.
- Title-button clones: `das_lang_font` = what `DASText.RefreshOnRunTime` sets (GetFont + GetMaterial of the label's
  `_fontStyle` / `_fontMaterial`), without its text and wrap resets.
- Not run in the game yet (no game in this lane): the game test should check zh-Hant / ja titles and body text.

## Compared by value: fixed
- `dsl_ui.c` title order: the multiplayer "Multiplayer" button is found by its owner (module "multiplayer" running:
  the first title button after the stock Load button that is neither stock nor ours; mpmenu.c inserts its clones at
  Load + 1), not by its label.
- Soft-restart steps across processes (`dsl_soft.c`, `ui/dsl_progress.c`): the progress file is now
  `<pct>\t<step key>\t<arg>`; the step key is the English text used as an id (marked `TR_NOOP`), the drawing process
  translates it (`"Loading %s"` + the mod names as arg). "Step n of m" is found from the key. Log lines stay English.
- `dsl_reg.c` flow K: "missing mods" comes from `key_names()`'s count, not from searching "(not installed)".
- Mod "Not running" reasons and the restart failure reason are stored as English keys and translated where the
  Mods window draws them.
- Cloud status (`dsl_cloud.c`): built on the cloud thread before; now a code + numbers, made into text on the main
  thread (`dsl_cloud_status`); the log line stays English (`l1_cloud_test.py` looks for "cloud: Cloud backup: ").
  The texts are two tables by code (`CS_TEXT` with `TR_NOOP`, `CS_PLURAL` with `TRN_NOOP`), each text written once.

## UTF-8
- `dsl_ui.c str()` drops a cut last character before `il2cpp_string_new` (an invalid tail makes it return "");
  notice and tooltip copies, title labels and composed texts get `dsi_utf8_fix_end`.
- `ui/dsl_win.c` description cut and `dsl_reg.c dsl_mod_short` cut back to a character boundary.
- `dsl_json.c`: `\uXXXX` is written as UTF-8 (surrogate pairs too; lone surrogate / NUL = U+FFFD), plus `\b \f`.
  Unit test `loader/test/i18n/run.sh`. `loader/modules/build_mods.sh` writes mod.json with `ensure_ascii=False` (UTF-8).
- Windows starter: dialogs through `msgbox()` (UTF-8 -> `MultiByteToWideChar(CP_UTF8)` -> `MessageBoxW`).
  `DSL_BIE_REASON` / `DSL_ML_REASON` are set with `bie_env_text` (bytes >= 0x80 and '%' as %XX, so the ANSI
  environment keeps them on any code page) and decoded by `dsl_reg.c env_text`.
- Mac starter: the osascript notification text is escaped (`\`, `"`) and cut with `dsi_utf8_clip`.

## mod.json translations
- Optional object `"i18n": {"fr": {"name": "...", "short_name": "...", "description": "..."}, ...}` (codes as
  `ds_i18n.h`); the Mods window and the world rows show the current language's text, else the top-level English one.
  Left absent in all mod.json files for now.
- Not part of the mod-set hash: `dsl_main.c listed_hashes` hashes the sha256s listed under `platforms.*.files`, and
  `core/src/join.rs modset_lines` uses id@version + file sha256s + byte patches. mod.json text is in neither.

## Packaging
- `loader/modules/copy_lang.sh <repo> <commit> <mod dir>` copies a repo's top-level `lang/*.json` at a pinned commit
  into `<mod dir>/lang/` (nothing when that commit has none). Used by `loader/build.sh` (Mac build/mods: multiplayer
  from the mp pin, custom-modes from the cm pin tree) and `loader/modules/build_mods.sh` (mods_all/mac and /win).
  `package_mods.sh`, the Mac app and the Windows kit copy whole mod folders, so `lang/` ships with them.
- `lang/` is a subfolder: `build_mods.sh` keeps it out of mod.json `files`, so it is out of the mod-set hash.
- The loader's own `loader/lang/*.json`: Mac app `Resources/dsml/lang`, Windows kit `files/lang` (only `*.json`).

## Hell difficulty texts
- `loader/modules/hell-difficulty/lang/hell_texts.json`: TextKeyTable key -> {language code -> text}; English and
  the current French filled, the other 8 empty (= as before: the copied Downfall cell, Devastation's colour tag made
  Hell's). `tools/apply_hell.py clone_text` takes all 10 (pt-BR -> PortugueseBrazil column).
- Checked: the Mac delta made with the new tool is byte-identical to main's (sha256 775095f5...).
- After filling texts, rebuild the deltas (game files stay out of git):
  `HELL_STOCK_MAC=<stock Mac table bundle> HELL_STOCK_WIN=<stock Windows table bundle> zsh loader/modules/build_mods.sh`
  (or one: `python3 loader/modules/hell-difficulty/tools/hell_delta.py <stock bundle> <out>.dsdelta`).

## For the other lanes
- `title_add_button` again with the same self, click and ud = new label / tip for that button. This is API 4:
  `DSL_API_VERSION` 4 in `loader/sdk/dsl_sdk.h` (same DSLHost layout; the header says what 4 means). Custom Modes
  relabels only when `host->api_version >= 4` (`home/cm_i18n.c cm_i18n_loader_relabels`), so on an API 3 loader it
  adds no second button.
- The loader loads a module when its `dsl_plugin_api_version` and mod.json `"api"` are 1..`DSL_API_VERSION`
  (`dsl_main.c` scan + open): API 1, 2 and 3 modules load as before (bundled mod.json files say 2 or 3; tests 31/31).
  A module built with the new header exports 4, so an older loader (API 3) refuses it: modules ship with their loader.
- `ds_multiplayer/sdk/dsl_sdk.h` (its own drifted copy, API 3): does NOT need the line now. Its only host is
  `src/rig_main.c cm_host` (the standalone rig), which has no `title_add_button`; bumping it there would claim a
  relabel that host does not do. The Windows multiplayer module build puts `-I$DSMP_ROOT/sdk` before `DSL_SDK`, so
  multiplayer.dll is built against that copy (exports 3; loads fine). Custom Modes reads `host->api_version` at run
  time, so the header it was built with does not matter.
- Multiplayer's "Multiplayer" title button is NOT registered through `title_add_button`: mpmenu.c makes its own clone
  of Credits (inserted at Load + 1) and, on a language change, destroys and rebuilds all its title clones itself
  (`title_sync`, ds_multiplayer `ext/i18n_mpmenu/NOTES.md`). So `loader/modules/multiplayer/mp_module.c` needs no
  relabel call; the loader finds that button by owner, not label. Its strings come from `mods/multiplayer/lang`
  (dladdr on multiplayer.dylib), which `copy_lang.sh` fills from the mp pin (Mac `loader/build.sh`, both platforms
  in `build_mods.sh`). The current mp pin (`mp_pin.txt`) has no `lang/` and no i18n yet: the pin moves later.
- Load-row texts, filter labels and tab titles of modules are asked for / copied by the loader as before: owners
  translate them.

## Game words inside our sentences (keep the game's term)
| English | TEXTKEY |
|---|---|
| Load Campaign ("You can open it from Load Campaign.") | TEXTKEY_TitleMenu_LoadCampaign |
| Load (button of "Load it with them?") | TEXTKEY_SaveLoad_Load |
| Cancel | TEXTKEY_CommonPopUp_Cancel |
| OK (notice button; the game's own box says) | TEXTKEY_CommonPopUp_Confirm |
| Save | TEXTKEY_Common_Save |
| Apply | TEXTKEY_Common_Apply |
| Close | TEXTKEY_Common_Close |
| Next ("Next world") | TEXTKEY_Shared_Next |
| Devastation (Hell texts) | TEXTKEY_CreateCampaign_DownfallDifficulty |
| Hard (Hell texts) | TEXTKEY_CreateCampaign_HardDifficulty |
| Very Hard | TEXTKEY_CreateCampaign_VeryHardDifficulty |
| Ironman Mode (Hell texts) | TEXTKEY_CreateCampaign_IronMode |
Ours (translate, one glossary): Mods, Multiplayer, Custom Modes, Dungeon Settlers Modded (product name: kept as is),
BepInEx, MelonLoader (names: kept), Steam Cloud.

## Not wrapped
- Log lines, test registry names, env names, file names, JSON keys, "x"/"X" close buttons, the product name.
- dsml-helper (Rust) "ERROR" texts shown by the Mac starter: English (Rust not translated now). They reach the player
  as the outermost message of `dsml-helper prepare`: `core/src/bin/dsml_helper.rs` :139-140 (game not installed),
  :144 (Steam updating), :65-66 (changed game files), :151 (disk space), :36/:38 (failed commands), :101, :109;
  `core/src/copy_mac.rs` :104, :88, :93-94, :57/:59, :74/:76, :151-152; `core/src/crypto.rs` :18; `core/src/lib.rs`
  :84, :88-92 and bare io errors ("No such file or directory (os error 2)").
- Mac `install_bepinex.sh` / `install_melon.sh` "ERROR" lines inside the BepInEx / MelonLoader dialog (shell).
- Installer (NSIS) and `kit/*.bat`: out of scope (they run before any game language exists).
- Unused Tauri prototype (`src-tauri/`, `ui/`) and the MP snapshot: not ours.

## Main v0.1.9 merged (2026-10-17): auto-update, fastboot, pathwarn, mod zip messages
- Title corner (`src/dsl_update.c`): the line is a kind + arg (`line_kind`, `ready_kind`) made into text every frame,
  so a language change shows at once; the switch, the text and the button are destroyed and made again when
  `dsi_generation()` changes (label + the new language's font). The switch keeps no tooltip.
- Starter (`start/dsl_update_start.c`): whole sentences per kind (kit / mods; with or without the old version), no
  more "%c%s" capital trick; the install reasons in `dsl_update_mac.c` / `dsl_update_win.c` are TR fragments.
  notice.txt holds the starter's line already translated (an older loader after a rollback shows it as it is).
- Windows starter: update window title fallback, "was updated, but ..." fail, the folder-path warning (msgbox, UTF-8
  root) and the "could not be installed (the mods folder ...)" box go through TR; `DSML_UPDATE` is set with
  `dsl_u8_setenv` (the label holds mod names, maybe translated). `DSL_BIE_REASON` stays `bie_env_text` (%XX).
- Mac stub: `DSI_IMPLEMENTATION` before the `dsl_json.c` include (it pulls ds_i18n.h in first).
- `ui/dsl_pkg.c`: "is ready" / "are ready" (TRN) / "could not be installed" texts.
- en.json 207 keys (49 new). Translated in all 9 languages (plain pass + one review per language: game terms, own terms,
  the file's older wording, reason fragments inside "(...)", "Applications" = the Finder's own name). `i18n_tool.py check`
  0 missing / 0 bad; `loader/test/i18n/run.sh` ok.
- Game test (Mac, hidden 960x540 copy, notch fix off; en, ja, ru): title corner switch + "Update to 0.1.10" (local release
  server), the "installs the next time" line after the click, the Mods window "is ready" / "could not be installed"
  lines (DSL_PKG_TEST_INUSE; `.pending` made a file), the soft-restart strip, a starter notice line in B's corner: right
  language, fits (the notice and the long Mods-window lines wrap to two lines). The starter's update window (440x110,
  one-line label 392 px): all 10 languages fit (AppKit measure). Not run: the Windows window and the folder-path box
  (wine). Test-only: the UI test registry copies a text into 160 bytes and can cut a CJK character (the screen has the
  whole text).
- `loader/ui/test/build_test.sh` (Windows part) and `loader/test/winbie/build.sh` do not build on main either (main's
  UTF-8 compat headers are not on their include path): not changed here.

## Re-running on a newer main
`python3 tools/i18n_wrap.py` applies the literal wraps (78 entries; `--check` reports only). The structural edits
(language plumbing, fonts, step keys, cloud status codes, mod.json texts, the Windows env encoding) are hand edits in
this branch. Then `python3 tools/i18n_tool.py extract loader/lang loader/src loader/ui loader/start app/stub`.

## Tests run
2026-10-17 (9 translations, TRC): `loader/test/i18n/run.sh` now also builds `lang_test.c` (shipped fr.json + ja.json: dsi_dropped() 0, all
158 en.json keys resolve; TRC context key; every mod.json fr name through the loader JSON reader). run_tests 31/31, cloud 25 ok,
`loader/build.sh` + `build_mods.sh` with the i18n pins (lang/ in multiplayer + custom-modes, not in "files", no `\u`), Windows kit
`files/lang`, `app/build_app.sh` (`Resources/dsml/lang`). The pin files still point at the old commits.
2026-10-17 (core v2 + API 4): `loader/test/run_tests.sh` 31/31, `loader/test/cloud/run.sh` 25 ok, `loader/test/i18n/run.sh`
ok, `loader/build.sh`, `loader/modules/build_mods.sh` (check OK), `loader/start/win/build_win.sh`, winbie, ui test build,
Mac stub compile; en.json 159 keys (same keys as before). Earlier:
`loader/test/run_tests.sh` 31/31, `loader/test/cloud/run.sh` 25 ok, `loader/test/i18n/run.sh` ok,
`loader/build.sh` (all Mac modules), `loader/modules/build_mods.sh` (Mac + Windows, check OK), `package_mods.sh`,
`loader/start/win/build_win.sh` (baselib.dll, starter, kit with `files/lang`), `loader/ui/test/build_test.sh`,
`loader/test/winbie/build.sh`, the Mac stub compile. A manual host run with a test `fr.json` next to the loader and
`SaveCurrentLanguageType: 2`: the start notice and `dsload_status.json` came out in French, a bad entry was dropped.
Not run: the game (later lane), `app/build_app.sh` / `app/test/run.sh` (Rust build + the game).
