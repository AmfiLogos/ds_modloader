#!/usr/bin/env python3
"""Is the weapon-skill slot mod CLIENT-side? Host WITH the mod, guest WITHOUT it, one lockstep game.
Client-side would mean: the guest joins the host's world, replays the host's orders, and both copies have the
same fingerprint every tick.

  weaponslot_pair.py <PAIR> [--ticks 3000]

Needs: a pair leased with `rig.py lease` (its port), and the two copies made by
`PAIR=<PAIR> cargo test -p dsml-core --test e2e_mac make_pair_copies -- --ignored`:
  test_env/pair/<host letter>/DungeonSettlers.app   stock + weapon-slot mod (code + assets) + no-Steam patch
  test_env/pair/<guest letter>/DungeonSettlers.app  stock + no-Steam patch
The rig (rig.py, mp2_test.py helpers) is used READ-ONLY: its instance folder is pointed at test_env/pair, and
the multiplayer dylib is a snapshot copy. Hidden launch, rig guard, owner prefs checked before and after.
"""
import json, os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAIRDIR = os.path.join(ROOT, 'test_env', 'pair')
RIG = os.path.expanduser('~/ds_multiplayer/rig')
pair = sys.argv[1]
os.environ['MP2_PAIR'] = pair
sys.path.insert(0, RIG)
import rig  # noqa: E402

rig.INST = PAIRDIR
rig.__file__ = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pair_guard.py')  # guard sees test_env/pair
snap = os.path.join(PAIRDIR, 'rigdylib')
os.makedirs(snap, exist_ok=True)
for f in ('dsrig.dylib', 'det_ranges.tsv'):
    shutil.copy2(os.path.join(rig.ROOT, 'build', f), os.path.join(snap, f))
rig.DYLIB = os.path.join(snap, 'dsrig.dylib')
import mp2_test as m  # noqa: E402  (its A/B helpers now map to this pair, in test_env/pair)
from demo import make_demo_save  # noqa: E402

TICKS = int(sys.argv[sys.argv.index('--ticks') + 1]) if '--ticks' in sys.argv else 3000
H, G = pair[0], pair[1]
log = m.log


def quickslots(path):
    """QuickSlots length per player unit in a save (5 = weapon-slot format, 4 = stock)"""
    d = json.load(open(path, encoding='utf-8-sig'))
    qs = d.get('PlayerUnitsSaveData', {}).get('QuickSlots', {})
    return sorted({len(v.get('QuickSlots', [])) for v in qs.values()}) if isinstance(qs, dict) else qs


def main():
    for x in pair:
        assert os.path.exists(os.path.join(rig.idir(x), rig.EXE_REL)), f'copy {x} missing: run make_pair_copies first'
    assert rig.check_prefs(), 'owner prefs differ from backup before start'
    port = rig.pair_port(H)
    json.dump({'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True},
              open(os.path.join(rig.idir(H), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Guest', 'partial_resync': False},
              open(os.path.join(rig.idir(G), 'mp.json'), 'w'))
    res = {'pair': pair, 'ticks': TICKS}
    try:
        # host save: the demo save, migrated to the mod's 5-slot format (what a player of the mod has)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo_buffed.json')
        make_demo_save(src)
        r = subprocess.run([sys.executable, os.path.expanduser('~/ds_weaponslot/migrate_saves.py'), '--apply', src],
                           capture_output=True, text=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        log('migrate:', (r.stdout + r.stderr).strip()[-300:])
        res['host_save_quickslots'] = quickslots(src)

        rig.start('A', fps=10)
        rig.wait_for('A', lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host', timeout=1500)
        log('host load', m.c('A', cmd='load_save', file=src, det=1))
        m.wait('host locked', lambda: m.st('A').get('det', {}).get('locked'))
        log('host locked at tick', m.st('A')['tick'])
        rig.start('B', fps=30)
        rig.wait_for('B', lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest', timeout=1500)
        try:
            sb = m.wait('guest loaded the host save and locked',
                        lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1 else None)(m.st('B')),
                        timeout=600)
            res['guest_joined'] = True
            log('guest locked at tick', sb['tick'], 'mp', sb.get('mp'))
        except Exception as e:
            res['guest_joined'] = False
            res['join_error'] = str(e)
            res['guest_status'] = m.st('B') if rig.alive(rig.read_pid(G)) else 'guest process gone'
            raise
        sync_saves = [os.path.join(rig.idir(G), 'data', 'Saves', f) for f in os.listdir(os.path.join(rig.idir(G), 'data', 'Saves'))
                      if f.endswith('.json')] if os.path.isdir(os.path.join(rig.idir(G), 'data', 'Saves')) else []
        res['guest_received_save_quickslots'] = {os.path.basename(p): quickslots(p) for p in sync_saves[:3]}
        time.sleep(5)

        us = m.units('A')
        players = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u]
        near_portal = lambda c_: 446 <= c_[0] <= 472 and 181 <= c_[1] <= 205
        enemies = [u for u in us if u['faction'] == 3 and not u['dead'] and 'cell' in u and not near_portal(u['cell'])]
        mine = {'A': players[:2], 'B': players[2:4]}
        for x in 'AB':
            for u in mine[x]:
                m.c(x, cmd='mp_claim', guid=u['guid'])
            m.c(x, cmd='ord_hover', n=0)
        time.sleep(3)
        t_start = m.st('A')['tick']
        d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
        plan = [(x, u, min(enemies, key=lambda e: d2(u, e))['cell'], u['cell']) for x in 'AB' for u in mine[x]]
        phase, last, stuck, popups = 0, 0, [0, time.time()], 0
        while True:
            sa = m.st('A')
            done = sa['tick'] - t_start
            if phase == 0 or (phase == 1 and done > TICKS * 0.35) or (phase == 2 and done > TICKS * 0.55) or (phase == 3 and done > TICKS * 0.75):
                for x, u, far, home in plan:
                    tgt = far if phase in (0, 2) else home
                    log(f'phase {phase}: {x} orders {u["name"][:10]} -> {tgt}: {"sent" if m.order_move(x, u["guid"], tgt) else "INJECT FAILED"}')
                phase += 1
            if time.time() - last > 30:
                log(f'host tick {sa["tick"]} | guest tick {m.st("B")["tick"]} | desync host {sa.get("mp", {}).get("desyncs")}'); last = time.time()
            if done >= TICKS:
                break
            if m.handle_portal():
                stuck[:] = [m.st('A')['tick'], time.time()]; continue
            if sa['tick'] != stuck[0]:
                stuck[:] = [sa['tick'], time.time()]
            elif time.time() - stuck[1] > 15 and popups < 5:
                for x in 'AB':
                    m.c(x, cmd='mp_popup', reason=4, add=0)
                popups += 1; stuck[1] = time.time()
            elif time.time() - stuck[1] > 60:
                raise RuntimeError('world stopped during the fight')
            time.sleep(2)
        time.sleep(3)
        ma, mb = m.mp('A'), m.mp('B')
        n, first = m.fp_equal_from(t_start)
        hp0 = m.summary(us)
        m.c('B', cmd='mp_pause', on=1)
        same = m.settle_both()
        la, lb = m.units('A'), m.units('B')
        key = lambda us_: sorted((u['guid'], u.get('cell'), u['hp'], u['dead'], u['task']) for u in us_)
        res.update({
            'fp_common_ticks': n, 'fp_first_diff_tick': first, 'fp_equal': n > TICKS * 0.9 and first is None,
            'orders_applied': (ma.get('orders'), mb.get('orders')), 'desyncs': (ma.get('desyncs'), mb.get('desyncs')),
            'units_equal_at_paused_tick': same is not None and key(la) == key(lb), 'paused_tick': same,
            'enemy_hp_start_end': (hp0.get('f3'), m.summary(la).get('f3')),
        })
    except Exception as e:
        res['error'] = repr(e)
    finally:
        for x in 'AB':
            try:
                rig.stop(x)
            except Exception as e:
                log('stop', x, e)
        res['owner_prefs_unchanged'] = rig.check_prefs()
        json.dump(res, open(os.path.join(PAIRDIR, 'result.json'), 'w'), indent=1)
        log('RESULT', json.dumps(res))


if __name__ == '__main__':
    main()
