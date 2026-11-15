#!/bin/bash
# tools/neutral_session_check/mwsaveload.sh (task 0177): Morrowind save compatibility between two executables.
# <writer build> starts a new Morrowind game in Balmora and saves it through the menu script; <reader build> loads that
# save and logs the player's cell, position and a digest of the cell. Run it twice with the builds swapped to show
# both directions. Prints the N6MW lines of both runs and whether they are equal (the position may differ by the
# distance the player settles; the digest and counts must match).
#
#   mwsaveload.sh <writer build> <reader build> <base config dir> <work dir>
#
# <base config dir> is a COPY of a Morrowind openmw.cfg; <work dir> under I:/Projects/OpenEden-work.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
WRITER=$1; READER=$2; BASE=$3; WORK=$4
HERE=$(cd "$(dirname "$0")" && pwd)
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
cp -r "$HERE/luatest/." "$WORK/luatest/"
phase() { echo "return '$1'" > "$WORK/luatest/scripts/n6/phase.lua"; }
cfg() { # build dir
    grep -vE '^(resources|extension|user-data|config)=|^data=.*(vfs-mw|script_commands_check|crime_check|worldobjects_check)|^content=.*omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
    printf 'resources=%s\ndata="%s"\nextension=compat_morrowind\ndata="%s"\ncontent=n6mw.omwscripts\n' \
        "$(native "$1/resources")" "$(native "$1/resources/vfs-mw")" "$(native "$WORK/luatest")" >> "$WORK/config/openmw.cfg"
    : > "$WORK/config/settings.cfg"
}
run() { # build dir, log, arguments
    local build=$1 log=$2; shift 2
    cfg "$build"
    cd "$build"
    set +e
    timeout 200 ./openmw.exe --replace=config --config "$(native "$WORK/config")" --user-data "$(native "$WORK/user")" \
        --no-grab=true --random-seed 7 --skip-menu "$@" --auto-quit 4000 > "$WORK/$log" 2>&1
    echo "exit $?" >> "$WORK/$log"
    set -e
}
phase save
run "$WRITER" writer.txt --start Balmora
SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
[ -n "$SAVE" ] || { echo "no save written"; grep -E " E\] " "$WORK/writer.txt" | head -3; exit 1; }
echo "save $(stat -c %s "$SAVE") bytes written by $(basename "$WRITER")"
phase load
run "$READER" reader.txt --load-savegame "$(native "$SAVE")"
grep -h "N6MW" "$WORK/writer.txt" "$WORK/reader.txt" | sed 's/^\[[^]]*\] //'
grep -E "Failed to load|Ignoring unknown record|exit " "$WORK/reader.txt" | head -5
w=$(grep -h "N6MW" "$WORK/writer.txt" | sed 's/^\[[^]]*\] //; s/pos=[^ ]* //; s/digest=[0-9]* //')
r=$(grep -h "N6MW" "$WORK/reader.txt" | sed 's/^\[[^]]*\] //; s/pos=[^ ]* //; s/digest=[0-9]* //')
[ -n "$w" ] && [ "$w" = "$r" ] && echo "MW-ROUNDTRIP-EQUAL" || echo "MW-ROUNDTRIP-DIFFERENT"
