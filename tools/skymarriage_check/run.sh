#!/bin/bash
# usage: run.sh <name> <input-file|-> [frames] ; env PLAN, FRIENDS, CELLID, TARGET, GOLD, SETTINGS_EXTRA, PHASE
export PATH="/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin/Release:/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin:/i/Projects/OpenEden/deps/Qt/6.6.3/msvc2019_64/bin:$PATH"
export OPENEDEN_QUIET_WINDOW=1 OPENMW_DISABLE_CRASH_CATCHER=1
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
NAME=$1; [ "$2" != "-" ] && export INPUT=$2; FRAMES=${3:-4000}
export CFG_EXTRA='fallback-archive=Skyrim - Voices_en0.bsa\nfallback-archive=Skyrim - Misc.bsa\n'
cd /i/Projects/OpenEden/.claude/worktrees/agent-ad477b24858d028a8
bash tools/skymarriage_check/check.sh I:/Projects/OpenEden-work/combat5/build I:/Projects/OpenEden-work/integ2/base "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data" I:/Projects/OpenEden-work/combat5/marriage/run_$NAME $FRAMES > /i/Projects/OpenEden-work/combat5/marriage/run_$NAME.out 2>&1
echo "RUN_EXIT=$?" >> /i/Projects/OpenEden-work/combat5/marriage/run_$NAME.out
