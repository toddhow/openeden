#!/bin/bash
# Editor windows open off-screen, out of the taskbar and without taking focus (apps/opencs/view/doc/quietwindows.hpp); run with OPENEDEN_QUIET_WINDOW=0 to see them.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# The editor module panel's undo behaviour in the real editor, with the SAMPLE module (sdk/examples/sample_editor_module):
# editing its panel must not mark the ESM document modified, and Edit > Undo and Edit > Redo must act on the module's
# own undo stack (CSMDoc::UndoRouting, task 0031 follow-up) while it was touched last. It also shows that the Asset
# Browser lists the sample's note and nothing else.
#
# Usage: tools/modulecheck/check.sh <build dir> <openmw.cfg with your data= and content= lines> <content file> <empty work dir>
# The build dir must hold eden-studio and editor-modules/sample_editor_module. The cfg is copied; the work folder must be
# under I:/Projects/OpenEden-work; the real user folder (Documents/My Games/OpenMW) is only listed, before and after.
#
# Pass/fail lines:
#   1. the editor exits with code 0 (a module step on the content stack would leave a "save changes?" question)
#   2. two panel edits are one module undo step and add none to the content stack, and the document is not modified
#   3. Edit > Undo undoes the module edit and the document stays unmodified; Edit > Redo redoes it
#   4. the real user folder is unchanged
set -u
[ $# -ge 4 ] || { sed -n 9,12p "$0"; exit 2; }
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
CFG=$2
CONTENT=$3
WORK=$4
native() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
FAILED=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; FAILED=1; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "refused: $WORK is not empty"; exit 2; }
if [ -d "${USERPROFILE:-}/OneDrive/Documents/My Games/OpenMW" ]; then REAL="$USERPROFILE/OneDrive/Documents/My Games/OpenMW"
elif [ -d "${USERPROFILE:-}/Documents/My Games/OpenMW" ]; then REAL="$USERPROFILE/Documents/My Games/OpenMW"
else REAL="$HOME/.config/openmw"; fi
snapshot() { find "$REAL" -printf '%P\t%s\t%T@\n' 2>/dev/null | sort; }

mkdir -p "$WORK/config" "$WORK/user" "$WORK/assets" "$WORK/frames"
tr -d '\r' < "$CFG" > "$WORK/config/openmw.cfg"
printf '{\n  "format": "sample.note",\n  "title": "first",\n  "weight": 5.0\n}\n' > "$WORK/assets/first.note.json"
snapshot > "$WORK/real-before.tsv"
W=$(native "$WORK")
LIB=$(native "$BUILD/editor-modules/sample_editor_module.dll")
[ -f "$BUILD/editor-modules/sample_editor_module.dll" ] || LIB=$(native "$BUILD/editor-modules/libsample_editor_module.so")

(cd "$BUILD" && OPENEDEN_CS_UI_CHECK="c:#-2 -9;40000" OPENEDEN_CS_MODULECHECK="$W/frames/module" \
    OPENEDEN_CS_MODULES="sample=$LIB" OPENEDEN_CS_ASSETDIR="$W/assets" OPENEDEN_CS_GRAB="$W/frames" \
    timeout 300 ./eden-studio.exe --replace=config --config "$W/config" --user-data "$W/user" -- "$CONTENT" \
    > "$WORK/run.out" 2>&1)
rc=$?
LOG="$WORK/config/eden-studio.log"
[ $rc = 0 ] && pass "the editor exited with code 0" || fail "the editor exited with code $rc (see $WORK/run.out)"

if grep -q "UI check: module edit: undo steps 0 -> 1 (two edits merge into one), content undo steps \([0-9]*\) -> \1, document modified 0" "$LOG"; then
    pass "two panel edits are one module step, no content step, document not modified"
else
    fail "module edit: $(grep 'UI check: module edit:' "$LOG")"
fi
if grep -q "UI check: module undo (Edit > Undo): weight 5, module stack index 0, document modified 0" "$LOG" \
    && grep -q "UI check: module redo (Edit > Redo): weight 7.5" "$LOG"; then
    pass "Edit > Undo and Edit > Redo act on the module stack"
else
    fail "edit menu: $(grep -E 'UI check: module (edit menu|undo|redo)' "$LOG")"
fi
snapshot > "$WORK/real-after.tsv"
cmp -s "$WORK/real-before.tsv" "$WORK/real-after.tsv" && pass "the real user folder is unchanged: $REAL" \
    || fail "the real user folder changed: $(diff "$WORK/real-before.tsv" "$WORK/real-after.tsv" | head -5)"
[ $FAILED = 0 ] && echo "MODULE CHECK PASSED" || echo "MODULE CHECK FAILED"
exit $FAILED
