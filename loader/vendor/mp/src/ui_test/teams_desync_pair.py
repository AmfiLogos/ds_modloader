#!/usr/bin/env python3
"""Desync hunt (lead 2026-10-14, release blocker): expedition entries in a row, no auto-resync, so fp.log / trace.bin
of BOTH peers stay as they were at the first differing step (a resync reloads and restarts them).

  teams_desync_pair.py [--rounds N] [--keep]

X (host, the owner) takes f0 f3, Y (Friend) takes f1 f2. Each round: X proposes the 4 (stock Go to dungeon, supplies,
Send), Y packs (Next) and approves -> EXPEDITION_START -> both land in the dungeon; 90 steps; then return to base by
vote (ChangeMapRequested from X, Y votes yes); 90 steps. After every phase: mp desynced? On the first desync the run
stops (copies kept with --keep) and copies fp.log / trace.bin / detail of both to build/teams_test/desync_<time>/."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import teams_pair as T
rig, log, c, st, wait, check, tm, col_of, settle = T.rig, T.log, T.c, T.st, T.wait, T.check, T.tm, T.col_of, T.settle
results = T.results
ROUNDS = T.arg('--rounds', 10)


def mpst(x):
    return st(x).get('mp', {})


def inject(x, ev, **f):
    d = {'cmd': 'ord_inject', 'ev': ev}
    d.update({k.replace('_', '@'): v for k, v in f.items()})
    return c(x, **d)


def desynced(X, Y):
    return any(mpst(i).get('desynced') or mpst(i).get('desyncs', 0) for i in (X, Y))


def keep_evidence(X, Y, why):
    out = os.path.expanduser(f"~/ds_multiplayer/build/teams_test/desync_{time.strftime('%H%M%S')}")
    os.makedirs(out, exist_ok=True)
    for i in (X, Y):
        for f in ('fp.log', 'trace.bin', 'detail.log', 'rig.log'):
            p = os.path.join(rig.idir(i), f)
            if os.path.exists(p): shutil.copy2(p, os.path.join(out, f'{i}_{f}'))
    log('EVIDENCE', why, out, 'X', json.dumps(mpst(X))[:300], 'Y', json.dumps(mpst(Y))[:300])
    return out


def steps_after(X, Y, n, what):
    t0 = max(mpst(X).get('timer', 0), mpst(Y).get('timer', 0))
    wait(f'{n} steps after {what}', lambda: desynced(X, Y) or min(mpst(X).get('timer', 0), mpst(Y).get('timer', 0)) >= t0 + n, timeout=900)


def main():
    save = T.arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))
    rig.DYLIB = T.arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/teams_test/dsrig_teams.dylib'))
    pair = rig.lease('teams_desync', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    port = rig.PAIR_PORTS.get(pair, 7795)
    log('pair', pair, 'port', port, 'rounds', ROUNDS, 'dylib', rig.DYLIB)
    entries = 0
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': port, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True, 'auto_resync': 0},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Friend', 'auto_resync': 0},
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
        tm(X, action='open'); tm(Y, action='open'); time.sleep(2)
        for g in (f[0], f[3]): tm(X, action='click', guid=g, button=0); time.sleep(1.5)
        for g in (f[1], f[2]): tm(Y, action='click', guid=g, button=0); time.sleep(1.5)
        settle(X, Y, lambda a, b: all(col_of(a, g) == 0 for g in (f[0], f[3])) and all(col_of(b, g) == 1 for g in (f[1], f[2])), 'takes', timeout=60)
        tm(X, action='close'); tm(Y, action='close'); time.sleep(1)
        party = [f[0], f[3], f[1], f[2]]
        for r in range(ROUNDS):
            if desynced(X, Y): break
            if '--hold' in sys.argv:                                  # uxgame map_change: the guest holds a popup time lock (reason 4)
                log('guest popup hold', c(Y, cmd='mp_popup', reason=4, add=1)); time.sleep(2)
            log(f'round {r + 1}: propose at step', mpst(X).get('timer'))
            T.propose(X, party)
            settle(X, Y, lambda a, b: b['prop']['active'] and (b['stock']['open'] or b['prop']['shown']), 'receiver', timeout=60)
            if tm(Y)['stock']['open']:
                tm(Y, action='stock_next'); settle(X, Y, lambda a, b: b['prop']['shown'], 'guest summary', timeout=60)
            tm(Y, action='approve')                                  # (as uxgame_pair: Next only shows the summary)
            land = lambda x: (lambda s: s.get('map') == 1 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
            wait('both in the dungeon', lambda: desynced(X, Y) or (land(X) and land(Y)), timeout=900, every=3)
            steps_after(X, Y, 90, 'landing')
            if desynced(X, Y): keep_evidence(X, Y, f'round {r + 1}: after landing'); break
            entries += 1
            log(f'round {r + 1}: landed, 90 steps equal, step', mpst(X).get('timer'))
            region = st(X).get('region', 0)
            inject(X, 'ChangeMapRequested', i32_16=1, i32_20=0, i32_24=region, lguid_32=','.join(party), i32_40=0)
            try:
                wait('Y voted', lambda: c(Y, cmd='ui_map_vote', accept=1).get('rc') == 1 or st(Y).get('map') == 0, timeout=60, every=1)
            except RuntimeError:
                log('Y had no open prompt')
            back = lambda x: (lambda s: s.get('map') == 0 and not s.get('busy') and s.get('det', {}).get('locked'))(st(x))
            wait('both back at base', lambda: desynced(X, Y) or (back(X) and back(Y)), timeout=900, every=3)
            wait('unpaused', lambda: desynced(X, Y) or (not mpst(X).get('paused') and not mpst(Y).get('paused')), timeout=180)
            steps_after(X, Y, 90, 'return')
            if desynced(X, Y): keep_evidence(X, Y, f'round {r + 1}: after return'); break
        check(f'{ROUNDS} expedition entries in a row, no desync', entries == ROUNDS and not desynced(X, Y), f'entries {entries}')
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
