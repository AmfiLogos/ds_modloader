#!/usr/bin/env python3
"""The mod tags (owner 2026-11-02, PLAN_v013 item 33; template ui/dsl_modtags.h, rule src/dsl_modtag_rule.h).
 --static <dir>...: every mod.json under the dirs (default: loader/modules and build/mods) gives a tag the loader can
          read: "scope" absent, "client" or "gameplay"; "category" absent, "fix" or "feature"; "vanilla_plus" absent or
          true / false. Break caught: "scope": "Client" or "vanilla_plus": "true" is read as a gameplay mod without a
          word: the wrong icons in the Mods window and the wrong world kind on the Load list. build.sh runs this.
 game run (hidden test copy, default MIC, DSL_TEST_COPY; lease first: ~/ds_multiplayer/rig/rig.py lease <lane> --pid <pid>):
          this tree's build/ (dsload.dylib + every kit mod in build/mods), all mods on. The Mods window's three tabs:
          every mod row has both badges (tag-kind:<id>, tag-side:<id>) with the tag its mod.json gives, the legend
          shows its four parts, and the four icons were made from the game's sprites ("modtags: 4 of 4 icons", no
          "MODTAG-MISSING"). Breaks caught: a row without its tags, a wrong tag, an icon the game no longer gives (a
          game update renamed a sprite or a prefab). Shots: <out>/ui/tags_<tab>.png (local only, never commit).
  modtags_test.py --static [dir ...]          modtags_test.py [out] [--no-build]          Exit 0 = PASS."""
import glob, json, os, re, shutil, signal, subprocess, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); LW = os.path.dirname(HERE); sys.path.insert(0, HERE)

SCOPES, CATS = (None, 'client', 'gameplay'), (None, 'fix', 'feature')


def tag_of(j):
    """the tag a mod.json gives (src/dsl_modtag_rule.h): (kind, side)"""
    client = j.get('scope') == 'client'
    vanilla = client or j.get('category') == 'fix' or j.get('vanilla_plus') is True
    return ('vanilla' if vanilla else 'gameplay'), ('client' if client else 'all')


def static(dirs):
    bad, n = [], 0
    for d in dirs:
        for f in sorted(glob.glob(os.path.join(d, '*', 'mod.json'))):
            n += 1
            try: j = json.load(open(f, encoding='utf-8'))
            except Exception as e: bad.append(f'{f}: not JSON ({e})'); continue
            if j.get('scope') not in SCOPES: bad.append(f'{f}: "scope": {j.get("scope")!r} (client or gameplay)')
            if j.get('category') not in CATS: bad.append(f'{f}: "category": {j.get("category")!r} (fix or feature)')
            if 'vanilla_plus' in j and not isinstance(j['vanilla_plus'], bool): bad.append(f'{f}: "vanilla_plus": {j["vanilla_plus"]!r} (true or false)')
    for b in bad: print('FAIL mod tag:', b)
    print(f'mod tags: {n} mod.json read, {len(bad)} without a tag the loader reads')
    return 1 if bad or not n else 0


if '--static' in sys.argv:
    dirs = [a for a in sys.argv[1:] if not a.startswith('--')] or [os.path.join(LW, 'modules'), os.path.join(LW, 'build', 'mods')]
    sys.exit(static([d for d in dirs if os.path.isdir(d)]))

import lrig as r

ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(ARGS[0] if ARGS else os.path.join(LW, 'test_env', 'l1_out', 'modtags')))
X = os.environ.get('DSL_TEST_COPY', 'MIC'); I = r.idir(X); APP = os.path.join(I, 'DungeonSettlers.app')
WDRV = os.path.join(OUT, 'ui'); MODS_SRC = os.path.join(LW, 'build', 'mods')
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)
checks = []; violations = []; stop = threading.Event()


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:600]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def game_pids():
    out = subprocess.run(['pgrep', '-f', '^' + os.path.join(I, r.EXE_REL)], capture_output=True, text=True).stdout
    return [int(p) for p in out.split()]


def guard():                                                          # a test copy never shows a window nor takes the front
    while not stop.is_set():
        pids = game_pids()
        if pids:
            bad = [w for w in r.windows(pids) if w['alpha'] > 0 and w['onscreen'] and w['x'] > -5000 and w['y'] > -5000]
            fp = r.front_pid()
            if bad or fp in pids:
                violations.append({'t': time.time(), 'windows': bad, 'front': fp})
                for p in pids: os.kill(p, signal.SIGKILL)
                log('GUARD KILLED', bad, fp)
        stop.wait(0.5)


def put(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copy2(src, dst + '.tmp'); os.replace(dst + '.tmp', dst)


def install():
    if game_pids(): raise SystemExit(f'copy {X} is running: not installing over it')
    put(os.path.join(LW, 'build', 'dsload.dylib'), os.path.join(APP, 'Contents', 'Frameworks', 'dsload.dylib'))
    dst = os.path.join(APP, 'Contents', 'Resources', 'mods')
    for src in sorted(glob.glob(os.path.join(MODS_SRC, '*'))):
        if not os.path.isfile(os.path.join(src, 'mod.json')): continue
        for root, _, files in os.walk(src):
            for f in files: put(os.path.join(root, f), os.path.join(dst, os.path.basename(src), os.path.relpath(os.path.join(root, f), src)))
    for f in glob.glob(os.path.join(LW, 'lang', '*.json')): put(f, os.path.join(APP, 'Contents', 'Frameworks', 'lang', os.path.basename(f)))
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(APP, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True)


def kit_mods():
    out = {}
    for f in glob.glob(os.path.join(APP, 'Contents', 'Resources', 'mods', '*', 'mod.json')):
        j = json.load(open(f, encoding='utf-8')); out[j.get('id') or os.path.basename(os.path.dirname(f))] = j
    return out


def start(ids):
    r.require_background_only(X); r.prepare_data(X); r.require_windowed(X)
    pd = os.path.join(I, 'home', 'Library', 'Application Support', f'com.CanOpener.Dungeon-Settlers.mp{X}'); os.makedirs(pd, exist_ok=True)
    for f in ('UserSetting.json', 'OutGame.json'):
        if os.path.exists(os.path.join(I, 'data', f)): shutil.copy2(os.path.join(I, 'data', f), os.path.join(pd, f))
    mods = os.path.join(I, 'data', 'Mods'); os.makedirs(mods, exist_ok=True)
    prof = os.path.join(I, 'modtags_profile.json'); json.dump({'enabled': ids, 'order': ids}, open(prof, 'w'))
    os.makedirs(WDRV, exist_ok=True)
    for f in ('cmd.json', 'result.json'):
        if os.path.exists(os.path.join(WDRV, f)): os.replace(os.path.join(WDRV, f), os.path.join(WDRV, f + '.old'))
    crd = os.path.join(OUT, 'crashrep'); os.makedirs(crd, exist_ok=True); open(os.path.join(crd, 'answer'), 'w').write('no')
    if os.path.exists(r.sockpath(X)): os.replace(r.sockpath(X), r.sockpath(X) + '.old')
    env = {'HOME': os.path.join(I, 'home'), 'CFFIXED_USER_HOME': os.path.join(I, 'home'), 'USER': os.environ.get('USER', ''),
           'LOGNAME': os.environ.get('LOGNAME', ''), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'TMPDIR': os.environ.get('TMPDIR', '/tmp'),
           'DSRIG_DIR': I, 'DSRIG_FPS': '60', **r.ctl_env(), 'DYLD_INSERT_LIBRARIES': os.path.join(APP, 'Contents', 'Frameworks', 'dsload.dylib'),
           'DSL_PROFILE': prof, 'DSML_DATA': os.path.join(I, 'data'), 'DSML_SESSION': os.path.join(mods, 'worlds.json'),
           'DSL_SOFT_STAGE': '-20000,-20000', 'DSL_SOFT_NOACTIVATE': '1', 'DSL_NO_ALERT': '1', 'DSL_NOTCH': '0', 'DSL_UPDATE': '0',
           'DSL_CLOUD': '0', 'DSL_PREWARM': '0', 'DSL_CRASHREP_TEST': crd, 'DSL_BUGREP_DRY': '1', 'DSL_UI_TEST': WDRV}
    env.update(r.soft_test_env())
    ww, wh = r.win_size()
    a = [os.path.join(I, r.EXE_REL), '-logFile', os.path.join(I, 'player.log'), '-screen-fullscreen', '0', '-screen-width', str(ww), '-screen-height', str(wh)]
    def nice5():
        try: os.nice(5)
        except Exception: pass
    p = subprocess.Popen(a, env=env, cwd=I, stdin=subprocess.DEVNULL, stdout=open(os.path.join(I, 'stdout.log'), 'ab'),
                         stderr=subprocess.STDOUT, preexec_fn=nice5, start_new_session=True)
    open(r.pidfile(X), 'w').write(f'{p.pid}\n'); log('started', p.pid)
    return p.pid


SEQ = [0]
def win(op, timeout=20, **kw):
    SEQ[0] += 1; kw.update({'op': op, 'seq': str(SEQ[0])})
    tmp = os.path.join(WDRV, 'cmd.json.tmp'); open(tmp, 'w').write(json.dumps(kw)); os.replace(tmp, os.path.join(WDRV, 'cmd.json'))
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            j = json.load(open(os.path.join(WDRV, 'result.json'), encoding='utf-8', errors='replace'))
            if j.get('seq') == str(SEQ[0]): return j
        except Exception: pass
        time.sleep(0.1)
        if not game_pids(): raise RuntimeError('the game copy is gone (crash?)')
    return {'timeout': op}


def dlog():
    try: return open(os.path.join(I, 'dsload.log'), errors='replace').read()
    except OSError: return ''


def until(pred, limit, what):
    t0 = time.time()
    while time.time() - t0 < limit:
        v = pred()
        if v: return v
        time.sleep(0.5)
        if not game_pids(): raise RuntimeError(f'the game copy is gone while waiting for {what}')
    raise RuntimeError(f'timeout waiting for {what}')


def main():
    os.makedirs(OUT, exist_ok=True)
    if r.owner_game_running() if hasattr(r, 'owner_game_running') else False: log('note: the owner plays now; this copy stays hidden, nice 5')
    if '--no-build' not in sys.argv:
        subprocess.run(f'cd "{LW}" && nice -n 19 zsh build.sh', shell=True, check=True, stdout=subprocess.DEVNULL)
    check('static: every kit mod.json gives a tag', static([MODS_SRC]) == 0)
    install()
    kit = {k: v for k, v in kit_mods().items() if not v.get('hidden')}
    ids = sorted(kit)
    threading.Thread(target=guard, daemon=True).start()
    pid = start(ids)
    try:
        def title():
            try: s = r.send(X, {'cmd': 'status'}, timeout=5, connect_wait=0)
            except Exception: return None
            return s if s.get('scene') == 1 and not s.get('busy') else None
        until(title, 240, 'the title'); time.sleep(4)
        win('open')
        st = until(lambda: (lambda s: s if s.get('open') else None)(win('state')), 30, 'the Mods window')
        seen = {}
        for tab in ('game', 'fixes', 'launcher'):
            win('click', target='tab-mods:' + tab); time.sleep(1.5)
            st = win('state'); c = {x['name']: x for x in st.get('controls', [])}
            rows = [n[4:] for n in c if n.startswith('mod:')]
            for m in rows:
                want = tag_of(kit.get(m, {}))
                got = (c.get('tag-kind:' + m, {}).get('text'), c.get('tag-side:' + m, {}).get('text'))
                seen[m] = got
                check(f'{tab}: {m}: tags {want[0]} + {want[1]}', got == want, got)
            check(f'{tab}: legend with its four parts', all(('legend:' + t) in c for t in ('slogel', 'abyss', 'one', 'all')) or not rows,
                  [n for n in c if n.startswith('legend')])
            win('shot', name='tags_' + tab); time.sleep(0.5)
        check('every shown kit mod was on a tab', set(ids) <= set(seen), sorted(set(ids) - set(seen)))
        L = dlog()
        m = re.findall(r'modtags: (\d) of 4 icons', L)
        check('the four icons made from the game\'s sprites', m and m[-1] == '4', m)
        check('no MODTAG-MISSING', 'MODTAG-MISSING' not in L, re.findall(r'MODTAG-MISSING[^\n]*', L)[:4])
        win('close')
    except Exception as e:
        check('run', False, repr(e))
    finally:
        try: r.send(X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
        except Exception: pass
        t0 = time.time()
        while pid in game_pids() and time.time() - t0 < 8: time.sleep(0.5)
        for p in game_pids():
            if p == pid: os.kill(p, signal.SIGKILL)
        stop.set()
        shutil.copy2(os.path.join(I, 'dsload.log'), os.path.join(OUT, 'dsload.log')) if os.path.exists(os.path.join(I, 'dsload.log')) else None
    check('the copy stayed hidden', not violations, violations[:2])
    ok = all(c['ok'] for c in checks)
    json.dump({'ok': ok, 'checks': checks}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    log('PASS' if ok else 'FAIL', f'{sum(c["ok"] for c in checks)}/{len(checks)}')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
