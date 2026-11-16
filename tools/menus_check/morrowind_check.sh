#!/bin/bash
# tools/menus_check (task 0207): Morrowind is unchanged. Starts Balmora (new game, --random-seed 7, fixed frame time) with
# the new executable and a reference one, takes the fixed frame (--auto-screenshot 600) and prints the hashes; the new
# executable is also run with [GUI] rmlui on (the menus script is refused without the pack's namespace).
#   morrowind_check.sh <build dir> <reference build dir> <base config dir> <work dir>
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; REF=$2; BASE=$3; WORK=$4
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in I:/Projects/OpenEden-work/*) ;; *) echo "refused: $WORK"; exit 2 ;; esac
rm -rf "$WORK"; mkdir -p "$WORK"
run() { # name, build dir, extra settings
    local name=$1 build=$2 dir="$WORK/$1"
    mkdir -p "$dir/config" "$dir/user"
    grep -vE '^(resources|extension)=|^data=.*(vfs-mw|script_commands_check|crime_check|worldobjects_check)|^content=.*omwscripts' "$BASE/openmw.cfg" > "$dir/config/openmw.cfg"
    printf 'resources=%s\ndata="%s"\nextension=compat_morrowind\n' "$(native "$build/resources")" "$(native "$build/resources/vfs-mw")" >> "$dir/config/openmw.cfg"
    printf '[Sound]\nmaster volume = 0.0\n[Video]\nresolution x = 1280\nresolution y = 720\nvsync mode = 0\n%b' "$3" > "$dir/config/settings.cfg"
    export OPENEDEN_FIXED_FRAME_TIME=0.0166667 OPENMW_DISABLE_CRASH_CATCHER=1
    cd "$build"
    set +e
    timeout 200 ./openmw.exe --replace=config --config "$(native "$dir/config")" --user-data "$(native "$dir/user")" \
        --skip-menu --start Balmora --no-grab=true --random-seed 7 --auto-screenshot 600 --auto-quit 650 > "$dir/log.txt" 2>&1
    echo "$name exit $?, $(ls "$dir"/user/screenshots | wc -l) screenshots"
    set -e
    for f in "$dir"/user/screenshots/*.png; do echo "$name $(basename "$f") $(sha256sum "$f" | cut -c1-16)"; done
}
run new "$BUILD" ""
run reference "$REF" ""
run newrml "$BUILD" "[GUI]\nrmlui = true\nrmlui debug panel = false\n"
