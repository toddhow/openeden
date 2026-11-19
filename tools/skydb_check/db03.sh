#!/bin/bash
# tools/skydb_check (task 0343): DB02 to its end, the Black Door, the Sanctuary, Nazir's first contracts and DB03.
# usage: db03.sh <name> [frames]
#   phase 1 (default): a new game in the Bannered Mare. DB01 is stood in by the story aid (stage 200), the sleep that carries the player
#     to the shack and Astrid's wake-up conversation are the real chain of task 0325; the test aid kills the captive (stage 30) and a
#     `save` step writes the game at 9,500 frames (run_<name>/user/saves).
#   phase 2 (PHASE=load): loads that save in a new process and clicks through the rest with real mouse clicks: Astrid's second greeting
#     (the key, stage 40), the shack door (the leave box moves Astrid), a teleport to the Black Door, its passphrase, the Sanctuary
#     (stage 50, Astrid's welcome, stage 200), Nazir's first contracts, Narfi's death (the test aid; the game's own pNarfiScript), Astrid's
#     DB03 offer (DB03 stage 10) and the journal.
# env: BUILD, W (work folder), PLAN, INPUT, KILL, SETTINGS_EXTRA2, PKTRACE, PHASE (run|load)
D=${W:-I:/Projects/OpenEden-work/combat3/db03}; mkdir -p $D
HERE=$(cd "$(dirname "$0")" && pwd)
if [ "${PHASE:-run}" = load ]; then
  DEFAULT_PLAN="{ 3000, 'talk', 'skyrimse:b114100' }, { 5200, 'talk', 'skyrimse:b151078' }, { 6600, 'ext', -42100, -83800, -4400, 180 }, { 7500, 'talk', 'skyrimse:b154618' }, { 11800, 'talk', 'skyrimse:b115627' }, { 14400, 'ext', 73193, -62837, 10950, 0 }, { 17000, 'cell', 'DarkBrotherhoodSanctuary', 3201, -188, -337, 0 }, { 17800, 'talk', 'skyrimse:b114100' }"
  DEFAULT_INPUT=$HERE/db03_full.txt; DEFAULT_FRAMES=20500; DEFAULT_KILL='Narfi@115'
else
  DEFAULT_PLAN="{ 600, 'cell', 'WhiterunBanneredMare', -256, -306, 100, 0 }, { 2800, 'go', 120, 480, 100, 0 }, { 3000, 'furn', 146, 554, 40, 'b242718' }, { 9500, 'save' }"
  DEFAULT_INPUT=$HERE/db03_input_a.txt; DEFAULT_FRAMES=9800; DEFAULT_KILL='DB02Captive1@75'
fi
export PLAN="${PLAN-$DEFAULT_PLAN}"
export CELLID=${CELLID:-WhiterunBanneredMare}
export SETTINGS_EXTRA="story trace = true\nstory test quest = ${STORYAID:-01EA50:200@1}\nsummons = true\nsummons test kill = ${KILL:-$DEFAULT_KILL}\ncombat trace = true\nrest test ignore owner = true\nmap = true\nfast travel = true\nactor cell moves = true\ndark brotherhood = true\n${SETTINGS_EXTRA2:-}"
export OPENEDEN_PAPYRUS_NATIVE_TRACE=1
export OPENEDEN_PACKAGE_TRACE=${PKTRACE:-114100}
export BUILD=${BUILD:-I:/Projects/OpenEden-work/combat3/build} W=$D
export TARGET=${TARGET:-skyrimse:b114100}
bash "$HERE/run.sh" $1 ${INPUT:-$DEFAULT_INPUT} ${2:-$DEFAULT_FRAMES}
