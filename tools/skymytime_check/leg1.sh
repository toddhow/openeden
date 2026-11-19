#!/bin/bash
# tools/skymytime_check/leg1.sh (task 0341): MS08 in the Bannered Mare: the player talks to Saadia (the harness activates her,
# a stand-in for the crosshair) and real mouse clicks (CLICKS: "x,y x,y ...", clicks.sh) choose lines.
#   leg1.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$4
SAADIA=${SAADIA:-80802} # 0x013BA2
export CELLID=WhiterunBanneredMare
T=${TALK:-5600}
export ACTIVATE="$T,'$SAADIA'"
[ -n "${STEPS:-}" ] && export STEPS
export SETTINGS_EXTRA="scene park seconds = 5\nstory test delay = 1\n${SETTINGS_EXTRA:-}"
IN="$WORK.input.txt"
: > "$IN"
printf '%s shot\n' $((T + 60)) >> "$IN"
END=$(bash "$HERE/clicks.sh" "$IN" $((T + 120)) ${STEP:-120} "${CLICKS:-s,0}")
[ -n "${EXTRA_INPUT:-}" ] && printf "%b" "$EXTRA_INPUT" >> "$IN"
[ -n "${EXTRA_FILE:-}" ] && cat "$EXTRA_FILE" >> "$IN"
export INPUT="$IN"
exec bash "$HERE/golden.sh" "$1" "$2" "$3" "$4" "${5:-$((END + 100))}"
