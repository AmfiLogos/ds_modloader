#!/usr/bin/env python3
"""Hell always loaded (owner 2026-10-25): a host that runs Hell difficulty and a guest WITHOUT it play one world that is
not on Hell, with no desync. On a leased pair of hidden loader copies (never the owner's game, saves or screen).

Host HMH: multiplayer + hell-difficulty installed (Hell not switched on in the profile: it runs anyway). Guest HMG:
multiplayer only (Hell not installed). The host loads the seed world (combat / management levels below Hell), hosts;
the guest joins by address (title > Multiplayer > Join by address > Connect). Then both give orders for --play seconds.
Breaks caught: Hell's presence changes the simulation of a world on the stock levels (desync with a stock guest), or the
host's multiplayer set keeps Hell for that world (the guest without Hell is refused).
Checks: Hell runs on the host, the host's mod-set lines leave Hell out on this world, the guest joins with no refusal,
0 desyncs (mp desyncs, DESYNC / FULL-STATE / deep lines, det tick_errors, fp.log equal).

  hell_mp_pair.py [--play 240]

Copies, lease, guard, kit layout and play loop: hostmods_pair.py (its helpers, imported). Build: HM_BUILD (default
../build: dsload.dylib + mods/multiplayer + mods/hell-difficulty). Output: ../test_env/hostmods/<time>/ (results.json)."""
import json, os, shutil, sys, tempfile, threading, time
HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(HERE)
BUILD = os.environ.get('HM_BUILD', os.path.join(L, 'build'))
os.environ['HM_BUILD'] = tempfile.mkdtemp()                    # hostmods_pair's dev-key check is for its downloads: none here
sys.path.insert(0, HERE)
import hostmods_pair as hm  # noqa: E402
hm.BUILD = BUILD
lrig, realrig = hm.lrig, hm.realrig
H, G, HELL = hm.H, hm.G, 'hell-difficulty'
check, log, c, st, wait, flog, fsize = hm.check, hm.log, hm.c, hm.st, hm.wait, hm.flog, hm.fsize


def kit(x, with_hell):
    hm.kit(x, {}, [], ['multiplayer'])
    if with_hell:
        shutil.copytree(os.path.join(BUILD, 'mods', HELL), os.path.join(lrig.idir(x), 'kit', 'loader', 'mods', HELL))


def run(port):
    kit(H, True); kit(G, False)
    hdr = json.load(open(hm.SAVE, encoding='utf-8-sig')).get('CampaignSaveHeader', {})
    lv = (hdr.get('CombatDifficulty'), hdr.get('ManagementDifficulty'))
    check('the seed world is not on Hell', all(isinstance(v, int) and v < 5 for v in lv), lv)
    json.dump({'role': 'host', 'port': port, 'name': 'Host', 'allow_speedup': True, 'allow_pause': True, 'full_check_steps': 1800},
              open(os.path.join(lrig.idir(H), 'mp.json'), 'w'))
    hm.room_for(2)
    api = 'http://127.0.0.1:9'                                   # no update server: nothing is downloaded in this test
    hm.start(H, api)
    lrig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title host', timeout=1500)
    dl = flog(H, 'dsload.log')
    check('host: Hell runs although the profile does not switch it on', f'{HELL}: custom difficulty: runs whenever installed' in dl and 'Hell difficulty ready' in dl,
          [l for l in dl.splitlines() if HELL in l][:5])
    work = os.path.join(lrig.idir(H), 'save_in'); os.makedirs(work, exist_ok=True)
    src = os.path.join(work, 'hell_mp_base.json'); shutil.copy2(hm.SAVE, src)
    log('host load', c(H, cmd='load_save', file=src, det=1))
    wait('host locked', lambda: st(H).get('det', {}).get('locked'), timeout=900, every=2)
    time.sleep(3)
    hl = hm.session_lines(H)
    hm.info['host_lines'] = hl
    check('host on a world not on Hell: its mod-set lines leave Hell out', hl is not None and not any(l.startswith(HELL + '@') for l in hl), hl)

    hm.start(G, api)
    lrig.wait_for(G, lambda s: s.get('scene') == 1 and not s.get('busy'), 'title guest', timeout=1500)
    time.sleep(3)
    check('guest: Hell not installed', not os.path.exists(os.path.join(lrig.idir(G), 'kit', 'loader', 'mods', HELL)))
    rpos = fsize(G, 'rig.log')
    for b in ('MULTIPLAYER', 'JOIN_ADDR'):
        c(G, cmd='mpm_press', button=hm.B[b]); time.sleep(1.2)
    c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); c(G, cmd='mpm_press', button=hm.B['CONNECT'])
    log('guest: join by address sent')
    sg = wait('guest joined, loaded and locked', lambda: (lambda s: s if s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1
                                                             and not s.get('mp', {}).get('needs_state') else None)(st(G)), timeout=900, every=1)
    refusal = [l for l in flog(G, 'rig.log', rpos).splitlines() if 'modset refusal' in l or 'REJ_MODSET' in l]
    check('guest without Hell joins the Hell host with no refusal', sg and not refusal, refusal[-1:])
    hm.players('start', (H, G))

    # ---- play: both give orders (hostmods_pair's loop)
    time.sleep(5)
    hm.info['joined_shot'] = hm.shot(G, 'hell_mp_guest_joined.png')
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
    orders = {H: 0, G: 0}; t0 = time.time(); k = 0
    import random
    rnd = random.Random(25)
    while time.time() - t0 < hm.PLAY:
        realrig._with_leases(lambda d: [l.update(used=time.time()) for p_, l in d.items() if l.get('lane') == 'hostmods'])
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
             'partials': mg.get('partials', 0) - m0[G].get('partials', 0), 'full_ok': mg.get('full_ok'), 'full_fails': mg.get('full_fails'),
             'tick_errors': te, **{kk: v[:5] for kk, v in lines.items()}}
    hm.info['play'] = stats
    check('play: both gave orders, the world advanced', orders[H] >= 3 and orders[G] >= 3 and (sg.get('tick') or 0) > tick0 + 100, f"orders {orders}, ticks {stats['ticks']}")
    check('play: 0 desyncs between the Hell host and the stock guest (mp counters, desync / full-state / deep lines, det tick_errors, fp.log equal)',
          not any(stats['desyncs'].values()) and not any(lines.values()) and not any(te.values()) and len(com) >= 200 and first is None
          and not stats['full_fails'], json.dumps({kk: stats[kk] for kk in ('desyncs', 'tick_errors', 'fp_common_ticks', 'fp_first_diff', 'full_ok', 'full_fails')}))
    hm.players('end', (H, G))


def main():
    if os.path.exists(os.path.join(hm.RIGDIR, 'release.lock')):
        raise SystemExit('refused: ~/ds_multiplayer/rig/release.lock exists (release gate running)')
    for x in (H, G):
        if hm.pids(x):
            raise SystemExit(f'copy {x} already running: {hm.pids(x)}')
    t0 = time.time(); pair = realrig.lease('hostmods', os.getpid())
    while not pair and time.time() - t0 < 1800:
        time.sleep(15); pair = realrig.lease('hostmods', os.getpid())
    if not pair:
        raise SystemExit('no free pair lease for 30 min')
    port = realrig.PAIR_PORTS[pair]
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
