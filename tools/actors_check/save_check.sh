#!/bin/bash
# tools/actors_check/save_check.sh (task 0098 move 8, stage 8c): saved games across the move of the actor classes.
#
# NPC and creature state is written by the classes' own save hooks (writeAdditionalState / readAdditionalState), so a
# saved game written before the move must load after it, and one written after must load before. The check:
#   1. <ref> starts a new game in Balmora, a global Lua script changes the state of the actors in the cell (health,
#      fatigue, level, skills, inventory, a spell) and prints it; the menu script saves the game as "a".
#   2. <build> and <ref> each load that save and print the same lines (and save it again as "b").
#   3. the same with the roles swapped: <build> makes the save, <ref> and <build> load it.
# Every actor line printed after a load must equal one printed when the save was made (an actor that has walked out of
# the cell by then may be missing). The two re-saves "b" of step 2 are also
# compared byte for byte, and the number of differing bytes is reported (they are not expected to be identical: the game
# time and the actors' timers move between the load and the save).
#
#   save_check.sh <build dir> <reference build dir> <base config dir> <work dir>
#
# <base config dir> holds the openmw.cfg of a Morrowind setup: a COPY, never the real one. <work dir> must be under
# I:/Projects/OpenEden-work. The test game opens off-screen (OPENEDEN_QUIET_WINDOW).
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

# run <name> <build dir> <make|load> [<user dir to copy the saves from>]
run() {
    local name=$1 build=$2 mode=$3 from=$4 dir="$WORK/$1"
    mkdir -p "$dir/config" "$dir/user"
    if [ -n "$from" ]; then cp -r "$WORK/$from/user/." "$dir/user/"; rm -f "$dir/user"/*.dmp; fi
    grep -vE '^(resources|extension)=|^data=.*(vfs-mw|script_commands_check|crime_check|worldobjects_check|items_check|actors_check)|^content=.*omwscripts|^fallback=Movies_' "$BASE/openmw.cfg" > "$dir/config/openmw.cfg"
    printf 'resources=%s\ndata="%s"\nextension=compat_morrowind\ndata="%s"\ndata="%s"\ncontent=actorssave.omwscripts\n' \
        "$(native "$build/resources")" "$(native "$build/resources/vfs-mw")" "$(native "$HERE/save_common")" \
        "$(native "$HERE/save_$mode")" >> "$dir/config/openmw.cfg"
    local extra=""
    if [ "$mode" = make ]; then extra="--skip-menu --start Balmora"; fi
    cd "$build"
    set +e
    timeout "${TIMEOUT:-240}" ./openmw.exe --replace=config --config "$(native "$dir/config")" --user-data "$(native "$dir/user")" \
        $extra --no-grab=true --random-seed 7 --auto-quit 60000 > "$dir/log.txt" 2>&1
    local code=$?
    set -e
    local dumps; dumps=$(ls "$dir"/user/*.dmp "$dir"/config/*.dmp 2>/dev/null | wc -l)
    grep -h "ACSAVE" "$dir/log.txt" | sed 's/^.*ACSAVE/ACSAVE/' | grep -v "ACSAVE loading" > "$dir/result.txt" || true
    echo "$name: exit code $code, $dumps crash dumps, $(wc -l < "$dir/result.txt") result lines, saves: $(find "$dir/user" -name '*.omwsave' | wc -l)"
    if [ "$code" != 0 ] || [ "$dumps" != 0 ] || [ "$(wc -l < "$dir/result.txt")" -lt 12 ]; then
        echo "FAILED ($name)"; tail -5 "$dir/log.txt"; exit 1
    fi
}
same() { # a b: every line of b (the load) must be a line of a (the make); an actor that walked out of the cell in the
    # meantime may be missing, so at least 20 actors must match
    local only; only=$(sort "$WORK/$1/result.txt" | comm -13 - <(sort "$WORK/$2/result.txt") | grep -v "ACSAVE count" || true)
    local matching; matching=$(sort "$WORK/$1/result.txt" | comm -12 - <(sort "$WORK/$2/result.txt") | grep -vc "ACSAVE count" || true)
    if [ -z "$only" ] && [ "$matching" -ge 20 ]; then
        echo "same: every actor line of $2 is in $1 ($matching of $(($(wc -l < "$WORK/$1/result.txt") - 1)) actors)"
    else
        echo "FAILED: $2 differs from $1 ($matching lines match)"; echo "$only" | head -5 | cut -c1-300; exit 1
    fi
}

run ref_make "$REF" make
run new_loads_ref_save "$BUILD" load ref_make
run ref_loads_ref_save "$REF" load ref_make
same ref_make new_loads_ref_save
same ref_make ref_loads_ref_save

run new_make "$BUILD" make
run ref_loads_new_save "$REF" load new_make
run new_loads_new_save "$BUILD" load new_make
same new_make ref_loads_new_save
same new_make new_loads_new_save

# the two re-saves of the same file
A=$(find "$WORK/new_loads_ref_save/user" -name 'b.omwsave' | head -1)
B=$(find "$WORK/ref_loads_ref_save/user" -name 'b.omwsave' | head -1)
if [ -n "$A" ] && [ -n "$B" ]; then
    echo "re-saves: $(stat -c %s "$A") and $(stat -c %s "$B") bytes, $(cmp -l "$A" "$B" 2>/dev/null | wc -l) bytes differ"
fi
echo OK
