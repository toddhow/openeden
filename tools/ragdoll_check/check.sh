#!/bin/bash
# tools/ragdoll_check (task 0106, stage c): in a real game, an NPC is killed by a Lua script while the player watches
# from 450 units away; nine screenshots are taken over 4 s. Run it with the ragdolls on and off:
#
#   check.sh <build dir> <base config dir> <work dir> <on|off>
#
# <base config dir> holds the openmw.cfg and settings.cfg of a Morrowind setup: a COPY, never the real one. <work dir> must
# be under I:/Projects/OpenEden-work. OPENMW_EXE (default ./openmw.exe in the build dir) picks the executable, so the
# parent commit's can be compared. With OPENEDEN_RAGDOLL_TRACE the log shows where the ragdoll is every two seconds.
# The test game opens off-screen (OPENEDEN_QUIET_WINDOW); it quits by itself. Exit code 0 means: no crash dump, no error
# line, the kill happened, nine screenshots exist, and with "on" the log says an actor ragdoll started.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
export OPENEDEN_RAGDOLL_TRACE="${OPENEDEN_RAGDOLL_TRACE-1}"
export OPENEDEN_FIXED_FRAME_TIME="${OPENEDEN_FIXED_FRAME_TIME-0.0166667}"
BUILD=$1; BASE=$2; WORK=$3; MODE=${4:-on}
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
sed -i '/^creativemotion ragdolls/d' "$WORK/config/settings.cfg"
if [ "$MODE" = on ]; then
    printf '\n[Physics]\ncreativemotion ragdolls = true\n' >> "$WORK/config/settings.cfg"
fi
printf '\ndata="%s"\ncontent=ragcheck.omwscripts\n' "$(native "$HERE/mod")" >> "$WORK/config/openmw.cfg"
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
killed=$(grep -c 'RAG killed' "$WORK/log.txt" || true)
started=$(grep -c 'Actor ragdoll started' "$WORK/log.txt" || true)
echo "mode $MODE: exit code $code, $dumps crash dumps, $errors error lines, $shots screenshots, killed $killed, ragdolls started $started"
grep -E "RAG target|Actor ragdoll [0-9]|every body asleep" "$WORK/log.txt" | head -20
if [ "$code" != 0 ] || [ "$dumps" != 0 ] || [ "$errors" != 0 ] || [ "$shots" -lt 9 ] || [ "$killed" != 1 ]; then
    echo "FAILED"
    exit 1
fi
if [ "$MODE" = on ] && [ "$started" != 1 ]; then echo "FAILED: no ragdoll started"; exit 1; fi
if [ "$MODE" = off ] && [ "$started" != 0 ]; then echo "FAILED: a ragdoll started with the setting off"; exit 1; fi
