#!/usr/bin/env python3
"""Windows counterpart of loader/test/l1_bie_test.py, on the real Windows VM (VMware Fusion, ~/ds_multiplayer/VMWARE.md).

Question: on real Windows, does a BepInEx 6 IL2CPP (be.788) Harmony postfix on Refactor.Main.MainFlow.Update work
together with the multiplayer module's hook on the same function (the loader's MinHook engine, start/win/dsl_engine_win.c)?
  run 0  chaintest.exe (no game, seconds): the loader's engine + BepInEx's Dobby detour on one function, ours first and
         BepInEx first: both run, no crash (chaintest/chaintest.c).
  run 1  mods multiplayer + samefn-bie (BepInEx plugin, Harmony postfix on MainFlow.Update), fresh BepInEx in the game
         copy: both hooks fire (multiplayer frame counter + Harmony postfix count), no "refused" line for that hook,
         the game runs and leaves no crash report. Recorded (INFO): which engine patched the function first, the
         function's bytes before and after Harmony, and whether the loader's trampoline returns inside Harmony's patch.
  run 2  multiplayer only (the plugin turned off): Doorstop off, BepInEx does not start, the plugin folder left
         BepInEx\\plugins, the multiplayer hook still runs.
The VM must already run (this driver never starts, stops or reverts it). It installs the kit into its own test folder
C:\\dsm\\winharmony\\root (the VM's own install and shortcuts stay as they were), refuses to start when the Unity prefs are
not windowed (mode 3) or when a game already runs, collects the logs and kills the game at the end of every run.
Every vmrun call is short; the waits are loops of short calls.

Before: ./stage.sh (build/stage.zip, build/wh_guest.ps1).   Usage: python3 winharmony_test.py [out_dir]
Prints PASS/FAIL per check (INFO lines are records, not checks); exit 1 on any FAIL. Output: <out>/ (logs, summary.json)."""
import json, os, re, subprocess, sys, time, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(HERE, 'build')
OUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(B, 'out', time.strftime('%Y%m%d-%H%M%S'))
V = '/Applications/VMware Fusion.app/Contents/Library/vmrun'
X = '/Volumes/XTRA/Windows 11 64-bit Arm.vmwarevm/Windows 11 64-bit Arm.vmx'
GH = 'C:\\dsm\\winharmony'
PS = 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe'
RUN1_LIMIT = int(os.environ.get('WH_RUN1_S', '1500'))      # first BepInEx start: interop generation under x64 emulation
RUN2_LIMIT = int(os.environ.get('WH_RUN2_S', '300'))

checks, info = [], []


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=None):
    checks.append({'name': name, 'ok': bool(ok), 'detail': detail})
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok or detail is None else f'  [{str(detail)[:400]}]'), flush=True)


def note(name, val):
    info.append({'name': name, 'value': val})
    print(f'INFO {name}: {str(val)[:600]}', flush=True)


def secret(*args):
    return subprocess.run(['security', 'find-generic-password', *args, '-w'], capture_output=True, text=True, check=True).stdout.strip()


AUTH = None


def vm(*args, guest=True, timeout=85):
    """one vmrun call (passwords from the Keychain, never printed); (exit code, output)"""
    global AUTH
    if AUTH is None:
        try:
            vp = secret('-s', X)
        except subprocess.CalledProcessError:
            vp = ''                                        # the VM is no longer encrypted: no VM password
        AUTH = (vp, secret('-s', 'vmware-win-tester', '-a', 'jamesjackson'))
    a = [V, '-T', 'fusion'] + (['-vp', AUTH[0]] if AUTH[0] else []) + (['-gu', 'jamesjackson', '-gp', AUTH[1]] if guest else [])
    cmd = a + [args[0], X] + list(args[1:]) if args[0] != 'list' else [V, '-T', 'fusion', 'list']
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        o = p.stdout + p.stderr
        for x in AUTH:
            if x: o = o.replace(x, '***')
        return p.returncode, o
    except subprocess.TimeoutExpired:
        return 124, 'vmrun timed out'


def put(src, dst):
    rc, o = vm('copyFileFromHostToGuest', src, dst, timeout=85)
    if rc: raise RuntimeError(f'copy to guest {dst}: {o.strip()}')


def get(src, dst):
    return vm('copyFileFromGuestToHost', src, dst, timeout=60)[0] == 0


def procs():
    """[(pid, cmd)] of every guest process"""
    rc, o = vm('listProcessesInGuest', timeout=60)
    return [(int(m.group(1)), m.group(2)) for m in re.finditer(r'pid=(\d+), owner=[^,]*, cmd=(.*)', o)]


def games():
    """[(pid, cmd)] of DungeonSettlers.exe in the guest"""
    return [(p, c) for p, c in procs() if 'dungeonsettlers.exe' in c.lower()]


def ours(cmd):
    return '\\winharmony\\root\\' in cmd.lower()


NSTEP = [0]


def our_pids():
    """pids of the game / crash handler / starter running from the test folder, seen in the guest (Get-Process .Path).
    vmrun listProcessesInGuest did not show the test game's folder (VM 2026-10-18: "no game of ours runs" while it ran)."""
    NSTEP[0] += 1
    try:
        return step('procs', f'{time.strftime("%H%M%S")}n{NSTEP[0]}', limit=60).get('pids') or []
    except Exception as e:
        log('procs step failed:', e); return None


def kill_ours():
    """our game, and whatever else runs from the test folder (Unity's crash handler, the starter); waits until gone"""
    NSTEP[0] += 1
    try:
        r = step('kill', f'{time.strftime("%H%M%S")}k{NSTEP[0]}', limit=90)
        log(f"killed {r.get('killed')}, still running {r.get('left')}")
    except Exception as e:
        log('kill step failed:', e)
    for pid, cmd in procs():
        if ours(cmd):
            rc, o = vm('killProcessInGuest', str(pid), timeout=60)
            log(f'killed pid {pid} ({cmd[:80]})' if rc == 0 else f'kill {pid} failed: {o.strip()}')


def step_cmd(name, tag, args=''):
    """program + one argument string for runProgramInGuest. Through cmd's start: vmrun hands the arguments to the
    program as one quoted string (VM 2026-10-19: powershell started directly never ran the step); '& rem' takes the
    stray end quote."""
    return ('C:\\Windows\\System32\\cmd.exe',
            f'/c start "" /min {PS} -NoProfile -ExecutionPolicy Bypass -WindowStyle Minimized -File {GH}\\wh_guest.ps1 '
            f'-Step {name} -Tag {tag} {args}'.strip() + ' & rem')


def step(name, tag, args='', limit=600):
    """run wh_guest.ps1 -Step name on the guest desktop (interactive, no wait), then poll for its JSON"""
    rc, o = vm('runProgramInGuest', '-noWait', '-interactive', *step_cmd(name, tag, args))
    if rc: raise RuntimeError(f'runProgramInGuest {name}: {o.strip()}')
    dst = os.path.join(OUT, f'{name}-{tag}.json'); t0 = time.time()
    while time.time() - t0 < limit:
        time.sleep(4)
        if get(f'{GH}\\out\\{name}-{tag}.json', dst):
            return json.load(open(dst, encoding='utf-8-sig'))
    raise RuntimeError(f'step {name}: no result after {limit} s')


def read(p):
    try:
        return open(p, encoding='utf-8', errors='replace').read()
    except OSError:
        return ''


def collect(tag):
    r = step('collect', tag, limit=120)
    z = os.path.join(OUT, f'collect-{tag}.zip')
    if get(f'{GH}\\out\\collect-{tag}.zip', z):
        d = os.path.join(OUT, tag); os.makedirs(d, exist_ok=True)
        zipfile.ZipFile(z).extractall(d)
    return r


def lines(text, *keys):
    return [l for l in text.splitlines() if all(k in l for k in keys)]


def new_crashes(c, start_utc):
    return [d for d in c.get('crash_dirs') or [] if d.get('utc', '') > start_utc]


def wait_log(guest_file, local, pred, limit, alive_after=90):
    """poll one guest file until pred(text) (run 1: the postfix lines = MainFlow.Update runs = the game reached its frame
    loop; BepInEx's first start spends minutes generating interop assemblies before that); stops early when no game of
    ours runs for alive_after s"""
    t0 = time.time(); seen = None; text = ''
    while time.time() - t0 < limit:
        time.sleep(10)
        if get(guest_file, local): text = read(local)
        if pred(text): return text, True
        pids = our_pids()
        if pids or pids is None: seen = time.time()
        elif time.time() - t0 > alive_after and (seen is None or time.time() - seen > 30):
            log('no game of ours runs'); return text, False
    return text, False


def main():
    os.makedirs(OUT, exist_ok=True)
    summary = {'out': OUT}
    for f in ('stage.zip', 'wh_guest.ps1'):
        if not os.path.exists(os.path.join(B, f)):
            print(f'FAIL staged files: run ./stage.sh first ({f} missing)'); return 1
    rc, o = vm('list', guest=False)
    if X not in o:
        print('FAIL VM running: the VM is not running (this driver never starts it; the VM lane does)'); return 1
    g = games()
    if g:
        print(f'FAIL no game running in the VM before the test: {g[:2]}'); return 1
    try:
        vm('createDirectoryInGuest', 'C:\\dsm', timeout=60); vm('createDirectoryInGuest', GH, timeout=60)
        log('copying stage.zip (~35 MB) and wh_guest.ps1')
        put(os.path.join(B, 'stage.zip'), f'{GH}\\stage.zip'); put(os.path.join(B, 'wh_guest.ps1'), f'{GH}\\wh_guest.ps1')
        tag = time.strftime('%H%M%S')
        log('setup: install the kit into the test folder (a first copy of the game takes a few minutes)')
        s = step('setup', tag, limit=900)
        if get(f'{GH}\\out\\install-{tag}.log', os.path.join(OUT, 'install.log')): pass
        check('setup: Unity prefs windowed (mode 3), kit installed, test mod + BepInEx zip seeded', s.get('ok'), s.get('why') or s.get('install_tail'))
        if not s.get('ok'): return 1
        note('setup', {k: s.get(k) for k in ('reg_mode', 'usersetting_screen_type', 'kept_game_copy', 'game_build', 'mods')})

        # ---- run 0: chaintest.exe (no game): the engine and BepInEx's Dobby detour on one function, both orders
        ch = step('chain', tag + 'c', limit=120)
        for l in ch.get('log') or []: print('  chaintest:', l, flush=True)
        check('run 0: chaintest.exe, both orders (ours then BepInEx, BepInEx then ours) chain with no crash', ch.get('ok'), (ch.get('exit'), ch.get('why'), (ch.get('log') or [])[-3:]))
        summary['chaintest'] = ch.get('log')

        # ---- run 1: multiplayer + samefn-bie
        st = step('start', tag + 'a', '-Ids multiplayer,samefn-bie', limit=60)
        check('run 1 started (prefs still mode 3)', st.get('ok'), st.get('why'))
        if not st.get('ok'): return 1
        t1 = time.time()
        sl = os.path.join(OUT, 'samefn_win.peek.log')
        log(f'run 1: waiting for BepInEx (first start generates the interop assemblies) and 2 postfix lines, up to {RUN1_LIMIT} s')
        _, ok = wait_log(f'{GH}\\root\\game\\BepInEx\\samefn_win.log', sl, lambda t: len(lines(t, 'SAMEFN harmony postfix')) >= 2, RUN1_LIMIT, alive_after=180)
        summary['run1_to_postfix_s'] = round(time.time() - t1, 1)
        time.sleep(5)
        c = collect(tag + 'a')
        d = os.path.join(OUT, tag + 'a')
        L, R, S, BL = (read(os.path.join(d, f)) for f in ('dsload.log', 'rig.log', 'samefn_win.log', 'LogOutput.log'))
        hk = [l for l in L.splitlines() if re.search(r'MainFlow\.Update: hooked 0x[0-9a-f]+ for multiplayer', l)]
        ref = [l for l in (L + '\n' + R).splitlines() if 'refused' in l and 'MainFlow.Update' in l]
        check('run 1: multiplayer hooks MainFlow.Update through the loader engine', hk, L[-600:])
        note('run 1 engine thunk block (must be near GameAssembly)', lines(L, 'engine: MinHook'))
        check('run 1: no "refused" line for the MainFlow.Update hook', not ref and L, ref[:2])
        # known cause when this fails (2026-10-18): Doorstop sets DOORSTOP_DISABLE=TRUE inside the game process once its hooks
        # are in, and dsl_reg.c (Windows) reads that variable as "BepInEx is off" (the plugin logs the value it sees)
        check('run 1: the loader counts 1 BepInEx plugin running', '1 BepInEx plugin(s) running' in L, lines(L, 'running gameplay') + lines(S, 'DOORSTOP_DISABLE'))
        check('run 1: Harmony patched MainFlow.Update', 'SAMEFN harmony patched MainFlow.Update' in S, (S or BL)[-600:])
        np = len(lines(S, 'SAMEFN harmony postfix'))
        check('run 1: Harmony postfix fires (>= 2 log lines = > 600 calls)', np >= 2, np)
        f1, f2 = c.get('frames1', -1), c.get('frames2', -1)
        check('run 1: multiplayer hook fires too (its frame counter moves)', f2 > f1 > 0, (f1, f2))
        check('run 1: game alive, no crash report', c.get('game_alive') and not new_crashes(c, st['start_utc']), (c.get('game_alive'), new_crashes(c, st['start_utc'])))
        summary.update(run1_frames=(f1, f2), run1_postfix_lines=np, run1_refused_lines_all=len(lines(L, 'refused')))
        for k in ('SAMEFN site ', 'SAMEFN first:', 'SAMEFN site after', 'SAMEFN overlap', 'SAMEFN harmony detour on', 'SAMEFN harmony patch FAILED', 'SAMEFN describe'):
            for l in lines(S, k): note('run 1 plugin', l)
        if hk: note('run 1 dsload hook line (time = when the loader wrote it)', hk[0])
        pt = lines(S, 'SAMEFN harmony patched')
        if pt: note('run 1 Harmony patch line (time)', pt[0])
        cfg = read(os.path.join(d, 'BepInEx.cfg'))
        m = re.search(r'^\s*DetourProviderType\s*=\s*(\S+)', cfg, re.M)
        note('run 1 BepInEx detour provider (BepInEx.cfg)', m.group(1) if m else 'not in the config')
        note('run 1 refused lines (any hook)', lines(L, 'refused')[:5])
        note('run 1 crash lines (rig.log crash handler)', lines(R, 'CRASH')[:2] + [l for l in R.splitlines() if 'dobby.dll' in l][:3])
        chained = bool(hk) and not ref and np >= 2 and f2 > f1 > 0
        summary['run1_verdict'] = 'chained: both hooks fire' if chained else 'NOT chained (see FAIL lines)'
        first = next((l.split('SAMEFN first:', 1)[1].strip() for l in lines(S, 'SAMEFN first:')), 'unknown')
        summary['first_engine'] = first
        note('run 1 verdict', f"{summary['run1_verdict']}; first at the site: {first}")
        kill_ours(); time.sleep(5)

        # ---- run 2: the plugin off
        st2 = step('start', tag + 'b', '-Ids multiplayer', limit=60)
        check('run 2 started (prefs still mode 3)', st2.get('ok'), st2.get('why'))
        if not st2.get('ok'): return 1
        t2 = time.time()
        dl = os.path.join(OUT, 'dsload.peek.log')
        log(f'run 2: waiting for the loader line "0 BepInEx plugin(s) running", up to {RUN2_LIMIT} s')
        wait_log(f'{GH}\\root\\profiles\\modded\\dsload.log', dl, lambda t: '0 BepInEx plugin(s) running' in t, RUN2_LIMIT)
        summary['run2_to_loader_s'] = round(time.time() - t2, 1)
        time.sleep(20)
        c2 = collect(tag + 'b')
        d2 = os.path.join(OUT, tag + 'b')
        L2, ST2 = read(os.path.join(d2, 'dsload.log')), read(os.path.join(d2, 'starter.log'))
        bl = lines(ST2, 'bepinex:')
        check('run 2: the starter turned Doorstop off', bl and 'Doorstop off for this start' in bl[-1], bl[-2:])
        check('run 2: the loader counts 0 BepInEx plugins', '0 BepInEx plugin(s) running' in L2, lines(L2, 'running gameplay'))
        s0 = st2['start_utc']
        check('run 2: BepInEx did not start (LogOutput.log and samefn_win.log not written since the start)',
              (c2.get('logoutput_utc') or '') < s0 and (c2.get('samefn_log_utc') or '') < s0, (c2.get('logoutput_utc'), c2.get('samefn_log_utc'), s0))
        check('run 2: the plugin folder left BepInEx\\plugins', 'samefn-bie' not in (c2.get('plugins') or []), (c2.get('plugins'), c2.get('plugins_off')))
        hk2 = [l for l in L2.splitlines() if re.search(r'MainFlow\.Update: hooked 0x[0-9a-f]+ for multiplayer', l)]
        ref2 = [l for l in (L2 + '\n' + read(os.path.join(d2, 'rig.log'))).splitlines() if 'refused' in l and 'MainFlow.Update' in l]
        check('run 2: MainFlow.Update hooked, not refused', hk2 and not ref2, ref2[:2] or L2[-400:])
        f1, f2 = c2.get('frames1', -1), c2.get('frames2', -1)
        check('run 2: multiplayer frame counter moves', f2 > f1 > 0, (f1, f2))
        check('run 2: game alive, no crash report', c2.get('game_alive') and not new_crashes(c2, s0), (c2.get('game_alive'), new_crashes(c2, s0)))
        summary['run2_frames'] = (f1, f2)
    except Exception as e:
        check('run finished', False, e)
    finally:
        try:
            kill_ours()
        except Exception as e:
            log('kill at the end failed:', e)
        summary.update(checks=checks, info=info)
        json.dump(summary, open(os.path.join(OUT, 'summary.json'), 'w'), indent=1, default=str)
        n = sum(c['ok'] for c in checks)
        log(f'{n}/{len(checks)} checks pass; {OUT}')
    return 0 if checks and all(c['ok'] for c in checks) else 1


if __name__ == '__main__':
    sys.exit(main())
