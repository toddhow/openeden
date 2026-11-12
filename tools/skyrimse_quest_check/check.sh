#!/bin/bash
# Task 0139, M-S4 slice 2: the gated real-data check of the Skyrim SE quest records, the initial quest state and its effect on the Hello choice.
#
# Decodes QUST (and the dialogue) of the five official masters, builds the initial quest state, and compares which Hello INFO a few
# Bannered Mare NPCs get with and without it. Prints AGGREGATE numbers and official editor ids only (lines starting
# SKYRIMSE-QUEST); never a response text or a path. Skipped, not failed, when the data is absent.
#
# usage: tools/skyrimse_quest_check/check.sh <game Data folder> [build folder]
set -u
DATA="${1:-${OPENEDEN_SKYRIMSE_DATA:-}}"
BUILD="${2:-$(dirname "$0")/../../build-jolt}"
if [ -z "$DATA" ] || [ ! -d "$DATA" ]; then
    echo "skyrimse_quest_check: skipped (no Data folder '${DATA}')"
    exit 0
fi
TESTS="$BUILD/components-tests"
[ -x "$TESTS" ] || TESTS="$BUILD/components-tests.exe"
if [ ! -x "$TESTS" ]; then
    echo "skyrimse_quest_check: components-tests not found in $BUILD"
    exit 1
fi
OPENEDEN_SKYRIMSE_DATA="$DATA" "$TESTS" --gtest_filter='SkyrimQuestRealData.*' \
    | grep -E "SKYRIMSE-QUEST|PASSED|FAILED|SKIPPED"
