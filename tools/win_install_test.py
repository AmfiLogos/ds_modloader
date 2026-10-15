#!/usr/bin/env python3
"""Windows installer + uninstaller in the CrossOver bottle DSMP (hidden; never "DS Windows Test").

  tools/win_install_test.py <setup.exe>     use a build made with webviewInstallMode "skip" (the bottle must not
                                            get the WebView2 runtime installed by a test)

Silent install (/S) -> the installed exe (DSML_SMOKE=prepare, real default folder %LOCALAPPDATA%\\DSModLauncher,
fake Steam library from tools/win_smoke.py) makes the game copy -> a marker save in the profile -> silent uninstall
-> checks: app folder gone, games\\ cache\\ gone, profiles\\ (saves) kept, Steam's folder unchanged.
Result: test_env/win/install_result.json. One wine at a time (same wait rule as win_smoke.py).
"""
import json, os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from win_smoke import BOTTLE, C, DRIVE, OUT, T, dsmp_pids, tree, wine_busy

LOCAL = f"{DRIVE}/users/crossover/AppData/Local"
APP = f"{LOCAL}/DS Mod Launcher"
ROOT = f"{LOCAL}/DSModLauncher"

def wine(args, env=None, timeout=600):
    e = dict(os.environ, WINEDEBUG="-all", **(env or {}))
    e.pop("DSRIG_RANGES", None)
    return subprocess.run(["nice", "-n", "19", f"{C}/wine", "--bottle", BOTTLE] + args, env=e, capture_output=True,
                          text=True, timeout=timeout)

def wait_gone(p, s=120):
    t = time.time()
    while os.path.exists(p) and time.time() - t < s: time.sleep(2)
    return not os.path.exists(p)

def main():
    setup = sys.argv[1]
    t0 = time.time()
    while wine_busy():
        if time.time() - t0 > 600: sys.exit("another wine runs for 10 min: stop (one wine machine-wide)")
        time.sleep(10)
    for p in (APP, ROOT):
        if os.path.exists(p): os.rename(p, f"{p}-old-{int(time.time())}")
    os.makedirs(f"{T}/dist", exist_ok=True)
    shutil.copy2(setup, f"{T}/dist/setup.exe")
    game = f"{T}/steam/steamapps/common/Dungeon Settlers"
    if not os.path.exists(game): sys.exit("run tools/win_smoke.py first (it makes the fake Steam library)")
    steam_before = tree(game)
    res = {"checks": {}}
    ck = res["checks"]

    r = wine(["C:\\dsml_winlauncher\\dist\\setup.exe", "/S"])
    ck["installed"] = all(os.path.exists(f"{APP}/{f}") for f in ("ds-mod-launcher.exe", "WebView2Loader.dll", "uninstall.exe", "seed/index.json"))
    res["app_files"] = sorted(os.listdir(APP)) if os.path.exists(APP) else []

    exe = "C:\\users\\crossover\\AppData\\Local\\DS Mod Launcher\\ds-mod-launcher.exe"
    r = wine([exe], {"DSML_SMOKE": "prepare", "DSML_STEAM_ROOTS": "C:\\dsml_winlauncher\\steam"})
    try:
        prep = json.loads(r.stdout)
    except Exception:
        prep = None
        res["prepare_err"] = (r.stdout + r.stderr)[-1500:]
    ck["prepare"] = bool(prep and prep.get("build") == "25846389" and [m["id"] for m in prep["mods"]] == ["multiplayer"])
    ck["copy_made"] = os.path.exists(f"{ROOT}/games/25846389/base/DungeonSettlers.exe")
    ck["cache_made"] = os.path.exists(f"{ROOT}/cache/multiplayer")
    os.makedirs(f"{ROOT}/profiles/default/data/Saves", exist_ok=True)
    with open(f"{ROOT}/profiles/default/data/Saves/test_marker.txt", "w") as f: f.write("keep me\n")

    r = wine(["C:\\users\\crossover\\AppData\\Local\\DS Mod Launcher\\uninstall.exe", "/S"])
    ck["app_removed"] = wait_gone(f"{APP}/ds-mod-launcher.exe")
    ck["games_removed"] = wait_gone(f"{ROOT}/games")
    ck["cache_removed"] = not os.path.exists(f"{ROOT}/cache")
    ck["saves_kept"] = os.path.exists(f"{ROOT}/profiles/default/data/Saves/test_marker.txt")
    ck["state_kept"] = os.path.exists(f"{ROOT}/state.json")
    ck["steam_unchanged"] = tree(game) == steam_before
    for pid in dsmp_pids():
        try: os.kill(pid, 9)
        except Exception: pass
    res["pass"] = all(ck.values())
    json.dump(res, open(f"{OUT}/install_result.json", "w"), indent=2)
    print(json.dumps({"pass": res["pass"], "checks": ck, "app_files": res["app_files"]}, indent=1))

if __name__ == "__main__":
    main()
