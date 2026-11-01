#!/usr/bin/env python3
"""Windows VM run of the hook-chaining proof (kit 0.1.30, loader/src/dsl_chain.h; Mac proof in ds_multiplayer
ext/nvchain/NOTES.md) through winrig (ds_multiplayer win/winbot, WINBOT_DIR), the VM running visibly with the agent up and
the kit under test installed. The test module chainprobe (loader/test/chainprobe/chainprobe.c, built here for Windows with
llvm-mingw, never shipped) hooks EventSystem.Update, which the loader already hooks (its per-frame tick), calls orig,
and takes itself off after 600 frames. It goes into the profile's mods folder for the run only (moved away after).
  python3 chain_vm.py [--out DIR]
Real breaks it catches on Windows (the 14-byte x64 veneer is the Windows-only part of the chain):
  - the second hook on one function is refused (old loaders: "already hooked"), or the chain order is wrong;
  - a call through the chain crashes the game, or the loader's tick under the probe stops (UI test channel dead);
  - the unhook fails or leaves the probe in the chain; the loader's tick stops after the unhook.
Exit 0 = every check passed."""
import argparse, json, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); T_DIR = os.path.dirname(HERE)
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402

CC = os.path.expanduser('~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang')
SDK = os.path.join(os.path.dirname(T_DIR), 'sdk')
SRC = os.path.join(T_DIR, 'chainprobe', 'chainprobe.c')
MOD = {'id': 'chainprobe', 'name': 'Chain probe (test only)', 'version': '0.1.0', 'author': 'loader tests', 'kind': 'code',
       'api': 3, 'game_builds': ['25846389'], 'platforms': {'win-x64': {'entry': 'chainprobe.dll'}},
       'requires': ['loader>=2.0.0'], 'conflicts': [], 'scope': 'client',
       'description': 'Test module of loader/test/win/chain_vm.py: chains over the loader\'s own hook. Never shipped.'}
TO_DEL = W.WORK + r'\to_delete\chain_vm_' + time.strftime('%Y%m%d-%H%M%S')
checks = []
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:700]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def build(out):
    d = os.path.join(out, 'chainprobe'); os.makedirs(d, exist_ok=True)
    subprocess.run(['nice', '-n', '19', CC, '-ffile-prefix-map=' + os.path.expanduser('~') + '=/src', '-O2', '-Wall', '-I' + SDK,
                    '-shared', '-o', os.path.join(d, 'chainprobe.dll'), SRC, '-static', '-static-libgcc'], check=True)
    json.dump(MOD, open(os.path.join(d, 'mod.json'), 'w'), indent=1)
    return d


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='chain_vm_out')
    a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    probe = build(out)
    w = W.Win(); w.up(); w.kill_all()
    md = W.ROOT + r'\mods\chainprobe'
    try:
        for f in ('chainprobe.dll', 'mod.json'):
            w.upload(os.path.join(probe, f), md + '\\' + f)
        w.prepare(lang=0, consent='no', active=['multiplayer', 'chainprobe'], settings={})
        w.start(env={'DSL_CLOUD': '0'})
        dl = lambda: w.text(W.PROF + r'\dsload.log') or ''
        try: w.wait(lambda: 'chainprobe: 600 frames' in dl(), 'the probe\'s 600 frames', 120)
        except W.GameError as e: log(e)
        L = dl(); lines = L.splitlines()
        hk = [l for l in lines if 'chainprobe: hook=' in l]
        check('the probe\'s hook on EventSystem.Update (already the loader\'s) is accepted', hk and re.search(r'hook=0\b', hk[-1]), hk[-1:])
        ch = [l for l in lines if 'chained over' in l and 'runs ' in l]
        order = ch[-1].split('runs ', 1)[1].strip() if ch else ''
        check('the log shows the chain: the probe first, then the loader, then the game',
              order.startswith('chainprobe > ') and 'the loader' in order and order.endswith('game'), ch[-1:] or [l for l in lines if 'chainprobe' in l][-4:])
        fr = [l for l in lines if 'chainprobe: 600 frames through the chain' in l]
        check('600 frames ran through the chain (no crash) and the probe unhooked itself (unhook=0)', fr and fr[-1].rstrip().endswith('unhook=0'), fr[-1:])
        off = [l for l in lines if 'taken off the chain' in l]
        check('after the unhook the chain runs without the probe (the loader > game)',
              off and 'chainprobe' not in off[-1].split('runs ', 1)[-1] and off[-1].rstrip().endswith('game'), off[-1:])
        time.sleep(5)
        try:
            w.gshot(os.path.join(out, 'win_after_unhook.png'), 'chain_after'); shot = True   # the loader's per-frame UI test channel
        except Exception as e:
            shot = repr(e)
        alive = [p for p in w.procs() if p['pid'] == w.pid]
        check('the loader\'s per-frame tick still runs after the unhook (its UI test channel answers) and the game is alive', shot is True and alive, (shot, len(alive)))
        pl = L + (w.text(W.PROF + r'\player.log') or '') + (w.text(W.DATA + r'\ErrorLog.txt') or '')
        bad = [l for l in pl.splitlines() if any(b in l for b in ('Crash!!!', 'Access violation', 'NullReferenceException', 'OFF by the crash guard'))]
        check('no crash / exception line in the logs', not bad, bad[:4])
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: w.logs(out)
        except Exception as e: log('logs:', e)
        w.stop(); w.kill_all()
        for f in ('chainprobe.dll', 'mod.json'):                      # moved away, never deleted
            try:
                if w.exists(md + '\\' + f): w.move(md + '\\' + f, TO_DEL + '\\' + f)
            except Exception as e: log('move:', e)
        json.dump(checks, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', f'{len(checks) - len(bad)}/{len(checks)} checks')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
