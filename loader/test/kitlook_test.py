#!/usr/bin/env python3
"""The kit's red X, UI Scale and right-drag move, and the title corner over the game's windows (kit 0.1.28; owner
2026-10-25 UX review L1, L2, L4 and the MOVE RULE). Hidden Mac test copy (default KIT, DSL_TEST_COPY) with the loader,
multiplayer (test control socket) and the test module uikit-demo (fake mouse, kit state), as esc_test.py.
 red X:    the demo panel has the game's red X (close_x); a real click on it runs the panel's Escape close; the Mods
           window's X is the red X (no text "X") and a real click on it closes the window.
 UI Scale: the demo panel (scale_follow), the Mods window and the title corner grow 1.4x when Settings > UI Scale goes
           1.0 -> 1.4 (the game's UIScaleManager.SetScale) and come back at 1.0.
 L2:       with the game's Options window open, the title corner is hidden (it crossed that window's red X at 1.4).
 move:     movable panel: a right-drag moves it by the drag, a right-click alone does not, a drag past the edge leaves it
           on the screen, its place is kept (ui_places.tsv) and put back when the panel is built again.
Shots (local only, never commit): <out>/shots/<lang>_ui<scale>/*.png for en (this run) and --langs de,ru (one start each).
Old loaders fail: no red X (text X), nothing follows UI Scale, the corner stays over the Options window, no move.
  kitlook_test.py [out] [--no-build] [--langs de,ru]     Exit 0 = PASS. <out>/summary.json."""
import json, os, shutil, signal, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
os.environ.setdefault('DSL_TEST_COPY', 'KIT')
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(ARGS[0] if ARGS else os.path.join(os.path.dirname(HERE), 'test_env', 'l1_out', 'kitlook')))
LANGS = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--langs=')), '')
if '--langs' in sys.argv and sys.argv.index('--langs') + 1 < len(sys.argv): LANGS = sys.argv[sys.argv.index('--langs') + 1]
sys.argv = [sys.argv[0], OUT] + [a for a in sys.argv[1:] if a.startswith('--') and not a.startswith('--langs')]
import esc_test as E                                                  # its copy start, drivers and guard (same test copy rules)
import lrig as r
LANG = {'en': 0, 'de': 3, 'ru': 4}
log, check, ui, win, st, wait_for, until = E.log, E.check, E.ui, E.win, E.st, E.wait_for, E.until


def w_of(s, name): return s.get('w', {}).get(name, {})
def rect(s, name): return w_of(s, name).get('r', [0, 0, 0, 0])
def width(rc): return rc[2] - rc[0]
def near(a, b, tol): return abs(a - b) <= tol


def shot(lang, scale, name):
    d = os.path.join(OUT, 'shots', f'{lang}_ui{scale}'); os.makedirs(d, exist_ok=True)
    j = ui('capture', name=name); src = os.path.join(E.UDRV, 'cap', name + '.png')
    if j.get('rc', -1) > 0 and os.path.exists(src): shutil.copy2(src, os.path.join(d, name + '.png'))


def set_scale(v):
    ui('api', op='uiscale', a=v); return until(lambda s: near(s.get('w', {}).get('ui_scale', 0), v, 0.01), 4) or st()


def red_x_cases():
    ui('api', op='open'); s = until(lambda s: s.get('built') == 1)
    x = w_of(s, 'x')
    check('red X: the demo panel has the game\'s red X (close_x)', x.get('on') == 1 and width(x.get('r', [0, 0, 0, 0])) > 10, x)
    check('red X: the Load window\'s close button is the template', 'ui: red X: the Load window\'s close button' in E.dlog(), 'no template line')
    n0 = E.dlog().count('ui: red X closes')
    ui('click', target='x'); s = until(lambda s: s.get('built') == 0, 4) or st(); ui('api', op='mouse_off')
    check('red X: a click on it closes the panel (its Escape close)', s.get('built') == 0 and E.dlog().count('ui: red X closes') == n0 + 1, (s.get('built'), s.get('esc')))
    win('open'); wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10); time.sleep(0.6)
    s = st(); c = w_of(s, 'win:close')
    check('red X: the Mods window\'s X is the red X (no text "X")', c.get('on') == 1 and c.get('text') == '', c)
    shot('en', '1.0', 'mods_window')
    ui('click', target='win:close'); w = wait_for(lambda: (lambda j: j if not j.get('open') else None)(win('state')), 4) or win('state'); ui('api', op='mouse_off')
    check('red X: a click on the Mods window\'s red X closes it', not w.get('open'), w)


def scale_cases():
    set_scale(1.0)
    ui('api', op='open'); until(lambda s: s.get('built') == 1)
    j = ui('api', op='follow'); check('UI Scale: scale_follow accepted', j.get('rc') == 0, j.get('rc'))
    time.sleep(0.5); s = st(); p1, c1 = width(rect(s, 'panel')), width(rect(s, 'update'))
    win('open'); wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10); time.sleep(0.6)
    m1 = width(rect(st(), 'win:title'))
    win('close'); time.sleep(0.4)
    s = set_scale(1.4); time.sleep(0.6); s = st()
    check('UI Scale: the game says 1.4', near(s.get('w', {}).get('ui_scale', 0), 1.4, 0.01), s.get('w', {}).get('ui_scale'))
    p2, c2 = width(rect(s, 'panel')), width(rect(s, 'update'))
    check('UI Scale: the demo panel grows 1.4x', p1 > 0 and near(p2 / p1, 1.4, 0.03), (p1, p2))
    check('UI Scale: the title corner grows 1.4x', c1 > 0 and near(c2 / c1, 1.4, 0.03), (c1, c2))
    shot('en', '1.4', 'title_corner_panel')
    win('open'); wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10); time.sleep(0.6)
    m2 = width(rect(st(), 'win:title'))
    check('UI Scale: the Mods window grows 1.4x', m1 > 0 and near(m2 / m1, 1.4, 0.03), (m1, m2))
    shot('en', '1.4', 'mods_window')
    win('close'); time.sleep(0.4)
    # L2: the game's Options window over the title: the corner hides, and shows again when it closes
    ui('api', op='destroy', target='panel'); time.sleep(0.3)
    ui('stock', what='title_settings'); time.sleep(1.5); s = st()
    check('L2: with the game\'s Options window open the title corner is hidden', w_of(s, 'update').get('on') == 0, w_of(s, 'update'))
    shot('en', '1.4', 'stock_options')
    E.press(); s = until(lambda s: w_of(s, 'update').get('on') == 1, 4) or st()
    check('L2: the corner shows again when the Options window closes', w_of(s, 'update').get('on') == 1, w_of(s, 'update'))
    s = set_scale(1.0); time.sleep(0.6); s = st()
    check('UI Scale: back at 1.0 the corner is its 1.0 size again', near(width(rect(s, 'update')), c1, 2), (c1, width(rect(s, 'update'))))


def move_cases():
    ui('api', op='open'); until(lambda s: s.get('built') == 1)
    j = ui('api', op='movable'); check('move: movable accepted', j.get('rc') == 0, j.get('rc'))
    time.sleep(0.4); s = st(); r0 = rect(s, 'panel'); sw, sh = s.get('w', {}).get('screen', [0, 0])
    ui('rdrag', target='panel', dx=150, dy=-80); time.sleep(1.5); s = st(); r1 = rect(s, 'panel')
    check('move: a right-drag moves the panel by the drag', near(r1[0] - r0[0], 150, 4) and near(r1[1] - r0[1], -80, 4), (r0, r1))
    kept = s.get('w', {}).get('place', [0])
    check('move: the place is kept (ui_places.tsv)', kept[0] == 1 and 'ui: move: uikit-demo.panel kept' in E.dlog(), kept)
    ui('rclick', target='panel'); time.sleep(1.0); r2 = rect(st(), 'panel')
    check('move: a right-click alone does not move it', near(r2[0], r1[0], 1) and near(r2[1], r1[1], 1), (r1, r2))
    ui('rdrag', target='panel', dx=6000, dy=-6000); time.sleep(1.5); s = st(); r3 = rect(s, 'panel')
    check('move: a drag past the edge leaves it on the screen', r3[2] <= sw + 1 and r3[1] >= -1 and r3[0] >= -1 and r3[3] <= sh + 1, (r3, sw, sh))
    shot('en', '1.0', 'moved_panel')
    ui('api', op='destroy', target='panel'); time.sleep(0.3)
    ui('api', op='open'); until(lambda s: s.get('built') == 1); ui('api', op='movable'); time.sleep(0.4); r4 = rect(st(), 'panel')
    check('move: built again, the panel is back at its kept place', near(r4[0], r3[0], 2) and near(r4[1], r3[1], 2), (r3, r4))
    ui('api', op='destroy', target='panel')


def lang_shots(lang):
    """one start in lang: the corner, Mods window, demo panel and bug report at UI 1.0 and 1.4 (local shots)"""
    for v in (1.0, 1.4):
        set_scale(v); time.sleep(0.6)
        ui('api', op='open'); until(lambda s: s.get('built') == 1); ui('api', op='follow'); time.sleep(0.5)
        shot(lang, v, 'corner_panel')
        ui('api', op='destroy', target='panel')
        win('open'); wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10); time.sleep(0.8)
        shot(lang, v, 'mods_window'); win('close'); time.sleep(0.4)
        ui('stock', what='title_bug'); wait_for(lambda: (lambda j: j if j.get('open') else None)(E.bug('state')), 10); time.sleep(0.8)
        shot(lang, v, 'bug_report'); E.press(); time.sleep(0.6)


def run(lang):
    orig = r.prepare_data
    def prep(x):
        orig(x)
        p = os.path.join(r.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
        s['GeneralSettingData']['SaveCurrentLanguageType'] = LANG[lang]; s['GraphicSettingData']['UIScale'] = 1.0
        json.dump(s, open(p, 'w'), indent=1)
    r.prepare_data = prep
    root = os.path.join(r.idir(E.X), 'kit_root')                       # a ROOT with a release number: the title corner shows
    os.makedirs(os.path.join(root, 'loader'), exist_ok=True); os.makedirs(os.path.join(root, 'update'), exist_ok=True)
    open(os.path.join(root, 'loader', 'RELEASE'), 'w').write('0.1.28\n'); E.EXTRA_ENV['DSML_ROOT'] = root
    places = os.path.join(r.idir(E.X), 'data', 'Mods', 'ui_places.tsv')
    if os.path.exists(places): os.makedirs(os.path.join(OUT, 'old'), exist_ok=True); os.replace(places, os.path.join(OUT, 'old', 'ui_places.%d.tsv' % time.time()))
    try:
        E.start()
        wait_for(lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(E.ctl({'cmd': 'status'}, 5)), 240, 1)
        wait_for(lambda: (lambda j: j if j.get('kit') == 1 else None)(st()), 60, 1)
        time.sleep(4)
        if lang == 'en': red_x_cases(); scale_cases(); move_cases()
        lang_shots(lang)
    except Exception as e:
        check(f'{lang}: run finished', False, repr(e))
    finally:
        r.prepare_data = orig
        try: E.ctl({'cmd': 'quit'}, 5)
        except Exception: pass
        time.sleep(3)
        for p in E.game_pids(): os.kill(p, signal.SIGKILL)
        for f in ('dsload.log', 'player.log'):
            if os.path.exists(os.path.join(E.I, f)): shutil.copy2(os.path.join(E.I, f), os.path.join(OUT, f'{lang}_{f}'))


def main():
    if E.game_pids(): raise SystemExit(f'copy {E.X} already running')
    if os.getloadavg()[0] > 20: raise SystemExit('load over 20: no test copy now')
    os.makedirs(OUT, exist_ok=True)
    if '--no-build' not in sys.argv: E.build()
    E.install()
    g = threading.Thread(target=E.guard, daemon=True); g.start()
    for lang in ['en'] + [l for l in LANGS.split(',') if l in LANG and l != 'en']:
        if os.getloadavg()[0] > 20: check(f'{lang}: load under 20 for the start', False, os.getloadavg()[0]); continue
        run(lang)
    E.stop.set()
    fails = [c for c in E.checks if not c['ok']]
    json.dump({'checks': E.checks, 'violations': E.violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    bad = len(fails) + (1 if E.violations else 0)
    log(f'RESULT kitlook_test: {len(E.checks) - len(fails)} PASS, {bad} FAIL')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
