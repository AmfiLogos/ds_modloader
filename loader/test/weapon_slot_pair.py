#!/usr/bin/env python3
"""weapon-slot 0.2 is CLIENT ONLY (owner 2026-10-17 / 2026-11-01): a player WITH the mod and a player WITHOUT it play one
world, swap a weapon and cast weapon skills through the weapon button, and stay in sync. Hidden loader copies (host NWS,
guest NWG by default), one rig lease, never the owner's game: refused while it runs (2 copies). World: the 4-slot world of
weapon_slot_test (make_fill_fixture, units in a dungeon with enemies).

  weapon_slot_pair.py [--mod host|guest] [--minutes 5] [out]      exit 1 on a failed check
  --mod host   the host has weapon-slot, the guest is stock (default); --mod guest = the reverse
  env WS_MODS=<dir>   weapon-slot from <dir>/weapon-slot (the unzipped release package), else ../build/mods
  env WS_HOST / WS_GUEST   the copies (default NWS / NWG)

Real break caught: a desync (mp desync counters, DESYNC / FULL-STATE / deep lines, det tick errors, fp.log), a guest
refused because of the mod set (the mod must not be in it), the two players' quick slots differing after the swap and the
casts, a weapon button that does not cast the unit's weapon skill in a session."""
import json, math, os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
arg = lambda n, d: type(d)(sys.argv[sys.argv.index(n) + 1]) if n in sys.argv else d
H_, G_ = os.environ.get('WS_HOST', 'NWS'), os.environ.get('WS_GUEST', 'NWG')
os.environ.setdefault('WS_COPY', H_)
import lrig as r
import weapon_slot_test as T
H = os.path.expanduser('~')
MINUTES = arg('--minutes', 5.0)
MODP = arg('--mod', 'host')
log, check = T.log, T.check
MOD_X = H_ if MODP == 'host' else G_                   # the player with weapon-slot
STOCK_X = G_ if MODP == 'host' else H_


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


def prep(x, out, mp, enabled):
    I = r.idir(x)
    if r.alive(r.read_pid(x)): raise SystemExit(f'copy {x} already running')
    for p in ('mp.json', 'dsload.log', 'player.log', 'rig.log', 'fp.log', 'multibar_probe_req.txt', os.path.join('data', 'SaveHeaderCache.json'),
              os.path.join('data', 'Mods', 'worlds.json'), os.path.join('data', 'Mods', 'save_backups')):
        T.aside(os.path.join(I, p), out)
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    json.dump(mp, open(os.path.join(I, 'mp.json'), 'w'))
    md, prof = T.mods_dir(os.path.join(out, x), enabled)
    return {'DSL_MODS_DIR': md, 'DSL_PROFILE': prof, 'DSML_DATA': os.path.join(I, 'data')}


def button(x, g):
    """the weapon button of player x for unit g: select g, bar slot 5 (UseQuickSlotSkillRequested Index 5, the stock click
    path); the skill the game will use (AddUseSkill, probe), or None"""
    c(x, cmd='ord_select', guids=g, group=3003)
    pos = fsize(x, 'dsload.log')
    inject(x, 'UseQuickSlotSkillRequested', i32_16=5)
    t0 = time.time()
    while time.time() - t0 < 4:
        adds = [l.split('AddUseSkill ', 1)[1].split()[:2] for l in flog(x, 'dsload.log', pos).splitlines() if 'multibar-probe: AddUseSkill ' in l]
        hit = [a[1] for a in adds if a[0] == g]
        if hit: return hit[0]
        time.sleep(0.3)
    return None


OWNER_OK = os.environ.get('WS_OWNER_PLAY_OK') == '1'   # the lead's rule (2026-10-25): one pair at a time while he plays
owner_runs = lambda: not OWNER_OK and r.owner_game_running()


def main():
    out = os.path.abspath(next((a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith('--') and sys.argv[i - 1] not in ('--minutes', '--mod')),
                               os.path.join(T.LB, 'test_env', 'weapon_slot_pair_' + MODP)))
    os.makedirs(out, exist_ok=True); T.OUT = out
    if owner_runs(): raise SystemExit('refused: the owner\'s game runs (this test needs 2 copies)')
    lane, pair = T.lease()
    try:
        for k in ('DSML_ROOT', 'DSML_APP', 'DSML_SESSION', 'DSML_GAME_SAVES', 'DSRIG_DIAG'): os.environ.pop(k, None)
        os.environ.update({'LRIG_DYLIB': os.path.join(T.LB, 'build', 'dsload.dylib'), 'DSL_CLOUD': '0', 'DSL_SOFT_TEST': '1', 'DSL_NO_ALERT': '1',
                           'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_CRASHREP': '0', 'DSL_UPDATE': '0', 'DSRIG_BG': '1'})
        os.environ.update(r.soft_test_env())
        port = r.pair_port(H_)                                # this run's own port (rigports): no other lane's copy points at it
        busy = subprocess.run(['lsof', '-nP', f'-iTCP:{port}', '-sTCP:LISTEN'], capture_output=True, text=True).stdout.strip()
        if busy: raise SystemExit(f'port {port} in use: {busy[:200]}')
        en = {MOD_X: ['multiplayer', 'weapon-slot'], STOCK_X: ['multiplayer']}
        env_h = prep(H_, out, {'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': 1800}, en[H_])
        env_g = prep(G_, out, {'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Guest', 'partial_resync': False}, en[G_])
        src = os.path.join(out, 'save_in', 'ws_world.json'); os.makedirs(os.path.dirname(src), exist_ok=True)
        T.make_fill_fixture(src, free_eq=True)
        before = T.units_of(src)
        os.environ.update(env_h)
        r.start(H_, window=True)
        r.wait_for(H_, lambda s: s.get('scene') == 1 and not s.get('busy'), 'host title', timeout=900)
        log('host load', c(H_, cmd='load_save', file=src, det=1))
        wait('host locked', lambda: st(H_).get('det', {}).get('locked'))
        if owner_runs(): raise SystemExit('the owner started his game: stop before the second copy')
        os.environ.update(env_g)
        r.start(G_, window=True)
        joined = None
        try:
            joined = wait('guest joined', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1 else None)(st(G_)), timeout=600)
        except RuntimeError: pass
        check('join: the %s without weapon-slot plays with the %s that has it (the mod is not in the mod set)' % ('guest' if MODP == 'host' else 'host', MODP),
              joined, [l for l in flog(G_, 'rig.log').splitlines() if 'mod' in l.lower()][-4:])
        if not joined: raise SystemExit('guest did not join: stop')
        time.sleep(5)
        check('players: no extra player at the start', *r.rigports.players_guard(r.send, [H_, G_], 2, 'start'))
        check('the player with the mod runs it (client only, 7 hooks)', 'weapon skill slot on (client only: 7 hooks' in flog(MOD_X, 'dsload.log'),
              [l for l in flog(MOD_X, 'dsload.log').splitlines() if 'weapon-slot' in l][-4:])
        check('the player without the mod does not run it', 'weapon skill slot on' not in flog(STOCK_X, 'dsload.log'))
        mine = [u for u in c(H_, cmd='list_units')['units'] if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
        log('units on the map', [u['guid'][:8] for u in mine])
        gq = {x: {u['guid']: c(x, cmd='ord_read', guid=u['guid']).get('quick') for u in mine} for x in (H_, G_)}
        check('pair: both players have the same 4-slot bars after the join (the game\'s own data)',
              gq[H_] == gq[G_] and all(len(q or []) == 4 for q in gq[H_].values()), gq)
        # the guest's units: with --mod guest, units whose weapon button has a skill (the guest casts through it)
        armed = lambda g: g in before and T.weapon_skill(before[g])
        claim = [u['guid'] for u in mine if u['guid'] != T.EQ_UNIT and (armed(u['guid']) if MODP == 'guest' else True)][:2]
        for g in claim: c(G_, cmd='mp_claim', guid=g)
        owner_of = lambda g: G_ if g in claim else H_
        for x in (H_, G_): c(x, cmd='ord_hover', n=0); c(x, cmd='det_frame_reset')
        rp = {x: fsize(x, 'rig.log') for x in (H_, G_)}
        m0 = {x: st(x).get('mp', {}) for x in (H_, G_)}
        e0 = {x: st(x).get('det', {}).get('tick_errors', 0) for x in (H_, G_)}
        tick0 = max(st(H_)['tick'], st(G_)['tick'])
        mp = st(H_).get('map', 0)
        # weapon swap: the game's own equip order, from the host (both apply it)
        idx = before[T.EQ_UNIT]['inv'].index(T.EQ_ITEM) if T.EQ_ITEM in before[T.EQ_UNIT]['inv'] else -1
        if idx >= 0 and T.EQ_UNIT in [u['guid'] for u in mine]:
            c(H_, cmd='ord_select', guids=T.EQ_UNIT, group=3003); time.sleep(1)
            log('equip', inject(H_, 'EquipItemRequested', i32_16=mp, guid_20=T.EQ_UNIT, i32_36=0, guid_40=T.EQ_UNIT, i32_56=idx))
            time.sleep(5)
        # casts: the player with the mod casts its units' weapon skills through the weapon button (aim = the closest enemy,
        # the order the game's aiming sends: UseSkillTargetRequested by skill key); the stock player casts its units'
        # weapon skills from their bar
        casts = {'button': 0, 'stock': 0}; seen = {}; t0 = time.time()
        while time.time() - t0 < MINUTES * 60:
            r._with_leases(lambda d: [l.update(used=time.time()) for p_, l in d.items() if l.get('lane') == lane])
            us = c(H_, cmd='list_units').get('units')
            if us is None: log('host world not ready (map change, game over, reload): casts stop'); break
            foes = [e for e in us if e.get('faction') == 3 and not e.get('dead') and 'cell' in e]
            for u in [u for u in us if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]:
                x = owner_of(u['guid'])
                if not foes: continue
                if x == MOD_X:
                    w = button(x, u['guid']); kind = 'button'
                    seen[u['guid'][:8]] = w
                else:
                    q = c(x, cmd='ord_read', guid=u['guid']).get('quick') or []
                    w = next((s for s in q if s in T.W), None); kind = 'stock'
                if not w: continue
                f = min(foes, key=lambda e: math.dist(e['cell'], u['cell']))
                rc = inject(x, 'UseSkillTargetRequested', i32_16=mp, guid_20=u['guid'], guid_36=f['guid'], str_56=w)
                if rc.get('ok'): casts[kind] += 1
            if owner_runs(): log('the owner started his game: stop early'); break
            time.sleep(8)
        time.sleep(3)
        log('weapon button per unit (player with the mod):', seen)
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
        check('pair: weapon skills cast through the weapon button (player with the mod), the world advanced',
              casts['button'] >= 5 and (sg.get('tick') or 0) > tick0 + 1000, stats)
        check('pair: 0 desyncs (counters, desync / full-state / deep lines, tick errors, fp.log equal)',
              not any(des.values()) and not any(lines.values()) and not any(te.values()) and len(com) >= 200 and first is None and not mg.get('full_fails'), stats)
        q2 = {x: {u['guid']: c(x, cmd='ord_read', guid=u['guid']).get('quick') for u in mine} for x in (H_, G_)}
        check('pair: same 4-slot bars on both after the swap and the casts; the WoodClub unit got %s in its free slot (stock)' % T.EQ_SKILL,
              q2[H_] == q2[G_] and (q2[H_].get(T.EQ_UNIT) or [None] * 4)[3] == T.EQ_SKILL, q2)
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
