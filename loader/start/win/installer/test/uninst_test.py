#!/usr/bin/env python3
"""uninst_test.py <setup.exe> <kit dir>: the installer never mixes with other files and the uninstaller removes only what
it installed (DSMP CrossOver bottle, silent runs, one wine copy: ~/ds_multiplayer/rig/wine_lease.py).
  1 /D=C:\\USERDIR (holds other files)  -> installs into C:\\USERDIR\\Dungeon Settlers Modded, the other files stay
  2 /D=<folder named like ours that holds other files, no marker> -> refused, nothing changed
  3 a second Setup without /D           -> the same folder as before (InstallDirRegKey)
  4 install.bat (zip kit) into a folder with other files -> refused
  5 uninstall with a player file in game\\ and mods\\ and a save in profiles\\ -> only the installed files go"""
import os, signal, subprocess, sys, time
H = os.path.expanduser("~")
sys.path.insert(0, f"{H}/ds_multiplayer/win"); import wloader as w
C, BOTTLE, DRIVE = w.C, "DSMP", w.DRIVE
WL = f"{H}/ds_multiplayer/rig/wine_lease.py"
NAME = "Dungeon Settlers Modded"
setup, kit = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
U, F, F2 = f"{DRIVE}/USERDIR", f"{DRIVE}/FULLDIR/{NAME}", f"{DRIVE}/FULLDIR2"
I = f"{U}/{NAME}"
res = []
def check(n, ok, info=""): res.append(ok); print(("PASS" if ok else "FAIL"), n, info, flush=True)
def wine(args, **kw): return subprocess.run(["nice", "-n", "19", f"{C}/wine", "--bottle", BOTTLE] + args, capture_output=True, text=True, **kw)
def put(p, s="user file\n"): os.makedirs(os.path.dirname(p), exist_ok=True); open(p, "w").write(s)

if subprocess.run([sys.executable, WL, "take", "uninstfix", "--pid", str(os.getpid()), "--wait", "540"]).returncode: print("wine busy / locked after 9 min"); sys.exit(3)
try:
    if not w.bottle_silent(): sys.exit(4)
    old = f"{DRIVE}/old_uninstfix/{int(time.time())}"
    for d in (U, os.path.dirname(F), F2):
        if os.path.exists(d): os.makedirs(old, exist_ok=True); os.rename(d, f"{old}/{os.path.basename(d)}")
    put(f"{U}/notes.txt"); put(f"{U}/photos/a.jpg", "jpg")
    put(f"{F}/budget.xlsx"); put(f"{F2}/thesis.docx")
    # 1
    r = wine([w.winpath(setup), "/S", "/D=C:\\USERDIR"], timeout=900)
    check("1 install into a folder with other files: own subfolder", r.returncode == 0 and os.path.exists(f"{I}/game/DungeonSettlers.exe")
          and os.path.exists(f"{I}/dsmodded_install.txt") and os.path.exists(f"{I}/install_files.log"), f"rc {r.returncode}")
    check("1 the other files untouched", open(f"{U}/notes.txt").read() == "user file\n" and os.path.exists(f"{U}/photos/a.jpg"))
    # 2
    r = wine([w.winpath(setup), "/S", f"/D=C:\\FULLDIR\\{NAME}"], timeout=300)
    check("2 folder of ours by name but holding other files: refused", not os.path.exists(f"{F}/game")   # CrossOver's wine gives rc 0 for this GUI exe
          and os.listdir(F) == ["budget.xlsx"], f"rc {r.returncode}, {os.listdir(F)}")
    # 3
    t3 = time.time(); r = wine([w.winpath(setup), "/S"], timeout=900)
    loc = f"{DRIVE}/users/crossover/AppData/Local/{NAME}"
    check("3 second Setup without /D reuses the folder", r.returncode == 0 and os.path.getmtime(f"{I}/install_files.log") >= t3
          and not (os.path.exists(f"{loc}/install_files.log") and os.path.getmtime(f"{loc}/install_files.log") >= t3), f"rc {r.returncode}")
    # 4
    r = wine(["cmd", "/c", "set DSMODDED_NOPAUSE=1&& set DSMODDED_ROOT=C:\\FULLDIR2&& call " + w.winpath(f"{kit}/install.bat")], timeout=300)
    check("4 install.bat into a folder with other files: refused", r.returncode != 0 and os.listdir(F2) == ["thesis.docx"]
          and "already holds other files" in r.stdout, f"rc {r.returncode}")
    # 5
    put(f"{I}/game/player_note.txt"); put(f"{I}/mods/my-own-mod/mod.json", "{}"); put(f"{I}/profiles/modded/data/Saves/w1.json", "{}")
    r = wine([f"C:\\USERDIR\\{NAME}\\Uninstall.exe", "/S"], timeout=600); time.sleep(10)
    check("5 installed files removed", not os.path.exists(f"{I}/game/DungeonSettlers.exe") and not os.path.exists(f"{I}/{NAME}.exe")
          and not os.path.exists(f"{I}/mods/multiplayer") and not os.path.exists(f"{I}/Uninstall.exe"))
    check("5 player files, saves and the folder around stay", os.path.exists(f"{I}/game/player_note.txt") and os.path.exists(f"{I}/mods/my-own-mod/mod.json")
          and os.path.exists(f"{I}/profiles/modded/data/Saves/w1.json") and os.path.exists(f"{U}/notes.txt") and os.path.exists(f"{U}/photos/a.jpg"))
    left = sorted(os.path.relpath(os.path.join(d, f), I) for d, _, fs in os.walk(I) for f in fs)
    check("5 nothing else left", left == ["game/player_note.txt", "mods/my-own-mod/mod.json", "profiles/modded/data/Saves/w1.json"], str(left[:8]))
finally:
    for pid, c in w.dsmp_pids():
        if "USERDIR" in c or "FULLDIR" in c:
            try: os.kill(pid, signal.SIGKILL)
            except Exception: pass
    subprocess.run([sys.executable, WL, "release", "uninstfix"])
print(f"RESULT {sum(res)}/{len(res)} pass")
