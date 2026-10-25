#!/usr/bin/env python3
"""The Load Campaign filter panel (owner 2026-10-24) on a hidden test copy (DSL_TEST_COPY, default V; one copy at a
time; lease first: ~/ds_multiplayer/rig/rig.py lease dsfilter --pid <this pid>). Own save folders only, as
l1_saves_test.py; the owner's saves and prefs are hashed before/after.
The real break this guards (data loss): a save the player selected, then hid with a filter or with "Latest save only",
must not stay selected, so Remove File / Load never act on a save the player cannot see. On a loader without the guard
the stock Remove File + its two boxes answered Yes delete the hidden game save: this test fails there.
Player-visible faults checked in the same runs: the panel is outside the Load window on its right, inside the screen
and off the title corner (Auto-update mod loader), at 16:9 and 16:10 and UI scale 0.75 / 1.0 / 1.4; each world in its
kind; the multiplayer mod's box under the list is gone; Latest save only shows the newest save of a world; the choices
come back after a restart.
Worlds: Elven (game save, gcd-cdr + zetu-suite, installed and off = Modded; Hell would not count: Elven is not on Hell), Template (the wiki's public base save, only the fix gcd-cdr =
Vanilla & Vanilla+: a fix never counts as Modded, owner 2026-10-24; two saves),
Moonlight (tagged Multiplayer).
Usage: l1_filter_test.py <out dir>   (env DSL_TEST_MODS = built mods, default this tree's loader/build/mods;
LF_RUNS = comma list of runs, default main,restart,sizes; langs = every language's rows, one shot each)"""
import json, os, sys, time, shutil, glob, threading, subprocess
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
os.environ.setdefault('DSL_TEST_COPY', 'V')
os.environ.setdefault('DSL_TEST_MODS', os.path.join(os.path.dirname(HERE), 'build', 'mods'))
OUT = os.path.abspath(os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/ds_launcher/loader/test_env/l1_out/filter'))
sys.argv[1:] = [OUT]
import l1_saves_test as S
T, r = S.T, S.r
check, log = T.check, T.log
I, X = S.I, S.X
GSAVES, MSAVES, ELVEN, MOON, SEED = S.GSAVES, S.MSAVES, S.ELVEN, S.MOON, S.SEED
ELVEN_GUID, MOON_GUID = S.GUID, '41e9c1bb-8790-492c-bdc1-0f4dcad22179'
TPL_SRC = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # public (the wiki's base save)
TPL_GUID = '491bc260-98aa-4cd1-b63f-82da78369aaf'
TPL_NEW, TPL_OLD = 'Template__AutoSave_491bc26098aa4cd1b63f82da78369aaf_0', 'Template older'
LANG = {'en': 0, 'ko': 1, 'fr': 2, 'de': 3, 'ru': 4, 'zh-Hans': 5, 'zh-Hant': 6, 'ja': 7, 'es': 8, 'pt-BR': 9}
STATE = os.path.join(T.SDIR, 'lf_state.json')
GFILE = os.path.join(GSAVES, ELVEN + '.json')


def install():
    """l1_saves_test.install + the loader's language files next to it (as the app ships them: <loader>/lang). codesign
    refuses a bundle whose Frameworks holds that folder (not code): it is put aside while the executable is signed (a
    copy whose Info.plist changed after its last signature is killed at start: Code Signature Invalid)"""
    app = os.path.join(I, 'DungeonSettlers.app'); lang = os.path.join(app, 'Contents', 'Frameworks', 'lang'); aside = os.path.join(app, 'Contents', 'lang_aside')
    if os.path.isdir(lang): os.replace(lang, aside)
    try:
        S.install()
    finally:
        if os.path.isdir(aside): os.replace(aside, lang)
    os.makedirs(lang, exist_ok=True)
    for f in glob.glob(os.path.join(os.path.dirname(HERE), 'lang', '*.json')): shutil.copy2(f, lang)
    v = subprocess.run(['codesign', '-v', os.path.join(app, 'Contents', 'MacOS', 'Dungeon Settlers')], capture_output=True, text=True).stderr
    check('install: the copy\'s executable signature is valid (no "invalid Info.plist")', 'invalid Info.plist' not in v and 'not signed' not in v, v.strip())


def seed():
    old = os.path.join(I, 'old', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(old, exist_ok=True)
    for d in (MSAVES, S.GROOT, os.path.join(I, 'data', 'SaveHeaderCache.json'), os.path.join(T.MODS, 'save_copies'),
              os.path.join(I, 'mp_tags.txt'), os.path.join(T.MODS, 'loadlist.json')):
        if os.path.exists(d): shutil.move(d, os.path.join(old, os.path.basename(d)))
    os.makedirs(GSAVES); os.makedirs(MSAVES)
    shutil.copy2(os.path.join(SEED, ELVEN + '.json'), GSAVES)
    shutil.copy2(os.path.join(SEED, MOON + '.json'), MSAVES)
    t = json.load(open(TPL_SRC))
    json.dump(t, open(os.path.join(MSAVES, TPL_NEW + '.json'), 'w'))
    h = t['CampaignSaveHeader']; h.update(SaveName=TPL_OLD, DisplayName=TPL_OLD, SaveTime='2026-09-01 08:00:00', IsAutoSave=False)
    json.dump(t, open(os.path.join(MSAVES, TPL_OLD + '.json'), 'w'))
    open(os.path.join(I, 'mp_tags.txt'), 'w').write(f'{MOON_GUID}\tHost\tGuest\n')


WORLDS = {ELVEN_GUID: 'gcd-cdr+zetu-suite', TPL_GUID: 'gcd-cdr'}
FAKEROOT = os.path.join(I, 'lf_root')


def fake_root():
    """a ROOT (DSML_ROOT) so the title corner shows as a player sees it: Auto-update switch, the version line and the
    starter's one-line notice (the tallest corner without a download); DSL_UPDATE=0: no check, nothing downloaded"""
    os.makedirs(os.path.join(FAKEROOT, 'loader'), exist_ok=True); os.makedirs(os.path.join(FAKEROOT, 'update'), exist_ok=True)
    open(os.path.join(FAKEROOT, 'loader', 'RELEASE'), 'w').write('0.1.23\n')
    open(os.path.join(FAKEROOT, 'update', 'notice.txt'), 'w').write('The last update could not be installed. The game stays on 0.1.22.\n')
    return {'DSML_ROOT': FAKEROOT, 'DSL_UPDATE': '0'}


def start(size=(960, 540), scale=None, lang='en'):
    """S.start_modded at another window size / UI scale / language (UserSetting.json is written at every start)"""
    os.environ['LRIG_SIZE'] = f'{size[0]}x{size[1]}'
    orig = r.prepare_data
    def prep(x):
        orig(x)
        p = os.path.join(r.idir(x), 'data', 'UserSetting.json'); s = json.load(open(p))
        s['GeneralSettingData']['SaveCurrentLanguageType'] = LANG[lang]
        if scale is not None: s['GraphicSettingData']['UIScale'] = scale
        json.dump(s, open(p, 'w'), indent=1)
    r.prepare_data = prep
    r.cap_wait(f'{X}: start')                                        # the machine-wide test copy cap (copies, load, swap)
    try:
        S.start_modded(('multiplayer', 'gcd-cdr'), worlds=WORLDS, extra_env=fake_root())
    finally:
        r.prepare_data = orig


def mp(cmd, **kw):
    return r.send(X, dict(cmd=cmd, **kw), timeout=20, connect_wait=0)


def lf(cmd='lf_state', **kw):
    """a panel request (dsl_rows.c test cmd); returns the state file it writes"""
    if os.path.exists(STATE): os.replace(STATE, STATE + '.old')
    T.request(dict(cmd=cmd, **kw)); t0 = time.time()
    while time.time() - t0 < 10 and not os.path.exists(STATE): time.sleep(0.2)
    try: return json.load(open(STATE))
    except Exception as e: return {'error': repr(e)}


def world(st, guid):
    return next((w for w in st.get('worlds', []) if w['guid'] == guid), {})


def open_list():
    mark = len(T.dlog())
    mp('mpm_open_load')
    S.wait_line('rows: Load list rebuilt', mark, 30); time.sleep(2.5)
    return mark


def shot(name):
    p = os.path.join(OUT, name + '.png')
    if os.path.exists(p): os.makedirs(os.path.join(OUT, 'old'), exist_ok=True); os.replace(p, os.path.join(OUT, 'old', f'{name}.{time.time():.0f}.png'))
    T.request({'cmd': 'shot', 'path': p}); t0 = time.time()
    while time.time() - t0 < 30 and not os.path.exists(p): time.sleep(0.2)
    time.sleep(0.5); return os.path.exists(p)


def overlap(a, b):
    return a[0] < b[2] and a[2] > b[0] and a[1] < b[3] and a[3] > b[1]


def place_checks(tag, st):
    """the panel shows outside the Load window on its right, inside the screen, never over the title corner"""
    p, w, c, (sw, sh) = st.get('panel', [0] * 4), st.get('window', [0] * 4), st.get('corner', [0] * 4), st.get('screen', [0, 0])
    check(f'{tag}: the panel shows', st.get('panel_on') == 1 and st.get('panel_shown') == 1, st)
    check(f'{tag}: the panel is right of the Load window', p[0] >= w[2] - 0.5, (p, w))
    check(f'{tag}: the panel is inside the screen', p[0] >= 0 and p[1] >= -0.5 and p[2] <= sw + 0.5 and p[3] <= sh + 0.5, (p, sw, sh))
    if st.get('scene_title', 1):
        check(f'{tag}: the title corner shows (Auto-update, version, a notice line)', st.get('corner_shown') == 1, c)
    check(f'{tag}: the panel does not cover the title corner (Auto-update)', not st.get('corner_shown') or not overlap(p, c), (p, c))
    check(f'{tag}: the panel is not scaled below 0.6', st.get('scale', 0) >= 0.6, st.get('scale'))


def row_checks(tag, st):
    """each shown world row's "Mods:" line (owner 2026-10-24): its text starts after the name + multiplayer tag + "Remove tag"
    (it ran over "with <names>"), and it is one line (a Russian name took 3 lines and ran out of the row)"""
    rows = [w for w in st.get('worlds', []) if w.get('shown') and w.get('line_shown')]
    check(f'{tag}: world rows with a Mods line measured', rows, st.get('worlds'))
    for w in rows:
        lr, (rw, rh) = w['line_rect'], w['rendered']
        if not w.get('line_text'): continue                          # no room: the line is empty (the card has it)
        check(f'{tag}: {w["name"]}: the Mods line starts after the name / tag', lr[2] - rw >= w['left_end'] - 1.5,
              (round(lr[2] - rw, 1), w['left_end'], w['line_text'][:80]))
        check(f'{tag}: {w["name"]}: the Mods line is one line', rh <= (lr[3] - lr[1]) * 1.35, (rh, lr[3] - lr[1], w['line_text'][:80]))


def run_main():
    seed(); start()
    T.request({'cmd': 'warn_cancel'}); time.sleep(1)
    open_list()
    st = lf()
    log('state', json.dumps(st)[:600])
    place_checks('main 960x540', st)
    b = st.get('boxes', {})
    check('defaults = the list as before: Vanilla & Vanilla+ on, Modded on, Multiplayer off, all saves', (b.get('vanilla'), b.get('modded'), b.get('multiplayer'), b.get('latest_only'), b.get('mp_box')) == (1, 1, 0, 0, 1), b)
    e, t, m = world(st, ELVEN_GUID), world(st, TPL_GUID), world(st, MOON_GUID)
    check('kinds: Elven (gcd-cdr + zetu-suite) Modded, Template (only the fix gcd-cdr) Vanilla, Moonlight (tag) Multiplayer', (e.get('kind'), t.get('kind'), m.get('kind')) == (1, 0, 2), (e, t, m))
    check('Multiplayer off: Moonlight hidden, the others shown; the line counts 1', (e.get('shown'), t.get('shown'), m.get('shown'), st.get('hidden_line')) == (1, 1, 0, 1), st)
    ml = mp('mpm_list')
    check("the multiplayer mod's box under the list is gone and it hides nothing (the panel decides)", ml.get('open') and ml.get('toggle') == 0 and ml.get('hidden') == 0,
          {k: ml.get(k) for k in ('open', 'mode', 'toggle', 'hidden', 'shown')})
    row_checks('main 960x540', st)
    shot('main_defaults')
    # ---------------------------------------------------------------- the data guard: filter
    lf('lf_select', name=ELVEN); time.sleep(1); st = lf()            # the click reaches the game's presenter a frame later
    check('guard: the Elven save is selected, Remove File clickable', st.get('selected') == ELVEN and st.get('remove_clickable') == 1, st)
    st = lf('lf', modded=0)
    check('guard: Modded off hides Elven and unselects its save; Load and Remove File locked', world(st, ELVEN_GUID).get('shown') == 0 and st.get('selected') == ''
          and st.get('remove_clickable') == 0 and st.get('load_clickable') == 0 and st.get('guard', 0) >= 1, st)
    mark = len(T.dlog())
    lf('lf_remove'); time.sleep(2)
    for _ in range(2):                                              # a player who answers every box Yes
        b = mp('mpm_box', action='confirm'); log('box', b); time.sleep(2.5)
    check('guard: Remove File then Yes to every box: the hidden game save is still there', os.path.exists(GFILE),
          [l for l in T.dlog()[mark:].splitlines() if 'saves' in l or 'Remove File' in l][-6:])
    # ---------------------------------------------------------------- Latest save only (+ the guard)
    st = lf('lf', modded=1, multiplayer=1)
    check('Multiplayer on: Moonlight shows; no world hidden', world(st, MOON_GUID).get('shown') == 1 and st.get('hidden_line') == 0, st)
    lf('lf_select', name=TPL_OLD); time.sleep(1); st = lf()
    check('guard: the older Template save is selected', st.get('selected') == TPL_OLD, st)
    g0 = st.get('guard', 0)
    st = lf('lf', latest_only=1); t = world(st, TPL_GUID)
    check('Latest save only: Template shows its newest save only, the older one hidden', t.get('saves') == 2 and t.get('shown_names') == [TPL_NEW], t)
    check('Latest save only: the world row counts one save row (no empty band)', t.get('button_count') == 1, t)
    check('guard: the older save was unselected when Latest save only hid it; Remove File locked', st.get('selected') == '' and st.get('guard', 0) > g0
          and st.get('remove_clickable') == 0, st)
    lf('lf', multiplayer=0); time.sleep(0.5); shot('main_latest'); lf('lf', multiplayer=1)   # Template on screen (no empty band under its save)
    mark = len(T.dlog())
    lf('lf_remove'); time.sleep(2)
    for _ in range(2):
        b = mp('mpm_box', action='confirm'); log('box', b); time.sleep(2.5)
    check('guard: Remove File then Yes: the hidden older save is still there', os.path.exists(os.path.join(MSAVES, TPL_OLD + '.json')),
          [l for l in T.dlog()[mark:].splitlines() if 'saves' in l or 'Remove File' in l][-6:])
    st = lf('lf', latest_only=0); t = world(st, TPL_GUID)
    check('Latest save only off: both Template saves show again, the row counts 2', t.get('saves_shown') == 2 and t.get('button_count') == 2, t)
    lf('lf', latest_only=1)                                         # kept on for the restart run
    S.stop_all()


def run_restart():
    """the choices of run_main (Modded on, Multiplayer on, Latest save only on) come back after a restart"""
    start(); T.request({'cmd': 'warn_cancel'}); time.sleep(1)
    open_list(); st = lf(); b = st.get('boxes', {})
    check('restart: the choices came back (all kinds on, Latest save only on)', (b.get('vanilla'), b.get('modded'), b.get('multiplayer'), b.get('latest_only')) == (1, 1, 1, 1),
          (b, open(os.path.join(T.MODS, 'loadlist.json')).read() if os.path.exists(os.path.join(T.MODS, 'loadlist.json')) else 'no file'))
    check('restart: Latest save only applied at the opening', world(st, TPL_GUID).get('shown_names') == [TPL_NEW], world(st, TPL_GUID))
    S.stop_all()


def run_sizes():
    """16:9 and 16:10 windows, UI scale 0.75 / 1.0 / 1.4 (the game's range), en / de / ru / ja: the panel's place"""
    for size, scale, lang in (((960, 540), 0.75, 'en'), ((960, 540), 1.4, 'de'), ((960, 600), 1.0, 'ru'), ((960, 600), 1.4, 'ja')):
        tag = f'{size[0]}x{size[1]} UI {scale} {lang}'
        start(size, scale, lang); T.request({'cmd': 'warn_cancel'}); time.sleep(1)
        open_list(); st = lf('lf', multiplayer=1); time.sleep(1); st = lf()   # the multiplayer world's row too (its tag + names)
        log(tag, 'screen', st.get('screen'), 'window', st.get('window'), 'panel', st.get('panel'), 'scale', st.get('scale'), 'corner', st.get('corner'))
        place_checks(tag, st)
        row_checks(tag, st)
        shot(f'size_{size[0]}x{size[1]}_ui{scale}_{lang}')
        S.stop_all()


def run_langs():
    """every language at 960x540, UI 1.0, the multiplayer world shown: the rows' Mods lines (one shot each)"""
    for lang in [l for l in LANG if l in os.environ.get('LF_LANGS', ','.join(LANG)).split(',')]:
        start((960, 540), 1.0, lang); T.request({'cmd': 'warn_cancel'}); time.sleep(1)
        open_list(); lf('lf', multiplayer=1); time.sleep(1); st = lf()
        row_checks(f'rows {lang}', st)
        shot(f'rows_{lang}')
        S.stop_all()


def main():
    for p in T.game_pids(): raise SystemExit(f'copy {X} already running: {p}')
    real0 = S.tree_sum(r.REAL_DATA); plist0 = r.file_sha(r.REAL_PLIST)
    os.makedirs(T.SDIR, exist_ok=True)
    install()
    g = threading.Thread(target=T.guard, daemon=True); g.start()
    which = os.environ.get('LF_RUNS', 'main,restart,sizes').split(',')
    try:
        for w in which:
            {'main': run_main, 'restart': run_restart, 'sizes': run_sizes, 'langs': run_langs}[w]()
            for f in ('dsload.log',):
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
        log(f"RESULT {sum(c['ok'] for c in T.checks)}/{len(T.checks)} checks, violations {len(T.violations)}, out {OUT}")


if __name__ == '__main__':
    main()
    sys.exit(1 if not all(c['ok'] for c in T.checks) or T.violations or not T.checks else 0)
