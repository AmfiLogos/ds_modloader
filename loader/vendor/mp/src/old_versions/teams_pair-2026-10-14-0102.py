#!/usr/bin/env python3
"""Teams screen on a leased hidden pair, proved by read-back from the widgets on both peers.

  teams_pair.py [--dylib PATH] [--keep]

X hosts the ElvenExpedition save (det on), Y joins. Both open the Teams screen (ui_teams open) and every
step reads back `ui_teams state` on BOTH peers: the model columns (dsnet ownership / marks / ready) and the
real portrait clones (unit = SubUI_UnitShortCutParts._owner +0x228, SetSelected flag +0x21B, header texts).
Steps: Y takes a free unit, marks it, readies; X (host) picks a free unit and drops it on its own column,
marks it, readies -> everyone ready -> dsnet EXPEDITION_START clears ready (marks kept); Y is denied the
host's unit; the host moves Y's unit to Free -> Y's mark is gone. Fingerprints stay equal."""
import json, os, shutil, sys, time
RIG = os.path.expanduser('~/ds_multiplayer/rig')
sys.path.insert(0, RIG)
import rig

PORT = 7795
results = {}


def arg(name, default):
    a = sys.argv
    return type(default)(a[a.index(name) + 1]) if name in a else default


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def c(x, **k):
    return rig.send(x, k, timeout=120)


def st(x):
    for _ in range(10):
        s = c(x, cmd='status')
        if 'tick' in s:
            return s
        time.sleep(3)
    raise RuntimeError(f'{x}: no status: {s}')


def wait(what, pred, timeout=900, every=2):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = pred()
        if r:
            return r
        time.sleep(every)
    raise RuntimeError('timeout: ' + what)


def check(name, ok, info=''):
    results[name] = bool(ok)
    log(('PASS ' if ok else 'FAIL ') + name, info)


def tm(x, **k):
    return c(x, cmd='ui_teams', **k)


def col_of(s, guid):
    for col in s['columns']:
        if guid in col['model']:
            return col['player']
    return None


def widget(s, guid):
    return next((w for w in s['widgets'] if w['guid'] == guid), None)


def settle(X, Y, pred, what, timeout=30):
    """wait until pred(state X, state Y) holds on both peers (commands run a few steps later)"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        a, b = tm(X), tm(Y)
        if pred(a, b):
            return a, b
        time.sleep(1)
    log('settle timeout:', what, json.dumps(tm(X))[:1500], json.dumps(tm(Y))[:1500])
    return tm(X), tm(Y)


SHOTS = os.path.expanduser('~/ds_multiplayer/build/teams_test/shots')
HUD_POS = [480, -46]                                   # ui_teams.c HUD_X / HUD_Y: fixed, not under the portrait bar


def propose(x, units, timeout=60, region=None):
    """the game's Go to dungeon: the stock expedition window (local), step 1 = pick units (allies' too), Next,
    step 2 = supplies, its last button ("Send to friends") sends the proposal"""
    tm(x, action='go')
    t0 = time.time()
    while time.time() - t0 < timeout and not tm(x)['stock']['open']:
        time.sleep(1)
    tm(x, action='stock_select', units=','.join(units)); time.sleep(1)
    tm(x, action='stock_next')
    while time.time() - t0 < timeout and tm(x)['stock']['state'] != 1:
        time.sleep(1)
    return tm(x, action='stock_accept')


def texts(s):
    """every text of the screen + the HUD button, for the no-<b> rule (owner 2026-10-13: game fonts only)"""
    return [s.get(k, '') for k in ('title', 'rules', 'region_text', 'ready_text', 'hint', 'hud_text')] + [c_.get('header', '') for c_ in s['columns']]


def shot(X, Y, tag):
    for x, who in ((X, 'host'), (Y, 'guest')):
        p = f'{SHOTS}/teams_{tag}_{who}.png'
        r = tm(x, action='shot', path=p)
        log('frame', p, 'rc', r.get('rc'))


def fp_equal_from(x, y, t_from):
    def rd(i):
        d = {}
        for line in open(os.path.join(rig.idir(i), 'fp.log')):
            p = line.split()
            if len(p) >= 2 and p[0].isdigit() and int(p[0]) >= t_from:
                d[int(p[0])] = p[1]
        return d
    a, b = rd(x), rd(y)
    com = sorted(set(a) & set(b))
    return len(com), next((t for t in com if a[t] != b[t]), None)


def main():
    save = arg('--save', os.path.join(rig.SEED_DATA, 'Saves', 'MoonlightBrigade__AutoSave_41e9c1bb8790492cbdc10f4dcad22179_0.json'))  # CurrentMap 0 = base
    rig.DYLIB = arg('--dylib', os.path.expanduser('~/ds_multiplayer/build/teams_test/dsrig_teams.dylib'))
    pair = rig.lease('teams', os.getpid())
    if not pair:
        raise SystemExit('no free pair')
    X, Y = pair[0], pair[1]
    global PORT
    PORT = rig.PAIR_PORTS.get(pair, PORT)
    log('pair', pair, 'dylib', rig.DYLIB)
    try:
        assert rig.check_prefs(), 'owner prefs differ from backup before start'
        json.dump({'role': 'host', 'port': PORT, 'name': 'The owner', 'allow_speedup': True, 'allow_pause': True},
                  open(os.path.join(rig.idir(X), 'mp.json'), 'w'))
        json.dump({'role': 'client', 'host': '127.0.0.1', 'port': PORT, 'name': 'Friend'},
                  open(os.path.join(rig.idir(Y), 'mp.json'), 'w'))
        rig.start(X, fps=10, window=True)
        rig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + X, timeout=1500)
        work = os.path.join(rig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, os.path.basename(save)); shutil.copy2(save, src)
        log(X, 'load', c(X, cmd='load_save', file=src, det=1))
        wait(X + ' locked', lambda: st(X).get('det', {}).get('locked'))
        rig.start(Y, fps=30, window=True)
        rig.wait_for(Y, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title ' + Y, timeout=1500)
        wait(Y + ' loaded the host save', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(Y)), timeout=900)
        time.sleep(8)
        t0 = st(X)['tick']

        a, b = tm(X), tm(Y)
        check('HUD: framed "Teams" button on both (next to the player panel)', a['hud'] and b['hud'] and a['hud_text'] == 'Teams' and b['hud_text'] == 'Teams'
              and all(s_['hud_shown'] == (0 if s_['lobby_open'] else 1) for s_ in (a, b)), f"lobby {a['lobby_open']}/{b['lobby_open']} shown {a['hud_shown']}/{b['hud_shown']} X {a['hud_pos']} {a['hud_text']!r} Y {b['hud_pos']} {b['hud_text']!r}")
        p = f'{SHOTS}/teams_hud_host.png'; log('frame', p, 'rc', tm(X, action='shot', path=p).get('rc'))
        a, b = tm(X, action='open'), tm(Y, action='open')
        time.sleep(1)
        shot(X, Y, 'open')
        a, b = tm(X), tm(Y)
        check('screen opens on both (backdrop + HUD button built)', a['open'] and a['built'] and b['open'] and b['built'],
              f"X ok={a['ok']} hud={a['hud']} units={a['units']} / Y ok={b['ok']} hud={b['hud']} units={b['units']}")
        check('modal: own canvas sorted on top (32000) + raycaster, both peers',
              a['modal_sorting'] == 32000 and b['modal_sorting'] == 32000 and a['modal_raycaster'] and b['modal_raycaster'] and a['over_ui'] == 1,
              f"sorting {a['modal_sorting']}/{b['modal_sorting']} raycaster {a['modal_raycaster']}/{b['modal_raycaster']} over_ui {a['over_ui']}")
        check('texts: Teams only allocates (take / free / lend); expeditions via Go to dungeon; no Ready / Go button',
              'Left-click' in a['rules'] and 'Lend' in a['rules'] and 'Go to dungeon' in a['rules'] and a['rules'].count('\n') == 1
              and 'Go to dungeon' in a['hint'] or 'take a free unit' in a['hint'], repr(a['rules'][:200]) + ' / ' + repr(a['hint']))
        tm(X, action='info'); time.sleep(2); ti = tm(X); tm(X, action='shot', path=f'{SHOTS}/teams_info_host.png'); tm(X, action='info'); time.sleep(2); to = tm(X)
        check('rules behind the stock info icon: a click shows the tooltip, a second click hides it',
              ti['info_icon'] == 1 and ti['tip_shown'] == 1 and to['tip_shown'] == 0, f"icon {ti['info_icon']} shown {ti['tip_shown']} -> {to['tip_shown']}")
        check('no <b> in any text (game fonts only)', not any('<b>' in t for s_ in (a, b) for t in texts(s_)),
              str([t[:60] for s_ in (a, b) for t in texts(s_) if '<b>' in t]))
        check('game window + game fonts (Task Planner frame, Cinzel title, Lora body), full-screen dim layer',
              a['window'] == [1320, 888] and 'Cinzel' in a['font_title'] and 'Lora' in a['font_body'] and a['root'][0] >= 1000 and a['title'].startswith('Teams'),
              f"window {a['window']} root {a['root']} fonts {a['font_title']} / {a['font_body']} title {a['title']!r}")
        check('in the base (screen not locked)', a['in_dungeon'] == 0 and b['in_dungeon'] == 0)
        check('session running on both (no "not started" banner)', a['running'] == 1 and b['running'] == 1)
        check('no Ready count in the title, no marks shown', all(w['mark_shown'] == 0 for w in a['widgets']))
        check('ready api present (netcore EXPEDITION_READY)', a['ready_api'] == 1 and b['ready_api'] == 1)
        check('same units and columns on both', [c_['model'] for c_ in a['columns']] == [c_['model'] for c_ in b['columns']],
              f"columns {[ (c_['player'], len(c_['model'])) for c_ in a['columns']]}")
        check('one portrait clone per unit, showing that unit',
              len(a['widgets']) == a['units'] and all(w['unit_guid'] == w['guid'] and w['active'] for w in a['widgets'])
              and len(b['widgets']) == b['units'] and all(w['unit_guid'] == w['guid'] for w in b['widgets']),
              f"X {len(a['widgets'])}/{a['units']}, Y {len(b['widgets'])}/{b['units']}")
        log('X headers:', [col.get('header') for col in a['columns']])
        free = next(col for col in a['columns'] if col['player'] == -1)['model']
        check('at least 2 free units to play with', len(free) >= 2, f'{len(free)} free')
        uy, ux = free[0], free[1]

        tm(Y, action='click', guid=uy, button=0)                      # Y takes a free unit
        a, b = settle(X, Y, lambda a, b: col_of(a, uy) == 1 and col_of(b, uy) == 1, 'Y claim')
        check('Y takes a free unit: in Y column on both peers', col_of(a, uy) == 1 and col_of(b, uy) == 1)
        r = tm(Y, action='click', guid=uy, button=1)                  # right-click: nothing in Teams (no marks here)
        time.sleep(2); a, b = tm(X), tm(Y)
        check('right-click on my unit does nothing in Teams (no marks)', col_of(b, uy) == 1 and widget(b, uy)['mark_shown'] == 0 and b['ready'] == 0)
        tm(X, action='click', guid=ux, button=0)                      # host: plain left-click takes a free unit
        a, b = settle(X, Y, lambda a, b: col_of(a, ux) == 0 and col_of(b, ux) == 0, 'host take')
        check('host takes a free unit with one left click: both peers', col_of(a, ux) == 0 and col_of(b, ux) == 0)
        r = tm(X, action='click', guid=uy, button=0)                  # admin mode off: the Friend's unit is not clickable
        check('admin OFF: host cannot touch the Friend unit', r['rc'] == -4 and r['picked'] == -1 and col_of(r, uy) == 1)
        r = tm(X, action='admin', on=1)                               # host: the lobby's Admin mode command (DSNET_ADMIN_MODE)
        settle(X, Y, lambda a, b: a['admin'] == 1 and b['admin'] == 1, 'admin on')
        time.sleep(1)
        r = tm(X, action='click', guid=uy, button=0)
        check('admin ON: host picks up the Friend unit (no change yet)', r['picked'] >= 0 and col_of(r, uy) == 1)
        tm(X, action='click', guid=uy, button=0)                      # click again = put it back
        tm(Y, action='click', guid=ux, button=0)                      # Y may not take the host's unit
        time.sleep(3)
        a, b = tm(X), tm(Y)
        check("Y cannot take the host's unit", col_of(a, ux) == 0 and col_of(b, ux) == 0)
        shot(X, Y, 'after_take')
        r = tm(Y)
        check("Y's own units on top, then Free, then the host's section; host's cards greyed + not clickable for Y",
              [c_['header_player'] for c_ in sorted([c_ for c_ in r['columns'] if 'section_index' in c_], key=lambda c_: c_['section_index'])][:3] == [1, -1, 0]
              and widget(r, ux)['clickable'] == 0 and widget(r, ux)['alpha'] < 0.5 and widget(r, uy)['clickable'] == 1,
              str([(c_.get('header_player'), c_.get('section_index')) for c_ in r['columns']]))
        mk = lambda s, p: next(col for col in s['columns'] if col['player'] == p)['marked']
        tm(X, action='header', player=-1)                             # host: target column Free, then click Y's unit
        tm(X, action='click', guid=uy, button=0)
        a, b = settle(X, Y, lambda a, b: col_of(a, uy) == -1 and col_of(b, uy) == -1, 'host frees Y unit')
        check("host frees Y's unit: free on both", col_of(a, uy) == -1 and col_of(b, uy) == -1)
        r = tm(X, action='header', player=-1)
        check('host target column toggles off', r['target'] == -2)
        tm(Y, action='click', guid=uy, button=0)                      # Friend takes it back, marks, readies
        settle(X, Y, lambda a, b: col_of(b, uy) == 1, 'Y retake')
        a, b = tm(X), tm(Y)
        shot(X, Y, 'marked')
        check('Teams shows no expedition state: no Ready, no marks', all(w['mark_shown'] == 0 for s_ in (a, b) for w in s_['widgets']) and a['starts'] == 0)
        time.sleep(3)
        n, first = fp_equal_from(X, Y, t0)
        check('fingerprints equal every tick', n > 100 and first is None, f'common ticks {n}, first diff {first}')
        tm(X, action='escape'); time.sleep(2)
        check('Escape closes the Teams window', not tm(X)['open'])
        tm(X, action='open'); time.sleep(1)
        # the Friend leaves: his units stay RESERVED for him (netcore stable ids)
        tm(X, action='admin', on=0)
        time.sleep(3)
        rig.stop(Y)
        time.sleep(12)
        tm(X, action='open'); time.sleep(2)
        try:                                                       # the screen re-lays out within a few frames (slow under nice 19)
            r = wait('reserved section on screen', lambda: (lambda r: r if any(c_['player'] == -2 and c_.get('header') for c_ in r['columns']) else None)(tm(X)), timeout=120, every=3)
        except RuntimeError as e:
            log(e); r = tm(X)
        rh = [c_ for c_ in r['columns'] if c_['player'] == -2]
        check("Friend left: his unit shows 'Reserved for Friend', not free, not clickable",
              bool(rh) and 'Reserved for Friend' in rh[0].get('header', '') and uy in rh[0]['model'] and widget(r, uy)['clickable'] == 0
              and 'units you click go here' not in rh[0].get('header', ''),
              str([(c_['player'], c_.get('header', '')[:80]) for c_ in r['columns']]))
        p = f'{SHOTS}/teams_reserved_host.png'; log('frame', p, 'rc', tm(X, action='shot', path=p).get('rc'))
        for i in (X, Y):
            with open(os.path.join(rig.idir(i), 'rig.log'), errors='replace') as f:
                for line in f:
                    if 'teams:' in line:
                        log(i, '|', line.rstrip()[:200])
    finally:
        if '--keep' not in sys.argv:
            for i in pair:
                try:
                    rig.stop(i)
                except Exception as e:
                    log('stop', i, e)
            rig.release(pair)
        log('RESULT', 'PASS' if results and all(results.values()) else 'FAIL', json.dumps(results))


if __name__ == '__main__':
    main()
