#!/bin/bash
# Linux quality gate in Docker (see README.md). One container at a time, --memory 12g, -j 8.
# Usage: tools/linux_gate/run.sh [stage ...]   stages: clone format build test lua   (default: all)
# Env: GATE_ROOT (work folder, default I:/Projects/OpenEden-work/linuxgate), GATE_REPO (repo to clone, default this checkout),
#      GATE_BRANCH (default main), BUILD_IMAGE, FORMAT_IMAGE, GATE_JOBS (default 8), GATE_MEM (default 12g).
set -uo pipefail
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'   # git-bash on Windows: do not rewrite /container/paths

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GATE_ROOT="${GATE_ROOT:-I:/Projects/OpenEden-work/linuxgate}"
GATE_REPO="${GATE_REPO:-$(cd "$HERE/../.." && pwd)}"
GATE_BRANCH="${GATE_BRANCH:-main}"
BUILD_IMAGE="${BUILD_IMAGE:-openeden-linux-noosg}"
FORMAT_IMAGE="${FORMAT_IMAGE:-openeden-cf14}"
JOBS="${GATE_JOBS:-8}"; [ "$JOBS" -gt 8 ] && JOBS=8
MEM="${GATE_MEM:-12g}"
SRC="$GATE_ROOT/src"; BUILD="$GATE_ROOT/build"; OUT="$GATE_ROOT/out"
mkdir -p "$OUT" "$BUILD" "$GATE_ROOT/ccache"
stages=("$@"); [ ${#stages[@]} -eq 0 ] && stages=(clone format build test lua)

dock() { # one container, removed afterwards, never in parallel
    docker run --rm --memory "$MEM" "$@"
}

for st in "${stages[@]}"; do
  echo "=== stage: $st ($(date +%T))"
  case "$st" in
  clone)
    if [ -d "$SRC/.git" ]; then git -C "$SRC" fetch -q origin "$GATE_BRANCH" && git -C "$SRC" -c core.autocrlf=false checkout -q --force FETCH_HEAD
    else git -c core.autocrlf=false clone --no-hardlinks -q -b "$GATE_BRANCH" "$GATE_REPO" "$SRC"; fi
    git -C "$SRC" config core.autocrlf false
    echo "commit: $(git -C "$SRC" rev-parse HEAD)" | tee "$OUT/commit.txt"
    if grep -q $'\r' "$SRC/CI/check_clang_format.sh"; then echo "ERROR: CRLF in checkout"; exit 2; fi ;;
  format)
    # CI/check_clang_format.sh needs git, which the format image lacks, so the file list comes from the host and the
    # same dry-run / diff logic runs in the image. Nothing is reformatted.
    git -C "$SRC" ls-files -- ':(exclude)extern/' '*.cpp' '*.hpp' '*.h' > "$OUT/format_files.txt"
    cat > "$OUT/format_inner.sh" <<'IN'
#!/bin/bash
cd /src
one() { f="$1"; if ! clang-format-14 --dry-run -Werror "$f" &>/dev/null; then
    d=$(clang-format-14 "$f" | diff -u "$f" -); echo "$d"
    echo "DIFFERS: $f +$(echo "$d" | grep -c "^+[^+]") -$(echo "$d" | grep -c "^-[^-]")"; fi; }
export -f one
xargs -a /out/format_files.txt -P 8 -I{} bash -c 'one "$@"' _ {}
IN
    dock -v "$SRC:/src:ro" -v "$OUT:/out" "$FORMAT_IMAGE" bash /out/format_inner.sh > "$OUT/format.log" 2>&1
    grep '^DIFFERS: ' "$OUT/format.log" | sort > "$OUT/format_differs.txt"
    echo "files checked: $(wc -l < "$OUT/format_files.txt"), differing: $(wc -l < "$OUT/format_differs.txt")"
    awk '/^DIFFERS:/{a+=substr($3,2);d+=substr($4,2)} END{print "changed lines: +" a " -" d}' "$OUT/format_differs.txt" ;;
  build|test|lua)
    cat > "$OUT/inner.sh" <<'IN'
#!/bin/bash
set -uo pipefail
stage=$1; JOBS=$2
export CCACHE_DIR=/ccache CCACHE_BASEDIR=/src
cd /build
case $stage in
build)
  cmake -S /src -B /build -G Ninja -D CMAKE_BUILD_TYPE=RelWithDebInfo -D OPENMW_USE_SYSTEM_RECASTNAVIGATION=ON \
    -D USE_SYSTEM_TINYXML=ON -D BUILD_COMPONENTS_TESTS=ON -D BUILD_OPENMW_TESTS=ON -D BUILD_OPENCS_TESTS=ON \
    -D "OPENMW_CXX_FLAGS=-Werror -Werror=implicit-fallthrough" -D CMAKE_C_COMPILER_LAUNCHER=ccache \
    -D CMAKE_CXX_COMPILER_LAUNCHER=ccache -D CMAKE_INSTALL_PREFIX=/build/install > /out/configure.log 2>&1 || { tail -30 /out/configure.log; exit 10; }
  cmake --build /build -- -k 0 -j "$JOBS" > /out/build.log 2>&1; rc=$?
  echo "build exit $rc"; tail -3 /out/build.log; exit $rc ;;
test)
  ex='*Slang*:*Complement*:*Property*'
  ./components-tests --gtest_filter="-$ex" > /out/components-tests.log 2>&1; echo "components-tests exit $?"
  ./openmw-tests > /out/openmw-tests.log 2>&1; echo "openmw-tests exit $?"
  ./eden-studio-tests > /out/eden-studio-tests.log 2>&1; echo "eden-studio-tests exit $?"
  [ -x ./edenarchive-tests ] && { ./edenarchive-tests > /out/edenarchive-tests.log 2>&1; echo "edenarchive-tests exit $?"; }
  grep -hE '^\[  (PASSED|FAILED)  \]' /out/*-tests.log | sort | uniq -c | sort -rn | head -60 ;;
lua)
  EX=/out/example-suite
  if [ ! -d $EX/.git ]; then
    rev=$(grep -m1 EXAMPLE_SUITE_REVISION /src/.gitlab-ci.yml | sed 's/.*"\(.*\)".*/\1/; s/.*: *//')
    mkdir -p $EX && cd $EX && git init -q && git remote add origin https://gitlab.com/OpenMW/example-suite.git &&
      git lfs install >/dev/null && git fetch -q --depth=1 origin "$rev" && git checkout -q FETCH_HEAD || { echo "example suite fetch failed"; exit 20; }
    cd /build
  fi
  python3 -c 'import numpy, matplotlib, termtables, click' 2>/dev/null || { apt-get update -qq && apt-get install -y -qq --no-install-recommends python3-pip libvulkan1 mesa-vulkan-drivers >/dev/null && pip3 install -q --break-system-packages numpy matplotlib termtables click; }
  xvfb-run --auto-servernum --server-args='-screen 0 640x480x24x60' /src/scripts/integration_tests.py --verbose --omw ./openmw --workdir /out/integration_tests_output $EX/ > /out/lua.log 2>&1
  echo "lua exit $?"; tail -15 /out/lua.log ;;
esac
IN
    dock -v "$SRC:/src:ro" -v "$BUILD:/build" -v "$OUT:/out" -v "$GATE_ROOT/ccache:/ccache" "$BUILD_IMAGE" bash /out/inner.sh "$st" "$JOBS" 2>&1 | tee "$OUT/$st.summary" ;;
  *) echo "unknown stage $st"; exit 1 ;;
  esac
done
