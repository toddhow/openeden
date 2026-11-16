#!/bin/bash
# tools/skyrest_check (task 0210, milestone M-S7): waiting and sleeping in a Skyrim SE session. Starts in the Bannered Mare
# (through tools/journal_check/check.sh) with [GUI] rmlui and [extension.skyrimse] rest, the actors' packages and combat on,
# runs a global script that logs the game clock and where the Mare's actors stand every two simulation seconds
# (RESTCHECK lines) and the real input script INPUT (the rest key T, clicks, screenshots) through
# OPENEDEN_RMLUI_INPUT_SCRIPT.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: INPUT (input script), SETTINGS_EXTRA, HOUR (OPENEDEN_NEUTRAL_ACTOR_HOUR is NOT set: the clock is the game's),
#      PHASE (run|load: load reads the save the run made, in <work dir>.load), LOADARG
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
echo "return { phase = '$PHASE', hurtAt = ${HURT_AT:-0}, hurtHealth = ${HURT_HEALTH:-30}, saveAt = ${SAVE_AT:-0}, preAdvance = ${PRE_ADVANCE:-0}, bed = ${BED:-nil} }" > "$LUA/scripts/rest10/cfg.lua"
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
fi
export DEMO_DIR="$LUA" DEMO_ESP=rest10.omwscripts
export SETTINGS_EXTRA="[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\n[Video]\nframerate limit = 60\n[extension.skyrimse]\nrest = true\nactor packages = true\n${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-600}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
