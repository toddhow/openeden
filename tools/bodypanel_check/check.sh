#!/bin/bash
# Editor windows open off-screen, out of the taskbar and without taking focus (apps/opencs/view/doc/quietwindows.hpp); run with OPENEDEN_QUIET_WINDOW=0 to see them.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# The body package panel in the real editor (task 0069 stage f, editor_modules/advancedbodies_editor): opens a merged
# body cache through its `current` pointer and chooses another body with the module's Next Body command, then a
# SYNTHETIC modder package as a folder and as a .edenpak (written here by make_synthetic_package.py, no game data), and
# writes for each the model-less Mesh view's frame (the region colours and weld points), a grab of the panel and of
# the window, and the panel's rows in the log.
#
# Usage: tools/bodypanel_check/check.sh <build dir> <openmw.cfg with your data= and content= lines> <content file>
#                                        <user data folder holding openeden/bodies> <empty work dir> [<body to choose>]
# <body to choose> is "<race> <sex> <view>" as the cache names it (default "Breton female third"). On Windows, PATH must
# already find the editor's runtime libraries, Qt's bin included, and python must be on PATH. The cfg, the cache and
# its stage a report (<user data>/analysis/bodies.json, optional) are COPIED into the work folder, which must be under
# I:/Projects/OpenEden-work; no folder under "My Games" is accepted. The real user folder (Documents/My Games/OpenMW)
# is only listed, before and after, and must be unchanged.
#
# Pass/fail lines:
#   1. the editor exits with code 0 (a module command that went on the document's undo stack would leave a "save
#      changes?" question and the run would time out)
#   2. every source opened, and choosing a body left the undo stack and the document's modified flag unchanged
#   3. a frame of the Mesh view was written for every source
#   4. the synthetic package lists exactly its two planted errors (BODY-GEOM-005, BODY-WELD-005)
#   5. a second run with no source and no OPENEDEN_CS_ASSETDIR opens the merged cache through the host's user data
#      folder (API 1.1)
#   6. the real user folder is unchanged
set -u
[ $# -ge 5 ] || { sed -n 4,16p "$0"; exit 2; }
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
CFG=$2
CONTENT=$3
SOURCEUSER=$4
WORK=$5
CHOOSE=${6:-Breton female third}
HERE=$(cd "$(dirname "$0")" && pwd)
native() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
FAILED=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; FAILED=1; }

case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
for folder in "$CFG" "$SOURCEUSER" "$WORK"; do
    case "$(native "$folder")" in
        *"My Games"*) echo "refused: $folder looks like the real user folder; pass copies"; exit 2 ;;
    esac
done
[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "refused: $WORK is not empty"; exit 2; }
[ -f "$SOURCEUSER/openeden/bodies/current" ] || { echo "no merged body cache in $SOURCEUSER/openeden/bodies"; exit 2; }

if [ -d "${USERPROFILE:-}/OneDrive/Documents/My Games/OpenMW" ]; then REAL="$USERPROFILE/OneDrive/Documents/My Games/OpenMW"
elif [ -d "${USERPROFILE:-}/Documents/My Games/OpenMW" ]; then REAL="$USERPROFILE/Documents/My Games/OpenMW"
else REAL="$HOME/.config/openmw"; fi
snapshot() { find "$REAL" -printf '%P\t%s\t%T@\n' 2>/dev/null | sort; }

mkdir -p "$WORK/config" "$WORK/user/openeden" "$WORK/user/analysis" "$WORK/synthetic" "$WORK/frames"
tr -d '\r' < "$CFG" > "$WORK/config/openmw.cfg"
cp -r "$SOURCEUSER/openeden/bodies" "$WORK/user/openeden/"
[ -f "$SOURCEUSER/analysis/bodies.json" ] && cp "$SOURCEUSER/analysis/bodies.json" "$WORK/user/analysis/"
python "$HERE/make_synthetic_package.py" "$WORK/synthetic/tubes.body" > "$WORK/synthetic.txt" || fail "the synthetic package"
(cd "$WORK/synthetic" && "$BUILD/edenpack" tubes.body tubes.edenpak > /dev/null) || fail "packing the synthetic package"
snapshot > "$WORK/real-before.tsv"

W=$(native "$WORK")
SOURCES="$W/user/openeden/bodies/current|$CHOOSE;$W/synthetic/tubes.body/body.json;$W/synthetic/tubes.edenpak"
(cd "$BUILD" && OPENEDEN_CS_UI_CHECK="c:#-2 -9;40000" \
    OPENEDEN_CS_BODYCHECK="$W/frames/shot;$SOURCES" OPENEDEN_CS_ASSETDIR="$W/user/openeden/bodies" \
    OPENEDEN_CS_GRAB="$W/frames" timeout 300 ./eden-studio.exe --replace=config --config "$W/config" \
    --user-data "$W/user" -- "$CONTENT" > "$WORK/run.out" 2>&1)
rc=$?
LOG="$WORK/config/eden-studio.log"
[ $rc = 0 ] && pass "the editor exited with code 0" || fail "the editor exited with code $rc (see $WORK/run.out)"

opened=$(grep -c "UI check: body [0-9]* opened .*: yes" "$LOG" 2>/dev/null)
if [ "$opened" = 3 ] && grep -q "chose $CHOOSE after .*undo steps \([0-9]*\) -> \1, document modified 0" "$LOG"; then
    pass "3 sources opened, '$CHOOSE' chosen with no undo step"
else
    fail "opened $opened of 3 sources, or the choice touched the undo stack: $(grep 'UI check: body 0 chose' "$LOG")"
fi
frames=$(grep -c "UI check: body frame .*: written" "$LOG" 2>/dev/null)
[ "$frames" = 4 ] && pass "4 Mesh view frames written in $WORK/frames" || fail "$frames of 4 Mesh view frames written"
if grep -q "body 1 row finding.0 = /regionMap/regions:" "$LOG" && grep -q "body 1 row finding.1 = /weldGroups/groups/12:" "$LOG" \
    && ! grep -q "body 1 row finding.2 " "$LOG"; then
    pass "the synthetic package lists exactly BODY-GEOM-005 and BODY-WELD-005"
else
    fail "the synthetic package's findings are not the two planted ones (see $LOG)"
fi
# A second run with no source and no OPENEDEN_CS_ASSETDIR: the module's "Open Merged Body Cache" command finds the cache
# through the host's user data folder (Editor Module API 1.1, EditorContext::userDataPath).
(cd "$BUILD" && OPENEDEN_CS_UI_CHECK="c:#-2 -9;40000" OPENEDEN_CS_BODYCHECK="$W/frames/default"     OPENEDEN_CS_GRAB="$W/frames" timeout 300 ./eden-studio.exe --replace=config --config "$W/config"     --user-data "$W/user" -- "$CONTENT" > "$WORK/run-default.out" 2>&1)
rc=$?
if [ $rc = 0 ] && grep -q "UI check: body 0 opened the default merged cache: yes" "$LOG" 2>/dev/null; then
    pass "the merged cache was found through the user data folder, no asset root named (exit code 0)"
else
    fail "the default merged cache was not opened (exit code $rc, see $WORK/run-default.out and $LOG)"
fi
# Per-module filtering: while a body document is active, CreativeMotion's panel has no rows, the document's owner is the
# body module, and the Asset Browser's owner filter is the chosen workspace's module.
rows=$(grep -c "UI check: body [0-9b]* creativemotion panel rows 0, active owner 'advancedbodies_editor'" "$LOG" 2>/dev/null)
if [ "${rows:-0}" -ge 1 ] && ! grep -q "creativemotion panel rows [1-9]" "$LOG"     && grep -q "asset owner filter 'advancedbodies_editor'" "$LOG"; then
    pass "CreativeMotion's panel stays empty for a body document; the Asset Browser filter is the body module"
else
    fail "per-module filtering: $(grep 'creativemotion panel rows' "$LOG" | head -3)"
fi
snapshot > "$WORK/real-after.tsv"
cmp -s "$WORK/real-before.tsv" "$WORK/real-after.tsv" && pass "the real user folder is unchanged: $REAL" \
    || fail "the real user folder changed: $(diff "$WORK/real-before.tsv" "$WORK/real-after.tsv" | head -5)"

[ $FAILED = 0 ] && echo "BODY PANEL CHECK PASSED" || echo "BODY PANEL CHECK FAILED"
exit $FAILED
