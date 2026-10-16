#!/usr/bin/env python3
"""The modded game opens with the player's own window mode and size (owner v13.3: "the game you start starts in full
screen"), on test copy S (hidden, staged, -screen-* arguments as always; take a lease first).
Source = a TEST preferences domain (com.CanOpener.Dungeon-Settlers.DSMLscreen, windowed 960x540) in place of the plain
game's; target = copy S's own domain, set to full screen first. The plain game's preferences are only read (bytes
compared before / after). After an Apply, the new game shows A's window mode (handoff fsmode)."""
import json, os, sys, time, subprocess, hashlib, threading, signal, shutil
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/screen')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
check, log = T.check, T.log
SRC = 'com.CanOpener.Dungeon-Settlers.DSMLscreen'; TGT = 'com.CanOpener.Dungeon-Settlers.mpS'
PLAIN = os.path.expanduser('~/Library/Preferences/com.CanOpener.Dungeon-Settlers.plist')


def dw(dom, key, v): subprocess.run(['defaults', 'write', dom, key, '-int', str(v)], check=True)
def dr(dom, key):
    p = subprocess.run(['defaults', 'read', dom, key], capture_output=True, text=True); return p.stdout.strip()
def sha(p):
    try: return hashlib.sha256(open(p, 'rb').read()).hexdigest()
    except OSError: return None


def main():
    for p in T.game_pids(): raise SystemExit(f'copy S already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    V.install()
    plain0 = sha(PLAIN)
    for k, v in (('Screenmanager Fullscreen mode', 3), ('Screenmanager Resolution Width', 960), ('Screenmanager Resolution Height', 540),
                 ('Screenmanager Window Position X', 0), ('Screenmanager Window Position Y', 66)): dw(SRC, k, v)
    prep = r.prepare_data
    def prepare_then_full(x):                                    # the rig seeds copy S windowed: make it full screen after that
        prep(x)
        dw(TGT, 'Screenmanager Fullscreen mode', 1); dw(TGT, 'Screenmanager Resolution Width', 3456); dw(TGT, 'Screenmanager Resolution Height', 2234)
    r.prepare_data = prepare_then_full
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'violations': T.violations, 'checks': T.checks}
    try:
        a = T.start_a(extra_env={'DSL_SCREEN_FROM': SRC})
        T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(2)
        L = T.dlog(); line = [l for l in L.splitlines() if 'screen: window settings' in l]
        S['line'] = line
        check('start: window settings copied from the source domain', line and 'Fullscreen mode=3' in line[0] and 'Resolution Width=960' in line[0], line)
        S['target_after'] = {k: dr(TGT, 'Screenmanager ' + k) for k in ('Fullscreen mode', 'Resolution Width', 'Resolution Height')}
        check('target domain now windowed 960x540', S['target_after'] == {'Fullscreen mode': '3', 'Resolution Width': '960', 'Resolution Height': '540'}, S['target_after'])
        T.clear_flags(); t_req = time.time(); T.request({'cmd': 'apply', 'ids': ['multiplayer', 'gcd-cdr']})
        dt, b = T.wait_swap(a, t_req, 1)
        hand = json.load(open(os.path.join(T.SDIR, 'handoff.json')))
        LB = open(os.path.join(T.I, 'dsload.log'), errors='replace').read()
        S['fsmode'] = hand.get('fsmode')
        check("handoff carries A's window mode", isinstance(hand.get('fsmode'), int) and hand['fsmode'] >= 0, hand)
        check('B does not copy the settings again (it takes the window of A)', 'screen: window settings' not in LB)
        check("the plain game's preferences are unchanged", sha(PLAIN) == plain0)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
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
