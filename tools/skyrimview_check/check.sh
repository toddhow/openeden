#!/bin/bash
# Editor windows open off-screen (apps/opencs/view/doc/quietwindows.hpp); run with OPENEDEN_QUIET_WINDOW=0 to see them.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# Task 0086 stage e: Eden Studio's mesh viewer opens the installed Skyrim Special Edition's skeleton and bodies. Each
# model is captured through the real editor (eden-studio --open-mesh ... --auto-screenshot), which reads the game's own
# BSAs through the VFS and draws the file as a Nif::Scene at the bind pose.
#
# Usage: tools/skyrimview_check/check.sh <build dir> <empty work dir>
# Needs OPENEDEN_SKYRIMSE_DATA (the game's Data folder). Reads only; the work folder must be under
# I:/Projects/OpenEden-work and holds a scratch config and the frames. Prints aggregates and PASS/FAIL lines; no mod
# path is ever written (only the game's own Data folder is used).
set -u
[ $# -ge 2 ] || { sed -n 6,6p "$0"; exit 2; }
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
WORK=$2
DATA=${OPENEDEN_SKYRIMSE_DATA:-}
native() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
[ -n "$DATA" ] && [ -d "$DATA" ] || { echo "OPENEDEN_SKYRIMSE_DATA is not set to a Data folder"; exit 2; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "refused: $WORK is not empty"; exit 2; }
FAILED=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; FAILED=1; }

mkdir -p "$WORK/config" "$WORK/user" "$WORK/empty-data"
# The editor needs one content file to open: a generated empty plugin (TES3 header only, no records).
perl -e 'my $hedr = pack("fVa32a256V", 1.3, 0, "generated", "empty content file for the Skyrim mesh view check", 0);
    my $sub = "HEDR" . pack("V", length $hedr) . $hedr;
    binmode STDOUT; print "TES3" . pack("VVV", length $sub, 0, 0) . $sub;' > "$WORK/empty-data/empty.esp"
W=$(native "$WORK")
D=$(native "$DATA")
{
    echo "data=\"$D\""
    echo "data=\"$W/empty-data\""
    echo "content=empty.esp"
    echo "fallback-archive=Skyrim - Meshes0.bsa"
    echo "fallback-archive=Skyrim - Meshes1.bsa"
    echo "fallback-archive=Skyrim - Textures0.bsa"
} > "$WORK/config/openmw.cfg"

# name | VFS path | extra options
run() {
    local tag=$1 path=$2
    shift 2
    mkdir -p "$WORK/user-$tag"
    (cd "$BUILD" && timeout 300 ./eden-studio.exe --replace=config --config "$W/config" --user-data "$W/user-$tag" \
        --open-mesh "$path" --auto-screenshot 20 --auto-quit 40 "$@" > "$WORK/$tag.out" 2>&1)
    local rc=$?
    local log="$WORK/config/eden-studio.log"
    [ $rc = 0 ] && pass "$tag: the editor exited with code 0" || fail "$tag: the editor exited with code $rc (see $WORK/$tag.out)"
    local shot="$WORK/user-$tag/screenshots/mesh0.png"
    [ -s "$shot" ] && pass "$tag: frame written ($(stat -c %s "$shot") bytes)" || fail "$tag: no frame"
    cp "$shot" "$WORK/$tag.png" 2>/dev/null
    grep -h "read as a Nif::Scene" "$WORK/$tag.out" "$log" 2>/dev/null | sed 's/^.*Mesh viewer: [^ ]* read/read/' | head -1 | sed "s/^/$tag: /"
}

SKEL="actors/character/character assets/skeleton.nif"
MALE="actors/character/character assets/malebody_0.nif"
FEMALE="actors/character/character assets/femalebody_0.nif"
run skeleton "$SKEL" --mesh-show skeleton,grid
run skeleton_axes "$SKEL" --mesh-show skeleton,axes,grid --mesh-mode wireframe
run malebody "$MALE" --mesh-show grid
run malebody_bones "$MALE" --mesh-show skeleton,grid --mesh-mode wireframe
run femalebody "$FEMALE" --mesh-show grid
run femalebody_flat "$FEMALE" --mesh-show skeleton,bounds,grid --mesh-mode flat

exit $FAILED
