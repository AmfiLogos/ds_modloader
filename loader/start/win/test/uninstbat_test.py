#!/usr/bin/env python3
"""uninstbat_test.py <kit dir>: the zip kit's uninstall.bat removes everything the install and the starter put there,
itself and the install folder included, from any start folder; the player's saves and own mods stay (CrossOver bottle
DSMP: wine, not real Windows; one wine copy: ~/ds_multiplayer/rig/wine_lease.py). uninstall.bat only knows the default
folder (%LOCALAPPDATA%\\Dungeon Settlers Modded): the rig's install there goes aside first and comes back at the end.
  main   install.bat; files the starter makes later (download cache, retired\\ old mod folder); no saves. uninstall.bat
         started from the install folder: the folder is gone
  other  install.bat; a save in profiles\\, a player's own mod in mods\\, the player's BepInEx / MelonLoader settings in
         game\\; install.bat again (a reinstall must not record the player's mod as ours). The kit's uninstall.bat
         (another folder): only the save, the player's mod, the settings (in keep_settings\\) and the marker stay;
         ROOT\\uninstall.bat is gone
Prints the folder's contents after each uninstall."""
import os, subprocess, sys, time
H = os.path.expanduser("~")
sys.path.insert(0, f"{H}/ds_multiplayer/win"); import wloader as w
C, DRIVE = w.C, w.DRIVE
WL = f"{H}/ds_multiplayer/rig/wine_lease.py"
GAME = "C:\\SteamLibrary\\steamapps\\common\\Dungeon Settlers"
NAME = "Dungeon Settlers Modded"
LOCAL = f"{DRIVE}/users/crossover/AppData/Local"
R = f"{LOCAL}/{NAME}"
WR = f"C:\\users\\crossover\\AppData\\Local\\{NAME}"
res = []
def check(n, ok, info=""): res.append(ok); print(("PASS" if ok else "FAIL"), n, info, flush=True)
def wine(args, timeout=900):
    return subprocess.run(["nice", "-n", "19", f"{C}/wine", "--bottle", "DSMP"] + args, capture_output=True,
                          env=dict(os.environ, DSMODDED_NOPAUSE="1"), timeout=timeout)
def tree(p):
    if not os.path.exists(p): return "(folder gone)"
    out = []
    for d, ds, fs in os.walk(p):
        for x in sorted(fs): out.append(os.path.relpath(os.path.join(d, x), p))
        for x in sorted(ds):
            if not os.listdir(os.path.join(d, x)): out.append(os.path.relpath(os.path.join(d, x), p) + "\\")
    return ", ".join(sorted(out)) or "(empty folder)"
def put(p, s="x\n"): os.makedirs(os.path.dirname(p), exist_ok=True); open(p, "w").write(s)
def install(kit):
    r = wine(["cmd.exe", "/c", w.winpath(f"{kit}/install.bat"), GAME])
    return r.returncode == 0 and os.path.exists(f"{R}/uninstall.bat") and os.path.exists(f"{R}/game/DungeonSettlers.exe")
def starter_files():                                   # what the starter / the auto-update make later
    put(f"{R}/cache/BepInEx_win_x64_be.788.zip", "zip"); put(f"{R}/retired/mods-multiplayer-20261017-120000/mod.json", "{}")

def main(kit):
    if subprocess.run([sys.executable, WL, "take", "uninstbat", "--pid", str(os.getpid()), "--wait", "540"]).returncode:
        print("wine busy / locked after 9 min"); return 3
    old = f"{LOCAL}/old_uninstbat/{int(time.time())}"; os.makedirs(old)
    had = os.path.exists(R)
    if had: os.rename(R, f"{old}/rig-install")            # the rig's install, put back at the end
    try: return run(kit, old)
    finally:
        if os.path.exists(R): os.rename(R, f"{old}/after-test")
        if had: os.rename(f"{old}/rig-install", R)
        subprocess.run([sys.executable, WL, "release", "uninstbat"])

def run(kit, old):
    if not w.bottle_silent(): return 4
    check("main: install", install(kit)); starter_files()
    r = wine(["cmd.exe", "/c", f"{WR}\\uninstall.bat"], timeout=300)
    print("   main, after:", tree(R))
    check("main: uninstall.bat from the install folder: everything gone, the folder too", r.returncode == 0 and not os.path.exists(R), f"rc {r.returncode} " + r.stdout.decode("utf-8", "replace").strip()[-300:].replace("\r\n", " | "))

    check("other: install", install(kit)); starter_files()
    put(f"{R}/profiles/modded/data/Saves/slot1.json", "save"); put(f"{R}/mods/player-own/mod.json", '{"id":"player-own"}')
    put(f"{R}/game/BepInEx/config/BepInEx.cfg", "cfg"); put(f"{R}/game/UserData/MelonPreferences.cfg", "cfg")
    check("other: reinstall over it", install(kit))
    r = wine(["cmd.exe", "/c", w.winpath(f"{kit}/uninstall.bat")], timeout=300)
    t = tree(R); print("   other, after:", t)
    want = ("dsmodded_install.txt, keep_settings\\BepInEx\\config\\BepInEx.cfg, keep_settings\\UserData\\MelonPreferences.cfg, "
            "mods\\player-own\\mod.json, profiles\\modded\\data\\Saves\\slot1.json").replace("\\", os.sep)
    check("other: the kit's uninstall.bat: only the save, the player's mod, the kept settings and the marker stay",
          r.returncode == 0 and t == want, f"rc {r.returncode} " + r.stdout.decode("utf-8", "replace").strip()[-300:].replace("\r\n", " | "))
    print(f"== {sum(res)}/{len(res)} passed", flush=True)
    return 0 if all(res) else 1

if __name__ == "__main__": sys.exit(main(os.path.abspath(sys.argv[1])))
