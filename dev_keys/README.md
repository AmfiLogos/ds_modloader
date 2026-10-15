# Shared signing key pair

`dev.key` (secret) and `dev.pub` (public) sign the launcher's mod lists (tests, local runs and the bundled list).
They are public in this repo, so anyone can sign a list with them. Owner decision 2026-10-15: that is fine, the mod
list is open for anybody to modify. `TRUSTED_KEYS` in `src-tauri/src/main.rs` lists `dev.pub`.
