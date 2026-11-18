#!/bin/bash
# tools/skyweapons2_check (task 0300: crossbows, real bows, dual wielding, dual casting; derived from
# tools/skyweaponmagic_check): starts a Skyrim neutral session in START_CELL (default the Bannered Mare) with the combat
# rules, `combat real gear` and the pack settings in PACK_EXTRA (escaped line breaks between settings), holds the first
# actor of TAGS (a substring of its base record id, default Hulda `b108142`) DIST units from the player with
# luatest/scripts/dv/global.lua (mode fire: held every frame) and lets the REAL attack control (left mouse button,
# synthetic SDL events from INPUT through OPENEDEN_RMLUI_INPUT_SCRIPT) act for the player. The test game opens
# off-screen (OPENEDEN_QUIET_WINDOW). Prints the COMBAT and DV lines.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: INPUT (the input script), START_CELL, TAGS (a quoted, comma separated list), PACK_EXTRA, GAME_EXTRA, DIST, RELEASE,
#      SHOT_FROM, SHOT_TO, EVERY (simulation seconds), CAMERA, EQUIP (quoted hex ids the player wields at EQUIP_AT real
#      seconds), WIDTH, HEIGHT, CHECK_TIMEOUT.
#      <work dir> must be under I:/Projects/OpenEden-work.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-4500}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
cp -r "$HERE/luatest/." "$WORK/luatest/"
OFFS="${OFFSETS:-}"; [ -z "$OFFS" ] && OFFS="{0,0},{0,70},{0,-70}"
echo "return { mode = '${MODE:-fire}', dist = ${DIST:-450}, tags = { ${TAGS:-'b80803'} }, offsets = { $OFFS }, pitch = ${PITCH:-0}, release = ${RELEASE:-0}, parkAway = ${PARK:-900}, every = ${EVERY:-0.5}, shotFrom = ${SHOT_FROM:-0}, shotTo = ${SHOT_TO:-0}, start = ${START:-0.3}, camera = ${CAMERA:-nil}, second = ${SECOND:-nil}, focus = ${FOCUS:-0.8}, focusTag = ${FOCUSTAG:-nil}, equip = { ${EQUIP:-} }, equipAt = ${EQUIP_AT:-3} }" > "$WORK/luatest/scripts/dv/cfg.lua"
B=$(native "$BUILD")
grep -vE '^(content|data-local|resources|extension|user-data|config)=|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
cat >> "$WORK/config/openmw.cfg" <<CFG
resources=$B/resources
data="$B/resources/vfs-mw"
data="$(native "$SKYRIM")"
data="$(native "$WORK/luatest")"
game=skyrimse
fallback-archive=Skyrim - Animations.bsa
fallback-archive=Skyrim - Interface.bsa
fallback-archive=Skyrim - Meshes0.bsa
fallback-archive=Skyrim - Meshes1.bsa
fallback-archive=Skyrim - Misc.bsa
fallback-archive=Skyrim - Textures0.bsa
fallback-archive=Skyrim - Textures1.bsa
fallback-archive=Skyrim - Textures2.bsa
fallback-archive=Skyrim - Textures3.bsa
fallback-archive=Skyrim - Textures4.bsa
fallback-archive=Skyrim - Textures5.bsa
fallback-archive=Skyrim - Textures6.bsa
fallback-archive=Skyrim - Textures7.bsa
fallback-archive=Skyrim - Textures8.bsa
content=Skyrim.esm
content=Update.esm
content=Dawnguard.esm
content=HearthFires.esm
content=Dragonborn.esm
content=delivery.omwscripts
CFG
printf '[Game]\nneutral world = true\nneutral actors = true\nneutral combat = true\nneutral actor idles = true\n%b[GUI]\nrmlui = true\nrmlui debug panel = false\n[Video]\nresolution x = %s\nresolution y = %s\nvsync mode = 0\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[extension.skyrimse]\ncombat real gear = true\ncombat trace = true\n%b' "${GAME_EXTRA:-}" "${WIDTH:-1280}" "${HEIGHT:-720}" "${PACK_EXTRA:-}" > "$WORK/config/settings.cfg"
[ -n "${INPUT:-}" ] && export OPENEDEN_RMLUI_INPUT_SCRIPT="$(native "$INPUT")"
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
export OPENEDEN_NEUTRAL_ACTOR_TRACE=1
export OPENEDEN_SHADER_CACHE="$(native "$WORK/../shadercache")"
export OPENMW_DISABLE_CRASH_CATCHER=${OPENMW_DISABLE_CRASH_CATCHER:-1}
cd "$BUILD"
set +e
timeout "${CHECK_TIMEOUT:-420}" ./openmw.exe --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --no-grab=true --random-seed 7 --start ${START_CELL:-WhiterunBanneredMare} \
    --auto-quit "$FRAMES" > "$WORK/log.txt" 2>&1
code=$?
set -e
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
grep -E "COMBAT|DV |CROSSBOW" "$WORK/log.txt" | sed 's/^\[[^]]*\] //' || true
errors=$(grep -E " E\] " "$WORK/log.txt" | grep -vc "Saving refused" || true)
echo "exit code $code, $dumps crash dumps, $errors error lines, $(ls "$WORK"/user/screenshots 2>/dev/null | wc -l) screenshots"
