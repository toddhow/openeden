#!/bin/bash
# Task 0339: one measured run of the 0323 walk with [Game] neutral actor walk through / long paths, with the machine's
# state recorded (other openmw, compile and link processes, free virtual memory).
#   navcost.sh <name> <mode> <off|W|L|B> [navdb]
cd "$(dirname "$0")/../.."
. tools/streaming_check/env.sh
name=$1; mode=$2; cfg=$3; navdb=$4
case $cfg in W) x='\n[Game]\nneutral actor walk through = true\n';; L) x='\n[Game]\nneutral actor long paths = true\n';; B) x='\n[Game]\nneutral actor walk through = true\nneutral actor long paths = true\n';; *) x='';; esac
RES=I:/Projects/OpenEden-work/followers/nav339; mkdir -p $RES
W=I:/Projects/OpenEden-work/streaming/run339_$name
env_before=$(powershell -c "\$p=Get-Process openmw,cl,link,ninja,cc1plus -ErrorAction SilentlyContinue | group name | % { \$_.Name+'='+\$_.Count }; 'free='+[int]((Get-CimInstance Win32_OperatingSystem).FreeVirtualMemory/1MB)+'GB procs='+(\$p -join ',')")
NAVDB=$navdb bash tools/streaming_check/run.sh I:/Projects/OpenEden-work/followers/build I:/Projects/OpenEden-work/integ2/base "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data" "$W" "$mode" 0 "$(printf '%b' "$x")" > $RES/$name.run 2>&1
env_after=$(powershell -c "\$p=Get-Process openmw,cl,link,ninja,cc1plus -ErrorAction SilentlyContinue | group name | % { \$_.Name+'='+\$_.Count }; 'free='+[int]((Get-CimInstance Win32_OperatingSystem).FreeVirtualMemory/1MB)+'GB procs='+(\$p -join ',')")
sim=$(grep -oE "S323 start sim=[0-9.]+" "$W/log.txt" | cut -d= -f2)
{ echo "== $name mode=$mode cfg=$cfg navdb=$navdb"; echo "before: $env_before"; echo "after:  $env_after"
  grep -E "S323 done" "$W/log.txt" | cut -c1-140
  python tools/streaming_check/frames.py "$W/user/load-trace.jsonl" --simstart=$sim ${SCOPES:+--scopes=$SCOPES}
  python tools/streaming_check/analyze.py "$W/user/load-trace.jsonl" 0 --simstart=$sim --top=30 --worst=6
  grep -c "NACTNAV\|NACTWALK" "$W/log.txt" | sed 's/^/NACT lines: /'; } > $RES/$name.txt 2>&1
[ -n "$KEEP_NAVDB" ] && cp "$W/user/navmesh.db" $RES/navmesh_$name.db
[ -n "$KEEP_TRACE" ] && cp "$W/user/load-trace.jsonl" $RES/$name.jsonl
cp "$W/log.txt" $RES/$name.log
rm -rf "$W"
echo done $name
