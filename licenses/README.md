# License files that ship

Every release file must hold these two files:

- `LICENSE.txt`: our own license (MIT, the same text as `../LICENSE`).
- `THIRD_PARTY_NOTICES.txt`: the licenses of the third-party code inside the release files.

Where they go: in the Mac app at `Contents/Resources/licenses/`, in the Windows install folder at `licenses\`,
and at the top of each mod zip (`<mod>/licenses/`).

What is in which release file:

| Release file | Third-party code |
|---|---|
| Mac .dmg | Rust crates in `dsml-helper`; musl (multiplayer); QuickJS and tinf (Custom Modes); the RimWorld Multiplayer timer logic (multiplayer) |
| Windows Setup.exe | MinHook (`baselib.dll`); zlib; MinGW-w64 runtime and winpthreads (all .dll and .exe); musl, RimWorld Multiplayer (multiplayer); QuickJS, tinf (Custom Modes); NSIS (the installer itself) |
| Mod zips | multiplayer: musl, RimWorld Multiplayer, MinGW-w64 runtime, zlib. custom-modes: QuickJS, tinf, MinGW-w64 runtime. gcd-cdr and hell-difficulty: MinGW-w64 runtime |

`make_notices.py` writes `THIRD_PARTY_NOTICES.txt` from the license files in the source trees. Run it after a
change of the dependencies and commit the result. `make_notices.py --check` tells you if the file is out of date.
