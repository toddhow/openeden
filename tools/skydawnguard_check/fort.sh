#!/bin/bash
# tools/skydawnguard_check/fort.sh (task 0319): Fort Dawnguard and the start of "Awakening". A new game in the worldspace
# DLC1HunterHQWorld (the road to the fort), level 10, `dawnguard = true`. The quest DLC1VQ01MiscObjective is put at stage 5
# and 10 by the developer aid (Durak's own approach is task 0305's check); then the player is stood in the game's own trigger
# boxes (stages 50, 80, 100), at the fort's door (a stand-in for the door), in the sphere of stage 115, and clicks through
# Isran's conversation with the real mouse path (a sweep of clicks, see fort_input.txt).
#
#   fort.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#   DIMHOLLOW=1 starts in Tamriel at Dimhollow Crypt's door instead and activates it (the quest DLC1VQ01 at stage 10).
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -n "${DIMHOLLOW:-}" ]; then
    export LOADARG="--start @Tamriel,6120,64600" WATCH="'33567594'" EVERY=300 ACTIVATE="500,'202903'"
    export SETTINGS_EXTRA="dawnguard = true\nstory test delay = 1\nstory test quest = 0200352A:10\n"
    export INPUT="$HERE/dimhollow_input.txt"
    exec bash "$HERE/check.sh" "$1" "$2" "$3" "$4" "${5:-2200}"
fi
export LOADARG="--start @DLC1HunterHQWorld,-4500,-10397" WATCH="'33567594'" EVERY=300
export STEPS="{ 600,'abs',-6342,-10397,-3900,0,0 }, { 1500,'abs',5547,2303,-3000,0,0 }, { 2300,'abs',6796,725,-1900,0,0 }, { 3000,'cell','DLC1DawnguardHQ01',-1134,-800,150,0,0 }, { 7500,'abs',-1148,316,-100,0,0 }"
export SETTINGS_EXTRA="quest alias packages = true\nmap = true\nstory trace = true\nplayer level = 10\nrest = true\nactor cell moves = true\nservices = true\ndawnguard = true\nstory test delay = 1\nstory test quest = 0200D06C:5,0200D06C:10@1\n"
export INPUT="$HERE/fort_input.txt"
exec bash "$HERE/check.sh" "$1" "$2" "$3" "$4" "${5:-11200}"
