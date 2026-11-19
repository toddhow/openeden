#!/bin/bash
# tools/solitude_check/scenarios.sh (task 0346): the scenarios of docs/tasks/0346-skyrim-solitude.md.
#   scenarios.sh <s1|s2|...> <name> <build dir> <base config dir> <Skyrim Data folder> <work root under I:/Projects/OpenEden-work>
# s1 the execution: the player is put in the gate's box in SolitudeWorld; the citizens, the guards and the executioner are brought
#    from their homes (a stand-in for their schedules).
HERE=$(cd "$(dirname "$0")" && pwd)
S=$1; NAME=$2; BUILD=$3; BASE=$4; DATA=$5; ROOT=$6
W=229087
# brings the crowd near the platform (base decimal ids; home interior names)
crowd() { # <first update>
    local u=$1 x=-64900 i=0
    for e in "SolitudeCastleDour,270264" "SolitudeJalasHouse,78431" "SolitudeJalasHouse,78476" "SolitudeAddvarsHouse,78421" "SolitudeAddvarsHouse,78505" \
        "SolitudeAddvarsHouse,78467" "SolitudeWinkingSkeever,78492" "SolitudeWinkingSkeever,78503" "SolitudeWinkingSkeever,78487" \
        "SolitudeTempleoftheDivines,78501" "SolitudeEvetteSansHouse,78451" "SolitudeRadiantRaiment,78507" "SolitudeBitsandPieces,78433" \
        "SolitudeAngelinesAromatics,78510"; do
        printf "{ %d,'bring','%s',%s,%d,%d,%d,-8160 }," $((u + i)) "${e%,*}" "${e#*,}" $W $((x + i * 70)) 104300
        i=$((i + 1))
    done
}
export LOADARG="--start @Tamriel,-66300,104500"
export OPENEDEN_SCENE_WAIT=1
case $S in
s1)
    export EVERY=600 RADIUS=900 NEAR=6
    export STEPS="{ 300,'world',$W,-65800,104652,'land',90 }, $(crowd 700) { 1000,'abs',-65237,104652,-8300,90,0,'free' }"
    export SETTINGS_EXTRA="summons = true
combat spell delivery = true
summons test kill = ${KILLWHO:-Roggvir}@${KILLAT:-85}
${SETTINGS_EXTRA:-}"
    exec bash "$HERE/golden.sh" "$BUILD" "$BASE" "$DATA" "$ROOT/$NAME" ${FRAMES:-4000} ;;
s2)
    # the throne room: the player stands in MS06StartIntroTrigger (a box in SolitudeBluePalace)
    export LOADARG="--start SolitudeBluePalace"
    export EVERY=600 RADIUS=1500 NEAR=10
    export STEPS="{ 600,'cell','SolitudeBluePalace',-1300,388,560,0,0 }, { 4000,'act','78442',-1300,388,560,3000 }, { 4600,'act','78452',-1300,388,560,3000 }"
    exec bash "$HERE/golden.sh" "$BUILD" "$BASE" "$DATA" "$ROOT/$NAME" ${FRAMES:-3000} ;;
s3)
    # Wolfskull Cave: stages 15 and 50 are set by the story aid (s2 reached them by real talk); the player is put in the cave
    export LOADARG="--start SolitudeBluePalace"
    export EVERY=600 RADIUS=1500 NEAR=10
    export SETTINGS_EXTRA="story test quest = 093807:5@3,093807:10@4,093807:15@5,093807:50@6
${SETTINGS_EXTRA:-}"
    export STEPS="{ 1500,'cell','WolfSkullCave02',-384,-2205,1000,0,0 }, { 3000,'cell','WolfSkullCave02',3085,658,60,0,0 }, { 4500,'cell','WolfSkullCave02',349,1169,1300,0,0 }, { 6000,'cell','WolfSkullCave02',2814,1546,1450,0,0 }"
    exec bash "$HERE/golden.sh" "$BUILD" "$BASE" "$DATA" "$ROOT/$NAME" ${FRAMES:-4000} ;;
s4)
    # the way to the cave: the player stands at Wolfskull Cave's map marker in Tamriel; DOORS lists the teleport doors around
    export LOADARG="--start @Tamriel,-97493,107386"
    export EVERY=300 RADIUS=900 NEAR=14 DOORS=1
    export SETTINGS_EXTRA="story test quest = 093807:5@3,093807:10@4,093807:15@5,093807:50@6
${SETTINGS_EXTRA:-}"
    export STEPS="${STEPS:-}"
    exec bash "$HERE/golden.sh" "$BUILD" "$BASE" "$DATA" "$ROOT/$NAME" ${FRAMES:-1500} ;;
esac
