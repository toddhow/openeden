#!/bin/bash
# sweep.sh <from frame> <reps> <input file> [shots]: appends to the input script a sweep of mouse clicks down the conversation
# box (reps times, a screenshot after each when shots=1), and then one sweep from the bottom up so that the lowest button (Goodbye)
# is hit first (as tools/skydawnguard_check/awaken.sh does). Prints the frame after the last click.
f=$1; reps=$2; IN=$3; shots=${4:-0}
for rep in $(seq 1 $reps); do
    y=330
    while [ $y -lt 520 ]; do
        printf '%s mouse 600 %s\n%s down L 600 %s\n%s up L 600 %s\n' $f $y $((f + 2)) $y $((f + 4)) $y >> "$IN"
        f=$((f + 14)); y=$((y + 14))
    done
    [ "$shots" = 1 ] && printf '%s shot\n' $f >> "$IN"
    f=$((f + 60))
done
y=520
while [ $y -gt 330 ]; do
    printf '%s mouse 643 %s\n%s down L 643 %s\n%s up L 643 %s\n' $f $y $((f + 2)) $y $((f + 4)) $y >> "$IN"
    f=$((f + 14)); y=$((y - 14))
done
echo $((f + 60))
