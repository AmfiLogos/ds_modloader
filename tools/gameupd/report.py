#!/usr/bin/env python3
"""Game-update change report and address-table generator (one platform, two game builds).

Usage:
  nice -n 10 python3 tools/gameupd/report.py --plat mac|win \
      --old-bin OLD.dylib|OLD.dll --old-dump OLD_DUMP_DIR --new-bin NEW --new-dump NEW_DUMP_DIR \
      [--mp DIR] [--cm DIR] [--loader DIR] [--win-src DIR] [--win-map TSV] [--ref 25846389] --out DIR

The OLD build is the reference build the sources were written against (today 25846389).

1. Harvest every game address our code uses (hex 0x10000..0x7FFFFFF, comments skipped):
   multiplayer   MP/src/**.c|h (test folders skipped, but ord_test/ord_smoke.c: in the module) + mp_module.c
   Custom Modes  CM/cm_mp.c, CM/fe/, CM/pvp/, CM/units/pvp_units.c (multiplayer part only)
   loader        LOADER/modules/*/rva/<ref>_<plat>.tsv and <ref>_<plat>_patches.tsv
   Windows start LOADER/start/win/dsl_start_win.c  #define W_*  (--plat win only)
   --plat win: the multiplayer / Custom Modes sources hold Mac addresses. With --win-src DIR (gen_port output,
   win/build/gen) those files are harvested as they are. Without it, every Mac literal is translated to its
   Windows twin through MP/win/win_map.tsv (the same table gen_port.py uses); Mac literals with no row there are
   counted, not resolved.
2. Classify each address in the OLD build: method | inside (method or internal function + offset) | metadata
   (TypeInfo / MethodInfo / string slot) | export (il2cpp_*) | internal (unnamed function start) | other.
3. Twin in the NEW build: methods by (Name, Signature with MethodInfo_xxx normalised), overload order or equal
   body as tie break; metadata by name (MethodInfo slots also by their method's twin); exports by symbol name;
   internals by a call-site vote (old callers -> their new twins -> most common unnamed callee);
   inside = same function + same offset, only when the function body is the same.
4. Body compare with address-only operands masked (arm64: B/BL, ADRP/ADR, ADD/LDR/STR low-12 on an ADRP
   register, LDR literal, B.cond/CBZ/TBZ leaving the function; x64: call/jmp rel32, jcc leaving the function,
   RIP-relative disp32). Bounds: arm64 LC_FUNCTION_STARTS + script.json Addresses; x64 .pdata (chained parts).
   Status: same | moved | changed (N differing instructions) | gone | ambiguous.
5. Field offsets of every class named in the sources: same | moved | gone.
6. String-literal name lookups (cm_il_method, find_method_address, hook_method, find_class, field_offset,
   find_method ...) in the same sources + Custom Modes solo part + loader sources: ok | missing | missing-both.
Outputs (--out): report.md, report.tsv, table_<plat>.tsv.

If one side has no script.json (Windows 25284551), methods are matched from dump.cs on BOTH sides
(name = Namespace.Class$$Method, key = the C# declaration); metadata slots then cannot be twinned (ambiguous).
"""
import argparse, bisect, collections, difflib, glob, json, os, re, struct, sys, time

T0 = time.time()
HOME = os.path.expanduser("~")
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
ap.add_argument("--plat", choices=("mac", "win"), required=True)
ap.add_argument("--old-bin", required=True); ap.add_argument("--old-dump", required=True)
ap.add_argument("--new-bin", required=True); ap.add_argument("--new-dump", required=True)
ap.add_argument("--mp", default=os.path.join(HOME, "ds_multiplayer"))
ap.add_argument("--cm", default=os.path.join(HOME, "ds_custommodes"))
ap.add_argument("--loader", default=os.path.join(REPO, "loader"))
ap.add_argument("--win-src", help="Windows multiplayer sources already ported (gen_port output); --plat win")
ap.add_argument("--win-map", help="Mac->Windows map (default MP/win/win_map.tsv); --plat win without --win-src")
ap.add_argument("--ref", default="25846389", help="reference build of the loader rva tables")
ap.add_argument("--old-ver"); ap.add_argument("--new-ver")
ap.add_argument("--addrs", help="take the addresses from this TSV instead of the sources (validation: a table's new column)")
ap.add_argument("--addrs-col", type=int, default=1, help="1-based column of --addrs (default 1)")
ap.add_argument("--out", required=True)
A = ap.parse_args()
os.makedirs(A.out, exist_ok=True)

def ver_of(*paths):
    for p in paths:
        m = re.search(r"(\d{8})", p)
        if m:
            return m.group(1)
    return "?"
OLD_VER = A.old_ver or ver_of(A.old_bin, A.old_dump)
NEW_VER = A.new_ver or ver_of(A.new_bin, A.new_dump)
PLAT_LABEL = "mac-arm64" if A.plat == "mac" else "win-x64"
def log(*a):
    print(f"[{time.time() - T0:6.1f}s]", *a, file=sys.stderr, flush=True)

# =====================================================================================================
# 1. harvest
# =====================================================================================================
HEX = re.compile(r"\b0x([0-9A-Fa-f]{5,8})\b")
TOK = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/', re.S)
SRC_EXT = (".c", ".h")

def strip_comments(text):
    """Comments -> blanks (newlines kept, so line numbers stay right). Strings stay."""
    def rep(m):
        s = m.group(0)
        return s if s[0] in "\"'" else re.sub(r"[^\n]", " ", s)
    return TOK.sub(rep, text)

def walk_src(top, skip=lambda d: False):
    if os.path.isfile(top):
        yield top; return
    for d, dirs, files in os.walk(top):
        dirs[:] = sorted(x for x in dirs if not x.startswith(".") and not skip(os.path.join(d, x)))
        for f in sorted(files):
            if f.endswith(SRC_EXT):
                yield os.path.join(d, f)

def is_test_dir(p):
    b = os.path.basename(p)
    return b == "test" or b.endswith("_test") or b == "tests" or b == "quickjs"

def rel(fp):
    for tag, root in (("mp", A.mp), ("cm", A.cm), ("loader", A.loader), ("winsrc", A.win_src or "\0")):
        root = os.path.abspath(root)
        if os.path.abspath(fp).startswith(root + os.sep):
            return f"{tag}/{os.path.relpath(fp, root)}"
    return fp.replace(HOME, "~")

idents = set()
code_cache = {}
def read_code(fp):
    if fp not in code_cache:
        raw = open(fp, errors="replace").read()
        idents.update(re.findall(r"[A-Za-z_][A-Za-z0-9_]{3,}", raw))
        code_cache[fp] = (raw, strip_comments(raw))
    return code_cache[fp]

def harvest_c(files, sink):
    for fp in files:
        _, code = read_code(fp)
        for n, line in enumerate(code.split("\n"), 1):
            for m in HEX.finditer(line):
                v = int(m.group(1), 16)
                if 0x10000 <= v < 0x8000000:
                    sink[v].append(f"{rel(fp)}:{n}")

mp_files = list(walk_src(os.path.join(A.mp, "src"), is_test_dir))
smoke = os.path.join(A.mp, "src/ord_test/ord_smoke.c")      # a test folder, but built into the module (src/build.sh)
if os.path.exists(smoke):
    mp_files.append(smoke)
mp_mod = os.path.join(A.loader, "modules/multiplayer/mp_module.c")
if os.path.exists(mp_mod):
    mp_files.append(mp_mod)
cm_files = []
for p in ("cm_mp.c", "fe", "pvp", "units/pvp_units.c"):
    if os.path.exists(os.path.join(A.cm, p)):
        cm_files += list(walk_src(os.path.join(A.cm, p), is_test_dir))

used = collections.defaultdict(list)          # address in OLD build -> ["file:line"]
ROUND = lambda v: v % 0x10000 == 0 or (v & (v + 1)) == 0      # 0x100000, 0x7FFFF ...: sizes / masks, never an RVA
hint = {}                                     # address -> readable name from a table / define
patches = {}                                  # address -> expected bytes (hell patch tables)
untranslated = collections.defaultdict(list)  # win: Mac literal with no Windows twin
via_map = set()
if A.addrs:
    for n, line in enumerate(open(A.addrs), 1):
        f = line.rstrip("\n").split("\t")
        if not line.startswith("#") and len(f) >= A.addrs_col and f[A.addrs_col - 1].startswith("0x"):
            used[int(f[A.addrs_col - 1], 16)].append(f"{os.path.basename(A.addrs)}:{n}")
    for fp in mp_files + cm_files:
        read_code(fp)
elif A.plat == "mac":
    harvest_c(mp_files + cm_files, used)
elif A.win_src:
    harvest_c(list(walk_src(A.win_src, is_test_dir)), used)
    for fp in mp_files + cm_files:
        read_code(fp)                         # identifiers only
else:
    wm = A.win_map or os.path.join(A.mp, "win/win_map.tsv")
    tr = {}
    for line in open(wm):
        f = line.rstrip("\n").split("\t")
        if f[0] in ("method", "metadata", "export", "internal") and len(f) > 3 and f[3]:
            tr[int(f[2], 16)] = int(f[3], 16)
    mac_used = collections.defaultdict(list)
    harvest_c(mp_files + cm_files, mac_used)
    for v, where in mac_used.items():
        if v in tr:
            used[tr[v]].extend(f"{w} (mac {v:#x})" for w in where); via_map.add(tr[v])
        else:
            untranslated[v].extend(where)

# loader per-version tables
for fp in ([] if A.addrs else sorted(glob.glob(os.path.join(A.loader, f"modules/*/rva/{A.ref}_{A.plat}*.tsv")))):
    for n, line in enumerate(open(fp), 1):
        if line.startswith("#") or not line.strip():
            continue
        f = line.rstrip("\n").split("\t")
        if f[0] == "patch" and len(f) >= 3:
            v = int(f[1], 16); patches[v] = bytes.fromhex(f[2])
            hint.setdefault(v, "patch " + (f[4] if len(f) > 4 else ""))
        elif len(f) >= 2 and f[1].startswith("0x"):
            v = int(f[1], 16); hint.setdefault(v, f[0])
        else:
            continue
        used[v].append(f"{rel(fp)}:{n}")
if A.plat == "win" and not A.addrs:
    fp = os.path.join(A.loader, "start/win/dsl_start_win.c")
    if os.path.exists(fp):
        for n, line in enumerate(open(fp), 1):
            m = re.match(r"\s*#define\s+(W_\w+)\s+0x([0-9A-Fa-f]+)", line)
            if m:
                v = int(m.group(2), 16); used[v].append(f"{rel(fp)}:{n}"); hint.setdefault(v, m.group(1))
log(f"harvest: {len(used)} distinct addresses" + (f", {len(untranslated)} Mac literals without a Windows twin" if A.plat == "win" and not A.win_src else ""))

# =====================================================================================================
# 2. builds
# =====================================================================================================
MI = re.compile(r"MethodInfo_[0-9A-F]+")
FS = re.compile(r"\s+")

class Build:
    def __init__(self, label, binpath, dump):
        self.label, self.binpath, self.dump = label, binpath, dump
        self.names_at = collections.defaultdict(list)   # addr -> [(name, sig, idx)]
        self.byname = collections.defaultdict(list)     # (name, sig) -> [addr] in dump order
        self.md_at = collections.defaultdict(list)      # addr -> [(name, idx)]
        self.md_byname = collections.defaultdict(list)  # name -> [addr]
        self.md_meth = {}                               # MethodInfo slot -> its method address
        self.extra_starts = []
        self._body = {}
        self._callers = None
        self.has_script = os.path.exists(os.path.join(dump, "script.json"))
        if A.plat == "mac":
            self._load_macho()
        else:
            self._load_pe()

    # ---------- dumps ----------
    def load_script(self, meta_only=False):
        s = json.load(open(os.path.join(self.dump, "script.json")))
        for m in ([] if meta_only else s["ScriptMethod"]):
            k = (m["Name"], MI.sub("MethodInfo", m["Signature"]))
            self.names_at[m["Address"]].append((k[0], k[1], len(self.byname[k])))
            self.byname[k].append(m["Address"])
        for m in s["ScriptMetadata"]:
            self._md(m["Address"], m["Name"])
        for m in s["ScriptMetadataMethod"]:
            self._md(m["Address"], m["Name"])
            if m.get("MethodAddress"):
                self.md_meth[m["Address"]] = m["MethodAddress"]
        for m in s["ScriptString"]:
            self._md(m["Address"], "string:" + m["Value"])
        if not meta_only:
            self.extra_starts = s["Addresses"]

    def _md(self, a, name):
        if any(n == name for n, _ in self.md_at[a]):
            return
        self.md_at[a].append((name, len(self.md_byname[name])))
        self.md_byname[name].append(a)

    def load_dumpcs(self):
        """Methods from dump.cs (RVA comment + declaration). Name = Namespace.Class$$Method, sig = declaration."""
        ns = cls = None; pending = None; gen_decl = None
        RVA = re.compile(r"^\t// RVA: 0x([0-9A-F]+) ")
        GRVA = re.compile(r"^\t\|-RVA: 0x([0-9A-F]+) ")
        for line in open(os.path.join(self.dump, "dump.cs"), errors="replace"):
            if line.startswith("// Namespace: "):
                ns = line[14:].strip(); continue
            if not line.startswith("\t"):
                m = CLS.match(line)
                if m:
                    cls = (ns + "." if ns else "") + m.group(2)
                continue
            m = RVA.match(line)
            if m:
                pending = int(m.group(1), 16); continue
            if pending is not None:
                decl = FS.sub(" ", line.strip())
                mm = re.search(r"([^\s(]+)\(", decl)
                nm = f"{cls}$${re.sub(r'<.*', '', mm.group(1)) if mm else '?'}"
                self._meth(pending, nm, decl); gen_decl = decl; pending = None; continue
            if line.startswith("\t/* GenericInstMethod") or line.startswith("\t/*"):
                continue
            m = GRVA.match(line)
            if m:
                pending_g = int(m.group(1), 16); self._g = pending_g; continue
            if line.startswith("\t|-") and getattr(self, "_g", None) is not None:
                self._meth(self._g, "g:" + line[3:].strip(), gen_decl or ""); self._g = None
            elif line.startswith("\t") and "(" in line and line.rstrip().endswith("{ }"):
                gen_decl = FS.sub(" ", line.strip())

    def _meth(self, a, nm, sig):
        k = (nm, sig)
        self.names_at[a].append((nm, sig, len(self.byname[k])))
        self.byname[k].append(a)

    # ---------- binaries ----------
    def _load_macho(self):
        import lief
        self.raw = open(self.binpath, "rb").read()
        b = lief.MachO.parse(self.binpath).at(0)
        self.exports = {s.value: s.name.lstrip("_") for s in b.exported_symbols if s.value}
        self.text = []; self.secs = []
        for sg in b.segments:
            for sec in sg.sections:
                self.secs.append((sec.virtual_address, sec.virtual_address + sec.size, sec.offset))
                if sec.name in ("__text", "il2cpp"):
                    self.text.append((sec.virtual_address, sec.virtual_address + sec.size, sec.offset))
        self.fstarts = list(b.function_starts.functions)
        self.ends = None

    def _load_pe(self):
        import pefile
        pe = pefile.PE(self.binpath, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
        self.exports = {e.address: e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
        self.raw = bytes(pe.__data__)
        self.secs = [(s.VirtualAddress, s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData), s.PointerToRawData)
                     for s in pe.sections]
        self.text = [(s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize, s.PointerToRawData)
                     for s in pe.sections if s.Characteristics & 0x20000000]
        sec = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".pdata")
        d = sec.get_data()
        pd = {}
        for b_, e_, u_ in struct.iter_unpack("<III", d[:len(d) // 12 * 12]):
            if b_:
                pd[b_] = (e_, self.read(u_, 1)[0] >> 3 & 4)
        self.pdata = pd
        self.fstarts = [b_ for b_, (e_, ch) in pd.items() if not ch]
        self.ends = {}

    def read(self, va, n):
        for lo, hi, off in self.secs:
            if lo <= va < hi:
                return self.raw[off + va - lo: off + va - lo + n]
        return b""

    def in_text(self, a):
        return any(lo <= a < hi for lo, hi, _ in self.text)

    def finish(self):
        st = set(self.fstarts) | set(self.names_at) | set(self.extra_starts) | {a for a in self.exports if self.in_text(a)}
        self.starts = sorted(a for a in st if self.in_text(a))
        self.startset = set(self.starts)

    def func_of(self, a):
        i = bisect.bisect_right(self.starts, a) - 1
        return self.starts[i] if i >= 0 else None

    def func_end(self, s):
        if A.plat == "win" and s in self.pdata:
            e = self.pdata[s][0]
            while e in self.pdata and self.pdata[e][1]:
                e = self.pdata[e][0]
            return e
        i = bisect.bisect_right(self.starts, s)
        e = self.starts[i] if i < len(self.starts) else s + 0x1000
        for lo, hi, _ in self.text:
            if lo <= s < hi:
                e = min(e, hi)
        return min(e, s + 0x100000)

    # ---------- masked body ----------
    def body(self, s):
        """(tuple of masked instructions, tuple of instruction addresses)"""
        if s not in self._body:
            e = self.func_end(s)
            self._body[s] = mask_arm64(self.read(s, e - s), s, e) if A.plat == "mac" else mask_x64(self.read(s, e - s), s, e)
        return self._body[s]

    # ---------- call graph ----------
    def callers(self, target):
        import numpy as np
        if self._callers is None:
            self._callers = []
            for lo, hi, off in self.text:
                if A.plat == "mac":
                    w = np.frombuffer(self.raw, dtype="<u4", count=(hi - lo) // 4, offset=off)
                    idx = np.nonzero(((w >> 26) == 0x25) | ((w >> 26) == 0x05))[0]
                    imm = (w[idx] & 0x3FFFFFF).astype(np.int64)
                    imm = np.where(imm & 0x2000000, imm - 0x4000000, imm)
                    src = lo + idx.astype(np.int64) * 4
                    self._callers.append((src, src + imm * 4))
                else:
                    b = np.frombuffer(self.raw, dtype=np.uint8, count=hi - lo - 5, offset=off)
                    idx = np.nonzero((b == 0xE8) | (b == 0xE9))[0]
                    rb = np.frombuffer(self.raw, dtype=np.uint8, count=hi - lo, offset=off)
                    rel32 = (rb[idx + 1].astype(np.int64) | (rb[idx + 2].astype(np.int64) << 8)
                             | (rb[idx + 3].astype(np.int64) << 16) | (rb[idx + 4].astype(np.int64) << 24))
                    rel32 = np.where(rel32 & 0x80000000, rel32 - 0x100000000, rel32)
                    src = lo + idx.astype(np.int64)
                    self._callers.append((src, src + 5 + rel32))
        out = []
        for src, dst in self._callers:
            out.extend(src[dst == target].tolist())
        return out

    def callees(self, s):
        e = self.func_end(s)
        out = []
        if A.plat == "mac":
            data = self.read(s, e - s)
            for i, (w,) in enumerate(struct.iter_unpack("<I", data[:len(data) // 4 * 4])):
                if (w & 0x7C000000) == 0x14000000:
                    imm = w & 0x3FFFFFF
                    t = s + 4 * i + 4 * (imm - 0x4000000 if imm & 0x2000000 else imm)
                    if not s <= t < e:
                        out.append(t)
        else:
            for ins in CS64.disasm(self.read(s, e - s), s):
                if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
                    t = int(ins.op_str, 16)
                    if not s <= t < e:
                        out.append(t)
        return out

CLS = re.compile(r"^(?:\[.*\]\s*)?(?:public|internal|private|protected)?[\w\s]*?\b(class|struct|enum|interface)\s+([^\s:]+).*// TypeDefIndex")

def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v

CALLEE_SAVED = sum(1 << r for r in range(19, 29))

def arm64_page_regs(ws, s, e):
    """Per instruction: bitmask of registers that MAY hold an ADRP page (forward may-dataflow over the
    branches inside the function, so a page register set before a branch is still known at its target)."""
    n = len(ws)
    succ = [None] * n; out_tr = [None] * n
    for i, w in enumerate(ws):
        pc = s + 4 * i; tgt = None; fall = True
        rd = w & 31
        if (w & 0x7C000000) == 0x14000000:                      # B / BL
            if w & 0x80000000:
                kill, gen = ~CALLEE_SAVED, 0                    # BL: x0-x18, x30 clobbered
            else:
                tgt = pc + 4 * sext(w & 0x3FFFFFF, 26); fall = False; kill, gen = 0, 0
        elif (w & 0x9F000000) == 0x90000000:                    # ADRP
            kill, gen = 0, 1 << rd
        elif (w & 0xFF000010) == 0x54000000 or (w & 0x7E000000) == 0x34000000:
            tgt = pc + 4 * sext((w >> 5) & 0x7FFFF, 19); kill, gen = 0, 0
        elif (w & 0x7E000000) == 0x36000000:
            tgt = pc + 4 * sext((w >> 5) & 0x3FFF, 14); kill, gen = 0, 0
        elif (w & 0xFFFFFC1F) in (0xD65F0000, 0xD61F0000):      # RET / BR
            fall = False; kill, gen = 0, 0
        elif (w & 0x0A000000) == 0x08000000:                    # load/store: a load writes Rt
            kill, gen = (1 << rd if w & (1 << 22) else 0), 0
        else:                                                   # data processing writes Rd
            kill, gen = 1 << rd, 0
        nx = []
        if fall and i + 1 < n:
            nx.append(i + 1)
        if tgt is not None and s <= tgt < e:
            nx.append((tgt - s) // 4)
        succ[i] = nx; out_tr[i] = (kill, gen)
    inn = [0] * n
    work = list(range(n - 1, -1, -1)); queued = [True] * n
    while work:
        i = work.pop(); queued[i] = False
        kill, gen = out_tr[i]
        o = (inn[i] & ~kill) | gen
        for j in succ[i]:
            if o & ~inn[j]:
                inn[j] |= o
                if not queued[j]:
                    queued[j] = True; work.append(j)
    return inn

def mask_arm64(data, s, e):
    ws = [w for (w,) in struct.iter_unpack("<I", data[:len(data) // 4 * 4])]
    page = arm64_page_regs(ws, s, e)
    ins, addrs = [], []
    for i, w in enumerate(ws):
        pc = s + 4 * i
        m = w
        if (w & 0x7C000000) == 0x14000000:                      # B / BL
            m = w & 0xFC000000
        elif (w & 0x1F000000) == 0x10000000:                    # ADR / ADRP
            m = w & 0x9F00001F
        elif (w & 0x3B000000) == 0x18000000:                    # LDR literal
            m = w & 0xFF00001F
        elif (w & 0xFF000010) == 0x54000000 or (w & 0x7E000000) == 0x34000000:   # B.cond, CBZ/CBNZ
            if not s <= pc + 4 * sext((w >> 5) & 0x7FFFF, 19) < e:
                m = w & 0xFF00001F
        elif (w & 0x7E000000) == 0x36000000:                    # TBZ/TBNZ
            if not s <= pc + 4 * sext((w >> 5) & 0x3FFF, 14) < e:
                m = w & 0xFFF8001F
        elif page[i] >> ((w >> 5) & 31) & 1 and ((w & 0x7F800000) == 0x11000000 or (w & 0x3B000000) == 0x39000000):
            m = w & 0xFFC003FF                                  # ADD imm / LDR-STR unsigned imm on a page reg
        ins.append(m); addrs.append(pc)
    return tuple(ins), tuple(addrs)

CS64 = None
def mask_x64(data, s, e):
    """Masked x64 body. MSVC switch tables sit after the code and are read as [base + idx*4 + table_rva]:
    image-relative displacements (>= 0x10000) are masked, the code stops at the first table, and table
    entries become offsets from the function start."""
    from capstone import x86
    global CS64
    if CS64 is None:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_64
        CS64 = Cs(CS_ARCH_X86, CS_MODE_64); CS64.detail = True
    ins, addrs = [], []
    cut = e
    for i in CS64.disasm(data, s):
        if i.address >= cut:
            break
        b = bytearray(i.bytes)
        for o in i.operands:
            if o.type != x86.X86_OP_MEM:
                continue
            d = o.mem.disp
            if o.mem.base == x86.X86_REG_RIP or 0x10000 <= d < 0x10000000:
                # capstone 5 reports disp_size 2 for some SSE forms (movdqa): find the disp32 bytes by value
                off = i.disp_offset if i.disp_size == 4 else bytes(b).find(struct.pack("<i", d), 1)
                if off > 0:
                    b[off:off + 4] = b"\0\0\0\0"
                if o.mem.base != x86.X86_REG_RIP and s < d < cut:
                    cut = d                                      # a switch table inside the function range
        if i.imm_size == 4 and (1 in i.groups or 2 in i.groups) and i.op_str.startswith("0x"):   # jump / call rel32
            t = int(i.op_str, 16)
            if 2 in i.groups or not s <= t < e:
                b[i.imm_offset:i.imm_offset + 4] = b"\0\0\0\0"
        ins.append(bytes(b)); addrs.append(i.address)
    while addrs and addrs[-1] >= cut:
        ins.pop(); addrs.pop()
    for k in range(cut - s, (e - s) // 4 * 4, 4):                # switch tables: case label RVAs -> offsets
        (w,) = struct.unpack_from("<I", data, k)
        ins.append(("T", w - s) if s <= w < e else ("T", w)); addrs.append(s + k)
    return tuple(ins), tuple(addrs)

def diff_count(a, b):
    if a == b:
        return 0
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    return sum(max(i2 - i1, j2 - j1) for op, i1, i2, j1, j2 in sm.get_opcodes() if op != "equal")

def aligned_offset(ob, nb, off):
    """New offset of the instruction at old offset `off` when it sits in an equal block, else None."""
    (oi, oa), (ni, na) = ob, nb
    if not oa:
        return None
    try:
        k = oa.index(oa[0] + off)
    except ValueError:
        return None
    sm = difflib.SequenceMatcher(None, oi, ni, autojunk=False)
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == "equal" and i1 <= k < i2:
            return na[j1 + k - i1] - na[0]
    return None

log("loading old build"); OLD = Build("old", A.old_bin, A.old_dump)
log("loading new build"); NEW = Build("new", A.new_bin, A.new_dump)
MODE = "script" if OLD.has_script and NEW.has_script else "dumpcs"
for B in (OLD, NEW):
    if MODE == "script":
        B.load_script()
    else:
        B.load_dumpcs()
        if B.has_script:
            B.load_script(meta_only=True)              # metadata slots still classified on this side
    B.finish()
NEW_EXP = {v: k for k, v in NEW.exports.items()}
log(f"dumps loaded ({MODE}): old {len(OLD.names_at)} method addresses, new {len(NEW.names_at)}")

# =====================================================================================================
# 3/4. twins and body compare
# =====================================================================================================
def method_twin(a):
    """-> (new address | None, name, note, ambiguous)"""
    names = OLD.names_at[a]
    votes = collections.Counter(); first = {}; notes = []
    for nm, sig, idx in names[:24]:
        cands = NEW.byname.get((nm, sig))
        if not cands:
            continue
        olds = OLD.byname[(nm, sig)]
        if len(cands) == 1 and len(olds) == 1:
            t = cands[0]; how = ""
        else:
            ob = OLD.body(a)[0]
            eq = sorted({c for c in cands if NEW.body(c)[0] == ob})
            if len(eq) == 1:
                t = eq[0]; how = f"overload picked by equal body ({len(olds)} old / {len(cands)} new)"
            elif len(cands) == len(olds):
                t = cands[idx]; how = f"overload order ({len(olds)})"
            else:
                notes.append(f"{nm}: {len(olds)} old vs {len(cands)} new overloads")
                continue
        votes[t] += 1; first.setdefault(t, (nm, how))
    if not votes and not notes:
        return renumbered_twin(a)
    if not votes:
        return None, names[0][0], "; ".join(notes[:2]), bool(notes)
    t, c = votes.most_common(1)[0]
    nm, how = first[t]
    note = how
    if len(names) > 1:
        note = (note + "; " if note else "") + f"shared body ({len(names)} names)"
        if len(votes) > 1:
            note += f", names split over {len(votes)} new addresses"
    return t, nm, note, False

GENNUM = re.compile(r"((?:d|b|c__DisplayClass|U)__)\d+(?:_\d+)?")
_renum = None
def renumbered_twin(a):
    """Compiler-generated names renumber between builds (<OnEvent>d__31 -> d__30): match with the numbers
    blanked; a candidate with the same body wins, a lone candidate with another body is only a hint."""
    global _renum
    nm0 = OLD.names_at[a][0][0]
    if not GENNUM.search(nm0):
        return None, nm0, "", False
    if _renum is None:
        _renum = collections.defaultdict(set)
        for (nm, sig), addrs in NEW.byname.items():
            if GENNUM.search(nm):
                _renum[(GENNUM.sub(r"\1#", nm), GENNUM.sub(r"\1#", sig))].update(addrs)
    cands = set()
    for nm, sig, _ in OLD.names_at[a][:24]:
        cands |= _renum.get((GENNUM.sub(r"\1#", nm), GENNUM.sub(r"\1#", sig)), set())
    if not cands:
        return None, nm0, "", False
    ob = OLD.body(a)[0]
    eq = sorted(c for c in cands if NEW.body(c)[0] == ob)
    if len(eq) == 1:
        return eq[0], nm0, f"compiler-generated number changed (new name {NEW.names_at[eq[0]][0][0]})", False
    if len(cands) == 1:
        c = next(iter(cands))
        return None, nm0, f"renumbered? lone candidate {c:#x} {NEW.names_at[c][0][0]} has another body", True
    return None, nm0, f"renumbered? {len(cands)} candidates, none with the same body", True

def metadata_twin(a):
    nm, idx = OLD.md_at[a][0]
    cands = NEW.md_byname.get(nm)
    if not cands:
        return None, nm, "", False
    olds = OLD.md_byname[nm]
    if len(cands) == 1 and len(olds) == 1:
        return cands[0], nm, "", False
    if a in OLD.md_meth:
        mt = method_twin(OLD.md_meth[a])[0] if OLD.md_meth[a] in OLD.names_at else None
        hit = [c for c in cands if mt is not None and NEW.md_meth.get(c) == mt]
        if len(hit) == 1:
            return hit[0], nm, f"duplicate name ({len(olds)}), picked by its method's twin", False
    if len(cands) == len(olds):
        return cands[idx], nm, f"duplicate name, order {idx + 1}/{len(olds)}", False
    return None, nm, f"{len(olds)} old vs {len(cands)} new slots with this name", True

def vote_internal(t, limit=150):
    votes = collections.Counter(); n = 0; seen = set()
    for c in OLD.callers(t):
        f = OLD.func_of(c)
        if f is None or f in seen:
            continue
        seen.add(f)
        if f in OLD.names_at:
            w = method_twin(f)[0]
        elif f in OLD.exports:
            w = NEW_EXP.get(OLD.exports[f])
        else:
            continue
        if w is None:
            continue
        votes.update({x for x in NEW.callees(w) if x not in NEW.names_at and NEW.in_text(x)})
        n += 1
        if n >= limit:
            break
    return votes, n

def func_twin(f):
    """Twin of an OLD function start. -> (new | None, kind, name, note, ambiguous)"""
    if f in OLD.names_at:
        t, nm, note, amb = method_twin(f)
        return t, "method", nm, note, amb
    if f in OLD.exports:
        nm = OLD.exports[f]
        return NEW_EXP.get(nm), "export", nm, "", False
    votes, n = vote_internal(f)
    if not votes:
        return None, "internal", f"sub_{f:x}", (f"{n} caller twins, no vote" if n else "no direct caller"), n > 0
    top = votes.most_common(3)
    ob = OLD.body(f)[0]
    best = top[0]
    for cand, c in top:                              # prefer an equal body among the leaders
        if c >= top[0][1] * 0.5 and NEW.body(cand)[0] == ob:
            best = (cand, c); break
    strong = best[1] >= max(2, n // 2)
    return best[0], "internal", f"sub_{f:x}", f"vote {best[1]}/{n}", not strong

def compare(a, t):
    """status + differing instruction count of function a (old) vs t (new)"""
    ob, nb = OLD.body(a), NEW.body(t)
    if ob[0] == nb[0]:
        return ("same" if a == t else "moved"), 0
    return "changed", diff_count(ob[0], nb[0])

rows = []   # (kind, name, old, new, status, note, used_in)
def where_of(v):
    w = sorted(set(used.get(v, [])))
    return ",".join(w[:6]) + (f",+{len(w) - 6}" if len(w) > 6 else "")

for v in sorted(used):
    W = where_of(v)
    hn = hint.get(v)
    if ROUND(v) and v not in OLD.names_at and v not in hint:
        rows.append(("other", "", v, None, "n/a", "round constant (size / mask), not an address", W)); continue
    if v in OLD.names_at:
        t, nm, note, amb = method_twin(v)
        if t is None:
            rows.append(("method", nm, v, None, "ambiguous" if amb else "gone", note, W)); continue
        st, d = compare(v, t)
        if st == "changed" and GENNUM.search(nm):          # numbering shifted: a sibling may hold this body
            t2, _, n2, _ = renumbered_twin(v)
            if t2 is not None:
                t, note = t2, n2
                st, d = compare(v, t)
        if st == "changed":
            note = f"{d} instructions differ" + ("; " + note if note else "")
        rows.append(("method", nm, v, t, st, note, W)); continue
    if v in OLD.md_at:
        if MODE != "script":
            rows.append(("metadata", OLD.md_at[v][0][0], v, None, "ambiguous", "no script.json on one side: metadata not twinned", W)); continue
        t, nm, note, amb = metadata_twin(v)
        st = "ambiguous" if amb else "gone" if t is None else "same" if t == v else "moved"
        rows.append(("metadata", nm, v, t, st, note, W)); continue
    if v in OLD.exports:
        nm = OLD.exports[v]; t = NEW_EXP.get(nm)
        if t is None:
            rows.append(("export", nm, v, None, "gone", "", W)); continue
        st, d = compare(v, t)
        rows.append(("export", nm, v, t, st, f"{d} instructions differ" if d else "", W)); continue
    if OLD.in_text(v):
        f = OLD.func_of(v)
        if f is not None and f != v and v >= OLD.func_end(f) and A.plat == "win":
            f = v                                          # past the .pdata range: a leaf function without unwind data
            OLD.starts.insert(bisect.bisect_left(OLD.starts, v), v); OLD.startset.add(v)
        if f == v:
            t, kind, nm, note, amb = func_twin(v)
            if t is None:
                rows.append(("internal", hn or nm, v, None, "ambiguous" if amb else "gone", note, W)); continue
            st, d = compare(v, t)
            if amb and st != "same" and st != "moved":
                st = "ambiguous"; note += "; weak vote and body differs"
            elif d:
                note = f"{d} instructions differ; " + note
            rows.append(("internal", hn or nm, v, t, st, note, W)); continue
        off = v - f
        ob = OLD.body(f)
        if v not in set(ob[1]):
            rows.append(("other", hn or "", v, None, "n/a", f"not an instruction boundary in {f:#x} (a constant)", W)); continue
        t, kind, nm, note, amb = func_twin(f)
        name = f"{nm}+{off:#x}"
        pnote = ""
        if t is None:
            rows.append(("inside", name, v, None, "ambiguous" if amb else "gone", f"{kind} twin: " + (note or "none"), W)); continue
        nb = NEW.body(t)
        if v in patches:
            pnote = "; patch bytes " + ("found" if NEW.read(t + off, len(patches[v])) == patches[v] else "NOT at new site")
        if ob[0] == nb[0] and (not amb or kind == "internal"):
            rows.append(("inside", name, v, t + off, "same" if t + off == v else "moved", (note + pnote).strip("; "), W)); continue
        g = aligned_offset(ob, nb, off)
        gn = f"body changed ({diff_count(ob[0], nb[0])} instr); " + (f"aligned guess {t + g:#x}" if g is not None else "no aligned guess")
        if g is not None and v in patches:
            gn += "; patch bytes at guess " + ("found" if NEW.read(t + g, len(patches[v])) == patches[v] else "NOT found")
        rows.append(("inside", name, v, None, "ambiguous", gn + (f"; {kind} at {t:#x}" if t else ""), W)); continue
    rows.append(("other", hn or "", v, None, "n/a", "not code, not metadata (constant / data / bounds)", W))
for v in sorted(untranslated):
    if ROUND(v):
        rows.append(("other", "", v, None, "n/a", "round constant (size / mask), not an address", ",".join(sorted(set(untranslated[v]))[:6])))
    else:
        rows.append(("other", "", v, None, "untranslated", "Mac literal with no row in win_map.tsv: re-run win/win_map.py "
                     "(gen_port.py fails on it if it is an RVA)", ",".join(sorted(set(untranslated[v]))[:6])))
log(f"addresses resolved: {len(rows)} rows")

# =====================================================================================================
# 5. fields
# =====================================================================================================
FLD = re.compile(r"^\t(?!//)(.*?)\s+(\S+); // 0x([0-9A-F]+)$")
def fields(path):
    out = {}; ns = ""; cls = None
    for line in open(path, errors="replace"):
        if line.startswith("// Namespace: "):
            ns = line[14:].strip()
        if not line.startswith("\t"):
            m = CLS.match(line)
            if m:
                cls = (ns + "." if ns else "") + m.group(2)
            continue
        if cls:
            m = FLD.match(line.rstrip("\n"))
            if m and "const" not in m.group(1).split():
                key = ("static:" if "static" in m.group(1).split() else "") + m.group(2)
                out[(cls, key)] = int(m.group(3), 16)
    return out
of = fields(os.path.join(A.old_dump, "dump.cs"))
nf = fields(os.path.join(A.new_dump, "dump.cs"))
used_cls = {k[0] for k in of if k[0].rsplit(".", 1)[-1].split("<")[0] in idents}
for k in sorted(of):
    if k[0] in used_cls:
        w = nf.get(k)
        st = "gone" if w is None else "same" if w == of[k] else "moved"
        rows.append(("field", f"{k[0]}.{k[1]}", of[k], w, st, "", ""))
log(f"fields: {len(used_cls)} classes")

# =====================================================================================================
# 6. name lookups
# =====================================================================================================
def dump_index(path):
    """(ns, class) -> {"fields": set, "methods": {name: [param type lists]}, "bases": [...]}; also by short name"""
    idx = {}; ns = ""; cur = None
    for line in open(path, errors="replace"):
        if line.startswith("// Namespace: "):
            ns = line[14:].strip(); continue
        if not line.startswith("\t"):
            m = CLS.match(line)
            if m:
                full = m.group(2)
                bases = []
                bm = re.search(r":\s*(.*?)\s*// TypeDefIndex", line)
                if bm:
                    bases = [b.strip().split("<")[0] for b in split_top(bm.group(1))]
                cur = {"fields": set(), "methods": collections.defaultdict(list), "bases": bases}
                for key in {(ns, full), (ns, full.split(".")[-1]), (ns, full.split("<")[0])}:
                    idx.setdefault(key, cur)
            continue
        if cur is None or line.startswith("\t//"):
            continue
        s = line.strip()
        m = FLD.match(line.rstrip("\n"))
        if m:
            cur["fields"].add(m.group(2)); continue
        if s.endswith("{ }") and "(" in s:
            mm = re.search(r"([^\s(]+)\((.*)\)\s*\{ \}$", s)
            if mm:
                nm = re.sub(r"<.*", "", mm.group(1))
                ps = [p.strip() for p in split_top(mm.group(2))] if mm.group(2).strip() else []
                cur["methods"][nm].append([p.rsplit(" ", 1)[0].split()[-1] if " " in p else p for p in ps])
    return idx

def split_top(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "<[(":
            depth += 1
        elif ch in ">])":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return out

PRIM = {"Single": "float", "Int32": "int", "Boolean": "bool", "String": "string", "Object": "object", "Double": "double",
        "Int64": "long", "UInt32": "uint", "Byte": "byte", "Char": "char", "Int16": "short", "UInt64": "ulong",
        "UInt16": "ushort", "SByte": "sbyte"}
def short_type(t):
    t = t.strip().split(".")[-1]
    return PRIM.get(t, t)

LOOK = re.compile(r"\b(?:\w+(?:->|\.))?(cm_il_method|find_method_address|dsl_find_method_address|hook_method|"
                  r"cm_il_class|find_class|dsl_find_class|cm_il_field|field_offset|dsl_field_offset|find_method|dsl_find_method)\s*\(")
def call_args(code, pos):
    depth, cur, out, i = 1, "", [], pos
    while i < len(code) and depth:
        ch = code[i]
        if ch == '"':
            j = i + 1
            while j < len(code) and code[j] != '"':
                j += 2 if code[j] == "\\" else 1
            cur += code[i:j + 1]; i = j + 1; continue
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                break
        if ch == "," and depth == 1:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
        i += 1
    out.append(cur.strip())
    return out

look_files = list(dict.fromkeys(mp_files + cm_files + list(walk_src(A.cm, is_test_dir))
                                + [f for top in ("src", "modules", "ui", "start") for f in walk_src(os.path.join(A.loader, top), is_test_dir)]))
gconst = {}
for fp in look_files:
    for m in re.finditer(r'#define\s+(\w+)\s+"([A-Za-z][\w.]*)"', read_code(fp)[1]):
        gconst.setdefault(m.group(1), m.group(2))
lookups = collections.OrderedDict()   # key -> [where]
for fp in look_files:
    code = read_code(fp)[1]
    const = dict(gconst)
    for m in re.finditer(r'\b(\w+)\s*=\s*"([A-Za-z][\w.]*)"', code):
        const[m.group(1)] = m.group(2)
    def val(x):
        x = x.strip()
        if re.fullmatch(r'"[^"]*"', x):
            return x[1:-1]
        if x in const:
            return const[x]
        if x in ("NULL", "0", "nullptr"):
            return ""
        return None
    clsvar = {}
    for m in LOOK.finditer(code):
        fn = m.group(1); args = call_args(code, m.end())
        line = code.count("\n", 0, m.start()) + 1
        wh = f"{rel(fp)}:{line}"
        pre = code[max(0, m.start() - 80):m.start()]
        asg = re.search(r"(\w+)\s*=\s*(?:\([^()]*\)\s*)?$", pre)
        if fn == "hook_method":
            args = args[1:]
        key = None
        if fn in ("cm_il_method", "find_method_address", "dsl_find_method_address", "hook_method") and len(args) >= 4:
            ns, k, mn = (val(a) for a in args[:3])
            n = args[3].strip()
            sig = val(args[4]) if fn == "cm_il_method" and len(args) > 4 else ""
            if None not in (ns, k, mn) and re.fullmatch(r"-?\d+", n):
                key = ("method", ns, k, mn, int(n), sig or "")
        elif fn in ("cm_il_class", "find_class", "dsl_find_class") and len(args) == 2:
            ns, k = val(args[0]), val(args[1])
            if None not in (ns, k):
                key = ("class", ns, k)
                if asg:
                    clsvar[asg.group(1)] = (ns, k)
        elif fn in ("cm_il_field", "field_offset", "dsl_field_offset") and len(args) == 2:
            f = val(args[1])
            if f:
                key = ("field", clsvar.get(args[0].strip()), f)
        elif fn in ("find_method", "dsl_find_method") and len(args) == 3:
            mn = val(args[1])
            if mn and re.fullmatch(r"-?\d+", args[2].strip()):
                key = ("cmethod", clsvar.get(args[0].strip()), mn, int(args[2]))
        if key:
            lookups.setdefault(key, []).append(wh)

def check_lookup(idx, key):
    def cls_of(ns, k):
        return idx.get((ns, k)) or idx.get((ns, k.replace("/", ".")))
    def chain(c):
        seen = 0
        while c and seen < 12:
            yield c
            nxt = None
            for b in c["bases"]:
                cand = [v for (n_, k_), v in idx.items() if k_ == b.split(".")[-1]]
                if cand:
                    nxt = cand[0]; break
            c = nxt; seen += 1
    kind = key[0]
    if kind == "class":
        return cls_of(key[1], key[2]) is not None, ""
    if kind == "method":
        _, ns, k, mn, n, sig = key
        c = cls_of(ns, k)
        if c is None:
            return False, "class not found"
        ov = c["methods"].get(mn)
        if not ov:
            return False, "method not found"
        hits = [p for p in ov if n < 0 or len(p) == n]
        if not hits:
            return False, f"no overload with {n} params"
        if sig:
            want = [short_type(t) for t in sig.split(",")]
            if not any([short_type(t) for t in p] == want for p in hits):
                return True, f"param types {sig} not matched exactly (soft)"
        return True, ""
    if kind in ("field", "cmethod"):
        cv = key[1]
        if cv:
            c = cls_of(*cv)
            if c is None:
                return False, "class not found"
            for cc in chain(c):
                if kind == "field" and key[2] in cc["fields"]:
                    return True, ""
                if kind == "cmethod" and any(key[3] < 0 or len(p) == key[3] for p in cc["methods"].get(key[2], [])):
                    return True, ""
            return False, "not in class or its bases"
        anyhit = any((key[2] in c["fields"]) if kind == "field" else (key[2] in c["methods"]) for c in idx.values())
        return anyhit, "class not known statically; name checked in any class"
    return False, "?"

log(f"lookups: {len(lookups)} distinct, indexing dumps")
oidx = dump_index(os.path.join(A.old_dump, "dump.cs"))
nidx = dump_index(os.path.join(A.new_dump, "dump.cs"))
for key, wh in lookups.items():
    ok_o, _ = check_lookup(oidx, key)
    ok_n, note = check_lookup(nidx, key)
    if key[0] == "method":
        nm = f"{key[1]}.{key[2]}::{key[3]}/{key[4]}" + (f"({key[5]})" if key[5] else "")
    elif key[0] == "class":
        nm = f"{key[1]}.{key[2]}"
    elif key[0] == "field":
        nm = f"{'.'.join(key[1]) if key[1] else '?'}.{key[2]} (field)"
    else:
        nm = f"{'.'.join(key[1]) if key[1] else '?'}::{key[2]}/{key[3]}"
    st = "ok" if ok_n else ("missing" if ok_o else "missing-both")
    rows.append(("lookup", nm, "yes" if ok_o else "no", "yes" if ok_n else "no", st, note, ",".join(sorted(set(wh))[:6])))

# =====================================================================================================
# 7. outputs
# =====================================================================================================
def fx(x):
    return "" if x is None else x if isinstance(x, str) else f"{x:#x}"
with open(os.path.join(A.out, "report.tsv"), "w") as o:
    o.write("kind\tname\told\tnew\tstatus\tnote\tused_in\n")
    for r in rows:
        o.write("\t".join([r[0], r[1], fx(r[2]), fx(r[3]), r[4], r[5], r[6]]) + "\n")

TABLE_KINDS = ("method", "inside", "metadata", "export", "internal")
tab = os.path.join(A.out, f"table_{A.plat}.tsv")
with open(tab, "w") as o:
    o.write(f"# {PLAT_LABEL} address table: reference {OLD_VER} -> {NEW_VER}, made by tools/gameupd/report.py\n")
    o.write("# old <TAB> new <TAB> kind <TAB> name <TAB> status (same|moved|changed); GONE / AMBIGUOUS rows are comments\n")
    if A.plat == "win" and not A.win_src:
        o.write("# Windows reference addresses = Mac source literals translated through win_map.tsv (as gen_port.py does)\n")
    for r in sorted((r for r in rows if r[0] in TABLE_KINDS), key=lambda r: r[2]):
        nm = r[1].replace("\t", " ")
        if r[4] in ("same", "moved", "changed") and r[3] is not None:
            o.write(f"{r[2]:#X}\t{r[3]:#X}\t{r[0]}\t{nm}\t{r[4]}\n".replace("0X", "0x"))
        else:
            o.write(f"# {r[4].upper()} {r[2]:#X} {r[0]} {nm}\n".replace("0X", "0x"))

cnt = collections.Counter((r[0], r[4]) for r in rows)
kinds = ["method", "inside", "metadata", "export", "internal", "other", "field", "lookup"]
stats = ["same", "moved", "changed", "gone", "ambiguous", "ok", "missing", "missing-both", "untranslated", "n/a"]
md = [f"# Game-update report: {PLAT_LABEL} {OLD_VER} -> {NEW_VER}", "",
      f"Old: `{A.old_bin.replace(HOME, '~')}` + `{A.old_dump.replace(HOME, '~')}`  ",
      f"New: `{A.new_bin.replace(HOME, '~')}` + `{A.new_dump.replace(HOME, '~')}`  ",
      f"Method matching from: {'script.json' if MODE == 'script' else 'dump.cs on both sides (no script.json on one side; metadata not twinned)'}  ",
      f"Addresses harvested: {len(used)}" + (f" (+{len(untranslated)} Mac literals with no Windows twin, not translated)" if untranslated else ""), "",
      "## Counts", "", "| kind | " + " | ".join(stats) + " |", "|---" * (len(stats) + 1) + "|"]
for k in kinds:
    if any(cnt[(k, s)] for s in stats):
        md.append(f"| {k} | " + " | ".join(str(cnt[(k, s)] or "") for s in stats) + " |")
need = [r for r in rows if r[4] in ("changed", "gone", "ambiguous", "missing", "untranslated")
        or (r[0] == "field" and r[4] == "moved")
        or (r[0] in TABLE_KINDS and "NOT" in r[5])]
md += ["", f"## Needs a look ({len(need)} rows)", ""]
if not need:
    md.append("Nothing. Every used address has a twin with the same code, every field and name lookup is unchanged.")
for k in kinds:
    sel = [r for r in need if r[0] == k]
    if not sel:
        continue
    md += [f"### {k} ({len(sel)})", "", "| status | name | old | new | note | used in |", "|---|---|---|---|---|---|"]
    for r in sel[:400]:
        md.append(f"| {r[4]} | `{r[1]}` | {fx(r[2])} | {fx(r[3])} | {r[5]} | {r[6].replace(',', ', ')[:200]} |")
    if len(sel) > 400:
        md.append(f"| ... | {len(sel) - 400} more in report.tsv | | | | |")
    md.append("")
md += ["", "Statuses: same = same address, same code; moved = new address, same code (the table handles it); "
       "changed = code differs (a person looks); gone = no twin; ambiguous = no safe twin (see note). "
       "Code compare masks only address operands (calls, page loads, jumps leaving the function).", ""]
open(os.path.join(A.out, "report.md"), "w").write("\n".join(md))

print(f"{PLAT_LABEL} {OLD_VER} -> {NEW_VER}: {len(rows)} rows, {len(need)} need a look, {time.time() - T0:.0f}s")
for k in kinds:
    line = ", ".join(f"{s} {cnt[(k, s)]}" for s in stats if cnt[(k, s)])
    if line:
        print(f"  {k:9s} {line}")
print(f"  out: {A.out.replace(HOME, '~')}")
