#!/usr/bin/env python3
"""MelonLoader on Mac through the loader (lane melon), on test copy Q (hidden, staged off-screen, same guard as l1_test.py).

1. A without MelonLoader (multiplayer: its dsload hook on MainFlow.Update runs the rig command pump).
   Apply + hello-melon (kind "melon": Harmony postfixes on Assembly-CSharp + an injected MonoBehaviour) with a FRESH
   MelonLoader folder: install at first use (our package zip; DSML_MELON_ZIP = the local zip), then the soft restart with
   the MelonLoader env, 300 s first-start timeout. B: HelloMelon's Harmony + MonoBehaviour lines, the dsload hook fires too,
   first-start marker written, the mod set lists hello-melon with its DLL sha256.
2. From B, Apply with a BepInEx mod and a MelonLoader mod together: refused, with the reason.
3. From B, Apply without hello-melon: C starts without MelonLoader.
usage: l1_ml_test.py <out> <melonloader zip>   (takes a pair lease for the copy)
Output: <out>/ (logs, frames, summary.json)."""
import json, os, shutil, subprocess, sys, threading, time, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import l1_test as t

X = 'Q'; t.X = X; t.I = t.r.idir(X); t.SDIR = os.path.join(t.I, 'softrestart'); t.MODS = os.path.join(t.I, 'data', 'Mods')
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/ml')
ZIP = os.path.expanduser(sys.argv[2]) if len(sys.argv) > 2 else ''
t.OUT = OUT; os.makedirs(OUT, exist_ok=True)
L = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(t.I, 'DungeonSettlers.app'); MD = os.path.join(APP, 'Contents/Resources/mods')
HM = os.path.expanduser('~/ds_multiplayer/ext/melon/plugin/mods/hello-melon')
MDIR = os.path.join(t.I, 'melon_test', time.strftime('%H%M%S'), 'mac-arm64', '0.1.0')
ENV = {'DSML_MELON_DIR': MDIR, 'DSML_MELON_SCRIPT': os.path.join(os.path.dirname(L), 'app', 'melon', 'install_melon.sh'),
       'DSML_MELON_ZIP': ZIP, 'DSML_DSLOAD': os.path.join(APP, 'Contents/Frameworks/dsload.dylib')}


def ml_log():
    try:
        return open(os.path.join(MDIR, 'MelonLoader', 'Latest.log'), errors='replace').read()
    except OSError:
        return ''


def wait_any(s, limit):
    """a line in dsload.log OR dsload.prev.log (the new process renames the old log as soon as it starts)"""
    t0 = time.time()
    while time.time() - t0 < limit:
        for f in ('dsload.log', 'dsload.prev.log'):
            try:
                if s in open(os.path.join(t.I, f), errors='replace').read():
                    return True
            except OSError:
                pass
        time.sleep(0.2)
    return False


def wait_swap(a_pid, t_req, rnd, limit):
    """l1_test.wait_swap with a longer limit (the first MelonLoader start generates its interop assemblies)"""
    shown = os.path.join(t.SDIR, 'handoff.json.shown')
    while time.time() - t_req < limit and not os.path.exists(shown):
        if os.path.exists(os.path.join(t.SDIR, 'handoff.json.failed')):
            raise RuntimeError('A reported failure: ' + open(os.path.join(t.SDIR, 'handoff.json.failed')).read())
        time.sleep(0.2)
    if not os.path.exists(shown):
        raise RuntimeError(f'no swap within {limit} s')
    dt = time.time() - t_req
    while t.alive(a_pid) and time.time() - t_req < dt + 30:
        time.sleep(0.1)
    b = [p for p in t.game_pids() if p != a_pid]
    t.log(f'round {rnd}: on screen {dt:.2f} s after the request; A exited {not t.alive(a_pid)}; B {b}')
    return dt, (b[0] if b else None)


def main():
    os.makedirs(MD, exist_ok=True)
    for m, src in (('hello-melon', HM),):                     # test mods into the copy (moved out again at the end)
        d = os.path.join(MD, m); os.makedirs(d, exist_ok=True)
        for f in os.listdir(src):
            shutil.copy2(os.path.join(src, f), os.path.join(d, f + '.tmp')); os.replace(os.path.join(d, f + '.tmp'), os.path.join(d, f))
    fb = os.path.join(MD, 'fake-bie'); os.makedirs(fb, exist_ok=True)   # a BepInEx mod for the "one .NET host" refusal
    json.dump({'id': 'fake-bie', 'version': '1.0.0', 'kind': 'bepinex', 'scope': 'client', 'platforms': {'any': {'files': {}}}}, open(os.path.join(fb, 'mod.json'), 'w'))
    g = threading.Thread(target=t.guard, daemon=True); g.start()
    summary = {'melon_dir': MDIR}; lease = None
    try:
        t_l = time.time()                                         # the copy counts as a pair: wait for a lease (max 4 machine-wide)
        while not lease and time.time() - t_l < 1200:
            r = subprocess.run([sys.executable, os.path.expanduser('~/ds_multiplayer/rig/rig.py'), 'lease', 'melon', '--pid', str(os.getpid())], capture_output=True, text=True)
            lease = r.stdout.strip().splitlines()[-1] if r.returncode == 0 and r.stdout.strip() else None
            if not lease: time.sleep(15)
        t.log('lease', lease, f'after {time.time() - t_l:.0f} s')
        if not lease: raise RuntimeError('no pair lease in 20 min')
        a = t.start_a(('multiplayer',), ENV)
        t.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(2)
        D = t.dlog()
        t.check('A: multiplayer hooks MainFlow.Update (dsload)', 'MainFlow.Update' in D)
        t.check('A: no MelonLoader yet (state 0)', '0 MelonLoader mod(s) running, MelonLoader state 0' in D, [l for l in D.splitlines() if 'running gameplay' in l])
        t.clear_flags()
        t_req = time.time(); t.request({'cmd': 'apply', 'ids': ['multiplayer', 'hello-melon']})
        t.check('Apply waits for the MelonLoader install', wait_any('reg: Apply waits for the MelonLoader download', 20))
        t.check('install done, Apply runs', wait_any('reg: MelonLoader ready: the waiting Apply runs', 300))
        summary['install_s'] = round(time.time() - t_req, 1)
        dt, b = wait_swap(a, t_req, 1, 360)                          # first start: MelonLoader generates its interop
        summary['apply_to_screen_s'] = round(dt, 1)
        t.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'B title', 120); time.sleep(15)
        D = t.dlog(); ML = ml_log()
        t.check('B: MelonLoader mod running', '1 MelonLoader mod(s) running' in D, [l for l in D.splitlines() if 'running gameplay' in l])
        t.check('B: MelonLoader support module loaded', 'Support Module Loaded' in ML, ML[-300:])
        t.check('B: HelloMelon Harmony patched (2)', ML.count('HELLOMELON PATCHED') == 2, [l for l in ML.splitlines() if 'PATCH' in l][:4])
        t.check('B: Harmony postfix fires', 'HELLOMELON PATCH-HIT' in ML)
        t.check('B: injected MonoBehaviour Update fires', 'HELLOMELON TICK' in ML)
        t.check('B: no MelonLoader errors', '[ERROR]' not in ML, [l for l in ML.splitlines() if 'ERROR' in l][:3])
        f1 = t.status().get('frames', 0); time.sleep(3); f2 = t.status().get('frames', 0)
        t.check('B: dsload hook fires too (multiplayer frame counter moves)', f2 > f1 > 0, (f1, f2))
        t.check('B: first-start marker', bool(glob.glob(os.path.join(MDIR, 'ds_ready_*'))), os.listdir(MDIR))
        ms = json.load(open(os.path.join(t.I, 'dsload_modset.json')))
        hm = [m for m in ms.get('melon', []) if m.get('id') == 'hello-melon']
        sha = json.load(open(os.path.join(HM, 'mod.json')))['platforms']['any']['files']['HelloMelon.dll']
        t.check('B: mod set lists hello-melon with its DLL sha256', hm and sha in json.dumps(hm), ms.get('melon'))
        t.shot('b2_B_title')
        shutil.copy2(os.path.join(MDIR, 'MelonLoader', 'Latest.log'), os.path.join(OUT, 'B_Latest.log'))
        # 2: BepInEx + MelonLoader in one set: refused with the reason
        t.clear_flags(); n0 = len(t.dlog())
        t.request({'cmd': 'apply', 'ids': ['multiplayer', 'hello-melon', 'fake-bie']})
        t.check('mixed set refused with the reason', wait_any('cannot run together', 15), t.dlog()[n0:][-300:])
        # 3: back without MelonLoader
        t.clear_flags()
        t_req = time.time(); t.request({'cmd': 'apply', 'ids': ['multiplayer']})
        dt2, c = wait_swap(b, t_req, 2, 150)
        summary['apply_off_s'] = round(dt2, 1)
        t.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'C title', 60); time.sleep(5)
        D = t.dlog()
        t.check('C: no MelonLoader mod', '0 MelonLoader mod(s) running' in D)
        out = subprocess.run(['vmmap', str(c)], capture_output=True, text=True).stdout
        t.check('C: no MelonLoader in the process', 'MelonLoader.Bootstrap' not in out and out != '', 'vmmap' if not out else 'MelonLoader.Bootstrap mapped')
        f1 = t.status().get('frames', 0); time.sleep(2); t.check('C: multiplayer frame counter moves', t.status().get('frames', 0) > f1)
    except Exception as e:
        t.check('run finished', False, e)
    finally:
        t.stop_guard.set()
        for p in t.game_pids():
            try:
                t.r.send(t.X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception:
                pass
            time.sleep(3)
            if t.alive(p):
                os.kill(p, 9)
        if lease:
            subprocess.run([sys.executable, os.path.expanduser('~/ds_multiplayer/rig/rig.py'), 'release', lease, 'melon'], capture_output=True)
        for f in ('dsload.log', 'dsload.prev.log'):
            if os.path.exists(os.path.join(t.I, f)): shutil.copy2(os.path.join(t.I, f), os.path.join(OUT, f))
        os.makedirs(os.path.join(t.I, 'old'), exist_ok=True)
        for m in ('hello-melon', 'fake-bie'):                 # test modules out of the copy again
            if os.path.exists(os.path.join(MD, m)): os.replace(os.path.join(MD, m), os.path.join(t.I, 'old', '%s.%d' % (m, time.time())))
        summary.update({'checks': t.checks, 'violations': t.violations})
        json.dump(summary, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        t.log(f"{sum(c['ok'] for c in t.checks)}/{len(t.checks)} checks, violations {len(t.violations)}", {k: summary.get(k) for k in ('install_s', 'apply_to_screen_s', 'apply_off_s')})


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in t.checks) or t.violations or not t.checks else 0)   # any FAIL, guard violation or no check: exit 1
