#!/bin/bash
# tools/services_check (task 0219, milestone M-S6): the Skyrim SE dialogue services in game: training, renting a room and
# gifts. Starts a neutral session (default: the Bannered Mare) with [GUI] rmlui, [Game] neutral inventory and combat, the pack
# settings inventory, player progression, rest, crime and services, a global Lua script (luatest/scripts/svc219) that
# moves the player to the NPC and activates it, and the real input script INPUT (clicks and screenshots) through
# OPENEDEN_RMLUI_INPUT_SCRIPT. The test game opens off-screen (OPENEDEN_QUIET_WINDOW) and the sound is off.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: CELL (cell editor id to move to, default none), TARGET (record id of the NPC base, e.g. skyrimse:b108249),
#      DIRECT ("'gift', '0001a6da', '0001a6d9', 'Name'": send the engine's service event instead of activating),
#      START_AT (update count, default 9000), BED ("x, y, z, heading, pitch, frame" a place to stand at a bed),
#      SAVE_AT (update count to save at, default 0), LOADARG (default --start WhiterunBanneredMare), INPUT, SETTINGS_EXTRA,
#      PACK_EXTRA (more [extension.skyrimse] lines), EXE (default openmw.exe), USER_KEEP (a user-data folder to keep),
#      WIDTH and HEIGHT (default 1280 and 720), CHECK_TIMEOUT.
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-6000}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
if [ -n "${USER_KEEP:-}" ]; then
    rm -rf "$WORK/config" "$WORK/luatest"; mkdir -p "$WORK/config"
else
    rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user"
fi
LUA="$WORK/luatest"
mkdir -p "$LUA"; cp -r "$HERE/luatest/." "$LUA/"
{
    echo "return {"
    echo "  cell = '${CELL:-}', target = '${TARGET:-}', startAt = ${START_AT:-9000}, saveAt = ${SAVE_AT:-0},"
    if [ -n "${BED:-}" ]; then echo "  bed = { $BED },"; else echo "  bed = nil,"; fi
    if [ -n "${DIRECT:-}" ]; then echo "  direct = { $DIRECT },"; fi
    echo "}"
} > "$LUA/scripts/svc219/cfg.lua"
B=$(native "$BUILD")
grep -vE '^(content|data-local|resources|extension|user-data|config)=|^fallback=Movies_|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
cat >> "$WORK/config/openmw.cfg" <<CFG
resources=$B/resources
data="$B/resources/vfs-mw"
data="$(native "$SKYRIM")"
data="$(native "$LUA")"
game=skyrimse
fallback-archive=Skyrim - Animations.bsa
fallback-archive=Skyrim - Interface.bsa
fallback-archive=Skyrim - Meshes0.bsa
fallback-archive=Skyrim - Meshes1.bsa
fallback-archive=Skyrim - Misc.bsa
fallback-archive=Skyrim - Sounds.bsa
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
content=svc219.omwscripts
CFG
printf '[Game]\nneutral world = true\nneutral actors = true\nneutral saves = true\nneutral combat = true\nneutral inventory = true\nneutral actor idles = true\n[GUI]\nrmlui = true\nrmlui debug panel = false\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[Video]\nresolution x = %s\nresolution y = %s\nvsync mode = 0\nframerate limit = 60\n[extension.skyrimse]\ninventory = true\nplayer progression = true\nrest = true\ncrime = true\nservices = %s\n%b' "${WIDTH:-1280}" "${HEIGHT:-720}" "${SERVICES:-true}" "${PACK_EXTRA:-}" > "$WORK/config/settings.cfg"
[ -n "${SETTINGS_EXTRA:-}" ] && printf '%b' "$SETTINGS_EXTRA" >> "$WORK/config/settings.cfg"
[ -n "${INPUT:-}" ] && export OPENEDEN_RMLUI_INPUT_SCRIPT="$(native "$INPUT")"
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
export OPENEDEN_SHADER_CACHE="$(native "$WORK/../shadercache")"
export OPENMW_DISABLE_CRASH_CATCHER=${OPENMW_DISABLE_CRASH_CATCHER:-1}
USERDIR="${USER_KEEP:-$WORK/user}"
cd "$BUILD"
set +e
timeout "${CHECK_TIMEOUT:-420}" ./${EXE:-openmw.exe} --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$USERDIR")" --skip-menu --no-grab=true --random-seed 7 ${LOADARG:---start WhiterunBanneredMare} \
    --auto-quit "$FRAMES" > "$WORK/log.txt" 2>&1
code=$?
set -e
dumps=$(ls "$USERDIR"/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
grep -E "SVC219|SERVICES|NINV service|NTALK service|REST |CRIME trespass" "$WORK/log.txt" | sed 's/^\[[^]]*\] //' | head -${LOGMAX:-60} || true
errors=$(grep -E " E\] " "$WORK/log.txt" | wc -l || true)
echo "exit code $code, $dumps crash dumps, $errors error lines, $(ls "$USERDIR"/screenshots 2>/dev/null | wc -l) screenshots"
