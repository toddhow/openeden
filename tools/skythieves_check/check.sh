#!/bin/bash
# tools/skythieves_check (task 0271): a new game in Riften's market (the Thieves Guild questline TG00 and TG01 through the game's own
# quests, dialogue, scenes and Papyrus), the Papyrus host, scenes, dialogue and voices, the journal
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
CFG_LUA="return { every = ${EVERY:-120}, from = 1, near = ${NEAR:-6}, radius = ${RADIUS:-900}, hour = ${HOUR:-12}"
[ -n "${TELEPORT:-}" ] && CFG_LUA="$CFG_LUA, teleport = { $TELEPORT }"
[ -n "${AIM:-}" ] && CFG_LUA="$CFG_LUA, aim = { $AIM }"
[ -n "${MOVES:-}" ] && CFG_LUA="$CFG_LUA, moves = { $MOVES }"
[ -n "${ACTIVATE:-}" ] && CFG_LUA="$CFG_LUA, activate = { $ACTIVATE }"
# task 0275: STEPS = "{ 3400,'110717',90,173200,-96590,20 }, { ... }" (see mq/global.lua)
[ -n "${STEPS:-}" ] && CFG_LUA="$CFG_LUA, steps = { $STEPS }"
echo "$CFG_LUA }" > "$LUA/scripts/mq/cfg.lua"
export LOADARG="${LOADARG:---start @RiftenWorld,173100,-96800,11120}"
export DEMO_DIR="$LUA" DEMO_ESP=mq.omwscripts
export CFG_EXTRA='fallback-archive=Skyrim - Voices_en0.bsa\n'
export SETTINGS_EXTRA="[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\nneutral actor locomotion = true\nneutral lip sync = true\nneutral saves = true\n[Video]\nframerate limit = 60\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\nvoice volume = 0.0\n[Lua]\nlua num threads = 0\n[extension.skyrimse]\ninventory = true
lockpicking and books = true
crime = true
sneak = true\nactor packages = true\nbarks = false\n${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-900}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
