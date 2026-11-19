#!/bin/bash
# loot.sh <from frame> <to frame> <input file>: pairs of mouse clicks on the loot window's Take all and Close buttons
# (tools/skydragonborn_check/ambush_input.txt: (497, 511) and (560, 511)) every 120 frames from <from> to <to>. A click with no
# window open hits the world and does nothing that matters; the pair that finds the window open takes everything and closes it.
f=$1; to=$2; IN=$3
while [ $f -lt $to ]; do
    printf '%s mouse 497 511\n%s down L 497 511\n%s up L 497 511\n' $f $((f + 2)) $((f + 4)) >> "$IN"
    printf '%s mouse 560 511\n%s down L 560 511\n%s up L 560 511\n' $((f + 40)) $((f + 42)) $((f + 44)) >> "$IN"
    f=$((f + 120))
done
