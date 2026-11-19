#!/bin/bash
# tools/skydawnguard_check/awaken.sh (task 0330): Dawnguard's "Awakening" (DLC1VQ01) inside Dimhollow Crypt, through the game's own
# quests, scenes, dialogue and Papyrus. A new game in the crypt's first cell (the quest is put at stage 10 by the developer aid,
# as fort.sh does); then, in game frames (60 a second, BTN = the frame of the button):
#   600  into the first cell; 900 in the box of stage 15 (the vampires' scene); 2400-2450 the door to the second cell (activated by
#   the harness, a stand-in for the key); the arrival box sets stage 17 (the puzzle room scene, Adalvald dies, stage 18);
#   BTN the button (stage 19; refused while the player fought lately, so the vampires are killed by the developer aid
#   `summons test kill` first), BTN+400.. the five braziers (the harness activates each, a stand-in for the key, standing on the
#   side of the data's markers), BTN+2850 the coffin (stages 20 and 30), BTN+3800 Serana's conversation (the harness opens it; real
#   mouse clicks on the box choose; her line sets stage 200), BTN+7000 the exit door to the wilderness.
#
#   awaken.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [frames]
# env: BTN (default 7700), SHOTS (0 drops the screenshots)
HERE=$(cd "$(dirname "$0")" && pwd)
BTN=${BTN:-7700}
WORK=$4
BUTTON=33570267 # 0x02003DDB the button's base
BRAZ=33570439 # 0x02003E87 the braziers' base (five placements at one place, told apart by their ids)
COFFIN=33617907 # 0x0200F7F3
SERANA=33565548 # 0x02002B6C
T() { echo $((BTN + $1)); }
b() { echo "{ $(T $1),'abs',$2,$3,$4,0,0 }, { $(T $(($1 + 50))),'act','$BRAZ',4280,1133,-1007,300,$5 }"; }
export LOADARG="--start DLC1DimHollowCrypt01" WATCH="'$SERANA'" EVERY=${EVERY:-600} RADIUS=${RADIUS:-1200} NEAR=${NEAR:-4}
export STEPS="{ 600,'cell','DLC1DimHollowCrypt01',-300,30,40,90,0 }, { 900,'abs',397,136,230,90,0 }, { 2400,'abs',-271,2900,-10,0,0 }, { 2450,'act','*',-271,3053,-10,120 }, { 2600,'abs',4255,1000,-1000,0,0 }, { $BTN,'abs',4255,1000,-1000,0,0 }, { $(T 100),'act','$BUTTON',4255,1107,-1017,200 }, $(b 400 4015 877 -885 1), $(b 700 4706 280 -915 2), $(b 1000 5219 1272 -915 3), $(b 1300 4339 1498 -925 4), $(b 1600 3443 1572 -915 5), { $(T 2850),'abs',4256,1000,-1090,0,0 }, { $(T 2900),'act','$COFFIN',4256,1109,-1103,300 }, { $(T 7000),'abs',9800,11600,1380,0,0 }, { $(T 7100),'act','*',9846,11653,1374,200 }"
export ACTIVATE="$(T 3800),'$SERANA'"
export SETTINGS_EXTRA="quest alias packages = true\nmap = true\nplayer level = 10\nrest = true\nactor cell moves = true\nservices = true\ndawnguard = true\ndungeon mechanics = true\ncombat ai = true\ncreatures = true\ncombat factions = true\nsummons = true\ncombat trace = true\nsummons test kill = DLC1LvlVampire*@92,LvlVampire*@92\nstory test delay = 1\nstory test quest = 0200352A:10\n"
# the real mouse path: a sweep of clicks down the conversation box, and screenshots of what it shows
IN="$WORK.input.txt"
: > "$IN"
if [ "${SHOTS:-1}" != 0 ]; then printf '%s shot\n%s shot\n%s shot\n' 5000 "$(T 150)" "$(T 3700)" >> "$IN"; fi
f=$(T 4000)
for rep in 1 2 3 4 5 6 7 8; do
    y=330
    while [ $y -lt 520 ]; do
        printf '%s mouse 600 %s\n%s down L 600 %s\n%s up L 600 %s\n' $f $y $((f + 2)) $y $((f + 4)) $y >> "$IN"
        f=$((f + 14)); y=$((y + 14))
    done
    [ "${SHOTS:-1}" != 0 ] && printf '%s shot\n' $f >> "$IN"
    f=$((f + 60))
done
# the way out: a last sweep from the bottom up, so the lowest button (Goodbye) is hit first and the conversation ends
for rep in 1; do
    y=520
    while [ $y -gt 330 ]; do
        printf '%s mouse 643 %s\n%s down L 643 %s\n%s up L 643 %s\n' $f $y $((f + 2)) $y $((f + 4)) $y >> "$IN"
        f=$((f + 14)); y=$((y - 14))
    done
    f=$((f + 60))
done
[ "${SHOTS:-1}" != 0 ] && printf '%s shot\n' "$(T 7200)" >> "$IN"
export INPUT="$IN"
exec bash "$HERE/check.sh" "$1" "$2" "$3" "$4" "${5:-$(T 8300)}"
