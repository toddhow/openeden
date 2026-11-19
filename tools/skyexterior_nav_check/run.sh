#!/bin/bash
# tools/skyexterior_nav_check (task 0336): a neutral NPC walks a long exterior path ([extension.skyrimse] actor test goto).
#   run.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> <start "@Tamriel,x,y"> <editor id,x,y,z> <tag> [extra settings text]
# NAV336 lines (Lua) give the NPC's position every 2 simulation seconds; NACTGOTO lines (OPENEDEN_NEUTRAL_ACTOR_TRACE) the path.
# Env: FRAMES (6000), QUIT (simulation seconds, 60), NAVTRACE (extra trace), CHECK_TIMEOUT.
set -e
export OPENMW_DISABLE_CRASH_CATCHER=1 OPENEDEN_QUIET_WINDOW=1 OPENEDEN_FIXED_FRAME_TIME=${FIXED_DT:-0.0166667} OPENEDEN_NEUTRAL_ACTOR_TRACE=1
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; CELL=$5; GOTO=$6; TAG=$7; EXTRA=$8
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in I:/Projects/OpenEden-work/*) ;; *) echo "refused: $WORK"; exit 2 ;; esac
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
cp -r "$HERE/luatest/." "$WORK/luatest/"
PR=""; [ -n "${PROBE:-}" ] && PR=", probe = { $PROBE }"; [ -n "${CAM:-}" ] && PR="$PR, cam = { $CAM }"
echo "return { tags = { '$TAG' }, every = 2, quit = ${QUIT:-60}$PR }" > "$WORK/luatest/scripts/nav336/cfg.lua"
B=$(native "$BUILD")
grep -vE '^(content|data-local|resources|extension|user-data|config)=|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
{
echo "resources=$B/resources"; echo "data=\"$B/resources/vfs-mw\""; echo "data=\"$(native "$SKYRIM")\""; echo "data=\"$(native "$WORK/luatest")\""
echo game=skyrimse
for a in "Animations" "Interface" "Meshes0" "Meshes1" "Misc" "Textures0" "Textures1" "Textures2" "Textures3" "Textures4" "Textures5" "Textures6" "Textures7" "Textures8"; do echo "fallback-archive=Skyrim - $a.bsa"; done
for c in Skyrim.esm Update.esm Dawnguard.esm HearthFires.esm Dragonborn.esm; do echo "content=$c"; done
echo "content=nav336.omwscripts"
} >> "$WORK/config/openmw.cfg"
printf "[Game]\nneutral world = true\nneutral actors = true\nneutral actor idles = true\nneutral actor locomotion = true\nneutral pathfind half extents = 18 18 66.5\nneutral terrain layers = true\n[Sound]\nmaster volume = 0.0\n[extension.skyrimse]\nactor packages = ${PACKAGES:-false}\nactor test goto = $GOTO\n" > "$WORK/config/settings.cfg"
[ -n "$EXTRA" ] && printf '%b' "$EXTRA" >> "$WORK/config/settings.cfg"
cd "$BUILD"
set +e
timeout "${CHECK_TIMEOUT:-300}" ./openmw.exe --replace=config --config "$(native "$WORK/config")" --user-data "$(native "$WORK/user")" \
  --skip-menu --no-grab=true --random-seed 7 --start "$CELL" --auto-quit "${FRAMES:-8000}" ${SHOT:+--auto-screenshot $SHOT} > "$WORK/log.txt" 2>&1
echo "exit $? dumps $(ls "$WORK"/user/*.dmp 2>/dev/null | wc -l)"
grep -E "NAV336|NACTGOTO" "$WORK/log.txt" | sed -e 's/^.*\(NAV336\|NACTGOTO\)/\1/' | head -${HEAD:-200}
