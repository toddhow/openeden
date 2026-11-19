#!/usr/bin/env python3
"""Task 0339: robust frame statistics of a load-trace.jsonl (a p99 of a run with about 55 slow frames is the 56th worst
frame, so it jumps between 13 and 40 ms when two frames cross the limit): counts per band and the time in each band.
usage: frames.py <trace> --simstart=<sim seconds> [--scopes=a,b,c]"""
import json, sys, bisect, collections
args = [a for a in sys.argv[1:] if not a.startswith('--')]
opts = dict(a[2:].split('=', 1) for a in sys.argv[1:] if a.startswith('--') and '=' in a)
frames, scopes = [], []
for line in open(args[0]):
    d = json.loads(line)
    if 'f' in d: frames.append((d['f'], d['b'], d['e']))
    else: scopes.append((d['n'], d['b'], d['e'], d['v'], d['t']))
firstRun = min((s[1] for s in scopes if s[0] == 'run.time'), default=None)
allf = sorted(frames)
f0 = next((f for f, b, e in allf if firstRun is not None and b <= firstRun <= e), allf[0][0])
first = f0 + int(round(float(opts.get('simstart', 0)) * 60)) + 2
frames = sorted(f for f in frames if f[0] >= first)
ms = [(e - b) / 1000 for _, b, e in frames]
s = sorted(ms)
bands = [(0, 5), (5, 8), (8, 16.7), (16.7, 25), (25, 50), (50, 100), (100, 500), (500, 1e9)]
print('N %d median %.2f p95 %.2f p99 %.2f p99.9 %.2f max %.1f total %.1f s' % (
    len(ms), s[len(s) // 2], s[int(len(s) * .95)], s[int(len(s) * .99)], s[int(len(s) * .999)], s[-1], sum(ms) / 1000))
print('band(ms)      count   time(ms)')
for lo, hi in bands:
    sel = [x for x in ms if lo <= x < hi]
    print('%6g-%-6g %7d %10.1f' % (lo, hi, len(sel), sum(sel)))
# scope totals inside frames of each band class, and per band of the frame
main = None
for n, b, e, v, t in scopes:
    if n == 'frame.game.run' or n == 'run.world.update': main = t; break
sc = sorted((x for x in scopes if x[4] == main), key=lambda x: x[1])
starts = [x[1] for x in sc]
names = opts.get('scopes', 'world.navigator.wait,physics.neutral.actors,run.physics,physics.step,neutral.cell.build,neutralactor.create,obj.physics,phase.GameSimulation,neutral.pathfind,neutral.walk').split(',')
tot = collections.defaultdict(float); mid = collections.defaultdict(float); cnt = collections.Counter()
for f, b, e in frames:
    lo = bisect.bisect_left(starts, b); hi = bisect.bisect_right(starts, e)
    d = (e - b) / 1000
    for x in sc[lo:hi]:
        if x[0] in names and x[2] <= e:
            dur = (x[2] - x[1]) / 1000
            tot[x[0]] += dur; cnt[x[0]] += 1
            if 5 <= d < 25: mid[x[0]] += dur
print('scope                      calls    total(ms)  in 5-25ms frames(ms)')
for n in names:
    print('%-26s %7d %12.1f %12.1f' % (n, cnt[n], tot[n], mid[n]))
