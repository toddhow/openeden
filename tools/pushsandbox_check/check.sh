#!/bin/bash
# Editor windows open off-screen, out of the taskbar and without taking focus (apps/opencs/view/doc/quietwindows.hpp); run with OPENEDEN_QUIET_WINDOW=0 to see them.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# The push sandbox in the real editor (task 0065 stage C, Editor Module API 1.3, editor_modules/creativemotion_editor):
# opens a rig and a profile, builds the sandbox with the module's Reset command, sets a push through the panel and
# runs the module's Push command, steps the editor's physics world with the Step command's path, lets the ragdoll
# settle and resets it. Frames of the model-less Mesh view are written for each moment (before, pending, flight,
# settled, reset). The whole scenario runs TWICE and the logged poses must be identical.
#
# Usage: tools/pushsandbox_check/check.sh <build dir> <openmw.cfg with your data= and content= lines> <content file>
#                                          <rig.json> <profile.json> <empty work dir> [<bone to push>]
# The build dir must hold eden-studio and editor-modules/creativemotion_editor, and PATH must find the editor's runtime
# libraries (Qt's bin included). The cfg is copied; the work folder must be under I:/Projects/OpenEden-work; the real
# user folder (Documents/My Games/OpenMW) is only listed, before and after. The rig and profile are CreativeMotion's
# data files (outside this repository); paths are given as I:/... or converted with cygpath.
#
# Pass/fail lines:
#   1. the editor exits with code 0 (a push that went on the undo stack would leave a "save changes?" question)
#   2. the module's commands were enabled with a rig and a profile open, and Reset built the sandbox
#   3. the push is pending before the first step and applied (pending 0) after it
#   4. the pushed ragdoll moved, came to rest above the ground (z > 0), and Reset put it back where it started
#   5. five frames were written
#   6. a second run logs the same poses to the last digit
#   7. the real user folder is unchanged
set -u
[ $# -ge 6 ] || { sed -n 11,13p "$0"; exit 2; }
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
CFG=$2
CONTENT=$3
RIG=$(cygpath -m "$4" 2>/dev/null || echo "$4")
PROFILE=$(cygpath -m "$5" 2>/dev/null || echo "$5")
WORK=$6
BONE=${7:-}
native() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
FAILED=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; FAILED=1; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "refused: $WORK is not empty"; exit 2; }
[ -f "$RIG" ] && [ -f "$PROFILE" ] || { echo "the rig or the profile does not exist"; exit 2; }
if [ -d "${USERPROFILE:-}/OneDrive/Documents/My Games/OpenMW" ]; then REAL="$USERPROFILE/OneDrive/Documents/My Games/OpenMW"
elif [ -d "${USERPROFILE:-}/Documents/My Games/OpenMW" ]; then REAL="$USERPROFILE/Documents/My Games/OpenMW"
else REAL="$HOME/.config/openmw"; fi
snapshot() { find "$REAL" -printf '%P\t%s\t%T@\n' 2>/dev/null | sort; }
snapshot > "$WORK/real-before.tsv"
W=$(native "$WORK")

runOnce() { # $1 = run name
    local name=$1
    mkdir -p "$WORK/$name/config" "$WORK/$name/user" "$WORK/$name/frames"
    tr -d '\r' < "$CFG" > "$WORK/$name/config/openmw.cfg"
    (cd "$BUILD" && OPENEDEN_CS_UI_CHECK="c:#-2 -9;50000" \
        OPENEDEN_CS_PUSHCHECK="$W/$name/frames/push;$RIG;$PROFILE;$BONE" \
        timeout 300 ./eden-studio.exe --replace=config --config "$W/$name/config" --user-data "$W/$name/user" \
        -- "$CONTENT" > "$WORK/$name/run.out" 2>&1)
    echo $? > "$WORK/$name/rc"
}
runOnce run1
runOnce run2
LOG="$WORK/run1/config/eden-studio.log"
LOG2="$WORK/run2/config/eden-studio.log"
rc=$(cat "$WORK/run1/rc"); rc2=$(cat "$WORK/run2/rc")
[ "$rc" = 0 ] && [ "$rc2" = 0 ] && pass "both editor runs exited with code 0" || fail "exit codes $rc and $rc2 (see $WORK/run*/run.out)"

if grep -q "UI check: push commands enabled: push 1, reset 1" "$LOG" && grep -q "UI check: push reset command ran 1" "$LOG" \
    && grep -q "UI check: push built: step 0, built yes, pending 0" "$LOG"; then
    pass "the commands were enabled and Reset built the sandbox"
else
    fail "build: $(grep -E 'UI check: push (opened|commands|reset command|built)' "$LOG")"
fi
if grep -q "UI check: push pushed, before any step: step 0, built yes, pending 1" "$LOG" \
    && grep -q "UI check: push after one step: step 1, built yes, pending 0" "$LOG"; then
    pass "the push is pending before the first step and applied by it"
else
    fail "pending: $(grep -E 'UI check: push (pushed|after one step)' "$LOG" | cut -c1-200)"
fi
python - "$LOG" > "$WORK/analysis.txt" 2>&1 <<'PY'
import re, sys
log = open(sys.argv[1], errors="replace").read().splitlines()
def poses(tag):
    for line in log:
        if f"UI check: push {tag}:" in line:
            return {m.group(1): tuple(float(x) for x in m.group(2).split()) for m in re.finditer(r", (\w+) \(([^)]*)\)", line)}
    return None
before, flight, settled, reset = poses("before"), poses("after 12 steps"), poses("after 412 steps"), poses("after reset")
ok = all(p for p in (before, flight, settled, reset))
if not ok:
    print("FAIL missing report", before is None, flight is None, settled is None, reset is None); sys.exit(1)
moved = max(abs(flight[k][0]-before[k][0]) + abs(flight[k][1]-before[k][1]) for k in before)
low = min(p[2] for p in settled.values())
back = max(max(abs(reset[k][i]-before[k][i]) for i in range(3)) for k in before)
print(f"moved {moved:.4f} m in 12 steps; lowest settled body z {low:.4f}; reset differs by {back:.2e}")
sys.exit(0 if moved > 0.05 and low > 0.0 and back < 1e-6 else 1)
PY
if [ $? = 0 ]; then pass "the ragdoll moved, rested above the ground and Reset put it back ($(cat "$WORK/analysis.txt"))"; else fail "poses: $(cat "$WORK/analysis.txt")"; fi
frames=$(grep -c "UI check: push frame .*: written" "$LOG" 2>/dev/null)
[ "$frames" = 5 ] && pass "5 frames written in $WORK/run1/frames" || fail "$frames of 5 frames written"
if diff <(grep -E "UI check: push (built|before|pushed|after)" "$LOG" | sed 's/^[^ ]* //') \
        <(grep -E "UI check: push (built|before|pushed|after)" "$LOG2" | sed 's/^[^ ]* //') > /dev/null \
    && [ -n "$(grep 'UI check: push after 412 steps' "$LOG")" ]; then
    pass "the second run logged the same poses"
else
    fail "the two runs differ"
fi
snapshot > "$WORK/real-after.tsv"
cmp -s "$WORK/real-before.tsv" "$WORK/real-after.tsv" && pass "the real user folder is unchanged: $REAL" \
    || fail "the real user folder changed: $(diff "$WORK/real-before.tsv" "$WORK/real-after.tsv" | head -5)"
[ $FAILED = 0 ] && echo "PUSH SANDBOX CHECK PASSED" || echo "PUSH SANDBOX CHECK FAILED"
exit $FAILED
