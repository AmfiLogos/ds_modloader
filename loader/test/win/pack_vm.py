#!/usr/bin/env python3
"""The mod pack Import window on the Windows VM (owner 2026-11-02, PLAN_v013 item 36; ui/dsl_pack.c + src/dsl_hostmods.c;
Mac proof: test/pack_test.py) through winrig (ds_multiplayer win/winbot, WINBOT_DIR), the VM running visibly with the agent
up. The INSTALLED kit (the release Setup) with every installed kit mod on (multiplayer gives the control port). The
clipboard is the file DSL_PACK_CLIP (a test copy never touches the clipboard).
Flow: title, Mods window (DSL_UI_TEST open), Share (the code goes to the clip file), Import (the window fills itself from
the clip file and shows the plan), a REAL Escape key press (agent key, VK_ESCAPE), the Mods window closed.
The real breaks this catches on Windows:
  - Share writes no code, or one the reader refuses (bad check value).
  - Import does not open, or shows no plan for the code (the plan of dsl_hostmods.c on win-x64: no rows, no group line).
  - The Import window logs UI-RULE / ESC-MISSING (kit window template broken), or a real Escape does not close it first
    (the game's own key path on Windows: the press goes to the game, or closes the Mods window under it).
Nothing is downloaded or saved (the plan's button is not pressed).
  python3 pack_vm.py [--out DIR]
Exit 0 = every check passed. Last line: RESULT n/m checks."""
import argparse, base64, json, os, re, sys, time, zlib

sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402

STAMP = time.strftime('%Y%m%d-%H%M%S')
TO_DEL = W.WORK + r'\to_delete\pack_vm_' + STAMP
CLIP = W.UITEST + r'\pack_clip.txt'
VK_ESCAPE = 0x1B
checks = []
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:700]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def decode(code):
    """the code's lines (the loader's format: DSPACK1.<base64>.<crc32>), None when it does not read"""
    m = re.search(r'DSPACK1\.([A-Za-z0-9+/=\s]+)\.([0-9a-f]{8})', code or '')
    if not m: return None
    raw = base64.b64decode(re.sub(r'\s', '', m.group(1)))
    return raw.decode('utf-8').split('\n') if '%08x' % zlib.crc32(raw) == m.group(2) else None


def focus_game(w):
    """the game window gets the keyboard (a real key goes to the foreground window in the guest): one click on its title
    bar, which touches no game control (r2 2026-11-02: after a fresh install the Escape went elsewhere, no kit line at all)"""
    win = next((x for p in w.procs() if p['pid'] == w.pid for x in p.get('windows', []) if x.get('w', 0) > 600), None)
    if not win: log('focus: no game window'); return False
    w.a('click', x=int(win['x'] + win['w'] / 2), y=int(win['y'] + 12)); time.sleep(0.5)
    return True


def kit_ids(w):
    """the installed kit mods (ROOT\\mods\\<id>\\mod.json), hidden ones left out"""
    ids = []
    for e in w.a('ls', path=W.ROOT + r'\mods').get('entries', []):
        if not e.get('dir'): continue
        j = w.json_file(W.ROOT + f'\\mods\\{e["name"]}\\mod.json')
        if j and not j.get('hidden'): ids.append(j.get('id') or e['name'])
    return sorted(ids)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='pack_vm_out')
    a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    w = W.Win(); w.up(); w.kill_all()
    try:
        for n in ('cmd.json', 'result.json', 'cmd.done.json', 'pack_clip.txt'):    # a stale command or code never counts
            if w.exists(W.UITEST + '\\' + n): w.move(W.UITEST + '\\' + n, TO_DEL + '\\' + n)
        ids = kit_ids(w); log('kit mods on:', ids)
        w.prepare(lang=0, consent='no', active=ids)
        w.start(env={'DSL_CLOUD': '0', 'DSL_PACK_CLIP': CLIP})
        time.sleep(3)
        w.ui({'op': 'open'}, 20)
        w.wait(lambda: (lambda s: s if s.get('open') else None)(w.ui({'op': 'state'})), 'the Mods window', 30, 1)
        ctl = lambda: {x['name']: x for x in w.ui({'op': 'state'}).get('controls', [])}
        c = ctl()
        check('Groups header: Share and Import', 'group-share' in c and 'group-import' in c, sorted(n for n in c if n.startswith('group')))
        w.ui({'op': 'click', 'target': 'group-share'}); time.sleep(1)
        code = w.text(CLIP) or ''
        lines = decode(code) or []
        mods = [l.split('@')[0] for l in lines if '@' in l and '/' not in l.split('@')[0]]
        check('Share: a code that reads (check value), with mods in it', lines and mods, code[:80])
        w.mark('dsload.log')
        w.ui({'op': 'click', 'target': 'group-import'}); time.sleep(2)
        c = ctl()
        rows = sorted(n for n in c if n.startswith('pack-row:'))
        check('Import: the window opens with the code and one plan row per mod of the code',
              'pack-code' in c and len(rows) == len(mods), {'rows': rows, 'code mods': mods})
        check('Import: the plan names the group it becomes (nothing to download)',
              'saved as the group' in c.get('pack-status', {}).get('text', '') and c.get('pack-go', {}).get('text') == 'Save as a group',
              {k: c.get(k, {}).get('text') for k in ('pack-status', 'pack-go')})
        try: w.gshot(os.path.join(out, 'pack_import.png'), 'pack_import')
        except Exception as e: log('shot:', e)
        time.sleep(1)                                                 # the template's UI-RULE check runs 30 frames after a window shows
        L = w.text(W.PROF + r'\dsload.log') or ''
        tail = L[w.marks.get('dsload.log', 0):] if len(L) >= w.marks.get('dsload.log', 0) else L
        check('Import window: no UI-RULE / ESC-MISSING line (kit window template)', not re.search(r'UI-RULE|ESC-MISSING', tail),
              re.findall(r'(?:UI-RULE|ESC-MISSING)[^\n]*', tail)[:3])
        focus_game(w); w.a('key', vk=VK_ESCAPE); time.sleep(1.5)
        s = w.ui({'op': 'state'}); c = {x['name']: x for x in s.get('controls', [])}
        check('a real Escape closes the Import window first (the Mods window stays)', 'pack-code' not in c and s.get('open') == 1,
              {'open': s.get('open'), 'pack controls': sorted(n for n in c if n.startswith('pack'))[:4]})
        w.ui({'op': 'close'})
        bad = [l for l in (w.text(W.PROF + r'\dsload.log') or '').splitlines() if any(b in l for b in ('Crash!!!', 'Access violation'))]
        check('no crash line in dsload.log', not bad, bad[:3])
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: w.logs(out)
        except Exception as e: log('logs:', e)
        w.stop(); w.kill_all()
        json.dump(checks, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    bad = [c for c in checks if not c['ok']]
    log('RESULT', f'{len(checks) - len(bad)}/{len(checks)} checks')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
