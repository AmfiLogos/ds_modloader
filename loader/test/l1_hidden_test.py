#!/usr/bin/env python3
"""Worlds on disk that the Load list does not show get a line with how many and why (owner 2026-10-18: "hidden, not
lost"). Test copy DSL_TEST_COPY (default Y; hidden, muted, staged off screen; take a lease first:
~/ds_multiplayer/rig/rig.py lease hiddenworlds --pid <this pid>). Own save folders only, as l1_saves_test.py: the game
folder is <copy>/gameroot/data/Saves, the modded folder <copy>/data/Saves. The owner's saves and prefs: hash before/after.
Real breaks this catches (each: the player thinks worlds are lost, or is told a wrong number):
 M  multiplayer + gcd-cdr, title Load list: the modded-only world tagged Multiplayer is hidden by the multiplayer list
    filter -> bottom line "1 multiplayer world is hidden"; the box ticked -> the world shows, the line goes. A game save
    the game cannot read stays in the game folder (never quarantined) -> top line "1 game save could not be read".
 F  the game-saves hooks off this session (DSL_TEST_SAVES_OFF=1, as names missing / a hook refused) -> top line
    "1 of your game worlds is not listed".
 B  mods off for this game build (launcher pin of another build): dsload_status.json message (the start notice) says the
    game world is not in the Load list (Mac copy: the modded folder is listed, the game saves are hidden). The notice is
    a system alert: never shown in a test (DSL_NO_ALERT), so no screenshot.
 G  crash guard: mods off after a stop during the start -> the same line in the log (the notice text).
M and F run in English and in German (long language): screenshots M_en, M_de, F_en, F_de.
Windows-only cases (unknown build / failed save-folder redirect: the modded worlds are hidden) are covered by the host
test loader/test/saves_hidden/run.sh (the same function, Windows branch).
Usage: l1_hidden_test.py <out dir>   (env DSL_TEST_MODS = built mods, default this tree's loader/build/mods)"""
import json, os, sys, time, shutil, glob
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
os.environ.setdefault('DSL_TEST_COPY', 'Y')
os.environ.setdefault('DSL_TEST_MODS', os.path.join(os.path.dirname(HERE), 'build', 'mods'))
OUT = os.path.abspath(os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/hidden'))
sys.argv[1:] = [OUT]
import l1_saves_test as S
T, r = S.T, S.r
check, log = T.check, T.log
I, X = S.I, S.X
GSAVES, MSAVES, ELVEN, MOON, SEED = S.GSAVES, S.MSAVES, S.ELVEN, S.MOON, S.SEED
MOON_GUID = '41e9c1bb-8790-492c-bdc1-0f4dcad22179'
LANG = {'en': 0, 'de': 3}
_start = S.start_modded
def _capped(*a, **k):
    r.cap_wait(f'{X}: start')                         # the machine-wide test copy cap (6 copies, load, swap)
    return _start(*a, **k)
S.start_modded = _capped


def with_lang(code):
    """r.prepare_data writes UserSetting.json at every start: the language goes in after it"""
    orig = r.prepare_data
    def prep(x):
        orig(x)
        p = os.path.join(r.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
        s['GeneralSettingData']['SaveCurrentLanguageType'] = LANG[code]; json.dump(s, open(p, 'w'), indent=1)
    r.prepare_data = prep


def shot(name):
    """T.shot says ok for an old file of that name: move it aside first, then wait for the new one"""
    p = os.path.join(OUT, name + '.png')
    if os.path.exists(p): os.makedirs(os.path.join(OUT, 'old'), exist_ok=True); os.replace(p, os.path.join(OUT, 'old', f'{name}.{time.time():.0f}.png'))
    T.request({'cmd': 'shot', 'path': p}); t0 = time.time()
    while time.time() - t0 < 30 and not os.path.exists(p): time.sleep(0.2)
    time.sleep(0.5); log('shot', name, 'ok' if os.path.exists(p) else 'MISSING')
    return os.path.exists(p)


def install():
    """l1_saves_test.install + the loader's language files next to it (as the app ships them: <loader>/lang)"""
    S.install()
    lang = os.path.join(I, 'DungeonSettlers.app', 'Contents', 'Frameworks', 'lang'); os.makedirs(lang, exist_ok=True)
    for f in glob.glob(os.path.join(os.path.dirname(HERE), 'lang', '*.json')): shutil.copy2(f, lang)


def folders():
    old = os.path.join(I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True)
    for d in (MSAVES, S.GROOT, os.path.join(I, 'data', 'SaveHeaderCache.json'), os.path.join(T.MODS, 'save_copies'),
              os.path.join(I, 'mp_tags.txt'), os.path.join(I, 'dsload_state', '_codepatch.disabled')):
        if os.path.exists(d): shutil.move(d, os.path.join(old, os.path.basename(d)))
    os.makedirs(GSAVES); os.makedirs(MSAVES)
    shutil.copy2(os.path.join(SEED, ELVEN + '.json'), GSAVES)
    shutil.copy2(os.path.join(SEED, MOON + '.json'), MSAVES)
    open(os.path.join(GSAVES, 'Broken save.json'), 'w').write('{"CampaignSaveHeader": {"CampaignGuid": ')   # cut short
    open(os.path.join(I, 'mp_tags.txt'), 'w').write(f'{MOON_GUID}\tHost\tGuest\n')


def lines_log(mark):
    return [l for l in T.dlog()[mark:].splitlines() if 'hidden-worlds lines' in l or 'saves: off' in l or 'could not read' in l]


def open_list():
    mark = len(T.dlog())
    r.send(X, {'cmd': 'mpm_open_load'}, timeout=20, connect_wait=0)
    S.wait_line('rows: Load list rebuilt', mark, 30); time.sleep(2.5)
    return mark


def run_m(code):
    with_lang(code); folders()
    S.start_modded(('multiplayer', 'gcd-cdr'), worlds={})
    mark = open_list()
    L = T.dlog()[mark:]
    check(f'M {code}: the multiplayer world is hidden by the list filter, the line says 1 multiplayer world',
          'bottom 1 (kind 1, 1 by other filters' in L, lines_log(mark))
    check(f'M {code}: the unreadable game save stays in the game folder and the top line says 1',
          os.path.exists(os.path.join(GSAVES, 'Broken save.json')) and 'unreadable 1)' in L, lines_log(0)[-4:])
    check(f'M {code}: screenshot', shot(f'M_{code}'))
    mark = len(T.dlog())
    st = r.send(X, {'cmd': 'mpm_list', 'flip': 1}, timeout=20, connect_wait=0); time.sleep(2.5)
    L = T.dlog()[mark:]
    check(f'M {code}: box ticked -> the world shows, the bottom line goes', 'bottom 0 (kind 0, 0 by other filters' in L, (st, lines_log(mark)))
    check(f'M {code}: screenshot (ticked)', shot(f'M_{code}_ticked'))
    S.stop_all()


def run_f(code):
    with_lang(code); folders()
    S.start_modded(('multiplayer', 'gcd-cdr'), worlds={}, extra_env={'DSL_TEST_SAVES_OFF': '1'})
    check(f'F {code}: hooks off, 1 game world counted', 'saves: off: 1 game world(s)' in T.dlog(), [l for l in T.dlog().splitlines() if 'saves:' in l][:3])
    mark = open_list()
    check(f'F {code}: the top line says 1 game world is not listed', 'top 1 (game worlds not listed 1, unreadable 0)' in T.dlog()[mark:], lines_log(mark))
    check(f'F {code}: screenshot', shot(f'F_{code}'))
    S.stop_all()


def wait_file(p, limit=60):
    t0 = time.time()
    while time.time() - t0 < limit and not os.path.exists(p): time.sleep(0.5)
    return os.path.exists(p)


def run_b():
    with_lang('en'); folders()
    st = os.path.join(I, 'dsload_status.json')
    if os.path.exists(st): os.replace(st, st + '.old')
    S.start_modded(('multiplayer', 'gcd-cdr'), worlds={}, extra_env={'DSML_BUILD': '11111111'})
    ok = wait_file(st); time.sleep(1)
    j = json.load(open(st)) if ok else {}
    check('B: mods off (other build): the start notice says the game world is hidden, not lost',
          j.get('active') is False and 'build 11111111' in j.get('message', '') and
          '1 of your game worlds is not in the Load list this session. It is not lost' in j.get('message', ''), j.get('message'))
    time.sleep(5); S.stop_all()


def run_g():
    with_lang('en'); folders()
    os.makedirs(os.path.join(I, 'dsload_state'), exist_ok=True)
    open(os.path.join(I, 'dsload_state', '_codepatch.pending'), 'w').write('test: a stop during the start')
    S.start_modded(('multiplayer', 'gcd-cdr'), worlds={}, extra_env={'DSML_BUILD': '25846389'})   # no title wait: mods are off
    ok = T.wait_log('crash guard: The game stopped', 60)
    check('G: crash guard off: the notice text names the hidden game world', ok and '1 of your game worlds is not in the Load list' in T.dlog(),
          [l for l in T.dlog().splitlines() if 'crash guard' in l.lower()][-2:])
    time.sleep(3); S.stop_all()
    d = os.path.join(I, 'dsload_state', '_codepatch.disabled')
    if os.path.exists(d): os.replace(d, os.path.join(I, 'old', '_codepatch.disabled.%d' % time.time()))   # the guard back to on


def main():
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    real0 = S.tree_sum(r.REAL_DATA); plist0 = r.file_sha(r.REAL_PLIST)
    os.makedirs(T.SDIR, exist_ok=True)
    install()
    import threading
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    which = os.environ.get('DSL_HIDDEN_RUNS', 'M_en,M_de,F_en,F_de,B,G').split(',')
    try:
        for w in which:
            mark = len(T.dlog())
            {'M_en': lambda: run_m('en'), 'M_de': lambda: run_m('de'), 'F_en': lambda: run_f('en'), 'F_de': lambda: run_f('de'),
             'B': run_b, 'G': run_g}[w]()
            for f in ('dsload.log', 'dsload_status.json'):
                if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(OUT, f'{w}_{f}'))
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        check('never frontmost, never a visible window (guard)', not T.violations, T.violations)
        T.stop_guard.set()
        S.stop_all()
        check("owner's real save folder unchanged (hash before/after)", S.tree_sum(r.REAL_DATA) == real0)
        check("owner's prefs plist unchanged by this run (hash before/after)", r.file_sha(r.REAL_PLIST) == plist0)
        json.dump({'checks': T.checks, 'violations': T.violations}, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        log(f"{sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, out {OUT}")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)
