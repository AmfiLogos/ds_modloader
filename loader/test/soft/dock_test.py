#!/usr/bin/env python3
"""RETIRED 2026-10-15 (lead): no test copy may lack LSBackgroundOnly / LSUIElement, so this test refuses to run.
Was: B's start in a copy WITHOUT LSUIElement (like a player's copy): is it ever a Dock (Foreground) app or visible
before the swap? Fake handoff from this script (title case); B is killed once it reports ready. No activation."""
import json, os, re, signal, subprocess, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r
raise SystemExit('dock_test.py is retired: test copies must keep LSBackgroundOnly / LSUIElement (lead 2026-10-15)')
X = 'T'; I = r.idir(X); SD = os.path.join(I, 'softrestart'); os.makedirs(SD, exist_ok=True)
r.prepare_data(X)
H = os.path.join(SD, 'handoff.json')
for x in ('ready', 'go', 'shown', 'failed'):
    if os.path.exists(H + '.' + x): os.replace(H + '.' + x, H + '.' + x + '.old')
json.dump({'v': 1, 'from_pid': os.getpid(), 'profile': os.path.join(I, 'profile2.json'), 'scene': 1, 'save': '', 'save_pending': 0,
           'speed': -1, 'frame': [-20000, -20000, 480, 298], 'apply_at': time.time()}, open(H, 'w'))
env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
       'PATH': '/usr/bin:/bin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'), 'DSL_LOG_DIR': I,
       'DYLD_INSERT_LIBRARIES': os.path.join(I, r.DYLIB_REL), 'DSL_PROFILE': os.path.join(I, 'profile2.json'),
       'DSL_SOFT_HANDOFF': H, 'DSL_SOFT_NOACTIVATE': '1', 'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_NO_ALERT': '1'}
args = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', '960', '-screen-height', '540']
t0 = time.time()
p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'), stderr=subprocess.STDOUT,
                     preexec_fn=lambda: os.nice(5), start_new_session=True)
types = []; bad = []
while time.time() - t0 < 60 and p.poll() is None:
    out = subprocess.run(['lsappinfo', 'list'], capture_output=True, text=True).stdout
    m = re.search(r'pid = %d .*?type="(\w+)"' % p.pid, out)
    ty = m.group(1) if m else None
    if not types or types[-1][1] != ty: types.append((round(time.time() - t0, 2), ty))
    ws = [w for w in r.windows([p.pid]) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000]
    if ws or r.front_pid() == p.pid: bad.append(ws); os.kill(p.pid, signal.SIGKILL); break
    if os.path.exists(H + '.ready'): types.append((round(time.time() - t0, 2), 'READY')); break
    time.sleep(0.3)
if p.poll() is None: os.kill(p.pid, signal.SIGKILL)
print('types over time:', types, 'violations:', bad)
