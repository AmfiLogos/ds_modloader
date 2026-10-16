#!/usr/bin/env python3
"""Check the Windows x64 Hell patch table against the stock GameAssembly.dll (read only).
    check_win_patches.py [patches.tsv] [GameAssembly.dll] [functions.tsv] [script.json]
Defaults: rva/25846389_win_patches.tsv, ~/ds_multiplayer/win/25846389/GameAssembly.dll, rva/25846389_win.tsv,
~/ds_multiplayer/win/25846389/dump/script.json.
Checks: 1..16 bytes, same old/new length, expected bytes == stock bytes, no overlap, patch starts on an instruction
boundary of the containing method and the new bytes decode to whole instructions of the same total length.
Prints the containing method and the disassembly before / after. Then checks the function table: names match
script.json, both entries hookable (first instructions printed, no rip-relative operand in the first 5 bytes),
ParseItems_return is right after exactly one direct call to ParseItems inside DetermineInitialEquipments.
Exit 0 = pass."""
import bisect, json, os, struct, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

HERE = os.path.dirname(os.path.abspath(__file__))
H = os.path.expanduser("~")
TSV = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "../rva/25846389_win_patches.tsv")
DLL = sys.argv[2] if len(sys.argv) > 2 else f"{H}/ds_multiplayer/win/25846389/GameAssembly.dll"
FUN = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, "../rva/25846389_win.tsv")
SCR = sys.argv[4] if len(sys.argv) > 4 else f"{H}/ds_multiplayer/win/25846389/dump/script.json"

pe = pefile.PE(DLL, fast_load=True)
md = Cs(CS_ARCH_X86, CS_MODE_64)
s = json.load(open(SCR))
names = {}
for m in s["ScriptMethod"]:
    names.setdefault(m["Address"], []).append(m["Name"])
starts = sorted(set(names) | set(s["Addresses"]))
fails = []


def owner(a):
    i = bisect.bisect_right(starts, a) - 1
    return starts[i], (starts[i + 1] if i + 1 < len(starts) else starts[i] + 0x1000)


def dis(code, a):
    return [(i.address, i.size, i.mnemonic + " " + i.op_str) for i in md.disasm(code, a)]


patches = []
for ln, line in enumerate(open(TSV), 1):
    if line.startswith("#") or not line.strip():
        continue
    f = line.rstrip("\n").split("\t")
    if f[0] != "patch" or len(f) < 4:
        fails.append(f"line {ln}: bad row")
        continue
    patches.append((int(f[1], 16), bytes.fromhex(f[2]), bytes.fromhex(f[3]), f[4] if len(f) > 4 else "", ln))

for rva, old, new, note, ln in patches:
    tag = f"{rva:#x} ({note})"
    if not (1 <= len(old) <= 16) or len(old) != len(new):
        fails.append(f"{tag}: length {len(old)}/{len(new)}")
    stock = pe.get_data(rva, len(old))
    if stock != old:
        fails.append(f"{tag}: stock {stock.hex()} != expected {old.hex()}")
    fs, fe = owner(rva)
    ins = dis(pe.get_data(fs, rva + len(old) - fs + 16), fs)
    bounds = {a for a, _, _ in ins}
    if rva not in bounds:
        fails.append(f"{tag}: not on an instruction boundary of {fs:#x}")
    after = dis(new, rva)
    if sum(z for _, z, _ in after) != len(new):
        fails.append(f"{tag}: new bytes do not decode to whole instructions")
    if rva + len(old) not in bounds:
        fails.append(f"{tag}: patch end splits an instruction")
    print(f"{rva:#x}  {len(old)}B  in {names.get(fs, ['?'])[0]} (+{rva - fs:#x})   {note}")
    for a, _, t in dis(old, rva):
        print(f"    before {a:#x}  {t}")
    for a, _, t in after:
        print(f"    after  {a:#x}  {t}")

spans = sorted((r, r + len(o), r_ln) for r, o, _, _, r_ln in patches)
for (a0, e0, l0), (a1, e1, l1) in zip(spans, spans[1:]):
    if a1 < e0:
        fails.append(f"overlap {a0:#x}-{e0:#x} (line {l0}) with {a1:#x} (line {l1})")

# function table
fun = {}
for line in open(FUN):
    if line.startswith("#") or not line.strip():
        continue
    k, v = line.split()[:2]
    fun[k] = int(v, 16)
want = {"RecruitHelper.DetermineInitialEquipments": "Refactor.Main.Event.RecruitHelper$$DetermineInitialEquipments",
        "RecruitHelper.ParseItems": "Refactor.Main.Event.RecruitHelper$$ParseItems"}
for k, n in want.items():
    a = fun.get(k)
    if a is None or names.get(a) != [n]:
        fails.append(f"{k}: {a} is not {n} alone in script.json (got {names.get(a)})")
        continue
    head = dis(pe.get_data(a, 32), a)
    cov, first = 0, []
    for ia, sz, t in head:
        first.append(f"{ia:#x} {t}")
        if "rip" in t:
            fails.append(f"{k}: rip-relative operand in the hook bytes: {t}")
        cov += sz
        if cov >= 5:
            break
    print(f"{k} {a:#x}: first instructions ({cov} bytes for a 5-byte jump): " + " | ".join(first))
det, parse, ret = (fun.get("RecruitHelper.DetermineInitialEquipments"), fun.get("RecruitHelper.ParseItems"),
                   fun.get("DetermineInitialEquipments.ParseItems_return"))
if det and parse and ret:
    ds, de = owner(det)
    body = pe.get_data(ds, de - ds)
    sites = [i.address for i in md.disasm(body, ds)
             if i.mnemonic in ("call", "jmp") and i.op_str.startswith("0x") and int(i.op_str, 16) == parse]
    ok = len(sites) == 1 and pe.get_data(sites[0], 1) == b"\xe8" and sites[0] + 5 == ret
    print(f"ParseItems direct calls in DetermineInitialEquipments: {[hex(x) for x in sites]}; return {ret:#x} {'OK' if ok else 'BAD'}")
    if not ok:
        fails.append("ParseItems_return check failed")
else:
    fails.append("function table incomplete")

print(f"\n{len(patches)} patches, {len(fails)} problem(s)")
for x in fails:
    print("FAIL", x)
sys.exit(1 if fails else 0)
