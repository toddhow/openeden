#!/bin/bash
# tools/skydungeon_check (task 0289): Bleak Falls Barrow's puzzle through the game's own scripts. Starts a Skyrim neutral
# session in BleakFallsBarrow01 with the Papyrus host, `dungeon mechanics`, the inventory and object animations on, and
# runs a plan of the global script (cfg.plan: { seconds, what, ... }): 'go x y z yaw' (places the player), 'act x y z
# radius' (activates the object nearest to the point as the player does), 'shot cx cy cz tx ty tz' (a picture from a free
# camera), 'trace tag'. Prints the DUNGEON, NANIM, PAPYRUS animation and DNG lines of the log.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: PLAN (Lua text of the plan table; the default is the pillar puzzle), CELL (default BleakFallsBarrow01), ITEMS (the pack
#      setting `inventory test items`, e.g. 00039647 for the golden claw),
#      SETTINGS_EXTRA, CHECK_TIMEOUT. <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-4000}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
LUA="$WORK.luatest"
rm -rf "$LUA"; mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
# pillars A (5656,-384), B (-640), C (-896) are base 698574; they start at 1, 3 and 1 and need 2, 2 and 3 (positions go 1 to 2
# to 3 to 1). The lever is base 753076; the gate is 4992,-1408,-768.
P=698574
DEFAULT_PLAN="{ {4,'go',5300,-640,-690,1.5708}, {6,'shot',5330,-640,-640,5656,-640,-740}, {8,'trace','start'},
 {10,'act',5002,-903,-754,60,'skyrimse:b753076'}, {18,'act',5656,-384,-768,80,'skyrimse:b$P'}, {21,'act',5656,-640,-768,80,'skyrimse:b$P'},
 {24,'act',5656,-640,-768,80,'skyrimse:b$P'}, {27,'act',5656,-896,-768,80,'skyrimse:b$P'}, {30,'act',5656,-896,-768,80,'skyrimse:b$P'},
 {34,'shot',5330,-640,-640,5656,-640,-740}, {36,'shot',5250,-1150,-640,4992,-1408,-700}, {37,'act',5002,-903,-754,60,'skyrimse:b753076'},
 {45,'shot',5250,-1150,-640,4992,-1408,-700}, {46,'trace','end'} }"
echo "return { plan = ${PLAN:-$DEFAULT_PLAN} }" > "$LUA/scripts/dng/cfg.lua"
export DEMO_DIR="$LUA" DEMO_ESP=dng.omwscripts
export SETTINGS_EXTRA="[Game]\nneutral combat = true\nneutral actor idles = true\nneutral inventory = true\nneutral object animations = true\n[extension.skyrimse]\ninventory = true\ndungeon mechanics = true\n${ITEMS:+inventory test items = $ITEMS\n}[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\nvoice volume = 0.0\n${SETTINGS_EXTRA:-}"
export LOADARG="--start ${CELL:-BleakFallsBarrow01}"
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-400}
bash "$HERE/../journal_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
grep -E "DNG|DUNGEON|NANIM|PAPYRUS animation|Skyrim sequence model|Skyrim Havok" "$WORK/log.txt" | sed 's/^\[[^]]*\] //' || true
