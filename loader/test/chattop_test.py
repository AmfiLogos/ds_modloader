#!/usr/bin/env python3
"""TOP RULE across our windows (kit 0.1.30 + Multiplayer; owner: "all of the windows that are under the right click
irrespective of whether it's on top or below they all move at the same time"). Hidden Mac test copy (DSL_TEST_COPY) with
the loader, multiplayer (control socket, chat) and uikit-demo (fake mouse through Unity's Input, as a player's mouse).
The multiplayer chat's open box (read only, no session) and a big movable kit panel over it, both orders:
 1. kit panel on the kit layer (sorting 30900) under the chat (32030): a right-drag on the box moves the chat only;
 2. kit panel raised to its own canvas 32100, above the chat: the same right-drag moves the kit panel only, and the chat
    says it left the press to the window above (drag_yield).
Before the fix both moved (the chat had its own right-drag, outside the kit's choice).
  chattop_test.py [out] [--no-build]     Exit 0 = PASS. Needs the loader build with MP_REPO / MP_PIN of the MP change."""
import json, os, shutil, signal, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
os.environ.setdefault('DSL_TEST_COPY', 'KIT')
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(ARGS[0] if ARGS else os.path.join(os.path.dirname(HERE), 'test_env', 'l1_out', 'chattop')))
sys.argv = [sys.argv[0], OUT] + [a for a in sys.argv[1:] if a.startswith('--')]
import esc_test as E
log, check, ui, st, wait_for = E.log, E.check, E.ui, E.st, E.wait_for


def rect(s, n): return s.get('w', {}).get(n, {}).get('r', [0, 0, 0, 0])
def chat(): return (E.ctl({'cmd': 'mpm_status'}, 10) or {}).get('chat') or {}
def same(a, b, d=1): return all(abs(a[i] - b[i]) <= d for i in range(len(a)))


def drag_at(x, y, dx, dy):
    # a right-drag from the screen point (x, y): the demo's rdrag starts at a target's centre, so use a tiny rect there
    ui('api', op='rdrag_xy', x=x, y=y, w=dx, h=dy); time.sleep(2.0)


def main():
    if E.game_pids(): raise SystemExit(f'copy {E.X} already running')
    if os.getloadavg()[0] > 20: raise SystemExit('load over 20: no test copy now')
    os.makedirs(OUT, exist_ok=True)
    if '--no-build' not in sys.argv: E.build()
    E.install()
    g = threading.Thread(target=E.guard, daemon=True); g.start()
    os.environ['LRIG_SIZE'] = '960x540'
    try:
        E.start()
        wait_for(lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(E.ctl({'cmd': 'status'}, 5)), 240, 1)
        wait_for(lambda: (lambda j: j if j.get('kit') == 1 else None)(st()), 60, 1)
        time.sleep(4)
        a = E.ctl({'cmd': 'load_save', 'name': E.SAVE}, 30); log('load_save', a)       # the chat lives in a world
        t = wait_for(lambda: (lambda s: s if s.get('scene') == 2 and not s.get('busy') else None)(E.ctl({'cmd': 'status'}, 5)), 180, 1)
        check('world loaded', bool(t), t)
        time.sleep(8)
        E.ctl({'cmd': 'mpm_chat', 'local': 'Top rule test line'}, 5); time.sleep(1.0)   # no session: a local line shows the chat
        E.ctl({'cmd': 'mpm_chat', 'open': 1}, 5); time.sleep(1.5)
        c = chat(); check('the chat box is open (read only)', (c.get('box_rect') or [0])[0] == 1, c.get('box_rect'))
        for order, label, top in ((0, 'chat above the kit panel', 'chat'), (32100, 'kit panel above the chat', 'kit')):
            j = ui('api', op='kp', a=order); time.sleep(1.0)
            c0 = chat(); br = c0.get('box_rect') or [0, 0, 0, 0, 0]; k0 = rect(st(), 'ovA')
            cx, cy = (br[1] + br[3]) / 2, (br[2] + br[4]) / 2
            over = br[0] == 1 and k0[0] < cx < k0[2] and k0[1] < cy < k0[3]
            check(f'{label}: the kit panel covers the chat box', j.get('rc') == 0 and over, (j.get('rc'), br, k0))
            if not over: continue
            y0 = c0.get('drag_yield', 0)
            drag_at(cx, cy, 60, 20)
            c1 = chat(); k1 = rect(st(), 'ovA'); b1 = c1.get('box_rect') or [0, 0, 0, 0, 0]
            chat_moved = abs((b1[1] - br[1]) - 60) <= 4 and abs((b1[2] - br[2]) - 20) <= 4
            kit_moved = abs((k1[0] - k0[0]) - 60) <= 4 and abs((k1[1] - k0[1]) - 20) <= 4
            if top == 'chat':
                check(f'{label}: a right-drag on the box moves the chat only', chat_moved and same(k0, k1), (br, b1, k0, k1))
            else:
                check(f'{label}: a right-drag on the box moves the kit panel only, the chat leaves the press to it',
                      kit_moved and same(br[1:], b1[1:]) and c1.get('drag_yield', 0) > y0, (br, b1, k0, k1, y0, c1.get('drag_yield')))
            ui('capture', name=f'top_{top}')
        ui('api', op='ov_off')
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: E.ctl({'cmd': 'quit'}, 5)
        except Exception: pass
        time.sleep(3)
        for p in E.game_pids(): os.kill(p, signal.SIGKILL)
        for f in ('dsload.log', 'player.log'):
            if os.path.exists(os.path.join(E.I, f)): shutil.copy2(os.path.join(E.I, f), os.path.join(OUT, f))
    E.stop.set()
    fails = [c for c in E.checks if not c['ok']]
    json.dump({'checks': E.checks, 'violations': E.violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    bad = len(fails) + (1 if E.violations else 0)
    log(f'RESULT chattop_test: {len(E.checks) - len(fails)} PASS, {bad} FAIL')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
