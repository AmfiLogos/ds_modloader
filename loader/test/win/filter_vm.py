#!/usr/bin/env python3
"""Windows VM run of the Load Campaign filter panel (test/l1_filter_test.py on the Mac) through winrig (ds_multiplayer
win/winbot, WINBOT_DIR), VM running visibly with the agent up and the kit under test installed (multiplayer 0.1.22 in it).
  python3 filter_vm.py [--out DIR]
Worlds: three copies of the wiki's public base save (never a player's save): Vanilla (two saves, the older one named
"Template older"), Modded (gcd-cdr in worlds.json), Multiplayer (in the profile's mp_tags.txt). What the profile had
(mp_tags.txt, loadlist.json) is moved aside and back; the test saves are moved to C:\\dsm\\to_delete after (never deleted).
Checks: the guard hooks install on Windows; the panel shows right of the Load window, inside the screen, off the title
corner; kinds; the multiplayer box under the list is gone; the data guard (a selected save hidden by Latest save only
or by a kind box: Remove File + Yes to every box removes nothing); the choices come back after a restart.
Exit 0 = every check passed."""
import argparse, json, ntpath, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402

TPL_SRC = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')   # public (the wiki's base save)
G_V, G_M, G_P = '491bc260-98aa-4cd1-b63f-82da78369aaf', '5a1e0c2d-0b7e-4f42-9a51-6c3d2e1f0a01', '5a1e0c2d-0b7e-4f42-9a51-6c3d2e1f0a02'
TPL_NEW, TPL_OLD = 'Template__AutoSave_491bc26098aa4cd1b63f82da78369aaf_0', 'Template older'
MOD_SAVE, MP_SAVE = 'Filter modded', 'Filter multi'
TO_DEL = W.WORK + r'\to_delete\filter_vm_' + time.strftime('%Y%m%d-%H%M%S')
checks = []
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def seeds(out):
    """the four save files (local), each a copy of the public base save with its own guid / name / time"""
    raw = open(TPL_SRC).read(); files = {}
    def one(name, guid, clan, when, auto):
        t = json.loads(raw.replace(G_V, guid).replace(G_V.replace('-', ''), guid.replace('-', '')))
        h = t['CampaignSaveHeader']; h.update(CampaignGuid=guid, SaveName=name, DisplayName=clan if auto else name, SaveTime=when, IsAutoSave=auto, ClanName=clan)
        p = os.path.join(out, name + '.json'); json.dump(t, open(p, 'w')); files[name + '.json'] = p
    one(TPL_NEW, G_V, 'Template', '2026-09-04 09:59:06', True)
    one(TPL_OLD, G_V, 'Template', '2026-09-01 08:00:00', False)
    one(MOD_SAVE, G_M, 'Filter modded', '2026-09-02 08:00:00', False)
    one(MP_SAVE, G_P, 'Filter multi', '2026-09-03 08:00:00', False)
    return files


class Run:
    def __init__(self, w, out): self.w, self.out = w, out
    def dlog(self): return self.w.text(W.PROF + r'\dsload.log') or ''
    def lf(self, cmd='lf_state', **kw):
        p = W.PROF + r'\softrestart\lf_state.json'
        if self.w.exists(p): self.w.move(p, TO_DEL + r'\lf_state_' + str(int(time.time() * 1000)) + '.json')
        self.w.write(W.PROF + r'\softrestart\reg_request.json', json.dumps(dict(cmd=cmd, **kw)))
        t0 = time.time()
        while time.time() - t0 < 15:
            j = self.w.json_file(p)
            if j: return j
            time.sleep(0.3)
        return {}
    def open_list(self):
        n = len(self.dlog())
        self.w.cmd(cmd='mpm_open_load')
        self.w.wait(lambda: 'rows: Load list rebuilt' in self.dlog()[n:], 'the Load list rebuilt', 40)
        time.sleep(2)
    def boxes(self, n=2):
        for _ in range(n):                                   # a player who answers every box Yes
            try: log('box', self.w.cmd(cmd='mpm_box', action='confirm'))
            except W.GameError as e: log('box', e)
            time.sleep(2)


def world(st, guid): return next((x for x in st.get('worlds', []) if x['guid'] == guid), {})
def overlap(a, b): return a[0] < b[2] and a[2] > b[0] and a[1] < b[3] and a[3] > b[1]


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='filter_vm_out'); a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    w = W.Win(); w.up(); R = Run(w, a.out)
    files = seeds(a.out)
    w.kill_all()
    kept = []
    for p in (W.PROF + r'\mp_tags.txt', W.DATA + r'\Mods\loadlist.json'):
        if w.exists(p): w.move(p, TO_DEL + '\\kept_' + ntpath.basename(p)); kept.append(p)
    try:
        w.prepare(lang=0, consent='no', active=['multiplayer', 'gcd-cdr'], saves=files)
        w.write(W.DATA + r'\Mods\worlds.json', json.dumps({'worlds': {G_V: 'vanilla', G_M: 'gcd-cdr'}}))
        w.write(W.PROF + r'\mp_tags.txt', f'{G_P}\tHost\tGuest\n')
        w.start(env={'DSL_SOFT_TEST': '1'})
        L = R.dlog()
        check('the guard hooks install on Windows (SaveLoadPresenter Remove File + Load)', 'Remove File on, Load on' in L,
              [l for l in L.splitlines() if 'load guard' in l])
        R.open_list(); st = R.lf()
        log('state', json.dumps(st)[:700])
        p, wd, c, (sw, sh) = st.get('panel', [0] * 4), st.get('window', [0] * 4), st.get('corner', [0] * 4), st.get('screen', [0, 0])
        check('the panel shows', st.get('panel_on') == 1 and st.get('panel_shown') == 1, st)
        check('the panel is right of the Load window and inside the screen', p[0] >= wd[2] - 0.5 and p[0] >= 0 and p[1] >= -0.5 and p[2] <= sw + 0.5 and p[3] <= sh + 0.5, (p, wd, sw, sh))
        check('the panel does not cover the title corner', not st.get('corner_shown') or not overlap(p, c), (p, c, st.get('corner_shown')))
        b = st.get('boxes', {})
        check('defaults: Vanilla & Vanilla+ on, Modded on, Multiplayer off, all saves; Multiplayer box there',
              (b.get('vanilla'), b.get('modded'), b.get('multiplayer'), b.get('latest_only'), b.get('mp_box')) == (1, 1, 0, 0, 1), b)
        v, m, mp = world(st, G_V), world(st, G_M), world(st, G_P)
        check('kinds: vanilla / modded (gcd-cdr) / multiplayer (tag)', (v.get('kind'), m.get('kind'), mp.get('kind')) == (0, 1, 2), (v, m, mp))
        check('Multiplayer off: the multiplayer world hidden, the others shown', (v.get('shown'), m.get('shown'), mp.get('shown')) == (1, 1, 0), (v, m, mp))
        ml = w.cmd(cmd='mpm_list')
        check("the multiplayer mod's box under the list is gone", ml.get('open') and ml.get('toggle') == 0 and ml.get('hidden') == 0,
              {k: ml.get(k) for k in ('open', 'mode', 'toggle', 'hidden')})
        w.gshot(os.path.join(a.out, 'filter_panel_en.png'))
        # ---------------------------------------------------------------- guard: Latest save only
        R.lf('lf_select', name=TPL_OLD); time.sleep(1.5); st = R.lf()
        check('the older Template save is selected', st.get('selected') == TPL_OLD, st.get('selected'))
        st = R.lf('lf', latest_only=1); v = world(st, G_V)
        check('Latest save only: the newest save shows, the older one hidden; the row counts 1', v.get('shown_names') == [TPL_NEW] and v.get('button_count') == 1, v)
        check('the hidden selected save is unselected, Remove File locked', st.get('selected') == '' and st.get('remove_clickable') == 0, st.get('selected'))
        n = len(R.dlog()); R.lf('lf_remove'); time.sleep(2); R.boxes()
        check('Remove File + Yes: the hidden older save is still there', w.exists(W.DATA + '\\Saves\\' + TPL_OLD + '.json'),
              [l for l in R.dlog()[n:].splitlines() if 'Remove File' in l or 'saves' in l][-5:])
        # ---------------------------------------------------------------- guard: a kind box
        R.lf('lf', latest_only=0); R.lf('lf_select', name=MOD_SAVE); time.sleep(1.5)
        st = R.lf('lf', modded=0)
        check('Modded off: the modded world hidden, its save unselected', world(st, G_M).get('shown') == 0 and st.get('selected') == '', (world(st, G_M), st.get('selected')))
        n = len(R.dlog()); R.lf('lf_remove'); time.sleep(2); R.boxes()
        check('Remove File + Yes: the hidden modded save is still there', w.exists(W.DATA + '\\Saves\\' + MOD_SAVE + '.json'),
              [l for l in R.dlog()[n:].splitlines() if 'Remove File' in l or 'saves' in l][-5:])
        R.lf('lf', multiplayer=1, latest_only=1)
        # ---------------------------------------------------------------- restart: the choices come back
        w.stop(); w.start(env={'DSL_SOFT_TEST': '1'}); R.open_list(); st = R.lf(); b = st.get('boxes', {})
        check('restart: the choices came back (Modded off, Multiplayer on, Latest save only on)',
              (b.get('vanilla'), b.get('modded'), b.get('multiplayer'), b.get('latest_only')) == (1, 0, 1, 1), b)
        w.gshot(os.path.join(a.out, 'filter_panel_restart.png'))
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: w.logs(a.out)
        except Exception as e: log('logs', e)
        try: w.stop()
        except Exception: pass
        w.kill_all()
        for name in files:                                    # the test saves: moved away, never deleted
            if w.exists(W.DATA + '\\Saves\\' + name): w.move(W.DATA + '\\Saves\\' + name, TO_DEL + '\\' + name)
        for p in (W.PROF + r'\mp_tags.txt', W.DATA + r'\Mods\loadlist.json'):
            if w.exists(p): w.move(p, TO_DEL + '\\test_' + ntpath.basename(p))
        for p in kept: w.move(TO_DEL + '\\kept_' + ntpath.basename(p), p)
        json.dump(checks, open(os.path.join(a.out, 'summary.json'), 'w'), indent=1)
        log(f"RESULT {sum(c['ok'] for c in checks)}/{len(checks)} checks")
    sys.exit(0 if checks and all(c['ok'] for c in checks) else 1)


if __name__ == '__main__':
    main()
