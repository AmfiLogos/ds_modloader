#!/usr/bin/env python3
"""locked-slots in multiplayer: one leased rig pair (port), two hidden loader copies (host LSP_HOST default Z, guest
LSP_GUEST default R), both with multiplayer + locked-slots. World = the fixture of locked_slots_test.py (the wiki's
public base save under a new world guid, resource stacks in the bags). Nothing of the owner is read or written.
Both copies run with the session file as the app starts them (DSML_SESSION = data/Mods/worlds.json, written by the
loader): the multiplayer HELLO carries the running gameplay set, so the join's mod-set check is active (without it the
set is empty and nothing is compared). The full-state check runs every 600 steps; no desync of any kind is allowed
(the guest's unit selection no longer makes it differ: ds_multiplayer d72bf03). The host listens on the port leased for this run (rigports).

  locked_slots_pair.py [out]

Real breaks it catches (a desync or a lock that one player has and the other not):
 J  the host's locks of its world (file) reach the joining guest (join-point block): same table, same hash.
 G  the guest's lock (its own unit, through the stock menu row = the game's order, marked Index) passes the multiplayer
    mod intact and is applied on BOTH players at the same tick (fingerprints with the lock table equal); a guest's lock order on a unit it does not own changes nothing anywhere.
 S  both players' multiplayer set (the HELLO) holds locked-slots.
 T  dungeon and back by the map-change vote: no desync, step fingerprints equal for
    600+ ticks after the return, both players' sims hold the same bags (read in each game),
    the host's save keeps the locked stacks (host's and guest's locks) and drops the unlocked resources; both kept 3."""
import json, os, shutil, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import lrig as r
import locked_slots_test as T
HX, GX = os.environ.get('LSP_HOST', 'Z'), os.environ.get('LSP_GUEST', 'R')
LB = os.path.dirname(HERE)
HOME = os.path.expanduser('~')
U, RES = T.U, T.RES
checks = []


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:900]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)
def c(x, timeout=60, **k): return r.send(x, k, timeout=timeout, connect_wait=5)
def st(x):
    try: return c(x, timeout=10, cmd='status')
    except Exception: return {}
def mp(x): return st(x).get('mp', {})
def lk(x): return c(x, cmd='lks_state')
def locked(x, u): return next((e['slots'] for e in lk(x).get('units', []) if e['unit'] == u), [])
def wait(what, f, timeout=60, every=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            v = f()
            if v: return v
        except Exception: pass
        time.sleep(every)
    log('timeout:', what); return None
def inject(x, ev, **k):
    o = {'cmd': 'ord_inject', 'ev': ev}
    for a, v in k.items(): o[a.replace('_', '@', 1)] = v
    return c(x, **o)


def up(x, out, mpcfg, seed_locks=None, world=None):
    if r.alive(r.read_pid(x)): raise SystemExit('copy %s already running' % x)
    I = r.idir(x); md = os.path.join(out, 'mods_' + x)
    if os.path.exists(md): T.aside(md, out)
    os.makedirs(md)
    for m in ('multiplayer', 'locked-slots'): shutil.copytree(os.path.join(LB, 'build', 'mods', m), os.path.join(md, m))
    prof = os.path.join(out, 'profile_%s.json' % x)
    json.dump({'enabled': ['multiplayer', 'locked-slots'], 'order': ['multiplayer', 'locked-slots']}, open(prof, 'w'))
    for p in ('mp.json', 'dsload.log', 'player.log', 'rig.log', 'fp.log', os.path.join('data', 'SaveHeaderCache.json'),
              os.path.join('data', 'Mods', 'worlds.json'), os.path.join('data', 'Mods', 'save_backups')):
        T.aside(os.path.join(I, p), out)
    os.makedirs(os.path.join(I, 'data', 'Saves'), exist_ok=True)
    if seed_locks:                                # the host's own locks of this world, as the mod writes them
        d = os.path.join(I, 'data', 'ModData', 'locked-slots'); os.makedirs(d, exist_ok=True)
        open(os.path.join(d, 'locks_%s.txt' % world), 'w').write(seed_locks)
    json.dump(mpcfg, open(os.path.join(I, 'mp.json'), 'w'))
    for k in ('DSML_ROOT', 'DSML_APP', 'DSML_MODSET', 'DSML_GAME_SAVES', 'DSRIG_DIAG'): os.environ.pop(k, None)
    os.environ.update({'LRIG_DYLIB': os.path.join(LB, 'build', 'dsload.dylib'), 'DSL_MODS_DIR': md, 'DSL_PROFILE': prof,
                       'DSML_SESSION': os.path.join(I, 'data', 'Mods', 'worlds.json'),   # as the app: the loader owns it
                       'DSML_DATA': os.path.join(I, 'data'), 'DSL_CLOUD': '0', 'DSL_SOFT_TEST': '1', 'DSL_NO_ALERT': '1',
                       'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_CRASHREP': '0', 'DSL_UPDATE': '0', 'DSRIG_BG': '1'})
    os.environ.update(r.soft_test_env())
    r.start(x, window=True)
    r.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + x, timeout=1500)


def save(x, out, name):
    I = r.idir(x); p = os.path.join(I, 'data', 'Saves', name + '.json')
    T.aside(p, out)
    c(x, cmd='ord_save', name=name)
    if not wait('save ' + name, lambda: os.path.exists(p) and os.path.getsize(p) > 1000, timeout=60): return None
    time.sleep(1); k = os.path.join(out, 'saves', '%s_%s.json' % (x, name)); os.makedirs(os.path.dirname(k), exist_ok=True)
    shutil.copy2(p, k); return k


def fp(x, t_from):
    d = {}
    try:
        for line in open(os.path.join(r.idir(x), 'fp.log')):
            p = line.split()
            if len(p) >= 2 and p[0].isdigit() and int(p[0]) >= t_from: d[int(p[0])] = p[1]
    except OSError: pass
    return d


FULL = 600             # full-state check every 600 steps: a lock (or anything else) one player lacks is a difference
def other_desyncs(x):
    """every desync and full-state difference line of copy x (none is allowed)"""
    return [l[:200] for l in T.rd(os.path.join(r.idir(x), 'rig.log')).splitlines() if 'FULL-STATE CHECK DIFFERS' in l or 'DESYNC:' in l]
def hello_lines(x):
    try: return json.load(open(os.path.join(r.idir(x), 'data', 'Mods', 'worlds.json'))).get('modset_lines') or []
    except Exception: return []


def map_change(name, ev_fields, want, guest_votes=True):
    log(name, 'host proposes', inject(HX, 'ChangeMapRequested', **ev_fields))
    if guest_votes:
        wait('guest voted', lambda: c(GX, cmd='ui_map_vote', accept=1).get('rc') == 1 or st(GX).get('map') == want, timeout=90, every=1)
    return wait(name + ' on both', lambda: all((lambda s: s.get('map') == want and s.get('det', {}).get('locked') and not s.get('busy'))(st(x)) for x in (HX, GX)),
                timeout=900, every=3)


def run(out, port):
    fx = os.path.join(r.idir(HX), 'save_in', 'lks_pair_fixture.json'); world = T.make_fixture(fx)
    up(HX, out, {'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': FULL},
       seed_locks='%s 0 3\n' % U[0], world=world)
    if 'cannot listen' in T.rd(os.path.join(r.idir(HX), 'rig.log')):
        raise RuntimeError('the host cannot listen on port %d: no guest is started' % port)
    log('host load', c(HX, cmd='load_save', file=fx, det=1))
    wait('host locked', lambda: st(HX).get('det', {}).get('locked'), timeout=900, every=2)
    up(GX, out, {'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Guest', 'full_check_steps': FULL})
    ok = wait('guest loaded the host world', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1
                                                       and not s.get('mp', {}).get('needs_state'))(st(GX)), timeout=900, every=2)
    h, g = lk(HX), lk(GX)
    check('guest joined and loaded THIS host world', ok and g.get('world') == world, (st(GX).get('mp'), g.get('world')))
    check('players: no extra player at the start', *r.rigports.players_guard(r.send, [HX, GX], 2, 'start'))
    check('S: both players\' multiplayer set (the HELLO) holds locked-slots: the join\'s mod-set check saw it',
          all(any(l.startswith('locked-slots@') for l in hello_lines(x)) for x in (HX, GX)), {x: hello_lines(x) for x in (HX, GX)})
    if g.get('world') != world: raise RuntimeError('the guest is not in this test world: stop')
    t_join = max(st(HX).get('tick', 0), st(GX).get('tick', 0))
    check('J: the host\'s locks reached the guest (same world, same table)', h.get('world') == g.get('world') == world and locked(GX, U[0]) == [0, 3]
          and h.get('hash') == g.get('hash') != '0000000000000000', (h, g))

    # ---- G: the guest locks its own unit through the stock menu row (the game's order)
    log('guest claims', U[1], c(GX, cmd='mp_claim', guid=U[1])); time.sleep(3)
    inject(GX, 'SelectPlayerUnitRequested', guid_20=U[1]); time.sleep(1); log('guest open', c(GX, cmd='lks_open')); time.sleep(2)
    c(GX, cmd='lks_menu', unit=U[1], slot=1)
    rows = wait('guest menu rows', lambda: c(GX, cmd='lks_rows').get('rows'), timeout=10)
    check('G: the guest\'s menu shows "Keep on return"', any(x.get('ours') and x.get('lock') == 1 for x in rows or []), rows)
    c(GX, cmd='lks_pick')
    ok = wait('the lock on both', lambda: locked(HX, U[1]) == [1] and locked(GX, U[1]) == [1], timeout=30)
    check('G: the guest\'s lock is applied on both players', ok and lk(HX).get('hash') == lk(GX).get('hash'), (lk(HX), lk(GX)))
    th, tg = lk(HX).get('last_tick'), lk(GX).get('last_tick')
    check('G: the marked order passed the multiplayer mod intact and ran at the same tick on both', th == tg and th is not None and th > t_join,
          f'host tick {th}, guest tick {tg}, join tick {t_join}')
    wait('300 ticks after the lock', lambda: min(st(HX).get('tick', 0), st(GX).get('tick', 0)) >= (th or 0) + 300, timeout=300, every=3)
    a, b = fp(HX, t_join + 1), fp(GX, t_join + 1)
    common = sorted(set(a) & set(b)); first = next((t for t in common if a[t] != b[t]), None)
    check('G: step fingerprints (lock table included) equal before and after the lock tick, no other desync',
          common and common[0] < th < common[-1] and first is None and not other_desyncs(GX) and not other_desyncs(HX),
          f'common ticks {len(common)} ({common[0] if common else None}..{common[-1] if common else None}), lock tick {th}, first diff {first}, '
          f'other desyncs {other_desyncs(GX)[:3]}')
    inject(GX, 'SelectPlayerUnitRequested', guid_20=U[0]); time.sleep(1); c(GX, cmd='lks_open'); time.sleep(2)
    c(GX, cmd='lks_menu', unit=U[0], slot=6); time.sleep(2); c(GX, cmd='lks_pick'); time.sleep(5)
    check('G: a guest\'s lock order on a unit it does not own changes nothing', locked(HX, U[0]) == locked(GX, U[0]) == [0, 3], (lk(HX), lk(GX)))
    check('G: the host wrote the guest\'s lock to its file; the guest wrote none',
          ('%s 1' % U[1]) in T.rd(os.path.join(r.idir(HX), 'data', 'ModData', 'locked-slots', 'locks_%s.txt' % world))
          and not os.path.exists(os.path.join(r.idir(GX), 'data', 'ModData', 'locked-slots', 'locks_%s.txt' % world)), '')

    # ---- T: dungeon and back by the map-change vote
    d0 = {x: mp(x).get('desyncs', 0) for x in (HX, GX)}
    guids = ','.join(U[:2])
    ok = map_change('to the dungeon', dict(i32_16=0, i32_20=1, i32_24=0, lguid_32=guids, i32_40=1), 1)
    check('T: both players went to the dungeon', ok, (st(HX), st(GX)))
    time.sleep(10)
    ok = map_change('back to base', dict(i32_16=1, i32_20=0, i32_24=0, lguid_32=guids, i32_40=0), 0)
    check('T: both players came back to base', ok, (st(HX), st(GX)))
    t0 = max(st(HX).get('tick', 0), st(GX).get('tick', 0))
    wait('600 ticks after the return', lambda: min(st(HX).get('tick', 0), st(GX).get('tick', 0)) >= t0 + 600, timeout=600, every=5)
    a, b = fp(HX, t0 + 1), fp(GX, t0 + 1)
    common = sorted(set(a) & set(b)); first = next((t for t in common if a[t] != b[t]), None)
    d1 = {x: mp(x).get('desyncs', 0) for x in (HX, GX)}
    check('T: step fingerprints equal after the return, no desync', len(common) >= 600 and first is None
          and not other_desyncs(GX) and not other_desyncs(HX), f'common ticks {len(common)}, first diff {first}, desyncs {d0} -> {d1}, other {other_desyncs(GX)[:3]}')
    sh = save(HX, out, 'lks_pair_host')            # a guest writes no save: its world = the host's by the fingerprints above
    bh = T.bags(sh)
    check('T: the host\'s save keeps the locked stacks (host lock + guest lock), unlocked resources left the bags',
          bh[U[0]][0] == ('ITEM_LumaLog', 3) and bh[U[0]][3] == ('ITEM_LumaLog', 2) and bh[U[1]][1] == ('ITEM_WildLongGrass', 4)
          and not any(x and x[0] in RES for i, x in enumerate(bh[U[0]]) if i not in (0, 3)), (bh[U[0]][:8], bh[U[1]][:4]))
    bags = {x: {u: c(x, cmd='lks_bag', unit=u).get('slots') for u in U[:2]} for x in (HX, GX)}
    check('T: both players\' sims have the same bags after the return (the guest\'s copy of each unit = the host\'s)',
          bags[HX] == bags[GX] and bags[HX][U[0]] and bags[HX][U[0]][0] == ['ITEM_LumaLog', 3] and bags[HX][U[1]][1] == ['ITEM_WildLongGrass', 4],
          {x: {u[:8]: (v or [])[:8] for u, v in bags[x].items()} for x in (HX, GX)})
    check('T: the module kept the same stacks on both', lk(HX).get('kept') == lk(GX).get('kept') == 3, (lk(HX), lk(GX)))
    check('players: no extra player at the end', *r.rigports.players_guard(r.send, [HX, GX], 2, 'end'))


def main():
    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(LB, 'test_env', 'locked_slots_pair'))
    os.makedirs(out, exist_ok=True)
    T.OUT = out
    lane = 'dslock-pair-%d' % os.getpid(); pair = None; t0 = time.time()
    while time.time() - t0 < 1800 and not pair:
        p = subprocess.run([sys.executable, f'{HOME}/ds_multiplayer/rig/rig.py', 'lease', lane, '--pid', str(os.getpid())], capture_output=True, text=True)
        pair = p.stdout.strip() if p.returncode == 0 and p.stdout.strip() else None
        if not pair: time.sleep(20)
    if not pair: raise SystemExit('no rig lease in 30 min')
    port = r.pair_port(HX)                        # this run's own port (rigports): no other lane's copy points at it
    log('pair', pair, 'port', port, '(leased for this run)', 'host', HX, 'guest', GX)
    busy = subprocess.run(['lsof', '-nP', '-iTCP:%d' % port, '-sTCP:LISTEN'], capture_output=True, text=True).stdout.strip()
    if busy:
        subprocess.run([sys.executable, f'{HOME}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
        raise SystemExit('port %d of the lease is in use: %s' % (port, busy.splitlines()[-1]))
    try:
        run(out, port)
    except Exception as e:
        check('run', False, repr(e))
    finally:
        for x in (GX, HX):
            try:
                if r.alive(r.read_pid(x)): r.stop(x)
            except Exception as e: log('stop', x, e)
            T.aside(os.path.join(r.idir(x), 'mp.json'), out)
        subprocess.run([sys.executable, f'{HOME}/ds_multiplayer/rig/rig.py', 'release', pair, lane], capture_output=True)
    json.dump(checks, open(os.path.join(out, 'result.json'), 'w'), indent=1)
    bad = [k for k in checks if not k['ok']]
    log('RESULT', 'PASS' if not bad else 'FAIL', '%d/%d' % (len(checks) - len(bad), len(checks)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
