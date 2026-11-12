#!/bin/bash
# Task 0116: the gated real-data Skyrim SE data survey (docs/research/0053-skyrim-rules-survey.md).
#
# Measures, on the player's own installed data, which pieces of Skyrim's rules real content uses most: condition
# functions (CTDA) by index, record type and parameter kind; the native Papyrus functions the game's scripts call and
# the events they handle; and (optional) the NIF block types of the five masters' meshes. Read only. Prints AGGREGATE
# lines only (starting SKYRIMSE-SURVEY): function indices, public Papyrus API names and NIF block type names; no record
# name, form id, script name, mesh path or mod path. Nothing is written. Skipped, not failed, when the data is absent.
#
# usage: tools/skyrimse_survey_check/check.sh <game Data folder> [parts] [build folder]
#   parts: comma list of conditions, papyrus, nif (default all). Windows paths with forward slashes work, for example
#   "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data". The build folder holds components-tests.
#   OPENEDEN_SKYRIMSE_SURVEY_NIF_MAX limits the NIF part to that many files.
set -u
DATA="${1:-${OPENEDEN_SKYRIMSE_DATA:-}}"
PARTS="${2:-${OPENEDEN_SKYRIMSE_SURVEY_PARTS:-}}"
BUILD="${3:-$(dirname "$0")/../../build-jolt}"
if [ -z "$DATA" ] || [ ! -d "$DATA" ]; then
    echo "skyrimse_survey_check: skipped (no Data folder '${DATA}'; pass the game's Data folder)"
    exit 0
fi
TESTS="$BUILD/components-tests"
[ -x "$TESTS" ] || TESTS="$BUILD/components-tests.exe"
[ -x "$TESTS" ] || TESTS="$BUILD/survey_real.exe"
if [ ! -x "$TESTS" ]; then
    echo "skyrimse_survey_check: components-tests not found in $BUILD"
    exit 1
fi
OPENEDEN_SKYRIMSE_DATA="$DATA" OPENEDEN_SKYRIMSE_SURVEY_PARTS="$PARTS" \
    "$TESTS" --gtest_filter='SkyrimSeSurveyReal.*' | grep -E "SKYRIMSE-SURVEY|PASSED|FAILED|SKIPPED"
