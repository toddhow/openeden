#!/bin/bash
# tools/skydeaths_check (task 0254, dead stays dead across a load; derived from tools/neutral_session_check/saveload.sh):
# SAVE phase: a Skyrim neutral session in the Bannered Mare with the combat rules and the pack setting `summons test kill = Hulda`
# (the test aid that kills her 3 combat seconds after the data is read); at REPORT real seconds the global script logs her
# and a free camera takes a picture; the menu script saves ("deaths") and quits. LOAD phase: a new process loads that save,
# logs the same lines and, at ACTIVATE real seconds, activates her (a dead actor opens its loot window); pictures follow.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [phase: both|save|load]
#
# env: REPORT (60), ACTIVATE (62), TAGS ("'b80803'"), CAM ("{170,-170,110}"), PACK_EXTRA, GAME_EXTRA, PHYS_EXTRA, KILL (Hulda),
#      PLACE/PLACEAT/DIST (the raise scenario: the player is put DIST units east of the first actor), INPUT (an input script),
#      PACKAGES (true), FRAMES (30000), CHECK_TIMEOUT (300), LOADN (load: the log name of the load phase).
#      <work dir> must be under I:/Projects/OpenEden-work.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; ONLY=${5:-both}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
B=$(native "$BUILD")
if [ "$ONLY" != load ]; then
    rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
    cp -r "$HERE/luatest/." "$WORK/luatest/"
    grep -vE '^(content|data-local|resources|extension|user-data|config)=|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
    cat >> "$WORK/config/openmw.cfg" <<CFG
resources=$B/resources
data="$B/resources/vfs-mw"
data="$(native "$SKYRIM")"
data="$(native "$WORK/luatest")"
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
content=deaths.omwscripts
CFG
    printf '[Game]\nneutral world = true\nneutral saves = true\nneutral actors = true\nneutral combat = true\nneutral actor idles = true\nneutral inventory = true\n%b[GUI]\nrmlui = true\nrmlui debug panel = false\n[Video]\nresolution x = 1280\nresolution y = 720\nvsync mode = 0\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n%b[extension.skyrimse]\ncombat spell delivery = true\nsummons = true\nsummons test kill = %s\ninventory = true\nactor packages = %s\ncombat trace = true\n%b' "${GAME_EXTRA:-}" "${PHYS_EXTRA:-}" "${KILL-Hulda}" "${PACKAGES:-true}" "${PACK_EXTRA:-}" > "$WORK/config/settings.cfg"
fi
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
export OPENEDEN_NEUTRAL_ACTOR_TRACE=1
export OPENEDEN_SHADER_CACHE="$(native "$WORK/../shadercache")"
export OPENMW_DISABLE_CRASH_CATCHER=${OPENMW_DISABLE_CRASH_CATCHER:-1}
cfgfor() { # phase
    echo "return { phase = '$1', report = ${REPORT:-60}, activate = ${ACTIVATE:-62}, tags = { ${TAGS:-'b80803'} }, cam = ${CAM:-{170,-170,110\}}, place = ${PLACE:-false}, placeAt = ${PLACEAT:-30}, dist = ${DIST:-450} }" > "$WORK/luatest/scripts/dth/cfg.lua"
}
run() { # phase, log name, extra arguments
    cfgfor "$1"
    unset OPENEDEN_RMLUI_INPUT_SCRIPT
    # the input script (INPUT) belongs to the SAVE phase: the load phase only looks
    if [ "$1" = save ] && [ -n "${INPUT:-}" ]; then export OPENEDEN_RMLUI_INPUT_SCRIPT="$(native "$INPUT")"; fi
    local log="$WORK/$2.txt"
    shift 2
    cd "$BUILD"
    set +e
    timeout "${CHECK_TIMEOUT:-300}" ./openmw.exe --replace=config --config "$(native "$WORK/config")" --user-data "$(native "$WORK/user")" \
        --no-grab=true --random-seed 7 "$@" --auto-quit "${FRAMES:-30000}" > "$log" 2>&1
    echo "exit $?" >> "$log"
    set -e
}
if [ "$ONLY" != load ]; then
    run save save --skip-menu --start WhiterunBanneredMare
    grep -E "COMBAT death|DEATHS|DTH|Writing saved game|COMBAT restored" "$WORK/save.txt" | sed 's/^\[[^]]*\] //' | cut -c1-200
fi
if [ "$ONLY" != save ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -z "$SAVE" ] && { echo "no save file"; exit 1; }
    echo "save: $(stat -c %s "$SAVE") bytes"
    run load "${LOADN:-load}" --skip-menu --load-savegame "$(native "$SAVE")"
    grep -E "COMBAT death|DEATHS|DTH|COMBAT restored|agdoll|ontainer|loot| E\] " "$WORK/${LOADN:-load}.txt" | sed 's/^\[[^]]*\] //' | cut -c1-200
fi
echo "crash dumps: $(ls "$WORK"/user/*.dmp 2>/dev/null | wc -l)"
