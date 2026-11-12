#!/bin/bash
# tools/neutral_session_check (tasks 0107 and 0109, neutral world stages N3a-2b and N3a-3): starts the game with
# game=skyrimse and [Game] neutral world on and the five Skyrim SE masters as the ONLY content (no Morrowind.esm), the
# player in a named Skyrim interior (--start), and checks the log: the pack was handed only the Skyrim plugins, built its
# world view, registered its source, the session started through its own start path, the player stub stands in the
# cell, the engine inserted the cell's objects (counts by kind), and a menu script's attempt to save was refused. Prints
# aggregates only. Read only: nothing is written outside <work dir>. The test game opens off-screen
# (OPENEDEN_QUIET_WINDOW).
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [interior editor id] [frames] [screenshot frame]
#
# <base config dir> holds the openmw.cfg of a Morrowind setup (a COPY; its resources/extension/content/data-local lines
# are replaced; its Morrowind data folder stays in the VFS for the engine's interface files). <work dir> must be under
# I:/Projects/OpenEden-work.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; CELL=${5:-WhiterunBanneredMare}; FRAMES=${6:-400}; SHOT=${7:-}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -n "${KEEP_USER:-}" ] && [ -d "$WORK/user" ]; then
    mv "$WORK/user" "$WORK.user.keep"; rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/luatest"; mv "$WORK.user.keep" "$WORK/user"
else
    rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
fi
cp -r "$HERE/luatest/." "$WORK/luatest/"
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
content=n3a3save.omwscripts
CFG
# Optional extra openmw.cfg text (for example "fallback-archive=_ResourcePack.bsa", which holds the textures\skyrimhd files some tree
# meshes name), appended as given.
[ -n "${CFG_EXTRA:-}" ] && printf '%b' "$CFG_EXTRA" >> "$WORK/config/openmw.cfg"
# Task 0121: "n5actors" visits each neutral actor of the Bannered Mare and takes a picture of it (use at least 1500 frames).
[ "$SHOT" = n5actors ] && echo "content=n5actors.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = n5idle ] && echo "content=n5idle.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0141: "n5talk" activates each neutral actor of the Bannered Mare and takes pictures of the conversation box; the presses come from OPENEDEN_NEUTRAL_DIALOGUE_PRESS (use at least 1500 frames).
[ "$SHOT" = n5talk ] && echo "content=n5talk.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0146: "n5lip" stands before Hulda, talks to her and takes a picture of her face every quarter second (use at least 6000 frames).
[ "$SHOT" = n5lip ] && echo "content=n5lip.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0135: "n5body" walks the player into each neutral actor of the Bannered Mare (use at least 3600 frames).
[ "$SHOT" = n5body ] && echo "content=n5body.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = n5bodydraw ] && echo "content=n5bodydraw.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0135 stage S2: "n5walk" takes pictures of a neutral actor walked by OPENEDEN_NEUTRAL_ACTOR_WALK (use at least 900 frames).
[ "$SHOT" = n5walk ] && echo "content=n5walk.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0135 stage S3 (actor hooks): the same scenario, but the pack walks the actor: put "[extension.skyrimse]" and
# "actor test walk = <editor id>" into SETTINGS_EXTRA and OPENEDEN_NEUTRAL_ACTOR_TRACE=1 in the environment; the NACTWALK
# lines then follow the pacing actor. Leave OPENEDEN_NEUTRAL_ACTOR_WALK unset.
# Task 0135 stage S4 (go-to): "n5goto" takes pictures every 2 s of the actor the pack sends along a navmesh path (luatest/scripts/n5/goto.lua; use at least 2400 frames).
[ "$SHOT" = n5goto ] && echo "content=n5goto.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = look ] && echo "content=n3a4look.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0131 stage N3a-6: "ms0sweep" runs the Lua API sweep over the neutral cell (use at least 1500 frames).
[ "$SHOT" = ms0sweep ] && echo "content=ms0sweep.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0110 stage N3a-4b: "collision" as the screenshot argument runs the player through the collision script (stands, then
# walks four legs, logging height above the floor and what stops it); use at least 1000 frames.
[ "$SHOT" = collision ] && echo "content=n3a4bcollision.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = colldraw ] && echo "content=n3a4bdraw.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0128: "stairs" walks the player up and down the Bannered Mare stairs (use at least 1500 frames); "keylocked" and "keycontainer" are the lock scenarios (give the key with OPENEDEN_NEUTRAL_KEYS).
[ "$SHOT" = stairs ] && echo "content=n3a4bstairs.omwscripts" >> "$WORK/config/openmw.cfg"
case "$SHOT" in keylocked|keycontainer) echo "content=n4$SHOT.omwscripts" >> "$WORK/config/openmw.cfg" ;; esac
# Task 0115 stage N4 (doors): "doorshop", "doorsexterior" and "doorslocked" run the door scenarios (use at least 2400 frames).
case "$SHOT" in doorshop|doorsexterior|doorslocked|doorsstreet|doorsgate|doorsdrift|doorssweep) echo "content=n4$SHOT.omwscripts" >> "$WORK/config/openmw.cfg" ;; esac
# Task 0125 stage E2: "e2terrain" leaves the Mare by its front door into WhiterunWorld, walks, and hops across cell borders
# (use at least 4000 frames).
# Task 0134 (M-S3 stage 1): "navpath" runs path queries over the navmesh of the Mare and of Whiterun's street, with the
# navmesh debug view (use at least 6000 frames; set [Navigator] settings through SETTINGS_EXTRA).
[ "$SHOT" = navpath ] && echo "content=navpath.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = e2terrain ] && echo "content=n4e2.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0129: "e2water" visits a lake and a river of WhiterunWorld and takes pictures (use at least 4000 frames).
[ "$SHOT" = e2water ] && echo "content=n4e2water.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0127 stage W-S1: "w1" leaves the Mare into WhiterunWorld, looks at the sky at several hours, then speeds the clock up and
# takes a picture at each weather change (use at least 4000 frames; set [Game] neutral weather = true through SETTINGS_EXTRA).
[ "$SHOT" = w1 ] && echo "content=w1.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0129 swimming follow-up: "e2swim" swims in the Whiterun lake and walks out (use at least 6000 frames).
[ "$SHOT" = e2swim ] && echo "content=n4e2swim.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0127 stage W-S2: "w2" leaves the Mare into WhiterunWorld and takes pictures on a fast clock; force a weather with
# OPENEDEN_NEUTRAL_FORCE_WEATHER=<editor id> (use at least 3500 frames and CHECK_TIMEOUT=240).
[ "$SHOT" = w2 ] && echo "content=w2.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0127 stage W-S3: "w3" leaves the Mare and aims the camera at the sun's direction for several hours (use at least 4000 frames).
[ "$SHOT" = w3 ] && echo "content=w3.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0133 (milestone M-S2): "lodbake" runs `openmw --bake-lod-terrain` in the session (the world is OPENEDEN_LOD_BAKE_WORLD,
# default Tamriel); the pack lands in <work>/user/openeden/lod/skyrimse/<world>/terrain.edenpak. "lodview" is the e2 walk
# with the world LOD on (put the [WorldLod] settings in SETTINGS_EXTRA and keep the baked user folder with KEEP_USER=1).
BAKE=""
[ "$SHOT" = lodbake ] && BAKE="--bake-lod-terrain"
[ "$SHOT" = lodview ] && echo "content=n4lodview.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0136: "trees" walks out of the Mare, stands before the nearest Skyrim tree and takes four pictures half a second apart
# (use at least 2500 frames; set [Game] neutral tree sway = true through SETTINGS_EXTRA; OPENEDEN_NEUTRAL_TREE_WIND forces the wind).
[ "$SHOT" = trees ] && echo "content=trees.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0133 stage 2: "lodobjbake" bakes the far object clusters (--bake-lod-objects) of the same world; "lodobjview" is "lodview" for them.
[ "$SHOT" = lodobjbake ] && BAKE="--bake-lod-objects"
case "$SHOT" in lodview|lodobjview) echo "content=n4lodview.omwscripts" >> "$WORK/config/openmw.cfg" ;; esac
# Task 0136 (skinned trunks): the same walk to a grove of the 35 TREE records whose model has a BSTreeNode (trunk and branches are skinned shapes).
[ "$SHOT" = trunks ] && echo "content=trunks.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = poslog ] && echo "content=poslog.omwscripts" >> "$WORK/config/openmw.cfg"
printf '[Game]\nneutral world = true\n' > "$WORK/config/settings.cfg"
# Optional extra settings text (for example "[Rendering]\nframe counters = true\n"), appended as given.
[ -n "${SETTINGS_EXTRA:-}" ] && printf '%b' "$SETTINGS_EXTRA" >> "$WORK/config/settings.cfg"
cd "$BUILD"
set +e
PROBE_CELL="$CELL"
# Task 0142: a start text beginning with @ is an exterior spot (--start @<world>,<x>,<y>[,<z>]); the probe cell is then a fixed interior.
case "$CELL" in @*) PROBE_CELL=WhiterunBanneredMare ;; esac
OPENEDEN_SKYRIMSE_PROBE_CELL="$PROBE_CELL" timeout "${CHECK_TIMEOUT:-170}" ./openmw.exe --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --no-grab=true --random-seed 7 --start "$CELL" \
    ${SHOT:+$(case "$SHOT" in look|ms0sweep|collision|colldraw|doors*|n5actors|n5idle|e2terrain|e2water|e2swim|w1|stairs|keylocked|keycontainer|w2|lodbake|lodview|navpath|n5body|n5bodydraw|trees|lodobjbake|lodobjview|w3|trunks|n5talk|n5lip|poslog|n5walk|n5goto) ;; *) echo --auto-screenshot "$SHOT" ;; esac)} ${SCRIPT_RUN:+--script-run "$(native "$SCRIPT_RUN")"} --auto-quit "$FRAMES" > "$WORK/log.txt" 2>&1
code=$?
set -e
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
grep -E "Session content|world view built|world source registered|probe cell|Starting a new game|Neutral start|Neutral cell|N3A3|Skyrim models|Skyrim collision|N4B|N4S|NDOOR|N4D|MS0|NLOCK|Neutral door|E2 |E2W|NWATER|Neutral terrain|Neutral exterior|W1 |W2 |Neutral weather|LV |LOD terrain bake|World LOD|NAV |Navmesh|navmesh|N5B|NACT|TREES |Skyrim tree sway|LOD object bake|LOD clusters|TRUNKS |POSLOG |NTALK|N5T|NLIP|Skyrim lip sync " "$WORK/log.txt" | sed 's/^\[[^]]*\] //'
# The one expected error line is the refusal to save (a neutral session has no save support until stage N6).
errors=$(grep -E " E\] " "$WORK/log.txt" | grep -vc "Saving refused" || true)
echo "exit code $code, $dumps crash dumps, $errors unexpected error lines"
grep -q "world view built from the session: 5 of 5 plugins" "$WORK/log.txt" || { echo "FAILED: the pack was not given the five Skyrim plugins"; exit 1; }
grep -q "probe cell found: yes" "$WORK/log.txt" || { echo "FAILED: the probe cell was not found"; exit 1; }
grep -q "Starting a new game (neutral world)" "$WORK/log.txt" || { echo "FAILED: the neutral start path did not run"; exit 1; }
case "$CELL" in
    @*) grep -q "Neutral start: the player stands at" "$WORK/log.txt" || { echo "FAILED: the exterior start did not run"; exit 1; } ;;
    *) grep -q "Neutral cell '$CELL' inserted into the scene: [1-9]" "$WORK/log.txt" || { echo "FAILED: no objects were inserted"; exit 1; } ;;
esac
grep -q "Saving refused" "$WORK/log.txt" || { echo "FAILED: the save attempt was not refused"; exit 1; }
[ "$code" = 0 ] && [ "$dumps" = 0 ] && [ "$errors" = 0 ] || { echo FAILED; exit 1; }
echo OK
