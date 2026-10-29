#!/usr/bin/env bash
# tools/verify_no_osg_deps.sh - Verify built openmw executable has NO OSG or OpenThreads dependencies.
set -euo pipefail

BINARY="${1:-}"

if [[ -z "$BINARY" ]]; then
    for candidate in "build-jolt/openmw" "build/openmw" "bin/openmw" "./openmw" "build-jolt/openmw.exe" "build/openmw.exe"; do
        if [[ -f "$candidate" ]]; then
            BINARY="$candidate"
            break
        fi
    done
fi

if [[ -z "$BINARY" || ! -f "$BINARY" ]]; then
    echo "Error: Binary not found: '$BINARY'" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY_SCRIPT="$SCRIPT_DIR/verify_no_osg_deps.py"

PYTHON_BIN="python3"
if ! command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN="python"
fi

if [[ -f "$PY_SCRIPT" ]]; then
    exec "$PYTHON_BIN" "$PY_SCRIPT" "$BINARY"
fi

# Fallback: check directly with readelf / objdump / ldd
echo "Checking dependencies for: $BINARY"
deps=""
if command -v readelf >/dev/null 2>&1; then
    deps=$(readelf -d "$BINARY" | grep NEEDED | sed -E 's/.*\[(.*)\].*/\1/')
elif command -v objdump >/dev/null 2>&1; then
    deps=$(objdump -p "$BINARY" | grep NEEDED | awk '{print $2}')
elif command -v ldd >/dev/null 2>&1; then
    deps=$(ldd "$BINARY" | awk '{print $1}')
fi

violations=$(echo "$deps" | grep -Ei '^(lib)?(osg|openthreads)' || true)

if [[ -n "$violations" ]]; then
    echo "FAIL: Forbidden dependencies detected:" >&2
    echo "$violations" >&2
    exit 1
fi

echo "PASS: No OSG or OpenThreads dependencies found in $BINARY."
exit 0
