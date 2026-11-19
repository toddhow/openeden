#!/bin/bash
# tools/skymytime_check/scenarios.sh (task 0341): the four scenarios of docs/tasks/0341-skyrim-in-my-time-of-need.md.
#   scenarios.sh <s1|s2|s3|s4> <name> <build dir> <base config dir> <Skyrim Data folder> <work root under I:/Projects/OpenEden-work>
# s1 Saadia in the Bannered Mare: stage 10 (aid) to 20 to 25 by real clicks; s2 Dragonsreach: stage 25 (aid) to 100 by real clicks (prisoner, guard,
# prisoner); s3 Swindler's Den and the Mare: stage 100 (aid), Kematu killed (aid), stage 160 to 200 by real clicks; s4 the betrayal branch
# from stage 175: Saadia follows to the stables, the ambush scene (the Den is not visited, so Kematu does not appear).
HERE=$(cd "$(dirname "$0")" && pwd)
S=$1; NAME=$2; BUILD=$3; BASE=$4; DATA=$5; ROOT=$6
EX="$ROOT/$NAME.extra"
C="$HERE/click.sh"
export TALKTO=
case $S in
s1)
    { echo "7700 shot"; $C 7800 640 374 100; $C 8120 640 357 100; $C 8280 640 357 100; $C 8440 640 431 100; $C 8700 748 420 60; echo "9300 shot"; } > "$EX"
    export OPENEDEN_PACKAGE_TRACE=80802 WATCH="'80802'" EVERY=480 EXTRA_FILE="$EX"
    export SETTINGS_EXTRA="side quests = true\nstory test quest = 01CF25:10@3\n"
    export STEPS="{ 7500,'abs',-1300,-470,318,90,0 }, { 7600,'act','80802',0,0,0,100000 }" CLICKS="640,358 640,404"
    exec bash "$HERE/leg1.sh" "$BUILD" "$BASE" "$DATA" "$ROOT/$NAME" 9500 ;;
s2)
    { echo "3790 shot"; $C 3800 643 392 100; $C 3960 643 376 40; $C 4000 643 392 40; $C 4440 640 375 100; $C 4600 640 375 100; $C 4760 640 375 100; $C 4920 640 420 100; } > "$EX"
    export STEPS="{ 2400,'abs',-642,520,-312,0,0 }, { 3700,'act','364766',-300,400,-312,100000 }, { 4300,'act','136693',-300,400,-312,100000 }"
    export EVERY=480 CLICKS="640,375 592,412 642,398 762,420" EXTRA_FILE="$EX"
    exec bash "$HERE/leg2.sh" "$BUILD" "$BASE" "$DATA" "$ROOT/$NAME" 5700 ;;
s3)
    { echo "4780 shot"; $C 4800 640 366 100; $C 5000 643 483 100; } > "$EX"
    export TALKTO=1 SETTINGS_EXTRA="summons test kill = MS08Kematu@30\n" EVERY=480 CLICKS="s,0" EXTRA_FILE="$EX"
    export STEPS="{ 2300,'abs',4990,-1845,1672,90,0 }, { 4000,'cell','WhiterunBanneredMare',-256,-306,100,0,0 }, { 4700,'act','80802',0,0,0,100000 }"
    exec bash "$HERE/leg3.sh" "$BUILD" "$BASE" "$DATA" "$ROOT/$NAME" 5800 ;;
s4)
    export OPENEDEN_PACKAGE_TRACE=80802 WATCH="'80802','136705'" EVERY=480 STEP=170 CLICKS="640,358 640,383 640,375 643,442"
    export STEPS="{ 3800,'ext','skyrimse:w107119',4,-3,18300,-10800,-4600,90 }"
    exec bash "$HERE/leg4.sh" "$BUILD" "$BASE" "$DATA" "$ROOT/$NAME" 6500 ;;
esac
