#!/usr/local/bin/python3
"""Hell data half as a delta (no game file is shipped): the owner's table edits (apply_hell.py edit_tables, next to
this file) on a stock table bundle, written uncompressed, stored as COPY / ADD operations
against the stock bundle's DECOMPRESSED data. The module (hell.c) rebuilds the Hell bundle from the player's own stock
file at start and loads it instead of the stock one (AssetBundle.LoadFromFileAsync_Internal path redirect, crc 0).

  hell_delta.py <stock table_assets_all_*.bundle> <out .dsdelta>

Format (little endian): "DSDLT1\\0\\0", u8 source mode (1 = UnityFS decompressed data), 3 pad, u64 source file size,
32 source file sha256, u64 target size, 32 target sha256, u16 name length + bundle file name (UTF-8), then ops:
u8 1 COPY u64 offset u32 length | u8 2 ADD u32 length + bytes | u8 0 end."""
import hashlib, struct, sys
from pathlib import Path
import lz4.block
sys.path.insert(0, str(Path(__file__).resolve().parent))
import apply_hell as ah


def unityfs_data(raw: bytes) -> bytes:
    """the decompressed block data of a UnityFS file (what hell.c's ufs_data() returns)"""
    i = raw.index(b"\0") + 1
    if raw[:i] != b"UnityFS\0": sys.exit("not a UnityFS bundle")
    ver = struct.unpack_from(">I", raw, i)[0]; i += 4
    i = raw.index(b"\0", i) + 1; i = raw.index(b"\0", i) + 1
    size, csz, usz, flags = struct.unpack_from(">QIII", raw, i); i += 20
    if ver >= 7: i = (i + 15) & ~15
    if flags & 0x80: bi = raw[len(raw) - csz:]
    else: bi = raw[i:i + csz]; i += csz
    comp = flags & 0x3F
    if comp in (2, 3): bi = lz4.block.decompress(bi, uncompressed_size=usz)
    elif comp != 0: sys.exit("blocks info compression %d not supported" % comp)
    if flags & 0x200: i = (i + 15) & ~15
    n = struct.unpack_from(">I", bi, 16)[0]; out = []; k = 20
    for _ in range(n):
        u, c, f = struct.unpack_from(">IIH", bi, k); k += 10
        blk = raw[i:i + c]; i += c
        if f & 0x3F in (2, 3): blk = lz4.block.decompress(blk, uncompressed_size=u)
        elif f & 0x3F: sys.exit("block compression %d not supported" % (f & 0x3F))
        if len(blk) != u: sys.exit("block size mismatch")
        out.append(blk)
    return b"".join(out)


def make_ops(src: bytes, dst: bytes, k=32, stride=16):
    idx = {}
    for s in range(0, len(src) - k + 1, stride): idx.setdefault(src[s:s + k], s)
    ops, lit, i = [], bytearray(), 0
    while i < len(dst):
        s = idx.get(dst[i:i + k]) if i + k <= len(dst) else None
        if s is None: lit.append(dst[i]); i += 1; continue
        b = 0                                                     # extend backwards into the pending literal
        while b < len(lit) and s - b - 1 >= 0 and src[s - b - 1] == lit[len(lit) - b - 1]: b += 1
        if b: del lit[len(lit) - b:]
        if lit: ops.append((2, bytes(lit))); lit = bytearray()
        n = k
        while i + n < len(dst) and s + n < len(src) and dst[i + n] == src[s + n]: n += 1
        ops.append((1, s - b, n + b)); i += n
    if lit: ops.append((2, bytes(lit)))
    return ops


def apply_ops(src: bytes, ops) -> bytes:
    out = bytearray()
    for op in ops: out += src[op[1]:op[1] + op[2]] if op[0] == 1 else op[1]
    return bytes(out)


def main():
    if len(sys.argv) != 3: sys.exit(__doc__)
    stock, outp = Path(sys.argv[1]), Path(sys.argv[2])
    raw = stock.read_bytes(); src = unityfs_data(raw)
    UnityPy = ah.load_unitypy()
    env = UnityPy.load(str(stock)); ah.edit_tables(env)
    files = list(env.files.values())
    if len(files) != 1: sys.exit("table bundle has %d top-level files, expected 1" % len(files))
    dst = bytes(files[0].save(packer="none"))
    import tempfile
    with tempfile.TemporaryDirectory() as td:                # owner's check: Hell is a sixth step (the built bundle is a
        chk = Path(td) / "check.bundle"; chk.write_bytes(dst); ah.verify_bundle(chk)   # game file: never left next to the delta
    ops = make_ops(src, dst)
    if apply_ops(src, ops) != dst: sys.exit("delta self-check failed")
    name = stock.name.encode()
    b = bytearray(b"DSDLT1\0\0" + bytes([1, 0, 0, 0]) + struct.pack("<Q", len(raw)) + hashlib.sha256(raw).digest()
                  + struct.pack("<Q", len(dst)) + hashlib.sha256(dst).digest() + struct.pack("<H", len(name)) + name)
    nadd = 0
    for op in ops:
        if op[0] == 1: b += struct.pack("<BQI", 1, op[1], op[2])
        else: b += struct.pack("<BI", 2, len(op[1])) + op[1]; nadd += len(op[1])
    b += b"\0"
    tmp = Path(str(outp) + ".tmp"); tmp.write_bytes(b); tmp.replace(outp)
    print("delta %s: %d ops, %d literal bytes, %d bytes; source %d (data %d) -> target %d (%s)"
          % (outp.name, len(ops), nadd, len(b), len(raw), len(src), len(dst), hashlib.sha256(dst).hexdigest()[:16]))


if __name__ == "__main__":
    main()
