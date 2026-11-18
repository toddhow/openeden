#!/bin/bash
# tools/skyambush_check (task 0302): Bleak Falls Barrow's draugr ambush through the game's own scripts. Two draugr
# (placements 229555 and 229557, masterAmbushScript, linked to CreatureAlcoveBgMarker furniture) lie in their alcoves at
# (8844, -5257, -2049) and (8842, -4226, -2047). The player is put far from them, waits until they lie down, then is put
# within the wake distance and the plan takes pictures from a free camera. Runs tools/skydungeon_check with the ambush
# settings; prints the AMBUSH, NACTFURN, COMBAT ai and PAPYRUS event lines.
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
#
# env: NEAR_AT (seconds the player is put near, default 200), FAR ('x y z' of the far place), NEAR ('x y z'),
#      SETTINGS_EXTRA, CHECK_TIMEOUT. <work dir> must be under I:/Projects/OpenEden-work.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FRAMES=${5:-16000}
T=${NEAR_AT:-200}
NEAR=${NEAR:-"8700 -4350 -2040"}
# the player starts at the cell's door (far from the beds) and is put near at T, facing the bed of 229557; a picture
# from the player's eye every 0.4 s from T + 0.3 (shot times are seconds of simulation)
shots=""
# PROBE='x y z;x y z;...' takes one picture from each place at T - 20 + 2 i (before the wake), looking at the bed
if [ -n "${PROBE:-}" ]; then
    i=0
    IFS=';' read -ra places <<< "$PROBE"
    for place in "${places[@]}"; do
        t=$((T - 40 + 4 * i)); i=$((i + 1))
        shots="$shots {$t,'shot',${place// /,},8842,-4226,-2035},"
    done
fi
for i in $(seq 0 19); do
    t=$(awk -v T=$T -v i=$i 'BEGIN{printf "%.2f", T + 0.3 + 0.4 * i}')
    shots="$shots {$t,'shot',8700,-4350,-1985,8842,-4226,-2035},"
done
export PLAN="{ {10,'trace','start'}, {$T,'go',${NEAR// /,},0.85}, $shots {$((T + 14)),'trace','end'} }"
export SETTINGS_EXTRA="[Game]\nneutral actor locomotion = true\n[extension.skyrimse]\nambush = true\ncombat trace = true\nactor packages = true\ncreatures = true\ncombat ai = true\nquest alias packages = true\n${SETTINGS_EXTRA:-}"
export OPENEDEN_NEUTRAL_ACTOR_TRACE=${OPENEDEN_NEUTRAL_ACTOR_TRACE:-1}
export OPENEDEN_NEUTRAL_COMBAT_TRACE=1 OPENEDEN_PAPYRUS_NATIVE_TRACE=1
export CHECK_TIMEOUT=${CHECK_TIMEOUT:-900}
bash "$HERE/../skydungeon_check/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK" "$FRAMES"
grep -E "AMBUSH|NACTFURN|COMBAT ai|COMBAT creature|Papyrus event (OnGetUp|OnCombatStateChanged)|DNG|NACTVAL" "$WORK/log.txt" | sed 's/^\[[^]]*\] //' || true
