#!/usr/bin/env python3
"""Bring Units CARD window of the multiplayer mod, in Foreign Expedition and in PvP (owner 2026-10-20), on one leased pair
of hidden loader copies (BCH host, BCG guest; never the owner's game, saves or screen). Neither copy has Units Laboratory:
each runs multiplayer + the test-only units-driver (fake mouse, ds_multiplayer ext/units13/driver).
One multiplayer mode since 2026-10-21 (owner: "keep only the Hub: Co-op or PvP"; the separate PvP host choice HOST_C1 and
its PvP world are gone): the hub is hosted, PvP is played from the hub (Esc > PvP Match).
 FE:  host page "Hub: Co-op or PvP" (Death Off) -> hub lobby -> guest joins -> both Bring through the card window (host 1,
      ALONE before the guest joins; guest 2) -> Start -> hub with 3 units -> expedition (Teams) -> fight -> portal -> back on the base map -> 0 desyncs.
 PvP: then, in the hub: Esc > PvP Match (Fight, best of 1, 60 s, this settlement) -> each player presses the window's
      Bring Units: the card window opens (cards with game icons, the player's own saves) -> picks 2 units by clicking
      their ticks -> Bring -> the host has 4 units -> Start Match -> fight vote -> attack orders -> fight -> match end -> 0 desyncs -> Back to the Hub.
Breaks caught: no card window (the text list of the mpsplit build), picks that do not reach the host, a host alone in
its lobby refused ("Not in a multiplayer session", v0.1.19), a desync from the brought units. Frames (game back buffer): <out>/*.png.
  bringcards_pair.py [--dive 420] [--only-open]       (--only-open: stop after the first card window check)
  bringcards_pair.py --compact [LANGTYPE=3]          (one copy, the host alone in the hub lobby: 8 compact cards on a page,
                                                      a click on a card's body ticks / unticks it; LANGTYPE = the game's
                                                      language, 3 de, 4 ru; shots to look at. Fails on the 3-card window
                                                      and on cards that only the small tick selects, before 2026-10-20)
Env: HM_BUILD = loader build folder (dsload.dylib with the dev key + mods/multiplayer), BC_MP = the ds_multiplayer checkout
whose ext/units13/driver and ext/fepvp13/tools are used (default ~/ds_multiplayer).
Output: ../test_env/bringcards/<time>/ (logs, frames, results.json)."""
import json, os, re, shutil, subprocess, sys, threading, time, zipfile

HERE = os.path.dirname(os.path.abspath(__file__)); L = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import hostmods_pair as HP  # noqa: E402  (copies, kits, start/stop, guard, checks)
from hostmods_pair import lrig, realrig, c, st, wait, flog, fsize, check, log  # noqa: E402

HP.H, HP.G = H, G = 'BCH', 'BCG'
A = sys.argv
DIVE = int(A[A.index('--dive') + 1]) if '--dive' in A else 420
ONLY_OPEN = '--only-open' in A
COMPACT = '--compact' in A
LANGTYPE = os.environ.get('LANGTYPE')                      # the game's LanguageType (3 de, 4 ru)
if LANGTYPE:
    _prep = lrig.prepare_data
    def _prep_lang(x):
        _prep(x); us = os.path.join(lrig.idir(x), 'data', 'UserSetting.json'); s = json.load(open(us))
        s['GeneralSettingData']['SaveCurrentLanguageType'] = int(LANGTYPE); json.dump(s, open(us, 'w'), indent=1)
    lrig.prepare_data = _prep_lang
MP = os.path.expanduser(os.environ.get('BC_MP', '~/ds_multiplayer'))
FIX = os.path.expanduser('~/ds_multiplayer/ext/foreign_expedition/fixtures')            # test saves (not in git)
TPL = os.path.expanduser('~/dungeonsettlersdb/site/assets/saves/base_25846389.json')      # hub / PvP base template
HP.OUT = OUT = os.path.join(L, 'test_env', 'bringcards', time.strftime('%Y%m%d-%H%M%S')); os.makedirs(OUT, exist_ok=True)
HP.SHOTS = OUT
info = HP.info
DRV = 'units-driver'
BTN = json.loads(subprocess.run([sys.executable, os.path.join(MP, 'ext/fepvp13/tools/mpm_ids.py'), MP], capture_output=True, text=True).stdout)
SI = dict(HealthHead=12, HealthBody=13, Exp=18, Level=17)


def driver_pkg():
    """the test-only driver as a mod zip (built here, never shipped)"""
    d = os.path.join(OUT, 'pkg', 'src', DRV); os.makedirs(d, exist_ok=True)
    subprocess.run(['nice', '-n', '19', 'clang', '-arch', 'arm64', '-O2', '-Wall', '-Wno-unused-function', '-I' + os.path.join(L, 'sdk'), '-dynamiclib',
                    '-framework', 'CoreFoundation', '-framework', 'CoreGraphics', '-lobjc', '-install_name', f'@rpath/{DRV}.dylib',
                    '-o', os.path.join(d, f'{DRV}.dylib'), os.path.join(MP, 'ext/units13/driver/units_driver.c')], check=True)
    subprocess.run(['codesign', '-f', '-s', '-', os.path.join(d, f'{DRV}.dylib')], capture_output=True)
    shutil.copy2(os.path.join(MP, 'ext/units13/driver/mod.json'), os.path.join(d, 'mod.json'))
    zp = os.path.join(OUT, 'pkg', f'{DRV}.zip')
    with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as zf:
        for f in ('mod.json', f'{DRV}.dylib'):
            zf.write(os.path.join(d, f), os.path.join(DRV, f))
    return {DRV: zp}


# ------------------------------------------------------------------ units-driver (fake mouse in the hidden copy)
seq = {}
def drv_dir(x):
    return os.path.join(OUT, f'drv_{x}')


def req(x, cmd, timeout=20, **kw):
    seq[x] = seq.get(x, 0) + 1
    q = dict(kw, seq=seq[x], cmd=cmd); d = drv_dir(x)
    open(os.path.join(d, 'req.json.tmp'), 'w').write(json.dumps(q)); os.replace(os.path.join(d, 'req.json.tmp'), os.path.join(d, 'req.json'))
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            a = json.load(open(os.path.join(d, 'resp.json')))
            if a.get('seq') == seq[x]:
                return a
        except (OSError, ValueError):
            pass
        time.sleep(0.05)
    raise RuntimeError(f'{x}: no driver answer to {q}')


def click(x, target, pause=1.2):
    a = req(x, 'click', target=target); time.sleep(pause)
    return a.get('rc') == 0 and a.get('on', 0) > 0


def press(x, name):
    r = c(x, cmd='mpm_press', button=BTN[name]); time.sleep(1.2); return r


def bst(x):
    return c(x, cmd='mpm_bring_state')


def open_cards(x, tag, pvp=False):
    """the lobby's Bring Units button (the menu's own press), or the PvP window's (pvp) -> the card window"""
    if not pvp:
        wait(f'{x} lobby', lambda: c(x, cmd='mpm_status').get('state') in ('lobby', 'guest_lobby'), timeout=120)
    s = {}
    for _ in range(6):
        if pvp: c(x, cmd='mpm_pvp', action='bring'); time.sleep(2)
        else: press(x, 'FE_BRINGWIN'); time.sleep(2)
        s = bst(x)
        if s.get('open') == 1:
            break
    time.sleep(2); s = bst(x)                                    # icons load in the frames after the open
    info[f'shot_{tag}'] = HP.shot(x, f'{tag}_cards.png')
    log(x, tag, 'card window', s)
    return s


def pick(x, n, tag):
    """the first save of the left column, then the ticks of its first n cards, then Bring (real clicks)"""
    ok = click(x, 'bring:left0', 1.5)
    for k in range(n):
        ok = click(x, f'bring:tick{k}') and ok
    s = bst(x); info[f'shot_{tag}_picked'] = HP.shot(x, f'{tag}_picked.png')
    ok = click(x, 'bring:bring', 2.0) and ok
    s2 = bst(x)
    log(x, tag, 'picked', s, '| after Bring', s2)
    return ok and s.get('picked') == n and s2.get('open') == 0, s


WORLD = re.compile(r'^(mp_sync|mp_fullcheck|MPFE_|MPHub_|MPPvP_)|_(fe5ea00|fe5eb|b45e0)[0-9a-fA-F]{25,27}(?![0-9a-fA-F])')   # the mod's own worlds


def cards_ok(s):
    """open, cards with game icons, and only real worlds in the save list (no hub / PvP world saves or autosaves)"""
    files = s.get('save_files', [])
    return s.get('open') == 1 and s.get('cards', 0) >= 1 and s.get('card_sprites', 0) >= 3 and s.get('saves', 0) >= 1 \
        and len(files) == s.get('saves') and not any(WORLD.search(f) for f in files)


def fp_read(path, t_from):
    d = {}
    if os.path.exists(path):
        for line in open(path, errors='replace'):
            q = line.split()
            if len(q) >= 2 and q[0].isdigit() and int(q[0]) >= t_from:
                d[int(q[0])] = q[1]
    return d


def desync_lines(rp):
    pats = ('DESYNC:', 'FULL-STATE CHECK DIFFERS', 'deep hash differs', 'Desync report saved')
    return [l for x in (H, G) for l in flog(x, 'rig.log', rp[x]).splitlines() if any(p in l for p in pats)][:5]


def to_title(x):
    lrig.wait_for(x, lambda s: s.get('scene') == 1 and not s.get('busy'), f'title {x}', timeout=1500); time.sleep(4)


def join(port):
    press(G, 'MULTIPLAYER'); press(G, 'JOIN_ADDR'); c(G, cmd='mpm_type', text=f'127.0.0.1:{port}'); press(G, 'CONNECT')
    wait('guest loaded the host world + locked', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('loads', 0) >= 1)(st(G)), timeout=900, every=2)


# ------------------------------------------------------------------ PvP (from the hub)
def hub_pvp():
    """after fe(): both on the hub's base map, paused. Esc > PvP Match, each player's Bring Units (card window), Start Match"""
    for x in (H, G):
        c(x, cmd='mp_pause', on=0)
    wait('both running', lambda: not st(H).get('mp', {}).get('paused') and not st(G).get('mp', {}).get('paused'), timeout=180)
    time.sleep(3)
    for x in (H, G):
        c(x, cmd='mpm_esc'); time.sleep(2); c(x, cmd='mpm_pvp', action='press_esc'); time.sleep(2)
    w = {x: c(x, cmd='mpm_pvp') for x in (H, G)}
    check('PvP 0: the PvP window opens from the Esc menu in the hub (host and guest)', all(w[x].get('window') for x in (H, G)), {x: w[x].get('window') for x in (H, G)})
    if not all(w[x].get('window') for x in (H, G)):
        return False
    if c(H, cmd='mpm_pvp').get('mode') != 1: c(H, cmd='mpm_pvp', action='arrow', row=0, dir=1); time.sleep(1.2)   # Fight
    for _ in range(3):                                           # best of 1, 60 s rounds (whatever the copy's saved rules say)
        if c(H, cmd='mpm_pvp').get('bo') == 1: break
        c(H, cmd='mpm_pvp', action='arrow', row=1, dir=-1); time.sleep(1.2)
    for _ in range(5):
        if c(H, cmd='mpm_pvp').get('secs') == 60: break
        c(H, cmd='mpm_pvp', action='arrow', row=2, dir=-1); time.sleep(1.2)
    for _ in range(4):                                           # map: this settlement (English copies; the saved rules may say a dungeon)
        if c(H, cmd='mpm_pvp')['rows'][4][1] == 'This settlement': break
        c(H, cmd='mpm_pvp', action='arrow', row=4, dir=1); time.sleep(1.2)
    q = c(H, cmd='mpm_pvp'); log('PvP rules', q.get('mode'), q.get('bo'), q.get('secs'), q['rows'][4])
    worlds = sorted(f for f in os.listdir(os.path.join(lrig.idir(H), 'data', 'Saves')) if WORLD.search(f))
    info['world_saves_on_disk'] = worlds                             # the mod's own world saves on disk (none since multiplayer 0.1.26); never in the list
    picks = {}
    for x in (H, G):
        s = open_cards(x, f'pvp_{x}', pvp=True)
        check(f'PvP 1 ({x}): the PvP window\'s Bring Units opens the CARD window (cards with game icons, real worlds only: no hub world saves)',
              cards_ok(s), f'{s} | world saves on disk {len(worlds)}')   # multiplayer 0.1.26: the hub leaves no save (hub_nosave), so none may be on disk
        picks[x] = pick(x, 2, f'pvp_{x}')
    try:
        u = wait('host has 4 brought units', lambda: (lambda q: q if q.get('received') == 4 else None)(c(H, cmd='pvp_units_status')), timeout=90, every=1)
    except RuntimeError:
        u = c(H, cmd='pvp_units_status')
    check('PvP 2: picking 2 units in the card window brings them (host and guest; host has 4)', all(picks[x][0] for x in picks) and u.get('received') == 4,
          f"host picks {picks[H][1].get('picked')} guest {picks[G][1].get('picked')}, received {u.get('received')}")
    if u.get('received') != 4:
        return False
    rp = {x: fsize(x, 'rig.log') for x in (H, G)}
    log('Start Match', c(H, cmd='mpm_pvp', action='start'))              # the brought units are built first, then the match
    wait('match on both', lambda: all(c(x, cmd='pvp_status').get('active') for x in (H, G)), timeout=600, every=2)
    t_match = st(H)['tick']
    ps = c(H, cmd='pvp_status'); names = {u['guid'] for u in ps.get('units', [])}
    info['pvp_units'] = [(u.get('name'), u['guid'][:8], u.get('team')) for u in ps.get('units', [])]
    time.sleep(12)                                               # v0.1.21 (E4): the teams walk to their ends, then a fight vote: G proposes, H agrees
    c(G, cmd='pvp_vote', op='propose'); wait('vote open', lambda: c(H, cmd='pvp_status').get('vote_open'), timeout=60, every=1); c(H, cmd='pvp_vote', op='agree')
    wait('fight begins', lambda: c(H, cmd='pvp_status').get('fight_start', 0) > 0 and not c(H, cmd='pvp_status').get('placing'), timeout=300, every=2)
    for x in (H, G):
        HP.shot(x, f'pvp_fight_{x}.png')
    s0 = c(H, cmd='pvp_status'); hp0 = {u['guid']: u.get('hp', 0) for u in s0['units']}
    owner = {u['guid']: (H if u['player'] == 0 else G) for u in s0['units']}
    def orders():                                                # v0.1.21 (E7): no automatic attack orders: each player orders his units, like right-clicks
        q = c(H, cmd='pvp_status'); cl = {u['guid']: u for u in c(H, cmd='list_units')['units']}
        al = [u for u in q.get('units', []) if u.get('out_tick', -1) < 0 and 'cell' in cl.get(u['guid'], {})]
        for u in al:
            en = [v for v in al if v['team'] != u['team']]
            if en:
                c0 = cl[u['guid']]['cell']; tg = min(en, key=lambda v: (cl[v['guid']]['cell'][0] - c0[0]) ** 2 + (cl[v['guid']]['cell'][1] - c0[1]) ** 2); tc = cl[tg['guid']]['cell']
                c(owner[u['guid']], cmd='ord_run_attack', unit=u['guid'], target=tg['guid'], x=tc[0], y=tc[1], mp=1)
    lost = down = None; t0 = time.time()
    while time.time() - t0 < 600:
        s = c(H, cmd='pvp_status')
        if s.get('active') and not s.get('ending') and not s.get('resetting') and s.get('fight_start') and not s.get('placing'): orders()
        if s.get('units'):
            l = {u['guid']: hp0.get(u['guid'], 0) - u.get('hp', 0) for u in s['units'] if 'hp' in u}
            team = {u['guid']: u['team'] for u in s['units']}
            if not lost and any(v > 0 and team[g] == 1 for g, v in l.items()) and any(v > 0 and team[g] == 2 for g, v in l.items()):
                lost = l
            outs = [u['guid'][:8] for u in s['units'] if u.get('out_tick', -1) >= 0]
            if outs and not down:
                down = outs; HP.shot(H, 'pvp_unit_out.png')
        if not s.get('active') and s.get('matches', 0) >= 1:
            break
        if not s.get('resetting'):                                   # fp.log starts again at each reload: keep the match's lines
            for x in (H, G):
                shutil.copy2(os.path.join(lrig.idir(x), 'fp.log'), os.path.join(OUT, f'fp_pvp_{x}.log'))
        time.sleep(2)
    check('PvP 3: the match plays with the brought units (both teams lose HP, a unit goes out)', bool(lost) and bool(down) and len(names) >= 4, f'lost {bool(lost)} down {down} units {len(names)}')
    wait('match over + hub back', lambda: all((lambda q: not q.get('active') and q.get('phase') == 0 and q.get('matches', 0) >= 1)(c(x, cmd='pvp_status')) for x in (H, G)), timeout=600, every=2)
    wait('both loaded + running', lambda: all((lambda q: q.get('scene') == 2 and not q.get('busy') and q.get('det', {}).get('locked'))(st(x)) for x in (H, G)), timeout=600, every=2)
    for x in (H, G):
        c(x, cmd='mp_pause', on=1)
    wait('both paused at one tick', lambda: (lambda a, b: a['tick'] == b['tick'] and a.get('mp', {}).get('paused') and b.get('mp', {}).get('paused'))(st(H), st(G)), timeout=120)
    fh, fg = fp_read(os.path.join(OUT, f'fp_pvp_{H}.log'), t_match), fp_read(os.path.join(OUT, f'fp_pvp_{G}.log'), t_match)
    com = sorted(set(fh) & set(fg)); first = next((t for t in com if fh[t] != fg[t]), None)
    mh, mg = st(H)['mp'], st(G)['mp']; dl = desync_lines(rp)
    check('PvP 4: 0 desyncs over the match (fingerprints equal for 500+ common ticks, no desync line)',
          len(com) >= 500 and first is None and not mh.get('desyncs') and not mg.get('desyncs') and not dl,
          f'{len(com)} common ticks, first diff {first}, desyncs {mh.get("desyncs")}/{mg.get("desyncs")}, lines {dl}')
    for x in (H, G):
        c(x, cmd='mp_pause', on=0)
    wait('result window on both', lambda: all(c(x, cmd='mpm_pvp').get('result') for x in (H, G)), timeout=120, every=2)
    for x in (H, G):
        c(x, cmd='mpm_pvp', action='press_ok'); time.sleep(2)
    time.sleep(3)
    check('PvP 5: "Back to the Hub" keeps both in the hub', all(st(x).get('scene') == 2 and c(x, cmd='fe_status').get('hub_world') and not c(x, cmd='mpm_pvp').get('result') for x in (H, G)),
          {x: (st(x).get('scene'), c(x, cmd='fe_status').get('hub_world')) for x in (H, G)})
    HP.players('end', (H, G))
    return True


# ------------------------------------------------------------------ compact cards (one copy)
def compact():
    """the host alone in the hub lobby: 8 cards on a page; a click on a card's body (its centre, not the tick) ticks it,
       a second click unticks it"""
    to_title(H)
    press(H, 'MULTIPLAYER'); press(H, 'HOST_FE'); time.sleep(1); press(H, 'HOST_START'); time.sleep(2)   # the one host choice: the hub
    wait('hub lobby + host session', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)), timeout=900, every=2)
    lg = LANGTYPE or 'en'
    s = open_cards(H, f'compact_{lg}')
    click(H, 'bring:left0', 1.5); time.sleep(2); s = bst(H)            # the Elven home save (newest = row 0)
    info[f'shot_compact_{lg}_page'] = HP.shot(H, f'compact_{lg}_page.png')
    check(f'C1 ({lg}): 8 cards on one page (units in the save {s.get("units")})', s.get('open') == 1 and s.get('units', 0) >= 8 and s.get('cards', 0) >= 8, s)
    a = click(H, 'bring:card0'); s1 = bst(H)
    b = click(H, 'bring:card1'); s2 = bst(H)
    info[f'shot_compact_{lg}_picked'] = HP.shot(H, f'compact_{lg}_picked.png')
    d = click(H, 'bring:card0'); s3 = bst(H)
    check(f'C2 ({lg}): a click on a card body ticks it (1, 2), a second click unticks it (1)', a and b and d and s1.get('picked') == 1 and s2.get('picked') == 2 and s3.get('picked') == 1,
          f"picked {s1.get('picked')} {s2.get('picked')} {s3.get('picked')}")
    click(H, 'bring:cancel', 1.5)


# ------------------------------------------------------------------ Foreign Expedition
def order_move(x, guid, cell):
    c(x, cmd='ord_hover', n=0)
    r1 = c(x, cmd='ord_inject', ev='SelectPlayerUnitRequested', **{'i32@16': 0, 'guid@20': guid})
    r2 = c(x, cmd='ord_inject', ev='InteractGridRequested', **{'v2@16': f'{cell[0]},{cell[1]}', 'u8@24': 0})
    return bool(r1.get('ok') and r2.get('ok'))


def live(x):
    return {u['guid']: u for u in c(x, cmd='list_units')['units'] if u['faction'] == 1 and not u['dead'] and not u.get('downed') and 'cell' in u}


def fe(port):
    to_title(H); to_title(G)
    press(H, 'MULTIPLAYER'); press(H, 'HOST_FE')
    want = dict(fe=1, fe_death=0, fe_xp=1, fe_loot=1, fe_recruit=0)
    press(H, 'SETTINGS'); time.sleep(1)
    for k, b in (('fe_death', 'FED_NEXT'), ('fe_xp', 'FEX_NEXT'), ('fe_loot', 'FEL_NEXT'), ('fe_recruit', 'FER_NEXT')):
        if c(H, cmd='mpm_status')['settings'].get(k) != want[k]:
            press(H, b)
    got = {k: c(H, cmd='mpm_status')['settings'].get(k) for k in want}
    if got != want:
        check('FE 0: host page Foreign Expedition, Death Off', False, got); return
    press(H, 'FE_DONE'); time.sleep(1); press(H, 'HOST_START'); time.sleep(2)
    wait('hub lobby + host session', lambda: (lambda s: s.get('det', {}).get('locked') and s.get('mp', {}).get('started'))(st(H)), timeout=900, every=2)
    port = c(H, cmd='mpm_status').get('port') or port
    s2 = open_cards(H, 'fe_host')                                   # the host alone in its lobby (no guest yet)
    if ONLY_OPEN:
        check('FE 1: the host ALONE in the hub lobby: the CARD window opens', cards_ok(s2), s2); return False
    ok_h, sh = pick(H, 1, 'fe_host')
    h_alone = c(H, cmd='fe_status')
    check('FE 1: the host ALONE in the hub lobby: the CARD window opens and its pick reaches its own list (no "Not in a multiplayer session")',
          cards_ok(s2) and ok_h and h_alone.get('received') == 1, f"{s2} | received {h_alone.get('received')}")
    join(port)
    HP.players('start', (H, G))
    wait('guest knows the FE mode', lambda: c(G, cmd='mpm_fewin').get('fe_mode') == 1, timeout=90)
    s = open_cards(G, 'fe_guest')
    check('FE 1b: the guest\'s card window (cards with game icons, its own saves)', cards_ok(s), s)
    ok_g, sg = pick(G, 2, 'fe_guest')
    try:
        rcv = wait('host received 3 units', lambda: (lambda f: f if f.get('received') == 3 else None)(c(H, cmd='fe_status')), timeout=90)
    except RuntimeError:
        rcv = c(H, cmd='fe_status')
    check('FE 2: picking units in the card window brings them (guest 2, host 1; host has 3)', ok_g and ok_h and rcv.get('received') == 3,
          f"guest picks {sg.get('picked')} host {sh.get('picked')}, received {rcv.get('received')}")
    if rcv.get('received') != 3:
        return
    HP.shot(H, 'fe_lobby_host.png')
    press(H, 'START')
    wait('both in the hub', lambda: c(H, cmd='fe_status').get('hub_world') and c(G, cmd='fe_status').get('hub_world')
         and st(H).get('det', {}).get('locked') and st(G).get('det', {}).get('locked'), timeout=600, every=2)
    fh = c(H, cmd='fe_status'); hub = fh['hub_units']; gu = c(G, cmd='fe_status')['uid']
    mine = {H: [h['hub'] for h in hub if h['player'] != gu], G: [h['hub'] for h in hub if h['player'] == gu]}
    info['fe_hub_units'] = hub
    check('FE 3: hub built with the 3 brought units (guest 2, host 1)', len(hub) == 3 and len(mine[G]) == 2 and len(mine[H]) == 1, f'{len(hub)} units')
    wait('both running', lambda: not st(H).get('mp', {}).get('paused') and not st(G).get('mp', {}).get('paused'), timeout=180)
    rp = {x: fsize(x, 'rig.log') for x in (H, G)}; t_hub = st(H)['tick']
    s0 = {g: (c(H, cmd='fe_stats', guid=g).get('stats') or [0] * 20) for g in mine[H] + mine[G]}
    tm = lambda x, **k: c(x, cmd='ui_teams', **k)
    tm(H, action='open'); tm(G, action='open'); time.sleep(3)
    for x in (H, G):
        for g in mine[x]:
            tm(x, action='click', guid=g, button=1); time.sleep(1)
    tm(H, action='close'); tm(G, action='close'); time.sleep(2)
    tm(H, action='open'); time.sleep(1); log('go', tm(H, action='go'))
    wait('stock step 1', lambda: (lambda t: t.get('stock', {}).get('open') and t['stock'].get('state') == 0)(tm(H)), timeout=60)
    tm(H, action='stock_select', units=','.join(mine[H] + mine[G])); time.sleep(1); tm(H, action='stock_next')
    wait('proposer stock step', lambda: tm(H)['stock'].get('state') == 1, timeout=60)
    tm(H, action='stock_accept')
    wait('guest receiver', lambda: (lambda t: (t.get('stock', {}).get('open') and t['stock'].get('mode') == 2) or t.get('prop', {}).get('shown'))(tm(G)), timeout=90)
    if tm(G).get('stock', {}).get('open'):
        tm(G, action='stock_next'); wait('guest summary', lambda: tm(G).get('prop', {}).get('shown'), timeout=60)
    tm(G, action='approve')
    wait('both in the dungeon', lambda: all(st(x).get('map', 0) != 0 and not st(x).get('busy') for x in (H, G)), timeout=600, every=3)
    t_dg = st(H)['tick']; hits = {}
    t_end = time.time() + DIVE; t_min = time.time() + 90
    d2 = lambda a, b: (a['cell'][0] - b['cell'][0]) ** 2 + (a['cell'][1] - b['cell'][1]) ** 2
    shot_done = False
    while time.time() < t_end:
        time.sleep(15)
        if st(G).get('map', 1) == 0 or c(H, cmd='ui_result').get('open'):
            break
        en = [u for u in c(H, cmd='list_units')['units'] if u['faction'] == 3 and not u['dead'] and 'cell' in u]
        for x in (H, G):
            lv = live(x)
            for g in mine[x]:
                u = lv.get(g)
                if u and en and u.get('task') == 0:
                    order_move(x, g, min(en, key=lambda e: d2(u, e))['cell'])
        fs = {g: c(H, cmd='fe_stats', guid=g).get('stats') or [0] * 20 for g in s0}
        for g in fs:
            hits[g] = fs[g][SI['Exp']] > s0[g][SI['Exp']] or fs[g][SI['HealthBody']] < s0[g][SI['HealthBody']] or fs[g][SI['HealthHead']] < s0[g][SI['HealthHead']]
        if any(hits.values()) and not shot_done:
            shot_done = True; HP.shot(G, 'fe_dungeon_guest.png')
        log('dungeon tick', st(H)['tick'], 'enemies', len(en), 'fought', {g[:8]: v for g, v in hits.items()})
        if time.time() > t_min and all(hits.get(g) for g in mine[G]):
            break
    t_fight = st(H)['tick']
    check('FE 4: the expedition plays with the brought units: ticks advance in the dungeon, the guest\'s 2 units fight',
          t_fight - t_dg > 300 and all(hits.get(g) for g in mine[G]), f'ticks {t_dg} -> {t_fight}, fought {dict((g[:8], v) for g, v in hits.items())}')
    if st(G).get('map', 1) != 0 and not c(H, cmd='ui_result').get('open'):        # back to the base: the portal, then Return
        bl = c(G, cmd='ui_buildings', map=st(G).get('map', 1), subtype=2)
        pc = (int(bl['buildings'][0]['x']), int(bl['buildings'][0]['y'])) if bl.get('buildings') else None
        if pc:
            for x in (G, H):
                for g in mine[x]:
                    if g in live(x):
                        order_move(x, g, pc)
            t9 = time.time()
            while time.time() - t9 < 180:
                time.sleep(10)
                allu = {u['guid']: u for x in (G, H) for u in c(x, cmd='list_units')['units']}
                far = [g for x in (G, H) for g in mine[x] if g in allu and not allu[g]['dead'] and (not allu[g].get('cell') or abs(allu[g]['cell'][0] - pc[0]) + abs(allu[g]['cell'][1] - pc[1]) > 3)]
                if not far:
                    break
                for x in (G, H):
                    for g, u in live(x).items():
                        if g in mine[x] and u.get('task') == 0 and abs(u['cell'][0] - pc[0]) + abs(u['cell'][1] - pc[1]) > 3:
                            order_move(x, g, pc)
            tries = [(x, g) for x in (H, G) for g in mine[x]]
            for k in range(12):
                x, g = tries[k % len(tries)]
                if g not in live(x):
                    continue
                log(x, 'portal use', g[:8], c(x, cmd='ui_interact', unit=g, building=bl['buildings'][0]['guid'], map=st(x).get('map', 1)))
                t8 = time.time()
                while time.time() - t8 < 20 and not c(H, cmd='ui_result').get('open'):
                    time.sleep(2)
                if c(H, cmd='ui_result').get('open'):
                    break
    if st(H).get('map') != 0:
        rs = wait('return screen on both', lambda: (lambda a, b: (a, b) if a.get('open') and b.get('open') else None)(c(H, cmd='ui_result'), c(G, cmd='ui_result')), timeout=900, every=3)
        P = H if rs[0]['chooser'] == 0 else G
        c(P, cmd='ui_result', action='return'); time.sleep(3)
        for _ in range(4):
            c(P, cmd='ui_result', action='confirm'); time.sleep(4)
            if not c(P, cmd='ui_result')['open']:
                break
    wait('both on the base map', lambda: all(st(x).get('map') == 0 and not st(x).get('busy') for x in (H, G)), timeout=900, every=3)
    time.sleep(8)
    HP.shot(H, 'fe_back_home_host.png')
    for x in (H, G):
        c(x, cmd='mp_pause', on=1)
    wait('both paused at one tick', lambda: (lambda a, b: a['tick'] == b['tick'] and a.get('mp', {}).get('paused') and b.get('mp', {}).get('paused'))(st(H), st(G)), timeout=120)
    a, b = HP.fp_map(H, t_hub), HP.fp_map(G, t_hub)
    com = sorted(set(a) & set(b)); first = next((t for t in com if a[t] != b[t]), None)
    mh, mg = st(H)['mp'], st(G)['mp']; dl = desync_lines(rp)
    check('FE 5: 0 desyncs over the hub + expedition round (fingerprints equal, no desync line)',
          len(com) >= 300 and first is None and not mh.get('desyncs') and not mg.get('desyncs') and not dl,
          f'{len(com)} common ticks (from tick {t_hub}), first diff {first}, desyncs {mh.get("desyncs")}/{mg.get("desyncs")}, lines {dl}')
    return True


def prepare(pk):
    """kits without Units Laboratory; test saves: the PvP list = data/Saves (the Elven home save, newest), the FE list =
       data/Saves + DSRIG_FE_GAME_SAVES (a fixture: host a Moonlight hub save, guest the Elven home save); hub template cached"""
    env = {}
    for x, f in ((H, 'hub_Moonlight_mp.json'), (G, 'home_Elven.json')):
        HP.kit(x, pk, [DRV], ['multiplayer', DRV])
        I = lrig.idir(x)
        for sub in ('data/fe', 'data/mp_owners', 'mp.json'):
            HP.park(os.path.join(I, sub))
        hs = os.path.join(I, 'data', 'Saves', 'home_Elven.json')     # this game version (the kit's autosave is an older one)
        shutil.copy2(os.path.join(FIX, 'home_Elven.json'), hs); tt = time.time() + 86400; os.utime(hs, (tt, tt))   # newest = row 0
        gs = os.path.join(OUT, f'game_saves_{x}'); os.makedirs(gs, exist_ok=True); shutil.copy2(os.path.join(FIX, f), os.path.join(gs, f))
        cache = os.path.join(I, 'data', 'Mods', 'multiplayer', 'wiki', 'saves'); os.makedirs(cache, exist_ok=True)
        shutil.copy2(TPL, os.path.join(cache, os.path.basename(TPL)))
        d = drv_dir(x); os.makedirs(os.path.join(d, 'cap'), exist_ok=True)
        env[x] = {'DSRIG_FE_GAME_SAVES': gs, 'DSRIG_FE_BACKUP': os.path.join(OUT, f'backups_{x}'), 'DSL_UNITS_TEST': d}
    return env


def main():
    if os.path.exists(os.path.join(HP.RIGDIR, 'release.lock')):
        raise SystemExit('refused: ~/ds_multiplayer/rig/release.lock exists (release gate running)')
    t0 = time.time(); pair = realrig.lease('bringcards', os.getpid())
    while not pair and time.time() - t0 < 1800:
        time.sleep(15); pair = realrig.lease('bringcards', os.getpid())
    if not pair:
        raise SystemExit('no free pair lease for 30 min')
    port = realrig.PAIR_PORTS[pair]; log('lease', pair, 'port', port, 'build', HP.BUILD, 'out', OUT)
    info['build'] = {'multiplayer': open(os.path.join(HP.BUILD, 'mods', 'multiplayer', 'PINNED_COMMIT')).read().strip()}
    g = threading.Thread(target=HP.guard, daemon=True); g.start()
    res = {}
    try:
        info['owner_prefs_before'] = lrig.check_prefs(restore=False)
        XS = (H,) if COMPACT else (H, G)
        for x in (H, G):
            HP.make_copy(x)
        env = prepare(driver_pkg())
        HP.rp.set_host_port(lrig.idir(H), port)                  # the menu host listens on the leased pair port, never the default 47777 (the owner's game)
        api = 'http://127.0.0.1:9'                                    # no release server: nothing is downloaded
        HP.room_for(len(XS))
        for x in XS:
            HP.start(x, api, env[x])
        for x in XS:
            to_title(x)
            dl = flog(x, 'dsload.log')
            check(f'{x}: multiplayer loaded, Units Laboratory not', ' multiplayer ' in dl and not any(' custom-modes ' in l and ': loaded (' in l for l in dl.splitlines()),
                  [l for l in dl.splitlines() if ': loaded (' in l][:4])
        if COMPACT:
            compact()
        elif fe(port):
            hub_pvp()
    except BaseException as e:
        import traceback; log(traceback.format_exc())
        check('run finished', False, repr(e))
    finally:
        check('never a visible window on a display, never frontmost (guard)', not HP.violations, HP.violations[:3])
        HP.stop_guard.set()
        for x in (G, H):
            try:
                HP.stop(x)
            except Exception as e:
                log('stop', x, repr(e))
            for f in ('dsload.log', 'rig.log', 'player.log', 'fp.log'):
                p = os.path.join(lrig.idir(x), f)
                if os.path.exists(p):
                    shutil.copy2(p, os.path.join(OUT, f'{x}_{f}'))
        realrig.release(pair)
        info['owner_prefs_after'] = lrig.check_prefs(restore=False)
        res = {'checks': HP.checks, 'all_pass': all(k['ok'] for k in HP.checks) and bool(HP.checks), 'info': info}
        json.dump(res, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=str)
        log('RESULT', 'PASS' if res['all_pass'] else 'FAIL', f"{sum(k['ok'] for k in HP.checks)}/{len(HP.checks)}", os.path.join(OUT, 'results.json'))
    sys.exit(0 if res.get('all_pass') else 1)


if __name__ == '__main__':
    main()
