#!/bin/bash
# tools/sneak_check (task 0227): sneaking and detection in a Skyrim SE session. Starts in the Bannered Mare through
# tools/journal_check/check.sh with [GUI] rmlui, the combat and actor settings and [extension.skyrimse] sneak (and the
# combat test weapon, so the player's blows use an iron sword), runs a global script (luatest/scripts/sneak22) that
# logs the sneak eye and the sneak attack message every half second (SNEAKCHECK lines) and executes the steps of STEPS
# (dump the NPCs, place the player, take a screenshot), and the REAL input script INPUT (the sneak stance as a controller
# button press, W for walking and the left mouse button for the blow) through OPENEDEN_RMLUI_INPUT_SCRIPT.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: STEPS ("{ {t,'dump'}, {t,'place',x,y,z,heading,pitch}, {t,'shot'}, ... }"), INPUT, SNEAK (true|false, default true),
#      SETTINGS_EXTRA (more settings text), PROGRESSION (true: [extension.skyrimse] player progression)
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-3000}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
STEPS=${STEPS:-"{ {2,'dump'} }"}
echo "return { steps = $STEPS }" > "$LUA/scripts/sneak22/cfg.lua"
export DEMO_DIR="$LUA" DEMO_ESP=sneak22.omwscripts
export SETTINGS_EXTRA="[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\n[Video]\nframerate limit = 60\n[extension.skyrimse]\nsneak = ${SNEAK:-true}\ncrime = true\ninventory = true\nactor packages = ${ACTPKG:-false}\ncombat trace = true\ncombat test weapon = IronSword\n${PROGRESSION:+player progression = $PROGRESSION\n}${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-400}
export OPENEDEN_NEUTRAL_COMBAT_TRACE=1
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
