#!/bin/bash
# Every .cpp under a unit test folder (apps/components_tests, apps/openmw_tests, apps/opencs_tests) must be named
# exactly once in one of the test CMakeLists.txt files. The lists are written out file by file (no directory glob), so
# a file missing from a list is silently never built or run, and a list that lost a line in a merge looks fine.
# A file one test executable builds from another folder is named there as ${CMAKE_SOURCE_DIR}/apps/... and counts.
# Names inside set_source_files_properties(...) are properties, not registrations, and are not counted.
# Exceptions go in CI/tests_not_registered.txt (one path per line, then a reason after a space).
# Run from the repository root; a path to another root may be given as the first argument (the self-test uses a copy).
set -u
root="${1:-.}"
cd "$root" || exit 2
dirs="apps/components_tests apps/openmw_tests apps/opencs_tests"
status=0

# Prints one registered path (relative to the repository root) per line, with the repeats.
registered() {
    for d in "$@"; do
        [ -f "$d/CMakeLists.txt" ] || continue
        sed -e 's/#.*$//' "$d/CMakeLists.txt" | tr -d '\r' | awk -v dir="$d" '
            /set_source_files_properties[[:space:]]*\(/ { skip = 1 }
            {
                if (!skip) {
                    for (i = 1; i <= NF; ++i) {
                        t = $i
                        gsub(/[()"]/, "", t)
                        if (t !~ /\.cpp$/) continue
                        sub(/^\$\{CMAKE_SOURCE_DIR\}\//, "", t)
                        if (t !~ /^apps\//) {
                            sub(/^\$\{CMAKE_CURRENT_SOURCE_DIR\}\//, "", t)
                            t = dir "/" t
                        }
                        print t
                    }
                }
                if (skip && $0 ~ /\)/) skip = 0
            }'
    done
}

# A file may be built by two executables (a shared fixture), but not named twice in one list.
dups="$(for d in $dirs; do registered "$d" | sort | uniq -d; done)"
regs="$(for d in $dirs; do registered "$d" | sort -u; done)"
for d in $dirs; do
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        if grep -qE "^$(printf '%s' "$f" | tr -c 'A-Za-z0-9_/\n' '.')( |$)" CI/tests_not_registered.txt 2>/dev/null; then
            continue
        fi
        n="$(printf '%s\n' "$regs" | grep -cxF -- "$f")"
        if printf '%s\n' "$dups" | grep -qxF -- "$f"; then
            echo "check_tests_registered: $f is listed twice in one test CMakeLists.txt"
            status=1
        fi
        if [ "$n" -eq 0 ]; then
            echo "check_tests_registered: $f is in no test CMakeLists.txt (it is never built)"
            status=1
        fi
    done < <(git ls-files -c -o --exclude-standard -- "$d" | grep -E '\.cpp$' | sort -u)
done
[ $status -eq 0 ] && echo "check_tests_registered: ok"
exit $status
