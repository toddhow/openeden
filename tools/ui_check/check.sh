#!/bin/bash
# Task 0162: runs the new UI system's demo in a quiet-window game and collects what it costs.
#
# usage: check.sh <name> <variant> [--quit FRAME] [--shot FRAME] [--res WxH]
#   variant  cpp  : [GUI] rmlui on with the C++ debug panel (components/ui_rml/debugpanel.cpp)
#            lua  : [GUI] rmlui on, the C++ panel off, the debug panel as `openeden.ui` documents (tools/ui_check/mod)
#            none : [GUI] rmlui on, C++ panel off, no script (the overlay's own cost with nothing in it)
#            off  : [GUI] rmlui off (the game as before)
#   needs: BUILD (the build folder with openmw.exe), WORK (a folder under I:/Projects/OpenEden-work),
#          MORROWIND_DATA (default "I:\GOG Galaxy\Games\Morrowind\Data Files"), PROFILE_CFG (an openmw.cfg to take the
#          content lines from; default the main test profile of the work folder)
# Writes $WORK/runs/<name>/{log.txt,frames.txt,user/screenshots/*.png}; prints the [UI] and [uipanel] lines of the log
# and the median and 95th percentile of the frame time column of frames.txt.
set -e
NAME=$1; VARIANT=$2; shift 2
QUIT=1500; SHOT=1200; RES=1280x720
while [ $# -gt 0 ]; do
    case "$1" in
        --quit) QUIT=$2; shift 2 ;;
        --shot) SHOT=$2; shift 2 ;;
        --res) RES=$2; shift 2 ;;
        *) echo "unknown option $1"; exit 2 ;;
    esac
done
: "${BUILD:?set BUILD to the build folder}"
: "${WORK:?set WORK to a work folder}"
case "$WORK" in /i/Projects/OpenEden-work/*|I:/Projects/OpenEden-work/*) ;; *) echo "WORK must be under I:/Projects/OpenEden-work"; exit 2 ;; esac
HERE="$(cd "$(dirname "$0")" && pwd)"
MW=${MORROWIND_DATA:-"I:\GOG Galaxy\Games\Morrowind\Data Files"}
PROFILE_CFG=${PROFILE_CFG:-/i/Projects/OpenEden-work/testprofile/main/config/openmw.cfg}
RML=true; PANEL=true; MOD=0
case "$VARIANT" in
    cpp) ;;
    lua) PANEL=false; MOD=1 ;;
    none) PANEL=false ;;
    off) RML=false ;;
    *) echo "unknown variant $VARIANT"; exit 2 ;;
esac
W=$WORK/runs/$NAME
rm -rf "$W"; mkdir -p "$W/config" "$W/user"
grep -v "^data=\|^data-local=\|^resources=\|^extension=\|^content=.*omwscripts" "$PROFILE_CFG" > "$W/config/openmw.cfg"
{ echo "data=\"$(cygpath -m "$BUILD")/resources/vfs-mw\""; echo "data=\"$MW\""; echo "resources=$(cygpath -m "$BUILD")/resources"
  if [ "$MOD" = 1 ]; then echo "data=\"$(cygpath -m "$HERE/mod")\""; echo "content=uipanel.omwscripts"; fi; } >> "$W/config/openmw.cfg"
printf '[Saves]\ncharacter = Refn\n\n[Video]\nantialiasing = 0\nresolution x = %s\nresolution y = %s\nvsync mode = 0\n\n[GUI]\nrmlui = %s\nrmlui debug panel = %s\n' \
    "${RES%x*}" "${RES#*x}" "$RML" "$PANEL" > "$W/config/settings.cfg"
VCPKG_BIN=${VCPKG_BIN:-/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows}
export PATH="/usr/bin:$VCPKG_BIN/bin:$VCPKG_BIN/bin/Release:$BUILD:/c/Windows/System32:/c/Windows:$PATH"
export OPENEDEN_QUIET_WINDOW=1
export OPENMW_DISABLE_CRASH_CATCHER=${OPENMW_DISABLE_CRASH_CATCHER:-1}
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
export OPENEDEN_FRAME_STATS_FILE="$(cygpath -m "$W/frames.txt")"
export OPENEDEN_SHADER_CACHE="$(cygpath -m "$WORK/shadercache")"
cd "$BUILD"
timeout 420 "${OPENMW_EXE:-./openmw.exe}" --replace=config --config "$(cygpath -m "$W/config")" --user-data "$(cygpath -m "$W/user")" \
    --skip-menu --new-game --no-grab --no-sound --random-seed 1 --auto-screenshot "$SHOT" --auto-quit "$QUIT" > "$W/log.txt" 2>&1 < /dev/null || echo "exit $?"
ls "$W/user/screenshots" 2>/dev/null || true
grep "\[UI\]\|\[uipanel\]" "$W/log.txt" | grep -v "bitmap font" || true
ls "$W"/user/*.dmp 2>/dev/null && echo "CRASH DUMP" || true
