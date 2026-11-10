#!/bin/bash
# Editor windows open off-screen (apps/opencs/view/doc/quietwindows.hpp); run with OPENEDEN_QUIET_WINDOW=0 to see them.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# Task 0086, world model option 1: Eden Studio's read-only Skyrim SE cell viewer. Each cell is captured through the real
# editor (eden-studio --open-mesh skyrimcell:<spec> --auto-screenshot ...): the game pack's world view (the SDK's
# WorldView contract) lists the cell's placed objects and the viewer draws each as its real mesh (read from the game's
# BSAs through the VFS) or, where no mesh loads, as a box of its bounds.
#
# Usage: tools/skyrimcell_check/check.sh <build dir> <empty work dir> <cell spec> [<cell spec> ...]
#   a cell spec is an interior cell's editor id, or "@<world editor id>,<x>,<y>[,<radius>]" for the exterior cells
#   around a grid square (radius 0 to 4). Pass editor ids on the command line only; none is kept in the repository.
# EXE=<file name in the build dir> runs another editor executable (default eden-studio.exe), for before/after runs.
# Needs OPENEDEN_SKYRIMSE_DATA (the game's Data folder) and OPENEDEN_SKYRIMSE_PLUGINS (a text file with one plugin path
# per line, in load order; the game's masters are enough). Optional: MESH_CAMERA="ex,ey,ez,cx,cy,cz" for every cell.
# Reads only; the work folder must be under I:/Projects/OpenEden-work and holds a scratch config and the frames.
set -u
[ $# -ge 3 ] || { sed -n 9,10p "$0"; exit 2; }
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
WORK=$2
shift 2
DATA=${OPENEDEN_SKYRIMSE_DATA:-}
PLUGINS=${OPENEDEN_SKYRIMSE_PLUGINS:-}
native() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
[ -n "$DATA" ] && [ -d "$DATA" ] || { echo "OPENEDEN_SKYRIMSE_DATA is not set to a Data folder"; exit 2; }
[ -n "$PLUGINS" ] && [ -f "$PLUGINS" ] || { echo "OPENEDEN_SKYRIMSE_PLUGINS is not set to a plugin list file"; exit 2; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "refused: $WORK is not empty"; exit 2; }
FAILED=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; FAILED=1; }

mkdir -p "$WORK/config" "$WORK/empty-data"
# The editor needs one content file to open: a generated empty plugin (TES3 header only, no records).
perl -e 'my $hedr = pack("fVa32a256V", 1.3, 0, "generated", "empty content file for the Skyrim cell view check", 0);
    my $sub = "HEDR" . pack("V", length $hedr) . $hedr;
    binmode STDOUT; print "TES3" . pack("VVV", length $sub, 0, 0) . $sub;' > "$WORK/empty-data/empty.esp"
W=$(native "$WORK")
D=$(native "$DATA")
{
    echo "data=\"$D\""
    echo "data=\"$W/empty-data\""
    echo "content=empty.esp"
    # Meshes and textures: every mesh and texture archive of the base game, then Update.bsa (it overrides).
    for a in "$DATA"/"Skyrim - Meshes"*.bsa "$DATA"/"Skyrim - Textures"*.bsa "$DATA"/Update.bsa; do
        [ -f "$a" ] && echo "fallback-archive=$(basename "$a")"
    done
} > "$WORK/config/openmw.cfg"

i=0
for spec in "$@"; do
    i=$((i + 1))
    mkdir -p "$WORK/user-$i"
    extra=()
    [ -n "${MESH_CAMERA:-}" ] && extra=(--mesh-camera "$MESH_CAMERA")
    (cd "$BUILD" && timeout 300 ./${EXE:-eden-studio.exe} --replace=config --config "$W/config" --user-data "$W/user-$i" \
        --open-mesh "skyrimcell:$spec" --auto-screenshot 30 --auto-quit 60 "${extra[@]}" > "$WORK/$i.out" 2>&1)
    code=$?
    # The first run after a cold disk can be ended by the freeze monitor (exit 57005): run again.
    if [ "$code" -eq 57005 ]; then
        (cd "$BUILD" && timeout 300 ./${EXE:-eden-studio.exe} --replace=config --config "$W/config" --user-data "$W/user-$i" \
            --open-mesh "skyrimcell:$spec" --auto-screenshot 30 --auto-quit 60 "${extra[@]}" > "$WORK/$i.out" 2>&1)
        code=$?
    fi
    [ "$code" -eq 0 ] && pass "cell $i exits 0" || fail "cell $i exits $code"
    [ -s "$WORK/user-$i/screenshots/mesh0.png" ] && pass "cell $i wrote a frame" || fail "cell $i wrote no frame"
    grep -h "Cell viewer:" "$WORK/$i.out" | sed 's/^\[[^]]*\] //' | head -1
done
exit $FAILED
