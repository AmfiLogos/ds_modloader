#!/usr/bin/env python3
"""Windows launcher end to end in the CrossOver bottle DSMP (hidden tests only; never "DS Windows Test").

  tools/win_smoke.py [exe folder]     default target/x86_64-pc-windows-gnu/release (+ src-tauri/seed)

Fake Steam library C:\\dsml_winlauncher\\steam (APFS clone of the bottle's stock game + the build's appmanifest),
fresh launcher folder C:\\dsml_winlauncher\\root-<time>. Steps, all through the real exe with DSML_SMOKE (no window):
status (bundled list installed, multiplayer ON, Steam's game found) -> play (base copy + play copy + game started
with -batchmode -nographics) -> the multiplayer DLL answers on TCP 127.0.0.1:47899 -> quit -> checks: Steam's
folder unchanged, base copy stock, play copy has the proxy + baselib_orig.dll, saves go to the launcher profile.
Rules: one wine at a time machine-wide (waits up to 10 min for other wine processes), nice 19, every DSMP process
started here is killed by PID at the end. Result: test_env/win/result.json.
"""
import hashlib, json, os, shutil, socket, subprocess, sys, time
H = os.path.expanduser("~")
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
C = f"{H}/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin"
BOTTLE = "DSMP"
DRIVE = f"{H}/Library/Application Support/CrossOver/Bottles/{BOTTLE}/drive_c"
T = f"{DRIVE}/dsml_winlauncher"
STOCK = f"{DRIVE}/SteamLibrary/steamapps/common/Dungeon Settlers"
ACF = f"{H}/ds_multiplayer/win/25846389/appmanifest_2798330.acf"
PORT = 47899
OUT = f"{HERE}/test_env/win"

def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()

def tree(d):
    return {os.path.relpath(os.path.join(r, f), d): sha(os.path.join(r, f)) for r, _, fs in os.walk(d) for f in fs}

def wine_busy():
    """Another wine at work: a CrossOver `wine` command or a program (not an idle Windows service under
    C:\\windows, not Edge's updater) in any bottle. Idle services left by earlier runs do not count."""
    out = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True, text=True).stdout
    for l in out.splitlines():
        pid, cmd = int(l.split()[0]), l.split(None, 1)[1]
        if pid == os.getpid(): continue
        if "/CrossOver/bin/wine " in cmd and "python" not in cmd.lower(): return True
        c = cmd.lower()
        if c[1:3] == ":\\" and not c.startswith("c:\\windows\\") and "edgeupdate" not in c: return True
    return False

def dsmp_pids():
    out = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True, text=True).stdout
    res = []
    for l in out.splitlines():
        if "wine" in l.lower() or ".exe" in l.lower():
            pid = int(l.split()[0])
            lo = subprocess.run(["lsof", "-p", str(pid)], capture_output=True, text=True).stdout
            if f"/Bottles/{BOTTLE}/" in lo: res.append(pid)
    return res

def wine(args, env, timeout=600, wait=True):
    e = dict(os.environ, WINEDEBUG="-all", **env)
    e.pop("DSRIG_RANGES", None)
    cmd = ["nice", "-n", "19", f"{C}/wine", "--bottle", BOTTLE] + ([] if wait else ["--no-wait"]) + args
    return subprocess.run(cmd, env=e, capture_output=True, text=True, timeout=timeout)

def ask(obj, timeout=30):
    s = socket.create_connection(("127.0.0.1", PORT), timeout=timeout)
    s.sendall(json.dumps(obj).encode() + b"\n")
    buf = b""
    while not buf.endswith(b"\n"):
        d = s.recv(1 << 20)
        if not d: break
        buf += d
    s.close()
    return json.loads(buf)

def main():
    exe_dir = sys.argv[1] if len(sys.argv) > 1 else f"{HERE}/target/x86_64-pc-windows-gnu/release"
    os.makedirs(OUT, exist_ok=True)
    t0 = time.time()
    while wine_busy():
        if time.time() - t0 > 600: sys.exit("another wine runs for 10 min: stop (one wine machine-wide)")
        time.sleep(10)
    # app folder = what the installer puts on disk: exe, WebView2Loader.dll, seed\
    app = f"{T}/app"
    if os.path.exists(app): os.rename(app, f"{T}/app-old-{int(time.time())}")
    os.makedirs(app)
    for f in ("ds-mod-launcher.exe", "WebView2Loader.dll"): shutil.copy2(f"{exe_dir}/{f}", app)
    shutil.copytree(f"{HERE}/src-tauri/seed", f"{app}/seed")
    lib = f"{T}/steam"
    game = f"{lib}/steamapps/common/Dungeon Settlers"
    if not os.path.exists(game):
        os.makedirs(os.path.dirname(game), exist_ok=True)
        subprocess.run(["cp", "-c", "-R", STOCK, game], check=True)
    shutil.copy2(ACF, f"{lib}/steamapps/appmanifest_2798330.acf")
    steam_before = tree(game)
    stamp = time.strftime("%H%M%S")
    root_w = f"C:\\dsml_winlauncher\\root-{stamp}"
    root = f"{T}/root-{stamp}"
    env = {"DSML_ROOT": root_w, "DSML_STEAM_ROOTS": "C:\\dsml_winlauncher\\steam", "DSRIG_PORT": str(PORT)}
    exe = "C:\\dsml_winlauncher\\app\\ds-mod-launcher.exe"
    res = {"root": root, "steps": {}}

    r = wine([exe], dict(env, DSML_SMOKE="status"))
    st = json.loads(r.stdout)
    mp = [m for m in st["mods"] if m["id"] == "multiplayer"]
    res["steps"]["status"] = ok = (st["platform"] == "win-x64" and st["steam"]["found"] and st["index_seq"] is not None
                                   and mp and mp[0]["enabled"] and st["decision"]["plan"]["kind"] == "setup")
    if not ok: res["status_out"] = st

    if not subprocess.run(["pgrep", "-f", "dsmp_guard.py"], capture_output=True).stdout:
        subprocess.Popen(["python3", f"{H}/ds_multiplayer/win/tools/dsmp_guard.py", "1800"], start_new_session=True,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t1 = time.time()
    # the launcher exits after starting the game; wine may keep waiting for the game, so read its stdout file
    e = dict(os.environ, WINEDEBUG="-all", **env, DSML_SMOKE="play", DSML_GAME_ARGS="-batchmode -nographics")
    e.pop("DSRIG_RANGES", None)
    so = open(f"{OUT}/play.out", "w")
    subprocess.Popen(["nice", "-n", "19", f"{C}/wine", "--bottle", BOTTLE, exe], env=e, stdout=so, stderr=subprocess.STDOUT,
                     start_new_session=True)
    played = None
    while time.time() - t1 < 900 and played is None:
        time.sleep(3)
        for l in open(f"{OUT}/play.out", errors="replace"):
            if l.startswith("{"):
                try: played = json.loads(l)
                except Exception: pass
    res["play_seconds"] = round(time.time() - t1, 1)
    if played is None: res["play_err"] = open(f"{OUT}/play.out", errors="replace").read()[-2000:]
    res["steps"]["play"] = bool(played and played.get("build") == "25846389")
    res["played"] = played

    answer = None
    for _ in range(90):
        try:
            answer = ask({"cmd": "status"}, timeout=10)
            break
        except Exception:
            time.sleep(2)
    res["steps"]["dll_answers"] = answer is not None
    res["dll_status"] = answer
    try: ask({"cmd": "quit"}, timeout=5)
    except Exception: pass
    time.sleep(3)
    for pid in dsmp_pids():
        try: os.kill(pid, 9)
        except Exception: pass

    gd = f"{root}/games/25846389"
    plays = [d for d in os.listdir(gd) if d.startswith("play-") and os.path.isdir(f"{gd}/{d}")] if os.path.isdir(gd) else []
    base = f"{gd}/base"
    checks = {
        "steam_unchanged": tree(game) == steam_before,
        "base_stock_baselib": os.path.exists(f"{base}/baselib.dll") and sha(f"{base}/baselib.dll") == sha(f"{STOCK}/baselib.dll"),
        "base_no_mod_files": not os.path.exists(f"{base}/baselib_orig.dll") and not os.path.exists(f"{base}/det_ranges_win.tsv"),
        "one_play_copy": len(plays) == 1,
    }
    if plays:
        p = f"{gd}/{plays[0]}"
        checks["play_proxy"] = b"baselib proxy" in open(f"{p}/baselib.dll", "rb").read()
        checks["play_orig_is_stock"] = sha(f"{p}/baselib_orig.dll") == sha(f"{STOCK}/baselib.dll")
        checks["play_ranges"] = os.path.exists(f"{p}/det_ranges_win.tsv")
        checks["play_appid"] = open(f"{p}/steam_appid.txt").read().strip() == "2798330"
    prof = f"{root}/profiles/default"
    checks["rig_log_in_profile"] = os.path.exists(f"{prof}/rig.log")
    checks["session_file"] = os.path.exists(f"{prof}/dsml_session.json")
    checks["launcher_log_steam"] = os.path.exists(f"{prof}/launcher.log") and "steam before Play" in open(f"{prof}/launcher.log").read()
    if os.path.exists(f"{prof}/rig.log"): res["rig_log_tail"] = open(f"{prof}/rig.log", errors="replace").read()[-1500:]
    res["checks"] = checks
    res["left_dsmp_pids"] = dsmp_pids()
    res["pass"] = all(res["steps"].values()) and all(checks.values())
    json.dump(res, open(f"{OUT}/result.json", "w"), indent=2)
    print(json.dumps({"pass": res["pass"], "steps": res["steps"], "checks": checks, "play_seconds": res["play_seconds"]}, indent=1))

if __name__ == "__main__":
    main()
