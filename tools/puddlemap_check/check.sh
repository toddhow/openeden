#!/bin/bash
# tools/puddlemap_check (task 0095, stage W-d): visits the poses of a scenario file in a real game and takes a screenshot
# at each. The weather and the hour are pinned by a Lua global script; the game runs in a quiet window (it quits by
# itself). Used for the frame checks of the puddle map: puddles before (noise) and after (map), the debug views, the
# growth of a puddle while the rain wets the ground, and the retreat while it dries.
#
#   check.sh <build dir> <base config dir> <work dir> <scenario file> [pack file]
#
# <base config dir> holds the openmw.cfg and settings.cfg of a Morrowind setup (a COPY, never the real one; its
# `resources=` line must name the build's resources). <work dir> must be under I:/Projects/OpenEden-work. The optional
# pack is copied to <work>/user/openeden/puddles/puddles.edenpak (made by `openmw --bake-puddles`; never committed).
# Environment: OPENMW_EXE (default ./openmw.exe in the build dir, to compare the parent commit's), EXTRA_SETTINGS (lines
# appended to settings.cfg, for example "[Rendering]\nwetness puddle map = true"), FIXED (frame time, default 1/60).
#
# The scenario file has one command a line (# starts a comment):
#   region <name>            the region whose weather is set (default Bitter Coast Region)
#   weather <id>             clear, rain, ... (a weather record id)
#   hour <h>                 the game hour, set again at every pose
#   timescale <n>            game seconds per real second (default 1)
#   hold <s>                 seconds of simulation time between two poses (default 3)
#   first <s>                seconds before the first pose (default 4)
#   change_after <s> <id>    at that time change the weather to <id> (to watch the ground dry)
#   pose <name> <x> <y> <z> <yaw> <pitch>   a camera pose (yaw 0 faces north, +Y; pitch > 0 looks down)
#   probe <x> <y> <top>      (after a pose) a ray down at that point is cast at the shot; "PM probe" lines print the
#                            ground height it hits next to the baked <top> (checks the map's x and y orientation)
# The script prints the screenshot numbers with the pose names (files <work>/frames/screenshotNNN.png).
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
export OPENEDEN_FIXED_FRAME_TIME="${FIXED-0.0166667}"
BUILD=$1; BASE=$2; WORK=$3; SCEN=$4; PACK=${5:-}
HERE="$(cd "$(dirname "$0")" && pwd)"
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
case "$(native "$BASE")" in
    *"My Games"*) echo "refused: $BASE looks like the real game folder; pass a copy"; exit 2 ;;
esac
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/frames" "$WORK/mod/scripts"
cp "$BASE/openmw.cfg" "$WORK/config/openmw.cfg"
[ -f "$BASE/settings.cfg" ] && cp "$BASE/settings.cfg" "$WORK/config/settings.cfg"
touch "$WORK/config/settings.cfg"
[ -n "$EXTRA_SETTINGS" ] && printf '\n%b\n' "$EXTRA_SETTINGS" >> "$WORK/config/settings.cfg"
if [ -n "$PACK" ]; then
    mkdir -p "$WORK/user/openeden/puddles"
    cp "$PACK" "$WORK/user/openeden/puddles/puddles.edenpak"
fi
cp "$HERE"/mod/pmcheck.omwscripts "$WORK/mod/"
cp "$HERE"/mod/scripts/*.lua "$WORK/mod/scripts/"
python "$HERE/scenario.py" "$SCEN" > "$WORK/mod/scripts/pmcheck_poses.lua"
printf '\ndata="%s"\ncontent=pmcheck.omwscripts\n' "$(native "$WORK/mod")" >> "$WORK/config/openmw.cfg"
cd "$BUILD"
set +e
timeout "${TIMEOUT:-420}" "${OPENMW_EXE:-./openmw.exe}" --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --start "${START:-Seyda Neen}" --no-grab --random-seed 1 \
    ${EXTRA_ARGS} > "$WORK/log.txt" 2>&1
code=$?
set -e
cp "$WORK"/user/screenshots/* "$WORK/frames"/ 2>/dev/null || true
cp "$WORK/user/openmw.log" "$WORK/openmw.log" 2>/dev/null || cp "$WORK/config/openmw.log" "$WORK/openmw.log" 2>/dev/null || true
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
errors=$(grep -ciE "\[[0-9:.]+ E\]" "$WORK/openmw.log" "$WORK/log.txt" 2>/dev/null | awk -F: '{s+=$NF} END {print s+0}')
shots=$(ls "$WORK/frames" | wc -l)
echo "exit code $code, $dumps crash dumps, $errors error lines, $shots screenshots"
grep -hE "PM (weather|start|shot|done|no weather)" "$WORK/log.txt" "$WORK/openmw.log" 2>/dev/null | sort -u | head -60
if [ "$code" != 0 ] || [ "$dumps" != 0 ] || [ "$errors" != 0 ]; then echo "FAILED"; exit 1; fi
