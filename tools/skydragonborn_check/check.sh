#!/bin/bash
# tools/skydragonborn_check (task 0306): the start of the Dragonborn questline, through the game's own quests (fork of skycivilwar_check, task 0298).
# Skyrim neutral session in a cell (CELLID, default WhiterunBanneredMare; LOADARG wins) with the Papyrus host, `houses`, the crime and rest
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
# Task 0306, the start of the Dragonborn questline (fork of skycivilwar_check): a new game in Whiterun's exterior
# (LOADARG="--start @WhiterunWorld,25670,-7633", the first place event is CLOC to Whiterun), SETTINGS_EXTRA with
#   dragonborn test prerequisites = true, dragonborn = true, quest alias packages = true, combat trace = true,
#   summons = true, combat spell delivery = true, lockpicking and books = true,
#   summons test kill = DLC2LvlCultistNonHostileSummoner@45   (the aid kills the cultist at combat second 45: after
#   the talk; nobody fights in the check),
# INPUT=ambush_input.txt (next to this file), 6500 frames, OPENEDEN_NEUTRAL_ACTOR_TRACE=1 and
#   PLAN="{10,'trace','start'},{1500,'go',21500,-7300,-3560,1.5},{2950,'trace','stage10'},{4900,'talk',67331574},{4905,'trace','loot'},{6450,'trace','end'}"
# The player is put near Whiterun's centre marker after the ambush quest started (the host starts 15 to 25 s after the
# launch: a teleport before it changes the place and no event is raised). Steps: the cultist walks up and force-greets
# ("You there! You're the one they call Dragonborn?"), three clicks on the conversation box (stage 10: DLC2MQ01 starts at
# stage 5), the corpse is looted (Take all: the Cultists' Orders reach the player: stage 7), the inventory (I) and
# Read on the note (stage 10), the journal (J).
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
export LOADARG="${LOADARG:---start ${CELLID:-WhiterunBanneredMare}}"
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
