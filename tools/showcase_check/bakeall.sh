#!/bin/bash
# tools/showcase_check/bakeall.sh <build> <Skyrim Data> <work>: every bake the showcase shots use, one after the other
# (far land tiles, far object clusters, interior probes of the showcase interiors, the sky field round Whiterun).
H=$(cd "$(dirname "$0")" && pwd)
for step in "bake-terrain" "bake-objects" "bake-gi-interior WhiterunBanneredMare" "bake-gi-interior WhiterunDragonsreach" \
            "bake-gi-interior BleakFallsBarrow01" "bake-gi-interior WhiterunJorrvaskr" "bake-gi-exterior 2,-2:4,0"; do
    echo "=== $step $(date +%T)"
    bash "$H/check.sh" "$1" "$2" "$3" $step 2>&1 | tail -3
done
echo "=== all done $(date +%T)"
