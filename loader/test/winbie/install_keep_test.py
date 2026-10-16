#!/usr/bin/env python3
"""TEST ONLY: install audit P2-7. install.bat into a separate test root inside the DSMP bottle (DSMODDED_ROOT), then a
game-build change (dsmodded_build.txt says another build) makes install.bat replace the game copy: the player's
BepInEx\\config and UserData files must be there again afterwards, and keep_settings gone. Takes the wine lock.
  install_keep_test.py <kit dir>"""
import os, re, shutil, subprocess, sys, time
H = os.path.expanduser("~"); KIT = os.path.abspath(sys.argv[1])
C = f"{H}/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
DRIVE = f"{H}/Library/Application Support/CrossOver/Bottles/DSMP/drive_c"
ROOTW, ROOT = "C:\\xm_install_test", f"{DRIVE}/xm_install_test"
STEAMGAME = "C:\\SteamLibrary\\steamapps\\common\\Dungeon Settlers"; LOCK = f"{H}/ds_multiplayer/rig/wine.lock"
res = []
def check(n, ok, info=""): res.append(bool(ok)); print("PASS" if ok else "FAIL", n, info, flush=True)
def install():
    r = subprocess.run(["nice", "-n", "19", C, "--bottle", "DSMP", "--cx-app", "cmd.exe", "/c", "Z:" + KIT.replace("/", "\\") + "\\install.bat", STEAMGAME],
                       env=dict(os.environ, DSMODDED_NOPAUSE="1", DSMODDED_ROOT=ROOTW, WINEDEBUG="-all"), capture_output=True, text=True, timeout=1800)
    return r.stdout + r.stderr
busy = [l for l in subprocess.run(["ps", "-axo", "etime=,comm="], capture_output=True, text=True).stdout.splitlines()
        if re.search(r"wine|\.exe$", l, re.I) and not re.match(r"\s*\d+-", l)]
if busy: sys.exit("wine busy: " + "; ".join(busy))
os.mkdir(LOCK); open(f"{LOCK}/owner", "w").write("crossmods-audit\n")
try:
    if os.path.exists(ROOT): shutil.move(ROOT, ROOT + f".old{int(time.time())}")
    o = install(); g = f"{ROOT}/game"
    check("first install made the game copy", os.path.exists(f"{g}/dsmodded_build.txt"), o.strip().splitlines()[-1][:100] if o.strip() else "")
    os.makedirs(f"{g}/BepInEx/config", exist_ok=True); os.makedirs(f"{g}/UserData", exist_ok=True)
    open(f"{g}/BepInEx/config/BepInEx.cfg", "w").write("player setting 1\n")
    open(f"{g}/UserData/MelonPreferences.cfg", "w").write("player setting 2\n")
    open(f"{g}/dsmodded_build.txt", "w").write("1\n")           # another game build: install.bat replaces the copy
    open(f"{g}/xm_marker_old_copy.txt", "w").write("x")
    o = install()
    check("build change: the game copy was replaced", not os.path.exists(f"{g}/xm_marker_old_copy.txt") and os.path.exists(f"{g}/GameAssembly.dll"))
    a = open(f"{g}/BepInEx/config/BepInEx.cfg").read() if os.path.exists(f"{g}/BepInEx/config/BepInEx.cfg") else None
    b = open(f"{g}/UserData/MelonPreferences.cfg").read() if os.path.exists(f"{g}/UserData/MelonPreferences.cfg") else None
    check("P2-7 BepInEx and MelonLoader settings kept", a == "player setting 1\n" and b == "player setting 2\n", f"{a!r} {b!r}")
    check("keep_settings folder removed after the restore", not os.path.exists(f"{ROOT}/keep_settings"))
finally:
    os.makedirs(f"{H}/ds_multiplayer/rig/old", exist_ok=True)
    os.replace(f"{LOCK}/owner", f"{H}/ds_multiplayer/rig/old/wine.lock.owner.crossmods-audit.{int(time.time())}"); os.rmdir(LOCK)
    if os.path.exists(ROOT): shutil.move(ROOT, f"{DRIVE}/xm_install_test.done{int(time.time())}")   # throwaway test root, out of the way
print(f"{sum(res)} PASS {len(res) - sum(res)} FAIL")
sys.exit(0 if all(res) else 1)
