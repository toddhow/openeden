#!/bin/bash
# tools/skyhorse_check (task 0278, M-S7): buying a horse from Whiterun's stablemaster (TARGET, default Skulvar Drenim's base) through the
# game's own dialogue and scripts, then riding it. A copy of tools/skyhouses_check (read its notes): the global script is the same,
# with a 'mount' step (activates the nearest object whose record id contains the text, cfg.horse); the settings differ.
# (tools/skyhouses_check: buying Breezehome through the game's own dialogue and scripts.) Starts a
# Skyrim neutral session in a cell (CELLID, default WhiterunDragonsreach) with the Papyrus host, `houses`, the crime and rest
# systems, the inventory and the services on, a global script (luatest/scripts/hse/global.lua) that talks to the steward,
# moves the player, activates doors and furniture and logs gold and the key (HSE lines), and the real input script INPUT
# (mouse clicks and screenshots) through OPENEDEN_RMLUI_INPUT_SCRIPT. The test game opens off-screen.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: INPUT (the input script), PLAN (the Lua list of plan steps, see global.lua), TARGET (record id of the actor's base,
#      default Proventus Avenicci), CELLID, EVERY (sample period in updates), FROM, KEY (hex id text of the house key the trace counts, default Breezehome's 00093b08), FURN (record id text of the furniture a
#      'furn' step looks for), GOLD (the player's gold, default 6000), PERMISSION (default true: the jarls like the player),
#      SETTINGS_EXTRA, PHASE (run|load: load reads the save the run made, in <work dir>.load), LOADARG, CHECK_TIMEOUT.
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
echo "return { target = '${TARGET:-skyrimse:b80823}', every = ${EVERY:-300}, from = ${FROM:-1}, furn = '${FURN:-bed}', key = '${KEY:-00093b08}', horse = '${HORSE:-skyrimse:b429306}', world = '${WORLD:-skyrimse:w60}', plan = { ${PLAN:-} } }" > "$LUA/scripts/hse/cfg.lua"
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
fi
export LOADARG="${LOADARG:---start ${CELLID:-@WhiterunWorld,18700,-11200}}"
export DEMO_DIR="$LUA" DEMO_ESP=hse.omwscripts
export SETTINGS_EXTRA="[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\nneutral actor locomotion = true\nneutral player body = true\nneutral saves = true\n[Video]\nframerate limit = 60\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[extension.skyrimse]\nactor packages = true\nservices = true\ninventory = true\ncrime = true\nrest = true\ncreatures = true\nplayer body = true\nriding = true\npaired animations = ${PAIRED:-true}\nplayer gold = ${GOLD:-6000}\n${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-900}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
