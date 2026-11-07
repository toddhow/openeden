#!/bin/bash
# Test games open off-screen, out of the taskbar and without taking focus (apps/openmw/vsgengine.cpp); run with OPENEDEN_QUIET_WINDOW=0 to see the window.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# Checks an extension's view of the game world in the real game (task 0017): read-only physics queries, the entity mapping,
# lifecycle, physics and game events. The bridge to the engine lives where no unit test runs.
#
# Usage: tools/extension_world_check/check.sh <build dir> <example suite> <empty work dir>
# On Windows, PATH must already find the game's runtime libraries.
#
# Three runs of one scripted scene (data/extension_world_check_global.lua), each with its own config and user data folder:
#   off:  extension enabled, [Physics] async num threads = 0
#   on:   extension enabled, [Physics] async num threads = 1
#   none: no extension, async num threads = 1
#
# Where each expected value comes from:
#   - The scene's order, from the scene itself: what Lua does, and that a creature placed in an unloaded cell can only
#     spawn with that cell.
#   - The ground height: the engine's own query, nearby.castRay, over the same segment the extension casts (LUA_GROUND).
#   - Extension fractions: equal between the runs off and on. Terrain is not known in advance, so no number is written here.
#   - The player's position: the teleport's destination, and the height above the ground that an actor's body can be.
#   - With and without the extension: only the exit status and the Lua log are compared.
#
# Kills and revivals: Lua can set health only on its own actor and cannot resurrect, and world.mwscript only reads and
# writes variables of scripts already running. So this writes a small content file holding one mwscript, started by a
# start script as in the example suite's mwscript.omwaddon, and Lua sets that script's variables to drive it.

set -u
# pwd -W gives a path Windows programs can read; elsewhere there is no such option.
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
SUITE=$2
WORK=$3
HERE=$(cd "$(dirname "$0")" && pwd)
ID=openeden_test_extension_c
FAILED=0

# The point the extension casts over, in meters; the Lua scripts hold the same number (PROBE_M). 4096 game units.
PROBE_M=58.5216
UNITS_PER_METER=69.99125109 # Constants::UnitsPerMeter, components/misc/constants.hpp
# One game unit: rays over one segment in one world, compared through a float's precision at these sizes.
GROUND_TOLERANCE_UNITS=1
# The player's body is anywhere from its feet up to a tall actor's centre. A generous bound: 3 m.
BODY_ABOVE_GROUND_MAX_M=3
# As kProbeReachM in the extension.
PROBE_REACH_M=1
# A sphere of the extension's 0.25 m radius touches level or sloped ground near the ray's point.
SPHERE_POINT_TOLERANCE_M=1

[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "FAIL: $WORK is not empty"; exit 1; }

pass() { echo "ok: $1"; }
fail() { echo "FAIL: $1"; FAILED=1; }

check() { # description, fixed text, file
    if grep -qF -- "$2" "$3" 2>/dev/null; then pass "$1"; else fail "$1: no \"$2\" in $3"; fi
}
check_not() {
    if grep -qF -- "$2" "$3" 2>/dev/null; then fail "$1: found \"$2\" in $3"; else pass "$1"; fi
}
check_event() { # description, whole words at the start of a line, file. "3:1" must not match "3:10".
    if [ -n "$(first_line "$2" "$3")" ]; then pass "$1"; else fail "$1: no \"$2\" in $3"; fi
}
first_line() { # fixed text at the start of a line, file; prints its line number, or nothing
    awk -v wanted="$1" '$0 == wanted || index($0, wanted " ") == 1 { print NR; exit }' "$2"
}
check_before() { # description, earlier text, later text, file
    local earlier later
    earlier=$(first_line "$2" "$4")
    later=$(first_line "$3" "$4")
    if [ -z "$earlier" ]; then
        fail "$1: no \"$2\""
    elif [ -z "$later" ]; then
        fail "$1: no \"$3\""
    elif [ "$earlier" -ge "$later" ]; then
        fail "$1: \"$2\" (line $earlier) is not before \"$3\" (line $later)"
    else
        pass "$1"
    fi
}
count_lines() { # fixed text at the start of a line, file
    awk -v wanted="$1" '$0 == wanted || index($0, wanted " ") == 1 { n++ } END { print n + 0 }' "$2"
}
id_of_record() { # record ID, file of extension events; prints the entity of the first Spawned
    awk -v wanted="$1" '$1 == "life" && $2 == "Spawned" && tolower($4) == wanted { print $3; exit }' "$2"
}
field() { # name, file of extension events; prints the first "name fraction=F point=X Y Z" as "F X Y Z"
    awk -v wanted="$1" '$1 == wanted && $2 ~ /^fraction=/ { sub("fraction=", "", $2); sub("point=", "", $3); print $2, $3, $4, $5; exit }' "$2"
}

# --- The content file. Layout from components/esm3/loadtes3.cpp, loadscpt.cpp and loadsscr.cpp, in the same order as the
# example suite's mwscript.omwaddon. The game compiles scripts from their text (MWScript::ScriptManager::compile).

le32() { # value; prints it as printf escapes
    printf '\\x%02x\\x%02x\\x%02x\\x%02x' $(($1 & 255)) $((($1 >> 8) & 255)) $((($1 >> 16) & 255)) $((($1 >> 24) & 255))
}
size_of() { wc -c < "$1" | tr -d ' '; }
add_subrecord() { # output file, tag, payload file
    printf '%s' "$2" >> "$1"
    # shellcheck disable=SC2059 # the format is the escapes le32 made
    printf "$(le32 "$(size_of "$3")")" >> "$1"
    cat "$3" >> "$1"
}
add_record() { # output file, tag, file of subrecords
    printf '%s' "$2" >> "$1"
    # shellcheck disable=SC2059
    printf "$(le32 "$(size_of "$3")")" >> "$1"
    head -c 8 /dev/zero >> "$1" # unused and flags
    cat "$3" >> "$1"
}
zero_padded() { # text, size, output file
    printf '%s' "$1" > "$3"
    head -c $(($2 - ${#1})) /dev/zero >> "$3"
}
write_addon() { # output file
    local out=$1 tmp="$WORK/addon_tmp" name=OE_WorldCheck
    mkdir -p "$tmp"

    # HEDR: version 1.3 as a float, type 0, author (32), description (256), record count 2.
    printf '\x66\x66\xa6\x3f\x00\x00\x00\x00' > "$tmp/hedr"
    head -c 288 /dev/zero >> "$tmp/hedr"
    printf '\x02\x00\x00\x00' >> "$tmp/hedr"
    zero_padded template.omwgame 17 "$tmp/mast"
    head -c 8 /dev/zero > "$tmp/mastsize"
    : > "$tmp/tes3"
    add_subrecord "$tmp/tes3" HEDR "$tmp/hedr"
    add_subrecord "$tmp/tes3" MAST "$tmp/mast"
    add_subrecord "$tmp/tes3" DATA "$tmp/mastsize"

    # SCHD: name (32), then five counts, which the game does not use. The rest of the script is its text.
    zero_padded "$name" 52 "$tmp/schd"
    sed 's/\r$//' "$HERE/oe_world_check.mwscript" > "$tmp/sctx"
    : > "$tmp/scpt"
    add_subrecord "$tmp/scpt" SCHD "$tmp/schd"
    add_subrecord "$tmp/scpt" SCTX "$tmp/sctx"

    # SSCR: starts the script with the game. NAME holds the script, DATA one NUL, as in mwscript.omwaddon.
    zero_padded "$name" $((${#name} + 1)) "$tmp/sscr_name"
    zero_padded "" 1 "$tmp/sscr_data"
    : > "$tmp/sscr"
    add_subrecord "$tmp/sscr" NAME "$tmp/sscr_name"
    add_subrecord "$tmp/sscr" DATA "$tmp/sscr_data"

    : > "$out"
    add_record "$out" TES3 "$tmp/tes3"
    add_record "$out" SCPT "$tmp/scpt"
    add_record "$out" SSCR "$tmp/sscr"
    rm -rf "$tmp"
}

# --- Setup

for needed in game_template/data/template.omwgame example_animated_creature/data/landracer.omwaddon \
    the_hub/data/the_hub.omwaddon; do
    [ -f "$SUITE/$needed" ] || { echo "FAIL: $SUITE/$needed not found"; exit 1; }
done
[ -e "$BUILD/extensions/$ID" ] && { echo "FAIL: remove $BUILD/extensions/$ID first: it would hide this copy"; exit 1; }
LIBRARY=$(find "$BUILD" -name "openeden_test_extension_c*.dll" -o -name "libopeneden_test_extension_c*.so" | head -1)
[ -n "$LIBRARY" ] || { echo "FAIL: test extension library not found in $BUILD"; exit 1; }

mkdir -p "$WORK/gamedata" "$WORK/extension_template/$ID"
cp "$HERE"/data/* "$WORK/gamedata/"
find "$WORK/gamedata" -maxdepth 1 -type f -exec sed -i 's/\r$//' {} +
write_addon "$WORK/gamedata/oe_world_check.omwaddon"

cat > "$WORK/extension_template/$ID/extension.json" <<EOF
{
  "format": "openeden.extension",
  "version": 1,
  "id": "$ID",
  "name": "World check",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "sdk": { "major": 4, "minor": 0 },
  "toolchain": "",
  "library": { "windows": "$(basename "$LIBRARY")", "linux": "$(basename "$LIBRARY")" },
  "provides": [],
  "requires": [],
  "optional": [],
  "loadAfter": [],
  "settings": []
}
EOF
cp "$LIBRARY" "$WORK/extension_template/$ID/"

run() { # name, async threads, extra arguments. Writes $WORK/<name>.out, and <name>.txt if the extension ran.
    local name=$1 threads=$2
    shift 2
    local config="$WORK/$name/config" userdata="$WORK/$name/userdata"
    mkdir -p "$config" "$userdata"
    cp "$SUITE/settings.cfg" "$config/settings.cfg"
    printf '[Video]\nresolution x = 640\nresolution y = 480\nframerate limit = 60\n[Physics]\nasync num threads = %s\n' \
        "$threads" >> "$config/settings.cfg"
    cat > "$config/openmw.cfg" <<EOF
resources="$BUILD/resources"
data="$SUITE/game_template/data"
data="$SUITE/example_animated_creature/data"
data="$SUITE/the_hub/data"
data="$SUITE/example_static_props/data"
data="$WORK/gamedata"
user-data="$userdata"
content=template.omwgame
content=landracer.omwaddon
content=the_hub.omwaddon
content=oe_world_check.omwaddon
content=extension_world_check.omwscripts
EOF
    cp -r "$WORK/extension_template" "$userdata/extensions"
    OPENEDEN_TEST_EXTENSION_LOG="$WORK/$name.txt" OPENEDEN_TEST_EXTENSION_WORLD="$PROBE_M $PROBE_M" timeout 400 "$BUILD/openmw" \
        --replace=config --config="$config" --no-grab --no-sound "$@" > "$WORK/$name.out" 2>&1
    local status=$?
    echo "run $name: exit $status"
    [ "$status" -eq 0 ] || FAILED=1
}

lua_log() { # output of a run; prints the lines that must be equal between runs
    grep -aoE 'LUA_(STEP|GROUND|SCENE_DONE)[^'$'\r'']*' "$1"
}

check_scene() { # name of a run
    local out="$WORK/$1.out"
    check "$1: the scene finished" "LUA_SCENE_DONE" "$out"
    check_not "$1: the scene did not time out" "LUA_SCENE_TIMEOUT" "$out"
    check_not "$1: the scene had no Lua error" "LUA_SCENE_ERROR" "$out"
    check_not "$1: the mwscript started" "LUA_STEP no-mwscript" "$out"
    check_not "$1: the mwscript ran without error" "Execution of script" "$out"
    check "$1: the player reached the museum" "LUA_STEP entered Museum of Wonders" "$out"
    check "$1: the game loaded after the scene" "LUA_STEP loaded" "$out"
}

check_extension_run() { # name of a run
    local name=$1 events="$WORK/$1.events" out="$WORK/$1.out"
    if [ ! -s "$WORK/$name.txt" ]; then
        fail "$name: the extension wrote nothing"
        return
    fi
    grep -a " world " "$WORK/$name.txt" | sed -e 's/^[^ ]* world //' -e 's/\r$//' > "$events"
    [ -s "$events" ] || { fail "$name: the extension saw nothing"; return; }

    check_not "$name: every callback ran on the main thread" "wrong-thread" "$events"
    check_not "$name: all services were there" "missing-service" "$events"
    check_not "$name: all query masks were published" "mask-missing" "$events"
    check_not "$name: per-entity data attached" "attach-failed" "$events"
    check "$name: the extension counted to the end" "final events=" "$events"

    # Queries against the known scene.
    local ray sphere ground position
    ray=$(field ray "$events")
    sphere=$(field sphere "$events")
    position=$(awk '$1 == "player-position" { print $2, $3, $4; exit }' "$events")
    ground=$(tr -d '\r' < "$out" | awk '{ for (i = 1; i < NF; ++i) if ($i == "LUA_GROUND") { print $(i + 1); exit } }')
    if [ -z "$ray" ] || [ -z "$sphere" ] || [ -z "$position" ] || [ -z "$ground" ] || [ "$ground" = miss ]; then
        fail "$name: missing a result: ray=[$ray] sphere=[$sphere] position=[$position] ground=[$ground]"
    else
        awk -v ray="$ray" -v sphere="$sphere" -v position="$position" -v ground="$ground" -v units="$UNITS_PER_METER" \
            -v probe="$PROBE_M" -v groundTolerance="$GROUND_TOLERANCE_UNITS" -v bodyMax="$BODY_ABOVE_GROUND_MAX_M" \
            -v reach="$PROBE_REACH_M" -v sphereTolerance="$SPHERE_POINT_TOLERANCE_M" '
            function abs(x) { return x < 0 ? -x : x }
            BEGIN {
                split(ray, r, " "); split(sphere, s, " "); split(position, p, " "); bad = 0
                if (abs(r[4] * units - ground) > groundTolerance) { print "ray point z " r[4] " m, Lua ground " ground " units"; bad = 1 }
                if (abs(r[2] - probe) > 0.01 || abs(r[3] - probe) > 0.01) { print "ray point not under the probe: " ray; bad = 1 }
                if (s[1] > r[1]) { print "sphere fraction " s[1] " is beyond ray fraction " r[1]; bad = 1 }
                if (abs(s[4] - r[4]) > sphereTolerance) { print "sphere point z " s[4] " far from ray point z " r[4]; bad = 1 }
                if (abs(p[1] - probe) > reach || abs(p[2] - probe) > reach) { print "player not at the probe: " position; bad = 1 }
                if (p[3] * units < ground - groundTolerance || p[3] - ground / units > bodyMax) { print "player body height " p[3] " m wrong for ground " ground " units"; bad = 1 }
                exit bad
            }'
        if [ $? -eq 0 ]; then pass "$name: queries match the scene and the engine's own ray"; else FAILED=1; echo "FAIL: $name: queries"; fi
    fi
    check "$name: the player's body maps back to the player" "player-body-roundtrip ok" "$events"
    check "$name: the player's body is right after a load" "player-body-after-load ok" "$events"
    check_not "$name: the player's body is never stale after a load" "player-body-after-load wrong" "$events"
    check "$name: an actor ray finds the player's entity" "actor-ray entity=player" "$events"
    local groundEntity
    groundEntity=$(awk '$1 == "ray-entity" { print $2; exit }' "$events")
    [ -n "$groundEntity" ] && pass "$name: entityOfBody answered for the ground hit ($groundEntity)" \
        || fail "$name: no entityOfBody line for the ground hit"

    # Entity order. The IDs come from the Spawned lines, which name their records.
    local dagger landracer player
    dagger=$(id_of_record basic_dagger1h "$events")
    # The hub already has landracers: this one is the first to spawn after entering the museum.
    landracer=$(awk '$0 == "cell-loaded Museum of Wonders" { seen = 1 } seen && $1 == "life" && $2 == "Spawned" && tolower($4) == "landracer" { print $3; exit }' "$events")
    player=$(awk '$1 == "player-entity" { print $2; exit }' "$events")
    [ -n "$player" ] && pass "$name: playerEntity answered" || fail "$name: no player entity"
    if [ -z "$dagger" ] || [ -z "$landracer" ] || [ -z "$player" ]; then
        fail "$name: cannot check order: dagger=[$dagger] landracer=[$landracer] player=[$player]"
    else
        # Listeners hear at once; the bus delivers notices later, so each stream has its own order.
        # The player moves only between these two lines, so the new cell's load must be among them.
        if awk -v after="life Despawned $dagger" -v before="life Spawned $landracer" '
            index($0, after) == 1 && ($0 == after || substr($0, length(after) + 1, 1) == " ") { seen = 1; next }
            seen && $1 == "life" && $2 == "CellLoaded" { loaded = 1 }
            index($0, before) == 1 && substr($0, length(before) + 1, 1) == " " { found = 1; exit !loaded }
            END { if (!found) exit 1 }' "$events"; then
            pass "$name: a cell loads before its objects spawn (listener)"
        else
            fail "$name: no life CellLoaded between the dagger going and the creature spawning"
        fi
        check_before "$name: a cell loads before its objects spawn (bus)" \
            "cell-loaded Museum of Wonders" "notice Spawned $landracer" "$events"
        check_before "$name: the object spawns before it despawns" \
            "life Spawned $dagger" "life Despawned $dagger" "$events"
        check_before "$name: the creature spawns before it despawns" \
            "life Spawned $landracer" "life Despawned $landracer" "$events"
        check_before "$name: per-entity data is freed after the spawn" \
            "life Spawned $dagger" "deleter $dagger" "$events"
        # The dagger went long before the museum, so this is not the cleanup at shutdown.
        check_before "$name: per-entity data is freed on despawn" \
            "deleter $dagger" "cell-loaded Museum of Wonders" "$events"
        check_before "$name: the extension's notice follows the spawn" \
            "life Spawned $dagger" "notice Spawned $dagger" "$events"
        check_event "$name: the creature died" "life Died $landracer" "$events"
        check_before "$name: the player dies before being revived" \
            "life Died $player" "life Revived $player" "$events"
        check_before "$name: the player's notices keep that order" \
            "notice Died $player" "notice Revived $player" "$events"
        # The scripted swing does not reliably connect here, so actor-hit is checked by running the Lua integration
        # tests' 'player weapon attack' with the extension loaded (task 0017's notes). This only reports.
        echo "info: $name: melee: $(grep -c "actor-hit $player $landracer" "$events") hit events; $(grep -ao 'LUA_ATTACK hit=[a-z]*' "$out" | head -1)"
        echo "info: $name: after the creature's Resurrect: $(awk -v id="$landracer" '$1 == "life" && $3 == id { printf "%s ", $2 }' "$events")"
    fi

    # Game events.
    check "$name: a cell unloads" "cell-unloaded " "$events"
    check "$name: the cell change is told" "cell-changed Museum of Wonders" "$events"
    check "$name: the save is told" "game-saved" "$events"
    local loaded
    loaded=$(count_lines game-loaded "$events")
    # The new game and the load.
    [ "$loaded" -ge 2 ] && pass "$name: two games loaded" || fail "$name: game-loaded $loaded times, expected 2 or more"
    # Actors are kinematic bodies (mwphysics/actor.cpp), and Jolt reports no contact between a kinematic body and a
    # static one, so a fall on the ground gives none. Contacts are tested in openmw-tests through ExtensionWorld.
    echo "info: $name: contact-begin events: $(grep -c "contact-begin " "$events")"
}

# --- Runs

run off 0 --extension=$ID
run on 1 --extension=$ID
run none 1

check_scene off
check_scene on
check_scene none

check_extension_run off
check_extension_run on

[ ! -f "$WORK/none.txt" ] && pass "none: the extension did not load" || fail "none: the extension loaded"

# Determinism: casts over one fixed point in a static scene, with async physics off and on. Text with 9 digits is exact
# for a float.
if [ -s "$WORK/off.events" ] && [ -s "$WORK/on.events" ]; then
    grep -aE '^(ray|sphere) fraction=' "$WORK/off.events" > "$WORK/off.fractions"
    grep -aE '^(ray|sphere) fraction=' "$WORK/on.events" > "$WORK/on.fractions"
    if [ "$(wc -l < "$WORK/off.fractions")" -ne 2 ]; then
        fail "expected one ray and one sphere line, found: $(cat "$WORK/off.fractions")"
    elif diff "$WORK/off.fractions" "$WORK/on.fractions"; then
        pass "the extension's fractions are equal with async physics off and on"
    else
        fail "the extension's fractions differ with async physics off and on"
    fi
else
    fail "no events to compare between off and on"
fi

# The extension does not change what Lua sees in this scene. It is not a comparison of all body states.
lua_log "$WORK/on.out" > "$WORK/on.lua"
lua_log "$WORK/none.out" > "$WORK/none.lua"
if [ ! -s "$WORK/none.lua" ]; then
    fail "the run without the extension has no Lua log"
elif diff "$WORK/on.lua" "$WORK/none.lua"; then
    pass "the Lua log is the same with and without the extension"
else
    fail "the Lua log differs with and without the extension"
fi

[ "$FAILED" -eq 0 ] && echo "ALL OK" || echo "FAILED"
exit $FAILED
