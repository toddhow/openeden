#!/bin/bash
# tools/skygoldenclaw_check/golden.sh (task 0335): see docs/tasks/0335-skyrim-golden-claw.md for the PLAN of the scenario.
#   golden.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
HERE=$(cd "$(dirname "$0")" && pwd)
export LOADARG="--start ${CELLID:-RiverwoodTrader}"
export EVERY=${EVERY:-300} RADIUS=${RADIUS:-1200} NEAR=${NEAR:-6}
export SETTINGS_EXTRA="quest alias packages = true\nmap = true\nplayer level = 10\nrest = true\nactor cell moves = true\nservices = true\ndungeon mechanics = true\ncombat ai = true\ncreatures = true\ncombat factions = true\ncombat trace = true\n${SETTINGS_EXTRA:-}"
[ -n "${INPUT:-}" ] && export INPUT
exec bash "$HERE/check.sh" "$1" "$2" "$3" "$4" "${5:-3000}"
