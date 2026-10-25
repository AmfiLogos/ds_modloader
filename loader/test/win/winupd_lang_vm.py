#!/usr/bin/env python3
"""Windows VM: the auto-update puts the loader's texts where a full install puts them (files\\lang -> ROOT\\game\\lang).
Up to 0.1.28 the Windows auto-update copied only baselib.dll, the starter, RELEASE and the mods, so every loader text
added after the player's last full install showed in English (found 2026-10-25 with the What's new window's title).
Through winrig (ds_multiplayer win/winbot, WINBOT_DIR), VM running visibly with the agent up; the REAL player starter
(Steam on, the VM's test account), the game in German.
  python3 winupd_lang_vm.py --old-setup <published Setup.exe, v0.1.27 or older (A checks it)> --kit <"Dungeon Settlers Modded - Windows.zip"
          of the build under test> [--ver 0.1.28] [--out DIR]
 A  the old release installed fresh (Setup /S; the install there before is moved to C:\\dsm\\to_delete, never deleted).
 B  the kit as a downloaded update (update\\ready.txt, CONSENT 1), kit RELEASE --ver: the OLD starter installs it (no
    texts), the new starter puts them back at its start (ds_modded_win.c lang_heal). Every files\\lang file of the kit is in ROOT\\game\\lang,
    byte-equal, and the Mods window shows a text the old release did not have in German (multiplayer's Settings page,
    the "This computer" mark = the kit's de.json text).
 C  the same kit as the next version, installed by the NEW starter: dsl_uos_kit_install copies every text file itself
    (starter.log), all byte-equal. The starter carries the same bytes as its kit's files\\lang (build_win.sh), so a kit
    with only its files\\lang changed is not a real case: lang_heal would put the starter's copy back.
 D  no update: game\\lang damaged (de.json = the old release's, ja.json and the old starter's leftovers update\\backup-*.new
    moved away), one start through the starter: every file back, byte-equal; "This computer" in German again.
Fails on a kit built before the fix (B, C and D show "This computer" in English). Exit 0 = every check passed.
B needs an old release whose German texts lack KEY (v0.1.27 or older; A fails otherwise)."""
import argparse, hashlib, json, os, re, sys, time, zipfile

sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402

KEY = 'This computer'                         # loader/ui/dsl_win.c: a "local" setting's mark; new in 0.1.28 ("Display" before)
LANG_DE = 3                                   # LanguageType German (ds_i18n.h dsi_codes)
UPD = W.ROOT + r'\update'
checks = []
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def kit_files(zf):
    """{name below files/lang/: bytes} and the prefix of files/ in the kit zip"""
    pre = next(n[:-len('files/RELEASE')] for n in zf.namelist() if n.endswith('files/RELEASE'))
    lang = {n[len(pre + 'files/lang/'):]: zf.read(n) for n in zf.namelist() if n.startswith(pre + 'files/lang/') and not n.endswith('/')}
    return pre, lang


def variant(src, dst, ver, de_text):
    """the kit zip src as version ver (files/RELEASE) with de.json's KEY = de_text (None = unchanged)"""
    zi = zipfile.ZipFile(src); pre, _ = kit_files(zi)
    with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as zo:
        for it in zi.infolist():
            d = zi.read(it.filename)
            if it.filename == pre + 'files/RELEASE':
                d = (ver + '\r\n').encode()
            elif it.filename == pre + 'files/lang/de.json' and de_text is not None:
                j = json.loads(d.decode('utf-8')); j[KEY] = de_text; d = json.dumps(j, ensure_ascii=False, indent=1).encode('utf-8')
            zo.writestr(it, d)
    return dst


def offer(w, local, ver):
    """the kit as a verified download the starter installs at the next start (src/dsl_update.c ready.txt)"""
    g = UPD + r'\test\kit-' + ver + '.zip'
    w.upload(local, g)
    sha = hashlib.sha256(open(local, 'rb').read()).hexdigest()
    w.write(UPD + r'\ready.txt', f'KIND\tkit\nLABEL\tv{ver}\nKEY\tkit {ver}\nCONSENT\t1\nKIT\t{ver}\t{w.expand(g)}\t{sha}\n')


def shown_mark(w):
    """the "This computer" mark of multiplayer's host port on its Settings page, as the Mods window shows it"""
    w.ui({'op': 'open'}); time.sleep(1)
    w.ui({'op': 'click', 'target': 'tab-mods:launcher'}); time.sleep(1)  # multiplayer is a launcher mod
    r = w.ui({'op': 'click', 'target': 'settings:multiplayer'}); time.sleep(1.5)
    st = {c['name']: c for c in w.ui({'op': 'state'}).get('controls', [])}
    w.ui({'op': 'close'})
    return (st.get('set-kind:host_port') or {}).get('text'), r


def run_update(w, a, step, local, ver, want_text, lang):
    """local = a kit zip offered as an update to ver; None = a plain start"""
    w.prepare(lang=LANG_DE, consent='no')
    if local:
        offer(w, local, ver)
    w.start(env={'DSL_UPDATE_WINDOW': 'hidden'})
    sl = w.text(W.PROF + r'\starter.log') or ''
    if local:
        check_update(w, step, ver, sl)
    bad = []
    for n, d in lang.items():
        g = W.GAME + '\\lang\\' + n
        if not w.exists(g) or w.read(g)[0] != d: bad.append(n)
    check(f'{step}: every files\\lang file of the kit in game\\lang, byte-equal ({len(lang)} files)', lang and not bad, bad[:6])
    text, r = shown_mark(w)
    check(f'{step}: the Mods window shows the kit\'s German text "{want_text}"', text == want_text, (text, r.get('msg')))
    log(f'{step} starter.log:', ' | '.join(l for l in sl.splitlines() if 'texts:' in l or 'Windows kit installed' in l)[:700])
    return sl
    w.logs(os.path.join(a.out, step)); w.stop(); w.kill_all()


def check_update(w, step, ver, sl):
    check(f'{step}: the update to {ver} installed and started (starter.log)', 'new kit installed: started its starter' in sl,
          [l for l in sl.splitlines() if 'update:' in l][-6:])
    rel = (w.text(W.ROOT + r'\RELEASE') or '').strip()
    check(f'{step}: RELEASE {ver}', rel == ver, rel)
    try: w.wait_log('dsload.log', [f'v{ver} confirmed at the title'], timeout=30, since=0); ok = True
    except W.GameError: ok = False
    check(f'{step}: confirmed at the title', ok, [l for l in (w.text(W.PROF + r'\dsload.log') or '').splitlines() if 'update:' in l][-4:])


def heal(sl):
    return [l for l in sl.splitlines() if 'texts:' in l or 'Windows kit installed' in l][-3:]


def damage(w, old_de):
    """game\\lang as an old install left it: de.json of the old release, ja.json missing; no unpacked kit left in update\\"""
    w.kill_all()
    keep = W.WORK + r'\to_delete\winupd_lang_' + time.strftime('%Y%m%d-%H%M%S')
    w.write(W.GAME + r'\lang\de.json', old_de)
    w.move(W.GAME + r'\lang\ja.json', keep + r'\ja.json')
    for e in w.a('ls', path=UPD)['entries']:
        if e['dir'] and e['name'].startswith('backup-'):
            w.move(UPD + '\\' + e['name'], keep + '\\' + e['name'])
    left = [e['name'] for e in w.a('ls', path=UPD)['entries'] if e['name'].startswith('backup-')]
    check('D: damaged: de.json old, ja.json gone, no update\\backup-* left', not w.exists(W.GAME + r'\lang\ja.json') and not left, left)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old-setup', required=True); ap.add_argument('--kit', required=True)
    ap.add_argument('--ver', default='0.1.28'); ap.add_argument('--out', default='winupd_lang_vm_out')
    a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
    _, lang = kit_files(zipfile.ZipFile(a.kit))
    de = json.loads(lang['de.json'].decode('utf-8')).get(KEY)
    if not de:
        sys.exit(f'the kit\'s de.json has no "{KEY}": this test needs a kit of 0.1.28 or newer')
    nv = a.ver.rsplit('.', 1); nv = nv[0] + '.' + str(int(nv[1]) + 1)
    k1 = variant(a.kit, os.path.join(a.out, f'kit-{a.ver}.zip'), a.ver, None)
    k2 = variant(a.kit, os.path.join(a.out, f'kit-{nv}.zip'), nv, None)
    w = W.Win(); w.starter = True; w.up(); w.kill_all()
    try:
        if w.exists(W.ROOT):
            w.move(W.ROOT, W.WORK + r'\to_delete\install_' + time.strftime('%Y%m%d-%H%M%S'))
        rc = w.install(a.old_setup)
        old = (w.text(W.ROOT + r'\RELEASE') or '').strip()
        check(f'A: old release installed (Setup /S rc {rc})', rc == 0 and old, old)
        od = w.text(W.GAME + r'\lang\de.json') or ''; old_de = w.read(W.GAME + r'\lang\de.json')[0]
        check(f'A: the old release has no German "{KEY}" (the text under test is new)', od and f'"{KEY}"' not in od, len(od))
        sl = run_update(w, a, 'B', k1, a.ver, de, lang)
        check('B: the new starter put the texts back (the old starter copied none)', re.search(r'texts: [1-9]\d* of \d+ file', sl), heal(sl))
        sl = run_update(w, a, 'C', k2, nv, de, lang)
        m = re.search(r'RELEASE, (\d+) text file', sl)
        check(f'C: the new starter\'s install copied the {len(lang)} text files itself', m and int(m.group(1)) == len(lang), heal(sl))
        damage(w, old_de)
        sl = run_update(w, a, 'D', None, nv, de, lang)
        check('D: put back at the start: de.json + ja.json (1 new)', re.search(r'texts: 2 of \d+ file\(s\) .*\(1 new\), 0 failed', sl), heal(sl))
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: w.logs(os.path.join(a.out, 'end'))
        except Exception as e: log('logs', e)
        try: w.stop()
        except Exception: pass
        w.kill_all()
        json.dump(checks, open(os.path.join(a.out, 'summary.json'), 'w'), indent=1)
        log(f"RESULT {sum(c['ok'] for c in checks)}/{len(checks)} checks")
    sys.exit(0 if checks and all(c['ok'] for c in checks) else 1)


if __name__ == '__main__':
    main()
