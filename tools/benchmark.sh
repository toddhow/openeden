#!/usr/bin/env bash
# tools/benchmark.sh (task 0023, F0b)
# ----------------------------------
# Runs the heavy scenes of tools/render_scenes.txt (the ones whose name starts with "heavy_") with GPU timing on, and
# prints a table of frame time and the GPU time of every render pass: median and 95th percentile over the last frames of the run.
# Works on Linux and in Git Bash on Windows. It needs a game install that its openmw.cfg points at.
#
# Usage:
#   tools/benchmark.sh [openmw_bin] [scenes_file] [output_dir]
#     openmw_bin   default build-jolt/openmw.exe (or build/openmw on Linux)
#     scenes_file  default tools/render_scenes.txt
#     output_dir   default benchmark-out (frame stats files, logs and one screenshot per scene go here)
# Environment:
#   OPENMW_CFG_DIR   a folder with the openmw.cfg to start from (default: the user's, as tools/render_compare.sh)
#   BENCH_FRAMES     how many of the last frames of each run to measure (default 400); the start of a run is loading screens,
#                    whose frames would make every median look tiny
#   BENCH_FILTER     only scenes whose name contains this text
#   BENCH_TIMEOUT    seconds before a scene's run is stopped (default 240)
#   BENCH_RENDERING  extra lines for the [Rendering] section of settings.cfg, as printf %b text (task 0026)
#
# A scene line is  name|start|cell|shot_frame|quit_frame|description|options  with options a comma-separated list of
#   viewdist=<units>   [Camera] viewing distance
#   distant            distant terrain and object paging on
#   walk               add tools/benchmark_data, whose Lua player script runs the player forward and turns now and then
#   look               add tools/benchmark_data, whose Lua player script holds the camera still and looking at the sky
#   script=<file>      a console script in tools/benchmark_data to run at the start (--script-run), to place the camera
# render_compare.sh reads the same file and skips the heavy_ scenes.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

BIN="${1:-}"
if [[ -z "$BIN" ]]; then
    if [[ -x "$ROOT/build-jolt/openmw.exe" ]]; then BIN="$ROOT/build-jolt/openmw.exe"; else BIN="$ROOT/build/openmw"; fi
fi
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
SCENES="${2:-$HERE/render_scenes.txt}"
OUT="${3:-$ROOT/benchmark-out}"
FRAMES="${BENCH_FRAMES:-400}"
TIMEOUT="${BENCH_TIMEOUT:-240}"

# The game is a Windows program in Git Bash and wants Windows paths in its arguments and config.
native() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }

find_cfg() {
    if [[ -n "${OPENMW_CFG_DIR:-}" && -f "$OPENMW_CFG_DIR/openmw.cfg" ]]; then echo "$OPENMW_CFG_DIR/openmw.cfg"; return; fi
    for c in "$HOME/.config/openmw/openmw.cfg" "${USERPROFILE:-/nonexistent}/Documents/My Games/OpenMW/openmw.cfg" \
        "${USERPROFILE:-/nonexistent}/OneDrive/Documents/My Games/OpenMW/openmw.cfg"; do
        [[ -f "$c" ]] && { echo "$c"; return; }
    done
}
CFG="$(find_cfg)"
if [[ -z "$CFG" ]]; then echo "No openmw.cfg found; set OPENMW_CFG_DIR" >&2; exit 1; fi
[[ -x "$BIN" ]] || { echo "Not an executable: $BIN" >&2; exit 1; }

# median, 95th percentile (nearest rank) and count of column $2 of stats file $1, over the last frames. -1 means a
# pass had no result for that frame and is left out.
column_stats() {
    tail -n "$FRAMES" "$1" | awk -v c="$2" 'NF >= c { print $c }' | grep -v '^-1$' | sort -g | awk '
        { a[NR] = $1 }
        END {
            if (NR == 0) { print "-", "-", 0; exit }
            m = (NR % 2) ? a[(NR + 1) / 2] : (a[NR / 2] + a[NR / 2 + 1]) / 2
            r = 0.95 * NR; ri = int(r); if (ri < r) ri++; if (ri < 1) ri = 1
            printf "%.3f %.3f %d\n", m, a[ri], NR
        }'
}

mkdir -p "$OUT"
printf '%-26s %-34s %9s %9s %7s\n' scene metric median_ms p95_ms frames
printf '%s\n' "----------------------------------------------------------------------------------------------"

while IFS='|' read -r name type target shot quit desc options || [[ -n "${name:-}" ]]; do
    [[ -z "${name:-}" || "$name" == \#* ]] && continue
    [[ "$name" == heavy_* ]] || continue
    [[ -n "${BENCH_FILTER:-}" && "$name" != *"$BENCH_FILTER"* ]] && continue
    options="${options%$'\r'}"

    work="$OUT/work-$name"
    rm -rf "$work"; mkdir -p "$work/config" "$work/userdata/screenshots"
    tr -d '\r' < "$CFG" > "$work/config/openmw.cfg"

    {
        echo "[Video]"; echo "resolution x = 1280"; echo "resolution y = 720"; echo "fullscreen = false"
        echo "window mode = 2"; echo "vsync mode = 0"; echo "framerate limit = 0"
        echo "[Rendering]"; echo "gpu timing = true"
        # Extra [Rendering] lines (task 0026): BENCH_RENDERING with backslash-n between lines.
        [[ -n "${BENCH_RENDERING:-}" ]] && echo -e "$BENCH_RENDERING"
        echo "[Shadows]"; echo "enable shadows = false"
        [[ ",$options," == *",distant,"* ]] && { echo "[Terrain]"; echo "distant terrain = true"; echo "object paging = true"; }
        if [[ "$options" =~ viewdist=([0-9.]+) ]]; then echo "[Camera]"; echo "viewing distance = ${BASH_REMATCH[1]}"; fi
    } > "$work/config/settings.cfg"

    if [[ ",$options," == *",walk,"* ]]; then
        { echo "data=\"$(native "$HERE/benchmark_data")\""; echo "content=bench.omwscripts"; } >> "$work/config/openmw.cfg"
    fi

    # Task 0042: a static camera that looks at the sky (volumetric clouds).
    if [[ ",$options," == *",look,"* ]]; then
        { echo "data=\"$(native "$HERE/benchmark_data")\""; echo "content=look.omwscripts"; } >> "$work/config/openmw.cfg"
    fi

    script_args=()
    if [[ "$options" =~ script=([A-Za-z0-9_.-]+) ]]; then script_args=(--script-run "$(native "$HERE/benchmark_data/${BASH_REMATCH[1]}")"); fi
    stats="$work/frames.txt"
    log="$work/run.log"
    (
        cd "$(dirname "$BIN")" && OPENEDEN_FRAME_STATS_FILE="$(native "$stats")" timeout "$TIMEOUT" "$BIN" \
            --replace=config --config "$(native "$work/config")" --user-data "$(native "$work/userdata")" \
            --skip-menu --start "$target" --no-grab ${script_args[@]+"${script_args[@]}"} --auto-screenshot "${shot:-300}" --auto-quit "${quit:-600}" > "$log" 2>&1 < /dev/null
    )
    rc=$?
    shotfile="$(ls -t "$work/userdata/screenshots" 2>/dev/null | head -1)"
    [[ -n "$shotfile" ]] && cp "$work/userdata/screenshots/$shotfile" "$OUT/$name.png"

    if ls "$work"/userdata/*.dmp >/dev/null 2>&1 || [[ ! -s "$stats" ]]; then
        printf '%-26s %s\n' "$name" "FAILED (exit $rc; see $log)"
        continue
    fi

    # Columns: 1 frame, 2 wall_ms, 3 wait_ms, then gpu_frame and one per pass (names from the header).
    header=($(head -1 "$stats"))
    for i in "${!header[@]}"; do
        col=$((i + 1))
        metric="${header[$i]}"
        case "$metric" in frame|gpu_frame) continue ;; esac
        read -r med p95 n < <(column_stats "$stats" "$col")
        printf '%-26s %-34s %9s %9s %7s\n' "$name" "$metric" "$med" "$p95" "$n"
    done
done < "$SCENES"
