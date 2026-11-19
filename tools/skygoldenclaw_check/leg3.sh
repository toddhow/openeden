#!/bin/bash
# tools/skygoldenclaw_check/leg3.sh (task 0335): the return to Riverwood alone (a way to look at Lucan's topics with the quest at
# stage 60 and the claw held): the quest stages and the claw are the developer aids' (stand-ins). env TOPIC (dialogue debug topic hex).
HERE=$(cd "$(dirname "$0")" && pwd)
export CELLID=RiverwoodRiverwoodTrader
export ACTIVATE="${TALK:-3600},'78970'"
export SETTINGS_EXTRA="golden claw = true\nscene park seconds = 5\nstory test quest = 039645:10@3,039645:40@4,039645:50@5,039645:60@6\nstory test delay = 1\ninventory test items = 00039647\n${TOPIC:+dialogue debug topic = $TOPIC\n}${SETTINGS_EXTRA:-}"
IN="$4.input.txt"
: > "$IN"
END=$(bash "$HERE/sweep.sh" $((${TALK:-3600} + 100)) ${REPS:-3} "$IN" 1)
export INPUT="$IN"
exec bash "$HERE/golden.sh" "$1" "$2" "$3" "$4" "${5:-$END}"
