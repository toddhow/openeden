#!/bin/bash
# click.sh <frame> <x> <y> [shot offset]: prints the input script lines of one real click (and a screenshot offset frames later)
f=$1; x=$2; y=$3
printf '%s mouse %s %s\n%s down L %s %s\n%s up L %s %s\n' $f $x $y $((f+2)) $x $y $((f+4)) $x $y
[ -n "$4" ] && printf '%s shot\n' $((f + $4))
exit 0
