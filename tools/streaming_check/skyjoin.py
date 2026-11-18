#!/usr/bin/env python3
"""Task 0323: joins the pack's SKYFRAME lines (OPENEDEN_PACK_FRAME_TRACE=1; absolute steady-clock microseconds) with a
load trace: for the hitch frames, how much of the pack's GameSimulation callback is the Papyrus host and how much the rest.
usage: skyjoin.py <trace> <log> [--simstart=S] [--hitch=25]"""
import json, re, sys, bisect
args = [a for a in sys.argv[1:] if not a.startswith('--')]
opts = dict(a[2:].split('=') for a in sys.argv[1:] if a.startswith('--') and '=' in a)
frames, gs = [], []
runtime = None
for line in open(args[0]):
    d = json.loads(line)
    if 'f' in d: frames.append((d['f'], d['b'], d['e']))
    elif d['n'] == 'phase.GameSimulation': gs.append((d['b'], d['e']))
    elif d['n'] == 'run.time' and runtime is None: runtime = d['b']
frames.sort()
if 'simstart' in opts:
    f0 = next((f for f, b, e in frames if runtime is not None and b <= runtime <= e), frames[0][0])
    first = f0 + int(round(float(opts['simstart']) * 60)) + 2
    frames = [f for f in frames if f[0] >= first]
sky = []
for line in open(args[1], errors='replace'):
    m = re.search(r'SKYFRAME b=(\d+) e=(\d+) papyrus=(\d+) rest=(\d+)', line)
    if m: sky.append(tuple(int(x) for x in m.groups()))
sky.sort()
starts = [s[0] for s in sky]
limit = float(opts.get('hitch', 25)) * 1000
hitch = [f for f in frames if f[2] - f[1] > limit]
tp = tr = tg = 0; n = 0
worst = []
for f, b, e in hitch:
    lo = bisect.bisect_left(starts, b); hi = bisect.bisect_right(starts, e)
    p = sum(s[2] for s in sky[lo:hi]); r = sum(s[3] for s in sky[lo:hi])
    g = sum(ge - gb for gb, ge in gs if b <= gb <= e)
    tp += p; tr += r; tg += g; n += 1
    worst.append((p + r, f, p, r, g))
print('hitch frames %d: phase.GameSimulation %.1f ms, of which papyrus.frame %.1f ms, rest %.1f ms (only frames the pack logged as slow, >= 5 ms, are counted in the split)' % (n, tg / 1000, tp / 1000, tr / 1000))
for t, f, p, r, g in sorted(worst, reverse=True)[:8]:
    print('  frame %d: GameSimulation %.1f ms = papyrus %.1f + rest %.1f (logged slow part)' % (f, g / 1000, p / 1000, r / 1000))
print('SKYFRAME lines in the log: %d, papyrus total in them %.1f ms, rest %.1f ms' % (len(sky), sum(s[2] for s in sky) / 1000, sum(s[3] for s in sky) / 1000))
