#!/bin/bash
# tools/journal_check (task 0185, milestone M-S6): starts a Skyrim neutral session in the Bannered Mare with the quest journal
# switched on ([extension.skyrimse] journal and papyrus, [GUI] rmlui), so the start-up quests run and set stages, and
# runs the real input script (INPUT: J, mouse clicks and screenshots) through OPENEDEN_RMLUI_INPUT_SCRIPT. Prints the
# JOURNAL and PAPYRUS lines of the log. The test game opens off-screen (OPENEDEN_QUIET_WINDOW).
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: INPUT (the input script), SETTINGS_EXTRA (more settings text), WIDTH and HEIGHT (default 1280 and 720).
# <work dir> must be under I:/Projects/OpenEden-work.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-3000}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user"
B=$(native "$BUILD")
grep -vE '^(content|data-local|resources|extension|user-data|config)=|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
cat >> "$WORK/config/openmw.cfg" <<CFG
resources=$B/resources
data="$B/resources/vfs-mw"
data="$(native "$SKYRIM")"
game=skyrimse
fallback-archive=Skyrim - Animations.bsa
fallback-archive=Skyrim - Interface.bsa
fallback-archive=Skyrim - Meshes0.bsa
fallback-archive=Skyrim - Meshes1.bsa
fallback-archive=Skyrim - Misc.bsa
fallback-archive=Skyrim - Textures0.bsa
fallback-archive=Skyrim - Textures1.bsa
fallback-archive=Skyrim - Textures2.bsa
fallback-archive=Skyrim - Textures3.bsa
fallback-archive=Skyrim - Textures4.bsa
fallback-archive=Skyrim - Textures5.bsa
fallback-archive=Skyrim - Textures6.bsa
fallback-archive=Skyrim - Textures7.bsa
fallback-archive=Skyrim - Textures8.bsa
content=Skyrim.esm
content=Update.esm
content=Dawnguard.esm
content=HearthFires.esm
content=Dragonborn.esm
CFG
printf '[Game]\nneutral world = true\nneutral actors = true\n[GUI]\nrmlui = true\nrmlui debug panel = false\n[Video]\nresolution x = %s\nresolution y = %s\nvsync mode = 0\n[extension.skyrimse]\npapyrus = true\njournal = true\n' "${WIDTH:-1280}" "${HEIGHT:-720}" > "$WORK/config/settings.cfg"
[ -n "${SETTINGS_EXTRA:-}" ] && printf '%b' "$SETTINGS_EXTRA" >> "$WORK/config/settings.cfg"
[ -n "${INPUT:-}" ] && export OPENEDEN_RMLUI_INPUT_SCRIPT="$(native "$INPUT")"
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
export OPENEDEN_SHADER_CACHE="$(native "$WORK/../shadercache")"
export OPENMW_DISABLE_CRASH_CATCHER=${OPENMW_DISABLE_CRASH_CATCHER:-1}
cd "$BUILD"
set +e
timeout "${CHECK_TIMEOUT:-300}" ./openmw.exe --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --no-grab=true --random-seed 7 ${LOADARG:---start WhiterunBanneredMare} \
    --auto-quit "$FRAMES" > "$WORK/log.txt" 2>&1
code=$?
set -e
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
grep -E "JOURNAL|PAPYRUS started|PAPYRUS summary|\[UI\]|journal|requires" "$WORK/log.txt" | sed 's/^\[[^]]*\] //' || true
errors=$(grep -E " E\] " "$WORK/log.txt" | grep -vc "Saving refused" || true)
echo "exit code $code, $dumps crash dumps, $errors error lines, $(ls "$WORK"/user/screenshots 2>/dev/null | wc -l) screenshots"
