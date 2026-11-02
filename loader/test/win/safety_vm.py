#!/usr/bin/env python3
"""Start safety on the Windows VM (kit 0.1.32, src/dsl_main.c boot guard + crash guard cascade + dsl_safety_frame; Mac
proof: test/safety_test.py) through winrig (ds_multiplayer win/winbot, WINBOT_DIR), the VM running visibly with the agent up.
The loader under test is the INSTALLED one (the release Setup's baselib.dll). --baselib PATH puts another loader over it
for the run (the installed one is moved to C:\\dsm\\to_delete and put back after; never deleted), for example an older
release's, to see the checks fail.
Mods: the test module ev-probe (test/mods_src/ev-probe, built here with llvm-mingw, never shipped) and ev-dep (the same
code, requires ev-probe). Only these two are on: no multiplayer, so no control port; every wait is on dsload.log lines and
on the Mods window's test driver (DSL_UI_TEST).

The real breaks this catches on Windows:
  boot:  two starts in a row died after the modules started and before the title; the next start must run with mods
         off, reset the count (else every later start counts on and the player is stuck with mods off or in a crash loop)
         and OPEN THE MODS WINDOW BY ITSELF with the reason on its status line, no notice box first (owner 2026-11-02
         "Ok"). Seeded: <profile>\\dsload_state\\_boot.pending + _boot.count=1, DSL_BOOTGUARD=1 (test copies have the
         guard off through DSRIG_CTL). Fails on a loader without the boot guard (kit 0.1.31 and older).
  crash: the crash guard turned a mod off (<profile>\\dsload_state\\ev-probe.pending seeded: as if the game died in its
         init last time); a notice at the title must name it, and ev-dep (requires ev-probe) must not run (else it runs
         half-broken). Fails on a loader without the cascade or the title notice.
  next:  the start after that (no seed, ev-probe.dll unchanged): ev-probe must stay OFF ("is OFF by the crash guard") and
         ev-dep with it. Kit 0.1.32 before 5e759b3 wrote <id>.disabled in text mode on Windows (CRLF), so the file never
         matched: "crash guard reset, trying again" in the same start, the crashed mod ran again at every start.
Each run takes one game frame (DSL_UI_TEST shot) into <out>/<run>/notice_<run>.png for the lead (not a check).
  python3 safety_vm.py [--out DIR] [--only boot|crash (crash = crash + next)] [--baselib PATH]
Exit 0 = every check passed. Last line: RESULT n/m checks."""
import argparse, json, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); T_DIR = os.path.dirname(HERE)
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402

CC = os.path.expanduser('~/tools/llvm-mingw/bin/x86_64-w64-mingw32-clang')
SDK = os.path.join(os.path.dirname(T_DIR), 'sdk')
SRC = os.path.join(T_DIR, 'mods_src', 'ev-probe')
SD = W.PROF + r'\dsload_state'                        # DSL_LOG_DIR (winrig start_env) + \dsload_state
STATE_FILES = [f'{i}.{e}' for i in ('_boot', 'ev-probe', 'ev-dep') for e in ('pending', 'count', 'disabled', 'pending.tmp', 'count.tmp', 'disabled.tmp')]
MODS = ('ev-probe', 'ev-dep')
STAMP = time.strftime('%Y%m%d-%H%M%S')
TO_DEL = W.WORK + r'\to_delete\safety_vm_' + STAMP
BOOT_TEXT = 'The game closed twice in a row while starting with mods'
checks = []
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:700]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def build(out):
    """ev-probe.dll + ev-dep.dll (the same code) and their mod.json files (win-x64 only; ev-dep requires ev-probe)"""
    base = json.load(open(os.path.join(SRC, 'mod.json')))
    dirs = {}
    for mid in MODS:
        d = os.path.join(out, 'mods', mid); os.makedirs(d, exist_ok=True)
        subprocess.run(['nice', '-n', '19', CC, '-ffile-prefix-map=' + os.path.expanduser('~') + '=/src', '-O2', '-Wall', '-I' + SDK,
                        '-shared', '-o', os.path.join(d, mid + '.dll'), os.path.join(SRC, 'ev_probe.c'), '-static', '-static-libgcc', '-lpthread'], check=True)   # clock_gettime: winpthread, linked in
        m = dict(base)
        if mid == 'ev-dep':
            m.update({'id': 'ev-dep', 'name': 'Needs the events probe (test only)', 'requires': ['ev-probe']})
        m['platforms'] = {'win-x64': {'entry': mid + '.dll'}}
        json.dump(m, open(os.path.join(d, 'mod.json'), 'w'), indent=1)
        dirs[mid] = d
    return dirs


def set_aside(w, tag, keep=()):
    """the guard files of an earlier run (moved, never deleted): every run starts from its own seed only (keep: the crash
    run's <id>.disabled for the next start)"""
    for n in STATE_FILES:
        if n in keep: continue
        p = SD + '\\' + n
        if w.exists(p): w.move(p, TO_DEL + f'\\state_{tag}\\' + n)
    for n in ('cmd.json', 'result.json', 'cmd.done.json'):          # a stale test command never runs in the new game
        p = W.UITEST + '\\' + n
        if w.exists(p): w.move(p, TO_DEL + f'\\uitest_{tag}\\' + n)


def launch(w, env):
    """winrig.start() without the control-port wait (no multiplayer here): kill leftovers, park the old logs, windowed check,
    the game started visibly; returns its pid"""
    w.kill_all()
    park = TO_DEL + r'\logs_' + time.strftime('%H%M%S')
    for f in ('starter.log', 'dsload.log', 'player.log', 'player.soft.log', 'rig.log', 'dsload_status.json', r'data\ErrorLog.txt'):
        p = W.PROF + '\\' + f
        if w.exists(p): w.move(p, park + '\\' + f.replace('\\', '_'))
    for d in (W.WORK + r'\fake_gamesaves', W.UITEST):
        w.write(d + r'\.keep', b'')
    if not w.screen_mode_ok(): raise W.GameError('screen mode is not windowed (3): refusing to start')
    a = w.expand(f'-logFile "{W.PROF}\\player.log"')
    r = w.a('run', exe=w.expand(W.GAME + r'\DungeonSettlers.exe'), args=a, cwd=w.expand(W.GAME), env=w.start_env(env), show=True, wait_ms=0)
    w.pid = r['pid']; w.marks = {}
    log('game pid', w.pid)
    return w.pid


def until(w, pred, what, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v: return v
        if not [p for p in w.procs() if p['pid'] == w.pid]: raise W.GameError(f'the game is gone while waiting for {what}')
        time.sleep(0.5)
    raise W.GameError(f'timeout ({timeout} s) waiting for {what}')


def run(w, mode, rdir, mods):
    os.makedirs(rdir, exist_ok=True)
    set_aside(w, mode + '_before', ('ev-probe.disabled',) if mode == 'next' else ())
    if mode != 'next':                                       # next: the same files (a changed file resets the guard)
        for mid, d in mods.items():
            for f in (mid + '.dll', 'mod.json'): w.put(os.path.join(d, f), W.ROOT + f'\\mods\\{mid}\\{f}')
    if mode == 'boot':
        w.write(SD + r'\_boot.pending', 'modules started, title not reached\n')
        w.write(SD + r'\_boot.count', '1\n')
    elif mode == 'crash':
        w.write(SD + r'\ev-probe.pending', 'dsl_plugin_init')
    w.prepare(lang=0, consent='no', active=list(MODS), settings={})
    env = {'DSL_CLOUD': '0'}
    if mode == 'boot': env['DSL_BOOTGUARD'] = '1'
    json.dump({'mode': mode, 'env': env}, open(os.path.join(rdir, 'run.json'), 'w'), indent=1)
    after = {}; L = ''
    dl = lambda: w.text(W.PROF + r'\dsload.log') or ''
    try:
        launch(w, env)
        until(w, lambda: 'SAFETY-NOTICE' in dl() or (mode == 'next' and 'modtime: title' in dl()), 'SAFETY-NOTICE at the title', 240)
        log('title: SAFETY-NOTICE' if 'SAFETY-NOTICE' in dl() else 'title (no SAFETY-NOTICE)')
        after.update({n: w.exists(SD + '\\' + n) for n in ('_boot.pending', '_boot.count')})   # the title cleared them
        if mode == 'boot':                                   # nothing clicks here: the window opens by itself
            until(w, lambda: 'win: Mods window open' in dl().split('SAFETY-NOTICE', 1)[1], 'the Mods window (by itself)', 30)
            res = w.ui({'op': 'state'}, 20)
            after['win_open'] = res.get('open')
            after['status'] = next((c.get('text', '') for c in res.get('controls', []) if c.get('name') == 'status'), '')
            log('window', after['win_open'], 'status:', after['status'])
        elif mode == 'crash':
            try: until(w, lambda: re.search(r'ui: notice \d+ shown', dl().split('SAFETY-NOTICE', 1)[1]), 'the notice box', 20)
            except W.GameError as e: log(e)
        time.sleep(1)
        try: w.gshot(os.path.join(rdir, f'notice_{mode}.png'), f'safety_{mode}')
        except Exception as e: log('shot:', e)
    except Exception as e:
        log('FLOW:', repr(e)); after['flow'] = repr(e)
    finally:
        L = dl()
        open(os.path.join(rdir, 'dsload.log'), 'w').write(L)
        try: w.logs(rdir)
        except Exception as e: log('logs:', e)
        try: w.a('kill', pid=w.pid)
        except Exception: pass
        w.kill_all()
        set_aside(w, mode + '_after', ('ev-probe.disabled',) if mode == 'crash' else ())   # the next start is clean (crash: next reads it)
    return L, after


def parse(text, mode, after):
    out = []
    def chk(name, ok, detail=''): out.append((f'[{mode}] {name}', bool(ok), detail))
    if mode == 'boot':
        chk('boot guard: 2 dead starts in a row -> this start runs with mods off',
            'BOOT GUARD: 2 starts in a row ended before the title: this start runs with mods off' in text,
            [l for l in text.splitlines() if 'BOOT GUARD' in l][:3])
        chk('no mod runs in the mods-off start', not re.search(r'ev-(probe|dep) [0-9.]+: loaded', text),
            re.findall(r'ev-(?:probe|dep) [0-9.]+: loaded[^\n]*', text)[:2])
        chk('the Mods window opens by itself, the reason on its status line, no box before it',
            'SAFETY-NOTICE boot-guard' in text and after.get('win_open') == 1 and after.get('status', '').startswith(BOOT_TEXT)
            and not re.search(r'ui: notice \d+ shown [^\n]*The game closed twice', text),
            {k: after.get(k) for k in ('win_open', 'status', 'flow')})
        chk('the title reset the boot guard (no marker, no count)',
            '_boot.pending' in after and not after.get('_boot.pending') and not after.get('_boot.count'),
            {k: after.get(k) for k in ('_boot.pending', '_boot.count', 'flow')})
    elif mode == 'crash':
        chk('crash guard turned ev-probe off', 'CRASH GUARD: the game stopped last time while module ev-probe ran' in text,
            [l for l in text.splitlines() if 'CRASH GUARD' in l][:2])
        chk('cascade: ev-dep (requires ev-probe) does not run',
            'ev-dep: OFF this session: it requires ev-probe, which did not start' in text and not re.search(r'ev-dep [0-9.]+: loaded', text),
            [l for l in text.splitlines() if 'ev-dep' in l][:4])
        chk('the title notice names the mod (Mods / OK)',
            re.search(r'SAFETY-NOTICE crash-guard: (Loader events probe \(test only\)|ev-probe)', text)
            and re.search(r'ui: notice \d+ shown \(two buttons\): (Loader events probe \(test only\)|ev-probe) stopped the game', text),
            [l for l in text.splitlines() if 'SAFETY-NOTICE' in l or 'ui: notice' in l][:3] or after.get('flow'))
        chk('ev-probe stays off in this start (no "crash guard reset" for its unchanged file)', 'crash guard reset' not in text,
            [l for l in text.splitlines() if 'crash guard reset' in l][:2])
    if mode == 'next':
        chk('next start: ev-probe stays OFF (file unchanged), no "crash guard reset"',
            'module ev-probe is OFF by the crash guard' in text and 'crash guard reset' not in text and not re.search(r'ev-probe [0-9.]+: loaded', text),
            [l for l in text.splitlines() if 'crash guard' in l.lower() or re.search(r'ev-probe [0-9.]+: loaded', l)][:3] or after.get('flow'))
        chk('next start: ev-dep (requires ev-probe) still does not run',
            'ev-dep: OFF this session: it requires ev-probe, which did not start' in text and not re.search(r'ev-dep [0-9.]+: loaded', text),
            [l for l in text.splitlines() if 'ev-dep' in l][:3])
    bad = [l for l in text.splitlines() if any(b in l for b in ('Crash!!!', 'Access violation'))]
    chk('no crash line in dsload.log', not bad, bad[:3])
    return out


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='safety_vm_out'); ap.add_argument('--baselib')
    ap.add_argument('--only', choices=('boot', 'crash'))
    a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    mods = build(out)
    w = W.Win(); w.up(); w.kill_all()
    bl = W.GAME + r'\baselib.dll'; kept = TO_DEL + r'\baselib.dll'; swapped = False
    try:
        if a.baselib:
            w.move(bl, kept); swapped = True
            w.upload(os.path.abspath(os.path.expanduser(a.baselib)), bl)
            log('loader under test:', a.baselib)
        for mode in ({'boot': ['boot'], 'crash': ['crash', 'next']}[a.only] if a.only else ['boot', 'crash', 'next']):
            L, after = run(w, mode, os.path.join(out, mode), mods)
            for n, ok, d in parse(L, mode, after): check(n, ok, d)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        w.kill_all()
        try: set_aside(w, 'end')
        except Exception as e: log('state:', e)
        for mid in MODS:                                             # moved away, never deleted
            for f in (mid + '.dll', 'mod.json'):
                p = W.ROOT + f'\\mods\\{mid}\\{f}'
                try:
                    if w.exists(p): w.move(p, TO_DEL + f'\\mods\\{mid}\\{f}')
                except Exception as e: log('move:', e)
        if swapped:
            try: w.move(bl, TO_DEL + r'\baselib_test.dll'); w.move(kept, bl); log('installed baselib.dll put back')
            except Exception as e: log('BASELIB NOT PUT BACK:', e)
        json.dump(checks, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', f'{len(checks) - len(bad)}/{len(checks)} checks')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
