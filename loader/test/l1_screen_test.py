#!/usr/bin/env python3
"""Test copies never open full screen on the owner's display (owner v13.3: a full-screen test copy switched his Space),
on test copy S (hidden, staged; take a lease first). Uses this worktree's build/.
 1. copy S's own preferences say full screen -> lrig.require_windowed refuses the start (no game process)
 2. the same, with that python check skipped -> dsload sets the domain windowed 960x540 before Unity reads it, and
    DSL_NOTCH=0; the game starts staged and hidden as always
 3. Apply: the new game takes the old window's mode (handoff "fsmode")
The owner's own domains (com.CanOpener.Dungeon-Settlers, .Modded) are byte-identical before and after."""
import json, os, sys, time, subprocess, hashlib, threading, signal
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/screen')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
check, log = T.check, T.log
TGT = 'com.CanOpener.Dungeon-Settlers.mpS'
OWN = [os.path.expanduser(f'~/Library/Preferences/com.CanOpener.Dungeon-Settlers{s}.plist') for s in ('', '.Modded')]


def dw(dom, key, v): subprocess.run(['defaults', 'write', dom, key, '-int', str(v)], check=True)
def dr(dom, key): return subprocess.run(['defaults', 'read', dom, key], capture_output=True, text=True).stdout.strip()
def sha(p):
    try: return hashlib.sha256(open(p, 'rb').read()).hexdigest()
    except OSError: return None


def main():
    for p in T.game_pids(): raise SystemExit(f'copy S already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    V.install()
    own0 = [sha(p) for p in OWN]
    prep = r.prepare_data
    def prepare_then_full(x):                                    # the rig seeds copy S windowed: full screen after that
        prep(x); dw(TGT, 'Screenmanager Fullscreen mode', 1); dw(TGT, 'Screenmanager Resolution Width', 3456); dw(TGT, 'Screenmanager Resolution Height', 2234)
    r.prepare_data = prepare_then_full
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'violations': T.violations, 'checks': T.checks}
    try:
        try: T.start_a(); refused = None
        except SystemExit as e: refused = str(e)
        check('1: full-screen prefs -> the start is refused', refused and 'refused' in refused and not T.game_pids(), refused)
        keep = r.require_windowed; r.require_windowed = lambda x: None
        a = T.start_a(); r.require_windowed = keep
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(2)
        line = [l for l in T.dlog().splitlines() if 'screen: test copy' in l]; S['line'] = line
        check('2: dsload sets the copy windowed 960x540, DSL_NOTCH=0', line and 'windowed 960x540 (mode 3)' in line[0] and 'DSL_NOTCH=0' in line[0], line)
        S['domain'] = {k: dr(TGT, 'Screenmanager ' + k) for k in ('Fullscreen mode', 'Resolution Width', 'Resolution Height')}
        check('2: the domain says windowed 960x540', S['domain'] == {'Fullscreen mode': '3', 'Resolution Width': '960', 'Resolution Height': '540'}, S['domain'])
        T.clear_flags(); t_req = time.time(); T.request({'cmd': 'apply', 'ids': ['multiplayer', 'gcd-cdr']})
        T.wait_swap(a, t_req, 1)
        hand = json.load(open(os.path.join(T.SDIR, 'handoff.json'))); S['fsmode'] = hand.get('fsmode')
        check("3: the new game takes the old window's mode (handoff fsmode)", isinstance(hand.get('fsmode'), int) and hand['fsmode'] >= 0, hand)
        check("the owner's own preferences are unchanged", [sha(p) for p in OWN] == own0)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        r.prepare_data = prep
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        for p in T.game_pids():
            try: r.send(T.X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception: pass
            time.sleep(3)
            if T.alive(p): os.kill(p, signal.SIGKILL)
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
