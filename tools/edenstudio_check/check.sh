#!/bin/bash
# Smoke check of Eden Studio's start-up with an isolated configuration: it must honour --replace=config, --config and
# --user-data and leave the real user folder alone. Until 2026-11-04 the editor never parsed its command line (upstream
# OpenMW-CS removed it in 2013, commit 4e26a61db3), so a check run read the real openmw.cfg and wrote eden-studio.log
# and eden-studio-layout.ini into the user's own folder (apps/opencs/CLAUDE.md, pitfalls).
#
# Usage: tools/edenstudio_check/check.sh <build dir> <openmw.cfg with your data= and content= lines> <content file>
#                                         <empty work dir> [<real user config folder to watch>]
# On Windows, PATH must already find the editor's runtime libraries, Qt's bin included. The cfg file is copied; never
# pass your real config folder. The watched folder defaults to Documents/My Games/OpenMW (on Windows, OneDrive's
# Documents when it exists); it is only listed, never written.
#
# Steps, each a pass/fail line:
#   1. an unknown option exits with code 2 and a message on stderr, before any window
#   2. --help exits with code 0
#   3. the content file opens with OPENEDEN_CS_UI_CHECK (a scene view, then every window closes) and exits with 0;
#      the log and the layout file are in the work folder's config
#   4. the watched folder's files (names, sizes, times) are the same before and after
#   5. a legacy openmw-cs.cfg in the config folder is copied to eden-studio.cfg on the first start (step 3's run)

set -u
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
CFG=$2
CONTENT=$3
WORK=$4
FAILED=0

[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "FAIL: $WORK is not empty"; exit 1; }
WORKW=$(cd "$WORK" && { pwd -W 2>/dev/null || pwd; })

if [ $# -ge 5 ]; then
    REAL=$5
elif [ -d "${USERPROFILE:-}/OneDrive/Documents/My Games/OpenMW" ]; then
    REAL="$USERPROFILE/OneDrive/Documents/My Games/OpenMW"
elif [ -d "${USERPROFILE:-}/Documents/My Games/OpenMW" ]; then
    REAL="$USERPROFILE/Documents/My Games/OpenMW"
else
    REAL="$HOME/.config/openmw"
fi

pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; FAILED=1; }
snapshot() { find "$REAL" -printf '%P\t%s\t%T@\n' 2>/dev/null | sort; }

mkdir -p "$WORK/config" "$WORK/user"
tr -d '\r' < "$CFG" > "$WORK/config/openmw.cfg"
printf '[Windows]\ndefault-width = 1234\n' > "$WORK/config/openmw-cs.cfg"
snapshot > "$WORK/real-before.tsv"

(cd "$BUILD" && timeout 60 ./eden-studio.exe --frobnicate 3 > "$WORK/unknown.out" 2> "$WORK/unknown.err")
rc=$?
if [ $rc = 2 ] && grep -q "frobnicate" "$WORK/unknown.err"; then pass "unknown option: exit 2 with a message"
else fail "unknown option: exit $rc, stderr: $(head -c 200 "$WORK/unknown.err")"; fi

(cd "$BUILD" && timeout 60 ./eden-studio.exe --help > "$WORK/help.out" 2>&1)
rc=$?
if [ $rc = 0 ] && grep -q "replace" "$WORK/help.out"; then pass "--help: exit 0"
else fail "--help: exit $rc"; fi

(cd "$BUILD" && OPENEDEN_CS_UI_CHECK="c:#-2 -9;3000" timeout 300 ./eden-studio.exe --replace=config \
    --config "$WORKW/config" --user-data "$WORKW/user" -- "$CONTENT" > "$WORK/run.out" 2>&1)
rc=$?
if [ $rc = 0 ] && grep -q "UI check: closing" "$WORK/config/eden-studio.log" 2>/dev/null \
    && [ -f "$WORK/config/eden-studio-layout.ini" ]; then
    pass "UI check: exit 0, log and layout in the work folder"
else
    fail "UI check: exit $rc (see $WORK/run.out and $WORK/config/eden-studio.log)"
fi

snapshot > "$WORK/real-after.tsv"
if cmp -s "$WORK/real-before.tsv" "$WORK/real-after.tsv"; then pass "the watched folder is unchanged: $REAL"
else fail "the watched folder changed: $(diff "$WORK/real-before.tsv" "$WORK/real-after.tsv" | head -5)"; fi

if grep -q "default-width = 1234" "$WORK/config/eden-studio.cfg" 2>/dev/null; then pass "openmw-cs.cfg migrated"
else fail "openmw-cs.cfg was not copied to eden-studio.cfg"; fi

[ $FAILED = 0 ] && echo "EDENSTUDIO CHECK PASSED" || echo "EDENSTUDIO CHECK FAILED"
exit $FAILED
