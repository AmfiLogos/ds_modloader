#!/usr/bin/env python3
"""BepInEx on Mac through the loader, on test copy S (hidden, staged off-screen, same guard as l1_test.py).

1. A without BepInEx (multiplayer: its dsload hook on MainFlow.Update runs the rig command pump).
   Apply + samefn-bie (BepInEx plugin, Harmony postfix on the SAME function MainFlow.Update) with a FRESH BepInEx folder:
   download at first use, then the soft restart with the Doorstop env, 300 s first-start timeout, "Preparing mods"
   on A's loading screen (the tip is set, and the loader makes no scene search there: scan guard, NOTES.md
   "Game-view lookups"). B: both the dsload hook and the Harmony postfix fire; first-start marker written.
2. From B, Apply without samefn-bie: C starts without Doorstop.
Output: <out>/ (logs, frames, summary.json)."""
import glob, hashlib, json, os, re, shutil, subprocess, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import l1_test as t

OUT = os.path.abspath(os.path.expanduser(sys.argv[1])) if len(sys.argv) > 1 else os.path.join(t.r.LROOT, 'test_env', 'l1_out', 'bie')
t.OUT = OUT; os.makedirs(OUT, exist_ok=True)
APP = os.path.join(t.I, 'DungeonSettlers.app'); MD = os.path.join(APP, 'Contents/Resources/mods')
# the repo's own installer and the BepInEx version it pins (an old worktree's script pointed at a deleted release)
SCRIPT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'app', 'bepinex', 'install_bepinex.sh')
VER = next(l.split('=', 1)[1].strip() for l in open(SCRIPT) if l.startswith('VER='))
BDIR = os.path.join(t.I, 'bepinex_test', time.strftime('%H%M%S'), 'mac-arm64', VER)
ENV = {'DSML_BEPINEX_DIR': BDIR, 'DSML_BEPINEX_SCRIPT': SCRIPT,
       'DSML_DSLOAD': os.path.join(APP, 'Contents/Frameworks/dsload.dylib')}
LDIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))          # this checkout's loader/
TM = os.path.join(LDIR, 'build', 'test_mods')     # samefn-bie is built here from test/bie/SameFn (was a removed worktree)


def build_samefn():
    """build test mod samefn-bie (SameFn.dll + mod.json kind bepinex) against the pinned BepInEx core; the core comes
    from the repo's own installer into a shared cache (download once, sha256 checked). Rebuilt when the source changes."""
    src = os.path.join(LDIR, 'test', 'bie', 'SameFn'); dst = os.path.join(TM, 'samefn-bie')
    dll = os.path.join(dst, 'SameFn.dll')
    newest = max(os.path.getmtime(os.path.join(src, f)) for f in os.listdir(src) if f.endswith(('.cs', '.csproj')))
    if os.path.exists(dll) and os.path.exists(os.path.join(dst, 'mod.json')) and os.path.getmtime(dll) >= newest:
        return
    core = os.path.join(t.r.LROOT, 'test_env', 'bepinex_core', 'mac-arm64', VER)
    o = subprocess.run(['zsh', SCRIPT, core], capture_output=True, text=True, timeout=900).stdout
    if 'BEPINEX ' not in o:
        raise RuntimeError(f'BepInEx core for the SameFn build: {o.strip()[-300:]}')
    obj = os.path.join(TM, '.samefn_obj'); os.makedirs(dst, exist_ok=True)
    p = subprocess.run(['nice', '-n', '19', os.path.expanduser('~/.dotnet/dotnet'), 'build', os.path.join(src, 'SameFn.csproj'),
                        '-c', 'Release', f'-p:BieDir={core}/BepInEx', f'-p:BaseIntermediateOutputPath={obj}/', '-o', obj + '_out',
                        '-nologo', '-v', 'q'], capture_output=True, text=True, timeout=600)
    if p.returncode:
        raise RuntimeError(f'SameFn build failed: {(p.stdout + p.stderr)[-600:]}')
    shutil.copy2(os.path.join(obj + '_out', 'SameFn.dll'), dll + '.tmp'); os.replace(dll + '.tmp', dll)
    h = hashlib.sha256(open(dll, 'rb').read()).hexdigest()
    json.dump({'id': 'samefn-bie', 'name': 'SameFn test plugin', 'version': '0.2.0', 'kind': 'bepinex', 'scope': 'gameplay',
               'platforms': {'any': {'files': {'SameFn.dll': h}}},
               'description': 'TEST ONLY: Harmony postfix on MainFlow.Update (loader/test/l1_bie_test.py).'},
              open(os.path.join(dst, 'mod.json'), 'w'), indent=1)


def bie_log():
    try:
        return open(os.path.join(BDIR, 'BepInEx', 'LogOutput.log'), errors='replace').read()
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


def count(text, key):
    return sum(1 for l in text.splitlines() if key in l)


def main():
    build_samefn()
    for m in ('samefn-bie',):
        d = os.path.join(MD, m); os.makedirs(d, exist_ok=True)
        for f in os.listdir(os.path.join(TM, m)):
            shutil.copy2(os.path.join(TM, m, f), os.path.join(d, f + '.tmp')); os.replace(os.path.join(d, f + '.tmp'), os.path.join(d, f))
    g = threading.Thread(target=t.guard, daemon=True); g.start()
    summary = {'bepinex_dir': BDIR}
    try:
        a = t.start_a(('multiplayer',), ENV)
        t.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(2)
        L = t.dlog()
        t.check('A: multiplayer hooks MainFlow.Update (dsload)', 'MainFlow.Update' in L and 'refused' not in L.replace('world check: REFUSED', ''))
        t.check('A: no BepInEx yet (state 0)', '0 BepInEx plugin(s) running, BepInEx state 0' in L, [l for l in L.splitlines() if 'running gameplay' in l])
        t.clear_flags()
        t_req = time.time(); t.request({'cmd': 'apply', 'ids': ['multiplayer', 'samefn-bie']})
        t.check('Apply waits for the download', wait_any('reg: Apply waits for the BepInEx download', 20))
        t.check('download done, Apply runs', wait_any('reg: BepInEx ready: the waiting Apply runs', 180))
        summary['download_s'] = round(time.time() - t_req, 1)
        t.check('download box closed', wait_any('ui: notice closed by its owner', 10))
        time.sleep(15); t.shot('b1_A_preparing')                          # A's loading screen while B generates interop
        dt, b = t.wait_swap(a, t_req, 1)
        summary['apply_to_screen_s'] = round(dt, 1)
        t.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'B title', 60); time.sleep(12)
        L = t.dlog(); BL = bie_log()
        AL = open(os.path.join(t.I, 'dsload.prev.log'), errors='replace').read()      # A's log (B renamed it at its start)
        AL = AL[AL.find('reg: Apply: '):] if 'reg: Apply: ' in AL else ''
        tip = re.findall(r'reg: loading tip: view (\S+) loading ui (\S+) tip text (\S+)', AL)
        t.check("A: 'Preparing mods' tip set on A's loading screen", tip and '0x0' not in tip[0], tip[:1])
        sc = [l for l in AL.splitlines() if re.search(r'scan: loading [\d.]+ s: .*\bloader \d', l)]
        t.check('A: no loader scene search on its loading screen (scan guard lines)', 'scan: loading' in AL and not sc, sc[:2])
        t.check('B: BepInEx plugin running', '1 BepInEx plugin(s) running' in L, [l for l in L.splitlines() if 'running gameplay' in l])
        t.check('B: Harmony patched the function', 'SAMEFN harmony patched MainFlow.Update' in BL, BL[-400:])
        t.check('B: Harmony postfix fires', count(BL, 'SAMEFN harmony postfix') >= 2, count(BL, 'SAMEFN harmony postfix'))
        f1 = t.status().get('frames', 0); time.sleep(3); f2 = t.status().get('frames', 0)
        t.check('B: dsload hook fires too (multiplayer frame counter moves)', f2 > f1 > 0, (f1, f2))
        t.check('B: MainFlow.Update hook not refused', 'refused' not in L.replace('world check: REFUSED', ''), [l for l in L.splitlines() if 'refused' in l][:2])
        t.check('B: first-start marker', bool(glob.glob(os.path.join(BDIR, 'ds_ready_*'))), os.listdir(BDIR))
        t.shot('b2_B_title')
        summary['harmony_calls_logged'] = count(BL, 'SAMEFN harmony postfix'); summary['mp_frames'] = (f1, f2)
        shutil.copy2(os.path.join(BDIR, 'BepInEx', 'LogOutput.log'), os.path.join(OUT, 'B_LogOutput.log'))
        # 2: back without BepInEx
        t.clear_flags(); n0 = len(BL)
        t_req = time.time(); t.request({'cmd': 'apply', 'ids': ['multiplayer']})
        dt2, c = t.wait_swap(b, t_req, 2)
        summary['apply_off_s'] = round(dt2, 1)
        t.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'C title', 60); time.sleep(5)
        L = t.dlog()
        t.check('C: no BepInEx plugin', '0 BepInEx plugin(s) running' in L)
        t.check('C: no Doorstop in the process', 'libdoorstop' not in L, [l for l in L.splitlines() if 'doorstop' in l.lower()][:2])
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
        for f in ('dsload.log', 'dsload.prev.log'):
            if os.path.exists(os.path.join(t.I, f)): shutil.copy2(os.path.join(t.I, f), os.path.join(OUT, f))
        os.makedirs(os.path.join(t.I, 'old'), exist_ok=True)
        for m in ('samefn-bie',):                             # test modules out of the copy again
            if os.path.exists(os.path.join(MD, m)): os.replace(os.path.join(MD, m), os.path.join(t.I, 'old', '%s.%d' % (m, time.time())))
        summary.update({'checks': t.checks, 'violations': t.violations})
        json.dump(summary, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        t.log(f"{sum(c['ok'] for c in t.checks)}/{len(t.checks)} checks, violations {len(t.violations)}", {k: summary.get(k) for k in ('download_s', 'apply_to_screen_s', 'apply_off_s')})


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in t.checks) or t.violations or not t.checks else 0)   # any FAIL, guard violation or no check: exit 1
