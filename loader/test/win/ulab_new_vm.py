#!/usr/bin/env python3
"""Windows VM run of the Units Laboratory 0.1.48 parts (ds_custommodes fights/test/setup_test.py (1)+(5) and
units/test/export_test.py, cut to what can break on Windows only: file paths, the kit's sections, ShellExecute) through
winrig (ds_multiplayer win/winbot, WINBOT_DIR), the VM running visibly with the agent up and the kit under test installed.
Driver: the test module units-probe (units_probe/, as units_lab_vm.py). The wiki is a VM folder (DSCM_WIKI_DIR: the
builder's data.js / builder.js / savegen.js and dbdata/gallery_codec.js from DSB_DIR/.., default ~/dungeonsettlersdb/site).
  python3 ulab_new_vm.py [--out DIR] [--probe DIR with units-probe/]
Browser guard: Windows has no loader quiet mode (Mac only), so the export's ShellExecute would open the real wiki (and a
public link posts to the Build Gallery). For the run, HKCU\\Software\\Classes\\<the https ProgId>\\shell\\open\\command is
set to write the URL into a file instead (per user, no admin); the guard is proven with a dummy URL BEFORE the game starts
(no export runs without it) and taken off after (only the key this script made).
Real breaks it catches on Windows:
  Test Fights (owner: "keep ALL test settings, ALWAYS, also after the game restarts"; sections fold and stay folded):
  - a changed choice (time of day, combat difficulty, a kit) is not the same after a game restart;
  - a section is not a fold header, a click does not fold it, it is open again after a restart, a second click does not
    open it.
  Open in Wiki Builder:
  - the link is not the wiki contract (https://dungeonsettlers.wiki/builder?team=..&from=game&pub=0|1&tn=<world>&gx=<16 hex>);
  - the browser open is not tried with exactly that link (ShellExecute);
  - the wiki builder (node, its own import of the same save) does not read EXACTLY the ticked units from the link;
  - the gallery switch: starts Public, flips to Private, kept on disk (wiki_export.txt), the link follows it;
  - the wiki's link format changed: the link must NOT go out (no open tried) and the window says to update.
Exit 0 = every check passed."""
import argparse, ast, json, os, re, subprocess, sys, time, urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import units_lab_vm as U  # noqa: E402  (Drv, make_library, make_world, check; imports winrig)
W = U.W
U.DRV = r'C:\dsm\ulab_new_vm\drv'                                # U.Drv writes its requests to U.DRV
check, log, checks = U.check, U.log, U.checks

CM = U.CM
DSB = os.path.expanduser(os.environ.get('DSB_DIR', '~/dungeonsettlersdb/site/builder'))
SITE = os.path.dirname(DSB)
NODE_DIR = os.path.join(CM, 'units', 'test', 'node')
WORK = r'C:\dsm\ulab_new_vm'
DRV = WORK + r'\drv'
LIB = WORK + r'\lib\library.json'
WIKI = WORK + r'\wiki'
OPENED = WORK + r'\opened.txt'
TO_DEL = W.WORK + r'\to_delete\ulab_new_vm_' + time.strftime('%Y%m%d-%H%M%S')
REG = r'C:\Windows\System32\reg.exe'


def builder_check_fn():
    """export_test.builder_check, taken from ds_custommodes' own test (one copy of the builder comparison)"""
    src = open(os.path.join(CM, 'units', 'test', 'export_test.py')).read()
    fn = next(n for n in ast.parse(src).body if isinstance(n, ast.FunctionDef) and n.name == 'builder_check')
    g = {'urllib': urllib, 'subprocess': subprocess, 'json': json, 'os': os, 'NODE_DIR': NODE_DIR, 'DSB': DSB}
    exec(compile(ast.Module(body=[fn], type_ignores=[]), 'export_test.builder_check', 'exec'), g)
    return g['builder_check']


def reg(w, args):
    return w.a('run', exe=REG, args=args, wait_ms=15000, capture=True)


def guard_on(w):
    """the https ProgId's open command writes the URL into OPENED; proven with a dummy URL. -> (key made, ok)"""
    out = reg(w, r'query HKCU\Software\Microsoft\Windows\Shell\Associations\UrlAssociations\https\UserChoice /v ProgId').get('out', '')
    m = re.search(r'ProgId\s+REG_SZ\s+(\S+)', out); prog = m.group(1) if m else 'MSEdgeHTM'
    root = r'HKCU\Software\Classes' + '\\' + prog                # per user: shadows the machine's class (HKCR)
    key = root + r'\shell\open\command'
    had = reg(w, 'query "' + root + '"').get('exit') == 0
    cmdv = r'C:\Windows\System32\cmd.exe /d /c echo \"%1\">>' + OPENED
    if not had:
        reg(w, 'add "' + key + '" /ve /t REG_SZ /f /d "' + cmdv + '"')
    dummy = 'https://example.invalid/ulab_new_vm_' + time.strftime('%H%M%S')
    w.a('run', exe=r'C:\Windows\System32\rundll32.exe', args='url.dll,FileProtocolHandler ' + dummy, wait_ms=15000)
    ok = False
    try: ok = bool(w.wait(lambda: dummy in (w.text(OPENED) or ''), 'the browser guard', 30, 1))
    except W.GameError as e: log(e)
    return (None if had else root), ok and not had


def opened(w):
    return [l.strip().strip('"') for l in (w.text(OPENED) or '').splitlines() if l.strip()]


def export(w, d, label):
    """press the button; (url from the log, the browser opens tried since, the end line)"""
    a = len(w.text(W.PROF + r'\dsload.log') or ''); o0 = len(opened(w))
    d.click('mu:export', 1)
    end = []
    def ended():
        m = re.findall(r'my units: export (browser opened|REFUSED[^\n]*|failed[^\n]*)', (w.text(W.PROF + r'\dsload.log') or '')[a:])
        return m or None
    try: end = w.wait(ended, 'the export end line', 40, 0.5)
    except W.GameError as e: log(e)
    time.sleep(5)                                                     # ShellExecute -> the guard's cmd: a few seconds
    L = (w.text(W.PROF + r'\dsload.log') or '')[a:]
    urls = re.findall(r'my units: export url (\S+)', L)
    op = opened(w)[o0:]
    log(label, end, (urls[-1][:140] if urls else None), len(op))
    return (urls[-1] if urls else ''), op, (end[-1] if end else '')


def export_part(w, d, out, save_local, sname):
    builder_check = builder_check_fn()
    s = d.mu(); worlds = s.get('worlds') or []
    sp = [x.split('|') for x in worlds]
    gi = next((i for i, x in enumerate(sp) if x[2] == sname), -1)
    clan = sp[gi][0] if gi >= 0 else ''
    d.click(f'mu:world{gi}', 4); s = d.mu(); wunits = s.get('wunits') or []
    check('[export] the test world shows its units', gi >= 0 and len(wunits) >= 2, (gi, len(wunits)))
    check('[export] the gallery switch starts on Public', s.get('x_pub') == 1, s.get('x_pub'))
    d.click('mu:tick0'); d.click('mu:tick1'); s = d.mu()
    ticked = wunits[:2]
    check('[export] 2 units ticked', s.get('wticked') == 2, s.get('wticked'))
    # private first (the switch), then public: same units = same team code and gx
    d.click('mu:export_pub', 1); s = d.mu()
    pref = w.text(LIB.rsplit('\\', 1)[0] + r'\wiki_export.txt') or ''
    check('[export] the switch flips to Private and the choice is kept on disk', s.get('x_pub') == 0 and 'public=0' in pref, (s.get('x_pub'), pref))
    url, op, end = export(w, d, 'export private')
    q = urllib.parse.urlparse(url); p = urllib.parse.parse_qs(q.query)
    check('[export] the link is the wiki contract (builder, from=game, pub=0, tn = the world, gx = 16 hex)',
          q.scheme == 'https' and q.netloc == 'dungeonsettlers.wiki' and q.path in ('/builder', '/builder/') and p.get('from') == ['game']
          and p.get('pub') == ['0'] and p.get('tn') == [clan] and re.fullmatch(r'[0-9a-f]{16}', (p.get('gx') or [''])[0]) is not None
          and 'browser opened' in end, (url[:300], end))
    check('[export] the browser open was tried with exactly that link (ShellExecute, caught by the guard)', op and op[-1] == url, (op[-1][:160] if op else None))
    bad = builder_check(url, save_local, ticked) if url else ['no link']
    check('[export] the wiki builder loads EXACTLY the 2 ticked units of the save (its own save import + computeBuild)', not bad, bad[:3])
    d.click('mu:export_pub', 1); s = d.mu()
    url2, op2, _ = export(w, d, 'export public')
    p2 = urllib.parse.parse_qs(urllib.parse.urlparse(url2).query)
    check('[export] public: the link says pub=1, same units = same team code and gx, open tried with it',
          s.get('x_pub') == 1 and p2.get('pub') == ['1'] and p2.get('team') == p.get('team') and p2.get('gx') == p.get('gx') and op2 and op2[-1] == url2,
          (s.get('x_pub'), url2[:200]))
    # the wiki's link format changed (a 7th major stat): nothing goes out, the window says to update
    cf = WIKI + r'\dbdata\gallery_codec.js'; good = w.text(cf) or ''
    w.write(cf, good.replace('"majors":[', '"majors":["Luck",', 1).encode())
    url5, op5, end5 = export(w, d, 'export new link format')
    s = d.mu()
    check('[export] a new wiki link format: REFUSED, no browser open tried, the window says to update Units Laboratory',
          'REFUSED' in end5 and not op5 and 'Update' in (s.get('msg') or ''), (end5, len(op5), s.get('msg')))
    w.write(cf, good.encode())
    d.click('mu:close', 2)


def tf_state(d): return ((d('state').get('cm') or {}).get('fights') or {}).get('tf') or {}


def cmf(d, **kw):
    r = d('call', fn='cmf_test', s=json.dumps(kw, separators=(',', ':')))
    try: return json.loads(r.get('out') or '{}')
    except ValueError: return {'ok': False, 'raw': (r.get('out') or '')[:300]}


def controls(d): return cmf(d, cmd='state').get('controls') or {}


def kept(d): return dict(x.split('=', 1) for x in (d('call', fn='tf_test_kept').get('out') or '').split(';') if '=' in x)


def open_tf(w, d, tag):
    d('open'); time.sleep(3)
    ok = d.click('home:Test Fights', 3)
    try: s = w.wait(lambda: (lambda t: t if t.get('engine') in (2, 3) else None)(tf_state(d)), 'Test Fights engine', 300, 2)
    except W.GameError: s = tf_state(d)
    check(f'[{tag}] Units Laboratory > Test Fights opened, the wiki\'s fight maker ready', ok and s.get('engine') == 2, (ok, s.get('engine'), s.get('status')))
    return ok and s.get('engine') == 2


def tf_part1(w, d):
    if not open_tf(w, d, 'tf first open'):
        return None
    k0 = kept(d)
    check('[tf] fresh settings: Time of Day starts on Day', k0.get('tod') == 'day', k0)
    cmf(d, cmd='set', name='fights:r.tod', index=1); time.sleep(0.6)
    cmf(d, cmd='set', name='fights:r.combat', index=2); time.sleep(0.8)
    cmf(d, cmd='click', name='fights:r.kit0'); time.sleep(0.8)
    k1 = kept(d)
    ch = [k for k in ('tod', 'combat', 'kits') if k1.get(k) != k0.get(k)]
    check('[tf] the choices changed from their defaults (Night, combat difficulty, a kit)', len(ch) == 3 and k1.get('tod') == 'night', {k: (k0.get(k), k1.get(k)) for k in ('tod', 'combat', 'kits')})
    c = controls(d)
    check('[tf] every heading is a fold header (the kit\'s section)', all(f'fights:sec.{k}' in c for k in ('squad', 'test', 'difficulty', 'kits')),
          [k for k in c if k.startswith('fights:sec.')])
    d.click('fights:sec.difficulty', 1.5); c = controls(d)
    check('[tf] a click on "Difficulty" folds it: its header stays, its rows go', 'fights:sec.difficulty' in c and 'fights:r.combat' not in c,
          [k for k in c if k.startswith('fights:sec.') or k == 'fights:r.combat'])
    return k1


def tf_part2(w, d, k1):
    if not open_tf(w, d, 'tf after a restart'):
        return
    c = controls(d)
    check('[tf] Difficulty still folded after the game restarts', 'fights:sec.difficulty' in c and 'fights:r.combat' not in c,
          sorted(k for k in c if 'sec.' in k or k == 'fights:r.combat'))
    d.click('fights:sec.difficulty', 1.5)
    check('[tf] a second click opens it again', 'fights:r.combat' in controls(d))
    k2 = kept(d)
    check('[tf] every choice is as it was before the restart', k2 == k1, {k: (k1.get(k), k2.get(k)) for k in set(k1) | set(k2) if k1.get(k) != k2.get(k)})


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='ulab_new_vm_out'); ap.add_argument('--probe')
    a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    probe = a.probe
    if not probe:
        probe = os.path.join(out, 'probe_build')
        subprocess.run(['zsh', os.path.join(HERE, 'units_probe', 'build.sh'), probe], check=True)
    w = W.Win(); w.up(); w.kill_all()
    md = W.ROOT + r'\mods\units-probe'
    lib_local, _ = U.make_library(out)
    gp, gsave, _ = U.make_world(out, 'UnitsLabExport', '2026-10-25 08:00:00')
    placed = [U.GAME_SAVES + '\\' + gsave + '.json']
    key = None
    try:
        for f in ('drv', 'lib', 'wiki', 'opened.txt'):                # a fresh work folder (moved, never deleted)
            if w.exists(WORK + '\\' + f): w.move(WORK + '\\' + f, TO_DEL + '\\work_' + f)
        for f in ('custom-modes\\settings.json', 'custom-modes\\fights_gallery.txt', 'ui_places.tsv'):   # tf_fresh.FILES
            if w.exists(W.DATA + r'\Mods' + '\\' + f): w.move(W.DATA + r'\Mods' + '\\' + f, TO_DEL + '\\kept_' + f.replace('\\', '_'))
        for f in ('units-probe.dll', 'mod.json'):
            w.upload(os.path.join(probe, 'units-probe', f), md + '\\' + f)
        w.write(LIB, open(lib_local, 'rb').read())
        w.upload(gp, placed[0])
        for rel in ('builder/data.js', 'builder/builder.js', 'builder/savegen.js', 'dbdata/gallery_codec.js'):
            w.upload(os.path.join(SITE, rel), WIKI + '\\' + rel.replace('/', '\\'))
        w.write(OPENED, b'')
        key, gok = guard_on(w)
        check('the browser guard is in place (a dummy https URL lands in the file, no browser)', gok, key)
        env = {'DSL_CLOUD': '0', 'DSL_UNITS_TEST': DRV, 'DSMP_UNITS_LIB': LIB, 'DSCM_WIKI_DIR': WIKI}
        w.prepare(lang=0, consent='no', active=['multiplayer', 'custom-modes', 'units-probe'], settings={})
        w.write(DRV + r'\.keep', b'')
        w.start(env=env)
        d = U.Drv(w)
        if gok:
            r = d('open'); time.sleep(3)
            ok = d.click('home:My Units', 4); s = d.mu()
            check('[export] My Units opened on the TEST library', ok and s.get('open') == 1, (ok, s.get('open'), s.get('lib')))
            export_part(w, d, out, gp, gsave)
        k1 = tf_part1(w, d)
        if k1 is not None:
            w.logs(os.path.join(out, 'run1')); w.stop()
            w.start(env=env)
            tf_part2(w, d, k1)
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: w.logs(out); w.get(OPENED, os.path.join(out, 'opened.txt'))
        except Exception as e: log('logs:', e)
        w.stop(); w.kill_all()
        if key:                                                       # only the key this script made
            try: reg(w, 'delete "' + key + '" /f'); log('browser guard off', key)
            except Exception as e: log('guard off:', e)
        for p in [md + r'\units-probe.dll', md + r'\mod.json'] + placed:   # moved away, never deleted
            try:
                if w.exists(p): w.move(p, TO_DEL + '\\' + p.split('\\')[-2] + '_' + p.split('\\')[-1])
            except Exception as e: log('move:', e)
        try:
            ed = [p for p in w.a('ps', name='msedge.exe').get('procs', [])]
            check('no browser was started by the run', not ed, len(ed))
        except Exception as e: log('ps:', e)
        json.dump(checks, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', f'{len(checks) - len(bad)}/{len(checks)} checks')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
