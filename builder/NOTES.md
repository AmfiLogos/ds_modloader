# Unit Builder engine (ds-unit-builder)

The logic behind the launcher's Unit Builder: build a unit by hand, see the wiki builder's stat sheet live,
share it as a wiki link, keep "my units", and export units for PvP / Foreign Expedition. No UI here; the UI
(design: ~/ds_multiplayer/design/project/UnitBuilderA/B.dc.html) calls the Tauri commands below.

Data: game build 25846389 (DS_B.0.4.29), the wiki builder payload of that build.

## Sources and rules

- Reference = the wiki builder: ~/dungeonsettlersdb/site/builder/builder.js + data.js. savegen.js is never
  loaded, read or copied.
- The sheet is the BUILDER's sheet: conditional and acquired passives fold at their maximum on purpose (owner
  rule, ~/dungeonsettlersdb/CORE_TRUTHS.md). It is not an at-rest game unit.
- Export = port of ~/ds_multiplayer/ext/pvp/src/pvp_build.c + pvp_wiki.c + fe/ (fe_save.c, fe_json.c), tables
  as pvp_unit_tables.h / pvp_wiki_tables.h. Those trees are not edited by this crate.

## Modules (src/)

| module | what |
|---|---|
| `data` | `data/dsb.json` (the builder payload, unchanged) |
| `model` | `Build` = the builder's own JSON build shape (`#b=` link, its localStorage). Unknown fields ride in `extra`. |
| `compute` | `compute(&Build) -> Sheet`: line-by-line port of `computeBuild` + tree / skill point / stacking passive / estimate helpers, same f64 operations in the same order |
| `validate` | the builder's `validate()`: race, background, traits, level, portrait, inscriptions, soul stone, elixirs, needs, gear per slot (two-handers, ammo), roll normalisation, skill prune with the per-tree shelf |
| `codec` | share links both ways: `?team=` / `#team=` (v1 raw, v2 deflate-raw), `#b=~CODE`, `#b=<base64 JSON>`, bare code. `/api/shorten` short links need the wiki server: open once in a browser and copy the long link. |
| `store` | "my units" file |
| `export` | team -> `{closure, manifest}` entries, byte-identical to the C |
| `handoff` | writes the export file + returns the team link |
| `tauri_cmds` | Tauri 2 commands (feature `tauri`) |

Every hand choice is a field of `Build`: race, bg, inds (traits), lvl, tal (talent tiers) / majorBase / manual
(typed stats), insctrees + learned (skill trees, learned skills), insc, souls, acqInds, elix, cond, eq + tags
(gear per slot with stat rolls), name, portrait.

## Tests (all green, 2026-10-14)

`nice -n 19 cargo test --release -j 4` (needs node, cc, zlib; tests skip with a note when one is missing).

- `tests/sheet_vs_builder.rs`: builder.js runs in node (`tests/node/builder_ref.js`, its pure core + `validate()`
  lifted from the UI closure) on 500 random builds (seed 11 x400, seed 424242 x100; `DSB_CASES=N` for more).
  Legal and illegal picks, typed stats, elixirs, needs, stacking passives, rolls, bad inputs.
  - sheet: 114 377 numbers compared, exact same double (no tolerance), stat key order included: **0 differences**
  - validate: 500 builds, 0 differences, and a second pass changes nothing
  - links: 500 builds + 60 teams of 3: Rust v1 codes byte-equal to the builder's, each side decodes the other's
    (v1, v2, `#b=`, `#b=~`): **0 differences**
  - the builder's own selftests (D.selftest): pass
- `tests/export_vs_c.rs`: compiles the PvP mod's C with `tests/c_harness/harness.c`, same links + seed:
  671 cases (648 units built, 70 refusals), 671 identical, 0 differences (full log: `tests/c_harness/last_run.txt`).
- `tests/handoff.rs`: export file + store round trip.

Known divergences from the C (only on links no encoder writes): an index >= 2^31 is undefined behaviour in C;
here it is refused (list / race / item) or skipped (tag / stat / stack / portrait). Duplicate JSON keys sort
stably here (C qsort order unspecified).

## My units file

`<launcher root>/builder/units.json`, atomic write (temp + rename), previous file kept as `units.json.bak`.

```json
{ "format": "ds-launcher-units", "version": 1, "game_build": "25846389",
  "units": [ { "id": "16 hex", "name": "...", "created": 0, "updated": 0, "build": { ...builder JSON build... } } ],
  "teams": [ { "id": "...", "name": "...", "unit_ids": ["..."] } ] }
```

Units are stored by KEY (builder JSON), not by link index, so a data update that shifts the link tables does not
break them. Teams hold 1 to 8 units (the builder's team size).

## Export for PvP / Foreign Expedition

Two things, from `handoff::export_to_dir`:

1. **The team link** (`https://dungeonsettlers.wiki/builder?team=CODE`). This is what the mod takes TODAY: the
   PvP "Bring Units" window, "Paste Wiki Link" (`cmd_wiki` in pvp_units.c). The mod builds the units itself.
2. **The export file** `<root>/builder/exports/<name>-<ms>.json`:

```json
{ "format": "ds-launcher-units-export", "version": 1, "game_build": "...", "game_version": "...",
  "link": "...?team=...", "entries": [ { "closure": {...}, "manifest": {...} } ] }
```

Each entry is the exact compact text the mod writes for a wiki unit (closure hash computed over it): closure =
the unit save data (components, items with registry guid/key); manifest = export_id, player_uid,
home_campaign_guid (zero), home_save_name "wiki link", home_save_version, unit_guid, closure_hash,
progress_sig, name_key, level, exp 0, source "wiki", wiki_hash. Guids are random per export (seeded only in
tests); player_uid is empty in the file (the launcher does not know the player's uid; a direct import must set it).
The mod has no "load closure file" path yet: the file is for a future direct import (it has the same shape as
the host's `recv_entries`). Before writing, the export runs the mod's own checks and refusal texts (level 1..12,
skill points, slot rules, skills outside the unit's trees, races with no look in this build: Dwarf, Tal,
WhiteDemon, 1 to 8 units, wrong data fingerprint).

## Tauri commands (feature `tauri`)

App commands, not a plugin. Wired into the launcher (2026-10-14): root workspace member, `src-tauri/src/main.rs`
handler list + managed state; the UI is `ui/builder.js` / `ui/builder.css` (Unit Builder + My Units tabs), icons
copied by `gen/copy_icons.sh` into `ui/bicons/`. Tests: `cargo test -p ds-mod-launcher --test builder_cmds` (the
commands through the mock IPC), headless UI: `src-tauri/tests/ui_smoke.py` against `examples/ui_bridge.rs`.
The wiring steps were:
1. `src-tauri/Cargo.toml`: `ds-unit-builder = { path = "../builder", features = ["tauri"] }`; remove the
   `[workspace]` table from builder/Cargo.toml and add `"builder"` to the root workspace members.
2. `.manage(ds_unit_builder::tauri_cmds::UnitBuilderState::new(root))` with root = the launcher folder.
3. Add to `generate_handler!`: `builder_catalog, builder_new_unit, builder_compute, builder_validate,
   builder_set_item, builder_set_stack_level, builder_decode_link, builder_encode_link, units_list, units_save,
   units_rename, units_duplicate, units_delete, units_import_link, teams_create, teams_update, teams_delete,
   units_export` (all in `ds_unit_builder::tauri_cmds`).

UI flow: `builder_catalog` once (names, icons, pools, trees, items); on each change `builder_validate` then
`builder_compute` (returns the sheet, estimates, skill point budgets and use, inscription slots, active trees,
skill costs, quick bar, stacking passives). Errors are `Err(String)` written for the player.

## Data update (new game build)

```
node gen/gen_tables.js                                  # data/dsb.json from site/builder/data.js
node gen/gen_unit_tables.js > data/unit_tables.json     # export tables (game tables + builder link order)
nice -n 19 cargo test --release -j 4
```
The link fingerprint changes with the data: old links then say "made with other wiki data". Units in
units.json keep working (stored by key).
