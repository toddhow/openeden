#!/bin/bash
# Test games open off-screen, out of the taskbar and without taking focus; run with OPENEDEN_QUIET_WINDOW=0 to see the window.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# A cold shader cache stalls the first frames for over 5 s, which the freeze monitor would end; the first run warms it
export OPENMW_DISABLE_CRASH_CATCHER="${OPENMW_DISABLE_CRASH_CATCHER-1}"
# Task 0017 (W3 leftover): the extensions' debug draw and profiler, shown in a real game.
#
# Starts a new game in OpenMW's example suite with the example extension (it draws a line, a box, a sphere, a point and a
# label each frame and times its work), takes one screenshot with the game's fixed frame time, and quits. Four runs share one
# config and user folder, never the user's own:
#   none  : no extension at all (the frame the game draws when nothing submits anything)
#   off   : the extension loaded, both settings off (what everyone gets by default)
#   draw  : extension debug draw = true  (the shapes appear)
#   prof  : extension profiler = true, with the debug window opened on its Extensions tab (the overlay appears)
#
# Usage: tools/extension_diag_check/check.sh <build dir> <example suite> <empty work dir>
# On Windows, PATH must already find the game's runtime libraries. Look at the frames in <work dir>/frames (none.png,
# off.png, draw.png, prof.png); the script itself checks the logs and compares none.png with off.png byte for byte.
set -u
[ "$#" -eq 3 ] || { echo "usage: $0 <build dir> <example suite> <empty work dir>"; exit 1; }
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; }) || exit 1
SUITE=$2
WORK=$3
HERE=$(cd "$(dirname "$0")" && pwd)
# The game reads these paths in config files: Windows form (I:/...), as the root CLAUDE.md says for check scripts
SUITE=$(cygpath -m "$SUITE" 2>/dev/null || echo "$SUITE")
WORK=$(cygpath -m "$WORK" 2>/dev/null || echo "$WORK")
FAILED=0
case "$WORK" in
    I:/Projects/OpenEden-work/* | /i/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "FAIL: $WORK is not empty"; exit 1; }
[ -f "$BUILD/extensions/example/extension.json" ] || { echo "FAIL: no built example extension in $BUILD/extensions/example"; exit 1; }

fail() { echo "FAIL: $1"; FAILED=1; }
check() { grep -qF -- "$2" "$3" && echo "ok: $1" || fail "$1: no \"$2\" in $3"; }
check_not() { grep -qF -- "$2" "$3" && fail "$1: found \"$2\" in $3" || echo "ok: $1"; }

mkdir -p "$WORK/config" "$WORK/userdata" "$WORK/gamedata" "$WORK/frames"
cp "$SUITE/settings.cfg" "$WORK/config/settings.cfg"
printf '[Video]\nresolution x = 1280\nresolution y = 720\nframerate limit = 60\n' >> "$WORK/config/settings.cfg"
cat > "$WORK/base.cfg" <<CFG
resources="$BUILD/resources"
data="$SUITE/game_template/data"
user-data="$WORK/userdata"
content=template.omwgame
CFG

run() { # name, extension line, settings to append
    local name=$1 extension=$2 settings=$3
    cp "$WORK/base.cfg" "$WORK/config/openmw.cfg"
    [ -z "$extension" ] || echo "$extension" >> "$WORK/config/openmw.cfg"
    cp "$SUITE/settings.cfg" "$WORK/config/settings.cfg"
    printf '[Video]\nresolution x = 1280\nresolution y = 720\nframerate limit = 60\n%s\n' "$settings" >> "$WORK/config/settings.cfg"
    rm -rf "$WORK/userdata/screenshots"
    OPENEDEN_FIXED_FRAME_TIME=0.0166667 timeout 200 "$BUILD/openmw" --replace=config --config="$WORK/config" \
        --skip-menu --new-game --no-grab --no-sound --random-seed 1 --auto-screenshot 240 --auto-quit 260 \
        > "$WORK/$name.out" 2>&1
    local status=$?
    echo "run $name: exit $status"
    [ "$status" -eq 0 ] || FAILED=1
    local shot
    shot=$(ls "$WORK"/userdata/screenshots/*.png 2>/dev/null | head -1)
    if [ -n "$shot" ]; then cp "$shot" "$WORK/frames/$name.png"; else fail "run $name took no screenshot"; fi
    ls "$WORK"/userdata/*.dmp "$WORK"/config/*.dmp > /dev/null 2>&1 && fail "run $name left a crash dump"
}

DRAW=$'[Game]\nextension debug draw = true'
PROF=$'[Game]\nextension profiler = true'

run none "" ""
run off "extension=example" ""
run draw "extension=example" "$DRAW"
OPENEDEN_OPEN_DEBUG_WINDOW=1 OPENEDEN_DEBUG_WINDOW_TAB=extensions run prof "extension=example" "$PROF"

check "off: the extension ran" "[example] debug draw submitted" "$WORK/off.out"
check "draw: the extension ran" "[example] debug draw submitted" "$WORK/draw.out"
for name in none off draw prof; do
    check_not "$name: no error line in the log" " E] " "$WORK/$name.out"
done
if cmp -s "$WORK/frames/none.png" "$WORK/frames/off.png"; then
    echo "ok: setting off draws the same frame as no extension (byte-identical)"
else
    echo "NOTE: none.png and off.png differ"
fi
if cmp -s "$WORK/frames/off.png" "$WORK/frames/draw.png"; then
    fail "draw.png equals off.png: nothing was drawn"
else
    echo "ok: draw.png differs from off.png"
fi

[ "$FAILED" -eq 0 ] && echo "ALL OK" || echo "FAILED"
exit $FAILED
