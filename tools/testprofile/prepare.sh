#!/bin/bash
# OpenEden living test profile: makes (or updates) a profile folder that runs every finished feature at once.
#
# usage: tools/testprofile/prepare.sh <your config folder> [--no-bake] [--name NAME] [--build DIR] [variant ...]
#
#   <your config folder>  the folder holding your openmw.cfg and settings.cfg (on Windows usually
#                         Documents/My Games/OpenMW). It is only READ: two files are copied, nothing is written to it.
#   variant               the name of a file in tools/testprofile/variants/ (msaa, glsl, lowtier, aces, stats, pcss,
#                         lowmemory ...). Variants are merged after profile.cfg, in the order given, and win over it.
#   --no-bake             do not run the bakers (the LOD tiles and clusters, the merged bodies, the light probes).
#   --name NAME           the profile folder is I:/Projects/OpenEden-work/testprofile/NAME (default: main).
#   --build DIR           the build folder whose openmw.exe, resources and defaults.cfg the profile uses
#                         (default: I:/Projects/OpenEden/build-jolt).
#
# What it makes, all inside the profile folder (nothing outside I:/Projects/OpenEden-work is ever written):
#   config/openmw.cfg    your openmw.cfg with the build's resources, the engine's data folder, the Morrowind
#                        compatibility extension and a data-local folder INSIDE the profile; config=, user-data= and
#                        resources= lines are dropped (a config= line would chain back to your real folder)
#   config/settings.cfg  your settings.cfg with profile.cfg and the variants merged in: each key is written once, into
#                        the first section of its name (the game rewrites the file at exit and a key written twice is
#                        a parse error). Your other lines stay. The previous copy is kept as settings.cfg.previous.
#   user/                the game's user folder: saves, screenshots, logs and the baked data (openeden/lod, bodies, probes)
#   data/                the profile's data-local folder (so the real ...\My Games\OpenMW\data is not used)
#   start_game.bat/.sh, start_game_quiet.bat/.sh, start_editor.bat/.sh, profile_info.txt
#
# The game and editor run from the BUILD folder's own executables: a rebuild changes them under the profile (a build in
# progress can leave openmw.exe missing for a while). Run prepare.sh again after a rebuild that adds settings, so the
# profile gets the new keys and the check against the build's defaults.cfg runs again. Run it with the game closed.
# Every game the bakers start runs with OPENEDEN_QUIET_WINDOW=1 (a window far off the desktop, no focus).
set -euo pipefail

workRoot="I:/Projects/OpenEden-work"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

native() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
winpath() { cygpath -w "$1" 2>/dev/null || echo "$1"; }
lower() { tr '[:upper:]' '[:lower:]'; }
fail() { echo "prepare: $*" >&2; exit 2; }
usage() { sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

source=""; name="main"; build="I:/Projects/OpenEden/build-jolt"; bake=1; variants=()
while [ $# -gt 0 ]; do
    case "$1" in
        --no-bake) bake=0 ;;
        --name) shift; name=${1:?--name needs a value} ;;
        --build) shift; build=${1:?--build needs a value} ;;
        -h|--help) usage ;;
        --*) fail "unknown option $1 (see --help)" ;;
        *) if [ -z "$source" ]; then source=$1; else variants+=("$1"); fi ;;
    esac
    shift
done
[ -n "$source" ] || usage

case "$name" in
    ""|*[!A-Za-z0-9_-]*) fail "the profile name may only hold letters, digits, - and _ (got '$name')" ;;
esac
for v in "${variants[@]+"${variants[@]}"}"; do
    [ -f "$HERE/variants/$v.cfg" ] || fail "no variant '$v'; available: $(cd "$HERE/variants" && ls *.cfg | sed 's/\.cfg$//' | tr '\n' ' ')"
done

source=$(native "$source"); source=${source%/}
build=$(native "$build"); build=${build%/}
profile="$workRoot/testprofile/$name"
sourceL=$(echo "$source" | lower); profileL=$(echo "$profile" | lower); rootL=$(echo "$workRoot" | lower)

[ -f "$source/openmw.cfg" ] || fail "no openmw.cfg in $source"
[ -f "$build/openmw.exe" ] || fail "no openmw.exe in $build (is a build in progress? wait and retry)"
[ -f "$build/defaults.cfg" ] || fail "no defaults.cfg in $build"
[ -d "$build/resources" ] || fail "no resources folder in $build"
case "$profileL/" in "$rootL"/testprofile/?*/) ;; *) fail "the profile must be inside $workRoot/testprofile (got $profile)" ;; esac
case "$profileL/" in "$sourceL"/*) fail "the profile must not be inside the config folder" ;; esac
case "$sourceL/" in "$profileL"/*) fail "the config folder must not be inside the profile" ;; esac
if [ -d "$profile" ] && [ -n "$(ls -A "$profile")" ] && [ ! -f "$profile/.testprofile" ]; then
    fail "$profile exists, is not empty and was not made by this script: pick another --name"
fi

mkdir -p "$profile/config" "$profile/user" "$profile/data"
touch "$profile/.testprofile"

# --- the merged key list: profile.cfg, then the variants; a later value wins; first-seen order is kept ---
want="$profile/.merged_keys.tsv"
awk -f "$HERE/cfgkeys.awk" "$HERE/profile.cfg" $(for v in "${variants[@]+"${variants[@]}"}"; do echo "$HERE/variants/$v.cfg"; done) |
    awk -F'\t' '{ id = $1 SUBSEP $2; if (!(id in val)) order[++n] = id; val[id] = $3; s[id] = $1; k[id] = $2 }
        END { for (i = 1; i <= n; ++i) printf "%s\t%s\t%s\n", s[order[i]], k[order[i]], val[order[i]] }' > "$want"

# --- keys the build does not know: the build is older than the profile ---
awk -f "$HERE/cfgkeys.awk" "$build/defaults.cfg" > "$profile/.build_keys.tsv"
unknownToBuild=$(awk -F'\t' 'FNR == NR { known[$1 SUBSEP $2] = 1; next } !(($1 SUBSEP $2) in known) { print "  [" $1 "] " $2 }' \
    "$profile/.build_keys.tsv" "$want")
rm -f "$profile/.build_keys.tsv"
if [ -n "$unknownToBuild" ]; then
    echo "WARNING: these profile settings are not in $build/defaults.cfg: the build is older than the profile." >&2
    echo "         The game would ignore them (and warn). Rebuild, then run prepare.sh again:" >&2
    echo "$unknownToBuild" >&2
fi

# --- settings.cfg ---
settings="$profile/config/settings.cfg"
[ -f "$settings" ] && cp "$settings" "$settings.previous"
if [ -f "$source/settings.cfg" ]; then tr -d '\r' < "$source/settings.cfg" > "$settings.base"; else : > "$settings.base"; fi
awk -F'\t' '
    FNR == NR {
        id = $1 SUBSEP $2
        if (!($1 in add)) order[++sections] = $1
        add[$1] = add[$1] $2 " = " $3 "\n"; want[id] = 1
        next
    }
    {
        line = $0
        if (line ~ /^[ \t]*\[/) {
            sec = line; gsub(/^[ \t]*\[|\][ \t]*$/, "", sec); print line
            if ((sec in add) && !(sec in done)) { printf "%s", add[sec]; done[sec] = 1 }
            next
        }
        if (line !~ /^[ \t]*[#;]/ && line ~ /=/) {
            k = line; sub(/[ \t]*=.*/, "", k); sub(/^[ \t]+/, "", k)
            if ((sec SUBSEP k) in want) next
        }
        print line
    }
    END { for (i = 1; i <= sections; ++i) if (!(order[i] in done)) printf "\n[%s]\n%s", order[i], add[order[i]] }' \
    "$want" "$settings.base" > "$settings.new"
mv "$settings.new" "$settings"
rm -f "$settings.base" "$want"

# --- openmw.cfg: only this copy is read (--replace=config) ---
build_res="$build/resources"
cfg="$profile/config/openmw.cfg"
tr -d '\r' < "$source/openmw.cfg" |
    awk -v res="resources=$build_res" -v vfs="data=\"$build_res/vfs-mw\"" -v local="data-local=\"$profile/data\"" '
        { low = tolower($0); sub(/^[ \t]+/, "", low) }
        low ~ /^(config|user-data|resources|data-local)[ \t]*=/ { next }
        low ~ /^data[ \t]*=/ && low ~ /resources[\/\\]vfs-mw"?[ \t]*$/ { next }
        low ~ /^data[ \t]*=/ && !vfsDone { print vfs; vfsDone = 1 }
        { print }
        END { if (!vfsDone) print vfs; print res; print local }' > "$cfg"
grep -qi '^extension=compat_morrowind' "$cfg" || echo "extension=compat_morrowind" >> "$cfg"

cfgN="$profile/config"; userN="$profile/user"
cfgW=$(winpath "$cfgN"); userW=$(winpath "$userN"); buildW=$(winpath "$build")

# --- start scripts ---
variantText="${variants[*]+${variants[*]}}"
for kind in game editor; do
    if [ $kind = game ]; then exe=openmw.exe; else exe=eden-studio.exe; fi
    for mode in normal quiet; do
        if [ $mode = quiet ]; then stem="start_${kind}_quiet"; else stem="start_${kind}"; fi
        {
            echo "@echo off"
            echo "rem Starts $exe with the OpenEden test profile '$name' (tools/testprofile/prepare.sh). Extra arguments are passed on."
            [ $mode = quiet ] && echo "set OPENEDEN_QUIET_WINDOW=1"
            echo "cd /d \"$buildW\""
            echo "$exe --replace=config --config \"$cfgW\" --user-data \"$userW\" %*"
        } | sed 's/$/\r/' > "$profile/$stem.bat"
        {
            echo "#!/bin/bash"
            echo "# Starts $exe with the OpenEden test profile '$name' (tools/testprofile/prepare.sh). Extra arguments are passed on."
            [ $mode = quiet ] && echo "export OPENEDEN_QUIET_WINDOW=1"
            echo "cd \"$build\" && exec ./$exe --replace=config --config \"$cfgN\" --user-data \"$userN\" \"\$@\""
        } > "$profile/$stem.sh"
        chmod +x "$profile/$stem.sh"
    done
done
{
    echo "OpenEden test profile '$name'"
    echo "prepared:  $(date '+%Y-%m-%d %H:%M:%S')"
    echo "source:    $source (read only: openmw.cfg and settings.cfg were copied)"
    echo "build:     $build (openmw.exe of $(date -r "$build/openmw.exe" '+%Y-%m-%d %H:%M:%S'))"
    echo "repo:      $(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "variants:  ${variantText:-(none)}"
    echo "settings:  tools/testprofile/profile.cfg merged into config/settings.cfg"
} > "$profile/profile_info.txt"

# --- the bakers ---
failures=0
runBake() { # label, log name, pack file to look for (or empty), arguments of openmw...
    local label=$1 logName=$2 pack=$3; shift 3
    local log="$profile/bake_$logName.log" attempt why
    echo "baking $label (a game runs in a quiet window)..."
    # A cold shader cache (a new profile) can make the first start of a cell stall for more than 5 s, which the freeze
    # monitor ends with exit code 0xDEAD; the compiled shaders stay in <user>/openeden/shadercache, so one retry works.
    for attempt in 1 2; do
        why=""
        if ! (cd "$build" && OPENEDEN_QUIET_WINDOW=1 timeout 1200 ./openmw.exe --replace=config --config "$cfgN" --user-data "$userN" "$@" > "$log" 2>&1); then
            why="the game exited with an error"
            grep -q 'has frozen' "$log" && why="the game froze (cold shader cache)"
        elif [ -n "$pack" ] && [ ! -e "$userN/$pack" ]; then
            why="no $pack after the bake"
        fi
        [ -z "$why" ] && break
        if [ $attempt = 1 ]; then echo "  $why: trying once more"; fi
    done
    if [ -n "$why" ]; then echo "  FAILED: $why: see $log" >&2; failures=$((failures + 1)); return; fi
    if grep -qi 'up to date' "$log"; then echo "  already up to date"; else echo "  done"; fi
}
if [ $bake = 1 ]; then
    runBake "the terrain tiles (about 20 s)" terrain openeden/lod/terrain.edenpak --bake-lod-terrain
    runBake "the object clusters (about 40 s)" objects openeden/lod/objects.edenpak --bake-lod-objects
    runBake "the merged bodies (about 10 s)" bodies openeden/bodies/current --bake-bodies
    runBake "the puddle map (about 2 min; task 0095)" puddles openeden/puddles/puddles.edenpak --bake-puddles
    # Interiors worth looking at (README): the light probes of each (a second or so each). The game reads probe files
    # from the VFS (RenderManager::loadProbes), and the profile's user folder is not a data folder, so they are written
    # into the profile's data-local folder (data/openeden/probes). The LOD packs and the body cache are read from the
    # user folder directly.
    for cell in "Caldera, Guild of Mages" "Seyda Neen, Arrille's Tradehouse"; do
        runBake "the light probes of $cell" "gi_${cell//[^A-Za-z0-9]/_}" "" --bake-gi "$cell" --bake-out "$profile/data"
    done
    runBake "the exterior sky visibility around Seyda Neen" gi_exterior "" --bake-gi-exterior "-3,-10:-1,-8" --bake-out "$profile/data"
else
    echo "--no-bake: nothing baked. The LOD tiles and clusters, the merged bodies, the puddle map and the light probes are missing until you bake."
fi

# --- what to run ---
{
    echo
    echo "Test profile '$name' is ready in $(winpath "$profile")"
    echo "  executable: $buildW\\openmw.exe (built $(date -r "$build/openmw.exe" '+%Y-%m-%d %H:%M'))"
    echo "  variants:   ${variantText:-(none)}"
    echo
    echo "Start the game (visible window):"
    echo "  $(winpath "$profile")\\start_game.bat"
    echo "  Git Bash: $profile/start_game.sh"
    echo "Start it off-screen (for scripted runs): start_game_quiet.bat / .sh"
    echo "Start the editor: start_editor.bat / .sh"
    echo
    echo "Or by hand:"
    echo "  cd \"$buildW\" && openmw.exe --replace=config --config \"$cfgW\" --user-data \"$userW\""
    echo "  cd \"$buildW\" && eden-studio.exe --replace=config --config \"$cfgW\" --user-data \"$userW\""
    echo
    echo "Screenshots (F12): $userW\\screenshots. Log: $cfgW\\openmw.log."
    echo "Close the game before running prepare.sh again: the game rewrites config\\settings.cfg when it exits."
    if [ $failures -gt 0 ]; then echo "$failures bake(s) FAILED (see the bake_*.log files in the profile folder)."; fi
} | tee "$profile/start_here.txt"
[ $failures -eq 0 ]
