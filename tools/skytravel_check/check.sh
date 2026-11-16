#!/bin/bash
# tools/skytravel_check (task 0218, milestone M-S7): fast travel from the Skyrim SE world map, in game. Starts in Whiterun's
# street (@WhiterunWorld,25670,-7633) with [GUI] rmlui and [extension.skyrimse] map and fast travel on (through
# tools/journal_check/check.sh), walks the player next to Honningbrew Meadery's marker and back (luatest/scripts/ft18, FT18
# lines every two seconds), and runs the real input script INPUT (M opens the map, clicks, screenshots) through
# OPENEDEN_RMLUI_INPUT_SCRIPT. A hostile near the player (HOSTILE=<base editor id>) and followers (FOLLOWER=<base editor id>)
# are optional.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: INPUT, HOSTILE, CROWD (the N nearest actors attack), GATHER (a record id such as skyrimse:b146064: its placements are put beside the player at 20 s), SPELL (the real attack key casts it), FOLLOWER, NOPLAN (1: no teleports), SETTINGS_EXTRA, TRAVEL (false turns the setting off)
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
echo "return { noPlan = $(flag "$NOPLAN"), gather = $([ -n "$GATHER" ] && echo "'$GATHER'" || echo false) }" > "$LUA/scripts/ft18/cfg.lua"
export DEMO_DIR="$LUA" DEMO_ESP=ft18.omwscripts
EXTRA="map = true
fast travel = ${TRAVEL:-true}
actor packages = true
"
[ -n "$HOSTILE" ] && EXTRA="${EXTRA}combat ai = true
combat test hostile = $HOSTILE
"
[ -n "$CROWD" ] && EXTRA="${EXTRA}combat ai = true
combat test crowd = $CROWD
"
[ -n "$SPELL" ] && EXTRA="${EXTRA}combat ai = true
combat test player spell = $SPELL
"
[ -n "$FOLLOWER" ] && EXTRA="${EXTRA}follower test recruit = $FOLLOWER
"
# the pack's keys first (the file ends in [extension.skyrimse]), then the sections the check needs
export SETTINGS_EXTRA="${EXTRA}[Game]
neutral combat = true
neutral actor idles = true
[Video]
framerate limit = 60
[Sound]
master volume = 0.0
music volume = 0.0
${SETTINGS_EXTRA:-}"
export LOADARG=${LOADARG:---start @WhiterunWorld,25670,-7633}
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-500}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
