#!/usr/bin/env python3
"""Windows VM run of test/multibar_test.py (Multi-unit view, mod multi-bar): the same flow (the wiki base save, 2 / 3 / 4
units by Shift-select, a click on card k = unit k, back to one unit) through winrig (ds_multiplayer win/winbot,
WINBOT_DIR), VM running visibly with the agent up.
  python3 multibar_vm.py --mods <build/mods_all/win> [--out DIR]
The test files go into the profile's mods folder for the run (multi-bar from --mods, multibar-probe built here with
llvm-mingw); anything installed under those two names is moved aside and moved back after (never deleted).
Exit 0 = every check passed."""
import argparse, hashlib, json, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); T_DIR = os.path.dirname(HERE)
sys.path.insert(0, T_DIR)
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402
import multibar_test as M  # noqa: E402

KEEP = r'C:\dsm\ToDelete'
WORK = r'C:\dsm\multibar_vm'
CC = os.path.expanduser('~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang')
IDS = ('multi-bar', 'multibar-probe')


class VmT:
    def __init__(self, w): self.w = w
    def dlog(self): return self.w.text(W.PROF + r'\dsload.log') or ''
    def plog(self): return self.w.text(W.PROF + r'\player.log') or ''
    def req(self, line): self.w.write(W.PROF + r'\multibar_probe_req.txt', (line + '\n').encode())
    def gone(self):
        return None if any(p['pid'] == self.w.pid for p in self.w.procs()) else 'the game process ended'
    def load_world(self, out):
        g = WORK + '\\MultiBarTest__ManualSave_0.json'
        self.w.upload(M.SAVE, g)
        M.log('load', self.w.cmd(cmd='load_save', file=self.w.expand(g)))
        self.w.wait(self.w.loaded, 'the world loaded', 400)


def probe_dll(out):
    p = os.path.join(out, 'multibar-probe.dll')
    subprocess.run([CC, '-O2', '-Wall', '-I', os.path.join(os.path.dirname(T_DIR), 'sdk'), '-shared', '-Wl,--no-insert-timestamp',
                    '-o', p, os.path.join(T_DIR, 'multibar_probe.c'), '-Wl,--export-all-symbols', '-static', '-static-libgcc'], check=True)
    return p


def swap_in(w, mods, out):
    keep = KEEP + r'\multibar_vm_' + time.strftime('%Y%m%d-%H%M%S')
    w.kill_all()
    pd = probe_dll(out)
    files = {'multi-bar': {f: os.path.join(mods, 'multi-bar', f) for f in ('multi-bar.dll', 'mod.json')},
             'multibar-probe': {'multibar-probe.dll': pd}}
    for mid, fs in files.items():
        md = W.ROOT + '\\mods\\' + mid
        for f in list(fs) + ['mod.json']:
            if w.exists(md + '\\' + f):
                w.move(md + '\\' + f, keep + '\\' + mid + '\\' + f)
        for f, local in fs.items():
            w.upload(local, md + '\\' + f)
    w.write(W.ROOT + r'\mods\multibar-probe\mod.json', json.dumps({
        'id': 'multibar-probe', 'name': 'multibar probe (test)', 'version': '0.0.1', 'kind': 'code', 'api': 2, 'scope': 'client',
        'game_builds': ['25846389'], 'platforms': {'win-x64': {'entry': 'multibar-probe.dll', 'files': {
            'multibar-probe.dll': hashlib.sha256(open(pd, 'rb').read()).hexdigest()}}}}, indent=1).encode())
    return keep


def swap_back(w, keep):
    w.kill_all()
    for mid in IDS:
        md = W.ROOT + '\\mods\\' + mid
        for f in (mid + '.dll', 'mod.json'):
            if w.exists(md + '\\' + f):
                w.move(md + '\\' + f, keep + '\\test_' + mid + '\\' + f)
            if w.exists(keep + '\\' + mid + '\\' + f):
                w.move(keep + '\\' + mid + '\\' + f, md + '\\' + f)
    print('test mods moved aside, installed ones (if any) back in place', flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mods', required=True); ap.add_argument('--out', default='multibar_vm_out')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    w = W.Win(); w.up()
    M.T = VmT(w)
    keep = swap_in(w, a.mods, a.out)
    S = {}
    try:
        try:
            w.prepare(lang=0, consent='no', active=['multiplayer', 'multi-bar', 'multibar-probe'], settings={})
            w.start(env={'DSL_CLOUD': '0'})
            L = M.dlog()
            M.check('module ready (every name found, 17 hooks)', 'multi-bar: ready (17 hooks' in L, [l for l in L.splitlines() if 'multi-bar' in l][-6:])
            M.check('probe ready', 'multibar-probe: ready' in L, [l for l in L.splitlines() if 'probe' in l][-3:])
            M.T.load_world(a.out)
            S = M.flow()
        except Exception as e:
            M.check('run finished', False, repr(e))
        finally:
            w.logs(a.out)
            w.stop()
    finally:
        swap_back(w, keep)
        S['checks'] = M.checks
        json.dump(S, open(os.path.join(a.out, 'summary.json'), 'w'), indent=1)
    fails = [c for c in M.checks if not c['ok']]
    for c in M.checks: print(('PASS ' if c['ok'] else 'FAIL ') + c['name'])
    print(f'\nRESULT {len(M.checks) - len(fails)} PASS {len(fails)} FAIL; out {a.out}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
