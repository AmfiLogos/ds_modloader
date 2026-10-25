#!/usr/bin/env python3
"""Hell always loaded; the New Game screen's switch only shows or hides the Hell level (owner 2026-10-25: "keep it always on
& have the toggle just activate its display"). On a hidden Mac test copy (default R; DSL_TEST_COPY), never the owner's game,
saves or screen. Uses this worktree's build/ (loader + mods) and loader/lang.

Full run (no LANGTYPE), ONE game process all along (no restart anywhere):
 1. A starts with Hell NOT in the profile (multiplayer only; the seed world is registered with gcd-cdr): Hell runs anyway.
    New Game -> settings: the switch shows, off (no newgame.json). Hell is not offered: combat and management stop at 4,
    both "+" locked at 4, a level 5 written past the selectors falls back to 4.
 2. Switch on: no restart, newgame.json says shown, Hell offered ("+" open at 4, combat 5 = the Hell text).
 3. A world on combat Hell: registered WITH hell-difficulty, its save says combat 5, the multiplayer set (worlds.json
    modset_lines) has Hell while it is open and not at the title; loaded again: no question, no refusal, same process.
 4. Switch off with combat Hell chosen: falls back to 4. A world on combat Very Hard (3): registered WITHOUT Hell, its save
    says 3, no Hell in the multiplayer set while it is open; loaded again: same process.
 5. The seed world kept its set.
Breaks caught: a Hell world loses Hell (wrong difficulty next load; a guest without Hell gets a save it cannot read), a world
not on Hell gets Hell (a save the stock game cannot read; players without Hell refused), a reload to change worlds, Hell
offered with the switch off.
LANGTYPE=3 (de) / 4 (ru) / 7 (ja) ...: the Mods window's Hell row and the settings panel with the switch, shots, text fit.
Exit 0 = every check passed. Output: <out>/ (logs, shots, summary.json)."""
import glob, json, os, shutil, signal, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
LANGTYPE = os.environ.get('LANGTYPE')
LANGCODE = {'1': 'ko', '2': 'fr', '3': 'de', '4': 'ru', '5': 'zh-Hans', '6': 'zh-Hant', '7': 'ja', '8': 'es', '9': 'pt-BR'}.get(LANGTYPE or '', 'en')
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/hell_toggle_' + LANGCODE)
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT; V.UI = os.path.join(OUT, 'ui'); os.makedirs(V.UI, exist_ok=True)
X = os.environ.get('DSL_TEST_COPY', 'R')
T.X = X; T.I = r.idir(X); T.SDIR = os.path.join(T.I, 'softrestart'); T.MODS = os.path.join(T.I, 'data', 'Mods')
check, log = T.check, T.log
HELL = 'hell-difficulty'
if LANGTYPE:
    _prep = r.prepare_data
    def _prep_lang(x):
        _prep(x); us = os.path.join(r.idir(x), 'data', 'UserSetting.json'); s = json.load(open(us))
        s['GeneralSettingData']['SaveCurrentLanguageType'] = int(LANGTYPE); json.dump(s, open(us, 'w'), indent=1)
    r.prepare_data = _prep_lang


def session():
    try: return json.load(open(os.path.join(T.MODS, 'worlds.json')))
    except Exception: return {}


def worlds(): return session().get('worlds', {})
def hello_has_hell(): return any(l.startswith(HELL + '@') for l in session().get('modset_lines', []))


def ng(op, **kw):
    """a New Game screen request; returns its state file (written after the request ran)"""
    p = os.path.join(T.SDIR, 'newgame_state.json'); t0 = os.path.getmtime(p) if os.path.exists(p) else 0
    T.request(dict(kw, cmd='newgame', op=op))
    t1 = time.time()
    while time.time() - t1 < 10:
        if os.path.exists(p) and os.path.getmtime(p) != t0:
            try: return json.load(open(p, encoding='utf-8', errors='replace'))
            except Exception: pass
        time.sleep(0.1)
    raise RuntimeError('no newgame state after ' + op)


def wait_state(pred, what, limit=60):
    t0 = time.time(); s = {}
    while time.time() - t0 < limit:
        s = ng('state')
        if pred(s): return s
        time.sleep(0.5)
    raise RuntimeError(f'timeout waiting for {what}: {s}')


def to_settings():
    ng('open')
    wait_state(lambda s: s['open'] and s['view_type'] == 0, 'New Game screen', 30)
    time.sleep(1)
    ng('next')
    return wait_state(lambda s: s['settings_shown'] and s['view_type'] == 2 and s['toggles'] and s['toggles'][0]['shown'], 'settings panel with the switch', 30)


def tog(s): return s['toggles'][0] if s.get('toggles') else {}


def side_ok(s, tag):
    """the Custom difficulty panel: right of the New Game window, its top level with the settings list's top (fixed while the
    list scrolls, owner 2026-10-25; lower only under the title corner), on screen (newgame_side_test.py: scroll, sizes)"""
    S, W, L, C, sc = s['side_now'], s['frame'], s['list'], s['corner'], s['screen']
    check(f'{tag}: Custom difficulty panel right of the window, level with the list top, on screen',
          s['side_now_shown'] == 1 and S[0] >= W[2] and (abs(S[3] - L[3]) <= 2 or (s['corner_shown'] and S[3] <= C[1])) and S[2] <= sc[0] and S[1] >= 0, (S, W, L, C, sc))


def save_of(guid):
    """the newest save file of that world and its header's CombatDifficulty"""
    hx = guid.replace('-', '')
    fs = sorted(glob.glob(os.path.join(T.I, 'data', 'Saves', f'*_{hx}_*.json')), key=os.path.getmtime)
    if not fs: return None, None
    try: d = json.load(open(fs[-1], encoding='utf-8-sig'))
    except Exception: return os.path.basename(fs[-1])[:-5], None
    return os.path.basename(fs[-1])[:-5], d.get('CampaignSaveHeader', {}).get('CombatDifficulty')


def start_world(before):
    """the settings panel's Start: the intro skipped, the world open, registered; its guid"""
    ng('start')
    t0 = time.time(); new = None
    while time.time() - t0 < 240:
        try:
            st = T.status(); r.send(X, {'cmd': 'mpm_skipintro'}, timeout=10, connect_wait=0)
        except Exception:
            st = {}
        new = [g for g in worlds() if g not in before]
        if st.get('scene') == 2 and not st.get('busy') and new: break
        time.sleep(2)
    time.sleep(2)                                                  # the multiplayer set is written for the open world
    return new[0] if new else None


def quit_title():
    r.send(X, {'cmd': 'mpm_quit_title'}, timeout=20, connect_wait=0)
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', 120); time.sleep(3)


def reload_same(guid, tag, a):
    """load that world's save again from the title: it opens in THIS process (no refusal, no restart)"""
    name, _ = save_of(guid)
    mark = len(T.dlog())
    if name: r.send(X, {'cmd': 'load_save', 'name': name}, timeout=20, connect_wait=0)
    try: T.wait(lambda s: s.get('scene') == 2 and not s.get('busy'), tag + ' loaded again', 120); ok = True
    except Exception: ok = False
    time.sleep(2)
    L = V.since(mark)
    check(f'{tag}: loads again in the same game: no question, no refusal, no restart',
          ok and 'REFUSED' not in L and 'This world uses' not in L and 'A: handoff written' not in L and T.alive(a), (a, T.game_pids(), L[-500:]))


def one_process(a, tag):
    check(f'{tag}: still the first game process (no restart)', T.alive(a) and 'A: handoff written' not in T.dlog(), (a, T.game_pids()))


def lang_run():
    a = T.start_a(enabled=('multiplayer',), extra_env={'DSL_UI_TEST': V.UI})
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'title'); time.sleep(3)
    V.ui('open'); time.sleep(2); st = V.ui('state')
    row = [c for c in st.get('controls', []) if c['name'] == 'always:' + HELL]
    check(f'{LANGCODE}: Mods window: Hell row says "always on" in the language', row and row[0]['text'] and ('Always on' not in row[0]['text'] or LANGCODE == 'en'), row)
    V.ui('shot', name=f'{LANGCODE}_mods_window_hell'); V.ui('close'); time.sleep(1.5)
    s = to_settings(); time.sleep(1.5); s = ng('state'); t = tog(s)
    T.shot(f'{LANGCODE}_settings_switch_off')
    check(f'{LANGCODE}: switch label in the language', t.get('label') and (t['label'] != 'Hell difficulty' or LANGCODE == 'en'), t.get('label'))
    check(f'{LANGCODE}: label fits its switch (one line, box free)', t.get('text_w', 0) + 60 <= t.get('width', 0), t)
    side_ok(s, LANGCODE)
    s = ng('toggle', i=0); time.sleep(1); ng('set', combat=5); time.sleep(1); s = ng('state')
    T.shot(f'{LANGCODE}_settings_hell_shown')
    check(f'{LANGCODE}: switch on shows Hell (combat 5)', s['combat'] == 5, s)
    return a


def full_run():
    a = T.start_a(enabled=('multiplayer',), worlds={T.GUID: 'gcd-cdr'})
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title'); time.sleep(3)
    L = T.dlog()
    check('Hell runs although the profile does not switch it on', f'{HELL}: custom difficulty: runs whenever installed' in L and 'Hell difficulty ready' in L and 'Hell tables ready' in L,
          [l for l in L.splitlines() if HELL in l][:6])
    check('title: the multiplayer set leaves Hell out', not hello_has_hell(), session().get('modset_lines'))
    check('newgame.json absent: the switch starts off', not os.path.exists(os.path.join(T.MODS, 'newgame.json')))
    # 1 switch off: no Hell
    s = to_settings()
    ng('set', combat=4, management=4); time.sleep(1); s = ng('state'); t = tog(s)
    check('switch shown, off, Hell running', t.get('shown') == 1 and t.get('on') == 0 and t.get('running') == 1, t)
    side_ok(s, 'off')
    check('switch off: no Hell (levels stop at 4, both "+" locked at 4)', (s['max_level'], s['top_level'], s['combat'], s['management'], s['inc_combat'], s['inc_management']) == (4, 5, 4, 4, 0, 0), s)
    ng('set', combat=5, management=5); time.sleep(1); s = ng('state')
    check('switch off: asking for level 5 gives 4', (s['combat'], s['management']) == (4, 4), s)
    ng('force', combat=5); time.sleep(1.5); s = ng('state')
    check('switch off: a level 5 written past the selectors falls back to 4', s['combat'] == 4, s)
    T.shot('1_settings_hell_hidden')
    # 2 switch on: Hell shows, no restart
    mark = len(T.dlog())
    s = ng('toggle', i=0); time.sleep(1.5); s = ng('state'); t = tog(s)
    one_process(a, 'switch on')
    check('switch on: saved (newgame.json), Hell offered ("+" open at 4)', t.get('on') == 1 and t.get('saved') == 1 and s['max_level'] == 5 and s['inc_combat'] == 1 and s['inc_management'] == 1, (t, s))
    ng('set', combat=5, management=4); time.sleep(1); s = ng('state')
    check('switch on: combat Hell can be chosen', s['combat'] == 5 and s['combat_text'] and s['combat_text'] != s['management_text'], s)
    check('switch on: no restart asked (no Apply, no soft restart)', 'reg: Apply' not in V.since(mark) and 'soft restart' not in V.since(mark), V.since(mark)[-400:])
    T.shot('2_settings_hell_shown')
    # 3 a world on Hell (management Devastation: the first scenario asks "play the tutorial?" unless a level is 4)
    before = dict(worlds())
    g1 = start_world(before)
    w = worlds()
    check('Hell world registered with hell-difficulty', g1 and HELL in w.get(g1, '').split('+'), (g1, w.get(g1)))
    check('Hell world open: the multiplayer set has Hell', hello_has_hell(), session().get('modset_lines'))
    one_process(a, 'Hell world made')
    quit_title()
    name1, cd1 = save_of(g1) if g1 else (None, None)
    check('Hell world save: combat difficulty 5', cd1 == 5, (name1, cd1))
    check('back at the title: the multiplayer set leaves Hell out again', not hello_has_hell(), session().get('modset_lines'))
    if g1: reload_same(g1, 'Hell world', a)
    check('Hell world still on Hell after the load (save, set, multiplayer set)', g1 and save_of(g1)[1] == 5 and HELL in worlds().get(g1, '') and hello_has_hell(),
          (save_of(g1) if g1 else None, worlds().get(g1), session().get('modset_lines')))
    quit_title()
    # 4 switch off with Hell chosen -> 4; a world on Very Hard (3)
    s = to_settings(); t = tog(s)
    check('the switch kept its state (on)', t.get('on') == 1 and t.get('saved') == 1, t)
    ng('set', combat=5, management=4); time.sleep(1)
    s = ng('toggle', i=0); time.sleep(1.5); s = ng('state'); t = tog(s)
    check('switch off with combat Hell chosen: it falls back to 4, "+" locked again', t.get('on') == 0 and t.get('saved') == 0 and s['combat'] == 4 and s['inc_combat'] == 0, (t, s))
    T.shot('3_settings_hell_hidden_again')
    ng('set', combat=3, management=4); time.sleep(1)
    before = dict(worlds())
    g2 = start_world(before)
    w = worlds()
    check('Very Hard world: no hell-difficulty in its set', g2 and HELL not in w.get(g2, '+').split('+'), (g2, w.get(g2)))
    check('Very Hard world open: the multiplayer set leaves Hell out', g2 and not hello_has_hell(), session().get('modset_lines'))
    quit_title()
    name2, cd2 = save_of(g2) if g2 else (None, None)
    check('Very Hard world save: combat difficulty 3', cd2 == 3, (name2, cd2))
    if g2: reload_same(g2, 'Very Hard world', a)
    check('Very Hard world still without Hell after the load', g2 and HELL not in worlds().get(g2, '+').split('+') and not hello_has_hell(), (worlds().get(g2), session().get('modset_lines')))
    quit_title()
    w = worlds()
    check('other worlds unchanged (seed world gcd-cdr, Hell world with Hell)', w.get(T.GUID) == 'gcd-cdr' and g1 and HELL in w.get(g1, ''), w)
    one_process(a, 'end')
    return a


def main():
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    sv = os.path.join(T.I, 'data', 'Saves')                     # a clean save folder (the old one moved aside)
    if os.path.isdir(sv):
        old = os.path.join(T.I, 'old', time.strftime('%Y%m%d-%H%M%S') + '_saves'); os.makedirs(os.path.dirname(old), exist_ok=True); shutil.move(sv, old)
    ngj = os.path.join(T.MODS, 'newgame.json')                  # off by default: no switch state from an earlier run
    if os.path.exists(ngj):
        os.makedirs(os.path.join(T.MODS, 'old'), exist_ok=True); os.replace(ngj, os.path.join(T.MODS, 'old', 'newgame.json.%d' % time.time()))
    V.install(); r.install_lang(X)
    threading.Thread(target=T.guard, daemon=True).start()
    try:
        lang_run() if LANGTYPE or os.environ.get('HT_LANG_ONLY') == '1' else full_run()
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        for p in T.game_pids():
            try: r.send(X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
            except Exception: pass
            time.sleep(3)
            if T.alive(p): os.kill(p, signal.SIGKILL)
        for f in ['dsload.log', 'dsload.prev.log', 'player.log']:
            if os.path.exists(os.path.join(T.I, f)): shutil.copy2(os.path.join(T.I, f), os.path.join(OUT, f))
        for f in glob.glob(os.path.join(T.MODS, '*.json')): shutil.copy2(f, os.path.join(OUT, os.path.basename(f)))
        json.dump({'checks': T.checks, 'violations': T.violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        n_ok = sum(c['ok'] for c in T.checks)
        print('RESULT', 'PASS' if T.checks and n_ok == len(T.checks) else 'FAIL', f'{n_ok}/{len(T.checks)}', OUT, flush=True)
    return 0 if T.checks and all(c['ok'] for c in T.checks) else 1


if __name__ == '__main__':
    sys.exit(main())
