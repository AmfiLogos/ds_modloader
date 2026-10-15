#!/usr/bin/env python3
"""Viewer approval (owner 2026-10-13): a player whose units do not go approves with 0 units (dsnet READY n = 0).

  teams_viewer_pair.py [--dylib PATH] [--keep]

Y takes + marks f1 and proposes (stock window -> its last button). X owns no units: no supplies step, the summary
at once, his row "No units: watches the expedition"; X approves with 0 units -> EXPEDITION_START with 1 unit on
both, both land with f1 only; fingerprints equal. Frame: research/ux_shots/v2_proposal_viewer_host.png."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, widget, settle, fp_equal_from = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.widget, T.settle, T.fp_equal_from
results = T.results
OUT = os.path.expanduser('~/ds_multiplayer/research/ux_shots')


def shot(x, name):
    p = f'{OUT}/v2_proposal_{name}.png'
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
        rig.start(X, fps=10, window=True)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30, window=True)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        a = tm(X)
        free = next(col for col in a['columns'] if col['player'] == -1)['model']
        check('5 free units', len(free) >= 5, f'{len(free)}')
        f = free[:5]
        t0 = st(X)['tick']
        tm(X, action='open'); tm(Y, action='open'); time.sleep(2)
        take_mark(Y, [f[1]])
        settle(X, Y, lambda a, b: a['prop']['members'] == 1 and b['prop']['members'] == 1, 'Y marks', timeout=60)
        tm(Y, action='go')
        settle(X, Y, lambda a, b: b['stock']['open'] and b['stock']['state'] == 1, 'Y stock', timeout=60)
        tm(Y, action='stock_accept')
        a, b = settle(X, Y, lambda a, b: a['prop']['shown'] and a['prop']['ok'] == 'Approve', 'viewer summary', timeout=60)
        check('viewer: no supplies step, the summary at once with Approve', a['prop']['shown'] and not a['stock']['open'] and a['prop']['ok'] == 'Approve'
              and a['prop']['members'] == 1, json.dumps(a['prop']) + json.dumps(a['stock']))
        time.sleep(1); shot(X, 'viewer_host')
        tm(X, action='approve')
        a, b = settle(X, Y, lambda a, b: a['starts'] >= 1 and b['starts'] >= 1, 'start', timeout=60)
        check('viewer approves with 0 units: EXPEDITION_START on both with 1 unit', a['starts'] == 1 and b['starts'] == 1 and a['start_units'] == 1 and b['start_units'] == 1,
              f"starts {a['starts']}/{b['starts']} units {a['start_units']}/{b['start_units']} ready {a['ready']}")
        land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
        wait('both in the dungeon', lambda: land(X) and land(Y), timeout=900, every=3)
        time.sleep(5)
        dx, dy = player_units(X, 1), player_units(Y, 1)
        check('both land with f1 only', dx == [f[1]] and dy == [f[1]], f'X {dx} Y {dy}')
        t_land = max(st(X)['tick'], st(Y)['tick'])
        wait('300 ticks after landing', lambda: min(st(X)['tick'], st(Y)['tick']) > t_land + 300, timeout=900, every=5)
        time.sleep(3)
        n, first = fp_equal_from(X, Y, t0)
        check('fingerprints equal every tick', n >= 300 and first is None, f'common ticks {n}, first diff {first}')
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
