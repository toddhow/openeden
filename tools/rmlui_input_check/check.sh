#!/bin/bash
# Task 0111 (UI system U2): one quiet-window game run with the RmlUi overlay and a synthetic input script
# (OPENEDEN_RMLUI_INPUT_SCRIPT, apps/openmw/uiinputscript.hpp), fixed frame time and seed.
#
# usage: check.sh <name> <script file> [--rmlui 0|1] [--scale F] [--slang 0|1] [--quit FRAME] [--res WxH]
#   needs: BUILD (the build folder with openmw.exe), WORK (a folder under I:/Projects/OpenEden-work),
#          MORROWIND_DATA (default "I:\GOG Galaxy\Games\Morrowind\Data Files"), PROFILE_CFG (an openmw.cfg to take the
#          content lines from; default the main test profile of the work folder)
# Writes $WORK/runs/<name>/{log.txt,user/screenshots/*.png}; prints the [UI] lines of the log.
set -e
NAME=$1; SCRIPT=$(cd "$(dirname "$2")" && pwd)/$(basename "$2"); shift 2
RML=1; SCALE=1; SLANG=0; QUIT=900; RES=1280x720; AUTOSHOT=; FOCUS_BUTTON=; FOCUS_KEY=
while [ $# -gt 0 ]; do
    case "$1" in
        --rmlui) RML=$2; shift 2 ;;
        --scale) SCALE=$2; shift 2 ;;
        --slang) SLANG=$2; shift 2 ;;
        --quit) QUIT=$2; shift 2 ;;
        --res) RES=$2; shift 2 ;;
        --focus-button) FOCUS_BUTTON=$2; shift 2 ;;   # [GUI] rmlui focus button (SDL controller button name)
        --focus-key) FOCUS_KEY=$2; shift 2 ;;         # [GUI] rmlui focus key (SDL key name)
        --auto-shot) AUTOSHOT=$2; shift 2 ;;   # openmw --auto-screenshot N (works with rmlui off, where the script is inactive)
        *) echo "unknown option $1"; exit 2 ;;
    esac
done
: "${BUILD:?set BUILD to the build folder}"
: "${WORK:?set WORK to a work folder}"
case "$WORK" in /i/Projects/OpenEden-work/*|I:/Projects/OpenEden-work/*) ;; *) echo "WORK must be under I:/Projects/OpenEden-work"; exit 2 ;; esac
MW=${MORROWIND_DATA:-"I:\GOG Galaxy\Games\Morrowind\Data Files"}
PROFILE_CFG=${PROFILE_CFG:-/i/Projects/OpenEden-work/testprofile/main/config/openmw.cfg}
W=$WORK/runs/$NAME
rm -rf "$W"; mkdir -p "$W/config" "$W/user"
grep -v "^data=\|^data-local=\|^resources=\|^extension=" "$PROFILE_CFG" > "$W/config/openmw.cfg"
{ echo "data=\"$(cygpath -m "$BUILD")/resources/vfs-mw\""; echo "data=\"$MW\""; echo "resources=$(cygpath -m "$BUILD")/resources"; } >> "$W/config/openmw.cfg"
printf '[Saves]\ncharacter = Refn\n\n[Video]\nantialiasing = 0\nresolution x = %s\nresolution y = %s\nvsync mode = 0\n\n[GUI]\nrmlui = %s\nscaling factor = %s\nrmlui focus button = %s\nrmlui focus key = %s\n\n[Rendering]\nslang shaders = %s\n' \
    "${RES%x*}" "${RES#*x}" "$( [ "$RML" = 1 ] && echo true || echo false )" "$SCALE" "$FOCUS_BUTTON" "$FOCUS_KEY" "$( [ "$SLANG" = 1 ] && echo true || echo false )" > "$W/config/settings.cfg"
VCPKG_BIN=${VCPKG_BIN:-/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows}
export PATH="/usr/bin:$VCPKG_BIN/bin:$VCPKG_BIN/bin/Release:$BUILD:/c/Windows/System32:/c/Windows:$PATH"
export OPENEDEN_QUIET_WINDOW=1
export OPENMW_DISABLE_CRASH_CATCHER=${OPENMW_DISABLE_CRASH_CATCHER:-1}  # the freeze monitor ends a start-up that stalls for 5 s on a loaded machine (exit 173)
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
export OPENEDEN_RMLUI_INPUT_SCRIPT="$(cygpath -m "$SCRIPT")"
export OPENEDEN_SHADER_CACHE="$(cygpath -m "$WORK/shadercache")"  # shared by the runs: a cold cache makes the start-up (and so the frame of the first input) vary
cd "$BUILD"
timeout 420 "${OPENMW_EXE:-./openmw.exe}" --replace=config --config "$(cygpath -m "$W/config")" --user-data "$(cygpath -m "$W/user")" \
    --skip-menu --new-game --no-grab --no-sound --random-seed 1 ${AUTOSHOT:+--auto-screenshot "$AUTOSHOT"} --auto-quit "$QUIT" > "$W/log.txt" 2>&1 < /dev/null || echo "exit $?"
ls "$W/user/screenshots" 2>/dev/null || true
grep "\[UI\]" "$W/log.txt" | grep -v "bitmap font" || true
