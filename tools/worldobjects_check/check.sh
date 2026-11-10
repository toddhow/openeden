#!/bin/bash
# tools/worldobjects_check (task 0098 move 8, stage 8a): reads what the world-object classes (door, container, activator,
# static) answer for every such object in the player's cell and neighbours in a real game (Balmora, new game), through
# the public Lua API, and compares the lines between two executables started with the same --random-seed: the one under
# test (the classes in the Morrowind game pack) and a reference built before the move. Read only.
#
#   check.sh <build dir> <reference build dir> <base config dir> <work dir>
#
# <base config dir> holds the openmw.cfg of a Morrowind setup: a COPY, never the real one; its resources= and
# extension lines are replaced by each executable's own. <work dir> must be under I:/Projects/OpenEden-work. The test game
# opens off-screen (OPENEDEN_QUIET_WINDOW). The script fails on a non-zero exit code, a crash dump or different results.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; REF=$2; BASE=$3; WORK=$4
HERE="$(cd "$(dirname "$0")" && pwd)"
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
case "$(native "$BASE")" in
    *"My Games"*) echo "refused: $BASE looks like the real game folder; pass a copy"; exit 2 ;;
esac
rm -rf "$WORK"; mkdir -p "$WORK"
run() { # name, build dir
    local name=$1 build=$2 dir="$WORK/$1"
    mkdir -p "$dir/config" "$dir/user" "$dir/frames"
    grep -vE '^(resources|extension)=|^data=.*(vfs-mw|script_commands_check|crime_check|worldobjects_check)|^content=.*omwscripts' "$BASE/openmw.cfg" > "$dir/config/openmw.cfg"
    printf 'resources=%s\ndata="%s"\nextension=compat_morrowind\ndata="%s"\ncontent=worldobjectscheck.omwscripts\n' \
        "$(native "$build/resources")" "$(native "$build/resources/vfs-mw")" "$(native "$HERE/mod")" >> "$dir/config/openmw.cfg"
    cd "$build"
    set +e
    timeout "${TIMEOUT:-200}" ./openmw.exe --replace=config --config "$(native "$dir/config")" --user-data "$(native "$dir/user")" \
        --skip-menu --start Balmora --no-grab=true --random-seed 7 --auto-quit 3000 > "$dir/log.txt" 2>&1
    local code=$?
    set -e
    cp "$dir"/user/screenshots/* "$dir/frames"/ 2>/dev/null || true
    local dumps; dumps=$(ls "$dir"/user/*.dmp "$dir"/config/*.dmp 2>/dev/null | wc -l)
    grep -h "WOCHECK" "$dir/log.txt" | sed 's/^.*WOCHECK/WOCHECK/' > "$dir/result.txt"
    echo "$name: exit code $code, $dumps crash dumps, $(wc -l < "$dir/result.txt") result lines, $(ls "$dir/frames" | wc -l) screenshots"
    if [ "$code" != 0 ] || [ "$dumps" != 0 ] || [ "$(wc -l < "$dir/result.txt")" -lt 20 ]; then
        echo "FAILED ($name)"; exit 1
    fi
}
run new "$BUILD"
run reference "$REF"
tail -3 "$WORK/new/result.txt" | cut -c1-200
if ! cmp -s "$WORK/new/result.txt" "$WORK/reference/result.txt"; then
    echo "FAILED: the results differ"; diff "$WORK/new/result.txt" "$WORK/reference/result.txt" | head -10; exit 1
fi
echo "OK"
