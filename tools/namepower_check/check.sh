#!/bin/bash
# tools/racemenu_check (task 0228): the Skyrim SE character creation in a real session. Starts in the Bannered Mare (through
# tools/journal_check/check.sh) with [GUI] rmlui, the player's Skyrim body, [extension.skyrimse] menus + character creation +
# player progression, and the real input script INPUT (mouse clicks and screenshots) through OPENEDEN_RMLUI_INPUT_SCRIPT.
# A player script (luatest/scripts/np228/player.lua) prints NP228 lines with the character service's and the progression's
# state. The test game opens off-screen (OPENEDEN_QUIET_WINDOW).
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: INPUT (input script), SETTINGS_EXTRA, PHASE (run|load: load reads the save the run made, in <work dir>.load),
#      SAVE_AT (frames after the start: the menu script saves once), CHARACTER (false turns the creation off: the old body), LOADARG
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-3000}
PHASE=${PHASE:-run}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
echo "return { saveAt = ${SAVE_AT:-0} }" > "$LUA/scripts/np228/cfg.lua"
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
fi
export DEMO_DIR="$LUA" DEMO_ESP=np228.omwscripts
export SETTINGS_EXTRA="[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\nneutral actor locomotion = true\nneutral player body = true\nneutral saves = true\n[Video]\nframerate limit = 60\n[extension.skyrimse]\nmenus = true\nplayer body = true\nplayer progression = true\ncharacter creation = ${CHARACTER:-true}\ninventory = true\nactor packages = true\n${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-600}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
