#!/bin/bash
# tools/face_check (task 0071, engine wiring): in a real game the nearest NPC is turned toward the player, who stands 90
# units in front of it, and says a vanilla voice line; the player script takes SHOTS screenshots 0.1 simulated seconds apart.
#
#   check.sh <build dir> <base config dir> <work dir> <curves|nocurves> <on|off> [voice file]
#
# curves:   a test-pattern curves file (tools/face_check/mkcurves.cpp, built with MKCURVES=<exe>) is placed beside the
#           voice line in the work mod folder (never in the repository).
# nocurves: no curves file (the loudness drive must run).
# on|off:   [Face] speech curves.
# A voice file of "none" says nothing (for frame comparisons of the setting on and off; use OPENEDEN_FIXED_FRAME_TIME=0.0166667).
# <base config dir> holds the openmw.cfg and settings.cfg of a Morrowind setup: a COPY, never the real one. <work dir> must
# be under I:/Projects/OpenEden-work. VOICEDIR is the folder that holds Sound/Vo (default: the GOG install's Data Files).
# OPENEDEN_FACE_TRACE=1 logs FACETRACE lines (voice file, stream position, talk, blink) every frame the line plays.
# The test game opens off-screen (OPENEDEN_QUIET_WINDOW) and quits by itself.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
export OPENEDEN_FACE_TRACE="${OPENEDEN_FACE_TRACE-1}"
# The voice plays in real time, so the simulated clock is the real one here (set OPENEDEN_FIXED_FRAME_TIME only for
# comparisons of scenes without a voice line).
BUILD=$1; BASE=$2; WORK=$3; CURVES=${4:-curves}; MODE=${5:-on}; VOICE=${6:-Sound/Vo/a/f/Hlo_AF002.mp3}
HERE="$(cd "$(dirname "$0")" && pwd)"
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
case "$(native "$BASE")" in
    *"My Games"*) echo "refused: $BASE looks like the real game folder; pass a copy"; exit 2 ;;
esac
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/frames"
cp "$BASE/openmw.cfg" "$WORK/config/openmw.cfg"
[ -f "$BASE/settings.cfg" ] && cp "$BASE/settings.cfg" "$WORK/config/settings.cfg"
touch "$WORK/config/settings.cfg"
sed -i '/^speech curves/d' "$WORK/config/settings.cfg"
printf '\n[Face]\nspeech curves = %s\n[Camera]\nfield of view = 35\nfirst person field of view = 35\n' \
    "$([ "$MODE" = on ] && echo true || echo false)" >> "$WORK/config/settings.cfg"
mkdir -p "$WORK/mod"; cp -r "$HERE/mod/." "$WORK/mod/"
voicenorm=$(echo "$VOICE" | tr 'A-Z' 'a-z')
stem="${voicenorm%.*}"
if [ "$CURVES" = curves ] && [ "$VOICE" != none ]; then
    mkdir -p "$WORK/mod/$(dirname "$stem")"
    voicefile="${VOICEDIR:-/i/GOG Galaxy/Games/Morrowind/Data Files}/$VOICE"
    dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$voicefile")
    sha=$(sha256sum "$voicefile" | cut -d' ' -f1)
    "${MKCURVES:?set MKCURVES to the mkcurves executable}" "$WORK/mod/$stem.facecurves" "$voicenorm" "$dur" "$sha"
fi
vlua=$VOICE
# No voice line: the clock is the simulated one (fast under a fixed frame time) and the screenshot writer drops requests
# that come too close together, so fewer, slower shots.
NSHOTS=26; STEP=0.1; MINSHOTS=20
if [ "$VOICE" = none ]; then NSHOTS=12; STEP=0.5; MINSHOTS=12; fi
printf "return { voice = '%s', distance = 90, shots = %s, shotStep = %s, pitch = 0.0 }
" "$vlua" "$NSHOTS" "$STEP" > "$WORK/mod/scripts/faceparams.lua"
printf '\ndata="%s"\ncontent=face.omwscripts\n' "$(native "$WORK/mod")" >> "$WORK/config/openmw.cfg"
cd "$BUILD"
set +e
timeout "${TIMEOUT:-150}" "${OPENMW_EXE:-./openmw.exe}" --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --start "${START:-Seyda Neen}" --no-grab --random-seed 1 > "$WORK/log.txt" 2>&1
code=$?
set -e
cp "$WORK"/user/screenshots/* "$WORK/frames"/ 2>/dev/null || true
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
errors=$(grep -ciE "\[[0-9:.]+ E\]|exception" "$WORK/log.txt" || true)
shots=$(ls "$WORK/frames" | wc -l)
said=$(grep -c 'FACE say' "$WORK/log.txt" || true)
traced=$(grep -c FACETRACE "$WORK/log.txt" || true)
active=$(grep FACETRACE "$WORK/log.txt" | grep -c 'active=1' || true)
echo "curves $CURVES, setting $MODE: exit $code, $dumps dumps, $errors error lines, $shots screenshots, said $said, $traced trace lines ($active active)"
if [ "$code" != 0 ] || [ "$dumps" != 0 ] || [ "$errors" != 0 ] || [ "$shots" -lt "$MINSHOTS" ] || { [ "$VOICE" != none ] && [ "$said" != 1 ]; }; then
    echo "FAILED"
    exit 1
fi
if [ "$MODE" = on ] && [ "$CURVES" = curves ] && [ "$active" = 0 ]; then echo "FAILED: curves never drove the face"; exit 1; fi
if { [ "$MODE" = off ] || [ "$CURVES" = nocurves ]; } && [ "$active" != 0 ]; then echo "FAILED: curves drove the face"; exit 1; fi
