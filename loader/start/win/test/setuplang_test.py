#!/usr/bin/env python3
"""setuplang_test.py <setup.exe> <kit dir>: the Windows installer speaks the game's language (CrossOver bottle DSMP: wine,
not real Windows; one wine copy: ~/ds_multiplayer/rig/wine_lease.py). The language comes from the game's saved language
(UserSetting.json in the install folder's profile), as for a player who reinstalls.
  ja     install.bat, Japanese saved, Japanese console (code page 932): the install finishes, its lines are Japanese
  ru     install.bat, Russian saved, a folder with other files: refused in Russian, nothing changed
  ja437  install.bat, Japanese saved, a console that cannot show Japanese (code page 437): English
  setup  Setup.exe /S, Japanese saved (the folder of the last install): the install finishes (the language pick runs the
         starter with --lang in .onInit; install.bat gets DSMODDED_LANG); then Uninstall.exe /S removes it
Texts expected: setup_lang/<code>.json. install.bat runs under cmd /U (UTF-16 output, as Setup runs it)."""
import json, os, subprocess, sys, time
H = os.path.expanduser("~")
sys.path.insert(0, f"{H}/ds_multiplayer/win"); import wloader as w
C, DRIVE = w.C, w.DRIVE
WL = f"{H}/ds_multiplayer/rig/wine_lease.py"
GAME = "C:\\SteamLibrary\\steamapps\\common\\Dungeon Settlers"
NAME = "Dungeon Settlers Modded"
UKEY = "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DungeonSettlersModded"
LANGDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "setup_lang")
T = {c: json.load(open(f"{LANGDIR}/{c}.json", encoding="utf-8")) for c in ("en", "ja", "ru")}
JA = {"LC_ALL": "ja_JP.UTF-8", "LANG": "ja_JP.UTF-8"}
res = []
def check(n, ok, info=""): res.append(ok); print(("PASS" if ok else "FAIL"), n, info, flush=True)
def wine(args, env=None, timeout=600):
    return subprocess.run(["nice", "-n", "19", f"{C}/wine", "--bottle", "DSMP"] + args, capture_output=True,
                          env=dict(os.environ, **(env or {})), timeout=timeout)
def saved(folder, lang):                       # the game's saved language in <folder>'s modded profile
    d = f"{folder}/profiles/modded/data"; os.makedirs(d, exist_ok=True)
    open(f"{d}/UserSetting.json", "w").write('{"GeneralSettingData":{"SaveCurrentLanguageType": %d}}' % lang)
def bat(kit, root, env):
    r = wine(["cmd.exe", "/U", "/c", w.winpath(f"{kit}/install.bat"), GAME], env=dict(env, DSMODDED_NOPAUSE="1", DSMODDED_ROOT=root))
    return r.returncode, r.stdout.decode("utf-16-le", "replace")

def main(setup, kit):
    if subprocess.run([sys.executable, WL, "take", "setuplang", "--pid", str(os.getpid()), "--wait", "540"]).returncode:
        print("wine busy / locked after 9 min"); return 3
    try: return run(setup, kit)
    finally:
        wine(["reg", "delete", UKEY, "/f"])
        subprocess.run([sys.executable, WL, "release", "setuplang"])

def run(setup, kit):
    if not w.bottle_silent(): return 4
    old = f"{DRIVE}/old_setuplang/{int(time.time())}"                   # earlier runs' folders go aside, never deleted
    for d in ("SETUPLANG_JA", "SETUPLANG_RU", "SETUPLANG_437", "SETUPLANG_SETUP"):
        if os.path.exists(f"{DRIVE}/{d}"): os.makedirs(old, exist_ok=True); os.rename(f"{DRIVE}/{d}", f"{old}/{d}")

    f = f"{DRIVE}/SETUPLANG_JA/{NAME}"; saved(f, 7)
    rc, o = bat(kit, f"C:\\SETUPLANG_JA\\{NAME}", JA)
    check("ja: install.bat finishes", rc == 0 and os.path.exists(f"{f}/game/DungeonSettlers.exe"), f"rc {rc}")
    check("ja: its lines are Japanese", T["ja"]["B_DONE"] in o and T["ja"]["B_STEAM_FOLDER"] in o and T["en"]["B_DONE"] not in o,
          o.strip()[-200:].replace("\r\n", " | "))

    f = f"{DRIVE}/SETUPLANG_RU/{NAME}"; saved(f, 4); open(f"{f}/notes.txt", "w").write("user file\n")
    rc, o = bat(kit, f"C:\\SETUPLANG_RU\\{NAME}", {})
    check("ru: refused in Russian, nothing changed", rc != 0 and T["ru"]["B_OTHER_FILES"] in o and sorted(os.listdir(f)) == ["notes.txt", "profiles"],
          o.strip()[-200:].replace("\r\n", " | "))

    f = f"{DRIVE}/SETUPLANG_437/{NAME}"; saved(f, 7); open(f"{f}/notes.txt", "w").write("user file\n")
    rc, o = bat(kit, f"C:\\SETUPLANG_437\\{NAME}", {})
    check("ja437: English where the console cannot show Japanese", rc != 0 and T["en"]["B_OTHER_FILES"] in o, o.strip()[-120:].replace("\r\n", " | "))

    f = f"{DRIVE}/SETUPLANG_SETUP/{NAME}"; saved(f, 7)
    wine(["reg", "add", UKEY, "/v", "InstallLocation", "/d", f"C:\\SETUPLANG_SETUP\\{NAME}", "/f"])
    r = wine([w.winpath(setup), "/S", f"/GAME={GAME}"], env=JA, timeout=900)
    check("setup: Japanese Setup /S installs", r.returncode == 0 and os.path.exists(f"{f}/game/DungeonSettlers.exe")
          and os.path.exists(f"{f}/Uninstall.exe") and os.path.exists(f"{f}/install_files.log"), f"rc {r.returncode}")
    # install.bat runs under cmd /U there: the files the starter and Steam read must still be plain ASCII
    b, a = open(f"{f}/game/dsmodded_build.txt", "rb").read(), open(f"{f}/game/steam_appid.txt", "rb").read()
    check("setup: build and Steam app id files in ASCII", b.strip() == open(f"{kit}/files/BUILD.txt", "rb").read().strip() and a.strip() == b"2798330",
          f"{b[:12]!r} {a[:12]!r}")
    wine([f"C:\\SETUPLANG_SETUP\\{NAME}\\Uninstall.exe", "/S"], env=JA, timeout=300)
    for i in range(60):                                                  # the uninstaller runs from a copy in %TEMP%
        if not os.path.exists(f"{f}/game/DungeonSettlers.exe"): break
        time.sleep(5)
    time.sleep(5)
    check("setup: its uninstaller removes it, the saves stay", not os.path.exists(f"{f}/game") and not os.path.exists(f"{f}/Uninstall.exe")
          and os.path.exists(f"{f}/profiles/modded/data/UserSetting.json"), str(sorted(os.listdir(f)) if os.path.exists(f) else "gone"))
    print(f"== {sum(res)}/{len(res)} passed", flush=True)
    return 0 if all(res) else 1

if __name__ == "__main__": sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
