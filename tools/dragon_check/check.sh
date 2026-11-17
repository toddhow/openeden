#!/bin/bash
# tools/dragon_check (task 0280, a dragon in flight survives a save and a load; derived from tools/skydeaths_check):
# SAVE phase: a Skyrim neutral session on the Whiterun plains (@Tamriel,17311,-17845) with the combat rules, the pack
# settings `creatures`, `dragons`, `combat ai` and the test aids that put a dragon in the place of the cow there and keep it
# in the air; at REPORT real seconds the global script logs it and a free camera takes a picture; the menu script saves
# ("dragon") and quits. LOAD phase: a new process loads that save, logs the same lines and takes a picture again.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [phase: both|save|load]
#
# env: REPORT (40), TAGS ("'b117251'"), CAM ("{900,-900,-300}": the camera's offset from the dragon), PACK_EXTRA,
#      GAME_EXTRA, AIR (1000), GROUND (3), DRAGON (EncDragon01Fire), FRAMES (30000), CHECK_TIMEOUT (300), LOADN (the log
#      name of the load phase). <work dir> must be under I:/Projects/OpenEden-work.
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
content=dragon.omwscripts
CFG
    printf '[Game]\nneutral world = true\nneutral saves = true\nneutral actors = true\nneutral combat = true\nneutral actor idles = true\nneutral actor locomotion = true\nneutral spell effects = true\n%b[GUI]\nrmlui = false\n[Video]\nresolution x = 1280\nresolution y = 720\nvsync mode = 0\n[Sound]\nmaster volume = 0.0\nmusic volume = 0.0\n[extension.skyrimse]\ncreatures = true\ndragons = true\ndragons flight = true\ndragons test place = %s\ndragons test replace = EncCow@17311,-17845\ndragons test ground seconds = %s\ndragons test air seconds = %s\ncombat ai = true\ncombat test hostile = %s\ncombat trace = true\n%b' "${GAME_EXTRA:-}" "${DRAGON:-EncDragon01Fire}" "${GROUND:-3}" "${AIR:-1000}" "${DRAGON:-EncDragon01Fire}" "${PACK_EXTRA:-}" > "$WORK/config/settings.cfg"
fi
export OPENEDEN_FIXED_FRAME_TIME=0.0166667
export OPENEDEN_NEUTRAL_ACTOR_TRACE=1
export OPENEDEN_SHADER_CACHE="$(native "$WORK/../shadercache")"
export OPENMW_DISABLE_CRASH_CATCHER=${OPENMW_DISABLE_CRASH_CATCHER:-1}
cfgfor() { # phase
    echo "return { phase = '$1', report = ${REPORT:-40}, activate = 9999, tags = { ${TAGS:-'b117251'} }, cam = ${CAM:-{900,-900,-300\}}, place = false, placeAt = 30, dist = 450 }" > "$WORK/luatest/scripts/drg/cfg.lua"
}
run() { # phase, log name, extra arguments
    cfgfor "$1"
    local log="$WORK/$2.txt"
    shift 2
    cd "$BUILD"
    set +e
    timeout "${CHECK_TIMEOUT:-300}" ./openmw.exe --replace=config --config "$(native "$WORK/config")" --user-data "$(native "$WORK/user")" \
        --no-grab=true --random-seed 7 "$@" --auto-quit "${FRAMES:-30000}" > "$log" 2>&1
    echo "exit $?" >> "$log"
    set -e
}
PATTERN="COMBAT dragon (actor [0-9]+ (at|is a dragon|breathes)|flight|lands|resumes|breath begin)|DRG|Writing saved game|dragons| E "
if [ "$ONLY" != load ]; then
    run save save --skip-menu --start "@Tamriel,17311,-17845"
    grep -E "$PATTERN" "$WORK/save.txt" | sed 's/^\[[^]]*\] //' | cut -c1-200 | tail -40
fi
if [ "$ONLY" != save ]; then
    SAVE=$(ls "$WORK"/user/saves/*/*.omwsave 2>/dev/null | head -1)
    [ -z "$SAVE" ] && { echo "no save file"; exit 1; }
    echo "save: $(stat -c %s "$SAVE") bytes"
    run load "${LOADN:-load}" --skip-menu --load-savegame "$(native "$SAVE")"
    grep -E "$PATTERN" "$WORK/${LOADN:-load}.txt" | sed 's/^\[[^]]*\] //' | cut -c1-200 | head -60
fi
echo "crash dumps: $(ls "$WORK"/user/*.dmp 2>/dev/null | wc -l)"
