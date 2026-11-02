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
 start 1 (960x540, UI Scale 1.0): it shows "-" and "+" in the game's font; ANCHOR (kit 0.1.30; owner 2026-11-01: "the - +
          should stay in the same place so i can click multiple time"): "+" 3 times and "-" 3 times, each at one fixed screen
          point, all size the panel and the button's centre moves < 2 px, the new place is kept; SIZE KEYS: Ctrl + "=" /
          "-" over the panel = one step each way (button < 2 px), nothing with the mouse off it or while a text field
          has the keyboard; key capture in the Mods window (kit 0.1.31; owner 2026-11-02: "No, just do press any key to
          capture."): "Bigger" = Ctrl + K resizes and the old key does not, Escape keeps the key and the window open,
          "Clear" = off; covered (review 2026-11-02): the Mods window or a kit window over the panel blocks the keys;
          LAYOUT (item 27, "Sizes only"): the saved layout holds sizes only, "Reset all" puts the size back and leaves
          every place; "+" = 110 %, "-" "-" = 90 %, the size stops at
          60 % (one more "-" does nothing), each size kept in ui_places.tsv; left at 130 %. Made in a hidden panel (as the
          chat's closed box), the buttons still show "-" and "+" once it shows (their labels were empty: blank buttons).
 start 2 (800x600, the smallest window the tests use, UI Scale 1.4): the panel comes back at 130 % (on top of UI Scale);
          "+" "+" = 150 % and the whole panel is on the screen (its first place, x 1400, runs past a 800x600 window);
          a kit 0.1.30 "Size keys" choice (Ctrl + keypad) shows as the keys Ctrl + Keypad - / +.
          FOLD RULE (kit 0.1.30; owner 2026-11-01: "the size of the dps check window changes depending on collapsed
          expanded toggles... It shouldn't"): a UI Scale panel 300 high folded / 900 open keeps its width (scale) both ways.
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
log, check, ui, st, wait_for, until, win = E.log, E.check, E.ui, E.st, E.wait_for, E.until, E.win


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


def part(r, name):                                                    # the "-" (left 22 of 46) or "+" (right 22) of the holder rect
    w = (r[2] - r[0]) * 22 / 46.0
    return [r[0], r[1], r[0] + w, r[3]] if name == 'minus' else [r[2] - w, r[1], r[2], r[3]]


def centre(r): return ((r[0] + r[2]) / 2.0, (r[1] + r[3]) / 2.0)


def anchor_cases():
    # ANCHOR (owner 2026-11-01: "the - + should stay in the same place so i can click multiple time"): the mouse stays on one
    # screen point; each click there must hit the button and size the panel, and the button's centre stays on that point
    s = st(); r0 = rect(s, 'stdp'); sw, sh = s.get('w', {}).get('screen', [0, 0])
    ui('rdrag', target='stdp', dx=sw / 2.0 - (r0[0] + r0[2]) / 2.0, dy=sh / 2.0 - (r0[1] + r0[3]) / 2.0); time.sleep(1.8)   # off the screen's edges
    hover_on(); s = st(); f = size(s).get('f', 0)
    for name, d in (('plus', 0.1), ('minus', -0.1)):
        bx, by = centre(part(rect(s, 'stdp_size'), name)); seen = []
        for n in range(3):
            ui('api', op='click_xy', x=bx, y=by); time.sleep(1.2); s = st(); f2 = size(s).get('f', 0)
            cx, cy = centre(part(rect(s, 'stdp_size'), name)); seen.append((round(f2, 2), round(cx - bx, 1), round(cy - by, 1)))
            ok = abs(f2 - f - d) < 0.001 and abs(cx - bx) < 2 and abs(cy - by) < 2
            f = f2
            if not ok: break
        check(f'anchor: "{"+" if d > 0 else "-"}" clicked 3 times at one point: each click sizes the panel 10 % and the button stays under the mouse (< 2 px)', ok and len(seen) == 3, ((bx, by), seen))
    away(); r1 = rect(st(), 'stdp')
    ui('api', op='stdpanel_off'); time.sleep(0.4); _, s = open_panel(); r2 = rect(s, 'stdp')
    check('anchor: the place after the clicks is kept (closed and opened: same place)', same(r1, r2) or all(abs(r1[i] - r2[i]) <= 2 for i in range(4)), (r1, r2))


def ctrl_key(code):                                                   # real AppKit events: Control down, the key down + up, Control up
    ui('key', code=code, ctrl=1); time.sleep(0.5); ui('key', code=-1, ctrl=0); time.sleep(0.8)


def mods_win():                                                        # the Mods window (title screen: its Global settings block shows)
    win('open'); wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10); time.sleep(0.5)


def wtext(name):                                                      # a named control's text in the Mods window
    return next((c.get('text') for c in win('state').get('controls', []) if c.get('name') == name), None)


def capture(row, code, ctrl=1):                                       # click the row's key (Mods window open), then press the key
    j = win('click', target=row); time.sleep(0.5); t = wtext(row)
    if code == 'esc': ui('key', code=53); time.sleep(0.8)
    else: ui('key', code=code, ctrl=ctrl); time.sleep(0.5); ui('key', code=-1, ctrl=0); time.sleep(0.8)
    return j.get('ok'), t, wtext(row)


def log_since(n): return E.dlog()[n:]


def key_cases():
    # SIZE KEYS (owner 2026-11-01: "configurable keys" for the - / + resize): Ctrl + "-" / "=" size the panel under the mouse
    hover_on(); s = st(); f0 = size(s).get('f', 0); pb = centre(part(rect(s, 'stdp_size'), 'plus'))
    ctrl_key(24); hover_on(); s = st(); f1 = size(s).get('f', 0); pc = centre(part(rect(s, 'stdp_size'), 'plus'))
    check('keys: Ctrl + "=" with the mouse over the panel = 10 % bigger, its "+" stays put (< 2 px)', abs(f1 - f0 - 0.1) < 0.001 and abs(pc[0] - pb[0]) < 2 and abs(pc[1] - pb[1]) < 2, (f0, f1, pb, pc))
    mb = centre(part(rect(s, 'stdp_size'), 'minus')); ctrl_key(27); hover_on(); s = st(); f2 = size(s).get('f', 0); mc = centre(part(rect(s, 'stdp_size'), 'minus'))
    check('keys: Ctrl + "-" = 10 % smaller, its "-" stays put (< 2 px)', abs(f2 - f1 + 0.1) < 0.001 and abs(mc[0] - mb[0]) < 2 and abs(mc[1] - mb[1]) < 2, (f1, f2, mb, mc))
    away(); ctrl_key(24); f3 = size(st()).get('f', 0)
    check('keys: with the mouse off every panel, Ctrl + "=" changes nothing', abs(f3 - f2) < 0.001, (f2, f3))
    # TEXT FIELD: nothing while a text field has the keyboard (the demo panel's field)
    ui('api', op='open'); time.sleep(0.8); ui('click', target='field'); time.sleep(1.2); typing = st().get('typing')
    n = len(E.dlog()); hover_on(); ctrl_key(24); f4 = size(st()).get('f', 0); lg = log_since(n)
    check('keys: a text field has the keyboard: Ctrl + "=" over the panel changes nothing', typing == 1 and abs(f4 - f2) < 0.001 and 'a text field has the keyboard' in lg,
          (typing, f2, f4, lg[-300:]))
    ui('api', op='destroy', target='panel'); time.sleep(0.6)
    # CAPTURE (owner 2026-11-02: "No, just do press any key to capture."): "Bigger" = Ctrl + K
    mods_win(); ok, t0, t1 = capture('size-key-plus', 40)
    check('keys: the "Bigger" key shows "Press a key...", then the pressed Ctrl + K', ok and t0 == 'Press a key...' and t1 == 'Ctrl + K', (ok, t0, t1))
    win('esc'); time.sleep(0.8); hover_on(); f5 = size(st()).get('f', 0); ctrl_key(40); f6 = size(st()).get('f', 0); ctrl_key(24); f7 = size(st()).get('f', 0)
    check('keys: Ctrl + K over the panel = 10 % bigger; the old Ctrl + "=" does nothing now', abs(f6 - f5 - 0.1) < 0.001 and abs(f7 - f6) < 0.001, (f5, f6, f7))
    mods_win(); ok, t0, t1 = capture('size-key-plus', 'esc'); still = win('state').get('open')
    check('keys: Escape while it waits for a key: the key stays Ctrl + K and the Mods window stays open', ok and t0 == 'Press a key...' and t1 == 'Ctrl + K' and still == 1, (ok, t0, t1, still))
    j = win('click', target='size-key-plus-clear'); time.sleep(0.5); t2 = wtext('size-key-plus'); win('esc'); time.sleep(0.8)
    hover_on(); f8 = size(st()).get('f', 0); ctrl_key(40); f9 = size(st()).get('f', 0)
    check('keys: "Clear" = Off: Ctrl + K over the panel does nothing', j.get('ok') and t2 == 'Off' and abs(f9 - f8) < 0.001, (j, t2, f8, f9))
    mods_win(); ok, _, t3 = capture('size-key-plus', 24)
    check('keys: the default back by capture (Ctrl + "=")', ok and t3 == 'Ctrl + =', (ok, t3))
    # COVERED (review 2026-11-02): the keys act only on the window on top under the mouse
    n = len(E.dlog()); hover_on(); f10 = size(st()).get('f', 0); ctrl_key(24); f11 = size(st()).get('f', 0); lg = log_since(n); win('esc'); time.sleep(0.8)
    check('keys: the Mods window over the panel: Ctrl + "=" does nothing', abs(f11 - f10) < 0.001 and 'is covered by' in lg, (f10, f11, lg[-300:]))
    away(); s = st(); pr = rect(s, 'stdp'); j = ui('api', op='kp'); time.sleep(0.8); away(); s = st(); kr = rect(s, 'ovA')
    ix = (max(pr[0], kr[0]), max(pr[1], kr[1]), min(pr[2], kr[2]), min(pr[3], kr[3]))
    if j.get('rc') == 0 and ix[2] - ix[0] > 8 and ix[3] - ix[1] > 8:
        n = len(E.dlog()); ui('api', op='move_xy', x=(ix[0] + ix[2]) / 2, y=(ix[1] + ix[3]) / 2); time.sleep(0.6)
        f12 = size(st()).get('f', 0); ctrl_key(24); f13 = size(st()).get('f', 0); lg = log_since(n)
        check('keys: a kit window over the panel: Ctrl + "=" there does nothing', abs(f13 - f12) < 0.001 and 'is covered by' in lg, (f12, f13, lg[-300:]))
    else:
        check('keys: a kit window over the panel (it must overlap the panel)', False, (j, pr, kr))
    ui('api', op='ov_off'); time.sleep(0.4); hover_on(); ctrl_key(27); away()   # back to 100 % (Ctrl + K made it 110 %) for the cases after


def rows(path, pre):                                                  # {key: (x, y)} of a places file, keys starting with pre
    try: return {l.split('\t')[0]: tuple(float(v) for v in l.split('\t')[1:3]) for l in open(path) if l.startswith(pre)}
    except OSError: return {}


def near(a, b, d=3): return all(abs(a[i] - b[i]) <= d for i in range(4)) and a[2] > a[0]


def layout(op):                                                       # the Mods window's Global settings buttons, then the notice's first button
    win('open'); wait_for(lambda: (lambda j: j if j.get('open') else None)(win('state')), 10); time.sleep(0.5)
    j = win('click', target='layout-' + op); time.sleep(0.8); k = ui('click', target='notice_ok'); time.sleep(1.2)
    win('esc'); time.sleep(0.8); return j.get('ok') and k.get('rc', 0) == 0


def layout_cases(tag):
    # LAYOUT (owner 2026-11-01: "add a global settings + save current options + reset all"; 2026-11-02, item 27: the saved
    # layout = the SIZES only, "Sizes only": "Reset all" puts the saved sizes back, every window stays where the player put it)
    dd = os.path.join(r.idir(E.X), 'data', 'Mods'); saved, places = os.path.join(dd, 'ui_layout_saved.tsv'), os.path.join(dd, 'ui_places.tsv')
    if tag == 'start1':                                                # stdp (open) + ovB (opened, moved, closed: a panel not open at the reset)
        away(); s = st(); s1, f1 = rect(s, 'stdp'), size(s).get('f', 0); ui('api', op='stdpanel_off'); time.sleep(0.4)
        def ov_b(dx, dy):
            ui('api', op='ov', a=1); time.sleep(0.8); away(); ui('rdrag', target='ovB', dx=dx, dy=dy); time.sleep(1.8); away()
            b = rect(st(), 'ovB'); ui('api', op='ov_off'); time.sleep(0.4); return b
        b1 = ov_b(20, 15); open_panel(); ok = layout('save')
        sv = rows(saved, '')
        check('layout: "Save current layout" writes the sizes only (the 130 % size row, no place row)',
              ok and abs(sv.get('uikit-demo.stdpanel@size', (0, 0))[0] - f1) < 0.001 and all(k.endswith('@size') for k in sv), (ok, sv, f1))
        away(); ui('rdrag', target='stdp', dx=-50, dy=-30); time.sleep(1.8); click('stdp_size.minus'); click('stdp_size.minus'); away(); s = st()
        s2, f2 = rect(s, 'stdp'), size(s).get('f', 0); ui('api', op='stdpanel_off'); time.sleep(0.4)
        b2 = ov_b(-30, 20); open_panel()
        moved = not near(s1, s2) and abs(f2 - (f1 - 0.2)) < 0.001 and not near(b1, b2)
        ok = layout('reset'); away(); s = st(); s3, f3 = rect(s, 'stdp'), size(s).get('f', 0)
        ui('api', op='ov', a=1); time.sleep(0.8); away(); b3 = rect(st(), 'ovB'); ui('api', op='ov_off'); time.sleep(0.4)
        check('layout: moved + resized, then "Reset all" (confirmed): the saved 130 % size is back, both panels stay where they were moved',
              moved and ok and abs(f3 - f1) < 0.001 and abs(width(s3) - width(s1)) <= 2 and not near(s1, s3) and near(b2, b3), (moved, ok, s1, s2, s3, f1, f2, f3, b1, b2, b3))
        return
    # start 2 (a game restart): the saved file is still there and still used
    sv = rows(saved, 'uikit-demo.')
    j, s = open_panel(); click('stdp_size.minus'); away(); ui('rdrag', target='stdp', dx=-30, dy=-20); time.sleep(1.8)
    ok = layout('reset'); away(); s = st(); kp = rows(places, 'uikit-demo.stdpanel')
    check('layout: after a restart the saved sizes are still there; "Reset all" = 130 %, the panel keeps a place of its own',
          sv and ok and abs(size(s).get('f', 0) - 1.3) < 0.001 and 'uikit-demo.stdpanel' in kp, (sv, ok, kp, size(s)))
    os.makedirs(os.path.join(OUT, 'old'), exist_ok=True); os.replace(saved, os.path.join(OUT, 'old', 'ui_layout_saved.%d.tsv' % time.time()))
    ok = layout('reset'); away(); s = st(); r_reset, kp = rect(s, 'stdp'), rows(places, 'uikit-demo.stdpanel')
    ui('api', op='stdpanel_off'); time.sleep(0.4); j, s = open_panel(); away(); s = st(); r_new = rect(s, 'stdp')
    check('layout: no saved layout: "Reset all" = the mod\'s own place and 100 % (same as a panel never moved)',
          ok and abs(size(s).get('f', 0) - 1.0) < 0.001 and not kp and near(r_reset, r_new), (ok, size(s), kp, r_reset, r_new))
    ui('api', op='stdpanel_off'); time.sleep(0.4)


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
    anchor_cases()
    key_cases()
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
    layout_cases('start1')
    # made while the panel is hidden (the chat's closed box): the title button clones lost their label when they first showed
    ui('api', op='stdpanel_off'); time.sleep(0.4)
    j = ui('api', op='stdpanel_hidden'); time.sleep(1.0); hover_on(); s = st(); ui('capture', name='start1_hidden'); away()
    check('start 1: made in a hidden panel, they still show "-" and "+" once it shows', j.get('rc') == 0 and size(s).get('labels_on') == 1 and labels_ok(size(s).get('labels')), (j.get('rc'), size(s)))


def fold_cases():
    # FOLD RULE (owner 2026-11-01: "the size of the dps check window changes depending on collapsed expanded toggles... It
    # shouldn't"): a UI Scale panel 300 high folded, 900 open (taller than the screen at UI Scale 1.4): its width stays
    j = ui('api', op='secfit', a=1); time.sleep(1.0); away(); s = st(); r0 = rect(s, 'fitp')
    if j.get('rc') != 0 or width(r0) <= 0:
        check('fold: the fold panel opens', False, (j, r0)); return
    ui('click', target='fith'); time.sleep(1.5); away(); s = st(); r1 = rect(s, 'fitp')
    check('fold: opening its section makes the panel taller only (width = scale kept, < 1 %)', abs(width(r1) / width(r0) - 1) < 0.01 and (r1[3] - r1[1]) > 2 * (r0[3] - r0[1]), (r0, r1))
    ui('click', target='fith'); time.sleep(1.5); away(); s = st(); r2 = rect(s, 'fitp')
    check('fold: folding it again: back to the first size', abs(width(r2) / width(r0) - 1) < 0.01 and abs((r2[3] - r2[1]) - (r0[3] - r0[1])) <= 2, (r0, r2))
    ui('api', op='secfit', a=0); time.sleep(0.4)


def start2():
    mods_win(); km, kpl = wtext('size-key-minus'), wtext('size-key-plus'); win('esc'); time.sleep(0.8)
    check('start 2: the kit 0.1.30 choice "Ctrl + keypad" is kept: the rows show Ctrl + Keypad - / Ctrl + Keypad +',
          km == 'Ctrl + Keypad -' and kpl == 'Ctrl + Keypad +', (km, kpl))
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
    ui('api', op='stdpanel_off'); time.sleep(0.4)
    fold_cases()
    layout_cases('start2')


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
    for f in ((places, os.path.join(os.path.dirname(places), 'ui_layout_saved.tsv')) if fresh else ()):
        if os.path.exists(f): os.makedirs(os.path.join(OUT, 'old'), exist_ok=True); os.replace(f, os.path.join(OUT, 'old', '%s.%d' % (os.path.basename(f), time.time())))
    if tag == 'start2' and os.path.exists(places):                       # a kit 0.1.30 player's file: "Size keys" = Ctrl + keypad, no key rows
        keep = [l for l in open(places) if not l.startswith('kit@size_key')]
        open(places + '.tmp', 'w').write(''.join(keep) + 'kit@size_keys\t1.0\t0.0\n'); os.replace(places + '.tmp', places)
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
