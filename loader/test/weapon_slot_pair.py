#!/usr/bin/env python3
"""weapon-slot in multiplayer: a host and a guest, both with the mod (scope gameplay), stay in sync while the weapon
skill is used, a weapon is swapped and an order tries to write the weapon slot. Hidden loader copies X (host) and U
(guest), one rig lease, never the owner's game: refused while it runs (2 copies). The world: a copy of the 2026-10-11
backup (old weapon-slot layout: converted on load, 4 units in the dungeon).

  weapon_slot_pair.py [--minutes 5] [--world fill] [out]          exit 1 on a failed check
  --world fill: the world of weapon_slot_test F (weapon slots empty, two weapon skills also missing from the units):
                the fill at load end runs on both players (0.1.1)
  env WS_MODS=<dir>   weapon-slot from <dir>/weapon-slot (the unzipped release package), else ../build/mods

Real break caught: a desync (mp desync counters, DESYNC / FULL-STATE / deep lines, det tick errors, fp.log), or the
two players' quick slots differing after the weapon swap and the casts."""
import json, math, os, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
import weapon_slot_test as T
H_, G_ = 'X', 'U'
H = os.path.expanduser('~')
arg = lambda n, d: type(d)(sys.argv[sys.argv.index(n) + 1]) if n in sys.argv else d
MINUTES = arg('--minutes', 5.0)
WORLD = arg('--world', 'old')
log, check = T.log, T.check


def c(x, **k): return r.send(x, k, timeout=120, connect_wait=5)
def st(x): return c(x, cmd='status')
def wait(what, pred, timeout=900, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            v = pred()
        except Exception:
            v = None
        if v: return v
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)
def flog(x, name, pos=0):
    try:
        f = open(os.path.join(r.idir(x), name), errors='replace'); f.seek(pos); return f.read()
    except OSError: return ''
def fsize(x, name):
    try: return os.path.getsize(os.path.join(r.idir(x), name))
    except OSError: return 0
def fp_map(x, t0):
    out = {}
    for line in flog(x, 'fp.log').splitlines():
        p = line.split()
        if len(p) >= 5 and p[0].isdigit() and int(p[0]) > t0: out[int(p[0])] = p[1]
    return out
def inject(x, ev, **k):
    o = {'cmd': 'ord_inject', 'ev': ev}
    for a, v in k.items(): o[a.replace('_', '@', 1)] = v
    return c(x, **o)


def prep(x, out, mp):
    I = r.idir(x)
    if r.alive(r.read_pid(x)): raise SystemExit(f'copy {x} already running')
    for p in ('mp.json', 'dsload.log', 'player.log', 'rig.log', 'fp.log', os.path.join('data', 'SaveHeaderCache.json'),
              os.path.join('data', 'Mods', 'worlds.json'), os.path.join('data', 'Mods', 'save_backups')):
        T.aside(os.path.join(I, p), out)
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    json.dump(mp, open(os.path.join(I, 'mp.json'), 'w'))


OWNER_OK = os.environ.get('WS_OWNER_PLAY_OK') == '1'   # the lead's rule (2026-10-25): one pair at a time while he plays, never on 47777 (this run leases its own port)
owner_runs = lambda: not OWNER_OK and r.owner_game_running()


def main():
    out = os.path.abspath(next((a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith('--') and sys.argv[i - 1] not in ('--minutes', '--world')),
                               os.path.join(T.LB, 'test_env', 'weapon_slot_pair')))
    os.makedirs(out, exist_ok=True); T.OUT = out
    if owner_runs(): raise SystemExit('refused: the owner\'s game runs (this test needs 2 copies)')
    lane, pair = T.lease()
    try:
        md = os.path.join(out, 'mods')
        if os.path.exists(md): T.aside(md, out)
        os.makedirs(md)
        for m in ('multiplayer', 'weapon-slot'):
            src = os.path.join(os.environ['WS_MODS'], m) if m == 'weapon-slot' and os.environ.get('WS_MODS') else os.path.join(T.LB, 'build', 'mods', m)
            shutil.copytree(src, os.path.join(md, m))
        prof = os.path.join(out, 'profile.json'); json.dump({'enabled': ['multiplayer', 'weapon-slot'], 'order': ['multiplayer', 'weapon-slot']}, open(prof, 'w'))
        for k in ('DSML_ROOT', 'DSML_APP', 'DSML_SESSION', 'DSML_GAME_SAVES', 'DSRIG_DIAG'): os.environ.pop(k, None)
        os.environ.update({'LRIG_DYLIB': os.path.join(T.LB, 'build', 'dsload.dylib'), 'DSL_MODS_DIR': md, 'DSL_PROFILE': prof,
                           'DSL_CLOUD': '0', 'DSL_SOFT_TEST': '1', 'DSL_NO_ALERT': '1', 'DSL_SOFT_STAGE': '-20000,-20000',
                           'DSL_CRASHREP': '0', 'DSL_UPDATE': '0', 'DSRIG_BG': '1'})
        os.environ.update(r.soft_test_env())
        port = r.pair_port(H_)                                # this run's own port (rigports): no other lane's copy points at it
        busy = subprocess.run(['lsof', '-nP', f'-iTCP:{port}', '-sTCP:LISTEN'], capture_output=True, text=True).stdout.strip()
        if busy: raise SystemExit(f'port {port} in use: {busy[:200]}')
        prep(H_, out, {'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': 1800})
        prep(G_, out, {'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Guest', 'partial_resync': False})
        os.makedirs(os.path.join(out, 'save_in'), exist_ok=True)
        if WORLD == 'fill':
            src = os.path.join(out, 'save_in', 'fill_fixture.json'); want = T.make_fill_fixture(src)
        else:
            src = os.path.join(out, 'save_in', 'old_layout.json'); shutil.copy2(T.OLD_SAVE, src); want = {}
        os.environ['DSML_DATA'] = os.path.join(r.idir(H_), 'data')
        r.start(H_, window=True)
        r.wait_for(H_, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=900)
        log('host load', c(H_, cmd='load_save', file=src, det=1))
        wait('host locked', lambda: st(H_).get('det', {}).get('locked'))
        if owner_runs(): raise SystemExit('the owner started his game: stop before the second copy')
        os.environ['DSML_DATA'] = os.path.join(r.idir(G_), 'data')
        r.start(G_, window=True)
        wait('guest joined', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1 else None)(st(G_)))
        time.sleep(5)
        check('players: no extra player at the start', *r.rigports.players_guard(r.send, [H_, G_], 2, 'start'))
        mine = [u for u in c(H_, cmd='list_units')['units'] if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
        log('units on the map', [u['guid'][:8] for u in mine])
        gq = {x: {u['guid']: c(x, cmd='ord_read', guid=u['guid']).get('quick') for u in mine} for x in (H_, G_)}
        check('pair: both players have the same 5-slot bars after the join', gq[H_] == gq[G_] and all(len(q or []) == 5 for q in gq[H_].values()), gq)
        if want:
            check('pair: the fill at load end gave both players the same weapon slots (equipped weapon skill)',
                  all((gq[x].get(g) or [None] * 5)[4] == w for x in (H_, G_) for g, w in want.items() if g in gq[H_]), {g[:8]: (gq[H_].get(g), gq[G_].get(g), w) for g, w in want.items()})
        claim = [u['guid'] for u in mine if u['guid'] != T.EQ_UNIT][:1]
        for g in claim: c(G_, cmd='mp_claim', guid=g)
        for x in (H_, G_): c(x, cmd='ord_hover', n=0); c(x, cmd='det_frame_reset')
        rp = {x: fsize(x, 'rig.log') for x in (H_, G_)}
        m0 = {x: st(x).get('mp', {}) for x in (H_, G_)}
        e0 = {x: st(x).get('det', {}).get('tick_errors', 0) for x in (H_, G_)}
        tick0 = max(st(H_)['tick'], st(G_)['tick'])
        mp = st(H_).get('map', 0)
        # weapon swap on the host (an order: both apply it), a write to the weapon slot from the guest (refused on both)
        sv = T.units_of(T.OLD_SAVE)[T.EQ_UNIT]
        inv = sv['inv']; idx = inv.index(T.EQ_ITEM) if T.EQ_ITEM in inv else -1
        if idx >= 0 and T.EQ_UNIT in [u['guid'] for u in mine]:
            c(H_, cmd='ord_select', guids=T.EQ_UNIT, group=3003); time.sleep(1)
            log('equip', inject(H_, 'EquipItemRequested', i32_16=mp, guid_20=T.EQ_UNIT, i32_36=0, guid_40=T.EQ_UNIT, i32_56=idx))
            time.sleep(5)
        if claim:
            g = claim[0]; w = (gq[G_].get(g) or [None] * 5)[4]
            if w: log('guest set to slot 4', inject(G_, 'SetQuickSlotRequested', i32_16=mp, guid_20=g, str_40=w, i32_48=4)); time.sleep(3)
        # casts of each unit's weapon skill at the closest enemy, from the player who owns the unit
        casts = {H_: 0, G_: 0}; t0 = time.time()
        while time.time() - t0 < MINUTES * 60:
            r._with_leases(lambda d: [l.update(used=time.time()) for p_, l in d.items() if l.get('lane') == lane])
            us = c(H_, cmd='list_units').get('units')
            if us is None: log('host world not ready (map change, game over, reload): casts stop'); break
            foes = [e for e in us if e.get('faction') == 3 and not e.get('dead') and 'cell' in e]
            for u in [u for u in us if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]:
                x = G_ if u['guid'] in claim else H_
                w = (c(x, cmd='ord_read', guid=u['guid']).get('quick') or [None] * 5)[4]
                if not w or not foes: continue
                f = min(foes, key=lambda e: math.dist(e['cell'], u['cell']))
                rc = inject(x, 'UseSkillTargetRequested', i32_16=mp, guid_20=u['guid'], guid_36=f['guid'], str_56=w)
                if rc.get('ok'): casts[x] += 1
            if owner_runs(): log('the owner started his game: stop early'); break
            time.sleep(8)
        time.sleep(3)
        sh, sg = st(H_), st(G_)
        mh, mg = sh.get('mp', {}), sg.get('mp', {})
        lines = {k: [l for l in flog(G_, 'rig.log', rp[G_]).splitlines() + flog(H_, 'rig.log', rp[H_]).splitlines() if pat in l]
                 for k, pat in (('desync', 'DESYNC:'), ('full_diff', 'FULL-STATE CHECK DIFFERS'), ('deep_diff', 'deep hash differs'))}
        a, b = fp_map(H_, tick0), fp_map(G_, tick0); com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
        te = {x: s.get('det', {}).get('tick_errors', 0) - e0[x] for x, s in ((H_, sh), (G_, sg))}
        des = {H_: mh.get('desyncs', 0) - m0[H_].get('desyncs', 0), G_: mg.get('desyncs', 0) - m0[G_].get('desyncs', 0)}
        stats = {'casts': casts, 'ticks': [tick0, sh.get('tick'), sg.get('tick')], 'fp_common': len(com), 'fp_first_diff': first, 'desyncs': des,
                 'tick_errors': te, 'full_ok': mg.get('full_ok'), 'full_fails': mg.get('full_fails'), **{k: v[:3] for k, v in lines.items()}}
        log('play', json.dumps(stats))
        check('pair: weapon skills cast by both players, the world advanced', casts[H_] >= 5 and (sg.get('tick') or 0) > tick0 + 1000, stats)
        check('pair: 0 desyncs (counters, desync / full-state / deep lines, tick errors, fp.log equal)',
              not any(des.values()) and not any(lines.values()) and not any(te.values()) and len(com) >= 200 and first is None and not mg.get('full_fails'), stats)
        q2 = {x: {u['guid']: c(x, cmd='ord_read', guid=u['guid']).get('quick') for u in mine} for x in (H_, G_)}
        check('pair: same bars on both after the swap and the casts; WoodClub unit has StunCharge in the weapon slot',
              q2[H_] == q2[G_] and (q2[H_].get(T.EQ_UNIT) or [None] * 5)[4] == T.EQ_SKILL, q2)
        if claim: check('pair: the guest\'s write to slot 4 changed nothing', q2[G_].get(claim[0]) == gq[G_].get(claim[0]), (gq[G_].get(claim[0]), q2[G_].get(claim[0])))
        check('players: no extra player at the end', *r.rigports.players_guard(r.send, [H_, G_], 2, 'end'))
    finally:
        for x in (G_, H_):
            try:
                if r.alive(r.read_pid(x)): r.stop(x)
            except Exception as e: log('stop', x, e)
        subprocess.run([sys.executable, f'{H}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
    json.dump(T.checks, open(os.path.join(out, 'result.json'), 'w'), indent=1)
    bad = [x for x in T.checks if not x['ok']]
    log('RESULT', 'PASS' if not bad else 'FAIL', '%d/%d' % (len(T.checks) - len(bad), len(T.checks)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
