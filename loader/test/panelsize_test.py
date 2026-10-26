#!/usr/bin/env python3
"""The kit's "-" / "+" size of an in-game panel (kit 0.1.29, dsl_panel_size; owner 2026-10-25: "allow to both make the chat
and the DPS check window smaller or bigger with a plus or minus"). Hidden Mac test copy (default KIT, DSL_TEST_COPY) with the
loader, multiplayer (test control socket) and the test module uikit-demo (fake mouse, kit state), as kitlook_test.py.
The template's in-game panel (dsl_window_std DSL_WIN_PANEL + DSL_WIN_SIZE, key uikit-demo.stdpanel), two game starts:
 start 1 (960x540, UI Scale 1.0): it shows "-" and "+" in the game's font; "+" = 110 %, "-" "-" = 90 %, the size stops at
          60 % (one more "-" does nothing), each size kept in ui_places.tsv; left at 130 %. Made in a hidden panel (as the
          chat's closed box), the buttons still show "-" and "+" once it shows (their labels were empty: blank buttons).
 start 2 (800x600, the smallest window the tests use, UI Scale 1.4): the panel comes back at 130 % (on top of UI Scale);
          "+" "+" = 150 % and the whole panel is on the screen (its first place, x 1400, runs past a 800x600 window).
Old loaders fail: no "-" / "+" (the holder is missing), nothing is kept.
  panelsize_test.py [out] [--no-build]     Exit 0 = PASS. <out>/summary.json, <out>/<start>_dsload.log."""
import json, os, shutil, signal, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
os.environ.setdefault('DSL_TEST_COPY', 'KIT')
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
OUT = os.path.abspath(os.path.expanduser(ARGS[0] if ARGS else os.path.join(os.path.dirname(HERE), 'test_env', 'l1_out', 'panelsize')))
sys.argv = [sys.argv[0], OUT] + [a for a in sys.argv[1:] if a.startswith('--')]
import esc_test as E                                                  # its copy start, drivers and guard (same test copy rules)
import lrig as r
log, check, ui, st, wait_for, until = E.log, E.check, E.ui, E.st, E.wait_for, E.until


def rect(s, name): return s.get('w', {}).get(name, {}).get('r', [0, 0, 0, 0])
def width(rc): return rc[2] - rc[0]
def size(s): return s.get('w', {}).get('size', {})


def labels_ok(t): return (t or '').split('|') in (['\u2212', '+'], ['\u2013', '+'], ['-', '+'])


def click(t):
    ui('click', target=t); time.sleep(1.2); s = st(); ui('api', op='mouse_off'); return s


def open_panel():
    j = ui('api', op='stdpanel', a=1); s = until(lambda s: s.get('w', {}).get('std', {}).get('panel') == 1, 4) or st(); time.sleep(0.6)
    return j, st()


def start1():
    j, s = open_panel()
    hold = s.get('w', {}).get('stdp_size', {})
    check('start 1: the template panel has "-" / "+" (DSL_WIN_SIZE)', j.get('rc') == 0 and hold.get('on') == 1 and width(hold.get('r', [0, 0, 0, 0])) > 10, (j.get('rc'), hold))
    if j.get('rc') != 0: return
    check('start 1: their text is in the game\'s font', size(s).get('font') == 1, size(s))
    check('start 1: they show "-" and "+"', size(s).get('labels_on') == 1 and labels_ok(size(s).get('labels')), size(s))
    w0 = width(rect(s, 'stdp'))
    check('start 1: a new panel is at 100 %', abs(size(s).get('f', 0) - 1.0) < 0.001 and w0 > 0, (size(s), w0))
    s = click('stdp_size.plus'); w = width(rect(s, 'stdp'))
    check('start 1: "+" = 110 % (the panel and its text 1.1x)', abs(w / w0 - 1.1) < 0.02 and abs(size(s).get('f', 0) - 1.1) < 0.001, (w0, w, size(s)))
    check('start 1: 110 % is kept (ui_places.tsv)', size(s).get('kept') == [1, 1.1], size(s))
    click('stdp_size.minus'); s = click('stdp_size.minus'); w = width(rect(s, 'stdp'))
    check('start 1: "-" "-" = 90 %', abs(w / w0 - 0.9) < 0.02 and size(s).get('kept') == [1, 0.9], (w0, w, size(s)))
    for _ in range(3): s = click('stdp_size.minus')
    w = width(rect(s, 'stdp')); s = click('stdp_size.minus'); w2 = width(rect(s, 'stdp'))
    check('start 1: the size stops at 60 % (one more "-" does nothing)', abs(w / w0 - 0.6) < 0.02 and abs(w2 - w) <= 1 and size(s).get('kept') == [1, 0.6], (w0, w, w2, size(s)))
    for _ in range(7): s = click('stdp_size.plus')
    check('start 1: left at 130 %', abs(width(rect(s, 'stdp')) / w0 - 1.3) < 0.02 and size(s).get('kept') == [1, 1.3], (w0, width(rect(s, 'stdp')), size(s)))
    # made while the panel is hidden (the chat's closed box): the title button clones lost their label when they first showed
    ui('api', op='stdpanel_off'); time.sleep(0.4)
    j = ui('api', op='stdpanel_hidden'); time.sleep(1.0); s = st(); ui('capture', name='start1_hidden')
    check('start 1: made in a hidden panel, they still show "-" and "+" once it shows', j.get('rc') == 0 and size(s).get('labels_on') == 1 and labels_ok(size(s).get('labels')), (j.get('rc'), size(s)))


def start2():
    j, s = open_panel()
    sw, sh = s.get('w', {}).get('screen', [0, 0]); u = s.get('w', {}).get('ui_scale', 0)
    check('start 2: the window is 800x600 at UI Scale 1.4', (sw, sh) == (800, 600) and abs(u - 1.4) < 0.01, (sw, sh, u))
    w1 = width(rect(s, 'stdp')); want = 360 * 1.4 * 1.3 * sh / 1080.0
    check('start 2: after the restart the panel is back at 130 % (on top of UI Scale 1.4)', abs(size(s).get('f', 0) - 1.3) < 0.001 and abs(w1 - want) < 4, (size(s), w1, want))
    if j.get('rc') != 0: return
    click('stdp_size.plus'); s = click('stdp_size.plus'); time.sleep(0.5); s = st(); rc = rect(s, 'stdp')
    want = 360 * 1.4 * 1.5 * sh / 1080.0
    check('start 2: "+" "+" = 150 %', abs(size(s).get('f', 0) - 1.5) < 0.001 and abs(width(rc) - want) < 4, (size(s), width(rc), want))
    check('start 2: at 150 % the whole panel is on the 800x600 screen', rc[0] >= -1 and rc[1] >= -1 and rc[2] <= sw + 1 and rc[3] <= sh + 1 and width(rc) > 0, (rc, sw, sh))


def run(tag, wh, scale, fresh):
    os.environ['LRIG_SIZE'] = f'{wh[0]}x{wh[1]}'
    orig = r.prepare_data
    def prep(x):
        orig(x)
        p = os.path.join(r.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
        s['GeneralSettingData']['SaveCurrentLanguageType'] = 0; s['GraphicSettingData']['UIScale'] = scale
        json.dump(s, open(p, 'w'), indent=1)
    r.prepare_data = prep
    places = os.path.join(r.idir(E.X), 'data', 'Mods', 'ui_places.tsv')
    if fresh and os.path.exists(places): os.makedirs(os.path.join(OUT, 'old'), exist_ok=True); os.replace(places, os.path.join(OUT, 'old', 'ui_places.%d.tsv' % time.time()))
    try:
        E.start()
        wait_for(lambda: (lambda s: s if s.get('scene') == 1 and not s.get('busy') else None)(E.ctl({'cmd': 'status'}, 5)), 240, 1)
        wait_for(lambda: (lambda j: j if j.get('kit') == 1 else None)(st()), 60, 1)
        time.sleep(4)
        start1() if tag == 'start1' else start2()
        ui('capture', name=tag)
    except Exception as e:
        check(f'{tag}: run finished', False, repr(e))
    finally:
        r.prepare_data = orig
        try: E.ctl({'cmd': 'quit'}, 5)
        except Exception: pass
        time.sleep(3)
        for p in E.game_pids(): os.kill(p, signal.SIGKILL)
        for f in ('dsload.log', 'player.log'):
            if os.path.exists(os.path.join(E.I, f)): shutil.copy2(os.path.join(E.I, f), os.path.join(OUT, f'{tag}_{f}'))


def main():
    if E.game_pids(): raise SystemExit(f'copy {E.X} already running')
    if os.getloadavg()[0] > 20: raise SystemExit('load over 20: no test copy now')
    os.makedirs(OUT, exist_ok=True)
    if '--no-build' not in sys.argv: E.build()
    E.install()
    g = threading.Thread(target=E.guard, daemon=True); g.start()
    run('start1', (960, 540), 1.0, True)
    run('start2', (800, 600), 1.4, False)
    E.stop.set()
    for tag in ('start1', 'start2'):                                    # the UI rules' guard on the sized panel
        lf = os.path.join(OUT, f'{tag}_dsload.log')
        ur = [l for l in open(lf, errors='replace').read().splitlines() if 'UI-RULE ' in l] if os.path.exists(lf) else ['UI-RULE no dsload.log']
        check(f'{tag}: no UI-RULE line (the panel keeps every UI rule with "-" / "+")', not ur, ur[:4])
    fails = [c for c in E.checks if not c['ok']]
    json.dump({'checks': E.checks, 'violations': E.violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1)
    bad = len(fails) + (1 if E.violations else 0)
    log(f'RESULT panelsize_test: {len(E.checks) - len(fails)} PASS, {bad} FAIL')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
