#!/bin/bash
# tools/menus_check (task 0207, plan section 4.24): the Skyrim SE main menu, pause menu and loading screen tips in game.
# MODE=game (default): starts in the Bannered Mare (--skip-menu --start); MODE=menu: starts at the main menu of the
# session (no --skip-menu; the input script runs there with OPENEDEN_RMLUI_INPUT_SCRIPT_IN_MENU). [GUI] rmlui, [Game]
# neutral world, neutral actors, neutral saves and [extension.skyrimse] menus are on; the sound is off.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: MODE (game|menu), EXE (default openmw.exe), INPUT (an input script for OPENEDEN_RMLUI_INPUT_SCRIPT),
#      MENUS (true|false, default true), SETTINGS_EXTRA, WIDTH and HEIGHT (default 1280 and 720), LOADARG, USER_KEEP
#      (a user-data folder to keep: saves written by an earlier run), CHECK_TIMEOUT.
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-3000}
MODE=${MODE:-game}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
if [ -n "${USER_KEEP:-}" ]; then
    mkdir -p "$WORK/config"; rm -rf "$WORK/config"/*
else
    rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user"
fi
B=$(native "$BUILD")
grep -vE '^(content|data-local|resources|extension|user-data|config)=|^fallback=Movies_|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
cat >> "$WORK/config/openmw.cfg" <<CFG
resources=$B/resources
data="$B/resources/vfs-mw"
data="$(native "$SKYRIM")"
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
CFG
printf '[Game]\nneutral world = true\nneutral actors = %s\nneutral saves = %s\n[GUI]\nrmlui = true\nrmlui debug panel = false\n[Sound]\nmaster volume = %s\n[Video]\nresolution x = %s\nresolution y = %s\nvsync mode = 0\n[extension.skyrimse]\nmenus = %s\n' "${NEUTRAL_ACTORS:-true}" "${NEUTRAL_SAVES:-true}" "${MASTER_VOLUME:-0.0}" "${WIDTH:-1280}" "${HEIGHT:-720}" "${MENUS:-true}" > "$WORK/config/settings.cfg"
[ -n "${SETTINGS_EXTRA:-}" ] && printf '%b' "$SETTINGS_EXTRA" >> "$WORK/config/settings.cfg"
if [ -n "${INPUT:-}" ]; then
    export OPENEDEN_RMLUI_INPUT_SCRIPT="$(native "$INPUT")"
    if [ "$MODE" = menu ] || [ -n "${SCRIPT_IN_MENU:-}" ]; then export OPENEDEN_RMLUI_INPUT_SCRIPT_IN_MENU=1; fi
fi
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
export OPENEDEN_SHADER_CACHE="$(native "$WORK/../shadercache")"
export OPENMW_DISABLE_CRASH_CATCHER=${OPENMW_DISABLE_CRASH_CATCHER:-1}
USERDIR="${USER_KEEP:-$WORK/user}"
cd "$BUILD"
if [ "$MODE" = menu ]; then
    ARGS="${LOADARG:-} ${EXTRA_ARGS:-}"
else
    ARGS="--skip-menu --no-grab=true ${LOADARG:---start WhiterunBanneredMare}"
fi
set +e
timeout "${CHECK_TIMEOUT:-300}" ./${EXE:-openmw.exe} --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$USERDIR")" --random-seed 7 $ARGS \
    --auto-quit "$FRAMES" > "$WORK/log.txt" 2>&1
code=$?
set -e
dumps=$(ls "$USERDIR"/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
grep -E "MENUS|menus|\[UI\]|requires|Saving|saved|Loading save|Auto-" "$WORK/log.txt" | sed 's/^\[[^]]*\] //' | head -${LOGMAX:-30} || true
errors=$(grep -E " E\] " "$WORK/log.txt" | wc -l || true)
echo "exit code $code, $dumps crash dumps, $errors error lines, $(ls "$USERDIR"/screenshots 2>/dev/null | wc -l) screenshots"
