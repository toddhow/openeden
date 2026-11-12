#!/bin/bash
# Task 0118, stage N5-0 of decision 0017: the gated real-data check of the Skyrim SE actor and appearance records.
#
# Parses NPC_, RACE, ARMA, ARMO (armature list and slot mask), OTFT, HDPT and CLAS in the player's own Skyrim Special
# Edition masters (read only), checks that every reference resolves to the record type it should, follows every NPC
# template chain (cycles, depth), and checks that every NPC's race has a skeleton model and that the models of every
# default outfit exist in the archives. Prints AGGREGATE numbers only (lines starting SKYRIMSE-ACTORS). No record name,
# id or mod path is printed and nothing is written. Skipped, not failed, when the data is absent.
#
# usage: tools/skyrimse_actors_check/check.sh <game Data folder> [build folder]
#   Windows paths with forward slashes, for example "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data".
#   The build folder holds components-tests (or tests.exe of a private link).
set -u
DATA="${1:-${OPENEDEN_SKYRIMSE_DATA:-}}"
BUILD="${2:-$(dirname "$0")/../../build-jolt}"
if [ -z "$DATA" ] || [ ! -d "$DATA" ]; then
    echo "skyrimse_actors_check: skipped (no Data folder '${DATA}'; pass the game's Data folder)"
    exit 0
fi
TESTS="$BUILD/components-tests"
[ -x "$TESTS" ] || TESTS="$BUILD/components-tests.exe"
[ -x "$TESTS" ] || TESTS="$BUILD/tests.exe"
if [ ! -x "$TESTS" ]; then
    echo "skyrimse_actors_check: components-tests not found in $BUILD"
    exit 1
fi
OPENEDEN_SKYRIMSE_DATA="$DATA" "$TESTS" --gtest_filter='SkyrimActorsRealData.*' | grep -E "SKYRIMSE-ACTORS|PASSED|FAILED|SKIPPED"
