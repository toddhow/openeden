#!/bin/bash
# tools/skycompanions_check (task 0261, milestone M-S7): buying Breezehome through the game's own dialogue and scripts. Starts a
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
# Task 0272, the training scenario: CELLID=WhiterunJorrvaskrBasement, SETTINGS_EXTRA with `companions = true` and
# `companions test training = true`, INPUT=training_input.txt (next to this file) and
#   PLAN="{5800,'talk'},{8900,'face',108180,11800},{10000,'ids','b108180'},{11000,'ids','b108180'}", 12100 frames.
# Do NOT talk to Kodlak earlier than about frame 5000: the game's quest C00 sets itself back to stage 1 60 s of script time after
# the host starts (CompanionsHousekeepingScript.SetUpCompanions: Wait(60), then SetStage(1)), and a conversation that sets stage 20
# before that finds Vilkas' topic (needs GetStage(C00) == 20) failing. See docs/tasks/0272-skyrim-c00-stage-reset.md.
#
# Task 0277, the whole walk (no `companions test training` aid): SETTINGS_EXTRA with `companions = true`, `quest alias packages = true`
# and `combat trace = true`, CFG_EXTRA `fallback-archive=Skyrim - Voices_en0.bsa\n`, CELLID=WhiterunJorrvaskrBasement,
# INPUT=yard_input.txt (next to this file), 14500 frames and
#   PLAN="{5800,'talk'},{9600,'furn',256,-896,300,'b152263'},{10200,'furn',-256,640,300,'b152263'},{10600,'trace','outside'},
#         {10650,'go',29200,-4057,-2733,1.57},{10700,'face',108180,13500}"
# Steps: Kodlak's join topic and Vilkas' scene (clicks of the input file), the basement door (base 152263 is every Jorrvaskr
# door) and the back door out to the yard carry Vilkas with the player, Vilkas is placed at the training marker when the
# walk is blocked, "Just have a few swings at me" starts the training quest, the clicks fight (3 hits: stage 150), the
# package of the quest alias makes Vilkas greet again, the click at frame 13300 closes it and C00 reaches stage 30.
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
echo "return { target = '${TARGET:-skyrimse:b108174}', every = ${EVERY:-300}, from = ${FROM:-1}, furn = '${FURN:-bed}', plan = { ${PLAN:-} } }" > "$LUA/scripts/cmp/cfg.lua"
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
fi
export LOADARG="${LOADARG:---start ${CELLID:-WhiterunJorrvaskr}}"
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
