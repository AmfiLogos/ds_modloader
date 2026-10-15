# modcheck: in-game wrong-modset message (launcher lane, 2026-10-15)

- `ds_modcheck.h` (header only, static functions): compares the host's and the guest's GAMEPLAY mod lines
  (`<id>@<version>:<file sha256s>[:<patch hash>]`, core/src/join.rs `modset_lines`) and writes the player text:
  Missing (the host uses them) / Extra (the host does not use them) / Other version (host X, you Y) /
  Same version, different files (reinstall); Vanilla check: "Turn off: ...". Header only so the Mac build
  (src/build.sh) and the Windows port (win/port/gen_port.py, -I$S -I$S/net) need no build change.
- `test/test_modcheck.c`: unit tests (cc -fsanitize=address,undefined).
- `src_modset.patch` (src/mp_game.c, src/mpmenu.c, src/mpmenu.h) + copy `ds_modcheck.h` to `src/net/`. Needs corenext's
  HELLO modset fields (DSNET_REJ_MODSET, dsnet_reject_host_mods, dsnet_my_mod_lines; ext/core_next/NOTES.md).
  - mp_game: `reject_text(DSNET_REJ_MODSET)` = one short line; exported `mp_game_modset_text(out, n)` gives the full
    notice ONCE per refusal (host lines from the REJECT vs this copy's lines) and logs `mp: modset refusal: ...`.
  - mpmenu: join by address (`join_refused`) and the Steam / auto join (`observe`, MP_STEAM_ENDED) put it in the stock
    notice box (`M.notice`, enlarged 320 -> 640).
  - Names: mod ids (the session file has no display names yet; `names` argument ready for "id\tName" lines).
- `../tests/modset_pair.py <PAIR> <dylib>`: 3 cases on a leased pair (mods differ, vanilla check, same mods).

## Pair results 2026-10-15 (pair CD, test_env/pair/modset_result_*.json)
- 1 mods differ: refused (reason 8), notice names Missing hell-difficulty 1.0.0 / Extra cheaper-recruits 1.0.0 /
  Other version multiplayer (host 1.1.0, you 1.0.0). PASS.  2 vanilla check: "Turn off: hell-difficulty 1.0.0". PASS.
  3 same mods: joins. PASS.
- The patch also makes an mp.json join (rig / kit) end the session on a modset refusal so the notice shows
  (before, that path only logged "session did not start").
