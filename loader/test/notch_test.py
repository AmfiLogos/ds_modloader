#!/usr/bin/env python3
"""Built-in notch fix (start/mac/dsl_notch_mac.m) on a hidden loader copy Z (window made invisible by the multiplayer
module's DSRIG_HIDEWIN mode: alpha 0, never key, lowest level; the app never activates). Two starts:
  off: DSL_NOTCH=0 -> the game's own window; on: the fix -> borderless window over the whole screen frame.
Proof: dsload.log notch lines + the game's own screenshot (its size = the window's drawable) before / after, and the
owner's real game prefs file unchanged. One rig lease. Needs a screen with a notch (safeAreaInsets.top > 0).
  notch_test.py <mods dir with multiplayer>"""
import json, os, re, shutil, struct, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import lrig
X = "Z"; MODS = os.path.abspath(sys.argv[1])
OUT = os.path.join(L, "test_env", "notch_out", time.strftime("%Y%m%d-%H%M%S")); os.makedirs(OUT, exist_ok=True)
res = []
def log(*a): print("[notch]", *a, flush=True)
def check(n, ok, info=""): res.append(ok); log("PASS" if ok else "FAIL", n, info)
WINB = os.path.join(OUT, "winbounds")
open(WINB + ".m", "w").write('''#import <AppKit/AppKit.h>
int main(int c, char **v) { int pid = atoi(v[1]);
  NSArray *a = CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID));
  for (NSDictionary *d in a) if ([d[(id)kCGWindowOwnerPID] intValue] == pid && [d[(id)kCGWindowLayer] intValue] != 0x7fffffff) {
    NSDictionary *b = d[(id)kCGWindowBounds]; if ([b[@"Width"] doubleValue] < 200) continue;
    printf("%.0fx%.0f at %.0f,%.0f id %d\\n", [b[@"Width"] doubleValue], [b[@"Height"] doubleValue], [b[@"X"] doubleValue], [b[@"Y"] doubleValue], [d[(id)kCGWindowNumber] intValue]); }
}''')
subprocess.run(["clang", "-fobjc-arc", "-framework", "AppKit", WINB + ".m", "-o", WINB], check=True)
def win_bounds(pid): return subprocess.run([WINB, str(pid)], capture_output=True, text=True).stdout.strip()
def png_size(p):
    d = open(p, "rb").read(24); return struct.unpack(">II", d[16:24]) if d[:8] == b"\x89PNG\r\n\x1a\n" else None

prof = os.path.join(OUT, "profile.json"); json.dump({"enabled": ["multiplayer"], "order": ["multiplayer"]}, open(prof, "w"))
r = subprocess.run([sys.executable, os.path.expanduser("~/ds_multiplayer/rig/rig.py"), "lease", "notch", "--pid", str(os.getpid())], capture_output=True, text=True)
PAIR = r.stdout.strip() if r.returncode == 0 else None
if not PAIR: sys.exit("no lease: " + r.stderr.strip())
prefs_before = lrig.check_prefs(restore=False)
shots, wb = {}, {}
try:
    if not os.path.exists(os.path.join(lrig.idir(X), lrig.EXE_REL)):
        subprocess.run(["zsh", os.path.join(HERE, "make_instance.sh"), X], check=True, capture_output=True)
    mj = os.path.join(lrig.idir(X), "mp.json")
    if os.path.exists(mj): os.replace(mj, os.path.join(OUT, "mp.json.aside"))
    for run in ("off", "on"):
        p = os.path.join(lrig.idir(X), "dsload.log")
        if os.path.exists(p): os.replace(p, os.path.join(OUT, f"prev_{run}_dsload.log"))
        os.environ.update({"LRIG_DYLIB": os.path.join(L, "build", "dsload.dylib"), "DSL_MODS_DIR": MODS, "DSL_PROFILE": prof, "DSL_NOTCH": "0" if run == "off" else "1"})
        for k in ("DSML_SESSION", "DSML_DATA"): os.environ.pop(k, None)
        lrig.start(X, window=True)
        try:
            lrig.wait_for(X, lambda s: s.get("scene") == 1 and not s.get("busy"), "title", timeout=600)
            time.sleep(25)                                   # the fix's 0.5 s retries + Unity's window setup
            shot = os.path.join(OUT, f"title_{run}.png"); lrig.send(X, {"cmd": "screenshot", "path": shot})
            time.sleep(3); shots[run] = png_size(shot) if os.path.exists(shot) else None
            wb[run] = win_bounds(lrig.read_pid(X)); log(run, "game window (window server):", wb[run])
            if wb[run]:                                      # the window itself as the window server has it (alpha 0: content blank)
                subprocess.run(["screencapture", "-x", "-o", "-l", wb[run].split()[-1], os.path.join(OUT, f"window_{run}.png")], capture_output=True)
        finally:
            lrig.stop(X)
        L_ = open(p, errors="replace").read() if os.path.exists(p) else ""
        shutil.copy2(p, os.path.join(OUT, f"dsload_{run}.log")) if os.path.exists(p) else None
        lines = [l for l in L_.splitlines() if "notch:" in l]
        log(run, "screenshot", shots[run], "| notch lines:", len(lines))
        if run == "off": check("off (DSL_NOTCH=0): window untouched", not lines)
        else: check("on: game window made borderless over the whole screen", any("borderless over the whole screen" in l for l in lines), (lines or [""])[-1][-110:])
    sz = lambda s: tuple(int(x) for x in s.split()[0].split("x")) if s else None
    a, b = sz(wb.get("off")), sz(wb.get("on"))
    check("window after = the whole screen (notch band included), before = the game's own size", a and b and b != a and b[1] >= a[1], f"before {wb.get('off')} after {wb.get('on')}; game picture {shots}")
finally:
    subprocess.run([sys.executable, os.path.expanduser("~/ds_multiplayer/rig/rig.py"), "release", PAIR, "notch"], capture_output=True)
check("owner's game prefs file unchanged", prefs_before and lrig.check_prefs(restore=False))
print(f"{sum(res)} PASS {len(res) - sum(res)} FAIL; out {OUT}")
sys.exit(0 if all(res) else 1)
