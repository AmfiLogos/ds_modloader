#!/usr/bin/env python3
"""scan_names.py <word> <dir|zip>...: every file (zips opened) for <word> as bytes, UTF-8 and UTF-16LE, any case. Exit 1 = hit."""
import os, sys, zipfile
w = sys.argv[1].lower(); pats = [w.encode(), w.encode("utf-16-le")]
hits = 0
def scan(name, data):
    global hits
    low = data.lower()
    for p in pats:
        if p in low: hits += 1; print("HIT", name, "utf16" if p != pats[0] else "bytes"); return
for a in sys.argv[2:]:
    if a.endswith(".zip"):
        z = zipfile.ZipFile(a)
        for n in z.namelist():
            scan(f"{a}:{n}", n.encode() + z.read(n))
    else:
        for r, _, fs in os.walk(a):
            for f in fs:
                p = os.path.join(r, f); scan(p, p[len(a):].encode() + open(p, "rb").read())
print(f"{hits} file(s) with the name")
sys.exit(1 if hits else 0)
