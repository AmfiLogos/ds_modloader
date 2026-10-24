#!/usr/bin/env python3
"""Windows VM run of test/sound_fix_test.py (Sound settings fix): the same flow (title slider + mute, a world, a slider)
through winrig (ds_multiplayer win/winbot, WINBOT_DIR), VM running visibly with the agent up.
  python3 sound_fix_vm.py --mods <build/mods_all/win> [--out DIR] [stock|mod|both]
The test files go into the profile's mods folder for the run (sound-fix from --mods, sound-probe built here with
llvm-mingw); anything installed under those two names is moved aside and moved back after (never deleted).
Exit 0 = every check passed (stock: the game's bug shown; mod: the game's mixer follows the slider)."""
import argparse, hashlib, json, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); T_DIR = os.path.dirname(HERE)
sys.path.insert(0, T_DIR)
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402
import sound_fix_test as S  # noqa: E402

KEEP = r'C:\dsm\ToDelete'
WORK = r'C:\dsm\sound_vm'
CC = os.path.expanduser('~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang')


class VmT:
    def __init__(self, w): self.w = w
    def dlog(self): return self.w.text(W.PROF + r'\dsload.log') or ''
    def req(self, line): self.w.write(W.PROF + r'\sound_probe_req.txt', (line + '\n').encode())
    def load_world(self, out):
        g = WORK + '\\' + os.path.basename(S.MAP)
        self.w.upload(S.MAP, g)
        S.log('load', self.w.cmd(cmd='load_save', file=self.w.expand(g)))
        self.w.wait(self.w.loaded, 'the world loaded', 400)


def probe_dll(out):
    p = os.path.join(out, 'sound-probe.dll')
    subprocess.run([CC, '-O2', '-Wall', '-I', os.path.join(os.path.dirname(T_DIR), 'sdk'), '-shared', '-Wl,--no-insert-timestamp',
                    '-o', p, os.path.join(T_DIR, 'sound_probe.c'), '-Wl,--export-all-symbols', '-static', '-static-libgcc'], check=True)
    return p


def swap_in(w, mods, out):
    keep = KEEP + r'\sound_vm_' + time.strftime('%Y%m%d-%H%M%S')
    w.kill_all()
    pd = probe_dll(out)
    files = {'sound-fix': {f: os.path.join(mods, 'sound-fix', f) for f in ('sound-fix.dll', 'mod.json')},
             'sound-probe': {'sound-probe.dll': pd}}
    for mid, fs in files.items():
        md = W.ROOT + '\\mods\\' + mid
        for f in list(fs) + ['mod.json']:
            if w.exists(md + '\\' + f):
                w.move(md + '\\' + f, keep + '\\' + mid + '\\' + f)
        for f, local in fs.items():
            w.upload(local, md + '\\' + f)
    w.write(W.ROOT + r'\mods\sound-probe\mod.json', json.dumps({
        'id': 'sound-probe', 'name': 'sound probe (test)', 'version': '0.0.1', 'kind': 'code', 'api': 2, 'scope': 'client',
        'game_builds': ['25846389'], 'platforms': {'win-x64': {'entry': 'sound-probe.dll', 'files': {
            'sound-probe.dll': hashlib.sha256(open(pd, 'rb').read()).hexdigest()}}}}, indent=1))
    return keep


def swap_back(w, keep):
    w.kill_all()
    for mid in ('sound-fix', 'sound-probe'):
        md = W.ROOT + '\\mods\\' + mid
        for f in (mid + '.dll', 'mod.json'):
            if w.exists(md + '\\' + f):
                w.move(md + '\\' + f, keep + '\\test_' + mid + '\\' + f)
            if w.exists(keep + '\\' + mid + '\\' + f):
                w.move(keep + '\\' + mid + '\\' + f, md + '\\' + f)
    print('test mods moved aside, installed ones (if any) back in place', flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mods', required=True); ap.add_argument('--out', default='sound_vm_out')
    ap.add_argument('kinds', nargs='?', default='both')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    kinds = {'stock': ['stock'], 'mod': ['mod'], 'both': ['stock', 'mod']}[a.kinds]
    w = W.Win(); w.up()
    S.T = VmT(w)
    keep = swap_in(w, a.mods, a.out)
    try:
        for k in kinds:
            o = os.path.join(a.out, k)
            try:
                w.prepare(lang=0, consent='no', active=['multiplayer', 'sound-probe'] + (['sound-fix'] if k == 'mod' else []), settings={})
                w.start(env={'DSL_CLOUD': '0'})
                time.sleep(3)
                S.flow(k, o)
            except Exception as e:
                S.check(f'{k}: run finished', False, repr(e))
            finally:
                w.logs(o)
                w.stop()
    finally:
        swap_back(w, keep)
        json.dump({'checks': S.checks, 'evidence': S.EVID}, open(os.path.join(a.out, 'summary.json'), 'w'), indent=1)
    fails = [c for c in S.checks if not c['ok']]
    for c in S.checks: print(('PASS ' if c['ok'] else 'FAIL ') + c['name'])
    print(f'\nRESULT {len(S.checks) - len(fails)} PASS {len(fails)} FAIL; out {a.out}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
