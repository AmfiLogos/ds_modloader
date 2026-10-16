# Comparison test references

Two test files compare the Rust engine with outside code:

| Test | Compares with | Reference location (env var, default) |
|---|---|---|
| `export_vs_c.rs` | the PvP mod's C unit writer (Custom Modes `units/pvp_build.c`, `units/pvp_wiki.c`, `fe/`) | `DS_CM_DIR` = `$HOME/ds_custommodes`, at commit `DS_CM_REV` = `CM_PIN` in `tests/common/mod.rs` |
| `export_vs_c.rs` (cases), `sheet_vs_builder.rs` | the wiki builder (`data.js` + `builder.js`) | `DSB_DIR` = `$HOME/dungeonsettlersdb/site/builder` |

Tools: `cc` with zlib, `node`, `git`.

- The C sources are taken from the pinned commit with `git archive`, so the checkout's own branch or edits do not change the result. `DS_CM_REV=<commit>` tests another commit; `DS_CM_REV=worktree` builds the files as they are on disk. Bump `CM_PIN` when Custom Modes changes `units/` or `fe/`.
- Each comparison prints one line to stderr, also when the test passes: `REF RAN <test>: ...` (what was compared, against which commit) or `REF SKIP <test>: <why>`.
- `DS_REQUIRE_REFS=1` (release gates): a missing reference, a missing tool or a C compile error fails the test (`REF FAIL`).

Release gate command:

```
DS_REQUIRE_REFS=1 cargo test --release -p ds-unit-builder --test export_vs_c --test sheet_vs_builder 2>&1 | grep -E "^REF |test result|FAILED|panicked"
```
