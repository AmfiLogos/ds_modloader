# DEV ONLY - test signing keys

`dev.key` (secret) and `dev.pub` (public) sign TEST indexes for the launcher's tests and local runs.
They are public in this repo, so anyone can sign with them.

Before the first public release:
1. The owner makes the real key pair offline: `dsml-tool keygen <secret> <public>` (secret stays off any repo,
   plus one encrypted backup), and a second pair for rotation.
2. `TRUSTED_KEYS` in `src-tauri/src/main.rs` lists ONLY the two real public keys. Remove `dev.pub` from it.

The loader's signed auto-update (`loader/src/dsl_sig.c`) trusts `dev.pub` only in test builds (`-DDSL_SIGN_DEV_KEY`):
its host tests sign their local releases with `dev.key`. Release builds trust only the owner's two keys
(`tools/release_sign/sign_release.sh` refuses to sign a kit that trusts `dev.pub`).

Now: only debug builds of the `src-tauri/` prototype trust `dev.pub`. Release builds trust no key. No released
program uses these keys (see `src-tauri/README.md`).
