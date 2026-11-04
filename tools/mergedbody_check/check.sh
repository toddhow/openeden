#!/bin/bash
# Test games open off-screen, out of the taskbar and without taking focus (apps/openmw/vsgengine.cpp); run with OPENEDEN_QUIET_WINDOW=0 to see the window.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# tools/mergedbody_check (task 0069, stage d): the merged body against the stock parts in one scripted scene.
#
#   check.sh <build dir> <base config dir> <user data dir> <work dir> <variant> [cell]
#
# <base config dir> holds the openmw.cfg (and optionally settings.cfg) of the user's game: a COPY, never the real one.
# <user data dir> must hold the cache `openmw --bake-bodies` wrote (<user data>/openeden/bodies); it is reused by every
# variant. <variant> is `stock` ([Bodies] merged body = false) or `merged` (= true). The scene (mod/scripts): one NPC of
# every race and sex in a row without clothes, the player without clothes standing, walking, running, turning and
# attacking, and close-ups of the player's right wrist and knee. Screenshots and the log go to <work dir>/<variant>/.
# Pass Windows paths (cygpath -m) for everything the game reads.
set -e
BUILD=$1; BASE=$2; USERDATA=$3; WORK=$4; VARIANT=$5; CELL=${6:-Balmora}
HERE="$(cd "$(dirname "$0")" && pwd)"
native() { cygpath -m "$1"; }
case "$VARIANT" in
    stock) MERGED=false ;;
    merged) MERGED=true ;;
    *) echo "variant must be stock or merged"; exit 2 ;;
esac
dir="$WORK/$VARIANT"
rm -rf "$dir"; mkdir -p "$dir/config" "$dir/frames"
cp "$BASE/openmw.cfg" "$dir/config/openmw.cfg"
[ -f "$BASE/settings.cfg" ] && cp "$BASE/settings.cfg" "$dir/config/settings.cfg"
printf '\ndata="%s"\ncontent=mbcheck.omwscripts\n' "$(native "$HERE/mod")" >> "$dir/config/openmw.cfg"
printf '\n[Bodies]\nmerged body = %s\n' "$MERGED" >> "$dir/config/settings.cfg"
rm -f "$USERDATA"/screenshots/*
cd "$BUILD"
OPENEDEN_FRAME_STATS_FILE="$(native "$dir/stats.txt")" timeout 45 ./openmw.exe --replace=config \
    --config "$(native "$dir/config")" --user-data "$(native "$USERDATA")" --skip-menu --start "$CELL" --no-grab \
    --script-run "$(native "$HERE/start.txt")" --auto-quit 60000 > "$dir/log.txt" 2>&1 || true
cp "$USERDATA"/screenshots/* "$dir/frames"/ 2>/dev/null || true
grep -E "MBPHASE|MBSHOT|MBCHECK|Merged bodies" "$dir/log.txt" > "$dir/phases.txt" || true
echo "$VARIANT: $(ls "$dir/frames" | wc -l) screenshots, $(grep -c MBCHECK "$dir/phases.txt") spawn lines"
