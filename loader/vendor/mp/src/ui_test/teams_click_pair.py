#!/usr/bin/env python3
"""World clicks under the open Teams screen + the "Multiplayer not started" banner, on a leased hidden pair.

  teams_click_pair.py [--dylib PATH] [--keep]      (dylib built with -DTEAMS_MOUSE_TEST)

A fake mouse is fed into the game's REAL input path (UnityEngine.Input mouse functions hooked in the test build
only), so the stock MouseInputDispatcher -> RightClickState -> IsPointerOverUI -> InteractGrid / InteractUI chain
decides. With a unit selected, a right-click at the screen centre:
  screen closed -> a move order is captured (ord_stats captured +1 or more), the centre is not over UI;
  screen open   -> nothing is captured, the centre is over UI.
Then det is unlocked on the host (the owner's case: session up, lockstep not running): the screen shows
"multiplayer not started" and a take-click is refused (no claim)."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of
results = T.results


def captured(x):
    return c(x, cmd='ord_stats').get('captured', -1)


def right_click_centre(x, unit):
    T.c(x, cmd='ord_inject', ev='SelectPlayerUnitRequested', **{'i32@16': 0, 'guid@20': unit})
    time.sleep(2)
    time.sleep(6)                                # earlier orders settle first
    before = captured(x)
    tm(x, action='mouse', button=1)
    time.sleep(3)
    s = tm(x)                                    # read while the fake position is still held
    time.sleep(9)
    return before, captured(x), s


def main():
    save = T.arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))
    rig.DYLIB = T.arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/teams_test/dsrig_teams_mouse.dylib'))
    pair = rig.lease('teams', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS.get(pair, 7795)
    log('pair', pair, 'port', port, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Friend'},
                  open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.start(X, fps=10, window=True)   # hidden window: UI is rendered, so graphic raycasts work (batchmode never sets Graphic.depth)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30, window=True)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)

        a = tm(X, action='open')
        free = next(col for col in a['columns'] if col['player'] == -1)['model']
        u, u2 = free[0], free[1]
        tm(X, action='click', guid=u, button=0)                       # host takes the unit it will order
        T.settle(X, Y, lambda a, b: col_of(a, u) == 0 and col_of(b, u) == 0, 'host take')
        tm(X, action='close')
        time.sleep(2)

        b0, b1, s = right_click_centre(X, u)
        check('screen CLOSED: right-click at the centre sends a move order', b1 > b0 and s['over_ui'] == 0,
              f"captured {b0} -> {b1}, over_ui {s['over_ui']} at {s['mouse']}")
        tm(X, action='open'); time.sleep(2)
        tm(X, action='debug')
        o0, o1, s = right_click_centre(X, u)
        check('screen OPEN: the same right-click sends nothing (over UI)', o1 == o0 and s['over_ui'] == 1 and s['open'] == 1,
              f"captured {o0} -> {o1}, over_ui {s['over_ui']} at {s['mouse']}")
        tm(X, action='close'); time.sleep(2)
        c0, c1, s = right_click_centre(X, u)
        check('screen closed again: orders flow again', c1 > c0, f'captured {c0} -> {c1}')

        log(X, 'unlock', c(X, cmd='unlock'))                          # owner case: session up, lockstep not running
        time.sleep(3)
        a = tm(X, action='open')
        time.sleep(1)
        a = tm(X)
        mark = os.path.getsize(os.path.join(rig.idir(X), 'rig.log'))
        tm(X, action='click', guid=u2, button=0)
        time.sleep(4)
        b = tm(X)
        with open(os.path.join(rig.idir(X), 'rig.log'), errors='replace') as f:
            f.seek(mark); tail = f.read()
        check('not running: banner + legend on screen', a['running'] == 0 and 'not running' in a['hint'].lower(), repr(a['hint'][-160:]))
        check('not running: a take-click is refused, nothing claimed', 'refused: session not running' in tail and col_of(b, u2) == -1,
              f"u2 column {col_of(b, u2)}")
        for line in open(os.path.join(rig.idir(X), 'rig.log'), errors='replace'):
            if 'teams: debug' in line or 'modal canvas' in line:
                log(X, 'DBG', line.rstrip()[:900])
        for line in tail.splitlines():
            if 'teams:' in line or 'debug' in line:
                log(X, '|', line[:200])
    finally:
        if '--keep' not in sys.argv:
            for i in pair:
                try:
                    rig.stop(i)
                except Exception as e:
                    log('stop', i, e)
            rig.release(pair)
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
