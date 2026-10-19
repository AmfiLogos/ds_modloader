#!/usr/bin/env python3
"""Windows copy installed under a user folder with non-ASCII letters ("José Søren"), in the CrossOver bottle DSMP
(wine, not real Windows; graphics and audio off; holds rig/wine.lock through ~/ds_multiplayer/rig/wine_lease.py for the whole run).
  install   the kit's install.bat with DSMODDED_ROOT = ...\\AppData\\Local\\José Søren\\Dungeon Settlers Modded
  zip       a mod zip with a non-ASCII name (gcd-cdr) dropped into <root>\\mods: the starter unpacks it
  start     starter, -batchmode: title, save folder = <root>\\profiles\\modded\\data
  soft      soft restart at the title (DSL_SOFT_TEST apply request) to a set with the new mod on: B shown, mod loaded in B
  save      a fresh start loads a modded-only world and saves it (save_file request): the file is under <root>\\profiles\\modded\\data\\Saves
            (since v0.1.13 the starter copies no saves and a game-folder world saves back to LocalLow, an ASCII path in the
            bottle: the test puts the seed save in the modded folder itself, and its world must not be in the game folder)
usage: nonascii_test.py <kit dir> <mod folder>   (kit of build_win.sh with MODS=<multiplayer module>; mod folder = a
Windows build of gcd-cdr, e.g. build_mods.sh output win/gcd-cdr)"""
import json, os, re, shutil, signal, socket, subprocess, sys, time, zipfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'test'))
from ctl_token import token_line as ctl_token_line   # noqa: E402  loader/test/ctl_token.py: token first line
H = os.path.expanduser("~")
C = f"{H}/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin"
LOCAL = f"{H}/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/users/crossover/AppData/Local"
NAME = "José Søren"
ROOT = f"{LOCAL}/{NAME}/Dungeon Settlers Modded"
WROOT = f"C:\\users\\crossover\\AppData\\Local\\{NAME}\\Dungeon Settlers Modded"
P = f"{ROOT}/profiles/modded"
STEAMGAME = "C:\\SteamLibrary\\steamapps\\common\\Dungeon Settlers"
SAVE = "ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0"
SEED = os.path.expanduser(f"~/DungeonSettlers_SaveBackups/2026-10-11-1827/Saves/{SAVE}.json")
GAME_SAVES = f"{H}/Library/Application Support/CrossOver/Bottles/DSMP/drive_c/users/crossover/AppData/LocalLow/CanOpener/Dungeon Settlers/Saves"
PORT = 47851
res = {}

def log(*a): print(time.strftime("%H:%M:%S"), *a, flush=True)
def check(n, ok, info=""): res[n] = bool(ok); log("PASS" if ok else "FAIL", n, info)
def read(p):
    try: return open(p, encoding="utf-8", errors="replace").read()
    except OSError: return ""
def wait(pred, t):
    t0 = time.time()
    while time.time() - t0 < t:
        v = pred()
        if v: return v
        time.sleep(2)
    return None
def ours():                                          # this test's processes (game, starter)
    out = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True, text=True).stdout
    return [int(l.split()[0]) for l in out.splitlines() if NAME in l and ("DungeonSettlers.exe" in l or "Dungeon Settlers Modded.exe" in l)]
def kill_ours():
    for p in ours():
        try: os.kill(p, signal.SIGKILL)
        except OSError: pass
def cmd(obj, port=PORT, timeout=10):
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    s.sendall(ctl_token_line(P) + json.dumps(obj).encode() + b"\n"); buf = b""
    while not buf.endswith(b"\n"):
        d = s.recv(1 << 20)
        if not d: break
        buf += d
    s.close(); return json.loads(buf)
def request(obj):                                    # the loader's test requests (DSL_SOFT_TEST=1): <profile>/softrestart
    os.makedirs(f"{P}/softrestart", exist_ok=True)
    open(f"{P}/softrestart/reg_request.json.tmp", "w", encoding="utf-8").write(json.dumps(obj))
    os.replace(f"{P}/softrestart/reg_request.json.tmp", f"{P}/softrestart/reg_request.json")
def status(port=PORT):
    try: return cmd({"cmd": "status"}, port)
    except Exception: return {}

def main(kit, mod):
    lease = os.path.expanduser("~/ds_multiplayer/rig/wine_lease.py")              # the only way to take rig/wine.lock
    if subprocess.run(["python3", lease, "take", "winstart", "--pid", str(os.getpid())]).returncode: log("wine busy or locked"); return 2
    try: return run(kit, mod)
    finally:
        kill_ours()
        subprocess.run(["python3", lease, "release", "winstart"])

def run(kit, mod):
    base = f"{LOCAL}/{NAME}"
    if os.path.isdir(base):                          # never deleted: moved aside
        os.makedirs(f"{LOCAL}/old_nonascii", exist_ok=True); os.replace(base, f"{LOCAL}/old_nonascii/{int(time.time())}")
    env = dict(os.environ, DSMODDED_NOPAUSE="1", DSMODDED_ROOT=WROOT)
    r = subprocess.run(["nice", "-n", "19", f"{C}/wine", "--bottle", "DSMP", "--cx-app", "cmd.exe", "/c",
                        "Z:" + os.path.abspath(kit).replace("/", "\\") + "\\install.bat", STEAMGAME], env=env, capture_output=True, text=True)
    check("install under " + NAME, os.path.isfile(f"{ROOT}/game/baselib.dll") and os.path.isfile(f"{ROOT}/Dungeon Settlers Modded.exe"), r.stdout.strip()[-120:])
    if not res["install under " + NAME]: return 1
    src = mod
    ver = json.load(open(f"{src}/mod.json"))["version"]
    with zipfile.ZipFile(f"{ROOT}/mods/gcd-cdr Søren édition {ver}.zip", "w") as z:     # any name: the id is in mod.json
        for d, _, fs in os.walk(src):
            for f in fs: z.write(f"{d}/{f}", "gcd-cdr/" + os.path.relpath(f"{d}/{f}", src))
    env = {k: v for k, v in os.environ.items() if not k.startswith(("DSRIG_", "DSL_", "DSML_"))}
    env.update(DSRIG_PORT=str(PORT), DSL_NO_ALERT="1", DSL_SOFT_TEST="1", DSL_SOFT_NOACTIVATE="1", WINEDLLOVERRIDES="winhttp=n,b")
    t0 = time.time()
    subprocess.run(["nice", "-n", "19", f"{C}/wine", "--bottle", "DSMP", "--no-wait", WROOT + "\\Dungeon Settlers Modded.exe",
                    "-batchmode", "-nographics"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    st = wait(lambda: (lambda s: s if s.get("boot_ready") else None)(status()), 150)
    a_log = read(f"{P}/dsload.log")
    check("start: game at the title", st)
    check("start: save folder under " + NAME, f"save folder: C:/users/crossover/AppData/Local/{NAME}/Dungeon Settlers Modded/profiles/modded/data" in a_log)
    check("zip: unpacked into mods, not .failed", os.path.isfile(f"{ROOT}/mods/gcd-cdr/gcd-cdr.dll") and os.path.isdir(f"{ROOT}/mods/.installed")
          and not os.path.isdir(f"{ROOT}/mods/.failed"), str(sorted(os.listdir(f"{ROOT}/mods"))))
    if not st: return 1
    os.makedirs(f"{P}/data/Mods", exist_ok=True)
    open(f"{P}/data/Mods/soft_p1.json", "w").write('{"enabled":["multiplayer","gcd-cdr"],"order":["multiplayer","gcd-cdr"]}\n')
    os.makedirs(f"{P}/softrestart", exist_ok=True)
    req = {"profile": WROOT + "\\profiles\\modded\\data\\Mods\\soft_p1.json"}
    open(f"{P}/softrestart/apply_request.json", "w", encoding="utf-8").write(json.dumps(req, ensure_ascii=False))
    shown = wait(lambda: "A: B shown" in read(f"{P}/dsload.log") or "FAILED" in read(f"{P}/dsload.log"), 180)
    a_log = read(f"{P}/dsload.log")
    m = re.search(r"B started, pid (\d+)", a_log)
    b_path = f"{P}/dsload.{m.group(1)}.log" if m else ""
    time.sleep(8)
    b_log = read(b_path) if b_path else ""
    check("soft: B started and shown", shown and "A: B shown" in a_log and "B: SHOWN" in b_log, m.group(0) if m else a_log[-300:])
    check("soft: B has the profile path under " + NAME, NAME in b_log and re.search(r"gcd-cdr [\d.]+: loaded", b_log))
    kill_ours(); time.sleep(5)                        # B cannot take A's control port: a fresh start for the save
    port = PORT
    saves = f"{P}/data/Saves"
    os.makedirs(saves, exist_ok=True); shutil.copy2(SEED, f"{saves}/{SAVE}.json")   # a modded-only world under the accented path
    if os.path.isfile(f"{GAME_SAVES}/nonascii_test.json"):    # left by this test before v0.1.13 support: moved aside, never deleted
        os.makedirs(f"{LOCAL}/old_nonascii", exist_ok=True); os.replace(f"{GAME_SAVES}/nonascii_test.json", f"{LOCAL}/old_nonascii/game_nonascii_test_{int(time.time())}.json")
    clash = [f for f in os.listdir(GAME_SAVES) if SAVE.split("_")[-2] in f] if os.path.isdir(GAME_SAVES) else []
    if clash: log("the bottle's game folder holds this world (it would save there, not under " + NAME + "):", clash); return 1
    t0 = time.time()
    subprocess.run(["nice", "-n", "19", f"{C}/wine", "--bottle", "DSMP", "--no-wait", WROOT + "\\Dungeon Settlers Modded.exe",
                    "-batchmode", "-nographics"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ok = wait(lambda: (lambda s: s if s.get("boot_ready") and s.get("scene") == 1 and not s.get("busy") else None)(status(port)), 150)
    if ok:
        log("load", cmd({"cmd": "load_save", "name": SAVE}, port))
        wait(lambda: (lambda s: s if s.get("scene") == 2 and not s.get("busy") else None)(status(port)), 150)
        time.sleep(5)
        request({"cmd": "save_file", "name": "nonascii_test"})   # the game's own manual save (CampaignSaveLoader.Save)
    def written():
        if not os.path.isdir(saves): return None
        new = [f for f in os.listdir(saves) if f == "nonascii_test.json" and os.path.getmtime(f"{saves}/{f}") > t0]
        return new or None
    new = wait(written, 90)
    c_log = read(f"{P}/dsload.log")
    check("save: a save file written under " + NAME, new and "saving nonascii_test (world f816ebc6-195c-432c-9fff-754b75ee3738) to the modded folder" in c_log, str(new))
    wj = read(f"{P}/data/Mods/worlds.json")
    check("worlds.json: read at the load, written with the world", "could not be read" not in c_log and "not readable" not in c_log and re.search(r"world check: .*allowed", c_log)
          and "f816ebc6-195c-432c-9fff-754b75ee3738" in wj, (re.findall(r"world check: [^\n]*", c_log) or [""])[0][:120])
    return 0 if all(res.values()) else 1

if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(3))     # timeout: the lock is still released (finally)
    rc = main(sys.argv[1], sys.argv[2]) if len(sys.argv) > 2 else 2
    log("RESULTS", sum(res.values()), "/", len(res)); sys.exit(rc)
