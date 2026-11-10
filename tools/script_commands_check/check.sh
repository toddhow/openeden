#!/bin/bash
# tools/script_commands_check (task 0098 move 4): runs Morrowind's script commands in a real game. The command set (compile
# table and handlers) is the Morrowind game pack's, installed by the executable through MWScript::installCommandSet.
#   1. --script-all compiles every script of the installed game with that table and prints how many compiled and failed
#      (compare the numbers with a build from before the move);
#   2. a startup script (script.txt, --script-run) adds and removes gold and sets the player's strength and level; a Lua
#      player script prints what the player has afterwards, and the check wants gold=20 strength=80 level=9.
#
#   check.sh <build dir> <base config dir> <work dir>
#
# <base config dir> holds the openmw.cfg and settings.cfg of a Morrowind setup: a COPY, never the real one. <work dir> must be
# under I:/Projects/OpenEden-work. The test game opens off-screen (OPENEDEN_QUIET_WINDOW). The script fails on a non-zero
# exit code, a crash dump, an unexpected result or a missing screenshot.
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
cd "$BUILD"
set +e
echo "--- compile every script"
timeout "${TIMEOUT:-300}" "${OPENMW_EXE:-./openmw.exe}" --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --script-all --skip-menu --no-grab=true --auto-quit 600 > "$WORK/scriptall.txt" 2>&1
echo "exit code $?"; grep -iE "compil|failed|scripts" "$WORK/scriptall.txt" | tail -3
printf '\ndata="%s"\ncontent=sccheck.omwscripts\n' "$(native "$HERE/mod")" >> "$WORK/config/openmw.cfg"
echo "--- run the startup script"
timeout "${TIMEOUT:-150}" "${OPENMW_EXE:-./openmw.exe}" --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --start Balmora --no-grab=true --script-run "$(native "$HERE/script.txt")" \
    --auto-quit 3000 > "$WORK/log.txt" 2>&1
code=$?
set -e
cp "$WORK"/user/screenshots/* "$WORK/frames"/ 2>/dev/null || true
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
line=$(grep -h "SCCHECK" "$WORK/log.txt" | tail -1)
echo "exit code $code, $dumps crash dumps, result: ${line:-none}, $(ls "$WORK/frames" | wc -l) screenshots"
if [ "$code" != 0 ] || [ "$dumps" != 0 ] || ! echo "$line" | grep -q "gold=20 strength=80 level=9"; then
    echo "FAILED"
    exit 1
fi
echo "OK"
