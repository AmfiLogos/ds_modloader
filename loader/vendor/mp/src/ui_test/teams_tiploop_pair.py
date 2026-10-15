#!/usr/bin/env python3
"""Tooltips stay visible (lead 2026-10-14, release blocker): a hover loop over our elements in game (Players panel,
its cog rows, lend menu), the Teams window and the Esc menu, two passes. Each hover as gamelane's repro: hide,
0.5 s, hover, 1.5 s, then the ui_tip probe (drawn rect on screen) and a real-screen frame.

  teams_tiploop_pair.py [--keep]          frames research/ux_shots/v9_tiploop_<n>.png"""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, settle = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.settle
results = T.results
OUT = os.path.expanduser('~/ds_multiplayer/research/ux_shots')
GAME = ['Players', 'Settings', 'Show / Hide', 'Ben']
COG = ['Other Units See-through', 'Names Above Units', 'Show Tooltips on Hover', 'Panel Size', 'Chat Fade Time', 'Join and Leave Lines', 'Units per Player', 'Admin Mode']
ESC = ['Leave Multiplayer', 'Steam Invite', 'Load Campaign']


def main():
    save = T.arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))
    rig.DYLIB = T.arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/teams_test/dsrig_teams.dylib'))
    pair = rig.lease('teams_tips' + ('1512' if '--res1512' in sys.argv else ''), os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS.get(pair, 7795)
    log('pair', pair, 'port', port, 'dylib', rig.DYLIB)
    rows = []
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True}, open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Ben'}, open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        for i in (X, Y):                                                 # tooltips on, the panel open
            os.makedirs(os.path.join(rig.idir(i), 'data'), exist_ok=True)
            p = os.path.join(rig.idir(i), 'data', 'mp_ui.json')
            if os.path.exists(p): os.rename(p, p + time.strftime('.prev_%Y%m%d_%H%M%S'))
            open(p, 'w').write('{"see_through":0,"names":1,"list_open":1,"tooltips":1}\n')
        if '--res1512' in sys.argv:                                     # gamelane's setting (uxgame_pair: 1512x982)
            import uxgame_pair as U
            U.start(X, 1512, 982)
        else:
            rig.start(X, fps=10, window=True)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        if '--res1512' in sys.argv: U.start(Y, 1512, 982)
        else: rig.start(Y, fps=30, window=True)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        free = next(col for col in tm(X)['columns'] if col['player'] == -1)['model']
        n = [0]

        def probe(what, kind):
            time.sleep(1.5)
            pr = c(X, cmd='ui_tip', action='probe')
            n[0] += 1
            f = f'{OUT}/v9_tiploop_{n[0]:02d}.png'
            c(X, cmd='mpm_shot', how='backbuffer', path=f)
            ok = pr.get('shown') == 1 and pr.get('on_screen') == 1
            rows.append((n[0], kind, what, ok, pr.get('screen_rect'), pr.get('align_routine'), pr.get('self_placed'), pr.get('restarts'), f))
            log(f'{n[0]:02d} {kind:5} {what:28} {"VISIBLE" if ok else "NOT VISIBLE"} rect {pr.get("screen_rect")} routine {pr.get("align_routine")} self_placed {pr.get("self_placed")} restarts {pr.get("restarts")}')

        def clear():
            c(X, cmd='ui_tip_hover', title='-none-'); c(X, cmd='mpm_tip', title='-none-'); c(X, cmd='ui_tip', action='hide'); time.sleep(0.5)

        for pas in range(2):
            for t in GAME:
                clear(); c(X, cmd='ui_tip_hover', title=t); probe(t, 'game')
            c(X, cmd='ui_pop', open=1); time.sleep(1)
            for t in COG:
                clear(); c(X, cmd='ui_tip_hover', title=t); probe(t, 'cog')
            c(X, cmd='ui_pop', open=0); clear(); c(X, cmd='ui_tip_hover', title='')
            tm(X, action='open'); time.sleep(2)
            for w, kw in (('info', {}), ('header', {'player': 0}), ('header', {'player': -1}), ('header', {'player': 1}), ('card', {'guid': free[0]})):
                clear(); tm(X, action='tip', what=w, **kw); probe(w + str(kw.get('player', '')), 'teams')
            tm(X, action='close'); time.sleep(1)
            clear(); tm(X, action='tip', what='hud'); probe('hud', 'teams')
            c(X, cmd='mpm_esc'); time.sleep(2)
            for t in ESC:
                clear(); c(X, cmd='mpm_tip', title=t); probe(t, 'esc')
            c(X, cmd='mpm_tip'); c(X, cmd='mpm_key', key='escape'); time.sleep(2)
        clear(); c(X, cmd='ui_tip_hover', title='')
        bad = [r for r in rows if not r[3]]
        check(f'{len(rows)} tooltips across game, cog, Teams and Esc menu: all drawn on screen', len(rows) >= 30 and not bad,
              f'{len(rows) - len(bad)}/{len(rows)} visible; not visible: {[(r[0], r[2], r[4]) for r in bad]}')
        with open(os.path.join(rig.idir(X), 'rig.log'), errors='replace') as fh:
            diag = [l.rstrip() for l in fh if 'tip: Tooltip_Base' in l]
        log('stock re-align calls on our tooltip:', len(diag))
        for l in diag[-12:]: log('  ', l[:200])
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
