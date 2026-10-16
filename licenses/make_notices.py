#!/usr/bin/env python3
"""Write licenses/THIRD_PARTY_NOTICES.txt: the license texts of all third-party code in what we ship.

What ships it: the Mac .dmg (inside the app), the Windows Setup.exe (install folder) and every mod zip, next to
licenses/LICENSE.txt (our own MIT license). See licenses/README.md for which part is in which file.

  python3 licenses/make_notices.py            (re)write the file; run it after a dependency change, commit the result
  python3 licenses/make_notices.py --check    exit 1 when the committed file is out of date

Inputs (env): MP_REPO (ds_multiplayer checkout), CM_REPO (ds_custommodes checkout), LLVM_MINGW (llvm-mingw folder),
NSIS_SRC (NSIS 3.11 source folder, for COPYING). Defaults: the folders next to this repo, then in the home folder.
The Rust part comes from `cargo metadata` (the crates of dsml-helper, the only Rust binary that ships).
"""
import hashlib, json, os, subprocess, sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "licenses" / "THIRD_PARTY_NOTICES.txt"


def folder(env, *names):
    if os.environ.get(env): return Path(os.environ[env])
    for n in names:
        for base in (REPO.parent, Path.home()):
            if (base / n).is_dir(): return base / n
    sys.exit(f"make_notices: set {env}")


def read(p):
    p = Path(p)
    if not p.is_file(): sys.exit(f"make_notices: missing {p.name} (looked in {p.parent.name}/)")
    return p.read_text(encoding="utf-8", errors="replace").strip()


def section(title, where, text):
    return f"{'=' * 78}\n{title}\nUsed in: {where}\n{'=' * 78}\n\n{text}\n\n"


def rust_crates():
    """(name, version, license, [license file texts]) of every non-dev dependency of dsml-core's binaries."""
    meta = json.loads(subprocess.run(["cargo", "metadata", "--format-version", "1", "--locked",
                                      "--filter-platform", "aarch64-apple-darwin"],
                                     cwd=REPO, capture_output=True, text=True, check=True).stdout)
    pk = {p["id"]: p for p in meta["packages"]}
    nodes = {n["id"]: n for n in meta["resolve"]["nodes"]}
    root = next(i for i, p in pk.items() if p["name"] == "dsml-core")
    seen, todo = set(), [root]
    while todo:
        i = todo.pop()
        for d in nodes[i]["deps"]:
            if any(k["kind"] in (None, "normal") for k in d["dep_kinds"]) and d["pkg"] not in seen:
                seen.add(d["pkg"]); todo.append(d["pkg"])
    out = []
    for i in sorted(seen, key=lambda i: (pk[i]["name"], pk[i]["version"])):
        p = pk[i]; d = Path(p["manifest_path"]).parent
        files = sorted(f for f in d.iterdir() if f.is_file() and f.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE")))
        out.append((p["name"], p["version"], p.get("license") or "see crate", [(f.name, read(f)) for f in files]))
    return out


def build():
    mp, cm = folder("MP_REPO", "ds_multiplayer"), folder("CM_REPO", "ds_custommodes")
    mingw = folder("LLVM_MINGW", "tools/llvm-mingw") / "x86_64-w64-mingw32/share/mingw32"
    nsis = folder("NSIS_SRC", "tools/nsis_build/nsis-3.11")
    s = ["Third-party notices for Dungeon Settlers Modded (Mac app, Windows installer, mod zips).\n"
         "Our own code is under the MIT license in LICENSE.txt. The parts below keep their own licenses.\n\n"]
    s.append(section("MinHook (BSD 2-Clause)", "Windows: baselib.dll (the loader)",
                     read(mp / "win/third_party/minhook/LICENSE.txt")))
    s.append(section("musl libc math functions (MIT)", "multiplayer module (Mac and Windows)",
                     read(mp / "win/math/musl/COPYRIGHT")))
    s.append(section("RimWorld Multiplayer (MIT)",
                     "multiplayer module: the lockstep clock design and the server timer logic are adapted from it "
                     "(https://github.com/rwmt/Multiplayer)", read(mp / "src/net/LICENSE.rwmt-Multiplayer.txt")))
    s.append(section("QuickJS (MIT)", "Custom Modes module (Mac and Windows)", read(cm / "fights/quickjs/LICENSE")))
    s.append(section("tinf (zlib license)", "Custom Modes module (Mac and Windows)", read(cm / "fights/tinf/LICENSE")))
    z = read(mp / "win/third_party/zlib/README")
    s.append(section("zlib (zlib license)", "Windows DLLs", z[z.index("Copyright notice:"):]))
    for f, what in (("COPYING.MinGW-w64-runtime.txt", "MinGW-w64 runtime"), ("COPYING.winpthreads.txt", "winpthreads")):
        s.append(section(what, "Windows .dll and .exe files (static runtime)", read(mingw / f)))
    s.append(section("NSIS (zlib/libpng license; LZMA module: CPL 1.0)", "Windows: the Setup.exe installer stub",
                     read(nsis / "COPYING")))
    s.append(f"{'=' * 78}\nRust crates (MIT, Apache-2.0, ISC, BSD and others)\n"
             f"Used in: Mac app: dsml-helper\n{'=' * 78}\n\n")
    texts, crates = {}, rust_crates()
    for name, ver, lic, files in crates:
        refs = []
        for fn, t in files:
            h = hashlib.sha256(t.encode()).hexdigest()[:12]
            texts.setdefault(h, t); refs.append(f"{fn} [{h}]")
        s.append(f"{name} {ver}  ({lic})  {', '.join(refs) or 'no license file in the crate package'}\n")
    s.append("\nLicense texts of the crates above, by the id in [brackets]:\n\n")
    for h, t in texts.items(): s.append(f"----- [{h}] -----\n{t}\n\n")
    return "".join(s)


if __name__ == "__main__":
    new = build()
    if Path.home().as_posix() in new: sys.exit("make_notices: a home path got into the text")
    if "--check" in sys.argv:
        ok = OUT.is_file() and OUT.read_text() == new
        print("notices up to date" if ok else "notices OUT OF DATE: run licenses/make_notices.py"); sys.exit(0 if ok else 1)
    OUT.write_text(new); print(f"wrote {OUT.relative_to(REPO)} ({len(new)} bytes)")
