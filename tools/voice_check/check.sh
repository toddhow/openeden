#!/bin/bash
# tools/voice_check (task 0143): a Morrowind NPC says a vanilla mp3 voice line through SoundManager::say, the path the
# Skyrim .fuz voice shares. Checks that the line becomes active and ends, with no error in the log. Morrowind data only.
# The audio device is OpenAL Soft's null device (ALSOFT_DRIVERS=null) unless you set it: nothing is heard.
#
#   check.sh <build dir> <base config dir> <work dir>
#
# <base config dir> holds the openmw.cfg and settings.cfg of a Morrowind setup (a COPY); <work dir> must be under
# I:/Projects/OpenEden-work. The test game opens off-screen (OPENEDEN_QUIET_WINDOW).
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
export ALSOFT_DRIVERS="${ALSOFT_DRIVERS-null}"
BUILD=$1; BASE=$2; WORK=$3
HERE="$(cd "$(dirname "$0")" && pwd)"
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
case "$(native "$BASE")" in
    *"My Games"*) echo "refused: $BASE looks like the real game folder; pass a copy"; exit 2 ;;
esac
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user"
cp "$BASE/openmw.cfg" "$WORK/config/openmw.cfg"
[ -f "$BASE/settings.cfg" ] && cp "$BASE/settings.cfg" "$WORK/config/settings.cfg"
printf '\ndata="%s"\ncontent=voicecheck.omwscripts\n' "$(native "$HERE/mod")" >> "$WORK/config/openmw.cfg"
cd "$BUILD"
set +e
timeout "${TIMEOUT:-120}" "${OPENMW_EXE:-./openmw.exe}" --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --start Balmora --no-grab=true --random-seed 7 --auto-quit "${FRAMES:-4000}" > "$WORK/log.txt" 2>&1
code=$?
set -e
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
errors=$(grep -ciE "\[[0-9:.]+ E\]|exception" "$WORK/log.txt" || true)
grep -E "VOICECHECK" "$WORK/log.txt" | sed 's/^\[[^]]*\] //'
echo "exit code $code, $dumps crash dumps, $errors error lines"
grep -q "VOICECHECK active" "$WORK/log.txt" || { echo "FAILED: the line never became active"; exit 1; }
grep -q "VOICECHECK ended" "$WORK/log.txt" || { echo "FAILED: the line never ended"; exit 1; }
[ "$code" = 0 ] && [ "$dumps" = 0 ] && [ "$errors" = 0 ] || { echo FAILED; exit 1; }
echo OK
