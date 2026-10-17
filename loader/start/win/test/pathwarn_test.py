#!/usr/bin/env python3
"""pathwarn_test.py <setup.exe> <kit dir>: a folder path the game cannot start from (letters outside the ANSI code page,
code page 1252 in the CrossOver bottle DSMP: Chinese, Japanese, Cyrillic) gets the warning; a plain or accented path does
not (wine, not real Windows; one wine copy: ~/ds_multiplayer/rig/wine_lease.py). No game copy is made.
  check     starter --pathcheck: 3 for the bad paths, 0 for the plain and the accented one
  bat       install.bat into a Chinese folder: the warning, nothing made
  bat-ask   install.bat (no NOPAUSE): Chinese folder, the warning, then a plain folder typed in is taken (it holds other
            files: refused there, past the path check)
  setup     Setup.exe /S /D=<Chinese folder>: nothing made (a plain install is uninst_test.py's step 1)
  start     the starter alone in a Chinese folder: starter.log names the bad path, the game is not started"""
import os, shutil, subprocess, sys, time
H = os.path.expanduser("~")
sys.path.insert(0, f"{H}/ds_multiplayer/win"); import wloader as w
C, DRIVE = w.C, w.DRIVE
WL = f"{H}/ds_multiplayer/rig/wine_lease.py"
GAME = "C:\\SteamLibrary\\steamapps\\common\\Dungeon Settlers"
NAME = "Dungeon Settlers Modded"
ZH, FULL = "游戏测试", "PATHWARN_FULL"
res = []
def check(n, ok, info=""): res.append(ok); print(("PASS" if ok else "FAIL"), n, info, flush=True)
def wine(args, env=None, **kw):
    return subprocess.run(["nice", "-n", "19", f"{C}/wine", "--bottle", "DSMP"] + args, capture_output=True,
                          env=dict(os.environ, **(env or {})), **kw)
def out(r): return (r.stdout + r.stderr).decode("utf-8", "replace")

def main(setup, kit):
    if subprocess.run([sys.executable, WL, "take", "pathwarn", "--pid", str(os.getpid()), "--wait", "540"]).returncode:
        print("wine busy / locked after 9 min"); return 3
    try: return run(setup, kit)
    finally: subprocess.run([sys.executable, WL, "release", "pathwarn"])

def run(setup, kit):
    old = f"{DRIVE}/old_pathwarn/{int(time.time())}"                  # earlier runs' folders go aside, never deleted
    for d in (ZH, FULL, "PATHWARN_BAT"):
        if os.path.exists(f"{DRIVE}/{d}"): os.makedirs(old, exist_ok=True); os.rename(f"{DRIVE}/{d}", f"{old}/{d}")
    exe = w.winpath(f"{kit}/files/{NAME}.exe")
    os.makedirs(f"{DRIVE}/PATHWARN_BAT")                                # the path as an argument: a .bat is read in the OEM code page
    open(f"{DRIVE}/PATHWARN_BAT/check.bat", "w", newline="\r\n").write('@start "" /wait %1 --pathcheck %2\n@echo RC=%errorlevel%\n')
    for p, want in ((f"C:\\{ZH}\\{NAME}", 3), ("C:\\ゲーム\\x", 3), ("C:\\Игры\\x", 3), (f"C:\\Games\\{NAME}", 0), ("C:\\Jogos é ñ ü\\x", 0)):
        r = wine(["cmd.exe", "/c", "C:\\PATHWARN_BAT\\check.bat", exe, p])
        check(f"check {p} -> {want}", f"RC={want}" in out(r), out(r).strip()[-40:])

    bat = w.winpath(f"{kit}/install.bat")
    r = wine(["cmd.exe", "/c", bat, GAME], env={"DSMODDED_NOPAUSE": "1", "DSMODDED_ROOT": f"C:\\{ZH}\\{NAME}"}, timeout=300)
    check("bat: Chinese folder warned, nothing made", "will not start from this folder" in out(r) and not os.path.exists(f"{DRIVE}/{ZH}"),
          f"rc {r.returncode}")

    os.makedirs(f"{DRIVE}/{FULL}"); open(f"{DRIVE}/{FULL}/notes.txt", "w").write("user file\n")
    r = wine(["cmd.exe", "/c", bat, GAME], env={"DSMODDED_ROOT": f"C:\\{ZH}\\{NAME}"}, input=f"C:\\{FULL}\r\n".encode(), timeout=300)
    o = out(r)
    check("bat-ask: warned, then the typed plain folder is taken", "will not start from this folder" in o and "already holds other files" in o
          and not os.path.exists(f"{DRIVE}/{ZH}"), o.strip()[-160:].replace("\r\n", " | "))

    r = wine([w.winpath(setup), "/S", f"/GAME={GAME}", f"/D=C:\\{ZH}"], timeout=300)
    check("setup /S into a Chinese folder: nothing made", not os.path.exists(f"{DRIVE}/{ZH}/{NAME}/game") and
          not os.path.exists(f"{DRIVE}/{ZH}/{NAME}/dsmodded_install.txt"), f"rc {r.returncode}")

    d = f"{DRIVE}/{ZH}/DSM"; os.makedirs(d, exist_ok=True); shutil.copy(f"{kit}/files/{NAME}.exe", d)
    wine([f"C:\\{ZH}\\DSM\\{NAME}.exe"], env={"DSL_NO_ALERT": "1"}, timeout=120)
    lg = f"{d}/profiles/modded/starter.log"
    t = open(lg, encoding="utf-8", errors="replace").read() if os.path.exists(lg) else ""
    check("start in a Chinese folder: logged, the game not started", "ERROR folder path has letters" in t and "started " not in t, t.strip()[-120:])
    print(f"== {sum(res)}/{len(res)} passed", flush=True)
    return 0 if all(res) else 1

if __name__ == "__main__": sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
