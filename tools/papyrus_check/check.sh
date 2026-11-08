#!/bin/bash
# Task 0093: the gated real-data check of the Papyrus .pex reader.
#
# Reads every compiled script of the player's own Skyrim Special Edition or Fallout 4 (the loose ones in the game's
# Scripts folder and the ones inside the game's archives), and optionally every script of a folder of mod folders (for
# example Mod Organizer 2's "mods" folder), read only. Prints AGGREGATE numbers only (lines starting PAPYRUS-CHECK):
# files read, accepted and refused (and why), versions, the object size conventions seen, and how many calls between
# the game's scripts the loaded set resolves. Nothing from the game is written anywhere. Skipped, not failed, when the
# data is absent.
#
# Stage c adds the VM run (lines starting PAPYRUS-RUN): a fixed sample of the game's own scripts (200 for Skyrim SE, 100
# for Fallout 4, chosen by a stable hash of the script name; OPENEDEN_PAPYRUS_SAMPLE changes the size, 100000 runs them
# all) is attached and run against a stub world with recorders for every native, with none, and with only the 106 most
# called natives; OPENEDEN_PAPYRUS_VERBOSE=1 also names the scripts that fail.
#
# usage: tools/papyrus_check/check.sh <game Data folder> [mods folder] [build folder holding components-tests]
#   Windows paths with forward slashes work, for example "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data".
set -u
DATA="${1:-${OPENEDEN_PAPYRUS_DATA:-}}"
MODS="${2:-${OPENEDEN_PAPYRUS_MODS:-}}"
BUILD="${3:-$(dirname "$0")/../../build-jolt}"
if [ -z "$DATA" ] || [ ! -d "$DATA" ]; then
    echo "papyrus_check: skipped (no Data folder '${DATA}'; pass the game's Data folder)"
    exit 0
fi
TESTS="$BUILD/components-tests"
[ -x "$TESTS" ] || TESTS="$BUILD/components-tests.exe"
if [ ! -x "$TESTS" ]; then
    echo "papyrus_check: components-tests not found in $BUILD"
    exit 1
fi
OPENEDEN_PAPYRUS_DATA="$DATA" OPENEDEN_PAPYRUS_MODS="$MODS" "$TESTS" --gtest_filter='PapyrusRealData.*:PapyrusRealRun.*' | grep -E "PAPYRUS-CHECK|PAPYRUS-RUN|PASSED|FAILED|SKIPPED"
