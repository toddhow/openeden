#!/bin/bash
# Task 0066 stage e: frame-time summary of the climb itself. The stats file has no clock, so the climb starts at the first
# frame after frame 1000 whose draw count differs from the one before (the player holds still for the first 25 seconds of
# the script) and lasts `seconds` (default 85: 40 up, 5 at the top, 40 down) of summed frame time.
# usage: analyze_ascent.sh <stats.txt> [seconds]
awk -v span="${2:-85}" 'NR>1 { f[NR]=$1; w[NR]=$2; d[NR]=$8; tri[NR]=$9; rss[NR]=$4; dev[NR]=$5; last=NR }
 END {
   start=0; for (i=1002; i<=last; i++) if (d[i]!=d[i-1]) { start=i; break }
   if (!start) { print "no change of the draw count found: the player did not move"; exit }
   cum=0; n=0; for (i=start; i<=last && cum<span*1000; i++) { cum+=w[i]; n++; v[n]=w[i]; at[n]=cum/1000; if (w[i]>worst) { worst=w[i]; wi=n }
      if (w[i]>16.7) over++; if (w[i]>33.4) over2++; if (rss[i]>rmax) rmax=rss[i]; if (dev[i]>dmax) dmax=dev[i]; if (d[i]>dmaxd) dmaxd=d[i]; if (tri[i]>tmax) tmax=tri[i] }
   m=asort(v, s)
   printf "climb from frame %d: %d frames in %.1f s  median %.2f  p99 %.2f  p99.9 %.2f  worst %.2f ms at %.1f s  over 16.7 ms: %d  over 33 ms: %d  peak rss %d MiB  peak device %d MiB  peak draws %d  peak triangles %d\n", f[start], n, cum/1000, s[int(n/2)], s[int(n*0.99)], s[int(n*0.999)], worst, at[wi], over, over2, rmax, dmax, dmaxd, tmax }' "$1"
