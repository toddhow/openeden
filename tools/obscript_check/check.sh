#!/bin/bash
# Task 0092 stage e: the gated real-data check of the ObScript runtime.
#
# Compiles every script and script fragment of the player's own Fallout New Vegas masters (read only) in tolerant mode
# and prints AGGREGATE numbers only (lines starting OBSCRIPT-CHECK): scripts and fragments compiled, failures by
# kind, the number of different unknown function names, and how many scripts have the same Begin ... End block count
# in their compiled bytes (SCDA) as in the recompiled source. Nothing from the masters is written anywhere.
# The check is skipped, not failed, when the data is absent.
#
# usage: tools/obscript_check/check.sh <Fallout New Vegas Data folder> [build folder holding components-tests]
#   Windows paths with forward slashes work, for example "I:/SteamLibrary/steamapps/common/Fallout New Vegas/Data".
set -u
DATA="${1:-${OPENEDEN_FNV_DATA:-}}"
BUILD="${2:-$(dirname "$0")/../../build-jolt}"
if [ -z "$DATA" ] || [ ! -f "$DATA/FalloutNV.esm" ]; then
    echo "obscript_check: skipped (no FalloutNV.esm in '${DATA}'; pass the game's Data folder)"
    exit 0
fi
TESTS="$BUILD/components-tests"
[ -x "$TESTS" ] || TESTS="$BUILD/components-tests.exe"
if [ ! -x "$TESTS" ]; then
    echo "obscript_check: components-tests not found in $BUILD"
    exit 1
fi
OPENEDEN_FNV_DATA="$DATA" "$TESTS" --gtest_filter='ObScriptRealData.*' | grep -E "OBSCRIPT-CHECK|PASSED|FAILED|SKIPPED"

# Second check (item 2 of task 0092): derive the function table from the same data and print aggregate numbers only
# (OBSCRIPT-TABLE lines). Set OPENEDEN_OBSCRIPT_TABLE_OUT to a file OUTSIDE the repository to keep the derived table
# there; it is made from the player's data and is never committed or shipped. OPENEDEN_OBSCRIPT_SHAPES=1 adds the
# shapes (token classes, no words) of the lines that still fail.
OPENEDEN_FNV_DATA="$DATA" "$TESTS" --gtest_filter='ObScriptTableData.*' | grep -E "OBSCRIPT-TABLE|PASSED|FAILED|SKIPPED"
