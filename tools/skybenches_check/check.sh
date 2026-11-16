#!/bin/bash
# tools/skybenches_check (task 0229, from tools/skyrimcraft_check of task 0199, milestone M-S7), a copy of tools/inventory_check (task 0182, milestone M-S6): starts a Skyrim neutral session in the Bannered Mare with the
# inventory switched on ([Game] neutral inventory, [extension.skyrimse] inventory, [GUI] rmlui), stands the player before a
# container (luatest/scripts/inv/place.lua), and runs the real input script (INPUT: the activate key, mouse clicks and
# screenshots) through OPENEDEN_RMLUI_INPUT_SCRIPT. Prints the INV, NINV and N6INV lines of the log. The test game opens
# off-screen (OPENEDEN_QUIET_WINDOW).
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: INPUT (the input script, required), BASE_ID (the container's record id, "skyrimse:b<decimal base>"), NEAR ("x,y,z"
#      of the container), FROM ("x,y": the side the player stands on), DIST (default 120), PLACE (update number, 600),
#      OWNER and ITEM (hex ids the save and load phases print the counts of, default the wardrobe and its boots), SETTINGS_EXTRA (more settings text), WIDTH and HEIGHT (default 1280 and 720). <work dir> must be under
#      I:/Projects/OpenEden-work.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-3000}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
if [ "${PHASE:-none}" = load ]; then
    # the load phase reuses the work folder (and its save) of the save phase
    rm -rf "$WORK/luatest" "$WORK/log.txt"; mkdir -p "$WORK/luatest"
else
    rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
fi
cp -r "$HERE/luatest/." "$WORK/luatest/"
IFS=',' read -r nx ny nz <<< "${NEAR:-0,0,0}"
IFS=',' read -r fx fy <<< "${FROM:-0,0}"
echo "return { base = '${BASE_ID:-}', near = { $nx, $ny, $nz }, from = { $fx, $fy }, dist = ${DIST:-120}, frame = ${PLACE:-600}, saveAt = ${SAVEAT:-1800}, base2 = '${BASE2:-}', near2 = { ${NEAR2:-0,0,0} }, from2 = nil, dist2 = ${DIST2:-100}, frame2 = ${PLACE2:-0}, owner = '${OWNER:-000c8fcd}', item = '${ITEM:-00086993}', worn = '${WORN:-00086993}', kind = '${KIND:-alchemy}', openFrame = ${OPENFRAME:-700}, name = '${BENCHNAME:-Bench}', kind2 = '${KIND2:-}', openFrame2 = ${OPENFRAME2:-0}, equipFrame = ${EQUIPFRAME:-0}, equipItem = '${EQUIPITEM:-}', equipItemFrame = ${EQUIPITEMFRAME:-0}, useItem = '${USEITEM:-}', useFrame = ${USEFRAME:-0}, swingFrame = ${SWINGFRAME:-0}, pullTag = '${PULLTAG:-}', pullFrame = ${PULLFRAME:-0}, pullDx = ${PULLDX:-40}, pullDy = ${PULLDY:-0}, yawOff = ${YAWOFF:-0}, probe = ${PROBE:-false} }" > "$WORK/luatest/scripts/inv/cfg.lua"
echo "return '${PHASE:-none}'" > "$WORK/luatest/scripts/inv/phase.lua"
echo "return { pitch = ${PITCH:-0}, frame = ${PITCHFRAME:-590} }" > "$WORK/luatest/scripts/inv/pitchcfg.lua"
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
content=n6inv.omwscripts
CFG
printf '[Game]\nneutral world = true\nneutral actors = true\nneutral inventory = true\nneutral combat = true\nneutral actor idles = true\n[GUI]\nrmlui = true\nrmlui debug panel = false\n[Video]\nresolution x = %s\nresolution y = %s\nvsync mode = 0\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[extension.skyrimse]\ninventory = true\ncrafting = true\ninventory test items = %s\n' "${WIDTH:-1280}" "${HEIGHT:-720}" "${TESTITEMS:-}" > "$WORK/config/settings.cfg"
[ -n "${SETTINGS_EXTRA:-}" ] && printf '%b' "$SETTINGS_EXTRA" >> "$WORK/config/settings.cfg"
[ -n "${INPUT:-}" ] && export OPENEDEN_RMLUI_INPUT_SCRIPT="$(native "$INPUT")"
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
export OPENEDEN_NEUTRAL_DOOR_TRACE=1
export OPENEDEN_SHADER_CACHE="$(native "$WORK/../shadercache")"
export OPENMW_DISABLE_CRASH_CATCHER=${OPENMW_DISABLE_CRASH_CATCHER:-1}
cd "$BUILD"
set +e
timeout "${CHECK_TIMEOUT:-300}" ./openmw.exe --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --no-grab=true --random-seed 7 ${LOADARG:---start WhiterunBanneredMare} \
    --auto-quit "$FRAMES" > "$WORK/log.txt" 2>&1
code=$?
set -e
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
grep -E "INV |COMBAT poison|COMBAT effect|COMBAT damage|CRAFT|tooltip|ACT|NINV|N6INV|NCONT|\[UI\]|SkyrimInventory|skyrimse.inventory|requires" "$WORK/log.txt" | sed 's/^\[[^]]*\] //' || true
errors=$(grep -E " E\] " "$WORK/log.txt" | grep -vc "Saving refused" || true)
echo "exit code $code, $dumps crash dumps, $errors error lines, $(ls "$WORK"/user/screenshots 2>/dev/null | wc -l) screenshots"
