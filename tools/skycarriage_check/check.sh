#!/bin/bash
# tools/skycarriage_check (task 0223, milestone M-S7): carriages and the world map indoors, in game. Starts in the Whiterun
# plain next to the carriage driver (or in the Bannered Mare with NOPLAN=1), with [GUI] rmlui and the pack settings map, fast
# travel, papyrus, inventory and actor packages on (through tools/journal_check/check.sh), puts the player in front of the
# driver (luatest/scripts/ft23, FT23 lines every two seconds) and runs the real input script INPUT through
# OPENEDEN_RMLUI_INPUT_SCRIPT (Space, clicks, screenshots).
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: INPUT, DISCOVER ("world,x,y": the map's markers round it are marked found), NOPLAN (1: stay where the session starts), X, Y, Z, YAW (where the player stands; defaults in front of the
#      Whiterun driver), SETTINGS_EXTRA, LOADARG.
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-4600}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
flag() { [ -n "$1" ] && echo true || echo false; }
echo "return { noPlan = $(flag "$NOPLAN"), x = ${X:-17480}, y = ${Y:--12412}, z = ${Z:--4700}, yaw = ${YAW:-1.5708}, advance = ${ADVANCE:-9}, discover = $([ -n "$DISCOVER" ] && echo "{ $DISCOVER }" || echo nil) }" > "$LUA/scripts/ft23/cfg.lua"
export DEMO_DIR="$LUA" DEMO_ESP=ft23.omwscripts
export SETTINGS_EXTRA="map = true
fast travel = true
actor packages = true
inventory = true
[Game]
neutral inventory = true
neutral combat = true
neutral actor idles = true
[Video]
framerate limit = 60
[Sound]
master volume = 0.0
music volume = 0.0
${SETTINGS_EXTRA:-}"
export LOADARG=${LOADARG:---start @WhiterunWorld,17480,-12412}
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-500}
export OPENEDEN_NEUTRAL_ACTOR_TRACE=1
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
