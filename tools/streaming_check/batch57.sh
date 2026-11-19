#!/bin/bash
# Task 0357: before/after runs of the 0323 measurement with ONE build. The "before" variants switch the two new settings
# off ([Game] neutral preload models = false, neutral async nav mesh updater threads = 1), the "after" variant leaves the
# defaults. Keeps only summaries.
#   batch57.sh <build dir> <results dir> <name|mode|variant|navdb> ...
#   variant: before | after | nav (threads only) | pre (preload only)
cd "$(dirname "$0")/../.."
. tools/streaming_check/env.sh
BUILD=$1; RES=$2; shift 2; mkdir -p "$RES"
OFFPRE='[Game]\nneutral preload models = false\n'
ONE='[Navigator]\nneutral async nav mesh updater threads = 1\n'
for spec in "$@"; do
  IFS='|' read -r name mode variant navdb <<< "$spec"
  case "$variant" in
    before) extra="$OFFPRE$ONE" ;;
    nav) extra="$OFFPRE" ;;
    pre) extra="$ONE" ;;
    *) extra="" ;;
  esac
  W=I:/Projects/OpenEden-work/combat3/st57/run_$name
  NAVDB=$navdb bash tools/streaming_check/run.sh "$BUILD" I:/Projects/OpenEden-work/integ2/base "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data" "$W" "$mode" 0 "$(printf '%b' "$extra")" > "$RES/$name.run" 2>&1
  sim=$(grep -oE "S323 start sim=[0-9.]+" "$W/log.txt" | cut -d= -f2)
  { echo "== $name mode=$mode variant=$variant navdb=$navdb"; grep -E "S323 done" "$W/log.txt" | cut -c1-140;
    A="$W/user/load-trace.jsonl"
    if [ -n "$sim" ]; then python tools/streaming_check/analyze.py "$A" 0 --simstart=$sim --top=12 --worst=6; python tools/streaming_check/attrib.py "$A" --simstart=$sim | head -30;
    else python tools/streaming_check/analyze.py "$A" 0 --top=12 --worst=6; python tools/streaming_check/attrib.py "$A" | head -30; fi
    python tools/streaming_check/totals57.py "$A"
    grep -c "Failed to preload mesh" "$W/log.txt" | sed 's/^/preload failures: /'
    grep -E "Navigator|nav ?mesh updater" "$W/log.txt" | head -3; } > "$RES/$name.txt" 2>&1
  [ -n "${KEEP_NAVDB:-}" ] && [ "$name" = "$KEEP_NAVDB" ] && cp "$W/user/navmesh.db" "$RES/navmesh_$name.db"
  cp "$W/log.txt" "$RES/$name.log"
  rm -rf "$W"
done
echo BATCH_DONE >> "$RES/done.txt"
