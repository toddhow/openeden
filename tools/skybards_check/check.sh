#!/bin/bash
# tools/skybards_check (task 0296): the start of the Bards College questline (Viarmo, MS05Start, MS05 Tending the Flames) through the game's own dialogue and scripts. Starts a Skyrim
# neutral session in a cell (CELLID, default SolitudeBardsCollege) with the Papyrus host, the map, the inventory, services,
# crime and rest on, a global script (luatest/scripts/hse/global.lua: the same plan steps as tools/skyhouses_check, trace
# lines start with MRG) and the real input script INPUT (mouse clicks, keys and screenshots) through
# OPENEDEN_RMLUI_INPUT_SCRIPT. The test game opens off-screen.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: INPUT, PLAN (the Lua list of plan steps), TARGET (record id of the actor's base, default Viarmo skyrimse:b78509),
#      CELLID, EVERY, FROM, GOLD (default 500), SETTINGS_EXTRA, PHASE (run|load),
#      LOADARG, CHECK_TIMEOUT.
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
echo "return { target = '${TARGET:-skyrimse:b78509}', every = ${EVERY:-300}, from = ${FROM:-1}, furn = '${FURN:-bed}', plan = { ${PLAN:-} } }" > "$LUA/scripts/hse/cfg.lua"
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
fi
export LOADARG="${LOADARG:---start ${CELLID:-SolitudeBardsCollege}}"
export DEMO_DIR="$LUA" DEMO_ESP=hse.omwscripts
export SETTINGS_EXTRA="[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\nneutral saves = true\n[Video]\nframerate limit = 60\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[extension.skyrimse]\nactor packages = true\nservices = true\ninventory = true\ncrime = true\nrest = true\nmarriage = true\nmarriage test friends = ${FRIENDS:-}\nplayer gold = ${GOLD:-500}\n${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-900}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
