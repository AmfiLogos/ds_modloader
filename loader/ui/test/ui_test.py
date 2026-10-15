#!/usr/bin/env python3
"""TEST ONLY: Mods window proof on test copy Y (never the owner's install, saves or screen).

The game runs with this branch's loader (lane L1's build.sh, which compiles ../dsl_win.c) + L1's built multiplayer /
gcd-cdr / hell-difficulty modules + the test module dsltest-win (adds a tab through the SDK with --tab). Its window sits far off every display (DSL_SOFT_STAGE), never activates; a guard kills it if
a window with alpha > 0 is ever on a display or it becomes frontmost. Commands go through DSL_UI_TEST (files); a click calls
the named control's own callback (the kit's mouse path is lane L1's uikit_test). Shots = the game's own frames (PNG) in <out>/.
Usage: ui_test.py <out dir> [--apply]
"""
import json, os, shutil, signal, subprocess, sys, threading, time, glob
sys.path.insert(0, os.path.expanduser('~/ds_launcher/loader/test'))
import lrig as r

X = 'Y'
I = r.idir(X)
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'build', 'run'))
APPLY = '--apply' in sys.argv
TAB = '--tab' in sys.argv
UI = os.path.join(OUT, 'ui')
MODS = os.path.join(I, 'mods')
PROF = os.path.join(I, 'data', 'Mods', 'active.json')
STAGE = '-20000,-20000'
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
stop_guard = threading.Event(); violations = []
results = {'steps': [], 'violations': violations}


def game_pids():
    out = subprocess.run(['pgrep', '-f', os.path.join(I, r.EXE_REL)], capture_output=True, text=True).stdout
    return [int(p) for p in out.split()]


def guard():
    while not stop_guard.is_set():
        pids = game_pids()
        if pids:
            bad = [w for w in r.windows(pids) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
            fp = r.front_pid()
            if bad or fp in pids:
                violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                for p in pids:
                    os.kill(p, signal.SIGKILL)
                log('GUARD KILLED', bad, fp)
        stop_guard.wait(0.5)


def install_mods():
    fw = os.path.join(I, 'DungeonSettlers.app', 'Contents', 'Frameworks')
    shutil.copy2(os.path.join(HERE, '..', '..', 'build', 'dsload.dylib'), os.path.join(fw, 'dsload.dylib.tmp')); os.replace(os.path.join(fw, 'dsload.dylib.tmp'), os.path.join(fw, 'dsload.dylib'))
    os.makedirs(MODS, exist_ok=True)
    src = os.path.expanduser('~/ds_launcher_l1core/loader/build/mods')   # lane L1's built modules
    for m in ('multiplayer', 'gcd-cdr', 'hell-difficulty'):
        d = os.path.join(MODS, m)
        if os.path.exists(d):
            os.makedirs(os.path.join(I, 'old'), exist_ok=True); os.replace(d, os.path.join(I, 'old', f'{m}_{int(time.time())}'))
        shutil.copytree(os.path.join(src, m), d)
    d = os.path.join(MODS, 'test-bepinex'); os.makedirs(d, exist_ok=True)
    shutil.copy2(os.path.join(HERE, 'fake-bepinex', 'mod.json'), os.path.join(d, 'mod.json'))
    d = os.path.join(MODS, 'dsltest-win')
    os.makedirs(d, exist_ok=True)
    for f in ('dsltest-win.dylib', 'mod.json'):
        shutil.copy2(os.path.join(HERE, 'build', 'mods', 'dsltest-win', f), os.path.join(d, f + '.tmp')); os.replace(os.path.join(d, f + '.tmp'), os.path.join(d, f))


def start():
    r.prepare_data(X)
    os.makedirs(os.path.dirname(PROF), exist_ok=True)
    for f in ('groups.json', 'pending.json', 'worlds.json'):
        p = os.path.join(os.path.dirname(PROF), f)
        if os.path.exists(p):
            os.makedirs(os.path.join(I, 'old'), exist_ok=True); os.replace(p, os.path.join(I, 'old', f'{f}_{int(time.time())}'))
    json.dump({'enabled': ['multiplayer', 'dsltest-win'], 'order': ['multiplayer', 'dsltest-win']}, open(PROF, 'w'))
    os.makedirs(UI, exist_ok=True)
    for f in glob.glob(os.path.join(UI, '*.json')):
        os.replace(f, f + '.old')
    if os.path.exists(r.sockpath(X)):
        os.unlink(r.sockpath(X))
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', 'DYLD_INSERT_LIBRARIES': os.path.join(I, r.DYLIB_REL),
           'DSL_MODS_DIR': MODS, 'DSL_PROFILE': PROF, 'DSL_LOG_DIR': I, 'DSL_SOFT_STAGE': STAGE, 'DSL_SOFT_NOACTIVATE': '1',
           'DSL_UI_TEST': UI, **({'DSL_UI_TEST_TAB': '1'} if TAB else {}), 'DSL_NO_ALERT': '1', 'DSL_SOFT_CAPTURE': os.path.join(OUT, 'softcap')}
    os.makedirs(os.path.join(OUT, 'softcap'), exist_ok=True)
    args = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0',
            '-screen-width', '1280', '-screen-height', '800']
    p = subprocess.Popen(args, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=lambda: os.nice(5), start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n')
    log('started pid', p.pid)
    return p.pid


seq = [0]


def cmd(op, timeout=20, **kw):
    seq[0] += 1
    s = str(seq[0]); kw.update(op=op, seq=s)
    tmp = os.path.join(UI, 'cmd.json.tmp')
    open(tmp, 'w').write(json.dumps(kw)); os.replace(tmp, os.path.join(UI, 'cmd.json'))
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            res = json.load(open(os.path.join(UI, 'result.json')))
            if res.get('seq') == s:
                return res
        except Exception:
            pass
        time.sleep(0.05)
    raise RuntimeError(f'no answer to {kw}')


def step(label, op, check=None, **kw):
    res = cmd(op, **kw)
    ok = bool(res.get('ok')) and (check(res) if check else True)
    results['steps'].append({'step': label, 'op': op, 'args': kw, 'ok': ok, 'msg': res.get('msg'), 'open': res.get('open'), 'tab': res.get('tab')})
    log(('PASS' if ok else 'FAIL'), label, res.get('msg', ''))
    return res


def widgets():
    return {w['name']: w for w in cmd('state')['controls']}


def shot(name):
    time.sleep(0.4)                                   # a few frames so the change is drawn
    step('shot ' + name, 'shot', name=name)


def wait_title(limit=240):
    t0 = time.time()
    while time.time() - t0 < limit:
        try:
            st = r.send(X, {'cmd': 'status'}, timeout=5, connect_wait=0)
            if st.get('scene') == 1 and not st.get('busy'):
                return cmd('state', timeout=10)
        except Exception:
            pass
        time.sleep(0.5)
    raise RuntimeError('no title screen')


def main():
    if game_pids():
        raise SystemExit('copy T already running')
    os.makedirs(OUT, exist_ok=True)
    install_mods()
    g = threading.Thread(target=guard, daemon=True); g.start()
    err = None
    try:
        a = start()
        wait_title(); log('title screen'); time.sleep(5)
        step('open', 'open', lambda s: s['open'] == 1 and s['tab'] == 0, tab='mods')
        shot('01_open')
        w = widgets()
        results['controls_at_open'] = sorted(w)
        log('mods', {k: v['on'] for k, v in w.items() if k.startswith('mod:')})
        step('no change: status says how', 'state', lambda s: 'Switch mods on or off' in w['status']['text'])
        step('hidden test module not listed', 'state', lambda s: 'mod:dsltest-win' not in w)
        step('BepInEx mark on the BepInEx mod only', 'state', lambda s: 'mark:test-bepinex' in w and not any(k.startswith('mark:') and k != 'mark:test-bepinex' for k in w))
        step('switch gcd-cdr on', 'click', target='mod:gcd-cdr')
        w = widgets()
        step('status lists the change', 'state', lambda s: w['mod:gcd-cdr']['on'] == 1 and 'gcd' in w['status']['text'].lower() or 'Global cooldown' in w['status']['text'])
        results['status_after_toggle'] = w['status']['text']
        shot('02_switched')
        step('save as group', 'click', target='group-new')
        shot('03_naming')
        step('type name', 'type', target='group-name', text='Boss runs')
        step('save', 'click', target='group-save')
        gj = json.load(open(os.path.join(os.path.dirname(PROF), 'groups.json')))
        results['groups_json'] = gj
        step('groups.json has Boss runs = multiplayer + gcd-cdr', 'state',
             lambda s: any(g['name'] == 'Boss runs' and sorted(g['mods']) == ['gcd-cdr', 'multiplayer'] for g in gj['groups']))
        shot('04_group_saved')
        step('undo', 'click', target='undo')
        w = widgets()
        step('undo: gcd-cdr off', 'state', lambda s: w['mod:gcd-cdr']['on'] == 0 and 'Switch mods on or off' in w['status']['text'])
        step('pick group', 'click', target='group:Boss runs')
        w = widgets()
        step('group: gcd-cdr on', 'state', lambda s: w['mod:gcd-cdr']['on'] == 1)
        shot('05_group_picked')
        if TAB:
            step('module tab', 'click', target='tab:test')
            shot('07_module_tab')
            step('back to Mods', 'click', target='tab:mods')
        step('esc closes', 'esc', lambda s: s['open'] == 0)
        shot('08_closed')
        step('reopen', 'open', lambda s: s['open'] == 1, tab='mods')
        w = widgets()
        step('reopen: group listed, switches from the profile', 'state', lambda s: 'group:Boss runs' in w and w['mod:gcd-cdr']['on'] == 0)
        guid = 'f816ebc6195c432c9fff754b75ee3738'
        step('open for a world', 'open_world', lambda s: s['open'] == 1, guid=guid, world='Elven Expedition')
        w = widgets()
        step('world: heading + Save button', 'state', lambda s: w['heading']['text'] == 'Mods for Elven Expedition' and w['apply']['text'] == 'Save for this world')
        step('world: switch gcd-cdr on', 'click', target='mod:gcd-cdr')
        step('world: save', 'click', target='apply')
        shot('11_world_saved')
        wj = json.load(open(os.path.join(os.path.dirname(PROF), 'worlds.json')))
        results['worlds_json'] = wj
        step('worlds.json: the world has gcd-cdr', 'state', lambda s: 'gcd-cdr' in json.dumps(wj.get('worlds', {}).get(guid, '')))
        step('world: no restart, running set unchanged', 'state', lambda s: s['open'] == 1 and len(game_pids()) == 1)
        step('normal open again', 'open', lambda s: s['open'] == 1, tab='mods')
        w = widgets()
        step('normal: Apply + running set', 'state', lambda s: w['apply']['text'] == 'Apply' and w['mod:gcd-cdr']['on'] == 0)
        if not APPLY:
            step('delete asks (game notice)', 'click', target='group-del:Boss runs')
            shot('06_delete_notice')
        if APPLY:
            step('switch gcd-cdr on', 'click', target='mod:gcd-cdr')
            step('apply', 'click', target='apply')
            time.sleep(1.0); shot('09_applying')
            t0 = time.time()
            while time.time() - t0 < 150 and a in game_pids():
                time.sleep(0.5)
            results['a_exit_s'] = round(time.time() - t0, 1)
            log('A gone after', results['a_exit_s'], 's; pids', game_pids())
            time.sleep(3)
            res = wait_title(90)
            ms = json.load(open(os.path.join(I, 'dsload_modset.json')))
            ids = [m['id'] for m in ms.get('gameplay', [])] + [m['id'] for m in ms.get('client', [])]
            results['b_modset'] = ids
            step('B runs with gcd-cdr', 'state', lambda s: 'gcd-cdr' in ids)
            time.sleep(4)
            step('B shows the Mods window again by itself', 'state', lambda s: s['open'] == 1)
            w = widgets()
            step('B: gcd-cdr on, nothing to apply', 'state', lambda s: w['mod:gcd-cdr']['on'] == 1 and 'Switch mods on or off' in w['status']['text'])
            shot('10_after_apply')
    except Exception as e:
        err = repr(e); results['error'] = err; log('ERROR', err)
    finally:
        stop_guard.set()
        for f in ('dsload.log', 'dsload.prev.log', 'player.log', 'dsload_modset.json'):
            if os.path.exists(os.path.join(I, f)):
                shutil.copy2(os.path.join(I, f), os.path.join(OUT, f))
        for p in game_pids():
            try:
                r.send(X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception:
                pass
            time.sleep(3)
            try:
                os.kill(p, signal.SIGKILL)
            except OSError:
                pass
        results['pass'] = all(s['ok'] for s in results['steps']) and not violations and not err
        json.dump(results, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log('summary', os.path.join(OUT, 'summary.json'), 'PASS' if results['pass'] else 'FAIL', 'violations', len(violations))


if __name__ == '__main__':
    main()
