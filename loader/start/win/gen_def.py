#!/usr/bin/env python3
"""baselib_exports.def for the loader proxy: every export of the game's own baselib.dll forwards to baselib_orig.dll.
Usage: gen_def.py <game baselib.dll> <out.def>"""
import sys, pefile

pe = pefile.PE(sys.argv[1], fast_load=True)
pe.parse_data_directories(directories=[0])
with open(sys.argv[2], "w") as f:
    f.write("LIBRARY baselib.dll\nEXPORTS\n")
    for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        n = e.name.decode()
        f.write(f'    "{n}" = "baselib_orig.{n}" @{e.ordinal}\n')
print(len(pe.DIRECTORY_ENTRY_EXPORT.symbols), "forwarded exports")
