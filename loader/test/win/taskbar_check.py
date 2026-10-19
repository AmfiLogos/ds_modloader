#!/usr/bin/env python3
"""Windows VM check (player report 2026-10-19): after a soft restart (Apply in the Mods window) and a New game, the
game's window is still a normal app window with a taskbar button. The soft restart's B process made its window
WS_EX_NOACTIVATE while hidden and kept it: no taskbar button, Task Manager listed the game as a background process.

Driven by winrig (ds_multiplayer win/winbot, WINBOT_DIR), on the running VM with the agent up. Game started with the
player environment of the starter (winrig start_env) plus the control port. Needs a nosteam loader in the game copy.
  python3 taskbar_check.py [--mod gcd-cdr] [--expect-bug]
Exit 0 = every check as expected. Window state per step: taskbar_probe.ps1 (styles, owner, taskbar button by UI
Automation on Shell_TrayWnd)."""
import argparse, os, re, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.environ.get('WINBOT_DIR') or os.path.expanduser('~/ds_multiplayer/win/winbot'))
import winrig as W  # noqa: E402

PS = r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'


def probe(w, what):
    g = W.WORK + r'\wintaskbar\taskbar_probe.ps1'
    w.put(os.path.join(HERE, 'taskbar_probe.ps1'), g)
    out = w.a('run', exe=PS, args=f'-NoProfile -ExecutionPolicy Bypass -File "{w.expand(g)}"', wait_ms=60000, capture=True).get('out', '')
    print(f'--- {what}\n{out.strip()}')
    game = [l for l in out.splitlines() if 'class=UnityWndClass' in l and 'visible=1' in l]
    tb = re.search(r"taskbar buttons matching Dungeon: (.*)", out)
    f = dict(re.findall(r'(\w+)=(\S+)', game[0])) if game else {}
    return {'window': bool(game), 'noactivate': f.get('NOACTIVATE') == '1', 'toolwindow': f.get('TOOLWINDOW') == '1',
            'owner': f.get('owner', '0x0') != '0x0', 'taskbar': bool(tb) and tb.group(1).strip() != 'NONE'}


def new_game(w):
    """the player's clicks: New Game, Next (scenario), Confirm (settings), No (tutorial); the game's intro story then
    runs. Points are offsets in the 1728x1000 windowed game window (winrig's quarter-screen prefs), from its outer rect."""
    r = [p for p in w.a('ps', name='DungeonSettlers.exe')['procs'] if p['pid'] == w.pid][0]['windows'][0]
    for what, dx, dy, wait in (('New Game', 315, 407, 4), ('Next', 1113, 886, 3), ('Confirm', 1113, 886, 4), ('No (tutorial)', 785, 623, 8)):
        w.a('click', x=r['x'] + dx, y=r['y'] + dy); print(f'click {what}'); time.sleep(wait)
    s = w.status()
    print(f"after New game: scene {s.get('scene')} busy {s.get('busy')} loading {(s.get('map_loading') or {}).get('is_loading')}")


def normal(p):
    return p['window'] and not p['noactivate'] and not p['toolwindow'] and not p['owner'] and p['taskbar']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mod', default='gcd-cdr', help='the mod switched by the Apply (a soft restart)')
    ap.add_argument('--expect-bug', action='store_true', help='the unfixed loader: B must show the bug (reproduction)')
    a = ap.parse_args()
    w = W.Win(); w.up()
    w.prepare(lang=0, consent='no', active=['custom-modes', 'gcd-cdr', 'hell-difficulty', 'multiplayer'], settings={})
    w.start()
    res = []
    p = probe(w, 'A at the title (as started)'); res.append(('A title: normal app window', normal(p), p))
    old = w.pid
    w.ui({'op': 'open'}); w.ui({'op': 'click', 'target': 'mod:' + a.mod}); w.ui({'op': 'click', 'target': 'apply'})
    w.wait_restart(old, 240)
    w.wait(w.on_title, 'the restarted game (B) at the title', 120)
    time.sleep(2)                                                # B has the port once A quit (after the swap); the taskbar
                                                                 # adds the button right after the show
    p = probe(w, f'B at the title (pid {old} -> {w.pid})'); res.append(('B title (after Apply)', p, p))
    new_game(w)
    p = probe(w, 'B in the new game'); res.append(('B new game', p, p))
    w.stop()
    ok = res[0][1]
    for name, p, _ in res[1:]:
        good = normal(p)
        want = not good if a.expect_bug else good
        print(f'{"PASS" if want else "FAIL"} {name}: {"normal app window" if good else "NOT a normal app window"} {p}')
        ok &= want
    print(f'{"PASS" if res[0][1] else "FAIL"} {res[0][0]} {res[0][2]}')
    print('RESULT', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
