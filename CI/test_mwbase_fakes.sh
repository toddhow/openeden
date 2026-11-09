#!/bin/bash
# Self-test of CI/check_mwbase_fakes.sh (task 0085 move 2): in a scratch copy, the check passes on the committed doubles,
# fails when a committed double is edited by hand, and fails when an interface gains a pure virtual function nobody
# regenerated.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/.." && pwd)"
checker="$here/check_mwbase_fakes.sh"

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch/tools/mwbase_fakes" "$scratch/apps/openmw/mwbase" "$scratch/apps/openmw_tests/mwmechanics/fakes"
cp "$repo/tools/mwbase_fakes/gen.py" "$scratch/tools/mwbase_fakes/"
for h in world windowmanager mechanicsmanager luamanager; do cp "$repo/apps/openmw/mwbase/$h.hpp" "$scratch/apps/openmw/mwbase/"; done
cp "$repo"/apps/openmw_tests/mwmechanics/fakes/*.inc "$scratch/apps/openmw_tests/mwmechanics/fakes/"

failures=0
expect() { # expect <description> <exit status wanted: 0 or nonzero> <command...>
    local what="$1" want="$2"
    shift 2
    local out status=0
    out="$("$@" 2>&1)" || status=$?
    if { [ "$want" = 0 ] && [ "$status" = 0 ]; } || { [ "$want" != 0 ] && [ "$status" != 0 ]; }; then
        echo "ok   $what"
    else
        echo "FAIL $what (exit $status)"
        echo "$out" | sed 's/^/     /'
        failures=$((failures + 1))
    fi
}

expect "the committed doubles match their interfaces" 0 "$checker" "$scratch"

fake="$scratch/apps/openmw_tests/mwmechanics/fakes/luamanager.inc"
cp "$fake" "$fake.keep"
sed -i '0,/contentFilesLoaded/s/contentFilesLoaded/contentFilesLoadedX/' "$fake"
expect "a double edited by hand fails" nonzero "$checker" "$scratch"
mv "$fake.keep" "$fake"

header="$scratch/apps/openmw/mwbase/luamanager.hpp"
cp "$header" "$header.keep"
sed -i 's/virtual ~LuaManager() = default;/virtual ~LuaManager() = default;\n        virtual void aNewPureFunction(int value) = 0;/' "$header"
expect "an interface that gained a pure virtual function fails until the double is regenerated" nonzero "$checker" "$scratch"
mv "$header.keep" "$header"

expect "restoring both passes again" 0 "$checker" "$scratch"

if [ "$failures" != 0 ]; then
    echo "test_mwbase_fakes: $failures check(s) failed"
    exit 1
fi
echo "test_mwbase_fakes: all checks passed"
