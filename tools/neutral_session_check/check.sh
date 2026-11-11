#!/bin/bash
# tools/neutral_session_check (tasks 0107 and 0109, neutral world stages N3a-2b and N3a-3): starts the game with
# game=skyrimse and [Game] neutral world on and the five Skyrim SE masters as the ONLY content (no Morrowind.esm), the
# player in a named Skyrim interior (--start), and checks the log: the pack was handed only the Skyrim plugins, built its
# world view, registered its source, the session started through its own start path, the player stub stands in the
# cell, the engine inserted the cell's objects (counts by kind), and a menu script's attempt to save was refused. Prints
# aggregates only. Read only: nothing is written outside <work dir>. The test game opens off-screen
# (OPENEDEN_QUIET_WINDOW).
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [interior editor id] [frames]
#
# <base config dir> holds the openmw.cfg of a Morrowind setup (a COPY; its resources/extension/content/data-local lines
# are replaced; its Morrowind data folder stays in the VFS for the engine's interface files). <work dir> must be under
# I:/Projects/OpenEden-work.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; CELL=${5:-WhiterunBanneredMare}; FRAMES=${6:-400}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
cp -r "$HERE/luatest/." "$WORK/luatest/"
B=$(native "$BUILD")
grep -vE '^(content|data-local|resources|extension|user-data|config)=|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
cat >> "$WORK/config/openmw.cfg" <<CFG
resources=$B/resources
data="$B/resources/vfs-mw"
data="$(native "$SKYRIM")"
data="$(native "$WORK/luatest")"
game=skyrimse
fallback-archive=Skyrim - Interface.bsa
content=Skyrim.esm
content=Update.esm
content=Dawnguard.esm
content=HearthFires.esm
content=Dragonborn.esm
content=n3a3save.omwscripts
CFG
printf '[Game]\nneutral world = true\n' > "$WORK/config/settings.cfg"
cd "$BUILD"
set +e
OPENEDEN_SKYRIMSE_PROBE_CELL="$CELL" timeout 170 ./openmw.exe --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --no-grab=true --random-seed 7 --start "$CELL" \
    --auto-quit "$FRAMES" > "$WORK/log.txt" 2>&1
code=$?
set -e
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
grep -E "Session content|world view built|world source registered|probe cell|Starting a new game|Neutral start|Neutral cell|N3A3" "$WORK/log.txt" | sed 's/^\[[^]]*\] //'
# The one expected error line is the refusal to save (a neutral session has no save support until stage N6).
errors=$(grep -E " E\] " "$WORK/log.txt" | grep -vc "Saving refused" || true)
echo "exit code $code, $dumps crash dumps, $errors unexpected error lines"
grep -q "world view built from the session: 5 of 5 plugins" "$WORK/log.txt" || { echo "FAILED: the pack was not given the five Skyrim plugins"; exit 1; }
grep -q "probe cell found: yes" "$WORK/log.txt" || { echo "FAILED: the probe cell was not found"; exit 1; }
grep -q "Starting a new game (neutral world)" "$WORK/log.txt" || { echo "FAILED: the neutral start path did not run"; exit 1; }
grep -q "Neutral cell '$CELL' inserted into the scene: [1-9]" "$WORK/log.txt" || { echo "FAILED: no objects were inserted"; exit 1; }
grep -q "Saving refused" "$WORK/log.txt" || { echo "FAILED: the save attempt was not refused"; exit 1; }
[ "$code" = 0 ] && [ "$dumps" = 0 ] && [ "$errors" = 0 ] || { echo FAILED; exit 1; }
echo OK
