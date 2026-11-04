#!/usr/bin/env bash
# Test games open off-screen, out of the taskbar and without taking focus (apps/openmw/vsgengine.cpp); run with OPENEDEN_QUIET_WINDOW=0 to see the window.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# tools/render_compare.sh
# -----------------------
# Multi-scene screenshot capture and A/B comparison rig.
# Works on Linux and Git Bash on Windows.
#
# Usage:
#   ./tools/render_compare.sh capture <openmw_bin> <scenes_file> <output_dir> [base_config_dir]
#   ./tools/render_compare.sh compare <dir1> <dir2> [threshold_mae]
#   ./tools/render_compare.sh benchmark <openmw_bin> <scenes_file> <stats_output_dir>

set -euo pipefail

MODE="${1:-}"

if [[ -z "$MODE" ]]; then
    echo "Usage:"
    echo "  $0 capture <openmw_bin> <scenes_file> <output_dir> [base_config_dir]"
    echo "  $0 compare <dir1> <dir2> [threshold_mae]"
    echo "  $0 benchmark <openmw_bin> <scenes_file> <stats_output_dir>"
    exit 1
fi

setup_isolated_env() {
    local base_cfg="${1:-}"
    local work_dir="$2"

    local cfg_dir="$work_dir/config"
    local data_dir="$work_dir/userdata"
    local shot_dir="$data_dir/screenshots"

    rm -rf "$work_dir"
    mkdir -p "$cfg_dir" "$data_dir" "$shot_dir"

    # Seed openmw.cfg if base configuration provided or standard locations exist
    if [[ -n "$base_cfg" && -f "$base_cfg/openmw.cfg" ]]; then
        cp "$base_cfg/openmw.cfg" "$cfg_dir/openmw.cfg"
    elif [[ -f "$HOME/.config/openmw/openmw.cfg" ]]; then
        cp "$HOME/.config/openmw/openmw.cfg" "$cfg_dir/openmw.cfg"
    elif [[ -n "${USERPROFILE:-}" && -f "$USERPROFILE/Documents/My Games/OpenMW/openmw.cfg" ]]; then
        cp "$USERPROFILE/Documents/My Games/OpenMW/openmw.cfg" "$cfg_dir/openmw.cfg"
    fi

    # Fixed resolution 1280x720 and windowed mode
    cat << 'EOF' > "$cfg_dir/settings.cfg"
[Video]
resolution x = 1280
resolution y = 720
fullscreen = false
window border = false
framerate limit = 0
vsync = false

[Shadows]
enable shadows = false
EOF
}

capture_scenes() {
    local bin="$1"
    local scenes_file="$2"
    local out_dir="$3"
    local base_cfg="${4:-}"

    mkdir -p "$out_dir"
    local tmp_work
    tmp_work=$(mktemp -d -t openmw_render_XXXXXX 2>/dev/null || mktemp -d)

    echo "=== Running screenshot capture with binary: $bin ==="
    echo "Scenes file: $scenes_file"
    echo "Output directory: $out_dir"

    while IFS='|' read -r scene_name scene_type scene_target shot_frame quit_frame desc || [[ -n "$scene_name" ]]; do
        # Skip empty lines and comments
        [[ -z "$scene_name" || "$scene_name" =~ ^# ]] && continue
        # The heavy_ scenes are for tools/benchmark.sh (they need its options and run long).
        [[ "$scene_name" == heavy_* ]] && continue

        echo "--- Capturing scene: $scene_name ($desc) ---"
        setup_isolated_env "$base_cfg" "$tmp_work"

        local cfg_dir="$tmp_work/config"
        local user_data="$tmp_work/userdata"
        local shot_dir="$user_data/screenshots"

        local target_args=()
        if [[ "$scene_type" == "start" ]]; then
            target_args=(--skip-menu --start "$scene_target")
        elif [[ "$scene_type" == "save" ]]; then
            target_args=(--skip-menu --load-savegame "$scene_target")
        fi

        local log_file="$tmp_work/run_${scene_name}.log"
        local shot_num="${shot_frame:-120}"
        local quit_num="${quit_frame:-150}"

        "$bin" \
            --config "$cfg_dir" \
            --user-data "$user_data" \
            --replace=config \
            "${target_args[@]}" \
            --auto-screenshot="$shot_num" \
            --auto-quit="$quit_num" \
            > "$log_file" 2>&1 &
        local run_pid=$!

        # 30 second hard timeout
        ( sleep 30 && kill -TERM "$run_pid" 2>/dev/null && sleep 2 && kill -KILL "$run_pid" 2>/dev/null ) &
        local watchdog_pid=$!

        wait "$run_pid" 2>/dev/null || true
        kill "$watchdog_pid" 2>/dev/null || true

        local latest_shot
        latest_shot=$(ls -t "$shot_dir"/*.png 2>/dev/null | head -n 1 || true)
        if [[ -n "$latest_shot" && -f "$latest_shot" ]]; then
            cp "$latest_shot" "$out_dir/${scene_name}.png"
            echo "  Saved: $out_dir/${scene_name}.png"
        else
            echo "  WARNING: No screenshot produced for $scene_name (see $log_file)"
        fi
    done < "$scenes_file"

    rm -rf "$tmp_work"
    echo "=== Capture complete ==="
}

compare_dirs() {
    local dir1="$1"
    local dir2="$2"
    local threshold="${3:-2.0}"

    echo "=== Comparing render outputs ==="
    echo "Reference A: $dir1"
    echo "Reference B: $dir2"
    echo "Threshold MAE: $threshold"

    local python_bin="python3"
    if command -v python3 >/dev/null 2>&1 && "$python_bin" -c "import sys" >/dev/null 2>&1; then
        python_bin="python3"
    elif command -v python >/dev/null 2>&1 && python -c "import sys" >/dev/null 2>&1; then
        python_bin="python"
    elif [[ -f "/c/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe" ]]; then
        python_bin="/c/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe"
    fi

    "$python_bin" -c "
import sys, os
from PIL import Image
import numpy as np

dir1 = sys.argv[1]
dir2 = sys.argv[2]
threshold = float(sys.argv[3])

files1 = {f for f in os.listdir(dir1) if f.endswith('.png')}
files2 = {f for f in os.listdir(dir2) if f.endswith('.png')}
common = sorted(list(files1.intersection(files2)))

if not common:
    print('No common PNG files found to compare.')
    sys.exit(1)

all_passed = True
print(f'Comparing {len(common)} scenes:')
for f in common:
    p1 = os.path.join(dir1, f)
    p2 = os.path.join(dir2, f)
    img1 = np.array(Image.open(p1).convert('RGB'), dtype=np.float32)
    img2 = np.array(Image.open(p2).convert('RGB'), dtype=np.float32)
    if img1.shape != img2.shape:
        print(f'FAIL: {f} dimension mismatch: {img1.shape} vs {img2.shape}')
        all_passed = False
        continue
    diff = np.abs(img1 - img2)
    mae = np.mean(diff)
    max_diff = np.max(diff)
    status = 'PASS' if mae <= threshold else 'DIFF'
    if status == 'DIFF':
        all_passed = False
    print(f'  [{status}] {f:30s} MAE={mae:6.2f} / 255  MaxChannelDiff={max_diff:.0f}')

if not all_passed:
    print('Render differences exceed threshold.')
    sys.exit(2)
else:
    print('All scenes match within threshold.')
" "$dir1" "$dir2" "$threshold"
}

if [[ "$MODE" == "capture" ]]; then
    capture_scenes "${2:?missing binary}" "${3:?missing scenes file}" "${4:?missing output dir}" "${5:-}"
elif [[ "$MODE" == "compare" ]]; then
    compare_dirs "${2:?missing dir1}" "${3:?missing dir2}" "${4:-2.0}"
elif [[ "$MODE" == "benchmark" ]]; then
    echo "Benchmark mode: collecting stats per scene"
    # Runs scenes with OPENMW_OSG_STATS_FILE enabled
fi
