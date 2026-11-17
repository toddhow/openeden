#!/bin/bash
# tools/skymainquest_check (task 0268): a new game with `[extension.skyrimse] main quest start` (the cart at Helgen, the
# game's own quest MQ101 "Unbound" and its scene MQ101Scene1), the Papyrus host, scenes, dialogue and voices, the journal
# and a global script that logs the player and the nearest objects (MQ lines). The test game opens off-screen.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: INPUT (the input script for OPENEDEN_RMLUI_INPUT_SCRIPT), EVERY, NEAR, RADIUS (the Lua sampler), SETTINGS_EXTRA,
#      CHECK_TIMEOUT. <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-6000}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
echo "return { every = ${EVERY:-120}, from = 1, near = ${NEAR:-6}, radius = ${RADIUS:-900} }" > "$LUA/scripts/mq/cfg.lua"
export LOADARG="${LOADARG:---start @Tamriel,22499.8,-94862.6,13905.8}"
export DEMO_DIR="$LUA" DEMO_ESP=mq.omwscripts
export CFG_EXTRA='fallback-archive=Skyrim - Voices_en0.bsa\n'
export SETTINGS_EXTRA="[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\nneutral actor locomotion = true\nneutral lip sync = true\nneutral saves = true\n[Video]\nframerate limit = 60\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\nvoice volume = 0.0\n[Lua]\nlua num threads = 0\n[extension.skyrimse]\nmain quest start = true\nactor packages = true\nbarks = false\n${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-900}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
