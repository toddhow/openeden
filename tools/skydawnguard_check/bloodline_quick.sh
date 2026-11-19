#!/bin/bash
# tools/skydawnguard_check/bloodline_quick.sh (task 0334): the short form of bloodline.sh. A new game in the puzzle room's cell
# (DLC1DimHollowCrypt02) with the quest stages of "Awakening" set by the developer aid `story test quest` (STAND-IN for the
# puzzle and Serana's conversation, which bloodline.sh plays through); DLC1VQ01 stage 200 starts DLC1VQ02 at stage 5. Then the
# player (STAND-IN: teleport hops, no walk) goes from Serana's coffin to the brazier room and on to the exit door, and Serana's
# own walk to him is what is looked at (MQ watch lines; with OPENEDEN_NEUTRAL_ACTOR_TRACE the goals).
#
#   bloodline_quick.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
HERE=$(cd "$(dirname "$0")" && pwd)
SERANA=33565548 # 0x02002B6C
export LOADARG="--start DLC1DimHollowCrypt02" WATCH="'$SERANA'" EVERY=${EVERY:-60} RADIUS=${RADIUS:-1200} NEAR=${NEAR:-2}
export STEPS="{ 300,'cell','DLC1DimHollowCrypt02',4255,1000,-1000,0,0 }, { ${PLACE:-1500},'place','$SERANA',${PX:-4255},${PY:-1010},-1000 }, { $((${PLACE:-1500} + 20)),'abs',${AX:-4255},${AY:-950},-1000,${YAW:-0},0 }"
export SETTINGS_EXTRA="quest alias packages = true\nmap = true\nplayer level = 10\nrest = true\nactor cell moves = true\nservices = true\ndawnguard = true\ndungeon mechanics = true\ncombat ai = true\ncreatures = true\ncombat factions = true\nsummons = true\nstory test delay = 1\nstory test quest = 0200352A:10,0200352A:20@3,0200352A:30@4,0200352A:200@10\n"
# the player walks by the real ${KEY:-S} key for ${WALK:-300} frames from frame ${PLACE:-1500} + 60 (facing north, S walks south
# across the brazier room); Serana, put beside him, follows by her package
IN="$4.input.txt"
K=${KEY:-S}
P=$((${PLACE:-1500} + 60))
{ echo "300 shot"; echo "$P key $K"; echo "$((P + ${WALK:-300})) key $K up"; echo "$((P + ${WALK:-300} + 900)) shot"; } > "$IN"
export INPUT="$IN"
exec bash "$HERE/check.sh" "$1" "$2" "$3" "$4" "${5:-4500}"
