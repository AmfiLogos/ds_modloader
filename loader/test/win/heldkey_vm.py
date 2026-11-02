#!/usr/bin/env python3
"""The held-key reader hook on the Windows VM (kit 0.1.32, lane nvdrag f4692ef: src/dsl_ui.c h_kp_next on
KeyInputDispatcher.<GetInputOnKeyPressed>d__19.MoveNext, Windows RVA 0xa67850; loader/NOTES.md "KEPT FROM THE GAME"; Mac
proof: test/panelsize_test.py world_cases, Smaller = W) through winrig (ds_multiplayer win/winbot, WINBOT_DIR), the VM
running visibly with the agent up, on the INSTALLED kit.
Test parts (built here with llvm-mingw, never shipped): the module hk-probe (test/mods_src/hk-probe: one big centred kit
panel with "-" / "+" in a world, and a state file with the panel size, the camera place and the mouse) and vm_input.exe
(test/win/vm_input.c: a mouse move without a click and a HELD key, which the agent's click / key ops cannot do).
Smaller = W (Unity KeyCode 119, no Ctrl) is written into the profile's Mods\\ui_places.tsv as the Mods window's key
capture keeps it (the file is moved aside and put back after). Flow: title, the test save loaded, the probe panel, then
real input in the guest (before each case S is held 1.5 s with the mouse off the panels: the save starts with the camera at
its top edge, where W cannot move it, so a W that leaks to the game would not show):
  1. the mouse OFF every panel, W held 1 s: the camera moves, the panel size stays;
  2. the mouse OVER the panel, W held 1 s: the panel is one step (10 %) smaller and the camera does not move;
  3. the key let go, the mouse off again, W held 1 s: the camera moves again.
Each case also records the keys Unity saw held mid-press (119 = W must be among them, else the input is wrong).
The real breaks this catches on Windows:
  - the held-key reader hook is not installed (class / MoveNext / <>2__current not found, the hook refused): a size key that
    the game also uses (W) sizes the panel AND moves the camera;
  - the hook keeps the key too long or always (the camera no longer moves with W off the panels, or after the press that
    sized a panel was let go);
  - the size key does not size a panel under the mouse on Windows.
The loader under test is the INSTALLED one; --baselib PATH puts another loader over it for the run (and back after):
the v0.1.31 baselib (no held-key reader hook) fails a) and c) on the VM (camera moved under the panel; r2 2026-11-02).
  python3 heldkey_vm.py [--out DIR] [--baselib PATH]
Exit 0 = every check passed. Last line: RESULT n/m checks."""
import argparse, json, math, os, re, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__)); T_DIR = os.path.dirname(HERE)
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402

CC = os.path.expanduser('~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang')
SDK = os.path.join(os.path.dirname(T_DIR), 'sdk')
SRC = os.path.join(T_DIR, 'mods_src', 'hk-probe')
SAVE = os.path.expanduser('~/DungeonSettlers_SaveBackups/2026-10-11-1827/Saves/ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')
STAMP = time.strftime('%Y%m%d-%H%M%S')
TO_DEL = W.WORK + r'\to_delete\heldkey_vm_' + STAMP
HK = W.WORK + r'\heldkey'
STATE = HK + r'\state.json'
INPUT = HK + r'\vm_input.exe'
PLACES = W.DATA + r'\Mods\ui_places.tsv'
MD = W.ROOT + r'\mods\hk-probe'
SC_W, SC_S, KC_W = 0x11, 0x1f, 119     # scan codes (the physical keys: Unity reads those, the VM layout is AZERTY-like) + Unity's KeyCode.W
checks = []
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:700]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def build(out):
    d = os.path.join(out, 'build'); os.makedirs(d, exist_ok=True)
    fl = ['nice', '-n', '19', CC, '-ffile-prefix-map=' + os.path.expanduser('~') + '=/src', '-O2', '-Wall']
    subprocess.run(fl + ['-I' + SDK, '-shared', '-o', os.path.join(d, 'hk-probe.dll'), os.path.join(SRC, 'hk_probe.c'), '-static', '-static-libgcc', '-lpthread'], check=True)
    subprocess.run(fl + ['-o', os.path.join(d, 'vm_input.exe'), os.path.join(HERE, 'vm_input.c'), '-static'], check=True)
    return d


def state(w, after_frame=-1, timeout=10):
    """the probe's state (a newer frame than after_frame); a half-written file is read again"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            s = json.loads(w.text(STATE) or '')
            if s.get('frame', -1) > after_frame: return s
        except ValueError: pass
        time.sleep(0.2)
    raise W.GameError('no probe state')


def settled(w, timeout=6):
    """the camera at rest (the game eases it after a key): two reads 0.5 s apart at the same place"""
    s = state(w); t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(0.5); s2 = state(w, s['frame'])
        if math.hypot(s2['cam'][0] - s['cam'][0], s2['cam'][1] - s['cam'][1]) < 0.001: return s2
        s = s2
    return s


def moved(a, b): return math.hypot(b['cam'][0] - a['cam'][0], b['cam'][1] - a['cam'][1])


def vin(w, args):
    r = w.a('run', exe=w.expand(INPUT), args=args, wait_ms=15000, capture=True)
    return (r.get('out') or '').strip()


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='heldkey_vm_out'); ap.add_argument('--baselib')
    a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    b = build(out)
    w = W.Win(); w.up(); w.kill_all()
    kept_places = False
    bl = W.GAME + r'\baselib.dll'; kept_bl = TO_DEL + r'\baselib.dll'; swapped = False
    try:
        if a.baselib:
            w.move(bl, kept_bl); swapped = True
            w.upload(os.path.abspath(os.path.expanduser(a.baselib)), bl)
            log('loader under test:', a.baselib)
        for f in ('hk-probe.dll',): w.put(os.path.join(b, f), MD + '\\' + f)
        w.put(os.path.join(SRC, 'mod.json'), MD + r'\mod.json')
        w.put(os.path.join(b, 'vm_input.exe'), INPUT)
        if w.exists(PLACES): w.move(PLACES, TO_DEL + r'\ui_places.tsv'); kept_places = True
        w.write(PLACES, f'kit@size_key_minus\t{KC_W}.0\t0.0\n')                 # Smaller = W, no Ctrl (as the key capture keeps it)
        if w.exists(STATE): w.move(STATE, TO_DEL + r'\state_old.json')
        name = os.path.basename(SAVE)
        w.prepare(lang=0, consent='no', active=['multiplayer', 'hk-probe'], settings={}, saves={name: SAVE})
        w.start(env={'DSL_CLOUD': '0', 'HKPROBE_OUT': w.expand(STATE)})
        dl = lambda: w.text(W.PROF + r'\dsload.log') or ''
        L = dl()
        hk = [l for l in L.splitlines() if 'held-key reader' in l]
        check('a) the held-key reader hook is installed (KeyInputDispatcher <GetInputOnKeyPressed>d__N.MoveNext)',
              any(re.search(r'held-key reader\): hooked 0x[0-9a-f]+', l) for l in hk) and not any('refused' in l or 'not found' in l for l in hk), hk)
        log('hook:', hk[:1])
        w.cmd(cmd='load_save', file=w.expand(W.DATA + '\\Saves\\' + name), det=0)
        t0 = time.time(); ok = False
        while time.time() - t0 < 240:
            if ((w.mpm() or {}).get('box') or {}).get('on'): w.box('confirm'); time.sleep(0.5); continue
            s = w.loaded()
            if s and s.get('tick', -1) > 0: ok = True; break
            time.sleep(0.5)
        if not ok: raise W.GameError('the test save did not load (240 s)')
        s = w.wait(lambda: (lambda s: s if s.get('panel') and s.get('cam', [0, 0, 0])[2] == 1 else None)(state(w)), 'the probe panel + camera', 60, 1)
        sw, sh = s['screen']
        log('world loaded; probe', s)
        win = next((x for p in w.procs() if p['pid'] == w.pid for x in p.get('windows', []) if x.get('w', 0) > 600), None)
        if not win: raise W.GameError('no game window')
        w.a('click', x=int(win['x'] + win['w'] / 2), y=int(win['y'] + 12)); time.sleep(0.5)     # the title bar: the keyboard
        cx, cy = int(win['x'] + win['w'] / 2), int(win['y'] + win['h'] / 2)               # calibrate: desktop -> game pixels
        vin(w, f'move {cx} {cy}'); time.sleep(0.5); m = state(w, state(w)['frame'])['mouse']
        left, bottom = cx - m[0], cy + m[1]
        to = lambda u, v: (int(round(left + u)), int(round(bottom - v)))               # game pixels (y up) -> desktop
        over, off = to(sw / 2, sh / 2), to(40, sh - 40)
        log('window', win, 'screen', sw, sh, 'mouse at the centre', m, 'over', over, 'off', off)

        def hold_at(pt, tag):
            vin(w, f'move {off[0]} {off[1]}'); time.sleep(0.4)
            vin(w, f'hold {SC_S} 1500')                                              # room above the camera (see the top)
            vin(w, f'move {pt[0]} {pt[1]}'); time.sleep(0.6)
            s0 = settled(w); n0 = len(dl()); r = {}
            t = threading.Thread(target=lambda: r.update(o=vin(w, f'hold {SC_W} 1000'))); t.start(); time.sleep(0.6)
            mid = state(w); t.join(); s1 = settled(w)
            lg = dl()[n0:]
            s0['held'] = mid.get('held')
            log(tag, 'mouse', s0['mouse'], 'cam', s0['cam'][:2], '->', s1['cam'][:2], 'f', s0['f'], '->', s1['f'], 'held', mid.get('held'), r.get('o'),
                [l[13:] for l in lg.splitlines() if 'size keys' in l][:3])
            return s0, s1, lg
        s0, s1, lg = hold_at(off, 'off 1')
        check('b) W held with the mouse off every panel moves the camera, the panel size stays',
              KC_W in (s0['held'] or []) and moved(s0, s1) > 0.05 and abs(s1['f'] - s0['f']) < 0.001,
              {'cam': [s0['cam'], s1['cam']], 'f': [s0['f'], s1['f']], 'mouse': s0['mouse'], 'held': s0['held']})
        s2, s3, lg = hold_at(over, 'over')
        check('c) W held over the kit panel: one step smaller (10 %), the camera does not move, the key kept from the game',
              KC_W in (s2['held'] or []) and abs(s2['f'] - s3['f'] - 0.1) < 0.001 and moved(s2, s3) < 0.01 and f'key {KC_W} kept from the game' in lg,
              {'f': [s2['f'], s3['f']], 'cam': [s2['cam'], s3['cam']], 'mouse': s2['mouse'], 'held': s2['held'], 'log': [l[13:] for l in lg.splitlines() if 'size keys' in l][:3]})
        s4, s5, lg = hold_at(off, 'off 2')
        check('d) the key let go, the mouse off again: W held moves the camera again, the panel size stays',
              KC_W in (s4['held'] or []) and moved(s4, s5) > 0.05 and abs(s5['f'] - s4['f']) < 0.001,
              {'cam': [s4['cam'], s5['cam']], 'f': [s4['f'], s5['f']], 'held': s4['held']})
        try: w.gshot(os.path.join(out, 'heldkey.png'), 'heldkey')
        except Exception as e: log('shot:', e)
        L = dl()
        bad = [l for l in L.splitlines() if any(x in l for x in ('Crash!!!', 'Access violation', 'OFF by the crash guard'))]
        check('no crash line in dsload.log', not bad, bad[:3])
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try:
            t = w.text(W.PROF + r'\dsload.log')
            if t: open(os.path.join(out, 'dsload.log'), 'w').write(t)
            w.logs(out)
        except Exception as e: log('logs:', e)
        w.stop(); w.kill_all()
        for src, dst in ((MD + r'\hk-probe.dll', TO_DEL + r'\hk-probe.dll'), (MD + r'\mod.json', TO_DEL + r'\hk-probe.mod.json'),
                         (PLACES, TO_DEL + r'\ui_places_test.tsv')):                # moved away, never deleted
            try:
                if w.exists(src): w.move(src, dst)
            except Exception as e: log('move:', e)
        if swapped:
            try: w.move(bl, TO_DEL + r'\baselib_test.dll'); w.move(kept_bl, bl); log('installed baselib.dll put back')
            except Exception as e: log('BASELIB NOT PUT BACK:', e)
        if kept_places:
            try: w.move(TO_DEL + r'\ui_places.tsv', PLACES); log('ui_places.tsv put back')
            except Exception as e: log('UI_PLACES NOT PUT BACK:', e)
        json.dump(checks, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', f'{len(checks) - len(bad)}/{len(checks)} checks')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
