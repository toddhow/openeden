#!/bin/bash
# tools/neutral_session_check/objectsdemo.sh (task 0202): the in-game check of Papyrus MoveTo, Disable(abFadeOut) and
# PlaceAtMe: a generated script (oedemotick.pex, written by the gated test SkyrimPapyrusObjectsDemo) on the two placements of
# the generated plugin oedemo.esp moves one barrel, fades the other out and places a new barrel; the Lua scenario
# (luatest/scripts/n9) logs what the ENGINE holds ("N9OBJ"), takes pictures ("N9SHOT") and casts rays ("N9RAY").
#
#   objectsdemo.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> <phase> [frames] [papyrus]
#
# phase "ref": one long run with no save.   "save": the same, saving once at the simulation time cfg.saveAt (24 s).
# phase "load": a new process that loads the save of the "save" run of the same work dir.
# The logs are <work>/<phase>.txt; the N9OBJ, N9RAY and PAPYRUS lines are in <work>/<phase>.lines.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; PHASE=${5:-ref}; FRAMES=${6:-3000}; PAPYRUS=${7:-true}
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
content=n9objects.omwscripts
CFG
    printf '[Game]\nneutral world = true\nneutral saves = true\n[Sound]\nmaster volume = 0.0\n[extension.skyrimse]\npapyrus = %s\n' "$PAPYRUS" > "$WORK/config/settings.cfg"
    printf '%b' "${SETTINGS_EXTRA:-}" >> "$WORK/config/settings.cfg"
fi
run() { # phase, extra arguments
    local phase=$1; shift
    echo "return '$phase'" > "$WORK/luatest/scripts/n6/phase.lua"
    echo "return ${RESAVE:-false}" > "$WORK/luatest/scripts/n9/resave.lua"
    local log="$WORK/$phase.txt"
    cd "$BUILD"
    set +e
    timeout "${CHECK_TIMEOUT:-600}" ./openmw.exe --replace=config \
        --config "$(native "$WORK/config")" --user-data "$(native "$WORK/user")" --no-grab=true --random-seed 7 \
        "$@" --auto-quit "$FRAMES" ${SHOTARGS:-} > "$log" 2>&1
    echo "exit $?" >> "$log"
    set -e
    grep -E "N9OBJ|N9SHOT|N9RAY|N9 save|PAPYRUS|PAPYRUSLOAD|A game pack|Papyrus|Neutral world restored|Reading save|is saved in|Failed|refused| E\] " "$log" \
        | sed 's/^\[[^]]*\] //; s/^\[skyrimse\] //' > "$WORK/$phase.lines" || true
    echo "$phase: $(grep -c N9OBJ "$log") N9OBJ lines, $(grep -c 'N9SHOT' "$log") pictures, $(grep -c ' E\] ' "$log") error lines, $(ls "$WORK"/user/*.dmp 2>/dev/null | wc -l) crash dumps"
}
case "$PHASE" in
    ref|save) run "$PHASE" --skip-menu --start "${START:-WhiterunBanneredMare}" ;;
    load)
        SAVE=${SAVEFILE:-$(ls "$WORK"/user/saves/*/n9_objects.omwsave 2>/dev/null | head -1)}
        [ -n "$SAVE" ] || { echo "no save file in $WORK"; exit 1; }
        run load --skip-menu --load-savegame "$(native "$SAVE")" ;;
    *) echo "phase must be ref, save or load"; exit 2 ;;
esac
