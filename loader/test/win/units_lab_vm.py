#!/usr/bin/env python3
"""Windows VM run of Units Laboratory's My Units windows (custom-modes 0.1.15-0.1.16; the core of ds_custommodes
units/test/worlds_test.py, EN) through winrig (ds_multiplayer win/winbot, WINBOT_DIR), the VM running visibly with the
agent up and the kit under test installed. The test module units-probe (units_probe/, Windows only, never shipped) reads
Units Laboratory's own test registry and presses its buttons; it goes into the profile's mods folder for the run only.
  python3 units_lab_vm.py [--out DIR] [--probe DIR with units-probe/]   (default: builds it with units_probe/build.sh)
The game reads a TEST library (DSMP_UNITS_LIB, made here from ds_custommodes' launcher fixture, 6 units, team A = u0 u1 u2,
team B = u2 u3) and two TEST worlds made from the wiki's public base save under new world guids: one in the game saves
folder (DSML_GAME_SAVES), one in the modded saves. Real breaks it catches on Windows:
  - Delete team: the box (Team and units / Team only / Cancel) does not show, Cancel changes something, "Team and units"
    deletes another unit than the team's units in no other team, "Team only" deletes a unit, the file disagrees.
  - World units: a world of either saves folder is missing, the list is not newest first, a hub / multiplayer temp world
    is listed, a world's units are not its save's units, a world's units can be renamed / deleted.
  - Save snapshot: the new units are not the ticked save units with world, save, save time and snapshot time; the card
    shows no source line; a card or source line overflows its box.
Exit 0 = every check passed."""
import argparse, copy, json, os, subprocess, sys, time, uuid

HERE = os.path.dirname(os.path.abspath(__file__)); T_DIR = os.path.dirname(HERE)
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402

CM = os.path.expanduser(os.environ.get('CM_ROOT', '~/ds_custommodes'))      # its units/test/data/launcher_library.json
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # public wiki save
WORK = r'C:\dsm\units_lab_vm'
DRV = WORK + r'\drv'
LIB = WORK + r'\lib\library.json'
GAME_SAVES = W.WORK + r'\fake_gamesaves'                                      # winrig's DSML_GAME_SAVES
HUB_GUIDS = ('fe5ea000', 'fe5eb', 'b45e0553')                                # hub / PvP worlds: never listed
TO_DEL = W.WORK + r'\to_delete\units_lab_vm_' + time.strftime('%Y%m%d-%H%M%S')
checks = []
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:700]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def make_library(out):
    fx = json.load(open(os.path.join(CM, 'units', 'test', 'data', 'launcher_library.json')))
    units = fx['units'][:6]
    for i, u in enumerate(units):
        u['id'] = f'{0xa0 + i:02x}' * 8; u['name'] = f'Test unit {i}'
    lib = {'format': 'ds-launcher-units', 'version': 1, 'game_build': '25846389', 'units': units,
           'teams': [{'id': 'aa' * 8, 'name': 'Team A', 'unit_ids': [units[0]['id'], units[1]['id'], units[2]['id']]},
                     {'id': 'bb' * 8, 'name': 'Team B', 'unit_ids': [units[2]['id'], units[3]['id']]}]}
    p = os.path.join(out, 'lib_in.json'); json.dump(lib, open(p, 'w'), indent=2)
    return p, [u['id'] for u in units]


def make_world(out, prefix, when):
    """the public base save under a new world guid; file name <prefix>__AutoSave_<hex>_0, its header SaveTime = when"""
    d = json.load(open(TPL, encoding='utf-8-sig')); g = str(uuid.uuid4()); hx = g.replace('-', '')
    d['CampaignSaveHeader']['CampaignGuid'] = g; d['ClanSaveData']['CampaignGuid'] = g
    name = f'{prefix}__AutoSave_{hx}_0'; d['CampaignSaveHeader']['SaveName'] = name
    if 'SaveTime' in d['CampaignSaveHeader']: d['CampaignSaveHeader']['SaveTime'] = when
    css = d['CampaignSettingSaveData']['CampaignStartSettingSaveData']
    css.setdefault('LockConsecrationEnabled', False); css['LockConsecrationNoticeAcknowledged'] = True
    p = os.path.join(out, name + '.json'); json.dump(d, open(p, 'w'))
    return p, name, g


class Drv:
    def __init__(self, w): self.w, self.seq = w, 0

    def __call__(self, cmd, timeout=30, **kw):
        self.seq += 1; q = dict(kw, seq=self.seq, cmd=cmd)
        self.w.write(DRV + r'\req.json', json.dumps(q, ensure_ascii=False))
        def ans():
            a = self.w.json_file(DRV + r'\resp.json')
            return a if a and a.get('seq') == self.seq else None
        return self.w.wait(ans, f'units-probe answer to {q}', timeout, 0.2)

    def mu(self): return (self('state').get('cm') or {}).get('mu') or {}

    def click(self, t, wait=1.5):
        r = self('click', target=t); time.sleep(wait)
        log('click', t, r.get('rc'), r.get('text'))
        return r.get('rc') == 0

    def shown(self, t): return self('rect', target=t).get('rc', -1) > 0


def disk(w):
    d = w.json_file(LIB) or {}
    return d, {u['id'] for u in d.get('units', [])}, {t['name']: t['unit_ids'] for t in d.get('teams', [])}


def run(w, out, d, ids, gname, gsave, mname, msave):
    r = d('open'); time.sleep(3)
    check('Units Laboratory page opens', r.get('rc') == 0 and ((r.get('cm') or {}).get('home') or {}).get('open') in (1, None), r.get('rc'))
    ok = d.click('home:My Units', 4)
    s = d.mu()
    norm = lambda p: (p or '').replace('/', '\\').lower()
    check('My Units opened on the TEST library', ok and s.get('open') == 1 and norm(s.get('lib')) == norm(w.expand(LIB)) and s.get('units') == 6 and s.get('teams') == 2,
          (ok, s.get('open'), s.get('lib'), s.get('units'), s.get('teams')))
    worlds = s.get('worlds') or []
    sp = [x.split('|') for x in worlds]
    mt = {}                                                          # newest first = by the newest save FILE's time (dsunits), the
    for folder in (W.DATA + r'\Saves', GAME_SAVES):                  # row shows its header SaveTime (= the file time in a real save)
        for e in w.a('ls', path=folder).get('entries', []):
            if e['name'].endswith('.json'): mt[e['name'][:-5]] = max(mt.get(e['name'][:-5], 0), e['mtime'])
    order = [mt.get(x[2], -1) for x in sp]
    check('World units: the game saves\' world and the modded saves\' world, in one list, newest first (the times on the rows, UX U11)',
          any(x[2] == gsave for x in sp) and any(x[2] == msave for x in sp) and [x[3] for x in sp] == sorted((x[3] for x in sp), reverse=True), list(zip(worlds, order)))
    check('World units: no hub / multiplayer temp world listed', not any(h in x[2].lower() for x in sp for h in HUB_GUIDS) and not any(x[2].startswith(('MPHub_', 'MPFE_', 'MPPvP_', 'mp_')) for x in sp), worlds)
    # ---- Delete team: the box, Cancel
    d.click('mu:team0'); d.click('mu:delete_team')
    s = d.mu()
    try: w.gshot(os.path.join(out, 'win_dlg_en.png'), 'ulab_dlg')
    except W.GameError as e: log('picture:', e)
    check('Delete team shows the box with Team and units / Team only / Cancel', s.get('dlg') == 1 and d.shown('mu:dlg_units') and d.shown('mu:dlg_only') and d.shown('mu:dlg_cancel'), s.get('dlg'))
    d.click('mu:dlg_cancel'); s = d.mu(); _, du, dt = disk(w)
    check('Cancel: nothing deleted', s.get('dlg') == 0 and s.get('units') == 6 and s.get('teams') == 2 and len(du) == 6 and len(dt) == 2, (s.get('units'), s.get('teams'), len(du), len(dt)))
    # ---- Team and units on team A (u0 u1 u2; u2 also in team B): u0 and u1 go
    d.click('mu:team0'); d.click('mu:delete_team'); d.click('mu:dlg_units', 2)
    s = d.mu(); _, du, dt = disk(w)
    check('Team and units: the 2 units in no other team are deleted, the unit also in team B stays (window + file)',
          s.get('units') == 4 and s.get('teams') == 1 and du == set(ids[2:6]) and dt == {'Team B': [ids[2], ids[3]]}, (s.get('units'), sorted(du), dt, s.get('msg')))
    # ---- Team only on team B: no unit goes
    d.click('mu:team0'); d.click('mu:delete_team'); d.click('mu:dlg_only', 2)
    s = d.mu(); _, du, dt = disk(w)
    check('Team only: the team goes, every unit stays (window + file)', s.get('units') == 4 and s.get('teams') == 0 and du == set(ids[2:6]) and not dt, (s.get('units'), sorted(du), dt))
    # ---- World units: the game saves' world, read only
    gi = next((i for i, x in enumerate(sp) if x[2] == gsave), -1)
    d.click(f'mu:world{gi}', 4)
    s = d.mu()
    try: w.gshot(os.path.join(out, 'win_world_en.png'), 'ulab_world')
    except W.GameError as e: log('picture:', e)
    wunits = s.get('wunits') or []
    check('a world shows its save\'s units as cards', gi >= 0 and s.get('sel_world') == gi and len(wunits) >= 2 and s.get('cards', 0) >= 2, (gi, s.get('sel_world'), len(wunits), s.get('cards')))
    check('a world\'s units are read only (no Save as team / Rename / Delete / Delete team; Save snapshot shown)',
          not d.shown('mu:save_team') and not d.shown('mu:rename') and not d.shown('mu:delete') and not d.shown('mu:delete_team') and d.shown('mu:snapshot'))
    # ---- Save snapshot of the first 2 units
    d.click('mu:tick0'); d.click('mu:tick1'); s = d.mu()
    check('2 world units ticked', s.get('wticked') == 2, s.get('wticked'))
    d.click('mu:snapshot', 3)
    s = d.mu(); lib, du, _ = disk(w)
    new = [u for u in lib.get('units', []) if u['id'] not in ids]
    src = [u.get('source') or {} for u in new]
    hdr = next((x for x in sp if x[2] == gsave), ['', '', '', ''])
    check('Save snapshot: 2 new My Units units, each with the ticked save unit, world, save, save time and snapshot time',
          s.get('units') == 6 and len(new) == 2 and sorted(x.get('unit') for x in src) == sorted(wunits[:2])
          and all(x.get('kind') == 'snapshot' and x.get('world') == hdr[0] and x.get('save') == gsave and x.get('save_time') == hdr[3] and x.get('taken', 0) > 0 for x in src),
          (s.get('units'), src, s.get('msg')))
    # ---- the card shows the source line
    d.click('mu:all', 2); s = d.mu(); pp = max(1, s.get('per_page', 1))
    for _ in range((s.get('shown', 0) - 1) // pp):                   # the snapshot units are the last ones: their page
        d.click('mu:next', 1)
    s = d.mu(); pg = s.get('page', 0)
    try: w.gshot(os.path.join(out, 'win_src_en.png'), 'ulab_src')
    except W.GameError as e: log('picture:', e)
    want = sum(1 for u in lib.get('units', [])[pg * pp:pg * pp + pp] if u.get('source'))
    cs = [c for c in s.get('card_list') or [] if c.get('id') in {u['id'] for u in new}]   # as ds_custommodes worlds_test (0.1.35+ card)
    check('a snapshot unit\'s card shows its source inside the card (world + snapshot time; the full line with the save time on hover)',
          want >= 1 and s.get('src_lines') == want and cs and all(hdr[0] in c['src'] and hdr[0] in c['src_full'] and hdr[3] in c['src_full'] for c in cs),
          (s.get('src_lines'), want, pg, pp, cs, hdr))
    # (mu_test_overflow needs the loader's dsl_ui_test_rect, which the Windows loader does not export: Mac only)
    # ---- the modded saves' world shows too
    mi = next((i for i, x in enumerate(sp) if x[2] == msave), -1)
    d.click(f'mu:world{mi}', 4); s = d.mu()
    check('the modded saves\' world shows its units', mi >= 0 and s.get('sel_world') == mi and len(s.get('wunits') or []) >= 1, (mi, s.get('sel_world'), len(s.get('wunits') or [])))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='units_lab_vm_out'); ap.add_argument('--probe')
    a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    probe = a.probe
    if not probe:
        probe = os.path.join(out, 'probe_build')
        subprocess.run(['zsh', os.path.join(HERE, 'units_probe', 'build.sh'), probe], check=True)
    w = W.Win(); w.up(); w.kill_all()
    md = W.ROOT + r'\mods\units-probe'
    lib_local, ids = make_library(out)
    gp, gsave, _ = make_world(out, 'UnitsLabGame', '2026-10-25 08:00:00')
    mp, msave, _ = make_world(out, 'UnitsLabModded', '2026-10-25 09:00:00')
    placed = [GAME_SAVES + '\\' + gsave + '.json', W.DATA + '\\Saves\\' + msave + '.json']
    try:
        for f in ('units-probe.dll', 'mod.json'):
            w.upload(os.path.join(probe, 'units-probe', f), md + '\\' + f)
        w.write(LIB, open(lib_local, 'rb').read())
        w.upload(gp, placed[0]); w.upload(mp, placed[1])
        w.prepare(lang=0, consent='no', active=['multiplayer', 'custom-modes', 'units-probe'], settings={})
        w.write(DRV + r'\.keep', b'')
        w.start(env={'DSL_CLOUD': '0', 'DSL_UNITS_TEST': DRV, 'DSMP_UNITS_LIB': LIB})
        L = w.text(W.PROF + r'\dsload.log') or ''
        dl = [l for l in L.splitlines() if 'units test driver' in l]
        check('the test driver runs (frame hook on)', dl and 'frame hook 0' in dl[-1], dl[-1:])
        run(w, out, Drv(w), ids, None, gsave, None, msave)
        check('the world save copies are unchanged by the read', all((w.a('stat', path=p).get('size') or 0) == os.path.getsize(l) for p, l in zip(placed, (gp, mp))))
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: w.logs(out); w.get(LIB, os.path.join(out, 'library_after.json'))
        except Exception as e: log('logs:', e)
        w.stop(); w.kill_all()
        for p in [md + r'\units-probe.dll', md + r'\mod.json'] + placed:   # moved away, never deleted
            try:
                if w.exists(p): w.move(p, TO_DEL + '\\' + p.split('\\')[-2] + '_' + p.split('\\')[-1])
            except Exception as e: log('move:', e)
        json.dump(checks, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', f'{len(checks) - len(bad)}/{len(checks)} checks')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
