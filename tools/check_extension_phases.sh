#!/bin/bash
# Checks the frame phases a test extension recorded in a real game run (task 0015), because the phase calls live in the
# engine loop, which no unit test runs.
#
# Usage: tools/check_extension_phases.sh <file written through OPENEDEN_TEST_EXTENSION_LOG>
#
# Within a frame the phases must follow the documented order, each at most once. Simulation phases come as a group,
# and at least one running frame must have them.
# Lifecycle lines must come in order, around all the phases.

set -e

awk '
function fail(message) { print "FAIL line " NR ": " message ": " $0; failed = 1; exit 1 }
BEGIN {
    rank["Input"] = 1; rank["PreSimulation"] = 2; rank["PostSimulation"] = 3
    rank["Animation"] = 4; rank["PreRender"] = 5; rank["Ui"] = 6
}
$2 == "register" { if (stage != "") fail("register out of order"); stage = "register"; next }
$2 == "resolve" { if (stage != "register") fail("resolve out of order"); stage = "resolve"; next }
$2 == "start" { if (stage != "resolve") fail("start out of order"); stage = "start"; next }
$2 == "stop" { if (stage != "start") fail("stop out of order"); stage = "stop"; next }
$2 == "destroy" { if (stage != "stop") fail("destroy out of order"); stage = "destroy"; next }
$2 == "phase" {
    if (stage != "start") fail("phase outside start and stop")
    r = rank[$3]
    if (!r) fail("unknown phase")
    # A new frame starts at Input. A frame cut short by a game state change can end early, so any phase may be the
    # last one seen, but within a frame the order only moves forward.
    if ($3 == "Input") { last = 1; inputs++; next }
    if (r <= last) fail("phase out of order")
    if (r == 3 && last != 2) fail("PostSimulation without PreSimulation")
    if (r == 4 && last != 3) fail("Animation without PostSimulation")
    if (r == 2) simulated++
    if (r == 6) uis++
    last = r
    next
}
END {
    if (failed) exit 1
    if (stage != "destroy") { print "FAIL: run ended at stage \"" stage "\""; exit 1 }
    if (!inputs || !uis) { print "FAIL: no frames recorded"; exit 1 }
    if (!simulated) { print "FAIL: no running frame reached the simulation phases"; exit 1 }
    print "OK: " inputs " frames, " simulated " simulated, " uis " reached Ui"
}
' "$1"
