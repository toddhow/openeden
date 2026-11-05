#!/bin/bash
# Self-test of CI/check_layer_rules.sh (task 0081): builds a tiny scratch git repository, runs the checker on it with
# the real layer map (the real exceptions list removed) and checks that a forbidden include fails and names the file,
# the line and the rule, that removing it passes, that a listed exception is honoured, and that a stale one fails.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
checker="$here/check_layer_rules.sh"
realRules="$here/layer_rules.txt"

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
cd "$scratch"
git init -q .
git config core.autocrlf false
mkdir -p components/misc components/esm3 components/nif components/vfs apps/openmw/mwworld
printf '#pragma once\n' > components/esm3/loadnpc.hpp
printf '#pragma once\n' > components/vfs/pathutil.hpp
printf '#pragma once\n' > apps/openmw/mwworld/ptr.hpp
printf '#pragma once\n#include <string>\n' > components/misc/clean.hpp
printf '#pragma once\n#include <components/misc/clean.hpp>\n' > components/nif/clean.hpp
mkdir -p CI
grep -v '^except ' "$realRules" > CI/layer_rules.txt
git add -A

failures=0
expect() { # expect <description> <exit status wanted: 0 or nonzero> <output text wanted or ""> <command...>
    local what="$1" want="$2" text="$3"
    shift 3
    local out status=0
    out="$("$@" 2>&1)" || status=$?
    local ok=1
    if [ "$want" = 0 ] && [ "$status" != 0 ]; then ok=0; fi
    if [ "$want" != 0 ] && [ "$status" = 0 ]; then ok=0; fi
    if [ -n "$text" ] && ! grep -qF -- "$text" <<<"$out"; then ok=0; fi
    if [ "$ok" = 1 ]; then echo "ok   $what"; else echo "FAIL $what (exit $status)"; echo "$out" | sed 's/^/     /'; failures=$((failures + 1)); fi
}

expect "a clean tree passes" 0 "0 exceptions" "$checker"

printf '#pragma once\n// a comment\n#include <components/esm3/loadnpc.hpp>\n' > components/misc/bad.hpp
git add -A
expect "a base file including esm3 fails, naming file, line and rule" nonzero \
    "components/misc/bad.hpp:3: layer base may not include layer formats: components/esm3/loadnpc.hpp" "$checker"

printf '#pragma once\n#include <apps/openmw/mwworld/ptr.hpp>\n' > components/nif/bad.hpp
git add -A
expect "a formats file including mwworld fails (components may not include apps)" nonzero \
    "components/nif/bad.hpp:2: deny components/ -> apps/: apps/openmw/mwworld/ptr.hpp" "$checker"

printf '#pragma once\n#include "../vfs/pathutil.hpp"\n' > components/nif/relative.hpp
git add -A
expect "a relative include is resolved (formats including vfs)" nonzero \
    "components/nif/relative.hpp:2: layer formats may not include layer engine: components/vfs/pathutil.hpp" "$checker"

git rm -q -f --cached components/misc/bad.hpp components/nif/bad.hpp components/nif/relative.hpp
rm components/misc/bad.hpp components/nif/bad.hpp components/nif/relative.hpp
expect "removing the violations passes" 0 "" "$checker"

printf '#pragma once\n// #include <components/esm3/loadnpc.hpp>\n/* #include <components/esm3/loadnpc.hpp> */\nconst char* s = "#include <components/esm3/loadnpc.hpp>";\n' > components/misc/commented.hpp
git add -A
expect "an include in a comment or a string does not count" 0 "" "$checker"
git rm -q -f --cached components/misc/commented.hpp
rm components/misc/commented.hpp

printf '#pragma once\n#include <components/esm3/loadnpc.hpp>\n' > components/misc/listed.hpp
git add -A
echo 'except components/misc/listed.hpp -> components/esm3/loadnpc.hpp  # test' >> CI/layer_rules.txt
expect "a listed exception passes and is counted" 0 "1 exceptions (1 include lines)" "$checker"

git rm -q -f --cached components/misc/listed.hpp
rm components/misc/listed.hpp
expect "an exception that no longer occurs fails as stale" nonzero \
    "stale exception, no longer occurs or no longer a violation: components/misc/listed.hpp -> components/esm3/loadnpc.hpp" "$checker"

mkdir components/brandnew
printf '#pragma once\n' > components/brandnew/x.hpp
git add -A
sed -i '/^except /d' CI/layer_rules.txt
expect "a directory no dir line names fails" nonzero "components/brandnew: not in" "$checker"

if [ "$failures" != 0 ]; then
    echo "test_layer_rules: $failures check(s) failed"
    exit 1
fi
echo "test_layer_rules: all checks passed"
