#!/bin/bash
# usage: skyrim.sh <out name> <start spot, e.g. @WhiterunWorld,25670,-7633> <w> <h> <shadows 0|1>
M=/i/Projects/OpenEden-work/modellod; export OPENEDEN_QUIET_WINDOW=1
HERE=$(cd "$(dirname "$0")" && pwd)
PATH="/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin/Release:/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin:$PATH"
S="[Video]\nresolution x = $3\nresolution y = $4\nfullscreen = false\nwindow mode = 2\nvsync mode = 0\nframerate limit = 0\n[Sound]\nmaster volume = 0.0\n[Rendering]\ngpu timing = true\nframe counters = true\n[Shadows]\n"
if [ "$5" = 1 ]; then S="${S}enable shadows = true\nobject shadows = true\nterrain shadows = true\nactor shadows = true\n"; else S="${S}enable shadows = false\n"; fi
S="${S}[Camera]\nviewing distance = 7168\n"
rm -f $M/$1.frames.txt
OPENEDEN_FIXED_FRAME_TIME=0.0166667 OPENEDEN_FRAME_STATS_FILE=$M/$1.frames.txt SETTINGS_EXTRA="$S" CHECK_TIMEOUT=280 \
  bash $HERE/../neutral_session_check/check.sh I:/Projects/OpenEden-work/papyrus/build I:/Projects/OpenEden-work/integ2/base \
  "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data" $M/sky_$1 "$2" 1800 > $M/sky_$1.out 2>&1
echo "$1 $(tail -1 $M/sky_$1.out) $(python $HERE/summarize.py $M/$1.frames.txt 200)" >> $M/results_sky.txt
