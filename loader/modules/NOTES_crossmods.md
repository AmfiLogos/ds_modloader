# Lane crossmods (v13 WP XM): every mod we make works on Mac AND Windows

Owner, v12 item 11: "Our own mods (global cooldown, Hell difficulty, and every mod we make) must work on Windows too".
Branch `crossmods` (this repo) + `crossmods13` (ds_multiplayer, win/** and ext/crossmods13/).

## What changed here

| Module | Windows part | How |
|---|---|---|
| gcd-cdr | `rva/25846389_win.tsv`, `win-x64` in mod.json | The 3 table entries from ds_multiplayer win/win_map.tsv (method / metadata / internal 149 of 150 votes). SetGlobalCooldown() 0xAE56B0 has 2 direct callers on Windows (not inlined, not folded). Same C source, built with llvm-mingw. |
| hell-difficulty | `rva/25846389_win_patches.tsv` (23 x64 patches), `rva/25846389_win.tsv` | The 20 Mac sites mapped method by method (tools/WIN_SITES_NOTES.md, tools/check_win_patches.py). 23 on Windows: the iron-mode and "+" lock logic need 5 sites each where the Mac needs 4, and MSVC inlined 3 more copies (environment count, 2 mood colour switches). Staff strip: return address 0xA7E52A (call ParseItems at 0xA7E525 + 5). hell.c reads `<build>_win_patches.tsv` on Windows. |
| hell-difficulty, table half | `hell_data.c` (both OS) + `rva/<build>_<os>_tables.dsdelta` (built, not in git) | See below. |
| every module | `build_mods.sh` | Builds every module for mac-arm64 + win-x64 into `build/mods_all/{mac,win}/<id>/`. One mod.json per module, the SAME in both copies, listing both platforms' files (sha256). `--check` = the gate13 rule. |

## Hell table half: a delta, no game file, no file step

The owner's apply_hell.py edits the table bundle (difficulty rows, texts) and catalog.bin. We cannot ship the edited bundle
(it is a game file). Old plan: a data mod "hell-difficulty-data" applied to the game copy by the launcher / Windows starter.
Problem on Windows: a Mods-window Apply is a soft restart that starts the new game process without the starter, and the
running process holds the bundle open, so a file swap cannot follow the Mods window. Chosen instead (both OS):
- `tools/hell_delta.py <stock bundle> <out>`: the owner's `edit_tables` on the stock bundle, saved uncompressed, stored as
  COPY / ADD operations against the stock bundle's decompressed data (3.3 KB, 2185 literal bytes). Source and target sha256
  in the header.
- `hell_data.c` (in `dsl_plugin_init`): reads the player's own stock bundle (next to GameAssembly: `<game>\DungeonSettlers_Data\
  StreamingAssets\aa\StandaloneWindows64\` / `Contents/Resources/Data/StreamingAssets/aa/StandaloneOSX/`), checks its sha256,
  decompresses it (UnityFS + LZ4, own code), applies the delta, checks the result's sha256, caches it in
  `<DSRIG_DIR or DSL_LOG_DIR>/hell-difficulty/<sha>.bundle`. Then a hook on `AssetBundle.LoadFromFileAsync_Internal` (by
  name; not inlined: its 2 callers are the public LoadFromFileAsync overloads) gives the game that path (crc 0) when it asks
  for the stock table bundle. catalog.bin is not needed (the owner's script changed only the size and the CRC there).
- Any failure (other bundle, bad delta) turns the whole module off: the loader puts every patched byte back.
- `mod.json` no longer `requires` "hell-difficulty-data" (nothing provides it on Windows; the module has its tables now).
- Host test: `tools/test_hell_data.c` (fake host) rebuilt the same sha256 as the Python output.

## Gate / ship
- Ship the mods from `build/mods_all/mac` (Mac app) and `build/mods_all/win` (Windows kit, `MODS=...`), never from
  `build/mods`: only mods_all has the two-platform mod.json and the Hell deltas.
- `build_mods.sh --check build/mods_all` must say OK (every module lists mac-arm64 + win-x64, entry files exist, sha256 match,
  both copies identical).
- Hell deltas need the stock table bundles at build time (env HELL_STOCK_WIN / HELL_STOCK_MAC; defaults: the DSMP bottle's
  game copy and ~/DungeonSettlers_HellDifficulty/b25846389/stock). Read only.

## Tests (results: ds_multiplayer ext/crossmods13/NOTES.md)
