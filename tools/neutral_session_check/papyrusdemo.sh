#!/bin/bash
# tools/neutral_session_check/papyrusdemo.sh (task 0181, Papyrus slice 2): the in-game check of scripts on base forms, of
# a script's Disable showing on screen, and of the Papyrus save chunk.
#
# A generated plugin (oedemo.esp: one activator whose BASE record carries the script OEDemoTick, and two placements of it
# in the Bannered Mare, one of which overrides the script's properties in its own VMAD) and a generated script
# (scripts/oedemotick.pex, written by the gated test SkyrimPapyrusDemo) count updates every 2 script seconds, print
# "OEDEMO A n=<n>" and disable their reference when n reaches Limit (A: 3, B: 40 by default).
#
#   papyrusdemo.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> <phase> [frames] [saveAt] [papyrus]
#
# phase "ref":  one long run with no save.   "save": the same, saving once at simulation time <saveAt> (default 120).
# phase "load": a new process that loads the save of the "save" run of the same work dir.
# <papyrus> "off" runs the same with [extension.skyrimse] papyrus = false (default true).
# The logs are <work>/<phase>.txt; the OEDEMO, N7DEMO and PAPYRUS lines are in <work>/<phase>.lines.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; PHASE=${5:-ref}; FRAMES=${6:-12000}; SAVEAT=${7:-120}; PAPYRUS=${8:-true}
[ "$PAPYRUS" = off ] && PAPYRUS=false
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
B=$(native "$BUILD")
DEMO=$(native "${DEMO_DIR:-$WORK/../demo}")
if [ "$PHASE" != load ]; then
    rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
    cp -r "$HERE/luatest/." "$WORK/luatest/"
    grep -vE '^(content|data-local|resources|extension|user-data|config)=|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
    cat >> "$WORK/config/openmw.cfg" <<CFG
resources=$B/resources
data="$B/resources/vfs-mw"
data="$(native "$SKYRIM")"
data="$(native "$WORK/luatest")"
data="$DEMO"
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
content=${DEMO_ESP-oedemo.esp}
content=n7papyrus.omwscripts
CFG
    printf '[Game]\nneutral world = true\nneutral saves = true\n[extension.skyrimse]\npapyrus = %s\n' "$PAPYRUS" > "$WORK/config/settings.cfg"
    printf '%b' "${SETTINGS_EXTRA:-}" >> "$WORK/config/settings.cfg"
    printf 'return { saveAt = %s, shotEvery = %s }\n' "$SAVEAT" "${SHOT_EVERY:-10}" > "$WORK/luatest/scripts/n7/cfg.lua"
fi
run() { # phase, extra arguments
    local phase=$1; shift
    echo "return '$phase'" > "$WORK/luatest/scripts/n6/phase.lua"
    local log="$WORK/$phase.txt"
    cd "$BUILD"
    set +e
    timeout "${CHECK_TIMEOUT:-600}" ./openmw.exe --replace=config \
        --config "$(native "$WORK/config")" --user-data "$(native "$WORK/user")" --no-grab=true --random-seed 7 \
        "$@" --auto-quit "$FRAMES" ${SHOTARGS:-} > "$log" 2>&1
    echo "exit $?" >> "$log"
    set -e
    grep -E "N7DEMO|N7SHOT|N7 save|PAPYRUS|PAPYRUSLOAD|A game pack|Papyrus|Neutral world restored|Reading save|is saved in|Failed|refused| E\] " "$log" \
        | sed 's/^\[[^]]*\] //; s/^\[skyrimse\] //' > "$WORK/$phase.lines" || true
    echo "$phase: $(grep -c OEDEMO "$log") OEDEMO lines, $(grep -c 'N7SHOT' "$log") pictures, $(grep -c ' E\] ' "$log") error lines, $(ls "$WORK"/user/*.dmp 2>/dev/null | wc -l) crash dumps"
}
case "$PHASE" in
    ref|save) run "$PHASE" --skip-menu --start "${START:-WhiterunBanneredMare}" ;;
    load)
        SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
        [ -n "$SAVE" ] || { echo "no save file in $WORK"; exit 1; }
        run load --skip-menu --load-savegame "$(native "$SAVE")" ;;
    *) echo "phase must be ref, save or load"; exit 2 ;;
esac
