#!/bin/bash
# clicks.sh <input file> <first frame> <step> "x,y x,y ..." : a real click at each position, `step` frames apart, and a screenshot
# 40 frames after each click (the box shows the next turn). A position written as s,<n> is only a screenshot (no click).
IN=$1; f=$2; step=$3
for p in $4; do
    x=${p%,*}; y=${p#*,}
    if [ "$x" != "s" ]; then
        printf '%s mouse %s %s\n%s down L %s %s\n%s up L %s %s\n' $f $x $y $((f + 2)) $x $y $((f + 4)) $x $y >> "$IN"
    fi
    printf '%s shot\n' $((f + 40)) >> "$IN"
    f=$((f + step))
done
echo $f
