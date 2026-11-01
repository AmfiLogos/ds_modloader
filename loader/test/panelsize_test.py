#!/usr/bin/env python3
"""The kit's in-game panel templates. Hidden Mac test copy (default KIT, DSL_TEST_COPY) with the loader, multiplayer (test
control socket) and the test module uikit-demo (fake mouse, kit state), as kitlook_test.py.
 SIZE (kit 0.1.29, dsl_panel_size; owner 2026-10-25: "allow to both make the chat and the DPS check window smaller or bigger
 with a plus or minus") + HOVER (kit 0.1.30; owner: if you do not want to change anything, having them on screen is
 redundant): "-" / "+" are hidden while the mouse is off the panel, show while it is over it (no flicker when it moves onto
 the buttons), hide again after it leaves, and a click on them still sizes.
 TOP (kit 0.1.30, dsl_movable; owner: "all of the windows that are under the right click irrespective of whether it's on top
 or below they all move at the same time"): panels A (registered last, drawn below) and B (drawn on top) overlap; a
 right-drag on the overlap moves only B; with C on top (movable_own: its own drag code) the kit moves neither and
 drag_owner says C.
 SECTION (kit 0.1.30, dsl_section; owner, Units Laboratory sections: "otherwise they just take a lot of space for no
 reason"): a click on a section's title folds / unfolds it; folded stays folded after the panel is closed and opened, and
 after a game restart.
The template's in-game panel (dsl_window_std DSL_WIN_PANEL + DSL_WIN_SIZE, key uikit-demo.stdpanel), two game starts:
 start 1 (960x540, UI Scale 1.0): it shows "-" and "+" in the game's font; "+" = 110 %, "-" "-" = 90 %, the size stops at
          60 % (one more "-" does nothing), each size kept in ui_places.tsv; left at 130 %. Made in a hidden panel (as the
          chat's closed box), the buttons still show "-" and "+" once it shows (their labels were empty: blank buttons).
 start 2 (800x600, the smallest window the tests use, UI Scale 1.4): the panel comes back at 130 % (on top of UI Scale);
          "+" "+" = 150 % and the whole panel is on the screen (its first place, x 1400, runs past a 800x600 window).
Old loaders fail: no "-" / "+" (the holder is missing), nothing is kept; kit 0.1.29: "-" / "+" show without the mouse,
the newest registered panel (A) takes the right-drag, no sections.
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


def hover_on(t='stdp'):
    ui('hover', target=t); time.sleep(0.6)


def away():
    ui('api', op='mouse_away'); time.sleep(0.8)                        # the fake mouse far off the window (never the real cursor)


def click(t):
    if t.startswith('stdp_size'): hover_on()                          # the buttons show only under the mouse
    ui('click', target=t); time.sleep(1.2); s = st(); away(); return s


def held(s): return s.get('w', {}).get('stdp_size', {}).get('on')


def hover_cases():
    away(); s = st()
    check('hover: without the mouse on the panel, "-" / "+" are hidden', held(s) == 0 and size(s).get('labels_on') == 0, (held(s), size(s)))
    hover_on(); s = st()
    check('hover: with the mouse on the panel, "-" / "+" show', held(s) == 1 and size(s).get('labels_on') == 1, (held(s), size(s)))
    ui('hover', target='stdp_size.plus'); seen = []
    for _ in range(8): time.sleep(0.12); seen.append(held(st()))
    check('hover: the mouse moved onto "+": they stay shown (no flicker)', seen == [1] * 8, seen)
    away(); s = st()
    check('hover: the mouse left: hidden again', held(s) == 0, held(s))


def open_panel():
    j = ui('api', op='stdpanel', a=1); s = until(lambda s: s.get('w', {}).get('std', {}).get('panel') == 1, 4) or st(); time.sleep(0.6)
    return j, st()


def rmoved(a, b, dx, dy): return abs((b[0] - a[0]) - dx) <= 4 and abs((b[1] - a[1]) - dy) <= 4
def same(a, b): return all(abs(a[i] - b[i]) <= 1 for i in range(4)) and a[2] > a[0]


def top_cases():
    j = ui('api', op='ov', a=1); time.sleep(0.8); s = st(); a0, b0 = rect(s, 'ovA'), rect(s, 'ovB')
    ok = j.get('rc') == 0 and a0[2] > a0[0] and b0[2] > b0[0]
    cx, cy = (a0[0] + a0[2]) / 2, (a0[1] + a0[3]) / 2
    check('top: panels A and B overlap at A\'s centre (A registered last, B drawn on top)', ok and b0[0] < cx < b0[2] and b0[1] < cy < b0[3], (j.get('rc'), a0, b0))
    if not ok: return
    ui('rdrag', target='ovA', dx=-120, dy=-60); time.sleep(1.8); s = st(); a1, b1 = rect(s, 'ovA'), rect(s, 'ovB')
    check('top: a right-drag on the overlap moves only the top panel B', rmoved(b0, b1, -120, -60) and same(a0, a1) and s.get('ov', {}).get('seen') == 'B',
          (a0, a1, b0, b1, s.get('ov')))
    j = ui('api', op='ov', a=2); time.sleep(0.8); s = st(); a0, b0, c0 = rect(s, 'ovA'), rect(s, 'ovB'), rect(s, 'ovC')
    ui('rdrag', target='ovA', dx=100, dy=50); time.sleep(1.8); s = st(); a1, b1, c1 = rect(s, 'ovA'), rect(s, 'ovB'), rect(s, 'ovC')
    check('top: with C on top (its own drag code), the kit moves neither A nor B and drag_owner says C',
          j.get('rc') == 0 and same(a0, a1) and same(b0, b1) and same(c0, c1) and s.get('ov', {}).get('seen') == 'C' and s.get('ov', {}).get('c_seen', 0) > 0,
          (a0, a1, b0, b1, c0, c1, s.get('ov')))
    ui('api', op='ov_off'); time.sleep(0.4)


def on(s, n): return s.get('w', {}).get(n, {}).get('on')


def sec_cases(tag):
    if tag == 'start1':
        j = ui('api', op='secpanel', a=1); time.sleep(0.8); s = st(); sec = s.get('sec', {})
        check('section: a new panel: "Settings" open (its body shows), "Scarecrows" folded (DSL_SEC_FOLDED)', j.get('rc') == 0 and sec.get('one') == 1 and sec.get('two') == 0
              and on(s, 'sec1_body') == 1 and on(s, 'sec2_body') == -1, (j.get('rc'), sec, on(s, 'sec1_body'), on(s, 'sec2_body')))
        if j.get('rc') != 0: return
        ui('click', target='sec1'); time.sleep(1.2); away(); s = st()
        check('section: a click on "Settings" folds it (its body goes, the window painted again)', s.get('sec', {}).get('one') == 0 and on(s, 'sec1_body') == -1 and s.get('sec', {}).get('cbs', 0) >= 1, (s.get('sec'), on(s, 'sec1_body')))
        ui('click', target='sec2'); time.sleep(1.2); away(); s = st()
        check('section: a click on "Scarecrows" opens it', s.get('sec', {}).get('two') == 1 and on(s, 'sec2_body') == 1, (s.get('sec'), on(s, 'sec2_body')))
        ui('capture', name='sections')
        ui('api', op='secpanel', a=0); time.sleep(0.6)
        j = ui('api', op='secpanel', a=1); time.sleep(0.8); s = st()
        check('section: closed and opened again: "Settings" still folded, "Scarecrows" still open', s.get('sec', {}).get('one') == 0 and on(s, 'sec1_body') == -1 and on(s, 'sec2_body') == 1, (s.get('sec'), on(s, 'sec1_body'), on(s, 'sec2_body')))
    else:
        j = ui('api', op='secpanel', a=1); time.sleep(0.8); s = st()
        check('section: after a game restart: "Settings" still folded, "Scarecrows" still open', j.get('rc') == 0 and on(s, 'sec1_body') == -1 and on(s, 'sec2_body') == 1, (j.get('rc'), s.get('sec'), on(s, 'sec1_body'), on(s, 'sec2_body')))
    ui('api', op='secpanel', a=0); time.sleep(0.4)


def start1():
    j, s = open_panel()
    hover_on(); s = st()
    hold = s.get('w', {}).get('stdp_size', {})
    check('start 1: the template panel has "-" / "+" (DSL_WIN_SIZE)', j.get('rc') == 0 and hold.get('on') == 1 and width(hold.get('r', [0, 0, 0, 0])) > 10, (j.get('rc'), hold))
    if j.get('rc') != 0: return
    check('start 1: their text is in the game\'s font', size(s).get('font') == 1, size(s))
    check('start 1: they show "-" and "+"', size(s).get('labels_on') == 1 and labels_ok(size(s).get('labels')), size(s))
    hover_cases(); s = st()
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
    j = ui('api', op='stdpanel_hidden'); time.sleep(1.0); hover_on(); s = st(); ui('capture', name='start1_hidden'); away()
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
        ui('api', op='stdpanel_off'); time.sleep(0.4)
        if tag == 'start1': top_cases()
        sec_cases(tag)
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
