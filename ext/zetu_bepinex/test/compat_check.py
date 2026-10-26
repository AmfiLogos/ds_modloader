#!/usr/bin/env python3
"""Does the plugin still find its game code on other game builds? Checked against Il2CppDumper dumps, no game start.

  python3 test/compat_check.py build/plugin/ZetuSuite.dll <dump.cs> [<dump.cs> ...]

Per dump (one game build), two checks; any FAIL = a part of the mod is OFF (or a method fails at JIT) on that build:
  1. patch targets: every Plugin.Patch(...) in src/ finds exactly one method of that name with one of its parameter counts,
     and the patch methods' parameters (other than __instance / __result ...) are parameters of that method (Harmony binds
     them by name). Plugin.IterType: the type has a "<Method>d__N" iterator with MoveNext.
  2. references: every Assembly-CSharp type and member the DLL uses (its IL, by ilspycmd) exists on that build: a
     missing one makes the method that names it fail at JIT, before its own try/catch runs.
Why: ZET-U 0.1.2 asked for DungeonResultView.SetPlayerData with 4 parameters; a player's older build has 3, so the whole
max-units part was OFF (player log 2026-10-26). Builds 25096615..25846389 dumps: ~/ds_skill_truth/dump_*/dump.cs.
Exit 0 = every dump passes."""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get("ZETU_SRC") or os.path.join(HERE, "..", "src")      # ZETU_SRC: another source tree (test of this check)
ILSPY = os.path.expanduser("~/.dotnet/tools/ilspycmd")


def split_top(s):
    """Split on commas outside <>, (), []."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "<([": depth += 1
        elif ch in ">)]": depth -= 1
        if ch == "," and depth == 0: out.append(cur); cur = ""
        else: cur += ch
    if cur.strip(): out.append(cur)
    return [p.strip() for p in out]


def ident(n):                                   # Il2CppInterop names: '<X>k__BackingField' -> '_X_k__BackingField'
    return re.sub(r"[^A-Za-z0-9_]", "_", n)


def load_dump(path):
    """{full name: {'short', 'base', 'fields', 'methods': {name: [[param names], ...]}}}"""
    types, ns, cur = {}, "", None
    decl = re.compile(r"^(?:[\w ]*\s)?(?:class|struct|enum|interface)\s+([^\s:]+)(?:\s*:\s*([^/{]+?))?\s*// TypeDefIndex")
    meth = re.compile(r"^\t(?:\[[^\]]*\]\s*)*[\w ]*?\s?[\w<>\[\],. ?]+?\s([\w.<>|]+)\((.*)\)\s*\{ \}")
    field = re.compile(r"^\t(?:[\w ]+\s)?[\w<>\[\],. ?]+\s([^\s;]+);")
    prop = re.compile(r"^\t[\w ]*?[\w<>\[\],. ?]+\s([\w.<>]+)\s*\{ (?:get|set)")
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("// Namespace:"):
                ns = line[13:].strip(); continue
            m = decl.match(line)
            if m:
                name = m.group(1).split("<")[0] if not m.group(1).startswith("<") and "." not in m.group(1) else m.group(1)
                full = (ns + "." if ns else "") + name
                base = (m.group(2) or "").split(",")[0].strip().split("<")[0]
                cur = types.setdefault(full, {"short": name, "base": base, "fields": set(), "methods": {}})
                continue
            if cur is None or not line.startswith("\t") or line.startswith("\t//"): continue
            m = meth.match(line)
            if m:
                params = [p.split()[-1] for p in split_top(m.group(2)) if p] if m.group(2).strip() else []
                params = [p.split("=")[0].strip() for p in params]
                cur["methods"].setdefault(m.group(1), []).append(params); continue
            m = prop.match(line) or field.match(line)
            if m: cur["fields"].add(ident(m.group(1)))
    by_short = {}
    for full, t in types.items(): by_short.setdefault(t["short"], []).append(full)
    # nested types are dumped as "Outer.Inner" with an empty namespace (often before Outer): key them "<Outer's ns>.Outer.Inner"
    for full in [f for f, t in types.items() if t["short"] == f and "." in f]:
        outer = full.split(".")[0]
        hits = sorted((f for f in by_short.get(outer, []) if f != full), key=lambda f: (not f.startswith("Refactor"), len(f)))
        if hits and hits[0] != outer:
            t = types.pop(full); new = hits[0] + full[len(outer):]
            types[new] = t; by_short[t["short"]] = [new if f == full else f for f in by_short[t["short"]]]
    return types, by_short


def find_type(types, by_short, name):
    """name: 'Refactor.SystemVar/Campaign' (IL) or 'SystemVar.Campaign' / 'DungeonResultView' (C# source)."""
    name = name.replace("/", ".")
    if name in types: return name
    hits = [f for f in by_short.get(name, [])] or [f for f in types if f.endswith("." + name)]
    hits = sorted(hits, key=lambda f: (not f.startswith("Refactor"), len(f)))
    return hits[0] if hits else None


def members(types, full, kind, name):            # kind 'fields' / 'methods', walking the base types
    seen = 0
    while full and seen < 20:
        t = types[full]
        if kind == "fields" and name in t["fields"]: return True
        if kind == "methods" and name in t["methods"]: return t["methods"][name]
        full = find_type(types, {}, t["base"]) if t["base"] else None
        seen += 1
    return None


def patch_targets():
    src = {f: open(os.path.join(SRC, f), encoding="utf-8").read() for f in os.listdir(SRC) if f.endswith(".cs")}
    allsrc = "\n".join(src.values())
    consts = dict(re.findall(r'const string (\w+) = "(\w+)"', allsrc))
    sig = {m.group(1): m.group(2) for m in re.finditer(r"static \w+ (\w+)\(([^)]*)\)", allsrc, re.S)}
    out = []
    for m in re.finditer(r"Plugin\.Patch\(h,\s*[^,]+,\s*(typeof\(([\w.]+)\)|\w+),\s*(?:\"(\w+)\"|nameof\(([\w.]+)\)),\s*(\d+)([^;]*?)\)\s*(?:&&|;|\))", allsrc):
        rest = m.group(6)
        counts = {int(m.group(5))} | {int(x) for x in re.findall(r"orArgc:\s*(\d+)", rest)}
        hooks = re.findall(r"(?:prefix|postfix):\s*nameof\((\w+)\)", rest)
        binds = set()
        for hk in hooks:
            for p in split_top(sig.get(hk, "")):
                n = p.split()[-1] if p else ""
                if n and not n.startswith("__"): binds.add(n)
        if m.group(2):
            out.append(("method", m.group(2), m.group(3) or m.group(4).split(".")[-1], counts, binds))
    for m in re.finditer(r"Plugin\.IterType\(typeof\(([\w.]+)\),\s*(\w+)\)", allsrc):
        out.append(("iter", m.group(1), consts.get(m.group(2), m.group(2)), None, set()))
    return out


def il_refs(dll):
    il = subprocess.run([ILSPY, "-il", dll], capture_output=True, text=True,
                        env={**os.environ, "DOTNET_ROOT": os.environ.get("DOTNET_ROOT", os.path.expanduser("~/.dotnet"))}).stdout
    if len(il) < 1000: sys.exit("compat: FAIL, ilspycmd gave no IL (DOTNET_ROOT?)")
    tys, mems = set(), set()
    for m in re.finditer(r"\['Assembly-CSharp'\]([\w./`]+)(?:::([\w.<>]+)\(([^()]*(?:\([^()]*\))*[^()]*)\))?", il):
        t = re.sub(r"`\d+$", "", m.group(1))
        tys.add(t)
        if m.group(2): mems.add((t, m.group(2), len(split_top(m.group(3))) if m.group(3).strip() else 0))
    return tys, mems


def check(dump, targets, tys, mems):
    types, by_short = load_dump(dump)
    fails, n = [], 0
    for kind, tname, meth, counts, binds in targets:
        n += 1
        full = find_type(types, by_short, tname)
        if not full: fails.append(f"patch {tname}: type missing"); continue
        if kind == "iter":
            its = [f for f in types if f.startswith(full + ".<" + meth + ">d__")]
            if len(its) != 1 or "MoveNext" not in types[its[0]]["methods"]:
                fails.append(f"patch {tname} iterator <{meth}>d__N: {len(its)} found")
            continue
        hits = [p for p in (types[full]["methods"].get(meth) or []) if len(p) in counts]
        if len(hits) != 1:
            have = sorted(len(p) for p in types[full]["methods"].get(meth) or [])
            fails.append(f"patch {tname}.{meth} ({' or '.join(map(str, sorted(counts)))} args): {len(hits)} found, this build has {have}")
        elif binds - set(hits[0]):
            fails.append(f"patch {tname}.{meth}: hook parameter(s) {sorted(binds - set(hits[0]))} not in {hits[0]}")
    for t in sorted(tys):
        n += 1
        if not find_type(types, by_short, t): fails.append(f"type {t}: missing")
    for t, name, argc in sorted(mems):
        full = find_type(types, by_short, t)
        if not full or name == ".ctor": continue
        n += 1
        if name.startswith(("get_", "set_")) and members(types, full, "fields", name[4:]): continue
        ms = members(types, full, "methods", name)
        if ms and any(len(p) == argc for p in ms): continue
        fails.append(f"{t}::{name} ({argc} args): missing")
    return n, fails


def main():
    if len(sys.argv) < 3: sys.exit(__doc__)
    targets = patch_targets()
    if len(targets) < 10: sys.exit(f"compat: FAIL, only {len(targets)} patch targets parsed from src/ (parser out of date?)")
    tys, mems = il_refs(sys.argv[1])
    bad = 0
    for d in sys.argv[2:]:
        n, fails = check(d, targets, tys, mems)
        tag = os.path.basename(os.path.dirname(d))
        print(f"compat {tag}: {'PASS' if not fails else 'FAIL'} ({n} checks, {len(targets)} patch targets, {len(mems)} member refs)")
        for f in fails[:25]: print("   ", f)
        bad += bool(fails)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
