#!/usr/bin/env python3
"""steam_gate.py <kit folder>: exit 1 when a file of a release kit carries a path that turns the game's Steam check off
(owner 2026-10-18: "require Steam, like Mac"). Those paths exist only in test builds (build_win.sh DSL_TEST_NOSTEAM=1):
the proxy's StoreRuntimeHandler.Start hook and the starter's DSMODDED_NO_STEAM switch. Every file is scanned as bytes,
as UTF-8 and as UTF-16LE (the starter's wide strings), zip members included."""
import io, os, sys, zipfile

MARKS = ["StoreRuntimeHandler", "DSL_TEST_NOSTEAM", "DSMODDED_NO_STEAM", "Steam check off"]
PATS = [(m, enc) for m in MARKS for enc in ("ascii", "utf-16-le")]


def hits(name, data, out):
    for m, enc in PATS:
        if m.encode(enc) in data:
            out.append(f"{name}: {m} ({enc})")
    if data[:4] == b"PK\x03\x04":
        try:
            z = zipfile.ZipFile(io.BytesIO(data))
            for i in z.infolist():
                hits(f"{name}!{i.filename}", z.read(i), out)
        except zipfile.BadZipFile:
            pass


def main():
    if len(sys.argv) != 2 or not os.path.isdir(sys.argv[1]):
        print(__doc__); return 2
    out, n = [], 0
    for d, _, fs in os.walk(sys.argv[1]):
        for f in fs:
            p = os.path.join(d, f)
            with open(p, "rb") as fh:
                hits(os.path.relpath(p, sys.argv[1]), fh.read(), out); n += 1
    for line in out:
        print("STEAM-OFF PATH", line)
    print(f"steam gate: {n} files, {len(out)} hit(s)")
    return 1 if out else 0


if __name__ == "__main__":
    sys.exit(main())
