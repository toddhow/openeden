#!/usr/bin/env python3
"""Task 0323: summary of a load-trace.jsonl (frames and scopes).
usage: analyze.py <trace> [first frame=700] [last frame] [--hitch ms=25] [--top n=24]
Prints frame-time statistics and, for the frames over the hitch limit, the mean main-thread time of every scope in them
(nested scopes overlap, as in research note 0026)."""
import json, sys, bisect, collections
args = [a for a in sys.argv[1:] if not a.startswith('--')]
opts = dict(a[2:].split('=') for a in sys.argv[1:] if a.startswith('--') and '=' in a)
path = args[0]
first = int(args[1]) if len(args) > 1 else 700
last = int(args[2]) if len(args) > 2 else 10**9
limit = float(opts.get('hitch', 25)) * 1000
top = int(opts.get('top', 24))
frames, scopes = [], []
for line in open(path):
    d = json.loads(line)
    if 'f' in d: frames.append((d['f'], d['b'], d['e']))
    else: scopes.append((d['n'], d['b'], d['e'], d['v'], d['t']))
# --simstart=<sim seconds of the S323 start line>: the window starts at the frame whose simulation time that is
# (the frame with the first Game::run time scope is simulation second 0; a frame is 1/60 s at the fixed frame time)
if 'simstart' in opts:
    firstRun = min((s[1] for s in scopes if s[0] == 'run.time'), default=None)
    allf = sorted(frames)
    f0 = next((f for f, b, e in allf if firstRun is not None and b <= firstRun <= e), allf[0][0])
    first = f0 + int(round(float(opts['simstart']) * 60)) + 2
frames = [f for f in frames if first <= f[0] <= last]
frames.sort()
main = None
for n, b, e, v, t in scopes:
    if n == 'frame.game.run' or n == 'run.world.update': main = t; break
ms = sorted((e - b) / 1000 for _, b, e in frames)
def pct(p): return ms[min(len(ms) - 1, int(len(ms) * p))]
print('frames %d  median %.2f  p99 %.2f  p99.9 %.2f  max %.2f  over %.0f ms: %d  over 16.7: %d' % (
    len(ms), pct(.5), pct(.99), pct(.999), ms[-1], limit / 1000, sum(1 for x in ms if x * 1000 > limit), sum(1 for x in ms if x > 16.7)))
# frame times are measured from the end of the previous frame (note 0026): use the gap-free window [prev end, end]
ends = [f[2] for f in frames]
ms_scopes = [s for s in scopes if s[4] == main]
ms_scopes.sort(key=lambda s: s[1])
starts = [s[1] for s in ms_scopes]
hitch = [(f, b, e) for f, b, e in frames if (e - b) > limit]
acc = collections.defaultdict(float); cnt = collections.Counter(); mx = collections.defaultdict(float)
for f, b, e in hitch:
    lo = bisect.bisect_left(starts, b); hi = bisect.bisect_right(starts, e)
    seen = collections.defaultdict(float)
    for s in ms_scopes[lo:hi]:
        if s[2] <= e: seen[s[0]] += (s[2] - s[1]) / 1000
    for k, v in seen.items(): acc[k] += v; cnt[k] += 1; mx[k] = max(mx[k], v)
print('hitch frames (> %.0f ms): %d, mean %.1f ms, worst %.1f ms' % (limit / 1000, len(hitch),
      sum((e - b) for _, b, e in hitch) / 1000 / max(1, len(hitch)), max([(e - b) / 1000 for _, b, e in hitch] or [0])))
rows = sorted(acc.items(), key=lambda kv: -kv[1])[:top]
for k, v in rows:
    print('  %-34s mean/hitch %7.2f ms   max %7.2f ms   in %d hitches' % (k, v / max(1, len(hitch)), mx[k], cnt[k]))
# total main-thread time over all measured frames, by scope (what the steady frame costs)
tot = collections.defaultdict(float)
t0 = frames[0][1] if frames else 0; t1 = frames[-1][2] if frames else 0
for s in ms_scopes:
    if s[1] >= t0 and s[2] <= t1: tot[s[0]] += (s[2] - s[1]) / 1000
print('main-thread scope totals over the window (ms, %d frames):' % len(frames))
for k, v in sorted(tot.items(), key=lambda kv: -kv[1])[:top]:
    print('  %-34s %9.1f ms  (%.3f ms/frame)' % (k, v, v / max(1, len(frames))))
# --worst=N: the N slowest frames, each with its biggest main-thread scopes (leaf-ish names only: the loop and frame
# wrappers are skipped)
if 'worst' in opts:
    skip = {'loop.callback', 'frame.game.run', 'run.world.update', 'grid.total', 'load.insertcell', 'cell.load', 'insert.objects', 'loop.record.submit'}
    for f, b, e in sorted(frames, key=lambda f: -(f[2] - f[1]))[:int(opts['worst'])]:
        lo = bisect.bisect_left(starts, b); hi = bisect.bisect_right(starts, e)
        d = collections.defaultdict(float)
        for s in ms_scopes[lo:hi]:
            if s[2] <= e and s[0] not in skip: d[s[0]] += (s[2] - s[1]) / 1000
        print('worst frame %d %.1f ms: %s' % (f, (e - b) / 1000, ', '.join('%s %.1f' % kv for kv in sorted(d.items(), key=lambda kv: -kv[1])[:6])))
