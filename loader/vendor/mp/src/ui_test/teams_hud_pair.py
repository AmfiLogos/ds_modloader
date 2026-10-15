#!/usr/bin/env python3
"""The Teams HUD button (owner lan_v9): above game windows, its own right-drag and saved place, compact size.

  teams_hud_pair.py [--dylib PATH] [--keep]

Frames research/ux_shots/v2_teams_hud_*.png: the button moved away from the player panel; the Consume Policy
window open with the button still on top."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, widget, settle, fp_equal_from = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.widget, T.settle, T.fp_equal_from
results = T.results
OUT = os.path.expanduser('~/ds_multiplayer/research/ux_shots')
RES = '1512' if '--res1512' in sys.argv else '960'


def shot(x, name):
    p = f'{OUT}/v2_teams_hud_{name}.png'
    log('frame', p, 'rc', tm(x, action='shot', path=p).get('rc'))


def player_units(x, m):
    r = c(x, cmd='list_units', map=m)
    return sorted(u['guid'] for u in r.get('units', []) if u['faction'] == 1 and not u['dead'])


def take_mark(x, guids):
    for g in guids:
        tm(x, action='click', guid=g, button=0); time.sleep(1.5)
    for g in guids:
        tm(x, action='click', guid=g, button=1); time.sleep(1.5)


def main():
    save = T.arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))
    rig.DYLIB = T.arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/teams_test/dsrig_teams.dylib'))
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
        if '--compact' in sys.argv:                                       # the panel's "Compact" size (uxgame's mp_ui.json)
            uj = os.path.join(rig.idir(X), 'data', 'mp_ui.json')
            d_ = json.load(open(uj)) if os.path.exists(uj) else {}
            d_['compact'] = 1; json.dump(d_, open(uj, 'w'))
        for i_ in (X, Y):                                                # start from the default place
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
        a = tm(X)
        check('HUD button shown', a['hud'] and a['hud_shown'] == 1, f"{a['hud_pos']}")
        p0 = a['hud_pos']; time.sleep(1); shot(X, 'start_host' if '--compact' not in sys.argv else 'compact_host')
        pn = a['panel']; gap = a['hud_pos'][0] - (a['hud_canvas_w'] / 2 + pn[1] + pn[3])
        check('never dragged: the button sits 8 units right of the player panel, same top', pn[0] == 1 and abs(gap - 8) < 1.5 and abs(a['hud_pos'][1] - pn[2]) < 1.5 and a['hud_dragged'] == 0,
              f"panel {pn} button {a['hud_pos']} gap {gap:.1f}")
        if '--compact' in sys.argv:
            check('Compact size: the smaller button (28 high)', a['hud_compact'] == 1 and round(a['hud_size'][1]) == 28, f"{a['hud_size']} compact {a['hud_compact']}")
        else:
            check('Normal size: 34 high', a['hud_compact'] == 0 and round(a['hud_size'][1]) == 34, f"{a['hud_size']} compact {a['hud_compact']}")
        # lead 2026-10-14: the game tooltip must draw ABOVE the Teams button (31000); one laid over it, one next to it (real screen)
        hc = tm(X)['hud_screen']
        r_ = c(X, cmd='ui_tip', action='show', title='Over the Teams Button', body='This tooltip is laid over the Teams button; it must hide the button.', px=hc[0] - 90, py=hc[1] + 12)
        time.sleep(1.2); pv = os.path.join(OUT, 'v9_tip_over_teams_button.png'); log('frame', pv, c(X, cmd='mpm_shot', how='backbuffer', path=pv).get('ok'), json.dumps(r_), 'hud', hc)
        c(X, cmd='ui_tip', action='hide'); tm(X, action='tip', what='hud'); time.sleep(1.2)
        pv = os.path.join(OUT, 'v9_tip_next_to_teams_button.png'); log('frame', pv, c(X, cmd='mpm_shot', how='backbuffer', path=pv).get('ok'))
        time.sleep(3); p0 = tm(X)['hud_pos']                              # the first place (right of the player panel) is set by now
        tm(X, action='hud_move', dx=150, dy=-120); time.sleep(2)      # right-drag: 150 px right, 120 px down (into the window area)
        a = tm(X)
        dxu, dyu = a['hud_pos'][0] - p0[0], a['hud_pos'][1] - p0[1]
        check('right-drag moves the button alone: 150 px right, 120 px down (canvas units = px / UI scale), place saved',
              dxu > 0 and dyu < 0 and abs(dxu / 150 - (-dyu) / 120) < 0.05
              and os.path.exists(os.path.join(rig.idir(X), 'data', 'mp_teams_button.json')), f"{p0} -> {a['hud_pos']}")
        time.sleep(1); shot(X, 'moved_host')
        tm(X, action='popup', type=12); time.sleep(3)                    # the stock Consume Policy window
        a = tm(X)
        check('Consume Policy open: the Teams button is still shown', a['hud_shown'] == 1)
        time.sleep(1); shot(X, 'consume_policy_host')
        tm(X, action='popup', type=1); time.sleep(3)                     # the game's Esc menu (SystemMenu)
        a = tm(X)
        check('Esc menu open: the Teams button is hidden', a['hud_shown'] == 0, str(a['hud_shown']))
        time.sleep(1); shot(X, 'esc_menu_' + RES + '_host')
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
