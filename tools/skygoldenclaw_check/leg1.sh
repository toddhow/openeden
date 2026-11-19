#!/bin/bash
# tools/skygoldenclaw_check/leg1.sh (task 0335): MS13 leg 1 in the Riverwood Trader. The scene MS13LucanCamillaSceneView is started
# by the developer aid (`story test quest = 038386:10`: stand-in, see the task doc), plays its phases (Lucan sees the player in the
# cell, phase 4), ends and sets MS13 stage 5; then the player talks to Lucan (the harness activates him, a stand-in for the
# crosshair) and real mouse clicks choose.
#   leg1.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$4
LUCAN=${LUCAN:-78970} # 0x01347A Lucan's base (the harness takes the nearest reference of it)
export CELLID=RiverwoodRiverwoodTrader
export ACTIVATE="${TALK:-5600},'$LUCAN'"
export SETTINGS_EXTRA="golden claw = true\nscene park seconds = 5\nstory test quest = 038386:10@5\nstory test delay = 1\n${SETTINGS_EXTRA:-}"
IN="$WORK.input.txt"
: > "$IN"
printf '%s shot\n' $((${TALK:-5600} + 60)) >> "$IN"
END=$(bash "$HERE/sweep.sh" $((${TALK:-5600} + 200)) ${REPS:-6} "$IN" 1)
export INPUT="$IN"
exec bash "$HERE/golden.sh" "$1" "$2" "$3" "$4" "${5:-$END}"
