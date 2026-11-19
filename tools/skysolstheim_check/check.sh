#!/bin/bash
# tools/skysolstheim_check (task 0312): the ship from Windhelm to Raven Rock through Gjalund's own dialogue and the game's
# DLC2DialogueRRQuestScript.SetSail (fork of skydragonborn_check, task 0306). Same arguments and environment as that check.
# Defaults: a new game at Windhelm's docks (LOADARG="--start @Tamriel,141200,34000"), `dragonborn`, `fast travel`, `map`,
# `actor cell moves`, `quest alias packages` on, the aid `dragonborn test prerequisites`, 6000 gold; PLAN and INPUT as in
# skydragonborn_check (talk base 67207854 = Gjalund of Windhelm, DLC2RRGjalundSaltSageWindhelm).
# The default run (task 0312): talk to Gjalund of Windhelm at frame 2000 ("I need passage to Solstheim", "How much will it cost
# me?", "Why sail there at all then?", "Yes. (250 gold)", Goodbye: the ship leaves when the box closes), the player lands at the
# Raven Rock landing (frame 5300 trace), the aid starts DLC2MQ01 at stage 10 at script time 40 s (the log entry for a player
# on Solstheim sets stage 20), then he talks to Gjalund of Solstheim at frame 6500 ("I'd like passage back to Skyrim.",
# "Here you go. (250 gold)", Goodbye) and lands at Windhelm's landing marker (frame 9900 trace). 10,200 frames.
# Task 0329 (the arrival scene to Frea): INPUT=solstheim3_input.txt (Goodbye clicks for Adril, Neloth's three turns, Frea's
# four), SETTINGS_EXTRA with `dragonborn test stage = 5+7+10,70` under [extension.skyrimse] (the aid sets the stages in order at
# script second 70, after Adril's greeting began's talk) and PLAN="{10,'trace','start'},{2000,'talk',67207854},{5300,'trace','arrived'},
# {13000,'go',23300,30800,900},{13200,'trace','stone'},{15500,'go',56365,62368,9700},{15600,'trace','temple'},
# {19300,'trace','end'}" with 19500 frames. The two 'go' steps stand in for the walk to the Earth Stone and to the Temple of
# Miraak (MQ01's end trigger); the waits are script time, which follows a real-time worker, so the frames are loose.
#
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
export INPUT="${INPUT:-$HERE/ship_input.txt}"
export GOLD="${GOLD:-6000}"
DEFAULT_PLAN="{10,'trace','start'},{2000,'talk',67207854},{5300,'trace','arrived'},{6500,'talk',67262507},{9900,'trace','end'}"
export PLAN="${PLAN:-$DEFAULT_PLAN}"
export SETTINGS_EXTRA="${SETTINGS_EXTRA-[extension.skyrimse]
dragonborn test stage = 10,40
}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-10200}
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
export LOADARG="${LOADARG:---start @Tamriel,141200,34000}"
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
map = true
fast travel = true
actor cell moves = true
dragonborn = true
quest alias packages = true
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
