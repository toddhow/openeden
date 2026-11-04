#!/bin/bash
# Test games open off-screen, out of the taskbar and without taking focus (apps/openmw/vsgengine.cpp); run with OPENEDEN_QUIET_WINDOW=0 to see the window.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# Checks that every shadow caster can be switched on without crashing the game (task 0052): `object shadows = true`
# once crashed it in vsg::TransferTask, because a shadow cascade's view kept a default vsg::ViewDependentState (see
# apps/openmw/mwrender/CLAUDE.md). A unit test covers the view itself (apps/components_tests/view/testshadowview.cpp);
# this runs the real game, needs a GPU and Morrowind's data, and is the repeatable in-game check.
#
# Usage: tools/shadow_check/check.sh <build dir> <openmw.cfg with your data= and content= lines> <empty work dir>
# On Windows, PATH must already find the game's runtime libraries. Never pass your real config folder: the cfg file
# is copied, and the game runs with --replace=config in the work folder.
#
# Runs, each in Seyda Neen with all four casters on, a screenshot taken and the game quitting by itself:
#   1. hdr off, no antialiasing   2. hdr on, 4x antialiasing   3. hdr off, 4x antialiasing
# A run passes when the game exits normally, writes no openmw-crash.dmp and saves a screenshot.

set -u
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
CFG=$2
WORK=$3
FAILED=0

[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "FAIL: $WORK is not empty"; exit 1; }
WORKW=$(cd "$WORK" && { pwd -W 2>/dev/null || pwd; })

SHADOWS='[Shadows]
enable shadows = true
object shadows = true
terrain shadows = true
actor shadows = true
player shadows = true'

run() { # name, extra settings
    local dir="$WORK/$1"
    mkdir -p "$dir/config" "$dir/userdata/screenshots"
    tr -d '\r' < "$CFG" > "$dir/config/openmw.cfg"
    printf '[Video]\nresolution x = 1280\nresolution y = 720\nfullscreen = false\nwindow mode = 2\nvsync = false\n%b\n%s\n' "$2" "$SHADOWS" > "$dir/config/settings.cfg"
    printf 'ChangeWeather "Bitter Coast Region" 0\nset gamehour to 17.5\nset timescale to 0\nplayer->position -11050 -71300 60 0\n' > "$dir/start.txt"
    (cd "$BUILD" && timeout 240 ./openmw.exe --replace=config --config "$WORKW/$1/config" --user-data "$WORKW/$1/userdata" \
        --skip-menu --start "Seyda Neen" --script-run "$WORKW/$1/start.txt" --no-grab --auto-screenshot 100 --auto-quit 130 \
        > "$dir/game.log" 2>&1)
    local rc=$?
    if [ $rc -ne 0 ] || ls "$dir"/config/*.dmp >/dev/null 2>&1 || [ -z "$(ls -A "$dir/userdata/screenshots" 2>/dev/null)" ]; then
        echo "FAIL: $1 (exit $rc, see $dir/game.log and any .dmp in $dir/config)"
        FAILED=1
    else
        echo "ok: $1"
    fi
}

run hdr_off_aa_0 '[Rendering]\nhdr = false\n[Video]\nantialiasing = 0'
run hdr_on_aa_4 '[Rendering]\nhdr = true\n[Video]\nantialiasing = 4'
run hdr_off_aa_4 '[Rendering]\nhdr = false\n[Video]\nantialiasing = 4'
exit $FAILED
