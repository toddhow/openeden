#!/bin/bash
# tools/skydb_check (task 0294): usage run.sh <name> <input-file|-> [frames]; env PLAN, CELLID (default WindhelmAretinoResidence),
# TARGET (record id of the actor to talk to, default Aventus skyrimse:b82226), SETTINGS_EXTRA, BUILD.
# Wraps tools/skymarriage_check/check.sh (plan steps: talk, cell, go; MRG trace lines).
V="/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows"
export PATH="$V/bin/Release:$V/bin:/i/Projects/OpenEden/deps/Qt/6.6.3/msvc2019_64/bin:$PATH"
export OPENEDEN_QUIET_WINDOW=1 OPENMW_DISABLE_CRASH_CATCHER=1 OPENEDEN_FIXED_FRAME_TIME=0.0166667
NAME=$1; [ "$2" != "-" ] && export INPUT=$2; FRAMES=${3:-5000}
export CFG_EXTRA='fallback-archive=Skyrim - Voices_en0.bsa\nfallback-archive=Skyrim - Misc.bsa\n'
export CELLID=${CELLID:-WindhelmAretinoResidence} TARGET=${TARGET:-skyrimse:b82226}
export SETTINGS_EXTRA="[GUI]
controller menus = true
[extension.skyrimse]
${SETTINGS_EXTRA:-}"
HERE=$(cd "$(dirname "$0")" && pwd)
W=I:/Projects/OpenEden-work/ragdoll5/db; mkdir -p $W
bash "$HERE/check.sh" ${BUILD:-I:/Projects/OpenEden-work/ragdoll5/build} I:/Projects/OpenEden-work/integ2/base "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data" $W/run_$NAME $FRAMES > $W/run_$NAME.out 2>&1
echo "RUN_EXIT=$?" >> $W/run_$NAME.out
