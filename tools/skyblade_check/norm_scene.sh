#!/bin/bash
# scn.sh <tag> : normalised story lines of a run
grep -aE "SCENE 0[0-9A-F]+ (begins|ends|phase|line|package)|SCENEWAIT|PAPYRUS quest 00032926|PAPYRUS dialogue line|NTALK (turn|closed)|FORCEGREET|PKCHOSEN ref 78981.*behavior|SCENESPEECH sub" /i/Projects/OpenEden-work/papyrus/r349/$1/log.txt | sed -E 's/^\[[0-9:.]+ [A-Z*]\] //; s/ poll [0-9]+//; s/SCENESPEECH subtitle \(frame [0-9]+\)/SUB/' | awk '!s[$0]++' | cut -c1-220
