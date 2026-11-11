#!/bin/bash
# tools/neutral_session_check (task 0107, neutral world stage N3a-2b): starts the game with game=skyrimse and
# [Game] neutral world on, the Skyrim SE masters as content (and Morrowind.esm, which the engine's Morrowind-shaped
# start-up still needs, see the task's stage log), and checks the log: the pack was handed only the Skyrim plugins, built
# its world view, registered its source, and (OPENEDEN_SKYRIMSE_PROBE_CELL) found a known interior. Prints aggregates
# only. Read only: nothing is written outside <work dir>. The test game opens off-screen (OPENEDEN_QUIET_WINDOW).
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [interior editor id]
#
# <base config dir> holds the openmw.cfg of a Morrowind setup (a COPY; its resources/extension/content/data-local lines
# are replaced). <work dir> must be under I:/Projects/OpenEden-work.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; CELL=${5:-WhiterunBanneredMare}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user"
B=$(native "$BUILD")
grep -vE '^(content|data-local|resources|extension|user-data|config)=|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
cat >> "$WORK/config/openmw.cfg" <<CFG
resources=$B/resources
data="$B/resources/vfs-mw"
data="$(native "$SKYRIM")"
game=skyrimse
fallback-archive=Skyrim - Interface.bsa
content=Morrowind.esm
content=Skyrim.esm
content=Update.esm
content=Dawnguard.esm
content=HearthFires.esm
content=Dragonborn.esm
CFG
printf '[Game]\nneutral world = true\n' > "$WORK/config/settings.cfg"
cd "$BUILD"
set +e
OPENEDEN_SKYRIMSE_PROBE_CELL="$CELL" timeout 150 ./openmw.exe --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --no-grab=true --random-seed 7 --auto-quit 600 > "$WORK/log.txt" 2>&1
code=$?
set -e
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
grep -E "Session content|world view built|world source registered|probe cell" "$WORK/log.txt" | sed 's/^\[[^]]*\] //'
errors=$(grep -cE " E\] " "$WORK/log.txt" || true)
echo "exit code $code, $dumps crash dumps, $errors error lines"
grep -q "world view built from the session: 5 of 5 plugins" "$WORK/log.txt" || { echo "FAILED: the pack was not given the five Skyrim plugins"; exit 1; }
grep -q "probe cell found: yes" "$WORK/log.txt" || { echo "FAILED: the probe cell was not found"; exit 1; }
[ "$code" = 0 ] && [ "$dumps" = 0 ] || { echo FAILED; exit 1; }
echo OK
