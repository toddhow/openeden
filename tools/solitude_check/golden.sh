#!/bin/bash
# tools/solitude_check/golden.sh (task 0346): the settings of the Solitude scenarios; see docs/tasks/0346-skyrim-solitude.md.
#   golden.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: LOADARG (default a new game in the Blue Palace), CELLID, EVERY, RADIUS, NEAR, STEPS, INPUT, SETTINGS_EXTRA
HERE=$(cd "$(dirname "$0")" && pwd)
export LOADARG="${LOADARG:---start ${CELLID:-SolitudeBluePalace}}"
export EVERY=${EVERY:-300} RADIUS=${RADIUS:-1200} NEAR=${NEAR:-6}
export SETTINGS_EXTRA="quest alias packages = true\nmap = true\nplayer level = 5\nrest = true\nactor cell moves = true\nservices = true\ncombat ai = true\ncreatures = true\ncombat factions = true\ncombat trace = true\nstory trace = true\nsolitude = true\n${SETTINGS_EXTRA:-}"
[ -n "${INPUT:-}" ] && export INPUT
exec bash "$HERE/check.sh" "$1" "$2" "$3" "$4" "${5:-3000}"
