#!/bin/bash
# Test games open off-screen, out of the taskbar and without taking focus (apps/openmw/vsgengine.cpp); run with OPENEDEN_QUIET_WINDOW=0 to see the window.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# Measures how the player's animation changes around scripted events (task 0067, stage 0): idle to walk, walk to run,
# stop, turn, weapon draw, attack, a hit and the weapon put away. One scripted scenario (mod/scripts/cmcheck.lua) is run
# once per variant; the game's `CMTRACE` lines (`[Game] creativemotion animation debug`) and the scenario's `CMPHASE`
# lines are read by the creativemotioncheck program into a table and a JSON report per run.
#
# Usage: tools/creativemotion_check/check.sh <build dir> <base openmw.cfg> <empty work dir> [variant ...]
#   <build dir>        the folder holding openmw.exe and creativemotioncheck.exe
#   <base openmw.cfg>  a COPY of a working config (data, content, fallback and archive lines; never the user's own
#                      file: the check appends two lines to its copy). The user's settings.cfg is never read.
#   <work dir>         empty; each run gets <work dir>/<variant>-<n>/{config,user}, report.json and report.txt
#   variants           vanilla, inertial, cm, cmoff, natural (default: the first three)
#     vanilla   every feature off: Morrowind as it was
#     inertial  [Game] inertial locomotion = high
#     cm        inertial locomotion = high and creativemotion animation = true
#     cmoff     cm with the preset, transition rules and style layer written explicitly as off/false (the
#               unchanged-when-off check: must measure as cm does, within the spread between runs)
#     natural   inertial locomotion = high and creativemotion preset = natural (task 0067, stage 1); a natural run
#               whose trace never shows the intent wanting to move, starting and stopping FAILS (the engine feed is
#               broken: stage 1 review, finding 1)
# Environment: MODDIR=<folder> uses another scenario mod (tools/creativemotion_check/mod_firstperson: the same
#              scenario in first person; task 0124);
#              EXTRA_SETTINGS=<text> is appended to each run's settings.cfg (including section headers) and recorded
#              in each report's meta;
#              REPEAT=<n> runs each variant n times (default 3; a difference between variants counts only where it
#              is larger than the spread between the runs of one variant) and writes <variant>-summary.txt/.json with
#              the range of every measure over the runs;
#              CELL=<cell name> (default "Seyda Neen": open ground; the Ebonheart chambers are too small, the player runs into a wall after 145 units); FRAMES=<n> auto-quit frame
#              (default 1400, the scenario is about 1100 frames long).
# On Windows pass Windows paths (I:/...), see CLAUDE.md "Check scripts take Windows paths". Run the game's own folder
# so that its runtime libraries are found.
#
# The scenario counts frames, not seconds, so two runs change phase on the same frame. Frame times still differ a
# little between runs (no fixed timestep), so two runs are close but not byte-identical: compare the numbers, and use
# REPEAT=2 to see by how much they differ.

set -u
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
BASECFG=$2
mkdir -p "$3"
WORK=$(cd "$3" && { pwd -W 2>/dev/null || pwd; })
shift 3
HERE=$(cd "$(dirname "$0")" && pwd)
MOD=$(cd "${MODDIR:-$HERE/mod}" && { pwd -W 2>/dev/null || pwd; })
CELL=${CELL:-Seyda Neen}
FRAMES=${FRAMES:-1400}
REPEAT=${REPEAT:-3}
VARIANTS=("$@")
[ ${#VARIANTS[@]} -gt 0 ] || VARIANTS=(vanilla inertial cm)
FAILED=0

# Which build made the reports: the commit of the source tree (with the number of changed tracked files, since the
# build may hold uncommitted work) and the time openmw was linked.
REPO=$(cd "$HERE/../.." && pwd)
COMMIT=$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)
CHANGED=$(git -C "$REPO" status --porcelain --untracked-files=no 2>/dev/null | wc -l | tr -d ' ')

[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "FAIL: $WORK is not empty"; exit 1; }
[ -f "$BASECFG" ] || { echo "FAIL: no config at $BASECFG"; exit 1; }
GAME="./openmw.exe"
[ -x "$BUILD/openmw.exe" ] || GAME="./openmw"
[ -x "$BUILD/$GAME" ] || { echo "FAIL: no openmw in $BUILD"; exit 1; }
CHECK="$BUILD/creativemotioncheck.exe"
[ -x "$CHECK" ] || CHECK="$BUILD/creativemotioncheck"
[ -x "$CHECK" ] || { echo "FAIL: no creativemotioncheck in $BUILD (build the target)"; exit 1; }

variant_settings() {
    case "$1" in
        vanilla) printf '' ;;
        inertial) printf 'inertial locomotion = high\n' ;;
        cm) printf 'inertial locomotion = high\ncreativemotion animation = true\n' ;;
        cmoff) printf 'inertial locomotion = high\ncreativemotion animation = true\ncreativemotion preset = off\ncreativemotion transition rules = false\ncreativemotion style layer = false\n' ;;
        natural) printf 'inertial locomotion = high\ncreativemotion preset = natural\n' ;;
        *) return 1 ;;
    esac
}

run_one() {
    local label=$1 repeat=$2
    local dir="$WORK/$label-$repeat"
    mkdir -p "$dir/config" "$dir/user"
    local settings
    settings=$(variant_settings "$label") || { echo "FAIL: unknown variant $label"; FAILED=1; return; }
    tr -d '\r' < "$BASECFG" > "$dir/config/openmw.cfg"
    printf '\ndata="%s"\ncontent=cmcheck.omwscripts\n' "$MOD" >> "$dir/config/openmw.cfg"
    # A small window with the frame rate fixed at 60 and no vsync, so frame times stay close between runs.
    printf '[Video]\nresolution x = 960\nresolution y = 540\nfullscreen = false\nwindow mode = 2\nvsync = false\nframerate limit = 60\n[Game]\ncreativemotion animation debug = true\n%s' "$settings" > "$dir/config/settings.cfg"
    # EXTRA_SETTINGS: more settings.cfg text with its own section headers, for example
    # EXTRA_SETTINGS=$'[CreativeMotion Transitions]\nstopping = 2.5'; appended to every run.
    [ -z "${EXTRA_SETTINGS:-}" ] || printf '\n%s\n' "$EXTRA_SETTINGS" >> "$dir/config/settings.cfg"
    local cfgwin userwin
    cfgwin=$(cd "$dir/config" && { pwd -W 2>/dev/null || pwd; })
    userwin=$(cd "$dir/user" && { pwd -W 2>/dev/null || pwd; })
    echo "run $label-$repeat"
    (cd "$BUILD" && timeout 400 "$GAME" --replace=config --config "$cfgwin" --user-data "$userwin" --skip-menu \
        --start "$CELL" --no-grab --auto-quit "$FRAMES" > "$dir/stdout.log" 2>&1)
    local status=$?
    if ls "$dir/config"/*.dmp > /dev/null 2>&1; then
        echo "FAIL: $label-$repeat wrote a crash dump"
        FAILED=1
        return
    fi
    if [ "$status" -ne 0 ]; then
        echo "FAIL: $label-$repeat exited with $status"
        FAILED=1
        return
    fi
    local built
    built=$(date -r "$BUILD/$GAME" '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo unknown)
    META=(--meta "commit=$COMMIT (+$CHANGED changed tracked files)" --meta "openmw built=$built"
        --meta "variant settings=$settings" --meta "extra settings=${EXTRA_SETTINGS:-}")
    "$CHECK" --label "$label-$repeat" "${META[@]}" "$dir/config/openmw.log" > "$dir/report.txt" 2> "$dir/report.err" \
        || { echo "FAIL: $label-$repeat: $(cat "$dir/report.err")"; FAILED=1; return; }
    "$CHECK" --json --label "$label-$repeat" "${META[@]}" "$dir/config/openmw.log" > "$dir/report.json" \
        2>> "$dir/report.err"
    if [ "$label" = natural ]; then
        # The intent must be seen: frames that want to move, that start and that stop (finding 1 of the review).
        local intent
        intent=$(grep -o '"intentFrames": {[^}]*}' "$dir/report.json")
        for field in wants start stop; do
            if ! echo "$intent" | grep -q "\"$field\": [1-9]"; then
                echo "FAIL: $label-$repeat: the trace never shows intent $field ($intent)"
                FAILED=1
            fi
        done
    fi
    cat "$dir/report.txt"
}

for label in "${VARIANTS[@]}"; do
    for ((n = 1; n <= REPEAT; ++n)); do
        run_one "$label" "$n"
    done
    logs=()
    for ((n = 1; n <= REPEAT; ++n)); do
        [ -f "$WORK/$label-$n/config/openmw.log" ] && logs+=("$WORK/$label-$n/config/openmw.log")
    done
    if [ ${#logs[@]} -gt 0 ]; then
        settings=$(variant_settings "$label")
        "$CHECK" --summary --label "$label" --meta "commit=$COMMIT (+$CHANGED changed tracked files)" \
            --meta "variant settings=$settings" --meta "extra settings=${EXTRA_SETTINGS:-}" "${logs[@]}" \
            > "$WORK/$label-summary.txt"
        "$CHECK" --summary --json --label "$label" --meta "commit=$COMMIT (+$CHANGED changed tracked files)" \
            --meta "variant settings=$settings" --meta "extra settings=${EXTRA_SETTINGS:-}" "${logs[@]}" \
            > "$WORK/$label-summary.json"
        echo "summary of $label over ${#logs[@]} runs: $WORK/$label-summary.txt"
    fi
done

[ "$FAILED" -eq 0 ] && echo "creativemotion check done" || echo "creativemotion check FAILED"
exit "$FAILED"
