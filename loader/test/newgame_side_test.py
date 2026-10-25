#!/usr/bin/env python3
"""The New Game screen's side panels (owner 2026-10-25): "Custom difficulty toggle moves at the same time as the scroll"
and "Mods for this world appear atop the interface - should likely be below or on the side below custom difficulty".
On a hidden Mac test copy (default W; DSL_TEST_COPY), one copy at a time, never the owner's game, saves or screen. Uses
this worktree's build/ (loader + mods) and loader/lang.

One start per window size x UI scale (960x540, 960x600, 800x600; UI 1.0 and 1.4; languages en / de / ru / ja spread over
them), every gameplay mod on (the longest list), the title corner as a player sees it (Auto-update switch, version, a
notice line). New Game -> settings, Hell's switch on and combat Hell chosen (the tallest description). Then the settings
list at the top, the middle and the bottom:
 a. the Custom difficulty panel keeps its screen rect (it followed the combat row on the old code);
 b. the mods ("Mods for this world", with Change) are outside the New Game window (the old line lay over "Even small
    waste can lead to downfall", Seed and Time Limit);
 c. both panels: right of the window, inside the screen, off the title corner, the mods under the Custom difficulty
    panel without overlap; the mods' text inside its panel (a long list ends with "+N", never runs out).
A shot per start (text fit in de / ru / ja by eye). Exit 0 = every check passed. Output: <out>/ (logs, shots, summary.json).
NG_RUNS = comma list of run indexes (0-5), default all."""
import glob, json, os, shutil, signal, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
os.environ.setdefault('DSL_TEST_COPY', 'W')
OUT = os.path.abspath(os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/newgame_side'))
sys.argv[1:] = [OUT]
import hell_toggle_test as H                                       # its copy, New Game requests (ng, to_settings) and install
T, V, r = H.T, H.V, H.r
check, log, X = T.check, T.log, H.X
LANG = {'en': 0, 'de': 3, 'ru': 4, 'ja': 7}
RUNS = [((960, 540), 1.0, 'en'), ((960, 540), 1.4, 'de'), ((960, 600), 1.0, 'ru'), ((960, 600), 1.4, 'ja'),
        ((800, 600), 1.0, 'ja'), ((800, 600), 1.4, 'ru')]
FAKEROOT = os.path.join(T.I, 'ng_root')


def fake_root():
    """a ROOT (DSML_ROOT) so the title corner shows as a player sees it (l1_filter_test.fake_root); DSL_UPDATE=0: no check"""
    os.makedirs(os.path.join(FAKEROOT, 'loader'), exist_ok=True); os.makedirs(os.path.join(FAKEROOT, 'update'), exist_ok=True)
    open(os.path.join(FAKEROOT, 'loader', 'RELEASE'), 'w').write('0.1.27\n')
    open(os.path.join(FAKEROOT, 'update', 'notice.txt'), 'w').write('The last update could not be installed. The game stays on 0.1.26.\n')
    cr = os.path.join(T.I, 'crashrep_test'); os.makedirs(cr, exist_ok=True); open(os.path.join(cr, 'answer'), 'w').write('no')
    return {'DSML_ROOT': FAKEROOT, 'DSL_UPDATE': '0', 'DSL_CRASHREP_TEST': cr}


def gameplay_mods():
    """every installed gameplay mod that is not a launcher feature or a custom difficulty (Hell runs anyway)"""
    out = []
    for d in sorted(glob.glob(os.path.join(V.BUILD, 'mods', '*', 'mod.json'))):
        try: m = json.load(open(d))
        except Exception: continue
        if m.get('scope', 'gameplay') == 'gameplay' and m.get('category') != 'feature' and 'difficulty' not in m and m.get('kind', 'code') == 'code':
            out.append(m['id'])
    return out


def start(size, scale, lang, enabled):
    os.environ['LRIG_SIZE'] = f'{size[0]}x{size[1]}'
    orig = r.prepare_data
    def prep(x):
        orig(x)
        p = os.path.join(r.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
        s['GeneralSettingData']['SaveCurrentLanguageType'] = LANG[lang]; s['GraphicSettingData']['UIScale'] = scale
        json.dump(s, open(p, 'w'), indent=1)
    r.prepare_data = prep
    r.cap_wait(f'{X}: start')
    try:
        return T.start_a(enabled=tuple(enabled), extra_env=fake_root())
    finally:
        r.prepare_data = orig


def overlap(a, b): return a[0] < b[2] - 0.5 and a[2] > b[0] + 0.5 and a[1] < b[3] - 0.5 and a[3] > b[1] + 0.5
def inside(a, sw, sh): return a[0] >= -0.5 and a[1] >= -0.5 and a[2] <= sw + 0.5 and a[3] <= sh + 0.5
def same(a, b): return all(abs(x - y) <= 1.0 for x, y in zip(a, b))


def stop_copy():
    for p in T.game_pids():
        try: r.send(X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
        except Exception: pass
        time.sleep(3)
        if T.alive(p): os.kill(p, signal.SIGKILL)


def one_run(i, size, scale, lang, enabled):
    tag = f'{size[0]}x{size[1]} UI {scale} {lang}'
    a = start(size, scale, lang, enabled)
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), tag + ' title'); time.sleep(3)
    s = H.to_settings()
    if not H.tog(s).get('on'): H.ng('toggle', i=0); time.sleep(1)
    H.ng('set', combat=5); time.sleep(1.5)
    st = {}
    for pos in (1, 0.5, 0):
        H.ng('scroll', pos=pos); time.sleep(1.2); st[pos] = H.ng('state')
    T.shot(f'{i}_{size[0]}x{size[1]}_ui{scale}_{lang}')
    s = st[0]; sw, sh = s['screen']; F = s['frame']; C = s['corner']
    log(tag, 'screen', s['screen'], 'window', F, 'list', s['list'], 'corner', C, 'side', [st[p]['side_now'] for p in st], 'mods', [st[p]['mods'] for p in st])
    check(f'{tag}: the screen is the size asked', abs(sw - size[0]) <= 2 and abs(sh - size[1]) <= 2, s['screen'])
    check(f'{tag}: combat Hell chosen (the tallest description)', s['combat'] == 5, s['combat'])
    check(f'{tag}: the settings list scrolled (the combat row moved)', abs(st[1]['row'][3] - st[0]['row'][3]) > 20 or abs(st[1]['row'][1] - st[0]['row'][1]) > 20,
          (st[1]['row'], st[0]['row']))
    # a. the Custom difficulty panel stays where it is while the list scrolls
    sides = [st[p]['side_now'] for p in (1, 0.5, 0)]
    check(f'{tag}: a. Custom difficulty panel shown at every scroll position', all(st[p]['side_now_shown'] == 1 for p in st), [st[p]['side_now_shown'] for p in st])
    check(f'{tag}: a. Custom difficulty panel does not move when the list scrolls', same(sides[0], sides[1]) and same(sides[0], sides[2]), sides)
    # b. the mods are outside the New Game window
    mods = [st[p]['mods'] for p in (1, 0.5, 0)]
    check(f'{tag}: b. the mods ("Mods for this world" + Change) show', all(st[p]['mods_shown'] == 1 for p in st), [st[p]['mods_shown'] for p in st])
    check(f'{tag}: b. the mods are not over the New Game window', all(not overlap(m, F) for m in mods) and all(m[0] >= F[2] - 0.5 for m in mods), (mods, F))
    # c. the side column: right of the window, inside the screen, off the title corner, mods under Custom difficulty
    S, M = sides[2], mods[2]
    check(f'{tag}: c. Custom difficulty panel right of the window, inside the screen', S[0] >= F[2] - 0.5 and inside(S, sw, sh), (S, F, s['screen']))
    check(f'{tag}: c. the mods inside the screen', inside(M, sw, sh), (M, s['screen']))
    check(f'{tag}: c. neither panel covers the title corner', not s['corner_shown'] or (not overlap(S, C) and not overlap(M, C)), (S, M, C))
    check(f'{tag}: c. the mods under the Custom difficulty panel, no overlap', M[3] <= S[1] + 0.5 and not overlap(S, M), (S, M))
    check(f'{tag}: c. Custom difficulty panel top level with the list top (or moved by the corner / screen only)',
          abs(S[3] - s['list'][3]) <= 2 or (s['corner_shown'] and S[3] <= C[1] + 0.5) or S[1] <= 9 * size[1] / 540, (S, s['list'], C))
    fit = s.get('mods_fit', {})
    check(f'{tag}: c. the mods text stays inside its panel (wrapped or "+N")', fit.get('ok') == 1, fit)
    check(f'{tag}: c. Change inside the mods panel', fit.get('btn_in') == 1, fit)
    check(f'{tag}: c. the side panels are not scaled below 0.5', s.get('side_scale', 0) >= 0.5 - 1e-3, s.get('side_scale'))
    log(tag, 'mods text', fit.get('text'))
    H.one_process(a, tag)
    stop_copy()


def main():
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    sv = os.path.join(T.I, 'data', 'Saves')
    if os.path.isdir(sv):
        old = os.path.join(T.I, 'old', time.strftime('%Y%m%d-%H%M%S') + '_saves'); os.makedirs(os.path.dirname(old), exist_ok=True); shutil.move(sv, old)
    V.install(); r.install_lang(X)
    enabled = ['multiplayer'] + gameplay_mods(); log('mods on', enabled)
    threading.Thread(target=T.guard, daemon=True).start()
    which = [int(k) for k in os.environ.get('NG_RUNS', ','.join(str(i) for i in range(len(RUNS)))).split(',')]
    try:
        for i in which:
            try: one_run(i, *RUNS[i], enabled)
            except Exception as e:
                check(f'run {i} {RUNS[i]} finished', False, repr(e)); stop_copy()
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        stop_copy()
        for f in ['dsload.log', 'player.log']:
            if os.path.exists(os.path.join(T.I, f)): shutil.copy2(os.path.join(T.I, f), os.path.join(OUT, f))
        json.dump({'checks': T.checks, 'violations': T.violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        n_ok = sum(c['ok'] for c in T.checks)
        print('RESULT', 'PASS' if T.checks and n_ok == len(T.checks) else 'FAIL', f'{n_ok}/{len(T.checks)}', OUT, flush=True)
    return 0 if T.checks and all(c['ok'] for c in T.checks) else 1


if __name__ == '__main__':
    sys.exit(main())
