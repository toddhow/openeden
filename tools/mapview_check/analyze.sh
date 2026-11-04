#!/bin/bash
# Task 0066 stage e: frame-time summary of a stats file from run_ascent.sh (frame, wall_ms, wait_ms, rss, gpu, ...).
# usage: analyze.sh <stats.txt> [first frame] [last frame]
awk -v a="${2:-0}" -v b="${3:-1000000000}" 'NR>1 && $1>=a && $1<=b { n++; w[n]=$2; f[n]=$1; if ($2>worst) { worst=$2; wf=$1 } if ($2>16.7) over++ ; rss=$4; dev=$5 }
 END { if (!n) { print "no frames"; exit } m=asort(w, s); printf "frames %d  median %.2f  p99 %.2f  p99.9 %.2f  worst %.2f (frame %d)  over 16.7 ms: %d  rss %d MiB  device %d MiB\n", n, s[int(n/2)], s[int(n*0.99)], s[int(n*0.999)], worst, wf, over, rss, dev }' "$1"
