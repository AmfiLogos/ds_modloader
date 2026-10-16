#!/usr/bin/env python3
"""TEST ONLY: install audit P2-8 / P2-9 failure paths of start/win/bie_win.c, forced once each, in the CrossOver bottle
DSMP (console exe, no window). Takes ~/ds_multiplayer/rig/wine.lock.
  bie_fail_test.py <BepInEx be.788 win-x64 zip (the pinned one)> [old exe for comparison]
P2-9: damaged zips (stored entry larger than its data, name past the end, local header past the end, truncated) must
      return -1, never crash. P2-8: an unpack that fails half way leaves no completion marker and no winhttp.dll (the game
      starts without BepInEx), and the next start installs it fully; files without the marker count as not installed."""
import io, os, re, shutil, struct, subprocess, sys, time, zipfile
H = os.path.expanduser("~"); HERE = os.path.dirname(os.path.abspath(__file__))
C = f"{H}/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
EXE = os.path.join(HERE, "build", "bie_fail_test.exe"); BIEZIP = sys.argv[1]; OLD = sys.argv[2] if len(sys.argv) > 2 else None
W = os.path.join(HERE, "build", "work"); LOCK = f"{H}/ds_multiplayer/rig/wine.lock"
def wp(p): return "Z:" + p.replace("/", "\\")
res = []
def check(n, ok, info=""): res.append(bool(ok)); print("PASS" if ok else "FAIL", n, info, flush=True)
def run(exe, *a):
    """a crash must end the process, never start the debugger (winedbg --auto would hang the shared bottle):
    winedbg disabled for this process; on a timeout our own exe and any winedbg are killed before the lock goes back"""
    env = dict(os.environ, WINEDEBUG="-all", WINEDLLOVERRIDES="winedbg.exe=d")
    try:
        r = subprocess.run(["nice", "-n", "19", C, "--bottle", "DSMP", wp(exe)] + list(a), capture_output=True, text=True, timeout=120, env=env)
        return r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        subprocess.run(["pkill", "-9", "-f", os.path.basename(exe)]); subprocess.run(["pkill", "-9", "-f", "winedbg"])
        return "TIMEOUT"

def zips():
    b = io.BytesIO(); z = zipfile.ZipFile(b, "w", zipfile.ZIP_STORED); z.writestr("mod/mod.json", '{"id":"x"}' * 50); z.close()
    good = b.getvalue(); out = {"good": good}
    cd = good.rfind(b"PK\x01\x02"); lh = 0
    d = bytearray(good); struct.pack_into("<I", d, cd + 24, 1 << 30); struct.pack_into("<I", d, lh + 22, 1 << 30); out["stored_usz_too_big"] = bytes(d)
    d = bytearray(good); struct.pack_into("<H", d, cd + 28, 60000); out["name_past_end"] = bytes(d)
    d = bytearray(good); struct.pack_into("<I", d, cd + 42, 0x7fffff00); out["local_header_past_end"] = bytes(d)
    d = bytearray(good); eocd = d.rfind(b"PK\x05\x06"); struct.pack_into("<I", d, eocd + 16, 0x7fffff00); out["central_dir_past_end"] = bytes(d)
    out["truncated"] = good[:len(good) // 2] + good[good.rfind(b"PK\x05\x06"):]
    return out

if os.path.exists(W): shutil.move(W, W + f".old{int(time.time())}")
os.makedirs(W)
busy = [l for l in subprocess.run(["ps", "-axo", "etime=,comm="], capture_output=True, text=True).stdout.splitlines()
        if re.search(r"wine|\.exe$", l, re.I) and not re.match(r"\s*\d+-", l)]
if busy: sys.exit("wine busy: " + "; ".join(busy))
os.mkdir(LOCK); open(f"{LOCK}/owner", "w").write("crossmods-audit\n")
try:
    # P2-9
    for name, data in zips().items():
        p = os.path.join(W, name + ".zip"); open(p, "wb").write(data)
        for tag, exe in (("new", EXE), ("old", OLD)):
            if not exe: continue
            o = run(exe, "unzip", wp(p), wp(os.path.join(W, f"out_{tag}_{name}")))
            m = re.search(r"unzip (-?\d+)", o); rc = int(m.group(1)) if m else None
            if tag == "old": print(f"  (old code, {name}: {'rc %d' % rc if m else 'NO ANSWER (crashed)'})"); continue
            check(f"P2-9 {name}: {'files written' if name == 'good' else 'refused, no crash'}", (rc is not None and rc > 0) if name == "good" else rc == -1, f"rc {rc}")
    # P2-8
    root, game = os.path.join(W, "root"), os.path.join(W, "game"); os.makedirs(os.path.join(root, "cache")); os.makedirs(game)
    shutil.copy2(BIEZIP, os.path.join(root, "cache", "BepInEx-be.788-win-x64.zip"))
    open(os.path.join(game, "BepInEx"), "w").write("a FILE where the BepInEx folder must go: the unpack fails half way")
    o = run(EXE, "install", wp(root), wp(game))
    check("P2-8 unpack fails half way: not installed, winhttp.dll removed, no marker, starts without BepInEx",
          "install -1 installed 0" in o and not os.path.exists(os.path.join(game, "winhttp.dll")) and not os.path.exists(os.path.join(game, ".dsm_ok_bepinex")),
          o.strip().splitlines()[-1][:160])
    shutil.move(os.path.join(game, "BepInEx"), os.path.join(W, "blocker"))
    o = run(EXE, "install", wp(root), wp(game))
    check("P2-8 next start: installed fully, marker written", "install 0 installed 1" in o and os.path.exists(os.path.join(game, ".dsm_ok_bepinex")), o.strip().splitlines()[-1][:120])
    shutil.move(os.path.join(game, ".dsm_ok_bepinex"), os.path.join(W, "marker_aside"))
    o = run(EXE, "install", wp(root), wp(game))
    check("P2-8 files without the marker count as not installed: unpacked again", "BepInEx be.788 installed in" in o and "install 0 installed 1" in o, o.strip().splitlines()[-1][:120])
finally:
    for pat in ("bie_fail_test.exe", "winedbg"): subprocess.run(["pkill", "-9", "-f", pat])   # no leftover of ours in the bottle
    os.makedirs(f"{H}/ds_multiplayer/rig/old", exist_ok=True)
    os.replace(f"{LOCK}/owner", f"{H}/ds_multiplayer/rig/old/wine.lock.owner.crossmods-audit.{int(time.time())}"); os.rmdir(LOCK)
print(f"{sum(res)} PASS {len(res) - sum(res)} FAIL")
sys.exit(0 if all(res) else 1)
