#!/usr/bin/env python3
"""Lending from the Teams window (owner lan_v7 feedback), in the base and in a dungeon, on a leased hidden pair.

  teams_lend_pair.py [--dylib PATH] [--keep]

X (host) takes + marks f0, Y takes + marks f1. Base: X's card has a "Lend" chip -> menu "Lend control to Friend" ->
the controller is Y on both; Y's Teams shows "From the owner" on that card -> "Give back" -> the controller is X again;
X lends again and takes back. Dungeon (both go: Y proposes, X approves): the Teams hint says only lending works,
X lends f0 to Y there too. Frames research/ux_shots/v2_lend_*.png."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, widget, settle, fp_equal_from = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.widget, T.settle, T.fp_equal_from
results = T.results
OUT = os.path.expanduser('~/ds_multiplayer/research/ux_shots')


def shot(x, name):
    p = f'{OUT}/v2_teams_lend_{name}.png'
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
        take_mark(X, [f[0]]); take_mark(Y, [f[1]])
        a, b = settle(X, Y, lambda a, b: col_of(a, f[0]) == 0 and col_of(b, f[1]) == 1, 'takes', timeout=60)
        wx = lambda s_, g: next(w for w in s_['widgets'] if w['guid'] == g)
        check('chips: "Lend" on my cards only', wx(a, f[0])['chip'] == 1 and wx(a, f[1])['chip'] == 0 and wx(b, f[1])['chip'] == 1 and wx(b, f[0])['chip'] == 0,
              f"X f0 {wx(a, f[0])['chip']} f1 {wx(a, f[1])['chip']} / Y f1 {wx(b, f[1])['chip']} f0 {wx(b, f[0])['chip']}")
        tm(X, action='chip', guid=f[0])
        a = settle(X, Y, lambda a, b: a['lpop'] != '', 'menu')[0]
        check('the chip opens the menu "Lend control to Friend"', a['lpop'] == 'Lend control to Friend', repr(a['lpop']))
        time.sleep(1); shot(X, 'menu_host')
        tm(X, action='lpop', row=0)
        a, b = settle(X, Y, lambda a, b: wx(a, f[0])['controller'] == 1 and wx(b, f[0])['controller'] == 1 and wx(b, f[0])['chip'] == 1, 'lent', timeout=60)
        check('lend: Friend controls f0 on both peers, the owner still owns it; Friend gets a "From the owner" chip',
              wx(a, f[0])['controller'] == 1 and wx(b, f[0])['controller'] == 1 and wx(a, f[0])['lent_to'] == 1 and wx(b, f[0])['chip'] == 1 and T.col_of(b, f[0]) == 0,
              f"ctl {wx(a, f[0])['controller']}/{wx(b, f[0])['controller']} lent {wx(a, f[0])['lent_to']}")
        time.sleep(1); shot(Y, 'from_guest'); shot(X, 'lent_host')
        tm(Y, action='chip', guid=f[0]); b = settle(X, Y, lambda a, b: b['lpop'] != '', 'menu Y')[1]
        check('borrower menu: "Give back"', b['lpop'] == 'Give back', repr(b['lpop']))
        tm(Y, action='lpop', row=0)
        a, b = settle(X, Y, lambda a, b: wx(a, f[0])['controller'] == 0 and wx(b, f[0])['controller'] == 0, 'given back', timeout=60)
        check('give back: The owner controls f0 again on both', wx(a, f[0])['controller'] == 0 and wx(b, f[0])['controller'] == 0)
        tm(X, action='chip', guid=f[0]); settle(X, Y, lambda a, b: a['lpop'] != '', 'menu 2'); tm(X, action='lpop', row=0)
        settle(X, Y, lambda a, b: wx(a, f[0])['controller'] == 1 and wx(b, f[0])['controller'] == 1 and wx(a, f[0])['lent_to'] == 1, 'lent 2', timeout=60)
        time.sleep(2)                                                  # the lend is applied on every peer before the menu reads it
        tm(X, action='chip', guid=f[0]); a = settle(X, Y, lambda a, b: a['lpop'] != '', 'menu 3')[0]
        check('owner menu on a lent unit: "Take back control"', a['lpop'] == 'Take back control', repr(a['lpop']))
        tm(X, action='lpop', row=0)
        a, b = settle(X, Y, lambda a, b: wx(a, f[0])['controller'] == 0 and wx(b, f[0])['controller'] == 0, 'taken back', timeout=60)
        check('take back: The owner controls f0 again on both', wx(a, f[0])['controller'] == 0 and wx(b, f[0])['controller'] == 0)
        # dungeon
        T.propose(Y, [f[0], f[1]])                                     # Friend picks the owner's unit too
        settle(X, Y, lambda a, b: a['stock']['open'] and a['stock']['mode'] == 2, 'X supplies', timeout=60)
        tm(X, action='stock_next'); settle(X, Y, lambda a, b: a['prop']['shown'], 'X summary', timeout=60)
        tm(X, action='approve')
        a, b = settle(X, Y, lambda a, b: a['starts'] >= 1 and b['starts'] >= 1, 'start', timeout=60)
        check('both go: party of 2 (f0 from the owner, f1 from Friend)', a['start_units'] == 2 and b['start_units'] == 2, f"{a['start_units']}/{b['start_units']}")
        land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
        wait('both in the dungeon', lambda: land(X) and land(Y), timeout=900, every=3)
        time.sleep(5)
        tm(X, action='open'); tm(Y, action='open'); time.sleep(3); a = tm(X)
        check('dungeon: the hint says only lending works, my card has its chip', 'only lending works' in a['hint'] and wx(a, f[0])['chip'] == 1, repr(a['hint'][-80:]))
        check('dungeon: units on the run framed, units at the settlement not', wx(a, f[0])['mark_shown'] == 1 and wx(a, f[1])['mark_shown'] == 1
              and all(w['mark_shown'] == 0 for w in a['widgets'] if w['guid'] not in (f[0], f[1])), str([(w['name'], w['mark_shown']) for w in a['widgets']]))
        tm(X, action='chip', guid=f[0]); settle(X, Y, lambda a, b: a['lpop'] != '', 'menu dungeon'); time.sleep(1); shot(X, 'dungeon_menu_host')
        tm(X, action='lpop', row=0)
        a, b = settle(X, Y, lambda a, b: wx(a, f[0])['controller'] == 1 and any(w['guid'] == f[0] for w in b['widgets']) and wx(b, f[0])['controller'] == 1, 'lent in dungeon', timeout=60)
        check('dungeon: lend works, Friend controls f0 on both', wx(a, f[0])['controller'] == 1 and wx(b, f[0])['controller'] == 1)
        time.sleep(1); shot(X, 'dungeon_host')
        t_land = st(X)['tick']
        wait('300 ticks', lambda: min(st(X)['tick'], st(Y)['tick']) > t_land + 300, timeout=900, every=5)
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
