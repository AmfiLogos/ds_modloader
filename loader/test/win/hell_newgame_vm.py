#!/usr/bin/env python3
"""Windows VM check (player report 2026-10-20, ZET-U): with hell-difficulty on, a new game with COMBAT difficulty Hell (5)
stayed on the loading screen ([EnvHelper] no environment counts for '5' -> KeyNotFoundException in
CampaignLoadingEventHandler.InitEnvironmentData). Starts new games through the multiplayer test command
mpm_newcampaign (needs a multiplayer.dll with its combat / mgmt arguments) and checks each reaches the world.

Driven by winrig (ds_multiplayer win/winbot, WINBOT_DIR), VM running with the agent up. The test DLLs replace the
installed ones for the run; the installed files are moved aside and moved back after (never deleted).
  python3 hell_newgame_vm.py --mods <build/mods_all/win> [--hell-dll <dll>] [--expect-hang] [--out DIR] [c:m ...]
--hell-dll: another hell-difficulty.dll (an old build: the reproduction, with --expect-hang). Exit 0 = as expected."""
import argparse, hashlib, json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402

KEEP = r'C:\dsm\ToDelete'


def swap_in(w, mods, hell_dll):
    """test multiplayer + hell-difficulty in the profile's mods folder; returns the folder with the installed files"""
    keep = KEEP + r'\hell_vm_' + time.strftime('%Y%m%d-%H%M%S')
    w.kill_all()
    for mid, dll in (('multiplayer', None), ('hell-difficulty', hell_dll)):
        md = W.ROOT + '\\mods\\' + mid
        src = os.path.join(mods, mid)
        mj = json.load(open(os.path.join(src, 'mod.json')))
        files = list(mj['platforms']['win-x64'].get('files', {}))
        for f in files + ['mod.json']:
            if w.exists(md + '\\' + f):
                w.move(md + '\\' + f, keep + '\\' + mid + '\\' + f)
        for f in files:
            local = dll if (dll and f.endswith('.dll')) else os.path.join(src, f)
            mj['platforms']['win-x64']['files'][f] = hashlib.sha256(open(local, 'rb').read()).hexdigest()
            w.upload(local, md + '\\' + f)
        w.write(md + r'\mod.json', json.dumps(mj, indent=1))
    return keep


def swap_back(w, keep):
    w.kill_all()
    for mid in ('multiplayer', 'hell-difficulty'):
        md = W.ROOT + '\\mods\\' + mid
        mj = w.json_file(keep + '\\' + mid + r'\mod.json') or {}
        files = list(mj.get('platforms', {}).get('win-x64', {}).get('files', {}))
        for f in files + ['mod.json']:
            if w.exists(md + '\\' + f):
                w.move(md + '\\' + f, keep + '\\test_' + mid + '\\' + f)
            if w.exists(keep + '\\' + mid + '\\' + f):
                w.move(keep + '\\' + mid + '\\' + f, md + '\\' + f)
    print('installed multiplayer + hell-difficulty back in place', flush=True)


def case(w, out, cd, md):
    tag = f'c{cd}m{md}'
    w.prepare(lang=0, consent='no', active=['multiplayer', 'hell-difficulty'], settings={})
    w.start(env={'DSL_CLOUD': '0'})
    dl = w.text(W.PROF + r'\dsload.log') or ''
    ready = 'Hell tables ready' in dl and 'Hell difficulty ready' in dl
    print(tag, 'hell ready', ready, [l for l in dl.splitlines() if 'hell-difficulty' in l and ('OFF' in l or 'fail' in l)][:3], flush=True)
    print(tag, w.cmd(cmd='mpm_newcampaign', name='Hell ' + tag, scenario=0, combat=cd, mgmt=md), flush=True)
    t0 = time.time(); last = {}; loaded = False
    while time.time() - t0 < 240:
        try:
            last = w.status(); w.cmd(cmd='mpm_skipintro', timeout=10)
        except W.GameError as e:
            last = {'err': str(e)}
        if last.get('scene') == 2 and not last.get('busy') and not (last.get('map_loading') or {}).get('is_loading') and time.time() - t0 > 20:
            loaded = True; break
        time.sleep(2)
    d = os.path.join(out, tag); w.logs(d)
    pl = ''
    try: pl = open(os.path.join(d, 'player.log'), errors='replace').read()
    except OSError: pass
    bad = [l for l in pl.splitlines() if 'Exception' in l or '[EnvHelper]' in l]
    env = [l for l in (w.text(W.PROF + r'\dsload.log') or '').splitlines() if 'Hell environment' in l]
    w.stop()
    print(f'{tag}: loaded {loaded} after {time.time() - t0:.0f} s; errors {bad[:3]}; env hook lines {len(env)}; last '
          f'scene {last.get("scene")} busy {last.get("busy")} loading {(last.get("map_loading") or {}).get("is_loading")}', flush=True)
    return ready, loaded and not bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mods', required=True); ap.add_argument('--hell-dll'); ap.add_argument('--expect-hang', action='store_true')
    ap.add_argument('--out', default='hell_vm_out'); ap.add_argument('cases', nargs='*')
    a = ap.parse_args()
    cases = [tuple(int(x) for x in c.split(':')) for c in a.cases] or [(5, 1), (4, 1), (1, 5)]
    w = W.Win(); w.up()
    keep = swap_in(w, a.mods, a.hell_dll)
    ok = True
    try:
        for cd, md in cases:
            ready, good = case(w, a.out, cd, md)
            want = (not good) if (a.expect_hang and cd == 5) else good
            print(f'{"PASS" if (want and ready) else "FAIL"} c{cd}m{md}: {"loaded" if good else "NOT loaded / errors"} (hell ready {ready})', flush=True)
            ok &= want and ready
    finally:
        swap_back(w, keep)
    print('RESULT', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
