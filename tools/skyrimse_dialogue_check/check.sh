#!/bin/bash
# Task 0137, M-S4 slice 1: the gated real-data check of the Skyrim SE dialogue records and the response choice.
#
# Decodes DIAL, INFO, GLOB and FLST of the five official masters, merges them, and asks which Hello INFO a few
# Bannered Mare NPCs would get. Prints AGGREGATE numbers and official editor ids only (lines starting
# SKYRIMSE-DIALOGUE); never a response text or a path. Skipped, not failed, when the data is absent.
#
# usage: tools/skyrimse_dialogue_check/check.sh <game Data folder> [build folder]
set -u
DATA="${1:-${OPENEDEN_SKYRIMSE_DATA:-}}"
BUILD="${2:-$(dirname "$0")/../../build-jolt}"
if [ -z "$DATA" ] || [ ! -d "$DATA" ]; then
    echo "skyrimse_dialogue_check: skipped (no Data folder '${DATA}')"
    exit 0
fi
TESTS="$BUILD/components-tests"
[ -x "$TESTS" ] || TESTS="$BUILD/components-tests.exe"
if [ ! -x "$TESTS" ]; then
    echo "skyrimse_dialogue_check: components-tests not found in $BUILD"
    exit 1
fi
OPENEDEN_SKYRIMSE_DATA="$DATA" "$TESTS" --gtest_filter='SkyrimDialogueRealData.*' \
    | grep -E "SKYRIMSE-DIALOGUE|PASSED|FAILED|SKIPPED"
