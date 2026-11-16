#!/bin/bash
# tools/firstperson_check (task 0249): the character creation of the Skyrim SE session (tools/racemenu_check's way: the
# real input script clicks the race arrow and Done) followed by the player's FIRST-PERSON view: the arms of the race
# just made. The Lua script (luatest/scripts/fp249/player.lua) keeps the camera in first person, looks a little down once
# the creation is over and takes pictures. Prints FP249 lines.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: INPUT (the real input script: mouse clicks and screenshots), SETTINGS_EXTRA, WIDTH, HEIGHT (default 1280 x 720)
# <work dir> must be under I:/Projects/OpenEden-work. Starts through tools/journal_check/check.sh.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-3000}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
echo "return { pitch = ${FP_PITCH:-0.5}, shotsAfter = ${FP_SHOTS_AFTER:-2.0}, shotCount = ${FP_SHOT_COUNT:-3}, shotStep = ${FP_SHOT_STEP:-1.0} }" > "$LUA/scripts/fp249/cfg.lua"
export DEMO_DIR="$LUA" DEMO_ESP=fp249.omwscripts
export SETTINGS_EXTRA="[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\nneutral actor locomotion = true\nneutral player body = true\n[Video]\nframerate limit = 60\n[extension.skyrimse]\nmenus = true\nplayer body = true\nfirst person body = true\nheld gear = true\nplayer progression = true\ncharacter creation = true\ninventory = true\nactor packages = false\n${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-600}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
