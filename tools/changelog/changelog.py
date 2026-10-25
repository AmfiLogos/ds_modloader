#!/usr/bin/env python3
"""CHANGELOG.md of the three repos (ds_modloader, ds_multiplayer, ds_custommodes): one entry per released version.

Owner 2026-10-25 ("All three"): a CHANGELOG.md in each repo, a release check that FAILS when the version being released
has no entry, the GitHub release notes built FROM that entry, and an in-game "What's new" window (kit only) that shows
the entry once after the kit version changed. The same file serves all three repos (copy it unchanged).

Format (newest first; `lint` enforces it):
    # Changelog
    <free text>
    ## <player-visible name> (<id>)          one section per component: kit, multiplayer, custom-modes, gcd-cdr, ...
    ### Unreleased (<version>)               at most one, first in its section: the next version, still being filled
    ### <version> (<YYYY-MM-DD>)             one per released version, newest first
    - what changed, in plain English        bullets; a longer bullet goes on over lines indented by two spaces;
      (two spaces) more of the same bullet   a line ending in ":" groups the bullets under it ("Multiplayer:")
A tag names its entry: v0.1.27 = (kit, 0.1.27); multiplayer-v0.1.26 = (multiplayer, 0.1.26); bepinex-mac-0.1.3 =
(bepinex-mac, 0.1.3).

Usage:
  changelog.py lint   [--file F]                          format check (all entries)
  changelog.py check  --tag TAG [--file F] [--binary B]... [--notes N]
                                                           release check: a DATED entry for that version, not empty;
                                                           --binary (kit): its in-game text is inside that loader
                                                           (dsload.dylib / baselib.dll, or every loader inside a kit
                                                           release file .dmg / .zip / Setup.exe), so the window shows it;
                                                           --notes: that release-notes file holds the entry (built from it)
  changelog.py notes  --tag TAG [--file F]                the entry's text (markdown) for the release notes
  changelog.py fill   --tag TAG --head H [--file F]       H with its line "<!-- CHANGELOG -->" replaced by the entry
  changelog.py cheader --version V --out O [--file F]     C header for the loader: the kit entry of version V as the
                                                           in-game text (empty when there is none: no window)
F defaults to CHANGELOG.md of the repo this tool sits in. Exit 1 = a check failed (the line says why).
"""
import argparse, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
MARK = "<!-- CHANGELOG -->"
H2 = re.compile(r"^## (?P<name>.+) \((?P<id>[a-z0-9][a-z0-9-]*)\)\s*$")
H3 = re.compile(r"^### (?:Unreleased \((?P<uver>\d+(?:\.\d+)+)\)|(?P<ver>\d+(?:\.\d+)+) \((?P<date>\d{4}-\d{2}-\d{2})\))\s*$")
TAG = re.compile(r"^(?:v(?P<kv>\d+(?:\.\d+)+)|(?P<id>[a-z0-9][a-z0-9-]*?)-v?(?P<ver>\d+(?:\.\d+)+))$")


def die(msg):
    print(msg)
    sys.exit(1)


def vkey(v):
    return tuple(int(x) for x in v.split("."))


def parse(path):
    """-> (sections {id: {"name", "entries": [{"ver", "date" (None = Unreleased), "body": [lines], "line"}]}}, errors)"""
    try:
        lines = open(path, encoding="utf-8").read().split("\n")
    except OSError as e:
        die(f"CHANGELOG FAIL: cannot read {path}: {e}")
    secs, errs, cur, ent = {}, [], None, None
    for n, raw in enumerate(lines, 1):
        s = raw.rstrip()
        if s.startswith("#### "):
            errs.append(f"{path}:{n}: no heading below ### (use bullets)")
        elif s.startswith("### "):
            m = H3.match(s)
            if not m:
                errs.append(f"{path}:{n}: version heading must be '### <version> (<YYYY-MM-DD>)' or '### Unreleased (<version>)'")
                ent = None
                continue
            if cur is None:
                errs.append(f"{path}:{n}: version heading before any '## <name> (<id>)' section")
                continue
            ent = {"ver": m.group("ver") or m.group("uver"), "date": m.group("date"), "body": [], "line": n}
            cur["entries"].append(ent)
        elif s.startswith("## "):
            m = H2.match(s)
            if not m:
                errs.append(f"{path}:{n}: section heading must be '## <name> (<id>)'")
                cur = ent = None
                continue
            if m.group("id") in secs:
                errs.append(f"{path}:{n}: section ({m.group('id')}) twice")
            cur = secs.setdefault(m.group("id"), {"name": m.group("name"), "entries": []})
            ent = None
        elif ent is not None:
            ent["body"].append(s)
    for e in (e for sec in secs.values() for e in sec["entries"]):
        while e["body"] and not e["body"][0].strip():
            e["body"].pop(0)
        while e["body"] and not e["body"][-1].strip():
            e["body"].pop()
    return secs, errs


def lint(secs, errs, path):
    for sid, sec in secs.items():
        seen, prev = set(), None
        for i, e in enumerate(sec["entries"]):
            where = f"{path}:{e['line']}: ({sid}) {e['ver']}"
            if e["date"] is None and i:
                errs.append(f"{where}: 'Unreleased' must be the first entry of its section")
            if e["ver"] in seen:
                errs.append(f"{where}: version twice")
            seen.add(e["ver"])
            if prev and vkey(e["ver"]) >= vkey(prev):
                errs.append(f"{where}: not newest first (after {prev})")
            prev = e["ver"]
            if not any(l.strip() for l in e["body"]):
                errs.append(f"{where}: empty entry")
            for l in e["body"]:
                if l.strip() and not (l.startswith("- ") or l.startswith("  ") or l.endswith(":") or l[0].isalnum()):
                    errs.append(f"{where}: line '{l[:60]}' is not a bullet ('- '), a continuation (two spaces) or a group line")
                    break
    return errs


def find(secs, tag):
    m = TAG.match(tag)
    if not m:
        die(f"CHANGELOG FAIL: '{tag}' is not a release tag (v0.1.N, <id>-v0.1.N)")
    sid, ver = ("kit", m.group("kv")) if m.group("kv") else (m.group("id"), m.group("ver"))
    e = next((e for e in secs.get(sid, {}).get("entries", []) if e["ver"] == ver), None)
    return sid, ver, e


def loaders(path):
    """[(label, bytes)]: a loader file itself, or every loader inside a kit release file (check_keys.py unpacks them)"""
    if path.endswith((".dmg", ".zip", ".exe")):
        sys.path.insert(0, os.path.join(os.path.dirname(HERE), "release_sign"))
        import check_keys
        found = check_keys.loaders(path)
        if not found:
            die(f"CHANGELOG FAIL: no loader found in {path}")
        return found
    return [(path, open(path, "rb").read())]


def display(body):
    """the in-game text: markdown links -> their text, no backticks / bold marks; bullets and groups stay"""
    out = []
    for l in body:
        l = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", l)
        l = l.replace("`", "").replace("**", "")
        out.append(l)
    return "\n".join(out)


def c_lit(s):
    b = s.encode("utf-8")
    out, col = ['"'], 0
    for ch in b:
        c = chr(ch)
        if c == "\\": out.append("\\\\")
        elif c == '"': out.append('\\"')
        elif c == "\n": out.append('\\n"\n    "')
        elif 32 <= ch < 127: out.append(c)
        else: out.append(f'\\{ch:03o}')              # octal: never runs into the next character (hex escapes do)
    out.append('"')
    return "".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["lint", "check", "notes", "fill", "cheader"])
    ap.add_argument("--file", default=os.path.join(REPO, "CHANGELOG.md"))
    ap.add_argument("--tag")
    ap.add_argument("--binary", action="append", default=[])
    ap.add_argument("--head")
    ap.add_argument("--notes")
    ap.add_argument("--version")
    ap.add_argument("--out")
    a = ap.parse_args()
    secs, errs = parse(a.file)

    if a.cmd == "lint":
        errs = lint(secs, errs, a.file)
        for e in errs:
            print(e)
        n = sum(len(s["entries"]) for s in secs.values())
        if errs or not n:
            die(f"CHANGELOG LINT FAIL: {len(errs)} problem(s), {n} entries in {a.file}")
        print(f"CHANGELOG LINT OK: {len(secs)} sections, {n} entries")
        return

    if a.cmd == "cheader":
        if not a.version or not a.out:
            die("cheader needs --version and --out")
        e = next((e for e in secs.get("kit", {}).get("entries", []) if e["ver"] == a.version), None)
        text = display(e["body"]) if e and not errs else ""
        if not text:
            print(f"changelog: no kit entry for {a.version} in {a.file}: the loader shows no What's new window"
                  + (f" ({len(errs)} format problem(s): run changelog.py lint)" if errs else ""), file=sys.stderr)
        h = ("/* made by tools/changelog/changelog.py cheader from CHANGELOG.md: the kit entry of this release (the What's\n"
             "   new window, src/dsl_whatsnew.c). Never edit; never commit. */\n"
             f"#define DSL_WHATSNEW_VER {c_lit(a.version)}\n"
             f"static const char DSL_WHATSNEW_TEXT[] =\n    {c_lit(text)};\n")
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        tmp = a.out + ".tmp"
        open(tmp, "w", encoding="utf-8").write(h)
        os.replace(tmp, a.out)
        return

    if not a.tag:
        die(f"{a.cmd} needs --tag")
    if errs:
        for e in errs:
            print(e)
        die(f"CHANGELOG FAIL: {a.file} has {len(errs)} format problem(s) (changelog.py lint)")
    sid, ver, e = find(secs, a.tag)
    if e is None:
        die(f"CHANGELOG MISSING: no entry for {sid} {ver} (tag {a.tag}) in {a.file}: add '### {ver} (<date>)' under "
            f"'## <name> ({sid})' with what changed, in plain English")
    if a.cmd == "check":
        if e["date"] is None:
            die(f"CHANGELOG FAIL: {sid} {ver} is still 'Unreleased' in {a.file}: the release commit renames it "
                f"'### {ver} (<YYYY-MM-DD>)'")
        if not any(l.strip() for l in e["body"]):
            die(f"CHANGELOG FAIL: the {sid} {ver} entry is empty")
        text, nb = display(e["body"]).encode("utf-8"), 0
        for b in a.binary:
            for what, data in loaders(b):
                nb += 1
                if text not in data:
                    die(f"CHANGELOG FAIL: {what} does not hold the {sid} {ver} entry: it was built before the entry was final "
                        f"(rebuild the loader after the CHANGELOG change), so the What's new window would not show it")
        if a.notes and "\n".join(e["body"]) not in open(a.notes, encoding="utf-8").read():
            die(f"CHANGELOG FAIL: the release notes {a.notes} do not hold the {sid} {ver} entry (build them with "
                f"changelog.py fill: the head's line '{MARK}' becomes the entry)")
        print(f"CHANGELOG OK {sid} {ver} ({e['date']}, {sum(1 for l in e['body'] if l.startswith('- '))} bullets"
              + (f", in {nb} loader(s)" if a.binary else "") + (", in the notes" if a.notes else "") + ")")
    elif a.cmd == "notes":
        print("\n".join(e["body"]))
    elif a.cmd == "fill":
        if not a.head:
            die("fill needs --head")
        head = open(a.head, encoding="utf-8").read().split("\n")
        if sum(1 for l in head if l.strip() == MARK) != 1:
            die(f"CHANGELOG FAIL: {a.head} needs exactly one line '{MARK}' (the notes take their 'What changed' from CHANGELOG.md)")
        out = []
        for l in head:
            out.extend(e["body"] if l.strip() == MARK else [l])
        sys.stdout.write("\n".join(out))


if __name__ == "__main__":
    main()
