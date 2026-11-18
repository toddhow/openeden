#!/bin/bash
# tools/skycivilwar_check (task 0298): joining a side in the Civil War, through the game's own CW quests (fork of skycompanions_check).
# Skyrim neutral session in a cell (CELLID, default WhiterunJorrvaskr) with the Papyrus host, `houses`, the crime and rest
# systems, the inventory and the services on, a global script (luatest/scripts/cmp/global.lua) that talks to the steward,
# moves the player, activates doors and furniture and logs gold and the key (CMP lines), and the real input script INPUT
# (mouse clicks and screenshots) through OPENEDEN_RMLUI_INPUT_SCRIPT. The test game opens off-screen.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: INPUT (the input script), PLAN (the Lua list of plan steps, see global.lua), TARGET (record id of the actor's base,
#      default Proventus Avenicci), CELLID, EVERY (sample period in updates), FROM, FURN (record id text of the furniture a
#      'furn' step looks for), GOLD (the player's gold, default 6000), PERMISSION (default true: the jarls like the player),
#      SETTINGS_EXTRA, PHASE (run|load: load reads the save the run made, in <work dir>.load), LOADARG, CHECK_TIMEOUT.
# <work dir> must be under I:/Projects/OpenEden-work.
#
# Task 0298, the join chain (Rikke, then Tullius, then Rikke three times, then the journal): SETTINGS_EXTRA with
# `civil war = true` (and `quest alias packages = true`), INPUT=join_input.txt (next to this file), 14800 frames,
# `OPENEDEN_PACKAGE_TRACE=78462` (optional: the package trace of Tullius) and
#   PLAN="{5800,'talk'},{10200,'talk'},{11400,'talk'}"
# Steps: Rikke's "Are you lost?" (stage 7), Goodbye; Tullius' forced greeting and its choices (Helgen, ..., stage 10); Rikke's
# "You survived Helgen?" and "I can handle anything"; Rikke's "Are you ready to test yourself", "What's at Fort Hraggstad?",
# "Consider that fort already yours." (stage 20, CW01A started, objective 1 displayed); the journal (J) lists both quests.
#
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
echo "return { target = '${TARGET:-skyrimse:b78497}', every = ${EVERY:-300}, from = ${FROM:-1}, furn = '${FURN:-bed}', plan = { ${PLAN:-} } }" > "$LUA/scripts/cmp/cfg.lua"
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
fi
export LOADARG="${LOADARG:---start ${CELLID:-SolitudeCastleDour}}"
export DEMO_DIR="$LUA" DEMO_ESP=cmp.omwscripts
export SETTINGS_EXTRA="[Game]
neutral combat = true
neutral inventory = true
neutral actor idles = true
neutral saves = true
[Video]
framerate limit = 60
[Sound]
master volume = 0.0
music volume = 0.0
voice volume = 0.0
[extension.skyrimse]
actor packages = true
services = true
inventory = true
crime = true
story trace = true
combat ai = true
creatures = true
player gold = ${GOLD:-1000}
${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-900}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
