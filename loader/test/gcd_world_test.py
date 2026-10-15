#!/usr/bin/env python3
"""Game tests on one hidden copy (L) through dsload, build 25846389. Never the owner's game or saves (copies only).
  stock run: profile multiplayer (rig control) + gcd-probe          -> real global cooldown without the mod
  mod run:   profile multiplayer + gcd-cdr + gcd-probe              -> real global cooldown with the mod
  world run (task 3) is part of the stock run: the launcher session says the save's world uses another mod set ->
             LoadCampaign refused with the notice; then the session says the same mod set -> the load goes through.
gcd-probe (test/probe/gcd-probe, test only) measures each global cooldown as the sum of the game's own dt from the
first count-down tick until it reaches 0, and with DSL_GCDPROBE_FORCE=N starts N global cooldowns on units that have
cooldown reduction through the game's public SetGlobalCooldown() (the function gcd-cdr hooks).
usage: gcd_world_test.py [stock|mod|both]   env GAME_S (seconds of play per run, default 150)"""
import json, os, re, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lrig
X = 'L'
GAME_S = int(os.environ.get('GAME_S', '150'))
GUID = 'f816ebc6-195c-432c-9fff-754b75ee3738'          # CampaignGuid of the ElvenExpedition test save
out = os.path.join(lrig.LROOT, 'test_env', 'gcd_out', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(out, exist_ok=True)
app = os.path.join(lrig.idir(X), 'DungeonSettlers.app')
mods = os.path.join(app, 'Contents', 'Resources', 'mods')
probe_src = os.path.join(lrig.LROOT, 'test_env', 'probe_build', 'gcd-probe')
log = lambda *a: print('[gcd]', *a, flush=True)


def install_probe():
    d = os.path.join(mods, 'gcd-probe')
    if os.path.exists(d):
        os.makedirs(os.path.join(lrig.LROOT, 'test_env', 'old'), exist_ok=True)
        shutil.move(d, os.path.join(lrig.LROOT, 'test_env', 'old', f'gcd-probe.{time.time():.0f}'))   # never rm
    shutil.copytree(probe_src, d)


def profile(name, enabled):
    p = os.path.join(out, f'profile_{name}.json')
    json.dump({'enabled': enabled, 'order': enabled, 'provided': ['notch-fix']}, open(p, 'w'))
    return p


def session(worlds):
    p = os.path.join(out, 'session.json')
    json.dump({'world': GUID, 'modset': 'aaa', 'rule': 'mods', 'worlds': worlds, 'modset_lines': []}, open(p + '.tmp', 'w'))
    os.replace(p + '.tmp', p)
    return p


def dsload_log():
    return open(os.path.join(lrig.idir(X), 'dsload.log'), errors='replace').read()


def load(src):
    log('load_save:', lrig.send(X, {'cmd': 'load_save', 'file': src}))


def run(name, enabled, world_test):
    res = {'run': name, 'enabled': enabled}
    os.environ['DSL_PROFILE'] = profile(name, enabled)
    os.environ['DSL_GCDPROBE_FORCE'] = '8'
    os.environ['DSML_MODSET'] = 'aaa'
    os.environ['DSML_SESSION'] = session({GUID: 'bbb'} if world_test else {})
    os.environ.pop('LRIG_EXTRA_INSERT', None)
    try:
        lrig.start(X)
        lrig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title screen')
        work = os.path.join(lrig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(lrig.SAVE)); shutil.copy2(lrig.SAVE, src)
        if world_test:
            load(src)
            time.sleep(25)
            st = lrig.send(X, {'cmd': 'status'})
            shot = os.path.join(out, 'world_refused.png')
            lrig.send(X, {'cmd': 'screenshot', 'path': shot, 'w': 960, 'h': 540})
            L = dsload_log()
            res['world_refused'] = dict(scene=st.get('scene'), tick=st.get('tick'), busy=st.get('busy'), screenshot=shot,
                                        log=[l for l in L.splitlines() if 'world check' in l])
            log('after refused load:', st)
            session({GUID: 'aaa'})                     # same mod set now: the load must go through
            time.sleep(2)
        load(src)
        st = lrig.wait_for(X, lambda s: s.get('scene') == 2 and not s.get('busy') and s.get('map', -1) >= 0 and s.get('tick', -1) > 0,
                           'campaign loaded', timeout=400, every=2)
        log('loaded:', st)
        lrig.send(X, {'cmd': 'set_speed', 'speed': 1})
        t0 = time.time()
        while time.time() - t0 < GAME_S:
            time.sleep(15)
            if not lrig.alive(lrig.read_pid(X)):
                raise RuntimeError('game died')
        res['status_end'] = lrig.send(X, {'cmd': 'status'})
        res['alive_at_end'] = True
    finally:
        lrig.stop(X)
    L = dsload_log()
    shutil.copy2(os.path.join(lrig.idir(X), 'dsload.log'), os.path.join(out, f'dsload_{name}.log'))
    res['world_log'] = [l for l in L.splitlines() if 'world check' in l]
    res['gcd_lines'] = [l for l in L.splitlines() if 'GCD ' in l or 'global cooldown #' in l]
    res['measured'] = [dict(zip(('kind', 'start', 'lasted', 'cdr', 'stock', 'rule'),
                                (m[0], *map(float, m[1:]))))
                       for m in re.findall(r'GCD measured #\d+ \(([^)]*)\): started at ([\d.]+) s, lasted ([\d.]+) game s.*?cooldown reduction ([\d.-]+); stock would be ([\d.]+) s, with the rule ([\d.]+) s', L)]
    res['loaded_line'] = [l for l in L.splitlines() if 'modules:' in l or 'GAME BUILD' in l]
    return res


mode = sys.argv[1] if len(sys.argv) > 1 else 'both'
print('owner prefs identical before:', lrig.check_prefs(restore=False))
install_probe()
mj = os.path.join(lrig.idir(X), 'mp.json')
if os.path.exists(mj):
    os.replace(mj, mj + '.aside')                       # single-player runs; put back at the end
results = []
if mode in ('stock', 'both'):
    results.append(run('stock', ['multiplayer', 'gcd-probe'], world_test=True))
if mode in ('mod', 'both'):
    results.append(run('mod', ['multiplayer', 'gcd-cdr', 'gcd-probe'], world_test=False))
if os.path.exists(mj + '.aside'):
    os.replace(mj + '.aside', mj)
print('owner prefs identical after:', lrig.check_prefs(restore=False))
json.dump(results, open(os.path.join(out, 'results.json'), 'w'), indent=1)
for r in results:
    print('RESULT', r['run'], json.dumps({k: r[k] for k in ('measured', 'loaded_line') if k in r}))
    if 'world_refused' in r: print('WORLD', json.dumps(r['world_refused']))
print('out', out)
