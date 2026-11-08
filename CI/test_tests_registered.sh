#!/bin/bash
# Self-test of CI/check_tests_registered.sh: a scratch git repository with a test list; an unlisted file fails and is
# named, a file listed twice in one list fails, a path from another list (${CMAKE_SOURCE_DIR}/...) counts, a
# set_source_files_properties() name does not, and a listed exception is honoured.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
checker="$here/check_tests_registered.sh"
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
cd "$scratch" || exit 2
git init -q .
mkdir -p CI apps/components_tests/a apps/openmw_tests apps/opencs_tests
printf 'int a;\n' > apps/components_tests/a/testone.cpp
printf 'int b;\n' > apps/components_tests/a/testtwo.cpp
printf 'int c;\n' > apps/openmw_tests/testthree.cpp
printf 'file(GLOB S\n    a/testone.cpp\n    a/testtwo.cpp\n)\n' > apps/components_tests/CMakeLists.txt
printf 'file(GLOB S\n    testthree.cpp\n    ${CMAKE_SOURCE_DIR}/apps/components_tests/a/testone.cpp\n)\nset_source_files_properties(\n    testthree.cpp\n    PROPERTIES X Y)\n' > apps/openmw_tests/CMakeLists.txt
: > apps/opencs_tests/CMakeLists.txt
: > CI/tests_not_registered.txt
git add -A
failures=0
expect() { # expect <description> <want: 0 or 1 (nonzero)> <text wanted in output or "-"> 
    local what="$1" want="$2" text="$3" out status=0
    out="$("$checker" . 2>&1)" || status=$?
    if { [ "$want" = 0 ] && [ $status -ne 0 ]; } || { [ "$want" = 1 ] && [ $status -eq 0 ]; } ||
        { [ "$text" != "-" ] && ! grep -qF -- "$text" <<<"$out"; }; then
        echo "FAIL: $what (status $status): $out"
        failures=$((failures + 1))
    else
        echo "ok: $what"
    fi
}
expect "all registered passes" 0 "-"
printf 'int d;\n' > apps/components_tests/a/testnew.cpp
git add -A
expect "an unlisted file fails and is named" 1 "a/testnew.cpp is in no test CMakeLists.txt"
printf 'apps/components_tests/a/testnew.cpp not a test\n' > CI/tests_not_registered.txt
expect "a listed exception passes" 0 "-"
: > CI/tests_not_registered.txt
printf '    a/testnew.cpp\n    a/testnew.cpp\n)\n' > extra.txt
sed -i 's/^)$//' apps/components_tests/CMakeLists.txt
cat extra.txt >> apps/components_tests/CMakeLists.txt
rm extra.txt
expect "a file listed twice in one list fails" 1 "listed twice"
sed -i '0,/a\/testnew.cpp/{//d}' apps/components_tests/CMakeLists.txt
expect "listed once passes again" 0 "-"
[ $failures -eq 0 ] || exit 1
