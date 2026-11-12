#!/bin/bash
# Task 0127 (TBD-E): the check of the Skyrim SE weather / climate / region weather decoding on the player's own masters.
# Reads the five official masters read only and prints AGGREGATE numbers only (no record name, id or path). Two parts:
#
#   1. an independent survey in Python (sizes.py, references.py, regions_worlds.py, cell_links.py; a separate parser, so
#      it does not share a bug with the C++ decoder). Set OPENEDEN_SKYRIMSE_DATA or pass the Data folder; needs python.
#   2. the gated C++ test SkyrimWeatherRealData (components-tests, or the tests.exe of a private link), which prints
#      lines starting SKYRIMSE-WEATHER and asserts the survey's numbers.
#
# usage: tools/skyrimse_weather_check/check.sh <game Data folder> [build folder]
#   Windows paths with forward slashes, for example "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data".
# Skipped, not failed, when the data is absent. Nothing is written.
set -u
DATA="${1:-${OPENEDEN_SKYRIMSE_DATA:-}}"
BUILD="${2:-$(dirname "$0")/../../build-jolt}"
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -z "$DATA" ] || [ ! -d "$DATA" ]; then
    echo "skyrimse_weather_check: skipped (no Data folder '${DATA}'; pass the game's Data folder)"
    exit 0
fi
export OPENEDEN_SKYRIMSE_DATA="$DATA"
if command -v python >/dev/null 2>&1; then
    # walk.py keeps the Data folder in D; the survey scripts read it from the environment through ids.py
    (cd "$HERE" && for t in WTHR CLMT REGN; do python sizes.py "$t"; done && python references.py && python regions_worlds.py && python cell_links.py)
else
    echo "skyrimse_weather_check: python not found, the independent survey is skipped"
fi
TESTS="$BUILD/components-tests"
[ -x "$TESTS" ] || TESTS="$BUILD/components-tests.exe"
[ -x "$TESTS" ] || TESTS="$BUILD/tests.exe"
if [ ! -x "$TESTS" ]; then
    echo "skyrimse_weather_check: components-tests not found in $BUILD"
    exit 1
fi
"$TESTS" --gtest_filter='SkyrimWeatherRealData.*' | grep -E "SKYRIMSE-WEATHER|PASSED|FAILED|SKIPPED"
