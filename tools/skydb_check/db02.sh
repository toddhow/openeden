#!/bin/bash
# tools/skydb_check (task 0325): DB01 to the reward (task 0294), then the sleep that carries the player to the abandoned shack
# and the whole of Astrid's wake-up conversation (DB02 stage 10), the captive's death (stage 30) and her "Brother."
# usage: db02.sh <name> [frames]   (about 15,600 frames, 13 minutes of wall time)
# env: BUILD (default I:/Projects/OpenEden-work/combat5/build), W (work folder), KILL (the test kill list), PLAN2, INPUT,
#      SETTINGS_EXTRA2, PKTRACE (the base id whose packages are traced: Astrid 114100)
# The input script (db02_input.txt) clicks the real conversation box and the real sleep window; the buttons were read from frames
# (the box is centred, so a button moves with the text above it). The bed is the Bannered Mare's (base b242718, at 146,554).
D=${W:-I:/Projects/OpenEden-work/combat5/db02}; mkdir -p $D
HERE=$(cd "$(dirname "$0")" && pwd)
DEFAULT_PLAN2=", { 8700, 'cell', 'WhiterunBanneredMare', -256, -306, 100, 0 }, { 9500, 'go', 120, 480, 100, 0 }, { 9600, 'furn', 146, 554, 40, 'b242718' }, { 14000, 'talk', 'skyrimse:b114100' }"
export PLAN="{ 3000, 'talk' }, { 5000, 'cell', 'RiftenHonorhallOrphanage', -384, 640, 72, 0 }, { 7000, 'cell', 'WindhelmAretinoResidence', -244, -2767, 371, 0 }, { 7300, 'talk' }${PLAN2-$DEFAULT_PLAN2}"
export SETTINGS_EXTRA="story trace = true\nsummons = true\nsummons test kill = ${KILL:-Grelod,DB02Captive1@120}\ncombat trace = true\nrest test ignore owner = true\nmap = true\nfast travel = true\nactor cell moves = true\ncompanions = true\n${SETTINGS_EXTRA2:-}"
export OPENEDEN_PAPYRUS_NATIVE_TRACE=1
export OPENEDEN_PACKAGE_TRACE=${PKTRACE:-114100}
export BUILD=${BUILD:-I:/Projects/OpenEden-work/combat5/build} W=$D
bash "$HERE/run.sh" $1 ${INPUT:-$HERE/db02_input.txt} ${2:-15600}
