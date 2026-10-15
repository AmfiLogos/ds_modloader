#!/usr/bin/env python3
"""Teams windows vs the game HUD (lead 2026-10-14), REAL screen frames (backbuffer, every canvas as drawn).

  teams_layer_pair.py [--res1512] [--dylib PATH] [--keep]

Stock popups first (Task Planner, Consume Policy) as the reference, then the Teams window, its lend menu and the
proposal summary: nothing of the HUD (portraits, quest text, Teams button) may draw over them, and the Teams HUD
button hides while one of them is open. Canvas order of every canvas goes to the rig log (ui_teams canvases).
Frames research/ux_shots/v9_layer_<what>_<res>.png"""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, settle = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.settle
results = T.results
OUT = os.path.expanduser('~/ds_multiplayer/research/ux_shots')
RES = '1512' if '--res1512' in sys.argv else '960'


def bb(x, name):
    p = f'{OUT}/v9_layer_{name}_{RES}.png'
    log('frame', p, c(x, cmd='mpm_shot', how='backbuffer', path=p).get('ok'))


def main():
    save = T.arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))
    rig.DYLIB = T.arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/teams_test/dsrig_teams.dylib'))
    pair = rig.lease('teams_layer' + RES, os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS.get(pair, 7795)
    log('pair', pair, 'port', port, 'res', RES, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Friend'},
                  open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        for i_ in (X, Y):                                                # the button at its default place
            f_ = os.path.join(rig.idir(i_), 'data', 'mp_teams_button.json')
            if os.path.exists(f_): os.rename(f_, f_ + '.prev_' + time.strftime('%Y%m%d_%H%M%S'))
        if RES == '1512':
            import uxgame_pair as U
            U.start(X, 1512, 982)
        else:
            rig.start(X, fps=10, window=True)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        if RES == '1512': U.start(Y, 1512, 982)
        else: rig.start(Y, fps=30, window=True)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        a = tm(X)
        free = next(col for col in a['columns'] if col['player'] == -1)['model']
        check('5 free units', len(free) >= 5, f'{len(free)}')
        f = free[:5]
        check('HUD button shown (nothing open)', a['hud_shown'] == 1, str(a['hud_shown']))
        bb(X, 'hud')
        tm(X, action='open'); time.sleep(2); a = tm(X)                  # fresh: no stock popup opened before (portrait bar expanded)
        check('fresh Teams window: the HUD button is hidden', a['open'] and a['hud_shown'] == 0, f"open {a['open']} hud_shown {a['hud_shown']}")
        bb(X, 'teams_fresh'); tm(X, action='close'); time.sleep(2)
        # the reference: the game's own popups
        for ty, nm in ((13, 'stock_taskplanner'), (12, 'stock_consume')):
            tm(X, action='popup', type=ty); time.sleep(3)
            tm(X, action='canvases'); bb(X, nm)
            c(X, cmd='mpm_key', key='escape'); time.sleep(2)
        # the Teams window
        tm(X, action='open'); time.sleep(2)
        a = tm(X)
        check('Teams window open: the Teams HUD button is hidden', a['open'] and a['hud_shown'] == 0, f"open {a['open']} hud_shown {a['hud_shown']}")
        tm(X, action='canvases'); bb(X, 'teams')
        tm(X, action='shot', path=f'{OUT}/v9_layer_teams_camcapture_{RES}.png'); time.sleep(1); bb(X, 'teams_after_camcapture')   # does the test-only camera capture leave the layers changed?
        tm(X, action='click', guid=f[0], button=0); time.sleep(2)
        tm(X, action='chip', guid=f[0]); time.sleep(1.5)
        a = tm(X)
        check('lend menu open over the Teams window', bool(a['lpop']), repr(a['lpop']))
        bb(X, 'lend')
        tm(X, action='chip', guid=f[0]); tm(X, action='close'); time.sleep(2)
        a = tm(X)
        check('Teams window closed: the HUD button is back', not a['open'] and a['hud_shown'] == 1, f"open {a['open']} hud_shown {a['hud_shown']}")
        # the proposal summary (the proposer's window) and the receiver's view
        tm(Y, action='open'); time.sleep(1); tm(Y, action='click', guid=f[1], button=0); time.sleep(2); tm(Y, action='close'); time.sleep(1)
        T.propose(Y, [f[1], f[0]])
        a, b = settle(X, Y, lambda a, b: a['prop']['active'] and b['prop']['active'], 'proposal open', timeout=60)
        time.sleep(3); a, b = tm(X), tm(Y)
        check('proposal open: the Teams HUD button is hidden on the proposer (summary window)', b['hud_shown'] == 0, f"Y hud_shown {b['hud_shown']}")
        bb(Y, 'proposal_proposer'); bb(X, 'proposal_receiver')
        tm(Y, action='canvases')
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
