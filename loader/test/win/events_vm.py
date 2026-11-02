#!/usr/bin/env python3
"""Windows VM run of the loader events (SDK API 7, src/dsl_events.c; Mac proof: test/events_test.py) through winrig
(ds_multiplayer win/winbot, WINBOT_DIR), the VM running visibly with the agent up. The loader under test: --baselib
<baselib.dll> (loader/build/win, zsh loader/start/win/build_win.sh) is put over the installed one for the run (the
installed one is moved to C:\\dsm\\to_delete and put back after; never deleted). The test module ev-probe
(test/mods_src/ev-probe, built here with llvm-mingw, never shipped) logs one EVPROBE line per event; multiplayer is on
for its control port (the test commands).
  python3 events_vm.py --baselib PATH [--out DIR]
Real breaks it catches on Windows: UI_Base.Activate / Deactivate are folded code there (shared with 5 / 3 other
methods): the call site in PopUpViewContainer.ActivateView / DeactivateView is not found (the view hooks fall back to the
records check: the "view hooks all on" check fails) or the return-address filter misses the popup (EVENT-RULE line), or a
folded method called by other code crashes through the hook; scene / world / frame / language events missing.
Flow: title (ev-probe switches the language to German and back), Load list opened (mpm_open_load) and closed (mpm_escape),
the test save loaded, the Esc menu opened (mpm_esc), Quit to title (mpm_quit_title: world closed, scene back to 1).
Checks: the same as events_test.py (parse) in "hooks" mode + no crash line. Exit 0 = every check passed."""
import argparse, json, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); T_DIR = os.path.dirname(HERE)
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot')); sys.path.insert(0, T_DIR)
import winrig as W  # noqa: E402
import events_test as E  # noqa: E402

CC = os.path.expanduser('~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang')
SDK = os.path.join(os.path.dirname(T_DIR), 'sdk')
SRC = os.path.join(T_DIR, 'mods_src', 'ev-probe')
SAVE = os.path.expanduser('~/DungeonSettlers_SaveBackups/2026-10-11-1827/Saves/ElvenExpedition__AutoSave_f816ebc6195c432c9fff754b75ee3738_0.json')
GUID = 'f816ebc6-195c-432c-9fff-754b75ee3738'
STAMP = time.strftime('%Y%m%d-%H%M%S')
TO_DEL = W.WORK + r'\to_delete\events_vm_' + STAMP
checks = []
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:700]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def build(out):
    d = os.path.join(out, 'ev-probe'); os.makedirs(d, exist_ok=True)
    subprocess.run(['nice', '-n', '19', CC, '-ffile-prefix-map=' + os.path.expanduser('~') + '=/src', '-O2', '-Wall', '-I' + SDK,
                    '-shared', '-o', os.path.join(d, 'ev-probe.dll'), os.path.join(SRC, 'ev_probe.c'), '-static', '-static-libgcc', '-lpthread'], check=True)   # clock_gettime (ev-probe slow knob, 2209f33) is in winpthread
    m = json.load(open(os.path.join(SRC, 'mod.json'))); json.dump(m, open(os.path.join(d, 'mod.json'), 'w'), indent=1)
    return d


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='events_vm_out'); ap.add_argument('--baselib', required=True)
    a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    probe = build(out)
    w = W.Win(); w.up(); w.kill_all()
    md = W.ROOT + r'\mods\ev-probe'; bl = W.GAME + r'\baselib.dll'; kept = TO_DEL + r'\baselib.dll'
    swapped = False
    try:
        w.move(bl, kept); swapped = True
        w.upload(os.path.abspath(os.path.expanduser(a.baselib)), bl)
        for f in ('ev-probe.dll', 'mod.json'):
            w.upload(os.path.join(probe, f), md + '\\' + f)
        name = os.path.basename(SAVE)
        w.prepare(lang=0, consent='no', active=['multiplayer', 'ev-probe'], settings={}, saves={name: SAVE})
        w.start(env={'DSL_CLOUD': '0', 'EVPROBE_LANG': '1'})
        dl = lambda: w.text(W.PROF + r'\dsload.log') or ''
        try: w.wait(lambda: 'EVPROBE set-lang' in dl() and '(back)' in dl(), 'the probe\'s language switch and back', 60)
        except W.GameError as e: log(e)
        steps = {}
        steps['open_load'] = (w.cmd({'cmd': 'mpm_open_load'}) or {}).get('ok'); time.sleep(3)
        steps['escape'] = (w.cmd({'cmd': 'mpm_escape'}) or {}).get('ok'); time.sleep(2)
        # the rig reads only expanded paths (win_checklist.load_with_boxes); a stock box during the load is confirmed
        w.cmd(cmd='load_save', file=w.expand(W.DATA + '\\Saves\\' + name), det=0)
        t0 = time.time(); steps['load'] = 'timeout (240 s)'
        while time.time() - t0 < 240:
            if ((w.mpm() or {}).get('box') or {}).get('on'): w.box('confirm'); time.sleep(0.5); continue
            s = w.loaded()
            if s and s.get('tick', -1) > 0: steps['load'] = True; break
            time.sleep(0.5)
        if steps['load'] is True:                                   # the Esc menu exists only in a world
            time.sleep(4)
            steps['esc'] = (w.cmd({'cmd': 'mpm_esc'}) or {}).get('ok') is not False; time.sleep(2)
            steps['quit_title'] = (w.cmd({'cmd': 'mpm_quit_title'}) or {}).get('ok')
            try: w.wait(w.on_title, 'the title again', 120)
            except W.GameError as e: log(e)
        time.sleep(3)
        L = dl()
        open(os.path.join(out, 'dsload.log'), 'w').write(L)
        for n, ok, d in E.parse(L, 'hooks', GUID): check(n, ok, d)
        check('world closed after Quit to title', f'EVPROBE world-closed {GUID}' in L, [l for l in L.splitlines() if 'world-closed' in l][-1:])
        hk = [l for l in L.splitlines() if 'loader events UI_Base.Activate: hooked' in l or 'loader events UI_Base.Deactivate: hooked' in l]
        check('Windows: the folded Activate / Deactivate hooked (call sites found)', len(hk) >= 2, hk)
        check('flow (test commands answered)', all(v is True for v in steps.values()), steps)
        pl = L + (w.text(W.PROF + r'\player.log') or '') + (w.text(W.DATA + r'\ErrorLog.txt') or '')
        bad = [l for l in pl.splitlines() if any(b in l for b in ('Crash!!!', 'Access violation', 'NullReferenceException', 'OFF by the crash guard'))]
        check('no crash / exception line in the logs', not bad, bad[:4])
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: w.logs(out)
        except Exception as e: log('logs:', e)
        w.stop(); w.kill_all()
        for f in ('ev-probe.dll', 'mod.json'):                      # moved away, never deleted
            try:
                if w.exists(md + '\\' + f): w.move(md + '\\' + f, TO_DEL + '\\probe_' + f)
            except Exception as e: log('move:', e)
        if swapped:
            try: w.move(bl, TO_DEL + r'\baselib_test.dll'); w.move(kept, bl); log('installed baselib.dll put back')
            except Exception as e: log('BASELIB NOT PUT BACK:', e)
        json.dump(checks, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', f'{len(checks) - len(bad)}/{len(checks)} checks')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
