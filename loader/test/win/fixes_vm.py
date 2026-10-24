#!/usr/bin/env python3
"""Windows VM run of the Mods window's Fixes tab (kit v0.1.26, lane dsfixes' feature) through winrig (ds_multiplayer
win/winbot, WINBOT_DIR), VM running visibly with the agent up and the kit under test installed.
  python3 fixes_vm.py [--out DIR]
Checks: the Fixes tab lists the fixes (gcd-cdr, sound-fix) and no game mod; gcd-cdr has "On in all worlds" on by default,
sound-fix has the line "The same in all worlds."; switching "On in all worlds" off and on writes fixes.json each time; the
game still answers. What the profile had (fixes.json) is moved aside and back; test files go to C:\\dsm\\to_delete.
Exit 0 = every check passed."""
import argparse, json, ntpath, os, sys, time

sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402

FIXES = W.DATA + r'\Mods\fixes.json'
TO_DEL = W.WORK + r'\to_delete\fixes_vm_' + time.strftime('%Y%m%d-%H%M%S')
checks = []
log = lambda *a: print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)[:400]}); log('PASS' if ok else 'FAIL', name, '' if ok else detail)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='fixes_vm_out'); a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    w = W.Win(); w.up(); w.kill_all()
    kept = w.exists(FIXES)
    if kept: w.move(FIXES, TO_DEL + r'\kept_fixes.json')
    def state():
        return {c['name']: c for c in w.ui({'op': 'state'}).get('controls', [])}
    def all_worlds():
        return ((w.json_file(FIXES) or {}).get('all_worlds') or {}).get('gcd-cdr')
    try:
        w.prepare(lang=0, consent='no')
        w.start()
        w.ui({'op': 'open'}); time.sleep(1)
        r = w.ui({'op': 'click', 'target': 'tab-mods:fixes'}); time.sleep(1)
        check('the Fixes tab opens', r.get('ok') == 1, r)
        st = state(); mods = sorted(k[4:] for k in st if k.startswith('mod:'))
        check('Fixes tab: gcd-cdr and sound-fix, no game mod', 'gcd-cdr' in mods and 'sound-fix' in mods and 'hell-difficulty' not in mods
              and 'zetu-suite' not in mods and 'multiplayer' not in mods, mods)
        g, s = st.get('allworlds:gcd-cdr', {}), st.get('allworlds:sound-fix', {})
        check('gcd-cdr: "On in all worlds" is a switch, on by default (no fixes.json yet)', g.get('kind') == 1 and g.get('on') == 1, g)
        check('sound-fix: "The same in all worlds." (client fix, no switch)', s.get('kind') != 1 and 'same in all worlds' in s.get('text', '').lower(), s)
        w.gshot(os.path.join(a.out, 'fixes_tab_en.png'))
        w.ui({'op': 'click', 'target': 'allworlds:gcd-cdr'}); time.sleep(1.5)
        g = state().get('allworlds:gcd-cdr', {})
        check('switch off: the switch shows off and fixes.json says gcd-cdr false', g.get('on') == 0 and all_worlds() is False, (g, w.json_file(FIXES)))
        w.ui({'op': 'click', 'target': 'allworlds:gcd-cdr'}); time.sleep(1.5)
        g = state().get('allworlds:gcd-cdr', {})
        check('switch on again: on and fixes.json says gcd-cdr true', g.get('on') == 1 and all_worlds() is True, (g, w.json_file(FIXES)))
        w.ui({'op': 'close'})
        st = w.cmd(cmd='status')
        check('the game still answers after the Fixes tab', bool(st), str(st)[:200])
        L = w.text(W.PROF + r'\dsload.log') or ''
        check('no CRASH line in dsload.log', 'CRASH' not in L, [l for l in L.splitlines() if 'CRASH' in l][:3])
    except Exception as e:
        check('run finished', False, repr(e))
    finally:
        try: w.logs(a.out)
        except Exception as e: log('logs', e)
        try: w.stop()
        except Exception: pass
        w.kill_all()
        if w.exists(FIXES): w.move(FIXES, TO_DEL + r'\test_fixes.json')
        if kept: w.move(TO_DEL + r'\kept_fixes.json', FIXES)
        json.dump(checks, open(os.path.join(a.out, 'summary.json'), 'w'), indent=1)
        log(f"RESULT {sum(c['ok'] for c in checks)}/{len(checks)} checks")
    sys.exit(0 if checks and all(c['ok'] for c in checks) else 1)


if __name__ == '__main__':
    main()
