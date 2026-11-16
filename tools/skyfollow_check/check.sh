#!/bin/bash
# tools/skyfollow_check (task 0239, milestone M-S7): follower commands through the game's own dialogue. Starts a Skyrim
# neutral session in a cell (CELLID, default the Drunken Huntsman) with the Papyrus host, the actors' packages, the
# services (the "carry" window) and the inventory on, a global script (luatest/scripts/fcmd/global.lua) that logs where
# the player and the target actor stand (FCMD lines) and starts the conversation with it, and the real input script
# INPUT (mouse clicks and screenshots) through OPENEDEN_RMLUI_INPUT_SCRIPT. The test game opens off-screen.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: INPUT (the input script), PLAN (the Lua list of plan steps, default none), TARGET (record id of the actor's base,
#      default Jenassa), CELLID, EVERY (sample period in updates), FROM, SETTINGS_EXTRA, WIDTH and HEIGHT, PHASE (run|load:
#      load reads the save the run made, in <work dir>.load), LOADARG, CHECK_TIMEOUT.
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-6000}
PHASE=${PHASE:-run}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
echo "return { target = '${TARGET:-skyrimse:b760194}', every = ${EVERY:-120}, from = ${FROM:-1}, plan = { ${PLAN:-} } }" > "$LUA/scripts/fcmd/cfg.lua"
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
fi
export LOADARG="${LOADARG:---start ${CELLID:-WhiterunDrunkenHuntsman}}"
export DEMO_DIR="$LUA" DEMO_ESP=fcmd.omwscripts
export SETTINGS_EXTRA="[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\nneutral saves = true\n[Video]\nframerate limit = 60\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[extension.skyrimse]\nactor packages = true\nservices = true\ninventory = true\n${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-900}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
