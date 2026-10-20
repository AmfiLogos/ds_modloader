#!/usr/bin/env python3
"""Units Laboratory (custom-modes, a launcher feature) never blocks a multiplayer join (owner 2026-10-20: a Windows player
was refused with "Units Laboratory (host 0.1.10, you 0.1.10): same version, different files"). One leased pair of hidden
loader copies (ULH host, ULG guest; never the owner's game, saves or screen), this worktree's build/.
 A  host Units Laboratory ON, guest has it installed but OFF: the guest joins (no MODSET refusal); both play a fight
    (x10 demo save, each side sends its own units at the nearest enemies): 0 desyncs, combat happened.
 B  guest Units Laboratory ON, same version, other files listed in its mod.json (another win-x64 sha256, as a Windows
    build of another day): joins, plays: 0 desyncs.
 C  host also runs the gameplay mod gcd-cdr, the guest does not: the guest joins by address in the menu and is refused
    (MODSET); the box names gcd-cdr and not Units Laboratory.
Breaks caught: a launcher feature in the HELLO mod set (A, B refused); Units Laboratory state in a normal world's
fingerprint or join-point save (A: desync / broken load); the mod-set check gone for real gameplay mods (C).
  ulnoblock_pair.py [--play 240]
Output: ../test_env/ulnoblock/<time>/ (logs, shots, results.json)."""
import glob, hashlib, json, os, shutil, sys, threading, time, zipfile, random

HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import hostmods_pair as HP  # noqa: E402  (copies, kits, start/stop, guard, checks)
from hostmods_pair import lrig, realrig, c, st, wait, flog, fsize, session_lines, fp_map, order_move, shot, check, log  # noqa: E402

HP.H, HP.G = H, G = 'ULH', 'ULG'
CM, X = 'custom-modes', 'gcd-cdr'
PLAY = int(sys.argv[sys.argv.index('--play') + 1]) if '--play' in sys.argv else 240
HP.OUT = OUT = os.path.join(L, 'test_env', 'ulnoblock', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT, exist_ok=True)
HP.SHOTS = OUT
info = HP.info


def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()


def pkg(i, win_sha=None, name=None):
    """a release-style zip of build/mods/<i>; custom-modes: mod.json lists its two Mac dylibs (real sha256) and a win-x64
       file whose sha256 = win_sha (the copy never checks another platform's files; the HELLO line has them)"""
    src = os.path.join(HP.BUILD, 'mods', i); m = json.load(open(os.path.join(src, 'mod.json'), encoding='utf-8'))
    if win_sha:
        m['platforms']['mac-arm64']['files'] = {f: sha(os.path.join(src, f)) for f in ('custom-modes.dylib', 'custom-modes-mp.dylib')}
        m['platforms']['win-x64']['files'] = {'custom-modes.dll': win_sha}
    zp = os.path.join(OUT, 'pkg', name or f'{i}-{m["version"]}.zip'); os.makedirs(os.path.dirname(zp), exist_ok=True)
    with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as zf:
        for r, _, fs in os.walk(src):
            for f in sorted(fs):
                p = os.path.join(r, f)
                if f != 'mod.json' and not f.startswith('.'):
                    zf.write(p, os.path.join(i, os.path.relpath(p, src)))
        zf.writestr(f'{i}/mod.json', json.dumps(m, indent=1, ensure_ascii=False))
    return zp


def mpjson(x, d):
    json.dump(d, open(os.path.join(lrig.idir(x), 'mp.json'), 'w'))


def host_up(port, api, pk, installed, enabled):
    HP.kit(H, pk, installed, enabled)
    mpjson(H, {'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': 1800})
    HP.room_for(2); HP.start(H, api)
    lrig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host', timeout=1500)
    from demo import make_demo_save                              # ds_multiplayer rig/demo.py: the x10 demo save (short fights)
    work = os.path.join(lrig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'demo_buffed.json'); make_demo_save(src)
    log('host load', c(H, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: st(H).get('det', {}).get('locked'), timeout=900, every=2)


def guest_join(port, api, pk, installed, enabled, tag):
    HP.kit(G, pk, installed, enabled)
    mpjson(G, {'role': 'client', 'host': '127.0.0.1', 'port': port, 'name': 'Guest'})
    rp = fsize(G, 'rig.log'); HP.start(G, api)
    try:
        s = wait(f'{tag}: guest loaded the host world and locked', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1
                                                                        and not s.get('mp', {}).get('needs_state') else None)(st(G)), timeout=900, every=2)
    except RuntimeError:
        s = None
    rej = [l for l in flog(G, 'rig.log', rp).splitlines() if 'refus' in l.lower() or 'MODSET' in l][:5]
    return s, rej


def cm_loaded(x):
    return any((' ' + CM + ' ') in l and ': loaded (' in l for l in flog(x, 'dsload.log').splitlines())


def guest_cm_state():
    return 'loaded' if cm_loaded(G) else 'not loaded'


def play(secs, fight, tag):
    """host and guest give orders to their own units (fight: at the nearest enemies, then back); then the desync checks"""
    us = [u for u in c(H, cmd='list_units')['units'] if not u.get('dead') and 'cell' in u]
    pl = [u for u in us if u.get('faction') == 1]
    near_portal = lambda c_: 446 <= c_[0] <= 472 and 181 <= c_[1] <= 205        # demo save floor 0 exit portal (mp2_test.py)
    en = [u for u in us if u.get('faction') == 3 and not near_portal(u['cell'])]
    mine = {H: pl[:2], G: pl[2:4]}
    for u in mine[G]:
        c(G, cmd='mp_claim', guid=u['guid'])
    for x in (H, G):
        c(x, cmd='ord_hover', n=0); c(x, cmd='det_frame_reset')
    time.sleep(3)
    rp = {x: fsize(x, 'rig.log') for x in (H, G)}
    m0 = {x: st(x).get('mp', {}) for x in (H, G)}; e0 = {x: st(x).get('det', {}).get('tick_errors', 0) for x in (H, G)}
    tick0 = max(st(H)['tick'], st(G)['tick'])
    hp0 = sum(u.get('hp', 0) for u in en)
    d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
    rnd = random.Random(20); orders = {H: 0, G: 0}; t0 = time.time(); k = 0; stuck = [tick0, time.time()]
    while time.time() - t0 < secs:
        realrig._with_leases(lambda d: [l.update(used=time.time()) for p_, l in d.items() if l.get('lane') == 'ulnoblock'])
        for x in (H, G):
            for u in mine[x]:
                if fight and en:
                    tgt = min(en, key=lambda e: d2(u, e))['cell'] if (k // 4) % 2 == 0 else u['cell']
                else:
                    tgt = (u['cell'][0] + rnd.randint(-6, 6), u['cell'][1] + rnd.randint(-6, 6))
                if k % 4 == 0 and order_move(x, u['guid'], tgt):
                    orders[x] += 1
        k += 1
        t = st(H).get('tick', 0)
        if t != stuck[0]:
            stuck[:] = [t, time.time()]
        elif time.time() - stuck[1] > 15:                        # a level-up popup with a time lock paused both (batchmode)
            for x in (H, G):
                log(x, 'popup pause release', c(x, cmd='mp_popup', reason=4, add=0))
            stuck[1] = time.time()
        time.sleep(5)
    time.sleep(3)
    sh, sg = st(H), st(G); mh, mg = sh.get('mp', {}), sg.get('mp', {})
    lines = {kk: [l for l in flog(G, 'rig.log', rp[G]).splitlines() + flog(H, 'rig.log', rp[H]).splitlines() if pat in l]
             for kk, pat in (('desync', 'DESYNC:'), ('full_diff', 'FULL-STATE CHECK DIFFERS'), ('deep_diff', 'deep hash differs'), ('report', 'Desync report saved'))}
    a, b = fp_map(H, tick0), fp_map(G, tick0); com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
    te = {x: s.get('det', {}).get('tick_errors', 0) - e0[x] for x, s in ((H, sh), (G, sg))}
    ids = {u['guid'] for u in en}
    hp1 = sum(u.get('hp', 0) for u in c(H, cmd='list_units')['units'] if u['guid'] in ids and not u.get('dead'))
    stats = {'play_s': secs, 'orders': orders, 'ticks': [tick0, sh.get('tick'), sg.get('tick')], 'fp_common_ticks': len(com), 'fp_first_diff': first,
             'desyncs': {H: mh.get('desyncs', 0) - m0[H].get('desyncs', 0), G: mg.get('desyncs', 0) - m0[G].get('desyncs', 0)},
             'full_ok': mg.get('full_ok'), 'full_fails': mg.get('full_fails'), 'tick_errors': te, 'enemy_hp': [hp0, hp1], **{kk: v[:5] for kk, v in lines.items()}}
    info[f'play_{tag}'] = stats
    check(f'{tag}: both gave orders, the world advanced', orders[H] >= 3 and orders[G] >= 3 and (sg.get('tick') or 0) > tick0 + 100, f"orders {orders}, ticks {stats['ticks']}")
    if fight:
        check(f'{tag}: a fight happened (enemy HP lost)', hp1 < hp0, f'enemy HP {hp0} -> {hp1}')
    check(f'{tag}: 0 desyncs (mp counters, desync / full-state / deep lines, det tick_errors, fp.log equal)',
          not any(stats['desyncs'].values()) and not any(lines.values()) and not any(te.values()) and len(com) >= 100 and first is None
          and not stats['full_fails'], json.dumps({kk: stats[kk] for kk in ('desyncs', 'tick_errors', 'fp_common_ticks', 'fp_first_diff', 'full_ok', 'full_fails')}))


def run(port, api):
    pk = {CM: pkg(CM, win_sha='a' * 64), X: pkg(X)}
    pk_b = dict(pk, **{CM: pkg(CM, win_sha='b' * 64, name=f'{CM}-other-files.zip')})
    # ---- A: host UL on, guest UL installed but off
    host_up(port, api, pk, [CM], ['multiplayer', CM])
    hl = session_lines(H); info['host_lines_A'] = hl
    check('A: host runs Units Laboratory', cm_loaded(H), [l for l in flog(H, 'dsload.log').splitlines() if (' ' + CM + ' ') in l][:3])
    check('A: host gameplay lines without custom-modes', hl is not None and not any(l.startswith(CM + '@') for l in hl), hl)
    s, rej = guest_join(port, api, pk, [CM], ['multiplayer'], 'A')
    check('A: guest WITHOUT Units Laboratory joined (no mod-set refusal)', s is not None and not rej, f'refusals {rej}; guest Units Laboratory {guest_cm_state()}')
    if s:
        info['shot_A'] = shot(G, 'A_guest_joined.png')
        play(PLAY, True, 'A')
    HP.stop(G)
    # ---- B: guest UL on with other files (same version)
    s, rej = guest_join(port, api, pk_b, [CM], ['multiplayer', CM], 'B')
    gl = session_lines(G); info['guest_lines_B'] = gl
    check('B: guest with Units Laboratory ON, same version, other files: joined', s is not None and not rej and guest_cm_state() == 'loaded', f'refusals {rej}; guest Units Laboratory {guest_cm_state()}; lines {gl}')
    if s:
        play(max(60, PLAY // 3), False, 'B')
    HP.stop(G); HP.stop(H)
    # ---- C: a real gameplay mod difference still refuses
    host_up(port, api, pk, [CM, X], ['multiplayer', CM, X])
    HP.kit(G, pk, [CM], ['multiplayer', CM])                     # no mp.json: join by address in the menu
    HP.start(G, api)
    lrig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest', timeout=1500)
    time.sleep(3); rp = fsize(G, 'rig.log')
    for b in ('MULTIPLAYER', 'JOIN_ADDR'):
        c(G, cmd='mpm_press', button=HP.B[b]); time.sleep(1.2)
    c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); c(G, cmd='mpm_press', button=HP.B['CONNECT'])
    try:
        box = wait('C: mod-set box', lambda: next((l for l in flog(G, 'rig.log', rp).splitlines() if 'hostmods box:' in l or 'Could not join' in l), None), timeout=180, every=0.2)
    except RuntimeError:
        box = None
    time.sleep(1.5); info['shot_C'] = shot(G, 'C_guest_refused.png')
    txt = flog(G, 'rig.log', rp); ref = [l for l in txt.splitlines() if 'modset refusal' in l or 'REJ_MODSET' in l or 'MODSET' in l][:3]
    info['box_C'] = box; info['refusal_C'] = ref
    check('C: host with gcd-cdr, guest without: "Could not join" box, the refusal names gcd-cdr, not Units Laboratory',
          box is not None and 'Could not join' in box and bool(ref) and X in ref[0] and 'Units Laboratory' not in ref[0] and CM not in ref[0], f'box {box}; refusal {ref}')


def main():
    if os.path.exists(os.path.join(HP.RIGDIR, 'release.lock')):
        raise SystemExit('refused: ~/ds_multiplayer/rig/release.lock exists (release gate running)')
    t0 = time.time(); pair = realrig.lease('ulnoblock', os.getpid())
    while not pair and time.time() - t0 < 1800:
        time.sleep(15); pair = realrig.lease('ulnoblock', os.getpid())
    if not pair:
        raise SystemExit('no free pair lease for 30 min')
    port = realrig.PAIR_PORTS[pair]; log('lease', pair, 'port', port, 'build', HP.BUILD, 'out', OUT)
    g = threading.Thread(target=HP.guard, daemon=True); g.start()
    res = {}
    try:
        info['owner_prefs_before'] = lrig.check_prefs(restore=False)
        for x in (H, G):
            HP.make_copy(x)
        run(port, 'http://127.0.0.1:9')                          # no release server: nothing is downloaded here
    except BaseException as e:
        check('run finished', False, repr(e))
    finally:
        check('never a visible window on a display, never frontmost (guard)', not HP.violations, HP.violations[:3])
        HP.stop_guard.set()
        for x in (G, H):
            try:
                HP.stop(x)
            except Exception as e:
                log('stop', x, repr(e))
            for f in ('dsload.log', 'dsload.prev.log', 'rig.log', 'player.log', 'fp.log'):
                p = os.path.join(lrig.idir(x), f)
                if os.path.exists(p):
                    shutil.copy2(p, os.path.join(OUT, f'{x}_{f}'))
        realrig.release(pair)
        info['owner_prefs_after'] = lrig.check_prefs(restore=False)
        res = {'checks': HP.checks, 'all_pass': all(k['ok'] for k in HP.checks) and bool(HP.checks), 'info': info}
        json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
        log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', os.path.join(OUT, 'results.json'))
    sys.exit(0 if res.get('all_pass') else 1)


if __name__ == '__main__':
    main()
