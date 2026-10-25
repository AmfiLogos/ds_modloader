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
Moonlight (tagged Multiplayer), Slot (the base save as another world: gcd-cdr + weapon-slot, installed and off =
Vanilla & Vanilla+: weapon-slot's mod.json says "vanilla_plus", owner 2026-10-25).
Usage: l1_filter_test.py <out dir>   (env DSL_TEST_MODS = built mods, default this tree's loader/build/mods;
LF_RUNS = comma list of runs, default main,restart,sizes; langs = every language's rows, one shot each)"""
import json, os, re, sys, time, shutil, glob, threading, subprocess
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
SLOT_GUID = '5107aa11-0000-4c1e-9a7e-000000000001'                   # the base save as another world: gcd-cdr + weapon-slot
SLOT = 'Slot__AutoSave_5107aa1100004c1e9a7e000000000001_0'
LANG = {'en': 0, 'ko': 1, 'fr': 2, 'de': 3, 'ru': 4, 'zh-Hans': 5, 'zh-Hant': 6, 'ja': 7, 'es': 8, 'pt-BR': 9}
STATE = os.path.join(T.SDIR, 'lf_state.json')
GFILE = os.path.join(GSAVES, ELVEN + '.json')


def install():
    """l1_saves_test.install + the loader's language files next to it (as the app ships them: <loader>/lang). codesign
    refuses a bundle whose Frameworks holds that folder (not code): it is put aside while the executable is signed (a
    copy whose Info.plist changed after its last signature is killed at start: Code Signature Invalid)"""
    app = os.path.join(I, 'DungeonSettlers.app'); lang = os.path.join(app, 'Contents', 'Frameworks', 'lang'); aside = os.path.join(app, 'Contents', 'lang_aside')
    if os.path.isdir(lang): os.replace(lang, aside)
    md = os.path.join(app, 'Contents', 'Resources', 'mods', 'weapon-slot')   # installed, not on (as hell-difficulty): its mod.json flag
    for f in glob.glob(os.path.join(os.environ['DSL_TEST_MODS'], 'weapon-slot', '*')):
        if os.path.isfile(f): os.makedirs(md, exist_ok=True); shutil.copy2(f, md)
    shutil.copy2(os.path.join(os.path.dirname(HERE), 'modules', 'weapon-slot', 'mod.json'), os.path.join(md, 'mod.json'))   # this tree's
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
    h.update(CampaignGuid=SLOT_GUID, ClanName='Slot', SaveName=SLOT, DisplayName='Slot', SaveTime='2026-10-20 08:00:00', IsAutoSave=True)   # the newest world (before the game save Elven); Template (2 saves) the last
    json.dump(t, open(os.path.join(MSAVES, SLOT + '.json'), 'w'))
    open(os.path.join(I, 'mp_tags.txt'), 'w').write(f'{MOON_GUID}\tAmfi\tFoctopus\tBartholomew\n')   # long: the tag's "with" part must give way


WORLDS = {ELVEN_GUID: 'gcd-cdr+zetu-suite', TPL_GUID: 'gcd-cdr', SLOT_GUID: 'gcd-cdr+weapon-slot'}
FAKEROOT = os.path.join(I, 'lf_root')


def fake_root():
    """a ROOT (DSML_ROOT) so the title corner shows as a player sees it: Auto-update switch, the version line and the
    starter's one-line notice (the tallest corner without a download); DSL_UPDATE=0: no check, nothing downloaded"""
    os.makedirs(os.path.join(FAKEROOT, 'loader'), exist_ok=True); os.makedirs(os.path.join(FAKEROOT, 'update'), exist_ok=True)
    open(os.path.join(FAKEROOT, 'loader', 'RELEASE'), 'w').write('0.1.23\n')
    open(os.path.join(FAKEROOT, 'update', 'notice.txt'), 'w').write('The last update could not be installed. The game stays on 0.1.22.\n')
    cr = os.path.join(I, 'crashrep_test'); os.makedirs(cr, exist_ok=True); open(os.path.join(cr, 'answer'), 'w').write('no')   # a fresh copy's consent box: No
    return {'DSML_ROOT': FAKEROOT, 'DSL_UPDATE': '0', 'DSL_CRASHREP_TEST': cr, 'DSL_ROWS_OPEN_TEST': '1'}   # the open frames are logged


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


NEWEST = {ELVEN_GUID: '2026-10-11 18:25:31', MOON_GUID: '2026-10-04 19:56:07', TPL_GUID: '2026-09-04 09:59:06', SLOT_GUID: '2026-10-20 08:00:00'}
MOD_MARK, MP_MARK = '<color=#E3A65B>', '<color=#8FC4FF>'                # the loader's "Modded" tag, the multiplayer mod's tag


def open_checks(mark):
    """owner 2026-10-25: on the first open after a start the whole list showed for a moment, then the filters hid worlds.
    Every frame from the opening on (logged right before it is drawn, DSL_ROWS_OPEN_TEST) shows no world the filters hide."""
    t0 = time.time()
    while time.time() - t0 < 10 and T.dlog()[mark:].count('rows test: open frame') < 30: time.sleep(0.3)
    fr = re.findall(r'rows test: open frame (\d+): (\d+) world row\(s\) drawn, (\d+) of them hidden by the filters', T.dlog()[mark:])
    bad = [f for f in fr if int(f[2]) > 0]
    check('first open after the start: 30 frames logged from the opening', len(fr) >= 30, len(fr))
    check('first open after the start: no frame draws a world the filters hide (no flash)', fr and not bad, bad[:5])


def order_checks(tag, st):
    """owner 2026-10-25: one list in the stock order (worlds by their newest save, newest first), no group headings"""
    ws = sorted([w for w in st.get('worlds', []) if w.get('shown') and w['guid'] in NEWEST], key=lambda w: -w['row_rect'][3])
    times = [NEWEST[w['guid']] for w in ws]
    check(f'{tag}: one list, newest world first (the stock order; a modded-only world above an older game save)', times == sorted(times, reverse=True) and len(ws) >= 3,
          [(w['name'], NEWEST[w['guid']]) for w in ws])


def tag_checks(tag, st, on):
    """owner 2026-10-25 "Show tags": on = "Modded" on a Modded world (not on a fix-only or Vanilla+ world), the multiplayer
    tag with its Remove tag on a multiplayer world; off = no tag, no Remove tag"""
    w = {g: world(st, g) for g in (ELVEN_GUID, TPL_GUID, SLOT_GUID, MOON_GUID)}
    if on:
        check(f'{tag}: Elven (Modded) shows the Modded tag', MOD_MARK in w[ELVEN_GUID].get('name_text', ''), w[ELVEN_GUID].get('name_text'))
        check(f'{tag}: Template (only a fix) and Slot (Vanilla+) show no tag', not any(m in w[g].get('name_text', '') for g in (TPL_GUID, SLOT_GUID) for m in (MOD_MARK, MP_MARK)),
              [w[g].get('name_text') for g in (TPL_GUID, SLOT_GUID)])
        if w[MOON_GUID].get('shown'):
            check(f'{tag}: Moonlight shows the multiplayer tag and its Remove tag', MP_MARK in w[MOON_GUID].get('name_text', '') and w[MOON_GUID].get('remove_tag') == 1, w[MOON_GUID])
    else:
        shown = [x for x in w.values() if x.get('shown')]
        check(f'{tag}: Show tags off: no tag on any row, no Remove tag', shown and not any(MOD_MARK in x.get('name_text', '') or MP_MARK in x.get('name_text', '') or x.get('remove_tag') for x in shown),
              [(x.get('name'), x.get('name_text'), x.get('remove_tag')) for x in shown])


def rm_checks(tag, st):
    """owner (next version after v0.1.27): "Remove tag" sat under [Change], over the world's detail box. It must be on the
    world's title line (the band 1..21 units under the row's top), overlap neither the "Mods:" line nor [Change]"""
    w = world(st, MOON_GUID)
    if not (w.get('shown') and w.get('remove_tag')): check(f'{tag}: Remove tag shows on the multiplayer world', False, w); return
    r, rr, px = w['rm_rect'], w['row_rect'], w.get('px') or 1
    top, bot = rr[3] - 0.5 * px, rr[3] - 22 * px
    check(f'{tag}: Remove tag is on the title line (not over the detail box)', r[1] >= bot - 1 and r[3] <= top + 1, (r, round(bot, 1), round(top, 1)))
    hit = [n for n, q in (('Mods line', w['line_rect'] if w.get('line_shown') and w.get('line_text') else None), ('Change', w.get('chg_rect'))) if q and q[2] > q[0] and overlap(r, q)]
    check(f'{tag}: Remove tag overlaps neither the Mods line nor [Change]', not hit, (r, hit, w.get('line_rect'), w.get('chg_rect')))


def run_main():
    seed(); start()
    T.request({'cmd': 'warn_cancel'}); time.sleep(1)
    m0 = open_list()
    open_checks(m0)
    st = lf()
    log('state', json.dumps(st)[:600])
    place_checks('main 960x540', st)
    b = st.get('boxes', {})
    check('defaults = the list as before: Vanilla & Vanilla+ on, Modded on, Multiplayer off, all saves, Show tags on', (b.get('vanilla'), b.get('modded'), b.get('multiplayer'), b.get('latest_only'), b.get('show_tags'), b.get('mp_box')) == (1, 1, 0, 0, 1, 1), b)
    e, t, m = world(st, ELVEN_GUID), world(st, TPL_GUID), world(st, MOON_GUID)
    check('kinds: Elven (gcd-cdr + zetu-suite) Modded, Template (only the fix gcd-cdr) Vanilla, Moonlight (tag) Multiplayer', (e.get('kind'), t.get('kind'), m.get('kind')) == (1, 0, 2), (e, t, m))
    sl = world(st, SLOT_GUID)
    check('kinds: Slot (gcd-cdr + weapon-slot, "vanilla_plus") is Vanilla & Vanilla+ and shows', (sl.get('kind'), sl.get('shown')) == (0, 1), sl)
    check('Multiplayer off: Moonlight hidden, the others shown; the line counts 1', (e.get('shown'), t.get('shown'), m.get('shown'), st.get('hidden_line')) == (1, 1, 0, 1), st)
    ml = mp('mpm_list')
    check("the multiplayer mod's box under the list is gone and it hides nothing (the panel decides)", ml.get('open') and ml.get('toggle') == 0 and ml.get('hidden') == 0,
          {k: ml.get(k) for k in ('open', 'mode', 'toggle', 'hidden', 'shown')})
    row_checks('main 960x540', st)
    order_checks('main', st)
    tag_checks('main, Show tags on (default)', st, True)
    shot('main_defaults')
    st = lf('lf', multiplayer=1); time.sleep(0.5); st = lf()
    tag_checks('main, Multiplayer box on', st, True); rm_checks('main en', st); shot('main_tags_on')
    st = lf('lf', show_tags=0); time.sleep(0.5); st = lf()
    tag_checks('main, Show tags off', st, False); line_checks('main, Show tags off', st); shot('main_tags_off')
    st = lf('lf', show_tags=1); time.sleep(0.5); st = lf()
    tag_checks('main, Show tags on again', st, True)
    st = lf('lf', vanilla=0, modded=0, multiplayer=0); time.sleep(0.5); st = lf()     # owner 2026-10-25: all off = every world
    check('all three kind boxes off: every world shows (not an empty list), the line says nothing is hidden',
          all(world(st, g).get('shown') for g in NEWEST) and not (st.get('hid_lines') or [{}, {}])[1].get('shown') and st.get('hidden_line') == 0,
          ([(w['name'], w.get('shown')) for w in st.get('worlds', [])], (st.get('hid_lines') or [{}, {}])[1]))
    lf('lf', vanilla=1, modded=1, multiplayer=0); time.sleep(0.5)
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


def line_checks(tag, st):
    """owner 2026-10-25: the hidden-worlds line under the list ("3 worlds are hidden by the filters on the right.") was drawn
    over the last save row: it must sit below every shown world row and save row"""
    ws = [w for w in st.get('worlds', []) if w.get('shown')]
    out = [(w['name'], r) for w in ws for r in w.get('save_rects', []) if r[1] < w['row_rect'][1] - 0.5 or r[3] > w['row_rect'][3] + 0.5]
    check(f'{tag}: every shown save row is inside its world row (the world row is as tall as its save rows)', not out, out[:3])
    ln = (st.get('hid_lines') or [{}, {}])[1]
    if not ln.get('shown'): return
    q = ln['rect']; rows = [w['row_rect'] for w in ws] + [r for w in ws for r in w.get('save_rects', [])]
    bad = [r for r in rows if overlap(q, r) or r[1] < q[3] - 0.5]
    check(f'{tag}: the hidden-worlds line is below every shown row (no overlap)', not bad, (q, bad[:3], ln.get('text', '')[:60]))


def run_layout():
    """the hidden-worlds line after filter / Latest save only changes (owner 2026-10-25: over the last save row after
    Latest save only went off while a world with 2 saves was hidden)"""
    seed(); start(); T.request({'cmd': 'warn_cancel'}); time.sleep(1)
    open_list()
    for tag, kw in (('defaults', {}), ('latest on', dict(latest_only=1)), ('Vanilla off', dict(vanilla=0)), ('latest off', dict(latest_only=0)),
                    ('Vanilla on', dict(vanilla=1)), ('Modded off', dict(modded=0)), ('Modded on', dict(modded=1))):
        st = lf('lf', **kw) if kw else lf(); time.sleep(0.8); st = lf()
        log('layout', tag, 'line', (st.get('hid_lines') or [{}, {}])[1], [(w['name'], w.get('shown'), w.get('button_count'), w.get('saves_shown')) for w in st.get('worlds', [])])
        line_checks(f'layout {tag}', st)
        if tag == 'Vanilla on': shot('layout_after_latest')
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


def run_look():
    """de, ru, ja at 960x600: the panel (kinds, the ornament line, Latest save only, Show tags) and the tags, one shot each"""
    seed()
    for lang in ('de', 'ru', 'ja'):
        start((960, 600), 1.0, lang); T.request({'cmd': 'warn_cancel'}); time.sleep(1)
        open_list(); st = lf('lf', multiplayer=1); time.sleep(1); st = lf()
        tag_checks(f'look {lang}', st, True); line_checks(f'look {lang}', st); row_checks(f'look {lang}', st); rm_checks(f'look {lang}', st)
        shot(f'look_{lang}')
        S.stop_all()


def run_langs():
    """every language at 960x540, UI 1.0, the multiplayer world shown: the rows' Mods lines (one shot each)"""
    for lang in [l for l in LANG if l in os.environ.get('LF_LANGS', ','.join(LANG)).split(',')]:
        start((960, 540), 1.0, lang); T.request({'cmd': 'warn_cancel'}); time.sleep(1)
        open_list(); lf('lf', multiplayer=1); time.sleep(1); st = lf()
        row_checks(f'rows {lang}', st)
        shot(f'rows_{lang}')
        S.stop_all()


TIPS = ('vanilla', 'modded', 'multiplayer', 'latest_only')        # the panel's boxes in dsl_rows.c order (pn:<k>)


def tip_rect(k):
    """hover the panel's box k (no mouse, dsl_reg.c hover pn:<k>): (shown, title, card rect) 20 frames later; the card stays
    about 2 s more (for a shot)"""
    mark = len(T.dlog()); T.request({'cmd': 'hover', 'what': f'pn:{k}'}); t0 = time.time()
    rx = re.compile(r"reg test: tip pn:%d shown (\d) title '([^']*)' card ([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)" % k)
    while time.time() - t0 < 15:
        m = rx.search(T.dlog()[mark:])
        if m: return int(m.group(1)), m.group(2), [float(m.group(i)) for i in range(3, 7)]
        time.sleep(0.2)
    return 0, '', [0] * 4


def tip_checks(tag, st, shot_k=None):
    """owner 2026-10-25: a box's hover card never covers the filter panel or the Load window (it hid the other boxes:
    "barely usable"); right below the panel with its left edge when there is room; inside the screen"""
    p, w, (sw, sh) = st.get('panel', [0] * 4), st.get('window', [0] * 4), st.get('screen', [0, 0])
    for k, key in enumerate(TIPS):
        on, title, c = tip_rect(k)
        log(tag, key, 'card', c, 'panel', p, 'window', w)
        check(f'{tag}: {key}: the hover card shows', on == 1 and title, (on, title))
        check(f'{tag}: {key}: the card does not cover the filter panel', not overlap(c, p), (c, p))
        check(f'{tag}: {key}: the card does not cover the Load window', not overlap(c, w), (c, w))
        check(f'{tag}: {key}: the card is inside the screen', c[0] >= -0.5 and c[1] >= -0.5 and c[2] <= sw + 0.5 and c[3] <= sh + 0.5, (c, sw, sh))
        if p[1] - 24 - (c[3] - c[1]) >= 0:                            # room below the panel: there, with its left edge
            check(f'{tag}: {key}: room below the panel: the card is right below it, same left edge', abs(c[0] - p[0]) <= 1.5 and p[1] - 24 <= c[3] <= p[1] + 0.5,
                  (c, p))
        if k == shot_k: shot(f'tip_{tag.replace(" ", "_")}')
        time.sleep(3)                                               # the hover ends (2 s), the card goes


def run_tips():
    """the boxes' hover cards at 16:9 and 16:10, UI scale 0.75 / 1.0 / 1.4, en / de / ru / ja (owner 2026-10-25)"""
    seed()
    for size, scale, lang in (((960, 540), 1.0, 'en'), ((960, 540), 0.75, 'en'), ((960, 540), 1.4, 'de'), ((960, 600), 1.0, 'ru'), ((960, 600), 1.4, 'ja')):
        tag = f'tips {size[0]}x{size[1]} UI {scale} {lang}'
        start(size, scale, lang); T.request({'cmd': 'warn_cancel'}); time.sleep(1)
        open_list(); st = lf()
        log(tag, 'screen', st.get('screen'), 'window', st.get('window'), 'panel', st.get('panel'), 'scale', st.get('scale'))
        tip_checks(tag, st, shot_k=0)
        S.stop_all()


def run_tiplangs():
    """every language at 960x540, UI 1.0: the four boxes' cards (one shot each language, the Vanilla card: the longest text)"""
    seed()
    for lang in [l for l in LANG if l in os.environ.get('LF_LANGS', ','.join(LANG)).split(',')]:
        start((960, 540), 1.0, lang); T.request({'cmd': 'warn_cancel'}); time.sleep(1)
        open_list(); st = lf()
        tip_checks(f'tips {lang}', st, shot_k=0)
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
            {'main': run_main, 'restart': run_restart, 'sizes': run_sizes, 'langs': run_langs, 'tips': run_tips, 'tiplangs': run_tiplangs, 'layout': run_layout, 'look': run_look}[w]()
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
