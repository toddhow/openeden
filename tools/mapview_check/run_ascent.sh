#!/bin/bash
# Task 0066 stage e: one continuous climb from the ground to 25,000 units and back, in a quiet window.
# usage: run_ascent.sh <work folder> <config folder (a COPY with its settings.cfg)> <user data folder> [seconds]
# Writes <work>/stats.txt (a line per frame: tools/mapview_check/analyze.sh reads it), <work>/log.txt and the
# screenshots (the user data folder's screenshots). Windows paths for the folders (see CLAUDE.md pitfalls).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
work=$1; config=$2; user=$3; limit=${4:-150}
native() { cygpath -m "$1"; }
mkdir -p "$work"
cp "$config/openmw.cfg" "$work/openmw.cfg.orig"
run_cfg="$work/cfg"; rm -rf "$run_cfg"; mkdir -p "$run_cfg"; cp "$config"/* "$run_cfg"/
{ echo "data=\"$(native "$HERE/data")\""; echo "content=ascent.omwscripts"; } >> "$run_cfg/openmw.cfg"
printf 'tgm\ntcl\n' > "$work/start.txt"
rm -f "$user"/screenshots/*
cd "$(dirname "$(command -v openmw.exe || echo /i/Projects/OpenEden/build-jolt/openmw.exe)")"
OPENEDEN_FRAME_STATS_FILE="$(native "$work/stats.txt")" timeout "$limit" ./openmw.exe --replace=config \
    --config "$(native "$run_cfg")" --user-data "$(native "$user")" --skip-menu --start Balmora --no-grab \
    --script-run "$(native "$work/start.txt")" > "$work/log.txt" 2>&1 || true
mkdir -p "$work/frames"; cp "$user"/screenshots/* "$work/frames"/ 2>/dev/null || true
echo "done: $(wc -l < "$work/stats.txt") frames, $(ls "$work/frames" | wc -l) screenshots"
