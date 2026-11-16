#!/bin/bash
# tools/werewolf_check (task 0259): the Skyrim werewolf beast form in a real session. Starts in the Bannered Mare (through
# tools/journal_check/check.sh) with [GUI] rmlui, the player's Skyrim body, creatures, character creation (for the P power
# key) and [extension.skyrimse] werewolf with the power granted. A player script (luatest/scripts/ww259/player.lua) finishes
# the creation, takes pictures from a timeline and prints WW259 lines; a global script puts the player in front of Hulda.
# The keys (P, W, the attack, activate) come from INPUT (OPENEDEN_RMLUI_INPUT_SCRIPT, synthetic SDL events).
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: INPUT, SETTINGS_EXTRA (more pack settings text), SECONDS (werewolf test seconds), WW (false: the setting off)
# <work dir> must be under I:/Projects/OpenEden-work.
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
echo "return { placeAt = ${PLACE_AT:-400}, dist = ${DIST:-170}, hulda = '${HULDA:-skyrimse:b80803}', finish = ${FINISH:-false}, pitch = ${PITCH:-0.3}, distance = ${CAMDIST:-300} }" > "$LUA/scripts/ww259/cfg.lua"
export DEMO_DIR="$LUA" DEMO_ESP=ww259.omwscripts
export SETTINGS_EXTRA="[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[Game]\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\nneutral actor locomotion = true\nneutral player body = true\n[Video]\nframerate limit = 60\n[extension.skyrimse]\nmenus = true\nplayer body = true\nplayer progression = true\ncharacter creation = true\ninventory = true\ncreatures = true\ncombat trace = true\nwerewolf = ${WW:-true}\nwerewolf test grant = true\nwerewolf test seconds = ${SECONDS_FORM:-0}\n${SETTINGS_EXTRA:-}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-600}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
