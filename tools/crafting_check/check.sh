#!/bin/bash
# tools/crafting_check (task 0085 move 2, stage 2c): opens the alchemy, repair, recharge and enchanting windows through the
# Lua UI interface in a real game, so that each window's open path runs through the crafting contract (the Morrowind game
# pack's rules installed by the executable), and takes a screenshot of each.
#
#   check.sh <build dir> <base config dir> <work dir>
#
# <base config dir> holds the openmw.cfg and settings.cfg of a Morrowind setup: a COPY, never the real one. <work dir> must be
# under I:/Projects/OpenEden-work (the script empties its user folder and screenshots). The game quits by itself after
# about 30 s; the script fails on a non-zero exit code, a crash dump, an error or exception in the log, or fewer than four
# screenshots. The frames are copied to <work dir>/frames: look at them (the alchemy window with the apparatus in its four
# slots and the ingredients; "Uses 25 / Quality 0.5" in the repair window; "Charge: 165" in the recharge window and the
# enchanting dialog). The test game opens off-screen (OPENEDEN_QUIET_WINDOW).
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
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
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/frames"
cp "$BASE/openmw.cfg" "$WORK/config/openmw.cfg"
[ -f "$BASE/settings.cfg" ] && cp "$BASE/settings.cfg" "$WORK/config/settings.cfg"
printf '\ndata="%s"\ncontent=craftcheck.omwscripts\n' "$(native "$HERE/mod")" >> "$WORK/config/openmw.cfg"
cd "$BUILD"
set +e
timeout "${TIMEOUT:-150}" "${OPENMW_EXE:-./openmw.exe}" --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --start Balmora --no-grab \
    --script-run "$(native "$HERE/start.txt")" > "$WORK/log.txt" 2>&1
code=$?
set -e
cp "$WORK"/user/screenshots/* "$WORK/frames"/ 2>/dev/null || true
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
errors=$(grep -ciE "\[[0-9:.]+ E\]|exception" "$WORK/log.txt" || true)
shots=$(ls "$WORK/frames" | wc -l)
echo "exit code $code, $dumps crash dumps, $errors error lines, $shots screenshots; $(grep -c 'CRAFT shot' "$WORK/log.txt") windows photographed"
if [ "$code" != 0 ] || [ "$dumps" != 0 ] || [ "$errors" != 0 ] || [ "$shots" -lt 4 ]; then
    echo "FAILED"
    exit 1
fi
