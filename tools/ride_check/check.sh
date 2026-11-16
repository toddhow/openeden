#!/bin/bash
# tools/ride_check (task 0251, horse riding): starts a Skyrim session on the plains outside Whiterun's gate with the riding
# rules on (`[extension.skyrimse] riding`, `horse test owned`), runs scripts/ride/global.lua (finds the horse, takes
# pictures, logs RIDE lines) and drives the REAL controls (W, A, D, Space, the activate key) through
# OPENEDEN_RMLUI_INPUT_SCRIPT (synthetic SDL events; needs [GUI] rmlui). The sound is off.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: START (default @WhiterunWorld,18000,-12400), RIDE_PLAN (a Lua table text for cfg.lua), INPUT (an input script),
#      OWNED (default *), SETTINGS_EXTRA, CHECK_TIMEOUT, WIDTH, HEIGHT, TRACE (1: OPENEDEN_NEUTRAL_ACTOR_TRACE).
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-3000}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
cp -r "$HERE/luatest/." "$WORK/luatest/"
echo "return { ${RIDE_PLAN:-} }" > "$WORK/luatest/scripts/ride/cfg.lua"
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
content=ride.omwscripts
CFG
printf '[Game]\nneutral world = true\nneutral actors = true\nneutral actor idles = true\nneutral actor locomotion = true\nneutral player body = true\n[GUI]\nrmlui = true\nrmlui debug panel = false\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[Video]\nresolution x = %s\nresolution y = %s\nvsync mode = 0\n[extension.skyrimse]\ncreatures = true\nplayer body = true\nriding = %s\nhorse test owned = %s\n' "${WIDTH:-1280}" "${HEIGHT:-720}" "${RIDING:-true}" "${OWNED:-*}" > "$WORK/config/settings.cfg"
[ -n "${SETTINGS_EXTRA:-}" ] && printf '%b' "$SETTINGS_EXTRA" >> "$WORK/config/settings.cfg"
if [ -n "${INPUT:-}" ]; then export OPENEDEN_RMLUI_INPUT_SCRIPT="$(native "$INPUT")"; fi
[ -n "${TRACE:-}" ] && export OPENEDEN_NEUTRAL_ACTOR_TRACE=1
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
export OPENEDEN_SHADER_CACHE="$(native "$WORK/../shadercache")"
export OPENMW_DISABLE_CRASH_CATCHER=${OPENMW_DISABLE_CRASH_CATCHER:-1}
cd "$BUILD"
set +e
timeout "${CHECK_TIMEOUT:-280}" ./${EXE:-openmw.exe} --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --no-grab=true --random-seed 7 --start "${START:-@WhiterunWorld,18000,-12400}" \
    --auto-quit "$FRAMES" > "$WORK/log.txt" 2>&1
code=$?
set -e
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
errors=$(grep -cE " E\] " "$WORK/log.txt" || true)
echo "exit code $code, $dumps crash dumps, $errors error lines, $(ls "$WORK"/user/screenshots 2>/dev/null | wc -l) screenshots"
