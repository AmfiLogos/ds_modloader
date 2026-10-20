#!/usr/bin/env python3
"""TEST ONLY: the number field of the mod Settings page (mod.json "input": "number") with the multiplayer mod's
"host_port" setting, on ONE hidden test copy (never the owner's install, saves, screen, keyboard or mouse).

Fails on the old loader (host_port was a slider: no typed field, an exact port could not be set).
 1. Mods window > Settings of Multiplayer: host_port is a number field showing 47777.
 2. Typed keys go through the field's own TMP_InputField.ProcessEvent (its digit validation); Enter = the test "click".
    80, 70000, letters (typed: dropped by the field) and "12ab" (set as text: past the field's validation) are refused:
    the field shows 47777 again, the status line says the range, settings.json and the module keep 47777.
 3. 12345 is taken: field 12345, settings.json, the multiplayer module reads 12345 live (mpm_status host_port).
 4. Window closed and opened again: 12345. Game quit and started again: 12345, then Host > Load: listens on 12345.
Reuses the copy / kit / start / host helpers of ds_multiplayer ext/hostport/hostport_pair.py (HP_PAIR_DIR, else
~/ds_multiplayer/ext/hostport, else ~/ds_mp_hostport/ext/hostport). Port 47777 is never bound: the host step runs only
when the module reads 12345 and 12345 is free.
  HP_BUILD=<this worktree>/loader/build numfield_test.py [out dir]   (build with MP_PIN = a ds_multiplayer commit with host_port)
"""
import json, os, shutil, sys, threading, time

for d in (os.environ.get('HP_PAIR_DIR', ''), os.path.expanduser('~/ds_multiplayer/ext/hostport'), os.path.expanduser('~/ds_mp_hostport/ext/hostport')):
    if d and os.path.exists(os.path.join(d, 'hostport_pair.py')):
        sys.path.insert(0, d); break
import hostport_pair as hp  # noqa: E402

lrig, H, PORT, I = hp.lrig, hp.H, 12345, hp.lrig.idir(hp.H)
hp.OUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(os.path.expanduser('~/ds_launcher/loader/test_env/numfield_out'), time.strftime('%Y%m%d-%H%M%S'))
os.makedirs(hp.OUT, exist_ok=True)
UI = os.path.join(hp.OUT, 'ui'); MODS = os.path.join(I, 'data', 'Mods'); T = 'set:host_port'
RANGE = 'Type a whole number from 1024 to 65535.'
check, log = hp.check, hp.log

_soft = lrig.soft_test_env                                       # + the Mods window's test driver for hp.start
lrig.soft_test_env = lambda: {**_soft(), 'DSL_UI_TEST': UI, 'DSL_PREWARM': '0'}

SEQ = [0]
def ui(op, **kw):
    SEQ[0] += 1; seq = str(SEQ[0]); kw.update({'op': op, 'seq': seq})
    tmp = os.path.join(UI, 'cmd.json.tmp'); open(tmp, 'w').write(json.dumps(kw)); os.replace(tmp, os.path.join(UI, 'cmd.json'))
    t0 = time.time()
    while time.time() - t0 < 30:
        try:
            j = json.load(open(os.path.join(UI, 'result.json'), encoding='utf-8', errors='replace'))
            if j.get('seq') == seq: return j
        except Exception: pass
        time.sleep(0.1)
    raise RuntimeError('ui command timed out: %s' % op)


def ctl(st, name):
    return next((c for c in st.get('controls', []) if c['name'] == name), None)


def field():
    return (ctl(ui('state'), T) or {}).get('text')


def status():
    return (ctl(ui('state'), 'status') or {}).get('text', '')


def saved():
    try: return json.load(open(os.path.join(MODS, 'settings.json'))).get('multiplayer', {}).get('host_port')
    except Exception: return None


def open_page(tag):
    for _ in range(40):                                          # the kit's templates come with the title screen
        ui('open'); time.sleep(1.0); st = ui('state')
        if st.get('open') and not ctl(st, 'settings:multiplayer') and ctl(st, 'tab-mods:launcher'):   # modtabs: Multiplayer is on the Launcher mods tab
            ui('click', target='tab-mods:launcher'); time.sleep(1.0); st = ui('state')
        if st.get('open') and ctl(st, 'settings:multiplayer'): break
    ui('click', target='settings:multiplayer'); time.sleep(1.5)
    c = ctl(ui('state'), T)
    log(tag, 'host_port control', c)
    return c


def start_title():
    os.makedirs(UI, exist_ok=True)
    for f in os.listdir(UI):
        if f.endswith('.json'): os.replace(os.path.join(UI, f), os.path.join(UI, f + '.old'))
    hp.start(H)
    lrig.wait_for(H, lambda s: s.get('scene') == 1 and not s.get('busy'), f'title {H}', timeout=1500)
    time.sleep(3)


def refuse(txt, keys):
    ui('type', target=T, text=txt, **({'keys': True} if keys else {})); time.sleep(0.3)
    typed = field(); ui('click', target=T); time.sleep(0.5)
    f, s, m = field(), status(), hp.ms(H).get('host_port')
    check(f'refused {txt!r} ({"typed keys" if keys else "set as text"}; field had {typed!r})',
          f == '47777' and RANGE in s and saved() is None and m == 47777, f'field {f!r} status {s!r} saved {saved()} module {m}')
    return typed


def main():
    assert lrig.check_prefs(), 'owner prefs changed before the run'
    pair, t0 = None, time.time()
    while not pair and time.time() - t0 < 3600:
        pair = hp.realrig.lease('numfield')
        if not pair: time.sleep(30)
    assert pair, 'no free pair for 60 min'
    log('pair', pair)
    threading.Thread(target=hp.guard, daemon=True).start()
    try:
        hp.make_copy(H); hp.kit(H, None)
        ld = os.path.join(I, 'kit', 'loader', 'lang'); os.makedirs(ld, exist_ok=True)      # the loader's texts
        for f in os.listdir(os.path.join(os.path.dirname(hp.BUILD), 'lang')):
            if f.endswith('.json'): shutil.copy2(os.path.join(os.path.dirname(hp.BUILD), 'lang', f), os.path.join(ld, f))
        start_title()
        c = open_page('first open')
        check('host_port is a number field showing 47777', c and c['kind'] == 6 and c['text'] == '47777', c)
        ui('shot', name='numfield')
        if os.path.exists(os.path.join(UI, 'numfield.png')): shutil.copy2(os.path.join(UI, 'numfield.png'), os.path.join(hp.OUT, 'numfield.png'))
        refuse('80', True); refuse('70000', True)
        t = refuse('abc', True)
        check('letters typed are dropped by the field (digits only)', t == '', t)
        refuse('12ab', False)
        ui('type', target=T, text=str(PORT), keys=True); time.sleep(0.3)
        typed = field(); ui('click', target=T); time.sleep(0.5)
        f, m = field(), hp.ms(H).get('host_port')
        check(f'typed {PORT}: field, settings.json and the module have it', typed == str(PORT) and f == str(PORT) and saved() == PORT and m == PORT,
              f'typed {typed!r} field {f!r} saved {saved()} module {m}')
        ui('close'); time.sleep(1.0)
        c = open_page('reopen')
        check(f'window opened again shows {PORT}', c and c['text'] == str(PORT), c)
        ui('close'); hp.stop(H)
        start_title()
        c = open_page('after restart')
        check(f'game started again shows {PORT}', c and c['text'] == str(PORT), c)
        ui('close'); time.sleep(1.0)
        m = hp.ms(H).get('host_port')
        if m != PORT or not hp.port_free(PORT):
            check('host step', False, f'module host_port {m}, {PORT} free {hp.port_free(PORT)}: not hosting (never 47777)')
        else:
            hp.press(H, 'MULTIPLAYER'); hp.press(H, 'HOST'); hp.press(H, 'LOAD_SAVE'); time.sleep(2)
            work = os.path.join(I, 'save_in'); os.makedirs(work, exist_ok=True)
            src = os.path.join(work, 'numfield_base.json'); shutil.copy2(lrig.SAVE, src)
            log(H, 'stock list pick', hp.c(H, cmd='load_save', file=src, det=0))
            r = hp.wait('host listening', lambda: (lambda x: x if x.get('port') else None)(hp.ms(H)), timeout=900, every=2)
            check(f'host listens on {PORT}', r.get('port') == PORT, f"port {r.get('port')} direct {r.get('direct')}")
        for f in ('dsload.log', 'rig.log'):
            if os.path.exists(os.path.join(I, f)): shutil.copy2(os.path.join(I, f), os.path.join(hp.OUT, f))
    except Exception as e:
        check('run completed', False, repr(e))
    finally:
        hp.stop_guard.set(); hp.stop(H)
        hp.realrig.release(pair, 'numfield')
        json.dump(checks_out(), open(os.path.join(hp.OUT, 'results.json'), 'w'), indent=1)
        log('RESULT', 'ALL PASS' if hp.checks and all(k['ok'] for k in hp.checks) else 'FAIL', hp.OUT)
        log('prefs identical:', lrig.check_prefs())


def checks_out():
    return hp.checks


if __name__ == '__main__':
    main()
