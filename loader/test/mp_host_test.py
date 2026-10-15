#!/usr/bin/env python3
"""Single-copy multiplayer self-test through dsload (no test pair needed).
Copy L hosts a session (mp.json role host) with multiplayer + gcd-cdr + floor-logger + a foreign inserted dylib.
Loads the x10 demo save with det on -> lockstep locks; player units get orders through the real capture path
(ord_inject select + right-click -> ord capture -> dsnet host -> replay) towards enemies, so they fight (skills ->
global cooldown -> gcd-cdr hook). Soak SOAK_S seconds (default 600): alive, tick advancing, det tick_errors 0.
Owner prefs only CHECKED. Writes test_env/smoke_out/<time>/mp_host_results.json."""
import json, os, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(1, os.path.expanduser('~/ds_multiplayer/rig'))
import lrig
X = 'L'
SOAK = int(os.environ.get('SOAK_S', '600'))
out = os.path.join(lrig.LROOT, 'test_env', 'smoke_out', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(out, exist_ok=True)
os.environ['LRIG_EXTRA_INSERT'] = os.path.join(lrig.LROOT, 'test_env', 'foreign.dylib')
os.environ['FOREIGN_OUT'] = os.path.join(lrig.idir(X), 'foreign.txt')
SESSION = os.path.join(lrig.idir(X), 'dsml_session.json')
GUID = 'f816ebc6-195c-432c-9fff-754b75ee3738'                         # CampaignGuid of the demo save
HELL = os.environ.get('HELL', '1') == '1'
def session(world_modset):
    json.dump({'world': GUID, 'modset': 'mine', 'rule': 'mods', 'offer': None, 'offer_error': None, 'client_mods': [],
               'worlds': {GUID: world_modset}, 'modset_lines': []}, open(SESSION + '.tmp', 'w')); os.replace(SESSION + '.tmp', SESSION)
os.environ['DSML_SESSION'] = SESSION; os.environ['DSML_MODSET'] = 'mine'
res = {}
def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
def c(**k): return lrig.send(X, k, timeout=120)
def st():
    for _ in range(10):
        s = c(cmd='status')
        if 'tick' in s: return s
        time.sleep(3)
    raise RuntimeError(f'status not answered: {s}')
def check(name, ok, info=''):
    res[name] = bool(ok); log(('PASS ' if ok else 'FAIL ') + name, info)
def inject(ev, **f):
    d = {'cmd': 'ord_inject', 'ev': ev}; d.update({k.replace('_', '@'): v for k, v in f.items()}); return c(**d)
def order_move(guid, cell):
    c(cmd='ord_hover', n=0)
    return inject('SelectPlayerUnitRequested', i32_16=0, guid_20=guid).get('ok') and \
        inject('InteractGridRequested', v2_16=f'{cell[0]},{cell[1]}', u8_24=0).get('ok')
def portal():
    r = c(cmd='ui_result')
    if not r.get('open'): return 0
    log('portal reached: Return + confirm as the host')
    c(cmd='ui_result', action='return'); time.sleep(3)
    for _ in range(3):
        c(cmd='ui_result', action='confirm'); time.sleep(4)
        if not c(cmd='ui_result').get('open'): break
    return 1
def gcd_lines():
    p = os.path.join(lrig.idir(X), 'dsload.log')
    return [l for l in open(p) if '[gcd-cdr] global cooldown #' in l] if os.path.exists(p) else []

json.dump({'role': 'host', 'port': lrig.pair_port(X), 'name': 'L', 'allow_speedup': True, 'allow_pause': True},
          open(os.path.join(lrig.idir(X), 'mp.json'), 'w'))
log('owner prefs identical before:', lrig.check_prefs(restore=False))
try:
    lrig.start(X, fps=30)
    lrig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=900)
    work = os.path.join(lrig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
    import rig as _rig; _rig.SAVE = lrig.SAVE                        # demo.py reads rig.SAVE (owner backup, read only)
    from demo import make_demo_save
    src = os.path.join(work, 'demo_buffed.json'); log('x10 demo save:', make_demo_save(src))
    if HELL:                                                          # play the demo world on Hell (CombatDifficulty 5)
        d = json.load(open(src))
        d['CampaignSaveHeader']['CombatDifficulty'] = 5
        d['CampaignSettingSaveData']['CampaignStartSettingSaveData']['CombatDifficulty'] = 5
        json.dump(d, open(src + '.tmp', 'w')); os.replace(src + '.tmp', src)
    # mods per world: the launcher says this world was played with another mod set -> refused
    session('other-modset')
    log('load (world registered with another mod set)', c(cmd='load_save', file=src, det=1))
    time.sleep(20)
    s = st(); shot = os.path.join(out, 'refused.png'); c(cmd='screenshot', path=shot, w=960, h=540)
    dl = open(os.path.join(lrig.idir(X), 'dsload.log')).read()
    check('world with another mod set refused, game stays on the title', s.get('scene') == 1 and 'world check: REFUSED' in dl and lrig.alive(lrig.read_pid(X)),
          f"scene {s.get('scene')} busy {s.get('busy')}; popup screenshot {shot}")
    session('mine')
    log('load (same mod set)', c(cmd='load_save', file=src, det=1))
    t0 = time.time()
    while not st().get('det', {}).get('locked'):
        if time.time() - t0 > 600: raise RuntimeError('det never locked')
        time.sleep(2)
    s = st(); log('locked at tick', s['tick'], 'mp', s.get('mp'))
    dl = open(os.path.join(lrig.idir(X), 'dsload.log')).read()
    check('same mod set: world loads', 'uses this mod set: allowed' in dl and s.get('scene') == 2)
    if HELL:
        check('Hell module: 20 code patches applied, world on CombatDifficulty 5 loaded', '[hell-difficulty] Hell difficulty ready: 20 code patches' in dl and s.get('scene') == 2)
    check('host session up, lockstep locked', s.get('det', {}).get('locked') and s.get('mp'), json.dumps(s.get('mp')))
    t1 = time.time()
    while st().get('mp', {}).get('busy_mask', 1) and time.time() - t1 < 300: time.sleep(2)
    time.sleep(5)
    us = c(cmd='list_units')['units']
    near_portal = lambda c_: 446 <= c_[0] <= 472 and 181 <= c_[1] <= 205
    players = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u]
    enemies = [u for u in us if u['faction'] == 3 and not u['dead'] and 'cell' in u and not near_portal(u['cell'])]
    for u in players: log('claim', u['name'], c(cmd='mp_claim', guid=u['guid']))
    d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
    orders0 = st()['det'].get('orders', 0)
    sent = 0
    for u in players:
        if enemies:
            e = min(enemies, key=lambda e: d2(u, e)); sent += bool(order_move(u['guid'], e['cell'])); log('order', u['name'], '->', e['cell'])
    time.sleep(15)
    s = st()
    moved = {u['guid']: u.get('cell') for u in c(cmd='list_units')['units'] if u['guid'] in {p['guid'] for p in players}}
    nmoved = sum(1 for p in players if moved.get(p['guid']) != p['cell'])
    log('mp after orders:', {k: s.get('mp', {}).get(k) for k in ('orders', 'orders_bad', 'orders_dropped_owner', 'ticks', 'busy_mask')}, 'det orders', s['det'].get('orders'), 'units moved', nmoved)
    check('orders through the capture path reach the lockstep', s.get('mp', {}).get('orders', 0) >= sent > 0 and nmoved > 0,
          f"mp orders {s.get('mp', {}).get('orders')}, sent {sent}, units moved {nmoved}/{len(players)}")
    t0 = time.time(); ticks = []; errs = []
    while time.time() - t0 < SOAK:
        time.sleep(20)
        if not lrig.alive(lrig.read_pid(X)): raise RuntimeError(f'process died after {time.time() - t0:.0f} s of soak')
        portal()
        s = st(); ticks.append(s['tick']); errs.append(s['det'].get('tick_errors', 0))
        if len(ticks) % 3 == 1:
            uu = c(cmd='list_units')['units']
            log('  players', [(u['name'][:6], u.get('cell'), round(u.get('hp', 0)), u.get('task')) for u in uu if u['faction'] == 1 and 'cell' in u],
                'enemies alive', sum(1 for u in uu if u['faction'] == 3 and not u['dead']))
            log(f"soak {time.time() - t0:4.0f} s tick {s['tick']} locked {s['det'].get('locked')} tick_errors {s['det'].get('tick_errors')} gcd calls logged {len(gcd_lines())}")
        if len(ticks) % 6 == 0 and enemies:                         # keep them fighting
            us = c(cmd='list_units')['units']
            players = [u for u in us if u['faction'] == 1 and not u['dead'] and 'cell' in u]
            enemies = [u for u in us if u['faction'] == 3 and not u['dead'] and 'cell' in u and not near_portal(u['cell'])]
            for u in players:
                if enemies: order_move(u['guid'], min(enemies, key=lambda e: d2(u, e))['cell'])
    check(f'soak {SOAK} s: alive, tick advancing, no det tick errors', lrig.alive(lrig.read_pid(X)) and ticks and ticks[-1] > ticks[0] and not any(errs),
          f'ticks {ticks[0] if ticks else None} -> {ticks[-1] if ticks else None}, tick_errors max {max(errs) if errs else None}')
    g = gcd_lines()
    check('gcd-cdr hook ran inside the multiplayer session', len(g) > 0, (g[0].strip() if g else 'no global cooldown logged'))
    res['gcd_samples'] = [l.strip() for l in g[:5]]
finally:
    lrig.stop(X)
    mj = os.path.join(lrig.idir(X), 'mp.json'); os.replace(mj, mj + '.done')
log('owner prefs identical after:', lrig.check_prefs(restore=False))
json.dump(res, open(os.path.join(out, 'mp_host_results.json'), 'w'), indent=1)
print('RESULT', json.dumps(res))
