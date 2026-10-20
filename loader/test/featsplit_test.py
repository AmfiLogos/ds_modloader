#!/usr/bin/env python3
"""Launcher mods vs game mods (owner 2026-10-20) on test copy S (hidden, muted, staged; take a lease first). Uses
this worktree's build/ (dsload.dylib; the mods already in the copy: multiplayer, custom-modes = Units Laboratory, gcd-cdr).
 run 1: Units Laboratory OFF, the world saved WITH it (worlds.json "custom-modes+gcd-cdr"):
        the Mods window has two tabs, "Game mods" (open first: game mods only) and "Launcher mods" (Multiplayer + Units
        Laboratory only); Units Laboratory switched on in the Launcher tab stays on after a visit to the Game tab and
        Apply turns it on (soft restart); the save loads with no "other mods" refusal; the stored key loses custom-modes.
 run 2: Units Laboratory ON, the world saved WITHOUT it ("gcd-cdr"): the row shows only gcd-cdr, the card names the
        features in their own block; the world's Mods window ([Change]) lists no feature; the save loads, no refusal.
Breaks caught: a world refused ("This world uses other mods") because of a launcher feature; features shown as world mods;
a tab that lists the other tab's mods; a switch lost when the player changes tab; Apply that misses the other tab's change.
LANGTYPE=3 (de) / 4 (ru) ...: run 1's tab part only, in that language (labels = loader/lang/<code>.json), shots to look at.
Output: <out>/ (logs, shots, summary.json)."""
import json, os, sys, time, glob, shutil, signal, threading
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import l1_test as T
import l1_v13_test as V
r = T.r
OUT = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_featsplit/out/featsplit')
os.makedirs(OUT, exist_ok=True); T.OUT = OUT; V.OUT = OUT
V.UI = os.path.join(OUT, 'ui'); os.makedirs(V.UI, exist_ok=True)
check, log = T.check, T.log
FEAT = {'multiplayer', 'custom-modes'}
LANGTYPE = os.environ.get('LANGTYPE')                    # the game's LanguageType (3 de, 4 ru)
LANGCODE = {'3': 'de', '4': 'ru', '1': 'ko', '2': 'fr', '5': 'zh-Hans', '6': 'zh-Hant', '7': 'ja', '8': 'es', '9': 'pt-BR'}.get(LANGTYPE or '', 'en')
if LANGTYPE:
    _prep = T.r.prepare_data
    def _prep_lang(x):
        _prep(x); us = os.path.join(T.r.idir(x), 'data', 'UserSetting.json'); s = json.load(open(us))
        s['GeneralSettingData']['SaveCurrentLanguageType'] = int(LANGTYPE); json.dump(s, open(us, 'w'), indent=1)
    T.r.prepare_data = _prep_lang
LANG = json.load(open(os.path.join(HERE, '..', 'lang', LANGCODE + '.json'), encoding='utf-8'))
L_GAME, L_LAUNCH = LANG.get('Game mods', 'Game mods'), LANG.get('Launcher mods', 'Launcher mods')


def worlds():
    return json.load(open(os.path.join(T.MODS, 'worlds.json'))).get('worlds', {})


def quit_all():
    for p in T.game_pids():
        try: r.send(T.X, {'cmd': 'quit'}, timeout=5, connect_wait=0)
        except Exception: pass
        time.sleep(3)
        if T.alive(p): os.kill(p, signal.SIGKILL)
    for f in ['dsload.log', 'dsload.prev.log']:
        if os.path.exists(os.path.join(T.I, f)): shutil.copy2(os.path.join(T.I, f), os.path.join(OUT, f'{f}.{RUN[0]}'))


RUN = [0]
def one_run(n, enabled, key, S):
    RUN[0] = n
    T.start_a(enabled=enabled, extra_env={'DSL_UI_TEST': V.UI}, worlds={T.GUID: key})
    T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), f'run {n} title'); time.sleep(3)
    if n == 1:
        a = T.game_pids()
        V.ui('open'); st = {}
        for _ in range(15):
            time.sleep(1); st = V.ui('state')
            if st.get('open'): break
        ctl = lambda st: {c['name']: c for c in st.get('controls', [])}
        mods_of = lambda st: [x[4:] for x in ctl(st) if x.startswith('mod:')]
        c = ctl(st); S['run1_game_tab'] = list(c)
        check('Mods window: two tabs "%s" + "%s"' % (L_GAME, L_LAUNCH), c.get('tab-mods:game', {}).get('text') == L_GAME
              and c.get('tab-mods:launcher', {}).get('text') == L_LAUNCH, {k: v.get('text') for k, v in c.items() if k.startswith('tab')})
        game = mods_of(st)
        check('Game mods tab opens first: game mods only', 'gcd-cdr' in game and not (set(game) & FEAT), game)
        T.shot('1a_game_tab')
        st = V.ui('click', target='tab-mods:launcher'); time.sleep(1); st = V.ui('state')
        feat = mods_of(st); S['run1_launcher_tab'] = list(ctl(st))
        check('Launcher mods tab = Multiplayer + Units Laboratory only', set(feat) == FEAT, feat)
        T.shot('1b_launcher_tab')
        if not LANGTYPE:
            check('stored key lost custom-modes at start', worlds().get(T.GUID) == 'gcd-cdr', worlds())
            V.ui('click', target='mod:custom-modes'); time.sleep(0.5)
            V.ui('click', target='tab-mods:game'); time.sleep(1); st = V.ui('state'); c = ctl(st)
            check('Game tab: no launcher row, the pending change is listed', not (set(mods_of(st)) & FEAT)
                  and 'Units Laboratory on' in c.get('status', {}).get('text', ''), c.get('status'))
            V.ui('click', target='tab-mods:launcher'); time.sleep(1); st = V.ui('state'); c = ctl(st)
            check('back on the Launcher tab: Units Laboratory still on', c.get('mod:custom-modes', {}).get('on') == 1, c.get('mod:custom-modes'))
            T.shot('1c_launcher_pending')
            T.clear_flags(); t_req = time.time(); V.ui('click', target='apply')
            dt, b = T.wait_swap(a[0] if a else 0, t_req, 1)
            T.wait(lambda s: s.get('scene') == 1 and not s.get('busy'), 'B title after Apply', 90); time.sleep(3)
            act = json.load(open(os.path.join(T.MODS, 'active.json')))
            check('Apply: Units Laboratory on after the restart', 'custom-modes' in act.get('enabled', []), act)
            V.ui('open'); st = {}
            for _ in range(15):
                time.sleep(1); st = V.ui('state')
                if st.get('open'): break
            c = ctl(st)
            check('after Apply: Game mods tab first, Units Laboratory on in the Launcher tab', set(mods_of(st)) and not (set(mods_of(st)) & FEAT)
                  and V.ui('click', target='tab-mods:launcher') and (time.sleep(1) or ctl(V.ui('state')).get('mod:custom-modes', {}).get('on') == 1), mods_of(st))
            T.shot('1d_after_apply')
        V.ui('close'); time.sleep(1)
        if LANGTYPE: return
    else:
        r.send(T.X, {'cmd': 'mpm_open_load'}, timeout=20, connect_wait=0)
        check('Load list rows rebuilt', T.wait_log('rows: Load list rebuilt', 20)); time.sleep(1.5)
        mark = len(T.dlog()); m, text = V.tip('row:0', '2_row_card')
        b = V.wait_line(r"reg test: tip row:0 body '([^']*)'", mark, 5)
        body = b.group(1) if b else ''
        S['run2_row'], S['run2_card'] = text, body
        check('row line names only the game mod', text and 'Global cooldown' in text and 'Multiplayer' not in text and 'Laboratory' not in text and '+' not in text.split('Global cooldown')[-1], text)
        check('card: features in their own block', 'Launcher features' in body and 'Multiplayer' in body and 'Units Laboratory' in body
              and body.index('Global cooldown') < body.index('Launcher features'), body)
        T.request({'cmd': 'change', 'row': 0}); st = {}
        for _ in range(15):
            time.sleep(1); st = V.ui('state')
            if st.get('open'): break
        names = [c['name'] for c in st.get('controls', [])]
        S['run2_world_controls'] = names
        check("world's Mods tab: no launcher mod, no tabs", 'mod:gcd-cdr' in names and not any(x[4:] in FEAT for x in names if x.startswith('mod:'))
              and not any(x.startswith('tab-mods:') for x in names), names)
        T.shot('2_world_mods')
        V.ui('close'); time.sleep(1)
    mark = len(T.dlog()); r.send(T.X, {'cmd': 'load_save', 'name': T.SAVE}, timeout=20, connect_wait=0)
    ok = V.wait_line(r'world check: %s \(world [0-9a-f-]+\) uses this mod set: allowed' % T.SAVE, mark, 30)
    check(f'run {n}: the world loads, no "other mods" refusal', ok is not None and 'REFUSED' not in V.since(mark), V.since(mark)[-400:])
    try:
        T.wait(lambda s: s.get('scene') == 2 and not s.get('busy'), f'run {n} in the world', 90)
        check(f'run {n}: in the world', True); T.shot(f'{n}_in_world')
    except Exception as e:
        check(f'run {n}: in the world', False, repr(e))
    quit_all()


def main():
    for p in T.game_pids(): raise SystemExit(f'copy S already running: {p}')
    os.makedirs(T.SDIR, exist_ok=True)
    for f in glob.glob(os.path.join(T.I, 'data', 'Saves', '*')) + glob.glob(os.path.join(T.I, 'mp.json')):
        os.makedirs(os.path.join(T.I, 'old'), exist_ok=True); os.replace(f, os.path.join(T.I, 'old', os.path.basename(f) + '.%d' % time.time()))
    V.install(); T.r.install_lang(T.X)                         # this tree's loader texts (the tab labels)
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    S = {'violations': T.violations, 'checks': T.checks}
    try:
        one_run(1, ('multiplayer', 'gcd-cdr'), 'custom-modes+gcd-cdr', S)
        if not LANGTYPE: one_run(2, ('multiplayer', 'custom-modes', 'gcd-cdr'), 'gcd-cdr', S)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        quit_all()
        json.dump(S, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, summary {OUT}/summary.json")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)
