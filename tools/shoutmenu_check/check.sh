#!/bin/bash
# tools/shoutmenu_check (task 0266): the Skyrim shout menu. PHASE=run starts the Bannered Mare with the shouts on, four words
# learned and two dragon souls, opens the menu with H and presses Unlock twice and Equip through the real input script
# (INPUT, mouse clicks), prints the menu through the script namespace and saves 3 s after a shout got equipped. PHASE=load
# (the same <work dir>) loads that save with NO test learning, so the state printed comes from the save alone, and opens the
# menu again.
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
echo "return { phase = '$PHASE' }" > "$LUA/scripts/sm266/cfg.lua"
BASE_SET='shouts = true\n'
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
    TEST_SET=''
    export INPUT=${INPUT:-$HERE/load.txt}
else
    TEST_SET='shouts test learn = WordFus,WordRo,WordYol,WordToor\nshouts test souls = 2\n'
    export INPUT=${INPUT:-$HERE/run.txt}
fi
export DEMO_DIR="$LUA" DEMO_ESP=sm266.omwscripts
export SETTINGS_EXTRA="${BASE_SET}${TEST_SET}\n[Game]\nneutral combat = true\nneutral actor idles = true\nneutral saves = true\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\nvoice volume = 0.0\n${SM_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-300}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
