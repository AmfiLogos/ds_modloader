#!/usr/bin/env python3
"""In-game wrong-modset check on a leased pair: the guest joins with other gameplay mods than the host and must be
refused (DSNET_REJ_MODSET) with a message that names the missing / extra mods.

  modset_pair.py <PAIR> <dylib> [--case 1,2,3]

<dylib>: a dsrig.dylib built with corenext's HELLO modset fields + modcheck (modcheck/src_modset.patch).
Each copy gets its own DSML_SESSION file (what the launcher writes): rule, modset hash, modset_lines.
Cases: 1 Allow mods, host {hell 1.0.0, multiplayer 1.1.0}, guest {cheaper-recruits, multiplayer 1.0.0};
       2 Vanilla check, host none, guest {hell}.
       3 same set on both: the guest joins (no refusal).
Copies: test_env/pair/<letter>/DungeonSettlers.app (any copy that boots, e.g. the weaponslot pair copies).
Result: test_env/pair/modset_result.json.
"""
import hashlib, json, os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAIRDIR = os.path.join(ROOT, 'test_env', 'pair')
RIG = os.path.expanduser('~/ds_multiplayer/rig')
pair, dylib = sys.argv[1], sys.argv[2]
os.environ['MP2_PAIR'] = pair
sys.path.insert(0, RIG)
import rig  # noqa: E402

rig.INST = PAIRDIR
rig.__file__ = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pair_guard.py')
snap = os.path.join(PAIRDIR, 'rigdylib_modset')
os.makedirs(snap, exist_ok=True)
shutil.copy2(dylib, os.path.join(snap, 'dsrig.dylib'))
shutil.copy2(os.path.join(rig.ROOT, 'build', 'det_ranges.tsv'), os.path.join(snap, 'det_ranges.tsv'))
rig.DYLIB = os.path.join(snap, 'dsrig.dylib')
import mp2_test as m  # noqa: E402
from demo import make_demo_save  # noqa: E402

H, G = pair[0], pair[1]
log = m.log
SESSION = {}                                              # letter -> session file path

# rig.start only passes DSRIG_* variables: add DSML_SESSION to the launch spec before the guard reads it
_popen = subprocess.Popen
def _popen_with_session(args, *a, **k):
    if isinstance(args, list) and len(args) >= 3 and args[-2] == '_guard' and args[-1] in SESSION:
        p = os.path.join(rig.INST, args[-1], 'launch.json')
        spec = json.load(open(p)); spec['env']['DSML_SESSION'] = SESSION[args[-1]]
        json.dump(spec, open(p, 'w'))
    return _popen(args, *a, **k)
rig.subprocess.Popen = _popen_with_session


def session(x, rule, lines):
    lines = sorted(lines)
    s = {'rule': rule, 'modset': hashlib.sha256('\n'.join(lines).encode()).hexdigest(), 'modset_lines': lines}
    p = os.path.join(rig.INST, x, 'dsml_session.json')
    json.dump(s, open(p, 'w')); SESSION[x] = p


def rlog(x):
    p = os.path.join(rig.INST, x, 'rig.log')
    return open(p, errors='replace').read() if os.path.exists(p) else ''


def run_case(name, host_rule, host_lines, guest_rule, guest_lines, expect_refused, must_have):
    session(H, host_rule, host_lines); session(G, guest_rule, guest_lines)
    port = rig.pair_port(H)
    json.dump({'role': 'host', 'port': port, 'name': 'Host'}, open(os.path.join(rig.idir(H), 'mp.json'), 'w'))
    json.dump({'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Guest'}, open(os.path.join(rig.idir(G), 'mp.json'), 'w'))
    r = {'case': name}
    try:
        rig.start('A', fps=10)
        rig.wait_for('A', lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host', timeout=1500)
        work = os.path.join(rig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo.json'); make_demo_save(src)
        log(name, 'host load', m.c('A', cmd='load_save', file=src, det=1))
        m.wait('host locked', lambda: m.st('A').get('det', {}).get('locked'))
        g0 = len(rlog(G))
        rig.start('B', fps=30)
        rig.wait_for('B', lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest', timeout=1500)
        t0 = time.time(); w = {}
        while time.time() - t0 < 300:
            w = m.c('B', cmd='mp_world')
            st = m.st('B')
            if w.get('last_reject') or (st.get('mp', {}).get('started') and st.get('mp', {}).get('loads', 0) >= 1):
                break
            time.sleep(3)
        time.sleep(8)                                        # the title's notice box opens ~3 s after the title is calm
        gl = rlog(G)[g0:]
        refusal = [l for l in gl.splitlines() if 'modset refusal' in l]
        notice = [l for l in gl.splitlines() if 'mpmenu: notice box' in l or 'mpmenu: no notice box' in l]
        r.update({'last_reject': w.get('last_reject'), 'ended': w.get('ended'), 'refusal_log': refusal[-1:] if refusal else [],
                  'notice_log': notice[-1:], 'joined': bool(m.st('B').get('mp', {}).get('started'))})
        text = refusal[-1] if refusal else ''
        r['pass'] = (w.get('last_reject') == 8 and all(s in text for s in must_have) and not r['joined']) if expect_refused \
            else (not w.get('last_reject') and r['joined'])
    except Exception as e:
        r['error'] = repr(e); r['pass'] = False
    finally:
        for x in 'BA':
            try:
                rig.stop(x)
            except Exception as e:
                log('stop', x, e)
    log('CASE', json.dumps(r))
    return r


def main():
    assert rig.check_prefs(), 'owner prefs differ from backup before start'
    cases = []
    cases.append(lambda: run_case('mods differ', 'mods', ['hell-difficulty@1.0.0:aa', 'multiplayer@1.1.0:cc'],
                        'mods', ['cheaper-recruits@1.0.0:bb', 'multiplayer@1.0.0:cc'], True,
                        ['Missing (the host uses them): hell-difficulty 1.0.0', 'Extra (the host does not use them): cheaper-recruits 1.0.0',
                         'Other version: multiplayer (host 1.1.0, you 1.0.0)']))
    cases.append(lambda: run_case('vanilla check', 'vanilla', [], 'mods', ['hell-difficulty@1.0.0:aa'], True,
                        ['Vanilla check', 'Turn off: hell-difficulty 1.0.0']))
    cases.append(lambda: run_case('same mods', 'mods', ['hell-difficulty@1.0.0:aa'], 'mods', ['hell-difficulty@1.0.0:aa'], False, []))
    pick = [int(x) for x in sys.argv[sys.argv.index('--case') + 1].split(',')] if '--case' in sys.argv else [1, 2, 3]
    out = [cases[i - 1]() for i in pick]                  # one case per call keeps a run under the tool timeout
    res = {'pair': pair, 'cases': out, 'all_pass': all(c['pass'] for c in out), 'owner_prefs_unchanged': rig.check_prefs()}
    json.dump(res, open(os.path.join(PAIRDIR, f'modset_result_{"_".join(map(str, pick))}.json'), 'w'), indent=1)
    log('RESULT', json.dumps(res))


if __name__ == '__main__':
    main()
