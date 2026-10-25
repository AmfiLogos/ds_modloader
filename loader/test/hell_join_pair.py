#!/usr/bin/env python3
"""A guest with Hell joins a host on a Hell world with no refusal and no restart (owner 2026-10-25). On a leased pair of
hidden loader copies (never the owner's game, saves or screen).

Host HMH and guest HMG both run multiplayer + hell-difficulty (the same build files; Hell is not switched on in either
profile: it runs anyway). The host hosts as a player does (Multiplayer > Host > Your save > the seed world set to combat
Hell, level 5; the single-player copy box, if asked: No) on the pair lease's own port (its "host_port" setting;
rigports.py: unique on the Mac, never a fixed pair port that other lanes' stale copies point at). The guest, on the title (its multiplayer set leaves
Hell out there: the case that was refused, then "Get the host's mods" restarted it), joins by address
(title > Multiplayer > Join by address > Connect). Then both give orders for --play seconds. Last, the guest starts again
WITHOUT Hell and joins again: refused, as before.
Breaks caught: the Hell guest refused or restarted (the owner's complaint); its set not counting Hell on the Hell world (a
later rejoin or another check would differ); a desync between two Hell peers on a Hell world; a guest without Hell let
into a Hell world (it would run the world without Hell's code).
Checks: the host's lines carry Hell; the guest's title set leaves Hell out and its HELLO offers it; the guest joins with no
refusal, in the same process (no soft restart), and the host's server says Hell was counted; after the load the guest's set
lines equal the host's; 0 desyncs (mp desyncs, DESYNC / FULL-STATE / deep lines, det tick_errors, fp.log equal); the guest
without Hell is refused (MODSET).

  hell_join_pair.py [--play 180]

Helpers (copies, lease, guard, kit, play loop): hostmods_pair.py, hell_mp_pair.py. Build: HM_BUILD (default ../build:
dsload.dylib + mods/multiplayer + mods/hell-difficulty). Output: ../test_env/hostmods/<time>/ (results.json)."""
import json, os, random, shutil, subprocess, sys, tempfile, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(HERE)
BUILD = os.environ.get('HM_BUILD', os.path.join(L, 'build'))
os.environ['HM_BUILD'] = tempfile.mkdtemp()                    # hostmods_pair's dev-key check is for its downloads: none here
if '--play' not in sys.argv: sys.argv += ['--play', '180']
sys.path.insert(0, HERE)
import hostmods_pair as hm  # noqa: E402
import hell_mp_pair as hp  # noqa: E402
hm.BUILD = hp.BUILD = BUILD
lrig, realrig = hm.lrig, hm.realrig
H, G, HELL = hm.H, hm.G, 'hell-difficulty'
check, log, c, st, wait, flog, fsize = hm.check, hm.log, hm.c, hm.st, hm.wait, hm.flog, hm.fsize
LOAD_MAX = 15.0                                                   # lead's rule for a pair while lanes run
B = dict(hm.B, HOST=2, LOAD_SAVE=7, START=10)                     # src/mpmenu.h ids


def hell_save(dst):
    """the seed world on combat Hell: the save header and the campaign's start setting (the loader reads either)"""
    w = json.load(open(hm.SAVE, encoding='utf-8-sig'))
    w['CampaignSaveHeader']['CombatDifficulty'] = 5
    w['CampaignSettingSaveData']['CampaignStartSettingSaveData']['CombatDifficulty'] = 5
    json.dump(w, open(dst, 'w', encoding='utf-8'), indent=2)


def join(port):
    rpos = fsize(G, 'rig.log')
    for b in ('MULTIPLAYER', 'JOIN_ADDR'):
        c(G, cmd='mpm_press', button=hm.B[b]); time.sleep(1.2)
    c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); c(G, cmd='mpm_press', button=hm.B['CONNECT'])
    log('guest: join by address sent', port)
    return rpos


def run(port):
    hp.kit(H, True); hp.kit(G, True)
    # the host hosts as a player does: the world first (Multiplayer > Host > Your save > the pick), then the session, so
    # its HELLO is about that world (an mp.json host starts its session on the title: its set would leave Hell out)
    hm.rp.set_host_port(lrig.idir(H), port)                      # the menu host listens on this run's port, never 47777
    hm.room_for(2)
    api = 'http://127.0.0.1:9'                                   # no update server: nothing is downloaded in this test
    hm.start(H, api)
    lrig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host', timeout=1500)
    time.sleep(3)
    work = os.path.join(lrig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'hell_join_world.json'); hell_save(src)
    hpos = fsize(H, 'rig.log')
    for b in ('MULTIPLAYER', 'HOST', 'LOAD_SAVE'):
        c(H, cmd='mpm_press', button=B[b]); time.sleep(1.5)
    log('host: pick the Hell world in the list', c(H, cmd='load_save', file=src, det=0))
    def hosting():
        m = c(H, cmd='mpm_status')
        if m.get('solo', {}).get('state') == 2 and m.get('box', {}).get('on'):
            log('host: single-player copy box: No', c(H, cmd='mpm_box', action='cancel'))
        s = st(H)
        return s if s.get('det', {}).get('locked') and s.get('mp', {}).get('started') else None
    wait('host session on the Hell world', hosting, timeout=900, every=2)
    time.sleep(3)
    hl = hm.session_lines(H) or []
    hm.info['host_lines'] = hl
    hs = [l for l in flog(H, 'rig.log', hpos).splitlines() if 'launcher session: rule' in l]
    check("host on a Hell world: its set lines carry Hell, and so does its session's HELLO", any(l.startswith(HELL + '@') for l in hl)
          and hs and '1 gameplay mod(s)' in hs[-1] and c(H, cmd='mpm_status').get('port') == port, {'lines': hl, 'hello': hs[-1:], 'port': c(H, cmd='mpm_status').get('port')})

    hm.start(G, api)
    lrig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest', timeout=1500)
    time.sleep(3)
    gpid = hm.pids(G)
    gl0 = hm.session_lines(G) or []
    check('guest on the title: Hell runs but its set leaves it out (the case that was refused)',
          f'{HELL}: custom difficulty: runs whenever installed' in flog(G, 'dsload.log') and not any(l.startswith(HELL + '@') for l in gl0), gl0)
    dpos = fsize(G, 'dsload.log')
    rpos = join(port)
    sg = wait('guest joined, loaded and locked', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1
                                                             and not s.get('mp', {}).get('needs_state') else None)(st(G)), timeout=900, every=1)
    gtxt = flog(G, 'rig.log', rpos)
    refusal = [l for l in gtxt.splitlines() if 'modset refusal' in l or 'REJ_MODSET' in l]
    offered = [l for l in gtxt.splitlines() if 'also running, not counted now' in l]
    check('guest HELLO offers its loaded Hell', any(HELL + '@' in l for l in offered), offered[-1:])
    counted = [l for l in flog(H, 'rig.log', hpos).splitlines() if 'loaded custom difficulty mods' in l]
    check('guest with Hell joins the Hell host: no refusal, the host counts its Hell', sg and not refusal and any(HELL + '@' in l for l in counted),
          f'refusal {refusal[-1:]}; host {counted[-1:]}')
    hm.players('start', (H, G))
    soft = [l for l in flog(G, 'dsload.log', dpos).splitlines() if 'soft restart' in l.lower() or 'DSMP_REJOIN' in l or 'hostmods' in l]
    check('no restart: the same guest process all along, no soft restart', hm.pids(G) == gpid and gpid and not soft, f'pids {gpid} -> {hm.pids(G)}; {soft[-2:]}')
    c(H, cmd='mpm_press', button=B['START']); time.sleep(3)       # the host's lobby Start: the world runs
    time.sleep(5)                                                 # the loader counts Hell once it sees the world's level
    gl1 = hm.session_lines(G) or []
    check("guest on the host's Hell world: its set lines = the host's (Hell counted)", sorted(gl1) == sorted(hl), {'guest': gl1, 'host': hl})

    # ---- play: both give orders (hell_mp_pair's loop)
    us = [u for u in c(H, cmd='list_units')['units'] if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
    mine_g = [u['guid'] for u in us[:2]]
    for g in mine_g:
        c(G, cmd='mp_claim', guid=g)
    time.sleep(3)
    for x in (H, G):
        c(x, cmd='det_frame_reset')
    rp = {x: fsize(x, 'rig.log') for x in (H, G)}
    m0 = {x: st(x).get('mp', {}) for x in (H, G)}
    e0 = {x: st(x).get('det', {}).get('tick_errors', 0) for x in (H, G)}
    tick0 = max(st(H)['tick'], st(G)['tick'])
    orders = {H: 0, G: 0}; t0 = time.time(); k = 0; rnd = random.Random(25)
    while time.time() - t0 < hm.PLAY:
        realrig._with_leases(lambda d: [l.update(used=time.time()) for p_, l in d.items() if l.get('lane') == 'helljoin'])
        for x in (H, G):
            try:
                uu = [u for u in c(x, cmd='list_units').get('units', []) if u.get('faction') == 1 and not u.get('dead') and 'cell' in u]
            except Exception as e:
                log('list_units', x, repr(e)[:120]); uu = []
            cand = [u for u in uu if (u['guid'] in mine_g) == (x == G)]
            if cand:
                u = rnd.choice(cand)
                if hm.order_move(x, u['guid'], (u['cell'][0] + rnd.randint(-6, 6), u['cell'][1] + rnd.randint(-6, 6))):
                    orders[x] += 1
        k += 1
        if k == max(1, hm.PLAY // 30):
            c(H, cmd='mp_speed', speed=2); c(G, cmd='mp_speed', speed=2); time.sleep(15)
            c(H, cmd='mp_speed', speed=1); c(G, cmd='mp_speed', speed=1)
        time.sleep(12)
    time.sleep(3)
    sh, sg = st(H), st(G)
    mh, mg = sh.get('mp', {}), sg.get('mp', {})
    lines = {kk: [l for l in flog(G, 'rig.log', rp[G]).splitlines() + flog(H, 'rig.log', rp[H]).splitlines() if pat in l]
             for kk, pat in (('desync', 'DESYNC:'), ('full_diff', 'FULL-STATE CHECK DIFFERS'), ('deep_diff', 'deep hash differs'), ('report', 'Desync report saved'))}
    a, b = hm.fp_map(H, tick0), hm.fp_map(G, tick0); com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
    te = {x: s.get('det', {}).get('tick_errors', 0) - e0[x] for x, s in ((H, sh), (G, sg))}
    stats = {'play_s': hm.PLAY, 'orders': orders, 'ticks': [tick0, sh.get('tick'), sg.get('tick')], 'fp_common_ticks': len(com), 'fp_first_diff': first,
             'desyncs': {H: mh.get('desyncs', 0) - m0[H].get('desyncs', 0), G: mg.get('desyncs', 0) - m0[G].get('desyncs', 0)},
             'full_ok': mg.get('full_ok'), 'full_fails': mg.get('full_fails'), 'tick_errors': te, **{kk: v[:5] for kk, v in lines.items()}}
    hm.info['play'] = stats
    check('play: both gave orders, the world advanced', orders[H] >= 3 and orders[G] >= 3 and (sg.get('tick') or 0) > tick0 + 100, f"orders {orders}, ticks {stats['ticks']}")
    check('play: 0 desyncs between the two Hell peers on the Hell world (mp counters, desync / full-state / deep lines, det tick_errors, fp.log equal)',
          not any(stats['desyncs'].values()) and not any(lines.values()) and not any(te.values()) and len(com) >= 200 and first is None
          and not stats['full_fails'], json.dumps({kk: stats[kk] for kk in ('desyncs', 'tick_errors', 'fp_common_ticks', 'fp_first_diff', 'full_ok', 'full_fails')}))

    # ---- a guest WITHOUT Hell: refused, as before
    c(G, cmd='mp_quit', mode='title'); time.sleep(3)
    hm.stop(G)
    hp.kit(G, False)
    hm.start(G, api)
    lrig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest (no Hell)', timeout=1500)
    time.sleep(3)
    rpos = join(port)
    wait('guest without Hell refused', lambda: [l for l in flog(G, 'rig.log', rpos).splitlines() if 'modset refusal' in l], timeout=120, every=1)
    gtxt = flog(G, 'rig.log', rpos)
    refusal = [l for l in gtxt.splitlines() if 'modset refusal' in l]
    check('guest without Hell: refused (MODSET), never let into the Hell world', bool(refusal) and not st(G).get('det', {}).get('locked') and (st(G).get('scene') == 1),
          refusal[-1:])
    if refusal:
        hm.players('end', (H,), 1)                                # the guest is refused: the host alone in its session


def main():
    if os.path.exists(os.path.join(hm.RIGDIR, 'release.lock')):
        raise SystemExit('refused: ~/ds_multiplayer/rig/release.lock exists (release gate running)')
    for x in (H, G):
        if hm.pids(x):
            raise SystemExit(f'copy {x} already running: {hm.pids(x)}')
    t0 = time.time()
    while os.getloadavg()[0] >= LOAD_MAX:
        if time.time() - t0 > 1200: raise SystemExit(f'1-minute load stayed at {LOAD_MAX:.0f} or more for 20 min')
        log(f'load {os.getloadavg()[0]:.1f}: waiting'); time.sleep(60)
    t0 = time.time(); pair = realrig.lease('helljoin', os.getpid())
    while not pair and time.time() - t0 < 1800:
        time.sleep(15); pair = realrig.lease('helljoin', os.getpid())
    if not pair:
        raise SystemExit('no free pair lease for 30 min')
    port = realrig.PAIR_PORTS[pair]                               # the pair lease's own port (rigports.py): unique on the Mac
    busy = subprocess.run(['lsof', '-nP', f'-iTCP:{port}', '-sTCP:LISTEN'], capture_output=True, text=True).stdout.strip()
    if busy: raise SystemExit(f'port {port} in use: {busy[:200]}')
    log('lease', pair, 'port', port, 'build', BUILD, 'out', hm.OUT)
    g = threading.Thread(target=hm.guard, daemon=True); g.start()
    res = {}
    try:
        hm.info['owner_prefs_before'] = lrig.check_prefs(restore=False)
        for x in (H, G):
            hm.make_copy(x)
        run(port)
    except BaseException as e:
        check('run finished', False, repr(e))
    finally:
        check('never a visible window on a display, never frontmost (guard)', not hm.violations, hm.violations[:3])
        hm.stop_guard.set()
        for x in (G, H):
            try:
                hm.stop(x)
            except Exception as e:
                log('stop', x, repr(e))
        for x in (G, H):
            for f in ('dsload.log', 'rig.log', 'player.log', 'stdout.log'):
                p = os.path.join(lrig.idir(x), f)
                if os.path.exists(p):
                    shutil.copy2(p, os.path.join(hm.OUT, f'{x}_{f}'))
        realrig.release(pair)
        hm.info['owner_prefs_after'] = lrig.check_prefs(restore=False)
        res = {'checks': hm.checks, 'all_pass': all(k['ok'] for k in hm.checks) and bool(hm.checks), 'info': hm.info}
        json.dump(res, open(os.path.join(hm.OUT, 'results.json'), 'w'), indent=1, default=str)
        log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', f"{sum(k['ok'] for k in hm.checks)}/{len(hm.checks)}", os.path.join(hm.OUT, 'results.json'))
    sys.exit(0 if res.get('all_pass') else 1)


if __name__ == '__main__':
    main()
