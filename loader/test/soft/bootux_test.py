#!/usr/bin/env python3
"""Boot screen proof on hidden test copy P (never the owner's game, saves, prefs or screen; notch fix off).
  bootux_test.py <out> [mac|winlike]
  mac      the Mac defaults (fast boot parts on: A's splash over the boot)
  winlike  the Windows defaults of the speed-up (DSL_FASTBOOT=0: the stock Unity splash, no speed-up) + DSL_FB_SPLASH=1
One boot with the default mods; the game's own frame every 0.25 s (DSL_FASTBOOT_SHOTS) from the first loader tick until
1 s after the title. A real break caught: a stretch of more than 1 s where the screen does not change at all (no logo,
no loading animation, no percent) before the title = the "looks bugged" screen the owner saw.
  1 the game's loading screen comes up in the first scene (log)
  2 the first logo is drawn at the stock place and size (960x540: 383,233-577,308, 3 px)
  3 no frame-to-frame freeze longer than 1.2 s between the first frame and the title
Lease: rig.py lease bootux --pid <this pid>."""
import os, re, shutil, subprocess, sys, time, json
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r
from PIL import Image, ImageChops

X = 'P'; I = r.idir(X)
LW = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))      # this worktree's loader/
OUT = os.path.abspath(sys.argv[1]); os.makedirs(OUT, exist_ok=True)
MODE = sys.argv[2] if len(sys.argv) > 2 else 'mac'
RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
res = []
def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, info=''): res.append((name, bool(ok))); log('PASS' if ok else 'FAIL', name, info)

MD = os.path.join(OUT, 'mods'); os.makedirs(MD, exist_ok=True)
SET = ['multiplayer', 'custom-modes', 'gcd-cdr']
for m in SET:
    if not os.path.exists(os.path.join(MD, m)): shutil.copytree(os.path.expanduser(f'~/ds_launcher/loader/build/mods/{m}'), os.path.join(MD, m))
prof = os.path.join(OUT, 'profile.json'); json.dump({'enabled': SET, 'order': SET}, open(prof, 'w'))
ROOT = os.path.join(OUT, 'root'); os.makedirs(os.path.join(ROOT, 'loader'), exist_ok=True)
open(os.path.join(ROOT, 'loader', 'RELEASE'), 'w').write('0.1.13\n')
SHOTS = os.path.join(OUT, 'shots'); os.makedirs(SHOTS, exist_ok=True)

pair = None; t_l = time.time()
while not pair and time.time() - t_l < 1800:
    p = subprocess.run([sys.executable, RIG, 'lease', 'bootux', '--pid', str(os.getpid())], capture_output=True, text=True)
    pair = p.stdout.strip() if p.returncode == 0 else None
    if not pair: time.sleep(15)
log('lease', pair)
try:
    if not pair: raise SystemExit('no lease')
    if os.path.exists(os.path.join(I, 'dsload.log')): os.replace(os.path.join(I, 'dsload.log'), os.path.join(OUT, 'prev_dsload.log'))
    mj = os.path.join(I, 'mp.json')
    if os.path.exists(mj): os.replace(mj, os.path.join(OUT, 'mp.json.aside'))
    os.environ.update({'LRIG_DYLIB': os.path.join(LW, 'build', 'dsload.dylib'), 'DSL_MODS_DIR': MD, 'DSL_PROFILE': prof,
                       'DSML_ROOT': ROOT, 'DSL_UPDATE': '0', 'DSL_FASTBOOT_SHOTS': SHOTS, 'DSL_SHOT_EVERY': '0.25', 'DSL_PREWARM': '0'})
    if MODE == 'winlike': os.environ.update({'DSL_FASTBOOT': '0', 'DSL_FB_SPLASH': '1'})
    os.environ.pop('DSML_SESSION', None)
    t0 = time.time(); r.start(X, window=True, fps=60)
    while time.time() - t0 < 150:
        try: s = r.send(X, {'cmd': 'status'}, timeout=10, connect_wait=0)
        except Exception: time.sleep(0.1); continue
        if s.get('scene') == 1 and not s.get('busy'): break
        time.sleep(0.1)
    time.sleep(7)
    L = open(os.path.join(I, 'dsload.log'), errors='replace').read()
    for l in L.splitlines():
        if re.search(r'soft \+.*(A: |MainFlow.Awake)', l): log('  ', l[13:])
    check('1 the game\'s loading screen comes up in the first scene', 'boot screen: the game\'s loading screen up' in L)
    shots = sorted((float(f[2:-4]), f) for f in os.listdir(SHOTS) if f.endswith('.png'))
    m = re.search(r'soft \+([\d.]+)s: A: boot screen: title ready', L)
    t_title = float(m.group(1)) if m else (shots[-1][0] if shots else 0)
    def load(f): return Image.open(os.path.join(SHOTS, f)).convert('RGB')
    def box(f):                              # above the progress line
        im = Image.open(os.path.join(SHOTS, f)).convert('RGB'); bg = Image.new('RGB', im.size, im.getpixel((5, 5)))
        return im.size, ImageChops.difference(im.crop((0, 0, 960, 460)), bg.crop((0, 0, 960, 460))).point(lambda v: 255 if v > 24 else 0).getbbox()
    def stock(b): return b[0] == (960, 540) and b[1] and all(abs(x - y) <= 3 for x, y in zip(b[1], (383, 233, 577, 308)))
    logo = [f for t, f in shots if stock(box(f))]
    check('2 the first logo is drawn', logo, f'{len(logo)} logo frames of {len(shots)}')
    worst, run0, prev = None, None, None
    for t, f in shots:
        if t > t_title: break
        im = load(f)
        if prev is not None:
            d = ImageChops.difference(im, prev[1]).getbbox()
            if d is None:
                run0 = run0 if run0 is not None else prev[0]
                if t - run0 > (worst[0] if worst else 0): worst = (t - run0, run0, t)
            else: run0 = None
        prev = (t, im)
    check('3 no frozen screen longer than 1.2 s before the title', not worst or worst[0] <= 1.2,
          f'longest unchanged stretch {worst[0]:.2f} s ({worst[1]:.2f}-{worst[2]:.2f})' if worst else 'every frame changed')
    log('title at', t_title, 's; frames', len(shots))
finally:
    try: r.stop(X)
    except Exception as e: log('stop:', e)
    if os.path.exists(os.path.join(I, 'dsload.log')): shutil.copy2(os.path.join(I, 'dsload.log'), OUT)
    if pair: subprocess.run([sys.executable, RIG, 'release', pair, 'bootux'], capture_output=True)
fails = [n for n, ok in res if not ok]
print(f'\n{len(res) - len(fails)} PASS {len(fails)} FAIL; {OUT}')
sys.exit(1 if fails else 0)
