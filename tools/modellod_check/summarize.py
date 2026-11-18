#!/usr/bin/env python3
# usage: summarize.py frames.txt [last_n]  -> median of every column over the last frames
import sys, statistics
f = sys.argv[1]; n = int(sys.argv[2]) if len(sys.argv) > 2 else 200
rows = [l.split() for l in open(f) if l.strip()]
hdr, data = rows[0], rows[1:][-n:]
out = {}
for i, h in enumerate(hdr):
    v = [float(r[i]) for r in data if len(r) > i and r[i] not in ('-1',)]
    if v: out[h] = statistics.median(v)
print(' '.join(f"{k}={out[k]:.3f}" for k in out))
