#!/usr/bin/env python3
"""i18n_tool.py - string tables for the Dungeon Settlers mods (same file in all three repos).

  i18n_tool.py extract <lang_dir> <src_dir>...   write <lang_dir>/en.json (every TR/TRN/TR_NOOP key)
                                                 and <lang_dir>/_where.tsv (key, file:line)
  i18n_tool.py missing <lang_dir> <code>         print keys without a translation (JSON list)
  i18n_tool.py check <lang_dir>                  parse every file, report missing, stale and bad entries
                                                 (bad = printf conversions or <tags> differ from English);
                                                 exit 1 on a bad entry or a broken file
  i18n_tool.py prune <lang_dir>                  drop stale keys (no longer in en.json) from every language

Keys are the English text. Plural keys are "one||other" with a list of forms as value.
"""
import json
import os
import re
import sys

CODES = ["ko", "fr", "de", "ru", "zh-Hans", "zh-Hant", "ja", "es", "pt-BR"]
NFORMS = {"ru": 3, "ko": 1, "ja": 1, "zh-Hans": 1, "zh-Hant": 1}  # others 2
EXTS = (".c", ".h", ".m", ".mm", ".cpp")

LIT = r'"(?:[^"\\\n]|\\.)*"'
LITS = rf'{LIT}(?:\s*{LIT})*'
RX = re.compile(rf'\b(TR|TR_NOOP|TRN)\s*\(\s*({LITS})(?:\s*,\s*({LITS}))?')


def c_unescape(lits):
    out = []
    for m in re.finditer(LIT, lits):
        s = m.group(0)[1:-1]
        b = bytearray()
        i = 0
        while i < len(s):
            c = s[i]
            if c != "\\":
                b += c.encode("utf-8")
                i += 1
                continue
            n = s[i + 1]
            i += 2
            if n in "01234567":
                j = i - 1
                while i < len(s) and i - j < 3 and s[i] in "01234567":
                    i += 1
                b.append(int(s[j:i], 8) & 0xFF)
            elif n == "x":
                j = i
                while i < len(s) and s[i] in "0123456789abcdefABCDEF":
                    i += 1
                b.append(int(s[j:i], 16) & 0xFF)
            else:
                b += {"n": b"\n", "t": b"\t", "r": b"\r", "\\": b"\\", '"': b'"', "'": b"'",
                      "a": b"\a", "0": b"\0"}.get(n, n.encode())
        out.append(bytes(b))
    return b"".join(out).decode("utf-8", "replace")


def scan(dirs):
    keys, where = {}, {}
    for d in dirs:
        for root, subs, files in os.walk(d):
            subs[:] = [s for s in subs if s not in ("quickjs", "build", "old", ".git", "work")]
            for f in sorted(files):
                if not f.endswith(EXTS) or f == "ds_i18n.h":
                    continue
                p = os.path.join(root, f)
                src = open(p, encoding="utf-8", errors="replace").read()
                for m in RX.finditer(src):
                    kind, a, b = m.group(1), m.group(2), m.group(3)
                    if kind == "TRN":
                        if not b:
                            continue
                        key = c_unescape(a) + "||" + c_unescape(b)
                    else:
                        key = c_unescape(a)
                    if not key.strip():
                        continue
                    line = src.count("\n", 0, m.start()) + 1
                    keys[key] = key
                    where.setdefault(key, []).append(f"{os.path.relpath(p, d)}:{line}")
    return keys, where


SIG = re.compile(r"%%|%[-+ #0-9.*$hljztLqI]*[a-zA-Z]|<[^<>]{0,64}>")


def sig(s):
    return [t for t in SIG.findall(s) if t != "%%"]


def bad_entry(key, val, code):
    forms = val if isinstance(val, list) else [val]
    if "||" in key:
        one, other = key.split("||", 1)
        if not isinstance(val, list):
            return "plural key needs a list"
        want = NFORMS.get(code, 2)
        if len(forms) != want:
            return f"needs {want} forms, has {len(forms)}"
        for f in forms:
            if sig(f) != sig(other) and sig(f) != sig(one):
                return f"conversions/tags differ: {f!r}"
        return None
    if isinstance(val, list):
        return "not a plural key, value must be a string"
    if not val:
        return "empty"
    if sig(val) != sig(key):
        return f"conversions/tags differ: {sig(key)} vs {sig(val)}"
    return None


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def save(path, obj):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=1, sort_keys=True)
        f.write("\n")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cmd, ld = sys.argv[1], sys.argv[2]
    if cmd == "extract":
        keys, where = scan(sys.argv[3:])
        os.makedirs(ld, exist_ok=True)
        save(os.path.join(ld, "en.json"), {k: k for k in keys})
        with open(os.path.join(ld, "_where.tsv"), "w", encoding="utf-8") as f:
            for k in sorted(where):
                f.write(json.dumps(k, ensure_ascii=False) + "\t" + " ".join(where[k]) + "\n")
        print(f"{len(keys)} keys -> {ld}/en.json")
    elif cmd == "missing":
        en = load(os.path.join(ld, "en.json"))
        p = os.path.join(ld, sys.argv[3] + ".json")
        have = load(p) if os.path.exists(p) else {}
        print(json.dumps([k for k in en if k not in have], ensure_ascii=False, indent=1))
    elif cmd in ("check", "prune"):
        en = load(os.path.join(ld, "en.json"))
        rc = 0
        for code in CODES:
            p = os.path.join(ld, code + ".json")
            if not os.path.exists(p):
                print(f"{code}: no file (English shown)")
                continue
            try:
                tr = load(p)
            except Exception as e:  # noqa: BLE001
                print(f"{code}: BROKEN {e}")
                rc = 1
                continue
            stale = [k for k in tr if k not in en and not k.startswith("_")]
            missing = [k for k in en if k not in tr]
            bad = [(k, why) for k, v in tr.items() if not k.startswith("_") and k in en
                   for why in [bad_entry(k, v, code)] if why]
            print(f"{code}: {len(en) - len(missing)}/{len(en)} done, {len(missing)} missing, "
                  f"{len(stale)} stale, {len(bad)} bad")
            for k, why in bad[:20]:
                print(f"   BAD {k!r}: {why}")
            if bad:
                rc = 1
            if cmd == "prune" and stale:
                save(p, {k: v for k, v in tr.items() if k not in stale})
        sys.exit(rc)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
