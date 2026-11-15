#!/bin/bash
# tools/neutral_session_check/saveload.sh (task 0177, neutral world saves, stage N6): one save and load round trip of a
# neutral session. Phase "save": starts the game in a Skyrim cell (interior editor id or @<world>,x,y), changes a few
# objects of the player's cell (luatest/scripts/n6/global.lua: one disabled, two moved, one enabled that the pack starts
# disabled, one deleted), logs every object (N6OBJ) and the player (N6PLAYER), saves through the menu script and quits.
# Phase "load": a new process loads that save (--load-savegame) and logs the same lines. The two logs are compared.
#
#   saveload.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [start] [frames] [reuse]
#
# <work dir> must be under I:/Projects/OpenEden-work. With a seventh argument "load" only the load phase runs, on the
# save of an earlier run of the same work dir (for the repeats of a round trip). Prints aggregates and the differences.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; START=${5:-WhiterunBanneredMare}; FRAMES=${6:-1200}; ONLY=${7:-}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
B=$(native "$BUILD")
if [ "$ONLY" != load ]; then
    rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
    cp -r "$HERE/luatest/." "$WORK/luatest/"
    grep -vE '^(content|data-local|resources|extension|user-data|config)=|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
    cat >> "$WORK/config/openmw.cfg" <<CFG
resources=$B/resources
data="$B/resources/vfs-mw"
data="$(native "$SKYRIM")"
data="$(native "$WORK/luatest")"
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
content=${N6_SCRIPTS:-n6saveload.omwscripts}
CFG
    [ -n "${CFG_EXTRA:-}" ] && printf '%b' "$CFG_EXTRA" >> "$WORK/config/openmw.cfg"
    printf '[Game]\nneutral world = true\nneutral saves = %s\n' "${NEUTRAL_SAVES:-true}" > "$WORK/config/settings.cfg"
    [ -n "${SETTINGS_EXTRA:-}" ] && printf '%b' "$SETTINGS_EXTRA" >> "$WORK/config/settings.cfg"
fi
run() { # phase, log name, extra arguments
    echo "return '$1'" > "$WORK/luatest/scripts/n6/phase.lua"
    local log="$WORK/$2.txt"
    shift 2
    cd "$BUILD"
    set +e
    OPENEDEN_SKYRIMSE_PROBE_CELL=WhiterunBanneredMare timeout "${CHECK_TIMEOUT:-240}" ./openmw.exe --replace=config \
        --config "$(native "$WORK/config")" --user-data "$(native "$WORK/user")" --no-grab=true --random-seed 7 \
        "$@" --auto-quit "$FRAMES" > "$log" 2>&1
    echo "exit $?" >> "$log"
    set -e
}
if [ "$ONLY" != load ]; then
    run save save --skip-menu --start "$START"
fi
SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
dumps=$(ls "$WORK"/user/*.dmp 2>/dev/null | wc -l)
if [ -z "$SAVE" ]; then
    echo "no save file was written"
    grep -E "Saving refused|Failed to save|Writing saved game| E\] " "$WORK/save.txt" | head -5
    exit 1
fi
echo "save: $(stat -c %s "$SAVE") bytes; $(grep -c 'N6OBJ' "$WORK/save.txt") objects logged in the save run; crash dumps $dumps"
LOADN=${LOADN:-load}
run load "$LOADN" --skip-menu --load-savegame "$(native "$SAVE")"
dumps=$(ls "$WORK"/user/*.dmp 2>/dev/null | wc -l)
grep -E "N6CHANGE|N6PLAYER|Neutral world restored|Failed to load|Reading save|Writing saved game|is saved in" "$WORK/save.txt" "$WORK/$LOADN.txt" | sed 's/^[^:]*\///; s/^\[[^]]*\] //' | cut -c1-220
grep "N6OBJ\|N6PLAYER" "$WORK/save.txt" | sed 's/^\[[^]]*\] //' > "$WORK/save.lines"
grep "N6OBJ\|N6PLAYER" "$WORK/$LOADN.txt" | sed 's/^\[[^]]*\] //' > "$WORK/$LOADN.lines"
if diff "$WORK/save.lines" "$WORK/$LOADN.lines" > "$WORK/$LOADN.diff"; then
    echo "ROUNDTRIP-EQUAL $(wc -l < "$WORK/$LOADN.lines") lines"
else
    echo "ROUNDTRIP-DIFFERENT"; head -20 "$WORK/$LOADN.diff"
fi
echo "crash dumps after load: $dumps; $(grep -c ' E\] ' "$WORK/$LOADN.txt") error lines in the load run"
# The cycle phase (N6_CYCLE=1): a process loads the save, moves one more object, saves a second generation and loads it
# again without leaving the process (menu.loadGame). The objects logged before that save and after that load must match.
if [ -n "${N6_CYCLE:-}" ]; then
    run cycle cycle --skip-menu --load-savegame "$(native "$SAVE")"
    grep "N6OBJ\|N6PLAYER" "$WORK/cycle.txt" | sed 's/^\[[^]]*\] //' > "$WORK/cycle.lines"
    n=$(grep -c N6PLAYER "$WORK/cycle.lines")
    first=$(awk '/N6PLAYER/ {print NR; exit}' "$WORK/cycle.lines")
    head -n "$first" "$WORK/cycle.lines" > "$WORK/cycle.a"
    tail -n +"$((first + 1))" "$WORK/cycle.lines" > "$WORK/cycle.b"
    grep -E "N6CHANGE|N6 |Neutral save|Neutral world restored" "$WORK/cycle.txt" | sed 's/^\[[^]]*\] //' | cut -c1-200
    if [ "$n" = 2 ] && diff "$WORK/cycle.a" "$WORK/cycle.b" > "$WORK/cycle.diff"; then
        echo "CYCLE-EQUAL $(wc -l < "$WORK/cycle.a") lines before the second save and after the in-process load"
    else
        echo "CYCLE-DIFFERENT (reports: $n)"; head -10 "$WORK/cycle.diff"
    fi
    echo "second generation changed against the first: $(diff "$WORK/save.lines" "$WORK/cycle.a" | grep -c '^>') lines"
    echo "error lines in the cycle run: $(grep -c ' E\] ' "$WORK/cycle.txt")"
fi
