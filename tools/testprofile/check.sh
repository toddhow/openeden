#!/bin/bash
# Test games open off-screen, out of the taskbar and without taking focus (apps/openmw/vsgengine.cpp); run with OPENEDEN_QUIET_WINDOW=0 to see the window.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# Self-test of the living test profile (tools/testprofile).
#
# usage: tools/testprofile/check.sh                                  the file checks (a second or two, no game)
#        tools/testprofile/check.sh --game <your config folder> [--no-bake] [--build DIR]
#                                    also prepares the profile "main" from your config folder (READ ONLY: the script
#                                    compares the folder's file names, sizes and times before and after), starts the game
#                                    in an exterior and in two interiors, off-screen, and checks the logs.
#
# File checks, each a pass/fail line:
#   1. coverage.sh passes (every feature-like setting is in the profile or on the exclusion list; no typos)
#   2. prepare.sh --no-bake with a made-up source config into the scratch profile "_selftest": the merged settings.cfg has
#      every key of profile.cfg and the variant exactly once with the right value, and your own other keys survive
#   3. the profile's openmw.cfg has no config= or user-data= line, names data-local inside the profile and the extension
#   4. every path in the start scripts and in the generated cfg lies in the build folder or inside the profile
#   5. prepare.sh refuses a profile name that would leave I:/Projects/OpenEden-work, an unknown variant and a source
#      folder inside the profile
# Game checks (--game): no "not initialized" line, no refused-setting warning, a screenshot was saved, exit code 0, no
# crash or freeze dump. Other warnings and errors are listed for you to read; they do not fail the run.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
workRoot="I:/Projects/OpenEden-work"
native() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
FAILED=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; FAILED=1; }

gameSource=""; prepArgs=(); build="I:/Projects/OpenEden/build-jolt"
while [ $# -gt 0 ]; do
    case "$1" in
        --game) shift; gameSource=${1:?--game needs your config folder} ;;
        --no-bake) prepArgs+=(--no-bake) ;;
        --build) shift; build=$1; prepArgs+=(--build "$1") ;;
        *) echo "unknown argument $1"; exit 2 ;;
    esac
    shift
done

# ---------- 1. coverage ----------
if bash "$HERE/coverage.sh" > /tmp/testprofile-coverage.out 2>&1; then pass "coverage: $(tail -1 /tmp/testprofile-coverage.out)"
else fail "coverage"; cat /tmp/testprofile-coverage.out; fi

# ---------- 2-4. a made-up source config ----------
src=$(mktemp -d)
trap 'rm -rf "$src"' EXIT
printf 'data="I:\\\\nowhere"\ncontent=Morrowind.esm\nconfig=C:/real/folder\nuser-data=C:/real/folder\nresources=C:/old\ndata-local=C:/real/data\n' > "$src/openmw.cfg"
printf '[Rendering]\nhdr = false\nmyown = 1\n\n[Game]\ncreativemotion animation = false\nmykey = 2\n[Rendering]\ngi = false\n[Saves]\ncharacter = Test\n' > "$src/settings.cfg"
selftest="$workRoot/testprofile/_selftest"
if bash "$HERE/prepare.sh" "$src" --no-bake --name _selftest msaa "${prepArgs[@]}" > /tmp/testprofile-prepare.out 2>&1; then pass "prepare.sh --no-bake ran"
else fail "prepare.sh --no-bake failed"; cat /tmp/testprofile-prepare.out; fi

out="$selftest/config/settings.cfg"
want=$(awk -f "$HERE/cfgkeys.awk" "$HERE/profile.cfg" "$HERE/variants/msaa.cfg" |
    awk -F'\t' '{ id = $1 SUBSEP $2; if (!(id in v)) o[++n] = id; v[id] = $3; s[id] = $1; k[id] = $2 }
        END { for (i = 1; i <= n; ++i) printf "%s\t%s\t%s\n", s[o[i]], k[o[i]], v[o[i]] }')
have=$(awk -f "$HERE/cfgkeys.awk" "$out")
bad=$(awk -F'\t' 'FNR == NR { w[$1 SUBSEP $2] = $3; next }
    { id = $1 SUBSEP $2; n[id]++; val[id] = $3 }
    END { for (id in w) { split(id, q, SUBSEP)
            if (n[id] != 1) print "  [" q[1] "] " q[2] " appears " n[id] + 0 " times"
            else if (val[id] != w[id]) print "  [" q[1] "] " q[2] " = " val[id] " (wanted " w[id] ")" }
          for (id in n) if (n[id] > 1) { split(id, q, SUBSEP); print "  [" q[1] "] " q[2] " is written " n[id] " times" } }' \
    <(echo "$want") <(echo "$have") | sort -u)
if [ -z "$bad" ]; then pass "merged settings.cfg: every profile/variant key exactly once with its value ($(echo "$want" | grep -c .) keys)"
else fail "merged settings.cfg:"; echo "$bad"; fi
if echo "$have" | grep -qP '^Rendering\tmyown\t1$' && echo "$have" | grep -qP '^Game\tmykey\t2$' && echo "$have" | grep -qP '^Saves\tcharacter\tTest$'; then
    pass "the source config's other keys survive"
else fail "a key of the source config was lost"; fi
if echo "$have" | grep -qP '^Game\tcreativemotion animation\ttrue$' && echo "$have" | grep -qP '^Rendering\ttaa\tfalse$'; then
    pass "profile values replace the source's (creativemotion animation true, msaa variant's taa false)"
else fail "a profile value did not replace the source's"; fi

cfg="$selftest/config/openmw.cfg"
if ! grep -qiE '^(config|user-data|resources=C|data-local=C)' "$cfg" && grep -qF "data-local=\"$selftest/data\"" "$cfg" \
    && grep -q '^extension=compat_morrowind' "$cfg" && grep -qF "resources=$build/resources" "$cfg"; then
    pass "openmw.cfg: no config= or user-data=, data-local inside the profile, the build's resources, the extension"
else fail "openmw.cfg is wrong:"; cat "$cfg"; fi

# every Windows or Git Bash path in the generated files is in the build folder, the profile, or (data= lines) the source's own
leaks=$( (grep -hoE '[A-Za-z]:[\\/][^" ]*' "$selftest"/start_*.bat "$selftest"/start_*.sh
        grep -v '^data=' "$cfg" | grep -oE '[A-Za-z]:[\\/][^" ]*') | tr '\\' '/' |
    grep -viE "^$(echo "$build" | sed 's/[.[\*^$]/\\&/g')|^$(echo "$selftest" | sed 's/[.[\*^$]/\\&/g')" | sort -u)
if [ -z "$leaks" ]; then pass "all generated paths are in the build folder or inside the profile"
else fail "paths outside the build folder and the profile:"; echo "$leaks" | sed 's/^/  /'; fi

# ---------- 5. refusals ----------
refused() { # description, expected text, args...
    local what=$1 text=$2; shift 2
    local msg; msg=$(bash "$HERE/prepare.sh" "$@" 2>&1); local rc=$?
    if [ $rc -ne 0 ] && echo "$msg" | grep -qi "$text"; then pass "refused: $what"
    else fail "not refused ($what): rc=$rc $msg" ; fi
}
refused "a name that leaves the work folder" "letters, digits" "$src" --no-bake --name ../../outside
refused "an unknown variant" "no variant" "$src" --no-bake --name _selftest nosuchvariant
refused "a source inside the profile" "inside the profile" "$selftest/config" --no-bake --name _selftest
foreign="$workRoot/testprofile/_selftest_foreign"
mkdir -p "$foreign" && echo x > "$foreign/someone-elses-file.txt"
refused "a profile folder that is not ours" "not made by this script" "$src" --no-bake --name _selftest_foreign
rm -rf "$foreign"

# ---------- the game ----------
if [ -n "$gameSource" ]; then
    real=$(native "$gameSource")
    snapshot() { find "$real" -printf '%P\t%s\t%T@\n' 2>/dev/null | sort; }
    snap1=$(snapshot)
    echo "--- preparing the profile 'main' from $real (read only) ---"
    if bash "$HERE/prepare.sh" "$real" --name main "${prepArgs[@]}"; then pass "prepare.sh for the real config"
    else fail "prepare.sh for the real config"; fi
    profile="$workRoot/testprofile/main"
    cfgN="$profile/config"; userN="$profile/user"

    runScene() { # label, start cell, console script text
        local label=$1 cell=$2 script=$3
        local dir="$profile/check_$label"
        rm -rf "$dir"; mkdir -p "$dir"
        printf '%b' "$script" > "$dir/start.txt"
        rm -rf "$userN/screenshots"; rm -f "$cfgN"/*.dmp "$userN"/*.dmp "$cfgN/openmw.log"
        (cd "$build" && timeout 900 ./openmw.exe --replace=config --config "$cfgN" --user-data "$userN" \
            --skip-menu --start "$cell" --script-run "$dir/start.txt" --no-grab --auto-screenshot 400 --auto-quit 430 \
            > "$dir/stdout.log" 2>&1)
        local rc=$?
        cp "$cfgN/openmw.log" "$dir/openmw.log" 2>/dev/null
        cp -r "$userN/screenshots" "$dir/" 2>/dev/null
        local log="$dir/openmw.log"
        local problems=""
        grep -qi 'not initialized' "$log" && problems="$problems not-initialized"
        grep -qE '\[[0-9:.]+ W\].*(needs |is off|are off|refus|using (off|medium)|not supported|ignored)' "$log" && problems="$problems refused-setting"
        ls "$cfgN"/*.dmp "$userN"/*.dmp >/dev/null 2>&1 && problems="$problems dump"
        [ $rc -ne 0 ] && problems="$problems exit-$rc"
        [ -z "$(ls -A "$dir/screenshots" 2>/dev/null)" ] && problems="$problems no-screenshot"
        if [ -z "$problems" ]; then pass "$label: exit 0, no dump, a screenshot, no refused setting"
        else fail "$label:$problems (see $dir)"; fi
        echo "--- warnings and errors in the $label log ---"
        grep -E '\[[0-9:.]+ [WE]\]' "$log" | sed 's/^/  /' | sort | uniq -c | sort -rn | head -40
    }
    clear='ChangeWeather "Bitter Coast Region" 0\nset gamehour to 15\nset timescale to 0\n'
    runScene exterior "Seyda Neen" "$clear"
    runScene interior "Seyda Neen, Arrille's Tradehouse" 'set timescale to 0\n'
    runScene interior_gi "Caldera, Guild of Mages" 'set timescale to 0\n'

    # The features that fall back silently (a missing bake or a refused look reads as "off" and logs nothing at start):
    # each needs a positive line in the log of the scene that uses it.
    expectLog() { # scene, extended regex, what it proves
        if grep -qE "$2" "$profile/check_$1/openmw.log"; then pass "$1 log: $3"
        else fail "$1 log has no line for: $3 (/$2/)"; fi
    }
    expectLog exterior 'GI: sky-visibility field for [1-9][0-9]* exterior cell' "gi exterior: the baked sky visibility was loaded"
    expectLog exterior 'World LOD terrain tiles: [1-9][0-9]* tiles' "world LOD: the terrain tile pack was adopted"
    expectLog exterior 'LOD clusters: [1-9][0-9]* clusters in' "world LOD: the object cluster pack was opened"
    expectLog exterior 'Shadows: point lights, [1-9]' "point light shadows are on"
    expectLog exterior 'TAA: temporal anti-aliasing' "taa is on"
    expectLog exterior 'AO: temporal accumulation' "ao temporal is on"
    expectLog exterior 'Volumetrics: a froxel volume' "volumetric fog is on"
    expectLog exterior 'Volumetric clouds: marching' "volumetric clouds are on"
    expectLog exterior 'Using the Slang permutations of' "the Slang shaders are used"
    if grep 'draws the merged body' "$profile/check_exterior/openmw.log" | grep -qv 'Merged bodies: player draws'; then
        pass "exterior log: NPCs draw the merged body"
    else fail "exterior log: no NPC draws the merged body (the cache is stale or the body is refused)"; fi
    expectLog interior 'GI: [1-9][0-9]* probe\(s\) for Seyda Neen, Arrille' "gi: Arrille's Tradehouse got its light probes"
    expectLog interior_gi 'GI: [1-9][0-9]* probe\(s\) for Caldera, Guild of Mages' "gi: Caldera got its light probes"

    snap2=$(snapshot)
    if [ "$snap1" = "$snap2" ]; then pass "your config folder is unchanged (names, sizes, times): $real"
    else fail "your config folder changed: $(diff <(echo "$snap1") <(echo "$snap2") | head -5)"; fi
fi

[ $FAILED = 0 ] && echo "TESTPROFILE CHECK PASSED" || echo "TESTPROFILE CHECK FAILED"
exit $FAILED
