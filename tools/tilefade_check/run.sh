#!/bin/bash
# Task 0087: one run of the tile fade scenarios (data/scripts/tilefade_player.lua) in a quiet window, for comparing two
# executables or two settings.
# usage: [OPENMW_EXE=<path to openmw.exe>] [RESOURCES=<resources folder>] [SCENARIO=still|flight] [EXTRA_SETTINGS=<text>]
#        [START_COMMANDS=<console lines separated by ;>] run.sh <work folder> <config folder (a COPY with its
#        settings.cfg and the base openmw.cfg)> <user data folder (with the baked LOD packs)> [seconds]
# Writes <work>/log.txt, <work>/cfg (a copy of the config with the scenario's lines), <work>/frames/*.png.
# Windows paths for the folders (see CLAUDE.md pitfalls). Knobs: START_DELAY, PITCH, HOLD, POSES (lua vectors),
# FROM, TO, FRAMES, SHOT_EVERY (see the lua file).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
export OPENEDEN_FIXED_FRAME_TIME="${OPENEDEN_FIXED_FRAME_TIME-0.0166667}"
work=$1; config=$2; user=$3; limit=${4:-240}
native() { cygpath -m "$1"; }
mkdir -p "$work"
run_cfg="$work/cfg"; rm -rf "$run_cfg"; mkdir -p "$run_cfg"; cp "$config"/* "$run_cfg"/
data="$work/data"; rm -rf "$data"; mkdir -p "$data/scripts"
cp "$HERE/data/tilefade.omwscripts" "$data/"; cp "$HERE/data/scripts/tilefade_global.lua" "$data/scripts/"
sed -e "s|@SCENARIO@|${SCENARIO:-still}|" -e "s|@START_DELAY@|${START_DELAY:-25}|" -e "s|@PITCH@|${PITCH:-0.61}|" \
    -e "s|@HOLD@|${HOLD:-150}|" -e "s|@POSES@|${POSES:-util.vector3(0, 0, 2000), util.vector3(0, 0, 8000), util.vector3(0, 0, 25000)}|" \
    -e "s|@FROM@|${FROM:-0, 0, 12000}|" -e "s|@TO@|${TO:-0, 0, 1500}|" -e "s|@FRAMES@|${FRAMES:-1200}|" \
    -e "s|@SHOT_EVERY@|${SHOT_EVERY:-6}|" "$HERE/data/scripts/tilefade_player.lua" > "$data/scripts/tilefade_player.lua"
{ echo "data=\"$(native "$data")\""; echo "content=tilefade.omwscripts"; } >> "$run_cfg/openmw.cfg"
[ -n "$EXTRA_SETTINGS" ] && printf '\n%b\n' "$EXTRA_SETTINGS" >> "$run_cfg/settings.cfg"
{ echo tgm; echo tcl; [ -n "$START_COMMANDS" ] && echo "$START_COMMANDS" | tr ';' '\n'; } > "$work/start.txt"
rm -rf "$user"/screenshots; mkdir -p "$user"/screenshots
default_exe=/i/Projects/OpenEden/build-jolt/openmw.exe
exe="${OPENMW_EXE:-$default_exe}"
res=()
[ -n "$RESOURCES" ] && res=(--resources "$(native "$RESOURCES")")
cd "$(dirname "$exe")"
timeout "$limit" "./$(basename "$exe")" --replace=config --config "$(native "$run_cfg")" --user-data "$(native "$user")" \
    "${res[@]}" --skip-menu --start Balmora --no-grab --script-run "$(native "$work/start.txt")" > "$work/log.txt" 2>&1 || true
mkdir -p "$work/frames"; rm -f "$work/frames"/*; cp "$user"/screenshots/* "$work/frames"/ 2>/dev/null || true
echo "done: $(ls "$work/frames" | wc -l) screenshots; exit log lines: $(wc -l < "$work/log.txt")"
