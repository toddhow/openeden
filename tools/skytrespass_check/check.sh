#!/bin/bash
# tools/skytrespass_check (task 0213): trespass and the clock in a Skyrim SE session. Starts in the Bannered Mare through
# tools/journal_check/check.sh with [GUI] rmlui and [extension.skyrimse] crime, rest, crafting and the actors' packages,
# runs a global script that logs the game clock, the cell, the player's bounty and the crime state (TRESPCHECK lines) and
# can teleport the player into another cell (GO_CELL, GO_POS "x,y,z", GO_AT simulation seconds) or advance the clock
# (PRE_ADVANCE hours), and optionally the real input script INPUT through OPENEDEN_RMLUI_INPUT_SCRIPT.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: EVENTS ("{ {seconds,'placement hex','base hex','name'}, ... }": the engine's furniture event is sent by the script), FURN ("{'skyrimse:b<base>', seconds, pitch, distance, hintX, hintY}": the player is put in front of the nearest such object), INPUT, SETTINGS_EXTRA, PHASE (run|load: load reads the save the run made, in <work dir>.load), SAVE_AT, PRE_ADVANCE,
#      GO_CELL, GO_POS, GO_AT, BED ("{x,y,z,heading,pitch,seconds}" a placement of the player), BACK_AT (teleport back at)
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-4500}
PHASE=${PHASE:-run}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
echo "return { phase = '$PHASE', saveAt = ${SAVE_AT:-0}, preAdvance = ${PRE_ADVANCE:-0}, goCell = '${GO_CELL:-}', goPos = { ${GO_POS:-0,0,0} }, goAt = ${GO_AT:-0}, backAt = ${BACK_AT:-0}, bed = ${BED:-nil}, furn = ${FURN:-nil}, events = ${EVENTS:-nil} }" > "$LUA/scripts/tresp10/cfg.lua"
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
fi
export DEMO_DIR="$LUA" DEMO_ESP=tresp10.omwscripts
export SETTINGS_EXTRA="[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\nneutral saves = true\n[Video]\nframerate limit = 60\n[extension.skyrimse]\nrest = true\ncrime = true\ninventory = true\ncrafting = true\ncombat ai = true\ncombat factions = true\nactor packages = true\n${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-600}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
