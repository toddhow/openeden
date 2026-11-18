#!/bin/bash
# tools/skyactormoves_check (task 0286): the Helgen opening (`main quest start`) with `actor cell moves`: the cast of the
# game's own quest reaches the cart as the real placements (no `relocate ... made FF80...` lines), and a save made while
# they stand there loads in a new process with them at the cart. Based on tools/skymainquest_check; the Lua sampler also
# logs the placements of WATCH (a comma separated list of record ids) and asks the menu script to save at update SAVEAT.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: PHASE (run|load; load reads the save the run made in <work dir>/user/saves and uses <work dir>.load), SAVEAT (update
#      number, default 0: no save), WATCH, EVERY, NEAR, RADIUS, SETTINGS_EXTRA, CHECK_TIMEOUT, MOVES (default true: set
#      `actor cell moves`; false runs the old glue for comparison). <work dir> must be under I:/Projects/OpenEden-work.
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
WATCHLUA=""
IFS=',' read -ra W <<< "${WATCH:-}"
for w in "${W[@]}"; do [ -n "$w" ] && WATCHLUA="$WATCHLUA ['$w']=true,"; done
echo "return { every = ${EVERY:-120}, from = 1, near = ${NEAR:-6}, radius = ${RADIUS:-900}, saveAt = ${SAVEAT:-0}, watch = {$WATCHLUA} }" > "$LUA/scripts/mq/cfg.lua"
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
fi
export LOADARG="${LOADARG:---start @Tamriel,22499.8,-94862.6,13905.8}"
export DEMO_DIR="$LUA" DEMO_ESP=mq.omwscripts
export CFG_EXTRA='fallback-archive=Skyrim - Voices_en0.bsa\n'
export SETTINGS_EXTRA="[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\nneutral actor locomotion = true\nneutral lip sync = true\nneutral saves = true\n[Video]\nframerate limit = 60\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\nvoice volume = 0.0\n[Lua]\nlua num threads = 0\n[extension.skyrimse]\nmain quest start = true\nactor packages = true\nquest alias packages = true\ncreatures = true\nbarks = false\nactor cell moves = ${MOVES:-true}\n${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-900}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
