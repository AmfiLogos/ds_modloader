#!/usr/bin/env python3
"""uninst_test.py <setup.exe> <kit dir>: the installer never mixes with other files and the uninstaller removes only what
it installed (DSMP CrossOver bottle, silent runs, one wine copy: ~/ds_multiplayer/rig/wine_lease.py).
  1 /D=C:\\USERDIR_José (holds other files; a letter outside ASCII) -> its own subfolder, the other files stay
  2 /D=<folder named like ours that holds other files, no marker> -> refused, nothing changed
  3 a second Setup without /D           -> the same folder as before (InstallDirRegKey)
  4 install.bat (zip kit) into a folder with other files -> refused
  5 after one game start: uninstall with a player mod in mods\\ and a save in profiles\\ -> the program goes (game copy
    and what the starter made included), the player mod, the saves and the files around stay"""
import os, signal, subprocess, sys, time
H = os.path.expanduser("~")
sys.path.insert(0, f"{H}/ds_multiplayer/win"); import wloader as w
C, BOTTLE, DRIVE = w.C, "DSMP", w.DRIVE
WL = f"{H}/ds_multiplayer/rig/wine_lease.py"
NAME = "Dungeon Settlers Modded"
setup, kit = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
U, F, F2 = f"{DRIVE}/USERDIR_José", f"{DRIVE}/FULLDIR/{NAME}", f"{DRIVE}/FULLDIR2"
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
    r = wine([w.winpath(setup), "/S", "/D=C:\\USERDIR_José"], timeout=900)
    check("1 install into a folder with other files: own subfolder", r.returncode == 0 and os.path.exists(f"{I}/game/DungeonSettlers.exe")
          and os.path.exists(f"{I}/dsmodded_install.txt") and os.path.exists(f"{I}/install_files.log"), f"rc {r.returncode}")
    check("1 licenses in the install folder", os.path.exists(f"{I}/licenses/LICENSE.txt") and os.path.exists(f"{I}/licenses/THIRD_PARTY_NOTICES.txt"))
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
    # 5 (after one game start + quit, the way a player uses it; the gate does the same). The install path has letters
    # outside the OEM code page and a mod zip with such a name is dropped in: the starter installs it and lists it.
    import zipfile
    with zipfile.ZipFile(f"{I}/mods/模组 mod-1.0.0.zip", "w") as z:
        z.writestr("uni-mod/mod.json", '{"id":"uni-mod","name":"Uni","version":"1.0.0","platforms":{}}')

    env = dict(os.environ, DSRIG_PORT=str(w.PORT), DSMP_GUARD_TAG="1", DSL_NO_ALERT="1", WINEDLLOVERRIDES="winhttp=n,b")
    subprocess.run(["nice", "-n", "19", f"{C}/wine", "--bottle", BOTTLE, "--no-wait", f"C:\\USERDIR_José\\{NAME}\\{NAME}.exe", "-batchmode", "-nographics"],
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0, title = time.time(), False
    while time.time() - t0 < 300 and not title:
        time.sleep(5)
        try: title = w.cmd({"cmd": "status"}, timeout=5).get("boot_ready", False)
        except Exception: pass
    check("5 the installed game starts once (title)", title, f"{time.time() - t0:.0f} s")
    inst = [x for x in os.listdir(f"{I}/mods/.installed")] if os.path.isdir(f"{I}/mods/.installed") else []
    check("5 the starter installed the dropped zip (non-OEM names)", os.path.exists(f"{I}/mods/uni-mod/mod.json") and any("模组" in x for x in inst), str(inst))
    try: w.cmd({"cmd": "quit"}, timeout=5)
    except Exception: pass
    time.sleep(3)
    for pid, c in w.dsmp_pids():
        if "USERDIR" in c:
            try: os.kill(pid, signal.SIGKILL)
            except Exception: pass
    time.sleep(5)
    put(f"{I}/game/player_note.txt"); put(f"{I}/mods/my-own-mod/mod.json", "{}"); put(f"{I}/profiles/modded/data/Saves/w1.json", "{}")
    r = wine([f"C:\\USERDIR_José\\{NAME}\\Uninstall.exe", "/S"], timeout=600); time.sleep(10)
    check("5 installed files removed", not os.path.exists(f"{I}/game/DungeonSettlers.exe") and not os.path.exists(f"{I}/{NAME}.exe")
          and not os.path.exists(f"{I}/mods/multiplayer") and not os.path.exists(f"{I}/Uninstall.exe"))
    check("5 the game copy is gone (ours top to bottom), a player mod, saves and the folder around stay", not os.path.exists(f"{I}/game") and os.path.exists(f"{I}/mods/my-own-mod/mod.json")
          and os.path.exists(f"{I}/profiles/modded/data/Saves/w1.json") and os.path.exists(f"{U}/notes.txt") and os.path.exists(f"{U}/photos/a.jpg"))
    left = sorted(os.path.relpath(os.path.join(d, f), I) for d, _, fs in os.walk(I) for f in fs)
    left = [x for x in left if not x.startswith("profiles/")]
    check("5 nothing else left (outside profiles)", left == ["dsmodded_install.txt", "mods/my-own-mod/mod.json"], str(left[:12]))
    # 6 reinstall into the same folder after the uninstall (saves and a player mod left there; the marker removed the way
    #   the v0.1.6 uninstaller did): accepted, the saves and the player mod are still there
    if os.path.exists(f"{I}/dsmodded_install.txt"): os.replace(f"{I}/dsmodded_install.txt", f"{old}/dsmodded_install.txt.removed")
    r = wine([w.winpath(setup), "/S", "/D=C:\\USERDIR_José"], timeout=900)
    check("6 reinstall into the folder our uninstall left: accepted, saves + player mod kept", os.path.exists(f"{I}/game/DungeonSettlers.exe")
          and os.path.exists(f"{I}/profiles/modded/data/Saves/w1.json") and os.path.exists(f"{I}/mods/my-own-mod/mod.json"), f"rc {r.returncode}")
finally:
    for pid, c in w.dsmp_pids():
        if "USERDIR" in c or "FULLDIR" in c:
            try: os.kill(pid, signal.SIGKILL)
            except Exception: pass
    subprocess.run([sys.executable, WL, "release", "uninstfix"])
print(f"RESULT {sum(res)}/{len(res)} pass")
