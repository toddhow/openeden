#!/bin/bash
# tools/wordwall_check (task 0256, milestone M-S7): walks the player up to a real word wall of Skyrim.esm (default: Bleak
# Falls Barrow's first wall, cell BleakFallsBarrow02) with the Papyrus host and the shouts on; the wall's own scripts
# (WordWallTriggerScript and relatives) teach the word. Prints the WW256, PAPYRUS and SHOUTS lines of the log.
# PHASE=run (default) places the player, and with SAVEAT > 0 saves at that second; PHASE=load loads that save with no
# test settings (the learned word must come back from the shouts chunk alone).
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: POS "x,y,z" and LOOK "x,y" (where the player stands and what he faces), PLACEAT (seconds, default 3),
#      SAVEAT (default 0), SETTINGS_EXTRA, CELL (default BleakFallsBarrow02), UNLOCK (a WOOP editor id the script unlocks).
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-1800}
PHASE=${PHASE:-run}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
IFS=',' read -r px py pz <<< "${POS:--7782,71,500}"
IFS=',' read -r lx ly <<< "${LOOK:--7876,188}"
echo "return { phase = '$PHASE', pos = { $px, $py, $pz }, look = { $lx, $ly }, placeAt = ${PLACEAT:-3}, saveAt = ${SAVEAT:-0}, unlock = '${UNLOCK:-}' }" > "$LUA/scripts/ww256/cfg.lua"
BASE_SET='shouts = true\ncombat trace = true\n'
if [ "$PHASE" = load ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -n "$SAVE" ] || { echo "no save in $WORK/user/saves"; exit 3; }
    export LOADARG="--load-savegame $(native "$SAVE")"
    WORK="$WORK.load"
    TEST_SET=''
    echo "return { phase = 'load', placeAt = 99999, saveAt = 0 }" > "$LUA/scripts/ww256/cfg.lua"
else
    TEST_SET='shouts test shout = UnrelentingForceShout\n'
fi
[ -z "${LOADARG:-}" ] && export LOADARG="--start ${CELL:-BleakFallsBarrow02}"
export DEMO_DIR="$LUA" DEMO_ESP=ww256.omwscripts
export SETTINGS_EXTRA="${BASE_SET}${TEST_SET}${SETTINGS_EXTRA:-}\n[Game]\nneutral combat = true\nneutral actor idles = true\nneutral saves = true\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\nvoice volume = 0.0\n"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-300}
exec bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
