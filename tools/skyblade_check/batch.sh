#!/bin/bash
# batch349.sh intro|mound <tag>: three parallel runs
cd /i/Projects/OpenEden-work/papyrus
for i in 1 2 3; do
 if [ "$1" = intro ]; then
  SETTINGS_EXTRA=$'story test quest = 032926:5,032926:10@6\n' STEPS="{ 1900,'abs',-738,-700,100,0,0 },{ 2750,'abs',-800,760,30,0,0 }" ACTIVATE="2800, '78968'" ACTIVATE2="4000, '78968'" INPUT=I:/Projects/OpenEden-work/papyrus/in349_i.txt timeout 1700 bash run349.sh $2$i 15200 >/dev/null 2>&1 &
 else
  LOADARG="--start @Tamriel,26008,-32200" SETTINGS_EXTRA=$'story test quest = 032926:5,032926:10,032926:30,032926:32,032926:35,032926:50@25\n' STEPS="{ 3000,'abs',135130,9904,-11900,0,0 },{ 4300,'abs',145900,12400,-6950,0,0 }" INPUT=I:/Projects/OpenEden-work/papyrus/in349_k.txt timeout 1700 bash run349.sh $2$i 12500 >/dev/null 2>&1 &
 fi
done
wait
for i in 1 2 3; do bash norm349.sh $2$i | md5sum | cut -c1-12; bash norm349.sh $2$i | wc -l; done
