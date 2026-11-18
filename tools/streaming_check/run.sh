#!/bin/bash
# tools/streaming_check (task 0323): one measured run of Skyrim cell streaming with [Cells] load trace on.
# NAVDB=<navmesh.db> starts with that navmesh cache (a warm second pass over the same route).
# PROFILE=play (default: neutral actors, grass, weather, Papyrus ... a play session) or bare.
#   run.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> <walk|fly|gate|hop> <incr 0|1> [extra settings text]
# walk and fly cross the exterior grid of Whiterun's hold along a fixed line (simulation time, fixed frame time);
# gate is the Mare door -> Whiterun street -> the gate door -> Tamriel -> back (real seconds); hop is the Dawnstar
# barracks door pair (interior to interior). The trace is <work>/user/load-trace.jsonl; summarize with analyze.py.
set -e
export OPENMW_DISABLE_CRASH_CATCHER=1 OPENEDEN_QUIET_WINDOW=1 OPENEDEN_FIXED_FRAME_TIME=${FIXED_DT:-0.0166667} OPENEDEN_NEUTRAL_UNLOCK_DOORS=1
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; MODE=$5; INCR=$6; EXTRA=$7
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in I:/Projects/OpenEden-work/*) ;; *) echo "refused: $WORK"; exit 2 ;; esac
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
cp -r "$HERE/../neutral_session_check/luatest/." "$WORK/luatest/"
B=$(native "$BUILD")
grep -vE '^(content|data-local|resources|extension|user-data|config)=|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
{
echo "resources=$B/resources"; echo "data=\"$B/resources/vfs-mw\""; echo "data=\"$(native "$SKYRIM")\""; echo "data=\"$(native "$WORK/luatest")\""
echo game=skyrimse
for a in "Animations" "Interface" "Meshes0" "Meshes1" "Misc" "Textures0" "Textures1" "Textures2" "Textures3" "Textures4" "Textures5" "Textures6" "Textures7" "Textures8"; do echo "fallback-archive=Skyrim - $a.bsa"; done
for c in Skyrim.esm Update.esm Dawnguard.esm HearthFires.esm Dragonborn.esm; do echo "content=$c"; done
} >> "$WORK/config/openmw.cfg"
case "$MODE" in
  walk) CELL='@Tamriel,19320,-7433'; FRAMES=${FRAMES:-60000}
        echo "return { mode='walk', start=${START:-50}, speed=${SPEED:-350}, dir={0.8,0.6}, from={19320,-7433}, duration=${DURATION:-90} }" ;;
  fly)  CELL='@Tamriel,19320,-7433'; FRAMES=${FRAMES:-60000}
        echo "return { mode='fly', start=${START:-50}, speed=${SPEED:-1500}, dir={0.8,0.6}, from={19320,-7433}, rise=${RISE:-1500}, duration=${DURATION:-40} }" ;;
  rwalk|rfly) CELL='@Tamriel,19320,-7433'; FRAMES=${FRAMES:-60000}
        echo "return { mode='real', start=${START:-50}, dir={0.8,0.6}, from={19320,-7433}, rise=${RISE:-$([ "$MODE" = rfly ] && echo 1500 || echo 400)}, speedAttr=${SPEEDATTR:-nil}, athletics=${ATHLETICS:-$([ "$MODE" = rfly ] && echo 600 || echo 133)}, duration=${DURATION:-90} }" ;;
  *) CELL=WhiterunBanneredMare; FRAMES=${FRAMES:-4000}; echo "return {}" ;;
esac > "$WORK/luatest/scripts/stream323/cfg.lua"
case "$MODE" in
  walk|fly|rwalk|rfly) echo "content=stream323.omwscripts" >> "$WORK/config/openmw.cfg" ;;
  gate) echo "content=stream323gate.omwscripts" >> "$WORK/config/openmw.cfg" ;;
  hop) CELL=DawnstarBarracks; echo "content=stream323hop.omwscripts" >> "$WORK/config/openmw.cfg" ;;
esac
PROFILE_GAME='neutral world = true
neutral actors = true
neutral actor idles = true
neutral actor locomotion = true
neutral combat = true
neutral spell effects = true
neutral object animations = true
neutral actor process levels = true
neutral inventory = true
neutral weather = true
neutral world materials = true
neutral havok collision = true
neutral pathfind half extents = 18 18 66.5
neutral tree sway = true
neutral terrain layers = true
neutral grass = true
'
PROFILE_PACK='[extension.skyrimse]
actor packages = true
inventory = true
papyrus = true
creatures = true
'
if [ "${PROFILE:-play}" = bare ]; then PROFILE_GAME='neutral world = true
'; PROFILE_PACK=''; fi
[ -n "${NAVDB:-}" ] && cp "$NAVDB" "$WORK/user/navmesh.db"
printf "[Game]
${PROFILE_GAME}[Cells]
load trace = true
incremental cell build = $([ "$INCR" = 1 ] && echo true || echo false)
[Sound]
master volume = 0.0
${PROFILE_PACK}" > "$WORK/config/settings.cfg"
[ -n "$EXTRA" ] && printf '%b' "$EXTRA" >> "$WORK/config/settings.cfg"
cd "$BUILD"
set +e
timeout "${CHECK_TIMEOUT:-400}" ./openmw.exe --replace=config --config "$(native "$WORK/config")" --user-data "$(native "$WORK/user")" \
  --skip-menu --no-grab=true --random-seed 7 --start "$CELL" --auto-quit "$FRAMES" > "$WORK/log.txt" 2>&1
echo "exit $? dumps $(ls "$WORK"/user/*.dmp 2>/dev/null | wc -l) trace $(wc -l < "$WORK/user/load-trace.jsonl" 2>/dev/null)"
