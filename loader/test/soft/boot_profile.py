#!/usr/bin/env python3
"""Where B's boot time goes: A at the title on copy V -> prewarm -> `sample` B's main thread from its start to "ready".
GameAssembly offsets are named from ~/ds_multiplayer/build/methods.tsv (local, game-derived, never committed).
  boot_profile.py <out>   -> <out>/sample.txt (raw), <out>/top.txt (inclusive / self time per method, main thread)
"""
import bisect, os, re, subprocess, sys, time, threading
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import v13_test as t

OUT = os.path.expanduser(sys.argv[1]); os.makedirs(OUT, exist_ok=True)
METHODS = os.path.expanduser('~/ds_multiplayer/build/methods.tsv')


def names():
    rv, nm = [], []
    for l in open(METHODS):
        a, n = l.rstrip('\n').split('\t', 1)
        rv.append(int(a, 16)); nm.append(n)
    o = sorted(range(len(rv)), key=rv.__getitem__)
    return [rv[i] for i in o], [nm[i] for i in o]


def main():
    if t.game_pids():
        raise SystemExit('copy V already running')
    pair, own = t.lease()
    g = threading.Thread(target=t.guard, daemon=True); g.start()
    try:
        a = t.start_a(t.P1)
        t.wait_status(t.A_SOCK, lambda s: s.get('scene') == 1 and not s.get('busy'), 'A title')
        time.sleep(2)
        t.clear_swap_flags()
        t.request('prewarm_request.json', {'profile': t.P2, 'env': t.envk(1)})
        b = None; t0 = time.time()
        while not b and time.time() - t0 < 10:
            b = next((p for p in t.game_pids() if p != a), None); time.sleep(0.005)
        print('B pid', b, 'seen', round(time.time() - t0, 3), 's after the request', flush=True)
        subprocess.run(['sample', str(b), '9', '1', '-mayDie', '-file', os.path.join(OUT, 'sample.txt')], capture_output=True)
        print('ready pid', t.ready_pid())
    finally:
        t.finish(OUT, {'mode': 'boot_profile'})
        t.release(pair, own)
    analyse(os.path.join(OUT, 'sample.txt'))


def analyse(path):
    rv, nm = names()
    txt = open(path, errors='replace').read()
    # main thread block only
    m = re.search(r'\n\s+\d+ Thread_\d+\s+DispatchQueue_1: com.apple.main-thread.*?\n(?=\s+\d+ Thread_)', txt, re.S)
    block = m.group(0) if m else txt
    incl, self_t = {}, {}
    stack = []
    for line in block.splitlines():
        mm = re.match(r'^([\s+!:|]*)(\d+) (.*)$', line)
        if not mm:
            continue
        depth = len(mm.group(1)); n = int(mm.group(2)); what = mm.group(3)
        ga = re.search(r'\(in GameAssembly\.dylib\) \+ (\d+)', what)
        if ga:
            off = int(ga.group(1)); i = bisect.bisect_right(rv, off) - 1
            label = nm[i] if i >= 0 else hex(off)
        else:
            label = re.sub(r'\s+\[0x[0-9a-f]+\].*$', '', what)[:90]
        while stack and stack[-1][0] >= depth:
            stack.pop()
        stack.append((depth, label, n))
        if label not in [s[1] for s in stack[:-1]]:
            incl[label] = incl.get(label, 0) + n
    with open(os.path.join(os.path.dirname(path), 'top.txt'), 'w') as f:
        for k, v in sorted(incl.items(), key=lambda x: -x[1])[:120]:
            f.write(f'{v:6d} {k}\n')
    print(open(os.path.join(os.path.dirname(path), 'top.txt')).read()[:6000])


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[2] == 'analyse':
        analyse(os.path.join(OUT, 'sample.txt'))
    else:
        main()
