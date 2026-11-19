#!/bin/bash
# tools/skymoonlight_check (task 0350): "Ill Met by Moonlight" (DA05, Hircine) through the game's own scripts, dialogue, scenes
# dialogue and Papyrus (fork of skybloodice_check). Same arguments and environment: check.sh <build> <base config> <Skyrim Data>
# <work> [frames]. A new game on the Falkreath road; the plan's `world`/`cell` steps stand in for walking (Tamriel is
# `skyrimse:w60`). Steps (global.lua): world id x y z|'land', go x y z, talk [base], hour h, trace tag, near ...
# PLAN, INPUT, SETTINGS_EXTRA, FRAMES as in the other checks.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
export INPUT="${INPUT:-$HERE/input.txt}"
export GOLD="${GOLD:-6000}"
DEFAULT_PLAN="{10,'hour',12},{300,'go',-28000,-87500,'land'},{1200,'cell','FalkreathBarracksJail',300,-195,-120,1.57},{1800,'talk',442807},{7000,'world',60,-28000,-87500,'land'}"
export PLAN="${PLAN:-$DEFAULT_PLAN}"
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
export LOADARG="${LOADARG:---start @Tamriel,-22000,-87000}"
export DEMO_DIR="$LUA" DEMO_ESP=cmp.omwscripts
export SETTINGS_EXTRA="[Game]
neutral combat = true
neutral inventory = true
neutral actor idles = true
neutral saves = true
[Video]
framerate limit = ${FRAMELIMIT:-60}
[Sound]
master volume = 0.0
music volume = 0.0
voice volume = 0.0
[extension.skyrimse]
actor packages = true
map = true
fast travel = true
actor cell moves = true
quest alias packages = true
daedric quests = true
summons = true
combat spell delivery = true
combat factions = true
services = true
inventory = true
crime = true
combat ai = true
creatures = true
player gold = ${GOLD:-1000}
${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-900}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
