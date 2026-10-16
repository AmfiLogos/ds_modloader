# src-tauri: unused prototype (does not ship)

This folder is an early launcher prototype (a Tauri 2 window with the HTML pages in `../ui/`).
It is not part of any release. The Mac .dmg, the Windows Setup.exe and the mod zips do not contain it,
and no released program uses its signed mod list (index) download.

Its tests still run (`cargo test -p ds-mod-launcher`): the Unit Builder lane uses them.

The only key it trusts is the test key in `../dev_keys/`. Its secret half is public, so anyone can sign with it.
That is why only debug builds trust it. A release build (`--release`) trusts no key and refuses every mod list.
Before this prototype can ship, the owner must put real keys in `TRUSTED_KEYS` (`src/main.rs`) and set a
real index address (`DEFAULT_INDEX_URL` in `../core/src/fetch.rs`, now a placeholder).
