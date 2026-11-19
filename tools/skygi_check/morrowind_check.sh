#!/bin/bash
# tools/skygi_check (task 0358): Morrowind is unchanged. Bakes one interior (--bake-gi) and a block of exterior cells
# (--bake-gi-exterior) with two executables and compares every written file byte for byte, then takes the Balmora fixed
# frame (--start Balmora --random-seed 7, OPENEDEN_FIXED_FRAME_TIME, frame 600) with an empty settings.cfg and with hdr = true.
#   morrowind_check.sh <build dir> <reference build dir> <base config dir> <work dir> [interior] [exterior block]
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}" OPENMW_DISABLE_CRASH_CATCHER=1
BUILD=$1; REF=$2; BASE=$3; WORK=$4; INTERIOR=${5:-Seyda Neen, Arrille\'s Tradehouse}; BLOCK=${6:--2,-9:-1,-8}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in I:/Projects/OpenEden-work/*) ;; *) echo "refused: $WORK"; exit 2 ;; esac
rm -rf "$WORK"; mkdir -p "$WORK"
run() { # name build label settings args...
    local name=$1 build=$2 label=$3 settings=$4; shift 4
    local dir="$WORK/$name-$label"
    mkdir -p "$dir/config" "$dir/user"
    grep -vE '^(resources|extension)=|^data=.*(vfs-mw|script_commands_check|crime_check|worldobjects_check)|^content=.*omwscripts' "$BASE/openmw.cfg" > "$dir/config/openmw.cfg"
    printf 'resources=%s\ndata="%s"\n' "$(native "$build/resources")" "$(native "$build/resources/vfs-mw")" >> "$dir/config/openmw.cfg"
    printf '[Sound]\nmaster volume = 0.0\n[Video]\nresolution x = 1280\nresolution y = 720\nvsync mode = 0\n%b' "$settings" > "$dir/config/settings.cfg"
    export OPENEDEN_FIXED_FRAME_TIME=0.0166667
    ( cd "$build"; timeout 900 ./openmw.exe --replace=config --config "$(native "$dir/config")" --user-data "$(native "$dir/user")" \
        --skip-menu --no-grab=true --random-seed 7 "$@" > "$dir/log.txt" 2>&1 ) || echo "$name-$label exit $?"
}
for n in new reference; do
    b=$BUILD; [ $n = reference ] && b=$REF
    run $n "$b" interior "" --bake-gi "$INTERIOR"
    run $n "$b" exterior "" --start Balmora --bake-gi-exterior "$BLOCK"
    run $n "$b" frame "" --start Balmora --auto-screenshot 600 --auto-quit 660
    run $n "$b" framehdr "[Rendering]\nhdr = true\n" --start Balmora --auto-screenshot 600 --auto-quit 660
done
for kind in interior exterior; do
    for n in new reference; do (cd "$WORK/$n-$kind/user/openeden/probes" && find . -type f | sort | while read f; do echo "$n $kind $f $(sha256sum "$f" | cut -c1-16)"; done) ; done
done | sort -k2,3 > "$WORK/hashes.txt"
cat "$WORK/hashes.txt"
for k in frame framehdr; do for n in new reference; do echo "$n $k $(sha256sum "$WORK/$n-$k/user/screenshots/screenshot000.png" | cut -c1-16)"; done; done
