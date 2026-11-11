#!/bin/bash
# Test games open off-screen (apps/openmw/vsgengine.cpp); OPENEDEN_QUIET_WINDOW=0 shows the window.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# Measures foot slip while inertial locomotion drives the player (task 0108): one scripted scenario (mod/scripts/
# footslip.lua: start, stop, walk to run, arcs, strafe, reverse, sharp turn) per variant, with a fixed frame time and
# seed; slip.py reads the game's CMTRACE lines (the ankles' world positions are CMFEET_L/R) and prints, per phase, the
# horizontal speed of a foot while it is planted (low): zero for a foot that holds still on the ground.
#
# Usage: tools/footslip_check/check.sh <build dir> <base openmw.cfg> <empty work dir> [variant ...]
#   SHOTS=1 takes screenshots (user/screenshotNNN.png) of the stops and the walk with a static side camera
#   variants: vanilla, inertial (inertial locomotion = high), profilelock (profile + foot lock), profilefull (profile + foot lock + inertial stride sync), inertialsync (inertial + stride sync), profile (toddhow's test profile: inertial, npcs, foot
#             planting, look at, smoother turns, lean, collision 4, creativemotion preset natural + animation)
# Environment: EXTRA_SETTINGS (settings.cfg text appended), FRAMES (default 1900), FIXED (frame time, default 0.0166667)
# On Windows pass Windows paths (I:/...).
set -u
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
BASECFG=$2
mkdir -p "$3"
WORK=$(cd "$3" && { pwd -W 2>/dev/null || pwd; })
shift 3
HERE=$(cd "$(dirname "$0")" && pwd)
MOD=$(cd "$HERE/mod" && { pwd -W 2>/dev/null || pwd; })
CELL=${CELL:-Seyda Neen}
FRAMES=${FRAMES:-1900}
FIXED=${FIXED:-0.0166667}
VARIANTS=("$@")
[ ${#VARIANTS[@]} -gt 0 ] || VARIANTS=(vanilla inertial)
[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "FAIL: $WORK is not empty"; exit 1; }
[ -f "$BASECFG" ] || { echo "FAIL: no config at $BASECFG"; exit 1; }
GAME="./openmw.exe"
[ -x "$BUILD/openmw.exe" ] || GAME="./openmw"

variant_settings() {
    case "$1" in
        vanilla) printf '' ;;
        inertial) printf 'inertial locomotion = high\n' ;;
        inertialsync) printf 'inertial locomotion = high\ninertial stride sync = true\n' ;;
        profile) printf 'actor collision shape type = 4\nfoot planting = true\nlook at = true\nsmoother turns = true\ninertial locomotion = high\ninertial locomotion npcs = true\nprocedural lean = true\ncreativemotion preset = natural\ncreativemotion animation = true\ncreativemotion animation transition = 0.35\n' ;;
        profilelock) printf 'foot lock = true\n'; variant_settings profile ;;
        profilefull) printf 'foot lock = true\ninertial stride sync = true\n'; variant_settings profile ;;
        *) return 1 ;;
    esac
}

FAILED=0
for label in "${VARIANTS[@]}"; do
    dir="$WORK/$label"
    mkdir -p "$dir/config" "$dir/user"
    settings=$(variant_settings "$label") || { echo "FAIL: unknown variant $label"; FAILED=1; continue; }
    tr -d '\r' < "$BASECFG" > "$dir/config/openmw.cfg"
    modwin=$MOD
    LIMIT=""
    if [ "${SHOTS:-0}" = 1 ]; then
        # a copy of the scenario with the camera and screenshots switched on
        cp -r "$HERE/mod" "$dir/mod"
        sed -i 's/^local shots = false/local shots = true/' "$dir/mod/scripts/footslip.lua"
        LIMIT='framerate limit = 25' # the screenshot writer must keep up (at most 8 copies wait)
        modwin=$(cd "$dir/mod" && { pwd -W 2>/dev/null || pwd; })
    fi
    printf '\ndata="%s"\ncontent=footslip.omwscripts\n' "$modwin" >> "$dir/config/openmw.cfg"
    printf '[Video]
resolution x = 960
resolution y = 540
fullscreen = false
window mode = 2
vsync = false
%s
[Game]
creativemotion animation debug = true
%s' "$LIMIT" "$settings" > "$dir/config/settings.cfg"
    [ -z "${EXTRA_SETTINGS:-}" ] || printf '\n%s\n' "$EXTRA_SETTINGS" >> "$dir/config/settings.cfg"
    cfgwin=$(cd "$dir/config" && { pwd -W 2>/dev/null || pwd; })
    userwin=$(cd "$dir/user" && { pwd -W 2>/dev/null || pwd; })
    echo "run $label"
    (cd "$BUILD" && OPENEDEN_FIXED_FRAME_TIME="$FIXED" timeout 600 "$GAME" --replace=config --config "$cfgwin" --user-data "$userwin" \
        --skip-menu --start "$CELL" --no-grab --random-seed 1 --auto-quit "$FRAMES" > "$dir/stdout.log" 2>&1)
    status=$?
    if ls "$dir/config"/*.dmp > /dev/null 2>&1; then echo "FAIL: $label wrote a crash dump"; FAILED=1; continue; fi
    if [ "$status" -ne 0 ]; then echo "FAIL: $label exited with $status"; FAILED=1; continue; fi
    python -I "$HERE/slip.py" "$dir/config/openmw.log" > "$dir/slip.txt" || { echo "FAIL: slip.py on $label"; FAILED=1; continue; }
    cat "$dir/slip.txt"
done
exit $FAILED
