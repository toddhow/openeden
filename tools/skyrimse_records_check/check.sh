#!/bin/bash
# Task 0086, record loading first slice: the gated real-data check of the Skyrim SE record model.
#
# Parses the records the model decodes (WRLD, CELL with its group structure, REFR, ACHR, STAT, NPC_, ARMO, WEAP, MISC,
# LVLI, LVLN) in the player's own Skyrim Special Edition masters, and optionally in every plugin of a Mod Organizer 2
# profile's loadorder.txt, read only. Prints AGGREGATE numbers only (lines starting SKYRIMSE-RECORDS): per type records
# seen, parsed, failed and deleted, subrecords whose size did not fit the layout, the group-label and form id layout
# checks, and how many localised names are found in the string tables of the official masters. No record name, id or
# mod path is printed and nothing is written. Skipped, not failed, when the data is absent.
#
# usage: tools/skyrimse_records_check/check.sh <game Data folder> [MO2 instance folder] [profile] [build folder]
#   Windows paths with forward slashes, for example "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data"
#   and "I:/Instances/Skyrim Special Edition". The build folder holds components-tests (or tests.exe of a private link).
set -u
DATA="${1:-${OPENEDEN_SKYRIMSE_DATA:-}}"
MO2="${2:-${OPENEDEN_SKYRIMSE_MO2:-}}"
PROFILE="${3:-${OPENEDEN_SKYRIMSE_PROFILE:-Default}}"
BUILD="${4:-$(dirname "$0")/../../build-jolt}"
if [ -z "$DATA" ] || [ ! -d "$DATA" ]; then
    echo "skyrimse_records_check: skipped (no Data folder '${DATA}'; pass the game's Data folder)"
    exit 0
fi
TESTS="$BUILD/components-tests"
[ -x "$TESTS" ] || TESTS="$BUILD/components-tests.exe"
[ -x "$TESTS" ] || TESTS="$BUILD/tests.exe"
if [ ! -x "$TESTS" ]; then
    echo "skyrimse_records_check: components-tests not found in $BUILD"
    exit 1
fi
OPENEDEN_SKYRIMSE_DATA="$DATA" OPENEDEN_SKYRIMSE_MO2="$MO2" OPENEDEN_SKYRIMSE_PROFILE="$PROFILE" \
    "$TESTS" --gtest_filter='SkyrimSeRecordsRealData.*' | grep -E "SKYRIMSE-RECORDS|PASSED|FAILED|SKIPPED"
