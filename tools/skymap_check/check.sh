#!/bin/bash
# tools/skymap_check (task 0201, milestone M-S7): the Skyrim SE compass and world map in game. Starts in Whiterun's street
# (the neutral exterior @WhiterunWorld,25670,-7633) with [GUI] rmlui and [extension.skyrimse] papyrus, journal and map on
# (through tools/journal_check/check.sh), walks the player along a fixed plan by teleports (luatest/scripts/m7map) taking a
# screenshot at each stop, optionally opens the world map with a synthetic M key (INPUT), and saves the game. A second run
# with PHASE=load (the same <work dir>) loads that save into <work dir>.load and takes pictures again.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: PHASE (run|load, default run), INPUT (an input script for OPENEDEN_RMLUI_INPUT_SCRIPT), SETTINGS_EXTRA
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-1500}
PHASE=${PHASE:-run}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
echo "return { phase = '$PHASE' }" > "$LUA/scripts/m7map/cfg.lua"
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
fi
export DEMO_DIR="$LUA" DEMO_ESP=m7map.omwscripts
export SETTINGS_EXTRA="map = true\n${SETTINGS_EXTRA:-}"
export LOADARG=${LOADARG:---start @WhiterunWorld,25670,-7633}
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-400}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
