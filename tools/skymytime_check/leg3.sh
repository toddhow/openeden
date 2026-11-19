#!/bin/bash
# tools/skymytime_check/leg3.sh (task 0341): MS08 at Swindler's Den: the quest is put at stage 100 by the developer aid (stand-ins for
# leg 1 and leg 2, which run for real), Kematu (the harness activates him, a stand-in for the crosshair) is talked to with real
# clicks (CLICKS / EXTRA_FILE); the kill aid (`summons test kill = MS08Kematu@<combat second>`) stands in for the fight.
#   leg3.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$4
export CELLID=${CELLID:-SwindlersDen01}
export WATCH="${WATCH:-'136705'}"
T=${TALK:-2600}
export ACTIVATE="$T,'${TALKTO:-136705}'" # 0x021601 Kematu
export SETTINGS_EXTRA="side quests = true\nscene park seconds = 5\nsummons = true\nstory test quest = ${STAGES:-01CF25:10@3,01CF25:20@4,01CF25:25@5,01CF25:100@6}\nstory test delay = 1\n${SETTINGS_EXTRA:-}"
IN="$WORK.input.txt"
: > "$IN"
printf '%s shot\n' $((T + 60)) >> "$IN"
END=$(bash "$HERE/clicks.sh" "$IN" $((T + 120)) ${STEP:-160} "${CLICKS:-s,0}")
[ -n "${EXTRA_INPUT:-}" ] && printf "%b" "$EXTRA_INPUT" >> "$IN"
[ -n "${EXTRA_FILE:-}" ] && cat "$EXTRA_FILE" >> "$IN"
export INPUT="$IN"
exec bash "$HERE/golden.sh" "$1" "$2" "$3" "$4" "${5:-$((END + 100))}"
