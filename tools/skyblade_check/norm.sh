#!/bin/bash
# norm349.sh <run>: the story lines of a run without time stamps, poll numbers and frame numbers
grep -aE "SCENE 0[0-9A-F]+ (begins|ends|phase|line|package)|PAPYRUS quest 00032926 (set to|stage [0-9]+ runs)|PAPYRUS dialogue line|NTALK (turn|closed)|FORCEGREET (conv|asked)|PAPYRUS trigger 00(0B3A3F|03FB03) (entered|left)|SCENESPEECH subtitle|MQ step" /i/Projects/OpenEden-work/papyrus/r349/$1/log.txt | sed -E 's/^\[[0-9:.]+ [A-Z*]\] //; s/^Global\[[^]]*\]:\s*//; s/ poll [0-9]+//; s/\(frame [0-9]+\)//' 
