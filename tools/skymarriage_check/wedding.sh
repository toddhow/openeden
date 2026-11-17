#!/bin/bash
# task 0276: the wedding scenario as it was run (the folders are that session work folders: change them). usage: wedding.sh <name> <input-file> [frames]; env PLAN (from wedding_input.py) FRIENDS SETTINGS_EXTRA ("marriage test fin stage = 10")
export PATH="/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin/Release:/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin:/i/Projects/OpenEden/deps/Qt/6.6.3/msvc2019_64/bin:$PATH"
export OPENEDEN_QUIET_WINDOW=1 OPENMW_DISABLE_CRASH_CATCHER=1
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
NAME=$1; export INPUT=$2; FRAMES=${3:-16000}
export CFG_EXTRA='fallback-archive=Skyrim - Voices_en0.bsa\nfallback-archive=Skyrim - Misc.bsa\n'
export FRIENDS=${FRIENDS:-13bab} GOLD=${GOLD:-500}
export SETTINGS_EXTRA="quest alias packages = true\n${SETTINGS_EXTRA:-}"
cd /i/Projects/OpenEden/.claude/worktrees/agent-a27975b18dfeb5f9d
bash tools/skymarriage_check/check.sh I:/Projects/OpenEden-work/combatai/build I:/Projects/OpenEden-work/integ2/base "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data" I:/Projects/OpenEden-work/combatai/marriage2/run_$NAME $FRAMES > /i/Projects/OpenEden-work/combatai/marriage2/run_$NAME.out 2>&1
echo "RUN_EXIT=$?" >> /i/Projects/OpenEden-work/combatai/marriage2/run_$NAME.out
