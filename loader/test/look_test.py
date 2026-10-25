#!/usr/bin/env python3
"""Our texts look like the game's own menus (owner 2026-10-24: "Font inside of the units lab are not the same as the rest
of the menus"; it replaced the plain-material rule of 2026-10-13). One hidden loader test copy (LKT: background-only,
small window, muted, notch fix off; never the owner's game, saves or screen) with multiplayer + custom-modes + the test
module font-probe (test/probe/font-probe: lists the font and material of every text), hosting alone on a leased pair port.
Views: the title, the game's Options window (the reference, same run and language), the loader's Mods window, Load
Campaign (filter panel), the Multiplayer page, Join by address, the host page and its Rules, Units Laboratory, then the
x10 demo save as a host: HUD + Players panel, its cog box, chat, Teams, Esc menu, Steam invite, PvP Match.
Fails when (the breaks this catches):
  - a text in the body font or the title font has another material than the game gives its role: the Options labels'
    (body: "Lora-Regular SDF OutLine16" in English), the Options section heads' (titles under 24). Not checked: text
    fields and titles of 24 and up (plain, as the game's), and texts that keep the look of their own stock DASText
    settings (the game's own texts and stock clones). The 2026-10-23 code (plain material) fails with ~376 texts.
  - a view does not open (no reference, no texts).
  look_test.py      env: HM_BUILD = loader build (build.sh with EXTRA_CFLAGS=-DDSL_SIGN_DEV_KEY; default ../build),
                    LANGTYPE = the game's language (3 de, 4 ru, 7 ja, 8 es),
                    LOOK_X = the copy's name (default LKT), MP_TREE = ds_multiplayer checkout (rig/demo.py, ext/fepvp13/tools/mpm_ids.py; default ~/ds_multiplayer)
Output: ../test_env/look/<time>_<copy>/ (results.json, fonts_<view>.jsonl, frames in drv/cap/)."""
import json, os, shutil, subprocess, sys, threading, time, traceback, zipfile

HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(HERE)
MPT = os.path.expanduser(os.environ.get('MP_TREE', '~/ds_multiplayer'))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(MPT, 'rig'))
import hostmods_pair as HP  # noqa: E402  (copies, kits, start/stop, owner screen guard, pair leases)
from hostmods_pair import lrig, realrig, log, check, c  # noqa: E402

X = os.environ.get('LOOK_X', 'LKT'); HP.H = HP.G = X                 # copy name (two runs at once: two names)
HP.OUT = OUT = os.path.join(L, 'test_env', 'look', time.strftime('%Y%m%d-%H%M%S') + '_' + X); os.makedirs(OUT, exist_ok=True)
DRV = os.path.join(OUT, 'drv'); os.makedirs(os.path.join(DRV, 'cap'), exist_ok=True)
UIT = os.path.join(OUT, 'uitest'); os.makedirs(UIT, exist_ok=True)
B = json.loads(subprocess.run([sys.executable, os.path.join(MPT, 'ext/fepvp13/tools/mpm_ids.py'), MPT], capture_output=True, text=True).stdout)
views = {}


def zip_dir(src, mid):
    zp = os.path.join(OUT, 'pkg', f'{mid}.zip'); os.makedirs(os.path.dirname(zp), exist_ok=True)
    with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as zf:
        for r, _, fs in os.walk(src):
            for f in sorted(fs):
                if not f.startswith('.'):
                    zf.write(os.path.join(r, f), os.path.join(mid, os.path.relpath(os.path.join(r, f), src)))
    return zp


def probe_pkg():
    """the test module as a mod zip (built here, never shipped)"""
    src = os.path.join(HERE, 'probe', 'font-probe'); d = os.path.join(OUT, 'pkg', 'font-probe'); os.makedirs(d, exist_ok=True)
    subprocess.run(['nice', '-n', '19', 'clang', '-arch', 'arm64', '-O2', '-Wno-unused-function', '-I' + os.path.join(L, 'sdk'), '-dynamiclib',
                    '-framework', 'CoreFoundation', '-framework', 'CoreGraphics', '-lobjc', '-install_name', '@rpath/font-probe.dylib',
                    '-o', os.path.join(d, 'font-probe.dylib'), os.path.join(src, 'font_probe.c')], check=True)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(d, 'font-probe.dylib')], capture_output=True)
    shutil.copy2(os.path.join(src, 'mod.json'), os.path.join(d, 'mod.json'))
    return zip_dir(d, 'font-probe')


seq = [0]
def req(cmd, timeout=30, **kw):
    seq[0] += 1; q = dict(kw, seq=seq[0], cmd=cmd)
    open(os.path.join(DRV, 'req.json.tmp'), 'w', encoding='utf-8').write(json.dumps(q, ensure_ascii=False)); os.replace(os.path.join(DRV, 'req.json.tmp'), os.path.join(DRV, 'req.json'))
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            a = json.load(open(os.path.join(DRV, 'resp.json')))
            if a.get('seq') == seq[0]:
                return a
        except (OSError, ValueError):
            pass
        time.sleep(0.2)
    raise RuntimeError(f'no answer to {q}')


useq = [0]
def ui(op):                                                  # the loader's DSL_UI_TEST channel (Mods window)
    useq[0] += 1
    open(os.path.join(UIT, 'cmd.json.tmp'), 'w').write(json.dumps({'op': op, 'seq': str(useq[0])})); os.replace(os.path.join(UIT, 'cmd.json.tmp'), os.path.join(UIT, 'cmd.json'))
    t0 = time.time()
    while time.time() - t0 < 15:
        try:
            if str(json.load(open(os.path.join(UIT, 'result.json'))).get('seq')) == str(useq[0]):
                return
        except (OSError, ValueError):
            pass
        time.sleep(0.2)


def view(name, wait=2.5):
    """every active text now (font-probe "fonts") and a frame of the game"""
    time.sleep(wait)
    req('fonts', name=name, all=0); req('capture', name=name)
    p = os.path.join(DRV, f'fonts_{name}.jsonl')
    views[name] = [json.loads(l) for l in open(p, encoding='utf-8')] if os.path.exists(p) else []
    log('view', name, len(views[name]))


def mc(**k):
    try:
        return c(X, **k)
    except Exception as e:
        return {'error': repr(e)}


def press(name, wait=1.5):
    r = mc(cmd='mpm_press', button=B[name]); time.sleep(wait); return r


def column(i):
    rows = [x.split('|') for x in (req('state').get('title_column') or '').split(';') if x]
    return next((r[0] for r in rows if len(r) > 2 and r[2] == str(i) and r[1] == '1'), None)


def click(t, wait=2.5):
    return req('click', target=t).get('rc') == 0 and (time.sleep(wait) or True)


def role_ref():
    r = {}
    for t in views.get('options', []):
        if 'GameOptionUI' in t['path'] and t['das_style'] in (0, 2) and t['das_mat'] == 1:
            r.setdefault('body' if t['das_style'] == 0 else 'head', (t['font'], t['mat']))
    return r


def off_look(t, r):
    """None, or why this text is not in the game's look for its role"""
    (f0, m0), (f2, m2) = r['body'], r['head']
    p = t['path']
    if not t['active'] or t['font'] not in (f0, f2) or 'Text Area' in p or 'InputField' in p or '/Input_' in p:
        return None
    want = m0 if t['font'] == f0 else (m2 if t['size'] < 24 else None)
    if want is None or t['mat'] == want:
        return None
    if t['cls'] == 'DASText' and {0: 'OutLine' not in t['mat'], 1: 'OutLine16' in t['mat'], 2: 'OutLine24' in t['mat']}.get(t['das_mat'], False):
        return None                                          # the look its own stock settings give it
    return f"{t['text'][:30]!r} {t['mat']} {t['size']:.0f} ..{p[-50:]}"


def main():
    if os.path.exists(os.path.join(HP.RIGDIR, 'release.lock')):
        raise SystemExit('refused: ~/ds_multiplayer/rig/release.lock exists (release gate running)')
    g = threading.Thread(target=HP.guard, daemon=True); g.start()
    pair = None; res = {}
    try:
        t0 = time.time(); pair = realrig.lease('look', os.getpid())
        while not pair and time.time() - t0 < 1800:
            time.sleep(15); pair = realrig.lease('look', os.getpid())
        if not pair:
            raise SystemExit('no free pair lease for 30 min')
        HP.make_copy(X)
        pk = {'custom-modes': zip_dir(os.path.join(HP.BUILD, 'mods', 'custom-modes'), 'custom-modes'), 'font-probe': probe_pkg()}
        HP.kit(X, pk, ['custom-modes', 'font-probe'], ['multiplayer', 'custom-modes', 'font-probe'])
        json.dump({'role': 'host', 'port': realrig.PAIR_PORTS[pair], 'name': 'Host'}, open(os.path.join(lrig.idir(X), 'mp.json'), 'w'))
        lang = os.environ.get('LANGTYPE')
        if lang:
            prep = lrig.prepare_data
            def prep_lang(x):
                prep(x); us = os.path.join(lrig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(us))
                s['GeneralSettingData']['SaveCurrentLanguageType'] = int(lang); json.dump(s, open(us, 'w'), indent=1)
            lrig.prepare_data = prep_lang
        crash = os.path.join(OUT, 'crash'); os.makedirs(crash, exist_ok=True); open(os.path.join(crash, 'answer'), 'w').write('no')
        HP.room_for(1)
        HP.start(X, 'http://127.0.0.1:9', {'DSL_UNITS_TEST': DRV, 'DSL_CRASHREP_TEST': crash, 'DSL_UI_TEST': UIT})
        lrig.wait_for(X, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title', timeout=1500); time.sleep(6)
        view('title', 0)
        if click('title:' + (column(38) or 'Settings'), 3):
            view('options'); req('key', code=53); time.sleep(1.5)
        ui('open'); view('mods'); ui('close'); time.sleep(1)
        if click('title:' + (column(2) or 'Load Campaign'), 3):
            view('load'); mc(cmd='mpm_list', close=1); time.sleep(1.5)
        press('MULTIPLAYER'); view('mp_page')
        press('JOIN_ADDR'); view('mp_join'); press('BACK')
        press('HOST_SAVE'); view('mp_host'); press('SETTINGS'); view('mp_rules'); press('RULES_DONE'); press('BACK'); press('BACK')
        if click('title:' + (column(5) or 'Units Laboratory'), 3):
            view('lab_home'); click('home:My Units', 4); view('lab_my_units'); click('mu:close', 2); click('home:Back', 2)
        from demo import make_demo_save                          # ds_multiplayer rig/demo.py: the x10 demo save
        work = os.path.join(lrig.idir(X), 'save_in'); os.makedirs(work, exist_ok=True)
        src = os.path.join(work, 'demo_buffed.json'); make_demo_save(src)
        mc(cmd='load_save', file=src, det=1)
        HP.wait('host locked', lambda: HP.st(X).get('det', {}).get('locked'), timeout=900, every=2); time.sleep(6)
        HP.players('start', (X,), 1)                             # hosting alone on the pair port: a second player = another run's copy
        view('game_hud')
        mc(cmd='ui_pop', open=1); view('game_cog'); mc(cmd='ui_pop', open=0)
        mc(cmd='mpm_chat', text='Hello from the look test'); mc(cmd='mpm_chat', open=1); view('game_chat'); mc(cmd='mpm_chat', escape=1); time.sleep(1)
        mc(cmd='ui_teams', action='open'); view('game_teams', 3); mc(cmd='ui_teams', action='close'); time.sleep(1)
        mc(cmd='mpm_esc'); view('game_esc'); press('ESC_INVITE', 3); view('game_invite'); mc(cmd='mpm_escape'); time.sleep(1.5)
        mc(cmd='mpm_esc'); time.sleep(2); mc(cmd='mpm_pvp', action='press_esc'); view('game_pvp', 3)
        HP.players('end', (X,), 1)
        r = role_ref()
        check("the game's Options window: body and section head look found (reference)", len(r) == 2, r)
        if len(r) == 2:
            for name, ts in views.items():
                bad = [x for x in (off_look(t, r) for t in ts) if x]
                check(f"{name}: shown, every text in the game's look for its role", len(ts) >= 8 and not bad, bad[:6] or len(ts))
    except BaseException as e:
        log(traceback.format_exc())
        check('run finished', False, repr(e))
    finally:
        check('never a visible window on a display, never frontmost (guard)', not HP.violations, HP.violations[:3])
        HP.stop_guard.set()
        try:
            HP.stop(X)
        except Exception as e:
            log('stop', repr(e))
        if pair:
            realrig.release(pair)
        for f in ('dsload.log', 'rig.log', 'player.log'):
            p = os.path.join(lrig.idir(X), f)
            if os.path.exists(p):
                shutil.copy2(p, os.path.join(OUT, f))
        res = {'checks': HP.checks, 'all_pass': all(k['ok'] for k in HP.checks) and bool(HP.checks)}
        json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
        log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', f"{sum(k['ok'] for k in HP.checks)}/{len(HP.checks)}", os.path.join(OUT, 'results.json'))
    sys.exit(0 if res.get('all_pass') else 1)


if __name__ == '__main__':
    main()
