#!/bin/bash
# Task 0117, stage E0: the gated real-data check of the Skyrim SE exterior decoding (LAND, LTEX, TXST, WRLD, WATR, exterior
# cell water). Reads the five official masters read only and prints AGGREGATE numbers only (lines starting
# SKYRIMSE-LAND): the shared-edge study that settles the height orientation, the quadrant and VTXT conventions, the PNAM
# bits, the XCLW sentinels, the null texture layers and Solstheim's LAND. Nothing is written. Skipped, not failed, when
# the data is absent.
#
# usage: tools/skyrimse_land_check/check.sh <game Data folder> [build folder]
#   Windows paths with forward slashes, for example "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data".
#   The build folder holds components-tests (or the tests.exe of a private link).
set -u
DATA="${1:-${OPENEDEN_SKYRIMSE_DATA:-}}"
BUILD="${2:-$(dirname "$0")/../../build-jolt}"
if [ -z "$DATA" ] || [ ! -d "$DATA" ]; then
    echo "skyrimse_land_check: skipped (no Data folder '${DATA}'; pass the game's Data folder)"
    exit 0
fi
TESTS="$BUILD/components-tests"
[ -x "$TESTS" ] || TESTS="$BUILD/components-tests.exe"
[ -x "$TESTS" ] || TESTS="$BUILD/rig.exe"
if [ ! -x "$TESTS" ]; then
    echo "skyrimse_land_check: components-tests not found in $BUILD"
    exit 1
fi
OPENEDEN_SKYRIMSE_DATA="$DATA" "$TESTS" --gtest_filter='SkyrimSeLandRealData.*' | grep -E "SKYRIMSE-LAND|PASSED|FAILED|SKIPPED"
