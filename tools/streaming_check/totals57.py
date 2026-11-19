#!/usr/bin/env python3
"""Task 0357: whole-run totals (all frames) of a few scopes: main-thread model reads, the navigator wait, the
preloader's own work. usage: totals57.py <trace>"""
import json, sys, collections
tot = collections.defaultdict(lambda: [0.0, 0])
for line in open(sys.argv[1]):
    d = json.loads(line)
    if 'n' not in d:
        continue
    tot[d['n']][0] += (d['e'] - d['b']) / 1000.0
    tot[d['n']][1] += 1
for n in ('world.navigator.wait', 'model.create', 'neutralactor.create', 'insert.objects', 'cell.load',
          'preload.operate', 'preload.model', 'preload.land', 'preload.animations', 'preload.parts'):
    t = tot.get(n)
    if t:
        print('total %-24s %10.1f ms  n=%d' % (n, t[0], t[1]))
