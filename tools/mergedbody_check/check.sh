#!/bin/bash
# Test games open off-screen, out of the taskbar and without taking focus (apps/openmw/vsgengine.cpp); run with OPENEDEN_QUIET_WINDOW=0 to see the window.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# tools/mergedbody_check (task 0069, stage d): the merged body against the stock parts in one scripted scene.
#
#   check.sh <build dir> <base config dir> <user data dir> <work dir> <variant> [cell]
#
# <base config dir> holds the openmw.cfg (and optionally settings.cfg) of the user's game: a COPY, never the real one.
# <user data dir> must hold the cache `openmw --bake-bodies` wrote (<user data>/openeden/bodies); it is reused by every
# variant, and its screenshots folder is emptied, so it must be a COPY too: the script refuses a user data folder
# outside I:/Projects/OpenEden-work and either folder under "My Games". The game quits by itself at the end of the
# scene (mbcheck_player.lua); a run that does not exit with code 0, or that leaves a crash dump, is reported as failed.
# The log line "Merged bodies: <ref> draws the merged body" is counted per variant. <variant> is `stock` ([Bodies] merged body = false) or `merged` (= true). The scene (mod/scripts): one NPC of
# every race and sex in a row without clothes, the player without clothes standing, walking, running, turning and
# attacking, and close-ups of the player's right wrist and knee. Screenshots and the log go to <work dir>/<variant>/.
# Pass Windows paths (cygpath -m) for everything the game reads.
# EXTRA_SETTINGS adds settings.cfg text (for example "[Rendering]\nhdr = true\n..." for the sss, wetness and cover
# frames); START_COMMANDS adds console lines to the start script (for example 'ChangeWeather "West Gash Region" 4').
set -e
BUILD=$1; BASE=$2; USERDATA=$3; WORK=$4; VARIANT=$5; CELL=${6:-Balmora}
HERE="$(cd "$(dirname "$0")" && pwd)"
native() { cygpath -m "$1"; }
case "$VARIANT" in
    stock) MERGED=false ;;
    merged) MERGED=true ;;
    *) echo "variant must be stock or merged"; exit 2 ;;
esac
case "$(native "$USERDATA")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: the user data folder must be a copy under I:/Projects/OpenEden-work"; exit 2 ;;
esac
for folder in "$BASE" "$USERDATA"; do
    case "$(native "$folder")" in
        *"My Games"*) echo "refused: $folder looks like the real game folder; pass copies"; exit 2 ;;
    esac
done
dir="$WORK/$VARIANT"
rm -rf "$dir"; mkdir -p "$dir/config" "$dir/frames"
cp "$BASE/openmw.cfg" "$dir/config/openmw.cfg"
[ -f "$BASE/settings.cfg" ] && cp "$BASE/settings.cfg" "$dir/config/settings.cfg"
printf '\ndata="%s"\ncontent=mbcheck.omwscripts\n' "$(native "$HERE/mod")" >> "$dir/config/openmw.cfg"
printf '\n[Bodies]\nmerged body = %s\n' "$MERGED" >> "$dir/config/settings.cfg"
if [ -n "$EXTRA_SETTINGS" ]; then printf '\n%b\n' "$EXTRA_SETTINGS" >> "$dir/config/settings.cfg"; fi
cp "$HERE/start.txt" "$dir/start.txt"
if [ -n "$START_COMMANDS" ]; then printf '%b\n' "$START_COMMANDS" >> "$dir/start.txt"; fi
rm -f "$USERDATA"/screenshots/* "$USERDATA"/*.dmp "$dir/config"/*.dmp
cd "$BUILD"
set +e
# The timeout is only a safety net: the scene quits by itself after about 30 s.
OPENEDEN_FRAME_STATS_FILE="$(native "$dir/stats.txt")" timeout 120 ./openmw.exe --replace=config \
    --config "$(native "$dir/config")" --user-data "$(native "$USERDATA")" --skip-menu --start "$CELL" --no-grab \
    --script-run "$(native "$dir/start.txt")" > "$dir/log.txt" 2>&1
code=$?
set -e
cp "$USERDATA"/screenshots/* "$dir/frames"/ 2>/dev/null || true
grep -E "MBPHASE|MBSHOT|MBCHECK|Merged bodies" "$dir/log.txt" > "$dir/phases.txt" || true
dumps=$(ls "$USERDATA"/*.dmp "$dir/config"/*.dmp 2>/dev/null | wc -l)
merged=$(grep -c "draws the merged body" "$dir/log.txt" || true)
echo "$VARIANT: exit code $code, $dumps crash dumps, $(ls "$dir/frames" | wc -l) screenshots, \
$(grep -c MBCHECK "$dir/phases.txt") spawn lines, $merged actors drew the merged body"
if [ "$code" != 0 ] || [ "$dumps" != 0 ]; then
    echo "$VARIANT: FAILED (the game did not exit normally)"
    exit 1
fi
