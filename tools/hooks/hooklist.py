#!/usr/bin/env python3
"""Machine-readable list of every native hook and code patch the loader and its official mods put into GameAssembly.

Owner order 2026-10-18: a BepInEx mod maker audits patches on load/save code and wants to list ours as known.
The list is made from the SOURCES (the loader tree + the pinned ds_multiplayer / ds_custommodes commits of
loader/mp_pin.txt and loader/cm_pin.txt), never typed by hand:

  every call of a hook primitive (dsl_hook, dsl_patch, rig_hook, host hook / hook_rva / hook_method / patch_rva and the
  wrappers around them, found automatically) is one site. Its game address comes from the call itself (a literal, a
  #define, a hook table row, an il2cpp name lookup, a module rva table); a site the scanner cannot follow must have a
  row in tools/hooks/sites.tsv, else the generator and the gate stop. Each address is then named from the
  Il2CppDumper dumps (script.json) of the supported game build, Mac arm64 and Windows x64.

Usage:
  python3 tools/hooks/hooklist.py gen   [--mac-dump DIR] [--win-dump DIR] [--out docs]   docs/hooks.json + docs/hooks.tsv
  python3 tools/hooks/hooklist.py check [--list docs/hooks.json]                         gate (sources only, no dump)
  python3 tools/hooks/hooklist.py xlog  LOG...                                           compare "hooked"/"patched" log lines
Sources: --mp / --cm = a ds_multiplayer / ds_custommodes tree (default: the pinned commit, read with git from MP_REPO /
CM_REPO, default ds_multiplayer / ds_custommodes in the home folder).
check: fails when the sources have a hook site that the list lacks (or a site it cannot follow). It reads no dump.
"""
import argparse, bisect, collections, fnmatch, glob, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
LOADER = os.path.join(REPO, "loader")
HOME = os.path.expanduser("~")
BUILD = "25846389"                       # the supported game build (BUILDING.md); rig.h RIG_BUILD
GAME_VERSION = "DS_B.0.4.29"
PLATS = ("mac", "win")
PLAT_LABEL = {"mac": "mac-arm64", "win": "win-x64"}

# ------------------------------------------------------------------------------------------------ source trees
class Tree:
    """files of a source tree: a folder on disk, or one commit of a git repo (read with git show)"""
    def __init__(self, root=None, repo=None, sha=None):
        self.root, self.repo, self.sha = root, repo, sha
        if repo:
            out = subprocess.run(["git", "-C", repo, "ls-tree", "-r", "--name-only", sha], capture_output=True, text=True, check=True).stdout
            self.names = out.split()
        else:
            self.names = sorted(os.path.relpath(os.path.join(d, f), root) for d, _, fs in os.walk(root) for f in fs)
    def read(self, rel):
        if self.repo:
            if not hasattr(self, "cache"): self.preload()
            if rel not in self.cache: raise SystemExit(f"hooklist: {rel} not in {self.repo} {self.sha[:7]}")
            return self.cache[rel]
        with open(os.path.join(self.root, rel), encoding="utf-8", errors="replace") as f:
            return f.read()
    def preload(self):
        """every source-like file of the commit in one git cat-file --batch"""
        want = [n for n in self.names if n.endswith((".c", ".h", ".m", ".py", ".sh", ".tsv", ".inc"))]
        out = subprocess.run(["git", "-C", self.repo, "cat-file", "--batch"], input="".join(f"{self.sha}:{n}\n" for n in want).encode(),
                             capture_output=True, check=True).stdout
        self.cache, i = {}, 0
        for n in want:
            j = out.index(b"\n", i); hdr = out[i:j].split()
            size = int(hdr[2]); self.cache[n] = out[j + 1:j + 1 + size].decode("utf-8", "replace"); i = j + 1 + size + 1
    def glob(self, pat):
        return [n for n in self.names if fnmatch.fnmatchcase(n, pat)]
    def has(self, rel):
        return rel in self.names

class Virtual:
    """C text cut out of another file (gen_port.py appends C code kept in Python strings to the Windows sources)"""
    def __init__(self, files): self.files = files; self.names = list(files)
    def read(self, rel): return self.files[rel]
    def glob(self, pat): return [n for n in self.names if fnmatch.fnmatchcase(n, pat)]
    def has(self, rel): return rel in self.files

def gen_port_c(mp):
    """the C blocks of win/port/gen_port.py that hold hook calls (Windows addresses), as one text"""
    if not mp.has("win/port/gen_port.py"): return ""
    t = mp.read("win/port/gen_port.py")
    blocks = [b for b in re.findall(r'"""(.*?)"""', t, re.S) + re.findall(r"'''(.*?)'''", t, re.S) if "rig_hook(" in b]
    return "\n".join(blocks)

def pinned(repo, pin_file):
    sha = open(os.path.join(LOADER, pin_file)).readline().strip()
    return Tree(repo=repo, sha=sha), sha

# ------------------------------------------------------------------------------------------------ C scanning
STR = r'"(?:\\.|[^"\\\n])*"'
TOKS = re.compile(STR + r"|'(?:\\.|[^'\\\n])*'|//[^\n]*|/\*.*?\*/", re.S)

def blank_comments(t):
    """comments -> spaces (offsets and lines kept); strings kept"""
    return TOKS.sub(lambda m: m.group(0) if m.group(0)[0] in "\"'" else re.sub(r"[^\n]", " ", m.group(0)), t)

def mask_strings(t):
    return re.sub(STR + r"|'(?:\\.|[^'\\\n])*'", lambda m: m.group(0)[0] + "_" * (len(m.group(0)) - 2) + m.group(0)[-1], t)

def split_args(s):
    """top-level comma split of an argument list text (no outer parens)"""
    out, depth, cur, i = [], 0, "", 0
    m = mask_strings(s)
    start = 0
    for i, c in enumerate(m):
        if c in "([{": depth += 1
        elif c in ")]}": depth -= 1
        elif c == "," and depth == 0:
            out.append(s[start:i].strip()); start = i + 1
    out.append(s[start:].strip())
    return [a for a in out] if s.strip() else []

def match_paren(m, i):
    """m[i] == '(' -> index of its ')' (m: strings masked)"""
    d = 0
    for j in range(i, len(m)):
        if m[j] in "([{": d += 1
        elif m[j] in ")]}":
            d -= 1
            if d == 0: return j
    return -1

class CFile:
    def __init__(self, rel, text, plat_hint=None):
        self.rel = rel
        self.text = blank_comments(text)
        self.m = mask_strings(self.text)
        self.line_at = [0]
        for i, c in enumerate(self.text):
            if c == "\n": self.line_at.append(i + 1)
        self.defines = {}
        for mm in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+(\w+)(?!\()[ \t]+(.+?)[ \t]*$", self.text, re.M):
            self.defines.setdefault(mm.group(1), mm.group(2).strip())
        self.conds = self._conds()
        self.funcs = self._funcs()
    def line(self, off):
        return bisect.bisect_right(self.line_at, off)
    def _conds(self):
        """per line (1-based): list of (expr, negated) of the enclosing #if blocks"""
        res, stack = [[]], []                       # stack frame: [exprs of the #if/#elif chain, in #else]
        def now():
            out = []
            for ex, els in stack:
                out += [(x, True) for x in ex[:-1]] + [(ex[-1], els)]
            return out
        for ln in self.text.split("\n"):
            mm = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", ln.strip())
            res.append(now())
            if not mm: continue
            k, e = mm.group(1), mm.group(2).strip()
            if k == "ifdef": stack.append([[f"defined({e})"], False])
            elif k == "ifndef": stack.append([[f"!defined({e})"], False])
            elif k == "if": stack.append([[e], False])
            elif k == "elif" and stack: stack[-1][0].append(e)
            elif k == "else" and stack: stack[-1][1] = True
            elif k == "endif" and stack: stack.pop()
        return res
    def _funcs(self):
        """top-level function bodies: (name, params text, body start, body end)"""
        out, d, i, m = [], 0, 0, self.m
        n = len(m)
        while i < n:
            c = m[i]
            if c == "{":
                if d == 0:
                    head = m[max(0, i - 600):i]
                    mm = re.search(r"(\w+)\s*\(([^;{}]*)\)\s*(?:__attribute__\s*\(\(.*?\)\)\s*)?$", head, re.S)
                    if mm and mm.group(1) not in ("if", "for", "while", "switch", "sizeof", "return"):
                        j = self._close(i)
                        out.append((mm.group(1), mm.group(2), i, j))
                        i = j + 1; continue
                d += 1
            elif c == "}": d = max(0, d - 1)
            i += 1
        return out
    def _close(self, i):
        d = 0
        for j in range(i, len(self.m)):
            if self.m[j] == "{": d += 1
            elif self.m[j] == "}":
                d -= 1
                if d == 0: return j
        return len(self.m) - 1
    def func_at(self, off):
        for f in self.funcs:
            if f[2] <= off <= f[3]: return f
        return None

def eval_cond(expr, defs):
    """#if expression with the platform's defines (C rules: unknown macro = 0)"""
    e = re.sub(r"defined\s*\(\s*(\w+)\s*\)|defined\s+(\w+)", lambda m: "1" if (m.group(1) or m.group(2)) in defs else "0", expr)
    e = re.sub(r"\b([A-Za-z_]\w*)\b", lambda m: str(defs.get(m.group(1), 0)) if not str(defs.get(m.group(1), 0)).startswith('"') else "1", e)
    e = e.replace("&&", " and ").replace("||", " or ").replace("!", " not ").replace(" not =", "!=")
    e = re.sub(r"(\d+)[uUlL]+\b", r"\1", e)
    try:
        return bool(eval(e, {"__builtins__": {}}))
    except Exception:
        raise SystemExit(f"hooklist: cannot evaluate #if {expr!r}")

def compiled(cf, off, defs):
    for expr, neg in cf.conds[cf.line(off)]:
        if eval_cond(expr, defs) == neg: return False
    return True

# ------------------------------------------------------------------------------------------------ primitives
# name -> (addr arg, who arg, kind, target kind). target kind: "ptr" (an absolute address), "rva", "name" (ns,k,m,argc
# at addr..addr+3), "patch" (rva + old/new bytes). who None = the module id only.
PRIMS = {
    "dsl_hook":    (1, 4, "detour", "ptr"),
    "dsl_patch":   (1, 5, "patch", "ptr"),
    "rig_hook":    (0, 3, "detour", "rva"),
    "hook":        (1, None, "detour", "ptr"),            # host->hook(self, target, repl, orig)
    "hook_rva":    (1, 4, "detour", "rva"),
    "hook_method": (1, None, "detour", "name"),
    "patch_rva":   (1, 5, "patch", "rva"),
}
REPL_ARG = {"dsl_hook": 2, "rig_hook": 1, "hook": 2, "hook_rva": 2, "hook_method": 5, "dsl_patch": 3, "patch_rva": 2}
# the primitives themselves and the host API that implements them (not hook sites)
IMPL = {("dsl_engine.c", "*"), ("dsl_engine_win.c", "*"), ("dsl_main.c", "mod_hook"), ("dsl_main.c", "h_hook"),
        ("dsl_main.c", "h_hook_method"), ("dsl_main.c", "h_hook_rva"), ("dsl_main.c", "h_patch_rva"),
        ("mp_module.c", "rig_hook"), ("cm_mp.c", "rig_hook"), ("rig_main_win.c", "rig_hook"), ("rig_main_win.c", "mh_hook"),
        ("rig_main.c", "cm_hook_rva"), ("rig_hook.c", "*")}

def is_impl(rel, fname):
    b = os.path.basename(rel)
    return (b, "*") in IMPL or (b, fname) in IMPL

# ------------------------------------------------------------------------------------------------ components
def components(mp, cm):
    """(owner, platform, tree, [files], defines, address space of literals: mac | win)"""
    L = Tree(root=LOADER)
    out = []
    core = [f for f in L.glob("src/*.c")] + L.glob("ui/*.c")
    out.append(("loader", "mac", L, core + L.glob("start/mac/*.c") + L.glob("start/mac/*.m"), {"__APPLE__": 1, "__aarch64__": 1}, "mac"))
    out.append(("loader", "win", L, [f for f in core if not f.endswith("/dsl_engine.c")] + L.glob("start/win/*.c"),
                {"_WIN32": 1, "_WIN64": 1, "__x86_64__": 1, "__MINGW32__": 1, "__MINGW64__": 1}, "win"))
    # every simple module = a folder of loader/modules with C sources (multiplayer is the pin's tree, below)
    for mid in sorted({f.split("/")[1] for f in L.glob("modules/*/*.c")} - {"multiplayer"}):
        fs = L.glob(f"modules/{mid}/*.c")
        out.append((mid, "mac", L, fs, {"__APPLE__": 1, "__aarch64__": 1}, "mac"))
        out.append((mid, "win", L, fs, {"_WIN32": 1, "_WIN64": 1, "__x86_64__": 1}, "win"))
    # multiplayer, Mac: the pin's src/build.sh dsrig list minus rig_main.c + rig_hook.c, + modules/multiplayer/mp_module.c
    t = mp.read("src/build.sh"); t = t[t.index("clang"):t.index("codesign")].replace("\\\n", " ")
    mac_src = [f"src/{w}" for w in t.split() if w.endswith(".c") and not w.startswith("-") and w not in ("rig_main.c", "rig_hook.c")]
    out.append(("multiplayer", "mac", mp, mac_src, {"__APPLE__": 1, "__aarch64__": 1}, "mac"))
    out.append(("multiplayer", "mac", L, ["modules/multiplayer/mp_module.c"], {"__APPLE__": 1, "__aarch64__": 1}, "mac"))
    # multiplayer, Windows (win/build_win.sh DSMP_TARGET=module): gen_port.py FILES (Mac addresses, translated) + port/
    skip = {"rig_main.c", "rig_hook.c", "rig_win.c", "rig_sock.c"}
    win_src = [f for f in mp.glob("src/*.c") if os.path.basename(f) not in skip] + ["src/ord_test/ord_smoke.c"]
    wdefs = {"_WIN32": 1, "_WIN64": 1, "__x86_64__": 1, "DSMP_MODULE": 1}
    out.append(("multiplayer", "win", mp, win_src, wdefs, "mac"))
    out.append(("multiplayer", "win", mp, [f for f in ("win/port/rig_main_win.c", "win/port/win_test_cmds.c", "win/port/win_kit.c",
                                                         "win/port/steam_stub_win.c") if mp.has(f)], wdefs, "win"))
    gp = gen_port_c(mp)
    if gp: out.append(("multiplayer", "win", Virtual({"win/port/gen_port.py": gp}), ["win/port/gen_port.py"], wdefs, "win"))
    # custom-modes: every source of its tree (only the files with hook calls matter); Mac addresses on both platforms
    cfs = [f for f in cm.names if f.endswith(".c") and "/test" not in f and not f.startswith("test")]
    out.append(("custom-modes", "mac", cm, cfs, {"__APPLE__": 1, "__aarch64__": 1}, "mac"))
    out.append(("custom-modes", "win", cm, cfs, {"_WIN32": 1, "_WIN64": 1, "__x86_64__": 1, "DSMP_MODULE": 1}, "mac"))
    return out

# ------------------------------------------------------------------------------------------------ site extraction
HEXRE = re.compile(r"^0x[0-9A-Fa-f]+$")
LOOKUP_NAME = re.compile(r"\b(?:dsl_find_method_address|find_method_address|meth|named|hook_named)\s*\(\s*(.*)\)\s*$", re.S)

def unstr(s):
    s = s.strip()
    return s[1:-1] if len(s) >= 2 and s[0] == '"' and s[-1] == '"' else None

def strip_casts(e):
    e = e.strip()
    while True:
        n = re.sub(r"^\(\s*(?:const\s+)?(?:void|uintptr_t|uint8_t|char|Obj)\s*\**\s*\)\s*", "", e)
        n = re.sub(r"^rv\s*\((.*)\)$", r"\1", n.strip(), flags=re.S).strip()
        if n.startswith("(") and n.endswith(")") and match_paren(mask_strings(n), 0) == len(n) - 1: n = n[1:-1].strip()
        if n == e: return e
        e = n

class Ctx:
    def __init__(self, cf, func, defines):
        self.cf, self.func, self.defines = cf, func, defines

def const_of(name, ctx):
    """a #define (this file, then the component's other files) as int, else None"""
    for d in (ctx.cf.defines, ctx.defines):
        v = d.get(name)
        if v is not None:
            v = re.sub(r"/\*.*", "", v).strip().rstrip("uUlL")
            if HEXRE.match(v): return int(v, 16)
            if re.match(r"^\(?\s*0x[0-9A-Fa-f]+[uUlL]*\s*\)?$", v): return int(re.search(r"0x[0-9A-Fa-f]+", v).group(0), 16)
            return const_of(v, ctx) if re.match(r"^\w+$", v) else None
    return None

def str_of(expr, ctx, before):
    """a string argument: literal, #define or a local const char * set before the call"""
    s = unstr(expr)
    if s is not None: return s
    e = expr.strip()
    if not re.match(r"^\w+$", e): return None
    v = ctx.cf.defines.get(e) or ctx.defines.get(e)
    if v and unstr(v) is not None: return unstr(v)
    region = ctx.cf.text[:before]
    mm = list(re.finditer(r"\b%s\s*=\s*(%s)" % (re.escape(e), STR), region))
    return unstr(mm[-1].group(1)) if mm else None

def last_assign(var, ctx, before):
    """the expression last assigned to var in the enclosing function before offset 'before' (or a file static)"""
    lo = ctx.func[2] if ctx.func else 0
    for region, base in ((ctx.cf.text[lo:before], lo), (ctx.cf.text[:lo], 0)):
        rm = mask_strings(region)
        best = None
        for mm in re.finditer(r"(?<![\w.>])\**\s*%s\s*=(?!=)" % re.escape(var), rm):
            st = mm.end(); d = 0; j = st
            while j < len(rm):
                c = rm[j]
                if c in "([{": d += 1
                elif c in ")]}":
                    if d == 0: break
                    d -= 1
                elif c in ",;" and d == 0: break
                j += 1
            best = region[st:j].strip()
        if best is not None: return best
    return None

def resolve_target(expr, ctx, before, depth=0):
    """-> ('rva', int) | ('name', ns, k, m, argc) | ('table', name) | ('export', sym) | None"""
    e = strip_casts(expr)
    if depth > 6: return None
    mm = re.match(r"^(.*?)\s*\?\s*(.+?)\s*:\s*(?:NULL|0|-1)$", e, re.S)
    if mm: e = strip_casts(mm.group(2))
    if HEXRE.match(e): return ("rva", int(e, 16))
    mm = re.match(r"^(?:dsl_ga_base|ds_base|base)\s*\+\s*(\w+)$", e)
    if mm:
        v = int(mm.group(1), 16) if HEXRE.match(mm.group(1)) else const_of(mm.group(1), ctx)
        return ("rva", v) if v is not None else None
    if re.match(r"^\w+$", e):
        v = const_of(e, ctx)
        if v is not None: return ("rva", v)
        a = last_assign(e, ctx, before)
        return resolve_target(a, ctx, before, depth + 1) if a else None
    mm = re.match(r"^\*\s*\(\s*void\s*\*\*\s*\)\s*(\w+)$", e)               # *(void **)MethodInfo = its method pointer
    if mm:
        a = last_assign(mm.group(1), ctx, before)
        r = resolve_mi(a, ctx, before, depth + 1) if a else None
        return r
    mm = re.match(r"^(?:\w+\s*->\s*)?(dsl_find_method_address|find_method_address|meth)\s*\((.*)\)$", e, re.S)
    if mm:
        a = split_args(mm.group(2))
        a = a[1:] if len(a) == 5 else a
        if len(a) == 4:
            ns, k, m = (str_of(x, ctx, before) for x in a[:3])
            if None not in (ns, k, m) and re.match(r"^-?\d+$", a[3].strip()): return ("name", ns, k, m, int(a[3]))
        return None
    mm = re.match(r"^(?:\w+\s*->\s*)?(dsl_method_address|method_address)\s*\((\w+)\)$", e)
    if mm:
        a = last_assign(mm.group(2), ctx, before)
        return resolve_mi(a, ctx, before, depth + 1) if a else None
    mm = re.match(r"^(?:\w+\s*->\s*)?table\s*\(\s*\w+\s*,\s*(%s)\s*\)$" % STR, e)
    if mm: return ("table", unstr(mm.group(1)))
    mm = re.match(r"^(?:\w+\s*->\s*)?(?:dsl_ga_sym|ga_sym|il2cpp_fn)\s*\(\s*(%s)\s*\)$" % STR, e)
    if mm: return ("export", unstr(mm.group(1)))
    return None

def resolve_mi(e, ctx, before, depth):
    """a MethodInfo expression -> ('name', ...)"""
    e = strip_casts(e)
    mm = re.match(r"^(.*?)\s*\?\s*(.+?)\s*:\s*(?:NULL|0)$", e, re.S)
    if mm: e = strip_casts(mm.group(2))
    mm = re.match(r"^(?:\w+\s*->\s*)?(?:dsl_find_method|find_method|OPT|mi_sig)\s*\((.*)\)$", e, re.S)
    if not mm: return None
    a = split_args(mm.group(1))
    if a and a[0] in ("ME", "self"): a = a[1:]
    if len(a) < 3: return None
    m = str_of(a[1], ctx, before)
    if m is None or not re.match(r"^-?\d+$", a[2].strip()): return None
    k = resolve_class(a[0], ctx, before, depth + 1)
    return ("name", k[0], k[1], m, int(a[2])) if k else None

def resolve_class(e, ctx, before, depth):
    e = strip_casts(e)
    if depth > 6: return None
    mm = re.match(r"^(?:\w+\s*->\s*)?(?:dsl_find_class|find_class|klass)\s*\((.*)\)$", e, re.S)
    if mm:
        a = split_args(mm.group(1))
        if a and a[0] in ("ME", "self"): a = a[1:]
        if len(a) == 2:
            ns, k = str_of(a[0], ctx, before), str_of(a[1], ctx, before)
            return (ns, k) if ns is not None and k else None
        return None
    if re.match(r"^\w+$", e):
        a = last_assign(e, ctx, before)
        return resolve_class(a, ctx, before, depth + 1) if a else None
    return None

def table_rows(cf, name, ctx):
    """rows of an array initializer `name[] = { {..}, .. }` in this file (or the enclosing function)"""
    mm = re.search(r"\b%s\s*\[\s*\w*\s*\]\s*=\s*\{" % re.escape(name), cf.m)
    if not mm: return None
    st = mm.end() - 1
    en = cf._close(st)
    rows, i = [], st + 1
    while i < en:
        if cf.m[i] == "{":
            j = cf._close(i)
            rows.append((i, split_args(cf.text[i + 1:j]))); i = j + 1
        else: i += 1
    return rows

def flat_rows(cf, name):
    """a flat array `name[N] = { a, b, c }` -> its elements"""
    mm = re.search(r"\b%s\s*\[[^\]]*\]\s*=\s*\{" % re.escape(name), cf.m)
    if not mm: return None
    st = mm.end() - 1
    return split_args(cf.text[st + 1:cf._close(st)])

def guards(cf, func, off):
    """enclosing if/ternary conditions of a call inside its function (text), innermost last"""
    if not func: return []
    m, lo = cf.m, func[2]
    out, stack, i = [], [], lo
    # statements that open a block: remember the header text before each '{'
    while i < off:
        c = m[i]
        if c == "{":
            head = m[max(lo, i - 400):i]
            k = max(head.rfind(";"), head.rfind("{"), head.rfind("}"))
            stack.append(cf.text[max(lo, i - 400) + k + 1:i].strip())
        elif c == "}" and stack: stack.pop()
        i += 1
    out = [h for h in stack if re.match(r"^(?:else\s+)?(?:if|while|for)\b", h) or h.startswith("else")]
    # early returns before the call in the same or an outer block: `if (C) return ..;` / `if (C) { ..; return; }`
    depth_at = []
    d = 0
    for i in range(lo, off):
        if m[i] == "{": d += 1
        elif m[i] == "}": d -= 1
    call_depth = d
    for mm in re.finditer(r"\bif\s*\(", m[lo:off]):
        st = lo + mm.end() - 1; en = match_paren(m, st)
        if en < 0 or en >= off: continue
        dd = m.count("{", lo, st) - m.count("}", lo, st)
        if dd > call_depth: continue
        rest = m[en + 1:off].lstrip()
        body = None
        if rest.startswith("return"): body = rest[:rest.find(";") + 1]
        elif rest.startswith("{"):
            j = cf._close(en + 1 + (len(m[en + 1:off]) - len(rest)))
            if j < off: body = m[en + 1:j + 1]
        if body is not None and re.search(r"\breturn\b", body) and body.count("{") <= 1:
            out.append("early-return: " + re.sub(r"\s+", " ", cf.text[st:en + 1]))
    # the statement of the call itself
    k = max(m.rfind(";", lo, off), m.rfind("{", lo, off), m.rfind("}", lo, off))
    st = cf.text[k + 1:off].strip()
    if st: out.append(st)
    return [re.sub(r"\s+", " ", g) for g in out]

ENVRE = re.compile(r'getenv\s*\(\s*"(\w+)"\s*\)')

_CF = {}
def cfile(tree, rel):
    """parsed source, once per tree and file (the Mac and Windows passes share it)"""
    k = (id(tree), rel)
    if k not in _CF: _CF[k] = CFile(rel, tree.read(rel))
    return _CF[k]

FUNCBODY = {}                            # (owner, file, function) -> body text (local test switches)

def scan(comps, overrides):
    """-> list of site dicts (one per platform where compiled), list of problems"""
    sites, problems = [], []
    for owner, plat, tree, files, pdefs, space in comps:
        cfs = []
        for rel in files:
            if not tree.has(rel): continue
            cfs.append(cfile(tree, rel))
        alldefs = {}
        for cf in cfs:
            for k, v in cf.defines.items(): alldefs.setdefault(k, v)
        # headers of the tree next to the files (VA_ defines live in .h files)
        for h in sorted({os.path.dirname(f) for f in files}):
            for hf in tree.glob(f"{h}/*.h") if h else tree.glob("*.h"):
                for k, v in cfile(tree, hf).defines.items(): alldefs.setdefault(k, v)
        defs = dict(pdefs)
        prims = dict(PRIMS); rargs = dict(REPL_ARG)
        # wrappers: a function whose hook call takes its target from its own parameter becomes a primitive (fixpoint)
        for _ in range(4):
            added = False
            for cf in cfs:
                for name, params, b0, b1 in cf.funcs:
                    if name in prims or is_impl(cf.rel, name): continue
                    pnames = [re.findall(r"(\w+)\s*(?:\[\s*\])?$", p.strip())[-1] if re.findall(r"(\w+)\s*(?:\[\s*\])?$", p.strip()) else "" for p in split_args(params)]
                    for cm_ in CALL_RE(prims).finditer(cf.m, b0, b1):
                        p = cm_.group(1); o = cf.m.index("(", cm_.end() - 1); c = match_paren(cf.m, o)
                        args = split_args(cf.text[o + 1:c])
                        ai, wi, kind, tk = prims[p]
                        if not isinstance(ai, int) or ai >= len(args): continue
                        tgt = strip_casts(args[ai])
                        if tk == "name":
                            nm = [strip_casts(a) for a in args[ai:ai + 4]]
                            if any(x in pnames for x in nm):
                                prims[name] = ("wrapname", [pnames.index(x) if x in pnames else x for x in nm] + [pnames.index(strip_casts(args[rargs[p]])) if strip_casts(args[rargs[p]]) in pnames else None], kind, "name")
                                added = True
                            continue
                        if tgt in pnames:
                            wv = strip_casts(args[wi]) if wi is not None and wi < len(args) else None
                            prims[name] = (pnames.index(tgt), pnames.index(wv) if wv in pnames else None, kind, tk)
                            ra = strip_casts(args[rargs[p]]) if p in rargs and rargs[p] < len(args) else None
                            rargs[name] = pnames.index(ra) if ra in pnames else None
                            added = True
            if not added: break
        call_re = CALL_RE(prims)
        for cf in cfs:
            for name, params, b0, b1 in cf.funcs: FUNCBODY[(owner, cf.rel, name)] = cf.text[b0:b1]
            ctx_defs = alldefs
            for cm_ in call_re.finditer(cf.m):
                p = cm_.group(1)
                o = cf.m.index("(", cm_.end() - 1)
                pre = cf.m[max(0, cm_.start() - 40):cm_.start()]
                if re.search(r"(?:int|void|static|MH_STATUS)\s*\**\s*$", pre) and re.match(r"\s*\([^)]*\)\s*\{", cf.m[o:o + 400]):
                    continue                                                   # a definition, not a call
                func = cf.func_at(cm_.start())
                if func is None: continue                                      # prototypes, macros
                if is_impl(cf.rel, func[0]) or func[0] == p: continue
                if p in prims and prims[p][0] != "wrapname" and isinstance(prims[p][0], int) and p not in PRIMS:
                    pass
                if not compiled(cf, cm_.start(), defs): continue
                c = match_paren(cf.m, o)
                args = split_args(cf.text[o + 1:c])
                ctx = Ctx(cf, func, ctx_defs)
                spec = prims[p]
                # a wrapper's own body is not a site (its callers are)
                if func[0] in prims and func[0] not in PRIMS: continue
                g = guards(cf, func, cm_.start())
                base = dict(owner=owner, plat=plat, file=cf.rel, func=func[0], line=cf.line(cm_.start()), prim=p,
                            kind=spec[2], guards=g, space=space)
                if spec[0] == "wrapname":
                    idx = spec[1]
                    vals = [args[i] if isinstance(i, int) and i < len(args) else i for i in idx[:4]]
                    ns, k, m = (str_of(v, ctx, cm_.start()) for v in vals[:3])
                    argc = vals[3].strip() if isinstance(vals[3], str) else None
                    repl = strip_casts(args[idx[4]]) if idx[4] is not None and idx[4] < len(args) else None
                    t = ("name", ns, k, m, int(argc)) if None not in (ns, k, m) and argc and re.match(r"^-?\d+$", argc) else None
                    sites.append(dict(base, repl=repl, who=None, target=t))
                    continue
                ai, wi, kind, tk = spec
                ra = rargs.get(p)
                repl = strip_casts(args[ra]) if ra is not None and ra < len(args) else None
                who = str_of(args[wi], ctx, cm_.start()) if wi is not None and wi < len(args) else None
                whoexpr = strip_casts(args[wi]) if wi is not None and wi < len(args) else None
                if tk == "name":
                    a = args[ai:ai + 4]
                    ns, k, m = (str_of(x, ctx, cm_.start()) for x in a[:3])
                    t = ("name", ns, k, m, int(a[3])) if None not in (ns, k, m) and re.match(r"^-?\d+$", a[3].strip()) else None
                    sites.append(dict(base, repl=strip_casts(args[REPL_ARG["hook_method"]]), who=who, target=t)); continue
                texpr = strip_casts(args[ai]) if ai < len(args) else ""
                tm = re.match(r"^(\w+)\s*\[\s*(\w+)\s*\]\s*(?:\.\s*(\w+))?$", texpr)
                if tm:                                                         # hook table: one site per row
                    rows = table_rows(cf, tm.group(1), ctx)
                    if rows is None and tm.group(3) is None:
                        rows = None
                    if rows is not None:
                        names = table_fields(cf, tm.group(1))
                        for roff, row in rows:
                            if not compiled(cf, roff, defs): continue
                            fld = dict(zip(names, row)) if names else {}
                            tv = fld.get(tm.group(3)) if tm.group(3) else None
                            t = resolve_target(tv, ctx, roff) if tv else None
                            wm = re.match(r"^(\w+)\s*\[\s*\w+\s*\]\s*\.\s*(\w+)$", whoexpr or "")
                            w = unstr(fld.get(wm.group(2), "")) if wm else who
                            rm_ = re.match(r"^(\w+)\s*\[\s*\w+\s*\]\s*\.\s*(\w+)$", repl or "")
                            r = strip_casts(fld.get(rm_.group(2), "")) if rm_ else (repl or "")
                            sites.append(dict(base, line=cf.line(roff), repl=r, who=w, target=t, table=tm.group(1)))
                        # parallel flat arrays (repl[i]) are matched by row order
                        rr = re.match(r"^(\w+)\s*\[\s*(\w+)\s*\]$", repl or "")
                        if rr:
                            flat = flat_rows(cf, rr.group(1)) or []
                            n = len([1 for roff, _ in rows if compiled(cf, roff, defs)])
                            for i, s in enumerate(sites[-n:]): s["repl"] = flat[i] if i < len(flat) else repl
                        continue
                    flat_t = flat_rows(cf, tm.group(1))
                    rr = re.match(r"^(\w+)\s*\[\s*(\w+)\s*\]$", repl or "")
                    flat_r = flat_rows(cf, rr.group(1)) if rr else None
                    if flat_r is not None and flat_t is None:                 # a loop over runtime-found targets: one site
                        sites.append(dict(base, repl=f"{rr.group(1)}[]", who=who, target=None)); continue
                t = resolve_target(texpr, ctx, cm_.start())
                if tk == "rva" and t and t[0] == "rva": pass
                sites.append(dict(base, repl=repl, who=who, target=t))
    # module patch tables: hell-difficulty patch_rva in a loop over rva/<build>_<plat>_patches.tsv
    out = []
    for s in sites:
        if s["prim"] == "patch_rva" and s["target"] is None:
            for plat in (s["plat"],):
                rows = module_patches(s["owner"], plat)
                for rva, old, new, note in rows:
                    out.append(dict(s, target=("rva", rva), who=f"{s['owner']} {note}", repl=f"patch {note}", patch_old=old, patch_new=new,
                                    space=plat, line=None, from_table=f"modules/{s['owner']}/rva/{BUILD}_{plat}_patches.tsv"))
            continue
        out.append(s)
    # unresolved -> sites.tsv
    for s in out:
        ov = override_of(overrides, s)
        if ov: s["override"] = ov
        if s["target"] is None and ov and ov["target"]: s["target"] = ov["target"]
        if s["target"] is None:
            problems.append(f"{s['file']}:{s['line']} ({s['owner']}, {s['plat']}): hook target not followed; add a row to tools/hooks/sites.tsv: "
                            f"{s['owner']}\t{os.path.basename(s['file'])}\t{s['repl']}\t<Namespace.Class$$Method/argc>\tactive: <when>")
    return out, problems

def table_fields(cf, name):
    """field names of the struct of `struct {..} name[]` (anonymous struct in the declaration)"""
    mm = re.search(r"struct\s*\{([^{}]*)\}\s*%s\s*\[" % re.escape(name), cf.m)
    if not mm: return None
    return [re.findall(r"(\w+)\s*$", f.strip())[0] for f in cf.text[mm.start(1):mm.end(1)].split(";") if f.strip()]

def CALL_RE(prims):
    names = sorted(prims, key=len, reverse=True)
    return re.compile(r"(?:(?:\b(?:host|H|DH|h)\s*->\s*)|(?<![\w>.]))(%s)\s*\(" % "|".join(map(re.escape, names)))

def module_patches(mid, plat):
    p = os.path.join(LOADER, "modules", mid, "rva", f"{BUILD}_{plat}_patches.tsv")
    rows = []
    if os.path.exists(p):
        for ln in open(p):
            f = ln.rstrip("\n").split("\t")
            if f[0] == "patch" and len(f) >= 5: rows.append((int(f[1], 16), f[2], f[3], f[4]))
    return rows

def module_table(mid, plat):
    p = os.path.join(LOADER, "modules", mid, "rva", f"{BUILD}_{plat}.tsv")
    t = {}
    if os.path.exists(p):
        for ln in open(p):
            if ln.startswith("#"): continue
            f = ln.rstrip("\n").split("\t")
            if len(f) >= 2 and HEXRE.match(f[1].strip()): t[f[0]] = int(f[1], 16)
    return t

def load_overrides():
    ov = {}
    p = os.path.join(HERE, "sites.tsv")
    for ln in open(p) if os.path.exists(p) else []:
        if ln.startswith("#") or not ln.strip(): continue
        f = ln.rstrip("\n").split("\t")
        owner, fn, repl, tgt = f[0], f[1], f[2], f[3]
        note = f[4] if len(f) > 4 else ""
        if tgt == "-": t = None
        elif tgt.startswith("export:"): t = ("export", tgt[7:])
        elif "/" in tgt:                         # Name/argc, or Name/argc(Type) for one overload: Type = a parameter class
            nm, argc = tgt.rsplit("/", 1); ptype = None
            if argc.endswith(")") and "(" in argc: argc, ptype = argc[:-1].split("(", 1)
            t = ("full", nm, int(argc) if argc != "*" else None, ptype)
        else: t = ("full", tgt, None)
        ov[(owner, fn, repl)] = dict(target=t, note=note)
    return ov

def override_of(overrides, s):
    b = os.path.basename(s["file"])
    return overrides.get((s["owner"], b, s.get("repl") or "")) or overrides.get((s["owner"], b, "*"))

# ------------------------------------------------------------------------------------------------ dumps
class Dump:
    def __init__(self, d):
        sj = json.load(open(os.path.join(d, "script.json")))
        self.by_addr = collections.defaultdict(list)
        self.by_name = collections.defaultdict(list)
        for m in sj["ScriptMethod"]:
            self.by_addr[m["Address"]].append(m)
            self.by_name[m["Name"]].append(m)
        self.starts = sorted(set(sj.get("Addresses", [])) | set(self.by_addr))
    @staticmethod
    def argc(m):
        sig = m["Signature"]; inner = sig[sig.index("(") + 1:sig.rindex(")")]
        ps = [p.strip() for p in inner.split(",") if p.strip()]
        return len([p for p in ps if not p.endswith("__this") and not p.endswith("* method")])
    def at(self, rva):
        return self.by_addr.get(rva, [])
    def containing(self, rva):
        i = bisect.bisect_right(self.starts, rva) - 1
        if i < 0: return None, 0
        st = self.starts[i]
        ms = self.by_addr.get(st)
        return (ms[0]["Name"] if ms else None), rva - st
    def find(self, ns, k, m, argc):
        full = f"{ns}.{k}$${m}" if ns else f"{k}$${m}"
        c = [x for x in self.by_name.get(full, []) if argc is None or argc < 0 or self.argc(x) == argc]
        return c
    def glob(self, pat, argc):
        return [x for n, ms in self.by_name.items() if fnmatch.fnmatchcase(n, pat) for x in ms if argc is None or self.argc(x) == argc]

def win_map(mp):
    t = {}
    for ln in mp.read("win/win_map.tsv").split("\n"):
        f = ln.split("\t")
        if len(f) >= 4 and f[0] in ("method", "metadata", "export", "internal", "code-inside") and f[3]:
            t[int(f[2], 16)] = (int(f[3], 16), f[0], f[1])
    return t

def exports_mac(path):
    """exported symbol -> rva of GameAssembly.dylib (nm)"""
    if not path or not os.path.exists(path): return {}
    out = subprocess.run(["nm", "-gU", path], capture_output=True, text=True).stdout
    return {f[2].lstrip("_"): int(f[0], 16) for f in (l.split() for l in out.splitlines()) if len(f) == 3}

def exports_win(path):
    """exported symbol -> rva of GameAssembly.dll (PE export directory)"""
    import struct
    if not path or not os.path.exists(path): return {}
    b = open(path, "rb").read()
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    nsec = struct.unpack_from("<H", b, pe + 6)[0]; optsz = struct.unpack_from("<H", b, pe + 20)[0]
    opt = pe + 24
    exp_rva = struct.unpack_from("<I", b, opt + (112 if struct.unpack_from("<H", b, opt)[0] == 0x20B else 96))[0]
    secs = [struct.unpack_from("<8sIIII", b, opt + optsz + 40 * i) for i in range(nsec)]
    def off(rva):
        for _, vs, va, rs, ro in secs:
            if va <= rva < va + max(vs, rs): return rva - va + ro
        return None
    e = off(exp_rva)
    nfun, nnames, afun, anames, aords = struct.unpack_from("<IIIII", b, e + 20)
    res = {}
    for i in range(nnames):
        nm_off = off(struct.unpack_from("<I", b, off(anames) + 4 * i)[0])
        name = b[nm_off:b.index(b"\0", nm_off)].decode()
        ordi = struct.unpack_from("<H", b, off(aords) + 2 * i)[0]
        res[name] = struct.unpack_from("<I", b, off(afun) + 4 * ordi)[0]
    return res

# ------------------------------------------------------------------------------------------------ records
def key_of(s):
    """stable identity of a hook site (no line numbers): owner | file | replacement | label"""
    lab = s.get("who") or ""
    if s["kind"] == "patch": lab = "%#x" % s["target"][1] if s["target"] and s["target"][0] == "rva" else lab
    return "|".join((s["owner"], os.path.basename(s["file"]), s.get("repl") or "", lab))

def active_of(s, files):
    """when the hook is installed: sites.tsv note, else getenv() in the conditions around the call (tests), else always"""
    ov = s.get("override") or {}
    if ov.get("note", "").startswith("active:"): return ov["note"][7:].split(" | note: ")[0].strip()
    envs = set()
    for g in s["guards"]:
        if g.startswith("early-return: "):                           # if (!getenv("X")) return; -> needs X
            envs |= set(re.findall(r'!\s*getenv\s*\(\s*"(\w+)"\s*\)', g)); continue
        g = re.sub(r"rig_hook\s*\(.*", "", g)                       # the condition part only
        envs |= {e for e in re.findall(r'(?<!!)\bgetenv\s*\(\s*"(\w+)"\s*\)', g)}
        for fn in re.findall(r"(?<![!\w])(\w+)\s*\(\s*\)", g):    # a local test switch: static int test_mode(void) { return getenv(..) }
            body = files.get((s["owner"], s["file"], fn))
            if body: envs |= set(ENVRE.findall(body))
    if envs: return "env " + ", ".join(sorted(envs)) + " (test copies / debugging only)"
    return "always (module loaded)" if s["owner"] in ("multiplayer", "custom-modes") else "always"

def build_records(sites, dumps, wmap, mp_sha, cm_sha, files=None, exps=None):
    recs = {}
    notes = []
    for s0 in sites:
        k = key_of(s0)
        t0 = s0["target"]
        if t0[0] == "full" and any(c in t0[1] for c in "*?["):        # one site, several methods found at run time
            subs = [(dict(s0, target=("full", x["Name"], None)), x["Name"]) for x in dumps[s0["plat"]].glob(t0[1], t0[2])]
            if not subs: notes.append(f"{k} ({s0['plat']}): {t0[1]} not in the dump")
        else: subs = [(s0, "")]
        for s, sub in subs:
            r = recs.get(k + "|" + sub) if sub else recs.get(k)
            if not r:
                r = recs[k + "|" + sub if sub else k] = dict(key=k, owner=s["owner"], kind=s["kind"], label=label_of(s), source=src_name(s),
                                   active=active_of(s, files or {}), platforms={}, method=None, names=[])
                if s.get("patch_old"): r["patch"] = {}
                nt = ((s.get("override") or {}).get("note") or "").partition(" | note: ")[2]
                if nt: r["note"] = nt
            plat = s["plat"]; t = s["target"]
            ent = {}
            if t[0] == "rva":
                rva = t[1]
                if s["space"] == "mac" and plat == "win":
                    w = wmap.get(rva)
                    if w: ent["rva"] = "%#x" % w[0]
                    else: ent["rva"] = None; notes.append(f"{k}: Mac {rva:#x} has no Windows twin in win_map.tsv")
                else: ent["rva"] = "%#x" % rva
            elif t[0] == "name":
                _, ns, kk, m, argc = t
                ms = dumps[plat].find(ns, kk, m, argc)
                if len(ms) != 1:
                    notes.append(f"{k} ({plat}): {ns}.{kk}$${m}/{argc}: {len(ms)} match(es) in the dump")
                ent["rva"] = "%#x" % ms[0]["Address"] if ms else None
                if ms: ent["name"] = ms[0]["Name"]
            elif t[0] == "full":
                ms = dumps[plat].glob(t[1], t[2]) if any(c in t[1] for c in "*?[") else [x for x in dumps[plat].by_name.get(t[1], []) if t[2] is None or Dump.argc(x) == t[2]]
                if len(t) > 3 and t[3]: ms = [x for x in ms if re.search(r"[_ (]" + re.escape(t[3]) + r"_o\* ", x["Signature"])]
                if not ms: notes.append(f"{k} ({plat}): {t[1]} not in the dump")
                ent["rvas"] = ["%#x" % x["Address"] for x in ms] if len(ms) > 1 else None
                ent["rva"] = "%#x" % ms[0]["Address"] if len(ms) == 1 else None
                ent["names"] = [x["Name"] for x in ms]
            elif t[0] == "table":
                tab = module_table(s["owner"], plat)
                v = tab.get(t[1])
                ent["rva"] = "%#x" % v if v is not None else None
                if v is None: notes.append(f"{k} ({plat}): table name {t[1]} missing in modules/{s['owner']}/rva/{BUILD}_{plat}.tsv")
            elif t[0] == "export":
                v = (exps or {}).get(plat, {}).get(t[1])
                ent["rva"] = "%#x" % v if v is not None else None; ent["export"] = t[1]
                if v is None: notes.append(f"{k} ({plat}): export {t[1]} not found (no game binary given)")
            # what is at that address: every method whose code starts there (identical-code folding / shared generics)
            if ent.get("rva") and not ent.get("export"):
                ms = dumps[plat].at(int(ent["rva"], 16))
                if ms: ent["names"] = [x["Name"] for x in ms]
                else:
                    n, off = dumps[plat].containing(int(ent["rva"], 16))
                    ent["inside"] = f"{n}+{off:#x}" if n else "an address the dump does not name"
            ent.pop("name", None)
            if s.get("patch_old"): r["patch"][PLAT_LABEL[plat]] = {"expect": s["patch_old"], "write": s["patch_new"]}
            r["platforms"][PLAT_LABEL[plat]] = ent
    for r in recs.values():
        allnames = []
        for ent in r["platforms"].values():
            for n in ent.get("names") or []:
                if n not in allnames: allnames.append(n)
        r["method"] = pick_method(r["label"], allnames, r["platforms"])
        if r["method"] is None:
            ins = [e["inside"] for e in r["platforms"].values() if e.get("inside")]
            exp = [e["export"] for e in r["platforms"].values() if e.get("export")]
            r["method"] = f"export {exp[0]}" if exp else (f"inside {ins[0]}" if ins else None)
        for ent in r["platforms"].values():
            ns = ent.pop("names", None) or []
            if len(ns) > 1 and r["method"] in ns: ent["shared_with"] = [n for n in ns if n != r["method"]]
            elif len(ns) > 1 or (ns and ns[0] != r["method"]): ent["methods"] = ns
        r.pop("names", None)
    order = ("owner", "kind", "method", "platforms", "active", "label", "source", "note", "patch", "key")
    recs = {k: {f: r[f] for f in order if f in r} for k, r in recs.items()}
    return sorted(recs.values(), key=lambda r: (OWN_ORDER.get(r["owner"], 9), r["owner"], r["kind"], r["source"], r["label"])), notes

def label_of(s):
    """the name the loader log prints for this hook: '<label>: hooked <rva> for <owner>' (dsl_main.c host API)"""
    p, who, o = s["prim"], s.get("who"), s["owner"]
    if s["kind"] == "patch" and s.get("patch_old"): return who                 # already '<owner> <note>'
    if p == "dsl_hook" or (o == "loader" and who): return who or o
    t = s["target"]
    if t and t[0] == "name" and p not in ("rig_hook", "hook_rva", "hook"): return f"{o} {t[2]}.{t[3]}"
    return f"{o} {who}" if who else o

def short(n):
    """Namespace.Class$$Method -> Class.Method (nested: the innermost class)"""
    k, _, m = n.partition("$$")
    return k.split(".")[-1] + "." + m

def pick_method(label, names, plats):
    """the hooked method's name: one name, or the one the source's label names (shared code: several names)"""
    if not names: return None
    if len(names) == 1: return names[0]
    lab = re.sub(r"[^\w.<>]", " ", label or "")
    hits = [n for n in names if short(n).split("(")[0] in lab or short(n).replace(".", "..", 1) in lab]
    hits = hits or [n for n in names if n.split("$$")[1] in lab.split(".")[-1].split()]
    mac = (plats.get("mac-arm64") or {}).get("names") or []
    if len(hits) == 1: return hits[0]
    if len(mac) == 1: return mac[0]
    return None

OWN_ORDER = {"loader": 0, "multiplayer": 1, "custom-modes": 2, "gcd-cdr": 3, "hell-difficulty": 4, "zetu-suite": 5}

def src_name(s):
    f = s["file"]
    if s.get("from_table"): return "ds_modloader:loader/" + s["from_table"]
    if s["owner"] in ("loader", "gcd-cdr", "hell-difficulty", "zetu-suite") or f.startswith("modules/"): return "ds_modloader:loader/" + f
    return ("ds_multiplayer:" if s["owner"] == "multiplayer" else "ds_custommodes:") + f

# ------------------------------------------------------------------------------------------------ main
def trees(a):
    mp_repo = os.environ.get("MP_REPO", os.path.join(HOME, "ds_multiplayer"))
    cm_repo = os.environ.get("CM_REPO", os.path.join(HOME, "ds_custommodes"))
    if a.mp: mp, mps = Tree(root=a.mp), "tree"
    else: mp, mps = pinned(mp_repo, "mp_pin.txt")
    if a.cm: cm, cms = Tree(root=a.cm), "tree"
    else: cm, cms = pinned(cm_repo, "cm_pin.txt")
    return mp, cm, mps, cms

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", choices=("gen", "check", "xlog", "sites"))
    ap.add_argument("logs", nargs="*")
    ap.add_argument("--mp"); ap.add_argument("--cm")
    ap.add_argument("--mac-dump", default=os.environ.get("DS_MAC_DUMP", os.path.join(HOME, "ds_skill_truth", f"dump_{BUILD}")))
    ap.add_argument("--win-dump", default=None)
    ap.add_argument("--mac-bin", help="GameAssembly.dylib of the build (export addresses; default: the Steam copy)")
    ap.add_argument("--win-bin", help="GameAssembly.dll of the build (default: next to the Windows dump)")
    ap.add_argument("--out", default=os.path.join(REPO, "docs"))
    ap.add_argument("--list", default=os.path.join(REPO, "docs", "hooks.json"))
    a = ap.parse_args()
    mp, cm, mps, cms = trees(a)
    sites, problems = scan(components(mp, cm), load_overrides())
    if a.cmd == "sites":
        for s in sites: print(s["owner"], s["plat"], f"{s['file']}:{s['line']}", s["prim"], s.get("repl"), s.get("who"), s["target"], "|", "; ".join(s["guards"])[:160])
        for p in problems: print("PROBLEM", p)
        return 0
    if problems:
        for p in problems: print("hooklist:", p, file=sys.stderr)
        return 1
    if a.cmd == "check":
        have = json.load(open(a.list))
        listed = {h["key"] for h in have["hooks"]}
        missing = sorted({key_of(s) for s in sites} - listed)
        for k in missing: print(f"hooklist check: the sources hook {k!r}, docs/hooks.json does not list it (run tools/hooks/hooklist.py gen)", file=sys.stderr)
        if have.get("mp_pin") and mps != "tree" and have["mp_pin"] != mp.sha: print(f"hooklist check: docs/hooks.json is for ds_multiplayer {have['mp_pin'][:7]}, the pin is {mp.sha[:7]}", file=sys.stderr); missing.append("pin")
        if have.get("cm_pin") and cms != "tree" and have["cm_pin"] != cm.sha: print(f"hooklist check: docs/hooks.json is for ds_custommodes {have['cm_pin'][:7]}, the pin is {cm.sha[:7]}", file=sys.stderr); missing.append("pin")
        extra = sorted(listed - {key_of(s) for s in sites})
        for k in extra: print(f"hooklist check: warning: docs/hooks.json lists {k!r}, the sources no longer hook it (run tools/hooks/hooklist.py gen)", file=sys.stderr)
        if not missing: print(f"hooklist check: ok ({len({key_of(s) for s in sites})} hook sites, all listed)")
        return 1 if missing else 0
    win_dump = a.win_dump or os.environ.get("DS_WIN_DUMP") or os.path.join(os.environ.get("MP_REPO", os.path.join(HOME, "ds_multiplayer")), "win", BUILD, "dump")
    dumps = {"mac": Dump(a.mac_dump), "win": Dump(win_dump)}
    steam = os.path.join(HOME, "Library", "Application Support", "Steam", "steamapps", "common", "Dungeon Settlers")
    mac_bin = a.mac_bin or os.path.join(steam, "DungeonSettlers.app", "Contents", "Frameworks", "GameAssembly.dylib")
    win_bin = a.win_bin or os.path.join(os.path.dirname(win_dump), "GameAssembly.dll")
    exps = {"mac": exports_mac(mac_bin), "win": exports_win(win_bin)}
    recs, notes = build_records(sites, dumps, win_map(mp), getattr(mp, "sha", None), getattr(cm, "sha", None), FUNCBODY, exps)
    if a.cmd == "xlog":
        return xlog(recs, a.logs)
    os.makedirs(a.out, exist_ok=True)
    doc = {"format": 1, "about": "Every native hook (detour) and in-place code patch that the Dungeon Settlers mod loader and its official mods put "
                                 "into GameAssembly. Generated from the sources by tools/hooks/hooklist.py; see the README section 'Hook list'.",
           "game_build": BUILD, "game_version": GAME_VERSION, "platforms": ["mac-arm64", "win-x64"],
           "mp_pin": getattr(mp, "sha", None), "cm_pin": getattr(cm, "sha", None),
           "fields": {"owner": "loader or the mod id",
                      "kind": "detour (the first instructions jump to our code; the original stays callable) or patch (instruction bytes changed in place)",
                      "method": "Il2Cpp name of the hooked function as Il2CppDumper's script.json writes it (Namespace.Class$$Method); "
                                "'inside X+0xN' = an address inside that function; 'export X' = a GameAssembly export",
                      "platforms": "per platform: rva = offset in GameAssembly.dylib (mac-arm64) / GameAssembly.dll (win-x64); "
                                   "shared_with = other methods whose code starts at the same address (the hook sees their calls too); "
                                   "methods = every method at that address when the label names none of them; inside = the function around the address",
                      "active": "when the hook is installed: always = at game start whenever its owner is on; env X = only when that environment variable "
                                "is set (test copies, debugging); other text = the condition in the source",
                      "label": "the name the loader log prints: '<label>: hooked <rva> for <owner>' (patches: 'patched')",
                      "source": "repo:file of the hook call (multiplayer, custom-modes: at the pinned commit mp_pin / cm_pin)",
                      "patch": "patch only: per platform the bytes expected (hex) and the bytes written; the loader refuses a patch when the bytes differ",
                      "key": "stable id of the hook site, used by the release gate (tools/hooks/hooklist.py check)"},
           "hooks": recs, "notes": notes}
    with open(os.path.join(a.out, "hooks.json.tmp"), "w") as f: json.dump(doc, f, indent=1, ensure_ascii=False); f.write("\n")
    os.replace(os.path.join(a.out, "hooks.json.tmp"), os.path.join(a.out, "hooks.json"))
    with open(os.path.join(a.out, "hooks.tsv.tmp"), "w") as f:
        f.write(f"# Dungeon Settlers mod loader and official mods: native hooks and code patches in GameAssembly, game build {BUILD} ({GAME_VERSION}). "
                f"Generated by tools/hooks/hooklist.py; hooks.json has the details (shared code, patch bytes)\n")
        f.write("owner\tkind\tmethod\tmac_arm64_rva\twin_x64_rva\tactive\tlabel\tsource\n")
        for r in recs:
            m, w = r["platforms"].get("mac-arm64", {}), r["platforms"].get("win-x64", {})
            meth = r["method"] or ";".join(sorted({n for e in r["platforms"].values() for n in e.get("methods", [])}))
            f.write("\t".join([r["owner"], r["kind"], meth, rv(m), rv(w), r["active"], r["label"], r["source"]]) + "\n")
    os.replace(os.path.join(a.out, "hooks.tsv.tmp"), os.path.join(a.out, "hooks.tsv"))
    c = collections.Counter(r["owner"] for r in recs)
    print(f"hooklist: {len(recs)} hooks ({', '.join(f'{k} {v}' for k, v in sorted(c.items(), key=lambda x: OWN_ORDER.get(x[0], 9)))}); {len(notes)} note(s) -> {a.out}/hooks.json, hooks.tsv")
    for n in notes: print("  note:", n)
    return 0

def rv(e):
    if not e: return "-"
    if e.get("rva"): return e["rva"]
    if e.get("rvas"): return ";".join(e["rvas"])
    if e.get("export"): return "export " + e["export"]
    return "?"

def xlog(recs, logs):
    """compare the 'hooked'/'patched' lines of loader logs (one Mac run) with the Mac column of the list"""
    seen = collections.Counter()
    for p in logs:
        for ln in open(p, errors="replace"):
            m = re.search(r"\s(.+?): (?:hooked|patched) (0x[0-9a-f]+)(?: \(\d+ bytes\))? for (.+)$", ln.rstrip())
            if m: seen[(m.group(1), int(m.group(2), 16))] += 1
    listed = {}
    for r in recs:
        e = r["platforms"].get("mac-arm64")
        if not e: continue
        for v in ([e["rva"]] if e.get("rva") else []) + (e.get("rvas") or []): listed.setdefault(int(v, 16), []).append(r)
    bad = 0
    for (who, a), n in sorted(seen.items(), key=lambda x: x[0][1]):
        if a not in listed: print(f"LOG ONLY  {a:#x} {who}"); bad += 1
        elif who not in {r["label"] for r in listed[a]}: print(f"LABEL     {a:#x} log {who!r}, list {listed[a][0]['label']!r}"); bad += 1
    logged = {a for _, a in seen}
    for a, rs in sorted(listed.items()):
        if a not in logged: print(f"NOT IN LOG {a:#x} {rs[0]['owner']} {rs[0]['label']} [{rs[0]['active']}]")
    print(f"xlog: {len(seen)} logged sites, {len(listed)} listed Mac addresses, {bad} logged address(es) the list lacks")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
