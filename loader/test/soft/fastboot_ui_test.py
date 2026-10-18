#!/usr/bin/env python3
"""Fast boot UI proof on hidden test copy P (never the owner's game, saves, prefs or screen).
  fastboot_ui_test.py <out>
One fast boot with the default mods + the uikit-demo driver (real mouse events through the stock input path):
  1 boot time: process start -> title ready (scene 1, not busy) about 3 s
  2 the splash logos (the loader's overlay since v0.1.13) are drawn over the boot: a frame (every 0.25 s, DSL_FASTBOOT_SHOTS) shows the first logo at exactly
    the place and size of a stock boot's frame of this copy (960x540: logo box 383,233-577,308, measured 2026-10-17)
  3 the Auto-update switch first shows after the boot is done (never under the loading screen)
  4 hover over the switch: no tooltip; a real click flips it, a second click flips it back
  5 in a world the switch is hidden
Lease: rig.py lease fastboot-ui --pid <this pid>."""
import json, os, re, shutil, subprocess, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import lrig as r
from uikit_test import Drv

X = 'P'; I = r.idir(X)
LW = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))      # this worktree's loader/
OUT = os.path.abspath(sys.argv[1]); os.makedirs(OUT, exist_ok=True)
RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
res = []
def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, info=''): res.append((name, bool(ok))); log('PASS' if ok else 'FAIL', name, info)
def ts(l): h, m, s = l[:12].split(':'); return int(h) * 3600 + int(m) * 60 + float(s)

# mods: the default set (main checkout's build, read only) + the uikit-demo driver built here
MD = os.path.join(OUT, 'mods'); os.makedirs(MD, exist_ok=True)
for m in ('multiplayer', 'custom-modes', 'gcd-cdr'):
    if not os.path.exists(os.path.join(MD, m)): shutil.copytree(os.path.expanduser(f'~/ds_launcher/loader/build/mods/{m}'), os.path.join(MD, m))
d = os.path.join(MD, 'uikit-demo'); os.makedirs(d, exist_ok=True)
subprocess.run(f'cd "{LW}" && nice -n 19 clang -arch arm64 -O2 -Wall -Isdk -dynamiclib -framework CoreFoundation -framework CoreGraphics -lobjc '
               f'-install_name @rpath/uikit-demo.dylib -o "{d}/uikit-demo.dylib" test/mods_src/uikit-demo/uikit_demo.c && codesign -f -s - "{d}/uikit-demo.dylib"',
               shell=True, check=True, capture_output=True)
shutil.copy2(os.path.join(LW, 'test/mods_src/uikit-demo/mod.json'), d)
png = os.path.join(LW, 'modules/modmenu/slogel.png')
if os.path.exists(png): shutil.copy2(png, d)
SET = ['multiplayer', 'custom-modes', 'gcd-cdr', 'uikit-demo']
prof = os.path.join(OUT, 'profile.json'); json.dump({'enabled': SET, 'order': SET}, open(prof, 'w'))
ROOT = os.path.join(OUT, 'root'); os.makedirs(os.path.join(ROOT, 'loader'), exist_ok=True)
os.makedirs(os.path.join(ROOT, 'crash'), exist_ok=True); open(os.path.join(ROOT, 'crash', 'consent.json'), 'w').write('{"consent":"no"}\n')   # answered: the crash-report question (v0.1.15) would take the clicks
open(os.path.join(ROOT, 'loader', 'RELEASE'), 'w').write('0.1.8\n')
SHOTS = os.path.join(OUT, 'shots'); os.makedirs(SHOTS, exist_ok=True)
DRV = os.path.join(OUT, 'drv'); os.makedirs(os.path.join(DRV, 'cap'), exist_ok=True)

pair = None; t_l = time.time()
while not pair and time.time() - t_l < 1800:
    p = subprocess.run([sys.executable, RIG, 'lease', 'fastboot-ui', '--pid', str(os.getpid())], capture_output=True, text=True)
    pair = p.stdout.strip() if p.returncode == 0 else None
    if not pair: time.sleep(15)
log('lease', pair)
try:
    if not pair: raise SystemExit('no lease')
    for f in ('dsload.log',):
        if os.path.exists(os.path.join(I, f)): os.replace(os.path.join(I, f), os.path.join(OUT, 'prev_' + f))
    mj = os.path.join(I, 'mp.json')
    if os.path.exists(mj): os.replace(mj, os.path.join(OUT, 'mp.json.aside'))
    os.environ.update({'LRIG_DYLIB': os.path.join(LW, 'build', 'dsload.dylib'), 'DSL_MODS_DIR': MD, 'DSL_PROFILE': prof,
                       'DSML_ROOT': ROOT, 'DSL_UPDATE': '0', 'DSL_FASTBOOT_SHOTS': SHOTS, 'DSL_SHOT_EVERY': '0.25', 'DSL_UIKIT_TEST': DRV})
    os.environ.pop('DSML_SESSION', None)
    t0 = time.time(); r.start(X, window=True, fps=60); title = None
    while time.time() - t0 < 120:
        try: s = r.send(X, {'cmd': 'status'}, timeout=10, connect_wait=0)
        except Exception: time.sleep(0.1); continue
        if s.get('scene') == 1 and not s.get('busy'): title = time.time() - t0; break
        time.sleep(0.1)
    L = open(os.path.join(I, 'dsload.log'), errors='replace').read()
    m = re.search(r'soft \+([\d.]+)s: A: boot done', L)
    check('1 boot: process start -> title ready about 3 s', m and float(m.group(1)) < 4.5, f'in game {m.group(1) if m else "?"} s, seen by the test after {title and round(title, 2)} s')
    time.sleep(2)
    L = open(os.path.join(I, 'dsload.log'), errors='replace').read()
    shots = sorted(f for f in os.listdir(SHOTS) if f.endswith('.png'))
    from PIL import Image, ImageChops
    def box(p):
        im = Image.open(p).convert('RGB'); sz = im.size; im = im.crop((0, 0, sz[0], 460))   # above the boot progress line (v0.1.13)
        bg = Image.new('RGB', im.size, im.getpixel((5, 5)))
        return sz, ImageChops.difference(im, bg).point(lambda v: 255 if v > 24 else 0).getbbox()
    boxes = {f: box(os.path.join(SHOTS, f)) for f in shots}
    hit = [f for f, b in boxes.items() if b == ((960, 540), (383, 233, 577, 308))]
    check('2 splash logos drawn over the boot: the first logo at the stock place and size', hit,
          f'{len(shots)} frames, logo frames {hit[:3]}; {next((l[13:] for l in L.splitlines() if "ui: splash logos shown" in l), "no splash line")}')
    for _ in range(100):                     # the switch waits for the idle title (the splash fade-out ends ~3 s after boot done)
        if 'update: title corner shown' in L: break
        time.sleep(0.1); L = open(os.path.join(I, 'dsload.log'), errors='replace').read()
    lb = [l for l in L.splitlines() if 'A: boot done' in l]; lc = [l for l in L.splitlines() if 'update: title corner shown' in l]
    check('3 the Auto-update switch first shows after the boot is done', lb and lc and ts(lc[0]) >= ts(lb[0]), f'boot done {lb[0][:12] if lb else "-"}, corner {lc[0][:12] if lc else "never"}')
    D = Drv(DRV)
    st = D.state(); w = st.get('w', {}).get('update', {})
    a, st = D.act('hover', target='update', wait=1.5)
    check('4a hover over the switch: no tooltip', st.get('tip', {}).get('shown') == 0, f"tip {st.get('tip')} under {st.get('ptr_under')}")
    before = st.get('update_on')
    a, st = D.act('click', target='update', wait=0.8); after1 = st.get('update_on')
    a, st = D.act('click', target='update', wait=0.8); after2 = st.get('update_on')
    check('4b a real click flips the switch, a second click flips it back', before in (0, 1) and after1 == 1 - before and after2 == before,
          f'{before} -> {after1} -> {after2} at {a.get("at")}')
    work = os.path.join(OUT, 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, os.path.basename(r.SAVE)); shutil.copy2(r.SAVE, src)
    r.send(X, {'cmd': 'load_save', 'file': src})
    r.wait_for(X, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('tick', -1) > 0, 'world', timeout=300, every=0.5)
    st = D.state(); w = st.get('w', {}).get('update', {})
    check('5 in a world the switch is hidden', w.get('on', -1) != 1, str(w))
finally:
    try: r.stop(X)
    except Exception as e: log('stop:', e)
    if os.path.exists(os.path.join(I, 'dsload.log')): shutil.copy2(os.path.join(I, 'dsload.log'), OUT)
    if pair: subprocess.run([sys.executable, RIG, 'release', pair, 'fastboot-ui'], capture_output=True)
fails = [n for n, ok in res if not ok]
print(f'\n{len(res) - len(fails)} PASS {len(fails)} FAIL; {OUT}')
sys.exit(1 if fails else 0)
