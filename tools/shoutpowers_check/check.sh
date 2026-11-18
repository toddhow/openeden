#!/bin/bash
# tools/shoutpowers_check (task 0281): the powers section of the H menu. PHASE=run: choose a power with Equip (input script), press P, save; PHASE=load: the choice comes from the save.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: INPUT (the input script, default run.txt or load.txt beside this file)
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-5200}
PHASE=${PHASE:-run}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
echo "return { phase = '$PHASE' }" > "$LUA/scripts/sp281/cfg.lua"
BASE_SET='shouts = true
werewolf = true
menus = true
player body = true
player progression = true
character creation = true
inventory = true
actor packages = true
'
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
    TEST_SET=''
    export INPUT=${INPUT:-$HERE/load.txt}
else
    TEST_SET='werewolf test grant = true
'
    export INPUT=${INPUT:-$HERE/run.txt}
fi
export DEMO_DIR="$LUA" DEMO_ESP=sp281.omwscripts
export SETTINGS_EXTRA="${BASE_SET}${TEST_SET}\n[Game]\nneutral combat = true\nneutral actor idles = true\nneutral saves = true\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\nvoice volume = 0.0\n${SM_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-300}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
