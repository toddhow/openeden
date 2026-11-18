#!/bin/bash
# tools/streaming_check (task 0323): runs the measurement matrix one run at a time and keeps only the summaries.
#   batch.sh <results dir> <name|mode|incr|extra settings|navdb> ...
# After each run analyze.py's text goes to <results>/<name>.txt, the log's S323 lines too; the run folder is deleted
# (the first trace of each mode is kept as <results>/<name>.jsonl when KEEP_TRACE=1).
cd "$(dirname "$0")/../.."
. tools/streaming_check/env.sh
RES=$1; shift; mkdir -p "$RES"
for spec in "$@"; do
  IFS='|' read -r name mode incr extra navdb <<< "$spec"
  W=I:/Projects/OpenEden-work/streaming/run_$name
  NAVDB=$navdb bash tools/streaming_check/run.sh I:/Projects/OpenEden-work/followers/build I:/Projects/OpenEden-work/integ2/base "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data" "$W" "$mode" "$incr" "$(printf '%b' "$extra")" > "$RES/$name.run" 2>&1
  sim=$(grep -oE "S323 start sim=[0-9.]+" "$W/log.txt" | cut -d= -f2)
  { echo "== $name mode=$mode incr=$incr extra=$extra navdb=$navdb"; grep -E "S323 done" "$W/log.txt" | cut -c1-140;
    if [ -n "$sim" ]; then python tools/streaming_check/analyze.py "$W/user/load-trace.jsonl" 0 --simstart=$sim --top=60 --worst=6;
    else python tools/streaming_check/analyze.py "$W/user/load-trace.jsonl" 0 --top=60 --worst=6; fi; } > "$RES/$name.txt" 2>&1
  [ -n "${KEEP_NAVDB:-}" ] && [ "$name" = "$KEEP_NAVDB" ] && cp "$W/user/navmesh.db" "$RES/navmesh_$name.db"
  [ "${KEEP_TRACE:-}" = 1 ] && cp "$W/user/load-trace.jsonl" "$RES/$name.jsonl"
  cp "$W/log.txt" "$RES/$name.log"
  rm -rf "$W"
done
echo BATCH_DONE >> "$RES/done.txt"
