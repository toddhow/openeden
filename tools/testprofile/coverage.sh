#!/bin/bash
# Keeps the living test profile alive (root CLAUDE.md, "Agent workflow"): lists every feature-like setting of
# files/settings-default.cfg that is neither in profile.cfg or a variant nor on the exclusion list, and exits 1 if there
# is one. Also fails on a profile or variant line naming a setting that does not exist in files/settings-default.cfg (a
# typo would otherwise be silently ignored by the game).
#
# usage: tools/testprofile/coverage.sh
#
# Checked: every key under [Rendering], [WorldLod], [Bodies] and [Shadows]; in [Game] the keys of CreativeMotion, inertial
# locomotion, sheathing and the M9 animation features; [Cells] incremental cell build; [Physics] creativemotion ragdolls.
# exclude.txt lines are "Section|key|reason"; the key may use * as a glob, for example "Rendering|*debug*|reason".
# (One awk program does all the matching: starting a process per setting takes minutes on Windows.)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DEFAULTS="$ROOT/files/settings-default.cfg"

awk -f "$HERE/cfgkeys.awk" "$DEFAULTS" > "${TMPDIR:-/tmp}/testprofile-all.$$"
awk -f "$HERE/cfgkeys.awk" "$HERE/profile.cfg" "$HERE"/variants/*.cfg > "${TMPDIR:-/tmp}/testprofile-profile.$$"
trap 'rm -f "${TMPDIR:-/tmp}/testprofile-all.$$" "${TMPDIR:-/tmp}/testprofile-profile.$$"' EXIT

# Files in order: the settings table, the profile's keys, the exclusion lines.
awk -F'\t' -v exclude="$HERE/exclude.txt" '
    function checked(sec, key) {
        if (sec == "Rendering" || sec == "WorldLod" || sec == "Bodies" || sec == "Shadows") return 1
        if (sec == "Game")
            return key ~ /creativemotion|inertial|sheath/ || key == "foot planting" || key == "foot lock" || key == "look at" \
                || key == "smoother turns" || key == "procedural lean" || key == "lean scale" \
                || key == "actor collision shape type" || key == "enable authored transitions" \
                || key ~ /^extension (debug draw|profiler)$/ || key == "neutral world"
        if (sec == "Cells") return key ~ /incremental/
        if (sec == "GUI") return key == "rmlui"
        if (sec == "Physics") return key ~ /creativemotion/
        return 0
    }
    function globToRegex(g,    r) {
        r = g; gsub(/[.]/, "[.]", r); gsub(/[*]/, ".*", r); return "^" r "$"
    }
    BEGIN {
        while ((getline line < exclude) > 0) {
            sub(/\r$/, "", line)
            if (line ~ /^[ \t]*#/ || line ~ /^[ \t]*$/) continue
            n = split(line, p, "|")
            if (n < 3 || p[3] ~ /^[ \t]*$/) { print "FAIL: exclude.txt line without a reason: " line; bad = 1; continue }
            ++nex; exSec[nex] = p[1]; exKey[nex] = globToRegex(p[2]); exText[nex] = p[1] "|" p[2]
        }
    }
    FILENAME ~ /testprofile-all/ { known[$1 SUBSEP $2] = 1; order[++nall] = $1 SUBSEP $2; next }
    { inProfile[$1 SUBSEP $2] = 1; profileOrder[++nprof] = $1 SUBSEP $2 }
    END {
        for (i = 1; i <= nprof; ++i) if (!(profileOrder[i] in known)) {
            split(profileOrder[i], q, SUBSEP); print "FAIL: profile.cfg or a variant names a setting that is not in files/settings-default.cfg: [" q[1] "] " q[2]; bad = 1 }
        for (i = 1; i <= nall; ++i) {
            id = order[i]; split(id, q, SUBSEP)
            if (seen[id]++ || !checked(q[1], q[2])) continue
            ++total
            if (id in inProfile) continue
            hit = 0
            for (e = 1; e <= nex; ++e) if (exSec[e] == q[1] && q[2] ~ exKey[e]) { hit = 1; used[e] = 1 }
            if (!hit) { missing = missing "  [" q[1] "] " q[2] "\n"; bad = 1 }
        }
        if (missing != "") {
            print "FAIL: settings that are neither in tools/testprofile/profile.cfg (or a variant) nor in exclude.txt:"
            printf "%s", missing
            print "Add each to the profile with a comment and a README line, or to exclude.txt with a reason."
        }
        for (e = 1; e <= nex; ++e) if (!used[e]) print "note: exclude.txt entry matches no unlisted setting now (stale or in the profile): " exText[e]
        if (!bad) printf "COVERAGE OK: %d feature-like settings, each in the profile, a variant or the exclusion list.\n", total
        exit bad
    }' "${TMPDIR:-/tmp}/testprofile-all.$$" "${TMPDIR:-/tmp}/testprofile-profile.$$"
