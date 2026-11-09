#!/bin/bash
# tools/dialogue_check (task 0085 move 3, stage 3c): starts a real conversation in a real game, so that the journal and the
# dialogue manager made by the Morrowind game pack (installed by the executable through MWDialogue::installFactories)
# run: the greeting, the topic list, a topic response chosen by the script, a quest entry, and the journal window. It takes
# a screenshot of the dialogue window (greeting and topic list) and of the journal window. Known limit: no topic response and
# no journal entry is made (the Lua API has no call to choose a topic; a Journal command run by --script-run did not
# leave an entry in the new game), so the journal frame shows the empty-journal text.
#
#   check.sh <build dir> <base config dir> <work dir>
#
# <base config dir> holds the openmw.cfg and settings.cfg of a Morrowind setup: a COPY, never the real one. <work dir> must be
# under I:/Projects/OpenEden-work. The game quits by itself after about 40 s; the script fails on a non-zero exit code, a
# crash dump, an error or exception in the log, or fewer than two screenshots. The frames are copied to <work dir>/frames:
# look at them. The test game opens off-screen (OPENEDEN_QUIET_WINDOW).
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
printf '\ndata="%s"\ncontent=dlgcheck.omwscripts\n' "$(native "$HERE/mod")" >> "$WORK/config/openmw.cfg"
cd "$BUILD"
set +e
timeout "${TIMEOUT:-150}" "${OPENMW_EXE:-./openmw.exe}" --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --start Balmora --no-grab=true \ > "$WORK/log.txt" 2>&1
code=$?
set -e
cp "$WORK"/user/screenshots/* "$WORK/frames"/ 2>/dev/null || true
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
errors=$(grep -ciE "\[[0-9:.]+ E\]|exception" "$WORK/log.txt" || true)
shots=$(ls "$WORK/frames" | wc -l)
echo "exit code $code, $dumps crash dumps, $errors error lines, $shots screenshots; $(grep -c 'DLG shot' "$WORK/log.txt") pictures taken"
if [ "$code" != 0 ] || [ "$dumps" != 0 ] || [ "$errors" != 0 ] || [ "$shots" -lt 2 ]; then
    echo "FAILED"
    exit 1
fi
