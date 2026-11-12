#!/bin/bash
# tools/neutral_session_check/acceptance.sh (task 0131, stage N3a-7): the written acceptance list for milestones M-S0 and
# M-S1, run in order with check.sh. Each step prints its own OK/FAILED line; screenshots stay in <work dir>/<step>/user/
# screenshots for you to look at. All games open off-screen (OPENEDEN_QUIET_WINDOW). One game at a time.
#
#   acceptance.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames per second hint, default 240]
#
# The steps (cell, scenario, what you should see in the pictures and the log):
#   1 look     WhiterunBanneredMare  the inn is lit by its own lights (warm pools of light, dim corners), furniture and
#                                    beams are textured
#   2 collide  WhiterunBanneredMare  the player stands on the floor (height above the floor ray about 0.5 to 1 unit), is
#                                    stopped by the counter, table and chairs
#   3 stairs   WhiterunBanneredMare  the player walks up the stairs (z rises about 235 units) and down again
#   4 doors    DawnstarBarracks      through the door to the jail and back, twice; the arrival matches the door
#   5 key      Angarvunde01          a locked door is refused without its key, opens with it, stays open, works both ways
#   6 chest    WhiterunBanneredMare  a locked chest with no key is refused ("It is locked.")
#   7 outside  WhiterunBanneredMare  out of the front door into Whiterun's exterior: ground under the feet, borders crossed
#   8 sweep    WhiterunBanneredMare  the Lua API sweep: no crash, every error a refusal
# Then, for Morrowind unchanged: tools/worldobjects_check/check.sh <build> <parent build> <base config> <work> must print
# OK, and a Balmora frame with OPENEDEN_FIXED_FRAME_TIME=0.0166667, --random-seed 7, --auto-screenshot 600 has the same
# SHA-256 (1c5d47ad...) for the parent and this build, [Game] neutral world off and on.
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; FPS=${5:-240}
HERE=$(cd "$(dirname "$0")" && pwd)
frames() { echo $(( $1 * FPS )); }   # seconds of scenario -> frames
step() {  # name cell seconds scenario [env...]
    local name=$1 cell=$2 secs=$3 scenario=$4; shift 4
    echo "== $name ($cell, $scenario)"
    for attempt in 1 2; do   # the first run after a build can trip the 5 s freeze monitor (cold caches): run it again
        if env "$@" bash "$HERE/check.sh" "$BUILD" "$BASE" "$SKYRIM" "$WORK/$name" "$cell" "$(frames "$secs")" "$scenario" > "$WORK-$name.txt" 2>&1; then
            break
        fi
    done
    grep -E "^(exit code|OK|FAILED)" "$WORK-$name.txt"
}
mkdir -p "$(dirname "$WORK")"
step 1_look WhiterunBanneredMare 20 look
step 2_collide WhiterunBanneredMare 20 collision
step 3_stairs WhiterunBanneredMare 25 stairs
step 4_doors DawnstarBarracks 55 doorshop OPENEDEN_NEUTRAL_DOOR_TRACE=1
step 5_key Angarvunde01 45 keylocked OPENEDEN_NEUTRAL_KEYS=skyrimse:8715F OPENEDEN_NEUTRAL_DOOR_TRACE=1
step 6_chest WhiterunBanneredMare 20 keycontainer OPENEDEN_NEUTRAL_DOOR_TRACE=1
step 7_outside WhiterunBanneredMare 40 e2terrain
step 8_sweep WhiterunBanneredMare 30 ms0sweep
