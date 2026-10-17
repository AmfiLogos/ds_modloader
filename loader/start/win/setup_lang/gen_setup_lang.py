#!/usr/bin/env python3
"""gen_setup_lang.py nsh <out.nsh> | bat <out dir>: the Windows installer's texts in the game's 10 languages from
setup_lang/<code>.json (English = en.json; a missing text falls back to English).
  nsh  LangString lines for dsmodded.nsi (S_* texts; UTF-8 with a BOM, NSIS reads it as UTF-8)
  bat  setup_<code>.bat for the kit's files\\lang (B_* and U_* texts as set "KEY=text" lines, UTF-8 without a BOM, CRLF):
       install.bat / uninstall.bat switch the console to code page 65001, call the file, switch back
Exit 1 on a bad text (a placeholder lost, or % " ! in a batch text: cmd would read them)."""
import json, os, re, sys

D = os.path.dirname(os.path.abspath(__file__))
# game LanguageType order; NSIS language names (MUI_LANGUAGE) and their LANGIDs
LANGS = [("en", "English"), ("ko", "Korean"), ("fr", "French"), ("de", "German"), ("ru", "Russian"),
         ("zh-Hans", "SimpChinese"), ("zh-Hant", "TradChinese"), ("ja", "Japanese"), ("es", "Spanish"), ("pt-BR", "PortugueseBR")]
PH = re.compile(r"\{[A-Z]+\}")


def load(code):
    p = os.path.join(D, code + ".json")
    if not os.path.exists(p):
        return {}
    return {k: v for k, v in json.load(open(p, encoding="utf-8")).items() if not k.startswith("_")}


def texts():
    en = load("en")
    out, bad = {}, []
    for code, _ in LANGS:
        t = load(code)
        for k in t:
            if k not in en:
                bad.append(f"{code}: {k} is not in en.json")
        m = {}
        for k, e in en.items():
            v = t.get(k, e)
            if sorted(PH.findall(v)) != sorted(PH.findall(e)) or v.count("\n") != e.count("\n"):
                bad.append(f"{code}: {k}: placeholders or line breaks differ from English")
                v = e
            if k[0] in "BU" and re.search(r'[%"!]', v):
                bad.append(f"{code}: {k}: % \" or ! in a batch text")
                v = e
            m[k] = v
        out[code] = m
    return out, bad


def nsis(s):
    s = s.replace("$", "$$").replace('"', '$\\"').replace("\n", "$\\r$\\n")
    return s.replace("{NAME}", "${NAME}").replace("{INSTDIR}", "$INSTDIR")


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("nsh", "bat"):
        print(__doc__); return 2
    t, bad = texts()
    for b in bad:
        print("gen_setup_lang:", b, file=sys.stderr)
    if bad:
        return 1
    if sys.argv[1] == "nsh":
        lines = ["; made by setup_lang/gen_setup_lang.py from setup_lang/<code>.json: do not edit"]
        for code, name in LANGS:
            for k, v in t[code].items():
                if k.startswith("S_"):
                    lines.append(f'LangString {k} ${{LANG_{name.upper()}}} "{nsis(v)}"')
        open(sys.argv[2], "w", encoding="utf-8-sig", newline="\r\n").write("\n".join(lines) + "\n")
    else:
        os.makedirs(sys.argv[2], exist_ok=True)
        for code, _ in LANGS[1:]:
            lines = ["@rem made by setup_lang/gen_setup_lang.py: install.bat and uninstall.bat call it with code page 65001"]
            lines += [f'set "{k}={v.replace("{BUILD}", "@1")}"' for k, v in t[code].items() if k[0] in "BU"]
            open(os.path.join(sys.argv[2], f"setup_{code}.bat"), "w", encoding="utf-8", newline="\r\n").write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
