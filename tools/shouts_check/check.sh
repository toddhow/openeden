#!/bin/bash
# tools/shouts_check (task 0205, milestone M-S7): a Skyrim shout state survives a save. PHASE=run starts the Bannered Mare with
# the shouts on (learned and unlocked words, souls, an equipped shout, an automatic shout so a cooldown is running), prints the
# state, and saves at 9 s of simulation time. PHASE=load (the same <work dir>) loads that save with NO test settings, so what
# it prints comes from the save chunk alone.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-1500}
PHASE=${PHASE:-run}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
echo "return { phase = '$PHASE' }" > "$LUA/scripts/sh205/cfg.lua"
BASE_SET='shouts = true\ncombat trace = true\n'
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
    TEST_SET=''
else
    TEST_SET='shouts test learn = WordFus,WordRo,WordDah\nshouts test unlock = WordFus,WordRo\nshouts test souls = 3\nshouts test shout = UnrelentingForceShout\nshouts test shout every = 3\nshouts test shout hold = 0.4\n'
fi
export DEMO_DIR="$LUA" DEMO_ESP=sh205.omwscripts
export SETTINGS_EXTRA="${BASE_SET}${TEST_SET}\n[Game]\nneutral combat = true\nneutral actor idles = true\nneutral saves = true\n"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-300}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
