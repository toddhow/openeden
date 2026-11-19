#!/bin/bash
# tools/skydb_check (task 0343): DB02 to its end and the Sanctuary. DB01 is stood in by the story aid (stage 200 at 4 s), the sleep that
# carries the player to the shack and Astrid's conversation are the real chain of task 0325; then the key, the shack's leave trigger,
# the Black Door, the Sanctuary and Astrid's welcome, DB02a (Nazir) and DB03.
# usage: db03.sh <name> [frames]; env: BUILD, W, PLAN, INPUT, KILL, SETTINGS_EXTRA2, PKTRACE, PHASE (run|load)
D=${W:-I:/Projects/OpenEden-work/combat3/db03}; mkdir -p $D
HERE=$(cd "$(dirname "$0")" && pwd)
DEFAULT_PLAN="{ 600, 'cell', 'WhiterunBanneredMare', -256, -306, 100, 0 }, { 2800, 'go', 120, 480, 100, 0 }, { 3000, 'furn', 146, 554, 40, 'b242718' }"
export PLAN="${PLAN-$DEFAULT_PLAN}"
export CELLID=${CELLID:-WhiterunBanneredMare}
export SETTINGS_EXTRA="story trace = true\nstory test quest = ${STORYAID:-01EA50:200@1}\nsummons = true\nsummons test kill = ${KILL:-DB02Captive1@120}\ncombat trace = true\nrest test ignore owner = true\nmap = true\nfast travel = true\nactor cell moves = true\ndark brotherhood = true\n${SETTINGS_EXTRA2:-}"
export OPENEDEN_PAPYRUS_NATIVE_TRACE=1
export OPENEDEN_PACKAGE_TRACE=${PKTRACE:-114100}
export BUILD=${BUILD:-I:/Projects/OpenEden-work/combat3/build} W=$D
export TARGET=${TARGET:-skyrimse:b114100}
bash "$HERE/run.sh" $1 ${INPUT:-$HERE/db03_input_a.txt} ${2:-9000}
