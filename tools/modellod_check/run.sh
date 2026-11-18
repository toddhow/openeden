#!/usr/bin/env bash
# Task 0062 stage a: one measurement run for the model LOD gate.
# usage: run.sh <build dir> <base config dir with openmw.cfg> <out dir> <pose> <width> <height> <shadows 0|1> [extra [Rendering] lines]
# poses: ground (Balmora start, 7168), ground15 (15000 with distant terrain and object paging), air (3000 up, 7168),
#        vivec (a teleport to Vivec, collision off, 900 up, 15000 with distant), crowd (Ebonheart council chambers).
# Writes <out>/frames.txt (frame stats with the ctr_ columns), run.log and one screenshot.
set -u
export OPENEDEN_QUIET_WINDOW=1
BUILD="$1"; BASE="$2"; OUT="$3"; POSE="$4"; W="$5"; H="$6"; SH="$7"; EXTRA="${8:-}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BD="$HERE/../benchmark_data"
export PATH="/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin/Release:/i/Projects/OpenEden/deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/bin:$PATH"
native() { cygpath -m "$1"; }
rm -rf "$OUT"; mkdir -p "$OUT/config" "$OUT/user"
grep -vE '^(resources|user-data|config)=|build-jolt|dialogue_check|dlgcheck' "$BASE/openmw.cfg" | tr -d '\r' > "$OUT/config/openmw.cfg"
echo "resources=$(native "$BUILD")/resources" >> "$OUT/config/openmw.cfg"
echo "data=\"$(native "$BUILD")/resources/vfs-mw\"" >> "$OUT/config/openmw.cfg"
start=Balmora; script=""; vd=7168; distant=0
case "$POSE" in
  ground) ;;
  ground15) vd=15000; distant=1 ;;
  air) script=balmora_view.txt ;;
  vivec) vd=15000; distant=1; script=vivec_view.txt ;;
  crowd) start="Ebonheart, Grand Council Chambers" ;;
esac
{
  echo "[Video]"; echo "resolution x = $W"; echo "resolution y = $H"; echo "fullscreen = false"; echo "window mode = 2"
  echo "vsync mode = 0"; echo "framerate limit = 0"
  echo "[Sound]"; echo "master volume = 0.0"
  echo "[Rendering]"; echo "gpu timing = true"; echo "frame counters = true"
  [[ -n "$EXTRA" ]] && echo -e "$EXTRA"
  echo "[Shadows]"
  if [[ "$SH" == 1 ]]; then
    echo "enable shadows = true"; echo "object shadows = true"; echo "actor shadows = true"; echo "player shadows = true"
    echo "terrain shadows = true"; echo "enable indoor shadows = true"
  else echo "enable shadows = false"; fi
  [[ $distant == 1 ]] && { echo "[Terrain]"; echo "distant terrain = true"; echo "object paging = true"; }
  echo "[Camera]"; echo "viewing distance = $vd"
} > "$OUT/config/settings.cfg"
sargs=()
[[ -n "$script" ]] && sargs=(--script-run "$(native "$BD/$script")")
cd "$BUILD" && OPENEDEN_FRAME_STATS_FILE="$(native "$OUT/frames.txt")" OPENEDEN_FIXED_FRAME_TIME=0.0166667 timeout 300 ./openmw.exe \
  --replace=config --config "$(native "$OUT/config")" --user-data "$(native "$OUT/user")" --skip-menu --start "$start" \
  --random-seed 7 --no-grab ${sargs[@]+"${sargs[@]}"} --auto-screenshot 700 --auto-quit 900 > "$OUT/run.log" 2>&1 < /dev/null
echo "exit $?"
