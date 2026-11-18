#!/bin/bash
# tools/skydb_check (task 0294): the whole DB01 "Innocence Lost" run. usage db01.sh <name> [frames]; see in8.txt in the task doc.
D=I:/Projects/OpenEden-work/ragdoll5/db
export PLAN="{ 3000, 'talk' }, { 5000, 'cell', 'RiftenHonorhallOrphanage', -384, 640, 72, 0 }, { 7000, 'cell', 'WindhelmAretinoResidence', -244, -2767, 371, 0 }, { 7300, 'talk' }"
export SETTINGS_EXTRA="story trace = true\nsummons = true\nsummons test kill = Grelod\ncombat trace = true\n"
bash "$(dirname "$0")/run.sh" $1 ${INPUT:-$D/in8.txt} ${2:-9600}
