#!/bin/bash
# tools/skygi_check (task 0358): bakes and shows the baked global illumination of a Skyrim SE (neutral world) session.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> <mode> [argument] [frames]
#
# mode: interior <editor id>   runs `openmw --bake-gi <id>`  (bake of one interior, written to <work>/user/openeden/probes)
#       exterior <arg>         runs `openmw --bake-gi-exterior <arg>` ("x,y" or "x0,y0:x1,y1": field cells of 8192 units) in the world of
#                              START_AT (default @WhiterunWorld,25670,-7633; the player starts there)
#       spots <start> [frames] starts at <start> (default @WhiterunWorld,25670,-7633), visits GI_SPOTS and takes a picture at each; frames = quit frame
#       view <start> [frames]  starts at <start> (an interior id or @world,x,y) with GI settings from GI_SETTINGS, with the
#                              probes in <work>/user and <work>/data on the data path; frame at FRAME (default 600)
# <base config dir> holds the openmw.cfg of a Morrowind setup (a COPY is made). <work dir> must be under I:/Projects/OpenEden-work.
# Bakes are derived from the user's game data: they stay in <work dir>, never in the repository. Test games open off-screen.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; MODE=$5; ARG=${6:-}; FRAMES=${7:-600}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
HERE=$(cd "$(dirname "$0")" && pwd)
cp -r "$HERE/luatest/." "$WORK/luatest/"
# GI_SPOTS: "x,y,yaw,pitch;x,y,yaw,pitch" for the "spots" mode; GI_HOUR the game hour (default 12); GI_DWELL real seconds at each spot.
spots=""; IFS=';' read -ra pts <<< "${GI_SPOTS:-}"; for pt in "${pts[@]}"; do [ -n "$pt" ] && spots="$spots{${pt},0},"; done
echo "return { hour = ${GI_HOUR:-12}, dwell = ${GI_DWELL:-14}, spots = { $spots } }" > "$WORK/luatest/scripts/skygi/cfg.lua"
B=$(native "$BUILD")
grep -vE '^(content|data-local|resources|extension|user-data|config|game)=|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
cat >> "$WORK/config/openmw.cfg" <<CFG
resources=$B/resources
data="$B/resources/vfs-mw"
data="$(native "$SKYRIM")"
data="$(native "$WORK/user")"
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
CFG
[ "$MODE" = spots ] && echo "content=skygi.omwscripts" >> "$WORK/config/openmw.cfg"
printf '[Game]\nneutral world = true\n[Sound]\nmaster volume = 0.0\n%b' "${GI_SETTINGS:-}" > "$WORK/config/settings.cfg"
cd "$BUILD"
case "$MODE" in
    interior) RUN="--bake-gi $ARG"; LIMIT=${CHECK_TIMEOUT:-900} ;;
    exterior) RUN="--start ${START_AT:-@WhiterunWorld,25670,-7633} --bake-gi-exterior $ARG"; LIMIT=${CHECK_TIMEOUT:-3000} ;;
    spots) RUN="--start ${ARG:-@WhiterunWorld,25670,-7633}"; LIMIT=${CHECK_TIMEOUT:-400}; QUIT="--auto-quit $FRAMES" ;;
    view) RUN="--start $ARG --auto-screenshot $FRAMES --auto-quit $((FRAMES + 60))"; LIMIT=${CHECK_TIMEOUT:-300} ;;
esac
set +e
OPENEDEN_FIXED_FRAME_TIME=${OPENEDEN_FIXED_FRAME_TIME-0.016667} timeout "$LIMIT" ./openmw.exe --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --no-grab=true --random-seed 7 $RUN ${QUIT:-} > "$WORK/log.txt" 2>&1
echo "exit code $?"
grep -E "\[GI\]|GI:|gi" "$WORK/log.txt" | head -40
