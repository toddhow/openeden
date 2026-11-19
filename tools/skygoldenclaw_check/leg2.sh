#!/bin/bash
# tools/skygoldenclaw_check/leg2.sh (task 0335): MS13 in Bleak Falls Barrow. A new game in BleakFallsBarrow01; the quest is put at
# stages 10, 15 and 35 by the developer aid (stand-ins for the talk with Lucan, the web and the cut), Arvel dies by the aid,
# the harness activates his body (stand-in for the crosshair) and real clicks take all (the claw: stage 40); then into
# BleakFallsBarrow02: the three rings and the keyhole (the claw: stage 50), and the word wall (stage 60).
#   leg2.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
HERE=$(cd "$(dirname "$0")" && pwd)
export CELLID=${CELLID:-BleakFallsBarrow01}
export WATCH="${WATCH:-'235078'}"
ARVEL=235078 # 0x039646
U0=${U0:-5800}
UT=${UT:-$((U0+3300))} # the way back to Riverwood (a stand-in for walking out of the barrow)
export STEPS="${STEPS:-{ 2000,'abs',3690,-3336,-1789,90,0 \}, { ${CELLU:-3200},'cell','BleakFallsBarrow02',128,80,-240,180,0 \}, { $((U0-150)),'abs',128,80,-240,180,0 \}, { $U0,'act','329045',128,-320,-256,200 \}, { $((U0+300)),'act','329045',128,-320,-256,200 \}, { $((U0+600)),'act','329044',128,-320,-256,200 \}, { $((U0+900)),'act','329044',128,-320,-256,200 \}, { $((U0+1000)),'abs',128,80,-240,180,0 \}, { $((U0+1200)),'act','329043',128,-320,-256,200 \}, { $((U0+1500)),'act','329043',128,-320,-256,200 \}, { $((U0+1850)),'abs',128,80,-240,180,0 \}, { $((U0+1920)),'act','320178',128,-320,-256,200 \}, { $((U0+2600)),'abs',-7782,71,500,-38.8,0 \}, { $UT,'cell','RiverwoodRiverwoodTrader',-104,-127,1,0,0 \}, { $((UT+1900)),'abs',-104,-127,1,0,0 \}, { $((UT+2100)),'act','78970',-104,-127,1,5000 \}}"
export ACTIVATE="${ACTIVATE:-2600,'$ARVEL'}"
export SETTINGS_EXTRA="golden claw = true\nscene park seconds = 5\nsummons = true\nshouts = true\nstory test quest = 039645:10@3,039645:15@5,039645:35@8\nsummons test kill = MS13Arvel@12\nstory test delay = 1\n${SETTINGS_EXTRA:-}"
IN="$4.input.txt"
: > "$IN"
printf '%s shot\n' ${SHOT1:-2300} >> "$IN"
bash "$HERE/loot.sh" ${LOOTFROM:-2700} ${LOOTTO:-4500} "$IN"
printf '%s key I down\n%s key I up\n%s shot\n%s key I down\n%s key I up\n%s key J down\n%s key J up\n%s shot\n%s key J down\n%s key J up\n' 4700 4702 4800 4900 4902 4950 4952 5000 5150 5152 >> "$IN"
END=$(bash "$HERE/sweep.sh" $((UT + 2200)) ${REPS:-4} "$IN" 1)
export INPUT="$IN"
exec bash "$HERE/golden.sh" "$1" "$2" "$3" "$4" "${5:-$END}"
