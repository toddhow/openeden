#!/bin/bash
# usage: run349.sh <tag> <frames> ; env STEPS, ACTIVATE, INPUT, LOADARG, SETTINGS_EXTRA
export PATH="/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin/Release:/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin:/i/Projects/OpenEden/deps/Qt/6.6.3/msvc2019_64/bin:$PATH"
export OPENEDEN_PAPYRUS_NATIVE_TRACE=${PNTRACE:-} OPENEDEN_NEUTRAL_ACTOR_TRACE=${NATRACE:-} OPENEDEN_PACKAGE_TRACE=${PKTRACE:-} OPENEDEN_SCENE_WAIT=1 OPENEDEN_QUIET_WINDOW=1 OPENMW_DISABLE_CRASH_CATCHER=1 OPENEDEN_FIXED_FRAME_TIME=0.0166667
W=/i/Projects/OpenEden/.claude/worktrees/agent-a0500476f850c8134
export SETTINGS_EXTRA="[extension.skyrimse]

player level = ${LEVEL:-15}
blade in the dark = true
${SETTINGS_EXTRA:-}"
export LOADARG="${LOADARG:---start RiverwoodSleepingGiantInn}"
CHECK_TIMEOUT=${CHECK_TIMEOUT:-900} bash $W/tools/skyblade_check/check.sh I:/Projects/OpenEden-work/papyrus/build I:/Projects/OpenEden-work/integ2/base "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data" I:/Projects/OpenEden-work/papyrus/r349/$1 ${2:-3000} > I:/Projects/OpenEden-work/papyrus/r349/$1.out 2>&1
tail -3 I:/Projects/OpenEden-work/papyrus/r349/$1.out
