#!/usr/bin/env python3
"""Task 0323: table of the batch results (analyze.py texts). usage: summarize.py <results dir> [scope ...]"""
import re, sys, glob, os, collections
d = sys.argv[1]; extra = sys.argv[2:]
rows = []
for p in sorted(glob.glob(os.path.join(d, '*.txt'))):
    name = os.path.basename(p)[:-4]
    t = open(p).read()
    m = re.search(r'frames (\d+)\s+median ([\d.]+)\s+p99 ([\d.]+)\s+p99.9 ([\d.]+)\s+max ([\d.]+)\s+over 25 ms: (\d+)', t)
    if not m: continue
    h = re.search(r'hitch frames \(> 25 ms\): (\d+), mean ([\d.]+) ms', t)
    tot = {k: float(v) for k, v in re.findall(r'^\s{2}(\S+)\s+([\d.]+) ms\s+\(', t, re.M)}
    hit = {k: float(v) for k, v in re.findall(r'^\s{2}(\S+)\s+mean/hitch\s+([\d.]+) ms', t, re.M)}
    rows.append((name, m.groups(), h.groups() if h else ('0', '0'), tot, hit))
print('%-16s %6s %7s %8s %9s %9s %6s %9s' % ('run', 'frames', 'median', 'p99', 'p99.9', 'max', '>25', 'hitchmean'))
for n, m, h, tot, hit in rows:
    print('%-16s %6s %7s %8s %9s %9s %6s %9s' % (n, m[0], m[1], m[2], m[3], m[4], m[5], h[1]))
for s in extra:
    print('\n%s: total ms in the window / mean ms per hitch' % s)
    for n, m, h, tot, hit in rows:
        print('  %-16s %9.1f  %8.2f' % (n, tot.get(s, 0), hit.get(s, 0)))
