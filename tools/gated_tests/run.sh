#!/bin/bash
# Task 0309: run the tests that are gated on real game data, which a normal test run skips.
#
# Every one of these tests calls GTEST_SKIP() unless an environment variable names the data, so the merge test run
# never exercises them and a regression in what they pin goes unseen. This script sets every variable it can and runs
# components-tests and openmw-tests with the survey tests (names ending "when_asked": long scans that only print
# aggregates) and the slow Slang, terrain-complement and property groups left out. Read only. Prints the gtest lines
# only; game data is never printed or written.
#
# usage: tools/gated_tests/run.sh <build folder> [work folder]
#   <build folder>  holds components-tests and openmw-tests (and game_packs/skyrimse/skyrimse.dll)
#   [work folder]   where the extracted animation files go (default $OPENEDEN_GATED_WORK or
#                   I:/Projects/OpenEden-work/gated); about 0.3 GB, created once and kept
#
# Data it looks for (each missing one makes the tests that need it skip, and says so):
#   OPENEDEN_SKYRIMSE_DATA   the Skyrim SE Data folder (default: the Steam library on I:)
#   OPENEDEN_FNV_DATA        the Fallout New Vegas Data folder (default: the Steam library on I:)
#   OPENEDEN_TEST_MORROWIND_DATA / OPENEDEN_MORROWIND_DATA / OPENEDEN_NIF_MORROWIND_DIR: a Morrowind install (not on
#                            this machine; set them to run those tests)
# The Havok and animation-data tests need "Skyrim - Animations.bsa" extracted. The script does not extract it (no
# archive tool is guaranteed to be built): extract the archive into <work>/full (for example with the game's
# `bsatool extract`, or housecarl_bsa_extract), then run once with EXTRACT_HKX=1 to copy the .hkx files into <work>/hkx.
# Careful: some archive tools leave a few small text files as raw LZ4 frames (magic 04 22 4D 18); decompress those.
set -u
BUILD="${1:?usage: run.sh <build folder> [work folder]}"
WORK="${2:-${OPENEDEN_GATED_WORK:-I:/Projects/OpenEden-work/gated}}"
FULL="$WORK/full"
HKX="$WORK/hkx"
SKY="${OPENEDEN_SKYRIMSE_DATA:-I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data}"
FNV="${OPENEDEN_FNV_DATA:-I:/SteamLibrary/steamapps/common/Fallout New Vegas/Data}"

if [ "${EXTRACT_HKX:-0}" = 1 ] && [ -d "$FULL/meshes" ] && [ ! -d "$HKX/meshes" ]; then
    mkdir -p "$HKX"
    (cd "$FULL" && find . -name "*.hkx" | tar -cf - -T -) | tar -xf - -C "$HKX"
fi

export OPENEDEN_QUIET_WINDOW=1
[ -d "$SKY" ] && export OPENEDEN_SKYRIMSE_DATA="$SKY" && export OPENEDEN_BSA_ENTRY_CHECK_DIRS="$SKY"
[ -d "$FNV" ] && export OPENEDEN_FNV_DATA="$FNV"
if [ -d "$FULL/meshes" ]; then
    export OPENEDEN_PAIRED_ROOT="$FULL" OPENEDEN_HAVOK_IDLE_ROOT="$FULL" OPENEDEN_SKYRIMSE_ANIMDATA_DIR="$FULL"
    export OPENEDEN_HAVOK_ANIMDATA_DIR="$FULL/meshes/animationdata"
fi
[ -d "$HKX/meshes" ] && export OPENEDEN_HAVOK_HKX_DIR="$HKX"
DLL="$BUILD/game_packs/skyrimse/skyrimse.dll"
[ -f "$DLL" ] && export OPENEDEN_SKYRIMSE_PACK_DLL="$DLL"
# the sampling survey tests that are not "when_asked" ones get a cap, so the run takes minutes and not hours
export OPENEDEN_BHK_SURVEY_STRIDE=25 OPENEDEN_SKYMAT_SURVEY_MAX=2000 OPENEDEN_SKYRIMSE_SURVEY_NIF_MAX=2000

for v in OPENEDEN_SKYRIMSE_DATA OPENEDEN_FNV_DATA OPENEDEN_PAIRED_ROOT OPENEDEN_HAVOK_HKX_DIR OPENEDEN_HAVOK_ANIMDATA_DIR \
    OPENEDEN_SKYRIMSE_PACK_DLL OPENEDEN_TEST_MORROWIND_DATA OPENEDEN_MORROWIND_DATA OPENEDEN_NIF_MORROWIND_DIR; do
    if [ -n "${!v:-}" ]; then echo "gated: $v set"; else echo "gated: $v NOT set (its tests will skip)"; fi
done

status=0
for exe in components-tests openmw-tests; do
    t="$BUILD/$exe"
    [ -x "$t" ] || t="$BUILD/$exe.exe"
    if [ ! -x "$t" ]; then echo "gated: $exe not found in $BUILD"; status=1; continue; fi
    "$t" --gtest_filter='-*Slang*:*Complement*:*Property*:*when_asked*' 2>&1 \
        | grep --line-buffered -E '^\[|error:|Which is|Expected' | tee "$WORK/$exe.log" \
        | grep -E 'FAILED|PASSED|SKIPPED|tests ran' || true
    grep -q '^\[  FAILED' "$WORK/$exe.log" && status=1
done
echo "gated: every test marked SKIPPED above needed data that was not set"
exit $status
