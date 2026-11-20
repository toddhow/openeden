#!/bin/bash
# tools/showcase_check (task 0360): captures the Skyrim SE showcase shots the same way every time, with every finished feature on
# (the living test profile, tools/testprofile/profile.cfg, plus the Skyrim pack's combat and creature settings).
#
#   check.sh <build dir> <Skyrim Data folder> <work dir> <mode> [argument]
#
# mode:  bake-terrain            openmw --bake-lod-terrain in WhiterunWorld (the far land tiles)
#        bake-objects            openmw --bake-lod-objects in WhiterunWorld (the far object clusters)
#        bake-gi-interior <id>   openmw --bake-gi <interior editor id>
#        bake-gi-exterior <arg>  openmw --bake-gi-exterior "x0,y0:x1,y1" (FIELD cells of 8192 units) from START_AT
#        shots <group>           runs groups/<group>.lua: a list of shots (see luatest/scripts/sc/global.lua); START is the group's
#                                `start` field. Screenshots are renamed to <work>/keep/<group>/<NN>_<name>.png from the SHOWCASE log lines.
# <work dir> must be under I:/Projects/OpenEden-work. Bakes are derived from the user's game data: they stay in <work dir>.
# The game opens off-screen (OPENEDEN_QUIET_WINDOW=1) and plays no sound ([Sound] master volume = 0.0).
# Environment: SC_NOHUD (1 = hide the whole HUD in the shots), SC_FRAMES (frame limit, default 40000), SC_TIMEOUT (seconds, default 900), SC_OVERRIDES (extra INI merged last),
#              SC_BASE_CFG (an openmw.cfg of a Morrowind setup: its data folders, archives and fallback= lines are kept; default the n6saves mwA copy),
#              SC_HOUR (the group's first hour), OPENEDEN_FIXED_FRAME_TIME (default 0.016667).
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# SC_WEATHER: an editor id of a Skyrim weather forced for the whole run (default SkyrimClear; "none" keeps the regional weather)
[ "${SC_WEATHER-SkyrimClear}" != none ] && export OPENEDEN_NEUTRAL_FORCE_WEATHER="${SC_WEATHER-SkyrimClear}"
export OPENMW_DISABLE_CRASH_CATCHER="${OPENMW_DISABLE_CRASH_CATCHER-1}"
export PATH="$PATH:/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin:/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin/Release:/c/Windows/System32:/c/Windows"
BUILD=$1; SKYRIM=$2; WORK=$3; MODE=$4; ARG=${5:-}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest" "$WORK/keep"
cp -r "$HERE/luatest/." "$WORK/luatest/"
# SC_NOHUD=1: clean captures with no HUD at all ([GUI] rmlui off, Morrowind HUD hidden by Lua ui.setHudVisibility)
rm -f "$WORK/luatest/scripts/sc/nohud.lua"; [ "${SC_NOHUD-}" = 1 ] && echo "return true" > "$WORK/luatest/scripts/sc/nohud.lua"
B=$(native "$BUILD")
START_AT=${START_AT:-@WhiterunWorld,25670,-7633}
GROUP=""
case "$MODE" in shots) GROUP=$ARG; cp "$HERE/groups/$GROUP.lua" "$WORK/luatest/scripts/sc/cfg.lua" ;; esac
grep -vE '^(content|data-local|resources|extension|user-data|config|game)=|vfs-mw|worldobjects_check|omwscripts' "${SC_BASE_CFG:-/i/Projects/OpenEden-work/n6saves/mwA/config/openmw.cfg}" > "$WORK/config/openmw.cfg"
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
[ "$MODE" = shots ] && echo "content=sc.omwscripts" >> "$WORK/config/openmw.cfg"
# settings: the living test profile with the showcase overrides merged on top
cat > "$WORK/overrides.cfg" <<OV
[Video]
resolution x = 1600
resolution y = 900
[Sound]
master volume = 0.0
[Game]
neutral world = true
[WorldLod]
view distance = auto
[extension.skyrimse]
menus = __DEL__
character creation = __DEL__
combat ai = true
creatures = true
combat factions = true
combat test weapon = IronSword
${SC_OVERRIDES:-}
OV
# SC_NOHUD=1: no RmlUi overlay (so none of the pack's HUD) and, from the player script, no Morrowind HUD either
[ "${SC_NOHUD-}" = 1 ] && printf '[GUI]
rmlui = false
' >> "$WORK/overrides.cfg"
[ -n "$GROUP" ] && [ -f "$HERE/groups/$GROUP.ini" ] && cat "$HERE/groups/$GROUP.ini" >> "$WORK/overrides.cfg"
awk -f "$HERE/merge.awk" "$WORK/overrides.cfg" "${SC_BASE:-$REPO/tools/testprofile/profile.cfg}" > "$WORK/config/settings.cfg"
[ -n "${SC_EXTRA_INI:-}" ] && printf '%b' "$SC_EXTRA_INI" > "$WORK/extra.ini"
cd "$BUILD"
LIMIT=${SC_TIMEOUT:-900}
case "$MODE" in
    bake-terrain) export OPENEDEN_LOD_BAKE_WORLD=WhiterunWorld; RUN="--start $START_AT --bake-lod-terrain"; LIMIT=${SC_TIMEOUT:-1800} ;;
    bake-objects) export OPENEDEN_LOD_BAKE_WORLD=WhiterunWorld; RUN="--start $START_AT --bake-lod-objects"; LIMIT=${SC_TIMEOUT:-2400} ;;
    bake-gi-interior) RUN="--bake-gi $ARG" ;;
    bake-gi-exterior) RUN="--start $START_AT --bake-gi-exterior $ARG"; LIMIT=${SC_TIMEOUT:-3000} ;;
    shots) START=$(sed -n 's/^.*start *= *"\([^"]*\)".*$/\1/p' "$WORK/luatest/scripts/sc/cfg.lua" | head -1); RUN="--start ${START:-$START_AT} --auto-quit ${SC_FRAMES:-40000}" ;;
    *) echo "unknown mode $MODE"; exit 2 ;;
esac
[ "$MODE" = shots ] && rm -rf "$WORK/user/screenshots"
set +e
OPENEDEN_FIXED_FRAME_TIME=${OPENEDEN_FIXED_FRAME_TIME-0.016667} timeout "$LIMIT" ./openmw.exe --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --no-grab=true --random-seed 7 $RUN > "$WORK/log_${MODE}${GROUP:+_$GROUP}.txt" 2>&1
echo "exit code $?"
if [ "$MODE" = shots ]; then
    OUT="$WORK/keep/$GROUP"; rm -rf "$OUT"; mkdir -p "$OUT"
    python -I "$HERE/rename.py" "$WORK/log_${MODE}_$GROUP.txt" "$WORK/user/screenshots" "$OUT"
fi
grep -E "SHOWCASE|\[GI\]|LOD" "$WORK/log_${MODE}${GROUP:+_$GROUP}.txt" | head -20
