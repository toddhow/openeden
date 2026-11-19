#!/bin/bash
# tools/skymytime_check/leg2.sh (task 0341): MS08 in Dragonsreach's basement: the quest is put at stage 25 by the developer aid (a
# stand-in for leg 1's talk with Saadia, which leg1.sh runs for real), the Alik'r prisoner (the harness activates him, a stand-in for
# the crosshair) is talked to with real clicks (CLICKS).
#   leg2.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$4
export CELLID=${CELLID:-WhiterunDragonsreachBasement}
export WATCH="${WATCH:-'136693'}"
T=${TALK:-2600}
export ACTIVATE="$T,'${PRISONER:-136693}'" # 0x0215F5
export SETTINGS_EXTRA="side quests = true\nscene park seconds = 5\nstory test quest = 01CF25:10@3,01CF25:20@4,01CF25:25@5\nstory test delay = 1\n${SETTINGS_EXTRA:-}"
IN="$WORK.input.txt"
: > "$IN"
printf '%s shot\n' $((T + 60)) >> "$IN"
END=$(bash "$HERE/clicks.sh" "$IN" $((T + 120)) ${STEP:-160} "${CLICKS:-s,0}")
[ -n "${EXTRA_INPUT:-}" ] && printf "%b" "$EXTRA_INPUT" >> "$IN"
[ -n "${EXTRA_FILE:-}" ] && cat "$EXTRA_FILE" >> "$IN"
export INPUT="$IN"
exec bash "$HERE/golden.sh" "$1" "$2" "$3" "$4" "${5:-$((END + 100))}"
