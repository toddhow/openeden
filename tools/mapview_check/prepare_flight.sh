#!/bin/bash
# Task 0066 stage f: prepare toddhow's own flight over the whole-map view.
# usage: prepare_flight.sh <your config folder> <work folder under I:/Projects/OpenEden-work> [build folder]
#
# Makes a COPY of the config folder's openmw.cfg and settings.cfg in <work>/config (the source folder is only read),
# gives the copy the build's resources, the engine's data folder and the Morrowind pack (as the build's openmw.cfg
# would), sets the
# whole-map view settings in the copy's settings.cfg, bakes the terrain tiles and the object clusters into
# <work>/user/openeden/lod (each bake runs a game in a quiet window: OPENEDEN_QUIET_WINDOW=1), and prints the command
# that starts the game with the copy. Run it with the game closed. Writes <work>/start_game.txt with the same commands.
set -euo pipefail

workRoot="I:/Projects/OpenEden-work"
source=${1:?usage: prepare_flight.sh <config folder> <work folder under $workRoot> [build folder]}
work=${2:?usage: prepare_flight.sh <config folder> <work folder under $workRoot> [build folder]}
build=${3:-I:/Projects/OpenEden/build-jolt}

native() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
lower() { tr '[:upper:]' '[:lower:]'; }
fail() { echo "prepare_flight: $*" >&2; exit 2; }

source=$(native "$source"); source=${source%/}
work=$(native "$work"); work=${work%/}
build=$(native "$build"); build=${build%/}
sourceL=$(echo "$source" | lower); workL=$(echo "$work" | lower); rootL=$(echo "$workRoot" | lower)

[ -f "$source/openmw.cfg" ] || fail "no openmw.cfg in $source"
[ -f "$build/openmw.exe" ] || fail "no openmw.exe in $build"
case "$workL/" in "$rootL"/?*/) ;; *) fail "the work folder must be inside $workRoot (got $work)" ;; esac
case "$workL/" in "$sourceL"/*) fail "the work folder must not be inside the config folder" ;; esac
case "$sourceL/" in "$workL"/*) fail "the config folder must not be inside the work folder" ;; esac
if [ -d "$work" ] && [ -n "$(ls -A "$work")" ] && [ ! -f "$work/.prepare_flight" ]; then
    fail "$work exists, is not empty and was not made by this script: pick another folder"
fi

mkdir -p "$work/config" "$work/user"
touch "$work/.prepare_flight"
if [ -f "$source/settings.cfg" ]; then cp "$source/settings.cfg" "$work/config/settings.cfg"; else : > "$work/config/settings.cfg"; fi

# openmw.cfg: only the copy is read (--replace=config), so it gets what the build's own openmw.cfg gives: the build's
# resources, the engine's data folder ahead of every other data= line (where the build's openmw.cfg puts it) and the
# Morrowind pack. config= and user-data= lines are dropped: a config= line would chain back to a real config folder,
# and the game writes its settings.cfg into the last folder of that chain.
cfg="$work/config/openmw.cfg"
tr -d '\r' < "$source/openmw.cfg" |
    awk -v res="resources=$build/resources" -v vfs="data=\"$build/resources/vfs-mw\"" '
        { low = tolower($0); sub(/^[ \t]+/, "", low) }
        low ~ /^(config|user-data|resources)[ \t]*=/ { next }
        low ~ /^data[ \t]*=/ && low ~ /resources[\/\\]vfs-mw"?[ \t]*$/ { next }
        low ~ /^data[ \t]*=/ && !vfsDone { print vfs; vfsDone = 1 }
        { print }
        END { if (!vfsDone) print vfs; print res }' > "$cfg"
grep -qi '^extension=compat_morrowind' "$cfg" || echo "extension=compat_morrowind" >> "$cfg"

# settings.cfg: every key below is written once, into the first section of its name (the game rewrites settings.cfg at
# exit and copies a second section's keys into the first, and a key written twice is a parse error at the next start).
settings="$work/config/settings.cfg"
set_keys='Terrain|distant terrain|true
WorldLod|enabled|true
WorldLod|map view|true
WorldLod|view distance|auto
WorldLod|object clusters|true
WorldLod|haze|true
WorldLod|tier|auto'
awk -v want="$set_keys" '
    BEGIN {
        n = split(want, a, "\n")
        for (i = 1; i <= n; ++i) {
            split(a[i], p, "|"); key[p[1] "|" p[2]] = 1
            if (!(p[1] in add)) order[++sections] = p[1]
            add[p[1]] = add[p[1]] p[2] " = " p[3] "\n"
        }
    }
    { line = $0; sub(/\r$/, "", line) }
    line ~ /^[ \t]*\[/ {
        sec = line; gsub(/^[ \t]*\[|\][ \t]*$/, "", sec); print line
        if ((sec in add) && !(sec in done)) { printf "%s", add[sec]; done[sec] = 1 }
        next
    }
    line ~ /=/ { k = line; sub(/[ \t]*=.*/, "", k); sub(/^[ \t]+/, "", k); if ((sec "|" k) in key) next }
    { print line }
    END { for (i = 1; i <= sections; ++i) if (!(order[i] in done)) printf "\n[%s]\n%s", order[i], add[order[i]] }' \
    "$settings" > "$settings.new"
mv "$settings.new" "$settings"

cfgN=$(native "$work/config"); userN=$(native "$work/user")
export OPENEDEN_QUIET_WINDOW=1
cd "$build"
for what in terrain objects; do
    echo "baking the $what (a game runs in a quiet window; terrain about 20 s, objects about 40 s)..."
    if ! timeout 900 ./openmw.exe --replace=config --config "$cfgN" --user-data "$userN" "--bake-lod-$what" \
        > "$work/bake_$what.log" 2>&1; then
        fail "the $what bake failed: see $work/bake_$what.log"
    fi
    [ -f "$work/user/openeden/lod/$what.edenpak" ] || fail "no $what.edenpak after the bake: see $work/bake_$what.log"
    echo "  $what.edenpak: $(stat -c %s "$work/user/openeden/lod/$what.edenpak") bytes"
done

exeW=$(cygpath -w "$build/openmw.exe" 2>/dev/null || echo "$build/openmw.exe")
cfgW=$(cygpath -w "$work/config" 2>/dev/null || echo "$cfgN")
userW=$(cygpath -w "$work/user" 2>/dev/null || echo "$userN")
{
    echo "Start the game with the copy (close it before editing $cfgW\\settings.cfg: the game rewrites that file):"
    echo
    echo "PowerShell:"
    echo "  & \"$exeW\" --replace=config --config \"$cfgW\" --user-data \"$userW\""
    echo
    echo "Git Bash:"
    echo "  cd \"$build\" && ./openmw.exe --replace=config --config \"$cfgN\" --user-data \"$userN\""
    echo
    echo "Screenshots (F12) go to $userW\\screenshots; the log is $cfgW\\openmw.log."
} | tee "$work/start_game.txt"
