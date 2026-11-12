#!/bin/bash
# Editor windows open off-screen, out of the taskbar and without taking focus (apps/opencs/view/doc/quietwindows.hpp); run with OPENEDEN_QUIET_WINDOW=0 to see them.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# The Audio2Face curve generator in the real editor (task 0147 slice 2, editor_modules/face_editor, OPENEDEN_CS_FACEGEN in
# apps/opencs/editor.cpp): makes a curve file for a voice line, runs Face > Generate Curves with the chosen model, waits for
# the background task, logs the time and what the channels hold, writes frames of the curve canvas before and after, undoes
# and redoes the ONE step through Edit, and saves.
#
# Usage: tools/facegen_check/check.sh <build dir> <empty work dir> <voice line .mp3/.wav> <variant v2.3-mark|v3.0> <directml 0|1> <models root>
# <models root> is a folder holding v3.0/ and v2.3-mark/ (the downloads, or links to them): it is LINKED as the editor's
# models folder (<user data>/models/audio2face), never copied. The work folder must be under I:/Projects/OpenEden-work.
# The voice line is COPIED into the work folder (the editor writes the .facecurves.json beside its audio).
set -u
[ $# -ge 6 ] || { sed -n 9,12p "$0"; exit 2; }
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
WORK=$2
VOICE=$3
VARIANT=$4
DML=$5
MODELS=$6
native() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "refused: $WORK is not empty"; exit 2; }
mkdir -p "$WORK/config" "$WORK/user/models" "$WORK/empty-data" "$WORK/audio" "$WORK/frames"
perl -e 'my $hedr = pack("fVa32a256V", 1.3, 0, "generated", "empty content file for the generator check", 0);
    my $sub = "HEDR" . pack("V", length $hedr) . $hedr;
    binmode STDOUT; print "TES3" . pack("VVV", length $sub, 0, 0) . $sub;' > "$WORK/empty-data/empty.esp"
W=$(native "$WORK")
{
    echo "data=\"$W/empty-data\""
    echo "content=empty.esp"
} > "$WORK/config/openmw.cfg"
EXT="${VOICE##*.}"
cp "$VOICE" "$WORK/audio/line.$EXT"
# A junction (no admin rights needed) so that the real models are used where they are.
cmd //c "mklink /J $(native "$WORK" | sed 's#/#\\#g')\\user\\models\\audio2face $(native "$MODELS" | sed 's#/#\\#g')" > /dev/null
# Slice 3: DirectML is not part of the build. With <directml> 1 and OPENEDEN_DML_DLL set to a DirectML.dll, that file is
# placed where Face > Install DirectML... would keep it (the version folder of the pinned package) before the run.
if [ "$DML" != "0" ] && [ -n "${OPENEDEN_DML_DLL:-}" ]; then
    mkdir -p "$WORK/user/openeden/directml/1.15.4"
    cp "$OPENEDEN_DML_DLL" "$WORK/user/openeden/directml/1.15.4/DirectML.dll"
fi
[ -d "$WORK/user/models/audio2face/$VARIANT" ] || { echo "FAIL: the models folder has no $VARIANT"; exit 1; }

start=$(date +%s)
(cd "$BUILD" && OPENEDEN_CS_UI_CHECK=";90000" OPENEDEN_CS_FACEGEN="$W/frames/gen;$W/audio/line.$EXT;$VARIANT;$DML" \
    timeout 300 ./eden-studio.exe --replace=config --config "$W/config" --user-data "$W/user" -- "$W/empty-data/empty.esp" \
    > "$WORK/run.out" 2>&1)
rc=$?
end=$(date +%s)
LOG="$WORK/config/eden-studio.log"
echo "editor exit code $rc after $((end - start)) s"
grep -E "UI check: facegen|Generated |Generate Curves|face_editor" "$LOG" | sed 's/^.*UI check: //'
